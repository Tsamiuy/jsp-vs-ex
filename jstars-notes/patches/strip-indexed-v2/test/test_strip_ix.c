/* STRIP_INDEXED check, offline: the padded native strip (upload_strip_vp_native's
 * list mode: joins repeat the last vertex, then the new one, padded to keep
 * parity) and the indexed strip with cuts (upload_strip_vp_native_ix) must
 * produce the SAME non-degenerate triangles, in the same order and winding,
 * when both are assembled with D3D's strip rules (odd triangles swap their
 * first two vertices; a cut index starts a new strip at even parity).
 * Streams: J-Stars-like 3-index pieces + 0xFFFF restarts on a shared vertex
 * pool (meshes reuse vertices), some longer strips, consecutive restarts,
 * fed in 256-index calls that continue one record. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <windows.h>
typedef uint32_t u32;

static u32 rng = 2468u;
static u32 rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

/* ---- padded native (list mode), copied logic ---- */
static int s_pending;
static u32 s_last_vi; static int s_last_ok;
static u32 native_call(const uint16_t* ib, u32 first, u32 count, u32 t0, u32* outv /* record-relative */)
{
    if (t0 == 0) { s_pending = 0; s_last_ok = 0; }
    u32 o = 0;
    for (u32 k = 0; k < count; k++) {
        u32 v = ib[first + k];
        if (v == 0xFFFF) { if (t0 + o) s_pending = 1; continue; }
        if (s_pending && t0 + o > 0) {
            if (o == 0) outv[o] = s_last_vi; else outv[o] = outv[o - 1];
            o++;
            outv[o] = v; o++;
            if ((t0 + o) & 1) { outv[o] = outv[o - 1]; o++; }
            outv[o] = outv[o - 1]; o++;
            s_pending = 0; continue;
        }
        s_pending = 0;
        outv[o] = v; o++;
    }
    if (o) { s_last_vi = outv[o - 1]; s_last_ok = 1; }
    return o;
}

/* ---- indexed with cuts, copied logic of upload_strip_vp_native_ix ---- */
enum { DM = 1u << 16, PROBE = 8 };
static u32 dm_tag[DM], dm_vi[DM], dm_loc[DM], tag = 1;
static u32 ix_call(const uint16_t* ib, u32 first, u32 count, u32 t0, u32* vl /* record vertex list */, u32* ixo, u32* ni_out)
{
    if (t0 == 0) tag++;
    u32 nv = 0, ni = 0;
    for (u32 k = 0; k < count; k++) {
        u32 v = ib[first + k];
        if (v == 0xFFFF) { ixo[ni++] = 0xFFFFFFFFu; continue; }
        u32 h = (v * 2654435761u) >> 16, loc = 0xFFFFFFFFu, freeslot = 0xFFFFFFFFu;
        for (u32 pz = 0; pz < PROBE; pz++) {
            const u32 s = (h + pz) & (DM - 1);
            if (dm_tag[s] != tag) { if (freeslot == 0xFFFFFFFFu) freeslot = s; continue; }
            if (dm_vi[s] == v) { loc = dm_loc[s]; break; }
        }
        if (loc == 0xFFFFFFFFu) {
            loc = t0 + nv;
            vl[t0 + nv] = v; nv++;
            if (freeslot != 0xFFFFFFFFu) { dm_tag[freeslot] = tag; dm_vi[freeslot] = v; dm_loc[freeslot] = loc; }
        }
        ixo[ni++] = loc;
    }
    *ni_out = ni;
    return nv;
}

