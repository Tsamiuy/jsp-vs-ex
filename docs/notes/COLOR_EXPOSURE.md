# Neon grass, saturated sky, pale textures: colour-path study (2026-10-21)

Read-only study (AI assistant thread). Nothing was built or run; the game was not started. Sources:
`ps3recomp-src` at e799d57 (`jstars-port`), captures in `jstars\run\rsxcap_*` (`flush1`, `skin`,
`pass2`, `exp32`), the stage survey shots in `stages\`, the RPCS3 references in `reference\rpcs3`,
upstream ps3recomp v0.17.0 (commit 122690f), and the installed game data in `jstars\hdd0`.

**Symptom:** grass "slightly neon", saturated sky, and a harder, more contrasty picture than
RPCS3 on the same frames (our `run\shots_facedef_demo\shot_200s.png` vs RPCS3 `Screenshot
2026-10-20 174958.png`: RPCS3's Ace-card sky is a pale green haze with soft white clouds; ours is
saturated yellow-green with cyan cloud edges and a dark, noisy island). The "pale cream" skin was
texture gamma and is fixed (8ee4e58, FINDINGS §27).

## Causes, ranked by evidence

### 1. Auto-exposure is stuck at its maximum: the luminance target is never read back (measured)

The post chain in `rsxcap_flush1\frame_00` (ops 320-325) is:

| op | fp | target | what it does (from the decompiled HLSL) |
|---|---|---|---|
| 320 | `0x0FC9F901` | `0x0DE10000` 1280x720 | HDR encode: scene (t1) + glare buffer (t0, cleared to black), packed as colour / alpha, `/4`, max 63.75 |
| 321-322 | `0x0FC9FA01/B81` | 320x180 | 4-tap downsample with the luminance weights `0.2125/0.7154/0.0721` |
| 323 | `0x0C6ABE81` | `0x0D6A0000` 80x45 | 4-tap average |
| 324 | `0x0C6ABE81` | **`0x039FFA40` 20x11** | 4-tap average: the frame's **average-luminance** target |
| 325 | `0x0FC9FC01` | `0x0ED10000` | tone-map, reads only the encoded scene and 13 **constants** |

The tone-map's maths, lane by lane (`fp_op0325.hlsl`): `L = dot(rgb, c0)`,
`Lm = L*c1.w / (L*c1.w + 1)` (Reinhard), `out = 1 - exp(-4 * rgb * Lm)` (`c3.x = -4`), then the
per-channel `pow(x, 0.4545)` encode. So **`c1.w` is the exposure**, and it comes from the PPU
(the game patches it into the program with inline NV308A writes, see the `[GCM2D] inline write`
lines in every log). A 20x11 target that only the CPU can use, feeding a constant the next frame,
is the standard auto-exposure loop: the PPU averages the 20x11 pixels and sets
`exposure = key / max(avg, eps)`.

Evidence that the loop is broken in the port:

- `c1 = (63.75, 0.0001, 0.5, 7200)` in **every** capture of the tone-map: `rsxcap_flush1`
  frames 00 and 02 (Ace card) and `rsxcap_skin` frames 02-05 (Goku card): six frames, two scenes,
  one value. A working loop varies per scene. **7200 = 0.72 / 0.0001**: the average luminance the
  game measured is at or below its `eps` of 1e-4, i.e. it read black.
- The D3D12 backend never writes a render target back to guest memory (`grep readback`
  in `rsx_d3d12_backend.c` finds only the screenshot/capture paths). The 20x11 target exists on
  the GPU only; the bytes at offset `0x039FFA40` in guest memory stay whatever they were.
- The game does not copy it with NV0039 either: that engine is unimplemented and logs
  `[GCM2D-UNH]`, and no run log (`stages\s06`, `run\stdout.txt`) has that line. So the PPU reads
  the surface straight from local memory (inferred), which is exactly where nothing is written.

What a stuck exposure does to the picture (same tone curve, `Lm` saturates to 1 for any
`L > 0.005` when `E = 7200`; with a sane `E ~ 1`, `Lm = L/(L+1)`):

| scene colour (linear) | ours, E = 7200 | expected, E ~ 1 |
|---|---|---|
| grass (0.10, 0.50, 0.05) | (0.33, 0.86, 0.18) | (0.12, 0.48, 0.06) |
| sky (0.80, 0.90, 0.60), L = 0.86 | (0.96, 0.97, 0.91) clipped | (0.78, 0.82, 0.68) |
| skin (0.60, 0.35, 0.20) | (0.91, 0.75, 0.55) | (0.60, 0.42, 0.26) |

That is "neon" grass (bright, saturated green), a sky that clips to white/cyan at the cloud edges,
skin and rocks brighter and harder than RPCS3: the whole-picture difference in the Ace comparison.
Every material is affected, which is why texture-side fixes never quite closed the gap.

- **Fix:** `patches\color-exposure\lum-readback.patch` (`LUM_READBACK=1`, off by default). At the
  end of each frame's command list, every offscreen colour target of at most
  `LUM_READBACK_MAXPX` pixels (default 4096; the 20x11 and 80x45 targets qualify, nothing bigger)
  is copied into a readback buffer; after the next GPU drain the pixels are written into guest
  memory at the target's offset as big-endian A8R8G8B8 rows, pitch from `SET_SURFACE_PITCH_A`
  (now recorded per draw). One frame late, which an exposure loop tolerates. `[LUMRB]` logs it.
- **A/B without the readback:** `tonemap-exposure.patch` (`TONEMAP_EXPOSURE=<v>`): forces `c1.w`
  of the tone-map (recognised by its constant signature) and logs the live value as `[TONEMAP]`.
  If `TONEMAP_EXPOSURE=1` makes the Ace card look like RPCS3, the cause is confirmed before the
  readback is even tried; the readback then makes it automatic.
- **Risks:** the readback assumes a linear (untiled) surface, R8G8B8A8 targets only, and that the
  PPU reads local memory directly. If the game instead blits it with NV3089
  (`cellGcmSetTransferImage`), the D3D12 backend still does not serve that source
  (`cellGcmSys.c` ~line 889); `[LUMRB]` lines with `[TONEMAP]` still at 7200 would say so.

### 2. Signed texture fetches: `_bx2` (EXP_TEX) and the biased unsigned remap (unknown use)

Upstream 122690f: a `TEX` with OPDEST bit 21 (`_bx2`) returns `x*2-1`, and a unit whose
`SET_TEXTURE_ADDRESS` has bit 12 (`UNSIGNED_REMAP_BIASED`) returns `(round(x*255)-128)/127`. Our
decompiler ignores both (it only uses bit 21 to skip gamma), so such normal maps would read as
0..1 and lean to (1,1,1): too-bright lighting. Whether J-Stars uses either is **not known**:

- The one material we have decompiled (`run\duck_fp.hlsl`) expands its DXT5 normal maps by hand
  (`MAD * 2 - 1` on `.yw`), so at least that shader does not rely on `_bx2`.
- The game's shaders are not stored raw on disk: no `CgBinaryProgram` headers (profile 7004) in
  the ELF or any CPK, and the first 32 bytes of the captured program `run\fp_fail.bin` are not in
  any of them in any of three byte orders (the CPKs are not CRILAYLA-compressed either, so the
  shaders are packed some other way). An offline scan is a dead end; the captures keep only HLSL.
- `SET_TEXTURE_ADDRESS` bit 12 is not in any log yet (`[TEXFMT]` printed only the gamma bits).

**Patch:** `tex-expand.patch` (`TEX_EXPAND=1`, off by default): the upstream port, wired into
the FP PSO key like gamma. With or without the switch it logs `[FPEXPAND] fp=... bx2_units=...
biased_units=...` once per program that has either, so one run answers the question.
Verified offline: the decompiler and texture-layout files compile, and 21/21 tests pass
(`test_expand.c`, the upstream cases plus the opt-in gating).

### 3. sRGB colour writes (SET_SHADER_PACKER, 0x1FEC) are never applied (unknown use)

`rsx_fp_srgb_output_enabled` / `rsx_fp_apply_srgb_output` exist but only the live draw engine
calls them. If the game enables sRGB writes to its 8-bit scene target and reads the target back
with gamma bits set, we now decode (8ee4e58) without having encoded: darker, more saturated
midtones. The captures show every RT read with `remap=0xAAE4` and format `0xA5`, but the ADDRESS
word was never logged. `color-diag.patch` adds `[RSXPK] shader_packer=` on change and
`addr=0x...` to `[TEXFMT]`. If the packer is non-zero and an RT unit shows gamma bits, the next
patch is the output conversion (upstream's engine code at `rsx_draw_engine.c:1628-1741` is the
template).

### 4. Checked and ruled out (for these symptoms)

- **Scene RT precision:** the scene target is read back as `0xA5` A8R8G8B8 (1280x720) in every
  capture, so there is no FP16-vs-8-bit clamp; HDR is packed as colour/alpha by op320.
- **Blend / clear state:** scene draws are `blend=0`, HUD draws `blend=1` with the guest factors
  decoded (`gl_blend_op_d3d` etc.); clears carry the guest colour. Nothing in the captures points
  at them.
- **Namek (s06) teal ground and green sky** are the stage's real colours (Dragon Ball canon); the
  grain is the missing mipmaps (`patches\tex-mips`), not a colour bug.
- **Missing glare/bloom pass:** op320's glare input `0x0E590000` is cleared to black and never
  drawn in the captured frames, but RPCS3 shows no obvious glow either; the haze in RPCS3's sky
  is consistent with a sane exposure rather than a missing pass.

## How the AI assistant should test (cheapest first)

1. `git apply --check` all four patches on `jstars-port` (they apply alone or in order:
   tex-expand, color-diag, tonemap-exposure, lum-readback). Commit, build.
2. **A/B:** demo to the Ace card (shot at ~200 s), with `TONEMAP_EXPOSURE=1`, and once with `=0.5`
   and `=2`. Expect `[TONEMAP] fp=... exposure c1.w=7200` with no override. Compare with
   `reference\rpcs3\Screenshot 2026-10-20 174958.png`. A match at some value confirms cause 1.
3. **Fix:** `LUM_READBACK=1` alone. Expect `[LUMRB] off=0x039FFA40 20x11 pitch=...` with non-zero
   pixels, and `[TONEMAP] ... c1.w` dropping from 7200 and moving between scenes. Screenshot the
   Ace and Goku cards and the Namek fight; fps should be unchanged (one 20x11 copy per frame).
4. **Diagnostics:** in the same runs look for `[FPEXPAND]` lines (cause 2), `[RSXPK]` and
   `[TEXFMT] ... addr=` with `TEXFMTDBG=1` (cause 3). If `[FPEXPAND]` reports units, rerun with
   `TEX_EXPAND=1` and compare.
5. If `[LUMRB]` prints but `c1.w` stays 7200: the game reads the target another way. Next step
   is a `GCM2D_DBG=64` run and an `NV3089_GATE` log to see the transfer it uses.

## Pass 2 (2026-10-21 ~15:00): what the AI assistant's runs showed, revised ranking, next test

The AI assistant's runs are in `color\{base,tm05,tm1,tm2,lumrb,lumrb_xfer}` and FINDINGS §37 (second §37).

**1. `TONEMAP_EXPOSURE` 0.5/1/2 made the picture darker and browner. That is the expected
direction, and it rules exposure out as the brightness gap.** The tone-map (`fp_op0325.hlsl`) is
`out = 1 - exp(-4 * rgb * Lm)` with `Lm = L*E / (L*E + 1)`. With `E = 7200`, `Lm` is ~1 for
anything but black, so 7200 is the *brightest* this curve gets; any lower `E` only darkens.
Our picture is already darker than RPCS3's in the same scenes (`reference\rpcs3`), so the pinned
exposure cannot be what is missing. The difference must be in the tone-map's *input*.

**2. `LUM_READBACK=1` never read the 20x11 target, so that test has not run yet.**
`rsx_color_target_dims` (`rsx_d3d12_backend.c:4881`,
`if (w < 16 || w > 2048 || h < 16 || h > 2048) { w = 0; h = 0; }`) turns the 20x11 clip into
"window size": the target is a 1280x720 resource (`offrt[5] off=0x039FFA40 1280x720 dxgi=28` in
all 55 captured frames, every `rsxcap*` manifest) and was over `LUM_READBACK_MAXPX`. The only
target that qualified was 0x0D6A0000 80x45 (`[LUMRB]` lines in `color\lumrb\stderr.txt`).
`patches\color-exposure\lum-readback-2.patch` fixes this: the draw record keeps the raw
`surface_clip_w/h`, the OffRT remembers them as `gw/gh`, and the copy and the guest write cover
only that rect (20x11 => 11 rows of 80 bytes at the surface pitch, no overrun; `MAXPX` is applied
to the guest rect). The `[LUMRB]` line now prints the rect, the resource size and `meanRGB`.
Checked: applies to HEAD d8de15f and to the working tree with `--ignore-whitespace`.

**3. The 2D engines are not involved.** the AI assistant's gate run (`GCM2D_DBG=64`, `NV3089_GATE`) logged
no `[NV3089G]` and no `[GCM2D-UNH]` line at all, only six `[GCM2D] inline write` at boot. So the
game does not move the luminance or a glare image with NV3089/NV0039; whatever reads the 20x11
target reads local memory directly (PPU through 0xC39FFA40, or an SPU DMA). That is exactly what
`LUM_READBACK` serves.

**4. The glare layer is empty in every frame we have.** `fp_op0320.hlsl` (HDR encode) does
`out = glare.rgb * 2 + scene.rgb * glare.a / 4` with t0 = 0x0E590000 (1280x720), then
RGBM-encodes. 0x0E590000 and 0x0DC20000 (640x360, bound as t1 by ops 322-331 but never sampled
by the tone-map) are CLEARed every frame and drawn in none of the 55 captured frames (0 `DRAW rt=`
hits, no alias offsets in 0x0E5xxxxx or 0x0DC2xxxx). Captures record every draw, so the *game*
skips those passes in our build. Ruled out as the trigger: occlusion queries (GET_REPORT only
types 2 and 3 = ZCULL stats, `cellGcmSys.c:2083ff`; a ZPASS count would be type 1), 2D
transfers (point 3). The one CPU-visible GPU result we know the game reads is the 20x11
luminance target, which reads as zeros, so "scene black => no glare, exposure to its ceiling" is
the simplest story: exposure 1800 then 7200 in the first frames, constant afterwards; neither is a
literal float in `EBOOT.elf` (no 0x45E10000 / 0x44E10000), consistent with a computed value
hitting a clamp.

**Why this fits the look:** an additive glare layer through `1 - exp(-4x)` pushes bright areas
toward white: the sky gets a bright haze and bright grass desaturates. Without it, at the same
exposure, the picture is darker and *more* saturated: neon grass, flat sky. That is our picture
versus RPCS3's.

### Ranking after pass 2

1. **The game never sees the luminance target** (medium-high confidence): pins exposure at its
   ceiling and, most likely, suppresses the glare passes. Test: lum-readback-2.
2. **Glare decision has another input** (if 1 reads back but no glare appears): candidates are the
   80x45 / 320x180 buffers (also local memory; `LUM_READBACK_MAXPX=60000` covers them).
3. **Material-level colour** (texture `_bx2`/biased expand, gamma): `[FPEXPAND]` reported nothing,
   so TEX_EXPAND is parked.

### Smallest next test (AI assistant)

1. `git apply patches\color-exposure\lum-readback-2.patch` (on top of d4b3d58; `--ignore-whitespace`
   if the tree is CRLF), build.
2. Demo with `LUM_READBACK=1` (default MAXPX). Expect
   `[LUMRB] off=0x039FFA40 20x11 (res 1280x720) ... meanRGB=<non-zero, changing>` and
   `[TONEMAP] fp=0x0FC9FC01 exposure c1.w=` leaving 7200 within a second and varying by scene.
3. If `c1.w` moves: RSXCAP the Ace card and the Namek fight and `grep "DRAW  rt=0x0E590000"`.
   Glare draws appearing is the confirmation; compare the shots with `reference\rpcs3`.
4. If `meanRGB` is non-zero but `c1.w` stays 7200: rerun with `LUM_READBACK_MAXPX=60000`. If it
   still stays, the exposure is not fed from the GPU at all and the next step is on the PPU side
   (a watch on guest reads of 0xC39FFA40); hand that back to this thread.

### Yoshiwara comparison (the AI assistant, `reference\rpcs3\COMPARISON_yoshiwara.md`), read 2026-10-21 ~15:20

Looked at `frames\frame_210s.jpg`, `frame_240s.jpg` against `menu\tours\t6_texmips_default\16_fight_20s.png`,
`18_fight_35s.png`, and `color\base\t200.png` against `color\tm05\t200.png`.

The three things the AI assistant lists are the two halves of cause 1, and they split cleanly:

- **Burned-out shoji panels, lamp strips and floor tiles, flat darks = exposure at its ceiling.**
  The tone-map is `out = 1 - exp(-4 * rgb * Lm)`, `Lm = L*E/(L*E+1)`. With E = 7200, `Lm` = 1 and
  the curve is `1 - exp(-4*rgb)`: 0.25 -> 0.63, 0.5 -> 0.86, 1.0 -> 0.98. Anything moderately bright
  is near white, and the darks are lifted 4x and go flat. With an adapted exposure where
  `L*E` is about 1 (`Lm` = 0.5) the same inputs give 0.39, 0.63, 0.86: darker mid-tones and
  panels that keep their texture, which is RPCS3's look. The AI assistant's `TONEMAP_EXPOSURE` 0.5/1/2 were
  far below that (`L*E` ~ 0.1-0.5 for a scene average of ~0.2: `Lm` 0.1-0.3, hence dark and brown).
- **No halos around lanterns, panels and attack effects, no haze = the glare layer is empty**
  (point 4 above: 0x0E590000 cleared, never drawn, added x2 by the HDR encode).
- Floor sheen, skin shading and texture sharpness are separate items (material shaders, §31,
  TEX_MIPS), not this chain.

**Cheapest test, no build (exposure half):** the existing build with `TONEMAP_EXPOSURE=5`, `=10`,
`=20`, `=50` on the Yoshiwara fight at a fixed moment (the `16_fight_20s.png` view). If one value
brings the panels and lamp strips back to RPCS3's levels while the hall keeps its mid-tones, the
exposure half is confirmed and the right range is known. The halos will still be missing at every
value; they need the game to run its glare passes.

**Test for the glare half (one patch, already delivered):** `lum-readback-2.patch` with
`LUM_READBACK=1`; then RSXCAP a Yoshiwara fight frame and `grep "DRAW  rt=0x0E590000"`. Glare
draws appearing, plus `c1.w` leaving 7200 on its own, closes both halves. Nothing in the backend
can draw those passes for the game; they only appear if the game decides to issue them.

## Pass 3 (2026-10-21 ~16:30): the 20x11 target is in main memory; v2 wrote the wrong address

The AI assistant's lum-readback-2 run (`color\lumrb2`): `[LUMRB] off=0x039FFA40 20x11 (res 1280x720)
... meanRGB=0` eight times, `c1.w` 7200 throughout. Three findings:

1. **The GPU side is fine.** The draw into the 20x11 target lands where expected: `render_frame`
   uses the guest viewport literally when it fits the resource (`dvp = (vp_x, vp_y, vp_w, vp_h)`,
   backend ~6135), so op324 draws into pixels 0-19 x 0-10 of the 1280x720 resource. The capture
   dump `rsxcap_flush1\frame_00\pass_op0324_post_rt_039FFA40.bmp` shows exactly that: a 20x11
   block of luminance values (stored in the blue channel, like the 80x45 stage) in the top-left
   corner, zero in row 11 and column 20. No scissor or viewport patch is needed.
2. **"Eight times, always 0" was the log, not the data.** v2 counted all targets in one counter
   and printed the first 16 lines, then every 300th. Two targets per frame in fixed slot order
   (80x45 in slot 4, 20x11 in slot 5) means every 300th line is always the 80x45 one; the eight
   20x11 lines are frames 1-8, the black boot frames (the 80x45 lines of those frames are 0 too).
   The 20x11 rect was copied every frame after that, unseen.
3. **The copy went to the wrong memory.** `cellGcmResolveLocated(1, 0x039FFA40)` always succeeds
   (local 0xC39FFA40), so v2 wrote there every frame. But the game's IO window is
   `ioSize=0x3A00000` (`[HLE] _cellGcmInitBody(... ioSize=0x3A00000, ioAddr=0x45300000)` in every
   stdout), and 0x3A00000 - 0x039FFA40 = 0x5C0 = 1472 bytes = 11 rows x 128-byte pitch + 64. The
   20x11 target is the last 1.4 KB of the IO-mapped main memory: the PPU reads it there (fast
   main-memory read, the usual PS3 way to read a GPU result), never through the local alias.
   The backend keys surfaces by offset only; `rsx_state` had no field for SET_CONTEXT_DMA_COLOR
   (rsx_commands.h said so), so nothing could tell the two apart.

**`patches\color-exposure\lum-readback-3.patch`** (on top of lum-readback-2, applies to HEAD
cc50482; on the CRLF tree use `--ignore-whitespace`, plain apply fails on rsx_commands.c/.h):
- `rsx_commands.h/.c`: `surface_color_dma[4]` records SET_CONTEXT_DMA_COLOR_A..D (0x194, 0x18C,
  0x1B4, 0x1B8).
- backend: the draw record and the OffRT carry the handle; `lum_readback_finish` resolves main
  memory (`0xFEED0001`) through the IO table, local otherwise; the `[LUMRB]` line is per target
  (first 8, then every 150th) and prints `dma=` and `(main|local)`.

**Expected with `LUM_READBACK=1`:** `[LUMRB] off=0x039FFA40 dma=0xFEED0001 20x11 ... -> guest
0x48CFFA40 (main) ... meanRGB=<non-zero after the boot frames>` and, if the game reads it as
assumed, `[TONEMAP] fp=0x0FC9FC01 exposure c1.w=` leaving 7200 within a few frames of the first
non-zero mean. If `dma=` prints `0xFEED0000` for that target, the main-memory theory is wrong and
the next step is a PPU-side read watch. If `dma=0xFEED0001` and `c1.w` still stays 7200, the
PPU reads the value but something else fixes the exposure; then the Yoshiwara exposure sweep
decides whether to force it.

## Pass 4 (2026-10-21 ~17:30): the exposure sweep explained, the burn is in the scene alpha, and a FIFO-level test for the glare layer

### Revising the Yoshiwara note: the exposure value cannot un-burn the panels

The AI assistant's sweep (FINDINGS §40): `TONEMAP_EXPOSURE` 10/20/50 look identical to 7200, 5 is only a
little darker. That follows from the tone-map (`fp_op0325.hlsl`, verified line by line):

    in  = enc.rgb / enc.a / 4            (decode of the HDR-encode output)
    L   = dot(in, (0.2125, 0.7154, 0.0721))
    Lm  = L*E / (L*E + 1)                (E = c1.w)
    out = 1 - exp(-4 * Lm * in),  then pow(0.4545)

`Lm` is a per-pixel Reinhard on the pixel's own luminance, and it saturates: for a bright pixel
(L ~ 0.5) `Lm` is 0.83 at E=10, 0.96 at E=50, 1.0 at 7200, so the curve barely moves above
E~10. Only E~5 pulls mid-tones down. The earlier "lower exposure gives RPCS3's levels" was wrong;
the exposure is not what burns the panels, and the Yoshiwara sweep paragraph above is withdrawn.

### What does burn them: scene.rgb / scene.a before the tone-map

The HDR encode (`fp_op0320.hlsl`, same shape as Yoshiwara's fp 0x0FD00281) computes
`in = glare.rgb*2 + (scene.rgb / scene.a) * glare.a / 4`; with the glare layer cleared to
(0,0,0,1) that is `in = scene.rgb / (4 * scene.a)`, and the tone-map output is
`1 - exp(-Lm * scene.rgb / scene.a)`. The scene RT is 8-bit, so `scene.rgb <= 1`:

| scene.a | white pixel after tone-map + gamma |
|---|---|
| 1.0 | 0.81 (not burned) |
| 0.5 | 0.93 |
| 0.33 | 0.98 (burned) |
| 0.25 | 0.99 |

So the alpha the material shaders leave in the scene RT is the HDR scale (the sky/background
pass, the 3D draws into 0x0ED10000 all write it directly: 505 of 510 Yoshiwara scene draws are
`blend=0 cmask=F`, clear alpha is 0). Burned shoji panels and tiles mean our scene alpha on them
is 0.33 or less; RPCS3's is not. Where our alpha could go wrong: the FP decompiler's alpha lane
for those materials, the alpha-test/`fp_alpha` path, or a texture whose alpha lane feeds it.
The RGB capture dumps cannot show this, hence:

**`patches\color-exposure\cap-alpha.patch`** (backend, 56 lines): `RSX_CAP_ALPHA=1` writes the
alpha channel of every capture dump as a grey `<name>_a.bmp` next to it (8-bit, FP16 and FP32
targets). Test: Yoshiwara `RSX_CAP=...` with `RSX_CAP_ALPHA=1`, then look at
`rt_0ED10000_a.bmp` (end of frame; with `RSX_CAP_PASSES=2`, the `pass_op<encode>_pre_rt_...`
one is the scene the encode saw). Panels and tiles darker than ~85/255 grey confirm the alpha
cause; then the material FP for one panel draw is the next thing to read.

### The glare layer: what we know, and the decisive test

- Four surfaces are cleared every frame and drawn in none of the captured frames (demo and
  Yoshiwara): 0x0E590000 1280x720 cleared (0,0,0,1) = the glare layer (op320 adds it x2);
  0x0DC20000 640x360 cleared (0,0,0,0); 0x0DB30000 / 0x0DD10000 640x360 cleared to 0.498 grey
  (a velocity/DOF-style neutral) and read by the second chain (ops 353-365: 0x0DA40000, blur
  320x180, composite into 0x0ED10000). They are object layers: the game draws selected objects
  (glow sources, moving things) into them, then blurs and composites.
- Not MRT: no draw in either capture binds a second colour target.
- Not occlusion queries, not NV3089/NV0039 (passes 2-3).
- Two backend paths drop draws silently, so a capture cannot tell "never issued" from
  "dropped": `rsx_no_color_target()` (SET_SURFACE_COLOR_TARGET = NONE) and
  `d3d12_draw_indexed/arrays` ("other primitives are skipped rather than drawn wrong": POINTS,
  LINES, POLYGON). No warning is logged for either.

**`patches\color-exposure\rt-drawlog.patch`** (rsx_commands.c only, 44 lines; gcc syntax check
passes): `RT_DRAWLOG=0E590000,0DC20000,0DB30000,0DD10000` logs, at the FIFO level before any
backend filtering, every surface bind, colour_target change, clear, BEGIN (with primitive, fp,
attrib-0 enable, viewport) and draw batch that targets those surfaces, as `[RTDRAW] ...`
(`RT_DRAWLOG_MAX`, default 400 lines). Outcomes:
- only `surface A=...`, `clear ...` lines, no `begin`/`draw` for 0x0E590000: the game never
  issues the glare draws in our build. The gate is then on the PPU/SPU side; with lum-readback-3
  in, the luminance readback is the first input to check (does the glare appear once `c1.w`
  moves?), and after that a PPU trace of what reads the 20x11 target.
- `begin ... target=0x0` lines: the draws exist but go out with no colour target bound and the
  backend drops them; next step is a patch that keeps them.
- `begin ... prim=1/2/3/4/10`: points/lines/polygons, dropped by the backend; next step is a
  point/line path for those draws.
- `begin`/`draw` lines with prim 5-8 and target set: they reach the backend and are lost later
  (vertex fetch, VBFULL); the `[RTDRAW]` fp/vp fields say which draw to trace.

## Pass 5 (2026-10-21 ~18:00): why lum-readback-3 logged dma=0

The AI assistant's lum-readback-3 run (`color\lumrb3`): every `[LUMRB]` line has `dma=0x00000000`, the
20x11 rect has real, scene-dependent means (92/111/124/82) after boot, `c1.w` stays 7200. The
readback still went to the local alias.

Cause: `rsx_process_method` routes only methods 0x200-0x23C and 0x280-0x28C to
`process_surface_method` (rsx_commands.c, the range gate right after the semaphore handling).
The DMA context handles sit below that block (SET_CONTEXT_DMA_COLOR_B 0x18C, _A 0x194, ZETA
0x198, _C 0x1B4, _D 0x1B8), so the four `case` lines v3 added inside `process_surface_method`
were never reached; the method fell through to the main switch, which has no case for them.
Nothing else swallows them earlier (the sync hook only calls out for 0x50/0x64-0x6C/0x1D6C-
0x1D74/0x1800). The game does use SET_CONTEXT_DMA_COLOR_A; the capture tool's `rsx_dispatch.c`
decodes the same register (M_DMA_COLOR0 = 0x0194) for the live engine.

**`patches\color-exposure\lum-readback-4.patch`** (rsx_commands.c, 7 lines): adds the four
methods to the range gate so they reach the existing cases. Applies to HEAD de12d74 and to the
CRLF tree with `--ignore-whitespace`; gcc syntax check passes. Expected with `LUM_READBACK=1`:
`[LUMRB] off=0x039FFA40 dma=0xFEED0001 ... -> guest 0x48CFFA40 (main)`, then `[TONEMAP]
fp=0x0FC9FC01 exposure c1.w=` leaving 7200. If `dma=0xFEED0000` prints instead, the target is
local after all and the PPU reads it some other way (PPU read watch next).

## Pass 6 (2026-10-21 ~19:50): what the PPU does with the 20x11 target, why `[TONEMAP]` went quiet, `tonemap-log.patch`

The AI assistant's lum-readback-4 run (`color\lumrb4`, FINDINGS §40): `[LUMRB] off=0x039FFA40 dma=0xFEED0001 20x11
... -> guest 0x48CFFA40 (main) meanRGB=82..124`, the Ace/Goku cards darker and more contrasty than base,
`[TONEMAP]` only printed early draws (all 7200). Read-only answers from the recompiled EBOOT
(`jstars\port\recompiled\ppu_recomp_028.cpp`, the module with TOC 0x8e3858; constant pool at 0x8dc06c-0x8dc0c4:
0.18, 0.5, 0.025, 0.25, 63.75, &lumweights 0x79f3d0, 1.0, 255.0, FLT_EPSILON, 0.98, 15.0, 1e-4).

### What the 20x11 target holds (chain correction)

- op321 (fp `0x0FC9FA01`, 1280x720 -> 320x180 `0x0D650000`) is **not** the luminance downsample: per tap it
  decodes `enc.rgb/enc.a/4`, computes `L = dot(rgb, w)`, averages the four taps and multiplies the result by
  `(avgL - 0.5)` (c4.z of the same quad): a **bright-pass** with threshold 0.5. It is the glare input, not the
  exposure input.
- op322 (fp `0x0FC9FB81`, 1280x720 -> 320x180 `0x0D7A0000`) re-encodes the HDR scene: `out.rgb = enc.rgb`,
  `out.a = 0.25 / (M + c0.x * layer.a)` with `layer` = the 640x360 effect layer `0x0DC20000` (identity while
  that layer is empty, as it is in our runs).
- op323/op324 (fp `0x0C6ABE81`, no constants) are plain 4-tap averages of the **encoded bytes**: the 20x11
  target is averaged RGBM (rgb = scene/M/4 scaled, A = 0.25/M), 8-bit, A8R8G8B8. Not log, not RGBE.

### What the PPU computes (func_0055C5A4, every frame from func_00648374 with the HDR object at `[r30+0xC28]`)

If `obj+0x28` (enabled) is set it fetches the surface descriptor (func_0055A774: width `+0x1C` = 20, height
`+0x20` = 11, pitch `+0x28` = 128, pixel pointer `+0x34`), then per pixel, bytes in memory order
**A, R, G, B** (byte 0 = A: exactly what lum-readback writes):

```
rgb = (R,G,B)/255;  A = A/255
if (A > 0) rgb *= 0.25 / A              // RGBM decode = the tone-map's enc.rgb/enc.a/4
L   = dot(rgb, (0.2125, 0.7154, 0.0721)) // func_0055C0F0, weights at 0x79f3d0
sum += log(L + FLT_EPSILON)              // func_00719BCC
Lgeo = exp(sum / (w*h))                  // func_006532F8: log-average luminance -> obj+0x34
La  += (Lgeo - La) * (1 - 0.98^(15*dt))  // dt = obj+0x40; adapted luminance -> obj+0x2C
```

Then func_0055D388 -> func_0055D1F8(f1 = La `+0x2C`, f2 = key `+0x30`, f3 = `+0x38`, f4 = `+0x3C`) builds the
constant quad the inline NV308A writes put into the fp programs (c1 of the tone-map, c4 of op321):

```
c1 = ( 63.75, max(La + f3, 1e-4), clamp(La + f4, 0, 63.75), key / max(La + f3, 1e-4) )
```

The captured `(63.75, 0.0001, 0.5, 7200)` is therefore La = 0 (black readback), f3 = 0, f4 = 0.5,
**key = 0.72**. The boot value 1800 is the default key 0.18 (func_0055BCE0/func_0055C858 set 0.18 and 0.5),
replaced by 0.72 by the scene. So **E = 0.72 / La**, and La is the geometric mean of the decoded 20x11
luminance; at 1/60 s per frame the adaptation moves 0.5 % of the gap per frame (3-4 s to settle), 1 % at
1/30 s.

### What this means for the Ace card

- The AI assistant's log has A = 255 on the 20x11 (M = 0.25) and meanRGB 82-124: decoded mean ~0.08-0.12, and a
  geometric mean of luminance sits below the arithmetic one, so **La ~ 0.03-0.1 and E ~ 7-25** is what the
  loop is heading for. Darker than E = 7200 is the designed behaviour: at 7200 the tone-map had `Lm = 1`
  on every pixel (its ceiling, `out = 1 - exp(-4*in)`); any real E lowers it.
- **RPCS3 being brighter than our E = 7200 base cannot be an exposure difference.** RPCS3 runs the same PPU
  code on the same 20x11 chain, so its E is also ~0.72/La (if anything lower: its effect layers add to M).
  The extra brightness enters `in` itself: the glare layer (`in = glare*2 + scene/4`, ours is black) and/or
  the scene/alpha term. There is no E that reproduces RPCS3; the comparison target for E is simply
  E = 0.72/La, which `tonemap-log` now prints.
- The 80x45 target also gets a `Lgeo`/`E~` field; it is meaningless for the game (the PPU reads only the
  20x11), ignore it.

### Why `[TONEMAP]` only printed early draws

tonemap-exposure matched the tone-map with `k[5] > 0 && k[5] < 0.001`, i.e. **c1.y = max(La,1e-4) < 0.001**.
That is true only while La < 0.001, so the log (and the `TONEMAP_EXPOSURE` override) stopped exactly when
the readback started working; the lines the AI assistant saw were the pre-adaptation frames. The cadence (first 6,
then every 300th) hid the rest.

### `patches\color-exposure\tonemap-log.patch` (backend, on top of tonemap-exposure + lum-readback-2/3)

- Match: `1e-4 <= c1.y <= 63.75` instead of `< 0.001`. Log: `[TONEMAP] fp=... c1=(63.75, La=, thr=, E=)` on the
  first 6 draws, every 120th, and whenever E moves by more than 2 %. The override works at any La.
- `[LUMRB]`: computes the PPU's `Lgeo` from the copied bytes exactly as above and prints `Lgeo= E~=`
  (`0.72/max(Lgeo,1e-4)`) per target every 60 frames.
- Applies to HEAD c8d2329 and to the CRLF tree with `--ignore-whitespace`. Not compiled here.

**Test (the AI assistant):** `LUM_READBACK=1`, demo to the Ace card. Expect `[LUMRB] off=0x039FFA40 ... Lgeo=0.0x E~=NN`
and `[TONEMAP] ... La=` climbing from 1e-4 towards that `Lgeo` over 2-4 s with `E` ending near `E~`. If E tracks
E~ the exposure loop is complete and correct; the remaining gap to RPCS3 is the glare layer / scene alpha work
(rt-drawlog, cap-alpha), not exposure. Optional A/B: `TONEMAP_EXPOSURE=<E~>` with the readback off should look
the same as the settled readback.

### Pass 6 follow-up (2026-10-21 ~20:15): the AI assistant's Yoshiwara tonemap-log run explained

The AI assistant (`color\yoshi_tmlog`): in the fight the logged `La` is 0.09-0.15 while `Lgeo` is 0.06-0.09, and
`E x La = 0.36` on 387 of 388 lines, not 0.72. Both are the stage's HDR parameters, not a bug:

- `thr - La = 0.440` on all 1795 fight lines (0.500 on the 59 menu lines). `thr = La_real + f4` and
  `c1.y = La_real + f3` (func_0055D1F8), so `f4 - f3 = 0.44` with `f4 = 0.5` gives **f3 = 0.06**: the
  logged `La` is `c1.y = La_real + 0.06`. Subtracting it, `c1.y - 0.06` tracks `Lgeo` closely (0.0448 vs
  0.0448, 0.0771 vs 0.0771, 0.0582 vs 0.0587; the misses are the adaptation lag during camera moves). So
  La_real does settle on Lgeo; the apparent 1.5-2x gap is the 0.06 offset.
- `E x c1.y = 0.36` means **key = 0.36 in the Yoshiwara fight**; the menus and the demo use 0.72 (the 59
  lines at 7200) and the very first frames 0.18 (the default). The key and the offset are per-scene
  values of the HDR object (`+0x30`, `+0x38`), set by the stage, so `E~ = 0.72/Lgeo` printed by
  tonemap-log is only right for the demo. At Yoshiwara the right prediction is
  **`E = 0.36 / (Lgeo + 0.06)`** = 2.3-3.9, which is exactly what the log shows.

Conclusion unchanged: the loop works as the game designed it, RPCS3 runs the same arithmetic with the same
constants, and the darker fight picture is the intended exposure. The brightness gap to RPCS3 is in the
glare layer / scene term, not in E. (tonemap-log's `E~` field assumes key 0.72 and no offset; read it
as `0.72/Lgeo` and rescale, or ignore it outside the demo.)

## Brightness slider (2026-10-21 ~20:15, after the user's note that the options menu has one)

- **Where it acts:** the last full-screen pass of the frame (demo op386, fp `0x0FCA2FC1`, the only pass
  that writes the backbuffer `rt=0x00000000`) is `pixel_shader_postprocess_option_brightness.pcg`
  (name in the EBOOT string table, parameter `_sp_gamma`). Its code is
  `out.rgb = exp2(log2(in.rgb) * c.x)` per channel, i.e. **out = in ^ gamma**; nothing else is applied
  after it. It is not part of the tone-map (whose own `_sp_gamma`/`_sp_exposure` parameters are the fixed
  pow 0.4545 and the HDR quad `_sp_system_hdr_param_half`).
- **Value in our runs:** `fp_constants[0..2] = (1, 1, 9.78e-35, 0)`, i.e. **gamma = 1.0** (the pass is a
  no-op) in every capture we have: `rsxcap_flush1` frames 00/02 (Ace) and `rsxcap_skin` frame 03. So our
  pictures are at the neutral slider position.
- **Default:** our save (`jstars\run\gamedata\dev_hdd0\home\00000001\savedata\NPUB31647-B0\SYS-DATA`,
  350000 bytes, 629 non-zero) was written by our own port on 2026-10-20 22:25 and never touched in the
  options menu, so gamma 1.0 is the default position (inferred, not traced to the option code). A float
  pool at 0x7b0118-0x7b015c holds exactly the set 1/1.8, 1/1.6, 1/1.4, 1/1.3, 1, 1.3, 1.4, 1.6, 1.8, which
  looks like the slider's gamma steps around 1.0 (plausible, not confirmed: the function using it was not
  found in the time spent).
- **Save/options path in our build:** `[cellSaveData] AutoLoad2 ... LOAD dir='NPUB31647-B0' (new=0)`
  then a single `file op=0 ... name='SYS-DATA' off=0 size=4 ... -> 4` and `LOAD complete`: the game reads
  only the first 4 bytes of SYS-DATA at boot and ends the sequence itself (the callback returned LAST).
  Whether the options are then applied from the save is **untested**. The RPCS3 reference session
  (`jstars\run\rpcs3_reference.log`, 2026-10-19) had no save at all (`funcStat returned result=-4`,
  NODATA), so it ran on defaults too. RPCS3's current save (`RPCS3\dev_hdd0\home\00000001\savedata\
  NPUB31647-B0\SYS-DATA`, 2026-10-21 15:02) differs from ours in 39 bytes (header fields at 0x14/0x18 and
  two blocks at 0x29A8B and 0x4988D); which byte is the brightness option is not known.
- **Could a different brightness explain RPCS3's brighter look?** Only if the slider was moved in RPCS3:
  at gamma 1.0 both sides are neutral. Cheap tests, no build needed:
  1. In RPCS3, open Options and read the Brightness position used for the reference screenshots; if it is
     not the middle, retake one reference at the middle (or move ours to the same step).
  2. In our port, move the slider in the options menu: if the picture changes, the option path works and
     an `RSX_CAP` capture shows `fp_constants[0].x` of the last pass leaving 1.0; then quit and restart
     to see whether the setting survives (tests the SYS-DATA path above).
  3. If an A/B without the menu is wanted, a `BRIGHT_GAMMA=<v>` override of that pass's constants is a
     ten-line patch in the tonemap-exposure style (match: 3 constants, backbuffer target, 11 instructions);
     not written, ask if needed.

## Character-only brightness (2026-10-21 ~20:30, user question; read-only, no patch written)

**What the AI assistant's brightness tests changed.** Both were whole-frame: `TONEMAP_EXPOSURE=<v>` (color\yoshi_tm5..50)
forces the tone-map constant c1.w, and `LUM_READBACK=1` (color\lumrb4, yoshi_tmlog) lets the game set it from the
20x11 average. The tone-map (op325) and the brightness pass (op386) are full-screen passes over the finished
scene; neither knows what a pixel belongs to, so they cannot treat characters and stage differently. The look the
user liked (darker, more contrast on the characters) came from a lower E on everything.

**Can characters be told apart?** Yes, at the draw level, in the scene pass:
- Demo frame (`rsxcap_flush1\frame_00`): the scene target 0x0ED10000 gets 50 draws. Ace is ops 0-19, twenty
  batches of 762/726 vertices, one fragment program `0x0FC97BC1`, textures 0B998E80/0B94E300/0B605E00/0B983900
  (his skin/hat/cloth set); the stage comes later with other programs (`0x0FCA1E81`, `0x0FCA1801`,
  `0x0FCA0E01`, `0x0FCA0401`, texture 0B52BD80 / 01160C80). So characters are separate draws with their own
  fragment programs and textures; in fights each character/costume brings its own small set of programs.
  The character programs are also the ones that sum PCF shadow taps (FINDINGS "Shadow passes, pass 2").
- Fragment-program addresses move between runs (the tone-map was 0x0FC9FC01 in the demo capture and 0x0FC00241
  in the AI assistant's run), so a hook must match programs by content (the backend already hashes the decompiled HLSL
  for the shader cache, `shcache_key`), or by a texture/constant signature, not by address.

**Smallest opt-in patch (not written):** a per-program gain in the pixel shader, applied before the scene is
written, which the tone-map then sees as a darker/brighter object:
- `FP_GAIN=<v>` (e.g. 0.7) and `FP_GAIN_KEYS=<hash>[,<hash>...]`; the backend's fp compile path
  (`vp_get_fp_pso` -> decompile -> `D3DCompile`) inserts `_o.rgb *= <v>;` before the shader's final
  `return` when the program's cache key is listed. The modified text gets its own cache key, so the shader
  cache stays correct. With `FP_GAIN_LOG=1` the backend prints `[FPGAIN] fp=0x... key=... instrs= tex0=...`
  once per new program so the keys for the characters can be read off a run (or from a capture: ops 0-19 above).
- Since the scene RT stores rgb with the HDR scale in alpha, scaling rgb alone scales the pixel's HDR value;
  for a gain < 1 nothing clips. With E saturated at 7200 the tone-map gives `1 - exp(-4 * gain * in)`, so a
  gain of g on the characters is the same as exposure x g on them only; with the readback on, the 20x11
  average shifts a little too (characters are a small part of the frame).
- About 40 lines in `rsx_d3d12_backend.c`, backend only, default off. The AI assistant builds; the first run is
  `FP_GAIN_LOG=1` to collect keys, the second `FP_GAIN=0.7 FP_GAIN_KEYS=...` on the Ace card.
- Alternatives that are not smaller: changing the character lighting constants (per-program vp/fp constants,
  fragile), or a stencil/mask pass to apply the tone-map twice (new render passes).

### `patches\color-exposure\fp-gain.patch` (2026-10-21 ~20:45): the brightness tests on characters only

Backend only, 77 lines, applies to c8d2329 (HEAD, and the CRLF tree with `--ignore-whitespace`), not compiled
here. **Experiment, keep it off by default.** Hook: in `vp_get_fp_pso`, after the decompile, the chosen
programs get one or two lines inserted before their final `return`, so the tone-map and the glare chain see a
darker/brighter object; the inserted text is part of the HLSL, so the shader-cache key follows it. The RSX_CAP
shader header also gains `code=0x...`, the program's code hash (addresses move between runs, the code hash
does not).

Switches:
- Effect: `FP_EXPOSURE=<E>` multiplies the program's rgb by `Lm = L*E/(L*E+1)` with `L` the pixel's luminance as
  the tone-map computes it (`rgb/alpha/4`, weights 0.2125/0.7154/0.0721). With the game's own exposure at 7200
  (`Lm = 1` for the whole frame, the default without LUM_READBACK) this is exactly what `TONEMAP_EXPOSURE=<E>`
  did to every pixel, now only on the chosen programs. `FP_GAIN=<g>` is a plain rgb multiplier instead (or in
  addition).
- Selection: `FP_GAIN_KEYS=<hex>[,<hex>...]` (code hashes) and/or `FP_GAIN_MININSTR=<n>` (every program with at
  least n instructions). Nothing happens unless an effect and a selector are both set.
- `FP_GAIN_LOG=1`: one `[FPGAIN] fp= code= instrs= consts= nrt= blend=` line per distinct program as it compiles,
  with `(adjusted)` on the ones selected.

**Same looks as the AI assistant's tests, characters only (run with LUM_READBACK unset):**

| The AI assistant's run | whole-frame switch | characters-only equivalent |
|---|---|---|
| `color\tm05`, `tm1`, `tm2` (demo) | `TONEMAP_EXPOSURE=0.5 / 1 / 2` | `FP_EXPOSURE=0.5 / 1 / 2` |
| `color\yoshi_tm5/10/20/50` | `TONEMAP_EXPOSURE=5 / 10 / 20 / 50` | `FP_EXPOSURE=5 / 10 / 20 / 50` |
| `color\yoshi_tmlog` (readback, Yoshiwara, E settled 2.3-3.9) | `LUM_READBACK=1` | `FP_EXPOSURE=3` |
| `color\lumrb4` (readback, Ace demo, E ~7-25 predicted) | `LUM_READBACK=1` | `FP_EXPOSURE=10` (read the exact E from a tonemap-log run) |

**Picking the character programs (one run per stage):**
1. `FP_GAIN_LOG=1` on Yoshiwara, then on Namek, no other switch. The log lists every program once. The
   character programs are the long ones (the demo capture has Ace in 20 draws of one 0x0FC97BC1-class program;
   FINDINGS notes the 235-instruction character shader); stage programs are short. Quick first try without
   collecting anything: `FP_EXPOSURE=5 FP_GAIN_MININSTR=150` and look at who darkens; lower or raise 150
   until only the characters move.
2. For an exact set, note the `code=` of the lines that correspond to the characters (or take them from an
   RSX_CAP: in `rsxcap_flush1\frame_00` ops 0-19 are Ace; the new `code=` in `fp_opNNNN.hlsl` headers names
   them) and pass `FP_GAIN_KEYS=code1,code2,...` (up to 32). Characters that share a shader family will share
   codes across stages; a costume with its own shader needs its own entry.
3. Check: with `FP_GAIN_LOG=1` still set, the selected programs print `(adjusted)`; a frame compare against the
   same shot without the switch shows only the characters changing.

Limits: works on single-output programs (all J-Stars scene shaders); with LUM_READBACK on, the game's E is no
longer saturated and the two factors compound, so run the experiment with it off. Alpha-blended character parts
(blend=1 draws, e.g. effects) are adjusted too if their program is selected.

## Pass 7 (2026-10-21 ~21:10): the glare pass is a particle pass that is never enabled; the alpha capture needs per-pass dumps

The AI assistant's results (FINDINGS §45): `RT_DRAWLOG=0E590000` shows only bind / colour_target / clear for the glare layer,
no BEGIN and no draw, so the game itself issues nothing into it. `RSX_CAP_ALPHA` BMPs are in `color\yoshi_alpha_cap`.

**What the alpha BMPs say: nothing yet.** The capture was taken without `RSX_CAP_PASSES`, so only the end-of-frame
`rt_*_a.bmp` exist. At frame end 0x0ED10000 and 0x0DE10000 have been overwritten by the tone-map and the blur
composites (alpha 255 on 100 % of the pixels in both), the glare layer is 255 everywhere (its clear) and 0x0DC20000
is 0 everywhere. The scene alpha behind the burned panels is the alpha of 0x0ED10000 **before** the HDR encode
(op657 in `yoshi_alpha_cap\frame_01`); it needs `RSX_CAP_PASSES=1 RSX_CAP_ALPHA=1`, which writes
`pass_op0657_pre_rt_0ED10000_a.bmp`. That is the burned-panel test; it has not run.

**The particle system is alive; only its luminance pass is off.** In `yoshi_alpha_cap\frame_01` (875 ops) the effect
layers are not all empty: 0x0DC20000 (640x360) gets 7 draws per frame (ops 655-701: small blended quads, cmask 7/8,
particle textures) and 0x0DB30000 (the 0.498-grey distortion layer) gets 24 draws (ops 673-696). Only 0x0E590000
(glare) and 0x0DD10000 are cleared and never drawn. So `OpParticleEffectWorkServer` (the game's particle work
server, PPU-side on the `sp_job_cpu_thread_*` job threads; no SPU job is involved, the only SPU jobs in the run are
images 3 and 11) runs `Update`, `RenderBuffer` and `RenderParticleColor`, and its `RenderLuminance` draws nothing.

**Why `RenderLuminance` draws nothing (recompiled EBOOT, ppu_recomp_019.cpp):** `func_003A45E0` is
`OpParticleEffectWorkServer::RenderLuminance` (profiler string at TOC+0x3FC0). Its first test reads the server's
pass-flags word (`[server+0x18B04]`, written as `r31 - 0x74FC` with `r31 = server + 0x20000`) and returns at once
(`loc_003A4E3C`) when **bit 0** is clear; the sibling `func_003A5980` tests bit 2 the same way, and `func_003A2600`
(the per-server reset of its defaults) sets `flags |= 0x3F`, all six passes on. Bit 0 is toggled by
`func_0039C900(on)` (sets or clears 0x1; `func_0039C184` is bit 3, `func_0039C978` bit 5). Two callers:
- `func_0002FAA4(x, type)`: calls `func_0039C900(([obj+0x18] == 0))` where `obj = func_00030A14()`: it clears the
  pass whenever that counter/handle is non-zero, enables it when it is zero.
- `func_0024A4B4(obj, type, f1)`: enables the pass only if `f1 > 1.0` (TOC constant at -0x58C is 1.0f) and
  `func_001E9124(obj) != 0`.
So the pass is a per-request flag, not a quality setting, and in our runs it ends up clear every frame. Whether the
game never asks for it (its luminance effects never spawn, or `f1 <= 1.0`, or `[obj+0x18] != 0`), or asks and
something later clears it, is what the trace below answers. The glare/exposure state is not involved: the gate is
read before any HDR object is touched. Not a save/option: the flags are reset to 0x3F by the server itself.

**`patches\color-exposure\lumflag-trace.patch`** (2 log lines in `jstars\port\recompiled\ppu_recomp_019.cpp`, apply in
`jstars\` with `git apply --ignore-whitespace`, CRLF file; one object recompiles):
- `[LUMFLAG] set_luminance_pass(<0|1>) lr=0x...` on the first 200 calls of `func_0039C900`: who asks, with what.
- `[LUMFLAG] RenderLuminance: pass_flags=0x... aux=0x...` every 300th call: the flags word the pass sees.
Expected readings: `pass_flags=0x3E` (bit 0 clear) with `set_luminance_pass(0)` calls from `lr=0x0002FB04` = the
`[obj+0x18]` path keeps clearing it (then `func_00030A14`'s object is the next thing to read); `pass_flags=0x3E`
with no set calls at all = no effect ever requests luminance (look at `func_0024A4B4`'s callers and `f1`);
`pass_flags=0x3F` = the gate passes and the draw is lost later inside `RenderLuminance` (then an `RT_DRAWLOG` of
the glare layer with BEGIN lines, or a second gate deeper in the function, is next).

**Smallest next tests, in order:** (1) `lumflag-trace` on a Yoshiwara fight, 30 s, no other switch. (2) The same
capture the AI assistant took, with `RSX_CAP_PASSES=1 RSX_CAP_ALPHA=1`, for the burned panels (`pass_op0657_pre_rt_0ED10000_a.bmp`
and the matching colour dump; the panels should sit where alpha <= 85/255).

## Pass 8 (2026-10-21 ~21:50): RenderLuminance past the gate, and the panels' alpha

**The AI assistant's reading** (`yoshi_lumflag`): `set_luminance_pass(1) lr=0x0002FB04` x200, `RenderLuminance:
pass_flags=0x3F aux=0` on all samples. So the gate passes every frame; the question is what happens after it.

### What `func_003A45E0` does after the gate (`jstars\port\recompiled\ppu_recomp_019.cpp` 26262-27603)
- `aux` (`[server+0x18B00]`, written only by `func_0039C200`, called from `func_002FA570`) is a pointer that is
  normally 0. `aux == 0` takes `loc_003A4F70`: a **pre-pass**, not a skip. It binds the luminance target through
  the render context (`[toc+0x3F9C]`, `func_00525A6C` / `func_00527BE4`), flushes dirty state (`loc_003A5120`),
  then calls `func_003AC910(src=[server+0x18A28], dst=[server+0x18A38], r25, f1=const)` (a fill/copy of the
  target: this is the bind + clear of 0x0E590000 that `RT_DRAWLOG` saw) and `func_005259CC` (end pass), and
  falls into the main pass at `loc_003A4684`.
- Main pass (`loc_003A4684` .. `loc_003A4E30`): set colour target `[server+0x18A38]` and depth `[server+0x18A40]`
  (inlined "set target if changed" blocks: compare, flush matrices, `func_00527BE4`), set surface flags, then the
  **draw loop** at `loc_003A4CC4`/`loc_003A4D14`:
  ```
  node = [server+0x1880C] - 164          // head of the active-emitter list (link at node+0xA4/+0xA8,
  if (!node) goto end                     //  count at server+0x18814)
  do {
      vt = [node]; render = [vt+0x24]     // virtual slot 9
      render(node, r25, 0, 0, params); render(node, r25, 2, 0, params);
      render(node, r25, 0, 1, params); render(node, r25, 2, 1, params); render(node, r25, 3, 1, params);
      node = [node+0xA8] - 164
  } while (node);
  end: func_005259CC(ctx)                 // end pass
  ```
  `params = server+0x18A50`, `r25` = the function's second argument (the game passes it through
  `func_0039C7FC` -> `func_0039DBA8`; callers `func_0002FAA4`-area lr 0x0002FB64 and `func_0024A4B4` lr 0x0024A584).
  `RenderLuminance` itself issues **no draw commands**: every draw comes from the emitter's virtual `render`.
- The three sibling passes walk **the same list with the same virtual and the same `params`**; only `(pass,
  layer)` and the second argument differ: `func_003A5980` (1,0),(1,1); `func_003A6B0C` (5,0),(5,1) [the 24
  distortion draws into 0x0DB30000]; `func_003A7C10` (0,1),(2,1),(3,1) [RenderOut]. RenderLuminance asks for
  (0,0),(2,0),(0,1),(2,1),(3,1), a superset of RenderOut's. So "no luminance particles exist" cannot be the whole
  answer unless the emitter's `render` filters on something RenderLuminance passes differently (its second
  argument, stored into render-context+0x58, or the target bound). Pass numbers look like blend classes
  (0 alpha, 2 add, 3 ?, 1 and 5 special), layer 0/1 = behind/in front.
- The emitter class could not be identified statically: the vtable scan of `EBOOT.elf` (1001 tables; particle
  module classes have 6-slot tables, the game's effect classes 13/18/19 slots with slot 9 = a `return 999`
  stub) found no table whose slot 9 is a plausible render function. Nodes are moved between the server's two
  lists (`+0x18800` free, `+0x1880C` active, unlink helpers `func_003A1B98` / `func_003A1AA4`) by Update
  (`func_003A2274`) and `func_003A2C78` / `func_003A34E4`.

**Answer to "never issued or dropped":** not decidable from the code alone; the draw is issued (if at all) inside
an emitter virtual we have not located. The 24+7 draws of the siblings prove the list is non-empty and the
virtual works, so the two candidates left are (a) the virtual draws nothing for RenderLuminance's arguments
(second argument / bound target / a per-emitter "luminance" flag it reads from `params`), or (b) it draws and the
backend drops it (would show as BEGIN lines on 0x0E590000 in `RT_DRAWLOG`, which the AI assistant did not see, so (b) is
unlikely unless the draws go through a path the FIFO parser skips).

**Smallest next test: `patches\color-exposure\lumflag-trace-2.patch`** (one log line at each of the two virtual
call sites, `ppu_recomp_019.cpp`, applies on top of lumflag-trace with `git apply --ignore-whitespace` in
`jstars\`; one object recompiles). It prints, for the first 40 calls and every 1000th:
`[LUMFLAG] lum-vcall #n node= vt= fn= r4=` (RenderLuminance) and `[LUMFLAG] out-vcall ...` (RenderOut).
Readings: no `lum-vcall` lines at all = the active list is empty when RenderLuminance runs (ordering problem:
Update moves emitters after it, or the luminance request comes on a frame with no emitters); `lum-vcall` lines
with the same `fn` as `out-vcall` = the emitter's render function is `fn` (then I decode it: what it does with
`r4`/pass/layer, which is a few hundred lines once the address is known); different `fn` = two emitter classes.
In every case the `r4` difference between the two passes is the first thing to look at.

