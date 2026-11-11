# Namek shadow check, Athena sky streaks, Dark Tournament rubble (2026-10-22, read-only)

## 1. Namek ground darker with `ZPASS_CMP_SWAP=1 S_BORDER_UNITS=0x180`: most likely a real shadow

- The AI assistant's run: `shadows\labeled\demo_shadow_fix.png`, Goku card, normal vs shadow fix.
- **There is an RPCS3 shot of this exact card:** `reference\rpcs3\Screenshot 2026-10-20
  184154.png`, the Goku card ("Your little brother's super-tough!"), with the overlay on.
- In it, the ground left of and below the big blue coral bush, under the "Son Goku" text, is a
  darker green/teal. That matches the shadow-fix picture, while the normal picture has it lighter
  and sandier. The bush and Goku stand between that ground and the light (the light direction
  constant in the materials is about (0.77, -0.60, -0.23)).
- The game itself asks for border addressing on both maps (§54: `wrap s=4 t=4`). The fix uses the
  game's own setting there, not a guess.
- **So it is a real shadow.** The mix is now right on Yoshiwara and on the Namek card, and Ace is
  unchanged. The AI assistant can propose `ZPASS_CMP_SWAP=1 S_BORDER_UNITS=0x180` as the default for shadow
  maps, still with an off switch. Do a last check on two more stages with outdoor ground, for
  example Hidden Leaf Village and Inferno Crags.

## 2. Athena (Temple of Athena, s03): thin lines across the night sky

- **What it looks like:**
  - `stages\s03\16_fight_20s.png`: many parallel, evenly spaced, dashed purple-white lines at one
    angle across the sky.
  - `13_intro_a.png` enlarged: long straight lines, purple glow with a white core, some crossing
    characters.
- **Not mipmaps:** they stayed with `TEX_MIPS` (§36).
- **RPCS3 reference now exists; see §4: the lines are a bug (star field smeared).**

Ranked:

1. **A real effect drawn wrong (streaking stars), likely.**
   - The purple/white glow looks like an authored effect.
   - It may be particle quads whose vertex shader stretches them by a per-vertex value (velocity or
     size). With that value misread, the quads become long thin streaks.
   - The new TEX9 = `o[6]` wiring is one suspect, because o[6] is also the point-size register.
     Test: `FP_TC89=0`. It reverts TEX8/9 for all shaders, but only for one run.
2. **Line or point primitives through the fixed-function fallback.**
   - Non-indexed LINES, LINE_STRIP and POINTS do not go through the vertex-program path in
     `d3d12_draw_arrays`. They fall to the fixed-function passthrough, which applies no transform:
     raw object or world positions, so long lines across the screen.
   - The stage logs only sample every 1000th draw (only `prim=6`/`8` seen), so this is unproven.
   - Indexed lines and points are skipped entirely.
3. **The game's own look.** It needs RPCS3 to rule in or out: one RPCS3 screenshot of Temple of
   Athena at fight start.

To find the draw:
- run `FP_IDCOLOR=1` on Athena: each program gets a flat colour, so the streak program is named in
  stderr (`[FPID]`);
- or take `RSX_CAP_PASSES=2` of a fight frame and look for draws with odd vertex counts or a line
  topology.
- A small log patch (one line per primitive type per batch: `[PRIM] prim=N count=… vp=…`) would
  settle cause 2. I can draft it on request.

## 3. Dark Tournament (s09): some rubble faces flat black or grey

- **What it looks like:** `stages\s09\16_fight_20s.png` has big rubble blocks with rock texture on
  some faces. Other faces are flat grey (the large slab centre-left) or show long parallel streaks
  (blocks lower right).
- **Inferred cause:** a texture projected along one axis. A planar or box-mapped UV taken from
  position is constant (flat colour) or one-dimensional (streaks) on faces parallel to the
  projection direction.
- **This may be how the game looks.** The RPCS3 Yoshiwara video shows its broken-floor rubble the
  same way: dark chunks with rock texture on some faces and plain dark faces on others
  (`reference\rpcs3\frames\frame_150s.jpg`).

Ranked:

1. ~~**Authored look**~~: ruled out by the RPCS3 Dark Tournament shots (§4); rocks are fully textured there.
2. **Triplanar mapping weighted by normals, with wrong normals.** If the material blends three
   projections by the normal and the normal attribute is misdecoded (e.g. CMP 11/11/10), one
   projection wins everywhere and the others streak. Check: `RSX_CAP_PASSES=2` on Dark Tournament
   and read the rubble FP. Three samples of one texture with position-based UVs mean triplanar.
3. **Texture coordinates from TEX8/TEX9.** If the rubble material reads its UVs from TEX8/9 (new in
   v3), `FP_TC89=0` would change it.

## Suggested next runs for the AI assistant

1. Hidden Leaf Village and Inferno Crags with `ZPASS_CMP_SWAP=1 S_BORDER_UNITS=0x180`. If fine, make
   both the default.
2. Athena: `FP_IDCOLOR=1` (name the streak program) and one run with `FP_TC89=0`.
3. Dark Tournament: one run with `FP_TC89=0`, plus `RSX_CAP_PASSES=2` of a fight frame.
4. From the maintainer, if possible: RPCS3 screenshots of Temple of Athena and Dark Tournament at fight start.

## 4. RPCS3 comparison: Temple of Athena and Dark Tournament (the maintainer's screenshots, 2026-10-22)

