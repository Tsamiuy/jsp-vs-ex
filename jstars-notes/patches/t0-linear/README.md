# T0_LINEAR: wrapped unit-0 draws use the game's own filter

Patch: `t0-linear.patch` (`libs/video/rsx_d3d12_backend.c`, +20/-6), on e78f5c1 (T0_WRAP 4b2e7c4 +
alpha test). `git apply --check` passes against the clean main checkout. A syntax-only compile with
the build's warnings shows no errors and no new warnings. Not built or run. On by default;
`T0_LINEAR=0` gives exactly today's T0_WRAP.

(My earlier `unit0-wrap` draft used static samplers s16-s23. That cannot work on ps_5_0, which has
only 16 sampler slots, as the AI assistant found. This patch needs no new samplers.)

## The plant boxes are not a filter problem

Penguin Village's default capture `stages\s05\14_intro_b.png` (10-21, point filter, no S0_LINEAR, no
mips) already shows the black plant boxes. They come from the alpha test the shaders never applied,
which is now fixed in e78f5c1 (`ALPHA_TEST`). So unit 0 can take linear filtering without bringing
the boxes back. Linear only softens the cut edge, which RSX does as well.

## What it does

- `dr_t0_wrap` also reads unit 0's TEXTURE_FILTER (`tex[0].filt`): mag [26:24] == 2 (linear), or min
  [18:16] in {2 linear, 4 linear-mip-nearest, 6 linear-mip-linear}, means "linear". Only draws that
  already go through T0_WRAP (wrap or mirror on s or t) are affected. Clamp/clamp draws keep s0,
  point, exactly as now.
- For a linear draw, the mode gets +9 (so it gets its own pipeline key), and `_t0w` samples through
  `rsx_samp[1]` instead of `rsx_samp[0]`. s1 is LINEAR + WRAP, the same as every unit except 0,
  so no new sampler is needed.
- A clamped axis in a mixed draw (e.g. wrap s, clamp t) is kept half a texel inside the edge, using
  `GetDimensions`. Otherwise the WRAP sampler would blend in the opposite edge at 0 and 1.
- `T0_WRAP_LOG=1` now also prints `linear=0/1`.

## Risks

- `S_CLAMP_UNITS` with bit 1 set would make s1 clamp, and that would show up here. It is a test
  switch only.
- Mirror axes sampled through a WRAP sampler: at the mirror fold, bilinear can mix one texel from
  across the fold. That leaves a faint 1-texel line at most.
- Unit 0 draws that are **not** wrapped (clamp/clamp) stay point-filtered. That keeps the stair-steps
  on clamped cut-outs (Kameari clouds, palm cards). The next step, now that the alpha test is in:
  apply the game's filter to all unit-0 draws (the same switch, without the "wrapped only"
  condition), checked against the debug font.

## Test plan

1. Planet Namek, against `stages\labeled\namek_wrap_compare.png`: the ground should no longer be
   grainy. With `T0_LINEAR=0` the grain comes back.
2. Athena sky: still stars, no lines (the stars may look a little softer).
3. Yoshiwara and Dark Tournament: no new seams at texture edges.
4. Penguin Village: no plant boxes (with the alpha test on).
5. `T0_WRAP_LOG=1`: Namek's ground program should show `linear=1`.