### Burned panels: scene alpha confirmed (`color\yoshi_passes_cap\frame_00`)
In this capture the HDR encode is **op678** (fp 0x0FD038C1, t0 = 0x0E590000 glare, t1 = 0x0ED10000 scene,
target 0x0DE10000); nothing draws into 0x0ED10000 between op646 and op683, so `pass_op0683_pre_rt_0ED10000(_a).bmp`
is the scene exactly as the encode read it. Burned pixels of the final frame (min channel > 235, rows 0-500 to
exclude the HUD): **scene alpha 43 / 70 / 255 (5th / 50th / 95th percentile), scene rgb median (255, 247, 204)**,
decoded value `rgb/255 / (a/255) / 4` = **0.91** (0.73 at the 25th percentile). Bright-but-not-burned pixels:
alpha median 125 (0.49), decoded 0.5. So the prediction holds: the panels burn because the scene alpha behind
them is 0.17-0.35 (<= 0.33 threshold), and the encode turns (255,247,204, a=70) into a value near 1, which the
tone map at E 2.3-3.9 maps to white. 81.6% of the scene has alpha 255; the low-alpha pixels sit on the lantern
rows (cells x 800-1040, y 180-240, alpha 80-90; x 0-80, y 60-120, alpha 68).
When is that alpha written? Scanning the pass dumps of 0x0ED10000 in op order: the left region's alpha 92 appears
between `op0221_pre` and `op0281_pre`, the lantern region's 79-98 between `op0281_pre` and `op0499_pre`: both
inside the **opaque stage geometry pass** (`blend=0 bk=0x0` draws, 1280x720), not particles or post. The alpha is
therefore the stage material's own output (emissive-intensity encoding), and the same shaders run on RPCS3. Whether
the *value* is right (vertex colour alpha, a material constant, or a texture alpha we decode wrong) needs the
draw that writes it: smallest test is a per-draw alpha dump of ops 281-498 into 0x0ED10000 (or `RSX_CAP_PASSES`
with a finer boundary), then `fp_opNNNN.hlsl` of the draw covering (900, 210) shows how its `.a` is formed.

