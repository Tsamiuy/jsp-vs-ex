# Texture packs (custom / upscaled textures): plan (2026-10-27)

Read-only study. Nothing was edited or built. Code references are `ps3recomp-src` branch
`jstars-port` at **29906e8**, file `libs/video/rsx_d3d12_backend.c` (the AI assistant owns it).
Goal: a Dolphin/RPCS3-style "dump textures, edit or upscale them offline, load them back"
feature, usable for J-Stars' environments now and for the next game (Gundam Reborn) later.

**Short answer:** it fits our renderer well. Every guest texture the J-Stars draws sample goes
through one function, `vp_upload_tex_slot_impl` (line ~4940). It already decodes guest memory into
a D3D12 texture and caches it per slot with a change checksum. A dump and a swap-in can both sit
at its "new or changed texture" branch. Larger replacements need no shader change, because the
fragment programs sample with normalised coordinates. The one exception, UNNORM textures, is
scaled by the guest size, which we keep. Estimated **~1,200 lines in 5 steps, about 4-5 working
days of code**, mostly in a new backend-independent file. Only a ~40-line hook goes into
The AI assistant's file.

---

## 1. How textures flow today (what the plan builds on)

| Stage | Where | What happens |
|---|---|---|
| Bind | `vp_upload_tex_slot()` (~5621) | Per-batch memo: a texture already resolved in this batch returns its slot at once |
| Lookup | `vp_upload_tex_slot_impl()` (~4940) | Scan of up to `TEX_SLOTS` (default 1024, max 2048) `VPTexSlot`s, keyed on **guest offset + w + h + RSX format + cube** (`TEX_HINT` starts the scan at the remembered slot) |
| Change check | same, `tex_csum()` (~4769) | **Sparse** FNV-1a (about 1,024 samples) of the guest bytes. Same: reuse (`csum-same`). Different: re-upload (`reupload`) |
| Slot choice | `tex_scan_candidates` / `tex_pick_slot` (~4840) | `TEX_POOL` (default on): prefer an unused slot whose D3D resource already has this w/h/fmt |
| Layout | `rsx_texture_layout_pitched()` (rsx_texture_layout.c) | Format class, row bytes, swizzled vs linear, DXT block rows; `rsx_texture_mip_chain()` for levels |
| Format map | ~4975 | Decoded format to DXGI: RGBA8, RG8, **BC1/BC2/BC3** (DXT kept compressed), R16, RG16, RG16F, RGBA16F, R32F, RGBA32F, R8 |
| Create | ~5185 | `CreateCommittedResource` x2 (DEFAULT texture + UPLOAD buffer), only when the slot's resource has another size/format/levels |
| Decode | `rsx_texture_decode()` (~5273, per face; ~5538 per mip) | Deswizzle (Morton), channel order, packed 16-bit formats to RGBA8, DXT block copy, written straight into the mapped upload buffer |
| Copy + view | ~5545-5610 | `CopyTextureRegion` per face/level, barriers, SRV at heap slot `1 + slot` (`MipLevels = levels`; B8 gets an R,R,R,R swizzle) |
| Shader side | `rsx_fp_decompiler.c` ~500-545, backend ~5679 | Sampling uses normalised UVs. RSX UNNORM textures (fmt bit 0x40) get `uv = coord / size` with **size = guest w,h** from the constant buffer b1 |

Not covered by this path, which is fine: render targets sampled as textures (`RT_SRV_BASE`,
zeta views, screen-as-texture, shadow passes). They are produced every frame and are not art.

Measured context (CPU_PROFILE.md): one texture create + upload costs **~0.7-1 ms** on the drawing
thread. Kameari had 3,462 creates in a window, and bursts of 300-600 freeze the frame for 0.2-0.6 s.
Any feature that adds creates must keep them off the drawing thread or ahead of time (§5).

## 2. Where to hash and dump

**Hook point:** in `vp_upload_tex_slot_impl`, right after `s_txo` becomes `TXO_NEW` or
`TXO_STALE` (~5148), i.e. only when the bytes are new or changed. Never on the per-bind fast
paths (`memo`, `bound`, `csum-same`), so the cost of a cached frame is unchanged.

**Hash:** a **full** 64-bit hash (XXH3-64, single vendored header; or FNV-1a-64 if no new dependency is
wanted) of the **raw guest bytes of level 0**: `dxt ? blkrow*blkrows : w*h*bpp` bytes at
`vm_base + off`, plus `w`, `h` and the base format `fmt & 0x9F`. Why raw guest bytes:
- The hash doesn't depend on our decoder. A later decode fix (pitch, 16-bit formats, gamma) doesn't
  rename every file in a pack.
