# Fights: where the time goes, and indexed strips (2026-10-21)

`strip-indexed.patch` applies to `ps3recomp-src` at `e799d57`. On 2026-10-21 `git apply --check`
passed against the main checkout. It changes only `rsx_d3d12_backend.c`, which compiles with no
new warnings. **Not built or run.** **Opt-in:** `STRIP_INDEXED=1`.

## Where fight time goes (stage survey `stages\s0N\stderr.txt`, `[PERF]`/`[PERF2]`/`[PERF4]`)

Per frame, from typical fight windows:

| | s04 jungle (24.5 fps) | s06 Namek (21.9 fps) |
|---|---|---|
| vertex work on the present thread (`vtx`) | 12.5 ms (15.9M verts per 20 frames, ~37 cyc/vert) | 12.5 ms (17.4M, ~34 cyc/vert) |
| rest of the drain (FIFO decode, recording) | ~11.5 ms | ~12 ms |
| render_frame (prepass ~5 ms, includes GPU wait) | ~8 ms | ~8.5 ms |
| present thread idle (`rest`) | ~8.5 ms | ~13 ms |
| frame intervals (`[PERF4]`) | 9× 20-37 ms, 10× 37-54 ms | 5× 20-37 ms, 12× 37-54 ms |

- **Steady state:** in fights the present thread is busy **~32 ms per frame**, against 22-26 ms in
  the intro cards. That is at the 33 ms line, so frames split between 2 and 3 vblanks
  (19-27 fps). Vertex work is the largest single share: **~870k strip output vertices per
  frame**.
- **The dips to 13-17 fps** are hitches in specific windows, not the steady state (measured):
  - **Shader compiles:** `pso` time jumps to 0.3-0.6 s per 20-frame window with 3-12 PSO
    `MISS`es. That is ~50-125 ms per compile, on the present thread (s06 windows at 12.6, 16.4
    and 18.0 fps).
  - **New textures:** `tex` 0.25-0.39 s per window, with `[PERF3]` `new 270 / created 202`
    (s01 at 19.6 fps; s06 at 15.1 and 17.1 fps). Textures are created and uploaded as the camera
    reveals the stage.

## The patch: indexed strips with D3D strip cuts

**The problem:**
- Native strips write one 256-byte vertex per strip index, plus 2-3 repeated vertices at every
  restart join. The repeats are degenerate triangles that keep the winding.
- J-Stars' strips are mostly 3-index pieces plus a restart, so that's **~5-6 vertices per
  triangle**.
- Every strip vertex is decoded, uploaded and vertex-shaded again each time it appears, even when
  neighbouring pieces share it.

**With `STRIP_INDEXED=1`:**
- Each **distinct** guest vertex of a record is fetched **once**. A per-record dedup map,
  guest index → record-relative vertex, persists across the record's 256-index FIFO batches.
- The strip goes into a new **index buffer** (u32, 2 × 8 MB parity halves like `vp_vb`) with
  **0xFFFFFFFF at every restart**, drawn with `DrawIndexedInstanced` (base vertex = record
  start).
- The VP and guest-FP PSOs get `IBStripCutValue = 0xFFFFFFFF`, which non-indexed draws ignore.
- A cut resets the winding exactly as an RSX restart does, so the triangles drawn are the same,
  minus the degenerate joins.

**Safety:**
- Indexed records are marked by a batch generation, so a reused record slot can never be taken
  for one.
- The mid-frame flush also triggers when the index buffer is short.
- Off: the old native path, unchanged.

## Evidence (offline, `test/test_strip_ix.c`)

- **The test:** the padded native strip and the indexed strip are each built with the patch's
  own loop logic, then assembled with D3D strip rules (odd triangles swapped, cut = new strip).
  The streams are 50 random J-Stars-like ones: 3-index pieces, longer strips, broken pieces,
  double restarts, a shared vertex pool, and 256-index calls continuing one record.
- **Result:** **the non-degenerate triangles are identical in order and winding in 50 of 50
  trials.**
- **Vertex counts:** the padded strip writes ~1.35 vertices per index. The indexed strip writes
  at most 1 per index, and as few as its distinct vertices; in the synthetic test, where reuse is
  heavy, that is ~20× fewer. The real reduction depends on how much the game's meshes reuse
  vertices. It's at least ~45% even with no reuse (3 vs ~5.4 per triangle).

## Expected effect (inferred, not measured)

- **Fewer vertices** fetched by the workers and uploaded: today ~223 MB per frame at 256 bytes
  per vertex.
- **Fewer vertices shaded on the GPU,** which shortens render_frame's GPU wait.
- **Somewhat less list-building work on the present thread:** it still visits every index, now
  with a hash probe, but writes far fewer vertices.

My estimate is 3-8 ms per fight frame, enough to pull more frames under 33 ms. It won't fix the
compile and texture hitches.

**Suggested run:** a fight on s04 and s06 with `PERF=1`, with and without `STRIP_INDEXED=1`.
Compare `vtx`, `render_frame` and `[PERF4]`, and check the screenshots for missing or extra
triangles.

## Next, for the dips (not drafted)

1. **Shader compile hitches:** compile PSOs off the present thread, or keep a disk cache of
   compiled FP/VS blobs so a stage's shaders compile once per install, not once per run.
2. **Texture creation hitches:** pre-create and upload textures in a worker, or keep a pool of
   reusable resources by size and format; 202 creates in one window suggests a pool would help.
