/* Standalone check: rsx_fetch_plan_range (mask 0xFFFF) must match
 * rsx_fetch_attrib bit for bit, for every type, LOCAL and MAIN arrays
 * (MAIN through a scrambled IO page table), the divider, base offset/index
 * and the expansion topologies. Then a timing of the old vs new loop. */
#include "rsx_vertex_fetch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#define COBJMACROS
#include <d3d12.h>
#include <dxgi1_4.h>

/* A real D3D12 upload heap, mapped, like s_d3d.vp_vb_mapped. NULL if no device. */
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
    { IDXGIFactory4* f = NULL; IDXGIAdapter1* a = NULL; DXGI_ADAPTER_DESC1 d;
      if (SUCCEEDED(CreateDXGIFactory1(&IID_IDXGIFactory4, (void**)&f)) &&
          SUCCEEDED(IDXGIFactory1_EnumAdapters1(f, 0, &a)) && SUCCEEDED(IDXGIAdapter1_GetDesc1(a, &d)))
          printf("upload heap on adapter 0: %ls\n", d.Description); }
    return p;
}

u8* vm_base;
#define LOCAL_ADDR 0x40000000u
#define VM_SIZE    0x60000000ull
static u16 io_page[4096];                 /* IO page -> EA page (scrambled) */
u32 cellGcmResolveOffset(u32 off) { return LOCAL_ADDR + off; }
u32 cellGcmResolveLocated(int local, u32 off)
{
    if (local) return LOCAL_ADDR + off;
    u32 pg = off >> 20;
    if (pg < 4096 && io_page[pg]) return ((u32)io_page[pg] << 20) | (off & 0xFFFFF);
    return cellGcmResolveOffset(off);
}

static u32 rng = 12345u;
static u32 rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

/* reference: the source mapping the backend loops used */
static u32 ref_src(const rsx_fetch_src* s, u32 o)
{
    u32 src;
    if (s->topo == RSX_FETCH_QUADS) { static const int q6[6] = {0,1,2,0,2,3}; src = (o/6)*4 + q6[o%6]; }
    else if (s->topo == RSX_FETCH_LIST) src = o;
    else { u32 t = o/3, j = o%3; u32 i0 = (s->topo == RSX_FETCH_FAN) ? 0 : t, i1 = t+1, i2 = t+2;
           if (s->topo == RSX_FETCH_STRIP_SWAP && (t & 1)) { u32 x = i1; i1 = i2; i2 = x; }
           src = j == 0 ? i0 : j == 1 ? i1 : i2; }
    u32 k = s->first + src;
    if (s->isize == 2) return ((u32)s->ib[k*2] << 8) | s->ib[k*2+1];
    if (s->isize == 4) return ((u32)s->ib[k*4] << 24) | ((u32)s->ib[k*4+1] << 16) | ((u32)s->ib[k*4+2] << 8) | s->ib[k*4+3];
    return k;
}

static void random_state(rsx_state* st)
{
    memset(st, 0, sizeof *st);
    st->vertex_data_base_offset = (rnd() & 3) ? 0 : (rnd() & 0xFFF0);
    st->vertex_data_base_index  = (rnd() & 3) ? 0 : (rnd() & 0xFF);
    st->frequency_divider_op = rnd() & 0xFFFF;
    static const u32 types[] = {1,2,3,4,5,6,7, 9};
    for (int i = 0; i < 16; i++) {
        for (int k = 0; k < 4; k++) st->vertex_data4f[i][k] = (float)(rnd() % 1000) / 7.0f;
        rsx_vertex_attrib* a = &st->vertex_attribs[i];
        a->enabled = (rnd() % 4) != 0;
        a->type = types[rnd() % 8];
        a->size = rnd() % 5;                       /* 0 = 4 */
        a->stride = (rnd() % 8) ? 16 + (rnd() % 8) * 4 : 0;
        a->frequency = (rnd() % 6 == 0) ? 2 + rnd() % 5 : 0;
        const int main = rnd() & 1;
        /* main: IO offsets 0..63MB (crosses pages); local: 0..127MB */
        u32 off = main ? (rnd() % 0x3F00000u) : (rnd() % 0x7F00000u);
        a->offset = off | (main ? 0x80000000u : 0u);
    }
}