- It doesn't depend on the guest address. J-Stars streams textures to different offsets per stage
  and per load, so an address key would miss.
- The sparse `tex_csum` stays the cheap per-bind change detector. The full hash runs only on
  new/changed uploads. XXH3 runs at about 10+ GB/s, so a 1 MB texture costs ~0.1 ms.

Excluded from dump and replace: all-zero or single-value images (STAGE_TEXTURES.md: art whose VRAM is
still zeros would all share one hash), cube maps (step 5), and float/depth formats (data, not
art). Textures that change often are rate-limited (movie planes, font atlas, CPU-written
loading snapshots, FINDINGS 69): a texture whose hash at the same key changes more than N times is
marked dynamic and not dumped again.

**What to write (`TEX_DUMP=1`):**
- The decoded image is already in the mapped upload buffer, so no extra decode is needed. Copy it
  out and hand it to a **writer thread**, so the drawing thread never touches the disk.
- `<hash>_<w>x<h>_<ff>.dds`, keeping the exact decoded format: BC1/2/3 stay compressed and lossless,
  RGBA8/RG8/R8 uncompressed. DDS is header + bytes, so no image library is needed.
- With `TEX_DUMP_PNG=1`, also a `.png` of the RGBA8 (BC decoded by the same code as
  `tools\texbin.py`) for browsing and upscaling.
- One line per texture in `texdump\<TITLE>\dump.csv`: hash, w, h, RSX fmt, swizzled/linear, mip
  count, guest offset, first frame, binds so far, and a free tag from `TEX_DUMP_TAG` (e.g. the
  stage name set by the run script). That tag is how we pick "environment of stage X".
- Dumped once per hash per run (a set like `tex_seen_before`).

Channel convention: the dump is **after** our channel-order decode and **before** the
CONTROL1 remap and sRGB decode, which happen in the shader. A replacement must be authored in
the same convention as its dump. Simply editing or upscaling the dumped file keeps it correct.

## 3. Where a replacement is swapped in

Same hook point. When `TEXPACK=1` (on when a pack folder exists) and the full hash is in the pack
index:
1. Create the slot's resource with the **replacement's** width, height, format and mip count
   (from the DDS header) instead of the guest's.
2. Copy the replacement's bytes (from the RAM cache, §5) into the upload buffer with
   `GetCopyableFootprints`, the same way the TEX_MIPS path places levels now.
3. Create the SRV with the replacement's `MipLevels` and the same component mapping rules.
4. Mark the slot `replaced = 1`, `rep_hash = hash`.

Slot bookkeeping must change a little (`VPTexSlot`):
- `w/h/fmt/levels` stay the **guest** values, because they are the cache key and the b1 UNNORM
  scale reads guest sizes from the draw record. New fields `res_w/res_h/res_fmt/res_levels`
  describe the actual D3D resource.
- The "recreate if dims differ" test (~5181) and `TEX_POOL`'s fit test must compare the
  **resource** fields, or a pooled slot would reuse a 2048x2048 BC7 resource for a 512x512 guest texture.
- `t->csum` stays the guest checksum. If the game rewrites the texture, the replacement is dropped
  and the new hash is looked up again.

## 4. Larger sizes and other formats

| Case | Handling |
|---|---|
| 2x / 4x larger | Works without shader changes: normalised UVs, and UNNORM uses guest size. Keep the **same aspect ratio**, integer multiples recommended. The loader warns on a mismatch and uses it anyway |
| Mips | Replacement DDS should carry a full chain (`texconv -m 0`). If it has one level, the loader can generate a box-filtered chain on the CPU at load time (in the worker, not the draw thread). Without mips, an upscaled texture shimmers worse than the original (TEX_MIPS history, Namek) |
| Formats | Accept **BC7** (best quality per byte, recommended), BC1/BC3 (smaller), RGBA8 (exact). Always **UNORM, never _SRGB**: the shader already applies the guest's gamma bits (TEX_GAMMA). An _SRGB file would double it |
| Alpha | Kept. Alpha-tested foliage (`MENU_A2C`/alpha test) needs crisp alpha. Upscale alpha separately (§7) |
| Different format than guest | Allowed (e.g. DXT1 guest to BC7 replacement); the SRV takes the replacement's format. Exception: replacing a B8 (single channel, R,R,R,R swizzle) needs a single-channel file (R8 or BC4) or the swizzle must be applied to RGBA8 too, so the loader keeps the guest's component mapping |
| Cube maps | Step 5 (six faces as a DDS cube). Skipped at first |
| NPOT / pitched linear | Hash covers the guest bytes as read with the pitch. The replacement is just an image |

