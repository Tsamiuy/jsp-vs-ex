# [PERF6]: where the drawing thread's frame goes (2026-10-25, the maintainer's yes)

`perf6.patch` (+46/-5, `libs/video/rsx_d3d12_backend.c` only) applies to `ps3recomp-src` at
5160f10 (`git apply --check` passes). It compiles with the same warnings as before. It is a
diagnostic: the new timers run and print only with `PERF=1`, and nothing changes what is drawn or
when. Without PERF the draw callbacks pay one `perf_on()` check (a cached static).

## The line
After each `[PERF2]`, in ms per batch (a [PERF] window is 20 batches):
```
[PERF6] ms/batch | render_frame R = gpu-wait G + prepass P (textures T) + record C + other O
        | submit S | next-frame wait N | present Pr | drain D = draws W (vtx V, mid-frame M) + decode/other X
```
- **gpu-wait**: `wait_for_gpu()` at the start of render_frame. It drains the whole GPU before
  touching the upload buffers, so the CPU waits for the previous frame's GPU work every frame.
- **prepass**: the existing [PERF] prepass time (SRV writes, pipeline lookups). **textures** is the
  [PERF] tex time, which happens inside it.
- **record**: the replay loop that records the D3D12 command list (the existing `s_perf_gpu`, never
  printed until now).
- **other**: the rest of render_frame.
- **submit**: Close + ExecuteCommandLists. **next-frame wait**: `move_to_next_frame()`'s fence wait
  for the next back buffer. Both were outside every timer before, so they counted as idle `rest`
  in [PERF2]. **present**: as in [PERF2].
- **drain**: the FIFO drain. Of it:
  - **draws** is the time in the draw callbacks (record building, vertex work, the mid-frame
    flushes they trigger);
  - **decode/other** is everything else: FIFO walking, state methods, texture binds.
- As in [PERF2], mid-frame renders run inside the drain, so they appear in both drain and
  render_frame.

## Run for the AI assistant (same power mode, fixed characters per CPU_PROFILE §30)
Hidden Leaf fight and the demo fight, `PERF=1 GCM_FLIPCOUNT=1`, defaults (heat patches and
fifo-fast on). From the fight windows, report the [PERF6] parts averaged. Ideally also take the
heavy windows (lowest flips) separately. If gpu-wait or next-frame wait is large, the CPU and GPU
are serialized, and keeping one more frame in flight is the fix to try. If draws or record
dominate, it is per-draw work.
