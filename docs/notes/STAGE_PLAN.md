# Stage-first plan: what breaks stages, and a survey to run (2026-10-21)

The maintainer's priority is to fix the stages first. Once stages look right, it will be easier to tell
which characters really need work.

This is a read-only plan built from the notes and patches so far. Nothing was run for it.

## 1. Known problems that affect stages, ranked by how many stages they likely hit

| # | Problem | Likely reach | Status / switch |
|---|---|---|---|
| 1 | **Shaders that fail to build:** register index 48-63 vs `r[48]`/`h[48]`. The draw falls back to a flat dark debug shader. | Any stage whose materials use many registers. 3 programs on Yoshiwara (black floor slab). | Patch ready: `patches/fp-regs64` (`FP_REGS64=1`) |
| 2 | **No mipmaps:** only level 0 is uploaded. Distant ground, water and walls shimmer and look grainy (the "noisy" grass and water in every 3D shot so far). | **Every stage** | **Gap: no patch yet** (see section 3) |
| 3 | **Texture formats ignored at bind / null at upload:** 16-bit packed colour and D8R8G8B8. | Unknown until logged; UI is affected for sure | `patches/menu-fixes` (`MENU_FMT16=1`; `TEXFMT_SKIP=1` logs) |
| 4 | **Linear texture row pitch ignored** (NPOT linear and DXT images) | UI certain; stage decals and billboards possible | `patches/menu-fixes` (`MENU_TEX_PITCH=1`) |
| 5 | **Foliage cut-out by alpha-to-coverage ignored:** cards drawn whole, black rims. | Every stage with trees, grass or plants (Naruto forest, Namek-type stages, Goku card plants) | `patches/menu-fixes` (`MENU_A2C=1`) |
| 6 | **Two-sided materials read FACING = 0:** leaves and banners shade black. | Foliage, flags, cloth | `patches/foliage-facing` (`FP_FACING=1`) |
| 7 | **Water and reflective floors** need kept depth and the reflection/refraction images. | Water stages, polished floors | In since zeta v5 (default on). The remaining floor issue on Yoshiwara is #1 first. |
| 8 | **Shadow quality:** hard or dark shadows, polygon offset, comparison vs bitcast by opcode. | Every stage (cast shadows on the ground) | In since v5 (default on). Darkness not yet compared with RPCS3. |
| 9 | **Fixed samplers:** every unit is LINEAR+WRAP and s0 is POINT+CLAMP. The game's filter, wrap and LOD bias are ignored. | Every stage: tiled floors and walls stretched or blocky on unit 0 | Test switch `S0_LINEAR=1` exists; **gap: per-draw samplers** |
| 10 | **Depth clip:** `DEPTH_CLIP=1` blacks out the demo, so some pass emits z outside the range. It is clamped today. | Ground or water planes crossing the near plane at low cameras | Opt-in, cause unknown |
| 11 | **Sky dome gaps** | Fixed by native strips (§28) | Re-check only |

Character-only problems, out of scope for this plan: skin blotches (unit 1, §31), shattered faces
and slivers (vertex data, §26), Luffy blotches.

### Building-heavy stages need the most work (the maintainer's observation)

Of the three maps seen so far, the open one needs little work and the ones with buildings need a
lot (Yoshiwara: `menu\fight_hud.png`). What building stages have more of, and which problems that
feeds (inferred from the Yoshiwara shots and the code):

