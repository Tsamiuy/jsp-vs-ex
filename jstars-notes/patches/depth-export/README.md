# Fragment depth export (the leaf and grass cut-out)

Patch: `depth-export.patch` (`libs/video/rsx_d3d12_backend.c`, about +30/-8), on 1565a6b.
`git apply --check` passes, and a syntax-only compile with the build's warnings shows no errors and
no new warnings. Not built or run. On by default; `FP_DEPTH_EXPORT=0` turns it off.

## Finding (the AI assistant's capture `stages\leafcap\id_cap\frame_00`, FINDINGS §62; checked in code)

- The tree cards (`fp_op0218.hlsl`, code 0x8E610328, t0 DXT1) have no alpha test and no A2C. They
  cut out through **depth export**:
  - `r1.w = 10` (fp_constants[0]);
  - `cc0.x = (0.5 >= tex.a * 2 / 3)`, i.e. the texel is transparent;
  - where it is not transparent, `r1.w = position.z`;
  - finally `r1.z = r1.w`.

  r1.z is RSX's depth export. Transparent texels get depth 10, which clamps to the far plane and
  fails the LESS depth test, so they are dropped. Opaque texels keep their own depth. The grass
  (`fp_op0215.hlsl`, code 0x70B1A414, 3 targets) works the same way.
- The decompiler supports depth export (`depth_export = ctrl & 0xE`: PSOutput with `SV_Depth`,
  `_out.depth = saturate(r1.z)`). But **the backend never passes the depth bits.** It records only
  `shader_control & 0x40` (`fp_exp32`) and builds the control word as `exp32 ? 0x40 : 0`. The
  comment at `fp_ctrl` says so ("the depth-export bits (0xE) stay off, as before"). So these
  programs return plain colour, the cards keep the card's depth, and they draw whole.

## What it does

- `rsx_fp_exp_bits()` at the three record sites: `fp_exp32` bit 0 = 32-bit exports (0x40), bit 1 =
  depth export (0xE).
- `fp_ctrl_bits()` gives the decompiler 0x40 and/or 0x2 from those bits. It is used in
  `vp_get_fp_pso` and in the RSX_CAP shader dump, so the captures show the same HLSL.
- `fp_exp32` is already part of the pipeline key, so programs with and without depth export get
  separate pipelines.

## Risks

- **Every program the game marks with depth export now writes depth.** That is RSX's own behaviour,
  but it is new here. Full-screen passes that rebuild depth will now really write it, which should
  fix rather than break them, but check Yoshiwara's floor and fog.
- **Speed:** SV_Depth turns off early depth rejection for those programs. That is foliage and a few
  passes; check fps on Hidden Leaf.
- **A card drawn with depth function LEQUAL** against a cleared 1.0 would still pass at the clamped
  1.0. The captured leaf draws use LESS (zstate 0x113).

## Test plan

1. Hidden Leaf intro and fight: trees cut out against the sky, and grass as separate blades. A/B
   against `FP_DEPTH_EXPORT=0`. Compare with RPCS3 `reference\rpcs3\Screenshot 2026-10-22 123038.png`.
2. Penguin Village and Alabasta palms, Kameari intro: plants and palms without black cards.
3. Yoshiwara and Athena fight: nothing missing, the floor and fog unchanged or better, fps similar.
4. A pass capture of the Hidden Leaf frame: `fp_op0218.hlsl` should now end in `_out.depth = ...`.
