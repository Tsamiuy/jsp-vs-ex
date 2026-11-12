#!/usr/bin/env python3
"""Build the shader-hitches patch: apply.py <base.c> <out.c> <pure.h>
  base.c  libs/video/rsx_d3d12_backend.c as of ps3recomp-src d4b3d58 (branch
          jstars-port), LF line endings: `git show HEAD:libs/video/rsx_d3d12_backend.c`
  out.c   the patched file; `diff -u base.c out.c` is shader-hitches.patch
  pure.h  the D3D/Win32-free parts, for test/test_shader_hitches.c
Every anchor must occur exactly once, so a drifted base fails loudly instead
of patching the wrong place: re-run it on a newer base and fix the anchor it
names."""
import sys, re

src = open(sys.argv[1], encoding='utf-8').read()
out_path = sys.argv[2]
pure_path = sys.argv[3]

def rep(old, new, count=1):
    global src
    n = src.count(old)
    if n != count:
        sys.exit("anchor found %d times (want %d):\n%s" % (n, count, old[:300]))
    src = src.replace(old, new)

# ---------------------------------------------------------------- E1: slot field
rep("""    u32 vframe;         /* guest flip count when the source was last verified
                         * (TEX_VERIFY=frame only) */
} VPTexSlot;
""", """    u32 vframe;         /* guest flip count when the source was last verified
                         * (TEX_VERIFY=frame only) */
    u32 last_gen;       /* s_tex_gen when this slot was last bound (TEX_LRU /
                         * TEX_POOL eviction order) */
} VPTexSlot;
""")

# ---------------------------------------------------------------- E2: perf counters
rep("""static int s_perf_pso_calls = 0, s_perf_pso_miss = 0, s_perf_pso_hashbytes = 0;
""", """static int s_perf_pso_calls = 0, s_perf_pso_miss = 0, s_perf_pso_hashbytes = 0;
/* [PERF5] (shader-hitches patch): what a pipeline miss costs, split into the
 * HLSL compile (fxc), the disk-cache hits that replaced one, and the driver's
 * CreateGraphicsPipelineState; plus the PSO_ASYNC queue's counters. */
static double s_perf_fxc_t = 0.0, s_perf_psoc_t = 0.0, s_perf_pso_wait = 0.0;
static int    s_perf_fxc_n = 0, s_perf_psoc_n = 0, s_perf_shc_hit = 0;
static int    s_perf_pj_posted = 0, s_perf_pj_done = 0, s_perf_pj_fail = 0;
static int    s_perf_pj_skipped = 0, s_perf_pj_sib = 0;
""")

