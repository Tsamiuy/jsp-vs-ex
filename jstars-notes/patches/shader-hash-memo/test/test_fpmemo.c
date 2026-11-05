/* SHADER_HASH_MEMO check: rsx_fp_code_hash_memo must return rsx_fp_code_hash
 * (and rsx_fp_program_size) on every call while fragment programs are copied
 * into a ring at changing addresses, their inline constants re-patched, their
 * code changed in place, cut short or left without END; then the cost per call
 * for J-Stars-sized programs. Links the patched rsx_fp_decompiler.c. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
typedef uint8_t u8; typedef uint32_t u32;
u32 rsx_fp_code_hash(const u8* ucode, u32 max_bytes);
u32 rsx_fp_program_size(const u8* ucode, u32 max_bytes);
u32 rsx_fp_code_hash_memo(const u8* ucode, u32 max_bytes, u32* size_out);

static u32 rng = 12345u;
static u32 rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static u32 rdw(const u8* p) { u32 be = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; return (be << 16) | (be >> 16); }
static void wrw(u8* p, u32 w) { u32 be = (w << 16) | (w >> 16); p[0] = (u8)(be >> 24); p[1] = (u8)(be >> 16); p[2] = (u8)(be >> 8); p[3] = (u8)be; }
static int has_k(const u8* q) {
    u32 w1 = rdw(q + 4), w2 = rdw(q + 8), w3 = rdw(q + 12);
    if (w2 & 0x80000000u) return 0;
    return (w1 & 3u) == 2u || (w2 & 3u) == 2u || (w3 & 3u) == 2u;
}
/* A program of n instructions: random words, END only on the last; returns bytes. */
static u32 make_prog(u8* p, u32 n, u32 cap) {
    u32 off = 0;
    for (u32 i = 0; i < n && off + 16 <= cap; i++) {
        for (int b = 0; b < 16; b++) p[off + b] = (u8)rnd();
        u32 w0 = rdw(p + off) & ~1u; if (i == n - 1) w0 |= 1u; wrw(p + off, w0);
        int k = has_k(p + off);
        off += 16;
        if (k && off + 16 <= cap) { for (int b = 0; b < 16; b++) p[off + b] = (u8)rnd(); off += 16; }
    }
    return off;
}
/* Constant slots of a program (offsets), to re-patch them like the game does. */
static u32 const_slots(const u8* p, u32 len, u32* out) {
    u32 off = 0, n = 0;
    while (off + 16 <= len) { int k = has_k(p + off); u32 end = rdw(p + off) & 1u; off += 16;
        if (k) { if (off + 16 <= len) out[n++] = off; off += 16; } if (end) break; }
    return n;
}

int main(void)
{
    enum { NPROG = 40, RING = 1 << 20, MAXB = 16384 };
    static u8 prog[NPROG][MAXB]; static u32 plen[NPROG];
    for (int i = 0; i < NPROG; i++) plen[i] = make_prog(prog[i], 8 + rnd() % 240, MAXB);
    u8* ring = (u8*)malloc(RING + MAXB); memset(ring, 0, RING + MAXB);
    u32 rpos = 0; long calls = 0, bad = 0, badsz = 0;
    static u32 ks[2048];
    for (int it = 0; it < 300000; it++) {
        int i = rnd() % NPROG; u32 r = rnd() % 1000;
        if (r < 2) plen[i] = make_prog(prog[i], 8 + rnd() % 240, MAXB);            /* a new program */
        else if (r < 4) { u32 o = (rnd() % (plen[i] / 16)) * 16; prog[i][o + 4 + rnd() % 12] ^= (u8)(1 + rnd() % 255); } /* code edit */
        else if (r < 5) { u32 o = (rnd() % (plen[i] / 16)) * 16; u32 w0 = rdw(prog[i] + o); wrw(prog[i] + o, w0 ^ 1u); } /* END moved */
        u32 n = const_slots(prog[i], plen[i], ks);
        for (u32 c = 0; c < n; c++) if (rnd() & 1) for (int b = 0; b < 16; b++) prog[i][ks[c] + b] = (u8)rnd(); /* constants */
        if (rpos + plen[i] + 16 > RING) rpos = 0;
        u8* at = ring + rpos; memcpy(at, prog[i], plen[i]);
        memset(at + plen[i], (int)(rnd() & 0xFF), 16);        /* whatever follows in the ring */
        rpos += (plen[i] + 127) & ~127u;
        u32 maxb = (rnd() % 50 == 0) ? (rnd() % 64) * 16 : MAXB; /* sometimes cut short */
        if (maxb > RING + MAXB - rpos) maxb = MAXB;
        u32 sz = 0;
        u32 a = rsx_fp_code_hash_memo(at, maxb, &sz);
        if (a != rsx_fp_code_hash(at, maxb)) bad++;
        if (sz != rsx_fp_program_size(at, maxb)) badsz++;
        calls++;
    }
    printf("memo vs full hash: %ld hash mismatches, %ld size mismatches in %ld calls\n", bad, badsz, calls);

    /* Cost: a 235-instruction program (J-Stars' character shader size) and a 40-instruction one,
     * each at 64 ring addresses with constants re-patched per call. */
    u32 sizes[2] = { 235, 40 };
    for (int s = 0; s < 2; s++) {
        static u8 p[MAXB]; u32 len = make_prog(p, sizes[s], MAXB); u32 n = const_slots(p, len, ks);
        LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f);
        volatile u32 sink = 0; const int N = 50000; double tf, tm;
        for (int pass = 0; pass < 2; pass++) {
            QueryPerformanceCounter(&t0);
            for (int k = 0; k < N; k++) {
                u8* at = ring + (u32)(k % 64) * MAXB / 4; memcpy(at, p, len);
                if (n) memset(at + ks[k % n], k, 16);
                u32 z; sink += pass ? rsx_fp_code_hash_memo(at, MAXB, &z) : (rsx_fp_program_size(at, MAXB) + rsx_fp_code_hash(at, MAXB));
            }
            QueryPerformanceCounter(&t1);
            *(pass ? &tm : &tf) = (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart;
        }
        printf("%u instrs (%u B): size+hash %.2f us/call, memo %.2f us/call (both include a %u B copy)\n",
               sizes[s], len, tf * 1e6 / N, tm * 1e6 / N, len);
    }
    return (bad || badsz) ? 1 : 0;
}
