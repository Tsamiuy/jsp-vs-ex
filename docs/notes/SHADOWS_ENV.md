# Environment shadows flicker (trees, buildings) — 2026-10-27, read-only analysis

The maintainer: shadows cast by scenery (trees, buildings) disappear and reappear as the camera or the character
moves; character shadows are fine.

## What we already know (FINDINGS §29-§55)

- Shadow passes and zeta targets are on by default; the receivers' compare runs `stored OP reference`
  (`ZPASS_CMP_SWAP` on) and lookups outside a map read lit (`S_BORDER_UNITS=0x180`, the game asks for
  border addressing on both maps).
- Two shadow maps are drawn every frame (shadows\dbg\stderr.txt): target 2 = zeta `0x0CE40000`
  2048x1024 (sampled on unit 7) and target 3 = zeta `0x0CA30000` 1024x1024 (unit 8), each with ~106-109
  caster draws, depth test/write on, func LESS, front faces culled (cull=0x3), cleared to 1.0 once.
- Character shadows work, so the compare, border and caster depth state are right at least for one map.

## Leading suspect: the 2048x1024 map is a cascade atlas our backend overlaps

A 2:1 shadow map is the classic layout for two square cascades side by side. On RSX the cascade's place
in the map is set by the viewport transform (SET_VIEWPORT_SCALE/OFFSET). Our backend applies only the z
part of that transform; x/y are identity (`vp_record_cb`, rsx_d3d12_backend.c ~5799: "x/y identity"),
and the D3D viewport is the viewport rectangle or the whole target. If the game places cascade B with
the transform, both cascades cover the whole map and B overwrites A. A receiver then compares against
the wrong cascade: whether a tree's shadow shows depends on where the tree and the camera are, and the
game refits cascades every frame, so the shadows blink as things move. The 1024x1024 map holds one view
and is unaffected, which matches "character shadows are fine". Not confirmed: the only logged caster
viewport on target 2 is `0,0 2048x1024` (the existing log keys on depth state, not placement, so a
second cascade would not have been printed).

## Patch and check

`patches/shadow-vpxy/` (README has the run plan): `ZPASS_DBG` prints each caster's x/y placement, and
`ZPASS_VPXY=1` honours the transform for depth-only draws, with a per-cascade scissor.

## If it isn't an atlas (next suspects)

1. Caster culling: casters cull front faces (`cull=0x3`); camera-facing tree cards then cast only when
   their back faces the light. Test `CULL_OFF=1` once (all draws double-sided) to see if tree shadows
   stop blinking.
2. Ordering: receivers sampling the map before that frame's casters land (our copy-on-sample
   `dt_snapshot`) would show last frame's cascade fit; that drifts more than it blinks, so lower.

## Update: not an atlas (the AI assistant, b0ed18b, Hidden Leaf, ZPASS_DBG=1)

One caster placement on 0x0CE40000 (rect 0,0 2048x1024, scale (1024,-512), offset (1024,512)), i.e. one view
filling the map; 0x0CA30000 likewise (512,-512)/(512,512). The atlas theory is ruled out; ZPASS_VPXY stays
off. The AI assistant is testing CULL_OFF=1 (perf/shadows_env). Other candidates, most likely first:

1. **Caster face culling** (being tested). Casters cull front faces (cull=0x3). Camera-facing tree cards and
   thin one-sided building parts cast only while their back faces the light, so they blink as the camera
   turns. If CULL_OFF=1 fixes it, the real fix is a depth-only-only cull override (ZPASS_CULL), or the
   winding of our offscreen depth passes is flipped relative to the guest's.
2. **Map refit with a one-frame lag.** The game refits the 2048x1024 map around the camera and both
   fighters every frame. If receivers read the map before that frame's casters land (our copy-on-sample
   dt_snapshot keyed on `dirty`; draws are replayed per batch), they use this frame's light matrix with
   last frame's map: shadows slide or vanish while the view moves and settle when it stops. Check: log per
   flip the order of "clear 0x0CE40000", "casters into 0x0CE40000" and "first draw sampling unit 7", and
   whether dt_snapshot copied after the casters.
3. **Casters beyond the light's depth range.** Our pipelines clamp depth (DepthClipEnable FALSE, DEPTH_CLIP
   blacks out the demo), RSX clips. A building partly behind the light's far plane is clamped to 1.0 =
   "no caster" in some fits and inside the range in others. Check: depth range of target-2 casters (min/max
   z after the viewport map) against the refit; test DEPTH_CLIP=1 limited to depth-only draws.
4. **Receiver compare precision/bias.** D24 storage vs the receiver's reference from TXP: if the shader
   divides by q or adds a bias we drop, scenery near the comparison edge flips with distance. Check: the
   environment receiver's FP (fp 0xFBFFFC1 samples units 7/8/15) for a bias term and how TXP is lowered.
5. **Is it also in RPCS3?** The 2048x1024 map covers the whole fight area, so thin casters (poles, branches)
   can drop out at that density on real hardware too. A short RPCS3 clip of the same spot would tell.

## Update 2: flicker reproduced on Hidden Leaf; what's ruled out; next check

