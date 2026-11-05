/* Offline test for the TEX_EXPAND port (upstream ps3recomp 122690f) on the
 * jstars-port decompiler. Build from the scratchpad:
 *   gcc -std=c11 -I<work>/libs/video -I<ours>/include test_expand.c \
 *       <work>/libs/video/rsx_fp_decompiler.c -o test_expand && ./test_expand */
#include <stdio.h>
#include <string.h>
#include "rsx_fp_decompiler.h"

static int g_pass = 0, g_fail = 0;
static void put_word(u8* p, u32 h)
{
    const u32 be = (h << 16) | (h >> 16);   /* rsx_fp_read_word undoes this */
    p[0] = (u8)(be >> 24); p[1] = (u8)(be >> 16); p[2] = (u8)(be >> 8); p[3] = (u8)be;
}
static void check(const char* name, const char* hlsl, const char* needle)
{
    if (strstr(hlsl, needle)) { g_pass++; printf("  ok   %s\n", name); }
    else { g_fail++; printf("  FAIL %s: missing \"%s\"\n", name, needle); }
}
static void check_absent(const char* name, const char* hlsl, const char* needle)
{
    if (!strstr(hlsl, needle)) { g_pass++; printf("  ok   %s\n", name); }
    else { g_fail++; printf("  FAIL %s: found \"%s\"\n", name, needle); }
}
static void check_true(const char* name, int v)
{
    if (v) { g_pass++; printf("  ok   %s\n", name); }
    else { g_fail++; printf("  FAIL %s\n", name); }
}

#define OPC(x)      ((u32)(x) << 24)
#define OUTMASK_ALL (0xFu << 9)
#define END         (1u << 0)
#define INSRC(x)    ((u32)(x) << 13)
#define TEXU(x)     ((u32)(x) << 17)
#define SWZ_IDENT   ((0u<<9)|(1u<<11)|(2u<<13)|(3u<<15))
#define T_INPUT     1u
#define EXEC_ALWAYS ((7u << 18) | (0u << 21) | (1u << 23) | (2u << 25) | (3u << 27))
#define CTRL_32BIT  0x40u

