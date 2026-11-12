# Toward 30 fps: ranking and the next two patches (2026-10-20)

Baseline: `perf\baseline-2026-10-20`, build 62ec701, `PS3_VERBOSE=0`. It runs at **~16 fps,
62 ms per frame** (1 batch = 1 frame). 30 fps needs **33 ms, so 29 ms must go**. Neither patch
has been built into the game or run. Both are **opt-in**, and both pass `git apply --check` on
62ec701 (apply `env-cache.patch` first, then `async-fetch.patch`).

## Where the 62 ms goes

| cost | per frame | evidence |
|---|---|---|
| Vertex fetch (native strips), on the present thread | **~31 ms** | baseline: vtx 50-53%, ~840k vertices at ~90 cycles |
| Rest of the FIFO drain: command decode, draw recording, `getenv` | ~17-19 ms | drain 77-82% minus vtx |
| render_frame | ~9-12 ms | 15-19% |
| Present / idle | ~2 ms | 0% / 3-4% |

- **Only the present thread is busy.** In the run3 sampler, every other thread is 95%+ waiting.
  The present thread splits roughly into: our exe ~45%, `VCRUNTIME140` (memcpy/memset) ~27% and
  `ucrtbase` ~25%.
- **`ucrtbase` is very likely `getenv`.** It costs **1.1-1.3 µs per call** on this machine
  (measured), and about a third of the ~200 `getenv` calls in the video code aren't cached. At
  least ~7 of those run per draw, at ~600 draws per frame: `vp_record_cb` x3, `BLENDDBG`,
  `STRIP_EXPAND`, `FP_CONSTBUF` in `vp_get_fp_pso`, and others.
- **The vfetch-fast patch is already in the baseline.** It is 299aa12, which went from 8.8 to
  11.4 batches/s at the time. Together with strip-batch and native-batch it gives the
  ~90 cycles per vertex in the baseline, down from ~4,000. Fetch is now cheap per vertex, but
  there are 840k vertices per frame, all on one thread.

## Patch 1: `env-cache.patch`, opt-in with `RSX_ENV_CACHE=1`. Expected saving: ~5-12 ms

- The new header `libs/video/rsx_env.h` caches `getenv` answers by variable name. It is
  included by `rsx_d3d12_backend.c`, `cellGcmSys.c` and `rsx_commands.c`.
- Lock-free reads. The first answer per name is kept, which is what the cached call sites
  already do, and the video code never sets variables.
- `test/test_env.c`: answers are identical to the CRT, at **1.07 µs → 0.010 µs per call**.
- The range is inferred: ~5 ms from counting calls per draw, ~12 ms if `ucrtbase`'s sampler
  share is all `getenv`.

## Patch 2: `async-fetch.patch`, opt-in with `VFETCH_ASYNC=1`. Expected saving: ~20-27 ms

- **How it works:** the strip paths, native and expansion, still build the vertex list and
  the fetch plan from the current state on the present thread. They now queue the decode to 3
  workers (`VFETCH_ASYNC_THREADS=<n>`). The walker carries on with the FIFO while the workers
  decode.
- **It can't change what is drawn.** Every queued fetch finishes before:
  - `render_frame`;
  - any method that shows the guest RSX progress: label and semaphore writes, SET_REFERENCE,
    reports (new `g_rsx_sync_hook`, called at the top of `rsx_process_method`);
  - blits, user commands and flips in the walker;
  - a native join that copies the previous call's vertex;
  - `STRIP_DUMP`.

  So the guest can never be told a buffer was consumed before it was read. The decode code and
  its inputs are unchanged.
- **Test** (`test/test_vfa.c`): this is the engine cut verbatim from the patched backend. It
  ran 30 frames of 900k vertices in 300-800-vertex jobs with random label syncs. Output is
  **byte-identical**.
  - Synchronous: 27.0 ms per frame.
  - Async: 10.7 ms per frame including the final wait, with nothing to overlap.
  - In the game the walker's own ~17 ms of decoding runs at the same time, so the present
    thread should only pay for building lists, posting jobs and waiting at sync points.
- `[PERF2]` gains `async fetch wait`, the time spent waiting at sync points. If it is large,
  the game syncs (writes labels) often, and the overlap is smaller than hoped.
- `VTX_POS` is not reported for queued fetches.

## Together

62 ms - (5 to 12) - (20 to 27) ≈ **23-37 ms, i.e. 27-43 fps**. This is inferred, not measured.
Suggested test order:
1. `RSX_ENV_CACHE=1`
2. `VFETCH_ASYNC=1`
3. both

## Next, if still short of 30 or to go higher (not drafted)

1. **Indexed native strips with GPU primitive restart.** Match strips are mostly 3-index pieces,
   and each join adds 2-3 padding vertices. So 840k output vertices is probably 2-3× the unique
   vertices. Uploading each unique vertex once, plus an index buffer with D3D12's strip-cut value
   (`IBStripCutValue` in the PSO, 0xFFFF/0xFFFFFFFF in the index buffer), would cut fetch work,
   the 256-byte-per-vertex upload bandwidth (~215 MB per frame now) and GPU vertex work alike.
   This changes the replay and PSO setup, so it is a bigger patch.
2. **Name the remaining drain cost.** The exe has no PDB, so the sampler's `[samp] rva` lines
   can't be named. One build with `/Zi` and `/DEBUG` (or `/MAP`) would turn them into function
   names.
3. **Compact vertex.** Write only the attributes the VP reads (8 of 16 for characters). That
   halves upload bandwidth, but needs a per-mask input layout and stride in the PSO.
