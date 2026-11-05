/* VFETCH_ASYNC engine (vfa.inc, cut verbatim from the patched backend):
 * many small strip-sized jobs with sync points at random places, compared
 * byte for byte with synchronous fetches. Stresses the claim/help races. */
#include "rsx_vertex_fetch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <intrin.h>

u8* vm_base;
#define LOCAL_ADDR 0x40000000u
u32 cellGcmResolveOffset(u32 off) { return LOCAL_ADDR + off; }
u32 cellGcmResolveLocated(int local, u32 off) { (void)local; return LOCAL_ADDR + off; }

#define MAX_VP_VERTICES 1048576
typedef struct { float v[4]; } VPSlot;
static void (*g_rsx_sync_hook)(void);
static int  vf_legacy(void) { return 0; }
static u32  vf_input_mask(const rsx_state* s) { (void)s; return (1u<<0)|(1u<<2)|(1u<<3)|(1u<<8)|(1u<<14); }
static int  perf_on(void) { return 1; }
static double perf_now(void) { LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return (double)c.QuadPart / f.QuadPart; }
static double s_perf_vfa_wait;
static void vf_fetch(const rsx_state* st, const rsx_fetch_src* src, u32 n, VPSlot* out)
{
    rsx_fetch_plan p; rsx_fetch_plan_init(st, vf_input_mask(st), &p);
    rsx_fetch_plan_range(&p, src, 0, n, (float (*)[4])out);
}
static void vfa_sync(void);
#include "vfa.inc"

static u32 rng = 31337u;
static u32 rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

int main(void)
{
    _putenv("VFETCH_ASYNC=1");
    vm_base = (u8*)VirtualAlloc(NULL, 0x50000000ull, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    for (u32 p = 0; p < 0x2000000u; p += 4) { u32 r = rnd() ^ (rnd() << 12); memcpy(vm_base + LOCAL_ADDR + p, &r, 4); }
    rsx_state* st = (rsx_state*)calloc(1, sizeof *st);
    struct { int i; u32 type, size, off; } L[] = { {0, 2, 3, 0}, {2, 6, 1, 12}, {3, 4, 4, 16}, {8, 2, 2, 20}, {14, 6, 1, 52} };
    for (int k = 0; k < 5; k++) { rsx_vertex_attrib* a = &st->vertex_attribs[L[k].i];
        a->enabled = 1; a->type = L[k].type; a->size = L[k].size; a->stride = 64; a->offset = 0x200000u + L[k].off; }

    const u32 TOTAL = 900000;                 /* ~ one match frame */
    VPSlot* ref = (VPSlot*)malloc((size_t)TOTAL * 256);
    VPSlot* got = (VPSlot*)malloc((size_t)TOTAL * 256);
    u32* list = (u32*)malloc(TOTAL * 4);
    for (u32 i = 0; i < TOTAL; i++) list[i] = rnd() % 30000;
    int bad = 0; double tsync = 0, tasync = 0;
    for (int frame = 0; frame < 30; frame++) {
        LARGE_INTEGER f, a, b; QueryPerformanceFrequency(&f);
        /* reference: synchronous, same job boundaries */
        QueryPerformanceCounter(&a);
        for (u32 o = 0; o < TOTAL; ) {
            u32 n = 300 + rnd() % 500; if (o + n > TOTAL) n = TOTAL - o;
            rsx_fetch_src src = { 0, RSX_FETCH_VLIST, 0, NULL, list + o };
            vf_fetch(st, &src, n, ref + (u64)o * 16); o += n;
        }
        QueryPerformanceCounter(&b); tsync += (double)(b.QuadPart - a.QuadPart) / f.QuadPart;
        memset(got, 0xEE, (size_t)TOTAL * 256);
        u32 save = rng;
        QueryPerformanceCounter(&a);
        for (u32 o = 0; o < TOTAL; ) {
            u32 n = 300 + rnd() % 500; if (o + n > TOTAL) n = TOTAL - o;
            vfa_fetch_list(st, list + o, n, got + (u64)o * 16); o += n;
            if (rnd() % 97 == 0) g_rsx_sync_hook();   /* a label write */
        }
        vfa_sync();                                   /* render_frame */
        QueryPerformanceCounter(&b); tasync += (double)(b.QuadPart - a.QuadPart) / f.QuadPart;
        (void)save;
        if (memcmp(ref, got, (size_t)TOTAL * 256)) { bad++; printf("frame %d MISMATCH\n", frame); }
    }
    printf("async engine: %d threads, 30 frames x %u vertices: %s; sync %.1f ms/frame, async (posting + final sync) %.1f ms/frame\n",
           s_vfa.nthreads, TOTAL, bad ? "MISMATCH" : "byte-identical", tsync * 1e3 / 30, tasync * 1e3 / 30);
    return bad;
}
