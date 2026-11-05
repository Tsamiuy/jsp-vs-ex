# Unit 0: use the game's own wrap mode per draw (keep the current filter)

Patch: `unit0-wrap.patch` (`libs/video/rsx_d3d12_backend.c`, +69/-2), on HEAD 077fea4.
`git apply --check --cached` passes against HEAD. It does **not** apply to the main checkout as it
is now, because the working tree already holds the AI assistant's own uncommitted version of the same idea
(`T0_WRAP`, see "the AI assistant's working-tree version" below). Checked with a syntax-only compile using the
build's warnings: no errors, and the only new warnings are of style kinds the file already has
thousands of. Not built or run.

## Why

FINDINGS §57: `S0_LINEAR=1` removed the Athena sky lines. Unit 0's static sampler (s0) is
POINT + CLAMP, kept for the debug-font atlas. The game's star layer (fp `0x09CA7F81`, 1024x1024
DXT5, STAGE_TEXTURES.md §5) is tiled, so CLAMP repeated its edge texels into lines. `S0_LINEAR`
switches both the filter and the wrap mode. The plant boxes it causes (Penguin Village) come from
the LINEAR filter. Wrap alone cannot add colour around an alpha-tested leaf, because a leaf's UVs
stay inside 0..1.

## What it does

- **Where the forced clamp is today:** the root signature in `init_d3d12`. The `if
  (!getenv("S0_LINEAR"))` block sets `samp[0]` to POINT + CLAMP. Every fragment program samples
  unit 0 as `rsx_samp[0]`, which is register s0.
- **New static samplers s16..s23:** copies of s0 (same filter, so `S0_LINEAR` still behaves as
  before), with U/V in {wrap, mirror, clamp}. Clamp/clamp is left out because that is s0.
- **Per draw:** `u0_wrap_mode(dr)` reads unit 0's TEXTURE_ADDRESS word, which the draw record
  already keeps in `tex[0].addr`:
  - wrap S is bits 0-3, wrap T is bits 8-11;
  - 1 means wrap and 2 means mirror;
  - everything else (3 clamp-to-edge, 4 border, 5 clamp, 6-8 mirror-once) counts as clamp.
  - Clamp on both axes keeps s0, so those draws build exactly the same pipelines as today.
- **Wiring:** the callers set `s_u0_mode` next to `s_idcolor_draw`, at both `vp_get_fp_pso`
  calls. In `vp_get_fp_pso`:
  - the code hash is salted by the mode, so these draws get their own pipelines (same pattern as
    `FP_IDCOLOR_RT`);
  - after decompiling, every `rsx_samp[0],` becomes `_rsx_samp0a,`, declared as the chosen
    register just above `main`.
  - Units read through the depth helpers (shadow compare or bitcast on unit 0) are left alone.
- **Switches:** `U0_WRAP=0` turns it off. `U0_WRAP_LOG=1` prints `[U0WRAP] fp=... code=... s=wrap
  t=wrap (s16)` once per program and mode. That line also answers "does the game ask for wrap here?"

## The AI assistant's working-tree version (`T0_WRAP`), compared

The main checkout already has an uncommitted `T0_WRAP` that does the same thing with s16-s18
(wrap/wrap, wrap/clamp, clamp/wrap). It is fine to keep that one instead of this patch. Three
points worth folding in:

1. **Mirror (code 2) stays clamp there.** Any tiled texture that asks for mirror shows the same
   edge lines. This patch maps it. If the log shows no mirror on J-Stars, ignore it.
2. **Its rewrite starts at the `rsx_samp[16]` declaration**, so it also renames `rsx_samp[0]`
   inside the shadow and bitcast helpers that are inserted ahead of `main`. That only matters if
   unit 0 is ever a zeta unit (not seen so far), but this patch skips those units.
3. **Its samplers hardcode POINT.** With `S0_LINEAR=1` they would still be POINT, unlike s0. That
   is minor and only affects that diagnostic.

Both versions salt the hash even when the program never samples unit 0. That costs one extra
pipeline build per program and mode (the HLSL-keyed bytecode cache saves the D3DCompile). It is
harmless, but on first sight of a stage it can show up as a few more shader hitches.

## Risks

- **Stages that relied on the forced clamp:** none known. Only draws that ask for wrap or mirror
  change, and on RSX those textures were wrapping already. The one known user that needs clamp, the
  debug font atlas, draws with the fixed atlas shader rather than `vp_get_fp_pso` (inferred from
  the code), and in any case it would keep s0 unless it asks for wrap.
- **Textures that ask for wrap but whose UVs touch 1.0 exactly** (UI panels, decals): with POINT
  filtering, wrap at exactly 1.0 can read the opposite edge's texel, a 1-pixel seam. RSX does the
  same, so a seam that matches RPCS3 is correct.
- **More pipelines:** see above. The first visit to each stage may hitch a little more.
- **The filter is unchanged:** Athena's stars stay point-sampled, so they may shimmer a little
  next to RPCS3. Proper per-unit filtering is the later per-draw sampler work (STAGE_PLAN #9).

## Test plan (the AI assistant)

1. Athena, default settings: the sky should show a tiled star field with no lines, matching
   `stages\athena\athena_tiling.png` (the `S0_LINEAR` run) but with sharp texels. Add
   `U0_WRAP_LOG=1` and check that `fp=0x09CA7F81` reports `s=wrap t=wrap`.
2. Penguin Village: no plant boxes (the filter is unchanged).
3. Dark Tournament, facing the ring side (not the broken wall): rubble faces should be textured
   instead of flat grey or streaky. If not, unit 0 is not the rubble's cause.
4. Quick look at Yoshiwara, Namek and one menu: nothing should change apart from tiled textures
   that were smeared before.
5. `U0_WRAP=0` on Athena brings the lines back (the switch works).
