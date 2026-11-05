# Upscalers without motion vectors for J-Stars (2026-10-28, read-only, no code)

Short answer: **AMD FSR 1 (EASU upscale + RCAS sharpen)**, run as the last pass before Present, replacing today's
plain bilinear window stretch. It needs nothing from the game, costs well under a millisecond on the RTX laptop GPU
laptop, is MIT-licensed, and lets the maintainer pick between RES_SCALE 1-2 + FSR (cool and cheap) and RES_SCALE 3 + sharpen
only (sharpest). Follows DLSS.md (DLSS needs motion vectors the game doesn't have).

## 1. How the picture reaches the window today (ps3recomp-src a5676e7, `rsx_d3d12_backend.c`)

- The game draws its 3D scene into the main RT `0x0ED10000`, runs its post chain (tone-map, glow), copies the
  result into the HUD composite `0x0C6ABF40`, draws the HUD there, and a final pass writes the display buffer, which
  is our swap-chain backbuffer (FINDINGS §20-23).
- `RES_SCALE=n` makes every colour target and the swap chain n × 1280x720 (n = 1-4).
- The swap chain keeps that size and is **stretched to the window by DXGI/DWM**: `CreateSwapChainForHwnd` with
  FLIP_DISCARD and default scaling, ~line 1300. The comment at ~1181 says so: "the swap chain keeps the render size
  and is stretched to the client area". The window comes from `WINDOW_SIZE` / F11 fullscreen, shrunk to the work
  area. That stretch is bilinear: soft when growing, and it aliases when shrinking (for example RES_SCALE 3 = 4K
  in a 1440p window).
- So the natural place for an upscaler is **the final image, after the HUD**: swap the DXGI stretch for our own
  pass from the RES_SCALE-size image to a window-size swap chain.

Why not before the HUD: the scene, the post chain and the HUD composite are all one RES_SCALE size and are copied
into each other. Upscaling only the scene means two scales in one frame, the same split DLSS would need (DLSS.md
§6 step 2). It's possible later, but not needed for a spatial upscaler. The HUD is flat text and lines, which FSR
handles well.

## 2. The candidates

| | FSR 1 (EASU + RCAS) | NVIDIA NIS | Anime4K | CAS (sharpen only) |
|---|---|---|---|---|
| What it does | edge-adaptive upscale, then contrast-adaptive sharpen | scaler + sharpener (6-tap, edge-aware) | CNN line restore + upscale, tuned for anime video | sharpen at the same size (can do a mild upscale) |
| Fit for J-Stars' look (cel shading, outlines, flat colour, bloom) | Good: keeps outlines crisp, flat areas clean; slight ringing at high sharpness | Similar to FSR 1, a little more halo/ringing at default sharpness | Very crisp line art, but made for 2D video: can thin or warp outlines on moving 3D models, and can harden bloom, effects and HUD text. Best for stills | Good on top of a supersampled image (RES_SCALE 2-3), no upscale |
| GPU cost on RTX laptop (estimate) | ~0.2-0.5 ms to 4K | ~0.2-0.5 ms to 4K | ~1-4 ms (depends on preset; the larger CNNs more) | ~0.1-0.2 ms |
| Licence | MIT (AMD GPUOpen FidelityFX) | MIT (NVIDIA Image Scaling SDK) | MIT (bloc97/Anime4K) | MIT (FidelityFX CAS) |
| Shader form | HLSL headers, ready for D3D12 compute | HLSL, D3D12 sample included | GLSL (mpv) only: needs porting to HLSL ourselves. Don't copy from GPL ports such as Magpie | HLSL header |
| Vendor-locked? | No, any GPU | No, any GPU | No | No |

All four are fine for a repository that may go public: MIT only needs the licence text kept with the shader files.
Re-check each LICENSE file when vendoring.

Estimates, not measurements: the costs are typical published figures for a fullscreen pass at 4K, scaled to this
GPU; the AI assistant can measure with the existing [PERF] timers.

## 3. How each option interacts with RES_SCALE, window and launcher

- The upscaler always maps **render size (RES_SCALE × 720p) → window/fullscreen size**.
  - Window bigger than render (e.g. RES_SCALE 1 in a 4K fullscreen): EASU upscales, RCAS sharpens.
  - Same size (RES_SCALE 3 on a 4K screen): no upscale; RCAS alone = CAS sharpening.
  - Window smaller (RES_SCALE 3 in a 1440p window): a proper downscale pass instead of the DWM bilinear (less
    shimmer), then optional sharpening.
- The swap chain then has to follow the window size (ResizeBuffers on WM_SIZE / F11) instead of staying at the
  render size. That is the one structural change.
- Launcher (`J-Stars Settings.bat` → `play_settings.txt`): add `UPSCALER=off|fsr|nis` and `SHARPNESS=0-100`
  (FSR RCAS 0.0-2.0 stops, default ~0.25). RES_SCALE stays, now meaning "render quality", and the window/fullscreen
  setting means "output size". Suggested presets for the laptop: Quality = RES_SCALE 2 + FSR to the screen;
  Sharpest = RES_SCALE 3 + sharpen; Cool/quiet = RES_SCALE 1 + FSR.
- Frame pacing is unaffected (the game stays at 30); the cost is GPU time per presented frame only.

## 4. Recommendation and plan (for the AI assistant, behind a switch)

**FSR 1**, with NIS as an optional second mode only if the maintainer wants to compare (same plumbing, different shader).
Anime4K is a fun later experiment for stills/menus, not a default: it costs more, needs a port from GLSL, and is
built for 2D video, not moving 3D.

1. Vendor `ffx_a.h` + `ffx_fsr1.h` (MIT, FidelityFX FSR 1.0) into `libs/video/third_party/fsr1/` with the licence.
2. Render the game's display buffer into an offscreen texture at render size instead of the swap-chain buffer
   (the backbuffer becomes window size). Everything else in the frame stays untouched.
3. Two compute dispatches before Present: EASU (render → window size) into a temporary texture, RCAS into the
   backbuffer (UAV or a copy). Constants from `FsrEasuCon` / `FsrRcasCon` once per size change.
4. Swap chain follows the window: ResizeBuffers on WM_SIZE / F11, recreate the temporary texture.
5. Env switches `UPSCALER=fsr|off` (default off at first), `FSR_SHARPNESS`; then the launcher fields.
6. Check: 720p→4K and 1440p→4K screenshots against today's stretch at the usual Hidden Leaf spot, HUD text up
   close, PERF timers for the cost, F11 toggling and window resizing.

Risks: low. The pass is self-contained at the end of the frame. The main work is the backbuffer-size change
(dumps/captures that read the backbuffer must read the render-size texture instead) and resize handling.
Nothing touches the game's own rendering.
