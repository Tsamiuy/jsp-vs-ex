#define VF_MAX_WORKERS 15
#define VF_CHUNK       512u
static struct {
    int    n;                      /* workers, excluding the caller */
    u32    min_split;
    HANDLE th[VF_MAX_WORKERS];
    HANDLE go[VF_MAX_WORKERS];     /* auto-reset, one per worker    */
    HANDLE done;                   /* auto-reset                    */
    volatile LONG next;            /* next chunk to take            */
    volatile LONG busy;            /* workers still on this job     */
    const rsx_fetch_plan* plan;
    const rsx_fetch_src*  src;
    float (*out)[4];
    u32 total;
} s_vf;

static void vf_take_chunks(void)
{
    for (;;) {
        const u32 c = (u32)InterlockedIncrement(&s_vf.next) - 1u;
        const u64 o0 = (u64)c * VF_CHUNK;
        if (o0 >= s_vf.total) return;
        u32 o1 = (u32)o0 + VF_CHUNK; if (o1 > s_vf.total) o1 = s_vf.total;
        rsx_fetch_plan_range(s_vf.plan, s_vf.src, (u32)o0, o1, s_vf.out);
    }
}

static DWORD WINAPI vf_worker(LPVOID arg)
{
    const int me = (int)(intptr_t)arg;
    for (;;) {
        WaitForSingleObject(s_vf.go[me], INFINITE);
        vf_take_chunks();
        if (InterlockedDecrement(&s_vf.busy) == 0) SetEvent(s_vf.done);
    }
    return 0;
}

static void vf_pool_init(void)
{
    static int inited = 0;
    if (inited) return;
    inited = 1;
    SYSTEM_INFO si; GetSystemInfo(&si);
    /* Default: half the logical CPUs (the PPU/SPU threads need the rest),
     * minus the caller, at most VF_MAX_WORKERS. */
    int n = (int)si.dwNumberOfProcessors / 2 - 1;
    { const char* e = getenv("VFETCH_THREADS"); if (e) n = atoi(e); }
    if (n < 0) n = 0;
    if (n > VF_MAX_WORKERS) n = VF_MAX_WORKERS;
    s_vf.min_split = 2048u;
    { const char* e = getenv("VFETCH_MIN"); if (e) s_vf.min_split = (u32)strtoul(e, NULL, 0); }
    s_vf.done = CreateEventA(NULL, FALSE, FALSE, NULL);
    for (int i = 0; i < n && s_vf.done; i++) {
        s_vf.go[i] = CreateEventA(NULL, FALSE, FALSE, NULL);
        if (!s_vf.go[i]) break;
        s_vf.th[i] = CreateThread(NULL, 0, vf_worker, (LPVOID)(intptr_t)i, 0, NULL);
        if (!s_vf.th[i]) { CloseHandle(s_vf.go[i]); s_vf.go[i] = NULL; break; }
        s_vf.n = i + 1;
    }
    fprintf(stderr, "[VFETCH] %d worker thread(s), split draws >= %u vertices%c",
            s_vf.n, s_vf.min_split, 10);
}

