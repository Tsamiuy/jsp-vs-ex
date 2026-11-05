# White sky flash (Penguin Village, Kameari): the sun-shaft fan arrives in pieces

Patch: `fan-join.patch` (`libs/video/rsx_d3d12_backend.c`, 7 hunks, +169/-5), made against the
working tree of 2026-10-22 ~17:00 (HEAD 2a32615 plus the AI assistant's uncommitted backend edits);
`patch -p1 --dry-run` passes. Not built or run (this thread does not build). Default ON.
Switches: `FAN_JOIN=0` turns the join off (old behaviour); `FAN_LOG=1` logs every TRIANGLE_FAN batch;
`FAN_LOG=0D7A0000` logs every batch drawn into that colour target (the shaft target).

## What the white layer is (STAGE_TEXTURES.md §13, FINDINGS §65)

The white quarter-size layer from §10 is the game's **"Lens Godray"** sun-shaft post effect
(EBOOT strings "Postprocess GodRays", "Lens Godray", "Lens Godray Element"; shaders
`sp_*_postprocess_god_rays*` and `sp_*_postprocess_lens_godray_*`). Every frame it does, at 320x180:

1. `op712` mask: writes the sun colour (255,222,154) wherever the depth copy says "nothing drawn"
   on the sun side (the whole sky), black on everything else. Target 0x0D7A0000.
2. `op713-715`: three 10-tap radial blurs toward the sun (0x0D890000 -> 0x0D840000 -> 0x0D7F0000).
3. `op716` "element" pass: draws **back into 0x0D7A0000**, blend off, sampling the blurred result
   (t0 = 0x0D7F0000). Its geometry is a **42-vertex triangle fan**: vertex 0 is the sun's screen
   position, vertices 1..41 run around the screen edge (14 along the top, 6 down the right side, 14
   along the bottom, 7 up the left side, closing at the top-left corner). Built by `func_005567AC`
   (ppu_recomp_028.cpp 30324-30647): stride 20 bytes = position xyz + texcoord uv, 0x29 = 41 loop
   iterations, written into a ring whose write pointer is [[G]+8]+0x18. The fan covers the whole
   target, so it replaces the raw mask with the (scaled) blurred shafts everywhere.
4. `op717`: adds 0x0D7A0000 onto the HDR frame (plain add). Raw mask + sky blue = white.

So a white sky = the fan did not cover the sky, and the raw sun-colour mask was added to it.

## Evidence

- Six captured frames (`stages\pv_flash\cap\frame_00..05`, manifest line `rt=0x0D7A0000 ... vs=54`):
  the fan draw is recorded with **n=120** (= 40 triangles, a complete 42-vertex fan) in frame_00 and
  frame_04, and **n=114** (38 triangles) in frames 01, 02, 03, 05. 114 = 3*(42-4): the vertices
  reached the backend as **two batches**, each expanded as a fan of its own (each loses 2 triangles).
- `rt_0D7A0000.bmp` of exactly those four n=114 frames has 211 pixels of raw sun colour at rows 0-10,
  columns 125-173 (the wedge at the split), the n=120 frames have none (max red 34).
- The game emits draws through two functions only. The indexed one, `func_00657248`
  (ppu_recomp_033.cpp 49541), cuts the FIRST index batch at the next 128-byte boundary of the index
  buffer address, then continues in 256-index batches. The non-indexed one, `func_00656F0C`
  (49306), sends the remainder first and then 256-vertex batches. On the RSX every batch inside one
  BEGIN/END belongs to one primitive stream, so the hardware draws one fan. The backend already
  concatenates batches for TRIANGLES (the "tub mesh" fix) and native indexed strips, but a non-indexed
  STRIP/FAN batch and an indexed FAN batch were each expanded on their own: the second batch became a
  fan centred on a screen-edge point. Depending on where the cut falls, the two fans leave anything
  from a small wedge (cut on the top edge: the 211-pixel leak) to most of the upper screen (cut on
  the right, bottom or left edge: white sky) uncovered.
- Burst `stages\pv_flash\burst\16b_burst_00..09.png`: 4 of 10 frames white, irregular: the cut moves
  with the ring position, so the result changes from frame to frame.

## What the patch does

- `sf_continues()`: a STRIP/FAN batch (draw_arrays) or FAN batch (draw_indexed) whose `first` equals
  the previous batch's end, in the same BEGIN/END epoch with the same fragment program, continues the
  previous record's stream.
- `upload_sf_cont_vp()`: expands the continuation against the STREAM: fan triangles use the
  stream's first vertex as centre (`(start, v-1, v)`), strip triangles keep the stream's parity, then
  fetches the list with `vfa_fetch_list` (same async path as strips) or `read_vp_vertex` (legacy).
- `sf_append()`: appends the triangles to the previous record; otherwise a new record is made and the
  stream carries on from it.
- `sf_anchor()` remembers the stream after each FAN / non-indexed STRIP record; any other primitive,
  a dropped batch or a mid-frame flush ends it (the next batch is then drawn as before).
- Indexed strips keep their existing native continuation; QUADS and TRIANGLES are untouched.

## Test plan

1. Build with the patch. Penguin Village fight, `FAN_LOG=0D7A0000`, stderr saved. Expect per frame
   `[FANLOG] f=N arrays|indexed prim=7 first=F count=C ... new emitted=.. n=..` for the fan, and when
   the game splits it a second line `... JOINED emitted=.. n=120`. The `n=` of the last line of a
   frame should be 120 on every frame. This also settles whether the fan comes through draw_arrays or
   draw_indexed.
2. `tour.py`-style 10-frame burst as in §9: no white frames expected, and the small bright wedge at
   the top of the sky should be gone as well.
3. If frames still flash: compare `FAN_JOIN=0` vs default on the same spot, and look at the FANLOG
   lines of a white frame: if the fan arrives in ONE batch with count=42 on a white frame, the cause
   is not the split and the next step is `RSX_CAP_PASSES=2` (dumps the n=120 fan's fragment program
   too) plus `RSX_DBG_VP=0D7F0000` (writes the fan's vertex program to duck_vp.hlsl).
4. Kameari: the pale sky should turn into normal shafts.

## Not needed any more

`patches\zpass-report`: the game never issues a ZPASS report (the only cellGcmSetReport caller,
`func_00576808`, asks for zcull statistics types 2 and 3), so the occlusion-query idea is ruled out.

## v2 (2026-10-22 evening): `fan-join-v2.patch`, applies on top of v1 (commit 6afba53)

The AI assistant's run (stages\pv_flash\fanjoin\stderr.txt): the fan is `indexed prim=7`, 42 indices, split
as 8+34, 16+26, 24+18, 32+10, 40+2 or not at all; the pieces are contiguous and share epoch, target
and program, yet all 612 lines say `new`, 0 `JOINED`, and the wedge still flashes. The v1 join
condition that must have refused is the blanket `if (indexed && st->restart_enable) return 0;`: J-Stars
keeps primitive restart enabled for its strips, so every indexed fan was refused. v2:

- restart handling instead of refusal: join unless an index the joined triangles would use (the
  stream's first index, the previous batch's last index, the new batch) equals the restart index
  (same u16 width rule as `upload_strip_vp_native`).
- `[FANLOG]` lines now carry `why=N` for a refused join (1 FAN_JOIN=0, 2 no stream, 3 other primitive,
  4 not contiguous, 5 another record in between / mid-frame flush, 6 other BEGIN/END, 7 other fragment
  program, 8 previous record not a VP triangle list, 9 restart index in range) and the state's
  `restart=<enable>/<index> dma=<index_array_dma>`, so a refusal is diagnosable from the log.
- No other change; `patch -p1 --dry-run` passes on the current tree.

Test: same as steps 1-2 above. Expect the second piece of each split frame as `JOINED ... n=120`.
If a line still says `new why=N`, N names the failing check.