# ---------------------------------------------------------------- E3: shcache + async block
block = r'''
/* ===========================================================================
 * Shader-hitches patch (docs\archive\jstars-notes\patches\shader-hitches, 2026-10-21).
 * Fights dip to 10-17 fps when a pipeline is built on the present thread
 * (43-78 ms each in the stage survey, 120-450 of them at every match start).
 * Three opt-in switches, all off by default:
 *   SHADER_CACHE=1   compiled shader bytecode (DXBC) kept on disk, keyed by
 *                    the final HLSL text, so a program compiles once per
 *                    install instead of once per run. SHADER_CACHE_DIR=<dir>
 *                    (default "shadercache" in the working directory).
 *   PSO_ASYNC=1|2    guest-FP pipelines are built on worker threads. A draw
 *                    whose pipeline is not ready is skipped for that frame
 *                    (1) or drawn with a sibling pipeline: same shaders,
 *                    other blend/cull/mask (2). PSO_ASYNC_THREADS=<n>
 *                    (default 2), PSO_ASYNC_WAIT=<ms> (default 0 = never
 *                    wait; N = give the workers up to N ms per frame before
 *                    the draw pass so short builds still land in-frame).
 *   TEX_LRU=1 / TEX_POOL=1  texture slot eviction order (see
 *                    vp_upload_tex_slot_impl).
 * ======================================================================== */
/* SHP-PURE-BEGIN */
#define SHCACHE_VERSION 1u
typedef struct { u64 h1, h2; u32 len; } ShKey;
typedef struct { char magic[4]; u32 version; u64 h1, h2; u32 hlsl_len, blob_len; } ShCacheHdr;

/* Cache key of one shader: two independent 64-bit hashes of the FINAL HLSL
 * text (after every rewrite the backend applies) plus the profile. Any patch
 * that changes what the decompiler or the backend emits changes the text, so
 * it changes the key: stale entries are never served, they just go unused.
 * h1 is the FNV-1a the in-memory PS blob table keys on. */
static ShKey shcache_key(const char* text, u32 len, const char* profile)
{
    ShKey k; k.len = len;
    u64 h1 = 1469598103934665603ULL, h2 = 0x9E3779B97F4A7C15ull ^ (u64)len;
    for (u32 i = 0; i < len; i++) {
        h1 = (h1 ^ (u8)text[i]) * 1099511628211ULL;
        h2 = (h2 + (u8)text[i]) * 0xC2B2AE3D27D4EB4Full; h2 ^= h2 >> 31;
    }
    for (const char* p = profile; *p; p++) {
        h1 = (h1 ^ (u8)*p) * 1099511628211ULL;
        h2 = (h2 + (u8)*p) * 0xC2B2AE3D27D4EB4Full; h2 ^= h2 >> 31;
    }
    k.h1 = h1; k.h2 = h2 ^ ((u64)SHCACHE_VERSION * 0x165667B19E3779F9ull);
    return k;
}
static int shcache_hdr_ok(const ShCacheHdr* h, ShKey k)
{
    return memcmp(h->magic, "RSXC", 4) == 0 && h->version == SHCACHE_VERSION &&
           h->h1 == k.h1 && h->h2 == k.h2 && h->hlsl_len == k.len &&
           h->blob_len > 0 && h->blob_len < (16u << 20);
}
/* SHP-PURE-END */

static int shcache_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("SHADER_CACHE"); on = e ? atoi(e) : 0; }
    return on;
}
/* Identity of the HLSL compiler the blobs came from (d3dcompiler_47.dll's
 * size and write time), folded into the file name so bytecode from another
 * compiler build is never served. */
static u64 shcache_compiler_id(void)
{
    static u64 id = 0;
    if (id) return id;
    HMODULE m = GetModuleHandleA("d3dcompiler_47.dll");
    char path[MAX_PATH];
    if (m && GetModuleFileNameA(m, path, MAX_PATH)) {
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExA(path, GetFileExInfoStandard, &fa))
            id = (((u64)fa.nFileSizeHigh << 32) | fa.nFileSizeLow)
               ^ ((((u64)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime)
                  * 0x9E3779B97F4A7C15ull);
    }
    if (!id) id = 1;
    return id;
}
static const char* shcache_dir(void)
{
    static char dir[MAX_PATH]; static int init = 0;
    if (!init) {
        init = 1;
        const char* e = getenv("SHADER_CACHE_DIR");
        snprintf(dir, sizeof dir, "%s", (e && *e) ? e : "shadercache");
        CreateDirectoryA(dir, NULL);
    }
    return dir;
}
static int shcache_path(char* out, size_t n, const char* kind, ShKey k)
{
    return snprintf(out, n, "%s\\%s_%016llx%016llx.dxbc", shcache_dir(), kind,
                    (unsigned long long)k.h1,
                    (unsigned long long)(k.h2 ^ shcache_compiler_id())) < (int)n;
}
/* One file per shader: header (key, lengths) + the DXBC bytes. Returns a new
 * blob (one reference) or NULL. Safe from any thread: it touches no backend
 * state once shcache_dir()/shcache_compiler_id() have run once. */
static ID3DBlob* shcache_load(const char* kind, ShKey k)
{
    if (!shcache_on()) return NULL;
    char path[MAX_PATH];
    if (!shcache_path(path, sizeof path, kind, k)) return NULL;
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    ShCacheHdr h; ID3DBlob* b = NULL;
    if (fread(&h, sizeof h, 1, f) == 1 && shcache_hdr_ok(&h, k)) {
        if (SUCCEEDED(D3DCreateBlob(h.blob_len, &b)) && b) {
            if (fread(b->lpVtbl->GetBufferPointer(b), 1, h.blob_len, f) != h.blob_len) {
                b->lpVtbl->Release(b); b = NULL;
            }
        }
    }
    fclose(f);
    return b;
}
/* Written to a temp name and renamed into place, so a run killed mid-write
 * (run_test.ps1 kills the game on its timer) leaves no half file under the
 * real name; a half-written temp fails the header check anyway. */
static void shcache_store(const char* kind, ShKey k, ID3DBlob* b)
{
    if (!shcache_on() || !b) return;
    char path[MAX_PATH], tmp[MAX_PATH + 24];
    if (!shcache_path(path, sizeof path, kind, k)) return;
    snprintf(tmp, sizeof tmp, "%s.%lu.tmp", path, (unsigned long)GetCurrentThreadId());
    FILE* f = fopen(tmp, "wb");
    if (!f) return;
    ShCacheHdr h; memcpy(h.magic, "RSXC", 4); h.version = SHCACHE_VERSION;
    h.h1 = k.h1; h.h2 = k.h2; h.hlsl_len = k.len;
    h.blob_len = (u32)b->lpVtbl->GetBufferSize(b);
    const int ok = fwrite(&h, sizeof h, 1, f) == 1 &&
                   fwrite(b->lpVtbl->GetBufferPointer(b), 1, h.blob_len, f) == h.blob_len;
    fclose(f);
    if (!ok || !MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) DeleteFileA(tmp);
}

/* --- Guest-FP pipeline cache helpers (shared by the sync and async paths) --- */
/* Pixel-shader bytecode table keyed by the final HLSL text (h1). A pipeline is
 * one (VS, FP, blend, cull, ...) combination, and a J-Stars match builds well
 * over a thousand of them from far fewer distinct fragment programs. Present
 * thread only. */
enum { PS_BLOB_CACHE = 4096 };
static struct { unsigned long long key; ID3DBlob* blob; } s_ps_blobs[PS_BLOB_CACHE];
static int s_ps_blob_next = 0;
static ID3DBlob* ps_blob_find(unsigned long long hkey)   /* returns a reference */
{
    for (int i = 0; i < PS_BLOB_CACHE && s_ps_blobs[i].blob; i++)
        if (s_ps_blobs[i].key == hkey) {
            ID3DBlob* b = s_ps_blobs[i].blob; b->lpVtbl->AddRef(b); return b;
        }
    return NULL;
}
static void ps_blob_insert(unsigned long long hkey, ID3DBlob* pb)   /* takes its own reference */
{
    int slot = s_ps_blob_next; s_ps_blob_next = (s_ps_blob_next + 1) % PS_BLOB_CACHE;
    if (s_ps_blobs[slot].blob) s_ps_blobs[slot].blob->lpVtbl->Release(s_ps_blobs[slot].blob);
    s_ps_blobs[slot].key = hkey; s_ps_blobs[slot].blob = pb; pb->lpVtbl->AddRef(pb);
}
/* Disk cache, else D3DCompile (timed). One reference, or NULL with *hr/*err
 * set. Any thread: nothing here touches the tables. */
static ID3DBlob* ps_blob_build(const char* hlsl, ShKey pk, HRESULT* hr, ID3DBlob** err,
                               double* t_fxc, int* fxc_ran, int* disk_hit)
{
    *hr = S_OK;
    ID3DBlob* pb = shcache_load("ps", pk);
    if (pb) { if (disk_hit) *disk_hit = 1; return pb; }
    const double t0 = perf_on() ? perf_now() : 0.0;
    *hr = D3DCompile(hlsl, strlen(hlsl), "guest_fp", NULL, NULL, "main", "ps_5_0", 0, 0, &pb, err);
    if (perf_on() && t_fxc) *t_fxc += perf_now() - t0;
    if (fxc_ran) *fxc_ran = 1;
    if (FAILED(*hr) || !pb) { if (pb) pb->lpVtbl->Release(pb); return NULL; }
    shcache_store("ps", pk, pb);
    return pb;
}

/* A program whose pipeline cannot be built is not cached, so without this it
 * was decompiled and D3DCompiled again on EVERY draw that used it: J-Stars'
 * 235-instruction character shader was compiled 4,596 times and the frame
 * never finished. Failures are remembered by the same key fields. */
enum { PSO_FAIL_MAX = 512 };
static struct { u32 fp, uhash, vs_hash, rtfmt, misc; } s_pso_fail[PSO_FAIL_MAX];
static int s_pso_fail_n = 0;
static void pso_note_fail(u32 fp, u32 uhash, u32 vs_hash, u32 rtfmt, u32 misc)
{
    if (s_pso_fail_n < PSO_FAIL_MAX) {
        s_pso_fail[s_pso_fail_n].fp = fp; s_pso_fail[s_pso_fail_n].uhash = uhash;
        s_pso_fail[s_pso_fail_n].vs_hash = vs_hash; s_pso_fail[s_pso_fail_n].rtfmt = rtfmt;
        s_pso_fail[s_pso_fail_n].misc = misc; s_pso_fail_n++;
    }
}

static void vp_fp_key(VPFPEntry* k, u32 fp_addr, int vs_idx, u32 vs_hash, u32 blend, int nrt,
                      u32 rtfmt, int exp32, u64 gamma, u64 expand, u64 zkey, u32 uhash,
                      u32 cmask, u32 cull, u32 cube_mask)
{
    memset(k, 0, sizeof *k);
    k->fp_addr = fp_addr; k->vs_idx = vs_idx; k->vs_hash = vs_hash; k->gen = s_d3d.vp_gen;
    k->blend = blend; k->nrt = nrt; k->rtfmt = rtfmt; k->exp32 = exp32; k->gamma = gamma;
    k->expand = expand; k->zkey = zkey; k->ucode_hash = uhash; k->cmask = cmask;
    k->cull = cull; k->cube_mask = cube_mask;
}
static int vp_fp_same_key(const VPFPEntry* a, const VPFPEntry* b)   /* every PSO key field */
{
    return a->ucode_hash == b->ucode_hash && a->vs_idx == b->vs_idx && a->vs_hash == b->vs_hash &&
           a->gen == b->gen && a->blend == b->blend && a->nrt == b->nrt && a->rtfmt == b->rtfmt &&
           a->exp32 == b->exp32 && a->gamma == b->gamma && a->expand == b->expand &&
           a->zkey == b->zkey && a->cmask == b->cmask && a->cull == b->cull &&
           a->cube_mask == b->cube_mask;
}
/* Same VS and PS bytecode (every field the HLSL depends on), any blend, cull
 * or colour mask: the PSO_ASYNC=2 stand-in while the exact one is built. */
static int vp_fp_same_shaders(const VPFPEntry* a, const VPFPEntry* b)
{
    return a->ucode_hash == b->ucode_hash && a->vs_idx == b->vs_idx && a->vs_hash == b->vs_hash &&
           a->gen == b->gen && a->nrt == b->nrt && a->rtfmt == b->rtfmt && a->exp32 == b->exp32 &&
           a->gamma == b->gamma && a->expand == b->expand && a->zkey == b->zkey &&
           a->cube_mask == b->cube_mask;
}
static void vp_fp_insert(const VPFPEntry* ne)   /* evict oldest when full */
{
    if (s_d3d.vp_fp_n >= VP_FP_CACHE) {
        if (s_d3d.vp_fp[0].pso) s_d3d.vp_fp[0].pso->lpVtbl->Release(s_d3d.vp_fp[0].pso);
        memmove(&s_d3d.vp_fp[0], &s_d3d.vp_fp[1], sizeof(VPFPEntry) * (VP_FP_CACHE - 1));
        s_d3d.vp_fp_n = VP_FP_CACHE - 1;
    }
    s_d3d.vp_fp[s_d3d.vp_fp_n++] = *ne;
}

/* --- PSO_ASYNC: pipelines built on worker threads --------------------------
 * The present thread still decompiles (the decompiler keeps static state) and
 * builds the pipeline description; a worker does the two slow calls,
 * D3DCompile and CreateGraphicsPipelineState (both documented thread-safe),
 * and the present thread collects the result at the start of the next
 * prepass. While a build is in flight vp_get_fp_pso returns NULL with
 * s_pso_pending_hit set, and the draw pass skips that draw instead of
 * drawing it with the hardcoded fallback pixel shader. */
typedef struct {
    volatile LONG state;        /* 0 free, 1 queued or running, 2 done */
    VPFPEntry key;              /* pso NULL until done */
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd;
    D3D12_INPUT_ELEMENT_DESC il[16];
    ID3DBlob* vsb;              /* reference held for the build */
    ID3DBlob* pb;               /* known PS blob (reference) or NULL: hlsl compiles */
    char* hlsl;
    ShKey pk;
    u32 fail_misc;
    int pb_new, fxc_ran, disk_hit;
    double t_fxc, t_pso;
    HRESULT hr;
    ID3D12PipelineState* pso;
    char err[200];
} PsoJob;
#define PSO_JOBS 1024
static struct {
    int on;                     /* -1 undecided, 0 off, 1 skip, 2 sibling stand-in */
    int nthreads, wait_ms, used_hi;
    PsoJob* jobs;
    int* queue; int q_head, q_tail;   /* ring of job indices, under cs */
    CRITICAL_SECTION cs;
    HANDLE sem, done_ev;
    volatile LONG inflight;
} s_pj = { .on = -1 };
static int s_pso_pending_hit = 0;   /* last vp_get_fp_pso returned NULL because a build is in flight */

static DWORD WINAPI pso_worker(LPVOID arg)
{
    (void)arg;
    for (;;) {
        WaitForSingleObject(s_pj.sem, INFINITE);
        int ji = -1;
        EnterCriticalSection(&s_pj.cs);
        if (s_pj.q_head != s_pj.q_tail) {
            ji = s_pj.queue[s_pj.q_head]; s_pj.q_head = (s_pj.q_head + 1) % PSO_JOBS;
        }
        LeaveCriticalSection(&s_pj.cs);
        if (ji < 0) continue;
        PsoJob* j = &s_pj.jobs[ji];
        if (!j->pb) {
            ID3DBlob* e = NULL;
            j->pb = ps_blob_build(j->hlsl, j->pk, &j->hr, &e, &j->t_fxc, &j->fxc_ran, &j->disk_hit);
            if (e) {
                if (!j->pb) snprintf(j->err, sizeof j->err, "%s", (const char*)e->lpVtbl->GetBufferPointer(e));
                e->lpVtbl->Release(e);
            }
            j->pb_new = 1;
        }
        if (j->pb) {
            j->pd.PS.pShaderBytecode = j->pb->lpVtbl->GetBufferPointer(j->pb);
            j->pd.PS.BytecodeLength  = j->pb->lpVtbl->GetBufferSize(j->pb);
            const double t0 = perf_on() ? perf_now() : 0.0;
            j->hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(
                s_d3d.device, &j->pd, &IID_ID3D12PipelineState, (void**)&j->pso);
            if (perf_on()) j->t_pso = perf_now() - t0;
            if (FAILED(j->hr)) j->pso = NULL;
        } else if (SUCCEEDED(j->hr)) j->hr = E_FAIL;
        InterlockedExchange(&j->state, 2);
        SetEvent(s_pj.done_ev);
    }
    return 0;
}

static int pso_async_on(void)
{
    if (s_pj.on >= 0) return s_pj.on;
    const char* e = getenv("PSO_ASYNC");
    const int mode = e ? atoi(e) : 0;
    if (mode <= 0) { s_pj.on = 0; return 0; }
    if (!s_d3d.device) return 0;          /* decided again once the device exists */
    s_pj.on = 0;
    int n = 2;
    { const char* t = getenv("PSO_ASYNC_THREADS"); if (t && atoi(t) > 0) n = atoi(t); }
    if (n > 8) n = 8;
    { const char* w = getenv("PSO_ASYNC_WAIT"); s_pj.wait_ms = w ? atoi(w) : 0; }
    s_pj.jobs = (PsoJob*)calloc(PSO_JOBS, sizeof(PsoJob));
    s_pj.queue = (int*)calloc(PSO_JOBS, sizeof(int));
    s_pj.sem = CreateSemaphoreA(NULL, 0, PSO_JOBS, NULL);
    s_pj.done_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!s_pj.jobs || !s_pj.queue || !s_pj.sem || !s_pj.done_ev) return 0;
    InitializeCriticalSection(&s_pj.cs);
    /* Statics the workers read are decided here, on this thread. */
    perf_on();
    if (shcache_on()) { shcache_dir(); shcache_compiler_id(); }
    for (int i = 0; i < n; i++) {
        HANDLE h = CreateThread(NULL, 0, pso_worker, NULL, 0, NULL);
        if (!h) break;
        CloseHandle(h);
        s_pj.nthreads++;
    }
    if (!s_pj.nthreads) return 0;
    s_pj.on = mode > 1 ? 2 : 1;
    fprintf(stderr, "[PSO] async pipeline builds on: %d worker thread(s), mode %d, wait %d ms%c",
            s_pj.nthreads, s_pj.on, s_pj.wait_ms, 10);
    return s_pj.on;
}

static int pso_async_pending(const VPFPEntry* key)
{
    if (s_pj.inflight <= 0) return 0;
    for (int i = 0; i < s_pj.used_hi; i++)
        if (s_pj.jobs[i].state != 0 && vp_fp_same_key(&s_pj.jobs[i].key, key)) return 1;
    return 0;
}
/* What a draw gets while its pipeline is in flight: a sibling (mode 2) or
 * NULL with the pending flag raised (the draw pass then skips the draw). */
static ID3D12PipelineState* pso_async_standin(const VPFPEntry* key)
{
    s_pso_pending_hit = 1;
    if (s_pj.on >= 2)
        for (int i = 0; i < s_d3d.vp_fp_n; i++)
            if (vp_fp_same_shaders(&s_d3d.vp_fp[i], key)) {
                s_perf_pj_sib++; s_pso_pending_hit = 0;
                return s_d3d.vp_fp[i].pso;
            }
    return NULL;
}
/* Queue a build: 1 when queued, 0 when no job slot is free (the caller then
 * builds it in place, as without the switch). */
static int pso_async_submit(const VPFPEntry* key, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pd,
                            ID3DBlob* vsb, ID3DBlob* pb, const char* hlsl, ShKey pk, u32 fail_misc)
{
    int ji = -1;
    for (int i = 0; i < PSO_JOBS; i++) if (s_pj.jobs[i].state == 0) { ji = i; break; }
    if (ji < 0) return 0;
    char* hcopy = NULL;
    if (!pb) { hcopy = _strdup(hlsl); if (!hcopy) return 0; }
    PsoJob* j = &s_pj.jobs[ji];
    memset(j, 0, sizeof *j);
    j->key = *key; j->key.pso = NULL;
    j->pd = *pd;
    memcpy(j->il, pd->InputLayout.pInputElementDescs, sizeof j->il);
    j->pd.InputLayout.pInputElementDescs = j->il;
    j->pd.VS.pShaderBytecode = vsb->lpVtbl->GetBufferPointer(vsb);
    j->pd.VS.BytecodeLength  = vsb->lpVtbl->GetBufferSize(vsb);
    vsb->lpVtbl->AddRef(vsb); j->vsb = vsb;
    if (pb) { pb->lpVtbl->AddRef(pb); j->pb = pb; }
    j->hlsl = hcopy; j->pk = pk; j->fail_misc = fail_misc; j->hr = S_OK;
    if (ji + 1 > s_pj.used_hi) s_pj.used_hi = ji + 1;
    InterlockedIncrement(&s_pj.inflight);
    InterlockedExchange(&j->state, 1);
    EnterCriticalSection(&s_pj.cs);
    s_pj.queue[s_pj.q_tail] = ji; s_pj.q_tail = (s_pj.q_tail + 1) % PSO_JOBS;
    LeaveCriticalSection(&s_pj.cs);
    ReleaseSemaphore(s_pj.sem, 1, NULL);
    s_perf_pj_posted++;
    return 1;
}
/* Present thread: move finished builds into the caches. */
static void pso_async_collect(void)
{
    if (s_pj.on <= 0 || s_pj.inflight <= 0) return;
    for (int i = 0; i < s_pj.used_hi; i++) {
        PsoJob* j = &s_pj.jobs[i];
        if (j->state != 2) continue;
        MemoryBarrier();
        if (j->pb && j->pb_new) ps_blob_insert(j->pk.h1, j->pb);
        if (j->pso) {
            j->key.pso = j->pso;
            vp_fp_insert(&j->key);
            { static int _ok = 0; if (_ok++ < 32)
                printf("[FP] guest FP pipeline ready (fp=0x%08X, async)\n", j->key.fp_addr); }
        } else {
            pso_note_fail(j->key.fp_addr, j->key.ucode_hash, j->key.vs_hash, j->key.rtfmt, j->fail_misc);
            { static int _e = 0; if (_e++ < 16)
                printf("[FP] %s FAIL (fp=0x%08X, 0x%08lX, async) nrt=%d rtfmt=%u: %s\n",
                       j->pb ? "PSO" : "PS compile", j->key.fp_addr, (unsigned long)j->hr,
                       j->key.nrt, j->key.rtfmt, j->err); }
            s_perf_pj_fail++;
        }
        if (perf_on()) {
            s_perf_fxc_t += j->t_fxc; if (j->fxc_ran) s_perf_fxc_n++;
            if (j->disk_hit) s_perf_shc_hit++;
            s_perf_psoc_t += j->t_pso; if (j->pso) s_perf_psoc_n++;
        }
        if (j->pb) j->pb->lpVtbl->Release(j->pb);
        if (j->vsb) j->vsb->lpVtbl->Release(j->vsb);
        free(j->hlsl);
        j->pb = NULL; j->vsb = NULL; j->hlsl = NULL; j->pso = NULL;
        InterlockedExchange(&j->state, 0);
        InterlockedDecrement(&s_pj.inflight);
        s_perf_pj_done++;
    }
}
/* End of the prepass: collect, and with PSO_ASYNC_WAIT give the workers a
 * bounded time so builds that finish within it still draw this frame. */
static void pso_async_prepass_end(void)
{
    if (!pso_async_on()) return;
    pso_async_collect();
    if (s_pj.wait_ms > 0 && s_pj.inflight > 0) {
        const double t0 = perf_now();
        while (s_pj.inflight > 0 && (perf_now() - t0) * 1e3 < (double)s_pj.wait_ms) {
            WaitForSingleObject(s_pj.done_ev, 1);
            pso_async_collect();
        }
        if (perf_on()) s_perf_pso_wait += perf_now() - t0;
    }
}

static int vp_get_vs(const rsx_state* st)
'''
rep("""static int vp_get_vs(const rsx_state* st)
""", block.lstrip('\n'))

