**Update 2 (2026-10-22, AI assistant thread, read-only): root cause. The loading screen shows a
DXT1 *snapshot* of the page, and the snapshot is taken from guest memory we never write.**
The AI assistant's run A (`stages\loading\cap2`, `RSX_CAP=6400:2:100 RSX_CAP_PASSES=2`) settles the
earlier question: at frame 6400 (`frame_00`) the tips quads paint the real art. Chained pre/post
diffs (pre = previous op's post): op012 changes the left half, op013 the right half (the 320x360
paper tile), op014-op019 change 4-15% each (the 826x360 comic-panel pieces; `pass_op0019_post`
is the grey comic-panel background RPCS3 shows), op020-op023 change nothing, then op024 (fp
0xE71D59BE, `col0 + c0`, no texture: the red/blue gradient) covers 100% and op025 covers 78%,
and the "Battle Characters" summary draws on top. So the draws, the BC3 upload, the UV scale and
the vertex colour are all fine; the page is composed every frame underneath the summary. At 6450
(`cap\frame_13`) the same page draws and a fade then covers the layer in white (`rt_0ED10000.bmp`
is black, so op011's copy is black too: the white comes from the fade at the end of the frame).
The loading screen proper is different: frames 6500 (`cap2\frame_01`) and 6600 (`cap\frame_14`)
draw none of the part textures. The UI layer gets one full-screen draw of a **1280x720 DXT1
linear texture at 0x0082C900** (fmt 0xA6, fp code 0x3C263AA6, `out = col0 * tex`, vs=4) and then
"Now loading..." (512x78). `pass_op0011_post` of frame_01 is 255 everywhere outside the text:
that texture samples pure white. EBOOT.elf names the page parts (`loading_game_tips_main_*`,
`_mes_*`, `_title_*`, `_chara_*` paks) and has a "Screenshot" facility, so the game composes the
page with the RSX, snapshots it into one DXT1 texture (so the part textures can go while the
stage loads) and displays the snapshot. On a PS3 the snapshot reads the rendered surface from
local memory (PPU/SPU, or an RSX 2D transfer into main memory first). In our port the D3D12
backend never writes a render target back to guest memory (COLOR_EXPOSURE.md found the same for
the luminance target; only `LUM_READBACK` does it, for targets up to `LUM_READBACK_MAXPX`
pixels, one frame late), NV3089 in `cellGcmSys.c` is a CPU copy of guest memory and NV0039 is
unimplemented. So the encoder reads a blank surface and the snapshot is white. What is not yet
known is which path the game uses and when (an in-FIFO transfer, or a fence then a CPU read),
which decides where the write-back has to happen. Asked the AI assistant for run C (`GCM2D_DBG=100000
GCM2D_TRACE=1 NV3089_GATE=<file>` with the gate file created at the summary screen) and run D
(`LUM_READBACK=1 LUM_READBACK_MAXPX=921600`: if the page shows, an end-of-frame write-back is
early enough). Diagnostic patch (needs the user's yes): `patches\loading-tips\` adds frame-gated
`MEMPEEK_FRAMES` / `VRAMSCAN_FRAMES` and `RSX_CAP_TEX=1` (capture-time texture source dump,
decoded by `tools\texbin.py`), so the snapshot bytes can be read directly: DXT1 blocks
`FF FF FF FF 00 00 00 00` mean "encoded from a white source", raw FF/00 mean "never encoded".