RPCS3 shots in `reference\rpcs3\`: Athena = `Screenshot 2026-10-22 013715.png` (moon view) and
`013736.png` (fight, constellation effect); Dark Tournament = `013909.png` (broken wall, open sky),
`013933.png` (stands and big screen), `014020.png` (ring tiles and a boulder). Ours:
`stages\s03\13_intro_a.png`, `16_fight_20s.png`, `stages\s09\16_fight_20s.png`, all captured
10-21, so before the §54 shadow switches.

### Athena: the parallel lines are a bug, not designed

- **RPCS3 has no parallel lines.** Its sky is a dense star field (hundreds of small dots) with a soft
  blue nebula glow and a big yellow moon.
- **Ours has almost no star field:** a flat blue gradient, a handful of dots, and the evenly spaced
  dashed purple-white lines. The lines sit where the star field should be, and each "dash" is the
  size of a star. So **the star-field layer is drawn with one texture axis frozen**: each row of
  stars in the texture is smeared into a line along the sky. That replaces cause 1 in §2 (streaked
  particles) with a more specific reading; the fixed-function line path (cause 2) no longer fits,
  because the lines are clearly textured with the stars.
- **The constellation lines are designed.** RPCS3 `013736` shows a constellation figure (bright
  star nodes joined by thin lines, with a faint silhouette) during a fight. The star node with lines
  at the top right of our `13_intro_a.png` is that effect. The long purple lines running top to
  bottom over the whole intro frame are the bug.

Differences, most noticeable first:

1. **Star field replaced by parallel dashed lines** (whole sky, every frame). Likely cause: the sky
   material's texture coordinate collapses on one axis. Candidates: TEX8/TEX9 wiring (`FP_TC89=0`
   test), a texture coordinate built in the VP from a constant matrix we read short or wrong, or a
   UV vertex attribute misdecoded (for example half-float or 16-bit normalized read as another
   format). Same symptom family as the Dark Tournament rubble below.
2. **Blue nebula glow missing.** Probably part of the same sky draw or an additive layer on top of
   it; check once 1 is fixed.
3. **Moon:** not in our frames (different camera), so not judged. Worth one capture facing it.
4. Brightness of the stone and mountains is close; nothing to do there now (brightness waits).

### Dark Tournament: the rubble does not look the same

- **RPCS3 rocks are fully textured.** The boulder in `014020` and the broken pieces right of Goku in
  `013909` show a smooth dark rock texture on every face. No flat grey faces, no streaked faces.
- **Ours:** many faces flat grey with no texture, others covered in long parallel streaks, and the
  textured faces show a harsh white speckle.
- So §3's cause 1 ("authored, like Yoshiwara") is **ruled out** for this stage. Flat face = both
  texture axes frozen, streaked face = one frozen. That is the same failure as the Athena sky.

Differences, most noticeable first:

1. **Flat grey and streaked rubble faces** (centre of every fight view). Likely cause: texture
   coordinates collapse per face, as in Athena 1. Because it depends on face orientation, the
   strongest suspect is a coordinate computed from position and normal (box or triplanar style)
   with a misdecoded normal or matrix, or a misdecoded UV attribute. Test `FP_TC89=0` first, then
   `RSX_CAP_PASSES=2` and read the rubble VP/FP and its vertex attribute formats.
2. **Floor much darker** (near black in ours, brown dirt in RPCS3 `013909`/`013933`). Our capture
   predates `ZPASS_CMP_SWAP=1 S_BORDER_UNITS=0x180`, and an arena floor "all in shadow" is exactly
   what those fixed on Yoshiwara. Recapture with them before treating it as brightness.
3. **White speckle on the textured rock faces** (RPCS3 is smooth). Looks like the Namek grain that
   `TEX_MIPS` fixed; the capture may predate mips being the default. Recapture to check.
4. **Ring tiles and boulder** (`014020`) not seen in our capture (camera); not judged.
5. The big screen and the stands render in both; the stands look similar apart from brightness.

### What this changes in the plan

- One likely shared bug, texture coordinates collapsing on one or both axes, explains both the
  Athena lines and the Dark Tournament rubble. Fixing it should clear both stages at once.
- Next runs for the AI assistant: Athena and Dark Tournament with `FP_TC89=0`; then the shadow defaults plus
  mips on Dark Tournament to see how much of the dark floor and speckle remains; then
  `RSX_CAP_PASSES=2` of the Athena sky and a rubble draw so I can read their programs and attribute
  formats.

## 5. Athena sky: which draw makes the lines, and why (2026-10-22, read-only)

Sources: `stages\athena\intro_cap\frame_00` (Luffy intro card, normal colours) and
`normal_cap\frame_00` (fight, facing the temple).

### Where the lines are drawn

- After the post-process composite (op381), the scene target `0x0ED10000` gets the sky as
  blended draws, all with static-address programs (`0x09CA....`), all sampling **unit 0** with
  `tc0` only (e.g. `fp_op0396.hlsl`: one `Sample(rsx_samp[0], tc0.xy)`):

  | ops (intro f00) | fp | t0 texture | verts | likely role |
  |---|---|---|---|---|
  | 383-389 | 09CA8181 | 0x0B8DD580, A8R8G8B8 (0x85), 64x1024 | 57-73 strips x7 | sky gradient dome |
  | 390-392 | 09CA7F81 | 0x0BBC4400, DXT5 (0x88), **1024x1024** | 479-578 strips | star field / nebula layer |
  | 393 | 09CA7DC1 | 0x0BCC4400, DXT5, 512x512 | 518 | second sky layer |
  | 394-395, 398 | 09CA7BC1 | 0x0BA1D600, DXT5, 64x64 | 569, 70 | star sprites |
  | 396-400 | 09CA7841 / 76C1 / 7501 / 7341 | 128-256 px DXT1/5 | 4-80 | constellation flares and lines (designed) |

- Pass snapshots: `pass_op0382_post` has no lines; `pass_op0396_pre` has them. So the lines come
  from ops 383-395. The constellation star and its lines appear in 396-400 and are fine.
- In the fight capture (`normal_cap`) the same layers draw (ops 447-460) and the sky is a plain
  gradient with **no stars and no lines** at all.

### Sampler, wrap, size, format of the star layers

- All of them are on unit 0. Our unit 0 is a static sampler that is **POINT + CLAMP**, no mips
  filtering, no anisotropy (`rsx_d3d12_backend.c` root signature: "s0 keeps point/clamp" for the
  debug font). Every other unit is LINEAR + WRAP. The game's own ADDRESS/FILTER words for unit 0
  are ignored.
- Formats look ordinary (DXT5 and A8R8G8B8, remap 0xAAE4 = identity), so a format or swizzle error
  would give blocky garbage rather than clean lines.

### Causes, ranked, one cheap test each

1. **Unit 0 is CLAMP while the star texture is tiled (UVs past 0..1).** With CLAMP, wherever one
   coordinate is outside 0..1 the texture's edge row or column is repeated: each star on that edge
   becomes a straight line, all parallel, dashed where the edge texels hold stars. Where both are
   outside, the sky reads one corner texel: no stars at all. That matches all three captures:
   lines in the intro, parallel dashed lines in the fight, and an empty sky facing the temple.
   RPCS3 shows a full star field. **Test:** `S0_LINEAR=1` on Athena (it also switches s0 to WRAP).
   Ignore the plant boxes it causes; look only at the sky. If the star field comes back, the fix
   is per-draw addressing from the game's ADDRESS word (STAGE_PLAN #9), or at least WRAP on unit 0
   where the game asks for it.
2. **Point filtering on unit 0** (no mips, no linear). This does not make lines, but it makes small
   stars sparkle or vanish at a distance. **Test:** same run. If the stars come back but shimmer,
   that is this cause.
3. **Star sprite corners misdecoded** (ops 394-395: 64x64 sprite on strips of 569 vertices). If
   the corner-offset attribute were read with the wrong type or scale, each star quad would stretch
   along one screen direction. **Test:** `VP_ATTRS=1 VP_ATTRS_FP=0x09CA7BC1` (prints the
   attribute types) and `RSX_DBG_VP=0x0BA1D600` (writes that draw's vertex program). Only worth
   doing if test 1 leaves lines.
4. **Wrong texture format or swizzle.** Unlikely for the reason above. **Test:**
   `FP_TEX=0 FP_TEX_FP=0x09CA7F81 FP_TEX_UV=1` shows the star layer's texture exactly as sampled.
5. **Missing second coordinate (TC8/9) and anisotropy:** ruled out. `FP_TC89=0` kept the lines
   (§55), these programs read `tc0` only, and our static samplers have no anisotropy.

**Dark Tournament.** Its flat grey and streaky rubble faces (§4) fit cause 1 just as well: a tiled
rock albedo on unit 0 gives a flat colour where both UVs fall outside the texture and streaks where
one does. The same `S0_LINEAR=1` run on Dark Tournament checks it.

### Update from the AI assistant's coloured capture (`stages\athena\idrt_cap\frame_03`, FINDINGS §56)

- **The lines are drawn by fp `09CA7F81` (ops 404-406, VP code `CF90795D`).** Its texture is
  t0 `0x0BBC4400`, DXT5, 1024x1024, sampled through unit 0, which is POINT + CLAMP. In
  `pass_op0409_pre` the sky has added up to white, and every line pixel is a mix of white with
  that program's ID colour (233,176,233): R = B, with G lower, for example 240,202,240. The 512x512
  layer `09CA7DC1` (219,186,197) and the star sprites `09CA7BC1` (219,164,150) would leave R ≠ B,
  and none of the line pixels do.
- So the star sprites (ops 407-408) and their vertex data are **ruled out** as the source. The
  64x1024 gradient (`09CA8181`) is a plain gradient; CLAMP suits it.
- **The lines curve gently, following the dome.** That is a texture effect along the dome's
  texture coordinates, not stretched straight geometry. It fits CLAMP smearing the texture's edge
  texels: each star on the edge row or column becomes a thin line, and the rest of the sky outside
  the texture reads one empty corner texel.
- **Should it tile?** Very likely yes (inferred). The fight view facing the temple shows the same
  layer with no stars anywhere. With CLAMP that means most of the sky's coordinates lie outside
  0..1, so the game must be repeating the texture with WRAP. RPCS3 shows stars all over the sky.

Refined tests:
1. `S0_LINEAR=1` (running now): the lines should turn into a tiled star field, and the empty
   sky facing the temple should fill with stars.
2. If it works, separate addressing from filtering: an opt-in `S0_WRAP=1` (POINT + WRAP on s0,
   a few lines next to `S0_LINEAR`) avoids the plant boxes that LINEAR gives. I can draft it.
3. To confirm what the game asks for, log the unit 0 ADDRESS word for fp `09CA7F81` (the draw
   record already keeps `tex[0].addr`; the same print as the `ZPASS_DBG` wrap line). Expected:
   wrap s=1 t=1.
4. The real fix stays per-draw addressing from the ADDRESS word (STAGE_PLAN #9). Unit 0 also
   carries the albedo of most materials, so Dark Tournament's rubble should be checked in the same
   run.

## Resume here (paused 2026-10-22)

The Athena sky lines are explained and fixed in a test run (`S0_LINEAR=1`, FINDINGS §57). The
slot-0 per-draw wrap patch is drafted in `patches\unit0-wrap\`, and the AI assistant has an equivalent
`T0_WRAP` uncommitted in the main checkout. Next: the maintainer's yes, then run the README's test plan
(Athena, Penguin Village, Dark Tournament ring side). After that, Dark Tournament rubble and
brightness, in the maintainer's order.

## 6. Next texture problems after Athena (2026-10-22, read-only)

The slot settings today are as follows. Slot 0 is POINT, with clamp unless `T0_WRAP` picks the
game's wrap. Slots 1-6 and 9-15 are always LINEAR + WRAP. Slots 7-8 are border (shadow maps). So
**only slot 0 can smear an edge the way Athena did.** A texture on another slot that asks for clamp
gets WRAP: that bleeds the opposite edge in thin seams, but does not give flat faces.

**Dark Tournament rubble.** The default pictures in `stages\darktourn\darktourn_tiling.png` face
the intact side, where no rubble shows. The `S0_LINEAR` pictures face the broken side, and there
the rubble is textured with dark faces, much like RPCS3 `013909`. The flat-grey and streaky faces
come from the 10-21 capture `stages\s09\16_fight_20s.png`, taken before mips and slot-0 tiling.
They are most likely the same slot-0 clamp (inferred). They cannot come from slots 1+, because none
of those clamp.

Ranked by how visible they are:

1. **Blocky, stair-stepped edges on clouds and leaves** (Kameari intro clouds and tower, Penguin
   Village sky edges and palms, Alabasta palms). Slot 0 is POINT, so magnified alpha-cut edges show
   texel steps. `S0_LINEAR` smoothed Kameari (§36) but drew black boxes around the Penguin Village
   plants. A box is the whole quad turning opaque, which is too large for a filtering fringe. That
   points at the **lower mip levels' alpha**: LINEAR also blends between mips, and POINT mostly stays
   on one. **Test:** Penguin Village with `S0_LINEAR=1 TEX_MIPS=0`. If the boxes go, the mip chain's
   alpha is the bug (fix the mip upload), and LINEAR on slot 0 can then become the default, which
   fixes the stair steps everywhere.
2. **Dark Tournament rubble faces.** **Test:** a default run (T0_WRAP on) facing the broken side.
   If the flat or streaky faces are gone, it was slot 0. If not, add `FP_IDCOLOR_RT=0ED10000` to
   name the rubble program, and check its slot-0 wrap in the `T0_WRAP` log.
3. **Kameari "patchy tower" texture in the intro.** It is in the same intro shots as the clouds and
   was not checked after mips and tiling. **Test:** recapture the s01 intro with defaults, then once
   with `T0_WRAP=0`. If the patches change, it is slot 0 addressing. If they stay, include it in
   test 1.

## 7. Hidden Leaf Village (s07) trees and grass, and a correction to §6 (2026-10-22, read-only)

The maintainer: "some trees and grass" look wrong on the Naruto map. Captures: `stages\s07\13_intro_a.png`,
`14_intro_b.png`, `15/16/18_fight*.png`, taken 10-21 at 14:36, before mips, slot-0 tiling and the
shadow defaults.

- **Trees (intro A, top left):** the leaf clusters are drawn on solid black instead of sky. Each
  leaf card is opaque, and the black is its transparent texels.
- **Grass (intro A right edge and bottom left, intro B middle):** grass tufts show as solid green
  slabs with blades painted on, not cut-out blades.
- **Fight, top left:** the hanging plant decorations are dark squares.
- **Cliff in intro B (right):** long vertical streaks. These may be authored rock strata; RPCS3 is
  needed to tell.

**Correction to §6 test 1:** Penguin Village's black plant boxes are **not** caused by `S0_LINEAR`.
The default capture `stages\s05\14_intro_b.png` (10-21 12:35, no S0_LINEAR, no mips) already shows
the same boxes. So the boxes and the Hidden Leaf trees and grass are one problem: **cut-out foliage
drawn as whole opaque cards.**

Likely cause, ranked:

1. **Alpha-to-coverage is ignored.** The game cuts foliage with A2C (SET_ANTI_ALIASING_CONTROL bit
   4), which we never emulate unless `MENU_A2C=1`. That switch is still opt-in (FINDINGS §34, menu-fixes notes,
   `patches\menu-fixes`), and it was only checked on Yoshiwara, which has no foliage. The Naruto
   demo card's black-rimmed leaves were the same symptom.
   **Test:** Hidden Leaf Village and Penguin Village intros with `MENU_A2C=1`, A/B against the
   default. If the black and green cards turn into cut-out leaves and grass, make `MENU_A2C`
   default-on.
2. **The leaf alpha never reaches the test.** The material may output a constant alpha and cut with
   the texture's alpha some other way, or the texture format's alpha may be lost (DXT1 punch-through
   or 16-bit formats). **Test, only if 1 fails:** `RSX_CAP_PASSES=2 FP_IDCOLOR_RT=0ED10000` on the
   Hidden Leaf intro to name the leaf program. Then check its t0 format in the manifest and whether
   its alpha test is enabled (`[ALPHATEST]` in stdout prints only the first 4 enabled draws; a per-program variant would help).
3. **Point filtering on slot 0** only explains the stair-stepped edges (Kameari, Alabasta), not the
   boxes. That needs linear filtering on slot 0 once 1 is fixed. §6 test 1's `TEX_MIPS=0` idea is
   dropped, because the boxes predate mips.

Worth one RPCS3 screenshot from the maintainer: the Hidden Leaf Village intro, showing the trees and the
cliff, to settle the cliff streaks.

## 8. Second cause for the foliage boxes: the alpha test is never applied (2026-10-22)

The AI assistant noted that the Penguin Village boxes appeared with `MENU_A2C=1` on. In the code, the
backend still writes each draw's alpha test into the shader constants (`fp_alpha`), but the
current decompiler never reads it, and the backend never inserts a test. The first decompiler did
(0c2bb62); the one adopted later dropped it. So every alpha-tested card draws whole, and
`MENU_A2C`, which only converts A2C into an alpha test, cannot help. The capture headers show
alpha-tested draws (`alpha=(1,0.498,6,0)`) whose shaders have no `discard`.
Patch drafted: `patches\alpha-test\` (default-on; `ALPHA_TEST=0` turns it off), with a test plan.
This is now cause 1 for §7; A2C becomes cause 2 (test `MENU_A2C=1` on top of the patch).

### RPCS3 Hidden Leaf Village shots (the maintainer, 2026-10-22): `reference\rpcs3\Screenshot 2026-10-22 123038/123103/123135.png`

- **Forest (123038, 123103):** every tree is cut-out leaves with sky between them, bushes are
  cut-out, and the ground grass is a soft cut-out carpet. There are no black backgrounds and no
  solid slabs. This confirms our black-backed trees and green slabs are the missing alpha test
  (§8), not the game's look.
- **Rock slope (123103, left):** grey layered rock with moss and a natural texture, no long
  streaks. Our intro-B cliff (brown with vertical streaks) is a different spot, so it can't be
  compared directly. It looks suspicious, though; recheck it after the alpha-test and T0_WRAP
  builds.
- **Village street (123135):** street trees cut out cleanly, plus lanterns and cables. Our fight
  view of the gate shows the street through the gate, and it looks consistent.

## 9. Sky flashing white on Penguin Village and Kameari (2026-10-22, read-only + test patch)

The maintainer: the sky flashes white all the time on Penguin Village and Kameari, since well before the
Athena fix.

What the existing logs say:
- **Not dropped draws:** `[FRAMELOG]` in `stages\t0wrap3\s05` shows dropped=0 and ~570k/1M vertices.
- **Not the exposure on Penguin Village:** the fight tone-map gets a steady E=24 (La=0.03) in every
  logged frame. The 7200 lines alternating with it come from the luminance downsample program
  `0x0C6ABE81`, not from the tone-map, so they are not frame-to-frame changes. Kameari's tone-map sits
  at E=7200 all the time (over-exposed, the open luminance-readback issue, §35), which is steady,
  not flashing.
- **Still images do not show it** (`t0wrap3\s05\16/18`, `s01\16`): the flash is frame to frame.

Ranked causes:
1. **Sun-glare occlusion query always answers "fully visible" (0xFFFF samples)** (inferred). Our
   GET_REPORT returns 0xFFFF for every ZPASS query. A sun glare scaled by visible/expected samples
   would jump to white whenever the result is read. Both stages are sunny; Athena's night sky
   doesn't flash. **Test:** `patches\zpass-report\` (`REPORT_LOG=1`, then `REPORT_ZPASS=0`).
2. **A sky or sun pass reading a texture or target that is stale or white on some frames**: for
   example a glare/bloom buffer read before it is written, or alternating double buffers. **Test:**
   `RSX_CAP=<start>:10:1 RSX_CAP_PASSES=1` on a Penguin Village fight: 10 consecutive frames. Find
   the white one and the pass where it turns white.
3. **Exposure (Kameari only):** `LUM_READBACK=1` brings E down from 7200. If Kameari's flashes
   are clipped highlights that come and go with the camera, they should shrink.

### Kameari screenshot from the maintainer: white sky with a blue wedge on the far left

The Naruto intro card on Kameari shows the sky white. Only the far left is blue, and the border
between them is a **stair-stepped diagonal**. Reading (inferred):
- Kameari's tone-map runs at E=7200 all the time (every `[TONEMAP]` line in `t0wrap3\s01`), the
  stuck auto-exposure from §35. At that exposure, a light blue sky maps to near white, and only
  darker sky texels stay blue.
- The stair steps are the sky texture's texels, point-sampled on unit 0 and magnified. The
  white/blue border follows texel brightness. As the camera moves, texels cross that limit, which
  would look like flashing.
- So for Kameari, **cause 3 (exposure) moves to first place.** **Test:** Kameari with
  `TONEMAP_EXPOSURE=24` (Penguin Village's value) or `LUM_READBACK=1`. The sky should turn blue
  and stop changing. Penguin Village's tone-map is at 24, so its flashing still needs the ZPASS or
  frame-capture tests.

### Penguin Village flash: what changes between frames on sunny stages only (2026-10-22, reasoning from code)

The maintainer: it still flashes with T0_WRAP and the shadow fix off, so the bug is older. The AI assistant's stills show a
normal blue sky, so a burst capture is needed. Candidates, ranked:

1. **The sun's visibility, read from the scene depth.** A sun glare or lens flare decides how much of
   the sun shows. It does that either with an occlusion query (GET_REPORT ZPASS) or by sampling the
   scene's depth at the sun's screen position.
   - **Query:** we always answer 0xFFFF samples. The note in §38 that "GET_REPORT is only ZCULL
     stats" rests on the `[GET_REPORT]` log, which prints just the first six reports of a run, all at
     boot. So it is not ruled out. `patches\zpass-report` (`REPORT_LOG=1`) settles it in one run.
   - **Depth sample:** our scene-depth copies are taken per draw and re-cleared twice per batch
     (FINDINGS §41 pass 3). A flare reading 0x0E960000 can see the cleared value (1.0 = "sun fully
     visible") on some frames and real depth on others. That fits flashing on sunny stages only.
     **Check in the burst capture:** look for a small draw that samples 0x0E960000 or 0x8E960000 as
     a texture, typically a 1-4 vertex quad near the end of the scene. `ZPASS_DBG=1` lists draws that
     sample a zeta.
2. **The glare layer 0x0E590000.** It was never drawn on Yoshiwara (§38, §40) and is added twice by
   the composite. If Penguin Village or Kameari draws it only on some frames (when the sun is in
   view), a white layer over the sky appears and disappears. **Check:** in the burst, whether
   0x0E590000 gets draws, and `rt_0E590000.bmp` frame to frame.
3. **The HDR alpha of the sky.** The scene stores brightness in alpha, and the HDR encode divides by
   it (§40 pass 4). A sky dome whose alpha varies between frames, for example a blend or colour-mask
   state left over from an earlier pass, would jump to white. **Check:** `RSX_CAP_ALPHA=1` in the
   burst, and compare the sky's alpha across frames.
4. **Exposure:** steady on Penguin Village (E=24 in every logged fight frame), so it is unlikely
   there. It is still the main cause on Kameari (see above).
5. **Unlikely:** dropped draws (dropped=0), constant-buffer parity (it would affect the whole
   picture, not only the sky), T0_WRAP and the shadow fix (the maintainer: it flashes with them off).

**Burst recipe for the AI assistant:** a Penguin Village fight with the sky in view, using
`RSX_CAP=<start>:10:1 RSX_CAP_PASSES=1 RSX_CAP_ALPHA=1 ZPASS_DBG=1`, plus `REPORT_LOG=1` if the
zpass-report patch is in. Then find one white frame and one blue frame and diff their manifests
(draw lists and textures) around the sky draws, the composite and 0x0E590000.

## 10. Penguin Village white-sky frames: a quarter-resolution white layer over the sky (2026-10-22)

The AI assistant's burst `stages\pv_flash\burst\16b_burst_00..09.png` (FINDINGS §59): frames 00, 01, 05 and 09
have a white sky. Zoomed (00 against 02):
- **The blue sky is still there underneath.** In the white frames a thin blue fringe runs along every
  silhouette (tree, house, roof edge, lighthouse). Top left, a blue wedge follows the roof line.
- **The white has blocky, 4-pixel stair-step edges** around the objects. Objects have sharp edges
  everywhere else. So the white is a **low-resolution layer (about 320x180, a quarter of 1280x720)
  added on top of the finished sky**, upscaled without filtering. It covers the pixels that are
  "sky" in its own low-res copy of the depth.
- So it is **not a missing sky draw** (as §59 reads it). A post-process layer that marks sky pixels
  turns fully white on some frames: bloom, glare or light shafts from the sun, built in the 320x180
  chain (0x0D650000, 0x0D7A0000, 0x0D7F0000, 0x0D840000, 0x0D890000 in the Athena captures) and added
  in the composite. That would also explain Kameari's pale sky: the same layer, white most of the
  time.

Candidates for why it is white on some frames:
1. **Bright-pass or bloom constants stale or saturated.** The luminance/downsample program
   `0x0C6ABE81` reports E=7200 (La=1e-4) in every frame, even while the tone-map has 24. If a sky
   bright-pass uses that exposure, the sky saturates. Frames differ when the game alternates
   bloom-chain buffers or skips the pass.
2. **The sun term or occlusion in that pass** (the ZPASS answer or the depth copy, as in §9). The
   layer is only white when the sun counts as visible.
3. **A 320x180 target read before it is drawn** (double-buffered chain, stale content), the same
   family as the glare layer 0x0E590000 that is only cleared on Yoshiwara.

**Next capture (the decisive one):** a Penguin Village fight with the sky in view, using
`RSX_CAP=<start>:10:1 RSX_CAP_PASSES=1`. In a white frame and a blue frame, compare the 320x180 and
640x360 targets (`rt_0D7F0000.bmp` and the others), the composite's t1/t2 inputs, and their programs.
Then `FP_KILL=<that program>` is a one-run test that should make the white disappear. Optional:
`REPORT_LOG=1` (zpass-report patch) in the same run.

## 11. Loading screen before a match: white, only "Now loading..." (2026-10-22, read-only)

The maintainer's screenshots, copied here: ours is `reference\ours_loading_white.png`; RPCS3 is
`reference\rpcs3\loading_tips_lockon.png` and `loading_tips_dashing.png`.
- **RPCS3:** a full "TIPS" screen. A black banner with "TIPS" and the tip title, a grey manga-panel
  background, red borders top and bottom, one or two large in-game pictures in frames, a text bar
  with button icons, and "Now loading..." at the bottom right.
- **Ours:** pure white, with only "Now loading..." drawn correctly. The tours show the same
  (`stages\*\11_loading.png`, ~24 KB, nearly empty).

Reading:
- The one part that works, "Now loading...", is most likely in the persistent UI atlas used
  everywhere. Everything that is missing is **loading-screen-only art**: the tips pictures, panels
  and banner. These large images are loaded just for this screen, which points at **those textures,
  not the drawing.** It is probably not the same problem as the Penguin sky (§10): there is no
  low-res layer and no flicker.
- **Why white rather than black:** our backend binds a NULL SRV when a texture cannot be set up
  (`TEX_BINDDBG` path in `render_frame`). A null texture reads (0,0,0,0), and with alpha blending an
  alpha-0 layer is invisible. What shows is the clear colour or a white base quad underneath.

Ranked causes:
1. **The tips textures never get an SRV:** an unsupported size or format (for example a 1280x720
   or 2048-wide image, an NPOT linear image, or a format outside the switch in `render_frame`), or
   the texture pool full while loading. **Test:** a run with `TEX_BINDDBG=300 TEXFMTDBG=1` through
   the loading screen. `[TEXBIND] null SRV ...` lines with large sizes, or an unusual format there,
   name the textures and the reason.
2. **The textures are uploaded before the game finishes writing them.** The game streams them in
   while loading, and our cache keeps the first, still-empty copy, because a checksum match on
   unchanged pages skips re-uploading. **Test:** `RSX_CAP=<frame>:10:30 RSX_CAP_PASSES=1` spanning
   the loading screen. If late frames show the art, it is timing. If none do, it is cause 1 or 3.
3. **The textures live in main memory and are resolved through the wrong context** (local instead
   of IO, as happened once for the luminance readback, §40). They would read as zeros. Cause 1's run
   shows each texture's location and offset.

**Update 2026-10-22 (AI assistant thread, read-only): the page IS drawn; it comes out invisible.**
`stages\loading\cap\frame_13` (frame 6450) draws the whole tips page into the UI layer
`0x0C6ABF40`: op012-op013 the 320x360 panel, op014-op021 eight quads from the 826x360 picture
atlas, op022-op023 the 1280x63 and 1280x64 borders, op024-op025 the 640x360 image (all DXT5
linear 0xA8 at 0x03xxxxxx, fp code 0x06251137, vs=4, cmask=7, blend src-alpha/inv-src-alpha,
`tools\capdraws.py` lists them). The captured layer `rt_0C6ABF40.bmp` is pure white: every
pixel 255 in all channels, so those 14 quads changed nothing. Frame 6600 (`frame_14`) draws
"Now loading" (512x78, same format, same program code, same vertex program and blend) into the
same layer and it shows. Frame 6300 (`frame_12`) already has the tips quads, under the
"Battle Characters" summary; by 6600 they are gone. So FINDINGS §61's "never drawn" was read
from frame_14 only. What is left: the quads reach the GPU with alpha 0 or zero area. Not the
cause (checked in the backend): DXT5 goes up as BC3 (the visible 512x78 text takes the same
path), guest scissor is ignored, depth clip is off, blend keys decode to plain alpha blending.
Asked the AI assistant for a capture with `RSX_CAP_PASSES=2` (per-draw fp constants, alpha test, pre/post
snapshots), `RSX_DBG_VTX`/`RSX_DBG_VP` on the 826x360 picture (quad positions, UI vertex
program) and a `TEX_A1_UNITS=1` run (texture alpha forced to 1).

**Update 2 (2026-10-22, AI assistant thread, read-only): root cause. The loading screen shows a
DXT1 *snapshot* of the page, and the snapshot is taken from guest memory we never write.**
The AI assistant's run A (`stages\loading\cap2`, `RSX_CAP=6400:2:100 RSX_CAP_PASSES=2`) settles the
earlier question: at frame 6400 (`frame_00`) the tips quads paint the real art. Chained pre/post
diffs (pre = previous op's post): op012 changes the left half, op013 the right half (the 320x360
paper tile), op014-op019 change 4-15% each (the 826x360 comic-panel pieces; `pass_op0019_post`
is the grey comic-panel background RPCS3 shows), op020-op023 change nothing, then op024 (fp
0xE71D59BE, `col0 + c0`, no texture: the red/blue gradient) covers 100% and op025 covers 78%,
and the "Battle Characters" summary draws on top. So the draws, the BC3 upload, the UV scale and
the vertex colour are all fine; the page is composed every frame underneath the summary. At 6450
(`cap\frame_13`) the same page draws and a fade then covers the layer in white (`rt_0ED10000.bmp`
is black, so op011's copy is black too: the white comes from the fade at the end of the frame).
The loading screen proper is different: frames 6500 (`cap2\frame_01`) and 6600 (`cap\frame_14`)
draw none of the part textures. The UI layer gets one full-screen draw of a **1280x720 DXT1
linear texture at 0x0082C900** (fmt 0xA6, fp code 0x3C263AA6, `out = col0 * tex`, vs=4) and then
"Now loading..." (512x78). `pass_op0011_post` of frame_01 is 255 everywhere outside the text:
that texture samples pure white. EBOOT.elf names the page parts (`loading_game_tips_main_*`,
`_mes_*`, `_title_*`, `_chara_*` paks) and has a "Screenshot" facility, so the game composes the
page with the RSX, snapshots it into one DXT1 texture (so the part textures can go while the
stage loads) and displays the snapshot. On a PS3 the snapshot reads the rendered surface from
local memory (PPU/SPU, or an RSX 2D transfer into main memory first). In our port the D3D12
backend never writes a render target back to guest memory (COLOR_EXPOSURE.md found the same for
the luminance target; only `LUM_READBACK` does it, for targets up to `LUM_READBACK_MAXPX`
pixels, one frame late), NV3089 in `cellGcmSys.c` is a CPU copy of guest memory and NV0039 is
unimplemented. So the encoder reads a blank surface and the snapshot is white. What is not yet
known is which path the game uses and when (an in-FIFO transfer, or a fence then a CPU read),
which decides where the write-back has to happen. Asked the AI assistant for run C (`GCM2D_DBG=100000
GCM2D_TRACE=1 NV3089_GATE=<file>` with the gate file created at the summary screen) and run D
(`LUM_READBACK=1 LUM_READBACK_MAXPX=921600`: if the page shows, an end-of-frame write-back is
early enough). Diagnostic patch (needs the user's yes): `patches\loading-tips\` adds frame-gated
`MEMPEEK_FRAMES` / `VRAMSCAN_FRAMES` and `RSX_CAP_TEX=1` (capture-time texture source dump,
decoded by `tools\texbin.py`), so the snapshot bytes can be read directly: DXT1 blocks
`FF FF FF FF 00 00 00 00` mean "encoded from a white source", raw FF/00 mean "never encoded".

**Update 3 (2026-10-22 ~17:15, AI assistant thread): cause and fix.** Run F (`cap4\frame_05`,
`RSX_CAP_TEX=1`) dumped the snapshot's guest bytes, and they decode to the complete tips page
(`stages\loading\snapshot_decoded.png`); `TEX_NOCACHE`, `TEX_VERIFY=frame` and `TEX_MIPS=0` (runs
G, H) still gave a white screen. The fault is the `SCREEN_AS_TEX` heuristic in the backend's
per-draw texture bind: a texture that is exactly 1280x720 and not one of our offscreen RTs gets the
copy of the previous frame bound instead of its own bytes (added for the water reflection). The
snapshot qualifies, so the loading screen drew the white fade frame, then itself. Fix
`patches\loading-tips\loading-tips-fix.patch`: compressed formats are excluded from the heuristic
(default on, `SCREEN_AS_TEX_FORCE=1` reverts); the frame copy is bound only for an uncompressed texture whose guest bytes are empty, so the boot logos (also swallowed by the old rule, run I) keep their bytes too. Confirmation without code: `SCREEN_AS_TEX=0`.
Details FINDINGS §69.

## 12. Black still around leaves and plants after the alpha-test fix (2026-10-22)

`stages\labeled\leaf_fix.png`: on Hidden Leaf the trees cut out better with the fix (FINDINGS §59),
but dark, jagged card shapes remain around leaves and bushes. Penguin Village without `MENU_A2C` is
mostly clean; with it, there are black boxes. RPCS3 (`reference\rpcs3\Screenshot 2026-10-22
123038/123103.png`) shows clean leaves.

What the code says:
- The alpha test now runs only on draws whose own RSX alpha test is on (`[ALPHATEST] enable func=4
  ref=60`: GREATER 0.235). It tests colour target 0's output alpha.
- Draws that ask for **alpha-to-coverage** instead get no cut at all, because `MENU_A2C` is off by
  default. With `MENU_A2C=1`, each such draw becomes "alpha >= 0.5", and that is when the Penguin
  boxes appear. An alpha test can only remove pixels. So in the boxed draws, the output alpha must be
  >= 0.5 over the whole card, meaning **that alpha is not the leaf's coverage**. It is likely another
  value: the HDR scale the scene keeps in alpha (§35/§40), a vertex alpha, or an fp16-export alpha.

Ranked causes:
1. **The remaining leaf cards are A2C draws, which we draw whole.** Fixing this means using the
   right coverage value for A2C. That is probably texture 0's alpha (or whatever the program
   multiplies into it) rather than the program's final alpha, which here carries something else.
2. **The alpha that is tested is not the coverage alpha, even on alpha-tested draws** (fp16 vs fp32
   export, or an HDR-encode alpha). The partly cut trees fit this too.
3. **Texture alpha lost:** a remap (CONTROL1) that routes alpha to ONE, or DXT1 punch-through decoded
   as opaque. The headers show `remap=` and `fmt=` per unit.
4. **Mips mixing in black:** small, `TEX_MIPS=0` as an A/B.

**Capture for the AI assistant (decisive):** apply `patches\a2c-log` (logging only). Run a Hidden Leaf fight
with the trees in view, using `RSX_CAP=<start>:1 RSX_CAP_PASSES=2 FP_IDCOLOR_RT=0ED10000`, plus one
normal-colour capture of the same frame. In the coloured frame, find the colour of the black cards
and its program. Then read that program's `fp_opNNNN.hlsl` header:
- `alpha=(en, ref, func, a2c)`;
- t0 `fmt` and `remap`;
- what the HLSL writes to the output alpha.

If a2c=1 and the output alpha is not t0's alpha, cause 1 is confirmed. The fix then tests t0's
alpha for A2C draws (a small rewrite in `vp_get_fp_pso`), which I can draft as soon as the header
shows where the alpha comes from.

### §11 update: the tips art IS drawn; it comes out invisible, over a white background (2026-10-22)

The AI assistant's capture `stages\loading\cap` (RSX_CAP=4500:30:150; FINDINGS §61) read as "the game never
issues the tips draws". The manifests say otherwise:
- **frame_13 (6450)** has 12 textured, blended quads into the UI layer 0x0C6ABF40 (ops 012-025), all
  with the loading art seen in `[TEXFMT]`: linear DXT5 320x360 (0x032EDE80, twice), 826x360
  (0x03422080, eight times), 0x0330A080, 0x03396080, and 640x360 (0x0346AE00, twice). That is the tips
  page: background tiles, pictures, banner.
- **frame_14 (6600)** blends one full-screen **1280x720 linear DXT1** image (0x0082C900, fp
  0x0D64FF01) into the UI layer, then the two 512x78 "Now loading" pieces. So the page is either
  composed from pieces (13) or drawn as one image (14).
- **Both frames come out white anyway.** So the draws are issued and their textures bind. The texels
  they sample must be (nearly) transparent, or the draw lands outside the screen.

**Where the white comes from:** before the art, op010/011 copy the post chain onto the UI layer
(fp 0x0D64ED41, the tone-map pattern with E=7200). The scene target 0x0ED10000 is cleared to
(0,0,0,0) and stays black (`rt_0ED10000.bmp`). Tone-mapping an empty HDR scene with alpha 0 at E=7200
can give white. On RPCS3 the page covers it completely, so you never see it there.

Ranked causes for the invisible art:
1. **The source bytes in VRAM are empty or stale.** The art lives high in local memory (0x0330..-
   0x04EC....). If the game moves it there with a copy we do not carry out (an SPU or RSX transfer),
   our VRAM still holds zeros. A DXT5 block of zeros is fully transparent, so the white shows
   through. **Test:** `TEX_SRCDBG=200` through the loading screen. It reports for each upload
   whether the source is populated (`nz` counts). Add `TEX_SAVE=1` if needed.
2. **Linear DXT pitch.** All of these are LN (linear) DXT with CONTROL3 pitch. `MENU_TEX_PITCH`
   is **off by default** (`menu_tex_pitch_on`, 0 unless set). Without it, wide NPOT images
   (826, 805, 711, 671, 637, 1059) are read with the wrong row pitch. That gives scrambled blocks,
   not empty ones, so it is second. **Test:** the same run with `MENU_TEX_PITCH=1`, checking whether
   the tours set it.
3. **BC textures whose width or height is not a multiple of 4** (826x360, 805x32, 711x42, 637x329,
   1059x354, 230x91, 300x43, 52x25). `vp_upload_tex_slot_impl` passes the guest size straight to
   CreateCommittedResource. D3D12 may reject it, or round it and upload with the wrong footprint.
   **Test:** `TEXUP_DBG=1` (upload log) in the same run, looking for failures or odd footprints on
   those sizes.

### §11, pass 3: the art's source bytes are fine, so look at the draw's alpha (2026-10-22)

The AI assistant's `TEX_SRCDBG` run (`stages\loading\srcdbg`) found the tips images populated, and
`MENU_TEX_PITCH` made no difference. What is left:
- **"Now loading..." draws with the same kind of texture** (linear DXT5, 512x78, and 78 is not a
  multiple of 4), the same VP (vs=4), the same blend (SRC_ALPHA / INV_SRC_ALPHA; keys 0x126298B and
  0x121108B decode to that) and cmask=7, and it is visible. So NPOT/BC sizes, pitch and blend state
  are not the cause.
- That leaves **what alpha the art draws come out with**: the texture's alpha after decode, times
  whatever the fragment program multiplies in. That is usually a fade constant (fp_constants) or the
  vertex colour's alpha. Loading pages typically fade in. If the fade value reaches us as 0, every
  piece and the 1280x720 page (frame_14 op011, fp 0x0D64FF01) draw fully transparent over the white.

Two cheap runs separate this:
1. `TEX_A1_UNITS=1` (forces unit 0's alpha to 1). If the page appears, the texture's alpha decodes
   as 0, which points at DXT alpha decode.
2. `RSX_CAP=<loading frame>:1 RSX_CAP_PASSES=2`. Every draw's `fp_opNNNN.hlsl` header lists
   `fp_constants[]`, and the HLSL shows what multiplies the alpha. A constant of 0 there, or a
   multiply by `col0.a`, points at the fade value.
   (`FP_TEX` would be the third check, but it anchors on `_po.c0 =`, which the current decompiler no
   longer emits, so it does nothing now.)

### §12 result: the leaf and grass cards cut out through depth export, which we never enable (2026-10-22)

The AI assistant's capture (FINDINGS §62): the tree cards (code 0x8E610328) and grass (code 0x70B1A414) have
the alpha test off and a2c=0. They write depth 10 for transparent texels (so the depth test drops
them) and their own z otherwise. The backend never passes SHADER_CONTROL's depth-export bits
(0xE) to the decompiler, so the export is dropped and the whole card draws. A2C and the alpha
test are ruled out for these. Fix drafted: `patches\depth-export\` (`FP_DEPTH_EXPORT`, default on).

## 13. White sky = the "Lens Godray" fan drawn in pieces (2026-10-22, read-only + patch)

The quarter-size white layer of §10 is the sun-shaft post effect ("Lens Godray" in the EBOOT; shaders
`sp_*_postprocess_god_rays*`, `sp_*_postprocess_lens_godray_*`). Per frame at 320x180: op712 writes the
sun colour (255,222,154) where the depth copy says "nothing drawn" on the sun side (= the sky), black
elsewhere, into 0x0D7A0000; op713-715 blur it radially three times (0x0D890000 -> 0x0D840000 ->
0x0D7F0000); op716 draws back into 0x0D7A0000, blend off, sampling the blur: a 42-vertex TRIANGLE_FAN
from the sun's screen position to 41 points around the screen edge (14 top, 6 right, 14 bottom, 7
left), built by `func_005567AC` (ppu_recomp_028.cpp 30324; 20-byte vertices, xyz + uv). It covers the
whole target and replaces the raw mask with the scaled shafts. op717 adds the result onto the frame.
Raw mask + sky = white, so a white sky means the fan left the sky uncovered.

Why it leaves it uncovered: in the six captures the fan is recorded with n=120 (40 triangles, whole
fan) in frame_00/04 and n=114 (38) in frame_01/02/03/05, and exactly those four frames have 211 px of
raw sun colour at rows 0-10, cols 125-173 of rt_0D7A0000.bmp. 114 = 3*(42-4): the FIFO delivered the
fan as two DRAW batches and the backend expanded each as its own fan (the second one centred on a
screen-edge point), losing the wedges at the cut. The game's indexed draw emitter `func_00657248`
(ppu_recomp_033.cpp 49541) cuts the first batch at a 128-byte index-buffer boundary, so the cut moves
with the ring position; a cut on the top edge costs a small wedge, a cut on the right/bottom/left edge
costs most of the upper screen. RSX hardware treats all batches of one BEGIN/END as one primitive
stream; the backend did that only for TRIANGLES and native indexed strips.

Fix drafted: `patches\sky-flash\fan-join.patch` (default on, `FAN_JOIN=0` off, `FAN_LOG=0D7A0000` logs
the batches). Continuing STRIP/FAN batches are expanded against the stream (fan centre = stream's first
vertex) and appended to the previous record. Test plan in the README. The ZPASS idea (§9) is ruled out:
the game never issues a ZPASS report (`patches\zpass-report` is not needed).

Addendum (2026-10-22 evening): the AI assistant's FAN_LOG run confirms the split (indexed fan, 42 indices, cut
after 8/16/24/32/40 indices, moving per frame) but v1 joined nothing: it refused every indexed fan
because the game keeps primitive restart enabled. `patches\sky-flash\fan-join-v2.patch` (on top of
v1) checks the actual indices for the restart value instead and logs the refusal reason (`why=N`).

## 13. The last two open items: Hidden Leaf cliff streaks and Dark Tournament rubble (2026-10-22 evening)

**Hidden Leaf cliff streaks: not seen any more.** The streaks were in the 10-21 intro capture (brown
cliff with long vertical streaks), taken before slot-0 tiling. The current captures (`stages\dexp\s07`:
both intros and the fight, on the depth-export build) show natural rock and dirt slopes with no streaks.
The fight view is very close to RPCS3 `Screenshot 2026-10-22 123103.png`: cut-out trees, grass, sky.
Most likely it was the slot-0 clamp smear (the same as Athena's stars), fixed by T0_WRAP (inferred;
not the exact same camera). To close it: one intro with the same camera as the old shot, if the tour
lands there.

**Dark Tournament.** The newest capture is `stages\t0wrap3\s09` (13:xx), which predates depth export
(2a32615) and the triangle-fan join across FIFO batches (6afba53/27b2548). What it shows:
- **15_fight_start:** the ring's stone steps and sides are flat, untextured dark grey/black. In RPCS3
  `014020` the ring is light grey stone tiles with visible joints.
- **16/18_fight:** the ring edge (or rubble) is covered in a harsh blue-white speckle. RPCS3's rock is
  a smooth dark texture.
- The floor dirt, stands, fence, purple pillar and big screen look right.

Likely causes, ranked:
1. **Geometry split across batches.** The rubble and ring meshes may be triangle fans. Until 6afba53,
   fans that crossed a FIFO batch were split, which gives wrong triangles: flat faces and stretched
   streaks. Fan-join is now default on. **Test:** a fresh Dark Tournament tour on the current build,
   then the same with `FAN_JOIN=0` as an A/B.
2. **Speckle = shadow acne.** A noisy light/dark pattern on lit receivers is the classic sign of a
   shadow-map comparison with no bias. The rubble is a large receiver facing the light. **Test:**
   `ZPASS_SHOW=4` (always lit). If the speckle and the dark ring faces go away, it is the shadow
   compare or bias, not the texture.
3. **Slot-0 addressing or filter** on the ring texture. **Test:** `T0_WRAP=0` and `T0_LINEAR=0` A/B
   on the same run, looking only at the ring.
