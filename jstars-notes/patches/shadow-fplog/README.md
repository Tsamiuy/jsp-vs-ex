# ZPASS_FPLOG: per-draw inputs of one fragment program (screen-space shadow pass), logging only

Patch: `shadow-fplog.patch` (`libs/video/rsx_d3d12_backend.c`, +32), on ede600e. `git apply --check` passes. NOT
compile-checked (no compiler on my side). Nothing changes unless `ZPASS_FPLOG` is set.

`ZPASS_FPLOG=<fp hex>[,<n>]` (default n = 120) prints, for each of the first n draws of that program, one line:
`[ZFPLOG] flip=F d=D fp=0x.. czt=0x<scene zeta behind the colour target, or none> | u7 z=0x.. dirty=0/1 cast=N
clr=N cdraw=N | u8 ... | u15 ...`
- `z`: the zeta that unit samples (through its copy).
- `dirty=1`: the DepthRT was written since its last copy, so this draw refreshes the copy now.
- `cast` / `clr` / `cdraw`: caster draws, clears, and colour draws depth-bound to that zeta so far in this batch.

Run: Hidden Leaf fight, gate view, `ZPASS_FPLOG=9CA80C1,400` plus the usual 0.6 s bursts.
What I'll read, comparing "shadows present" and "gone" frames:
- u15 (scene depth): is it the same zeta as czt of the scene draws, and has it got colour draws (`cdraw` > 0)
  before this pass? A different zeta or `cdraw=0` in "gone" frames = the pass reads an empty/old depth.
- u7/u8: `cast` > 0 before the pass in the same batch (this frame's casters landed) vs 0 (last frame's map).