int main(void)
{
    static char hlsl[65536], plain[65536];
    u8 prog[16];
    u8 expand[16] = {0};
    u32 nk = 0;

    /* TEX TC0 with EXP_TEX (_bx2) on a biased unit */
    put_word(prog + 0, OPC(0x17) | OUTMASK_ALL | INSRC(4) | TEXU(0) | (1u << 21) | END);
    put_word(prog + 4, T_INPUT | SWZ_IDENT | EXEC_ALWAYS);
    put_word(prog + 8, 0);
    put_word(prog + 12, 0);
    expand[0] = 0x7;
    check_true("expand_units", rsx_fp_texture_expand_units(prog, sizeof prog, expand) == 1u);
    check_true("bx2_units", rsx_fp_texture_bx2_units(prog, sizeof prog) == 1u);
    rsx_fp_decompile_buffered_shadow_color_expand_ex(prog, sizeof prog, CTRL_32BIT, 0, NULL, NULL,
                                                     expand, hlsl, sizeof hlsl, &nk);
    check("expand_wrapped", hlsl, "_rsx_expand_input(rsx_tex[0].Sample(rsx_samp[0], ((input.tc0).xyzw).xy), 15u, true)");
    check("expand_helper",  hlsl, "float4 _rsx_expand_input(float4 v, uint mask, bool bx2)");
    check("expand_math",    hlsl, "v * 2.0 - 1.0");

    /* jstars-port gating: the older entry points never expand, even on _bx2 */
    rsx_fp_decompile_buffered_shadow_color_ex(prog, sizeof prog, CTRL_32BIT, 0, NULL, NULL,
                                              plain, sizeof plain, &nk);
    check_absent("old_entry_no_expand", plain, "_rsx_expand_input");
    check_absent("old_entry_no_helper", plain, "float4 _rsx_expand_input");
    rsx_fp_decompile_buffered_ex(prog, sizeof prog, CTRL_32BIT, 0, plain, sizeof plain, &nk);
    check_absent("buffered_entry_no_expand", plain, "_rsx_expand_input");

    /* new entry point with a NULL mask is byte-identical to the old one */
    rsx_fp_decompile_buffered_shadow_color_expand_ex(prog, sizeof prog, CTRL_32BIT, 0, NULL, NULL,
                                                     NULL, hlsl, sizeof hlsl, &nk);
    rsx_fp_decompile_buffered_shadow_color_ex(prog, sizeof prog, CTRL_32BIT, 0, NULL, NULL,
                                              plain, sizeof plain, &nk);
    check_true("null_mask_identical", strcmp(plain, hlsl) == 0);

    /* unit not biased: _bx2 still expands every lane when the feature is on */
    expand[0] = 0;
    check_true("expand_units_none", rsx_fp_texture_expand_units(prog, sizeof prog, expand) == 0u);
    rsx_fp_decompile_buffered_shadow_color_expand_ex(prog, sizeof prog, CTRL_32BIT, 0, NULL, NULL,
                                                     expand, hlsl, sizeof hlsl, &nk);
    check("expand_bx2_unbiased", hlsl, "_rsx_expand_input(rsx_tex[0].Sample(rsx_samp[0], ((input.tc0).xyzw).xy), 15u, true)");

    /* biased, no EXP_TEX: re-centred with the 8-bit formula */
    expand[0] = 0x7;
    put_word(prog + 0, OPC(0x17) | OUTMASK_ALL | INSRC(4) | TEXU(0) | END);
    check_true("expand_units_nobit", rsx_fp_texture_expand_units(prog, sizeof prog, expand) == 1u);
    rsx_fp_decompile_buffered_shadow_color_expand_ex(prog, sizeof prog, CTRL_32BIT, 0, NULL, NULL,
                                                     expand, hlsl, sizeof hlsl, &nk);
    check("expand_nobit_wrapped", hlsl, "_rsx_expand_input(rsx_tex[0].Sample(rsx_samp[0], ((input.tc0).xyzw).xy), 7u, false)");
    check("expand_nobit_math",    hlsl, "(floor(v * 255.0 + 0.5) - 128.0) / 127.0");

    /* gamma lanes are decoded, not re-centred */
    u8 gamma[16] = {0}; gamma[0] = 0x7; expand[0] = 0xF;
    rsx_fp_decompile_buffered_shadow_color_expand_ex(prog, sizeof prog, CTRL_32BIT, 0, NULL, gamma,
                                                     expand, hlsl, sizeof hlsl, &nk);
    check("expand_after_gamma", hlsl,
          "_rsx_expand_input(_rsx_gamma_input(rsx_tex[0].Sample(rsx_samp[0], ((input.tc0).xyzw).xy), 7u), 8u, false)");
    gamma[0] = 0xF;
    rsx_fp_decompile_buffered_shadow_color_expand_ex(prog, sizeof prog, CTRL_32BIT, 0, NULL, gamma,
                                                     expand, hlsl, sizeof hlsl, &nk);
    check_absent("expand_all_gamma", hlsl, "_rsx_expand_input(rsx_tex");
    /* with EXP_TEX, gamma steps aside */
    put_word(prog + 0, OPC(0x17) | OUTMASK_ALL | INSRC(4) | TEXU(0) | (1u << 21) | END);
    rsx_fp_decompile_buffered_shadow_color_expand_ex(prog, sizeof prog, CTRL_32BIT, 0, NULL, gamma,
                                                     expand, hlsl, sizeof hlsl, &nk);
    check("expand_bx2_over_gamma", hlsl, "_rsx_expand_input(rsx_tex[0].Sample(rsx_samp[0], ((input.tc0).xyzw).xy), 15u, true)");
    check_absent("expand_bx2_no_gamma", hlsl, "_rsx_gamma_input(");
    /* bit 4 (16-bit channels) rides along */
    expand[0] = 0x13;
    put_word(prog + 0, OPC(0x17) | OUTMASK_ALL | INSRC(4) | TEXU(0) | END);
    rsx_fp_decompile_buffered_shadow_color_expand_ex(prog, sizeof prog, CTRL_32BIT, 0, NULL, NULL,
                                                     expand, hlsl, sizeof hlsl, &nk);
    check("expand_16bit", hlsl, "_rsx_expand_input(rsx_tex[0].Sample(rsx_samp[0], ((input.tc0).xyzw).xy), 19u, false)");

    /* a program without either feature compiles to the same text through every entry point */
    put_word(prog + 0, OPC(0x17) | OUTMASK_ALL | INSRC(4) | TEXU(0) | END);
    expand[0] = 0;
    rsx_fp_decompile_buffered_shadow_color_expand_ex(prog, sizeof prog, CTRL_32BIT, 0, NULL, NULL,
                                                     expand, hlsl, sizeof hlsl, &nk);
    rsx_fp_decompile_buffered_ex(prog, sizeof prog, CTRL_32BIT, 0, plain, sizeof plain, &nk);
    check_true("plain_identical", strcmp(plain, hlsl) == 0);
    check_absent("plain_no_helper", hlsl, "_rsx_expand_input");

    printf("Results: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