## Pass 9 (2026-10-21 ~22:40): the emitter render function, and FP_GAIN_KEYS

### What `lumflag-trace-2` showed (the AI assistant, `color\yoshi_lumflag2`)
Both passes walk the same emitters (nodes 0x492BE130, 0x492BED70..0x492C1540), vtable 0x00884488, and call the same
render function **`func_00626790`** (`ppu_recomp_032.cpp` 48120). The only difference is the second argument:
RenderLuminance `r4=0x48433A20`, RenderOut `r4=0x4843A6E0`. That argument is not a particle-server object: the game
obtains it with `func_0052647C(renderer, [ctx+0x58], K, 0, [[ctx+0x58]+0x28])` right before calling the public
RenderLuminance (`func_0039C7FC`, `ppu_recomp_000.cpp` ~37500), i.e. it is a **render-pass / camera descriptor from
the renderer**, one per pass.

### `func_00626790(node, rctx, pass, layer, params)` decoded
- Returns at once unless node flags `[node+0x14]` have bit 0 and bit 3 set (both passes draw, so they are set).
- Copies four TOC vectors and the current view matrix (`func_00719A6C` on the render context's `+0x50` state) to
  the stack, then walks the node's element list `[node+0x98]` (next at `[elem+4]`).
- `params = server+0x18A50` is a filter: `[params+0] == 0` draws every element; `== 2` draws only elements whose
  id `[elem+0xC] == [params+4]`; `== 1` draws those whose id is in the list at `params+0xC` (count `[params+8]`).
  `func_003A2600` initialises it to `(mode, -1, -1)`; nothing in the particle module writes it per pass, so it is the
  same for both passes.
- For each element it calls **`func_0063A1AC(elem, rctx, [node+8], [node+0xC], m1, m2, global, pass, layer)`**
  (`ppu_recomp_033.cpp` 15745, 2.4k lines; `layer` goes as the 9th argument on the stack, `[r1+0x74]`).

### `func_0063A1AC`: the per-element draw
- `desc = [[[elem+0x1C]+0x14]+0x2C]`; **`mask = [desc+4]`, `type = byte [desc+8]`**.
- **`layer` selects the element type**: `layer == 1` draws elements with `type != 2`, any other layer draws only
  `type == 2` elements. The pass is a bit test: skip unless `mask & (1 << pass)`. So RenderOut's (0,1),(2,1),(3,1)
  draw the non-type-2 elements whose mask has bit 0/2/3, and RenderLuminance's (0,1),(2,1),(3,1) ask for exactly the
  same elements, plus (0,0),(2,0) for the type-2 ones. Nothing in this filter depends on the pass descriptor.
- The draws themselves are issued further down through `func_00631C24`, `func_00635BF0`, `func_00636664`,
  `func_00634968` or `func_006329BC` (all take `rctx` as an argument; `func_00635BF0` reads `[rctx+0x58]`).
  That is where the two passes can diverge: the pass descriptor decides the command stream the particles are written
  to, the camera, or whether the submit happens at all. I did not decode these (2.4k + lines); the trace below is
  cheaper and decisive.

**Answer to the question:** no per-emitter "luminance" flag or texture check exists between the virtual call and the
element draw; the element filter (type by layer, pass bit by mask) is identical for both passes. Either the
luminance pass's elements are all skipped by the mask (would need a different mask reading, impossible since the same
elements pass for RenderOut) or the submit functions do nothing / write elsewhere for the luminance pass descriptor.

