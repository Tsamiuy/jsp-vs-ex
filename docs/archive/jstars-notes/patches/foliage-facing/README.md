# Black foliage: the leaf shaders read the front/back-face input, which was always zero

Patch: `foliage-facing.patch`, `libs/video/rsx_fp_decompiler.c` only (+20/-1). It goes on top of
713f72a (`git apply --check` passes on the main checkout as of this writing). Opt-in:
`FP_FACING=1`. Not built or run. This is a draft for the AI assistant, pending the maintainer's go.

## What we know

- The maintainer compared the v5 Goku shot with the RPCS3 shot of the same spot. The black shards are the
  blue plants, drawn black.
- They are cut-out leaf shapes, so alpha test and the texture's alpha work; only the colour is
  black. They appear with shadows off too (`run\shots_v5_5_zetaonly\shot_230s.png`), and no v5
  switch changes them.
- In the v2 shot they could not be seen: the water under them was black too.

## Likely causes, ranked (inferred from the capture; nothing run)

1. **The FACING input reads as zero.**
   - The leaf cards are tiny draws: one quad or triangle (`n=3`/`n=4`), e.g. fp `0x0FC03741` and
     `0x0FC040C1` in `run\rsxcap_skin\frame_06`.
   - They contain `r[0] * (float4(0,0,0,0)).xxxx`: a vector scaled by an input register the
     decompiler cannot supply.
   - These shaders read no TEX8/TEX9-style shadow taps, and a scalar `.xxxx` multiply of a vector
     is the classic two-sided-lighting step: "flip the normal on back faces" with FACING (input
     `0xE`, RSX "SSA", ±1).
   - The decompiler maps `0xE` to zero ("FACING not plumbed"). So the normal is zero, the lighting
     terms vanish, and the leaves come out black. The rest of each texture (alpha cut-out) is
     unaffected, which matches the sharp black leaf shapes.
2. **TEX9 mapping (from v3).** If one of these programs reads TEX9 instead and `o[6]` is not where
   J-Stars writes it, the value is wrong. Test with `FP_TC89=0`. That would only matter if the
   shards differ between v2 and v3, which can't be seen because of the black water.
3. **Texture format, alpha or blending: unlikely.** The cut-out is correct and the leaf textures
   in this capture are DXT1/DXT5 (`0x86`/`0x88`) like everything else.

## What the patch does

- With `FP_FACING=1`, input `0xE` becomes `(front ? +1 : -1)` in all four lanes (`SV_IsFrontFace`),
  as RPCS3 feeds it.
- It is declared in `PSInput` only for programs that read it, so other shaders are byte-identical.
- D3D's front face follows the guest's front-face setting already (`FrontCounterClockwise` from
  the cull state).
- Checked: a FACING read through the patched decompiler compiles with `fxc ps_5_0`.

## What the AI assistant should run (Goku card, `PS3_VERBOSE=0`, v5 build + this patch)

1. **`ZETA_TARGETS=1 SHADOW_PASSES=1 FP_FACING=1`:** the shards should turn into blue plants.
2. **Same without `FP_FACING`:** the shards come back. This is the A/B.
3. **If it does not help, `FP_TC89=0`** (cause 2). If neither does, take an `RSX_CAP_PASSES=2`
   capture at the card and look at the leaf draws' FPs.
4. **Re-check the Ace card and the title** with `FP_FACING=1`. Every program that reads FACING
   changes; check for other two-sided materials, such as cloth and hair.