static float got[4096][16][4], want[4096][16][4];

static int compare(const rsx_state* st, const rsx_fetch_src* src, u32 n)
{
    memset(got, 0xCD, sizeof(float)*16*4*n);
    rsx_fetch_plan plan; rsx_fetch_plan_init(st, 0xFFFF, &plan);
    rsx_fetch_plan_range(&plan, src, 0, n / 2, &got[0][0]);
    rsx_fetch_plan_range(&plan, src, n / 2, n, &got[0][0]);   /* split like the pool does */
    for (u32 o = 0; o < n; o++) {
        u32 vi = ref_src(src, o);
        if (vi != rsx_fetch_src_vertex(src, o)) { printf("src map mismatch topo=%d o=%u\n", src->topo, o); return 1; }
        for (int i = 0; i < 16; i++) rsx_fetch_attrib(st, i, vi, want[o][i]);
    }
    if (memcmp(got, want, sizeof(float)*16*4*n)) {
        for (u32 o = 0; o < n; o++) for (int i = 0; i < 16; i++)
            if (memcmp(got[o][i], want[o][i], 16)) {
                const rsx_vertex_attrib* a = &st->vertex_attribs[i];
                printf("MISMATCH o=%u attr=%d type=%u size=%u stride=%u freq=%u off=0x%X: got %g %g %g %g want %g %g %g %g\n",
                       o, i, a->type, a->size, a->stride, a->frequency, a->offset,
                       got[o][i][0], got[o][i][1], got[o][i][2], got[o][i][3],
                       want[o][i][0], want[o][i][1], want[o][i][2], want[o][i][3]);
                return 1; }
    }
    return 0;
}

/* masked plan: slots outside the mask are zero */
static int check_mask(const rsx_state* st, const rsx_fetch_src* src, u32 n, u32 mask)
{
    memset(got, 0xCD, sizeof(float)*16*4*n);
    rsx_fetch_plan plan; rsx_fetch_plan_init(st, mask, &plan);
    rsx_fetch_plan_range(&plan, src, 0, n, &got[0][0]);
    for (u32 o = 0; o < n; o++) {
        u32 vi = ref_src(src, o);
        for (int i = 0; i < 16; i++) {
            float w[4]; rsx_fetch_attrib(st, i, vi, w);
            if (mask & (1u << i)) { if (memcmp(got[o][i], w, 16)) return 1; }
            else { static const float z[4]; if (memcmp(got[o][i], z, 16)) return 1; }
        }
    }
    return 0;
}

