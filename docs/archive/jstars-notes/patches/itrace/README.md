# SPU_ITRACE: per-instruction trace of one image on the interpreter (diagnostic)

Patch: `itrace.patch` (`runtime/spu/spu_interp.c`, about +30), on 3d75a51. `git apply --check` passes. Not
compile-checked (no compiler on my side). Nothing changes unless `SPU_ITRACE_IMG` is set.

With `SPU_INTERP_IMG=<id>` (so the job runs on the interpreter), `SPU_ITRACE_IMG=<id>` logs every executed
instruction whose pc is in [`SPU_ITRACE_LO`, `SPU_ITRACE_HI`) (hex), starting at flip `SPU_ITRACE_FLIP`, up to
`SPU_ITRACE_N` lines (default 6000):
`[itrace] <pc> <insn> r<rt> <4 lanes of rt after the instruction>`.

Run (Kamehameha demo, flag forced):
`SPU_FORCE_REC_FLAG=17 SPU_INTERP_IMG=17 SPU_ITRACE_IMG=17 SPU_ITRACE_LO=1CE0 SPU_ITRACE_HI=2860
SPU_ITRACE_FLIP=6612 SPU_ITRACE_N=20000 SPU_DMATRACE=17 SPU_DMATRACE_AFTER=225 SPU_DMATRACE_N=200`

Why: with the flag forced, every quad's 4 corners are identical and alpha is 0. Static reading of the flag
path (LS 0x1CE0 on) is too long to do reliably by hand; the trace gives the real data flow for a few
particles, and I'll name the exact input (record field, emitter param or job param) that zeroes the size.
