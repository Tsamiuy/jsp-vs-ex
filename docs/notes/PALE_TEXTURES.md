# Pale / "camo" surfaces in the demo match: texture-side study (2026-10-20)

Read-only study. Nothing was built, run or edited for this file. Sources: `ps3recomp-src\libs\video`
at commit e32a781 plus the uncommitted backend edits of the time, captures in `jstars\run`
(`rsxcap_exp32`, `rsxcap_flush1`, `rsxcap_pass2`), `run\duck_fp.hlsl` (a J-Stars material FP,
fp `0x0FC44801`, picked on 2026-10-19), screenshots `demo_after_strip_restart.png` and
`run\shots_culloff\shot_260s.png`.

**Symptom:** Ace's skin and hat come out pale cream instead of tan/orange (washed out and
desaturated), with darker tan blotches with sharp edges across them (the "camo"). `CULL_OFF=1` does
not change the blotches (§25).

## What the game feeds the texture units

- Material draws are almost all DXT: in `rsxcap_exp32\frame_00\manifest.txt` 99% of bound units are
  `0x86` DXT1 / `0x88` DXT5, swizzle flag set (`SZ`), 128-2048 px. Linear `0xA5` A8R8G8B8 shows up
  only for render targets that are read back as textures, plus a few NPOT DXT UI images (`0xA6`/`0xA8`).
