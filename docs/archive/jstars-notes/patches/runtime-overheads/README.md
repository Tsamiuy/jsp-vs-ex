# Runtime hot-path overheads: items 1-3 of CPU_PROFILE §11 (2026-10-23)

`runtime-overheads.patch` applies to `ps3recomp-src` at `eee9efe`; `git apply --check` passed on
the main checkout. It is **runtime only**: no renderer or `jstars\port` change. It touches four
files:

| file | change | off switch |
|---|---|---|
| `runtime/spu/spu_workload.c` | **job fingerprint cache**: the three dispatch paths call `spu_workload_fingerprint_cached`. Per thread, 16 recent images are kept as (address, size, bytes, fingerprint); if the image still `memcmp`s equal to the saved bytes, the saved FNV is returned, otherwise it is fully hashed again. Images over 256 KB are always hashed. | `SPU_FP_CACHE=0` |
| `runtime/spu/spu_coherency.c` | the reservation bitmap and the armed flag are exported (`g_spu_coh_bitmap`, `g_spu_coh_armed`) | (used by the next row) |
| `runtime/ppu/ppu_loader.cpp` | **inline reservation test** in `VM_WRITE_COH`, the commit of every out-of-line guest store; it was a call to `spu_coh_is_reserved` per store. **Store watch gated**: `barrier_watch_hit` runs only if `PPU_WVAL` or `PPU_WWATCH` was set at start, or the runtime's barrier sync watch is armed (still checked per store). | `PS3_COH_INLINE=0`; `PS3_FAST_DEBUG_HOOKS=0` |
| `runtime/spu/spu_drain.c` | **`spu_depth_guard` ring per thread** instead of one static array that every SPU thread wrote on every drain (one cache line bouncing between cores) | `PS3_FAST_DEBUG_HOOKS=0` |

All three are **exact**:
- the cache returns a fingerprint only for bytes identical to the ones it hashed;
- the inline test reads the same bitmap with the same bit math;
- the hooks still run whenever a watch is armed;
- the depth ring is diagnostic only. Its report now lists the tripping thread's own recent sites.

## Checks (offline)

- **Compiles:** all four files with clang-cl (`/Zs`, the game's include paths). The warnings are
  the same as on the unpatched files; only their line numbers shift.
- **Exact** (`test/test_fpcache.c`, using the cache function cut verbatim from the patched file):
  200,234 calls on 12 job images that are re-dispatched, rewritten in place, resized and moved.
  **0 mismatches** against the full FNV.
- **Cost per dispatch:** 37 KB images, 22,000 dispatches. **Full FNV 30.1 µs, cached 1.1 µs.**
  At ~2,200 dispatches a second that's ~64 ms of CPU saved per second, spread over the SPU job
  threads. Each job also starts ~29 µs sooner.

## Expected (inferred)

From CPU_PROFILE §10, these were:
- 20-36% of the SPU job threads' code time (fingerprint);
- 27% of the main thread's (reservation call);
- 37% of one PPU thread's (store watch);
- 55% of one SPU thread's (depth ring).

**Total CPU:** should drop somewhat. Most threads' CPU is still waiting, and a few busy threads get
faster.

**fps:** may rise a little. SPU jobs and the main thread are on the frame's hand-off chain. This
patch doesn't touch the drawing thread (that is item 4).

## Run for the AI assistant: demo fight before/after

Same as the precise-sleep run (`perf\psleep`), with `thread_cpu.ps1` and no `PS3_SAMPLE`:
1. Before: `SPU_FP_CACHE=0 PS3_COH_INLINE=0 PS3_FAST_DEBUG_HOOKS=0`.
2. After: defaults.

Compare:
- total and per-thread CPU;
- fps and `[PERF4]` over the last 40 windows;
- frames over 87 ms;
- screenshots;
- the SPU job rate.

`PS3_VERBOSE=0` hides the per-job lines, so for the job rate either count `[spurs-job] dispatch
HIT` lines in a short `-VerboseLog` run, or compare `[PERF]` windows per second as a proxy.
