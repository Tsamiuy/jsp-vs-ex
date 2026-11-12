
## 66. Loading-screen tips page: the game shows a DXT1 snapshot of the page, taken from guest memory the backend never writes (2026-10-22, AI assistant, read-only)

Corrects §61 ("never drawn") and completes STAGE_TEXTURES §11 (details and file names there).

- **The page is composed correctly.** `stages\loading\cap2\frame_00` (frame 6400, RSX_CAP_PASSES=2):
  the tips quads (fp 0x06251137, vs=4, DXT5 linear parts at 0x03xxxxxx) change the UI layer
  0x0C6ABF40 with the real art (paper tile halves at op012/013, comic-panel pieces op014-019), then
  op024 (fp 0xE71D59BE, vertex-colour gradient, no texture) and op025 cover it and the "Battle
  Characters" summary draws over that. At 6450 the page draws again and a white fade ends the
  frame (so `rt_0C6ABF40.bmp` of `cap\frame_13` being pure white is the fade, not a lost draw).
- **The loading screen draws a different thing.** Frames 6500 (`cap2\frame_01`) and 6600
  (`cap\frame_14`): no part textures; one full-screen draw of a 1280x720 DXT1 linear texture at
  0x0082C900 (fmt 0xA6, fp 0x3C263AA6 `out = col0 * tex`), then "Now loading..." (512x78, which
  shows). `pass_op0011_post` is 255 everywhere outside the text: the snapshot texture is white.
- **It is a snapshot.** EBOOT.elf strings: `loading_game_tips_main/mes/title/chara_%03d_%s_m.pak`
  (the parts) and "Screenshot". The game composes the page with the RSX, encodes the result to one
  DXT1 texture and shows that while the stage loads. The encoder's input is the rendered surface
  read from guest memory.
- **Why it is white here.** The D3D12 backend never writes a render target back to guest memory
  (same gap as the luminance target, COLOR_EXPOSURE.md; `LUM_READBACK` is the only write-back and
  is limited to `LUM_READBACK_MAXPX` pixels, one frame late). `cellGcmSys.c`: NV3089 is a CPU copy
  of guest memory (a render-target source copies whatever is in guest memory), NV0039 (m2mf) is
  unimplemented (`[GCM2D-UNH]`). Whichever path the game takes, it encodes a surface we never
  filled. No `[GCM2D-UNH]` and no `[NV3089] SKIPPED` lines in `color\base\stdout.txt`, so the
  path is either a format-supported NV3089 copy (silent without NV3089_DBG) or a CPU/SPU read
  after a fence; SPU jobs are rare in this title (4 job runs in `stages\alpha\s00`).
- **Open: which path and when.** Decides where the write-back goes. Requested: run C
  (`GCM2D_DBG=100000 GCM2D_TRACE=1 NV3089_GATE=<gate file>`), run D (`LUM_READBACK=1
  LUM_READBACK_MAXPX=921600`, a brute-force end-of-frame write-back of every 1280x720 target: if
  the page shows, the timing is fine and the fix is a targeted version). Diagnostic patch
  `patches\loading-tips\loading-tips-diag.patch` (opt-in `MEMPEEK_FRAMES`, `VRAMSCAN_FRAMES`,
  `RSX_CAP_TEX`; `tools\texbin.py` decodes the dumps) needs the user's yes before the AI assistant builds it.
- **Fix shape (draft, not written yet):** write the composed surface back to guest memory at the
  point the game reads it: for an NV3089/NV0039 source that is a live render target, a GPU
  readback of the source rectangle before the CPU copy (generic, helps the next game too); for a
  CPU read after a fence, a write-back of the UI layer when a snapshot is pending (gated on the
  loading-page draw pattern, or on cellGcmFinish). Cost: one 3.7 MB readback per snapshot, not
  per frame.
