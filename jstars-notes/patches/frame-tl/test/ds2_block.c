/* DRAW_SAMPLE=1 / GAME_SAMPLE=1 (diagnostic, off by default): a stack sampler
 * for the drawing thread and/or the game's main thread (the one polling the
 * flip status, func_006507DC, which also flushes the FIFO). Every 0.5-1.5 ms
 * (at random, so it cannot lock onto the game's own 1 ms timer waits) one
 * sampler thread suspends each sampled thread, walks its stack and resumes it,
 * then counts the sample by: the module the thread was in (exe, ntdll, an
 * ntdll wait stub, D3D12, NVIDIA, other), whether it ran since the previous
 * sample (thread cycle time moved), and the first two exe frames on the stack
 * -- so time in ntdll or the driver is charged to the exe code that called it.
 * Every DRAW_SAMPLE_SEC seconds (default 10) the counts are appended to
 * DRAW_SAMPLE_OUT (default draw_samples.txt) / GAME_SAMPLE_OUT (default
 * game_samples.txt) as exe RVAs; tools\ds_sym.py names them.
 * The walk runs while the thread is suspended, so it takes no locks: unwind
 * tables are found from a module list made before (and refreshed between)
 * windows, RtlVirtualUnwind only reads memory, and the walk never reads
 * outside the thread's own stack. Costs each sampled thread a suspend/resume
 * per sample (~1 ms of every 25 on the drawing thread, jstars-notes §36). */
#include <psapi.h>
typedef struct { u64 base, end; const RUNTIME_FUNCTION* rf; u32 nrf; int cls; } DsMod;
enum { DS_EXE, DS_NTDLL, DS_WAIT, DS_D3D12, DS_NV, DS_OTHER, DS_NCLS };
static const char* const s_ds_cls_name[DS_NCLS] = { "exe", "ntdll", "ntwait", "d3d12", "nvidia", "other" };
/* ntdll wait stubs: a sample whose top is in one is "ntwait" (blocked), not kernel work */
static u64 s_ds_wait[8];
static DsMod s_ds_mod[256];
static int   s_ds_nmod = 0;
typedef struct { u32 key_cls, e1, e2, n; } DsEnt;   /* key_cls: cls | ran << 4 */
#define DS_HASH 16384u
typedef struct {
    HANDLE th; u64 stack_lo, stack_hi; FILE* f; ULONG64 last_cyc;
    DsEnt tab[DS_HASH]; u32 total, lost;
} DsInst;
enum { DS_DRAW, DS_GAME, DS_NINST };
static DsInst* s_ds_inst[DS_NINST];
static volatile LONG s_ds_running = 0;

static void ds_load_modules(void)
{
    HMODULE hm[256]; DWORD need = 0;
    HANDLE p = GetCurrentProcess();
    if (!K32EnumProcessModules(p, hm, sizeof hm, &need)) return;
    int n = (int)(need / sizeof(HMODULE)); if (n > 256) n = 256;
    const HMODULE exe = GetModuleHandleA(NULL), nt = GetModuleHandleA("ntdll.dll");
    int k = 0;
    for (int i = 0; i < n; i++) {
        MODULEINFO mi;
        if (!K32GetModuleInformation(p, hm[i], &mi, sizeof mi)) continue;
        const IMAGE_DOS_HEADER* d = (const IMAGE_DOS_HEADER*)hm[i];
        const IMAGE_NT_HEADERS64* nh = (const IMAGE_NT_HEADERS64*)((const u8*)hm[i] + d->e_lfanew);
        const IMAGE_DATA_DIRECTORY* dd = &nh->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        char name[MAX_PATH] = { 0 };
        K32GetModuleBaseNameA(p, hm[i], name, sizeof name);
        DsMod* m = &s_ds_mod[k++];
        m->base = (u64)hm[i]; m->end = m->base + mi.SizeOfImage;
        m->rf   = dd->Size ? (const RUNTIME_FUNCTION*)((const u8*)hm[i] + dd->VirtualAddress) : NULL;
        m->nrf  = dd->Size / (u32)sizeof(RUNTIME_FUNCTION);
        m->cls  = hm[i] == exe ? DS_EXE : hm[i] == nt ? DS_NTDLL
                : !_strnicmp(name, "d3d12", 5) ? DS_D3D12
                : !_strnicmp(name, "nv", 2) ? DS_NV : DS_OTHER;
    }
    s_ds_nmod = k;
    static const char* const wn[8] = { "NtWaitForSingleObject", "NtWaitForMultipleObjects",
        "NtDelayExecution", "NtWaitForAlertByThreadId", "NtRemoveIoCompletion",
        "NtSignalAndWaitForSingleObject", "NtWaitForWorkViaWorkerFactory", "NtYieldExecution" };
    for (int i = 0; i < 8; i++) s_ds_wait[i] = nt ? (u64)GetProcAddress(nt, wn[i]) : 0;
}