# ---------------------------------------------------------------- E4: VS compile via disk cache
rep("""    ID3DBlob* vb = NULL; ID3DBlob* e = NULL;
    HRESULT hr = D3DCompile(hlsl, strlen(hlsl), "guest_vp2", NULL, NULL,
                            "main", "vs_5_0", 0, 0, &vb, &e);
    if (e) e->lpVtbl->Release(e);
""", """    ID3DBlob* vb = NULL; ID3DBlob* e = NULL;
    HRESULT hr = S_OK;
    {   /* SHADER_CACHE: the bytecode from disk when this exact HLSL was compiled before */
        const ShKey vk = shcache_key(hlsl, (u32)strlen(hlsl), "vs_5_0");
        vb = shcache_load("vs", vk);
        if (vb) s_perf_shc_hit++;
        else {
            const double _t0 = perf_on() ? perf_now() : 0.0;
            hr = D3DCompile(hlsl, strlen(hlsl), "guest_vp2", NULL, NULL,
                            "main", "vs_5_0", 0, 0, &vb, &e);
            if (perf_on()) { s_perf_fxc_t += perf_now() - _t0; s_perf_fxc_n++; }
            if (SUCCEEDED(hr) && vb) shcache_store("vs", vk, vb);
        }
    }
    if (e) e->lpVtbl->Release(e);
""")

