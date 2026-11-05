# What from the J-Stars port is reusable for the next PS3 game (2026-10-21)

Read-only survey. Nothing in `ps3recomp-src` or `jstars` was changed, built or committed.
Upstream v0.17.0 was checked in a throwaway clone outside the project folder (made from
`ps3recomp-src` + `jstars-notes\upgrade-0.17\*.bundle`, deleted afterwards).

Sources: `git log v0.15.0..jstars-port` (**42 commits**, 13 toolkit files, +3333/-188, 2684 lines
of it in `rsx_d3d12_backend.c`), `jstars-notes\patches\*\README.md`, `FINDINGS.md`, `STATUS.md`,
`STAGES.md`, `RUNTIME_BUGS.md`, `FAKED_IMPORTS.md`, `UPGRADE_0.17.md`, `jstars\port\*.cpp`,
`jstars-notes\tools\*`.

## Short answer

- **Most of the work is generic.** By my reading (not a line count), most of the toolkit diff fixes RSX/GCM/runtime behaviour
  every game hits (FIFO walker, FP decompiler, texture formats, depth targets, vertex fetch speed).
  The J-Stars-only part is small: a timing shim, the lifted SPU job list, test knobs, and the
  run/tour scripts' hard-coded paths, buttons and screens.
- **Eight of our fixes are already in upstream v0.17.0** (listed in §3). Don't extract those; take
  upstream's version.
- **About a dozen generic fixes are small, always-on, and not in upstream.** Those are the cheapest
  win for game #2 (§1, group A).
- **Seven runtime bugs are worked around inside the J-Stars port** (`stubs.cpp`,
  `jstars_game.cpp`, `jstars_sysutil.cpp`) instead of in the toolkit. Every new game would hit them
  again. They should move into the toolkit (§2).
- **The tooling works, but every script has J-Stars paths, the exe name, button timings or
  screen regions baked in.** A per-game config file plus a shared `tools` folder would make it
  reusable (§5).

Legend for "Extract": **clean** = one commit or self-contained file, applies alone;
**medium** = depends on one other item or touches several places; **hard** = spread over many
commits/versions, needs consolidation first.

---

## 1. Generic toolkit fixes (committed on `jstars-port`)

### A. Small correctness fixes. Not in upstream v0.17.0. Best first candidates.

| Fix | Where / commit | What it fixes | Switch, default | Extract | Upstream v0.17.0 |
|---|---|---|---|---|---|
| NV308A inline transfer accepts the full 0x0400-0x1FFC range | `cellGcmSys.c`, in snapshot c0dbbd4 | Inline uploads were cut at 1 KB, so any fragment program > 1 KB uploaded this way arrived truncated (no END) | none, always on | clean (1 line) | **not fixed** (still `<= 0x07FC`) |
| FIFO `RET` with no `CALL` outstanding is skipped | `cellGcmSys.c`, c0dbbd4 | Walker jumped to IO 0 and circled forever; nothing drawn (FINDINGS §11) | none, always on | clean | **not fixed** |
| FP branch words are not inline constants | `rsx_fp_decompiler.c` (`fp_instruction_has_constant`, `rsx_fp_program_size`), c0dbbd4 | IFE/LOOP/REP address bits read as "constant", walker skipped 16 bytes and lost END; looping shaders failed | none, always on | clean | **partly**: upstream guards other walkers but not these two functions |
| 64-entry r/h register files | `rsx_fp_decompiler.c`, 0b43edf + e799d57 | Programs using r48-r63 failed D3DCompile (X3504) and fell back to the font pipeline | `FP_REGS64`, **on** (=0 off) | clean | **not fixed** (`r[48]`) |
| Linear DXT textures honour a row pitch wider than the block row | `rsx_texture_layout.c`, 22d07f7 | Linear compressed images with padded rows decoded skewed | none, always on | clean | **not fixed** for DXT (upstream has it for uncompressed only) |
| 16-bit texture formats A1R5G5B5 / A4R4G4B4 / R5G6B5 | backend, 22d07f7 + e799d57 | Menu panels and character select garbled | `MENU_FMT16`, **on** | clean | **not in** the D3D12 backend |
| Primitive restart, vertex data base offset/index decoded into `rsx_state` | `rsx_commands.c/.h`, c0dbbd4 / e32a781 | Strips not split at restart index; vertex arrays read from the wrong base | none (`RSX_VBASE_OFF=1` disables base) | clean | upstream has these only in its new draw engine (`rsx_dispatch.c`), **not** in `rsx_commands` used by D3D12 |
| Polygon offset + anti-aliasing control decoded | `rsx_commands.c/.h`, 713f72a / 22d07f7 | Shadow acne; alpha-to-coverage state lost | consumed by zeta targets / `MENU_A2C` | medium (state is clean; users are in the backend) | **not in** |
| Mid-frame flush when the VP vertex buffer would overflow | backend, b7efcb5 | Frames with more geometry than the buffer silently lost their tail: the black match screen (FINDINGS §22) | `VP_NO_MIDFLUSH=1` = off, **on** | clean | **not fixed** (upstream only *reports* it via `FRAME_BUDGET`) |

