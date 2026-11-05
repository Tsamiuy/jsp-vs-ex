# Loading-screen tips page (white) -- cause and fix

Thread "AI assistant (hitches)", 2026-10-22. Analysis in FINDINGS §66/§68/§69 and STAGE_TEXTURES §11.

Status: DONE. Fix committed as 3b68f46 (default on). Verified: tips page, Namek water reflection,
boot logos (the maintainer, 19:02). Tips rotate per match within one session, as in RPCS3; not a bug.

## Cause (FINDINGS §69)

The loading screen draws one 1280x720 DXT1 linear texture at 0x0082C900: a snapshot of the tips
page that the game composes with the RSX and encodes on the CPU (run F: the guest bytes decode to
the complete page, `stages\loading\snapshot_decoded.png`). In `rsx_d3d12_backend.c`, the per-draw
texture bind has a "display-sized sampler source -> the rendered frame" heuristic (`SCREEN_AS_TEX`,
default on, added for the water reflection): a texture that is exactly 1280x720 and not one of our
offscreen RTs gets the frame copy `s_screen_copy` bound instead of its guest bytes, before the
upload path runs. The snapshot matches, so the loading screen drew the previous frame (white after
the fade) instead of the page. This is why `TEX_NOCACHE`, `TEX_VERIFY=frame`, `TEX_MIPS=0` and the
`LUM_READBACK` write-back (runs D, G, H) changed nothing.

## loading-tips-fix.patch (default on; `SCREEN_AS_TEX_FORCE=1` restores the old behaviour)

`apply_fix.py <base.c> <out.c>`; two hunks in `libs/video/rsx_d3d12_backend.c`:
- `screen_as_tex_wants()` next to `menu_tex_pitch_on()`: the frame copy stands in for a
  display-sized non-RT texture only when (a) the format is one the RSX can write (not DXT1/3/5:
  the GPU never writes compressed data) and (b) the guest bytes behind the texture are empty
  (256 samples spread over the whole image), i.e. the title samples a surface we rendered but
  never wrote back, which is the water-reflection case the heuristic was made for.
- the bind-site condition calls it.
CPU content at the display size keeps its own bytes: the match loading screen's DXT1 tips
snapshot AND the boot logos (uncompressed 1280x720, which the size-only rule also swallowed; run I
with `SCREEN_AS_TEX=0` showed them for the first time).

Confirmation without code: `SCREEN_AS_TEX=0` shows the page and the boot logos (and loses the
water reflection). After the patch: page, logos AND the reflection (check a water stage, Namek).

## loading-tips-diag.patch (applied 2026-10-22, opt-in switches, keep)

`apply_diag.py`; `MEMPEEK_FRAMES=<from>:<to>`, `VRAMSCAN_FRAMES=<from>:<to>` (frame windows for the
existing MEMPEEK / VRAMSCAN), `RSX_CAP_TEX=1` (capture-time dump of every draw's unit-0 texture
source bytes, `tex_<off>_<w>x<h>_<fmt>.bin`; decode with `tools\texbin.py`). These are what found
the snapshot in guest memory (run F, `cap4\frame_05`).

## Run history (stages\loading)

cap (first), cap2/runA (RSX_CAP_PASSES=2: the quads paint real art), runB (TEX_A1_UNITS, unusable),
runC (no 2D transfers), runD (LUM_READBACK 1280x720: still white), runE/cap3 (MEMPEEK constant dark
= the page's top-left corner), runF/cap4 (snapshot decodes to the page), runG (TEX_NOCACHE,
TEX_VERIFY: white), runH (TEX_MIPS=0: white). `notes-src\` holds the text spliced into the notes.
