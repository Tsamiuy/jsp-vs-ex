/* VPULL: a decompiled vertex program in pulling mode must compile as vs_5_0
 * (as the renderer compiles it) and take no vertex inputs but SV_VertexID;
 * the same program in normal mode still reads its ATTRn inputs. */
#define COBJMACROS
#include <windows.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <stdio.h>
#include <string.h>
#include "rsx_vp_decompiler.h"
#include "rsx_test_programs.h"

static char hlsl[262144];

static int build(int pull, const u8* vp, u32 n)
{
    rsx_vp_set_pull(pull);
    int ni = rsx_vp_decompile(vp, n, hlsl, sizeof hlsl);
    rsx_vp_set_pull(0);
    if (ni <= 0) { printf("decompile failed (%d)\n", ni); return 1; }
    ID3DBlob* code = NULL; ID3DBlob* err = NULL;
    HRESULT hr = D3DCompile(hlsl, strlen(hlsl), "vpull_vs", NULL, NULL, "main", "vs_5_0", 0, 0, &code, &err);
    if (FAILED(hr)) { printf("%s: compile FAILED: %s\n", pull ? "pull" : "attr", err ? (const char*)ID3D10Blob_GetBufferPointer(err) : "?"); return 1; }
    ID3D11ShaderReflection* r = NULL;
    D3DReflect(ID3D10Blob_GetBufferPointer(code), ID3D10Blob_GetBufferSize(code), &IID_ID3D11ShaderReflection, (void**)&r);
    D3D11_SHADER_DESC sd; r->lpVtbl->GetDesc(r, &sd);
    printf("%s: %u instrs, inputs:", pull ? "pull" : "attr", sd.InstructionCount);
    for (UINT i = 0; i < sd.InputParameters; i++) {
        D3D11_SIGNATURE_PARAMETER_DESC pd; r->lpVtbl->GetInputParameterDesc(r, i, &pd);
        printf(" %s%u", pd.SemanticName, pd.SemanticIndex);
    }
    printf(" | bound resources:");
    for (UINT i = 0; i < sd.BoundResources; i++) {
        D3D11_SHADER_INPUT_BIND_DESC bd; r->lpVtbl->GetResourceBindingDesc(r, i, &bd);
        printf(" %s(%c%u)", bd.Name, bd.Type == D3D_SIT_CBUFFER ? 'b' : 't', bd.BindPoint);
    }
    printf("\n");
    return 0;
}

int main(void)
{
    u8 vp[48];
    rsx_test_vp_mov_out(vp +  0, 0, RSX_TEST_VP_SWZ_IDENT, 0, 0);   /* MOV o0, v0  (position) */
    rsx_test_vp_mov_out(vp + 16, 3, RSX_TEST_VP_SWZ_IDENT, 1, 0);   /* MOV o1, v3  (colour)   */
    rsx_test_vp_mov_out(vp + 32, 8, RSX_TEST_VP_SWZ_IDENT, 7, 1);   /* MOV o7, v8  (tex0), END */
    int bad = build(0, vp, sizeof vp) + build(1, vp, sizeof vp);
    return bad;
}
