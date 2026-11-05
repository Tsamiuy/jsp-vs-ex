/* Native strip path, per-vertex vs batched. Both loops are copied from the
 * patched upload_strip_vp_native (the restart/join logic is identical; only
 * the emit differs). A match-like index stream is fed in 256-index calls that
 * continue one record (t0 > 0, the pending join carried across calls), so the
 * o == 0 join (copy of the previous call's last vertex) is exercised. Output
 * must be byte-identical; then both are timed into a real D3D12 upload heap. */
#include "rsx_vertex_fetch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#define COBJMACROS
#include <d3d12.h>

u8* vm_base;
#define LOCAL_ADDR 0x40000000u
u32 cellGcmResolveOffset(u32 off) { return LOCAL_ADDR + off; }
u32 cellGcmResolveLocated(int local, u32 off) { (void)local; return LOCAL_ADDR + off; }

static void* upload_heap(u64 bytes)
{
    ID3D12Device* dev = NULL;
    if (FAILED(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&dev))) return NULL;
    D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd = {0};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = bytes; bd.Height = 1;
    bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* r = NULL;
    if (FAILED(ID3D12Device_CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &bd,
            D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void**)&r))) return NULL;
    void* p = NULL; D3D12_RANGE rr = {0, 0};
    if (FAILED(ID3D12Resource_Map(r, 0, &rr, &p))) return NULL;
    return p;
}

static u32 rng = 9001u;
static u32 rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
#define STRIDE 256u
typedef struct { float v[4]; } VPSlot;

static void one(const rsx_fetch_plan* plan, u32 v, VPSlot* dst)
{
    rsx_fetch_src s1 = { v, RSX_FETCH_LIST, 0, NULL, NULL };
    rsx_fetch_plan_range(plan, &s1, 0, 1, (float (*)[4])dst);
}

static int s_pending;
static u32 s_last_vi; static int s_last_ok; static int s_carry = 1; static u32 s_prev0_calls, s_calls;
/* One call of the native loop. out points at this call's first output;
 * out[-16] is the previous call's last vertex when t0 > 0. */
static u32 native_call(const rsx_fetch_plan* plan, const u16* ib, u32 first, u32 count,
                       u32 t0, VPSlot* out, int batched, u32* nl)
{
    if (t0 == 0) { s_pending = 0; s_last_ok = 0; }
    s_calls++;
    const u32 ridx = 0xFFFF;
    int prev0 = 0;
    u32 o = 0;
    for (u32 k = 0; k < count; k++) {
        u32 v = ib[first + k];
        if (v == ridx) { if (t0 + o) s_pending = 1; continue; }
        if (s_pending && t0 + o > 0) {
            if (batched) {
                if (o == 0) { if (s_carry && s_last_ok) nl[o] = s_last_vi; else { prev0 = 1; nl[o] = v; } } else nl[o] = nl[o - 1];
                o++;
                nl[o] = v; o++;
                if ((t0 + o) & 1) { nl[o] = nl[o - 1]; o++; }
                nl[o] = nl[o - 1]; o++;
                s_pending = 0; continue;
            }
            memcpy(&out[o*16], &out[((int)o - 1)*16], STRIDE); o++;
            one(plan, v, &out[o*16]); o++;
            if ((t0 + o) & 1) { memcpy(&out[o*16], &out[(o - 1)*16], STRIDE); o++; }
            memcpy(&out[o*16], &out[(o - 1)*16], STRIDE); o++;
            s_pending = 0; continue;
        }
        s_pending = 0;
        if (batched) { nl[o] = v; o++; } else { one(plan, v, &out[o*16]); o++; }
    }
    if (batched && o) {
        s_last_vi = nl[o - 1]; s_last_ok = 1;
        if (prev0) s_prev0_calls++;
        rsx_fetch_src src = { 0, RSX_FETCH_VLIST, 0, NULL, nl };
        rsx_fetch_plan_range(plan, &src, 0, o, (float (*)[4])out);
        if (prev0) memcpy(&out[0], &out[-16], STRIDE);
    }
    return o;
}

