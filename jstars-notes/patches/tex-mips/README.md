# Mipmaps (TEX_MIPS) and the blocky sky and tree edges: causes and tests

Patch: `tex-mips.patch` (`libs/video/rsx_d3d12_backend.c`, +50/-4), on top of e799d57
(`git apply --check` passes; the main checkout was clean). Opt-in: **`TEX_MIPS=1`**. A
syntax-only compile with the build's warnings shows no errors. Not built or run.

## What the patch does

- **Uploads the full mip chain for 2D textures.** Before, only level 0 was uploaded.
  - RSX layout from `rsx_texture_mip_chain`: levels packed after level 0 at their own size,
    swizzled per level when POT.
  - D3D placement from `GetCopyableFootprints`.
- **Creates the resource with that many levels.** The slot is recreated if the level count changes.
- **Exposes every level to the shader.** The texture's own view and the per-draw views both
  include all levels; render targets and depth targets have one level, so they are unchanged.
- **Leaves some textures at one level:** cube maps, textures whose format register says 1 level,
  and DXT textures whose size is not a multiple of 4 (D3D rule).
- **Sampling picks the levels up with no other change.** The static samplers already use
  MIP_LINEAR with no LOD limit on units 1-15. Unit 0 is MIP_POINT, which still selects a level.

## Survey findings (STAGES.md, screenshots in `stages\sNN`): causes ranked

### Namek ground and water grainy/noisy teal (s06)

1. **No mipmaps (most likely).** Distant tiled ground and water sample level 0 at a heavy
   minification: grain and shimmer. `TEX_MIPS=1`.

### Night ruins: streaky diagonal lines across the sky (s03, `16_fight_20s.png`)

The lines are parallel and evenly spaced with gaps between dashes, across the whole sky dome.

1. **Moiré from a tiled star or sky texture with no mipmaps (most likely).** A repeating pattern
   minified with no lower levels turns into regular stripes. `TEX_MIPS=1`.
2. **Point sampling on unit 0.** If the sky's texture is on t0, the static sampler s0 is
   MIN_MAG_MIP_POINT + CLAMP (kept for the old debug font), which makes the aliasing harsher.
   `S0_LINEAR=1` (existing test switch, commit 80eeea0).
3. **Stretched sky-dome geometry** (vertex data). Only if both switches leave the lines unchanged.

### Kameari intros: blocky, stair-stepped cloud edges and a patchy tower (s01, `13_intro_a.png`)

Penguin Village: blocky sky edges and stair-stepped palm leaves (s05, `13_intro_a.png`).

1. **Point sampling on unit 0 (most likely for the blocky clouds and palms).**
   - The clouds and palm fronds are low-resolution textures magnified on screen.
   - With s0 = POINT, each texel becomes a visible square, and alpha cut-outs (palm leaves, cloud
     edges) step along texel boundaries.
   - RPCS3 samples them with the game's linear filter.
   - Test `S0_LINEAR=1`. The real fix is per-draw samplers from TEXTURE_FILTER/ADDRESS (STAGE_PLAN
     #9), to be drafted if the switch confirms it.
2. **Alpha-to-coverage as a hard 0.5 test (`MENU_A2C=1`, on in the survey).** On the PS3 the
   coverage with MSAA softens cut-out edges. Our one-sample emulation gives a hard edge, which
   point sampling then makes blocky. Linear filtering (cause 1) softens it most of the way.
3. **No mipmaps** for the patchy tower texture at a distance. `TEX_MIPS=1`.

## What the AI assistant should run

Defaults plus `MENU_A2C=1 MENU_TEX_PITCH=1`, same tour as `STAGES.md`:

1. **s06 Namek, s03 night ruins:** `TEX_MIPS=1` against the survey shots. Expect a smooth ground and
   water and no sky stripes.
2. **s01 Kameari, s05 Penguin Village:** `S0_LINEAR=1`, then `S0_LINEAR=1 TEX_MIPS=1`. Expect smooth
   cloud and palm edges.
3. **Title, main menu and Yoshiwara with `TEX_MIPS=1`:** check that UI and fonts are unchanged.
   UI textures usually have one level, so they are unaffected. Also note `[PERF]` texture time:
   uploading the chain costs up to a third more bytes per new texture, and cached textures are not
   re-uploaded.
4. **If anything turns into garbage at a distance with `TEX_MIPS=1`,** the RSX level offsets differ
   from `rsx_texture_mip_chain`. Report which stage and surface.
