# Why async under-delivered, and the fix (2026-10-20)

`strip-carry.patch` applies to `ps3recomp-src` at `266bfc6`. On 2026-10-20 `git apply --check`
passed against the main checkout. It changes only `rsx_d3d12_backend.c`. **Not built or run in
the game.** It is opt-in: it is on with `VFETCH_ASYNC=1` or `STRIP_CARRY=1`, and
`STRIP_CARRY=0` forces it off.

## Cause 1: nearly every native strip call forced a full sync (found in code, confirmed by test)

- **The streams:** match strips are 3-index pieces plus a restart, 4 entries each. FIFO batches
  are 256 indices, a multiple of 4. So almost every native-strip call **starts with a join**:
  the previous call ended on a restart.
- **The join:** it repeats the previous call's last vertex. In list mode that vertex was copied
  out of the upload heap *after* the fetch (`prev0`), so under `VFETCH_ASYNC` each such call ran
  `vfa_sync()` (wait for everything queued) and then fetched synchronously.
- **The effect:** async barely overlapped anything.
- **The test** (`test/test_native_batch.c`) feeds an aligned 3+restart stream in 256-index calls.
  **1,171 of 1,172 calls** took the copy path without this patch, and **0** with it.
- **Matches the runs** (`perf/speed30-2026-10-20`): with async, the present thread still spends
  `vtx` 0.44-0.48 s per window, ~22-24 ms per frame at 62-67 cycles/vertex. Only building lists
  and posting jobs should be left there, ~10 cycles/vertex.

**Fix:** keep the guest index of the record's last output, and put that index in the list for an
`o == 0` join instead of copying. The output is **byte-identical** to the per-vertex path in both
streams tested (383,791 and 449,997 vertices). The old copy remains for the one case where no
index is known.

## Cause 2: frames lock to vblanks, so gains between 34 and 50 ms don't show (inferred from code)

- J-Stars sets flip mode 2 (VSYNC; `SetFlipMode(mode=2)` in stdout).
- The runtime completes a flip only on the frame clock's 60 Hz ticks (`cellGcmTickFlip`). A game
  that waits for its flip before the next frame therefore runs at 60/n fps: 20 fps (50 ms),
  15 fps (67 ms) or a mix of the two.
- **That is why the two patches don't add up:**
  - env cache alone: the present thread is busy ~50 ms, which rounds to 3 vblanks;
  - both: busy ~38 ms, still 3 vblanks. The rest is idle, which is the `rest` jump to 24-31%.
- **30 fps comes only when the frame's critical path fits in ~33 ms**, and then it should jump,
  not creep.
- The patch adds **`[PERF4]`**, which buckets each batch interval: <20 ms, 20-37, 37-54, 54-70,
  70-87 and >87. That confirms or refutes this in one run.

## Expected effect

This is inferred, not measured. Run with `VFETCH_ASYNC=1 RSX_ENV_CACHE=1`:
- `vtx` on the present thread should drop from ~23 ms to ~5-8 ms per frame (list building, plan
  setup, label syncs: `async fetch wait` was ~5 ms per frame).
- The present thread's busy time should drop from ~38 ms to ~22-25 ms, under the 33 ms line.
- If the guest side keeps up, `[PERF4]` should move into the 20-37 bucket, i.e. 30 fps.

## Next, if still short

- **Indexed native strips with GPU strip-cut.** A 3-index piece costs 6 output vertices as a
  native strip (3 plus 3 join), against 3 as a list and 3 unique vertices + 4 indices with a cut
  index. That would halve, or better, the 840k vertices per frame and the 215 MB per frame upload.
  It changes the replay and PSO setup (`IBStripCutValue`), so it is a bigger patch.
- **A `/Zi` build** to name the remaining drain hot spots in `[samp] rva`.
