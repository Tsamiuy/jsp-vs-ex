# ZPASS_LOD0: shadow casters sample mip 0 (leaf shadows blinking with camera zoom)

Patch: `shadow-lod0.patch` (`libs/video/rsx_d3d12_backend.c`, +48/-1), on 3ebcc8c. `git apply --check` passes.
NOT compile-checked (no compiler on my side). Nothing changes unless a switch is set.

How: the per-draw texture view (SRV) of a caster's texture exposes mip 0 only, so whatever LOD the shader
computes (Sample, SampleGrad in the T0_WRAP helper, bias) lands on the full-resolution level. No shader change,
no new PSOs, so it can switch at runtime.

- `ZPASS_LOD0=1`: depth-only (shadow caster) draws always see mip 0.
- `ZPASS_LOD0=2`: alternates ON/off every `ZPASS_LOD0_SEC` seconds (default 3) in the same run and logs
  `[ZPASS] LOD0 ON|off at flip N` at each switch: a fair same-fight comparison.
- `TEX_LODCLAMP=1` (any draw): when the guest's TEXTURE_CONTROL0 caps max LOD below 1.0, the view exposes
  mip 0 only (the guest clamp our static samplers can't apply). The principled version, if the casters'
  textures turn out to carry such a clamp.
- `ZPASS_DBG=1` now logs each caster texture once: `[ZPASS] caster fp=.. unit .. tex .. WxH mips=..
  ctl0=.. lod=[min,max]` (+ "-> mip 0 only" when applied).

Run (Hidden Leaf, camera zooming in and out, 0.6 s bursts): `ZPASS_LOD0=2 ZPASS_DBG=1`. Match each burst
frame to the ON/off windows by flip (or by time). Expected if the theory holds: in ON windows the leaf
dapples stay in wide views; in off windows they drop out as before. Also tell me the `lod=[..]` of the
fp 0x9CA80C1 caster texture: max LOD < 1 means the guest itself clamps and TEX_LODCLAMP=1 is the right
default; a full range means the game relies on its own mips and LOD0 for casters is a workaround.