**Smallest next test: `patches\color-exposure\lumflag-trace-3.patch`** (`ppu_recomp_033.cpp`, 6 log lines, applies in
`jstars\` with `git apply --ignore-whitespace`, independent of the earlier traces):
- `[LUMFLAG] elem-draw #n ctx= pass= layer= elem= desc= mask= type=` at the entry of `func_0063A1AC`, first 40 and
  every 2000th call **per distinct ctx value** (so the luminance ctx 0x48433A20 gets its own 40 lines even though the
  out pass dominates).
- `[LUMFLAG] func_00631C24 / func_00635BF0 / func_00636664 / func_00634968 / func_006329BC #n r3= r4= r5= r6=` at the
  entry of the five submit functions (first 40, then every 2000th).
Readings: `elem-draw` lines with `ctx=0x48433A20` and `mask` bits 0/2/3 set = elements reach the draw for the
luminance pass; then look whether any submit line carries `r4=0x48433A20` (or the same value in r3/r5). Submit lines
only with the out ctx = the luminance pass descriptor stops the submit (decode the branch in the submit function that
reads it, `func_00635BF0` reads `[rctx+0x58]` first). Submit lines with the luminance ctx = the draw commands are
produced and lost afterwards: next is the FIFO side (does the pass descriptor own a separate command buffer executed
via CALL? `RT_DRAWLOG` with BEGIN lines on 0x0E590000 plus a log of CALL targets in the FIFO parser).

### FP_GAIN_KEYS: why the hashes cannot be read off the existing files, and the fix
The AI assistant asked for the character program hashes. The data at hand cannot give them:
- `[FPGAIN]` lines (`color\fpgain\*\stderr.txt`) list each program once with `code=`, `instrs=`, `consts=`, but no
  draw, texture or target: nothing says which program drew what.
- The captures' manifests list every draw with textures and `fp=` (the microcode address), but the fp address is a
  per-draw copy (633 distinct addresses for 864 draws in `yoshi_passes_cap\frame_00`) and differs between runs, so
  it cannot be joined to the `[FPGAIN]` `fp=` values (21 of 633 match, by chance). `code=` appears only in the
  `fp_opNNNN.hlsl` headers, and `RSX_CAP_PASSES` writes those only at pass boundaries (230 files, none of them a
  character draw).
- The demo threshold screenshots (`fpgain\demo_e05_m150_fix\t150..t250.png`) are different moments of a moving scene;
  pixel differences between them say nothing about which program changed.

What the capture does show, and how the characters are told apart without hashes (`yoshi_passes_cap`, both frames):
- Character textures live in a separate heap region. Main-pass opaque draws into 0x0ED10000 with `t0 <
  0x02000000`: **71 draws, 67 of them with an index count that also occurs in the shadow-caster passes** (targets
  0x8CE40000 2048x1024 and 0x8CA30000 1024x1024, 77 + 87 draws). Stage draws (`t0 >= 0x0A000000`): 438, only 53
  coincidental matches. The stage does not cast; the fighters do.
- The 71 draws come in four groups, one per fighter: frame_00 ops 171-190 (20 draws, 6 textures, 30302 indices),
  200-216 (17, 6 textures, 30965), 332-350 (19, 5 textures, 17237), 356-370 (15, 11 textures, 21410); the same
  groups recur in frame_01 at ops 165-184, 188-205, 292-311, 372-392. Which group is Luffy / Gintoki / Goku (and the
  fourth) needs the per-group texture dump or a frame where only one fighter is on screen.
- Ace in the demo (`rsxcap_flush1\frame_00` ops 0-19): 20 draws of one program `fp=0x0FC97BC1`, n=762 each, textures
  0x0B998E80 / 0x0B94E300 / 0x0B605E00. That address is also a per-run value (in `rsxcap_skin` the same address is a
  150-instruction stage program), so Ace's code hash is unknown too.

**Fix: `patches\color-exposure\cap-code.patch`** (backend, 23 lines, applies to HEAD ba84d89 and the CRLF tree with
`--ignore-whitespace`): the capture manifest's `DRAW` line gets `code=0x%08X` appended (computed like the hlsl header,
`cellGcmResolveLocated` + `rsx_fp_code_hash`; appended last so `capops.py` keeps parsing). With it, one capture on
Yoshiwara and one on the Ace card give the keys directly:
```
FP_GAIN_KEYS = the distinct code= values of the DRAW lines into 0x0ED10000 whose t0 < 0x02000000
               (Yoshiwara: the four op groups above); Ace: code= of ops 0-19 of the demo capture
```
I will write the exact `FP_GAIN_KEYS=<hex,...>` line from that capture (a one-line script over the manifest).
Until then, `FP_GAIN_MININSTR` cannot separate characters from the hall: the stage programs run to 228 instructions
and the character ones start around 150, so every threshold either misses fighters or catches walls.


