# SPU_FORCE_REC_FLAG: force the particle records' "real path" bit (test only)

Patch: `rec-flag.patch` (`runtime/spu/spu_dma.h`, +9), on a32ea92. `git apply --check` passes. NOT compile-checked
(no C compiler on my side); it uses only `getenv`/`atoi`/`ls_ptr`, which the file already uses. Nothing changes
unless `SPU_FORCE_REC_FLAG` is set.

`SPU_FORCE_REC_FLAG=<img>`: after each 0x130-byte GET by that image, set bit 0x02 of byte +0x121 of the record in LS.

Why: image 17 (spujob_857F80) copies each record to its stack (r127+0x250), then at LS 0x1074-0x107C tests
record+0x121 & 0x02:
- set: branch to 0x1CE0, the real path. It reads +0x70 (size), +0x80 (colour, clamped to 255.0), +0x100/+0x110,
  rotation by pi/2 (0x3FC90FDB), and the emitter params.
- clear: fall through to 0x1080, which sets size r90 = r91 = 0, colour r89 = 0 and a default basis
  (0,1,0,1)/(1,0,0,1) (exactly what the LS dumps hold at r127+0x00/+0x30), so every quad is zero.

Every record in the LS dumps has +0x120..+0x12F = 0, so the bit is clear. Also, image 17 has never PUT one
non-zero byte in any traced run (menus included), on lifted or interpreted code. The PPU reads the same bit
(func_00632DC0, func_0062A2FC: `lbz +0x121; rlwinm 0x2`), and no direct byte store to +0x121 exists in the
lifted PPU code, so it is set some other way (a word or vector store, or a copied template).

Run: Kamehameha demo with `SPU_FORCE_REC_FLAG=17 SPU_DMATRACE=17 SPU_DMATRACE_AFTER=220 SPU_DMATRACE_N=300`.
Read: do beams/effects appear (even garbled), and are the pc 0x02EC0 PUTs now `put nonzero` > 0? If yes, the
missing piece is whatever sets +0x121 (and maybe +0x00..+0x3F) on the PPU side.