# ---------------------------------------------------------------- E5/E6: fail table + pending check
rep("""    /* nrt 0 = a depth-only pass (SHADOW_PASSES): no colour target at all. */
    if (nrt < 0) nrt = 0; if (nrt > 4) nrt = 4;
""", """    s_pso_pending_hit = 0;
    /* nrt 0 = a depth-only pass (SHADOW_PASSES): no colour target at all. */
    if (nrt < 0) nrt = 0; if (nrt > 4) nrt = 4;
""")

rep("""    enum { PSO_FAIL_MAX = 512 };
    static struct { u32 fp, uhash, vs_hash, rtfmt, misc; } s_pso_fail[PSO_FAIL_MAX];
    static int s_pso_fail_n = 0;
    const u32 fail_misc""", """    /* (s_pso_fail is file scope now: the async collector records failures too.) */
    const u32 fail_misc""")

rep("""#define PSO_NOTE_FAIL() do { if (s_pso_fail_n < PSO_FAIL_MAX) { \\
        s_pso_fail[s_pso_fail_n].fp = fp_addr; s_pso_fail[s_pso_fail_n].uhash = uhash; \\
        s_pso_fail[s_pso_fail_n].vs_hash = vs_hash; s_pso_fail[s_pso_fail_n].rtfmt = (u32)rtfmt; \\
        s_pso_fail[s_pso_fail_n].misc = fail_misc; s_pso_fail_n++; } } while (0)
""", """#define PSO_NOTE_FAIL() pso_note_fail(fp_addr, uhash, vs_hash, (u32)rtfmt, fail_misc)
""")

