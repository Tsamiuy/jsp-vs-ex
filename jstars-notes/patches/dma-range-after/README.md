# SPU_DMA_RANGE_AFTER / _N / _PUT: log SPU transfers into a range only from a given time (logging only)

Patch: `dma-range-after.patch` (`runtime/spu/spu_dma.h`, about +15), on 5d799a1. `git apply --check` passes,
and a syntax-only compile of `spu_channels.c` with the patched header shows no errors and no new kinds of
warning. Not built or run. Nothing changes unless `SPU_DMA_RANGE` is set.

Adds to `SPU_DMA_RANGE=<lo>-<hi>`:
- `SPU_DMA_RANGE_AFTER=<seconds>`: log only from that many seconds after the run's first SPU DMA (about
  process start), so boot traffic doesn't use up the lines.
- `SPU_DMA_RANGE_N=<n>`: number of lines (default 64).
- `SPU_DMA_RANGE_PUT=1`: PUTs only.
- Every line now starts with `t=<seconds>`.
- The 5 s put/get rate lines are unchanged.

Run (Kamehameha demo; beam at roughly the time `[VPCODE] #1` appeared in `later2`/`dmarange`):
`SPU_DMA_RANGE=48300000-48D00000 SPU_DMA_RANGE_AFTER=<beam time - 5> SPU_DMA_RANGE_N=400 SPU_DMA_RANGE_PUT=1`
together with `VP_CODE=9A64BF1C VP_CODE_N=50 VP_CODE_FROM=<fight frame>`, so the beam's `ea=` values are in the
same log.

Reading (VFX.md):
- PUTs of particle-sized blocks (multiples of 24 bytes, e.g. 192) during the beam:
  - at the `[VPCODE] ea=` addresses: written after we drew (timing);
  - elsewhere: our fetch offset is wrong.
- No PUTs into the window while the beam fires: the producer is PPU code or isn't running. Next would be the
  PPU particle vertex build (`func_00631C24`, COLOR_EXPOSURE.md Round 10).