static u32 run(const rsx_fetch_plan* plan, const u16* ib, u32 ni, VPSlot* out, int batched, u32* nl)
{
    u32 t0 = 0;
    for (u32 f = 0; f < ni; f += 256) {
        u32 c = (ni - f < 256) ? ni - f : 256;
        t0 += native_call(plan, ib, f, c, t0, out + (u64)t0 * 16, batched, nl);
    }
    return t0;
}

int main(int argc, char** argv)
{
    const int aligned = argc > 1 && argv[1][0] == 'a';
    if (argc > 2) s_carry = 0;
    vm_base = (u8*)VirtualAlloc(NULL, 0x50000000ull, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    for (u32 p = 0; p < 0x2000000u; p += 4) { u32 r = rnd() ^ (rnd() << 12); memcpy(vm_base + LOCAL_ADDR + p, &r, 4); }
    rsx_state* st = (rsx_state*)calloc(1, sizeof *st);
    struct { int i; u32 type, size, off; } L[] = {
        {0, 2, 3, 0}, {2, 6, 1, 12}, {3, 4, 4, 16}, {8, 2, 2, 20}, {9, 2, 2, 28},
        {10, 2, 2, 36}, {11, 2, 2, 44}, {14, 6, 1, 52} };
    for (int k = 0; k < 8; k++) { rsx_vertex_attrib* a = &st->vertex_attribs[L[k].i];
        a->enabled = 1; a->type = L[k].type; a->size = L[k].size; a->stride = 64; a->offset = 0x200000u + L[k].off; }
    const u32 mask = (1u<<0)|(1u<<2)|(1u<<3)|(1u<<8)|(1u<<9)|(1u<<10)|(1u<<11)|(1u<<14);
    rsx_fetch_plan plan; rsx_fetch_plan_init(st, mask, &plan);

    const u32 NI = 300000;
    u16* ib = (u16*)malloc(NI * 2);
    for (u32 i = 0; i < NI; ) {
        u32 len = aligned ? 3 : ((rnd() % 4) ? 3 : 4 + rnd() % 20), base = rnd() % 20000;
        for (u32 j = 0; j < len && i < NI; j++) ib[i++] = (u16)(base + j);
        if (i < NI) ib[i++] = 0xFFFF;
    }
    const u64 cap = (u64)NI * 4 + 64;
    u32* nl = (u32*)malloc(1024 * 4);
    VPSlot* a = (VPSlot*)malloc(cap * STRIDE);
    VPSlot* b = (VPSlot*)malloc(cap * STRIDE);
    memset(a, 0x11, cap * STRIDE); memset(b, 0x22, cap * STRIDE);
    u32 na = run(&plan, ib, NI, a, 0, nl);
    u32 nb = run(&plan, ib, NI, b, 1, nl);
    int same = (na == nb) && !memcmp(a, b, (size_t)na * STRIDE);
    printf("%s stream, carry %s: %u indices -> %u / %u outputs (per-vertex / batched): %s; "
           "batched calls needing the heap copy: %u of %u\n",
           aligned ? "aligned 3+restart" : "mixed", s_carry ? "on" : "off", NI, na, nb,
           same ? "byte-identical" : "DIFFERENT", s_prev0_calls, s_calls / 2);

    if (1) return !same;
    VPSlot* up = (VPSlot*)upload_heap(cap * STRIDE);
    if (!up) { printf("no D3D12 device\n"); return !same; }
    LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f);
    for (int rep = 0; rep < 3; rep++) {
        QueryPerformanceCounter(&t0); u32 n1 = run(&plan, ib, NI, up, 0, nl); QueryPerformanceCounter(&t1);
        double ta = (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart;
        QueryPerformanceCounter(&t0); u32 n2 = run(&plan, ib, NI, up, 1, nl); QueryPerformanceCounter(&t1);
        double tb = (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart;
        printf("upload heap: per-vertex %.1f ms (%.3f us/v) | batched %.1f ms (%.3f us/v)\n",
               ta * 1e3, ta * 1e6 / n1, tb * 1e3, tb * 1e6 / n2);
    }
    return !same;
}