/* ---- STRIP_INDEXED=2 logic: reserve count per call, per-call dedup (worker) ---- */
static u32 ix2_call(const uint16_t* ib, u32 first, u32 count, u32 t0, u32* vbuf /* record vertex slots */, u32* ixo)
{
    u32 raw[4096]; for (u32 k = 0; k < count; k++) raw[k] = ib[first + k] == 0xFFFF ? 0xFFFFFFFFu : ib[first + k];
    static u32 key[8192], val[8192], gen[8192], g; u32 vl[4096];
    u32 msz = 64; while (msz < count * 2u) msz <<= 1; const u32 mask = msz - 1; g++;
    u32 nv = 0;
    for (u32 i = 0; i < count; i++) {
        const u32 v = raw[i];
        if (v == 0xFFFFFFFFu) { ixo[i] = 0xFFFFFFFFu; continue; }
        u32 s = (v * 2654435761u) & mask, loc;
        for (;;) {
            if (gen[s] != g) { gen[s] = g; key[s] = v; val[s] = loc = nv; vl[nv++] = v; break; }
            if (key[s] == v) { loc = val[s]; break; }
            s = (s + 1) & mask;
        }
        ixo[i] = t0 + loc;
    }
    for (u32 k = 0; k < nv; k++) vbuf[t0 + k] = vl[k];   /* the fetched vertices */
    for (u32 k = nv; k < count; k++) vbuf[t0 + k] = 0xDEADBEEFu;   /* reserved, unused */
    return count;
}

/* ---- D3D strip assembly -> normalized triangles ---- */
typedef struct { u32 a, b, c; } Tri;
static void norm(Tri* t)
{   /* rotate so the smallest comes first; keeps winding */
    while (!(t->a <= t->b && t->a <= t->c)) { u32 x = t->a; t->a = t->b; t->b = t->c; t->c = x; }
}
static u32 assemble(const u32* idx, u32 n, Tri* out)   /* idx: guest vertex ids, 0xFFFFFFFF = cut */
{
    u32 nt = 0, run = 0; u32 w[2] = {0, 0};
    for (u32 i = 0; i < n; i++) {
        if (idx[i] == 0xFFFFFFFFu) { run = 0; continue; }
        if (run >= 2) {
            u32 p = run - 2;            /* triangle index within this strip */
            Tri t;
            if (p & 1) { t.a = w[1]; t.b = w[0]; t.c = idx[i]; }
            else       { t.a = w[0]; t.b = w[1]; t.c = idx[i]; }
            if (t.a != t.b && t.b != t.c && t.a != t.c) { norm(&t); out[nt++] = t; }
        }
        w[0] = w[1]; w[1] = idx[i]; run++;
    }
    return nt;
}

