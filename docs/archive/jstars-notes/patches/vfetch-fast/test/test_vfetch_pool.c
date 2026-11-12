/* The backend's worker pool (pool.inc, cut verbatim from the patched
 * rsx_d3d12_backend.c) driven the way vf_fetch drives it: output must equal
 * the single-threaded plan for many draw sizes, and the timing shows the
 * scaling into a real upload heap. */
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

#include "pool.inc"

static void pool_fetch(const rsx_fetch_plan* plan, const rsx_fetch_src* src, u32 total, float (*o4)[4])
{
    vf_pool_init();
    int nw = 0;
    if (s_vf.n > 0 && total >= s_vf.min_split) {
        const u32 chunks = (total + VF_CHUNK - 1u) / VF_CHUNK;
        nw = (int)chunks - 1;
        if (nw > s_vf.n) nw = s_vf.n;
    }
    if (nw <= 0) { rsx_fetch_plan_range(plan, src, 0, total, o4); return; }
    s_vf.plan = plan; s_vf.src = src; s_vf.out = o4; s_vf.total = total;
    s_vf.next = 0;
    s_vf.busy = nw;
    MemoryBarrier();
    for (int i = 0; i < nw; i++) SetEvent(s_vf.go[i]);
    vf_take_chunks();
    WaitForSingleObject(s_vf.done, INFINITE);
}

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

static u32 rng = 777u;
static u32 rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

int main(void)
{
    vm_base = (u8*)VirtualAlloc(NULL, 0x50000000ull, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    for (u32 p = 0; p < 0x1000000u; p += 4) { u32 r = rnd() ^ (rnd() << 12); memcpy(vm_base + LOCAL_ADDR + p, &r, 4); }
    rsx_state* st = (rsx_state*)calloc(1, sizeof *st);
    const u32 tp[4] = {2, 2, 4, 3}, sz[4] = {3, 3, 4, 2}, at[4] = {0, 2, 3, 8}, ao[4] = {0, 12, 24, 28};
    for (int k = 0; k < 4; k++) { rsx_vertex_attrib* a = &st->vertex_attribs[at[k]];
        a->enabled = 1; a->type = tp[k]; a->size = sz[k]; a->stride = 32; a->offset = 0x100000u + ao[k]; }
    const u32 NV = 400000;
    u8* ib = (u8*)malloc(NV * 2);
    for (u32 k = 0; k < NV; k++) { u32 v = (k / 3 + (k % 3)) % 30000; ib[k*2] = (u8)(v >> 8); ib[k*2+1] = (u8)v; }
    float (*ref)[4] = (float (*)[4])malloc((size_t)NV * 256);
    float (*got)[4] = (float (*)[4])malloc((size_t)NV * 256);
    rsx_fetch_plan plan; rsx_fetch_plan_init(st, (1u<<0)|(1u<<2)|(1u<<3)|(1u<<8), &plan);
    rsx_fetch_src src = { 0, RSX_FETCH_STRIP, 2, ib };
    int bad = 0;
    const u32 sizes[] = {1, 3, 511, 512, 513, 2047, 2048, 2049, 5000, 65537, 399999};
    for (int t = 0; t < (int)(sizeof sizes / sizeof sizes[0]); t++) {
        const u32 n = sizes[t] - sizes[t] % 3 ? sizes[t] - sizes[t] % 3 : 3;
        rsx_fetch_plan_range(&plan, &src, 0, n, ref);
        for (int rep = 0; rep < 20; rep++) {
            memset(got, 0xAB, (size_t)n * 256);
            pool_fetch(&plan, &src, n, got);
            if (memcmp(got, ref, (size_t)n * 256)) { bad++; printf("pool mismatch n=%u\n", n); break; }
        }
    }
    printf("pool: %s (%d workers)\n", bad ? "MISMATCH" : "all sizes identical to single-threaded", s_vf.n);

    float (*up)[4] = (float (*)[4])upload_heap((u64)NV * 256);
    if (!up) { printf("no D3D12 device\n"); return bad; }
    LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f);
    rsx_fetch_src lst = { 0, RSX_FETCH_LIST, 2, ib };
    for (int rep = 0; rep < 3; rep++) {
        QueryPerformanceCounter(&t0); rsx_fetch_plan_range(&plan, &lst, 0, NV, up); QueryPerformanceCounter(&t1);
        double one = (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart;
        QueryPerformanceCounter(&t0); pool_fetch(&plan, &lst, NV, up); QueryPerformanceCounter(&t1);
        double many = (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart;
        printf("400k verts into upload heap: 1 thread %.2f ms | pool (%d+1) %.2f ms\n", one * 1e3, s_vf.n, many * 1e3);
    }
    return bad;
}