## FP_GAIN_KEYS picked (2026-10-21 ~23:10, from `color\capcode`, backend e0709b6)

**Method and evidence.** In every cap-code frame (Yoshiwara frames 10000/10300/10600, demo frames 6000-7400) the
opaque draws into the scene target 0x0ED10000 split cleanly by texture address: character textures live below
0x0A000000 (0x0090xxxx-0x03xxxxxx), stage textures at 0x0A..0x0B. The character set is 245 draws on Yoshiwara and
832 in the demo; **96% of them have an index count that also occurs in the shadow-caster passes** (targets 0x8CE40000
and 0x8CA30000), against 13% (Yoshiwara) / 3% (demo) of the stage draws: the fighters cast, the hall does not. **No
code hash is shared between the two sets in any frame** (0 overlaps over 11 frames), so the key list cannot touch a
wall. Yoshiwara shows the fighters as four op groups per frame (frame_00: 217-237, 390-404, 489-508, 529-550, plus a
fifth in frame_01 at 332-355), all recurring with the same codes in the three frames. The demo draws all three
characters in every frame (26-, 14- and 41-draw groups, order changes between frames 04 and 05), so the Ace card
frames contain Goku and Kumagawa too; the contact sheet only shows who the camera looks at. Two tiny programs were
excluded (`D89FC558`, 9 instructions on 30/42-index quads; `01BC2656`, 75 instructions on 6-index quads without a
shadow): decals, not surfaces. Instruction counts of the kept programs run from 75 to 230 while stage programs reach
228, which is why `FP_GAIN_MININSTR` could never separate them.

