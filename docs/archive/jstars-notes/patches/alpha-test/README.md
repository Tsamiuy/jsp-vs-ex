# The guest alpha test is never applied (foliage drawn as whole cards)

Patch: `alpha-test.patch` (`libs/video/rsx_d3d12_backend.c`, +31), on b42bca4 (`git apply --check`
passes against the clean main checkout). A syntax-only compile with the build's warnings shows no
errors and no new kinds of warning. Not built or run. On by default; `ALPHA_TEST=0` turns it off.

## Finding (checked in the code)

- The backend still records each draw's alpha test (`dr->alpha_ctl`: enable, func, ref) and writes
  it into the fragment constant buffer: `ts[16..18]` in `vp_record_cb`, which the shader sees as
  `fp_alpha` (x = enable, y = ref/255, z = func & 7).
- **No fragment program reads it.** The decompiler that was in use first emitted a dynamic test on
  that slot (`rsx_alphatest`, commit 0c2bb62, "guest alpha test"). The decompiler adopted later
  (`rsx_fp_decompiler.c`) only declares `float4 fp_alpha;`. Its own `rsx_fp_apply_alpha_test*`
  helpers are never called by the D3D12 backend, and `vp_get_fp_pso` adds no test. The capture
  headers show the alpha test set on real draws (for example `alpha=(1,0.498039,6,0)`, GEQUAL 0.5,
  in `stages\athena\normal_cap`), and none of those shaders contain a `discard`.
- **That is why `MENU_A2C=1` changed nothing on the plants.** It converts alpha-to-coverage into
  an alpha test, which is then dropped. Every alpha-cut card is drawn whole: black where its
  transparent texels are black (Penguin Village plants, Hidden Leaf trees, hanging plants) and
  green where they are green (Hidden Leaf grass slabs). It is probably also the Naruto card's
  black-rimmed leaves from the menu notes.

## What the patch does

In `vp_get_fp_pso`, after decompiling, it inserts the following just before the program's colour
output (`return (_o == _o)` for one target, `_out.t0 =` for several):

```hlsl
if (fp_alpha.x != 0.0) {
    const float _a = _o.a, _r = fp_alpha.y; const int _f = (int)fp_alpha.z;
    const bool _p = _f == 1 ? _a < _r : ... : _f == 6 ? _a >= _r : _f == 7;
    if (!_p) discard;
}
```

It tests colour target 0's alpha before the NaN guard, as RSX does. The test is dynamic (read from
the constant buffer), so the pipeline cache key does not change.

## Risks

- **Speed:** every compiled pixel shader now contains a `discard`, even when the draw's test is
  off. On most GPUs that disables early depth rejection for all draws, so fill-rate-heavy scenes
  may lose some fps. If the fps drops noticeably, the follow-up is to insert the test only for
  draws that enable it, and salt the code hash by the enable bit (the same pattern as
  `FP_IDCOLOR_RT` and `T0_WRAP`).
- **Surfaces that were "accidentally right":** anything the game alpha-tests now gains holes where
  the game intends them. A surface that loses pixels it should keep would point to a wrong alpha in
  that program, for example an fp16 export or a texture format whose alpha we decode wrongly.
- **`FP_CONSTBUF=0`** (old literal-constant path): the inserted code needs the rewritten b1 layout
  and is skipped when `fp_alpha` is not declared, so that path behaves as before.

## Test plan

1. Hidden Leaf Village intro and fight, defaults: the trees should be cut-out leaves against the
   sky, and the grass separate blades. A/B against `ALPHA_TEST=0`.
2. Penguin Village intro, with and without `MENU_A2C=1`: plants without boxes. If they are only
   right with `MENU_A2C=1`, those plants use alpha-to-coverage, and `MENU_A2C` should become the
   default.
3. Naruto demo card (menu notes): the black rims on the forest leaves should be gone.
4. Yoshiwara and Athena fight: fps against the current build (the speed risk above), and nothing
   should go missing.
5. `[ALPHATEST]` in stdout lists the first four alpha-tested draws: a quick sanity check that the
   inputs are set.
