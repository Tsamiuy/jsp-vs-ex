# VP_CODE_LATER / VP_CODE_FROM: re-read a logged draw's vertex bytes later (logging only)

Patch: `vp-code-later.patch` (`libs/video/rsx_d3d12_backend.c`, about +50), on a5420e0 (VP_CODE). `git apply
--check` passes, and a syntax-only compile with the build's warnings shows no errors and no new warnings. Not
built or run. Nothing changes unless `VP_CODE` (and `VP_CODE_LATER`) are set.

What it adds to VP_CODE:
- `VP_CODE_FROM=<frame_count>`: no logging before that frame, so pre-fight draws don't use up `VP_CODE_N`.
- `[VPCODE] #id f=<frame>` on each logged draw, and `ea=0x...` (the resolved guest address) on each vertex-bytes
  line.
- `VP_CODE_LATER=<n>`: for every logged draw, the first vertex (24 bytes from attribute 0's address) is
  re-read n frames later, at the end of `render_frame` (0 = the end of the same frame), and printed as
  `[VPCODE-LATER] #id f=<logged>-><now> ea=... nonzero=k/24 bytes: ...`. Up to 256 pending.

Run (Kamehameha demo): `VP_CODE=9A64BF1C VP_CODE_N=300 VP_CODE_FROM=<first fight frame, e.g. 6800>
VP_CODE_LATER=0`, then the same with `VP_CODE_LATER=2`.

Reading:
- `[VPCODE]` zero, `[VPCODE-LATER]` non-zero at the same ea: the producer writes the buffer after we
  consumed the draw. That is a sync or timing problem; find which wait or fence the game relies on.
- Zero both times, at 0 and 2 frames later: nothing writes that address. The address is wrong (compare
  with VRAMSCAN's changed blocks) or the producer is dead.
- Non-zero data that is clearly not vertex-like (pointer words): the address is reused by another
  allocation, so the fetch address or the IO mapping is wrong.
