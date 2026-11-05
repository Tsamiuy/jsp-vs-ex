# Why STRIP_INDEXED=1 got slower, and version 2 (2026-10-21)

`strip-indexed-v2.patch` applies on top of the current `jstars-port` (`85c4157`; `git apply
--check` also passes on `dbe9dba`). It changes only `rsx_d3d12_backend.c` and compiles with no
new warnings. **Not built or run.** **Opt-in:** `STRIP_INDEXED=2`. `STRIP_INDEXED=1` (the first
version) is unchanged.

## Why version 1 lost 1-4 fps (`strips\*`, FINDINGS §42)

Present-thread cycles in the vertex timer, per 20-frame window (`[PERF]`):

| run | vertices | Mcyc | cycles per vertex |
|---|---|---|---|
| s06 base / ix | 17.3M / 6.7M | 581-597 / 800-835 | 34 / 119-124 |
| demo base / ix | 15.0M / 5.7M | 492-503 / 771-792 | 33 / 134-138 |

- **Fewer vertices, more cycles.** Vertices fell to a third, but the present thread spent
  **~270 M more cycles per window**, ~13 M per frame, about +40% vertex time.
- **Where those cycles went:** version 1 runs its dedup map on the present thread. The map is
  768 KB, probed up to 8 slots in 3 arrays, once for every index.
- **Why the saving didn't count:** the work it removes (decoding, uploading and shading
  duplicate vertices) is done by the **async fetch workers and the GPU, which are not the
  bottleneck**. The present thread is: in fights it is busy ~32 ms a frame (FINDINGS "Fight
  speed"). So the trade moved work onto the slowest thread to save it on idle ones.
- **The AI assistant's guess was half right:** the async workers still ran. They just weren't what limited
  the frame.

## Version 2: the present thread only copies indices

- **Present thread:** per ~256-index call, it reads the guest indices, turns a restart into
  0xFFFFFFFF, reserves `count` vertex slots and `count` index slots, and posts one job. A call
  that continues the record reuses the record's fetch plan, so it skips building it again (that
  build hashes the whole vertex program). `STRIP_IX2_PLAN_REUSE=0` turns the reuse off.
- **Worker:** it builds the strip indices with a per-call dedup map, and the async fetch then
  decodes the distinct vertices.
- **Ordering:** the existing sync points (render, labels, blits, flips) wait for the jobs before
  anything reads them.
- **Reserved slots** that dedup doesn't fill are never indexed.

**Offline check** (`test/test_strip_ix.c`, the patch's loop logic): 50 random J-Stars-like
streams in 256-index calls. Versions 1 and 2 both produce **the same non-degenerate triangles, in
order and winding, as today's padded strip, in 50 of 50**.

Present-thread loop alone (no fetch), per index, on a 3+restart stream:

| version | per index |
|---|---|
| padded (today) | 1.0 ns |
| v1 (dedup map) | 2.0 ns |
| v2 (copy) | 0.1 ns |

## Will it beat the current speed? (inferred, not measured)

- **Probably a little:** fewer present-thread cycles than today. It does no join logic, writes
  one entry per index instead of ~1.35, and builds the plan once per record. The workers, the
  upload and the GPU also handle far fewer vertices.
- **Probably not a lot from the loop:** the loop itself is a small part of the vertex timer. In
  game the timer shows ~46 cycles per index today, against ~3 for the loop in isolation. The rest
  is per-call cost: building the plan, which hashes the vertex program; posting the job; and,
  under `PERF=1`, the timer's own `QueryThreadCycleTime` calls. The plan reuse removes the
  biggest per-call item for continuing calls.
- **Expected:** somewhere between break-even and a few ms per fight frame.

**Run:** s04 and s06 with `PERF=1` for three settings: `STRIP_INDEXED=0`, `=1` and `=2`.
- If `vtx` with `=2` is below `=0`, keep version 2.
- If not, drop the indexed idea; `=0` stays the default either way.

## If version 2 doesn't help

The remaining per-call cost is the real target. The safe fix is to have
`rsx_commands.c` count vertex-program uploads (`vp_ucode` generation) and key `vf_input_mask`'s
cache on that count instead of re-hashing the program on every call. That removes the hash from
every strip call, indexed or not.