## 5. Memory, VRAM and load-time cost

Sizes per texture, with a full mip chain (x1.33):

| Original (typical J-Stars env) | Bytes now | 2x as BC7 | 4x as BC7 | 4x as RGBA8 |
|---|---|---|---|---|
| 512x512 DXT1 | 0.17 MB | 1.4 MB | 5.6 MB | 22 MB |
| 256x256 DXT5 | 0.09 MB | 0.35 MB | 1.4 MB | 5.6 MB |

So **2x as BC7 is about 8x the VRAM of the original DXT1 art, and 4x is about 32x.** If one stage's
environment has ~300 textures in view (Kameari has >512 of all kinds, CPU_PROFILE §24), 2x BC7 is
~0.4 GB and 4x BC7 ~1.6 GB of VRAM, plus the same in RAM if preloaded. **Recommendation: 2x BC7 by
default; 4x only for large hero surfaces (floors, walls, sky).** A `TEXPACK_BUDGET_MB` cap stops
replacing and falls back to the original once the budget is used.

Load time and hitches:
- Disk: 0.4 GB at ~1-2 GB/s SSD is ~0.3 s; BC7 files need no decode.
- The real cost is the measured **0.7-1 ms per create + upload on the drawing thread**. Replacing a
  burst of 300 new textures with bigger ones would add to the existing freezes.
- So: (a) **preload** the pack index and file bytes into RAM at start (or per stage tag); (b) on
  a hit, draw this batch with the **original** texture and let a worker create and copy the replacement
  (D3D12 resource creation is thread-safe; the copy goes on a copy queue). The slot switches to it on a
  later batch, like Dolphin's async loading. The first frame of a new texture shows the original,
  which is invisible in practice.
- Slots: replacements live in the same 1024 slots. If VRAM gets tight, LRU eviction frees them;
  re-creation then comes from RAM, not disk.

## 6. File layout and naming

```
<port>\texpacks\BLUS31519\            one folder per title id (PARAM.SFO TITLE_ID)
    pack.json                         {name, author, version, scale, notes} (optional)
    any\sub\folders\you\like\         loader scans recursively; only the file NAME matters
        9f3c1a2b44e0d871_512x512_86.dds
<port>\texdump\BLUS31519\
    dump.csv
    9f3c1a2b44e0d871_512x512_86.dds   (+ .png with TEX_DUMP_PNG=1)
```

