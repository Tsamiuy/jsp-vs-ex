/* FIFO_FAST check: the same random method stream through rsx_process_method
 * (the real rsx_commands.c) must leave the same vertex-constant state with
 * FIFO_FAST on and off. Run once per mode; the program prints a hash of the
 * state after each 10,000 words and of the final state. Also times both. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "rsx_commands.h"

static uint32_t rng = 777u;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 4; }
static uint64_t hash_state(const rsx_state* s)
{
    uint64_t h = 1469598103934665603ull;
    const uint8_t* p = (const uint8_t*)s->vertex_constants;
    for (size_t i = 0; i < sizeof s->vertex_constants; i++) h = (h ^ p[i]) * 1099511628211ull;
    h = (h ^ s->vertex_constants_lo) * 1099511628211ull;
    h = (h ^ s->vertex_constants_hi) * 1099511628211ull;
    h = (h ^ (uint32_t)s->vertex_constants_dirty) * 1099511628211ull;
    h = (h ^ s->transform_constant_load) * 1099511628211ull;
    for (size_t i = 0; i < sizeof s->vp_ucode; i++) h = (h ^ s->vp_ucode[i]) * 1099511628211ull;
    h = (h ^ s->vp_ucode_write) * 1099511628211ull;
    h = (h ^ s->vp_ucode_bytes) * 1099511628211ull;
    h = (h ^ s->vp_gen) * 1099511628211ull;
    h = (h ^ (uint32_t)s->vp_dirty) * 1099511628211ull;
    return h;
}

int main(void)
{
    static rsx_state st;
    rsx_state_init(&st);
    const char* m = getenv("FIFO_FAST");
    uint64_t chk = 0;
    LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    const int N = 4000000;
    for (int i = 0; i < N; i++) {
        uint32_t r = rnd() % 100, method, data = rnd() ^ (rnd() << 12);
        if (r < 6) { method = NV4097_SET_TRANSFORM_CONSTANT_LOAD; data = rnd() % 540; }      /* some past 512 */
        else if (r < 90) method = NV4097_SET_TRANSFORM_CONSTANT + (rnd() % 32) * 4;         /* the bulk */
        else if (r < 92) method = NV4097_SET_TRANSFORM_CONSTANT + 32 * 4 + (rnd() % 8) * 4; /* just past the window */
        else if (r < 94) method = 0x1FC0;                                                 /* neighbour */
        else if (r < 96) { method = 0x1C00 + (rnd() % 64) * 4; }                          /* DATA4F */
        else if (r < 97) method = 0x0A00 + (rnd() % 64) * 4;                              /* viewport etc. */
        else if (r < 98) { method = NV4097_SET_TRANSFORM_PROGRAM_LOAD; data = rnd() % 1100; } /* some past the store */
        else method = NV4097_SET_TRANSFORM_PROGRAM + (rnd() % 34) * 4;                    /* program words (+ past) */
        if ((i % 50000) == 49999) st.vertex_constants_dirty = 0;                           /* backend consumed */
        rsx_process_method(&st, method, data);
        if ((i % 1000) == 999) chk = (chk ^ hash_state(&st)) * 1099511628211ull;
    }
    QueryPerformanceCounter(&t1);
    printf("FIFO_FAST=%s: running hash %016llX, final %016llX, %.1f ns per word\n",
           m ? m : "(default)", (unsigned long long)chk, (unsigned long long)hash_state(&st),
           (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart * 1e9 / N);
    return 0;
}
