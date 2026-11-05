# SPU_SDCRZ_RANGE: log SPU zero-fills (SDCRZ) into a range (logging only)

Patch: `sdcrz-log.patch` (`runtime/spu/spu_dma.h`, +25), on 7cbe7e7. `git apply --check` passes, and
`spu_channels.c` compiles syntax-only with the patched header with no errors and no new kinds of warning. Not
built or run. Nothing changes unless `SPU_SDCRZ_RANGE` is set.

Why: SDCRZ (MFC 0x89, zero a range in main memory) is handled at the top of `mfc_do_transfer` and returns
before the `SPU_DMA_RANGE` logger, so an SPU job that zeroes a buffer leaves no trace there. The beam's
vertices are PUT (2 flips before the draw) and are zero at draw time. Something zeroes them in between.

Switches: `SPU_SDCRZ_RANGE=<lo>-<hi>`, `SPU_SDCRZ_AFTER=<s>`, `SPU_SDCRZ_N=<n>` (default 400). Lines:
`[sdcrz] t=... ms=... flip=... pc=... ea=... size=...` (the same clock and flip fields as `[dma-range]`).

Run: the dmaclock setup plus `SPU_SDCRZ_RANGE=48440000-48A60000 SPU_SDCRZ_AFTER=200`.

Reading:
- An `[sdcrz]` covering a beam EA between its last PUT (flip F-2) and the draw (flip F): an SPU job zeroes
  it. Its pc says which job, and its flip says when it thinks the buffer is free.
- No SDCRZ there: the zeroing comes from the PPU (memset or a frame allocator), which needs a PPU-side check.
