# Zeta targets v2: apply every depth clear, use the game's depth state on kept depth

Patch: `zeta-targets-v2.patch` (`libs/video/rsx_d3d12_backend.c`, +44), on top of 2d03eee
(`git apply --check` passes; the main checkout was clean). Not built or run. Zeta targets stay
opt-in (`ZETA_TARGETS=1`). Flip the default once this passes.

## What went wrong in v1 (FINDINGS §31, `run\shots_zeta\shot_230s.png`)

All four side effects look like one thing: **regions of the frame that stop updating.**
- The logo "boxes" hold an older picture of the scene behind the logo.
- The "ghost text" includes Ace's earlier subtitle (`...of my little`) under Goku's.
- The "blocky patches" are rectangles of older scene.
- The 230 s and 240 s shots are almost identical.

The large faded "Son Goku" behind the name plate is not a bug: RPCS3 shows it too (`184154.png`).

Two causes, both inferred from the code and the shot (nothing was run):

1. **The game's depth clears never reached the kept depth.** v1 only recorded Z clears made
   while an offscreen colour target was bound. A Z clear with no colour target (dropped unless
   `SHADOW_PASSES`) or with the display bound (frame-boundary path) was ignored. Kept depth was
   then never reset, so each frame was tested against the previous one's.
2. **Colour ops used the forced LESS_EQUAL + write.** With the shared buffer, cleared at every
   switch, that was harmless. Against kept depth, every 2D quad (logos, name plate, subtitle) wrote
   its depth, even when the game draws it with depth test and write off. The next frame's scene
   then failed the depth test inside those rectangles: those are the boxes and the stale text.

## What v2 changes

1. Every depth/stencil clear of a zeta surface is recorded as a clear op on that zeta's DepthRT,
   whatever colour target is bound. It uses the existing depth-only clear record, so replay
   needs no change. A colour+Z clear still clears the colour as before.
2. A colour op whose depth is kept (an offscreen target with a zeta that has a DepthRT) is drawn
   with the game's own depth test, function and write mask. That is the same per-op `zstate` and
   PSO path the shadow casters already use, and the state is part of the PSO key. With the test
   off, nothing is written (GL semantics). Display draws and draws on the shared buffer are
   unchanged.

## Switches

| Env | Effect |
|---|---|
| `ZETA_TARGETS=1` | Turns zeta targets on (v1 + v2). Off by default, as in 2d03eee. |
| `ZETA_CLEARS_V1=1` | v1 clear rule (offscreen colour target bound only). |
| `ZETA_FORCED_DEPTH=1` | v1 depth state (forced LESS_EQUAL + write) on kept depth. |
| `ZPASS_DBG=1` | Also logs the first 16 zeta clears: offset, colour target, depth value. |

## Checked

- Syntax-only compile (`clang-cl /Zs`, the build's flags): no errors, no new warning kinds.
- `git apply --check` on 2d03eee.

## Test plan for the AI assistant

1. Build, then run the demo with `ZETA_TARGETS=1 ZPASS_DBG=1 PS3_VERBOSE=0`, screenshots at 230 s
   and 240 s.
   - **Expected:** Goku visible in front of the water, logos without boxes, only the current
     subtitle, no blocky patches, and the two shots differ.
   - The log should show `[ZPASS] zeta clear 0x0E960000 ...` lines each frame.
2. If boxes or stale text remain, A/B each half:
   - `ZETA_CLEARS_V1=1` (clears) and `ZETA_FORCED_DEPTH=1` (depth state);
   - the half whose switch brings the problem back is the one that fixed it.
3. If the frame still freezes with both halves on, the freeze is a separate problem: compare
   `[PERF]` batches/s with `ZETA_TARGETS=0`. The extra cost per frame is only the depth copies for
   sampled zetas.
4. Re-check the title screen and the Ace card.