rep("""            s_pso_fail[i].misc == fail_misc)
            return NULL;
    s_perf_pso_miss++;      /* falls through to a full decompile + D3DCompile */
""", """            s_pso_fail[i].misc == fail_misc)
            return NULL;
    /* PSO_ASYNC: this exact pipeline is already being built. Checked BEFORE
     * the decompile below, which every draw of a pending program would
     * otherwise repeat each frame. */
    if (pso_async_on()) {
        VPFPEntry want;
        vp_fp_key(&want, fp_addr, vs_idx, vs_hash, blend, nrt, (u32)rtfmt, exp32, gamma, expand,
                  zkey, uhash, cmask, cull, cube_mask);
        if (pso_async_pending(&want)) return pso_async_standin(&want);
    }
    s_perf_pso_miss++;      /* falls through to a full decompile + D3DCompile */
""")

# ---------------------------------------------------------------- E7: PS blob cache block
rep("""    ID3DBlob* pb = NULL; ID3DBlob* e = NULL;
    /* Pixel-shader bytecode cache keyed by the final HLSL text. A pipeline is
     * one (VS, FP, blend, cull, ...) combination, and a J-Stars match builds
     * well over a thousand of them from far fewer distinct fragment programs;
     * recompiling the same HLSL for each made every pipeline miss cost ~30 ms
     * and held the match at under 1 fps while the cache warmed. */
    enum { PS_BLOB_CACHE = 4096 };
    static struct { unsigned long long key; ID3DBlob* blob; } s_ps_blobs[PS_BLOB_CACHE];
    static int s_ps_blob_next = 0;
    unsigned long long hkey = 1469598103934665603ULL;
    for (const char* hp = hlsl; *hp; hp++) { hkey ^= (unsigned char)*hp; hkey *= 1099511628211ULL; }
    for (int i = 0; i < PS_BLOB_CACHE && s_ps_blobs[i].blob; i++)
        if (s_ps_blobs[i].key == hkey) {
            pb = s_ps_blobs[i].blob; pb->lpVtbl->AddRef(pb);   /* released after the PSO */
            break;
        }
    HRESULT hr = S_OK;
    if (!pb) {
        hr = D3DCompile(hlsl, strlen(hlsl), "guest_fp", NULL, NULL,
                        "main", "ps_5_0", 0, 0, &pb, &e);
        if (SUCCEEDED(hr) && pb) {
            int slot = s_ps_blob_next; s_ps_blob_next = (s_ps_blob_next + 1) % PS_BLOB_CACHE;
            if (s_ps_blobs[slot].blob) s_ps_blobs[slot].blob->lpVtbl->Release(s_ps_blobs[slot].blob);
            s_ps_blobs[slot].key = hkey; s_ps_blobs[slot].blob = pb; pb->lpVtbl->AddRef(pb);
        }
    }
    if (FAILED(hr) || !pb) {
""", """    ID3DBlob* pb = NULL; ID3DBlob* e = NULL;
    /* Pixel-shader bytecode cache keyed by the final HLSL text (s_ps_blobs,
     * file scope). A pipeline is one (VS, FP, blend, cull, ...) combination,
     * and a J-Stars match builds well over a thousand of them from far fewer
     * distinct fragment programs; recompiling the same HLSL for each made
     * every pipeline miss cost ~30 ms and held the match at under 1 fps while
     * the cache warmed. Then the disk cache (SHADER_CACHE), then D3DCompile.
     * With PSO_ASYNC the compile is the worker's job: pb stays NULL here. */
    const ShKey pk = shcache_key(hlsl, (u32)strlen(hlsl), "ps_5_0");
    const unsigned long long hkey = pk.h1;
    pb = ps_blob_find(hkey);                                /* released after the PSO */
    HRESULT hr = S_OK;
    const int pso_async = pso_async_on();
    if (!pb && !pso_async) {
        int disk_hit = 0;
        pb = ps_blob_build(hlsl, pk, &hr, &e, &s_perf_fxc_t, NULL, &disk_hit);
        if (pb) { if (disk_hit) s_perf_shc_hit++; else if (perf_on()) s_perf_fxc_n++;
                  ps_blob_insert(hkey, pb); }
    }
    if (!pso_async && (FAILED(hr) || !pb)) {
""")

