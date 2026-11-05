# SPU_INTERP_IMG: run one image's SPURS jobs on the interpreter (diagnostic)

Patch: `interp-img.patch` (`runtime/spu/spu_interp.c` +6/-1, `runtime/spu/spurs_job.c` +25/-1), on b8958b6.
`git apply --check` passes. Correction: my syntax check did not actually run (clang is not on the bash PATH
and the grep hid the error), and the patch had two errors the AI assistant fixed when applying it (a7fef41 + follow-up):
`spurs_job.c` needs `#include "spu_interp.h"`, and the newline escape in the new fprintf came out as a raw line break. Nothing changes unless `SPU_INTERP_IMG` is set.

`SPU_INTERP_IMG=<id>`: jobs of that image started through `spu_run_spurs_job` run on the interpreter from LS 0
until they return to the planted link (0x3FF00), and never enter their lifted functions
(`g_spu_interp_only` stops the interpreter's "rejoin the lifted path" check). Logs
`[spurs-job] img=<id> job 0x...: interpreted (SPU_INTERP_IMG)` for the first 4 jobs. The interpreter calls
the same `spu_<op>` helpers and the same channel/MFC code, so the only difference is the lifted control flow
and register plumbing.

Why: the LS dumps (`perf\scratch\laser\lsdump`) show image 17 (the particle job, spujob_857F80) does run its
quad writer: at the PUT, its output pointer r80 = 0x64C0 = 0x6400 + 0xC0, exactly the 2 quads (0x60 bytes
each) it sends. But every byte it wrote is zero, while its input records (0x130-byte particle records at
0xAA30/0xAB60/0xAE30/0xAF60) hold real values (colour 128,128,128,100, scales ~0.5-1.0, offsets). So the
values computed for the vertices are zero. This run tells a lifting fault from bad input.

Run (Kamehameha demo, same as the LS dump run):
`SPU_INTERP_IMG=17 SPU_DMATRACE=17 SPU_DMATRACE_AFTER=220 SPU_DMATRACE_N=600`

Read:
- Does the beam (and the other skill effects) appear? If yes, the lift of image 17 is wrong, and I bisect it
  against the interpreter next.
- `put nonzero=` on the image-17 PUT lines (pc 0x02EC0): non-zero with the interpreter = lifting bug; still
  0 = the job is given zero-producing input (PPU side), and I follow the records' fields next.
- Speed: the interpreter is slower; image 17 handles a few particles per frame, so it should be fine.
