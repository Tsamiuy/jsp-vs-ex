/* Offline test for the shader-hitches patch (jstars-notes\patches\shader-hitches).
 * Builds on any host C compiler; the parts under test are extracted from the
 * patched rsx_d3d12_backend.c by apply.py (pure.h: no D3D, no Win32).
 *
 *   gcc -std=c17 -O2 -Wall -Wextra -o test_shader_hitches test_shader_hitches.c && ./test_shader_hitches
 *
 * 1. Cache key: same text -> same key; one byte, the length or the profile
 *    changed -> a different key (both halves); the header check accepts the
 *    matching header and rejects every mismatch.
 * 2. Texture slot policy on a synthetic fight: a working set that drifts as
 *    the camera moves, more textures than fit in some phases. Counts, per
 *    policy, the uploads that would happen and the resources (re)created.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef uint8_t  u8;
typedef uint32_t u32;
typedef uint64_t u64;

/* The fields tex_scan_candidates reads, with the backend's names. */
typedef struct {
    void* res;
    void* up;
    u32 off, w, h, fmt;
    u32 csum;
    int cube;
    u32 levels;
    u32 key;
    int used;
    u32 vframe;
    u32 last_gen;
} VPTexSlot;

#include "pure.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static void test_key(void)
{
    static char a[20000], b[20000];
    for (int i = 0; i < 19999; i++) a[i] = (char)('a' + (i * 7 + i / 13) % 26);
    a[19999] = 0;
    memcpy(b, a, sizeof a);
    ShKey ka = shcache_key(a, 19999, "ps_5_0");
    ShKey kb = shcache_key(b, 19999, "ps_5_0");
    CHECK(ka.h1 == kb.h1 && ka.h2 == kb.h2 && ka.len == kb.len);
    /* one byte differs, deep inside */
    b[12345] = (char)(b[12345] == 'q' ? 'r' : 'q');
    kb = shcache_key(b, 19999, "ps_5_0");
    CHECK(ka.h1 != kb.h1 && ka.h2 != kb.h2);
    /* same text, other profile */
    kb = shcache_key(a, 19999, "vs_5_0");
    CHECK(ka.h1 != kb.h1 && ka.h2 != kb.h2);
    /* a prefix of the text */
    kb = shcache_key(a, 19998, "ps_5_0");
    CHECK(ka.h1 != kb.h1 && ka.h2 != kb.h2 && ka.len != kb.len);
    /* many near-identical programs: no two keys collide */
    enum { N = 5000 };
    static u64 seen1[N], seen2[N];
    char prog[256];
    int coll = 0;
    for (int i = 0; i < N; i++) {
        int n = snprintf(prog, sizeof prog, "float4 main(float2 uv : TEXCOORD0) : SV_Target { return float4(%d, %d, 0, 1); }", i, i * 3);
        ShKey k = shcache_key(prog, (u32)n, "ps_5_0");
        for (int j = 0; j < i; j++) if (seen1[j] == k.h1 || seen2[j] == k.h2) coll++;
        seen1[i] = k.h1; seen2[i] = k.h2;
    }
    CHECK(coll == 0);
    /* header check */
    ShCacheHdr h; memcpy(h.magic, "RSXC", 4); h.version = SHCACHE_VERSION;
    h.h1 = ka.h1; h.h2 = ka.h2; h.hlsl_len = ka.len; h.blob_len = 4096;
    CHECK(shcache_hdr_ok(&h, ka));
    { ShCacheHdr x = h; x.magic[0] = 'X';     CHECK(!shcache_hdr_ok(&x, ka)); }
    { ShCacheHdr x = h; x.version++;          CHECK(!shcache_hdr_ok(&x, ka)); }
    { ShCacheHdr x = h; x.h1 ^= 1;            CHECK(!shcache_hdr_ok(&x, ka)); }
    { ShCacheHdr x = h; x.h2 ^= 1;            CHECK(!shcache_hdr_ok(&x, ka)); }
    { ShCacheHdr x = h; x.hlsl_len--;         CHECK(!shcache_hdr_ok(&x, ka)); }
    { ShCacheHdr x = h; x.blob_len = 0;       CHECK(!shcache_hdr_ok(&x, ka)); }
    { ShCacheHdr x = h; x.blob_len = 1u << 30; CHECK(!shcache_hdr_ok(&x, ka)); }
    CHECK(sizeof(ShCacheHdr) == 32);
    printf("key/header: %s\n", fails ? "FAIL" : "ok");
}

/* --- synthetic fight ----------------------------------------------------- */
enum { SLOTS = 512, TEXN = 900, FRAMES = 2400 };
typedef struct { u32 key, w, h, fmt; } Tex;
static Tex s_tex[TEXN];
static VPTexSlot s_slots[SLOTS];
static u32 s_gen = 1;
static u32 rng = 12345u;
static u32 rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

/* Mirrors vp_upload_tex_slot_impl's slot choice: match -> reuse; else the
 * policy picks a free slot; a resource with other dims is recreated. */
static void bind(int policy, u32 pool_age, const Tex* t, u64* uploads, u64* creates, u64* noslot)
{
    int slot = -1, freeslot = -1;
    for (int i = 0; i < SLOTS; i++) {
        VPTexSlot* c = &s_slots[i];
        if (c->res && c->key == t->key && c->w == t->w && c->h == t->h && c->fmt == t->fmt) {
            if (!c->used) { c->used = 1; c->last_gen = s_gen; }
            return;                                     /* bound / csum-same */
        }
        if (!c->used && freeslot < 0) freeslot = i;
    }
    if (slot < 0 && freeslot >= 0 && policy > 0) {
        int es, fs, ls; u32 fa;
        tex_scan_candidates(s_slots, SLOTS, s_gen, t->w, t->h, t->fmt, 0, &es, &fs, &fa, &ls);
        freeslot = tex_pick_slot(policy, freeslot, es, fs, fa, ls, pool_age);
    }
    if (freeslot < 0) { (*noslot)++; return; }
    VPTexSlot* c = &s_slots[freeslot];
    if (c->res && (c->w != t->w || c->h != t->h || c->fmt != t->fmt)) c->res = NULL;
    if (!c->res) { c->res = (void*)1; (*creates)++; }
    (*uploads)++;
    c->key = t->key; c->w = t->w; c->h = t->h; c->fmt = t->fmt; c->used = 1; c->last_gen = s_gen;
}