# ---------------------------------------------------------------- E8: pd.PS only when known
rep("""    pd.PS.pShaderBytecode = pb->lpVtbl->GetBufferPointer(pb);
    pd.PS.BytecodeLength  = pb->lpVtbl->GetBufferSize(pb);
""", """    if (pb) {   /* PSO_ASYNC: the worker fills this in when it compiles the PS */
        pd.PS.pShaderBytecode = pb->lpVtbl->GetBufferPointer(pb);
        pd.PS.BytecodeLength  = pb->lpVtbl->GetBufferSize(pb);
    }
""")

# ---------------------------------------------------------------- E9: submit / timed create
rep("""    ID3D12PipelineState* pso = NULL;
    hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(
        s_d3d.device, &pd, &IID_ID3D12PipelineState, (void**)&pso);
    pb->lpVtbl->Release(pb);
    if (FAILED(hr)) {
""", """    if (pso_async) {
        VPFPEntry key;
        vp_fp_key(&key, fp_addr, vs_idx, vs_hash, blend, nrt, (u32)rtfmt, exp32, gamma, expand,
                  zkey, uhash, cmask, cull, cube_mask);
        if (pso_async_submit(&key, &pd, vsb, pb, hlsl, pk, fail_misc)) {
            if (pb) pb->lpVtbl->Release(pb);
            return pso_async_standin(&key);
        }
        if (!pb) {   /* no free job: build it here, as without the switch */
            int disk_hit = 0;
            pb = ps_blob_build(hlsl, pk, &hr, &e, &s_perf_fxc_t, NULL, &disk_hit);
            if (!pb) {
                static int _e4 = 0; if (_e4++ < 16)
                    printf("[FP] PS compile FAIL (fp=0x%08X): %s\\n", fp_addr,
                           e ? (const char*)e->lpVtbl->GetBufferPointer(e) : "?");
                if (e) e->lpVtbl->Release(e);
                PSO_NOTE_FAIL();
                return NULL;
            }
            if (e) { e->lpVtbl->Release(e); e = NULL; }
            if (disk_hit) s_perf_shc_hit++; else if (perf_on()) s_perf_fxc_n++;
            ps_blob_insert(hkey, pb);
            pd.PS.pShaderBytecode = pb->lpVtbl->GetBufferPointer(pb);
            pd.PS.BytecodeLength  = pb->lpVtbl->GetBufferSize(pb);
        }
    }
    ID3D12PipelineState* pso = NULL;
    {
        const double _tc0 = perf_on() ? perf_now() : 0.0;
        hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(
            s_d3d.device, &pd, &IID_ID3D12PipelineState, (void**)&pso);
        if (perf_on()) { s_perf_psoc_t += perf_now() - _tc0; s_perf_psoc_n++; }
    }
    pb->lpVtbl->Release(pb);
    if (FAILED(hr)) {
""")

# ---------------------------------------------------------------- E10: insert tail
rep("""    /* insert (evict oldest when full) */
    if (s_d3d.vp_fp_n >= VP_FP_CACHE) {
        if (s_d3d.vp_fp[0].pso) s_d3d.vp_fp[0].pso->lpVtbl->Release(s_d3d.vp_fp[0].pso);
        memmove(&s_d3d.vp_fp[0], &s_d3d.vp_fp[1], sizeof(VPFPEntry) * (VP_FP_CACHE - 1));
        s_d3d.vp_fp_n = VP_FP_CACHE - 1;
    }
    s_d3d.vp_fp[s_d3d.vp_fp_n].fp_addr = fp_addr;
    s_d3d.vp_fp[s_d3d.vp_fp_n].vs_idx  = vs_idx;
    s_d3d.vp_fp[s_d3d.vp_fp_n].vs_hash = vs_hash;
    s_d3d.vp_fp[s_d3d.vp_fp_n].gen     = s_d3d.vp_gen;
    s_d3d.vp_fp[s_d3d.vp_fp_n].blend   = blend;
    s_d3d.vp_fp[s_d3d.vp_fp_n].nrt     = nrt;
    s_d3d.vp_fp[s_d3d.vp_fp_n].rtfmt   = (u32)rtfmt;
    s_d3d.vp_fp[s_d3d.vp_fp_n].exp32   = exp32;
    s_d3d.vp_fp[s_d3d.vp_fp_n].gamma   = gamma;
    s_d3d.vp_fp[s_d3d.vp_fp_n].expand  = expand;
    s_d3d.vp_fp[s_d3d.vp_fp_n].zkey    = zkey;
    s_d3d.vp_fp[s_d3d.vp_fp_n].ucode_hash = uhash;
    s_d3d.vp_fp[s_d3d.vp_fp_n].cmask   = cmask;
    s_d3d.vp_fp[s_d3d.vp_fp_n].cull    = cull;
    s_d3d.vp_fp[s_d3d.vp_fp_n].cube_mask = cube_mask;
    s_d3d.vp_fp[s_d3d.vp_fp_n].pso     = pso;
    s_d3d.vp_fp_n++;
    return pso;
}
""", """    /* insert (evict oldest when full) */
    {
        VPFPEntry ne;
        vp_fp_key(&ne, fp_addr, vs_idx, vs_hash, blend, nrt, (u32)rtfmt, exp32, gamma, expand,
                  zkey, uhash, cmask, cull, cube_mask);
        ne.pso = pso;
        vp_fp_insert(&ne);
    }
    return pso;
}
""")

