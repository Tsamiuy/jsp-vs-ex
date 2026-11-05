# Shadow passes: record depth-only passes and give them to the materials

Patch: `shadow-passes.patch` (only `libs/video/rsx_d3d12_backend.c`, +412/-19). Written against
`jstars-port` at 8ee4e58. `git apply --check` passes on e6feaa6 (the main checkout on
2026-10-20 ~15:30). Not built into the game and not run: applying, building and testing it is for
The AI assistant.

**Before:** J-Stars draws its two shadow cascades as depth-only passes (colour target NONE, zeta
only):
- 2048x1024 at zeta `0x0CE40000`;
- 1024x1024 at zeta `0x0CA30000`.

`rsx_no_color_target()` dropped those passes, so the material shaders' PCF taps on t7/t8 read
guest memory that nothing writes (FINDINGS §27, `TEX_MARKEMPTY` confirmed both maps are empty).
That gives the sharp-edged "camo" blotches.

**After:** each depth-only pass is drawn into its own D3D depth target, keyed by the zeta offset. A
material that binds that offset as a texture samples the real depth.

## Switches

| Env | Effect |
|---|---|
| `SHADOW_PASSES=0` | Off: exactly the old behaviour (passes dropped, depth-format binds ignored). Default on. |
| `ZPASS_DBG=1` | Logs depth targets as they are created (`[ZPASS] depth target ...`). Also logs each unit that samples one: format, CONTROL1 remap, ADDRESS zfunc, CONTROL0 and the mode chosen. |
| `ZPASS_ARGB_CMP=1` | Treats an A8R8G8B8 view that has an ADDRESS zfunc as a depth comparison instead of a byte bitcast (see "Open question"). |

## Design

1. **Recording.** In SHADOW_PASSES mode, a state with `color_target == 0` and a zeta offset is a
   target:
   - `current_rt_off()` returns `ZT_TAG (0x80000000) | zeta_offset` with the zeta clip size.
   - Every "`rt_off == 0` = display" test keeps working, because the value is non-zero.
   - The tag keeps it from ever naming a colour RT. `dr_is_zonly()` tests it.
   - Z/stencil clears of that surface are recorded as ordered clear ops (`cc[0]` depth, `cc[1]`
     stencil, `cc[2]` flags).
2. **Depth targets** (`DepthRT`, up to 8, own DSV heap):
   - `R24G8_TYPELESS`, so one resource is both the pass's DSV (`D24_UNORM_S8_UINT`) and the
     material's SRV (`R24_UNORM_X8_TYPELESS`, `.r` = depth).
   - They are created in the pre-pass like OffRTs, persist across batches and frames, and are
     cleared only by the guest's clears (plus once at creation).
   - Colour passes keep the shared depth buffer, unchanged.
3. **Replay.** A depth-only op does three things:
   - binds its DepthRT with no RTV, with viewport and scissor set to the target size;
   - draws with the guest FP built with `nrt = 0` (`dr_num_rts()` = 0, a separate PSO key);
   - sets `cur_rt = -2`, so the next colour op rebinds its own targets.

   A sampled DepthRT goes to PIXEL_SHADER_RESOURCE first. Depth-only ops are excluded from the
   sub-viewport capture heuristic, because the 1024-wide cascade would otherwise look like a
   reflection pass.
4. **Hand-off to materials.**
   - `d3d12_bind_texture` now also records DEPTH24_D8/DEPTH16 binds (0x90-0x93) and CONTROL0.
   - In the per-draw SRV window, a unit whose raw offset is a DepthRT gets the R24 SRV.
   - `dr_z_info()` then tells the FP how to read it. Its hash is added to the PSO key and the
     failure cache.
   - **DEPTH24_D8/DEPTH16 with an ADDRESS zfunc:** the hardware comparison is emitted through the
     shared decompiler's existing path, `rsx_fp_decompile_buffered_shadow_color_ex` (as
     `rsx_draw_engine` already does).
   - **A8R8G8B8 (bitcast), which is what J-Stars' manifests show (`0xA5`, remap `0xAA93`):** RSX
     stores Z24S8 big-endian as d[23:16], d[15:8], d[7:0], stencil, i.e. A, R, G, B of the texel.
     The unit's samples are rewritten to `_rsx_zargbN(uv)`, which:
     - rebuilds those bytes from the depth (nearest texel, clamped);
     - applies the CONTROL1 crossbar exactly as an uploaded A8R8G8B8 image gets it.

     With `0xAA93` the shader then sees R,G,B = depth high/mid/low byte and A = stencil (0; stencil
     is not served).

