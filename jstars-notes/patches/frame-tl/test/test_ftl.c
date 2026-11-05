/* FRAME_TL offline check: the block (ftl_block.c, as pasted into cellGcmSys.c)
 * driven by a model of J-Stars' loop with known times. Ticker: 60 Hz, sets
 * DONE on the first tick after a request (as cellGcmTickFlip does). Game:
 * poll DONE -> request -> wait a tick -> build (B ms: 2/3 CPU spin, 1/3 sleep)
 * -> poll. Drawing: walker marker D ms after each request.
 * Expected with B = 12 ms (every 4th frame 22 ms), D = 20 ms: build ~14.5
 * (cpu ~9.7), 1 in 4 frames over 16.7 and those intervals ~50 ms, walker ~20. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned int u32; typedef unsigned long long u64;
#include "ftl_block.c"

static volatile LONG done_flag = 1, waiting = 0, last_req_tick = 0;
static volatile LONG reqs = 0;
static volatile double walk_due[1024];
static void spin_ms(double ms) { double t = ftl_now(); while (ftl_now() - t < ms) {} }
static void build_ms(double ms) { spin_ms(ms * 2 / 3); double t = ftl_now(); while (ftl_now() - t < ms / 3) Sleep(1); }
static DWORD WINAPI ticker(LPVOID a)
{
    (void)a; double next = ftl_now() + 16.6667;
    for (;;) {
        while (ftl_now() < next) Sleep(0);
        next += 16.6667;
        ftl_tick();
        if (waiting) { waiting = 0; done_flag = 1; }
    }
}
static DWORD WINAPI drawer(LPVOID a)
{
    (void)a; LONG k = 0;
    for (;;) {
        if (k < reqs && ftl_now() >= walk_due[k % 1024]) { ftl_walk(); ftl_present(); k++; }
        else Sleep(0);
    }
}
int main(void)
{
    _putenv("FRAME_TL=1");
    timeBeginPeriod(1);
    CloseHandle(CreateThread(NULL, 0, ticker, NULL, 0, NULL));
    CloseHandle(CreateThread(NULL, 0, drawer, NULL, 0, NULL));
    for (int f = 0; f < 241; f++) {
        while (ftl_poll(done_flag), !done_flag) Sleep(0);
        done_flag = 0;
        ftl_request();
        walk_due[reqs % 1024] = ftl_now() + 20.0;
        reqs++;
        waiting = 1;
        /* vblank wait: until the ticker has run once after the request */
        { double t = ftl_now(); LONG seen = 0; while (!seen) { seen = s_ftl_tick > t; Sleep(0); } }
        build_ms((f % 4 == 3) ? 22.0 : 12.0);
    }
    return 0;
}