### B. Renderer features (generic, larger)

| Feature | Commits | What it fixes | Switch, default | Extract | Upstream v0.17.0 |
|---|---|---|---|---|---|
| Capacity raises: 16 texture units, 48 offscreen RTs, 512 VP texture slots | c0dbbd4, 4757a62 | Games with long post-FX chains or many materials ran out of slots | `TEX_SLOTS` cap; on | medium (mixed into the snapshot) | upstream still `MAX_OFF_RTS 16` |
| Texture gamma (sRGB decode) in guest FPs | 8ee4e58 | Washed-out materials | `TEX_GAMMA_OFF=1` = off; on | clean | check: upstream has per-channel gamma in the decompiler (#305), not this backend wiring |
| Indexed strips: restart-aware expansion, then native strips | e32a781, 16c9a9d | Black triangles, cracked models, sky gaps (§25, §28) | native on; `STRIP_EXPAND=1` = old | medium | overlaps #391 (strip parity); ours goes further. Needs a hand merge |
| Zeta targets: per-surface depth kept until the guest clears it, guest depth clears/state, A8R8G8B8 views of a zeta, polygon offset | 10e44a0, 2d03eee, 058bb68, c668f4c, f752afd, 713f72a, b0bc4f5 | Water over characters, wiped depth, shadow lookups (§31) | `ZETA_TARGETS`, **on** (+ ~8 `ZETA_*` knobs) | **hard**: 5 versions, ~400 lines, J-Stars HUD fixes inside | overlaps #393 (depth/stencil-only clears) |
| Depth-only shadow passes given to materials | bfae355, c3eaf8b, c1b6225, b0bc4f5 | Shadow maps empty; every engine with shadow maps needs it | `SHADOW_PASSES`, **on** (+ `ZPASS_*` knobs) | **hard**: depends on zeta targets | not in |
| Alpha-to-coverage as alpha test | 22d07f7 | Foliage card edges | `MENU_A2C`, opt-in (used `=1` in the stage survey) | clean | not in |
| Linear texture row pitch (CONTROL3) in the backend | 22d07f7 | Speckles / title bar on character select | `MENU_TEX_PITCH`, opt-in | clean | not in |
| Full mip chains | 2df51e8 | Grainy distant ground (Namek) — **untested** | `TEX_MIPS`, opt-in | clean | not in |
| Depth clip at near/far | 7340a7b, 08acc3e | Correct behaviour, but **blacks out the J-Stars demo** | `DEPTH_CLIP`, opt-in | clean | not in. Keep opt-in until the cause is known |

### C. Speed (generic; all measured on J-Stars, 16 → 29.5 fps total)

| Item | Commits | What it does | Switch, default | Extract | Upstream |
|---|---|---|---|---|---|
| Cached `getenv` for RSX hot paths | `rsx_env.h`, 445974a | ~200 debug switches read per draw cost ms per frame | `RSX_ENV_CACHE`, **on** | **clean** (new header + includes) | not in |
| Per-draw vertex fetch plan, VP-read attributes only, worker pool | 299aa12 | Fetch was 85-96% of the frame | `VFETCH_LEGACY` = old; on | clean (vertex_fetch.c/.h + call sites) | not in |
| Async vertex fetch + `g_rsx_sync_hook` | 266bfc6, 1a8618e | Strip fetches on workers; guest never sees a buffer "consumed" early | `VFETCH_ASYNC`, **on** | medium (needs the fetch plan; hook calls in `cellGcmSys.c`) | not in |
| Strip carry, strip batching (expanded and native) | 292bdbc, e6feaa6, 7c57768 | Fewer syncs, decode once (~8x on native strips) | `STRIP_CARRY`, on | medium (needs async) | not in |
| Texture setup memo + counters | ee1079d | Per-draw texture setup was 45% of a batch | `TEX_VERIFY=frame` opt-in; counters with `PERF` | medium | not in |
| FIFO put-pointer poll during the idle wait | 941d257, 283ad47 | Present thread noticed a new frame only at the 4 ms timeout | `GCM_PUT_POLL=200`, **on** | clean | not in, but **must be re-measured** against upstream's flip rework (#343, #354) |

### D. Diagnostics (generic, keep as tooling)

| Item | Commit | Switch | Note |
|---|---|---|---|
| Per-pass RT snapshots + FP dumps in captures | c8f408e, 46f09a6 | `RSX_CAP_PASSES=1/2` | Extends upstream `RSX_CAP`; clean on top of it |
| PERF timers (`[PERF]`, `[PERF2]`, drain counts) | bce3ec9 + `cellGcmSys.c` | `PERF=1` | Conflicts with upstream `PS3_FTRACE` (UPGRADE_0.17) |
| Main-thread wait chains in the sampler | 0f8af23 | `PS3_SAMPLE_CHAIN=1` | Clean, `ppu_loader.cpp` only |
| Guest call stacks on file open/close | c0dbbd4 | `PS3_FS_GSTACK=1/2` | Clean |
| FIFO circle dump; trace skips NOPs | c0dbbd4 | `GCM_CIRCLE_DUMP=1` | Clean |
| Strip/race dumps, FP fail dumps | c87c390, b219262 | `STRIPX_DUMP`, `VRACE_DBG`, `FP_FAILDUMP`, `CULL_FRONT_SKIP` | Keep only if they stay useful |

---

## 2. Runtime bugs worked around in the J-Stars port (should move into the toolkit)

From `RUNTIME_BUGS.md` and `jstars\port\*.cpp`, checked against v0.17.0.

| Bug | Port file today | Generic? | Upstream v0.17.0 |
|---|---|---|---|
| `cellGameDataCheck` counts any directory as data; `ContentPermit` creates dirs when nothing was created; `CreateGameData` writes no PARAM.SFO → BROKEN_EXIT_GAMEDATA on first-boot installs | `jstars_game.cpp` | **yes**, any game with an HDD install | **not fixed** (only CATEGORY HG/DG added) |
| `cellSpursJq` is a stub; Port2 push/sync/pool-create missing (CRI ADX2 uses it) | `stubs.cpp` | **yes**, every CRI-middleware game | **not changed** |
| `cellFiber` SDK scheduler API missing | `jstars_fiber.cpp` (474 lines) | yes | **now implemented** (#400). Drop ours after a test; check HLE registration order |
| cellSearch / cellMusic result callbacks never fire | `jstars_sysutil.cpp` | yes (common post-movie stall) | not in |
| `UCS2toUTF8` unregistered + wrong signature; NID generator only scans `cell*/sys*/sce*` names | `stubs.cpp` | yes | not changed |
| `cellGcmSetDefaultCommandBuffer` does not repoint the current context | `stubs.cpp` | yes | not changed |
| Offline `cellNetCtlGetState` returns NOT_INITIALIZED | `stubs.cpp` | yes | not changed |
| `cellVdecOpenEx` / `QueryAttrEx` absent | `stubs.cpp` | yes | not checked in detail |
| Template `main.cpp` never calls `cellGame_init_from_paramsfo`; `cellFs.c` mount table differs from `PS3_HDD0_ROOT` | port `main.cpp` | yes | **not fixed** in the template |
| Debug console only in `boot_main.cpp` | `jstars_debug.cpp` | yes | not in template |
| `IID_IDXGIFactory6` link error | port CMake links `dxguid` | yes | **fixed upstream** (302525d). Drop the workaround |

Port `main.cpp` also carries three generic loop changes that belong in the template:
`VBLANK_QPC` (vblank tick from QueryPerformanceCounter instead of the 15.6 ms `GetTickCount64`,
on by default, 25.1 → 28.0 fps), the kick-wait present loop (`cellGcm_fifo_kick_wait`), and the
`ps3_sampler_start()` call. The template's frame clock still has the 15.6 ms problem in v0.17.0.

---

## 3. Already in upstream v0.17.0 (don't extract; take upstream's)

| Ours | Upstream |
|---|---|
| VP cache keyed on the program (VP_VS_CACHE 256) | #361 (cache stays 128 there; our size raise is the only extra) |
| FP pipeline cache keyed on code | #392 |
| FP export bit in the decompiler and PSO key (35b1f12) | #396 |
| FACING (`FP_FACING`), TC8/TC9 in FP and VP (3132243, 62ec701, c668f4c) | #328 (b8a7c5b), emitted only when read |
| cellFs octal `O_*` flags | #349 (compare: ours also adds `O_EXCL`) |
| WASAPI shared-mode autoconvert | #385 |
| Strip parity (part of e32a781) | #391 |
| Depth/stencil-only clears (part of zeta v2/v5) | #393 |
| `TEX_EXPAND` in `patches\color-exposure\tex-expand.patch` | is upstream 122690f (#331) |
| Scripted analog stick (STATUS "needs a cellPad change") | `PAD_STICK` already existed in v0.15.0; v0.17.0 adds `PAD_CLOCK=flips` so `PAD_SCRIPT` times count flips (more robust tours) |

---

## 4. J-Stars only

| Item | Where | Why it is game-specific |
|---|---|---|
| DataCheck delay shim `JSTARS_DATACHECK_MS` + `g_ppu_fs_last_read_ms` use | `jstars_game.cpp`, FINDINGS §9/§13 | Timing workaround for J-Stars' CPK rebind race. The `g_ppu_fs_last_read_ms` export itself is harmless and generic |
| Lifted SPU jobs, `spu_workloads.c`, `KNOWN` names in `lift_all_jobs.py` | `jstars\spu_jobs`, `port\spu` | The job set is per game (the *pipeline* is generic, §5) |
| Temporary diagnostics in `ppu_recomp_027/032.cpp` | `port\recompiled` | Lost on re-lift anyway |
| Bisect knobs: `S0_LINEAR`, `TEX_NULL_UNITS`, `S_CLAMP_UNITS`, `TEX_A1_UNITS`, `TEXFMT_SKIP`, most `ZETA_*`/`ZPASS_*` | backend | Written to bisect one J-Stars symptom; drop before upstreaming |
| `TONEMAP_EXPOSURE` (color-exposure patch) | not committed | Forces one J-Stars FP constant (c1.w) |
| Comments citing FP hashes, RT addresses, frame numbers | everywhere | Fine to keep as evidence, but strip them from upstream PRs |
| Capture frame numbers (5650/5690), RT addresses (`0x0ED10000`), menu screen regions, stage survey | tools, notes | Per game |

Pending, not yet committed, and generic: `lum-readback.patch` (`LUM_READBACK`: write small
offscreen targets back to guest memory; any game with GPU auto-exposure needs this),
`color-diag.patch` (logs), `strip-indexed.patch` (`STRIP_INDEXED`, fight speed). Wait for the
J-Stars test results before treating them as reusable.

---

## 5. Tooling: what exists and what makes it reusable

| Tool | Today | Hard-coded J-Stars bits | To reuse |
|---|---|---|---|
| `jstars\run_test.ps1` | Mounts ISO, sets VFS/HDD/flash env, runs exe with timeout, debug-console commands | ISO path, `JStarsRecomp.exe`, `EBOOT.elf`, `PS3_TITLE` | Read them from a per-game `game.psd1` (or params `-Game <dir>`) |
| `tools\capture_match.ps1` | One-command capture recipe | `PAD_SCRIPT`, frame numbers, screenshot seconds | Recipes in the game config ("capture presets") |
| `tools\menu_tour.sh` + `screen_match.py` + `tools\refs` | Closed-loop tour by recognising screens | Paths under `$HOME/Desktop/PS3Recomp`, the J-Stars menu flow, `REFS` regions in code | Split into a **tour engine** (`wait_for`, `press`, `snap`, `finish`) and a per-game **tour script** + `refs\regions.json`. Use upstream `PAD_CLOCK=flips` |
| `snap_now.ps1`, `jstars\snap_window.ps1`, `cpu_sample.ps1` | Window screenshot, CPU sampling | Process/window name | Take `-Process` from config |
| `capstat.py`, `capops.py`, `capcompare.py`, `capmanifest.py`, `bmp2png.ps1` | Capture analysis | None of substance | Already generic; move to a shared tools folder (or `ps3recomp-src\tools`) |
| `perfbase.py` | Speed summary from `[PERF]` lines | Default first batch 5800 | Make the start batch a required arg or auto-detect |
| PERF timers, sampler (`PS3_SAMPLE`, `PS3_SAMPLE_CHAIN`) | In the toolkit | None | Template `main.cpp` must call `ps3_sampler_start()`; write a one-page doc of the `[PERF*]` lines |
| Debug console (`jstars_debug.cpp`) | `PS3_DEBUG=<file>` stat/threads/mem | Name only | Move into the template |
| SPU job pipeline: `SPU_DUMP_MISS` → `scan_jobcrt.py` → `lift_all_jobs.py` → `spu_workloads.c` | `jstars\spu_jobs` | Paths, `KNOWN` table | Move scripts to shared tools; keep the `KNOWN` table in the game folder |
| `port\build.bat` | clang-cl + Ninja build | VS and Python locations | Template `build.bat` reading them from env/config |
| Notes layout (STATUS, FINDINGS, `patches\<name>\README.md` + patch) | `jstars-notes` | None | Copy as an empty `<game>-notes` template |

---

## 6. Proposed order of work

Each step is a code change, so each needs the maintainer's go-ahead first. Commits stay local; nothing is pushed.

0. **Pick the base.** Recommendation: a new local branch `generic-fixes` in `ps3recomp-src`
   from **v0.17.0** (the bundle is already on disk), leaving `jstars-port` untouched. Fixes
   are ported one commit each. This keeps the AI assistant's file alone and avoids the 21
   merge conflicts. Open question: by project rule only the AI assistant builds, so either
   that thread builds `generic-fixes`, or the maintainer allows a second thread to build *toolkit tests only*.
1. **Group A correctness fixes** (§1A): NV308A range, RET without CALL, FP branch constant,
   FP_REGS64, DXT pitch, 16-bit formats, restart/base offset, mid-frame flush. Small and
   always-on. Rename J-Stars-flavoured switches (`MENU_FMT16` → `TEX_FMT16`, `MENU_A2C` →
   `RSX_A2C`, `MENU_TEX_PITCH` → `TEX_LN_PITCH`).
2. **Port workarounds into the toolkit** (§2): cellGame handshake, cellSpursJq Port2,
   cellSearch/cellMusic callbacks, UCS2toUTF8, SetDefaultCommandBuffer, NetCtl. Test that
   upstream's cellFiber replaces `jstars_fiber.cpp`.
3. **Template upgrades**: debug console, `cellGame_init_from_paramsfo` + fs mappings,
   `VBLANK_QPC` clock, kick-wait loop, sampler start, `build.bat` with configurable paths.
4. **Speed set**: `rsx_env.h`, fetch plan, async fetch + sync hook, strip carry/batching, texture
   memo, `GCM_PUT_POLL`. Re-measure fps on J-Stars because v0.17.0 changed flip timing.
5. **Tooling**: shared `tools` folder, per-game config, tour engine + per-game tour script,
   notes template.
6. **Zeta targets + shadow passes**: consolidate v1-v5 into one clean change on top of
   upstream #393, keep only the knobs still needed.
7. **Unproven, opt-in** (`TEX_MIPS`, `DEPTH_CLIP`, `LUM_READBACK`, `STRIP_INDEXED`,
   `MENU_A2C`): only after J-Stars tests show they win.
8. Optional, the maintainer's call: offer the clean ones upstream as PRs (needs a GitHub repo and pushing,
   which the project rules forbid today).

---

## 7. New-game checklist (draft)

**Setup**
- [ ] The EBOOT is taken from your own copy of the game; for how to obtain the decrypted file, see RPCS3 and its documentation. Copy PARAM.SFO; note title id and CATEGORY (DG/HG).
- [ ] Copy the project template and `<game>-notes` template; fill `game.psd1` (ISO, ELF, exe name,
      title, VS/Python paths).
- [ ] `ppu_loader.py` → `ppu_lifter.py` → build with `build.bat`.

**First boot**
- [ ] `run_test.ps1 90` with `PS3_VERBOSE=1`; collect `[hle] unresolved NID` lines into
      `FAKED_IMPORTS.md`.
- [ ] Check the J-Stars blockers first, since they are common: SPURS job misses (`SPU_DUMP_MISS`,
      lift with the job pipeline), cellSpursJq, cellFiber, cellGame install handshake, cellFs flags,
      cellSearch/cellMusic callbacks, movie (cellVdec), FIFO walker circling (`GCM_CIRCLE_DUMP`).
- [ ] First image: confirm the GPU sees frames (`RSX_FRAMELOG=60`), then `RSX_CAP` +
      `capstat.py` for "black vs nothing drawn".

**Rendering**
- [ ] Capture a frame with `RSX_CAP_PASSES=1`; `capops.py --chain` on the display target.
- [ ] Count `[FP] PS compile FAIL` and `[TEXFMT-SKIP]`; check vertex buffer overflow
      (`FRAME_BUDGET=1`).
- [ ] Compare with RPCS3 screenshots in `<game>-notes\reference`.

**Speed**
- [ ] `PERF=1` + `perfbase.py`; `PS3_SAMPLE=2` and `PS3_SAMPLE_CHAIN=1` for what the main
      thread waits on. Run with `PS3_VERBOSE=0` (verbose logging halved J-Stars' speed).

**Automation**
- [ ] Take reference screenshots of each menu screen, define regions in `refs\regions.json`,
      write the per-game tour script with `PAD_CLOCK=flips`.

**Hygiene**
- [ ] Local commit before each experiment; one finding per FINDINGS section; patches with a
      README (base commit, switch, default, test steps).

## 8. Lesson from the loading-tips bug (2026-10-22, AI assistant, FINDINGS 69)

The D3D12 backend substitutes its own resources for guest memory in a few places (the frame copy
for a display-sized texture, captured reduced-viewport passes, offscreen RTs). The screen-as-
texture rule matched on SIZE only and silently replaced two pieces of CPU content with the previous
frame: the match loading screen's DXT1 tips snapshot and the boot logos. Commit 3b68f46 keeps the
rule only for an uncompressed texture whose guest bytes are empty. For the next game:

- Any substitution rule must exclude formats the GPU cannot produce (compressed) and must prefer
  the guest bytes whenever they are non-empty (sample across the whole image, not the first bytes).
- When a draw's output is wrong but the texture's guest bytes decode correctly (`RSX_CAP_TEX=1`
  + `tools\texbin.py`), look at the BIND path before the upload path: the substitutions run first
  and `continue` past the upload.
- `RSX_CAP_TEX`, `MEMPEEK_FRAMES`, `VRAMSCAN_FRAMES` (patches\loading-tips, opt-in) are generic.
