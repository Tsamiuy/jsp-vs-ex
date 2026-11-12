/* SPU_FP_CACHE check: the cached fingerprint (fpc.inc, cut verbatim from the
 * patched spu_workload.c) must equal the full FNV (fp.inc) on every call, while
 * job images are re-dispatched, moved, resized and rewritten in place; then the
 * cost of each, on a J-Stars-like mix (a few 4-48 KB images, ~2,200 dispatches/s). */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "fp.inc"
#include "fpc.inc"

static uint32_t rng = 777u;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

int main(void)
{
    enum { NIMG = 12 };
    uint8_t* img[NIMG]; size_t len[NIMG];
    for (int i = 0; i < NIMG; i++) {
        len[i] = 4096 + (rnd() % 45) * 1024;
        img[i] = (uint8_t*)malloc(256 * 1024);
        for (size_t k = 0; k < 256 * 1024; k++) img[i][k] = (uint8_t)rnd();
    }
    long bad = 0, calls = 0;
    for (int it = 0; it < 200000; it++) {
        int i = rnd() % NIMG;
        uint32_t r = rnd() % 1000;
        if (r < 3) img[i][rnd() % len[i]] ^= (uint8_t)(1 + rnd() % 255);   /* rewritten in place */
        else if (r < 5) len[i] = 4096 + (rnd() % 45) * 1024;                  /* other size, same address */
        else if (r < 6) { size_t off = (rnd() % 64) * 16; uint64_t a = spu_workload_fingerprint_cached(img[i] + off, len[i] - off);
                          if (a != spu_workload_fingerprint(img[i] + off, len[i] - off)) bad++; calls++; }  /* moved */
        uint64_t a = spu_workload_fingerprint_cached(img[i], len[i]);
        uint64_t b = spu_workload_fingerprint(img[i], len[i]);
        if (a != b) bad++;
        calls++;
    }
    printf("cached vs full FNV: %ld mismatches in %ld calls\n", bad, calls);

    LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f);
    volatile uint64_t sink = 0; const int N = 22000;   /* 10 s of dispatches */
    QueryPerformanceCounter(&t0);
    for (int it = 0; it < N; it++) { int i = it % 6; sink += spu_workload_fingerprint(img[i], len[i]); }
    QueryPerformanceCounter(&t1);
    double full = (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart;
    QueryPerformanceCounter(&t0);
    for (int it = 0; it < N; it++) { int i = it % 6; sink += spu_workload_fingerprint_cached(img[i], len[i]); }
    QueryPerformanceCounter(&t1);
    double cached = (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart;
    double avg = 0; for (int i = 0; i < 6; i++) avg += len[i]; avg /= 6;
    printf("%d dispatches, images ~%.0f KB: full FNV %.1f us/dispatch, cached %.2f us/dispatch\n",
           N, avg / 1024, full * 1e6 / N, cached * 1e6 / N);
    return bad != 0;
}
