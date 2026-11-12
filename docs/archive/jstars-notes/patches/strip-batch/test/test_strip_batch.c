/* Strip expansion, per-vertex vs batched:
 *  1. a restart-aware strip expansion (the loop in upload_strip_vp_indexed_rsx)
 *     is run both ways -- one plan range per vertex (vf_one) and one VLIST
 *     range over the listed indices -- and must give identical bytes, with
 *     dedup on and off;
 *  2. timing of both into a real D3D12 upload heap. */
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

static u32 rng = 4242u;
static u32 rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

/* The expansion loop of upload_strip_vp_indexed_rsx, with either emitter. */
static u32 expand(const u16* idx, u32 count, u32 ridx, int restart, u32* vl)
{
    u32 a = 0, b = 0, k = 0, o = 0;
    for (u32 i = 0; i < count; i++) {
        u32 v = idx[i];
        if (restart && v == ridx) { k = 0; continue; }
        if (k >= 2) { vl[o++] = (k & 1) ? b : a; vl[o++] = (k & 1) ? a : b; vl[o++] = v; }
        a = b; b = v; k++;
    }
    return o;
}

static void one(const rsx_fetch_plan* plan, u32 v, float (*dst)[4])
{
    rsx_fetch_src s1 = { v, RSX_FETCH_LIST, 0, NULL, NULL };
    rsx_fetch_plan_range(plan, &s1, 0, 1, dst);
}

/* J-Stars character layout (FINDINGS §26): float3 pos a0, CMP normal a2,
 * UB4 colour a3, float2 uv a8..a11, CMP tangent a14; stride 64. */
static void jstars_state(rsx_state* st)
{
    memset(st, 0, sizeof *st);
    struct { int i; u32 type, size, off; } L[] = {
        {0, 2, 3, 0}, {2, 6, 1, 12}, {3, 4, 4, 16}, {8, 2, 2, 20}, {9, 2, 2, 28},
        {10, 2, 2, 36}, {11, 2, 2, 44}, {14, 6, 1, 52} };
    for (int k = 0; k < 8; k++) { rsx_vertex_attrib* a = &st->vertex_attribs[L[k].i];
        a->enabled = 1; a->type = L[k].type; a->size = L[k].size; a->stride = 64;
        a->offset = 0x200000u + L[k].off; }
    for (int i = 0; i < 16; i++) st->vertex_data4f[i][3] = 1.0f;
}

int main(void)
{
    vm_base = (u8*)VirtualAlloc(NULL, 0x50000000ull, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    for (u32 p = 0; p < 0x2000000u; p += 4) { u32 r = rnd() ^ (rnd() << 12); memcpy(vm_base + LOCAL_ADDR + p, &r, 4); }
    rsx_state* st = (rsx_state*)calloc(1, sizeof *st);
    jstars_state(st);
    const u32 mask = (1u<<0)|(1u<<2)|(1u<<3)|(1u<<8)|(1u<<9)|(1u<<10)|(1u<<11)|(1u<<14);

    /* Match-like index stream: mostly 3-index pieces split by 0xFFFF, some
     * longer strips, 256-index FIFO batches. */
    const u32 NI = 300000;
    u16* idx = (u16*)malloc(NI * 2);
    for (u32 i = 0; i < NI; ) {
        u32 len = (rnd() % 4) ? 3 : 4 + rnd() % 20, base = rnd() % 20000;
        for (u32 j = 0; j < len && i < NI; j++) idx[i++] = (u16)(base + j);
        if (i < NI) idx[i++] = 0xFFFF;
    }
    u32* vl = (u32*)malloc((size_t)NI * 3 * 4);
    const u32 nout = expand(idx, NI, 0xFFFF, 1, vl);

    float (*a)[4] = (float (*)[4])malloc((size_t)nout * 256);
    float (*b)[4] = (float (*)[4])malloc((size_t)nout * 256);
    rsx_fetch_plan plan; rsx_fetch_plan_init(st, mask, &plan);
    rsx_fetch_plan nod = plan; nod.dedup = 0;
    for (u32 o = 0; o < nout; o++) one(&nod, vl[o], a + (u64)o * 16);
    rsx_fetch_src src = { 0, RSX_FETCH_VLIST, 0, NULL, vl };
    memset(b, 0xAB, (size_t)nout * 256);
    rsx_fetch_plan_range(&plan, &src, 0, nout, b);
    int same1 = !memcmp(a, b, (size_t)nout * 256);
    memset(b, 0xAB, (size_t)nout * 256);
    rsx_fetch_plan_range(&nod, &src, 0, nout, b);
    int same2 = !memcmp(a, b, (size_t)nout * 256);
    printf("%u indices -> %u output vertices; batched+dedup %s, batched no-dedup %s per-vertex\n",
           NI, nout, same1 ? "==" : "!=", same2 ? "==" : "!=");

    float (*up)[4] = (float (*)[4])upload_heap((u64)nout * 256);
    if (!up) { printf("no D3D12 device\n"); return !(same1 && same2); }
    LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f);
    for (int rep = 0; rep < 3; rep++) {
        double t[3];
        QueryPerformanceCounter(&t0);
        for (u32 o = 0; o < nout; o++) one(&plan, vl[o], up + (u64)o * 16);
        QueryPerformanceCounter(&t1); t[0] = (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart;
        QueryPerformanceCounter(&t0);
        rsx_fetch_plan_range(&nod, &src, 0, nout, up);
        QueryPerformanceCounter(&t1); t[1] = (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart;
        QueryPerformanceCounter(&t0);
        rsx_fetch_plan_range(&plan, &src, 0, nout, up);
        QueryPerformanceCounter(&t1); t[2] = (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart;
        printf("upload heap, %u verts: per-vertex %.1f ms (%.3f us/v) | batched %.1f ms | batched+dedup %.1f ms (%.3f us/v)\n",
               nout, t[0] * 1e3, t[0] * 1e6 / nout, t[1] * 1e3, t[2] * 1e3, t[2] * 1e6 / nout);
    }
    return !(same1 && same2);
}
