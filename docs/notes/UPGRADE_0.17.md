# Upgrade check: ps3recomp v0.15.0 -> v0.17.0 (2026-10-21)

Read-only survey. Nothing in `ps3recomp-src` was changed, fetched or built.

## Short answer

**Don't switch the whole toolkit to v0.17.0 right now. Cherry-pick 4-5 upstream commits instead,
and do the full upgrade later at a quiet point (e.g. after the menu/switch tours).**

Reasons, with evidence below:
1. About half of upstream's D3D12/shader work between v0.15.0 and v0.17.0 is **the same fixes we
   already made locally** (VP cache key, FP cache keyed on code, FP export width, strip parity,
   FACING/TC8/TC9, cellFs O_CREAT, WASAPI format convert). Upgrading buys us little there.
2. A trial merge gives **21 conflict blocks in 6 files**, 8 of them in `rsx_d3d12_backend.c`, the
   file the AI assistant is actively editing.
3. The things that *are* new and might help us apply **cleanly on their own** (see "Cherry-pick list").
4. Upstream's flip/GCM rework (v0.16) changes frame pacing, which our 29.5 fps depends on
   (`VBLANK_QPC`, `GCM_PUT_POLL`). That needs its own measured test, not a side effect of an upgrade.

## Where the facts come from

- Upstream: `github.com/sp00nznet/ps3recomp`. Release notes are in its `README.md` "Changelog"
  (v0.16.0 "Forty-Four", 44 PRs; v0.17.0 "Thirty-Three", 33 PRs). v0.17.0 = `c92d92a`,
  tagged 2026-10-20 19:24 -0700. 200 commits, 157 files, +12.3k/-2.5k lines since v0.15.0.
- Our base: local tag `v0.15.0` = `0fe7dbf`, identical to upstream's. `jstars-port` = `e799d57`,
  41 commits, 13 toolkit files, +3283/-184 (2630 of it in `rsx_d3d12_backend.c`).
- Trial merge/cherry-picks were done in a throwaway clone outside the project folder (deleted
  afterwards). Upstream commits are in `jstars-notes\upgrade-0.17\ps3recomp-v0.15.0..v0.17.0.bundle`
  so the AI assistant can use them offline:
  `git fetch <bundle> refs/tags/v0.17.0:refs/tags/v0.17.0` (the PC cannot reach GitHub directly).

## Upstream changes vs. our patches

### Already done locally (upstream duplicates ours, = merge conflicts, no gain)

| Upstream | Ours | Note |
|---|---|---|
| #361 VP cache keyed on the program, not the store | `rsx_d3d12_backend.c` VP identity hash (VP_VS_CACHE 256, in-place eviction) | Same idea; ours also raises the cache to 256 |
| #392 guest-FP pipeline cache keyed on code | FP cache keyed on code (J-Stars FP ring, 2,482 compiles) | Same fix |
| #396 tell the FP decompiler the export width | 35b1f12 FP export bit, PSO key | Same fix, different wording |
| #391 keep strip parity when expanding indexed strips | e32a781 restart + winding expansion, then native strips (16c9a9d) | Ours goes further |
| #328 FACING, TC8, TC9 in the FP decompiler | 3132243/62ec701 `FP_FACING`, c668f4c TC8/TC9 pass-through | Upstream emits them only when read; ours has switches |
| #349 cellFs `O_*` flags octal | `ppu_fs.cpp` octal flags (J-Stars first-boot install) | Same fix |
| #385 WASAPI shared mode converts the stream | `cellAudio.c` `AUDIO_AUTOCONVERT` | Same fix |
| #393 depth/stencil-only clears don't wipe the scene | zeta targets v2/v5 guest depth clears | Overlaps; would need a careful merge |

### New upstream, likely useful to us