static void run_fight(int policy, u32 pool_age, u64* uploads, u64* creates, u64* noslot)
{
    memset(s_slots, 0, sizeof s_slots); s_gen = 1; rng = 12345u;
    *uploads = *creates = *noslot = 0;
    for (int f = 0; f < FRAMES; f++) {
        for (int i = 0; i < SLOTS; i++) s_slots[i].used = 0;
        s_gen++;
        /* The visible window drifts over the stage's textures; the
         * characters' ~120 textures are always bound; now and then the camera
         * swings and a different window appears (an attack, a cutscene). */
        int base = (f / 40) * 25 % (TEXN - 400);
        if ((f / 300) & 1) base = (base + 380) % (TEXN - 400);
        for (int i = 0; i < 120; i++) bind(policy, pool_age, &s_tex[i], uploads, creates, noslot);
        for (int i = 0; i < 260; i++) bind(policy, pool_age, &s_tex[120 + (base + i) % (TEXN - 120)], uploads, creates, noslot);
        for (int i = 0; i < 30; i++)  bind(policy, pool_age, &s_tex[120 + rnd() % (TEXN - 120)], uploads, creates, noslot);
    }
}

static void test_policy(void)
{
    static const u32 dims[6][2] = { {512,512}, {256,256}, {1024,1024}, {128,128}, {512,256}, {64,64} };
    for (int i = 0; i < TEXN; i++) {
        s_tex[i].key = 0x100000u + (u32)i * 0x8000u;
        int d = (i * 7) % 6;
        s_tex[i].w = dims[d][0]; s_tex[i].h = dims[d][1];
        s_tex[i].fmt = (i % 3) ? 0x86 : 0x85;
    }
    u64 up[3], cr[3], ns[3];
    run_fight(0, 4, &up[0], &cr[0], &ns[0]);
    run_fight(1, 4, &up[1], &cr[1], &ns[1]);
    run_fight(2, 4, &up[2], &cr[2], &ns[2]);
    printf("slot policy over %d frames, %d textures, %d slots:\n", FRAMES, TEXN, SLOTS);
    printf("  first-free (old):   uploads %6llu  creates %6llu  no-slot %llu\n", (unsigned long long)up[0], (unsigned long long)cr[0], (unsigned long long)ns[0]);
    printf("  TEX_LRU=1:          uploads %6llu  creates %6llu  no-slot %llu\n", (unsigned long long)up[1], (unsigned long long)cr[1], (unsigned long long)ns[1]);
    printf("  TEX_POOL=1 (age 4): uploads %6llu  creates %6llu  no-slot %llu\n", (unsigned long long)up[2], (unsigned long long)cr[2], (unsigned long long)ns[2]);
    CHECK(ns[0] == 0 && ns[1] == 0 && ns[2] == 0);
    CHECK(up[1] <= up[0]);            /* LRU never uploads more than first-free */
    CHECK(cr[2] <= cr[1]);            /* the pool never creates more than plain LRU */
    CHECK(cr[2] * 2 < cr[0]);         /* and far fewer than the old rule */
    /* Policy functions on their own. */
    CHECK(tex_pick_slot(0, 7, 3, 5, 10, 9, 4) == 7);
    CHECK(tex_pick_slot(1, 7, 3, 5, 10, 9, 4) == 3);
    CHECK(tex_pick_slot(1, 7, -1, 5, 10, 9, 4) == 9);
    CHECK(tex_pick_slot(2, 7, -1, 5, 10, 9, 4) == 5);
    CHECK(tex_pick_slot(2, 7, -1, 5, 3, 9, 4) == 9);   /* fit too young: oldest instead */
    CHECK(tex_pick_slot(2, 7, -1, -1, 0, -1, 4) == 7);
    CHECK(tex_pick_slot(1, -1, -1, -1, 0, -1, 4) == -1);
    {   /* candidates: used slots never offered; ages compared against gen */
        VPTexSlot s[4] = {0};
        s[0].res = (void*)1; s[0].used = 1; s[0].last_gen = 1; s[0].w = 8; s[0].h = 8; s[0].fmt = 1;
        s[1].res = (void*)1; s[1].used = 0; s[1].last_gen = 5; s[1].w = 8; s[1].h = 8; s[1].fmt = 1;
        s[2].res = (void*)1; s[2].used = 0; s[2].last_gen = 2; s[2].w = 4; s[2].h = 4; s[2].fmt = 1;
        s[3].res = NULL;     s[3].used = 0;
        int es, fs, ls; u32 fa;
        tex_scan_candidates(s, 4, 10, 8, 8, 1, 0, &es, &fs, &fa, &ls);
        CHECK(es == 3 && fs == 1 && fa == 5 && ls == 2);
        s[3].used = 1;
        tex_scan_candidates(s, 4, 10, 4, 4, 1, 0, &es, &fs, &fa, &ls);
        CHECK(es == -1 && fs == 2 && fa == 8 && ls == 2);
    }
}

int main(void)
{
    test_key();
    test_policy();
    printf("%s\n", fails ? "FAILED" : "all tests passed");
    return fails ? 1 : 0;
}
