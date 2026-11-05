typedef struct { rsx_fetch_plan plan; u32 list_off, n; VPSlot* out; } VfaJob;
#define VFA_JOBS 4096
static struct {
    int     on;                   /* -1 = not decided yet */
    int     nthreads;
    HANDLE  sem;
    VfaJob* jobs;
    u32*    arena;  u32 arena_cap, arena_used;
    volatile LONG posted, taken, done;
} s_vfa = { .on = -1 };

static DWORD WINAPI vfa_worker(LPVOID arg)
{
    (void)arg;
    for (;;) {
        WaitForSingleObject(s_vfa.sem, INFINITE);
        /* Claim the next job by CAS: vfa_sync may have run it already (it
         * helps), in which case this wake-up finds nothing and sleeps again. */
        for (;;) {
            const LONG t = s_vfa.taken;
            if (t >= s_vfa.posted) break;
            if (InterlockedCompareExchange(&s_vfa.taken, t + 1, t) != t) continue;
            VfaJob* j = &s_vfa.jobs[t];
            rsx_fetch_src src = { 0, RSX_FETCH_VLIST, 0, NULL, s_vfa.arena + j->list_off };
            rsx_fetch_plan_range(&j->plan, &src, 0, j->n, (float (*)[4])j->out);
            InterlockedIncrement(&s_vfa.done);
            break;
        }
    }
    return 0;
}

static int vfa_enabled(void)
{
    if (s_vfa.on >= 0) return s_vfa.on;
    s_vfa.on = 0;
    if (!getenv("VFETCH_ASYNC") || vf_legacy()) return 0;
    SYSTEM_INFO si; GetSystemInfo(&si);
    int n = (int)si.dwNumberOfProcessors / 4; if (n > 3) n = 3; if (n < 1) n = 1;
    { const char* e = getenv("VFETCH_ASYNC_THREADS"); if (e && atoi(e) > 0) n = atoi(e); }
    if (n > 8) n = 8;
    s_vfa.jobs = (VfaJob*)malloc(sizeof(VfaJob) * VFA_JOBS);
    s_vfa.arena_cap = MAX_VP_VERTICES;
    s_vfa.arena = (u32*)malloc(sizeof(u32) * s_vfa.arena_cap);
    s_vfa.sem = CreateSemaphoreA(NULL, 0, VFA_JOBS, NULL);
    if (!s_vfa.jobs || !s_vfa.arena || !s_vfa.sem) return 0;
    for (int i = 0; i < n; i++) {
        HANDLE h = CreateThread(NULL, 0, vfa_worker, NULL, 0, NULL);
        if (!h) break;
        CloseHandle(h);
        s_vfa.nthreads++;
    }
    if (!s_vfa.nthreads) return 0;
    g_rsx_sync_hook = vfa_sync;
    s_vfa.on = 1;
    fprintf(stderr, "[VFETCH] async strip fetch on, %d worker thread(s)%c", s_vfa.nthreads, 10);
    return 1;
}

static void vfa_sync(void)
{
    if (s_vfa.on <= 0 || s_vfa.posted == 0) return;
    if (s_vfa.done != s_vfa.posted) {
        const double t0 = perf_on() ? perf_now() : 0.0;
        /* The present thread helps instead of only waiting. */
        for (;;) {
            const LONG t = s_vfa.taken;
            if (t >= s_vfa.posted) break;
            if (InterlockedCompareExchange(&s_vfa.taken, t + 1, t) == t) {
                /* Took job t from the workers' queue; consume its semaphore
                 * count so a worker does not wake for it. */
                WaitForSingleObject(s_vfa.sem, 0);
                VfaJob* j = &s_vfa.jobs[t];
                rsx_fetch_src src = { 0, RSX_FETCH_VLIST, 0, NULL, s_vfa.arena + j->list_off };
                rsx_fetch_plan_range(&j->plan, &src, 0, j->n, (float (*)[4])j->out);
                InterlockedIncrement(&s_vfa.done);
            }
        }
        while (s_vfa.done != s_vfa.posted) _mm_pause();
        if (perf_on()) s_perf_vfa_wait += perf_now() - t0;
    }
    MemoryBarrier();
    s_vfa.posted = s_vfa.taken = s_vfa.done = 0;
    s_vfa.arena_used = 0;
}

static void vfa_fetch_list(const rsx_state* state, const u32* list, u32 n, VPSlot* out)
{
    if (!n) return;
    if (!vfa_enabled()) {
        rsx_fetch_src src = { 0, RSX_FETCH_VLIST, 0, NULL, list };
        vf_fetch(state, &src, n, out);
        return;
    }
    if (s_vfa.posted >= VFA_JOBS || s_vfa.arena_used + n > s_vfa.arena_cap) vfa_sync();
    if (n > s_vfa.arena_cap) {          /* cannot queue: do it here */
        rsx_fetch_src src = { 0, RSX_FETCH_VLIST, 0, NULL, list };
        vf_fetch(state, &src, n, out);
        return;
    }
    VfaJob* j = &s_vfa.jobs[s_vfa.posted];
    rsx_fetch_plan_init(state, vf_input_mask(state), &j->plan);
    memcpy(s_vfa.arena + s_vfa.arena_used, list, (size_t)n * sizeof(u32));
    j->list_off = s_vfa.arena_used; j->n = n; j->out = out;
    s_vfa.arena_used += n;
    MemoryBarrier();
    InterlockedIncrement(&s_vfa.posted);
    ReleaseSemaphore(s_vfa.sem, 1, NULL);
}