int main(void)
{
    vm_base = (u8*)VirtualAlloc(NULL, VM_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!vm_base) { printf("VirtualAlloc failed\n"); return 2; }
    for (u64 p = 0; p < 0x8800000ull; p += 4) { u32 r = rnd() ^ (rnd() << 12); memcpy(vm_base + LOCAL_ADDR + p, &r, 4); }
    /* IO pages 0..69 -> EA pages scrambled inside 0x10000000.. */
    for (int pg = 0; pg < 70; pg++) {
        io_page[pg] = (u16)(0x100 + ((pg * 37) % 200));
        for (u32 q = 0; q < 0x100000u; q += 4) { u32 r = rnd() ^ (rnd() << 12); memcpy(vm_base + ((u32)io_page[pg] << 20) + q, &r, 4); }
    }
    static u8 ib[65536];
    int fails = 0, cases = 0, mfails = 0;
    for (int it = 0; it < 300; it++) {
        rsx_state* st = (rsx_state*)calloc(1, sizeof *st);
        random_state(st);
        for (u32 k = 0; k < sizeof ib; k++) ib[k] = (u8)rnd();
        for (int k = 0; k < (int)sizeof ib; k += 2) ib[k] &= 0x0F;   /* u16 indices < 4096 */
        for (int topo = 0; topo <= 4; topo++)
            for (int isz = 0; isz <= 4; isz += 2) {
                if (isz == 4) for (int k = 0; k < (int)sizeof ib; k += 4) { ib[k] = 0; ib[k+1] = 0; ib[k+2] &= 0x0F; }
                rsx_fetch_src src = { rnd() % 2000, (u8)topo, (u8)isz, isz ? ib : NULL };
                fails += compare(st, &src, 600); cases++;
                mfails += check_mask(st, &src, 300, rnd() & 0xFFFF);
            }
        free(st);
    }
    printf("full plan: %d/%d cases bit-identical to rsx_fetch_attrib; masked plan: %d failures\n",
           cases - fails, cases, mfails);

    /* Timing: a match-like indexed list draw, 300k vertices, 4 attributes the
     * VP reads (pos F32x3, normal F32x3, colour UBx4, uv SFx2) + 1 it does not. */
    rsx_state* st = (rsx_state*)calloc(1, sizeof *st);
    const u32 tp[4] = {2, 2, 4, 3}, sz[4] = {3, 3, 4, 2}, at[4] = {0, 2, 3, 8}, ao[4] = {0, 12, 24, 28};
    for (int k = 0; k < 4; k++) { rsx_vertex_attrib* a = &st->vertex_attribs[at[k]];
        a->enabled = 1; a->type = tp[k]; a->size = sz[k]; a->stride = 32; a->offset = 0x100000u + ao[k]; }
    st->vertex_attribs[1].enabled = 1; st->vertex_attribs[1].type = 2; st->vertex_attribs[1].size = 4;
    st->vertex_attribs[1].stride = 16; st->vertex_attribs[1].offset = 0x400000u;
    const u32 NV = 300000;
    u8* ibig = (u8*)malloc(NV * 2);
    for (u32 k = 0; k < NV; k++) { u32 v = (k / 3 + (k % 3)) % 20000; ibig[k*2] = (u8)(v >> 8); ibig[k*2+1] = (u8)v; }
    float (*outc)[4] = (float (*)[4])VirtualAlloc(NULL, (SIZE_T)NV * 256, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    /* An upload heap's CPU pages are write-combined; PAGE_WRITECOMBINE gives
     * the same memory type in system RAM. */
    float (*outw)[4] = (float (*)[4])VirtualAlloc(NULL, (SIZE_T)NV * 256, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE | PAGE_WRITECOMBINE);
    float (*outu)[4] = (float (*)[4])upload_heap((u64)NV * 256);
    if (!outu) printf("no D3D12 device: upload-heap timing skipped\n");
    LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f);
    rsx_fetch_src src = { 0, RSX_FETCH_LIST, 2, ibig };
    for (int rep = 0; rep < 6; rep++) {
        float (*out)[4] = (rep % 3 == 0) ? outc : (rep % 3 == 1) ? outw : outu;
        const char* kind = (rep % 3 == 0) ? "cached     " : (rep % 3 == 1) ? "WC sysmem  " : "upload heap";
        if (!out) continue;
        QueryPerformanceCounter(&t0);
        for (u32 k = 0; k < NV; k++) { u32 vi = rsx_fetch_src_vertex(&src, k);
            for (int i = 0; i < 16; i++) rsx_fetch_attrib(st, i, vi, out[(u64)k*16 + i]); }
        QueryPerformanceCounter(&t1);
        double legacy = (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart;
        rsx_fetch_plan plan;
        QueryPerformanceCounter(&t0);
        rsx_fetch_plan_init(st, 0xFFFF, &plan); rsx_fetch_plan_range(&plan, &src, 0, NV, out);
        QueryPerformanceCounter(&t1);
        double all16 = (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart;
        QueryPerformanceCounter(&t0);
        rsx_fetch_plan_init(st, (1u<<0)|(1u<<2)|(1u<<3)|(1u<<8), &plan); rsx_fetch_plan_range(&plan, &src, 0, NV, out);
        QueryPerformanceCounter(&t1);
        double masked = (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart;
        printf("%s 300k verts, 1 thread: legacy %.1f ms (%.3f us/v) | plan, all 16 %.1f ms | plan, 4 read %.1f ms (%.3f us/v)\n",
               kind, legacy * 1e3, legacy * 1e6 / NV, all16 * 1e3, masked * 1e3, masked * 1e6 / NV);
    }
    return (fails || mfails) ? 1 : 0;
}
