# Quiet logs (2026-10-26)

`quiet-logs.patch` (+15/-5, on 1a4a29d, `git apply --check` OK). It compiles with the same warnings
in both files. It changes logging only.

- `rsx_d3d12_backend.c`, `d3d12_draw_arrays_impl`:
  - The `[D3D12] draw_arrays #N` printf now covers the first 20 calls only.
  - It used to also print every 1000th call. On Hidden Leaf that is ~80 lines a second, and it was
    3.7-3.9% of the drawing thread's samples (§36, printf <- d3d12_draw_arrays_impl).
  - `DRAW_LOG_EVERY=1000` restores the old trace.
- `sys_event.c`, `sys_event_port_send`:
  - `[evt] port_send(port=N): queue N FULL -> EBUSY` is printed for the first 8 sends, then once per
    1000, with a running count.
  - It was logged ~4,100-15,500 times per run, 2.5-3.0% of the game's frame-loop thread (§38).

Expected: about 3-4% less drawing-thread time and ~3% less on the frame-loop thread (*inferred* from
the sample shares). Check: f/s with real flips, and that `[evt] ... (N so far)` lines appear.

## perf-cheap.patch (same folder, +19/-5 backend, applies alone on 1a4a29d)
PERF=1 only:
- `QueryThreadCycleTime` (a system call, two per draw) now runs only with `PERF_CYCLES=1`. It feeds
  the [PERF] cyc/vert figure.
- `perf_now` reads the QPC frequency once.

DRAW_SAMPLE charged vf_timed_end + vf_cycles with ~20% of the drawing thread's samples (§36). So
every PERF=1 measurement, including the 10.8 us per object and the busy ms, overstated the drawing
thread's cost. With this patch PERF=1 runs are closer to normal play. Real f/s should still be
judged without PERF.
