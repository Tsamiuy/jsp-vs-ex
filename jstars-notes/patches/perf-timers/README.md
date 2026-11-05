# Where the match's frame time goes: timer patch + kick-wait patch (2026-10-20)

Two independent patches. Neither has been built or run. Both passed `git apply --check`
against the main tree on 2026-10-20 (`ps3recomp-src` at `b7efcb5`, and `jstars\port`).

| file | applies to | what |
|---|---|---|
| `perf-timers.patch` | `ps3recomp-src` (`rsx_d3d12_backend.c`, `cellGcmSys.c`) | measurement only, no behaviour change |
| `jstars-main-kick-wait.patch` | `jstars\port\main.cpp` | **behaviour change**: present loop wakes on the FIFO kick |

## Findings (from the code and existing logs, no new run)

1. **The vtx timer never covered the match.** It sat inside `upload_tris_vp`,
   `upload_tris_vp_indexed` and `upload_strip_vp_native`. The match draws indexed strips through
   `upload_strip_vp_indexed` (the default expansion). So `vtx` reads `0k verts` for the whole
   run, old code and new (`run\perf_vfetch.txt`, `run\perf_legacy_pass3.txt`).
2. **The §19 figure (1.6 µs per vertex, 85-96%) does not describe this build.** It was taken
   when that timer still saw the match's draws (an older draw path). It cannot be checked
   against the current code. The current evidence agrees with the standalone tests:
   - Before the mid-frame flush, a match batch held ~1M output vertices (§22).
   - The fetch patch saved ~0.035 µs per vertex standalone (0.053 → 0.018), which predicts
     ~35 ms per batch.
   - Measured: 8.78 → 11.40 batches/s, i.e. 114 → 88 ms, a 26 ms saving.
   - This is inferred from fps; the patch below measures it directly.
3. **"guest" in `[PERF]` is not game logic.** It is the present thread's window minus
   `render_frame`. It contains:
   - the FIFO drain: command decode, draw recording and **all the vertex expansion**;
   - the swap-chain `Present(1, 0)` with vsync, which runs after `render_frame`'s timer stops;
   - the present loop's `Sleep(4)`;
   - the vblank/flip handlers;
   - any time spent waiting for the guest to submit.

   The PPU threads' own work runs on other threads and is not in this number at all.
4. **The J-Stars present loop never wakes early.**
   - `jstars\port\main.cpp` (copied from the template) idles with `Sleep(4)`.
   - cellGcmSys has a kick: when a guest thread spin-reads the ref register (`GCM_REFPOLL`,
     default on) and the fence queue is dry, it calls `SetEvent(s_gcm_kick_ev)`.
   - But that event is created lazily by its first waiter, `cellGcm_fifo_kick_wait`, which
     J-Stars never calls. So `s_gcm_kick_ev` stays NULL and the kick is a no-op.
   - The toolkit's `boot_main.cpp` already switched to `cellGcm_fifo_kick_wait(4)` for this
     reason (its comment: one 16 ms tick per fence made a movie run at 2 FPS).
   - Here every fence round trip can cost up to ~4 ms. Twenty per frame would be ~80 ms,
     about the whole 88 ms batch. That figure is a hypothesis; `[PERF2]` tests it.

## perf-timers.patch

- **Every VP upload is timed at its call site** in `d3d12_draw_arrays` / `d3d12_draw_indexed`
  (macro `VF_TIMED`). This covers strip/fan expansion, quads, tris and native strips, both
  legacy and plan paths. The old inner timers are removed, so nothing is counted twice.
  `vtx` and `cyc/vert` on the `[PERF]` line now include the match. Cycles are the recording
  thread's only; with the worker pool on, compare with `VFETCH_THREADS=0`.
- **New `[PERF2]` line**, printed after each `[PERF]`:
  ```
  [PERF2] drain 0.00s (0%, N calls, M with work) of which vtx 0.00s, mid-frame render 0.00s | present 0.00s (0%) | rest 0.00s (0%)
  ```
  - `drain`: wall time in `cellGcm_rsx_process_fifo`. `with work` means `get` advanced.
  - `mid-frame render`: `render_frame` runs started by `vp_flush_if_full`. They happen inside a
    drain and are also in `render_frame`, so they are subtracted once.
  - `present`: swap-chain Present.
  - `rest` = window − render_frame − present − (drain − mid-frame render). This is the loop's
    sleeps, the vblank/flip handlers and waiting for the guest.

## How to read one `PERF=1` run of the match

| `[PERF2]` shows | meaning | next step |
|---|---|---|
| `rest` large; drain calls near the loop's ceiling (2 per ~4 ms iteration, ~400-500/s), little time per call | the present loop is pacing the guest | apply `jstars-main-kick-wait.patch` |
| `drain` large and mostly `vtx` | vertex expansion dominates | native strips (no 3× expansion) or a compact vertex |
| `drain` large but `vtx` small | FIFO decode / draw recording cost | profile the drain |
| `present` large | vsync / swap-chain blocking | try `Present(0, 0)` or a waitable swap chain |

For the PPU side (real game logic), the runtime already has a sampler that has never been run on
J-Stars: `PS3_SAMPLE=2` (`runtime/ppu/ppu_loader.cpp`, `ps3_sampler_start`). It prints
`[samp] tid … guest=… ntdll=… other=…` per thread plus the hottest host functions. A guest thread
with mostly `ntdll` samples is waiting, not computing.

## jstars-main-kick-wait.patch

Replaces `Sleep(4)` with `cellGcm_fifo_kick_wait(4)` in `frame_clock` and declares the function.
- When nothing kicks, it keeps the same 4 ms ceiling.
- `GCM_FIFO_KICK=0` restores the plain sleep at runtime.
- The kick only fires for ref-register waits (`GCM_REFPOLL`). Waits on labels do not kick, so
  if `[PERF2]` still shows a large `rest` after this patch, the guest is probably waiting on
  labels.
