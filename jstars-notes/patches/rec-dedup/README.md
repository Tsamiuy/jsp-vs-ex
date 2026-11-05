# REC_DEDUP: fewer D3D12 calls per drawn object (2026-10-26)

`rec-dedup.patch` (backend +36/-10, applies alone on 1a4a29d). It compiles with the same warnings.
**Not built or run.** Default on; `REC_DEDUP=0` restores every call exactly as before.

In the record loop of `render_frame` (one pass per drawn object):
- `SetPipelineState` is skipped when the PSO is the previous object's.
- `RSSetViewports` + `RSSetScissorRects` are skipped when both are unchanged. The two target
  switches inside the loop, which set their own viewport, invalidate the remembered one.
- `IASetPrimitiveTopology` is skipped when the topology is unchanged.
- The two constant buffers' GPU addresses are read once per frame, not twice per object.

Nothing else in the loop sets the PSO, viewport or topology (checked in the code: the only other
setters are the two target switches, which are handled). The descriptor table and the per-object
CBVs still change every object and are left alone.

Why: in DRAW_SAMPLE (§36) the driver and runtime calls from this loop were ~8% of the drawing
thread's samples (NVIDIA 6.2%, D3D12 2.2%, all charged to render_frame). Expected: part of that, the
[PERF6] `record` time (2-4.5 ms per batch) going down (*inferred*; how many calls repeat is not
measured).

Run: Hidden Leaf FIXED_CHARS=1, real flips, REC_DEDUP=0 vs default. Compare [PERF6] record ms at
similar draw counts, and screenshots (geometry, HUD, shadows).
