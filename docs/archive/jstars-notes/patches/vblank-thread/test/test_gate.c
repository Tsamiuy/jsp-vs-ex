/* Present-gate check: a model of VBLANK_THREAD=1 with the gate as patched
 * (gcm_flip_present_blocked copied). Tick thread: every 16.667 ms, if a flip is
 * WAITING and the gate is open, DONE. Game: wait DONE -> request (ppu count++)
 * -> wait one tick -> 1 ms build -> loop. Drawer: frame k is "presented" D ms
 * after its request (D per phase), then reports presented = k.
 * Checks: when the game requests flip k+1, flip k is presented (run-ahead <= 1
 * frame), and flip intervals follow the drawing time: ~33 ms for D = 20, ~50
 * for D = 40; a drawer that stops for 300 ms is released after ~100 ms. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
typedef unsigned int u32; typedef int s32;
static unsigned long long ps3_ms_now(void) { return GetTickCount64(); }
static double now_ms(void) { LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return c.QuadPart * 1000.0 / f.QuadPart; }

static volatile u32 s_flip_ppu_requests = 0;
/* ---- as patched ---- */
static volatile u32 s_flip_presented = 0;
static int s_flip_gate_present = 0;
static int gcm_flip_present_blocked(void)
{
    if (!s_flip_gate_present || (s32)(s_flip_ppu_requests - s_flip_presented) <= 0) return 0;
    { static unsigned long long since = 0; static u32 owed_at = 0; static int lim = 100;
      const unsigned long long now = ps3_ms_now();
      if (owed_at != s_flip_ppu_requests) { owed_at = s_flip_ppu_requests; since = now; }
      else if (now - since >= (unsigned long long)lim) {
          static int n = 0;
          if (n++ < 8) fprintf(stderr, "[gate] %d flip(s) not presented for %d ms, releasing\n",
                               (int)(s_flip_ppu_requests - s_flip_presented), lim);
          s_flip_presented = s_flip_ppu_requests;
          return 0;
      } }
    return 1;
}
/* ---- model ---- */
static volatile LONG waiting = 0, done = 1, ticks = 0;
static volatile double req_t[100000];
static volatile double D = 20.0, stall_at = -1;
static DWORD WINAPI ticker(LPVOID a)
{
    (void)a; double next = now_ms() + 16.6667;
    for (;;) {
        while (now_ms() < next) Sleep(0);
        next += 16.6667;
        if (waiting && !gcm_flip_present_blocked()) { waiting = 0; done = 1; }
        InterlockedIncrement(&ticks);
    }
}
static DWORD WINAPI drawer(LPVOID a)
{
    (void)a; u32 k = 0;
    for (;;) {
        if (k < s_flip_ppu_requests) {
            double due = req_t[k] + D;
            if (stall_at >= 0 && k == (u32)stall_at) due += 300.0;
            if (now_ms() >= due) { k++; if ((s32)(k - s_flip_presented) > 0) s_flip_presented = k; continue; }
        }
        Sleep(0);
    }
}
int main(void)
{
    timeBeginPeriod(1);
    s_flip_gate_present = 1;
    CloseHandle(CreateThread(NULL, 0, ticker, NULL, 0, NULL));
    CloseHandle(CreateThread(NULL, 0, drawer, NULL, 0, NULL));
    int bad = 0; double last = 0, sum = 0; int n = 0;
    for (u32 k = 0; k < 400; k++) {
        if (k == 150) { printf("D=20: mean interval %.1f ms over %d\n", sum / n, n); sum = 0; n = 0; D = 40.0; }
        if (k == 300) { printf("D=40: mean interval %.1f ms over %d\n", sum / n, n); sum = 0; n = 0; D = 20.0; stall_at = 320; }
        while (!done) Sleep(0);
        done = 0;
        /* run-ahead: every earlier flip must be presented (or released) by now */
        if (k >= 1 && s_flip_presented < k) { if (bad < 5) printf("flip %u requested with only %u presented\n", k, s_flip_presented); bad++; }
        const double t = now_ms();
        if (k > 2) { sum += t - last; n++; }
        last = t;
        req_t[k] = t;
        s_flip_ppu_requests = k + 1;
        waiting = 1;
        LONG t0 = ticks; while (ticks == t0) Sleep(0);   /* wait a vblank */
        Sleep(1);                                         /* build */
    }
    printf("last 100 (one 300 ms stall): mean interval %.1f ms; run-ahead violations %d\n", sum / n, bad);
    return 0;
}
