# Shadows pass 2: what ZPASS_SHOW says, and the next fix/diagnostic

Patch: `shadow-depthmap.patch` (`libs/video/rsx_d3d12_backend.c`, +33), on de12d74
(`git apply --check` passes; the main checkout was clean). Everything is opt-in. A syntax-only
compile with the build's warnings is clean. Not run in the game.

## Reading the §41 numbers again

My README labelled mode 1 "in shadow" and mode 4 "lit". That assumed a comparison result of 1
means "in shadow". The measurements say the opposite: **in this game a result of 1 means lit.**

- Forcing 1 (mode 1) brightens the dark left floor to the level of the unshadowed right side (76).
- Forcing 0 (mode 4) leaves it dark (48).
- The normal picture equals mode 4. **So today the comparison returns 0, "in shadow", over the
  whole dark half.** The sharp diagonal edge is that shadow region's border (a cascade or map
  border), not a texture.
- Mode 2 (the stored map depth) gives the dark level (47), so **the map holds about 0 under the
  floor**. Mode 3 (the receiver's reference) gives the bright level (76), so **the reference is
  about 1**.
- With the reference about 1 and the stored depth about 0, the game's comparison (lit when
  reference <= stored, the usual GL sense) fails everywhere: everything is shadowed.

The floor is drawn into the shadow map too (it covers the whole region). Its own stored depth
should be about equal to its reference, not about 0. So **the casters' depths land near 0**, on a
different scale from the receivers' references. This is cause 1 from the first README.

## Likely reasons the casters land near 0 (inferred)

1. **The z mapping of the shadow pass.** Caster depth is the VP's clip z run through the viewport z
   scale/offset (`vp_posscale/posoffset`). If the shadow pass's viewport z scale is 0, we use 1/0
   (raw clip z). If the light projection is GL-style ([-1,1]), the near half then goes negative and
   is clamped to 0, because depth clip is off.
   - Candidate fix: **`ZPASS_CASTER_Z01=1`** forces scale 0.5 / offset 0.5 on casters, the GL
     mapping.
   - `ZPASS_DBG=1` now logs the actual mapping once per value:
     `[ZPASS] caster z map: viewport scale.z=... offset.z=... clip=[..] polyoffset ... zstate`.
2. **Polygon offset.** v5 applies the game's offset as D3D depth bias. A negative or huge value
   would push casters toward 0. Test `ZPASS_NO_POLYOFFSET=1`; the log line shows the values.
3. **The reference is wrong instead** (TEX9 wiring): a saturated reference of about 1 would also
   match mode 3. The new mode 5 tells them apart, as below.

## New in this patch

- **`ZPASS_SHOW_UNIT=<n>`:** apply `ZPASS_SHOW` to one unit only (7, 8 or 15), to see which map does
  the shadowing. Unit 15 is the scene zeta.
- **`ZPASS_SHOW=5`:** returns `0.5 + 4*(reference - depth)`. Mid-grey where they agree, bright where
  the receiver is behind the map, dark where it is in front.
- **`ZPASS_DBG=1`:** the caster z-map log line above.
- **Candidate fix `ZPASS_CASTER_Z01=1`.**

## Run plan for the AI assistant (Yoshiwara, fight start, same view as `shadows\show*`)

1. `ZPASS_DBG=1`, then grep `[ZPASS] caster z map` in stderr. Report the values. If `scale.z=0`, or
   offset and scale are not 0.5/0.5, cause 1 is likely.
2. `ZPASS_CASTER_Z01=1`: the dark half should disappear, and soft character shadows should appear
   at their feet (compare RPCS3 `reference\rpcs3\frames\frame_210s.jpg`).
3. If 2 does not change it: `ZPASS_NO_POLYOFFSET=1`.
4. If neither does: `ZPASS_SHOW=5` with `ZPASS_SHOW_UNIT=7`, then `=8`. A screenshot of each shows
   whether the reference or the stored depth is off, and in which cascade.

## Pass 3 (2026-10-21): casters are fine; the next suspects (read-only)

The AI assistant's `shadows\dbg` run logs one caster mapping, `scale.z=0.5 offset.z=0.5 clip=[0,1]`, with no
polygon offset (zstate 0x113: test, write, LESS). So `ZPASS_CASTER_Z01` and `ZPASS_NO_POLYOFFSET`
should change nothing. The same log shows two more things:

- Per batch, both cascades get ~107 caster draws (~146k vertices) and one clear to 1.0. So the maps
  are filled.
- **The floor program fp `0x0FBFFFC1` compares three units, not two:**
  - t7 = cascade `0x0CE40000`;
  - t8 = cascade `0x0CA30000`;
  - **t15 = the scene's own depth buffer `0x0E960000`**, also as DEPTH24_D8 with zfunc 4.

  The `ZPASS_SHOW` runs changed all three helpers at once (compare units 0x8180), so the dark half
  may come from t15, not from the shadow maps.

### Suspects, ranked

1. **t15, the scene depth compare.**
   - The floor compares its own depth against the scene's depth buffer; this looks like a contact
     or occlusion term, not a cast shadow.
   - Our copy of that buffer may be wrong for this use. It is taken at the floor draw, so it
     already contains the floor's own depth, which gives a self-comparison. Its clears also happen
     twice per batch.
   - Under the game's convention (1 = lit), a self-comparison with GREATER (reference > stored) is
     false wherever the floor wrote its own depth, so the floor comes out "in shadow". **That fits
     a sharp-edged dark half covering only the floor area.**
   - On RSX the same compare would read the depth buffer as it was when the texture was last
     flushed. The game may rely on reading it before the floor is drawn (an earlier pass), or not
     drawing to that zeta while sampling it.
2. **The snapshot never refreshed for the cascades.** An all-zero copy also reads as "stored ≈ 0".
   The log shows the cascades filled, so this is less likely. `ZPASS_SHOW=2 ZPASS_SHOW_UNIT=7`
   would show caster shapes if the copy is fine.
3. **Receiver reference saturated at 1:** the TEX9 wiring, or a w divide. Mode 3 was uniformly
   bright. Only if 1 and 2 are ruled out.

### Run plan additions (the AI assistant already queued `ZPASS_SHOW=5` with units 7 and 8)

- **Add `ZPASS_SHOW=1 ZPASS_SHOW_UNIT=15`:** if the dark half turns bright, t15 is the culprit, not
  the shadow maps.
- **Add `ZPASS_SHOW=2 ZPASS_SHOW_UNIT=7`:** caster shapes visible means the cascade copy is good.
- **How to read `ZPASS_SHOW=5` (per unit):**
  - mid-grey = reference equals stored, which is normal for self-shadowed or lit areas;
  - bright = receiver behind the map, so "in shadow" under GREATER;
  - uniform bright over the floor for 7/8 = reference too large (suspect 3).

### Fix candidates, drafted once the per-unit runs point at one

- **If t15:** take the scene-depth copy before the program's own pass draws (snapshot at the
  first sample of the frame, and do not refresh it while the same zeta is the bound target). Or
  read t15 without comparison.
- **If the cascades' copy:** sample the live depth target instead of the copy (the shadow maps are
  never bound as a target while being sampled), as a `ZPASS_NO_SNAP=1` switch.
- **Also noticed:** the `[ZPASS] colour op on kept zeta` log stops after 32 entries and was filled by
  HUD passes, so the scene's colour passes on `0x0E960000` never got logged. Raise that cap in the
  next patch.
