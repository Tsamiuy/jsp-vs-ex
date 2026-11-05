# Beam clock: put the beam draws and the SPU job's PUTs on one timeline (logging only)

Patch: `beam-clock.patch` (`libs/video/rsx_d3d12_backend.c` + `runtime/spu/spu_dma.h`, +10), on 66d1e1e.
`git apply --check` passes. A syntax-only compile of the backend and of `spu_channels.c` with the patched
header shows no errors and no new kinds of warning. Not built or run. Only adds fields to existing diagnostic
lines (VP_CODE, SPU_DMA_RANGE).

- `[VPCODE] #id ms=<ps3_ms_now> flip=<flips requested so far>` before each logged draw.
- `[dma-range] t=... ms=<ps3_ms_now> flip=<flips requested so far> ...` on each logged transfer.

Run: the same as dmaafter2 (`SPU_DMA_RANGE=48440000-48A60000 SPU_DMA_RANGE_AFTER=200 SPU_DMA_RANGE_N=3000
SPU_DMA_RANGE_PUT=1 VP_CODE=9A64BF1C VP_CODE_N=300 VP_CODE_FROM=6800`).

Reading: for each beam draw, find the first PUT covering its `ea` and compare `flip`:
- The same flip number (the PUT lands before the guest's next flip): the game relies on the RSX running
  behind the PPU by part of a frame. A drain held one flip behind fixes it (VFX.md fix A).
- flip + 1 or later: the RSX must be held back by a whole frame or more, and fix A needs a lag of 2.
- ms between draw and PUT: how long a hold-back with a timeout must wait.
