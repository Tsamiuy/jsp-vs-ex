# Higher output resolution (up to 4K): plan (2026-10-27, the AI assistant). DONE: see FINDINGS "Higher resolution: RES_SCALE" (0114cf0)

The maintainer: before any texture upscaling, the game should render above 720p, at least up to 4K (his monitor is 4K).
Machine: a laptop discrete GPU plus an integrated GPU, laptop panel 2560x1600 @ 240 Hz.

## How it rendered before RES_SCALE (guest sizes, i.e. RES_SCALE=1)

Now (RES_SCALE is done): every size below is multiplied by RES_SCALE n (colour targets, scene depth, shadow maps, swap chain).

- The guest display size is 1280x720 (`jstars\port\main.cpp` WINDOW_WIDTH/HEIGHT, passed to `rsx_backend_init`); the swap chain
  is created at that size times RES_SCALE and stretched to the window, which is resizable and can go borderless fullscreen
  (WINDOW_SIZE, FULLSCREEN, F11). There is still no WM_SIZE handler or ResizeBuffers.
- The game draws natively at 720p. Display draws (guest surface offset 0) go straight into the swap-chain
  back buffer; every other guest colour surface is an offscreen D3D target created at the guest's size
  (`off_rt`, created in the surface path ~line 6299), depth surfaces likewise (`s_zrt`).
- Targets in a match frame (captures rsxcap_rt48 / pass3 / demo):
  - full size 1280x720: scene HDR target 0x0ED10000, 0x0E590000, 0x0DE10000, 0x0C6ABF40, 0x039FFA40, plus the
    1280x720 depth;
  - reduced: 640x360 (3 effect layers, and a 640x360 depth for an effect pass), 320x180 x2, 80x45 (bloom /
    blur chain), the 20x11 luminance target (read back to the CPU for exposure, LUM_READBACK);
  - shadow maps: 2048x1024 (scenery) and 1024x1024 (characters); 256x256 targets (character icons/portraits).
- 42 places in the backend use the display size (`s_d3d.width/height`, the guest size; since RES_SCALE the swap chain is
  scaled separately): viewport/scissor defaults, captures, the rename panel, readbacks.

## What an internal-resolution scale needs

Option A (recommended): one scale factor S (1x = 720p, 1.5x = 1080p, 2x = 1440p, 3x = 4K, or "match the
window"). Every target whose guest size is the screen size or a fraction of it is created S times larger:
1280x720 -> 3840x2160 at 3x, 640x360 -> 1920x1080, and so on. The guest still believes it draws 720p.

1. Swap chain: create at the chosen output size, handle WM_SIZE (ResizeBuffers) and allow borderless
   fullscreen (Alt+Enter or a launcher option). Present the scaled image 1:1.
2. Targets: allocate colour and depth surfaces at S x guest size; keep a per-target scale so draws into
   them use viewport and scissor x S. Shadow maps can take their own factor (sharper shadows, e.g. 2x).
3. Viewport/scissor: multiply guest viewport rect, scissor rect and the viewport transform by S for scaled
   targets (the vertex epilogue already maps clip space through the D3D viewport, so geometry follows).
4. Copies and readbacks that the game reads as guest memory must stay at guest size: the 20x11 luminance
   readback, the loading-screen "screen as texture" snapshot (SCREEN_AS_TEX), render-target write-back
   paths. These need a downscale (or resolve) to guest size before the copy.
5. Fragment programs that read the pixel position (WPOS -> SV_Position) must divide by S; texel-offset
   constants (blur kernels written as 1/1280) then sample S times closer: blur/bloom gets narrower but
   still looks right; can be corrected later per shader if needed.
6. Point size and line width x S (few uses).
7. Overlays drawn on the back buffer (rename panel) scale with the output; already done for the panel.

Option B (cheap, lower quality): keep 720p internally and only scale the final image to the window with a
good filter (or FSR 1). Sharpening, not real detail. Useful as a fallback while A is built.

## What can break

- Screen-space effects that assume 1280x720 in constants (blur widths, outlines): look different, not broken.
- Anything the CPU reads back (exposure, screen snapshots) if the downscale step is missed: wrong exposure,
  black loading screens. These are known places (point 4).
- Render-to-texture targets that happen to be 1280x720 but are used as textures with unnormalized
  coordinates (none seen so far; RSX textures use normalized coordinates unless the rect flag is set,
  which the decompiler handles per texture).
- Menus/UI: drawn by the game into the same 720p surfaces, so they scale with everything else (sharper
  geometry, same texture detail until upscaling).

## VRAM at 4K (3x)

Full-size targets at 3840x2160: RGBA16F 66 MB each, RGBA8 33 MB, depth 33 MB. About 6 full-size targets +
depth + the scaled 640x360 / 320x180 chain is roughly 0.4 to 0.5 GB extra. The laptop GPU has far more
than that; no concern.

## FPS risk

- At 720p the frame rate is limited by the CPU side (drawing thread, FIFO, vertex work, ~29-30 f/s locked to
  the game's 30), not the GPU. Measured afterwards with RES_SCALE (FINDINGS "Higher resolution"): the first runs were GPU-bound
  (1440p 14-16 f/s, 4K 5-9 f/s); later 1440p about 29.0 and 4K about 28.4 f/s. Scaling multiplies only pixel work: 2.25x at 1080p, 4x at 1440p, 9x at 4K.
  On a laptop GPU the game's pixel shaders at 4K should still fit well inside 33 ms, so the expected
  cost is small; measure per step (1080p, 1440p, 4K) with GCM_FLIPCOUNT and GPU timestamps.
- Risk: the backend's per-frame copies of targets (copy-on-sample snapshots, dt_snapshot) grow 9x at 4K;
  those copies are the first thing to watch. Measured later: about 790 target copies per frame at RES_SCALE=3, cut to about 4
  by not copying depth bound on units the program does not sample (`ZSNAP_ALL=1` turns that off; STATUS 2026-11-02).

## Resolution choice for the player

Yes. The launcher (Play J-Stars.bat -> jstars\play.ps1) can offer a choice (720p / 1080p / 1440p / 4K /
match window, windowed or borderless fullscreen) and pass it as environment switches (e.g.
`RES_SCALE=3`, `WINDOW_SIZE=3840x2160`, `FULLSCREEN=1`), remembered in a small settings file. A first
version can be a simple menu in the console window before the game starts.

## Suggested order

1. Swap chain at window size + WM_SIZE + borderless fullscreen, 720p stretched with a proper filter
   (Option B; quick, makes 4K windows usable).
2. Internal scale S for colour/depth targets, viewport/scissor, downscaled readbacks (Option A), behind a
   switch; test 2x then 3x on Hidden Leaf, Kameari, menus, loading screens.
3. Launcher resolution menu.
4. Then texture upscaling.
