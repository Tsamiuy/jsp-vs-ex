# ZPASS_VPXY: shadow casters honour the guest viewport x/y transform (environment shadow flicker)

Patch: `shadow-vpxy.patch` (`libs/video/rsx_d3d12_backend.c`, +74/-1), on e547f31. `git apply --check`
passes. NOT compile-checked (no C compiler on my side). Default behaviour is unchanged: the new logging
is under the existing `ZPASS_DBG`, the fix under the new `ZPASS_VPXY=1`.

## What it does

- `ZPASS_DBG=1` now also prints, once per distinct placement,
  `[ZPASS] caster xy map: target 0x... rect=x,y wxh scale=(sx,sy) offset=(ox,oy)` for depth-only
  (shadow caster) draws. One line per target = one view per map; several offsets on target
  `0x0CE40000` = a cascade atlas.
- `ZPASS_VPXY=1`: for depth-only draws only, the vertex epilogue applies the guest's viewport
  scale/offset in x and y (today x/y are identity and only z is mapped), the D3D viewport becomes the
  whole map, and the scissor becomes the rectangle that transform covers (so a caster can't spill into
  the neighbouring cascade). At the full-surface defaults (scale = half size, offset = centre) it
  reproduces today's mapping exactly, in either y convention.

## Why (see SHADOWS_ENV.md)

The environment shadow map is a 2048x1024 depth target (`0x0CE40000`, unit 7); character shadows use a
separate 1024x1024 one (`0x0CA30000`, unit 8) and are fine. A 2:1 map is the usual layout for two
1024x1024 cascades side by side, placed by the viewport transform. Our backend ignores the viewport
scale/offset in x/y (`vp_record_cb`: "x/y identity"), so every cascade would be drawn over the whole map
and the last one would overwrite the first. Receivers then read cascade A's region and find cascade B's
casters, stretched: shadows that come and go as the camera and the characters move (the game picks the
cascade by distance and refits the cascades every frame). Not yet confirmed: the run below decides it.

## Repro and runs (the AI assistant)

Stage with trees and buildings (Hidden Leaf, Kameari, or the attract demo's Namek card), camera moving.
1. `ZPASS_DBG=1`: collect the `[ZPASS] caster xy map` lines. Two or more different offsets on
   `0x0CE40000` = atlas confirmed (go to 2). One offset equal to the full map (scale 1024/±512, offset
   1024/512) = not an atlas; this patch won't help, say so and I'll move to the next suspect.
2. `ZPASS_VPXY=1` (with the current shadow defaults): screenshots every 0.5 s while the camera pans
   past trees/buildings, and the same without it. Environment shadows should stay put instead of
   blinking; character shadows must look the same as before.
