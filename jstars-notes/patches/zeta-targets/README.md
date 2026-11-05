# Zeta targets: keep each surface's depth across colour-target switches

Patch: `zeta-targets.patch` (`libs/video/rsx_d3d12_backend.c`, +154/-13), on top of 7340a7b
(`git apply --check` passes there; the main checkout was clean). Not built or run. Why:
`docs\notes\GREEN_SURFACE.md`.

**Before:** colour passes all share one depth buffer, and it is cleared on every colour-target
switch. J-Stars draws its water as one quad after the 256x256 reflection pass and the
refraction downsample (capture `rsxcap_flush1` op 317: fp `0x0FC9F281`, n=6, samples the
reflection RT, the refraction RT, two water normal maps, the shadow maps and the scene zeta at
t15). By then the scene's depth is gone, so the water covers Goku and everything else drawn
before it. In RPCS3 the same water is drawn behind him.

**After:**
- An offscreen colour target that has a zeta surface (SET_SURFACE_FORMAT depth bits set) draws
  against a depth target keyed by that zeta offset (the DepthRT pool from the shadow-pass patch).
  The depth persists across switches and batches until the game clears it. The game's Z/stencil
  clears of those surfaces are now recorded as ordered ops, both Z-only and colour+Z.
- Display draws keep the shared buffer and its per-switch clear, as before.
- A pass that samples a zeta goes through a copy (`dt_snapshot`, refreshed only when the depth
  changed since the last copy). So the water can read the scene depth it is drawn into, at t15,
  without a read/write hazard. The shadow maps are now sampled the same way.
- The sampled-zeta hand-off (depth compare / bitcast, see `patches\shadow-fix`) now also works
  without `SHADOW_PASSES`. So t15 `0x0E960000` gets the real scene depth instead of the screen copy.
- Up to 16 depth targets (was 8). A zeta used at several sizes grows to the largest.

## Switches

| Env | Effect |
|---|---|
| `ZETA_TARGETS=0` | Off: the old shared, per-switch-cleared depth buffer and no sampled-zeta hand-off without `SHADOW_PASSES`. Default on. |
| `ZPASS_DBG=1` | As before. Also lists the units that sample the scene zeta and the mode used. |

## Checked

- Syntax-only compile of the patched file (`clang-cl /Zs`, the build's flags): no errors, and no
  new warning kinds.
- `git apply --check` on 7340a7b.
- Nothing at run time.

## Test plan for the AI assistant

1. Apply, build, run the demo to the Goku card (`PS3_VERBOSE=0`, screenshot ~300 s). The green
   water should sit behind Goku and the rocks, as in the RPCS3 shot
   (`Screenshot 2026-10-20 184154.png`, the one with the overlay).
2. A/B with `ZETA_TARGETS=0`.
3. With `ZPASS_DBG=1`, check that t15 `0x0E960000` shows up as sampled and in which mode. If the
   water turns black or vanishes, try `ZPASS_ARGB_BITCAST=1`.
4. Re-check the title screen, the Ace card and a match frame for regressions. Passes that relied
   on the per-switch clear would now see old depth, so look for missing geometry.

## Risks

- A game pass that expects depth to start empty without clearing it would now see old depth.
  RSX behaves that way too, so this should match the hardware.
- Stencil is persisted along with depth, but stencil state is still not modelled.
- `SHADOW_PASSES=1` gave a fully black demo in the last run (FINDINGS §31). This patch changes how
  the shadow maps are sampled (through the copy). That may help, but nobody has tested it.
