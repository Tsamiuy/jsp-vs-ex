# Shadow maps: "outside the map counts as lit" (S_BORDER_UNITS)

Patch: `shadow-border.patch` (`libs/video/rsx_d3d12_backend.c`, +17/-2), on fdce642 (`git apply
--check` passes; the main checkout was clean). Syntax-only compile with the build's warnings: no
errors or warnings in the changed lines. Not built or run.

## What it does

1. **`S_BORDER_UNITS=<hex mask>`** (opt-in test switch, next to `S_CLAMP_UNITS`): the static samplers
   on those units become clamp-to-border with an opaque white border. For the two shadow maps
   (units 7 and 8, mask `0x180`), any lookup outside the map reads depth 1.0, "nothing in front".
   With `ZPASS_CMP_SWAP=1` (RSX direction: lit when stored > reference) that is always lit.
   - WRAP (today) reads a random map texel there; CLAMP reads the map's edge, where casters can be.
   - If both switches name a unit, BORDER wins.
2. **`ZPASS_DBG=1` now also prints each zeta sampler's ADDRESS word** with its S/T wrap modes
   (`addr=0x... (wrap s=N t=N)`), so one run shows whether the game itself asks for border on the
   shadow units. RSX wrap codes: 1 wrap, 2 mirror, 3 clamp-to-edge, 4 border, 5 clamp. If it says
   4, this switch is the game's own behaviour and can become the default for shadow units.

## How to test (always with `ZPASS_CMP_SWAP=1`)

1. **Namek demo (Goku card, `strips\demo_base\t210.png` vs §46 `swap_clamp_demo\t210.png`):**
   `ZPASS_CMP_SWAP=1 S_BORDER_UNITS=0x180`. The ground and grass in the lower left, under Goku,
   should stay lit like `demo_base`, not dark like the clamp run. Real shadows (Goku's own,
   plants') may appear on the lit ground.
2. **Yoshiwara fight start (`16_fight_20s.png` hall view and `swap\16_fight_20s.png` stairs view):**
   same switches. The stairs view should stay as good as with the swap alone. For the hall view, see
   below.
3. **One run with `ZPASS_DBG=1` added** to report the `[ZPASS] ... unit 7/8 ... addr=... (wrap s=.. t=..)`
   lines.

## The Yoshiwara fight-start dark left floor, in this light

After the swap it sits at 57 (48 = fully "in shadow", 76 = fully lit). Possible readings:

- **(a) Lookups outside cascade 0.** The hall floor further from the camera may lie outside cascade 0
  (2048x1024, the near cascade) and wrap into casters elsewhere in the map. If so, this switch lifts
  the half toward 76 while character shadows stay. Most likely, given that the clamp run did not
  help but changed the Namek ground the same way.
- **(b) A real shadow, drawn too hard and dark.** Its sharp diagonal edge lines up with the upper
  gallery, so part of it may be the gallery's real shadow. The border switch leaves a real shadow
  in place. Whether it belongs needs RPCS3 at the same camera: the fight start facing the hall, as
  in our `15_fight_start.png`. The video frames `frame_126s`-`142s` show another angle.
- **(c) The floor's third comparison (t15, the scene's own depth).** It fails in both directions
  because our copy already holds the floor. It was a small term (52 vs 48). Check with
  `ZPASS_CMP_SWAP=1 S_BORDER_UNITS=0x180 ZPASS_SHOW=1 ZPASS_SHOW_UNIT=15` if (a) does not clear it.

If the logged wrap modes come back as 4 (border) for units 7/8, (a) is confirmed as the hardware
behaviour, and `ZPASS_CMP_SWAP=1 S_BORDER_UNITS=0x180` can be proposed as the default for shadows.
