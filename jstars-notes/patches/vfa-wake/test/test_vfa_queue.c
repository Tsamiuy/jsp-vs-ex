/* VFA_WAKE queue check: the same claim/sleep/wake/sync logic as the patched
 * vfa_* functions (copied, with the fetch replaced by "write the job's stamp"),
 * driven with random job runs and sync points. Checks: every job runs exactly
 * once, all of a sync's jobs are finished when it returns, no job runs from a
 * slot that was not posted; and counts producer wake-ups.
 * usage: test_vfa_queue <wake 0/1> <spin> <jobs> */
#include <windows.h>
#include <intrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned int u32;
#define VFA_JOBS 4096
typedef struct { u32 stamp; volatile LONG ran; } Job;
static struct {
    HANDLE sem; Job* jobs; volatile LONG posted, taken, done;
    int wake, spin; volatile LONG sleepers, spinning; LONG base;
} s_vfa;
static volatile LONG bad;

static void run_job(Job* j) { if (InterlockedIncrement(&j->ran) != 1) InterlockedIncrement(&bad); }
static int vfa_run_one(int consume_sem)
{
    for (;;) {
        const LONG t = s_vfa.taken;
        if ((LONG)((ULONG)t - (ULONG)s_vfa.posted) >= 0) return 0;
        if (InterlockedCompareExchange(&s_vfa.taken, t + 1, t) != t) continue;
        if (consume_sem) WaitForSingleObject(s_vfa.sem, 0);
        Job* j = &s_vfa.jobs[(u32)t % VFA_JOBS];
        if (j->stamp != (u32)t) InterlockedIncrement(&bad);   /* slot not (yet) posted */
        run_job(j);
        InterlockedIncrement(&s_vfa.done);
        return 1;
    }
}
static int vfa_claim_sleeper(void)
{
    LONG s = s_vfa.sleepers;
    while (s > 0) { const LONG o = InterlockedCompareExchange(&s_vfa.sleepers, s - 1, s); if (o == s) return 1; s = o; }
    return 0;
}
static int vfa_queued(void) { return (LONG)((ULONG)s_vfa.taken - (ULONG)s_vfa.posted) < 0; }
static DWORD WINAPI vfa_worker(LPVOID arg)
{
    (void)arg;
    if (!s_vfa.wake) { for (;;) { WaitForSingleObject(s_vfa.sem, INFINITE); vfa_run_one(0); } }
    for (;;) {
        while (vfa_run_one(0)) {}
        if (s_vfa.spin > 0 && InterlockedCompareExchange(&s_vfa.spinning, 1, 0) == 0) {
            int got = 0;
            for (int i = 0; i < s_vfa.spin && !got; i++) { _mm_pause(); got = vfa_queued(); }
            InterlockedExchange(&s_vfa.spinning, 0);
            if (got) continue;
        }
        InterlockedIncrement(&s_vfa.sleepers);
        if (vfa_queued() && vfa_claim_sleeper()) continue;
        WaitForSingleObject(s_vfa.sem, INFINITE);
    }
}
static u32 wakes;
static void vfa_sync(void)
{
    if (s_vfa.posted == s_vfa.base) return;
    if (s_vfa.done != s_vfa.posted) { while (vfa_run_one(!s_vfa.wake)) {} while (s_vfa.done != s_vfa.posted) _mm_pause(); }
    MemoryBarrier();
    /* all of this sync's jobs must have run */
    for (LONG t = s_vfa.base; t != s_vfa.posted; t++) if (s_vfa.jobs[(u32)t % VFA_JOBS].ran != 1) bad++;
    if (s_vfa.wake) s_vfa.base = s_vfa.posted; else s_vfa.posted = s_vfa.taken = s_vfa.done = 0;
}
static void vfa_post(void)
{
    if ((ULONG)s_vfa.posted - (ULONG)s_vfa.base >= VFA_JOBS) vfa_sync();
    Job* j = &s_vfa.jobs[(u32)s_vfa.posted % VFA_JOBS];
    j->stamp = (u32)s_vfa.posted; j->ran = 0;
    MemoryBarrier();
    InterlockedIncrement(&s_vfa.posted);
    if (!s_vfa.wake || vfa_claim_sleeper()) { ReleaseSemaphore(s_vfa.sem, 1, NULL); wakes++; }
}
int main(int argc, char** argv)
{
    s_vfa.wake = atoi(argv[1]); s_vfa.spin = atoi(argv[2]); long n = atol(argv[3]);
    /* start near the LONG wrap so the wrap-safe compares are exercised */
    if (s_vfa.wake) s_vfa.posted = s_vfa.taken = s_vfa.done = s_vfa.base = 0x7FFFFFFF - 100000;
    s_vfa.jobs = (Job*)calloc(VFA_JOBS, sizeof(Job));
    for (int i = 0; i < VFA_JOBS; i++) s_vfa.jobs[i].stamp = 0xFFFFFFFFu;
    s_vfa.sem = CreateSemaphoreA(NULL, 0, 1 << 30, NULL);
    for (int i = 0; i < 3; i++) CloseHandle(CreateThread(NULL, 0, vfa_worker, NULL, 0, NULL));
    u32 r = 99, syncs = 0;
    for (long i = 0; i < n; i++) {
        r = r * 1664525u + 1013904223u;
        vfa_post();
        if ((r >> 24) < 8) { vfa_sync(); syncs++; }
        if ((r >> 20 & 0xFF) < 2) Sleep(0);
        if ((r >> 12 & 0xFF) < 4) { volatile int k = 0; for (int q = 0; q < 2000; q++) k++; }
    }
    vfa_sync();
    printf("wake %d spin %d: %ld jobs, %u syncs, %u wakes, %ld errors\n", s_vfa.wake, s_vfa.spin, n, syncs, wakes, (long)bad);
    return bad ? 1 : 0;
}