# ---------------------------------------------------------------- E11: prepass collect + wait
rep("""    double _pre0 = perf_on() ? perf_now() : 0.0;
    for (int _i = 0; _i < VP_TEX_SLOTS; _i++) s_d3d.vp_tex[_i].used = 0;
    s_tex_gen++;                          /* invalidates the vp_upload_tex_slot memo */
""", """    double _pre0 = perf_on() ? perf_now() : 0.0;
    for (int _i = 0; _i < VP_TEX_SLOTS; _i++) s_d3d.vp_tex[_i].used = 0;
    s_tex_gen++;                          /* invalidates the vp_upload_tex_slot memo */
    if (pso_async_on()) pso_async_collect();   /* builds finished since last frame */
""")

rep("""        if (perf_on()) s_perf_pso += perf_now() - _ps0;
    }

    /* Transition render target to RENDER_TARGET state */
""", """        if (perf_on()) s_perf_pso += perf_now() - _ps0;
    }
    pso_async_prepass_end();   /* PSO_ASYNC: collect, optional bounded wait */

    /* Transition render target to RENDER_TARGET state */
""")

# ---------------------------------------------------------------- E12: draw loop skip
rep("""                /* A depth-only op has no colour target for the fallback PSO. */
                if (zdraw && !dpso) { zn_skip[cur_zt]++; continue; }
""", """                /* A depth-only op has no colour target for the fallback PSO. */
                if (zdraw && !dpso) { zn_skip[cur_zt]++; continue; }
                /* PSO_ASYNC: the pipeline is still being built. Skipping the
                 * draw for a frame beats the hardcoded fallback shader (wrong
                 * colours) and beats stalling the present thread. */
                if (dr->fp_addr && !dpso && s_pso_pending_hit) { s_perf_pj_skipped++; continue; }
""")

# ---------------------------------------------------------------- E13: texture statics + policy
rep("""static u64    s_txs_create;        /* texture resources (re)created */
static u32    s_tex_gen = 1;       /* bumped where vp_tex[].used is reset */
""", """static u64    s_txs_create;        /* texture resources (re)created */
static u32    s_tex_gen = 1;       /* bumped where vp_tex[].used is reset */
/* Shader-hitches patch: where a NEW upload's time goes, and how many "new"
 * textures had been uploaded before (evicted since, so re-uploaded). */
static double s_txs_t_create;      /* CreateCommittedResource x2, inside new/reupload */
static double s_txs_t_decode;      /* rsx_texture_decode of every face and level */
static u64    s_txs_again;         /* new uploads of a texture seen earlier in the run */
static u32    s_tex_seen[16384];   /* open-addressed set of texture fingerprints */
static int tex_seen_before(u32 key, u32 w, u32 h, u32 fmt)
{
    u32 fp = (key * 2654435761u) ^ (w * 40503u) ^ (h << 7) ^ (fmt << 21);
    if (!fp) fp = 1;
    u32 i = (fp ^ (fp >> 16)) & 16383u;
    for (u32 n = 0; n < 16384u; n++, i = (i + 1) & 16383u) {
        if (s_tex_seen[i] == fp) return 1;
        if (!s_tex_seen[i]) { s_tex_seen[i] = fp; return 0; }
    }
    return 0;
}
/* TEX_LRU=1 / TEX_POOL=1 (opt-in): which free slot a new texture takes.
 * The old rule took the FIRST slot not bound in this batch, whatever it
 * held: a texture the camera will need again next frame was evicted as
 * readily as one not seen for minutes, and the resource in the slot almost
 * never had the new texture's size, so 80% of new uploads also recreated
 * the D3D resources (stage survey: 4,000-7,600 new uploads per fight,
 * 1.4-2.1 ms each).
 *   TEX_LRU=1   take the slot bound longest ago (an empty slot first).
 *   TEX_POOL=1  LRU, but prefer an unused slot whose resource already has
 *               this size/format/cube-ness if it was last bound at least
 *               TEX_POOL_AGE batches ago (default 4): the upload then
 *               reuses the resources instead of creating two new ones. */
static int tex_policy(void)
{
    static int p = -1;
    if (p < 0) { const char* a = getenv("TEX_POOL"); const char* b = getenv("TEX_LRU");
                 p = (a && atoi(a)) ? 2 : (b && atoi(b)) ? 1 : 0; }
    return p;
}
static u32 tex_pool_age(void)
{
    static int a = -1;
    if (a < 0) { const char* e = getenv("TEX_POOL_AGE"); a = e ? atoi(e) : 4; if (a < 1) a = 1; }
    return (u32)a;
}
/* SHP-PURE-BEGIN */
/* One pass over the slots for the TEX_LRU / TEX_POOL policies: an unused
 * slot with no resource, the oldest unused slot, and the oldest unused slot
 * whose resource already has these dimensions. -1 = none. */
static void tex_scan_candidates(const VPTexSlot* slots, int n, u32 gen, u32 w, u32 h, u32 fmt,
                                int cube, int* empty_slot, int* fit_slot, u32* fit_age, int* lru_slot)
{
    u32 lru_age = 0;
    *empty_slot = *fit_slot = *lru_slot = -1; *fit_age = 0;
    for (int i = 0; i < n; i++) {
        const VPTexSlot* c = &slots[i];
        if (c->used) continue;
        if (!c->res) { if (*empty_slot < 0) *empty_slot = i; continue; }
        const u32 age = gen - c->last_gen;
        if (*lru_slot < 0 || age > lru_age) { *lru_slot = i; lru_age = age; }
        if (c->w == w && c->h == h && c->fmt == fmt && c->cube == cube &&
            (*fit_slot < 0 || age > *fit_age)) { *fit_slot = i; *fit_age = age; }
    }
}
static int tex_pick_slot(int policy, int first_free, int empty_slot, int fit_slot, u32 fit_age,
                         int lru_slot, u32 pool_age)
{
    if (policy <= 0) return first_free;
    if (empty_slot >= 0) return empty_slot;
    if (policy >= 2 && fit_slot >= 0 && fit_age >= pool_age) return fit_slot;
    if (lru_slot >= 0) return lru_slot;
    return first_free;
}
/* SHP-PURE-END */
""")

