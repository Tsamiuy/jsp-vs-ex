# Shadows pass 4: unit 7's comparison runs in the wrong direction

Patch (drafted, not applied): `cmp-swap.patch` (`rsx_d3d12_backend.c`, +12), on 3cef758
(`git apply --check` passes; syntax-only compile clean). Opt-in `ZPASS_CMP_SWAP=1`. Not run.

## What §44 says, read with the game's convention (comparison result 1 = lit)

Brightness of the left floor (the dark half) in each run:

| run | left floor | what it means |
|---|---|---|
| normal | 48 | result 0 = "in shadow" |
| ZPASS_SHOW=1, all units | 76 | result 1 = lit |
| ZPASS_SHOW=1, unit 15 only | 52 | not t15: scene depth is ruled out |
| ZPASS_SHOW=5, unit 8 | 48 | not t8 |
| ZPASS_SHOW=2, unit 7 | 74 | **stored depth ≈ 1**: the cascade map is empty (cleared) under the floor; no casters there |
| ZPASS_SHOW=5, unit 7 | 35 (right floor 179) | **reference < stored** on the left; reference > stored on the right |

So on the left floor, cascade t7 is empty (stored ≈ 1, nothing in front of the floor), and the
floor's reference is closer than that. Physically that is **lit**. The game's zfunc is 4
(GREATER), and our helper computes the GL form, `reference > stored`, which is false, so it gives
0 = "in shadow". RSX gives 1 there, because it compares the other way round:
**`stored > reference`**, "the map holds nothing nearer than me".

That matches RPCS3. For texture depth compares it reverses the comparison direction relative to GL
(`get_compare_func(op, reverse_direction=true)` in its Vulkan backend, from memory).

The fix is to mirror the function: GREATER becomes LESS, GEQUAL becomes LEQUAL, EQUAL, NOTEQUAL and
ALWAYS stay. Then the helper computes `reference < stored`, which is the RSX result.

## What else the numbers show

- **The right floor** (`SHOW=5` unit 7 bright, so reference > stored) would then turn "in shadow".
  If the right side is outside cascade 0's area, its lookups leave the map. Our static sampler
  WRAPs, so they read the opposite edge of the map (garbage). On the PS3, the game's ADDRESS state
  (probably clamp) applies. So pair the swap with the existing test switch
  **`S_CLAMP_UNITS=0x180`** (CLAMP on units 7 and 8).
- **No caster shapes in `SHOW=2` unit 7** fits "empty under the floor in this view": the casters
  (characters) are elsewhere in the map. Shapes should show where characters stand.
- **Not causes:** t8, the caster z mapping (`CASTER_Z01` gave no change), and scene depth t15.

## Run plan for the AI assistant (Yoshiwara fight start, same view)

1. `ZPASS_CMP_SWAP=1`: the dark left half should become lit, and soft dark shapes should appear at
   the characters' feet. The right floor may darken.
2. `ZPASS_CMP_SWAP=1 S_CLAMP_UNITS=0x180`: the right floor should come back too, and only real
   shadows remain. Compare with RPCS3 `reference\rpcs3\frames\frame_210s.jpg`.
3. If 1 brightens everything with no shadows at all: also look at
   `ZPASS_CMP_SWAP=1 ZPASS_SHOW=5 ZPASS_SHOW_UNIT=7` near a character. Bright spots should appear
   under them, which means the map has casters and the direction is right.
4. Re-check the Goku and Naruto demo cards with the swap: character self-shadowing goes through the
   bitcast path and is unaffected; ground and plants use the compare path.

## Pass 5 (after §46): what is left, read-only

§46: the swap fixes the stairs view (even grey floor, soft shadows at the feet). The fight-start
hall view still has a darker left half (57, between 48 "in shadow" and 76 "lit"). With swap +
clamp, the Goku card's Namek ground under him turns dark.

### Namek ground darker with `S_CLAMP_UNITS=0x180` (fairly confident)

The darkened area is ground close to the camera, outside the cascade's coverage, so its lookups
leave the map.
- With WRAP (default) they read a random texel from elsewhere in the map, mostly empty, so mostly lit.
- With CLAMP they read the map's edge texels. Where casters touch the edge (terrain, the
  pod, Goku), that is "in shadow".

On the PS3, shadow maps are normally addressed **clamp-to-border** with a border depth of 1
(TEXTURE_ADDRESS mode BORDER + TEXTURE_BORDER_COLOR white). Everything outside the map is then lit.
Neither our WRAP nor our CLAMP does that.

**Next candidate (needs a small code change once approved):** `S_BORDER_UNITS=<hex>`, a static
sampler with `TEXTURE_ADDRESS_MODE_BORDER` and `STATIC_BORDER_COLOR_OPAQUE_WHITE` on those units,
next to `S_CLAMP_UNITS` in the root signature. Run it with `ZPASS_CMP_SWAP=1 S_BORDER_UNITS=0x180`.
The real fix later is per-draw samplers from each unit's ADDRESS / BORDER_COLOR (STAGE_PLAN #9).

To confirm the game asks for border: log `ADDRESS & 0xF` (wrap S) and `BORDER_COLOR` for units 7/8.
The draw record already keeps `addr`; adding them to the `[ZPASS] ... samples zeta` line is one
fprintf.

### Hall view: darker left half (less sure)

1. **A legitimate shadow, too dark.** The left half lies under the upper gallery, and its sharp
   diagonal edge looks like the gallery's edge projected along the light. The swap moved it from
   48 to 57, so the comparison now does run.
   - What remains may be correct: the gallery really shades that floor.
   - It may look darker than RPCS3 because of hard per-texel comparisons (the helper is nearest
     unless the min filter is linear; RSX filters 2x2) and our harsher tone (the colour work).
   - RPCS3 at the same view is needed to judge: `reference\rpcs3\frames\frame_126s` to
     `frame_142s` are fight-start frames, but from another camera angle.
2. **Out-of-map lookups again.** Parts of the hall floor may also lie outside cascade 0 and wrap
   into casters. Fixed by the border sampler above if so.
3. **Cascade 1 (t8) or the scene-depth compare (t15) with the swap.** t15 compares the floor
   against the scene depth that already contains the floor, so it fails in both directions. That
   term was small (52 vs 48), but the swap does not fix it. If the half stays dark with
   `S_BORDER_UNITS`, run `ZPASS_CMP_SWAP=1 ZPASS_SHOW=1 ZPASS_SHOW_UNIT=15`. If the left floor then
   reaches about 76, t15 needs the "copy before the floor is drawn" fix.

### Suggested runs once `S_BORDER_UNITS` exists

1. `ZPASS_CMP_SWAP=1 S_BORDER_UNITS=0x180` on the Goku card (Namek ground should stay lit) and the
   Yoshiwara hall view.
2. If the hall half is still darker: `ZPASS_CMP_SWAP=1 ZPASS_SHOW=1 ZPASS_SHOW_UNIT=15`.
