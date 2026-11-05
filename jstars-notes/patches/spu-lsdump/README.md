# SPU_DMATRACE_LSDUMP: dump image 17's local store at its all-zero PUTs (logging only)

Patch: `spu-lsdump.patch` (`runtime/spu/spu_dma.h`, +28), on 9c4cb08. `git apply --check` passes, and
`spu_channels.c` compiles syntax-only with the patched header with no errors and no new kinds of warning. Not
built or run. Nothing changes unless `SPU_DMATRACE` and `SPU_DMATRACE_LSDUMP` are set.

`SPU_DMATRACE_LSDUMP=<n>`: on the first n PUTs of the traced image whose data is all zero, write
`spu<img>_ls_<k>.bin` (the whole 256 KB local store) and `spu<img>_regs_<k>.txt` (pc, lsa, ea, size, flip and
all 128 registers) to the working directory, and log `LS dump k written`.

Run (Kamehameha demo):
`SPU_DMATRACE=17 SPU_DMATRACE_AFTER=220 SPU_DMATRACE_N=600 SPU_DMATRACE_LSDUMP=4` (adjust AFTER to about 5 s
before the beam). Keep the four .bin/.txt pairs and the stderr, and hand them over.

What I'll read from the dumps, offline, with the lifted job source (`jstars\port\spu\spujob_857F80\
spu_recomp.c`):
- the 304-byte input records (their LSAs come from the `[DMA]` GET lines) and the job's parameter block;
- the output buffer at 0x6400 and the rest of LS: are the computed vertices sitting somewhere else (a wrong
  pointer), or was nothing computed?
- the registers at the PUT, against the code path into `func_00002860` → `func_00002EC0` (the flush that sends
  the buffer). Which counters and pointers are 0 shows which per-particle check (alive, lifetime, scale,
  count) failed.

Already checked in the code: `spu_frest`/`spu_frsqest`/`spu_fi` follow the hardware tables (RPCS3-matched), and
the job has no unlifted instructions (the 7 `spu_unsupported` are data words in the job header). Its only
channel reads are MFC_RdTagStat and SPU_RdInMbox.
