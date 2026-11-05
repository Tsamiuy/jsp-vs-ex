/* DRAW_SAMPLE offline check: the sampler block (ds_block.c, as pasted into the
 * backend) on a known workload. The "drawing thread" here spends ~50% in
 * spin_work (exe, called from frame_a), ~25% in ReleaseSemaphore/SetEvent
 * syscalls called from post_jobs (ntdll/kernel), ~25% blocked in a wait called
 * from wait_idle. Expected: exe ran ~2/4, ntdll ran ~1/4 with e1 = post_jobs,
 * ntdll idle ~1/4 with e1 = wait_idle. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned int u32; typedef unsigned long long u64;
static double perf_now(void)
{
    LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}
#include "ds_block.c"

static volatile double sink;
static HANDLE ev_a, ev_b;
__declspec(noinline) static void spin_work(double us)
{ double t = perf_now(), x = 1; while ((perf_now() - t) * 1e6 < us) x = x * 1.0000001 + 1e-9; sink = x; }
__declspec(noinline) static void frame_a(void) { spin_work(2000); }
__declspec(noinline) static void post_jobs(double us)
{ double t = perf_now(); while ((perf_now() - t) * 1e6 < us) { SetEvent(ev_a); ResetEvent(ev_a); } }
__declspec(noinline) static void wait_idle(void) { WaitForSingleObject(ev_b, 1); }
int main(void)
{
    ev_a = CreateEventA(NULL, TRUE, FALSE, NULL); ev_b = CreateEventA(NULL, FALSE, FALSE, NULL);
    timeBeginPeriod(1);
    _putenv("DRAW_SAMPLE=1"); _putenv("DRAW_SAMPLE_SEC=3"); _putenv("DRAW_SAMPLE_OUT=ds_test.txt");
    ds_start_once();
    printf("main 0x%llX frame_a 0x%llX post_jobs 0x%llX wait_idle 0x%llX spin_work 0x%llX\n",
           (u64)main - (u64)GetModuleHandleA(NULL), (u64)frame_a - (u64)GetModuleHandleA(NULL),
           (u64)post_jobs - (u64)GetModuleHandleA(NULL), (u64)wait_idle - (u64)GetModuleHandleA(NULL),
           (u64)spin_work - (u64)GetModuleHandleA(NULL));
    double t0 = perf_now();
    double ta = 0, tb = 0, tw = 0;
    while (perf_now() - t0 < 7.0) {
        double a = perf_now(); frame_a(); double b = perf_now(); post_jobs(1000); double w = perf_now(); wait_idle();
        ta += b - a; tb += w - b; tw += perf_now() - w; }
    double tt = ta + tb + tw;
    printf("measured: spin %.1f%% post %.1f%% wait %.1f%%\n", 100*ta/tt, 100*tb/tt, 100*tw/tt);
    return 0;
}
