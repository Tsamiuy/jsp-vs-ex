# Zeta targets v5: the black triangles are the blue foliage

Patch: `zeta-targets-v5.patch`, on top of f752afd. It **includes v4**, so apply this one
instead of v4. It touches `rsx_d3d12_backend.c`, `rsx_commands.c` and `rsx_commands.h`.
`git apply --check` passes, and a syntax-only compile with the build's warnings shows no errors.
Not built or run.

## What the black triangles are

The user's RPCS3 shot of the same Goku card has blue, coral-like spiky plants and small bushes
scattered over the ground/water near Goku. That shot was posted in the project chat; it is not
saved in `reference\rpcs3` yet. The newest file there is `184154.png`, an earlier moment of the
card, which shows the same plants.

Our shots are consistent with that:
- **`shots_zeta2\shot_230s.png`** (zeta v2, no shadow passes): the same foliage is drawn in colour,
  as blue and teal patches.
- **`shots_v3b_shadow\shot_230s.png`** (shadow passes on, TEX8/9 fixed): it comes out as black
  spiky shapes.

So the black triangles are the foliage, not shadows on the ground. They go black only when the
shadow maps hold real depth. This corrects v4, where I read them as cast shadows.

## Likely causes, ranked (inferred from code and shots)

1. **Self-shadowing acne on the leaves.** The plants are drawn into the shadow maps and then read
   the same maps. RSX casters use polygon offset (`glPolygonOffset`) to push their depth back.
   This backend never decoded it (methods 0x0A68/0x0A78/0x0A7C), so every leaf compares against
   its own depth. Thin alpha-cut leaves facing the light come out fully "in shadow", and the shader
   mixes them towards its near-black shadow colour (`fp_constants[16..20]` ~ 0.078).
2. **Comparison vs bitcast on the A8R8G8B8 views.** v4 made every A8R8G8B8 depth view a byte
   bitcast. That is right for the fog pass, but the material shadow taps (TXP, summing `.x`) want
   comparison results. If the foliage reads the maps through such a view, v4 alone would turn its
   taps into raw bytes. v5 decides per program: **TXP lookups compare, TEX reads are bitcast**.
3. **Not a texture problem.** In v2 the foliage drew in colour with the same slots, alpha test,
   formats and sRGB handling. Only the shadow-map contents changed, so the 512-slot change, alpha
   test, unit 1 and sRGB are unlikely causes.

## What v5 changes (on top of v4)

1. **Polygon offset is decoded.**
   - `rsx_commands.c`/`.h` decode `POLY_OFFSET_FILL_ENABLE`, `POLYGON_OFFSET_SCALE_FACTOR` and
     `POLYGON_OFFSET_BIAS`.
   - Each op records them, and depth-only (and kept-depth) PSOs get
     `DepthBias = units` / `SlopeScaledDepthBias = factor` (the D3D equivalent of GL's offset for
     UNORM depth).
   - Off: `ZPASS_NO_POLYOFFSET=1`.
2. **A8R8G8B8 + zfunc views by opcode.** A unit read with TXP compares; a unit read with TEX is a
   bitcast. The ucode is scanned per program. Off: `ZPASS_ARGB_TXP=0` (v4: always bitcast);
   `ZPASS_ARGB_CMP=1` (v2/v3: always compare).
3. **From v4:** `ZPASS_CMP_OFF=1` stays as the test switch (no real depth for
   DEPTH24_D8/DEPTH16 units).

## What the AI assistant should run (Goku card, `PS3_VERBOSE=0`)

1. **`ZETA_TARGETS=1 SHADOW_PASSES=1 ZPASS_DBG=1`:** the plants should be blue/teal like RPCS3.
   The `[ZPASS]` lines should show t7/t8 of the materials as "depth compare" and the fog pass t0 as
   "A8R8G8B8 bitcast".
2. **The same with `ZPASS_NO_POLYOFFSET=1`:** if the plants go black again, polygon offset was the
   fix (cause 1).
3. **The same with `ZPASS_ARGB_TXP=0`:** if the plants or characters change, the opcode rule
   matters (cause 2).
4. **The same with `ZPASS_CMP_OFF=1`:** the ground shader's comparison is off. Compare how much
   darker the ground is with it on.
5. **`ZETA_TARGETS=1` alone:** the frame should render now (fog pass bitcast, from v4).