- A typical material binds t0 = DXT1 albedo, t1 = DXT5, t2 = `0x0D840000` (320x180 linear RT, from
  the previous frame's post chain), and t3 = DXT5 256x256.
- `duck_fp.hlsl` (a material FP, 4 colour outputs) shows the roles:
  - t3/t4: DXT5 normal maps, read `.yw` then `*2-1`;
  - t0: albedo;
  - t1/t2: two colour layers mixed by `col0`;
  - **t7/t8: 8 projective taps (`.xy/.w`) that read `.x`, summed and scaled by 1/3. This is
    shadow-map PCF.**
- `rsxcap_pass2\frame_00\fp_op0002.hlsl` lists what is still bound on units 4-15:
  - t7 `0x0CE40000` 2048x1024 `0xA5` remap `0xAA93`;
  - t8 `0x0CA30000` 1024x1024 `0xA5` remap `0xAA93`;
  - t15 `0x0E960000` 1280x720.

  None of them is a tracked RT (`rt_slot=-1`).

## Likely causes, ranked

### 1. Texture gamma (sRGB decode) is ignored → everything washed out (the "pale")

- **Code:**
  - `d3d12_bind_texture` never reads `tex->address`, `filter` or `control0`, and the draw record's
    `tex[]` has no field for them.
  - `vp_get_fp_pso` calls `rsx_fp_decompile_buffered_ex` with no gamma masks.
  - The shared code already has the pieces, but only `rsx_draw_engine.c` uses them:
    `rsx_texture_gamma_mask(format, control1, filter, address)` and
    `rsx_fp_decompile_buffered_color_ex(..., gamma_masks, ...)`.
- **Why it fits:**
  - The tone-map pass (`rsxcap_flush1\frame_00\fp_op0325.hlsl`) ends with pow(x, 0.4545), so the
    scene RT is meant to hold linear colour.
  - The material FP does no log2/exp2 on the albedo sample, so the linearizing must come from the
    texture unit's gamma bits (ADDRESS bits 20-23).
  - Skipping that and then encoding again lifts every colour. Orange (0.9, 0.5, 0.2) becomes
    (0.95, 0.73, 0.48), which is the cream on screen; red beads turn pinkish.
  - The title and menus are not tone-mapped, which is why they still look right.
- **Check (one log line):** in `d3d12_bind_texture`, print `(tex->address >> 20) & 0xF` per unit
  next to `TEXFMTDBG`. If DXT albedo units show non-zero gamma, this is confirmed.
- **Fix direction:**
  - Record `address` and `filter` per draw unit.
  - Build `gamma_masks[16]` with `rsx_texture_gamma_mask` and decompile through `..._color_ex`.
  - Add the mask to the FP PSO key and failure cache, the same way `exp32` was added (§23).
  - Also check SHADER_PACKER (0x1FEC, `rsx_fp_srgb_output_enabled`), which the D3D12 backend never
    reads either.

### 2. Shadow cascades are never rendered; materials sample empty guest memory (the "camo")

- **Code:**
  - `rsx_no_color_target()` (comment above `current_rt_off`) drops every depth-only pass. J-Stars'
    shadow cascades are exactly those passes: 2048x1024 and 1024x1024, colour target NONE, zeta only.
  - So nothing ever writes `0x0CE40000` / `0x0CA30000`.
  - The material's t7/t8 are uploaded from that untouched guest memory every frame
    (`vp_upload_tex_slot`, 0xA5 path) and fed to the PCF taps.
- **Why it fits:** the shadow term comes out of whatever bytes sit there. Sharp-edged blotches that
  follow the light's projection, and do not change with culling, are what a shadow mask built from
  garbage looks like. If the memory is all zero the term is flat instead, and the blotches come from
  cause 6.
- **Check:**
  - **No code needed:** run with `TEX_MARKEMPTY=1` and look for `[TEXEMPTY] off=0x...CE40000 2048x1024`
    (and the 1024x1024 one). If they are listed, the maps are empty; if not, they hold stale data.
  - Then force those two units to a constant and watch whether the blotches change: an all-0xFF
    texture or a null SRV.
- **Fix direction:**
  - Record depth-only passes into a D3D depth target keyed by the zeta offset.
  - When a texture is bound at that raw offset, give the shader the depth the way RSX does: D24S8
    read back as A8R8G8B8, so honour remap `0xAA93`. RPCS3 does the same with a depth-to-RGBA8
    conversion.
  - Mind the vertex budget: the cascades were about 390k vertices, which is why they were dropped
    (§22 mid-frame flush now covers overflow).
  - Interim: bind a "fully lit" constant to those units.

### 3. Fixed samplers: per-unit filter, wrap, LOD and border are all ignored

- The root signature has 16 static samplers: LINEAR + WRAP for every unit, except **s0 = POINT + CLAMP**
  (kept for the old debug-font atlas).
- J-Stars puts the albedo on t0, so tiled UVs clamp to the edge texel. That gives the smeared or
  stretched surfaces noted in §23, with blocky point sampling on top.
- The shadow units wrap, so taps past a cascade edge read the opposite side.
- **Check:** give s0 LINEAR + WRAP for VP draws (one line) and compare a screenshot.
- **Fix:** build samplers per draw from FILTER / ADDRESS / CONTROL0 (a sampler heap, keyed by state).

### 4. No mip levels

- Every texture is created with `MipLevels = 1`, and only level 0 is uploaded.
- With 1024-2048 px DXT on small or distant models, this gives sparkle and shimmer noise. It does not
  explain the paleness.

### 5. Swizzled-flag DXT is treated as linear (uncertain)

- `rsx_texture_layout_pitched` assumes compressed data is never Morton-swizzled, and copies the
  blocks straight.
- Almost every J-Stars texture is DXT with the swizzle flag set. If that assumption were wrong,
  4x4-block regions of each atlas would land in the wrong place, which would give patchy colour.
- The title screen renders correctly, which argues against it.
- **Check:** `TEXFMTDBG=1` on a title-only run. If the title also binds `SZ` DXT and looks right, rule
  this out.

### 6. Not texture-side, but feeds the same look

- The toon lighting uses DXT5 normal maps (t3/t4 `.yw`) plus vertex normals and tangents
  (`tc6`/`tc7`).
- Wrong vertex normals or UVs from attribute decode would also give sharp-edged light/shadow
  patches. §25 already shows folded triangles, and the AI assistant is on this.

### 7. Smaller gaps (low for this symptom)

- **Stale texture bindings.**
  - `d3d12_bind_texture` only records formats 0x81/0x85/0x8B/0x86-0x88/0x9A. For any other format
    (depth 0x90/0x92, 0x9E, 16-bit colour) the unit keeps the *previous* texture.
  - Units are never cleared when CONTROL0's enable bit (31) drops.
  - 0x9A is accepted at bind but rejected at upload, so it gets a null SRV (samples black).
- **RTs sampled directly** use `D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING`, so their CONTROL1 remap
  is lost. This matters as soon as cause 2's depth targets are served (`0xAA93`).
- **Linear row pitch (CONTROL3)** is not passed to `rsx_texture_layout`. For J-Stars this is probably
  harmless (RT pitches = width x 4), but it is wrong in general.
- **The capture manifest** lists only t0-t3. Extending it to t0-t15 would show the shadow units in
  every capture.

## Suggested order for the AI assistant

1. Run once with `TEX_MARKEMPTY=1`, plus the one-line gamma log from cause 1. That is one run, and it
   answers causes 1 and 2.
2. If gamma bits are set: implement cause 1. It is mechanical, because the helpers exist.
3. Interim "fully lit" for the shadow units, to separate the camo from vertex-normal problems.
4. s0 LINEAR + WRAP for VP draws.
5. Then the real depth-only pass support (cause 2) and per-draw samplers (cause 3).
