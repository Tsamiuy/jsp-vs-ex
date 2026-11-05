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
        if (!same) bad++;
        if (trial < 3 || !same)
            printf("trial %d: %u indices; padded strip %u vertices, indexed %u vertices + %u indices; "
                   "triangles %u / %u %s\n", trial, NI, nn, nv, nix, na, nb, same ? "identical" : "DIFFERENT");
        free(ib); free(nat); free(vl); free(ix); free(ixv); free(ta); free(tb);
    }
    printf("%d of 50 trials differ\n", bad);
    return bad != 0;
}
