# SPU_DMATRACE_AFTER / _N: trace one SPU image's DMA during the effect (logging only)

Patch: `dmatrace-after.patch` (`runtime/spu/spu_dma.h`, about +30), on b9e2b2d. `git apply --check` passes,
and `spu_channels.c` compiles syntax-only with the patched header with no errors and no new kinds of warning.
Not built or run. Nothing changes unless `SPU_DMATRACE` is set.

The job: the zero quads come from **image 17 = `spujob_857F80`** (EBOOT 0x00857F80, 24,784 bytes,
`spu_workloads.c` line 106). Its `spu_func_00002EC0` is a generic "PUT a buffer" routine (MFC_LSA from r123,
EAL r82, size r8, cmd 0x20), which matches `pc=0x02EC0 lsa=0x06400` in the logs. (Image 16, job 0x852200, also
has a function at 0x2EC0, but there it is float math, not DMA.)

Adds to the existing `SPU_DMATRACE=<image id>` (which prints the first 160 transfers of one image, with the
bytes of small GETs):
- `SPU_DMATRACE_AFTER=<s>`: start s seconds after the first traced transfer, so boot doesn't use up the lines.
- `SPU_DMATRACE_N=<n>`: lines to print (default 160).
- `flip=` and `pc=` on each `[DMA]` line; `put nonzero=k/size` for PUTs (the LS data going out) and
  `get nonzero=k/size` for GETs over 64 bytes (the data coming in).

Run (Kamehameha demo):
`SPU_DMATRACE=17 SPU_DMATRACE_AFTER=<beam time minus 5 s, about 220> SPU_DMATRACE_N=3000` plus
`VP_CODE=9A64BF1C VP_CODE_N=20 VP_CODE_FROM=<beam frame>` for the timeline.

Reading, per job run (a GET of its parameter block, then data GETs, then PUTs):
- The data GETs are mostly zero: the inputs (particle or emitter state the PPU wrote) are empty when the job
  runs. Find their EAs and who writes them; likely a PPU-side ordering problem (the job starts before the PPU
  wrote this frame's state).
- The inputs are non-zero but every PUT is `nonzero=0`: the job's computation goes wrong in our lifted code
  (a branch, a float op, or a channel read returning 0). Next would be SPU_STEPTRACE or an LS dump of the
  job's working buffer.
- Some PUTs non-zero and others zero: compare the parameters of the zero ones (emitter type, count).
