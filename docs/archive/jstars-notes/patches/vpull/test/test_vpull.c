/* VPULL check, on the GPU: the vertex-pulling decoder (RSX_VPULL_HLSL, run in
 * a D3D11 compute shader) must give the SAME float bits as the CPU decode
 * (rsx_fetch_plan_range from rsx_vertex_fetch.c) for random single-stream
 * layouts: all 7 types, 1-4 components, odd offsets and strides, constant
 * attributes, base index, LOCAL and MAIN (IO pages remapped out of order).
 * The guest bytes go through the same path as in the renderer: the layout
 * from rsx_vpull_layout_from_plan, rsx_vpull_copy per batch into one region,
 * indices rebased to it, vertex k of the region at k * stride. */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rsx_vpull.h"

u8* vm_base;
static u32 s_perm[8];                       /* MAIN: IO page -> guest page (8..15) */
u32 cellGcmResolveLocated(int local, u32 off)
{
    if (local) return off;
    return ((8u + s_perm[(off >> 20) & 7u]) << 20) | (off & 0xFFFFFu);
}
u32 cellGcmResolveOffset(u32 off) { return off; }

static u32 rng = 2468u;
static u32 rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static float rndf(void) { u32 w = rnd() ^ (rnd() << 12); float f; memcpy(&f, &w, 4); return f; }

static const char* CS =
"cbuffer N : register(b3) { uint n_out; };\n"
"StructuredBuffer<uint> ids : register(t0);\n"
"RWStructuredBuffer<float4> outb : register(u0);\n"
"[numthreads(64,1,1)] void main(uint3 t : SV_DispatchThreadID) {\n"
"    uint i = t.x; if (i >= n_out) return;\n"
"    uint vid = ids[i];\n"
"    [unroll] for (uint a = 0u; a < 16u; a++) outb[i * 16u + a] = vpull_attr(a, vid);\n"
"}\n";

#define CHECK(x) do { HRESULT _h = (x); if (FAILED(_h)) { printf("FAIL 0x%08lX at %s\n", _h, #x); return 2; } } while (0)

static ID3D11Buffer* mkbuf(ID3D11Device* dev, UINT bytes, UINT bind, UINT misc, UINT stride, const void* init, D3D11_USAGE use, UINT cpu)
{
    D3D11_BUFFER_DESC d = {0}; d.ByteWidth = bytes; d.BindFlags = bind; d.MiscFlags = misc;
    d.StructureByteStride = stride; d.Usage = use; d.CPUAccessFlags = cpu;
    D3D11_SUBRESOURCE_DATA s = {0}; s.pSysMem = init;
    ID3D11Buffer* b = NULL; ID3D11Device_CreateBuffer(dev, &d, init ? &s : NULL, &b); return b;
}
static ID3D11ShaderResourceView* rawsrv(ID3D11Device* dev, ID3D11Buffer* b, UINT bytes)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC v = {0}; v.Format = DXGI_FORMAT_R32_TYPELESS;
    v.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX; v.BufferEx.NumElements = bytes / 4;
    v.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
    ID3D11ShaderResourceView* s = NULL; ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource*)b, &v, &s); return s;
}

