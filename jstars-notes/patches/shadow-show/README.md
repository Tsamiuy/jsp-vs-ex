# Shadows: current state, likely causes, and a diagnostic (ZPASS_SHOW)

Patch: `shadow-show.patch` (`libs/video/rsx_d3d12_backend.c`, diagnostic only), on eea13f6
(`git apply --check` passes; the main checkout was clean). Off unless `ZPASS_SHOW` is set. A
syntax-only compile is clean, and the text rewrite was unit-tested offline. Not run in the game.

## Current state (from code, logs and notes; nothing run)

- **Shadow passes are on by default** (`SHADOW_PASSES`, since zeta v5).
- **The casters land.** Round-2 log: about 100-140 caster draws per cascade per batch, maps
  cleared to 1.0. They are drawn into per-zeta depth targets with the game's depth state and
  polygon offset.
- **Materials do read the maps.** Since v3 the lookups have real coordinates (TEX8/TEX9).
  - The ground/environment programs read the cascades as DEPTH24_D8 with zfunc GREATER, which
    goes through the decompiler's comparison helper.
  - The character material (fp `0x0D64F4C1`) reads them as A8R8G8B8 bytes (bitcast), as RPCS3
    does.
- **But the result is invisible.** In the v5 tests, `ZPASS_CMP_OFF=1` (no comparison at all) looked
  "almost the same as all-on". In our Yoshiwara fight shots (`menu\tours\t6_texmips_default`) no
  character casts a shadow on the floor.
- **In RPCS3 they do.** The Yoshiwara video frames (`reference\rpcs3\frames\frame_210s.jpg`,
  `frame_240s.jpg`) show soft dark shapes on the floor at the characters' feet.

So the pipeline runs end to end, but the comparison almost never says "in shadow". The maintainer's guess
is plausible: part of the flat, harsh look (floors in particular) is shadowing that never shows up.

## Likely causes, ranked (inferred)

1. **The reference depth and the stored depth are on different scales.**
   - The receiver's reference comes from the game's light matrix: TEX8/TEX9, `z/w`.
   - The stored depth comes from our casters' clip-z through the viewport z scale/offset
     (`vp_posscale/posoffset`).
   - If the shadow pass's viewport z mapping, or the reference's range, differs (a [-1,1] vs [0,1]
     convention, or `posoffset` applied twice), "reference > stored" is almost never true.
   - `ZPASS_SHOW=2` vs `=3` shows both ranges directly.
2. **TEX9 is wired to the wrong vertex output.** v3 maps TEX9 to `o[6]` (shared with point size, as
   in RPCS3's table). If J-Stars writes the reference to another register, the reference is
   constant or zero, and nothing is ever shadowed. `ZPASS_SHOW=3` would show a flat value.
3. **Polygon offset too large.** v5 applies the game's offset as D3D depth bias. If the units are
   interpreted wrongly, the casters are pushed far away. `ZPASS_NO_POLYOFFSET=1` was "the same" in v5,
   which argues against this.
4. **Cascade selection.** The material picks a cascade by view distance (constants like 21.8 /
   143.3). If that picks the wrong cascade, shadows land off-screen. Less likely: both maps are
   sampled.
5. **Depth clip.** Not a cause here: casters are clamped, not clipped (`DEPTH_CLIP` is opt-in and
   off).

## The diagnostic (ZPASS_SHOW)

It rewrites each comparison helper the decompiler emits (`_rsx_shadow_cmpN`) to return something
visible. Only programs that compare change; character materials (bitcast) are unaffected.
`ZPASS_SHOW_FP=<hex>` limits it to one program. Each rewritten program logs
`[ZPASS] show mode N on fp=...`.

| `ZPASS_SHOW` | the lookup returns | what to look for |
|---|---|---|
| 1 | always "in shadow" | Does the floor go dark? If not, the material barely uses the term, and the harsh look is elsewhere. |
| 2 | the stored map depth | Character-shaped darker shapes on the floor mean the casters are in the map, at the right place. |
| 3 | the receiver's reference depth | A smooth gradient across the floor. Compare its brightness with mode 2: if 3 is always darker than 2, nothing is ever "behind" a caster (cause 1). |
| 4 | always "lit" | The no-shadow baseline. |

## Run plan for the AI assistant

On the Yoshiwara fight start (`menu_tour.sh` with stage 00), take one screenshot each with
`ZPASS_SHOW=1`, `2`, `3` and `4` at the same moment as `16_fight_20s.png`.

Report the four pictures. They decide between causes 1, 2 and 4. The fix (a z remap, a TEX9
register, or a cascade issue) is drafted after that.
