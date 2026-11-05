# VP_CODE: vertex layout, vertex bytes and vertex program for draws with a given fragment-program code (logging only)

**Use `vp-code.patch`.** `vpattr-tex.patch` (filter by texture) is superseded: the beam's texture addresses
change between runs, as the AI assistant found.

Patch: `vp-code.patch` (`libs/video/rsx_d3d12_backend.c`, about +100), on 1f52c30. `git apply --check`
passes, and a syntax-only compile with the build's warnings shows no errors. Not built or run. Nothing changes
unless `VP_CODE` is set.

Run: `VP_CODE=9A64BF1C,AD204EA4` (the Kamehameha's distortion-layer and alpha-layer programs; any code= from a
manifest works) during Goku's laser. It hashes the bound fragment program on every draw while set (diagnostic
cost only). The first 12 matching draws are logged (`VP_CODE_N=<n>` to change):

- `[VPCODE] <call> prim first count fp code tex0 divider_op vp_start base_off base_idx`
- for each enabled attribute: `aN off stride size type freq`, then the raw bytes of the draw's first two
  vertices (index-resolved, base offset and index applied, frequency divider applied);
- for each disabled attribute with a non-zero constant, and always for a3 (COLOR0): `aN disabled,
  constant=(...)`;
- per distinct vertex program: `vp_code_<code>_<n>.hlsl` in the working directory, holding the decompiled
  VP plus every non-zero vertex constant at that draw (`[VPCODE] wrote ...`).

Reading (VFX.md): find how the VP writes `o[1]` (COL0).
- From an attribute: check that attribute's type and bytes. Alpha 0 in memory means the game wrote 0, so look
  at the producer. Non-zero alpha means our decode or stepping is wrong.
- From `c[...]` with `a0` (relative addressing): check those constants in the .hlsl header and our a0 handling.
