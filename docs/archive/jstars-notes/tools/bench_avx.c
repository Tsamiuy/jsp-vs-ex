/* Cost of the SPU float helpers (spu_fa/fm/fma) and the PPU vmx_vmaddfp per call, built
 * without and with /arch:AVX2, plus a bit-exactness check of every result against the
 * exact reference (_ref) path. Same headers the game compiles. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>
#include <stdlib.h>
#include "spu_helpers.h"
#include "ppu_vmx.h"

static uint32_t rng = 12345u;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng; }
static uint32_t rndf(void) {            /* floats of every kind, mostly ordinary */
    uint32_t r = getenv("ORDINARY") ? 99u : rnd() % 100;
    uint32_t sign = rnd() & 0x80000000u;
    if (r < 3) return sign;                                   /* zero */
    if (r < 6) return sign | (rnd() & 0x007FFFFFu);           /* denormal */
    if (r < 8) return sign | 0x7F800000u | (rnd() & 0x7FFFFFu); /* inf/nan */
    return sign | ((60u + rnd() % 136u) << 23) | (rnd() & 0x7FFFFFu);
}

int main(void)
{
    enum { N = 4096, R = 400 };
    static u128 a[N], b[N], c[N], o[N];
    for (int i = 0; i < N; i++) for (int k = 0; k < 4; k++) { a[i]._u32[k] = rndf(); b[i]._u32[k] = rndf(); c[i]._u32[k] = rndf(); }
    long bad = 0; volatile uint32_t sink = 0;
    for (int i = 0; i < N; i++) {
        u128 x = spu_fma(a[i], b[i], c[i]), y = spu_fma_ref(a[i], b[i], c[i]);
        u128 p = spu_fm(a[i], b[i]),       q = spu_fm_ref(a[i], b[i]);
        u128 s = spu_fa(a[i], b[i]),       t = spu_fa_ref(a[i], b[i]);
        if (memcmp(&x, &y, 16) || memcmp(&p, &q, 16) || memcmp(&s, &t, 16)) bad++;
    }
    LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    for (int r = 0; r < R; r++) for (int i = 0; i < N; i++) { u128 v = spu_fma(a[i], b[i], c[(i + 7) & (N - 1)]); sink ^= v._u32[0] ^ v._u32[3]; }
    QueryPerformanceCounter(&t1);
    double t_fma = (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart * 1e9 / ((double)R * N);
    QueryPerformanceCounter(&t0);
    for (int r = 0; r < R; r++) for (int i = 0; i < N; i++) { u128 v = spu_fm(a[i], b[(i + 3) & (N - 1)]); sink ^= v._u32[1] ^ v._u32[2]; }
    QueryPerformanceCounter(&t1);
    double t_fm = (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart * 1e9 / ((double)R * N);
    /* vmx_vmaddfp on guest-order bytes, VSCR non-Java mode off (0) */
    static uint8_t va[N][16], vb[N][16], vc[N][16], vd[N][16];
    for (int i = 0; i < N; i++) for (int k = 0; k < 16; k++) { va[i][k] = (uint8_t)rnd(); vb[i][k] = (uint8_t)rnd(); vc[i][k] = (uint8_t)rnd(); }
    for (int i = 0; i < N; i++) for (int k = 0; k < 4; k++) {   /* ordinary floats, big-endian */
        uint32_t w[3] = { (0x3F000000u + (rnd() & 0x00FFFFFFu)), (0x3E000000u + (rnd() & 0x00FFFFFFu)), (0x3D000000u + (rnd() & 0x00FFFFFFu)) };
        for (int j = 0; j < 4; j++) { va[i][k*4+j] = (uint8_t)(w[0] >> (24 - 8*j)); vb[i][k*4+j] = (uint8_t)(w[1] >> (24 - 8*j)); vc[i][k*4+j] = (uint8_t)(w[2] >> (24 - 8*j)); }
    }
    QueryPerformanceCounter(&t0);
    for (int r = 0; r < R; r++) for (int i = 0; i < N; i++) vmx_vmaddfp(vd[i], va[i], vb[i], vc[i], 0x10000u);
    QueryPerformanceCounter(&t1);
    double t_vm = (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart * 1e9 / ((double)R * N);
    uint32_t h = 0; for (int i = 0; i < N; i++) for (int k = 0; k < 16; k++) h = h * 31u + vd[i][k];
#if defined(__AVX2__)
    const char* mode = "AVX2+FMA";
#else
    const char* mode = "SSE (current build)";
#endif
    printf("[%08X] %-20s spu_fma %.1f ns, spu_fm %.1f ns, vmx_vmaddfp %.1f ns per 4-lane op; spu fast-vs-ref mismatches %ld/%d; vmaddfp hash %08X\n",
           (unsigned)sink, mode, t_fma, t_fm, t_vm, bad, N, h);
    return 0;
}