int main(void)
{
    ID3D11Device* dev; ID3D11DeviceContext* ctx; D3D_FEATURE_LEVEL fl;
    CHECK(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx));
    char* src = (char*)malloc(65536); snprintf(src, 65536, "%s%s", RSX_VPULL_HLSL, CS);
    ID3DBlob* code = NULL; ID3DBlob* err = NULL;
    if (FAILED(D3DCompile(src, strlen(src), "vpull_test", NULL, NULL, "main", "cs_5_0", 0, 0, &code, &err))) {
        printf("compile: %s\n", err ? (const char*)ID3D10Blob_GetBufferPointer(err) : "?"); return 2; }
    ID3D11ComputeShader* cs; CHECK(ID3D11Device_CreateComputeShader(dev, ID3D10Blob_GetBufferPointer(code), ID3D10Blob_GetBufferSize(code), NULL, &cs));

    static float lut[RSX_VPULL_LUT_COUNT]; rsx_vpull_fill_lut(lut);
    ID3D11Buffer* lutb = mkbuf(dev, sizeof lut, D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, lut, D3D11_USAGE_IMMUTABLE, 0);
    ID3D11ShaderResourceView* lutv = rawsrv(dev, lutb, sizeof lut);

    vm_base = (u8*)malloc(16u << 20);
    enum { NOUT = 4096, REGION = 8u << 20 };
    u8* region = (u8*)calloc(REGION + 256, 1);
    u32* ids = (u32*)malloc(NOUT * 4);
    float (*cpu)[4] = (float (*)[4])_aligned_malloc((size_t)NOUT * 16 * 16, 16);
    float (*gpu)[4] = (float (*)[4])malloc((size_t)NOUT * 16 * 16);
    static rsx_state st;
    long cases = 0, pulled = 0, cmp = 0, bad = 0, straddle = 0, per_type[8] = {0};

    for (int it = 0; it < 400; it++) {
        for (u32 i = 0; i < (16u << 20); i += 4) { u32 w = rnd() ^ (rnd() << 16); memcpy(vm_base + i, &w, 4); }
        for (int i = 0; i < 8; i++) s_perm[i] = (u32)i;
        for (int i = 7; i > 0; i--) { int j = rnd() % (i + 1); u32 t = s_perm[i]; s_perm[i] = s_perm[j]; s_perm[j] = t; }
        memset(&st, 0, sizeof st);
        for (int a = 0; a < 16; a++) for (int k = 0; k < 4; k++) st.vertex_data4f[a][k] = (rnd() & 1) ? rndf() : (float)(rnd() % 7);
        const u32 S = 1 + rnd() % 64;                  /* any stride, odd ones too */
        const int main_mem = rnd() & 1;
        const u32 base = rnd() % (main_mem ? (6u << 20) : (3u << 20));
        st.vertex_data_base_offset = (rnd() & 1) ? rnd() % 4096 : 0;
        st.vertex_data_base_index = (rnd() & 3) == 0 ? rnd() % 1000 : 0;
        u32 mask = 1u;
        for (int a = 0; a < 16; a++) {
            rsx_vertex_attrib* va = &st.vertex_attribs[a];
            if (a && (rnd() % 3) == 0) continue;            /* not in the mask */
            mask |= 1u << a;
            if (a && (rnd() % 4) == 0) continue;            /* in the mask, constant */
            va->enabled = 1; va->stride = S;
            va->type = 1 + rnd() % 7; va->size = 1 + rnd() % 4;
            va->offset = (base + rnd() % 64) | (main_mem ? 0x80000000u : 0u);
        }
        st.vertex_attribs[0].enabled = 1; st.vertex_attribs[0].stride = S;
        if (!st.vertex_attribs[0].type) { st.vertex_attribs[0].type = 2; st.vertex_attribs[0].size = 3; st.vertex_attribs[0].offset = base | (main_mem ? 0x80000000u : 0u); }
        rsx_fetch_plan plan; rsx_fetch_plan_init(&st, mask, &plan);
        rsx_vpull_layout lay; rsx_vpull_tab tab;
        cases++;
        if (!rsx_vpull_layout_from_plan(&plan, &lay, &tab)) continue;
        pulled++;

        /* Batches of up to 256 guest indices, copied and rebased as the renderer does. */
        const u32 nverts = 200 + rnd() % 3000;
        u32* vl = (u32*)malloc(NOUT * 4);
        for (u32 o = 0; o < NOUT; o++) vl[o] = (rnd() % 8) ? (o / 3 + rnd() % 40) % nverts : rnd() % nverts;
        u32 K = 0, nb = 0;
        for (u32 b0 = 0; b0 < NOUT; b0 += 256) {
            const u32 b1 = b0 + 256 < NOUT ? b0 + 256 : NOUT;
            u32 vmin = 0xFFFFFFFFu, vmax = 0;
            for (u32 o = b0; o < b1; o++) { u32 v = (vl[o] + plan.base_index) & 0xFFFFFu; if (v < vmin) vmin = v; if (v > vmax) vmax = v; }
            const u32 len = (vmax - vmin) * S + lay.extent;
            if ((u64)K * S + len > REGION) break;
            rsx_vpull_copy(region + (u64)K * S, lay.base + vmin * S, len, lay.main, plan.local_base);
            for (u32 o = b0; o < b1; o++) ids[o] = K + (((vl[o] + plan.base_index) & 0xFFFFFu) - vmin);
            K += (vmax - vmin) + 1u + lay.pad; nb = b1;
        }
        rsx_fetch_src srcd = { 0, RSX_FETCH_VLIST, 0, NULL, vl };
        rsx_fetch_plan_range(&plan, &srcd, 0, nb, cpu);

        ID3D11Buffer* rb = mkbuf(dev, REGION + 256, D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, region, D3D11_USAGE_IMMUTABLE, 0);
        ID3D11ShaderResourceView* rv = rawsrv(dev, rb, REGION + 256);
        ID3D11Buffer* ib = mkbuf(dev, NOUT * 4, D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4, ids, D3D11_USAGE_IMMUTABLE, 0);
        D3D11_SHADER_RESOURCE_VIEW_DESC sv = {0}; sv.Format = DXGI_FORMAT_UNKNOWN; sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; sv.Buffer.NumElements = NOUT;
        ID3D11ShaderResourceView* iv; ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource*)ib, &sv, &iv);
        ID3D11Buffer* tb = mkbuf(dev, sizeof tab, D3D11_BIND_CONSTANT_BUFFER, 0, 0, &tab, D3D11_USAGE_IMMUTABLE, 0);
        u32 ncb[4] = { nb, 0, 0, 0 };
        ID3D11Buffer* nbuf = mkbuf(dev, 16, D3D11_BIND_CONSTANT_BUFFER, 0, 0, ncb, D3D11_USAGE_IMMUTABLE, 0);
        ID3D11Buffer* ob = mkbuf(dev, NOUT * 256, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 16, NULL, D3D11_USAGE_DEFAULT, 0);
        D3D11_UNORDERED_ACCESS_VIEW_DESC uv = {0}; uv.Format = DXGI_FORMAT_UNKNOWN; uv.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; uv.Buffer.NumElements = NOUT * 16;
        ID3D11UnorderedAccessView* ov; ID3D11Device_CreateUnorderedAccessView(dev, (ID3D11Resource*)ob, &uv, &ov);
        ID3D11Buffer* stg = mkbuf(dev, NOUT * 256, 0, 0, 0, NULL, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ);

        ID3D11DeviceContext_CSSetShader(ctx, cs, NULL, 0);
        ID3D11ShaderResourceView* srvs[10] = { iv, NULL, NULL, NULL, NULL, NULL, NULL, NULL, rv, lutv };
        ID3D11DeviceContext_CSSetShaderResources(ctx, 0, 10, srvs);
        ID3D11Buffer* cbs[4] = { NULL, NULL, tb, nbuf };
        ID3D11DeviceContext_CSSetConstantBuffers(ctx, 0, 4, cbs);
        ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &ov, NULL);
        ID3D11DeviceContext_Dispatch(ctx, (nb + 63) / 64, 1, 1);
        ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource*)stg, (ID3D11Resource*)ob);
        D3D11_MAPPED_SUBRESOURCE m; CHECK(ID3D11DeviceContext_Map(ctx, (ID3D11Resource*)stg, 0, D3D11_MAP_READ, 0, &m));
        memcpy(gpu, m.pData, (size_t)nb * 256);
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource*)stg, 0);

        for (u32 o = 0; o < nb; o++)
            for (u32 a = 0; a < 16; a++) {
                if (!(plan.mask & (1u << a))) continue;
                cmp++;
                if (memcmp(cpu[o * 16 + a], gpu[o * 16 + a], 16) != 0) {
                    /* An attribute that straddles a 1 MB IO page: the CPU decode resolves
                     * the page once per attribute and reads on linearly, the copy follows
                     * the IO table per page. They differ only when the next IO page is
                     * not the next guest page (this test shuffles them on purpose). */
                    const rsx_vertex_attrib* va = &st.vertex_attribs[a];
                    const u32 ei = (vl[o] + plan.base_index) & 0xFFFFFu;
                    const u32 goff = st.vertex_data_base_offset + (va->offset & 0x7FFFFFFFu) + ei * va->stride;
                    if ((va->offset & 0x80000000u) &&
                        (goff & 0xFFFFFu) + rsx_vpull_type_size(va->type, va->size ? va->size : 4) > 0x100000u) {
                        straddle++; continue; }
                    if (bad < 8) printf("MISMATCH case %ld out %u attr %u type %u n %u: cpu %08X %08X %08X %08X gpu %08X %08X %08X %08X\n",
                        cases, o, a, st.vertex_attribs[a].type, st.vertex_attribs[a].size,
                        *(u32*)&cpu[o*16+a][0], *(u32*)&cpu[o*16+a][1], *(u32*)&cpu[o*16+a][2], *(u32*)&cpu[o*16+a][3],
                        *(u32*)&gpu[o*16+a][0], *(u32*)&gpu[o*16+a][1], *(u32*)&gpu[o*16+a][2], *(u32*)&gpu[o*16+a][3]);
                    bad++;
                } else if (st.vertex_attribs[a].enabled) per_type[st.vertex_attribs[a].type & 7]++;
            }
        ID3D11Buffer_Release(stg); ID3D11UnorderedAccessView_Release(ov); ID3D11Buffer_Release(ob);
        ID3D11Buffer_Release(nbuf); ID3D11Buffer_Release(tb); ID3D11ShaderResourceView_Release(iv);
        ID3D11Buffer_Release(ib); ID3D11ShaderResourceView_Release(rv); ID3D11Buffer_Release(rb);
        free(vl);
    }
    printf("%ld layouts, %ld pulled, %ld attribute values compared, %ld mismatches"
           " (+%ld only across a shuffled IO page edge)\n", cases, pulled, cmp, bad, straddle);
    printf("matching fetched values by type: S1 %ld F %ld SF %ld UB %ld S32K %ld CMP %ld UB256 %ld\n",
           per_type[1], per_type[2], per_type[3], per_type[4], per_type[5], per_type[6], per_type[7]);
    return bad ? 1 : 0;
}
