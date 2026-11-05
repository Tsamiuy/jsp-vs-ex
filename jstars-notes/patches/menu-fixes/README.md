# Menu and demo-stage glitches: causes and three opt-in fixes

Patch: `menu-fixes.patch`, on top of 283ad47 (`git apply --check` passes; the main checkout was
clean). It touches `rsx_d3d12_backend.c`, `rsx_commands.c/.h` and `rsx_texture_layout.c`. A
syntax-only compile with the build's warnings shows no errors. Not built or run. Every fix is off
unless its switch is set.

Screenshots: `jstars-notes\menu\` (`main_menu.png`, `charsel_1.png`, `demo_naruto_1.png`). There is
no capture of these screens yet, so the causes below are inferred from the pictures and the code.

## 1. Naruto forest: leaves drawn as black-rimmed cards (most confident)

- **What it looks like:** every leaf card is drawn whole. Its transparent area is solid black,
  around correctly coloured leaves.
- **What that means:** this is DXT1 punch-through alpha. Transparent texels decode to black with
  alpha 0, so the cut-out step is missing.
- **Why it is missing:** the alpha test works in this backend (other cut-outs render).
  PS3 foliage usually cuts out with **alpha-to-coverage** instead
  (`SET_ANTI_ALIASING_CONTROL` 0x1D7C, bit 4). That register is never decoded, and the backend
  renders one sample, so coverage never drops.
- **Fix, `MENU_A2C=1`:**
  - decodes 0x1D7C;
  - when alpha-to-coverage is on and the game's own alpha test is off, applies an alpha test
    `alpha >= 0.5`, which is what one-sample coverage means;
  - uses the existing per-draw alpha-test path, so no new pipelines.
- This may also explain the remaining black shards on the Goku card's blue plants. The
  `FP_FACING` patch is the other candidate there.

## 2. Main Menu / Free Battle: top-right logo is scrambled red blocks

- **What it looks like:** the logo's colours are right, but rectangular pieces are shifted. That
  is the picture of an image read with the wrong row stride.
- **Why:**
  - J-Stars' UI art includes NPOT linear DXT images (`0xA6`/`0xA8` in earlier manifests, e.g.
    548x161).
  - The D3D12 path lays every texture out with rows of exactly one block row
    (`rsx_texture_layout(fmt, w, h)`, no pitch), ignoring the linear row pitch in
    `TEXTURE_CONTROL3`.
  - `rsx_texture_layout_pitched` also ignored the pitch for compressed images.
- **Fix, `MENU_TEX_PITCH=1`:**
  - records `CONTROL3 & 0xFFFFF` per unit and passes it to the layout;
  - for linear compressed images, rows of blocks are read `pitch` apart (only the image part is
    copied);
  - linear uncompressed images already supported a pitch and now receive it.
- Default behaviour is byte-identical: with pitch 0 the shared layout code does what it did before.

## 3. Character select: empty top-left title bar, black speckle noise beside the CPU stars

Lower confidence: two candidates.

- **(a) Packed 16-bit colour or D8R8G8B8 textures.**
  - `rsx_texture_layout` already decodes A1R5G5B5, A4R4G4B4, R5G6B5, R5G5B5A1, D1R5G5B5 and
    D8R8G8B8 to RGBA8.
  - But the D3D12 bind ignores those formats (the unit keeps its *previous* texture, which gives
    noise), and the upload gives them a null view (black, alpha 0, which makes the title empty).
  - **Fix, `MENU_FMT16=1`:** binds and uploads them as RGBA8 with the normal crossbar.
- **(b) The same row-pitch problem as item 2,** for a linear image: `MENU_TEX_PITCH=1`.
- **Diagnostic, `TEXFMT_SKIP=1`:** logs every texture format the bind ignores (`[TEXFMT-SKIP]
  unit fmt WxH off`). One run on the character select screen says which case applies.

## What the AI assistant should run

1. Naruto demo card with `MENU_A2C=1`: the black rims should be gone. A/B without it. Also try the
   Goku card (blue plants).
2. Main Menu with `MENU_TEX_PITCH=1`: the logo should read "J-STARS VICTORY VS+".
3. Character select with `TEXFMT_SKIP=1` first, then `MENU_FMT16=1 MENU_TEX_PITCH=1`: the title
   bar and the stars.
4. Re-check the title screen and a match with all three switches on.

## Free Battle match (Yoshiwara Paradise): what the menu-fixes patch does and does not cover

Screenshots `menu\fight_hud.png`, `fight_3.png`, `fight_6.png`; inferred, nothing run. Ranked:

1. **Black slab on the floor, and Goku partly hidden behind the floor: not this patch.**
   *Corrected:* zeta targets and shadow passes were already on (the defaults since v5), so this is
   not the shared-depth reset. Ranked:
   - **(a) Three fragment programs fail to compile (most likely).** The match's `stdout.txt` has
     `[FP] PS compile FAIL ... error X3504: array index out of bounds` for fp `0x0FC27A81`,
     `0x0FC268C1` and `0x0FC10641`.
     - Cause: register indices go to 63, but the HLSL declares `r[48]`/`h[48]` (reproduced
       offline).
     - Effect: those draws fall back to the debug-font pipeline, which outputs vertex colour where
       t0.r > 0.5. That gives a flat dark surface, like the slab.
     - Separate fix: `patches/fp-regs64/` (`FP_REGS64=1`).
   - **(b) Goku "behind the floor".** If the floor itself is one of those programs, its fallback
     draw still depth-tests normally, so (a) alone may not explain the hidden body. Next suspects,
     after (a) is tested:
     - the floor's reflection pass shares the scene zeta at a different size;
     - guest depth func on a kept zeta: v3 kept LESS_EQUAL; `ZETA_GUEST_FUNC=1` is the A/B;
     - a missing clear on that zeta (`ZPASS_DBG=1` lists zeta clears per batch).
   - **(c) The reflection image itself.** If the floor samples a reflection RT that is empty or of
     another size, it can come out dark. `RSX_CAP_PASSES=2` at the fight would show the floor draw's
     FP and inputs.
2. **Dark slivers through Goku, Gintoki's face in shards, and thin dark lines across the room:
   not this patch.** These are long thin or displaced triangles on skinned characters, so the
   vertex data is wrong: positions or bone weights, or vertices read while the SPU is still
   skinning them (FINDINGS §26). Native strips fixed this for the demo cards (§28). More
   characters on screen in a real match may bring the race back, or the match uses a vertex format
   or draw path the demo did not. Separate fix, on the vertex side.
3. **Luffy blotchy: not this patch.** These are the same skin blotches (texture unit 1, §31), already
   being worked on.
4. **20-27 fps (dips to 13): separate.** Performance work (vertex fetch / guest threads).
5. **Plain white loading screen: possibly this patch.** If its background art uses a 16-bit or
   pitched linear texture, `MENU_FMT16=1 MENU_TEX_PITCH=1` would show it. Check `TEXFMT_SKIP=1` on
   that screen. Compare with RPCS3.
