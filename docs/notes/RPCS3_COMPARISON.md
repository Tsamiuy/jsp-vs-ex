# RPCS3 comparison: what to capture and why

Written 2026-10-20 (notes only; RPCS3 and the game were not run for this file). Purpose: RPCS3
already runs J-Stars correctly, so it is the reference for what our D3D12 port should look like
and how fast the game can go. Use it for the open problems: shadow maps (FINDINGS §29 and
"Shadow passes round 2"), pale / blotchy skin (`PALE_TEXTURES.md`), and speed (§29-30).

> **Limit of the reference (user, 2026-10-20):** RPCS3's compatibility list rates J-Stars as
> **"Ingame"**, not "Playable". Not everything works there, so the emulator is a reference only for
> the parts that run: the intro cards, the title and the demo. A capped 30 fps in RPCS3 is not a
> speed ceiling. First shots are indexed in `reference/rpcs3/INDEX.md`, with the first comparison in
> `reference/rpcs3/COMPARISON_ace_intro.md`.

## Ground rules
- RPCS3 lives in `RPCS3\` (0.0.43, firmware installed as RPCS3 documents). The EBOOT is taken from your own copy of the game; for how to obtain the decrypted file, see RPCS3 and its documentation.
  How the reference run was started: DEBUGGING.md "Reference run in RPCS3". The debugger needs the GUI instead of `--no-gui`.
- Use the **same boot path** as our runs: no save data, one CROSS press early (our `PAD_SCRIPT=31:0x4000`),
  then leave the title idle until the demo match starts (~190 s on ours; RPCS3 will differ, so note
  the time of each scene).
- RPCS3 renderer: Vulkan, resolution scale 100 %, native 1280x720, **same window size as ours**
  (1280x720). Record the settings used in `settings.txt` (renderer, resolution scale, anisotropic
  filter, "Write Color Buffers", "Read Color/Depth Buffers", frame limit off, SPU decoder).
- Take screenshots with RPCS3's own capture or Windows Snipping at identical window size. Name:
  `<scene>_<what>_<yyyymmdd>.png`.
- Save everything under **`jstars-notes\reference\rpcs3\`** (subfolders `shots\`, `debugger\`,
  `perf\`), plus a one-line entry per file in `reference\rpcs3\INDEX.md` (what, how taken, setting).

## 1. Matching screenshots (same scenes as our captures)
| Scene | Our reference | What to compare |
|---|---|---|
| Title screen ("Press any button") | `title_screen.png` | text, logo, background colours |
| Intro card (character intro before the match) | `demo_after_*.png`, `run\shots_nbatch\shot_300s.png` | overlay, card colours, fonts |
| Idle demo match, wide shot | `run\shots_nbatch\shot_300s.png`, `demo_after_strip_restart.png` | stage, water, sky dome, characters |

Take 3 shots per scene a few seconds apart (the scene moves) and note elapsed time.

## 2. Close-ups on the known problems
- **Ace (skin, hat, beads), close.** Reference for the pale-cream-with-blotches look
  (PALE_TEXTURES.md): is the skin tan/orange in RPCS3? Are there any sharp-edged dark blotches?
  If RPCS3 also shows soft shading there, the blotches are ours.
- **Shadows on Ace and on the stage.** Note whether a shadow of a character appears on the floor,
  its softness and direction, and any self-shadowing on faces (FINDINGS: "skin blotches are not
  shadows" in our captures, so check RPCS3's skin for the same pattern).
- **Skin / material colour sample.** Use a colour picker on three fixed points (cheek, hat, floor)
  in each of our shots and RPCS3's; record RGB in `reference\rpcs3\colours.csv`. Differences point
  at gamma (PALE_TEXTURES cause 1) versus lighting.
- Sky dome and water edge (we fixed cracks/gaps in §28; confirm they match).

## 3. Speed baseline
Our numbers are in backend batches/s (6-11 with verbose logging, **~18-20 with `PS3_VERBOSE=0`**,
FINDINGS §29-30); `flips` come from the `stat` console command. Batches are not frames, so also
record the flip rate on our side for a fair comparison.
- RPCS3 overlay: Config > Advanced > "Show performance overlay" (FPS, frame time, CPU and GPU).
- Record fps and frame time for: title, intro card, demo match (30 s average each).
- Record CPU load per thread (PPU, SPU, RSX) from the overlay or Task Manager, and the SPU
  decoder and PPU decoder used. Our bottleneck is the guest PPU/SPU side plus vertex fetch (§29),
  so RPCS3's per-thread split is the useful part.
- Save as `perf\speed.md`: a table of scene / fps / frame time / settings.

## 4. RPCS3 debugger views and what they answer
Open the debugger (GUI), then the **RSX debugger / Frame debugger** (captured frame) and the
**Memory viewer**. Capture a frame during the demo match (RSX debugger "Capture frame", or
Config > Advanced "Frame debugger").
1. **Shadow maps (our open problem).** In the captured frame find the depth-only passes that write
   the two shadow maps (colour target NONE, zeta set; ours: `0x0CE40000` 2048x1024 and
   `0x0CA30000` 1024x1024, a third `0x0D940000` 640x360). Questions to answer:
   - Do the casters land in the map (is it non-empty)? What do the depth values look like?
   - How do the character material draws sample them? Ours: units 7/8 as **format 0xA5
     (A8R8G8B8)** with remap `0xAA93` and `zfunc 4`, i.e. a depth map read as A8R8G8B8 with a hardware
     compare; the sky-pass FP `0xFBFFFC1` binds them as format `0xB0`. RPCS3 should show how it
     treats the A8R8G8B8-looking view of a depth buffer (its depth-to-RGBA conversion plus compare).
   - Save the shadow-map image and the material draw's texture list (formats, remap, wrap/filter).
2. **Texture formats and swizzle.** For an Ace material draw list each bound texture's format
   (we see DXT1 `0x86`, DXT5 `0x88`, swizzle flag, 128-2048 px), the gamma bits, wrap mode and
   filter. Answers PALE_TEXTURES causes 1, 3 and 5 directly, since it shows the real values.
3. **Gamma.** In the texture settings check the sRGB/gamma bits per unit on albedo DXT (ours would
   show `(address >> 20) & 0xF`). Note whether the final tone-map pass and the output colour
   encoding are in the draw list (ours: `pow(x, 0.4545)`, FINDINGS §27).
4. **Render targets.** Record the offscreen target list and sizes (ours: 17 RTs per match frame,
   `0ED10000` main, `0C6ABF40` HUD composite); the order of post passes.
5. **Fragment program** of an Ace material draw (RPCS3 shows the decompiled shader). Compare with
   `jstars\run\duck_fp.hlsl` for the PCF taps (t7/t8) and the normal-map reads (t3/t4).

Save screenshots of each debugger panel in `debugger\` with the draw/call number in the name.

## 5. What to bring back
A short `reference\rpcs3\FINDINGS_RPCS3.md` (or add to `INDEX.md`) answering: (a) do shadows
appear in RPCS3, and how; (b) texture formats / gamma / wrap bits of Ace's material; (c) fps
baseline; (d) any scene where RPCS3 differs from our render. Then link them from STATUS.md.

## Sources
FINDINGS §17, §27-30 and "Shadow passes round 2"; PALE_TEXTURES.md; `patches\shadow-fix\README.md`;
DEBUGGING.md "Reference run in RPCS3" and "Rendering".