| Upstream | Why it may matter for J-Stars | Applies cleanly to jstars-port? |
|---|---|---|
| #331 biased-remap and `_bx2` texture fetches return signed values | Possible lead for **neon grass / saturated sky / pale textures**: an unsigned `_bx2` normal-map fetch makes lighting too bright. Guess, not measured. | **Yes** (122690f) |
| #369/#370 SPU float fast paths, AVX2 / SSE2, four lanes at a time | Our limit is the **main thread waiting ~72% on one SPU job chain** (`perf\main-wait-2026-10-20`). Faster SPU float could shorten it. Needs a measured fps. | **Yes** (517fc71, ef060b3) |
| #408 texture offsets are 31 bits | Correctness for textures in high memory; cheap. | **Yes** (e3f7872) |
| #384 X16 textures present like B8 | Font/atlas formats; may touch small texture issues. | Conflict only in a test file |
| #358 NV3089 copies were 256x too short (source origin 12.4) | If J-Stars uses NV3089 blits (post-FX, texture uploads) these copies were truncated. Worth checking our logs for NV3089 use. | Not tried (cellGcmSys) |
| #329 perspective-correct flag on texcoord reads | Texture swimming/warping on large polys. | Conflicts in `rsx_fp_decompiler.c` |
| #390 wrapped command ring no longer overwrites unread commands; #401 `get` past a RELEASE before its label; #347 `get` follows JUMP | FIFO correctness; could explain rare hangs/garbage. | Not tried; `cellGcmSys.c` conflicts with our PERF/trace code |
| #343/#354 flip completes when the RSX reaches it | Changes **frame pacing**. Could help or undo part of our 29.5 fps; must be measured. | Not tried; risky |
| #388 taskset bits atomic, #383 DMA-halt wall-time backoff, #362 SPU image by bytes | SPURS robustness. J-Stars mostly uses jobs + our `cellSpursJq` override, so probably low impact. | Not tried |

### Not relevant to us
Vulkan/Metal backend work, draw engine 3D textures, Linux/macOS/MinGW CI, cellVdec, other games'
specifics, lifter options, POSIX `mmap` template.

## Conflict map (trial `git merge v0.17.0` into jstars-port)

| File | Conflict blocks | What clashes |
|---|---|---|
| `libs/video/rsx_d3d12_backend.c` | 8 | VP cache size + hash, FP cache comment, FP export (75 lines ours vs 15 theirs), PSO fail log, FRAME_BUDGET, clear handling vs zeta targets, strip expansion (158 lines ours) |
| `libs/video/cellGcmSys.c` | 4 | our PERF drain timers vs upstream `PS3_FTRACE`; FIFO trace/`GCM_CIRCLE_DUMP` |
| `libs/video/rsx_fp_decompiler.c` | 3 | TC8/TC9/FACING done twice |
| `runtime/ppu/ppu_fs.cpp` | 3 | same O_CREAT fix; log lines (`PS3_LOG_FIRST`) |
| `libs/audio/cellAudio.c` | 2 | same WASAPI convert fix |
| `libs/video/rsx_vp_decompiler.c` | 1 | TEX8/TEX9 comment |

Our vertex-fetch files (`rsx_vertex_fetch.c/.h`), `rsx_env.h`, `rsx_commands.h` and
`ppu_loader.cpp` merge without conflict. Replaying our 41 commits one by one: the 2026-10-20
snapshot commit conflicts (13 blocks) and "zeta targets v3" conflicts (TC8/TC9); the rest
replay on top once those are resolved. All conflicts are resolvable; most are "keep ours" or
"both did the same thing". Estimate: half a day for the AI assistant, plus a full
re-test (title, demo, menu tour, fps).

## Other upgrade risk

- **cellFiber (#400)** now implements the SDK scheduler API (`InitializeScheduler`, `RunFibers`,
  `WaitSignal`, `SendSignal`, `JoinFiber`...), the same NIDs `jstars\port\jstars_fiber.cpp`
  overrides via `ps3_hle_register_ctx`. Ours wins only if it registers after the toolkit's. Check
  the registration order (or the `[HLE]` log) after any upgrade.
- **#296** separates LV2 user allocations from mapper reservations and protects ELF segments;
  J-Stars' memory layout was tuned on the old behaviour (DataCheck timing shim, FINDINGS §9/§13).
- **#399 `PS3_LOG_FIRST`** changes many log lines; our tools grep some of them (`[FP] PSO FAIL`,
  `[fs] open FAIL` format changed from `0%o` to `0x%X`).
- The port's build pulls toolkit sources by absolute path
  (`jstars\port\build\hle_nid_sources.txt`), so an upgrade needs a clean CMake reconfigure.

## Cherry-pick list (if the user approves; AI assistant does it)

In this order, one commit each, measure fps and take a screenshot after each:
1. `122690f` `_bx2`/biased remap signed (neon grass / pale texture lead)
2. `517fc71` + `ef060b3` SPU AVX2/SSE2 float paths (speed; compare with 29.5 fps)
3. `e3f7872` 31-bit texture offsets
4. Optional, needs hand-merge: `17bf55aa` + `a6cfb0f0` NV3089 12.4 origin, `7c1d6f12` ring wrap,
   `a8572e99` get past RELEASE.

Do the full v0.17.0 rebase later, when `jstars-port` is at a stable point, and keep the
pre-rebase branch as a tag so it can be undone in one command.
