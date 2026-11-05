# VPULL copies each vertex range once per record (2026-10-23)

`vpull-once.patch` (+37/-6, `rsx_d3d12_backend.c` only) applies on b8c179a alone or together with
draw-cost, tex-slots and fps-flips (checked in the scratch clone). **Not built or run.** It only acts
with `VPULL=1`, which is opt-in; `VPULL_ONCE=0` restores the per-batch copy.

**Why:** with VPULL every 256-index batch copied its whole vertex span. That was ~47 MB per batch,
~190 bytes per index in the demo (CPU_PROFILE §17). It filled the 64 MB ring, so heavy frames split
into 2 batches (1.59 batches per flip, §20), and each split waited for the GPU mid-frame.

**What:** each open record keeps its current segment (guest vertices lo..hi already copied, and where).
- A batch whose vertices lie inside the segment, or start at most 64 vertices above its end, uses
  that copy and appends only the vertices above it.
- A batch that starts below the segment opens a new segment, which is what every batch did before.
- Indices are `seg_k + (vertex - seg_lo)`. The region stays contiguous because only this record
  writes the ring while it is open.

**Offline test** (`test/test_vpull_once.c`, the patched `upload_strip_vpull` cut verbatim into
`vpull_upload.inc`; 300 records of 2-21 batches, 3-index strip pieces + restart with jumps back,
base index, LOCAL/MAIN, an attribute past the stride):
- For every index, the ring bytes at its region vertex equal the guest bytes of its vertex.
  **2,582,784 attribute reads, 0 wrong** with VPULL_ONCE on and off.
- Copied: **59.6 MB vs 195.9 MB for 0.86 M indices (72.6 vs 238.6 bytes/index)**. The real gain
  depends on how J-Stars orders its strips; the `[VPULL] copied MB` line shows it.

**Run (with VPULL=1):** the demo fight and Hidden Leaf, `GCM_FLIPCOUNT=1 RSX_FRAMELOG=20 PERF=1`,
`VPULL_ONCE=1` (default) vs `VPULL_ONCE=0`. Compare:
- `[VPULL] copied MB` per window (was median 934);
- `[FRAMELOG]` batches per flip (was 1.59; target ~1.0);
- [flip] rate;
- `dropped` must stay 0;
- the screenshots.