The AI assistant's evidence (perf/shadows_env/flicker_example.png, leaf_base/16b_burst_*): tree shadows on the ground,
gone ~0.6 s later when the camera pulls back, back again ~0.6 s after that. All of them go at once, not one
by one, which argues against per-caster culling (CULL_OFF run was inconclusive anyway).
Ruled out from logs: atlas (one placement per map, ZPASS_DBG on 3 stages); DepthRT eviction (only 5 zeta
targets, each created once, MAX 16); casters missing (every batch draws 82-128 casters into 0x0CE40000 and
0x0CA30000, no-PSO 0, cleared once). So the map is drawn every frame; the open question is whether the trees
are in it when the shadows are gone, or whether the receivers stop finding them.

Next check (existing ZPASS_SHOW, no new code), Hidden Leaf fight, 0.2 s bursts while the camera zooms:
- run A `ZPASS_SHOW=2 ZPASS_SHOW_UNIT=7`: floor shows the stored 2048x1024 map depth. Tree silhouettes
  present in "gone" frames = map fine, receiver side; absent = map side (depth range/clamp).
- run B `ZPASS_SHOW=2 ZPASS_SHOW_UNIT=8`: same for the 1024x1024 map, to learn which map the tree shadows
  near the fighters come from (if they come from the fighter-fit map, a wide camera that pushes the trees out
  of its fit would drop them; RPCS3 should do the same then).
- run C `ZPASS_SHOW=5 ZPASS_SHOW_UNIT=7`: 0.5 + 4·(reference − stored): flat grey with dark tree shapes when
  working; a whole-floor jump to white/black in "gone" frames = reference out of range (fit/depth range).

## Update 3: ZPASS_SHOW runs (showA/B/C) read

- A (stored depth of unit 7 painted by the compare helpers): floor evenly lit, no tree shapes; B (unit 8
  rewritten): leaf dapples still there and still blinking; C (unit 7 diff): normal image with dapples. So the
  dapples do NOT come from the compare path of fp 0xFBFFFC1 at all: rewriting its unit-7 and unit-8 helpers
  doesn't touch them.
- The other reader is fp 0x9CA80C1, which reads units 7/8/15 as A8R8G8B8 **bitcasts** (TEX, not TXP), so it
  rebuilds depths from the bytes and compares in its own code, like the fog pass. Unit 15 is the scene depth:
  this looks like a screen-space shadow pass (scene depth → world position → shadow map → mask). ZPASS_SHOW
  only rewrites compare helpers, so it was blind to it.
- Blinking with a static camera then fits a timing problem on that pass's inputs: e.g. its unit-15 scene
  depth copy (dt_snapshot, copied once per "dirty") taken before the scene depth is complete in some frames,
  or the scene depth alternating between the two 1280x720 zetas (0x0F0C0000 / 0x0E960000) and the pass
  reading the other one. Either gives wrong positions → whole-mask on/off.
Next: (1) FP_KILL=9CA80C1 (does the dapple vanish for good?) and FP_IDCOLOR=1 at a "present" moment, to confirm
which program paints the dapples; (2) then log per draw of that fp: flip, the zeta bound on units 7/8/15, and
whether each DepthRT was written after its last snapshot (patch to draft once (1) confirms).

## Update 4: fp 0x9CA80C1 is the leaf CASTER (the AI assistant, fplog2)

`[ZPASS] caster on target 2 ... fp=0x9CA80C1` — the program draws alpha-tested foliage into the 2048x1024 scenery
map (the unit 7/8/15 sampler lines were stale state). FP_KILL removed the casters, hence no dapples. No-PSO
skips are 0 in every batch, so the casters are drawn; the blinking is in what they write.
Leading suspect: **texture LOD in the caster pass.** Since 678d973 textures carry full mip chains (TEX_MIPS
default on), and our samplers are static (they can't honour the guest's per-texture LOD clamp/bias from
TEXTURE_CONTROL0/FILTER). When the camera pulls back, the game refits the scenery map over a larger area, the
leaves shrink in it, the caster samples a smaller mip, box-filtered alpha drops below the alpha-test
threshold, and whole leaf clusters stop casting. Zoom in → mip 0 → dapples back. That matches "all at once,
with zoom". Check: TEX_MIPS=0 on Hidden Leaf; stable (if grainy) dapples confirm it. Fix candidates: honour
the guest LOD clamp (likely max LOD 0 for these casters), or sample mip 0 in depth-only draws (ZPASS_LOD0).

## FIXED (pending the maintainer's play-test), 2026-10-27

Cause: the leaf shadow casters (fp 0x9CA80C1, alpha-tested foliage drawn into the 2048x1024 scenery map) sampled
smaller mips of their texture when a wider camera made the game refit the map over a larger area; the
box-filtered alpha of those mips falls below the alpha-test reference, so whole leaf clusters stopped casting.
Evidence: ZPASS_LOD0=2 same-fight A/B on Hidden Leaf (perf/shadows_env/lod0ab_crop.png): mip 0 on → dense,
sharp dapples; off → clearly fainter. The caster texture 0xCB84EB80 allows LOD 0-7 (ZPASS_DBG), so it isn't a
guest LOD clamp we drop (TEX_LODCLAMP doesn't apply). Fix: depth-only (caster) draws see mip 0 only, via the
per-draw SRV (patches/shadow-lod0); the AI assistant is making it the default. Ruled out on the way: cascade atlas
(ZPASS_VPXY, one placement per map), DepthRT eviction, missing casters, receiver compare.