- **More distinct materials per frame,** so more chances of a shader that fails to build (#1). The
  three failures so far are all on Yoshiwara.
- **Baked lighting on a second texture (likely unit 1, a lightmap or detail layer).** Character skin
  blotches were traced to unit 1, blended in by a lighting term (§31). Walls and floors lit the same
  way would show the same harsh patches and blown-out white areas seen on Yoshiwara's walls. This
  may be the shared root of "buildings look wrong" and "skin looks wrong".
- **Tiled wall, floor and roof textures,** so the fixed samplers (#9: unit 0 POINT+CLAMP, WRAP
  elsewhere, no LOD bias) and missing mipmaps (#2) show most. The grain and stair-stepping on the
  walls fit this.
- **Windows, paper screens, railings and signs** use alpha test or alpha-to-coverage cut-outs (#5) and
  linear UI-like textures (#4).
- **Reflective floors and interiors** (#7): the black floor slab.
- **Thin dark lines along edges:** likely the toon outline hulls of building geometry or long thin
  triangles. Check whether they follow building edges (outlines; compare `CULL_FRONT_SKIP=1`) or
  cross the room at random (vertex data).

So building stages are the test set. Survey them in detail, as section 2 says, and fix in this
order: #1, unit-1 lighting, #2/#9, #5.

## 2. Stage survey checklist (for the AI assistant)

**Building stages: record in detail.** For every stage with buildings or interiors, on top of the
basic capture:
- always take an `RSX_CAP_PASSES=2` capture of one fight frame (every FP and its bound textures);
- take one A/B screenshot each with `TEX_NULL_UNITS=2` (unit 1 off), `TEX_GAMMA_OFF=1`,
  `S0_LINEAR=1` and `CULL_FRONT_SKIP=1`, same camera;
- note per screenshot whether walls, floors, windows and outlines change. This tells which of the
  building problems above is the big one.

**Setup:**
- First fixes on: `FP_REGS64=1 MENU_A2C=1 MENU_TEX_PITCH=1 MENU_FMT16=1 FP_FACING=1`. Zeta and
  shadows stay at their defaults (on).
- Logs on: `TEXFMT_SKIP=1 ZPASS_DBG=1 PS3_VERBOSE=0`.
- Path to each stage (`menu\README.md`): Free Battle → 1 Player → START (random characters) →
  Stage Select. Then **RIGHT (`0x0020`) N times on the stage row** → START → CROSS at "Now PRESS ✕".
- Do one stage per run, or return to Stage Select between stages. Note each stage's name as shown
  on the select screen; the list order is the survey order.

**Per stage, capture:**
1. **Screenshots:**
   - the intro card where the camera shows the stage widely;
   - the fight start (HUD visible);
   - one shot 20 s into the fight (camera moved).
2. **Log counts from that run:**
   - `[FP] PS compile FAIL` (should be 0 with `FP_REGS64=1`);
   - `[TEXFMT-SKIP]` lines (formats still ignored);
   - `[FP] decompile fail`;
   - `[ZPASS]` zeta clears and targets (sanity).
3. **One `RSX_CAP_PASSES=2` capture** of a fight frame, only if something looks wrong. It gives every
   FP and the bound textures.
4. **The same stage in RPCS3** at a similar camera (intro card or fight start).

**Compare against RPCS3, item by item:**
- sky and distant scenery;
- floor or ground (colour, reflections, tiling sharpness);
- water (colour, transparency, depth behind characters);
- foliage (cut-out, colour, no black rims or shards);
- shadows (present, darkness, soft edges);
- distant texture noise or shimmer (mip issue #2);
- transparent effects (glass, smoke, particles);
- anything flat black or flat dark (#1, #3);
- logos and HUD.

**Record results** in a table in `docs\notes\STAGES.md`: one row per stage, one column per item
above, plus the log counts. A cell is "ok", or a short note with the screenshot name.

## 3. Gaps where a patch would be drafted after the survey

- **Mipmaps (#2):** upload the full mip chain. `rsx_texture_mip_chain` already computes the levels.
  The D3D12 path creates `MipLevels = 1` and copies level 0 only. Likely the single biggest
  look-improvement across all stages. I will draft it if the survey shows the shimmer on most
  stages, which is expected.
- **Per-draw samplers (#9):** build sampler state from TEXTURE_FILTER/ADDRESS/CONTROL0 (filter,
  wrap/clamp/mirror, LOD bias) instead of the 16 static samplers. Draft if tiled floors look
  stretched or blocky.
- **Depth clip (#10):** only if near-plane artefacts show up on stages with low cameras.
- **Anything new the survey finds:** formats from `[TEXFMT-SKIP]`, or remaining compile failures.
