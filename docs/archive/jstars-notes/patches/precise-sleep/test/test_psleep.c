/* PS3_PRECISE_SLEEP check: for the short sleeps J-Stars uses (20, 100, 200 us), the
 * old SwitchToThread spin vs the high-resolution waitable timer: how long each call
 * really sleeps (never shorter than asked?) and how much CPU it burns. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <windows.h>
#pragma comment(lib, "winmm.lib")
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
static LARGE_INTEGER f;

static void spin_sleep(uint64_t usec)      /* the old path */
{
    LARGE_INTEGER a, n; QueryPerformanceCounter(&a);
    const int64_t dl = a.QuadPart + (int64_t)((usec * (uint64_t)f.QuadPart) / 1000000ULL);
    do { SwitchToThread(); QueryPerformanceCounter(&n); } while (n.QuadPart < dl);
}
static HANDLE t;
static void timer_sleep(uint64_t usec)     /* the new path (tail 0) */
{
    LARGE_INTEGER due; due.QuadPart = -((LONGLONG)usec * 10);
    SetWaitableTimerEx(t, &due, 0, NULL, NULL, NULL, 0);
    WaitForSingleObject(t, INFINITE);
}
static double cpu_ms(void)
{
    FILETIME c, e, k, u; GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u);
    ULARGE_INTEGER kk = { .LowPart = k.dwLowDateTime, .HighPart = k.dwHighDateTime };
    ULARGE_INTEGER uu = { .LowPart = u.dwLowDateTime, .HighPart = u.dwHighDateTime };
    return (double)(kk.QuadPart + uu.QuadPart) / 10000.0;
}
static int cmpd(const void* a, const void* b) { double x = *(const double*)a, y = *(const double*)b; return x < y ? -1 : x > y; }
static void run(const char* name, void (*fn)(uint64_t), uint64_t us, int n)
{
    LARGE_INTEGER a, b; double mn = 1e9, mx = 0, sum = 0; int shorter = 0; static double v[4000];
    const double c0 = cpu_ms(); LARGE_INTEGER w0, w1; QueryPerformanceCounter(&w0);
    for (int i = 0; i < n; i++) {
        QueryPerformanceCounter(&a); fn(us); QueryPerformanceCounter(&b);
        const double d = (double)(b.QuadPart - a.QuadPart) * 1e6 / f.QuadPart;
        if (d < mn) mn = d; if (d > mx) mx = d; sum += d; if (d < (double)us) shorter++; v[i] = d;
    }
    QueryPerformanceCounter(&w1);
    const double wall = (double)(w1.QuadPart - w0.QuadPart) * 1e3 / f.QuadPart, cpu = cpu_ms() - c0;
    qsort(v, n, sizeof v[0], cmpd);
    printf("  %-6s p50 %6.1f  p99 %7.1f  p99.9 %7.1f us\n", name, v[n/2], v[n*99/100], v[n*999/1000]);
    printf("  %-6s usleep(%3llu): mean %7.1f us (min %6.1f, max %7.1f), shorter than asked %d/%d, CPU %3.0f%% of a core\n",
           name, (unsigned long long)us, sum / n, mn, mx, shorter, n, 100.0 * cpu / wall);
}
int main(void)
{
    timeBeginPeriod(1);                     /* as jstars\port\main.cpp does */
    QueryPerformanceFrequency(&f);
    t = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!t) { printf("no high-resolution timer on this Windows\n"); return 1; }
    const uint64_t uss[] = { 20, 100, 200 };
    for (int k = 0; k < 3; k++) {
        run("spin", spin_sleep, uss[k], 2000);
        run("timer", timer_sleep, uss[k], 2000);
    }
    return 0;
}
