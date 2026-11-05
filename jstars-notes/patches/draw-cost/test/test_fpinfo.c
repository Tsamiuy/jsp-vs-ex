/* FP_INFO_CACHE check: fp_info() (cut verbatim from the patched
 * rsx_d3d12_backend.c into fpi.inc) must give, on every call, the same code
 * hash, size, gamma units (for random masks), TXP units and inline constants
 * as the original functions, while programs are re-patched (constants), edited
 * (code, END moved), moved to new addresses and replaced at the same address;
 * then the cost per draw of the answers with and without the cache. Links the
 * real rsx_fp_decompiler.c. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "rsx_fp_decompiler.h"

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#define FP_MAX_BYTES      16384
#define VP_FPCB_STRIDE    4096
#define FP_MAX_KCONST     ((VP_FPCB_STRIDE / 16) - 5)
uint8_t* vm_base;
u32 cellGcmResolveLocated(int local, u32 offset) { (void)local; return offset; }
#include "fpi.inc"

/* The backend's fp_txp_units loop as it was before the patch. */
static u32 txp_orig(const u8* uc)
{
    u32 units = 0;
    for (u32 o = 0; o + 16 <= FP_MAX_BYTES;) {
        const u32 w0 = rsx_fp_read_word(uc + o), w1 = rsx_fp_read_word(uc + o + 4);
        const u32 w2 = rsx_fp_read_word(uc + o + 8), w3 = rsx_fp_read_word(uc + o + 12);
        if (!(w2 & (1u << 31)) && ((w0 >> 24) & 63u) == 0x18u) units |= 1u << ((w0 >> 17) & 15u);
        o += 16;
        if (!(w2 & (1u << 31)) && ((w1 & 3u) == 2 || (w2 & 3u) == 2 || (w3 & 3u) == 2)) o += 16;
        if (w0 & 1u) break;
    }
    return units;
}

static u32 rng = 4242u;
static u32 rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static u32 rdw(const u8* p) { u32 be = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; return (be << 16) | (be >> 16); }
static void wrw(u8* p, u32 w) { u32 be = (w << 16) | (w >> 16); p[0] = (u8)(be >> 24); p[1] = (u8)(be >> 16); p[2] = (u8)(be >> 8); p[3] = (u8)be; }
static int has_k(const u8* q) { u32 w1 = rdw(q + 4), w2 = rdw(q + 8), w3 = rdw(q + 12);
    if (w2 & 0x80000000u) return 0; return (w1 & 3u) == 2u || (w2 & 3u) == 2u || (w3 & 3u) == 2u; }
static u32 make_prog(u8* p, u32 n)
{
    u32 off = 0;
    for (u32 i = 0; i < n && off + 32 <= FP_MAX_BYTES; i++) {
        for (int b = 0; b < 16; b++) p[off + b] = (u8)rnd();
        u32 w0 = rdw(p + off) & ~1u;
        if ((rnd() & 3) == 0) w0 = (w0 & ~(63u << 24)) | ((u32)(0x17 + rnd() % 4) << 24);  /* TEX/TXP-family ops */
        if (i == n - 1) w0 |= 1u;
        wrw(p + off, w0);
        int k = has_k(p + off); off += 16;
        if (k) { for (int b = 0; b < 16; b++) p[off + b] = (u8)rnd(); off += 16; }
    }
    return off;
}
static u32 kslots(const u8* p, u32 len, u32* out)
{
    u32 off = 0, n = 0;
    while (off + 16 <= len) { int k = has_k(p + off); u32 e = rdw(p + off) & 1u; off += 16;
        if (k) { out[n++] = off; off += 16; } if (e) break; }
    return n;
}

