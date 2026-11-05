# Zeta targets v3: the shadow lookups' missing texcoords, hand-off scope, clear rect, depth func

Patch: `zeta-targets-v3.patch`, on top of 058bb68 (`git apply --check` passes; the main
checkout was clean). It touches three files: `rsx_d3d12_backend.c`, `rsx_fp_decompiler.c` and
`rsx_vp_decompiler.c`. Not built or run. Zeta targets stay opt-in (`ZETA_TARGETS=1`).

## What the v2 run showed, and why (from code and `run\rsxcap_skin`, nothing run)

**The root cause behind every "black silhouette": the shadow lookups had no coordinates.** Every
material that reads the cascades (for example `rsxcap_skin\frame_00\fp_op0001.hlsl`, fp
`0x0FBFFFC1`) builds its projective t7/t8 coordinates from `float4(0,0,0,0)`. The FP decompiler maps
input registers TEX8 (0xC) and TEX9 (0xD) to a literal zero ("TC8/TC9/FACING not plumbed"), and
the vertex decompiler never outputs them. So every tap sampled at 0/0:
- With the old uploaded-zero maps, the taps read 0 and everything looked lit, hiding the bug.
- With any real depth handed off (the shadow-pass bitcast, the shadow-pass compare, zeta v2),
  the taps read garbage at texel (0,0). That gives the silhouettes and the fully black frames.

This is also why the shadow-pass work never produced shadows.

**Why Ace went black under v2, without `SHADOW_PASSES`:** v2 records every zeta clear. That
created DepthRTs for the two cascades, which only ever received clears (the casters are not
drawn without `SHADOW_PASSES`). The hand-off then gave those maps to t7/t8.

**Why the water went black under v2:** the water FP (`fp_op0379`, fp `0x0FCCB1C1`) samples only t0-t3
(reflection RT, refraction RT, normals). It reads no depth, so the hand-off is not the cause. The
inferred cause is the reflection or refraction source going black. v2 changed two things that can
do that:
- the 256x256 reflection pass's depth function, now taken from the guest;
- a zeta clear on a surface shared with the scene, which wiped the whole buffer.

The v2 A/B switches below tell which.

## What v3 changes

1. **TEX8/TEX9.**
   - The vertex decompiler outputs `TEXCOORD8 = o[15]` and `TEXCOORD9 = o[6]` (TEX9 shares the
     point-size register, as in RPCS3's output table).
   - The FP decompiler reads them, and declares them only in programs that use them, so other
     VS/PS pairs link unchanged. Off: `FP_TC89=0`.
   - This applies to everything, not only zeta targets.
2. **Hand-off scope.** A unit only samples a DepthRT that holds real depth: a colour pass's zeta,
   or a map the shadow casters drew (with `SHADOW_PASSES`). Clear-only maps keep the old upload.
   Off: `ZETA_HANDOFF_ALL=1`.
3. **Depth clears cover the surface clip, as on RSX,** not the whole buffer. A smaller pass that
   shares a zeta no longer wipes the scene depth. Off: `ZETA_FULL_CLEAR=1`.
4. **Kept-depth colour ops** take the guest's depth test enable and write mask (what fixed the HUD
   boxes in v2). The function stays LESS_EQUAL as in v1, under which the water rendered. Off:
   `ZETA_GUEST_FUNC=1` restores v2's guest function.
5. **`ZPASS_DBG=1` also logs** each kept-depth colour op's depth state once per (fp, zeta, state).
   If the water still goes black, this names the pass.

## Checked

- Syntax-only compile of all three files: no errors.
- A TEX8 and a TEX9 read through the FP decompiler give `input.tc8` / `input.tc9` and the extra
  `PSInput` line; the HLSL compiles with `fxc ps_5_0`.
- A test vertex program writing o15 decompiles with `Out.t8=o[15]; Out.t9=o[6];`; it compiles with
  `fxc vs_5_0`.
- Nothing at run time.

## What the AI assistant should run

1. **v2 switches on the current build (058bb68, before v3).** This splits the v2 water problem.
   Use `ZETA_TARGETS=1 PS3_VERBOSE=0`, screenshots at the Goku card:
   - with `ZETA_FORCED_DEPTH=1`: if the water comes back, the guest depth function or state
     caused it (v3 item 4 keeps LESS_EQUAL);
   - with `ZETA_CLEARS_V1=1`: if the water comes back, a clear caused it (v3 item 3 limits clears
     to the surface clip);
   - Ace should stay black in both. That is the shadow-unit hand-off, fixed by v3 items 1 and 2.
2. **v3 build, defaults (zeta off):** the title, Ace and Goku cards should look as before. The
   TEX8/9 change is active even with zeta off. If the material shading changes, compare with
   `FP_TC89=0`.
3. **v3 with `ZETA_TARGETS=1 ZPASS_DBG=1`:** Goku in front of blue water, no logo boxes, Ace not
   black.
4. **v3 with `ZETA_TARGETS=1 SHADOW_PASSES=1`:** the first run where the shadow taps have real
   coordinates. If everything goes dark, try `ZPASS_ARGB_BITCAST=1`.