**Who is who.** Goku is certain: the 8 programs of the Yoshiwara group ops 390-404 are all in the demo's 26-draw
group (the only group shared between the scenes, and Goku is the only fighter in both). The other Yoshiwara groups
are Luffy, Gintoki and the assists; the demo's 14- and 41-draw groups are Ace and Kumagawa. Which is which does not
matter for the test (all keys go in), and can be read off a run with one group's keys if wanted.

**Lines for the AI assistant** (also in `color\capcode\fp_gain_keys.txt`). The union needs `fp-gain-keys.patch` (one
constant: key cap 32 -> 128, applies to e0709b6):
```
FP_GAIN_KEYS=7CA6AA8D,BF6E822D,80E6E9F4,E938CC97,0977D884,5514FF71,AB2346E1,8C9176DA,BF7046EB,6564C81B,C9E015CC,74EDE149,DBE6A3BF,9D8AB6E7,C519FBE2,F0913E66,7B66F8C1,25F1E778,51846477,5475D278,384CDFCD,4021CE5E,5F1B6FF9,E20A333E,C44C0A0B,7BC09A96,32E2C3F9,07712061,B62DF0B6,D8D32268,1AF4BB2A,B9EF0DF2,DCFF35BA,3A2BCAC8,BA4ACB56,61EE399E,83478126,6D978F58,2F097C94,AE282D6C,B60F5BDB,6579F038,9D449C77,24D163A9,04DE0F54,8321B9BA,4B6FF42C,FEA94DA7,F752CBF5,2F38926C,E90CEBF3,E3A973F7,9FF81C1D,E84844CA,28317FF6,60E0A340,B513C49B,2E24187A,131F7856,816E462F,E2CE6C7F,D334553C,9E806ACC,D5AC1583,0D0E400B,A931D894,16287CC3,09FDEED9,608569A8,1928159C,CAA0C0D7,F24CAE0A,73C3911D
```
Yoshiwara only (47 programs): `FP_GAIN_KEYS=E938CC97,80E6E9F4,E20A333E,7BC09A96,1AF4BB2A,B9EF0DF2,24D163A9,04DE0F54,BA4ACB56,2F097C94,5F1B6FF9,F752CBF5,C519FBE2,FEA94DA7,2F38926C,4B6FF42C,E90CEBF3,C44C0A0B,7CA6AA8D,E3A973F7,E84844CA,9FF81C1D,B513C49B,131F7856,28317FF6,D8D32268,B62DF0B6,32E2C3F9,07712061,2E24187A,60E0A340,16287CC3,AB2346E1,E2CE6C7F,D334553C,608569A8,9E806ACC,A931D894,1928159C,09FDEED9,0D0E400B,D5AC1583,816E462F,CAA0C0D7,BF6E822D,F24CAE0A,73C3911D`
Demo only (38 programs): `FP_GAIN_KEYS=7CA6AA8D,BF6E822D,80E6E9F4,0977D884,5514FF71,8C9176DA,AB2346E1,BF7046EB,6564C81B,DBE6A3BF,C9E015CC,74EDE149,E938CC97,9D8AB6E7,384CDFCD,51846477,C519FBE2,7B66F8C1,25F1E778,5475D278,4021CE5E,F0913E66,6D978F58,DCFF35BA,8321B9BA,B60F5BDB,D8D32268,32E2C3F9,C44C0A0B,07712061,B62DF0B6,5F1B6FF9,6579F038,9D449C77,AE282D6C,83478126,61EE399E,3A2BCAC8`
Goku only: `FP_GAIN_KEYS=07712061,32E2C3F9,5F1B6FF9,80E6E9F4,B62DF0B6,C44C0A0B,C519FBE2,D8D32268`