Name = `<hash64 hex>_<guest w>x<guest h>_<RSX base fmt hex>` + extension. Guest size and format in
the name make collisions practically impossible and make files sortable by type (86 = DXT1, 88 = DXT5,
85 = A8R8G8B8). Subfolders (e.g. `stages\s01_kameari\`) are for people, so packs can be organised by
stage without changing any code. Gundam Reborn would simply be `texpacks\BLES02057\`.

Switches (proposal): `TEX_DUMP=1`, `TEX_DUMP_PNG=1`, `TEX_DUMP_TAG=<text>`, `TEXPACK=0/1`
(default 1 if the folder exists), `TEXPACK_DIR=<path>`, `TEXPACK_BUDGET_MB=<n>`, `TEXPACK_HILITE=1`
(tint replaced textures, to check coverage), and a debug-console command `texpack on|off|reload`
for A/B comparison without restarting.

## 7. Offline AI upscaler workflow

1. **Dump** one stage: run the tour with `TEX_DUMP=1 TEX_DUMP_PNG=1 TEX_DUMP_TAG=s01`.
2. **Pick**: `texpack_list.py` (new) reads `dump.csv` and makes a contact sheet per tag. It sorts out
   textures that must **not** be AI-upscaled: fonts and UI (upscale with a sharp, non-AI filter or
   leave alone), lookup tables, ramps and gradients (1xN, 256x1), noise, shadow/light maps,
   normal maps (if any; check by colour profile), tiny textures (< 32 px).
3. **Upscale** environment art with an offline model (e.g. Real-ESRGAN / chaiNNer / Upscayl,
   which run locally on the GPU). Tiling textures: pad by wrapping before upscaling and crop after,
   or seams appear. **Alpha** separately: upscale the alpha channel as greyscale, then threshold it for
   alpha-tested foliage so the edge stays crisp. Common practice is 4x then downscale to 2x for
   cleaner results.
4. **Convert** with Microsoft's `texconv` (DirectXTex): `texconv -f BC7_UNORM -m 0 -y -o <out> <png>`.
   Keep the file name. Never pick an `_SRGB` format.
5. **Check** in game: `TEXPACK_HILITE=1` shows which surfaces are replaced. Use `texpack off/on` in the
   console for before/after screenshots (`imgdiff.py`, RPCS3 references in `reference\`).
6. **Iterate** per stage. The pack is plain files, so it can be versioned or zipped separately.

Packs contain art derived from the game: keep them private, like the ISO.

## 8. Work plan and size

Code split: a new **`libs/video/rsx_texpack.c/.h`** (hash, index scan, DDS read/write, writer and
loader threads, RAM cache, budget). It knows nothing about D3D12, so it is reusable for Gundam
Reborn and the Vulkan/Metal backends, and a thread that does not run the game can write and unit-test it. Only
the **hook** (~40 lines) and the `VPTexSlot` fields go into `rsx_d3d12_backend.c`, handed to
The AI assistant as a patch per the project rules.

| Step | What | Size | Time | Proof it works |
|---|---|---|---|---|
| 1 | **Dump**: full hash, writer thread, DDS + optional PNG, `dump.csv`, dynamic-texture filter | ~350 lines (+ vendored xxhash header) | 1 day | Dump of one stage; count of distinct textures per stage (also answers the VRAM question with real numbers); fps unchanged with `TEX_DUMP=0` |
| 2 | **Replace, same size/format**: index scan, DDS parse (BC1/2/3/7, RGBA8, mips), hook, slot fields | ~300 lines | 1 day | Recolour one wall texture by hand in its DDS; it shows in game with `TEXPACK_HILITE` |
| 3 | **Larger sizes + BC7 + mips**, resource-dim fields in recreate/pool tests, UNNORM check | ~120 lines | 0.5 day | A 2x upscale of one stage; no stretched/misaligned UVs; screenshot vs RPCS3 |
| 4 | **Preload + async creation + budget + console toggle** | ~250 lines | 1 day | Kameari [PERF3] creates and hitch count (hitchsum.py) not worse than without the pack |
| 5 | **Tools**: `texpack_list.py` (contact sheets, data-texture filter), `texpack_build.ps1` (texconv batch), docs; cube maps later | ~300 lines Python/PS | 0.5-1 day | One full stage pack built end to end |

**Total ~1,200 lines, ~4-5 days of code**, plus however long the art takes. Each step stays behind
its own switch with the default off until it proves itself, as usual.

Open questions for the maintainer:
- The GPU's VRAM size, which decides 2x vs 4x.
- Which stage first. Suggestion: a stage whose environment is mostly static DXT art and already
  renders correctly (e.g. s07 Hidden Leaf), not one with open texture bugs (Namek grain, pale/neon
  surfaces). Upscaling a wrong texture only makes it more visibly wrong.
- Whether to finish the open texture fixes first (STAGE_TEXTURES.md, PALE_TEXTURES.md). Recommended,
  because a hash is taken from guest bytes and doesn't change with those fixes, but judging the upscaled
  look needs the correct colour pipeline.

---

## 9. Effects (explosions, skill effects, particles, smoke) — added 2026-10-27

The maintainer asked whether the pack can also sharpen effects. **Partly.** Effects look low-resolution for
two separate reasons, and a texture pack only fixes the first:

1. **The effect textures are small** (sprite sheets, masks, smoke puffs). A pack can replace them,
   with the special handling below.
2. **Many effects are drawn into reduced-size layers and then composited**, which no texture
   replacement can fix. Facts from VFX.md: the distortion pass draws into the **640x360** grey layer
   `0x0DB30000`, the effect alpha layer `0x0DC20000` sits next to it, the glow layer `0x0E590000`
   is currently never drawn (glow missing), and there is a quarter-res sky/bloom layer
   (STAGE_TEXTURES §10). Even a 4x texture lands in a 640x360 target and gets stretched to 1280x720.
   This needs a different feature (§9.3).

### 9.1 How effect textures differ from environment art

| Property | Effect textures | Consequence for the pack |
|---|---|---|
| Blending | Additive (VFX.md: blend key 0x122088B = SRC_ALPHA·src + ONE·dst), alpha-blended, reverse-subtract, alpha-only writes (cmask=8) | Black or transparent surroundings matter: any grey haze an upscaler adds becomes a visible glowing square |
| Alpha | Soft, carries the shape (`out.a = tex0.a × col0.a × 2`) | Alpha must be upscaled as carefully as colour, and smoothly (not thresholded like foliage) |
| Size | Often 32-128 px | AI models do poorly on tiny inputs. Use 2x-4x with a model trained on soft/painted art, or redraw/regenerate by hand. Below ~32 px keep a classic filter |
| Atlases / flipbooks | One sheet with a grid of animation frames, selected by UV | Upscale **per cell** with padding, never the sheet as one image, or frames bleed into each other and the animation flickers. The grid must stay exactly aligned: integer scale only |
| Single-channel masks | B8/L8-style (`0x81`, shown R,R,R,R by our SRV swizzle), DXT5 alpha-only | The replacement must stay single-channel (R8 or BC4), or the loader keeps the R,R,R,R mapping. Plan change below |
| Data textures | Distortion offsets (neutral = 0.5 grey, as in the 0x0DB30000 layer), noise, gradient ramps, colour lookups | **Do not AI-upscale.** These are numbers, not pictures. Use plain bicubic/Lanczos or leave them alone |
| Colour range | Saturated, sometimes HDR-encoded in the shader (`× 2`, saturate) | The upscaler must not shift colours or clamp highlights. Compare averages before and after (the tool can check) |
| Mips | Effects are often small on screen | Generate mips with **premultiplied alpha**, or edges get dark halos as they shrink |
| Generated at runtime | Glow, distortion, bloom and blurred copies are render targets; some trails/ribbons are built per frame | Not on the texture path at all (§1), so the pack ignores them. Correct, since there is nothing to replace |

Effect textures that the game uploads once and then only samples are caught by the hash like any
other texture. CPU-animated ones are caught by the dynamic-texture filter (§2) and simply not
replaced. Flipbooks don't need that, because they animate through UVs over a static sheet.

### 9.2 Does a plain upscale work?

**Only for the larger, opaque-ish effect sheets.** For most effects use an alpha-aware workflow:
1. Split colour and alpha. With additive textures, colour × alpha is what shows; upscale the
   **premultiplied** image (colour × alpha) and alpha separately, then divide back (or ship
   premultiplied only if the shader expects it; ours doesn't, so divide back).
2. Upscale alpha as greyscale with a model or filter that keeps soft gradients (no sharpening halos).
3. Flipbooks: cut into cells by the grid, pad each cell with transparent/edge pixels, upscale, crop,
   reassemble. The tool records the grid (from the UVs the draws use, or by hand in a small JSON).
4. Clamp: transparent or black background stays exactly 0 after upscaling (upscalers add faint
   noise there, which additive blending turns into a visible box).
5. Mips with premultiplied alpha, then BC7 (or BC4 for single-channel masks). BC1 is unsuitable
   for soft alpha.

### 9.3 Effect layers at higher resolution (separate feature, the AI assistant's file)

To make the reduced-size layers sharp, the backend would create those offscreen targets
(0x0DB30000 640x360, 0x0DC20000, the glow and quarter-res layers) at 2x or 4x of their guest size and
scale viewports, scissors and the texel-size constants that sample them. RPCS3 and Dolphin call
this "resolution scaling". It touches the render-target and copy paths, not the texture upload,
so it is a **render-backend change** of medium-large size and risk (REUSABLE.md §8 describes those
substitution paths). Do it **after** the missing glow pass works (VFX.md), or we would be sharpening
an incomplete image. Rough size: 300-600 lines, 2-4 days, with a per-layer on/off switch.

### 9.4 Changes to the plan

- **Step 1 (dump):** `dump.csv` also records, per texture, the **draw context it was first seen in**:
  blend key, target RT address and size, and whether the bound unit is single-channel. The upload
  function doesn't know the draw, so the caller passes the draw record. About +30 lines. Then the
  tool can sort effect textures from environment textures automatically (blended into an effect layer
  = effect).
- **Step 2/3:** support single-channel replacements (R8, BC4) and keep the guest's component mapping
  for B8 units. About +40 lines.
- **Step 5 (tools):** an **effect mode** in `texpack_list.py`: detect flipbook grids, split and
  reassemble cells, premultiply/unpremultiply, clamp backgrounds, premultiplied mips, BC4 for
  masks, and flag data textures (distortion, ramps, noise) as "do not AI-upscale". About +250 lines.
- **New optional step 6:** effect-layer resolution scaling (§9.3). The maintainer's call, after the glow fix.
- Order unchanged: environment first, because it is simpler and proves the pipeline. Effects come
  after, once the dump has tagged which textures are effects.