## What was checked (no game build, no run)

- **Compile:** syntax-only compile (`clang-cl /Zs`, the build's flags) of the patched file. No
  errors, and no new warning kinds (only more of the file's usual unsafe-buffer and
  declaration-after-statement ones).
- **Shader rewrite:** the rewrite function was compiled standalone and run on a real J-Stars
  material FP (`jstars\run\duck_fp.hlsl`, fp `0x0FC44801`, PCF taps on t7/t8) with remap `0xAA93`.
  The output compiles with `fxc /T ps_5_0`, and the helper returns `(d_hi, d_mid, d_lo, stencil)`
  as intended.
- **Not tested at all:** anything at run time (D3D state, transitions, the PSO with 0 RTs, the look).

## Test plan for the AI assistant

1. Apply, build, run the demo with `ZPASS_DBG=1`. Expect `[ZPASS] depth target` lines for
   `0x0CE40000 2048x1024` and `0x0CA30000 1024x1024`, and `unit 7/8 samples zeta ...` lines.
2. Compare screenshots with `SHADOW_PASSES=0` (same build). The blotches should turn into shadow
   shapes that follow the characters and props.
3. If surfaces come out fully shadowed or black instead, check the `[ZPASS]` lines:
   - `fmt=0xA5` with `zfunc` non-zero: try `ZPASS_ARGB_CMP=1`;
   - a `depth (compare if zfunc)` mode: the comparison path is in use.
4. Check `[PERF]`: the casters add roughly 390k vertices per frame (backend comment above `rsx_no_color_target`), and
   `vtx` is already about 77% of the frame, so expect a slower demo.

## Open question

The material FP reads only `.x` of each tap and averages three taps. That looks more like
comparison results than raw depth bytes. Two readings fit:
- The game really binds the cascades as A8R8G8B8 with remap `0xAA93`, as the manifests and
  `TEX_MARKEMPTY` show. That is a bitcast on RSX (RPCS3 handles it the same way), so `.x` is the
  high depth byte.
- The material binds them as DEPTH24_D8 and the manifests showed a stale earlier bind: the old
  `d3d12_bind_texture` ignored depth formats, so a unit kept its previous texture.

The patch handles both. The first `ZPASS_DBG` run says which one happens; `ZPASS_ARGB_CMP=1` is
the fallback if neither looks right.

## Known gaps and risks

- **Polygon offset** (SET_POLYGON_OFFSET_*) is ignored and the depth function is fixed at
  LESS_EQUAL, as for every pass. Some shadow acne is possible.
- **Static samplers:** units 7/8 are LINEAR+WRAP. The comparison path filters by its own taps; the
  bitcast path uses Load (nearest, clamped).
- **The garbage-projection fallback in `vp_record_cb`** replaces c0-c3 when `c[0]` is not in
  (0, 8). A light projection with a negative or large `c[0]` would trip it (`[VPFB]` lines).
  `VP_NOFIXPROJ=1` disables it.
- **Limits:** at most 8 depth targets; DEPTH24_D8_FLOAT and Z16 bitcast are not modelled.
- **Follow-up (not in this patch):** the post chain also binds `0x0E960000` 1280x720 (t15),
  probably the scene's zeta. Today it gets the screen copy. The same DepthRT mechanism could serve
  it once colour passes record their zeta per target instead of the one shared depth buffer.
