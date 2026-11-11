# Heavy-map slow-down: per-draw cost, texture slots, real-frame FPS (2026-10-23)

Three patches, each on `ps3recomp-src` b8c179a. `git apply --check` passes for each on the main
checkout, and they apply together in any order (checked in the scratch clone). Only
`libs/video/rsx_d3d12_backend.c` changes. **Not built or run.** Every change has an off switch.

| patch | what | switch |
|---|---|---|
| `draw-cost/draw-cost.patch` (+180/-13) | per-draw work on the drawing thread | `FP_INFO_CACHE=0` off |
| `tex-slots/tex-slots.patch` | 2048 texture slots, 1024 used by default | `TEX_SLOTS=512` = old |
| `fps-flips/fps-flips.patch` | title-bar FPS = game flips/s, batches/s beside it | `TITLE_FPS_BATCHES=1` = old |

## Why (CPU_PROFILE §16-20)
- The AI assistant's 40 Hidden Leaf thread dumps (build-pdb at b7be61b, perf\scratch\slowmaps\s07_pdb,
  symbolized: scratch folder symdumps.py): the drawing thread's own code time is in
  - `vp_record_cb` ~40%, plus memcpy 13%;
  - FIFO decode 23%;
  - `dr_expand_key` / `zcmp_desc` ~15%.
- The two `vp_record_cb` hot lines are **reads back from upload-heap (write-combined) memory**:
  - the TONEMAP check read the just-written fragment constants from `ts`;
  - the projection fallback read `c[0]` from the just-copied constant bank.

  Both are uncached reads on every draw.
- Every draw also walked its fragment program up to 7 times: constants, the code hash in the
  prepass and replay, gamma units twice, TXP units twice.

## draw-cost.patch (exact: same results)
1. **Program cache** (`fp_info`, `FP_INFO_CACHE`):
   - per fragment-program address, the code hash, size, gamma units (all masks), TXP units and
     the inline constants' slots;
   - an entry is used only while every instruction block is byte-identical (16-byte memcmp at
     the stored offsets; the walks depend only on those);
   - constants are re-read every draw at the cached slots;
   - a miss runs the original functions.
   Used by `vp_record_cb`, `dr_gamma_key`, `fp_txp_units` and `vp_get_fp_pso`.
2. **No read-backs:** constants go to a cached buffer, are checked there, then copied to `ts` once.
   `c00` is read from `st->vertex_constants` (the copy's source). The TONEMAP check now also
   needs at least 2 constants: it used to compare whatever was left in the slot.
3. `NO_ALPHATEST`, `VP_NOFIXPROJ` and `VP_FIXPROJ` are read once instead of per draw.
4. The gamma and expand texture masks are cached by the unit's 4 registers (pure functions).
5. `[FPINFO] program cache: N hits, M misses` after each `[PERF4]`.

**Offline test** (`draw-cost/test/test_fpinfo.c`: `fp_info` cut verbatim from the patched file,
linked with the real `rsx_fp_decompiler.c`):
- **400,000 calls with programs re-patched, edited, END moved, moved and replaced at the same
  address: 0 mismatches** in hash, size, gamma units (random masks), TXP units, constants (count
  and values).
- **Cost of a draw's answers for a 235-instruction program: 10.0 µs before, 0.78 µs cached.**
  That "before" hashes twice without the existing memo; with it, HEAD is ~3.7 µs (inferred).

**Expected:** about 3 µs per draw plus the uncached read-backs (not measured offline). At Hidden
Leaf's ~1,300-1,600 records a frame that is ~4-6 ms off the drawing thread (*inferred*).

## tex-slots.patch
`VP_TEX_SLOTS` 512 -> 2048 (table size), 1024 used by default (`TEX_SLOTS=<n>` picks up to 2048).
- Kameari created 7,943 textures in a fight (Hidden Leaf 1,153) and hit `no-slot`.
- Each used slot keeps its texture and upload buffer, so memory grows only if the game really
  uses more textures.
- **Watch** GPU memory and the `[PERF3]` `created` / `no-slot` counts. If 1024 isn't enough on
  Kameari, try `TEX_SLOTS=2048`.

## fps-flips.patch
- Title-bar `FPS:` = `cellGcm_flip_request_count()` per second (the game's frames), then
  `(batches x)` = render_frame calls per second, the old number.
- Logs and [PERF] are unchanged.

## Run for the AI assistant (build at b8c179a + the three patches)
Kameari, Hidden Leaf and Alabasta fights plus the demo fight, `GCM_FLIPCOUNT=1 RSX_FRAMELOG=20 PERF=1`,
thread_cpu, with VPULL off (default):
1. patches with defaults;
2. same with `FP_INFO_CACHE=0 TEX_SLOTS=512` (= before).

Compare:
- [flip] rate per 5 s (especially Hidden Leaf's city view, Kameari's fight start);
- [PERF4] >70 ms frames;
- [PERF3] created / no-slot;
- [FPINFO] hits/misses;
- `drain` and `render_frame`;
- screenshots (must match);
- the title bar (FPS should match the [flip] rate).

Optional, the maintainer's wish: the same with `VPULL=1`.
