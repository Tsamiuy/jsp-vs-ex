# Shadow passes, round 2: read the maps as comparisons, use the casters' own depth state

Patch: `shadow-fix.patch` (only `libs/video/rsx_d3d12_backend.c`, +110/-22). It goes on top of
c3eaf8b (`git apply --check` passes there). Not built or run. Shadow passes stay opt-in
(`SHADOW_PASSES=1`), as in c3eaf8b.

## What the first test showed (FINDINGS §29, screenshots in `jstars\run`)

- **The skin blotches are not shadows.** `shots_nbatch\shot_300s.png` has no shadow passes at
  all, and `shots_shadowcmp\shot_300s.png` has shadow passes in compare mode. Goku's arm has the
  same blotches in both, so the "camo" has another cause. PALE_TEXTURES.md cause 2 was wrong
  about that. Untested leads: the material's other inputs, such as t2 = `0x0D840000`, a 320x180
  screen-space buffer, or the vertex normals and tangents.
- **The characters read the maps as comparison results.** Two units bind the cascades:
  - the material FP binds them as `0xA5` (A8R8G8B8) with remap `0xAA93` and zfunc 4 (GREATER);
  - the environment FP binds them as `0xB0` (DEPTH24_D8) with zfunc 4.

  With the byte bitcast, a map at its clear value (depth 1.0) gives `.x` ~ 1 everywhere, and every
  character went black. The shader sums `.x` over its PCF taps, so it expects 0/1 comparison
  results, with 1 meaning "in shadow" under GREATER (reference > stored). With nothing in the map,
  that comparison gives 0 (lit). That matches the compare run looking like "no shadows".
- **Not yet known:** whether the casters actually land in the maps. A compare run where the maps
  only hold their clear value would look exactly like the one we got.

## What the patch changes

1. **A8R8G8B8 + zfunc is a depth comparison by default.** It goes through the same decompiler
   path as DEPTH24_D8. `ZPASS_ARGB_BITCAST=1` restores the byte bitcast. `ZPASS_ARGB_CMP` no
   longer does anything.
2. **No silent fallback to a plain depth read.** The decompiler's comparison emitter only takes
   matching nearest/linear min and mag filters on one level. Anything else (mip count > 1, a
   mipmapped min filter) used to fall back to a plain read with no log in the A8R8G8B8 path. It now
   compares with the min filter's linearity and logs the approximation once (`ZPASS_DBG=1`).
3. **Casters use the guest's depth test, function and write mask.** Every op now records
   `zstate` (test, write, GL function). A depth-only op's PSO takes those values instead of the
   fixed LESS_EQUAL + write, and the state is part of its PSO key. With the test off, nothing is
   written (GL semantics). `ZPASS_FIXED_DEPTH=1` restores the fixed state.
4. **Logging (`ZPASS_DBG=1`), so one run answers "do the casters land?":**
   - `[ZPASS] batch N target T (zeta ...): drawn D (V verts), no-PSO S, clears C (last d)`. This
     prints for the first 24 batches with depth-only ops, then every 200th.
   - `[ZPASS] caster on target T: zstate=... (test/write/func) cull fp vp verts`, once per distinct
     state.
   - The per-unit sampler line now names the mode actually used (`depth compare`, `A8R8G8B8
     bitcast` or `plain depth read`). It used to print "bitcast" whatever the switch said.

## Checked (no game build, no run)

- Syntax-only compile of the patched file (`clang-cl /Zs`, the build's flags): no errors, no new
  warning kinds.
- `git apply --check` on c3eaf8b.

## Test plan for the AI assistant

Run the demo with `SHADOW_PASSES=1 ZPASS_DBG=1 PS3_VERBOSE=0`.

1. Check the `[ZPASS] batch` lines for targets 0 and 1 (`0x0CE40000`, `0x0CA30000`):
   - **drawn > 0 and no-PSO 0:** the casters land. Look for shadows on the ground and on
     characters. They may be subtle in a toon game; compare against `SHADOW_PASSES=0`.
   - **drawn 0, or no-PSO > 0:** the casters do not land. Search stdout for `[FP] PSO FAIL ...
     nrt=0` (the PSO with no colour target) and `[VPFB]` (the garbage-projection fallback, which
     replaces a light matrix whose `c[0]` is not in (0, 8)). Try `VP_NOFIXPROJ=1`.
   - **clears with `last` = 0.0000:** the game clears to 0. Then the caster `func` should be
     GREATER or GEQUAL (a reversed map). The patch now honours that function; before, the fixed
     LESS_EQUAL could never pass.
2. Check the sampler lines: units 7/8 of both FPs should say `depth compare`.
3. Take screenshots at the usual times; `run\shots_shadowfix\` would fit the naming.

## Still not modelled

- Polygon offset (SET_POLYGON_OFFSET_*), so some acne is possible.
- Stencil in the maps.
- Float depth.
- The static samplers. The comparison itself uses its own taps, so only the WRAP addressing past a
  cascade edge matters.
- Depth clipping. PSOs never set `DepthClipEnable`, so every pass already clamps depth instead of
  clipping. That is what shadow casters usually want, and it was already so before this patch.