rep("""            if (tex_verify_frame() && c->vframe == cellGcm_flip_request_count()) {
                c->used = 1; s_txo = TXO_FRAME; return i;
            }
""", """            if (tex_verify_frame() && c->vframe == cellGcm_flip_request_count()) {
                c->used = 1; c->last_gen = s_tex_gen; s_txo = TXO_FRAME; return i;
            }
""")

rep("""            if (cs == c->csum) {
                c->used = 1; c->vframe = cellGcm_flip_request_count();
                s_txo = TXO_SAME; return i;
            }
""", """            if (cs == c->csum) {
                c->used = 1; c->vframe = cellGcm_flip_request_count();
                c->last_gen = s_tex_gen;
                s_txo = TXO_SAME; return i;
            }
""")

rep("""        if (!c->used && freeslot < 0) freeslot = i;
    }
""", """        if (!c->used && freeslot < 0) freeslot = i;
    }
    if (slot < 0 && freeslot >= 0 && tex_policy() > 0) {   /* TEX_LRU / TEX_POOL */
        int es, fs, ls; u32 fa;
        tex_scan_candidates(s_d3d.vp_tex, nslots, s_tex_gen, w, h, fmt, cube, &es, &fs, &fa, &ls);
        freeslot = tex_pick_slot(tex_policy(), freeslot, es, fs, fa, ls, tex_pool_age());
    }
""")

# create timing
rep("""    if (!t->res) {
        D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td = {0};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = w; td.Height = h;
""", """    if (!t->res) {
        const double _tc0 = perf_on() ? perf_now() : 0.0;
        D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td = {0};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = w; td.Height = h;
""")
rep("""        fresh = 1;
        s_txs_create++;
    }
""", """        fresh = 1;
        s_txs_create++;
        if (perf_on()) s_txs_t_create += perf_now() - _tc0;
    }
""")

# decode timing
rep("""    const u32 _nfaces     = cube ? 6u : 1u;
    const u32 _off0 = off; u8* const _map0 = (u8*)mapped;
""", """    const u32 _nfaces     = cube ? 6u : 1u;
    const double _td0 = perf_on() ? perf_now() : 0.0;
    const u32 _off0 = off; u8* const _map0 = (u8*)mapped;
""")
rep("""    t->up->lpVtbl->Unmap(t->up, 0, NULL);

    if (!fresh) {   /* reused resource: PSR -> COPY_DEST first */
""", """    t->up->lpVtbl->Unmap(t->up, 0, NULL);
    if (perf_on()) s_txs_t_decode += perf_now() - _td0;

    if (!fresh) {   /* reused resource: PSR -> COPY_DEST first */
""")

rep("""    t->off = off; t->key = key_off; t->w = w; t->h = h; t->fmt = fmt;
    t->cube = cube; t->used = 1; t->vframe = cellGcm_flip_request_count();
    t->levels = levels;
""", """    t->off = off; t->key = key_off; t->w = w; t->h = h; t->fmt = fmt;
    t->cube = cube; t->used = 1; t->vframe = cellGcm_flip_request_count();
    t->levels = levels; t->last_gen = s_tex_gen;
    if (s_txo == TXO_NEW && tex_seen_before(key_off, w, h, fmt)) s_txs_again++;
""")

# ---------------------------------------------------------------- E15: PERF3 / PERF5 print
rep("""              fprintf(stderr, "%s | created %llu%c", line, (unsigned long long)s_txs_create, 10);
              s_txs_create = 0; }
""", """              fprintf(stderr, "%s | created %llu (%.3fs) | decode %.3fs | again %llu | policy %d%c",
                      line, (unsigned long long)s_txs_create, s_txs_t_create, s_txs_t_decode,
                      (unsigned long long)s_txs_again, tex_policy(), 10);
              s_txs_create = 0; s_txs_t_create = 0.0; s_txs_t_decode = 0.0; s_txs_again = 0; }
            /* [PERF5]: the compile side of the pipeline misses (shader-hitches patch). */
            fprintf(stderr, "[PERF5] shaders: fxc %d %.3fs | disk hits %d | pso create %d %.3fs"
                            " | async posted %d done %d fail %d inflight %ld skipped %d sibling %d wait %.3fs%c",
                    s_perf_fxc_n, s_perf_fxc_t, s_perf_shc_hit, s_perf_psoc_n, s_perf_psoc_t,
                    s_perf_pj_posted, s_perf_pj_done, s_perf_pj_fail, (long)s_pj.inflight,
                    s_perf_pj_skipped, s_perf_pj_sib, s_perf_pso_wait, 10);
            s_perf_fxc_n = 0; s_perf_fxc_t = 0.0; s_perf_shc_hit = 0; s_perf_psoc_n = 0;
            s_perf_psoc_t = 0.0; s_perf_pj_posted = 0; s_perf_pj_done = 0; s_perf_pj_fail = 0;
            s_perf_pj_skipped = 0; s_perf_pj_sib = 0; s_perf_pso_wait = 0.0;
""")

open(out_path, 'w', encoding='utf-8', newline='\n').write(src)

# the pure parts, for the offline test
pure = re.findall(r'/\* SHP-PURE-BEGIN \*/\n(.*?)/\* SHP-PURE-END \*/\n', src, re.S)
if len(pure) != 2:
    sys.exit("expected 2 pure regions, found %d" % len(pure))
open(pure_path, 'w', encoding='utf-8', newline='\n').write(
    "/* Generated by apply.py from the patched rsx_d3d12_backend.c: the parts\n"
    " * of the shader-hitches patch with no D3D or Win32 dependency. */\n" + "".join(pure))
print("ok: wrote", out_path, "and", pure_path)