static const DsMod* ds_mod_of(u64 pc)
{
    for (int i = 0; i < s_ds_nmod; i++)
        if (pc >= s_ds_mod[i].base && pc < s_ds_mod[i].end) return &s_ds_mod[i];
    return NULL;
}

static const RUNTIME_FUNCTION* ds_lookup(const DsMod* m, u64 pc)
{
    if (!m->rf) return NULL;
    const u32 rva = (u32)(pc - m->base);
    u32 lo = 0, hi = m->nrf;
    while (lo < hi) {
        const u32 mid = (lo + hi) / 2;
        if (rva < m->rf[mid].BeginAddress) hi = mid;
        else if (rva >= m->rf[mid].EndAddress) lo = mid + 1;
        else return &m->rf[mid];
    }
    return NULL;
}

static void ds_count(DsInst* in, u32 key_cls, u32 e1, u32 e2)
{
    u32 h = (key_cls * 0x9E3779B1u) ^ (e1 * 0x85EBCA77u) ^ (e2 * 0xC2B2AE3Du);
    for (u32 i = 0; i < 64; i++) {
        DsEnt* e = &in->tab[(h + i) & (DS_HASH - 1)];
        if (e->n && (e->key_cls != key_cls || e->e1 != e1 || e->e2 != e2)) continue;
        e->key_cls = key_cls; e->e1 = e1; e->e2 = e2; e->n++;
        return;
    }
    in->lost++;
}

static int ds_cmp(const void* a, const void* b)
{
    const u32 x = ((const DsEnt*)a)->n, y = ((const DsEnt*)b)->n;
    return x < y ? 1 : x > y ? -1 : 0;
}

static void ds_flush(DsInst* in, int win, double secs)
{
    static DsEnt srt[DS_HASH];
    u32 m = 0, by[DS_NCLS][2] = { { 0 } };
    for (u32 i = 0; i < DS_HASH; i++)
        if (in->tab[i].n) {
            srt[m++] = in->tab[i];
            by[in->tab[i].key_cls & 15][in->tab[i].key_cls >> 4] += in->tab[i].n;
        }
    qsort(srt, m, sizeof srt[0], ds_cmp);
    fprintf(in->f, "[DS] window %d: %u samples in %.1f s (%u lost)", win, in->total, secs, in->lost);
    for (int c = 0; c < DS_NCLS; c++)
        fprintf(in->f, " | %s ran %u idle %u", s_ds_cls_name[c], by[c][1], by[c][0]);
    fputc(10, in->f);
    for (u32 i = 0; i < m && i < 400; i++)
        fprintf(in->f, "%u %s %s 0x%X 0x%X%c", srt[i].n, s_ds_cls_name[srt[i].key_cls & 15],
                (srt[i].key_cls >> 4) ? "ran" : "idle", srt[i].e1, srt[i].e2, 10);
    fflush(in->f);
    memset(in->tab, 0, sizeof in->tab);
    in->total = in->lost = 0;
}

static void ds_sample(DsInst* in)
{
    if (SuspendThread(in->th) == (DWORD)-1) return;
    CONTEXT c; memset(&c, 0, sizeof c);
    c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    u32 cls = DS_OTHER, e1 = 0, e2 = 0;
    if (GetThreadContext(in->th, &c)) {
        const DsMod* m0 = ds_mod_of(c.Rip);
        cls = m0 ? (u32)m0->cls : DS_OTHER;
        if (cls == DS_NTDLL)
            for (int i = 0; i < 8; i++)
                if (s_ds_wait[i] && c.Rip >= s_ds_wait[i] && c.Rip < s_ds_wait[i] + 32) cls = DS_WAIT;
        for (int d = 0; d < 32 && c.Rip && !e2; d++) {
            const DsMod* m = d ? ds_mod_of(c.Rip) : m0;
            if (!m) break;                              /* unknown code: stop */
            if (m->cls == DS_EXE) {
                if (!e1) e1 = (u32)(c.Rip - m->base); else e2 = (u32)(c.Rip - m->base);
            }
            if (c.Rsp < in->stack_lo || c.Rsp + 8 > in->stack_hi) break;
            const RUNTIME_FUNCTION* rf = ds_lookup(m, c.Rip);
            if (!rf) { c.Rip = *(const u64*)c.Rsp; c.Rsp += 8; }   /* leaf */
            else {
                void* hd = NULL; DWORD64 ef = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, m->base, c.Rip, (PRUNTIME_FUNCTION)rf,
                                 &c, &hd, &ef, NULL);
            }
            /* return addresses point after the call: -1 keeps them inside it */
            if (c.Rip) c.Rip -= 1;
        }
    }
    ResumeThread(in->th);
    ULONG64 cyc = 0; QueryThreadCycleTime(in->th, &cyc);
    const u32 ran = cyc != in->last_cyc; in->last_cyc = cyc;
    ds_count(in, cls | (ran << 4), e1, e2);
    in->total++;
}