Suggested first runs: `FP_EXPOSURE=3 FP_GAIN_KEYS=<union> LUM_READBACK unset` on Yoshiwara (the look the AI assistant liked,
characters only, hall untouched), then `FP_EXPOSURE=5` and `FP_GAIN=1.5` for comparison; `FP_GAIN_LOG=1` prints
`(adjusted)` only on the listed programs, which confirms the selection without looking.

## Pass 10 (2026-10-21 ~23:45): the luminance pass does not "stop" - the glow draws are prepared and deferred

Read-only decode after the AI assistant's lumflag-trace-3 run (`color\yoshi_lumflag3`, FINDINGS §50). Short version: the
elements that produced no submit under a luminance context are the normal, non-glow children being skipped on
purpose; the glow-only children do pass the filter (40+ times during the fights) and are handed to the engine as
deferred draw records of the game's per-frame `pe::luminance` pass. What is still unknown is whether that pass's
records are ever executed and whether the backend drops the resulting draws. Two small patches answer that in one run.

### Correction first: module 033 uses TOC 0x8E3858

`ppu_recomp_033.cpp` (and 032) run with r2 = 0x8E3858, not the 0x8D3888 of the game modules (functions.json
`toc`). Every "[toc-0x....]" constant quoted for 032/033 in Passes 8-9 must be read from that base. Checked in
`EBOOT.elf`: `toc-0x5144` = 0.0f (the intensity threshold), `toc-0x5148` = FLT_MAX, `toc-0x50A0` = 0.5f,
`toc-0x513C` = 0x0096CF90 (pointer variable of the particle render state, "G" below), `toc-0x5138` = 0x00968F48
(pointer variable of the renderer, the same object `[toc019+0x3F9C]` RenderLuminance uses), `toc-0x5134` =
0x008BBE50 (a technique/vertex-format descriptor).

### 1. Luminance mode is a flag bit on the particle render state

- `G = [[0x0096CF90]]`. `G+0` is a flags word. RenderLuminance sets bit 0x2 at the top of its draw loop
  (`loc_003A4CC4`: `[G] = ([G] & ~1) | 2`, lum.txt 466-476); the normal passes run with bit 0x1.
- `func_0063A1AC` per child (`ppu_recomp_033.cpp` 16077ff, `loc_0063A63C`): with bit 0x2 clear it skips children
  whose header flag 0x1000 is set (`r8 = [[[child+0x14]+0]+0]`); with bit 0x2 set (`loc_0063B0FC`) it draws only
  children with flag 0x1000 whose intensity `f31 > 0.0` (`f31 = [[elem+0x28]+0x20]` if `byte[[elem+0x28]+0xB] & 8`,
  else `[[elem+0x2C]+0x14]`). So flag 0x1000 marks glow-only children: never in the scene, only in the glare target.
- This is exactly what trace-3 shows: `elem-draw ctx=0x48433A20 ...` (lum) for the first element and no
  `func_006329BC` afterwards, while the out context (0x48438BF0) with the same element calls `func_006329BC`
  three times. Not a bug: that element has no glow child.

### 2. Glow children take `func_00634968`, then the common submit

- At 17180 (`func_0063A1AC`): bit 0x2 set -> `loc_0063C0F4` -> `func_00634968(elem, matrix@stack+0x1E0)` with
  `f1 = f31`. It is the only caller. The function sets the shader state block (`renderer+0x50`): VP = `[G+0x2D0]`,
  FP = `[G+0x2F8]`, technique `+0x38 = 0x008BBE50`, a constant matrix (index `[[0x96B408]]`) and the constant
  `[[0x96B598]] = f31 * 0.5` (glow intensity), marks the dirty bits, and returns to `loc_0063B708`, the same path a
  normal child takes after `func_006329BC` (which picks VP/FP from the same tables by element type).
- Trace-3: `func_00634968 #0..#39` appear only in the fight part (LUMFLAG stream lines 241-307), r3 = element #9,
  #10, ... (element stride 0x978 from 0x4903B5B4). Between 40 and 2000 glow setups happened in the run. So the
  glow children exist and pass the filter in the port.

### 3. The submit is a deferred record, not a FIFO write

- `byte[desc+9]` selects the submitter: 1 -> `func_00636664`, 2 -> `func_00635BF0` (needs `[child+0x194] > 1`),
  otherwise the main path builds the vertices and, by `r25 = byte[[[child+0x14]+0x1C]+4]`, ends in
  `func_00719B1C` (r25 = 0 or 3 inline, 1 or 2 via `func_00631C24` first).
- `func_00631C24` is 15 instructions: a sort-key chooser (returns f1 = 0.0 or FLT_MAX from `[pass+0x28]` in {1,3}
  and r5). The 40 lines the AI assistant saw are the r25 = 1/2 menu-era elements; its budget was spent before the fight,
  so "no submit with a luminance context" was never tested for the glow children.
- `func_00719B1C` is a cross-TOC stub (`ppu_recomp_060.cpp` 29259) to `func_00526374` (`ppu_recomp_027.cpp`
  31266): allocate a `0x20 + extra` record from the renderer's frame allocator (`[renderer+0x8]+0x18`), snapshot
  the render state with `func_00528028(renderer+0x50)` into `rec+4`, key f1 at `rec+0x10`, sequence `rec+0xC`,
  and append it to the pass list `pass+0x10` (head) / `+0x14` (tail) / `+0x18` (count). The GPU commands are
  produced later by the engine when it executes the pass.
- Other stubs resolved while here: 00719A5C -> 00527B84, 00719A6C -> 00526D74, 00719A9C -> 005294E8,
  00719AAC -> 00527E84, 0071A01C -> 004AC550, 0071A17C -> 0071E200, 00717C8C -> 00702570 (the vcall thunk).

### 4. The pass is the game's per-frame `pe::luminance` child pass

- Game dispatcher `func_0002FAA4(?, stage)` (`ppu_recomp_000.cpp` 37456): stage 3 -> `func_0039C678(id)` (sets
  `G+0x298`, the owner id used by the `desc+0xE` filter), then `func_0052647C(renderer, parent=[renderer+0x58],
  "pe::luminance", 0, [parent+0x28])`, then `func_0039C7FC` (RenderLuminance) with the new pass. Stage 4 does the
  same with "pe::opaque" -> `func_0039C7B0`, stage 5 "pe::transparency" -> `func_0039C770`. Strings at
  `toc019-0x7AB0/-0x7AAC/-0x7AA8`.
- `func_0052647C` (027, 31339): a 0x40-byte pass: `+0x8` name copy, `+0xC` name hash (h = 1; h = (h + c) * 0x89),
  `+0x10..0x18` records, `+0x1C..0x24` child passes, `+0x28` = arg7, linked into the parent's child list.
  The hash constants (pe::luminance = 0xBDE8953E) appear nowhere in code, so passes are not looked up by name.
- pe::opaque / pe::transparency particles are visible in the port, so creation + record + execution works for
  those two children. The luminance child differs only in its records (glow shaders/technique) and its target.

### 5. What the capture says, and what it cannot say

- `func_003AC910` (the decay/feedback quad `src -> dst` of the pre-pass) records into `[renderer+0x58]` (the
  current pass), not into the pass RenderLuminance received; its target is set with `func_00527BE4`.
- `color\yoshi_passes_cap` frame_00/01: the only op on the glare target is `op676 CLEAR rt=0x0E590000`; no DRAW
  has `rt=0x0E590000` and no DRAW but the encode samples it. 0x0E590000 is 1280x720 A8R8G8B8 (fmt 0xA5), allocated
  by the backend as offrt[9] - an ordinary target.
- The manifest lists only draws that reached the D3D12 record stage: `d3d12_draw_arrays` / `d3d12_draw_indexed`
  return before recording when `rsx_no_color_target()`, `!pipeline_ready`, `count == 0` or `count > MAX`. So the
  empty manifest does not yet tell whether the FIFO carried the glow draws.

### 6. Smallest next test: `lumflag-trace-4.patch` (PPU) + `lum-rt-drawlog.patch` (backend), one Yoshiwara run

