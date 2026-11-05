# Zeta targets v4: A8R8G8B8 views of a depth surface are raw bytes, not a comparison

Patch: `zeta-targets-v4.patch` (`libs/video/rsx_d3d12_backend.c`, +28/-3), on top of f752afd
(`git apply --check` passes; the main checkout was clean). Not built or run. Zeta targets and
shadow passes stay opt-in.

## Why `ZETA_TARGETS=1` alone renders a black frame

Two facts from `run\stderr_v3b_zeta.txt` (`ZPASS_DBG`) and `run\rsxcap_skin`:
- **The scene's depth buffer is read as A8R8G8B8 with a compare function set.** The scene zeta
  `0x0E960000` is sampled by fp `0x0FC9F101` at t0 and by other programs at t15. They all use
  format `0xA5`, remap `0xAA93` and **zfunc 4**, so v2/v3 treated every one as a depth comparison.
- **The fog pass uses those bytes to rebuild the depth.** fp `0x0FC9ED81` (frame_01 op305) is a
  blended full-screen pass. It reads t0 = the scene zeta, floors the colour channels and computes
  `dot(rgb, (1/256, 1/65536, 1/2^24)) * 2`, constants `fp_constants[7]`.

So the A8R8G8B8 view is a byte bitcast even with a compare function set, which is also how RPCS3
treats it. Given 0/1 comparison results instead, the fog pass produces garbage over the whole frame.

v1's black silhouettes under the bitcast had another cause. The shadow lookups had no coordinates
(TEX8/TEX9, fixed in v3).

## What v4 changes

- **A8R8G8B8 views of a depth buffer are bitcast again, whatever the compare function.** Only
  DEPTH24_D8/DEPTH16 views compare. `ZPASS_ARGB_CMP=1` gives v2/v3's behaviour.
  `ZPASS_ARGB_BITCAST` no longer does anything.
- **New test switch `ZPASS_CMP_OFF=1`.** DEPTH24_D8/DEPTH16 units never get the real depth; they
  read as "lit", as before the shadow work. Use it to judge the black triangles below.

## The black triangles in the water (`run\shots_v3b_shadow\shot_230s.png`)

They are inferred to be **hard cast shadows, not missing geometry.** Their spiky outline repeats
Goku's hair just beside it, and they lie only on the ground/water.

That ground shader (fp `0x0FBFFFC1`, about 110 instructions) reads the cascades through the
DEPTH24_D8 view (`0xB0`, zfunc 4 = GREATER), a real comparison. The placement looks plausible; the
darkness may not be.

Candidates, in order:
1. **Too dark or too hard.** The shader mixes towards a near-black shadow colour
   (`fp_constants[16..20]` ~ 0.078). On RSX a linear filter turns each tap into a 2x2 PCF and
   softens edges. Our comparison is per-tap unless the min filter is linear.
2. **Wrong reference depth.** If TEX9 (now `o[6]`) is not what the game writes there, the reference
   z is wrong and shadows land where they shouldn't.
3. **No polygon offset.** That would show as acne (stripes), not as these shapes, so it is less
   likely.

## Checked

- Syntax-only compile with the build's own warning set (this is how v3's missing declaration
  should have been caught; the v3 check used `-Wno-everything`): no errors, no
  implicit-declaration diagnostics.
- `git apply --check` on f752afd.

## What the AI assistant should run (all with `PS3_VERBOSE=0`, Goku card screenshots)

1. **`ZETA_TARGETS=1 ZPASS_DBG=1`:** the frame should render now. The `[ZPASS]` sampler lines
   should say "A8R8G8B8 bitcast" for the `0xA5` units at t0/t15 and for t7/t8 of the character
   material.
2. **`ZETA_TARGETS=1 SHADOW_PASSES=1`:** expect blue water as in v3.
   - Characters now read their cascades as bytes. If they go dark, compare with
     `ZPASS_ARGB_CMP=1`, which is v3's look.
3. **The same with `ZPASS_CMP_OFF=1`:**
   - if the black triangles vanish, they are the ground's cast shadows (candidate 1 or 2);
   - if they stay, they are something else, such as culled or missing water triangles. A
     capture with `RSX_CAP_PASSES=2` of that frame would then show which draw.
4. **For the open question of shadow darkness:** an RPCS3 shot of this exact Goku close-up would
   show whether RPCS3 draws the ground with hard dark shadows too.