static DWORD WINAPI ds_thread(LPVOID arg)
{
    (void)arg;
    double win_s = 10.0;
    { const char* e = getenv("DRAW_SAMPLE_SEC"); if (e && atof(e) > 0) win_s = atof(e); }
    HANDLE tm = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!tm) tm = CreateWaitableTimerW(NULL, FALSE, NULL);
    u32 rng = GetCurrentThreadId() * 2654435761u;
    ds_load_modules();
    double t0 = perf_now(); int win = 0;
    for (;;) {
        /* 0.5-1.5 ms apart, at random: a fixed period would lock onto the
         * game's own 1 ms timer waits and over- or under-count them. */
        rng = rng * 1664525u + 1013904223u;
        LARGE_INTEGER due; due.QuadPart = -(5000 + (LONGLONG)(rng >> 8) % 10000);
        if (tm && SetWaitableTimer(tm, &due, 0, NULL, NULL, FALSE)) WaitForSingleObject(tm, INFINITE);
        else Sleep(1);
        for (int i = 0; i < DS_NINST; i++) if (s_ds_inst[i]) ds_sample(s_ds_inst[i]);
        const double t = perf_now();
        if (t - t0 >= win_s) {
            for (int i = 0; i < DS_NINST; i++) if (s_ds_inst[i]) ds_flush(s_ds_inst[i], win, t - t0);
            win++; t0 = t; ds_load_modules();
        }
    }
    return 0;
}

/* Start sampling the calling thread as instance `which` (once). */
static void ds_add_current(int which, const char* out_env, const char* out_def)
{
    if (s_ds_inst[which]) return;
    DsInst* in = (DsInst*)calloc(1, sizeof *in);
    if (!in) return;
    {   /* this thread's whole stack reservation: the walk never reads outside it */
        const NT_TIB* tib = (const NT_TIB*)NtCurrentTeb();
        MEMORY_BASIC_INFORMATION mb;
        in->stack_hi = (u64)tib->StackBase;
        if (VirtualQuery((const u8*)tib->StackBase - 1, &mb, sizeof mb))
            in->stack_lo = (u64)mb.AllocationBase;
    }
    in->th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                        FALSE, GetCurrentThreadId());
    const char* o = getenv(out_env);
    in->f = fopen(o && *o ? o : out_def, "a");
    if (!in->th || !in->f) { fprintf(stderr, "[DS] %s: cannot sample this thread%c", out_def, 10); return; }
    fprintf(in->f, "[DS] sampler start (tid %lu), exe base 0x%llX%c", GetCurrentThreadId(),
            (unsigned long long)(u64)GetModuleHandleA(NULL), 10);
    fflush(in->f);
    MemoryBarrier();
    s_ds_inst[which] = in;
    if (InterlockedCompareExchange(&s_ds_running, 1, 0) == 0) {
        HANDLE h = CreateThread(NULL, 0, ds_thread, NULL, 0, NULL);
        if (h) { SetThreadPriority(h, THREAD_PRIORITY_HIGHEST); CloseHandle(h); }
    }
    fprintf(stderr, "[DS] sampling tid %lu into %s%c", GetCurrentThreadId(), o && *o ? o : out_def, 10);
}

/* GAME_SAMPLE=1: called once by cellGcmGetFlipStatus on the game's main thread. */
static void ds_game_hook(void)
{
    const char* e = getenv("GAME_SAMPLE");
    if (e && *e && *e != '0') ds_add_current(DS_GAME, "GAME_SAMPLE_OUT", "game_samples.txt");
}

static void ds_start_once(void)
{
    static int st = -1;
    if (st >= 0) return;
    { extern void (*g_rsx_game_sample_hook)(void); g_rsx_game_sample_hook = ds_game_hook; }
    const char* e = getenv("DRAW_SAMPLE");
    st = (e && *e && *e != '0') ? 1 : 0;
    if (st) ds_add_current(DS_DRAW, "DRAW_SAMPLE_OUT", "draw_samples.txt");
}