- `lumflag-trace-4.patch` (applies to the current tree with trace-3 in place; `jstars\` with
  `git apply --ignore-whitespace`): `ppu_recomp_033.cpp` `func_00634968` logs `[LUMFLAG] glow-setup #n elem G gflags
  vp fp intensity` and raises a flag; `ppu_recomp_027.cpp` `func_0052647C` logs `[LUMFLAG] pass-create name parent
  parent_name arg7 cur` for every `pe::*` pass, and `func_00526374` logs `[LUMFLAG] record pass name count key extra
  glow cur` for every record of a `pe::lum*` pass and for the record that follows a glow setup (`glow=1`).
- `lum-rt-drawlog.patch` (backend, applies to d89b94e and to the current working tree): with `LUM_RT=0E590000`
  every draw the FIFO hands `d3d12_draw_arrays`/`d3d12_draw_indexed` while the colour target is 0x0E590000 is
  logged as `[LUMRT] draw_arrays #n prim first count noct ready vb` before the guards.
- Reading: (a) `record ... name=pe::luminance ... glow=1` with a growing count = the glow draws are appended to the
  pass (expected). (b) `glow-setup vp=0 / fp=0` = the glow shaders are missing (then find who fills `G+0x2D0` /
  `G+0x2F8`; no write by that offset exists in 032/033, so it is filled through a table pointer). (c) `[LUMRT]`
  lines present = the engine executed the pass and the FIFO carried the draws; `noct=1`, `ready=0` or `count=0`
  names the backend guard that drops them, and the fix is in the backend. (d) records present, no `[LUMRT]` at
  all = the engine never executes the `pe::luminance` records; next step is the pass executor (not located yet;
  candidates in 027 that walk pass lists: `func_005266E0` via `func_00525354`, `func_00526838`), and a one-line
  PPU experiment is available: record the glow draws into `[renderer+0x58]` (where the pre-pass quad goes) instead
  of the child pass, by setting r4 = `[renderer+0x58]` before the `func_00719B1C` calls in `func_0063A1AC`.
- No candidate fix yet: the root cause is behind (c)/(d).

## Pass 11 (2026-10-22 ~00:45): the record executor, and why trace-4 misled

### 1. Trace-4 (color\yoshi_lumflag4) re-read: "one glow record" is a logging artefact

- 305 `record` lines, 303 of them before the first `glow-setup` (stderr line 13372). The record log stopped at
  `_n4 < 300` and then printed every 1000th record only; the cap was spent in the menu era on one
  `count=0 key=FLT_MAX cur==pass` record per frame (the first record of each new `pe::luminance` pass = the
  `func_003AC910` decay quad, recorded before any glow child). The single `glow=1 count=1` line (stderr line 15440)
  is a 1-in-1000 sample, so it proves glow records *are* appended, not that they are rare. `_n4` was not printed,
  which made the cap invisible; trace-5 prints `#n` on every line.
- `[LUMRT]` = 0 cannot be read yet: `run_test.txt` lists no environment, so `LUM_RT=0E590000` may never have been
  exported. The next run has to confirm the variable (print it in the bat, or `set LUM_RT=0E590000` right before
  the exe).

### 2. Records are executed at frame flush, by the renderer, with no per-pass filter

All of this is static reading of `ppu_recomp_027.cpp` (renderer, TOC 0x8E3858), `ppu_recomp_029.cpp` (GCM layer,
same TOC) and `ppu_recomp_033.cpp` (libgcm static functions), plus the EBOOT data.

- Renderer vtable `0x887658` (10 slots): `+0x0 func_00525FFC` execute pass, `+0x4 func_005260D8` prepare pass,
  `+0x8 func_00525CC4`, `+0xC func_00525C1C` (dtor), `+0x10 func_0052576C`, `+0x14 func_00525778` (nop),
  `+0x20 func_005261D4` (reset lists). The renderer holds 11 root passes at `renderer+0x10..+0x38`.
- Frame flush `func_0052577C(renderer, mode, roots)`: for the 11 roots `func_00525488` (prepare: `vt+4` on the
  pass unless mode==1, then recurse into `pass+0x1C` children through `+0x0 next`), then `func_00525538`
  (execute: mode 0 = `vt+0` then children; mode 1 = only zero the record list `+0x10/+0x14/+0x18`; mode 2 =
  execute, zero, children). There is no name, hash or flag test anywhere in this walk: `pe::luminance` is
  executed exactly like `pe::opaque`, as long as it hangs under a root at flush time (it does: child of
  `Alpha::object` under `OpGameMain::DrawEntryTable_Alpha`, Pass 10 §4).
- Prepare `func_005260D8(renderer, pass)`: `count=[pass+0x18]`; `count<=0` -> `[pass+0x2C]=0`, done. Otherwise
  an array of `count` pointers from the frame allocator (`[renderer+0xC]+0x18`), `func_00525DE4(array,
  [pass+0x10] head, count, [pass+0x28] sort mode)` sorts the records; its result -> `[pass+0x30]` (64-bit, a sort
  job handle), array -> `[pass+0x2C]`; result 0 -> `func_0071943C`.
- Execute `func_00525FFC(renderer, pass)`: if `[pass+0x30] != 0` wait in `func_00538670`; then for each sorted
  record: `snap=[rec+0x4]`, `extra=[rec+0x8]`, `fn=[snap+0x38]` (a function descriptor); **`fn==0` -> the record
  is skipped silently**; else `fn(rec, snap, extra)` and the return values are summed.
- Record layout (from `func_00526374`): `+0x0 next`, `+0x4 snapshot` (copy of the current state block
  `[[renderer+0x50]+0x10]` made by `func_00528028`), `+0x8 extra` (= rec+0x20), `+0xC seq`, `+0x10 key`.
- The command functions are the four descriptors in TOC words `0x8DED84..0x8DED90`: `0x8BBF10 func_00527C50`
  (clear, mask 0xF0), `0x8BBEF8 func_00527AF4` (clear, mask 3), `0x8BBE68 func_005270A8` (clear, mask 0xF3),
  `0x8BBE50 func_00527008` (draw). So the "technique 0x8BBE50" that `func_00634968` writes (Pass 10 §2) is the
  **draw command descriptor** stored at `state+0x38`, and `func_00527BE4` only builds the 16-byte clear parameter
  block (`{4, 1, 3, &colour}`) that a clear record points to. Each command calls `func_00529C60`,
  `func_00529ED0(snap)` (apply state: VP via stub `func_0071952C` -> `func_00575668` = cellGcmSetVertexProgram,
  FP, targets, ...), `func_00529E2C`; the draw command then calls stub `func_0071944C` -> `func_00572E60`.
- `func_00572E60(snap)` (029): walks the 8 stream batches at `snap+0x20..+0x3C`, each `{u16 prim, +0x4 attr,
  +0x8 count, +0xC index ptr}`; `count==0` falls back to `[[[snap+0x18]+0xC]+0xC]`; a NULL batch is skipped;
  otherwise cellGcmSetDrawIndexArray (`func_00657248`) or cellGcmSetDrawArrays (`func_00656F0C`). These two
  libgcm functions are the **only FIFO draw emitters in the whole EBOOT** (no other code builds methods
  0x1808/0x1814/0x1824, and none of the 18 lifted SPU jobs does either), and `func_00572E60` is their only caller.
  Every draw the backend ever sees comes through this one function.

### 3. What this means for the glare target

The clear on `0x0E590000` seen in `color\yoshi_passes_cap` is itself a clear *record* of the luminance frame, so
the pass tree is executed. The luminance draw records can therefore vanish at exactly three points:

- (a) `[snap+0x38] == 0` when the record was snapshotted -> `func_00525FFC` skips it (nothing logged, nothing
  drawn). Candidate fix if seen: make the vertex-build path write the draw descriptor (`0x8BBE50`, TOC033-0x5134)
  into the state block before `func_00719B1C`, as `func_00634968` does.
- (b) `fn` is the draw command but the 8 batch pointers are NULL or have `count 0` -> `func_00572E60` emits
  nothing. Candidate fix if seen: the vertex build for these records (`func_00631C24` / inline path, Pass 10 §3)
  does not attach its batches to the state block; compare with a `pe::opaque` record.
- (c) the draw is emitted and the backend drops it (`rsx_no_color_target`, `pipeline_ready`, `count`), which
  `lum-rt-drawlog.patch` shows as `[LUMRT]` lines, once `LUM_RT` is really exported.

Note that even the decay quad (first record of every `pe::luminance` pass, not a glow child) produces no draw in
the capture, so the cause is most likely at the pass/state-block level, (a) or (b), rather than glow-specific.

### 4. Smallest next test: `lumflag-trace-5.patch` (+ trace-4 and `lum-rt-drawlog.patch`), one Yoshiwara run

`lumflag-trace-5.patch` (applies to the current `jstars\` tree with `git apply --ignore-whitespace`; 027 and 029
are CRLF) logs, only while the executor runs a `pe::lum*` pass:

- `ppu_recomp_027.cpp func_00525FFC`: `[LUMEXEC] #n exec pass name count sorted job` once per executed pass, and
  `[LUMEXEC] #n record rec snap extra key fn code geo streams(8) vp fp flags` for every record *before* the
  `fn==0` test (`fn` = descriptor, `code` = its code address: `0x00527008` = draw, `0x00527C50/AF4/0A8` = clears).
- `ppu_recomp_027.cpp func_00527008`: `[LUMEXEC] #n drawcmd rec snap extra`.
- `ppu_recomp_029.cpp func_00572E60`: `[LUMEXEC] #n draw snap geo fallback_count batches:[i prim a cnt idx]...`
  or `batches: none` = no FIFO draw emitted.
- Caps: 200/400 lines, then every 100th; the counter is printed on every line so a cap can be seen.
- Reading: no `exec` lines -> the pass is not under a root at flush time (then log `func_0052577C` roots);
  `record ... fn=0x00000000` -> (a); `fn=0x8BBE50 code=0x00527008` with `streams=00000000 ...` or `draw ...
  batches: none` -> (b); `draw ... batches:[0 prim=.. cnt=N]` lines -> the FIFO carries the draws and the answer
  is in the backend (`[LUMRT]` must then appear; if not, `LUM_RT` was not exported).
- Run with trace-4 still applied so the `glow-setup` and `record` lines can be matched by `rec`/`pass` address,
  and with `LUM_RT=0E590000` confirmed (echo it in the bat).

No code change proposed yet; the fix is one line at (a) or a vertex-build fix at (b), and backend-side at (c).

## Where we stopped (2026-10-22 ~00:45) and how to resume

Parked by the maintainer on 2026-10-22: brightness, glare and per-character exposure tuning wait until near the end;
textures, shadows and other broken rendering come first.

- State: the glare target `0x0E590000` is cleared every luminance frame but never drawn into, so the glow/glare
  layer is missing and the HDR encode gets glare = 0 (Passes 7-11). The particle server does enter luminance mode
  and does append its glow records (Pass 10 §1-3, Pass 11 §1); the renderer's frame flush executes every pass
  without any filter (Pass 11 §2). The draw records of `pe::luminance` therefore die at one of three points:
  command descriptor 0 in the record's state snapshot, empty vertex batches in `func_00572E60`, or a backend guard.
- Patches ready, none applied or built yet by the AI assistant for this step: `lumflag-trace-5.patch` (PPU, logs the
  executor for `pe::lum*` passes), together with `lumflag-trace-4.patch` and `lum-rt-drawlog.patch`
  (`LUM_RT=0E590000`, must be echoed in the bat to prove it is exported). Expected: one Yoshiwara fight run, read
  `[LUMEXEC]` lines with Pass 11 §4; the fix is then one line on the PPU side (write descriptor `0x8BBE50` into the
  state block, or attach the batches) or a backend guard change.
- Unrelated loose ends of this file: per-draw alpha dump of ops 281-498 for the burned panels (Pass 9), and the
  FP_EXPOSURE/FP_GAIN runs with the union key list. The user also wants character-only brightness later, which
  needs per-character tuning (menu Brightness slider notes in Passes 5-6).
- Notes for whoever resumes: patches README rows list every patch in order; FINDINGS.md §38 has the one-paragraph
  history per pass.