int main(void)
{
    int bad = 0;
    for (int trial = 0; trial < 50; trial++) {
        const u32 NI = 20000 + rnd() % 20000;
        uint16_t* ib = (uint16_t*)malloc(NI * 2);
        u32 pool = 500 + rnd() % 3000;
        for (u32 i = 0; i < NI; ) {
            u32 r = rnd() % 10;
            u32 len = r < 7 ? 3 : (r < 9 ? 4 + rnd() % 12 : 1 + rnd() % 2);   /* also short broken pieces */
            u32 base = rnd() % pool;
            for (u32 j = 0; j < len && i < NI; j++) ib[i++] = (uint16_t)((base + j * (1 + rnd() % 3)) % pool);
            if (i < NI) ib[i++] = 0xFFFF;
            if (rnd() % 20 == 0 && i < NI) ib[i++] = 0xFFFF;               /* double restart */
        }
        /* padded native */
        u32* nat = (u32*)malloc(NI * 4 * 4); u32 nn = 0;
        for (u32 f = 0; f < NI; f += 256) { u32 c = NI - f < 256 ? NI - f : 256; nn += native_call(ib, f, c, nn, nat + nn); }
        /* indexed */
        u32* vl = (u32*)malloc(NI * 4); u32* ix = (u32*)malloc(NI * 4 * 2);
        u32 nv = 0, nix = 0;
        for (u32 f = 0; f < NI; f += 256) { u32 c = NI - f < 256 ? NI - f : 256, ni = 0;
            nv += ix_call(ib, f, c, nv, vl, ix + nix, &ni); nix += ni; }
        /* indices -> guest ids for assembly */
        u32* ixv = (u32*)malloc(nix * 4);
        for (u32 i = 0; i < nix; i++) ixv[i] = ix[i] == 0xFFFFFFFFu ? 0xFFFFFFFFu : vl[ix[i]];
        Tri* ta = (Tri*)malloc(NI * 4 * sizeof(Tri)); Tri* tb = (Tri*)malloc(NI * 4 * sizeof(Tri));
        u32 na = assemble(nat, nn, ta), nb = assemble(ixv, nix, tb);
        int same = (na == nb) && !memcmp(ta, tb, na * sizeof(Tri));
        /* version 2 */
        u32* vb2 = (u32*)malloc(NI * 4); u32* ix2 = (u32*)malloc(NI * 4); u32 nv2 = 0, uniq2 = 0;
        for (u32 f = 0; f < NI; f += 256) { u32 c = NI - f < 256 ? NI - f : 256;
            u32 r = ix2_call(ib, f, c, nv2, vb2, ix2 + nv2); nv2 += r; }
        for (u32 k = 0; k < nv2; k++) if (vb2[k] != 0xDEADBEEFu) uniq2++;
        u32* ixv2 = (u32*)malloc(nv2 * 4); int badref = 0;
        for (u32 i = 0; i < nv2; i++) { if (ix2[i] == 0xFFFFFFFFu) { ixv2[i] = ix2[i]; continue; }
            if (vb2[ix2[i]] == 0xDEADBEEFu) badref = 1; ixv2[i] = vb2[ix2[i]]; }
        Tri* tc = (Tri*)malloc(NI * 4 * sizeof(Tri));
        u32 nc = assemble(ixv2, nv2, tc);
        int same2 = !badref && (na == nc) && !memcmp(ta, tc, na * sizeof(Tri));
        if (!same2) bad++;
        if (trial < 3 || !same2) printf("   v2: %u vertices fetched (%u slots reserved), triangles %u %s\n",
                                       uniq2, nv2, nc, same2 ? "identical" : "DIFFERENT");
        free(vb2); free(ix2); free(ixv2); free(tc);
        if (!same) bad++;
        if (trial < 3 || !same)
            printf("trial %d: %u indices; padded strip %u vertices, indexed %u vertices + %u indices; "
                   "triangles %u / %u %s\n", trial, NI, nn, nv, nix, na, nb, same ? "identical" : "DIFFERENT");
        free(ib); free(nat); free(vl); free(ix); free(ixv); free(ta); free(tb);
    }
    /* present-thread cost of each version's per-call loop (no vertex fetch) */
    { const u32 NI = 600000; uint16_t* ib = (uint16_t*)malloc(NI * 2);
      for (u32 i = 0; i < NI; ) { u32 base = rnd() % 30000; for (u32 j = 0; j < 3 && i < NI; j++) ib[i++] = (uint16_t)(base + j); if (i < NI) ib[i++] = 0xFFFF; }
      u32* a = (u32*)malloc(NI * 4 * 4); u32* b = (u32*)malloc(NI * 4); u32* c = (u32*)malloc(NI * 4 * 2);
      LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f); double tb = 0, t1v = 0, t2v = 0;
      for (int rep = 0; rep < 5; rep++) {
        u32 n = 0; QueryPerformanceCounter(&t0);
        for (u32 q = 0; q < NI; q += 256) { u32 cc = NI - q < 256 ? NI - q : 256; n += native_call(ib, q, cc, n, a + n); }
        QueryPerformanceCounter(&t1); tb += (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart;
        u32 nv = 0, nix = 0; QueryPerformanceCounter(&t0);
        for (u32 q = 0; q < NI; q += 256) { u32 cc = NI - q < 256 ? NI - q : 256, ni; nv += ix_call(ib, q, cc, nv, b, c + nix, &ni); nix += ni; }
        QueryPerformanceCounter(&t1); t1v += (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart;
        QueryPerformanceCounter(&t0);   /* v2 present thread: read + mark restarts + copy */
        for (u32 q = 0; q < NI; q += 256) { u32 cc = NI - q < 256 ? NI - q : 256;
            for (u32 k = 0; k < cc; k++) { u32 v = ib[q + k]; c[q + k] = v == 0xFFFF ? 0xFFFFFFFFu : v; } }
        QueryPerformanceCounter(&t1); t2v += (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart;
      }
      printf("present-thread loop per index (600k-index stream, 3+restart pieces): padded %.1f ns | v1 (dedup map) %.1f ns | v2 (copy) %.1f ns\n",
             tb * 1e9 / (5.0 * NI), t1v * 1e9 / (5.0 * NI), t2v * 1e9 / (5.0 * NI));
    }
    printf("%d of 50 trials differ\n", bad);
    return bad != 0;
}
