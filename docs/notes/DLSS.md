# Can DLSS be added to J-Stars? (2026-10-28, read-only, no code)

Short answer: yes, but not cheaply. DLSS needs motion vectors and camera jitter, and the PS3 game produces neither.
Depth and a clean "scene before post-processing" point we already have. The two things that need no code at all
are NVIDIA's driver features: **Smooth Motion** (driver frame generation, works on DX12 apps like our port) and
our existing `RES_SCALE` (render 2-3x and scale down, which is better image quality than DLSS, at a GPU cost).

## 1. What our D3D12 renderer has today (checked in `rsx_d3d12_backend.c`, FINDINGS §20-23, RES_SCALE section)

| DLSS input | Have it? | Where / why |
|---|---|---|
| Scene colour before post and HUD | **Yes** | The game draws the 3D scene into the main RT `0x0ED10000` (1280x720 guest), then runs its post chain (tone-map fp `0x0FC9FC01`, glow/blur RTs `0x0D65..0x0DE1`), copies into the HUD composite `0x0C6ABF40`, draws the HUD there, and a final pass writes the display buffer (backbuffer). So "end of geometry, start of post" is a real, findable point. |
| Depth | **Yes** | Scene zeta `0x0E960000`, a D3D12 DepthRT we own (`s_zrt`), at RES_SCALE size; standard (not reversed) z, depth clamp on. |
| Motion vectors | **No** | The PS3 game has no velocity buffer (its era didn't use one). Characters are skinned on the SPU (animation job image 1), so the GPU only ever sees final vertex positions. |
| Projection jitter | **No, but easy to add** | We own the vertex-shader epilogue (`vp_record_cb`): we can add a sub-pixel offset to clip x/y for scene draws only (draws into `0x0ED10000`), without touching the game. |
| Render size < output size | **Partly** | `RES_SCALE` scales every target by one factor. DLSS wants the scene at a low size and post+HUD at the output size: two scales in one frame, which the RES_SCALE code doesn't do yet. |
| HUD-less colour (for frame generation) | **Yes** | `0x0C6ABF40` right before the HUD draws, or the main RT after the post chain. |

## 2. What DLSS needs (NVIDIA Streamline / DLSS SDK)

- The Streamline SDK (free download) and its DLLs shipped next to our exe, an NVIDIA application ID (a development
  ID works for testing), and an RTX GPU (the maintainer's RTX laptop qualifies for everything, including frame gen).
- **Super Resolution (DLSS SR / DLAA):** low-res colour, depth, **motion vectors for every pixel including
  moving objects**, jitter offsets in pixels, camera matrices without jitter, ideally exposure. It runs at the
  start of post-processing.
- **Frame Generation (DLSS FG):** depth, motion vectors, HUD-less colour and the UI, at Present.
- Bad or missing motion vectors give ghosting and smearing; missing jitter gives flicker and poor anti-aliasing.

## 3. The hard part: motion vectors

Ways we could make them, best quality last:
1. **Camera-only vectors** from depth plus this frame's and last frame's view-projection matrix (a compute pass).
   Needs us to find the camera matrix among the vertex-program constants (per game, by heuristic). Static
   scenery comes out right; the fighters, effects and anything moving on its own smear or ghost. In a fighting
   game, that's most of what you look at.
2. **Per-vertex vectors**: keep each draw's previous-frame clip positions and output (current − previous) to an
   extra render target. Needs matching the same object across frames when the game re-records its command
   stream every frame, with SPU-skinned vertices in rotating buffers. This is the same "match draws across
   frames" problem FPS_60.md option 2 rejected as weeks of work.

## 4. Options, cheapest first

| Option | Code? | What you get | Risk |
|---|---|---|---|
| **A. NVIDIA Smooth Motion** (NVIDIA App, per program, on our `.exe`) | None | Driver frame generation, 30 → ~60 shown. No game support needed; works on DX11/DX12 apps. | Some latency, HUD/fast-motion artefacts. Best once the port holds a steady 30. |
| **B. RES_SCALE 2-3** (already in the launcher) | None | Supersampling: sharper than DLSS gives. 1440p/4K at ~28-29 f/s measured. | GPU heat/power on a laptop. |
| C. Spatial upscaler (NIS / FSR 1) at present | Small (one pass) | Sharpening upscale from 720p-ish; no motion vectors needed. | Looks only a bit better than plain scaling; HUD gets upscaled too unless placed before the HUD. |
| D. DLSS SR / DLAA with camera-only vectors | Large | Real DLSS on scenery. | Ghosting on fighters and effects (likely visible all the time); new render-scale split; matrix detection. |
| E. DLSS SR + FG with per-vertex vectors | Very large | Proper DLSS. | Weeks, high risk, same draw-matching problem as 60 fps interpolation. |

**Recommendation:** try A (and keep B) first; it is a settings change, not a project. Only consider D if the maintainer
wants DLSS specifically, and treat it as an experiment behind a switch.

## 5. "Can you use one that is not working?"

Read as: *can we use a DLSS-type feature that doesn't need the game to support it* (inferred; the question was
short). Yes: **Smooth Motion** is exactly that, NVIDIA's driver frame generation for games without DLSS, and it
runs on RTX 40/50 GPUs. The NVIDIA App's "DLSS override" does *not* help: it only upgrades games that already
ship DLSS. DLDSR (driver downscaling) needs exclusive-fullscreen resolutions, and our fullscreen is borderless, so
RES_SCALE is our equivalent.

How to try it (the maintainer, no code): NVIDIA App → Graphics → Program settings → add the J-Stars exe from `jstars\` →
turn on Smooth Motion → start the game as usual. If it's greyed out, update the driver/NVIDIA App.

## 6. If we ever build D: rough plan

1. Jitter: add a Halton sub-pixel offset in `vp_record_cb` for draws into the main RT only, behind a switch;
   check nothing else (shadow maps, post passes) gets it.
2. Split scales: scene RT + scene zeta at render size, post/HUD targets at output size.
3. Camera matrix detection from vertex constants, previous-frame copy, camera-motion-vector compute pass.
4. Streamline integration in the backend: init with the app ID, tag colour/depth/vectors, evaluate DLSS at
   "geometry done" (first post draw sampling `0x0ED10000`), write into the output-size main RT.
5. Tests: RPCS3/720p A/B at the usual spots, a fast character turn for ghosting.

Sources: [Streamline SDK](https://developer.nvidia.com/rtx/streamline/get-started),
[DLSS 4 via Streamline](https://developer.nvidia.com/blog/how-to-integrate-nvidia-dlss-4-into-your-game-with-nvidia-streamline),
[DLSS 3 integration tips](https://developer.nvidia.com/blog/how-to-successfully-integrate-dlss-3),
[Smooth Motion (Wccftech)](https://wccftech.com/nvidia-adds-smooth-motion-a-driver-based-frame-generation-ai-model-for-rtx-50-gpus/amp/),
[Smooth Motion on RTX 40 (Notebookcheck)](https://www.notebookcheck.net/Nvidia-s-new-preview-driver-enables-frame-generation-in-unsupported-games-on-RTX-40-series-GPUs.1058027.0.html).