int main(void)
{
    enum { MEM = 8 << 20, NADDR = 3000 };
    vm_base = (u8*)calloc(MEM + FP_MAX_BYTES, 1);
    static u8 prog[64][FP_MAX_BYTES]; static u32 plen[64];
    for (int i = 0; i < 64; i++) plen[i] = make_prog(prog[i], 4 + rnd() % 240);
    static u32 at_prog[NADDR];                         /* which program each address slot holds */
    for (int a = 0; a < NADDR; a++) at_prog[a] = 0xFFFFFFFFu;
    static float ka[FP_MAX_KCONST * 4], kb[FP_MAX_KCONST * 4];
    static u32 ks[2048];
    long calls = 0, bad = 0, hits0 = 0;
    for (int it = 0; it < 400000; it++) {
        const int a = rnd() % NADDR;                   /* program slot address: a * 2 KB, overlapping */
        u8* p = vm_base + (u32)a * 2048u;
        u32 r = rnd() % 1000;
        if (at_prog[a] == 0xFFFFFFFFu || r < 20) {     /* (re)place a program here */
            at_prog[a] = rnd() % 64; memcpy(p, prog[at_prog[a]], plen[at_prog[a]]);
        } else if (r < 23) { p[(rnd() % 64) * 16 + 4 + rnd() % 12] ^= (u8)(1 + rnd() % 255); }   /* code edit */
        else if (r < 24) { u32 o = (rnd() % 32) * 16; wrw(p + o, rdw(p + o) ^ 1u); }           /* END moved */
        { u32 n = kslots(p, FP_MAX_BYTES, ks);           /* constants re-patched */
          for (u32 c = 0; c < n; c++) if (rnd() & 1) for (int b = 0; b < 16; b++) p[ks[c] + b] = (u8)rnd(); }
        const FpInfo* fi = fp_info((u32)a * 2048u | 1u);
        if (!fi) { bad++; continue; }
        u8 m[16]; for (int u = 0; u < 16; u++) m[u] = (rnd() & 1) ? (u8)(rnd() & 15) : 0;
        u32 g = 0; for (int u = 0; u < 16; u++) if (((fi->tex_units >> u) & 1u) && m[u]) g |= 1u << u;
        const int na = fpi_consts(fi, ka, FP_MAX_KCONST);
        memset(kb, 0, sizeof kb);
        const int nb = rsx_fp_extract_consts(p, FP_MAX_BYTES, kb, FP_MAX_KCONST);
        int ok = fi->hash == rsx_fp_code_hash(p, FP_MAX_BYTES) &&
                 fi->size == rsx_fp_program_size(p, FP_MAX_BYTES) &&
                 g == rsx_fp_texture_gamma_units(p, FP_MAX_BYTES, m) &&
                 fi->txp_units == txp_orig(p) &&
                 na == nb && memcmp(ka, kb, (size_t)(na < FP_MAX_KCONST ? na : FP_MAX_KCONST) * 16) == 0;
        if (!ok) { if (bad < 5) printf("MISMATCH it %d addr %d\n", it, a); bad++; }
        calls++;
    }
    hits0 = s_fpi_hit;
    printf("fp_info vs the original functions: %ld mismatches in %ld calls (%u hits, %u misses)\n",
           bad, calls, s_fpi_hit, s_fpi_miss);

    /* Cost per draw: the answers a draw needs (record: constants; prepass and replay: hash+size,
     * gamma, TXP twice each) for a 235-instruction program whose constants change every draw. */
    u8* p = vm_base + 4096u * 1024u; u32 len = make_prog(p, 235); u32 n = kslots(p, len, ks);
    u8 m[16]; for (int u = 0; u < 16; u++) m[u] = 0x7;
    LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f); volatile u32 sink = 0; const int N = 20000;
    double tc[2];
    for (int pass = 0; pass < 2; pass++) {
        QueryPerformanceCounter(&t0);
        for (int k = 0; k < N; k++) {
            if (n) memset(p + ks[k % n], k, 16);
            if (pass == 0) {
                sink += (u32)rsx_fp_extract_consts(p, FP_MAX_BYTES, ka, FP_MAX_KCONST);
                for (int r2 = 0; r2 < 2; r2++)
                    sink += rsx_fp_code_hash(p, FP_MAX_BYTES) + rsx_fp_program_size(p, FP_MAX_BYTES) +
                            rsx_fp_texture_gamma_units(p, FP_MAX_BYTES, m) + txp_orig(p);
            } else {
                const FpInfo* fi = fp_info(4096u * 1024u | 1u);
                sink += (u32)fpi_consts(fi, ka, FP_MAX_KCONST);
                for (int r2 = 0; r2 < 2; r2++) { const FpInfo* f2 = fp_info(4096u * 1024u | 1u);
                    sink += f2->hash + f2->size + f2->tex_units + f2->txp_units; }
            }
        }
        QueryPerformanceCounter(&t1);
        tc[pass] = (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart * 1e6 / N;
    }
    printf("per draw, 235-instruction program with %u constants: original %.2f us, cached %.2f us\n", n, tc[0], tc[1]);
    (void)hits0;
    return bad ? 1 : 0;
}
