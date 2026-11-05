# Findings: every blocker so far

> Index: §1-10 boot blockers · §11 GPU FIFO · §12-15 post-movie stall · §16-17 first draws, title screen · §18-19 3D match fixes + bisect · **§20 latest capture (rsxcap_rt48), next ideas**. Current state: STATUS.md.

Each entry: what it looked like → what was really wrong → what fixed it → where.

---

## 1. Link error: `IID_IDXGIFactory6` undefined

- **Symptom:** everything compiled; final link failed.
- **Cause:** `ps3recomp-src\libs\video\rsx_d3d12_backend.c` includes `<initguid.h>` *after* the
  DXGI headers, so the GUID is only declared, never defined.
- **Fix:** link `dxguid` — `port\CMakeLists.txt` (WIN32 link libraries).

## 2. Game's own SPURS jobs not lifted ("dispatch MISS")

- **Symptom:** `[spurs-job] dispatch MISS fp=0x37F7DF42BD146C53 size=5072` and `…C589113B size=4944`;
  job guards "still closed after 20 s"; job server thread polling forever.
- **Cause:** SPURS job binaries are raw code blobs, not ELFs, so `extract_spu_images.py` finds
  nothing. They are embedded in the EBOOT; the job descriptor's `eaBinary` gives the address.
- **Fix:** extract `0x80B700` (5072 B) and `0x80FD00` (4944 B) from EBOOT.elf (file offset =
  vaddr − 0x10000 for segment 0), wrap as SPU ELF (base 0, entry 0 — the runtime loads jobs at
  LS 0), lift with `--auto-functions`, register in `port\spu\spu_workloads.c` (image ids 1, 2).
  The 7 `.word` "unsupported" at 0x20–0x38 are the `JOBCRT` header; the entry branches over it.

## 3. cellSpursJq (job queue) was a placeholder

- **Symptom:** CRI audio never ran; `cellSpursJobQueuePort2Sync` etc. unresolved.
- **Cause:** runtime `libs\spurs\cellSpursJq.c` "completes" every job without running it and stores
  the queue as a slot number; 5 of J-Stars' 9 Jq imports were missing. `0x983BE7FC` =
  `_cellSpursCreateJobQueueWithJobDescriptorPool`.
- **Fix:** `port\stubs.cpp` registers context handlers (`ps3_hle_register_ctx`, from
  `ps3_load_prx_modules()` so they precede the runtime's) for all 8 Port2 entries. A push
  (r3 port, r4 job descriptor, r5 descriptor size) runs the job **synchronously** through
  `spu_workload_dispatch_job`, so Sync is a no-op.

## 4. CRI's audio job not lifted

- **Symptom:** after (3), `dispatch MISS fp=0x01E73637D84700F4 size=29920`.
- **Fix:** `SPU_DUMP_MISS=<dir>` dumped the binary ("CRI SpursJob AsrVoice/PS3SPU"); lifted with
  `--code-end 0x5E80` (code ends with `bi $lr` at 0x5E74; strings/tables after, which otherwise
  decode as dozens of fake `stop 0` functions). Registered as image 3. Runs ~200×/s, rc=0.

## 5. cellFiber scheduler missing

- **Symptom:** `cellFiberPpuInitializeScheduler`, `RunFibers`, `WaitSignal`… unresolved.
- **Cause:** runtime `libs\spurs\cellFiber.c` implements a different API (host function pointers,
  one global scheduler) and none of the scheduler entry points.
- **Fix:** `port\jstars_fiber.cpp`: one Windows fiber per guest fiber, own host stack (16 MB
  reserve), own `ppu_context`, guest stack from CreateFiber, r13/thread id from the worker thread.
  On every switch save/restore the runtime's per-thread state: `g_active_ctx`, `g_trampoline_fn`,
  `*ps3_ppu_dispatch_depth_slot()`, `*ps3_ppu_vcall_sp_slot()`. Also overrides the runtime's
  `cellFiberPpuCreateFiber` (NID 0x7C2F4034).

## 6. Fibers never woken — the SPU signals them through guest memory

- **Symptom:** 6 fibers park in `WaitSignal`; no `SendSignal` ever arrives; the file server's load
  never finishes; the main thread polls a task (`+0x24` must reach 4) forever.
- **Investigation:** main thread → loader state machine (state 8) → `sp_file_server_load_thread`
  → it waits on the job system → job = SPU job + fiber. `SendSignal` is only called from
  destructors. `SPU_ATOMTRACE` showed SPU job `80FD00` doing `getllar` (0xD0) **on the fiber's
  address** (the fiber EA is in the job descriptor's user data at +0x30) but never `putllc`.
- **Cause:** the SPU job signals the PPU fiber itself by rewriting the guest `CellFiberPpu` word 0;
  my scheduler kept state on the host, word 0 stayed 0, and the job treats 0 as an error.
- **Layout** (from the SPU code): word0 flags `0x01` ready, `0x02` running (ours), `0x08` waiting
  for signal, `0x10` signal pending, `0x40` terminated; `+0x04` ready-list next; `+0x08` scheduler
  EA; `+0x19` priority byte. SPU: `w&8` → `(w&~8)|1` and link into scheduler; else `w|=0x10`;
  `w==0 || w&0x40` → error.
- **Fix:** `jstars_fiber.cpp` keeps the state in that guest word (`GF_*`), and `RunFibers`
  treats a waiting fiber whose word lost `0x08` as runnable.

## 7. UCS2toUTF8 wrong signature

- SDK: `int32_t UCS2toUTF8(uint16_t ucs2, uint8_t* utf8)` → byte count. The runtime's function of
  that name is buffer-to-buffer, takes host pointers, and is unregistered (faked 35×).
- **Fix:** handler in `port\stubs.cpp` (NID 0xA9A76FB8).

## 8. First-boot HDD install → BROKEN_EXIT_GAMEDATA

- **Symptom:** `cellGameContentErrorDialog(BROKEN_EXIT_GAMEDATA)`; title id `BLES00000`.
- **Causes (4):**
  1. template `main.cpp` never calls `cellGame_init_from_paramsfo` → placeholder title id.
  2. runtime `cellGameContentPermit` **creates the game-data dirs even when DataCheck said
     "none"**, and `cellGameDataCheck` counts any existing dir as data → after the first boot
     an empty `BLUS31519-INSTALL` looked installed.
  3. `ppu_fs.cpp` `CELL_FS_O_*` constants wrong (see RUNTIME_BUGS) → `O_CREAT` ignored → the
     install's `*.cpk.tmp` creates failed 257k times.
  4. `libs\filesystem\cellFs.c` (cellFsRename) maps `/dev_hdd0` to `./gamedata/dev_hdd0`, not
     `PS3_HDD0_ROOT` like `ppu_fs.cpp` → the final `.tmp → .cpk` renames went nowhere.
- **Fixes:** `main.cpp` calls `cellGame_init_from_paramsfo` and
  `cellfs_add_path_mapping("/dev_hdd0/", …)` / `("/dev_flash/", …)`;
  `port\jstars_game.cpp` overrides DataCheck (no `PARAM.SFO` ⇒ `CELL_GAME_RET_NONE`),
  CreateGameData (remember `CellGameSetInitParams`: title[128] @0, titleId[10] @0x80,
  version[6] @0x8C) and ContentPermit (empty paths when nothing to permit; writes `PARAM.SFO`
  on commit, so an interrupted install is offered again); `ppu_fs.cpp` flags fixed.
- **Install flow observed:** DataCheck NONE → ContentPermit (empty) → DataCheck NONE →
  CreateGameData → "Installing game data" progress dialog → write `*.tmp` → rename →
  ContentPermit (commit) → BootCheck again → trophies → save data → movie.

## 9. Second-boot deadlock: memory corruption from a CPK rebind race

- **Symptom:** threads 1, 4–9, 18 all blocked on `lwmutex 0x48E03578`; owner = tid 19
  (`sp_game_server_video_thread`) spinning.
- **Chain:** lock is the job-system hash table's (table obj `0x48E03500`, buckets `+0x90`, count
  `+0x94` = 512, 12-byte entries). Its buckets were full of RGBA pixel bytes → the insert
  (`func_00536C94`) never finds a free slot. `PPU_WWATCH` caught the writer: `memcpy`
  (`func_006FB480`) called by the file server at `0x52E874` with **n = 0x1C1E2443 (~470 MB)**.
  The length comes from a `[u32 BE len][data]` chunk header that was garbage because the stream
  skipped a block: `logo_US_v.pak` (in `game.cpk` at 0x491000) blocks 0–3 loaded, block 4 never
  did — CRI tried a **loose-file** `cellFsOpen("game/logo/logo_US_v.pak")` (fails) and then all CPKs
  were closed and re-bound, after which block 5 was read.
- **Root cause:** the game's partition manager (`func_0060B41C → func_005381B0 → func_003F0520`)
  re-evaluates disc-vs-HDD CPKs right after a successful `cellGameDataCheck` and re-binds them.
  Here that happens ~200 ms into the boot-logo stream; on hardware DataCheck and the Blu-ray CPK
  opens take long enough that the logo stream has finished. After the unbind, the next block's
  CRI load has no binder → loose-file fallback → fails → the file server moves on to the next
  block → parser desync → overrun.
- **Fix (timing shim):** `jstars_game.cpp` sleeps 1 s in a successful game-data DataCheck
  (`JSTARS_DATACHECK_MS`, default 1000). Result: no failed open, no overrun, no deadlock.
- **Better fix later:** find why CRI lets the unbind happen with a loader mid-stream (or make the
  file server not skip a failed block) — would remove the timing dependence.

## 10. Opening movie: cellVdecOpenEx / cellVdecQueryAttrEx missing

- **Symptom:** `StartSeq(handle=0)` on a slot never opened (OpenEx faked → handle stayed 0).
- **Fix:** `port\stubs.cpp` forwards `cellVdecQueryAttrEx` (0xC982A84A) → `cellVdecQueryAttr` and
  `cellVdecOpenEx` (0x0053E2D8) → `cellVdecOpen`; the Ex structs start with the plain structs'
  fields (TypeEx = {codecType, profileLevel, codecSpecificInfo}). codecType 0 = MPEG-2.
- **Result:** the movie runs to the end (2,946 AUs, ~98 s, then `EndSeq`). Frames are black because
  the build has no FFmpeg (`-DPS3RECOMP_FFMPEG_RUNTIME=ON` needs FFmpeg headers at build time;
  `avcodec-63.dll` is in the RPCS3 folder). Not needed to progress.

## 11. GPU never saw the game's frames (FIFO) — FIXED

- **Symptom:** after the movie only 6 report/zcull methods per frame reached the runtime; the
  FIFO walker "circled" in the default ring; no draws.
- **How J-Stars submits:** its own GCM context (`gCellGcmCurrentContext` @0x0096E12C → 0x0096E11C,
  callback 0x8BED48) with segments at IO 0x11000 / 0x311000 / 0x611000 / 0xA11000 (EA 0x45311000…).
  Per frame, in the current segment: render thread (tid 19) writes a z-cull/report subroutine
  ending in RET at +0x00..+0x30; main thread writes `CALL <draw list segment>` and
  `CALL <segment>+0` at +0x34/+0x38, then flip and a semaphore acquire. Between frames it calls
  `cellGcmSetDefaultCommandBuffer` (3×/frame) and writes into the **default ring**.
- **Cause 1 (the real one):** runtime `cellGcmSetDefaultCommandBuffer` only zeroes a host struct;
  libgcm repoints `gCellGcmCurrentContext` at the default context. So the game's default-ring
  writes went into its own segment and the GPU never got the hand-off. (The runtime's opt-in
  `GCM_DEFAULT_CTX_REPOINT=1` also *rewinds* the default context to its start each call — that
  breaks J-Stars.)
  **Fix:** port override in `stubs.cpp` (NID 0xBC982946): write the default context EA
  (0x0F800000) into 0x0096E12C, no rewind.
- **Cause 2 (walker):** a `RET` with no `CALL` outstanding jumped to IO 0. Now skipped
  (`libs\video\cellGcmSys.c`, local edit; RPCS3 also ignores a stray return). With fix 1 it is no
  longer hit, but kept.
- **Result:** 1 flip per real frame, no circling/resync, no data decoded as commands. Each frame:
  z-cull block → `CALL` draw list → flip. **The draw list itself is just `RET` — the game has
  nothing to draw yet** (see §12).
- Diagnostics added to the walker: `GCM_CIRCLE_DUMP=1` (dump trace on circling); the 64-entry
  trace now skips zero words.

## 12. Post-movie: empty scene — SOLVED by §14 + §15

- Opening movie can be skipped with `PAD_SCRIPT="30:0x4000,33:0x4000,…"` (CROSS); it starts ~29 s
  into boot.
- **Correction (from a second AI assistant, verified):** the character-image resources are queued together by
  `func_0042EDC8`: first `_v.pak` (slot 1), *then* `_m.pak` (slot 0), and both load. `_v` (9.47 MB at
  CPK 0x3C25000) is read first, `_m` (1,328 B at 0x3C24800) after. Earlier notes saying "`_v` is
  never requested" were a misreading of the log tail.
- Resource completion chain (second AI assistant, verified at run time with the `res` console command):
  1. Task state `T+0x24 == 4` is set by `func_0052AEB8`.
  2. Resource manager `func_0040F3C0` clears handle busy `H+0x10`.
  3. Container `func_00424A64` (vtable slot +0x14) reports not busy.
  4. Scene query `func_002F01FC` (scene vtable 0x8822C8, slot +0xA0) checks registry+0x58 and +0x64.
  5. Scene state machine `func_00444088` (`scene+0x3C`) goes 0 → 1 (wait) → 2 → 3 → 4 (done).

  Addresses:
  - Registry = `*(0x95BD9C)`.
  - Resource manager = `*(0x95BBF4)`.
  - Character container = registry+0x58 (vtable 0x885720).
  - Second container = registry+0x64 (vtable 0x8857D0, R+0x28 = -1, no tasks).
- **Measured:**
  - Both character handles loaded (T+0x24 = 4, H+0x10 = 0), and the busy query returns 0.
  - `func_00444088` ran 46 times. Its last call entered with **state 4 (done)**, and then it stopped
    being called.

  So the resource wait is **not** the blocker: that scene completed. What comes next is still
  unknown.
- Seen after the movie: `cellSearchInitialize` (0xC81CCF8A) and later `cellSearchFinalize`
  (0xBFAB7616). Both are unresolved (faked OK, no callback).
  - A second AI assistant: the init callback (event 1, result 0) must arrive *after* Initialize returns, or the
    game's object+0x04 stays 0 (`func_003EFA20` / callback `func_003EF680`).
  - Not yet shown to block anything; the game does call Finalize.
- Tried, no effect:
  - `cellNetCtlGetState` → CELL_OK/Disconnected (kept).
  - Pressing START/CROSS.
- `func_0060ADD8` is a generic server "kick" (`sys_event_port_send(obj+0x1C)`), not a file request.
- Next: compare with an RPCS3 run of the same boot (file opens and HLE calls after the movie) to see
  what a working boot does next.

## 13. Second-boot race, robust version

- The 1 s DataCheck delay (§9) was not enough when logging slowed the stream (race came back).
  **Fix:** `jstars_game.cpp` now waits after a successful game-data DataCheck until file I/O has
  been quiet for 750 ms (min `JSTARS_DATACHECK_MS`=1000, max 15 s), using
  `g_ppu_fs_last_read_ms` (new export in `ppu_fs.cpp`). Typical hold: ~2 s.

## 14. cellSearch / cellMusic callbacks (post-movie stall, part 1) — FIXED

- **Found by diffing an RPCS3 reference run** (DEBUGGING.md, "Reference run in RPCS3"). After the
  movie RPCS3 logs this sequence:
  1. `cellSearchInitialize`
  2. ~17 s later, `cellSearchFinalize`
  3. `cellSearchInitialize` again
  4. `cellMusicInitialize2SystemWorkload` and `cellMusicSetVolume2`

  Our port reached `Finalize` but never reached the cellMusic init.
- **Cause:** neither library is in the runtime. The imports were faked as CELL_OK, so no result
  callbacks were ever sent. The game's search callback `func_003EF680` (OPD 0x008AD010) takes
  `(event, result, param, userData)`. Event 1 (INITIALIZE_RESULT) sets the search object's +0x04
  to 1, and event 2 (FINALIZE_RESULT) continues to `func_003EEBE0`.
- **Fix:** new `port\jstars_sysutil.cpp`:
  - `cellSearchInitialize` / `Finalize` queue INITIALIZE_RESULT / FINALIZE_RESULT through
    `cellSysutilQueueGuestCallbackArgs`, delivered at the next `cellSysutilCheckCallback`, as RPCS3
    does.
  - The cellSearch search calls return NOT_SUPPORTED_SEARCH (there is no media library).
  - The cellMusic2 calls (Initialize2SystemWorkload, Finalize2, Set/GetVolume2,
    GetPlaybackStatus2 = STOP, SetPlaybackCommand2, SetSelectionContext2) queue their result
    events to the `(event, param=result, userData)` callback.
- NIDs: 0x61865281 = cellMusicInitialize2SystemWorkload, 0xF9073A24 = cellMusicGetVolume2,
  0x64FB0B76 = cellSearchStartContentSearchInList, 0xED20E079 =
  cellSearchGetMusicSelectionContextOfSingleTrack.

## 15. Unlifted SPU jobs (post-movie stall, part 2) — FIXED

- After §14 the game moved on and dispatched SPU jobs we had never lifted:
  - CRI **AtomDecoder** (HCA/ADX audio decode)
  - a set of game jobs: 7E7E00, 7EC700, 813580, 852200, 857F80, 85E080, …
- The runtime logs only the first 32 MISS lines (`[spurs-job] dispatch MISS`), so check
  `run\spu_miss\` for dumps rather than trusting the log.
- **Every job is a JOBCRT binary embedded in EBOOT.elf.** `jstars\spu_jobs\scan_jobcrt.py` finds all
  18 and computes each one's exact `sizeBinary`:
  - The header word at +0x3C is the offset of the `%SPURS JOB INFO%` block minus 0x10.
  - The info block ends with a list of (off, size) pairs terminated by `ff*8`.
  - The size is that end rounded up to 16 bytes.
- The fingerprint is FNV-1a-64 with the runtime's **truncated** offset basis 1469598103934665603
  (not the standard 14695981039346656037). The standard basis gives fingerprints that match
  nothing.
- **Fix:** `jstars\spu_jobs\lift_all_jobs.py` lifts every job not yet lifted (code end = last
  `bi $lr` before the data area) and **regenerates** `port\spu\spu_workloads.c`. All 18 are now
  registered as images 1–18 (in EBOOT address order), and there are no more MISS lines.

## 16. First drawn frames (2026-10-19)

- With §14 + §15 the game leaves the post-movie stall: thousands of `draw_arrays`/`draw_indexed`
  calls per second, real 3D geometry, and a moving camera on what looks like the title/press-start
  scene (seen by the user too). Screenshot: `jstars-notes\first_draw.png`.
- **It renders as flat white silhouettes**, with some spiky triangles fanning from one point.
  Fragment programs are translated (`[FP] guest FP pipeline ready`), so the cause is in the D3D12
  backend: textures, fragment constants or vertex attributes. That is the next phase.
- Window colour: the clear colour was dark blue/purple during the old stall and is black now.

## 17. Title screen and menus render — FIXED (2026-10-20)

All fixes are runtime edits in `libs\video\rsx_d3d12_backend.c` unless noted. Debugging used the
backend's frame capture: `RSX_CAP=start:count:stride` + `RSX_CAP_DIR` writes one manifest per frame
(every op: target RT, fragment program, textures) and every render target as a BMP.

- **White silhouettes / black screen.** The game renders the whole title into the 1280x720 RT
  `0x0C6ABF40`, then a gamma pass (`exp2(log2(c)*k)`) copies it to the display. The backend
  replaces any display-sized texture with a copy of the backbuffer (`SCREEN_AS_TEX`, a heuristic
  from another title), so the final pass sampled black.
  - Fix: a texture that is a tracked offscreen RT is always sampled as that RT.
- **Depth-only passes went to the screen.**
  - The match's shadow cascades (2048x1024 and 1024x1024, `SET_SURFACE_COLOR_TARGET` = 0, colour
    offset 0) were classified as "display" by `current_rt_off()`. They drew white silhouettes and
    used up the vertex buffer.
  - Fix: draws and clears with no colour target are skipped (`rsx_no_color_target()`). Shadows
    are not rendered for now.
- Result: title, "Press any button", the save-data dialog and "Now loading…" all render
  correctly at ~30 fps. Screenshot: `first_draw.png` is the old silhouettes; the title is in
  `title_screen.png`.
- Note: the test's `PAD_SCRIPT` CROSS presses can land on the title and open the save dialog.
  Use `PAD_SCRIPT="31:0x4000"` (the movie then plays in full; the title comes at ~140 s and the
  idle demo match at ~190 s).

## 18. 3D match (idle CPU-vs-CPU demo) — IN PROGRESS

Fixes so far (runtime edits):

1. **16 texture units.** The root signature had t0-t3/s0-s3; J-Stars' character FPs sample units 4,
   7 and 8, so their pipelines failed with `E_INVALIDARG`. Now `RSX_TEX_UNITS` = 16.
2. **Failed pipelines cached** (`PSO_NOTE_FAIL`). They had been recompiled on every draw.
3. **FP pipeline cache keyed on code, with constants buffered.**
   - The backend decompiled FPs with constants baked in as literals, while its cache key ignores
     constants. Hits could reuse another draw's constants.
   - The key also included the address, and J-Stars copies every draw's FP into a ring (2,482
     compiles in one run).
   - Now the buffered decompile is used, with the HLSL `cbuffer` rewritten to the backend's b1
     layout (texscale[4], alphatest, fp_k[251]), and the cache keys on `rsx_fp_code_hash`.
     Compiles on the title dropped from 2,482 to 28.
4. **NV308A inline transfer capped at 256 words** (`cellGcmSys.c`).
   - The COLOR method range is 0x0400-0x1FFC (1792 words); the runtime accepted only up to 0x07FC.
   - Every fragment program longer than 1 KB arrived truncated: no END instruction, zeros after
     0x400. This was found by dumping one with `FP_FAILDUMP=1` and walking it offline.
5. **FP branch instructions** (`rsx_fp_decompiler.c`). Their SRC1/SRC2 words are addresses whose low
   bits looked like "constant" register types. The size, constant-collection and decode walkers
   now skip constant detection for branches.
6. **Vertex budget.** `MAX_VP_VERTICES` = 1,048,576, separate from `MAX_VERTICES`. The old 393k
   buffer was filled by one pass, and every later draw in the frame (post-processing, UI, the final
   display pass) was dropped.
7. **Triangle strips drawn as strips** (`upload_strip_vp_native`).
   - Before, strips were expanded to triangle lists: 3x the vertices, primitive restart ignored
     (the "spikes"), and each <=256-index FIFO batch handled on its own.
   - Now restart-index pieces are joined with degenerates, batches continue one record, and the
     topology is set per draw.
   - `rsx_commands.c/.h` now track `restart_enable`/`restart_index` (0x1DAC/0x1DB0).
8. **VS cache.** 16 slots with `memmove` eviction renumbered every slot, so pipelines missed and
   recorded draws pointed at the wrong vertex program. Now 256 slots with in-place round-robin
   eviction.
9. **PS bytecode cache** keyed by the HLSL text, so one fragment program is compiled once,
   not once per pipeline.

Still open (round-2 review):
- the match renders **black**;
- it runs at **<1 fps**, spending most of the time creating ~700-1000 pipelines per report window;
- the geometry in the captured RT was scrambled (before fix 7).

Diagnostics added:
- `FRAME_BUDGET=2` (peak vertices per 300 frames);
- `PSOMISSDBG=N` now names exp32/cmask/cull and the VS indices;
- `[FP] decompile fail` dumps the size, constant count and first bytes;
- `FP_FAILDUMP=1` writes `fp_fail.bin`;
- the FP PSO failure prints the debug-layer messages. They need Windows "Graphics Tools", which
  is not installed.
- Port `main.cpp` now makes stdout unbuffered, so the end of a killed run's log is not lost.

## 19. 3D match, review round 2 + bisect (2026-10-20) — IN PROGRESS, paused here

A second AI assistant's round-2 review: its findings are mostly confirmed. All
fixes below are runtime edits (uncommitted, `git diff` in `ps3recomp-src`).

**Fixed / measured**

- **VP identity hashed the whole microcode store.** `vp_get_vs()` hashed from the start slot to
  `vp_ucode_bytes`, the store's high-water mark, so every other upload re-keyed the active VP. It
  now hashes the slot through END plus the start slot.
  - Result: pipeline misses fell from 700-1000 to 0-250 per `[PERF]` window.
- **VP branch base never set.** The backend never called `rsx_vp_set_branch_base`; it now passes
  the start slot. Switch: `VP_NO_BRANCH_BASE=1`. Bisect showed it is not the cause of the black
  screen.
- **VS-cache eviction of pending draws.** Slots named by a draw in the current batch are not
  evicted (`s_vs_batch` / `s_vs_last_batch`).
- **PSO key and counters.** `exp32` was dropped from the key (it is unused by the decompile). The
  miss counter now counts only real compiles.
- **Offscreen RT limit.** `MAX_OFF_RTS` went from 16 to 48. A match frame uses 17 RTs, and the HUD
  composite `0x0C6ABF40` (the final pass's source) got no slot.
- **SRV heap regions overlapped.** Legacy texture SRVs (1..96) overlapped RT SRVs (5..20) and the
  per-draw windows (from 32). Each region now has its own range: `RT_SRV_BASE` = 97,
  `DRAW_SRV_BASE` after that.
- **Vertex fetch** (`rsx_vertex_fetch.c`):
  - CMP (type 6, packed 11/11/10) is now decoded;
  - `SET_VERTEX_DATA_BASE_OFFSET/INDEX` (0x1738/0x173C) are applied. J-Stars always sets them to 0.
    Switch: `RSX_VBASE_OFF=1`.
- **Strips:**
  - a u16 index buffer with a restart sentinel above 0xFFFF disables restart;
  - continuation keys on the BEGIN/END epoch;
  - the index base is resolved once per batch.
- **Presenter.** `RSX_FRAMELOG=N` showed demo frames *are* complete and presented through the flip
  path: 258-406 records, 1 display draw, 380-560k vertices, nothing dropped,
  `clear_presents=0`. A safety flush was added for offscreen-only batches near capacity.
- **Speed.** The match runs at ~1.5-2.5 fps. `PERF` shows **vertex fetch is 85-96% of frame
  time** (~1.6 µs per vertex, ~7M vertices per 20 frames). It is single-threaded CPU decode of
  all 16 attributes per vertex. This is the next performance task: a per-draw fetch plan, only the
  attributes the VP reads, and multithreading.

**The open problem: native strips rasterize nothing**

- Measured with `RSX_CAP` frames 5660/5690 and pixel counts over the BMPs:
  - **strip expansion** (old path): the main RT `0x0ED10000` has content (1890 / 8492 non-black
    samples);
  - **native strips** (`upload_strip_vp_native`): main RT completely black, even with
    `CULL_FLIP=1`, with `CULL_OFF=1 DEPTH_OFF=1` (97 / 0 samples), and with `FP_IDCOLOR=1` (0).
  - So nothing rasterizes: it is not shading, culling or depth.
- `STRIP_DUMP=<frame>` (record side) shows valid data:
  - u16 indices, restart enabled with 0xFFFF, mostly 3-index pieces separated by restarts;
  - the joins look right;
  - decoded positions are sane model-space values.
- `[STRIPDRAW]` (replay side) shows each strip record submitted with the guest-FP PSO, RT 6
  (`0x0ED10000`), viewport 1280x720, cull 0x1, sensible vertex counts and contiguous start
  vertices.
- No device removal is logged. The D3D12 debug layer is unavailable (Graphics Tools is not
  installed).
- **Default is back to the expansion path.** Native strips are opt-in with `STRIP_NATIVE=1`.
- Ideas for next time:
  1. Install Graphics Tools and run `D3D12_DBG=1` with `STRIP_NATIVE=1`: does the debug layer
     reject `IASetPrimitiveTopology(TRIANGLESTRIP)` / the draw?
  2. Read back the vp_vb range of one strip record after the frame and compare it with what was
     written. Is the GPU reading the right parity half for these records?
  3. Try drawing a strip record as a triangle list of the same vertices (garbage shape, but it
     shows whether the record's data rasterizes at all).
  4. The reviewer's other open notes:
     - fragment-program code can change in the ring between record and replay; store a hash
       per draw and compare at replay;
     - stride-0 and missing-component defaults differ from the live engine;
     - `current_rt_off` display classification.
- The **window is still black even when the main RT has content** (bis2: backbuffer black). After
  native strips, trace why the final display pass (op `rt=0`, sampling `0x0C6ABF40`) outputs
  black: check that `0x0C6ABF40` now has content with MAX_OFF_RTS 48, then the composite FP.


### 2026-10-20 (day 3): snapshot + first capture with MAX_OFF_RTS 48

- Snapshot: toolkit edits committed on local branch `jstars-port` in `ps3recomp-src` (c0dbbd4,
  not pushed); `jstars\port` copied to `jstars\port-backup-2026-10-20` (without `build\`).
- Capture `run\rsxcap_rt48` (RSX_CAP 5650:2:40, default strip expansion):
  - frame 5650 (partial, scene only): main RT `0x0ED10000` 73% non-black;
  - frame 5690 (complete): `0x0C6ABF40` now has a slot but is **all black**; backbuffer black.
- Post chain in frame 5690 (all fullscreen n=6 passes): op1389 writes `0x0DE10000` from
  t0=`0x0E590000` (cleared to (0,0,0,1), never drawn) + t1=main RT. Every blur/glow RT after it
  (0DE1, 0D65, 0D7A, 0D7F, 0D84, 0D89, 0DA4) is black.
- op1394 and op1432 write the **main RT with blend off** from those black RTs only, so the scene is
  overwritten before op1433 copies it into the HUD composite.
- Suspect: `0x0E590000` is really depth or a second scene output (MRT) that this backend cannot
  sample; or those passes read units >= 4 (the manifest lists only t0-t3).
- Next: per-pass main-RT snapshots in RSX_CAP plus FP dumps of ops 1389/1394/1432. (Continued and measured with scripts in §20.)


## 20. Black screen: what capture `rsxcap_rt48` shows (2026-10-20) — OPEN

Capture: `jstars\run\rsxcap_rt48` (`RSX_CAP=5650:2:40`, default strip expansion, MAX_OFF_RTS 48).
Numbers below come from `tools\capstat.py` / `tools\capops.py` (lit % = share of sampled pixels
with R+G+B > 6; frame_00 = frame 5650, partial, scene only; frame_01 = frame 5690, complete).
The scripts only read the capture; nothing was run or built for this section.

**Evidence**

| Target | Frame 5650 | Frame 5690 | Role |
|---|---|---|---|
| `0ED10000` main scene RT 1280x720 | **73.7 %** (1721 writes) | **0.0 %** (1196 writes) | scene, then post |
| `0C6ABF40` HUD composite 1280x720 | not in frame | **0.0 %** (18 writes) | source of the final display pass |
| backbuffer | 0.0 % | 0.0 % | final op1451 samples `0C6ABF40` |
| `0E590000` 1280x720 | 0.0 % | 0.0 % | cleared to (0,0,0,1) at op1387, **never drawn** |
| `0DE10000`, `0DA40000`, `0D65..0D89` blur/glow RTs | | 0.0 % | all post-chain outputs black |
| `09BE5E80` 256x256 | 0.4 % | 68.8 % | drawn by 215 geometry draws, then read once (op1386) |
| `09C65F00` 256x256 | 0.0 % | 41.1 % | op1385 samples the main RT |
| `039FFA40` 20x11, `0D6A0000` 80x45 | | 20 %, 42 % | luminance-style downsample chain, has content |
| `0DB30000`, `0DD10000` 640x360 | 100 % (127,127,127) | 100 % | cleared to 0.498 grey, never drawn |

**The frame, in order** (`capops.py frame_01 --summary`)
1. op0-1167: geometry into the main RT (`0ED10000`). Before the post chain it has content.
2. op1168-1383: 215 draws into `09BE5E80`; op1385 downsamples the main RT into `09C65F00`.
3. **op1386** (fp `0FC7E301`, vs 56, n=6, blend off): writes the **main RT** from `09BE5E80` +
   `09C65F00`. Both inputs have content, yet the main RT ends at 0 %.
4. op1387 clears `0E590000` to black; op1389 writes `0DE10000` from `0E590000` + main RT.
5. op1390-1393 downsample chain; **op1394** (fp `0FC7EC81`) and **op1432** (fp `0C6AA881`) write the
   main RT again with blend off, from `0DE10000`/`0DC20000` and `0D890000`/`0DE10000`/`0D840000`.
6. op1433 copies the main RT into `0C6ABF40`; 12 geometry draws (HUD, fp `0D64F201`) and four
   n=6 draws (cmask=7) follow; op1451 draws to the display.

**Reading of it.** The scene is produced correctly but is replaced by a fullscreen pass early in
the post chain and never comes back. Two readings fit, and the capture cannot separate them
because only end-of-frame content is saved:
- **A. op1386 (or 1394/1432) outputs black** because its fragment program is mis-translated, or it
  reads a texture the backend cannot give it (see B).
- **B. A texture unit >= 4 or a second source is missing.** The manifest lists only t0-t3. The
  untracked linear textures sampled in frame 5690 are `00160100`, `0455CC80`, `04565500`,
  `0456B500`, `04571500`, `0E960000` (op1395 reads `0E960000` as t0; it is not an offscreen RT).
  `0E590000` is cleared but never drawn, so op1389 sees a flat black input; if it is really
  depth or an MRT output the backend never fills, the chain collapses to black.
- Not explained: whether op1386's black result is the first break. Nothing here shows the main RT
  between ops.
- Cleared 0.498-grey RTs (`0DB30000`, `0DD10000`) are never drawn either; they look like unused
  or lazily filled targets, not the blocker.
- Every op in both frames has `mrt=0x0/0x0/0x0`: **no MRT is ever bound**, so a second scene output
  is not the reason `0E590000` stays empty unless it comes from another path (depth readback,
  inline transfer, a copy the backend ignores). Worth checking how the game fills it.

**Next ideas** (ordered by cost; none done)
1. `RSX_CAP` per-op snapshot of the main RT after each of ops 1386, 1389, 1394, 1432, 1433 (needs
   a backend change by the AI assistant). Answers "which op blackens it".
2. Dump the FPs of ops 1386/1389/1394/1432 (`FP_PICK=<hex>` for `0FC7E301`, `0FC7E981`,
   `0FC7EC81`, `0C6AA881`; output is `run\duck_fp.hlsl`) and read which texture units and
   constants they use. Look for `tex2D` on units 4+ and for texture formats the backend maps wrong.
3. Extend the manifest writer to list all bound texture units (0-15), not t0-t3.
4. Find what is supposed to fill `0E590000` (is it a depth copy, `SET_SURFACE_ZETA`, a
   `cellGcmSetTransferImage` blit?). Log any GCM transfer/copy targeting that offset.
5. `FP_IDCOLOR=1` on the post-chain passes only: a flat colour per FP shows which passes rasterize.
6. Install Windows "Graphics Tools" and run `D3D12_DBG=1` (also needed for the native-strip question).
7. Remaining items from §19: native strip rasterizing nothing, vertex-fetch speed (per-draw fetch
   plan, multithread), FP code ring changing between record and replay, stride-0 defaults.

**Run-to-run fact:** `run\dbg.txt.out` for this capture: uptime 265 s, 5828 flips.


## 21. Vertex-fetch speed-up: draft patch, not yet run in-game (2026-10-20)

Patch + explanation + tests: `patches/vfetch-fast/` (`vfetch-fast.patch`, `README.md`). Touches
`rsx_vertex_fetch.{h,c}` and `rsx_d3d12_backend.c`, so applying, building and measuring it belongs
to the AI assistant. `git apply --check` passes on the main checkout as of today.
- Per-draw fetch plan; only the attributes the VP reads (`rsx_vp_analyze_inputs`, all 16 when
  not exact); whole 256-byte vertices streamed to the upload heap; a pool of 3 workers for draws
  of 2048+ vertices. `VFETCH_LEGACY=1` restores the old path.
- Standalone: bit-identical to `rsx_fetch_attrib` on 4500/4500 random cases. In a real upload
  heap, 0.053 -> 0.018 us/vertex on one thread; 400k vertices take 7.3 ms on one thread and
  4.6 ms on 3+1.
- **Open question:** in-game `[PERF]` showed ~1.6 us/vertex, 30x the old code's isolated cost.
  The patch adds CPU cycles/vertex to `[PERF]`. If that number is low, the fetch thread is
  starved or stalled, not busy, and the fix lies elsewhere.

## 22. Black match screen: the vertex buffer overflows every frame (2026-10-20) — CAUSE FOUND

- New capture switch `RSX_CAP_PASSES=1` (commit c8f408e on `jstars-port`): on a captured frame it
  saves the bound offscreen RT before/after every full-screen pass (`pass_opNNNN_{pre,post}_rt_*.bmp`)
  and writes `fp_opNNNN.hlsl` (decompiled FP, constants, all 16 texture units) per pass.
- Captures `run\rsxcap_pass1..3`. Frame numbers shift between runs: in one run the match began at
  backend batch ~5760, in another before 5650. Use `RSX_FRAMELOG=1` to find match batches.
- **Cause:** with `RSX_FRAMELOG=1`, 717 consecutive match batches read
  `records=1726 display_draws=0 vp_verts=1048575/1048576 present=0`. The scene (strips expanded to
  lists) fills all of `MAX_VP_VERTICES` part-way through. Every later op that frame (rest of the
  scene, the post chain, the HUD composite, the display pass) gets no vertices and is not recorded.
  At the next frame's clear the batch has no display draw, so the near-capacity path
  (`[CLEAR] ... flushing without present`) runs it without presenting. Nothing is ever presented
  during the match, so the window stays black. The uploads clip silently (`dropped=0`).
- The earlier `rsxcap_rt48` frame 5690 (with a post chain) was one of the rare batches that fit.
- Proposed fix (awaiting go): when an upload would not fit, run the recorded batch without
  presenting, reset the vertex buffer, and keep recording the same frame. Native strips (no 3x
  expansion) would also cut the budget, but they still rasterize nothing (§19).

### §21 follow-up: vertex-fetch patch built and run (2026-10-20)

- Applied as commit 299aa12 on `jstars-port` (after c8f408e). Built and ran the 275 s demo; no
  crash or device removal.
- Match, backend batches per second (`[PERF]` "fps", windows under 20 fps after the title):
  old code 8.78 (run `rsxcap_pass3`, which also had `RSX_FRAMELOG=1` and a capture) vs new 11.40.
  About 30% faster, but the old run carried extra logging, so treat it as approximate.
  Logs: `run\perf_legacy_pass3.txt`, `run\perf_vfetch.txt`.
- **cyc/vert is not measurable yet:** `vtx` reads `0k verts` for the whole run, old and new. The
  match's main path (strip expansion to lists) is not inside the timed sections, so neither the
  wall time nor the cycle counter covers it. The old §19 figure (85-96% vtx) predates this layout.
- Guest time is still ~82% of each window.

### §22 follow-up: mid-frame flush applied (2026-10-20)

- Commit b7efcb5 on `jstars-port`: `vp_flush_if_full()` runs before every VP upload (quads, tris,
  strips/fans, indexed and not). If the upload might not fit (guest count x3), or a new stream
  starts with < 64k vertices free, or records near `MAX_DRAWS`, the batch is rendered without
  present and recording continues in an empty one. A batch continuing the previous record's
  stream is only split if it truly does not fit. `VP_NO_MIDFLUSH=1` = old behaviour.
- Result: demo batches now carry the display draw and present (`records=387 display_draws=1
  present=1` late in the run). The overlay, "Press any button" logo and the character intro card
  appear (`demo_after_midflush.png`, `run\shots_midflush\`). **The 3D background is still black.**
- `[PERF]` late in the run: ~15.5 batches/s.
- Next: `RSX_CAP_PASSES=1` capture of the presenting frames (`run\rsxcap_flush1`).

## 23. 3D background black: tone-map input written with the wrong output register (2026-10-20) — CAUSE FOUND

Capture `run\rsxcap_flush1\frame_00` (presenting batch, 387 ops, `RSX_CAP_PASSES=1`):
- Main RT `0x0ED10000` holds the scene (64% lit) right up to op325.
- op320 (fp `0x0FC9F901`) builds `0x0DE10000` from the scene (t1) and `0x0E590000` (t0). Its
  result is blue only (0, 0, 18.6 mean).
- op325 (fp `0x0FC9FC01`, the tone-map: luminance with 0.2125/0.7154/0.0721, exp2/log2, gamma
  0.4545) reads `0x0DE10000`, divides by its alpha and writes the main RT. Output: all black.
- **Cause:** in `fp_op0320.hlsl` the program writes `r0.z` once (an intermediate) and computes its
  final colour in `h0`, but the shader returns `r0`. The backend calls the decompiler with
  `RSX_FP_CTRL_AUTO`, whose fallback heuristic is "r0 written => export r0". So the pass exports
  (0, 0, z, 0): blue only, alpha 0. The tone-map's divide by alpha gives NaN, which the decompiler's
  NaN guard turns into 0, and the scene is replaced by black.
- The draw record already has the real bit (`fp_exp32` = SHADER_CONTROL & 0x40), but
  `vp_get_fp_pso` ignores it (`(void)exp32`) and `exp32` was dropped from the PSO key in §19.
- Proposed fix: pass `exp32 ? 0x40 : 0` as the decompiler's ctrl and put `exp32` back in the FP
  PSO key.

## 24. Where the match's frame time goes: timers and the present loop (2026-10-20) — patches ready, not run

Patches + write-up: `patches/perf-timers/` (`perf-timers.patch`, `jstars-main-kick-wait.patch`, `README.md`).
- The vtx timer never covered the match: it was inside the tris/native uploads, and the match uses
  `upload_strip_vp_indexed`, so `vtx` showed 0k verts. The §19 figure (1.6 us/vertex) is from an
  older draw path and does not describe this build. The fps gain from the fetch patch (114 -> 88 ms
  per batch at ~1M vertices) fits the standalone ~0.035 us/vertex saving (inferred, not measured).
- "guest" in `[PERF]` is not game logic. It is the present thread's time outside render_frame:
  the FIFO drain (including all vertex expansion), the vsync Present (after render_frame's timer),
  the loop's `Sleep(4)` and the vblank handlers.
- `jstars\port\main.cpp` idles with `Sleep(4)`. cellGcmSys's FIFO kick event is created by its first
  waiter (`cellGcm_fifo_kick_wait`), which J-Stars never calls, so a guest fence wait can never wake
  the loop. `boot_main.cpp` already uses the kick wait. Each fence round trip may cost up to ~4 ms
  (hypothesis).
- `perf-timers.patch` (measurement only): times every VP upload at its call site and adds a
  `[PERF2]` line (drain time and calls, mid-frame renders, Present, rest).
  `jstars-main-kick-wait.patch` (behaviour): `Sleep(4)` -> `cellGcm_fifo_kick_wait(4)`
  (`GCM_FIFO_KICK=0` reverts). For the PPU side, `PS3_SAMPLE=2` has never been run on J-Stars.

### §23 follow-up: export-bit fix applied (2026-10-20)

- Commit 35b1f12 on `jstars-port`: `vp_get_fp_pso` passes `exp32 ? 0x40 : 0` as the decompiler's
  ctrl (depth-export bits stay off) and keys the FP PSO cache and failure cache on `exp32` again.
  `cap_write_fp` decompiles the same way and prints `exp32=` in each `fp_opNNNN.hlsl`.
  `FP_CTRL_AUTO=1` restores the old heuristic.
- Title screen re-checked after the fix: unchanged (`run\shots_title_exp32\shot_055s.png`).
- **Result: the demo's 3D scene renders** (`demo_after_exp32.png`, `run\shots_exp32\`): stage,
  water with reflections, ship, and the Ace intro behind the card.
- Still wrong, for next time:
  - large flat black triangles/blobs across characters and props (possibly strip-expansion joins,
    skinning, or more FPs whose output was mis-picked before; compare with `STRIP_NATIVE`);
  - smeared or stretched textures on some surfaces;
  - shots at 230 s and 265 s are identical: the intro may be frozen or just slow (~15 batches/s).

## 24. Where the demo's time goes: perf-timers run (2026-10-20)

- `perf-timers.patch` applied as commit bce3ec9 on `jstars-port` (measurement only). The
  kick-wait patch is **not** applied (held until the graphics glitches are sorted).
- One PERF=1 run (`run\perf2_run.txt`), late demo, per 20-batch window (~1.27 s, ~15.7 batches/s):
  - `render_frame` 56%, of which the prepass SRV setup is **45%** (~187k texture calls per window,
    ~9.4k per batch);
  - `vtx` **25%**: 12.7M vertices per window (~630k per batch) at **~60 CPU cycles per vertex**.
    Each vertex is cheap; the volume (strip expansion) is the cost;
  - `[PERF2]`: drain 75% (includes vtx and 34% mid-frame renders), present ~0%, rest **2%**.
    The present loop is not pacing the guest, so the kick-wait patch should matter little here.
- The intro is **slow, not frozen**: screenshots at 200/215/230/245/260 s all differ (Ace's cloth
  moves), `run\shots_perf2\`.
- Next perf targets: the per-draw texture/SRV prepass, then fewer vertices (native strips or a
  compact vertex).

## 25. Black triangles: strip expansion ignored primitive restart (2026-10-20) — FIXED

- The default path (`upload_strip_vp_indexed`, used unless `STRIP_NATIVE=1`) expanded every three
  consecutive indices into a triangle. Match strips are mostly 3-index pieces separated by restart
  0xFFFF (§19 `STRIP_DUMP`), so every piece also produced triangles through vertex 65535: garbage
  positions, i.e. the large flat black triangles. It also kept odd triangles' winding (culled) and
  lost the triangles at each <=256-index batch seam.
- Commit e32a781: `upload_strip_vp_indexed_rsx()` ends a piece at the restart index (same u16 rule
  as the native path), swaps the first two vertices of odd triangles, and carries the last two
  indices and parity into the next batch of the same BEGIN/END (`s_sx`, keyed on begin_epoch and
  index_array_offset). `STRIP_EXPAND_OLD=1` = old expansion. Fans keep the old code.
- Result (`demo_after_strip_restart.png`, `run\shots_strip\`): black blobs gone; Ace's hat, beads,
  face and hair now readable. Still wrong: models look cracked (missing triangles?) and many
  surfaces are pale or untextured. Testing `CULL_OFF=1` next.

### §25 follow-up: the cracks are culling (2026-10-20)

- `CULL_OFF=1` (`run\shots_culloff\`): the models fill in solid, so the cracks are culled
  triangles, not missing ones. The pale "camo" patches stay (a separate texture/shading issue).
- `CULL_FLIP=1` (`run\shots_cullflip\`): the complementary set shows. Triangles within one model
  have mixed winding.
- Tried: carry strip state across index batches on the BEGIN/END epoch alone (not also the index
  offset). No visible change (`run\shots_carry\`), so it was reverted and not committed.
- Next: dump one character draw's index stream (piece lengths, where restarts fall, batch seams)
  and check the winding of its expanded triangles against the native-strip rules; also check
  RSX `CULL_FACE` = FRONT_AND_BACK (0x0408), which `rsx_cull_key` maps to FRONT.
- The build in `jstars\port\build` was rebuilt after the revert and matches commit e32a781.

### §25 follow-up 2: strip winding is consistent; cracks are folded triangles (2026-10-20)

- `STRIPX_DUMP=<frame>` (diagnostic in `upload_strip_vp_indexed_rsx`, uncommitted at time of
  writing) writes the first 3000 strip calls' index streams to `run\stripx_dump.txt`: epoch,
  index offset, first, count, carried piece state, restart, cull, fp and every index (`|` =
  restart).
- Pieces often start with a doubled index (`247 247 245 ...`): a stripifier's parity fix.
- Edge-orientation check (scratch script, one stream = same epoch + index offset): with restart
  resetting parity (the current code) **415,296 of 415,296 shared edges are walked in opposite
  directions**, i.e. perfectly consistent. Global parity (with or without restart tokens) gives
  83-85%, no swap 31%.
- So the cracks are not a winding bug. With consistent winding, triangles that cull wrongly must
  be folded over on screen: vertex positions (skinning, attribute decode) are wrong. The pale
  "camo" textures may share the cause (texture coordinates). Next: vertex formats of character
  meshes (`VP_ATTRS=1`, now also called from the strip path).

## 26. Cracked characters: vertex data rewritten after the renderer reads it (2026-10-20) — CAUSE LIKELY

- `VP_ATTRS=1`: character/prop layouts are float3 position, CMP normal (a2), UB4 colour (a3), float2
  UVs (a8-a11), CMP tangent (a14). **No blend weights or indices**: skinning happens on the SPU,
  which writes finished positions that the RSX reads.
- `VRACE_DBG=1` (diagnostic, uncommitted at time of writing): hashes each strip batch's attribute-0
  bytes over its index range when the batch is recorded, and again when `render_frame` replays the
  batch. Result: in most batches **4-35% of strip batches changed after fetch** (e.g.
  `frame 5769: 729 of 2026`, `5838: 626 of 1438`). The renderer reads skinned vertices before the
  SPU has finished writing them (or after the next frame's write started), giving half-updated
  poses, i.e. folded triangles.
- On hardware the FIFO waits on an NV406E semaphore ACQUIRE (method 0x68) until the SPU job
  releases the label. `cellGcmSys.c` treats ACQUIRE as non-blocking unless `GCM_SEMA_ACQUIRE=1`.
  Testing with it on (`run\shots_semacq\`).

## 27. Pale / "camo" surfaces: texture-side causes (2026-10-20) — STUDY, see `PALE_TEXTURES.md`

- Read-only study (nothing built or run). Top two suspects: texture gamma/sRGB bits are never passed
  to the FP decompiler (washed-out colour), and the depth-only shadow cascades are dropped, so the
  material's PCF taps on t7/t8 read unwritten guest memory (sharp-edged blotches). One run with
  `TEX_MARKEMPTY=1` plus a one-line ADDRESS-gamma log answers both. Also: fixed samplers (s0 is
  POINT+CLAMP on the albedo unit), no mips, stale bindings.

## Texture setup cost: memo + counters patch, not yet run (2026-10-20)

Patch + write-up: `patches/tex-setup/` (`tex-setup.patch`, `README.md`), for the AI assistant
to apply.
- `tex` (vp_upload_tex_slot) is 43% of a window: 187,810 calls, ~2.9 us each (`run\perf2_run.txt`).
  Re-binds within a batch are cheap (~0.1 us). Since the mid-frame flush, every held texture is
  checksummed again in each batch (`used` resets per render_frame). Those checksums and any
  re-uploads are the likely cost (inferred from the code).
- The patch adds `[PERF3]` (outcome counts and times per call), a per-batch memo
  (`TEXCACHE_LEGACY=1` = off) that returns only what the scan would, and an opt-in
  `TEX_VERIFY=frame` (checksum once per guest frame, the pre-flush behaviour).
- For the pale textures: `VP_TEX_SLOTS` = 96. More distinct textures in one batch means null SRVs
  for the extras. `[PERF3]` `no-slot` > 0 would confirm it.
- The diagnostics (`STRIPX_DUMP`, `VRACE_DBG`, VP_ATTRS in the strip path) are commit c87c390 on
  `jstars-port`, all off by default.
- **`GCM_SEMA_ACQUIRE=1` did not help.** The game issues NV406E ACQUIRE only on label offset 0x10
  with value 0 (`GCM_RECDBG=1`), and the race counts stay the same (`run\shots_semacq\`). So this
  semaphore is not the skinning sync.
- **Static props crack too** (the teal trees in `run\shots_semacq\shot_230s.png`), so skinning
  races cannot be the whole story. The race counts may also include harmless rewrites made after
  the fetch (the next frame's skinning into a reused buffer).
- Open leads for next time:
  1. per-draw capture of one cracked prop: positions as fetched vs a re-read at replay;
  2. depth: the backend clears its single shared depth buffer on every RT switch and at the
     start of every batch, including the new mid-frame flush batches (§22), so geometry drawn
     after a switch loses depth against earlier geometry;
  3. how the game knows the RSX has consumed a vertex buffer (back-end/texture labels, written
     here when the drain reaches them).

## 27. Depth, outlines, texture slots and gamma (2026-10-20)

- `DEPTH_OFF=1` (`run\shots_depthoff\`): large black shapes cover Ace. They are drawn after him
  (most likely the toon "inverted hull" outlines).
- `CULL_FRONT_SKIP=1` (new diagnostic, commit b219262: drops draws that cull FRONT faces, i.e. the
  outline hulls): the outlines go, **the cracks stay** (`run\shots_skipfront\`). Neither depth nor
  outlines cause the cracks.
- `tex-setup.patch` applied as commit ee1079d. `[PERF3]` per 20-batch window (late demo):
  `memo 156930 | csum-same 880 | new 300 (0.30 s) | no-slot 29700 | created 300`.
  - **~29,700 binds per window found no free slot** (`VP_TEX_SLOTS` = 96). Each gets a null SRV:
    the surface is drawn untextured, and an alpha-tested material reads alpha 0 and is discarded
    (a likely source of the holes/cracks).
  - Almost all texture time is the 300 `new` uploads (slot thrash), not checksums.
- `TEXFMTDBG=1` now prints `gamma=(ADDRESS>>20)&0xF` and the filter word. **DXT1 (0x86) albedo
  units show `gamma=0x7`** (RGB sRGB decode) in 11 of 23 distinct binds, DXT5 in 2. The backend
  ignores these bits, which confirms PALE_TEXTURES.md cause 1 (washed-out colours).
- Next: `VP_TEX_SLOTS` 512 (`TEX_SLOTS=<n>` caps it at run time), then the sRGB decode, then the
  empty shadow-map check (`TEX_MARKEMPTY=1`).
- **Texture slots fixed** (commit 4757a62, `VP_TEX_SLOTS` 512, `TEX_SLOTS=<n>` caps it): `[PERF3]`
  now `no-slot 0 | new 0 | created 0`, texture time 0.55 s -> 0.02 s per window (43% -> 1%), and
  **the sky renders** (`run\shots_slots512\`). Ace's cracks unchanged. With texture time gone, `vtx`
  is now 77% of the window (379 cycles/vertex in that run).
- **sRGB decode fixed** (commit 8ee4e58): the draw record keeps each unit's ADDRESS/FILTER,
  `dr_gamma_key()` builds the lane masks (`rsx_texture_gamma_mask`, limited by
  `rsx_fp_texture_gamma_units`), the FP is decompiled with `rsx_fp_decompile_buffered_color_ex`, and
  the masks are in the PSO key and failure cache. `TEX_GAMMA_OFF=1` = old. Result: **skin, hat and
  hair have their real colours** (`demo` shots in `run\shots_gamma\`). Title screen not re-checked
  yet.
- Tried and dropped: carrying non-indexed strips (DRAW_ARRAYS) across batch seams. The sky's rows
  of missing triangles did not change.
- Still open: Ace's cracks/shards, black triangle gaps in the sky dome.
- Title screen re-checked after 8ee4e58: correct (`run\shots_title_gamma\shot_055s.png`).
- **Shadow maps confirmed empty** (`TEX_MARKEMPTY=1`): `[TEXEMPTY] off=0xCCE40000 2048x1024` and
  `0xCCA30000 1024x1024` (fmt 0xA5). Nothing renders the depth-only cascades
  (`rsx_no_color_target()` drops them), so materials sample untouched memory for shadows
  (PALE_TEXTURES.md cause 2). That run's screenshots were fully black; a rerun of the same build
  without the marker rendered normally (`run\shots_head\`), so it was the marker or timing, not a
  regression.
- The committed build (8ee4e58) matches `jstars\port\build`.

## Strip expansion vertex cost: batched fetch patch, not yet run (2026-10-20)

Patch + write-up + test: `patches/strip-batch/`, for the AI assistant to apply.
- Cause of the 379 cycles/vertex (§27): `upload_strip_vp_indexed_rsx` (e32a781, the match's main
  path) fetches each output vertex with its own one-vertex plan range: staging memset, page-cache
  reset and an sfence per vertex. Each strip vertex is also decoded ~3 times.
- Patch: the loop lists each output's guest vertex index, then one `vf_fetch` decodes the list
  (`RSX_FETCH_VLIST`). `rsx_fetch_plan_range` copies a vertex decoded within the last 4 distinct
  indices instead of decoding it again. Off switches: `STRIPX_PERVERTEX=1`, `VFETCH_NODEDUP=1`.
- Standalone, real upload heap, J-Stars layout, 497k output vertices: per-vertex 0.159 us/vertex
  (matches the in-game 379 cycles), batched 0.029, batched+dedup 0.023. Output is byte-identical
  across all three, and the 4500-case plan test still passes.
- Dedup makes a vertex's three copies identical even when the guest rewrites it mid-batch (§26
  race). Use `VFETCH_NODEDUP=1` to compare the cracks.

## 28. Cracks and sky gaps: gone with native strips (2026-10-20) — FIXED

- `VFETCH_LEGACY=1`: same cracks, so the fetch patch is not the cause (`run\shots_vflegacy\`).
- **`STRIP_NATIVE=1`: Ace, the stage, water and sky render cleanly: no shards, no black triangle
  gaps** (`run\shots_native\`). The remaining blotches on skin are the empty shadow maps (§27).
  The §19 "native strips rasterize nothing" was a symptom of the vertex-buffer overflow (§22) and
  the FP export bug (§23), fixed since.
- Commit 16c9a9d: native strips are the **default**; `STRIP_EXPAND=1` selects the list expansion,
  which still cracks for a reason not found (winding checked consistent in §25; possibly vertex
  data read per output vertex vs once per strip vertex). Native also needs 1/3 of the vertices.
- Commit e6feaa6: `strip-batch.patch` (batched fetch + decode-once for the expansion path;
  `STRIPX_PERVERTEX=1`, `VFETCH_NODEDUP=1` = off). It only affects `STRIP_EXPAND=1` now; the
  native path still decodes one vertex at a time through `vf_one`.

## Shadow passes: depth-only pass patch, not yet run (2026-10-20)

Patch + plan: `patches/shadow-passes/` (`shadow-passes.patch`, `README.md`), for the AI assistant
to apply. `git apply --check` passes on e6feaa6.
- Depth-only passes (colour target NONE + zeta) are recorded into per-zeta D3D depth targets
  (`R24G8_TYPELESS`) instead of being dropped. Materials binding those zeta offsets get the depth:
  DEPTH24_D8 + zfunc through the decompiler's existing compare path, A8R8G8B8 (what the manifests
  show, remap `0xAA93`) through an in-shader Z24S8-to-ARGB bitcast that honours the remap.
- `SHADOW_PASSES=0` = old behaviour, `ZPASS_DBG=1` logs targets and how each unit reads them,
  `ZPASS_ARGB_CMP=1` treats an A8R8G8B8 view with a zfunc as a comparison.
- Checked offline only: syntax-only compile of the patched file (no errors), and the bitcast
  rewrite applied to a real J-Stars material FP compiles with fxc ps_5_0. Expect a slower demo
  (~390k more vertices per frame).
- Timing run with native strips default + e6feaa6 (`run\shots_nativedef\`, PERF): the run reached
  the match only in its last ~4 windows: 1.5 -> 0.7 batches/s, with 169-748 pipeline compiles per
  window (warm-up for the newly visible scene; the gamma masks also add PSO variants) and **vtx at
  ~4,000 CPU cycles per vertex**. The native path fetches each vertex through `vf_one` (a
  one-vertex plan range: staging memset, page-cache reset, store, sfence), and that is the next
  vertex-speed target: give `upload_strip_vp_native` the same vertex-list batching as e6feaa6.
  Before the match, the title/intro windows ran at ~31 batches/s.
- Screens are clean: water, cliffs and sky have no black gaps.

## Native strip vertex cost: batched fetch patch, not yet run (2026-10-20)

Patch + write-up + test: `patches/native-batch/`, for the AI assistant to apply.
- Cause of ~4,000 cycles/vertex (§28): `upload_strip_vp_native` fetches each vertex alone
  (`vf_one`), and every join copies 2-3 vertices of 256 bytes back out of the write-combined upload
  heap. CPU reads from that memory are uncached, and match strips join every few vertices.
- Patch: the loop lists the guest vertex behind each output (a repeat gets its source's index),
  then one `vf_fetch` decodes it. Only a join at `o == 0` still copies the previous call's last
  vertex from the heap. `STRIP_NATIVE_PERVERTEX=1` = old loop.
- Standalone, real upload heap, J-Stars layout, 384k outputs in 256-index continuing calls:
  byte-identical; per-vertex 2.22 us/vertex vs batched 0.031 us.

## 29. Native-strip batching measured; shadow passes applied (2026-10-20)

- Commit 7c57768 (`patches\native-batch`, off: `STRIP_NATIVE_PERVERTEX=1`). Two 330 s runs
  (`run\stderr_nbatch.txt`, `run\stderr_npervert.txt`):
  - batched: match **6-11 batches/s**, vtx 16-29% at **~100-200 cycles/vertex**;
  - per-vertex: 0.7-1.6 batches/s, vtx 59-98% at ~4,000 cycles/vertex.
  - Latest batched window: `vtx 0.37s (20%) 101 cyc/vert | render_frame 6% | [PERF2] drain 31%,
    present 0%, rest 63%`. **The present loop's waiting (`rest`) is now the largest share**, so the
    held-back `jstars-main-kick-wait.patch` is now worth trying.
  - The demo reached Goku's intro, rendering cleanly (`run\shots_nbatch\shot_300s.png`).
- Commit bfae355 (`patches\shadow-passes`, off: `SHADOW_PASSES=0`; `ZPASS_DBG=1` logging,
  `ZPASS_ARGB_CMP=1` compare mode). First run with `ZPASS_DBG=1`: see below.
- Shadow run 1 (`ZPASS_DBG=1`, `run\stderr_shadow.txt`, `run\shots_shadow\`):
  - depth targets: `0x0CE40000 2048x1024`, `0x0CA30000 1024x1024`, `0x0D940000 640x360`;
  - samplers: fp `0xFBFFFC1` units 7/8 as **fmt 0xB0** (depth format) remap 0xAAE4, zfunc 4 ->
    depth compare; material fp `0xD64F4C1` units 7/8 as **fmt 0xA5** remap 0xAA93, zfunc 4 ->
    A8R8G8B8 bitcast;
  - match timing ~9.2-9.5 batches/s, vtx 27-29% at ~90 cycles/vertex (more vertices: the cascades);
  - **result: Ace renders as a solid black silhouette** (`shot_300s.png`). The default bitcast
    reading is wrong for this title. Next: `ZPASS_ARGB_CMP=1`.
- Shadow run 2 (`ZPASS_ARGB_CMP=1`, `run\stderr_shadowcmp.txt`, `run\shots_shadowcmp\`): no more
  silhouettes, but the image looks the same as with no shadow passes (camo blotches on skin
  unchanged). The `[ZPASS]` log still prints "A8R8G8B8 bitcast" for the material units, so either
  the switch does not reach those units or only the log text is stale. Handed back to the shadow
  design work.
- Commit c3eaf8b: **shadow passes are opt-in** (`SHADOW_PASSES=1`), so the default image is not
  broken.
- `jstars-main-kick-wait.patch` applied to `jstars\port\main.cpp` (not a git repo; the previous file
  is `main.cpp.pre-kickwait`, and `jstars\port-backup-2026-10-20` has the morning copy). Timing
  runs `run\stderr_kick.txt` / `run\stderr_nokick.txt` (`GCM_FIFO_KICK=0`).
- **Kick-wait measured** (330 s runs, late match windows): with the patch **~10-11.3 batches/s**,
  with `GCM_FIFO_KICK=0` ~9.6-10. About 10% better. `[PERF2]` is unchanged in shape: drain 32%
  (vtx 0.38 s), present 0%, **rest 64%**, with only 20 of ~400 drain calls finding new work. The
  present loop is now mostly waiting for the **guest** to submit commands, so the next limit is on
  the PPU/SPU side. Next tool: `PS3_SAMPLE=2` (per-thread sampler, never run on J-Stars).

## Game-side speed: what samples/run1 shows (2026-10-20) — see `samples/ANALYSIS.md`

- run1 has no sampler output: `jstars\port\main.cpp` never calls `ps3_sampler_start()`. Draft fix:
  `samples/jstars-main-sampler.patch` (one call after `ppu_recomp_register()`, no-op unless
  `PS3_SAMPLE` is set).
- Match windows: 6.5-8.3 batches/s. `[PERF2]` rest 66-68% (present thread idle), and only 20 of
  ~520 drains find new commands, so the game threads are the limit.
- **Every perf run so far logged verbosely.** stderr is redirected and `PS3_VERBOSE` is unset, so
  `ps3_log_verbose()` is on. That gives ~4,900 lines/s, mostly two `[spurs-job]` lines with an
  `fflush` for each of ~2,200 SPURS jobs/s, written from guest threads through one FILE lock.
  `ps3_log.h` itself warns that this "changes what the title does". Next: rerun with
  `PS3_VERBOSE=0` (no code change), then a sampler run, also with `PS3_VERBOSE=0`.

## 30. Verbose logging halves the game speed; sampler now works in the port (2026-10-20)

- `jstars\port\main.cpp` now calls `ps3_sampler_start()` (it never did, so `PS3_SAMPLE` was
  ignored). Old file: `main.cpp.pre-sampler`.
- `run_test.ps1` redirects the log to a file and leaves `PS3_VERBOSE` unset, so **every timing run
  so far had verbose logging on** (~4,900 lines/s in the match, a forced write per SPU job).
- Same build, sampler on: verbose on ~8-8.8 batches/s; **`PS3_VERBOSE=0` ~18-20 batches/s**.
  `[PERF2]` with verbose off: drain 83% (vtx 0.46 s), present 1%, rest 5%. Details and logs:
  `samples\README.md`, `samples\run2-verbose\`, `samples\run3\`.
- Use `PS3_VERBOSE=0` for every timing run from now on.
- `jstars\run_test.ps1` now defaults to `PS3_VERBOSE=0` (an already-set `PS3_VERBOSE` is kept;
  `-VerboseLog` sets it to 1). Old file: `run_test.ps1.pre-verbose0`.

## Shadow passes round 2: comparison reading + caster depth state, patch not yet run (2026-10-20)

Patch + write-up: `patches/shadow-fix/` (`shadow-fix.patch`, `README.md`), on top of c3eaf8b.
- **The skin blotches are not shadows:** they are identical in `shots_nbatch\shot_300s.png` (no
  shadow passes) and `shots_shadowcmp\shot_300s.png`. PALE_TEXTURES.md cause 2 was wrong about the
  camo; its cause is still open.
- The character FP sums `.x` of its PCF taps (comparison results); the byte bitcast made a
  cleared map read as "all shadow" (black silhouettes). The patch makes A8R8G8B8 + zfunc a depth
  comparison by default (`ZPASS_ARGB_BITCAST=1` = old), stops the silent fallback to a plain
  depth read, and draws casters with the guest's depth test/func/write (`ZPASS_FIXED_DEPTH=1` =
  old). `ZPASS_DBG=1` now logs per batch whether casters land in each map.
- Open: whether the casters land at all. The compare run looked like "no shadows", which is also
  what an empty map gives. The next `ZPASS_DBG=1` run answers it.

## Green surface over the characters: depth clamped instead of clipped (2026-10-20) — STUDY, patch not yet run

- See `GREEN_SURFACE.md`. Pipelines never set `DepthClipEnable` (zeroed desc = FALSE), so geometry
  in front of the near plane is clamped to depth 0 and covers everything: the green wedge with the
  hard diagonal edge in the Goku intro. Patch `patches/depth-clip/` (`DEPTH_CLAMP=1` = old).
- **Update:** the user's RPCS3 shot of the same card (`reference\rpcs3\Screenshot 2026-10-20
  183009.png`) shows the green wedge, but has no RPCS3 overlay and matches our own 300 s shot, so it may be
  from our port. Source asked; until then the depth-clip patch stays a candidate.

## 31. Skin/hat blotches come from material texture unit 1 (2026-10-20) — CAUSE NARROWED

- Shadow fix (commit c1b6225, `patches\shadow-fix`), `SHADOW_PASSES=1 ZPASS_DBG=1`
  (`run\stderr_shadowfix.txt`): casters land (target 0: 102 draws / 170,908 vertices, target 1:
  142 / 200,643, per batch, cleared to 1.0), material units now "depth compare". **But the demo
  screen is fully black** (`run\shots_shadowfix\`) while frames present; the title still renders.
  Shadows stay opt-in.
- Test switches (commit 80eeea0, all off by default): `S0_LINEAR=1` (s0 linear+wrap),
  `TEX_NULL_UNITS=<hex>` (null SRV on those units for 3D offscreen draws), `S_CLAMP_UNITS=<hex>`.
- Results (Ace card, `run\shots_*`):
  - `S0_LINEAR=1`: blotches unchanged;
  - `TEX_NULL_UNITS=4` (t2, 320x180 screen RT) and `=18` (t3/t4 normal maps): unchanged;
  - `TEX_NULL_UNITS=1` (t0): models go black (t0 is the albedo);
  - **`TEX_NULL_UNITS=2` (t1): skin smooth peach, hat smooth orange, soft shading, close to
    RPCS3** (`run\shots_null2\shot_200s.png`).
  - `S_CLAMP_UNITS=2` (t1 clamp instead of wrap): unchanged, so not a wrap problem.
- t1 in the materials (older capture `rsxcap_flush1`): static DXT1 256x256/512x512 textures in local
  memory (`0x0B6A3600`, `0x0C00FD80`, `0x0B94E300`), remap 0xAAE4. Next: dump one decoded t1 and
  the HLSL of a skin material to see how t1 is used (a second colour layer mixed by a lighting or
  vertex-colour term?). Suspects: the mix weight (vertex colour UB4 a3 byte order, CMP normals),
  or the t1 image itself.
- Depth-clip patch applied (commit 7340a7b, `DEPTH_CLAMP=1` = old); see the follow-up below.

## Green surface = the water, drawn after the shared depth was cleared (2026-10-20) — CAUSE FOUND, patch not yet run

- RPCS3 at the same moment (`reference\rpcs3\Screenshot 2026-10-20 184154.png`) shows translucent
  blue water *behind* Goku; ours shows it green and in front (`183009.png` is our port).
- The water is one quad (capture `rsxcap_flush1` op 317, fp `0x0FC9F281`, reflection RT t0,
  refraction t1, scene zeta `0x0E960000` t15) drawn after the reflection/refraction target
  switches. The backend clears its shared depth on every switch, so the water covers everything.
- Patch `patches/zeta-targets/` (on 7340a7b): per-zeta depth targets for offscreen colour passes,
  kept until the game clears them; sampled zetas (t15) get the real depth through a copy.
  `ZETA_TARGETS=0` = old. Details: `GREEN_SURFACE.md`.
- **Depth clip** (7340a7b): the title is fine, but **the whole demo renders black**
  (`run\shots_clip\`, 4 identical black shots). Same build with `DEPTH_CLAMP=1` renders normally
  (`run\shots_clamp_check\`). Likely some passes emit z outside D3D's [0, w]. Commit 08acc3e makes it
  opt-in: **`DEPTH_CLIP=1`** (the `DEPTH_CLAMP` switch is gone). This probably also explains the black
  `SHADOW_PASSES=1` run only if that path clips; not checked.
- **Zeta targets** (10e44a0, `patches\zeta-targets`): Goku card (`run\shots_zeta\shot_230s.png`):
  **Goku is fully visible, the green band is gone**, and the ground shows blue water and foliage
  instead of neon green. The title is fine. But new problems: the DBZ and J-Stars logos sit on opaque
  boxes, a larger copy of the card text ghosts over the frame, blocky patches appear in the scene,
  and the 230 s and 240 s shots are almost identical (possible stall or very slow). A follow-up commit makes
  it opt-in: **`ZETA_TARGETS=1`**.

## Zeta targets v2: causes of the §31 side effects, patch not yet run (2026-10-20)

- The logo boxes, stale subtitle (`...of my little` from Ace's card), blocky patches and near-identical
  230/240 s shots are all **regions that stop updating** under kept depth (the large faded "Son Goku"
  is in RPCS3 too). Inferred causes: (1) the game's Z clears made with no colour target or with
  the display bound never reached the kept depth, so each frame tested against the last; (2)
  colour ops kept the forced LESS_EQUAL + write, so 2D quads wrote depth and the next frame's
  scene failed the test inside their rectangles.
- Patch `patches/zeta-targets-v2/` (on 2d03eee): every zeta clear goes to its DepthRT
  (`ZETA_CLEARS_V1=1` = old rule), and colour ops on kept depth use the game's depth
  test/func/write (`ZETA_FORCED_DEPTH=1` = old). Still opt-in via `ZETA_TARGETS=1`.

### §31 follow-up: skin shader, alpha test, zeta-targets v2 (2026-10-20)

- `RSX_CAP_PASSES=2` (commit with `TEX_A1_UNITS`) dumps every distinct FP of a captured frame
  (`run\rsxcap_skin\frame_0N\fp_op*.hlsl`). A material read there (frame_07 op3, fp 0x0FC018C1,
  possibly a stage material, not confirmed as Ace's skin) blends `lerp(t1(tc1 + k), t0, t0.a)`, then
  multiplies by lighting. It also samples t7 at `float4(0,0,0,0)/0` once (a decompiler oddity).
- `TEX_A1_UNITS=1` (force t0 alpha to 1): **skin blotches unchanged** (`run\shots_a1\`), so t0's
  alpha is not the cause; the HUD breaks as expected. Blotch cause still open: needs the FP of an
  actual skin draw (match a draw's t1 against the unit-1 textures from `shots_null2`).
- **Zeta targets v2** (commit 058bb68, opt-in `ZETA_TARGETS=1`; `run\shots_zeta2\`):
  - Goku card (230 s): Goku fully visible, **logos have no boxes, no ghost text**: v1's HUD
    problems are gone. But the water/ground area is **black** (only foliage patches drawn).
  - Ace card (200 s): **Ace is a solid black silhouette** (as with the shadow-pass default
    reading), so a sampled-depth hand-off probably reaches the shadow units.
  - 240 s: fully black frame (same as the depth-clip and shadow runs).
  - Not yet A/B'd with `ZETA_CLEARS_V1=1` / `ZETA_FORCED_DEPTH=1`.

## Zeta targets v3: shadow lookups had no texcoords; hand-off scope, clear rect, depth func (2026-10-20) — patch not yet run

- **Root cause behind every black silhouette:** materials build their t7/t8 cascade lookups from
  input TEX8/TEX9, which the FP decompiler mapped to `float4(0,0,0,0)` and the VP decompiler never
  output (`rsxcap_skin\frame_00\fp_op0001.hlsl`). All shadow taps sampled at 0/0; harmless with the
  old zero maps, garbage once real depth was handed off (shadow-pass runs, zeta v2).
- v2's Ace silhouette: clear-only cascade DepthRTs (no casters without `SHADOW_PASSES`) were handed to
  t7/t8. v2's black water: the water FP (`fp_op0379`) reads no depth, so it is its reflection or
  refraction source; v2's guest depth func or a whole-buffer zeta clear are the suspects.
- Patch `patches/zeta-targets-v3/` (on 058bb68): TEX8 = o[15] / TEX9 = o[6] plumbed through both
  decompilers (`FP_TC89=0` = old, active even with zeta off); hand-off only for DepthRTs with real
  depth (`ZETA_HANDOFF_ALL=1`); clears limited to the surface clip (`ZETA_FULL_CLEAR=1`); kept-depth
  colour ops use guest test/write but LESS_EQUAL (`ZETA_GUEST_FUNC=1`). Run plan in its README.
- **v2 switch A/B** (`run\shots_z2_*`): `ZETA_FORCED_DEPTH=1` brings back the logo boxes and ghost
  text (so v2's depth-state change is what fixed the HUD) and the water stays black;
  `ZETA_CLEARS_V1=1` gives a fully black frame. Neither restores the water.
- **Zeta targets v3** (commit c668f4c; it did not compile: `dt_handoff_ok` used before its
  declaration, fixed in f752afd. The first v3 run set used the old exe and is void):
  - defaults (zeta off, TEX8/9 pass-through on): Ace card unchanged (`run\shots_v3b_def\`); title
    fine (`run\shots_title_v3b\`);
  - `ZETA_TARGETS=1 ZPASS_DBG=1`: **both shots fully black** (`run\shots_v3b_zeta\`, log
    `run\stderr_v3b_zeta.txt`);
  - `ZETA_TARGETS=1 SHADOW_PASSES=1`: renders (`run\shots_v3b_shadow\shot_230s.png`, a Goku
    close-up): **blue water like RPCS3**, no black silhouette, but black triangle gaps in the water
    and the skin blotches are unchanged.

## Zeta targets v4: A8R8G8B8 views of a zeta are a bitcast; black triangles = hard cast shadows (2026-10-20) — patch not yet run

- `ZETA_TARGETS=1` alone went black because the scene zeta `0x0E960000` is read as A8R8G8B8
  (remap 0xAA93, **zfunc 4**) by a fog pass (fp `0x0FC9ED81`, `rsxcap_skin\frame_01\fp_op0305.hlsl`)
  that floors the bytes and rebuilds depth with `dot(rgb, (1/256, 1/65536, 1/2^24))`. v2/v3 turned
  every A8R8G8B8+zfunc view into a comparison, feeding it 0/1. So these views are a byte bitcast
  whatever the zfunc (RPCS3 does the same); v1's silhouettes were the missing TEX8/TEX9.
- Patch `patches/zeta-targets-v4/` (on f752afd): A8R8G8B8 zeta views are bitcast again
  (`ZPASS_ARGB_CMP=1` = v2/v3), and test switch `ZPASS_CMP_OFF=1` (no real depth for
  DEPTH24_D8/16 units) to judge the black triangles.
- The black triangles in v3's water (`shots_v3b_shadow`) repeat Goku's hair outline and lie on the
  ground: hard cast shadows from the ground shader's DEPTH24_D8 compare (zfunc GREATER), probably
  placed right but too dark/hard (inferred). `ZPASS_CMP_OFF=1` confirms or rules this out.

## Zeta targets v5: the black triangles are the blue foliage, not shadows (2026-10-20) — patch not yet run

- User's RPCS3 shot of the Goku card (posted in chat, not yet saved to `reference\rpcs3`): blue
  coral-like spiky plants and bushes over the ground/water near Goku. Our zeta v2 shot draws that
  foliage in colour; the v3+shadow shot draws it as black spiky shapes. So v4's "hard cast shadows"
  reading was wrong: the foliage goes black once the shadow maps hold real depth.
- Likely cause (inferred): self-shadowing acne; RSX casters use polygon offset, which the backend
  never decoded (0x0A68/0x0A78/0x0A7C). Second: A8R8G8B8+zfunc views must compare for TXP shadow taps
  but bitcast for TEX reads (fog pass), not one rule for all.
- Patch `patches/zeta-targets-v5/` (on f752afd, includes v4): polygon offset decoded and applied as
  D3D depth bias on depth-only/kept-depth PSOs (`ZPASS_NO_POLYOFFSET=1` = off); A8R8G8B8+zfunc compare
  only for TXP units (`ZPASS_ARGB_TXP=0` = v4, `ZPASS_ARGB_CMP=1` = v3). Test order in its README.
- **Zeta targets v5** (commit 713f72a, includes v4; opt-in). Runs `run\shots_v5_*`, logs
  `run\stderr_v5_*.txt`:
  - `ZETA_TARGETS=1 SHADOW_PASSES=1 ZPASS_DBG=1`: **works**. Goku card: Goku fully visible, water and
    foliage blue/teal (RPCS3-like), logos clean, no ghosting. Ace card: Ace normal, no silhouette.
    A few small black shards remain near the foliage. `[ZPASS]`: material fp 0xD64F4C1 t7/t8/t15 →
    "A8R8G8B8 bitcast" (not "depth compare" as the README expected), environment fp 0xFBFFFC1 →
    depth compare, fog fp 0xFCCF541 t0 → bitcast.
  - `ZPASS_NO_POLYOFFSET=1`, `ZPASS_ARGB_TXP=0`, `ZPASS_CMP_OFF=1`: each looks almost the same as
    all-on at the Goku card. None of these three is what fixed it.
  - `ZETA_TARGETS=1` alone: **renders now** (was black in v3), same look as with shadows. So the v4
    part (fog pass bitcast) is what made zeta usable. The remaining black shards are there with
    shadows off too, so they are not shadow acne.
  - Skin blotches: still present on Ace and Goku.

## Black foliage shards: the leaf shaders read FACING, which decompiles to zero (2026-10-20) — STUDY, opt-in patch drafted

- The maintainer matched the v5 Goku shot with the RPCS3 shot of the same spot: the black shards are the blue
  plants. They are correctly cut out (alpha works), black in colour, present with shadows off, and
  unchanged by every v5 switch (they were hidden under v2's black water).
- The leaf cards are 3-4 vertex draws (`rsxcap_skin\frame_06`, fp `0x0FC03741`, `0x0FC040C1`) whose
  code scales a vector by an input the decompiler maps to zero, `r[0] * (float4(0,0,0,0)).xxxx`:
  the two-sided "flip the normal by FACING" step. FACING (input 0xE) is "not plumbed", so the normal
  is zero and the leaves shade black (inferred, not run).
- Patch `patches/foliage-facing/` (decompiler only, opt-in `FP_FACING=1`): FACING = ±1 from
  `SV_IsFrontFace`, declared only where read. Fallback test: `FP_TC89=0`.
- **Zeta targets and shadow passes are now ON by default** (user approved). `ZETA_TARGETS=0` /
  `SHADOW_PASSES=0` turn them off. Checked before committing: title correct
  (`run\shots_def_title\`), demo cards correct at 180-245 s (`run\shots_def_demo\`,
  `run\shots_on_intro\`), ~16-17 batches/s late in the demo. Black shots at 120 s (opening movie,
  black with both off too) and 170 s (a load transition: 160 s shows "Now loading", 180 s the Ace
  card) are not regressions.

## 32. FACING input fixes the skin blotches and the black foliage (2026-10-20) — FIXED (opt-in)

- `foliage-facing.patch` applied as commit 3132243 (`rsx_fp_decompiler.c`): input 0xE (FACING) is
  `SV_IsFrontFace ? +1 : -1` instead of 0, opt-in **`FP_FACING=1`**.
- Run with the new defaults (zeta + shadows on) plus `FP_FACING=1` (`run\shots_facing\`):
  - **Ace's skin is smooth and evenly shaded, his hat smooth orange with a clean edge**: the
    blotches are gone (`shot_230s.png`). Goku's face and skin likewise (`shot_245s.png`).
  - The black foliage shards are now shaded dark-green/teal leaves (not pure black).
  - Title unchanged (`run\shots_facing_title\`).
- So the "camo" blotches were the character shaders' two-sided lighting: with FACING = 0 the
  normal was multiplied by zero on part of the mesh. Unit 1 (§31) is the lit/shaded ramp layer that
  lighting selects, which is why blanking it hid the symptom.
- Recommended: make `FP_FACING=1` the default.
- **`FP_FACING` is now ON by default** (user approved; commit after 3132243). `FP_FACING=0` = old.
  Checked with no switches set: title correct (`run\shots_facedef_title\`), Ace card clean
  (`run\shots_facedef_demo\`), ~17 batches/s late in the demo.

## Toward 30 fps: ranking + getenv cache + async strip fetch patches, not yet run (2026-10-20)

Write-up, patches and tests: `patches/speed30/`. Baseline (`perf/baseline-2026-10-20`) is 62 ms per
frame, and 30 fps needs 33 ms.
- Only the present thread is busy (run3 sampler). Its biggest costs are vertex fetch (~31 ms, 840k
  vertices at ~90 cycles) and the rest of the drain (~17-19 ms). On that thread, `ucrtbase` is
  ~25% of samples, very likely `getenv`: 1.1-1.3 us per call measured here, with ~7+ uncached calls
  per draw x ~600 draws.
- `env-cache.patch` (`RSX_ENV_CACHE=1`): name-keyed getenv cache in `rsx_env.h`. Same answers,
  1.07 -> 0.010 us per call. Expected ~5-12 ms per frame.
- `async-fetch.patch` (`VFETCH_ASYNC=1`): strip fetches are queued to 3 workers. Every queued fetch
  finishes before render_frame, before label/semaphore/reference/report methods
  (`g_rsx_sync_hook`), and before blits, user commands, flips and native joins. Standalone engine
  test: byte-identical over 30 frames x 900k vertices. Expected ~20-27 ms per frame.
- Estimate with both: ~23-37 ms per frame (27-43 fps), inferred. Next ideas: indexed native strips
  with GPU strip-cut (fewer, unique vertices), a /Zi build to name the drain's hot spots, compact
  vertex.

## Async fetch under-delivered: every native call synced; frames lock to vblanks (2026-10-20) — patch not yet run

Write-up, patch and test: `patches/strip-carry/`.
- With 3-index pieces + restart (4 entries) and 256-index FIFO batches, nearly every native-strip
  call starts with a join that copied the previous call's last vertex from the upload heap after
  the fetch. Under `VFETCH_ASYNC` that forced `vfa_sync()` and a synchronous fetch per call (test:
  1,171 of 1,172 calls). That is why the present thread still showed vtx ~23 ms/frame at
  62-67 cyc/vert.
- Fix: carry the guest index of the record's last output and list it instead. Byte-identical in
  the test. On with `VFETCH_ASYNC=1` or `STRIP_CARRY=1`.
- Frames: flip mode is VSYNC (`SetFlipMode(mode=2)`) and flips complete on the 60 Hz ticks, so fps
  is 60/n. That explains why env-cache + async did not add up: the present thread was busy ~38 ms,
  still 3 vblanks, and the rest showed as idle `rest`. 30 fps needs the frame's critical path to fit
  in ~33 ms (inferred from code). The new `[PERF4]` buckets batch intervals to confirm it.

## Frames split between 2 and 3 vblanks: the flip tick runs on a 15.6 ms clock (2026-10-20) — patch not yet run

Write-up and patch: `patches/vblank-qpc/`.
- After strip-carry the present thread is idle ~40% (`rest`), yet `[PERF4]` puts half the frames at
  37-54 ms (3 vblanks).
- `jstars\port\main.cpp` `frame_clock` fires the vblank/flip tick from `GetTickCount64()`
  (`next_tick += 16`). That clock only moves in 15-16 ms steps even with `timeBeginPeriod(1)`
  (measured: 15,16,15,16,...), so flip completions (flip mode VSYNC) land on an irregular
  15.6/31.2 ms grid. The 4 ms idle wait can delay a tick further.
- `jstars-main-vblank-qpc.patch` (`VBLANK_QPC=1`): tick from QPC at exactly 1000/60 ms, idle wait
  capped at the time to the next tick. Expected: frames move from 37-54 ms to 20-37 ms (inferred).
- Next: a sampler run with it to see what the guest threads wait on. Indexed strips matter for going
  above 30, not for reaching it, since the present thread is now ~40% idle.

## What the main thread waits on: sampler + thread dump read, chain sampler patch, not yet run (2026-10-20)

Write-up and patch: `patches/main-wait/`.
- vblank-qpc sampler run: the main guest thread (host tid 8508) is ~20-25% guest code and ~65-75%
  ntdll in every 10 s report from 189 s to 300 s. No thread is CPU-bound (guest code 1.7% of all
  samples).
- The thread dump caught it in `func_0057154C`'s CRI loader wait loop: `func_005713E4` (LOADING)
  plus `func_0057136C` (error), `func_00571528` yield, `sys_timer_usleep(100)` (exact; spins with
  SwitchToThread, which also counts as "ntdll"). That is one snapshot, near the end of the demo
  (cards), so it is unknown whether this is per frame.
- `sampler-chain.patch` (`PS3_SAMPLE_CHAIN=1`): main-thread samples keyed by their first 4 lifted
  functions on the stack, with a waiting flag. Next run: `VBLANK_QPC=1 PS3_SAMPLE=2
  PS3_SAMPLE_CHAIN=1 PS3_POLLTOP=5` to rank the main thread's waits (CRI load vs flip/label vs SPU
  jobs).

## The main thread waits on the previous flip, not on an SPU job (2026-10-20) — put-poll patch, not yet run

Write-up and patch: `patches/frame-pacing/`.
- The top chain's `func_004FCE3C` is a folded `return 0` stub, so its attribution means nothing.
  The real frame is `func_006507DC`, which also holds all three main-thread poll sites. It is the
  game's flip pacing loop: `while (cellGcmGetFlipStatus()) usleep(200)`, `ResetFlipStatus`,
  queue flip + flush, then `while (vblank_count < 1) usleep(200)`. That loop is ~37% of the main
  thread's time and ~60% of its samples. The guest is waiting for the renderer to drain and present
  the previous frame plus the next tick.
- tid 18's 52k sleeps per 5 s are the CRI file-server thread idling (`func_005383D4`); harmless.
  Queue 2/3 "FULL": the queues have size 1 (kick semantics); benign.
- `put-poll.patch` (`GCM_PUT_POLL=<us>`): during the present loop's idle wait, poll the guest's
  `put` and return as soon as it moves. That removes up to 4 ms of flush-detection latency from
  every frame's critical path. Suggested run: `VBLANK_QPC=1 GCM_PUT_POLL=200`. After that: indexed
  native strips (fewer vertices on the renderer path).

## 33. Main menu reached (2026-10-21)

Driven with `PAD_FILE` (no code change), method and screenshots in `menu\README.md`. Title →
CROSS → save prompt → Yes → Main Menu → Free Battle → 1 Player → character select, all at a
steady 30 fps; input works. Glitches: scrambled red logo top-right on menu screens; empty title
bar and black speckles by the CPU stars on character select; black-rimmed foliage on the Naruto
demo stage. Save data `NPUB31647-B0` now exists under `jstars\run\gamedata\`.

## Menu glitches: causes and opt-in fixes (2026-10-21) — STUDY, patch not yet run

See `patches/menu-fixes/README.md` (one patch, three switches, on 283ad47). Inferred from
`menu\*.png` and the code; no capture of these screens yet.
- Naruto forest leaves with black rims: cards drawn whole, DXT1 transparent texels showing black.
  Alpha-to-coverage (SET_ANTI_ALIASING_CONTROL 0x1D7C bit 4) is never decoded and we render one
  sample. `MENU_A2C=1` turns it into an `alpha >= 0.5` test. May also cover the Goku card's shards.
- Scrambled red-block menu logo: linear texture row pitch (CONTROL3) ignored by the D3D12 path, and
  by the shared layout for compressed images (NPOT DXT UI art). `MENU_TEX_PITCH=1`.
- Character select empty title bar / star noise: packed 16-bit and D8R8G8B8 textures ignored at
  bind (stale texture) and given a null view at upload (`MENU_FMT16=1`), or the same pitch issue.
  `TEXFMT_SKIP=1` logs ignored formats.
- **Free Battle match reached and playable** (`menu\README.md`): stage select clean; white loading
  screen; intro cards with cracked/shattered character models; fight runs at **~13-27 fps (mostly
  20-27)**, HUD complete, a big black floor slab; button input controls Goku (jump, attack,
  lock-on mode). Walking needs live analog-stick input (`PAD_FILE` has buttons only).
- **Free Battle match (2026-10-21):** floor black slab + Goku hidden behind the floor = the
  shared-depth reset at colour-target switches (reflective floor drawn after its reflection pass),
  like the Goku-card water: test `ZETA_TARGETS=1 SHADOW_PASSES=1`. Shards/slivers/thin lines on
  characters = vertex data (skinning race §26 or a match-only path), separate fix. Luffy blotches =
  §31. 20-27 fps = performance. White loading screen: maybe `MENU_FMT16`/`MENU_TEX_PITCH`. Details in
  `patches/menu-fixes/README.md`.
- **Correction (zeta/shadows were already on in the match):** the floor slab is most likely 3
  fragment programs that fail to compile (`stdout.txt`: X3504 "array index out of bounds" for fp
  `0x0FC27A81`, `0x0FC268C1`, `0x0FC10641`). FP register indices go to 63 but the HLSL declares
  `r[48]`/`h[48]` (reproduced offline); the draws fall back to the dark debug-font pipeline.
  Patch `patches/fp-regs64/` (`FP_REGS64=1`). Goku behind the floor: test after that.

## Stages first: problem ranking and survey checklist (2026-10-21) — PLAN

See `STAGE_PLAN.md`. Ranked by stage reach: shaders failing to build (fp-regs64), no mipmaps on
any stage (gap, no patch yet), ignored texture formats / pitch, alpha-to-coverage foliage, FACING,
water/reflections (zeta v5), shadow quality, fixed samplers (gap), depth clip. Survey: every stage
with the first-fix switches on, three screenshots + log counts + RPCS3 comparison per stage, results
in `STAGES.md`.
- The maintainer: building-heavy stages need far more work than open ones. `STAGE_PLAN.md` now lists what
  buildings have more of (more materials, so more failing shaders; unit-1 baked lighting, which may
  share a root with the skin blotches; tiled textures, so samplers and mipmaps; cut-out windows;
  reflective floors; edge lines) and asks for building stages in detail: an `RSX_CAP_PASSES=2`
  capture plus A/B shots with `TEX_NULL_UNITS=2`, `TEX_GAMMA_OFF=1`, `S0_LINEAR=1`, `CULL_FRONT_SKIP=1`.

## 34. Menu fixes and FP_REGS64 tested (2026-10-21)

Closed-loop tours (`tools\menu_tour.sh`, `menu\tours\t0..t5`), random characters, Yoshiwara:
- `MENU_FMT16=1`: **the "scrambled red logo" is the main menu's JP/level panel and now renders**
  (`JP 010000`, Lv.1 icons); character select gets its title, clean CPU stars and portrait frames.
  Made **default-on** (`MENU_FMT16=0` = off).
- `FP_REGS64=1`: no visible change; no `PS compile FAIL` in any tour (the three failing programs
  belong to characters not picked this time). Made **default-on** (`FP_REGS64=0` = old 48).
- `MENU_A2C=1`, `MENU_TEX_PITCH=1`: no visible change on Yoshiwara (no foliage, no linear NPOT UI
  seen). Still opt-in; check in the stage survey.
- `TEXFMT_SKIP=1`: no skipped formats logged on menus or Yoshiwara.
- Verified defaults (`t5_newdefault`): menus and fight correct, fight ~23-26 fps.

## Stage survey round 1: mipmaps patch, blocky edges and sky streaks (2026-10-21) — patch not yet run

See `patches/tex-mips/README.md`. Inferred from `stages\sNN` shots and the code:
- Namek grainy ground/water (s06) and the night-ruins sky stripes (s03): no mipmaps, so level 0
  is minified (grain, moiré). Patch `patches/tex-mips/` (`TEX_MIPS=1`, opt-in): full mip chain for
  2D textures (RSX layout via `rsx_texture_mip_chain`, D3D placement via `GetCopyableFootprints`).
- Kameari clouds/tower (s01) and Penguin Village palms/sky edges (s05) blocky and stair-stepped: unit 0
  uses the static POINT+CLAMP sampler (kept for the debug font), so magnified clouds and cut-out
  leaves show texel squares. Test `S0_LINEAR=1`; the real fix is per-draw samplers (STAGE_PLAN #9).

## Fight speed: where the time goes + indexed strips patch, not yet run (2026-10-21)

Write-up, patch and test: `patches/strip-indexed/`. Data: stage survey `stages\s0N\stderr.txt`.
- Steady fight frames: the present thread is busy ~32 ms (vtx ~12.5 ms for ~870k strip output
  vertices, other drain ~12, render_frame ~8), against 22-26 ms in intro cards. That is at the
  33 ms line, so frames split between 2 and 3 vblanks (19-27 fps).
- The dips to 13-17 fps are hitches: shader compiles (`pso` 0.3-0.6 s per window, 3-12 MISS, s06)
  and new texture creation (`tex` 0.25-0.39 s, `[PERF3]` new 270 / created 202, s01/s06).
- `strip-indexed.patch` (`STRIP_INDEXED=1`): native strips drawn indexed with D3D strip cuts.
  Distinct guest vertices are fetched once per record (dedup map across its FIFO batches), with an
  index buffer (0xFFFFFFFF at restarts), `DrawIndexedInstanced`, and `IBStripCutValue` on the VP and
  guest-FP PSOs. Offline: identical non-degenerate triangles (order + winding) in 50/50 random
  streams. At least ~45% fewer vertices with no reuse, far fewer with real mesh reuse. Expected
  3-8 ms per fight frame (inferred).
- Next for the dips: compile PSOs off the present thread or cache them on disk; pool or
  pre-upload textures.


## 35. Neon grass / saturated sky: auto-exposure stuck, luminance target never read back (2026-10-21) — STUDY, patches not yet run

See `COLOR_EXPOSURE.md` and `patches\color-exposure\` (four opt-in patches + README).
- The tone-map (fp `0x0FC9FC01`, `rsxcap_flush1` op325) takes its exposure from constant `c1.w`,
  which the PPU computes from the 20x11 average-luminance target `0x039FFA40` (ops 321-324). The
  backend never writes any render target back to guest memory and the game does not copy it with
  NV0039 (no `[GCM2D-UNH]` in any log), so the PPU reads black: `c1.w = 0.72 / 1e-4 = 7200` in all
  six captured tone-map frames across two scenes. With `Lm = L·E/(L·E+1)` and `out = 1-exp(-4·c·Lm)`,
  E = 7200 saturates `Lm` for every pixel: grass (0.1,0.5,0.05) comes out (0.33,0.86,0.18) instead of
  (0.12,0.48,0.06), the sky clips. Fix: `lum-readback.patch` (`LUM_READBACK=1`); A/B:
  `tonemap-exposure.patch` (`TONEMAP_EXPOSURE=1`, logs `[TONEMAP]`).
- `_bx2` / biased-remap fetches (upstream 122690f) ported as `tex-expand.patch` (`TEX_EXPAND=1`),
  with a `[FPEXPAND]` log that reports use even when off. Use by J-Stars is unknown: the shaders
  are not stored raw on disk (no Cg headers; `fp_fail.bin`'s bytes absent from ELF and CPKs), so
  only a run can tell. Offline: decompiler compiles, 21/21 tests.
- SET_SHADER_PACKER (sRGB writes) is never applied by the D3D12 backend; `color-diag.patch` logs
  it (`[RSXPK]`) and adds `addr=` to `[TEXFMT]`.
- Ruled out: scene RT precision (it is 8-bit A8R8G8B8, HDR packed in alpha), blend/clear decode,
  Namek's teal/green palette (canon).

## 36. Mip chains default, S0_LINEAR rejected (2026-10-21)
- TEX_MIPS=1 (commit 2df51e8) uploads full mip chains. Namek ground grain gone, Yoshiwara unchanged,
  fps unchanged (Namek ~20, Athena 27-30). Default on since 678d973 (TEX_MIPS=0 off). Check tour
  menu/tours/t6_texmips_default: title, menus, character select and fight all look as before.
- Athena intro still has thin lines across the night sky: not a mip problem.
- S0_LINEAR=1 smooths Kameari clouds/tower but draws black boxes around alpha-cut plants on
  Penguin Village (stages/s0lin/s05/14_intro_b.png). Stays opt-in.

## 37. Fight hitches: shader builds and new textures, measured; three opt-in patches not yet run (2026-10-21, AI assistant thread)
- Over the fight part of every stage-survey run (`stages\sNN\stderr.txt`, ~320-390 s each):
  pipeline builds (`pso`) cost **11-39 s per fight**, 200-760 misses at **43-78 ms each**, with
  120-450 of them at every match start (one window at 1-2 fps for 8-16 s, e.g. s01 line 5935:
  214 misses, 13.95 s) and mid-fight windows of 5-15 misses (0.3-1.3 s) = the 10-17 fps dips.
  New texture uploads cost **7-11 s per fight**: 4,000-7,600 at 1.4-2.1 ms each, 80% of them
  also recreating the two D3D resources, in bursts of 300-800 per window (s07 line 9403: 802
  new in 0.86 s at 8.5 fps).
- Patch `patches\shader-hitches\shader-hitches.patch` (on d4b3d58, `rsx_d3d12_backend.c` only,
  `git apply --check` ok; README there): `SHADER_CACHE=1` (DXBC on disk, keyed by the final
  HLSL text, so every decompiler/backend patch re-keys itself), `PSO_ASYNC=1|2` (pipelines built
  on 2 worker threads; pending draws skipped or drawn with a same-shader sibling;
  `PSO_ASYNC_WAIT=<ms>`), `TEX_LRU=1` / `TEX_POOL=1` (least-recently-bound eviction, reuse of
  same-size resources). Plus `[PERF5]` (fxc vs CreateGraphicsPipelineState time, async counters)
  and `[PERF3] created (t) | decode t | again N`.
- Ranking: SHADER_CACHE first (largest from the second run of a stage, low risk), PSO_ASYNC
  second (removes the hitch on first runs too, objects can pop in late; medium risk), TEX_POOL
  third (removes resource creation from most uploads; low risk). Measure order in the README.
- Offline test `patches\shader-hitches\test`: key/header checks pass; synthetic fight: first-free
  48,799 uploads / 40,845 creates, TEX_LRU 38,672 / 32,306, TEX_POOL 38,681 / 1,862.
- Note: the checked-out `rsx_d3d12_backend.c` is CRLF while HEAD is LF, so `git diff` shows the
  whole file changed and LF patches need `git apply --ignore-whitespace`.

## 37. Color-exposure patches: first results (2026-10-21)
Commits 2a73fc2 tex-expand, 11f79d5 color-diag, 594aa17 tonemap-exposure, d4b3d58 lum-readback
(all opt-in, built with no fix-up). Demo runs with screenshots at 150-250 s in color\<run>\t*.png,
Ace card at t200 (tools\demo_shots.sh).
- Base: `[TONEMAP] exposure c1.w=7200` on every tone-map program (one at 1800), as predicted.
  The base Ace card is already close to RPCS3 (reference\rpcs3\Screenshot 2026-10-20 174958.png);
  ours is slightly more cyan and a little less bright in the sky.
- TONEMAP_EXPOSURE=1 / 0.5 / 2: all clearly DARKER than base and further from RPCS3 (hat, hair,
  skin go brown). So a lower exposure is not the fix; 7200 is not obviously wrong for this card.
- LUM_READBACK=1: reads back an 80x45 target at off=0x0D6A0000 (not the 20x11 at 0x039FFA40 the
  study expected), mostly px0=0,0,0; c1.w stays 7200; picture identical to base. Costs ~2-4 fps
  early on (prepass/pso up while it settles). Per COLOR_EXPOSURE.md step 5 the game reads the
  luminance another way; next is a GCM2D_DBG=64 / NV3089_GATE log run.
- No `[FPEXPAND]` line in any run, so TEX_EXPAND has nothing to change here.
- Log run (LUM_READBACK=1 GCM2D_DBG=64 NV3089_GATE on, color\lumrb_xfer): no NV3089 blit lines at
  all; only six GCM2D inline writes at boot. 0x039FFA40 shows up as `offscreen RT 5 ... 1280x720`,
  not a 20x11 target. The only small target read back is 80x45 at 0x0D6A0000. So the luminance
  path the study assumed is not what this build sees; needs another look before more runs.

## 38. Colour round 2: readback never hit the 20x11 target; glare layer never drawn (2026-10-21, AI assistant thread)

- The `TONEMAP_EXPOSURE` result (lower = darker/browner) is the expected direction: with c1.w =
  7200 the tone-map `1-exp(-4*rgb*Lm)` is at its brightest, so exposure cannot explain our picture
  being darker than RPCS3. The gap is in the tone-map's input.
- `LUM_READBACK=1` never read 0x039FFA40: `rsx_color_target_dims` turns clip dims < 16 into window
  size, so the 20x11 target is a 1280x720 resource (`offrt[5]` in all 55 captured frames) and was
  over MAXPX. `patches\color-exposure\lum-readback-2.patch` copies the guest rect instead
  (applies to d8de15f and to the CRLF tree with `--ignore-whitespace`). Not built yet.
- NV3089/NV0039 ruled out by the AI assistant's gate run (no `[NV3089G]`, no `[GCM2D-UNH]`).
- Glare layer 0x0E590000 (added x2 by the HDR encode fp 0x0FC9F901) and 0x0DC20000 are cleared
  every frame and drawn in none of the 55 captured frames; occlusion-query gating ruled out
  (GET_REPORT only ZCULL stats). The game skips those passes; the luminance readback is the one
  known CPU-visible input. Details, ranking and the test steps: `COLOR_EXPOSURE.md` "Round 2".

## RPCS3 gameplay video: Yoshiwara Paradise, look comparison (2026-10-21)

The user's 5:52 RPCS3 recording is a Free Battle on **Yoshiwara Paradise** (Stage Select at 1:00).
Twelve frames are in `reference\rpcs3\frames\` (extracted with Windows' built-in media stack; no
ffmpeg installed). Comparison: `reference\rpcs3\COMPARISON_yoshiwara.md`. The biggest differences are
the overall tone (ours harsher, clipped highlights, flat dark areas vs RPCS3's warm, soft look) and
missing bloom glow, then floor sheen, character shading, intro-card background softness, and
effects. RPCS3 also draws large dark stone chunks when the floor breaks, so not every dark block in
our shots is a bug.

## 39. Shader disk cache: the key is stable; what still compiles is first-time content (2026-10-21, AI assistant thread)

The AI assistant's reading of the Namek runs in `hitches\` was that `SHADER_CACHE=1` hits only about
half the time and that the match start still stalls (306 misses, 9.8 s), suspecting that the
cached HLSL text varies between runs (inlined animating constants). The logs say otherwise: the
two windows are different events, and the text does not vary.

`[PERF5]` per run, whole run and the match-start window (the `[PERF]` window with ~200 misses at
`[FRAMELOG] batch` ~6900; `tools\hitchsum.py` anchors on it too):

| run (`hitches\`) | switches | whole run: fxc compiles / disk hits | match-start window | later bursts (batch: misses, fxc, disk hits) | new files in `shadercache\` |
|---|---|---|---|---|---|
| base | none | 415 (18.3 s) / 0 | 1.57 fps, pso 10.40 s, 188 misses, 215 fxc | 9360: 125, 93, 0 | cache off |
| sc1_run1 | `SHADER_CACHE=1` (fill) | 312 (15.8 s) / 0 | 1.42 fps, pso 11.46 s, 206 misses, 230 fxc | none over 11 | 312 (241 ps, 71 vs) |
| sc1_run2 | `SHADER_CACHE=1` | 259 (8.6 s) / 293 | **16.75 fps, pso 0.18 s, 206 misses, 0 fxc, 230 disk hits** | 7860: 306, 255, 4 (1.92 fps, pso 9.79 s) | 259 (all ps) |
| sc_async1 | + `PSO_ASYNC=1` | 22 (1.7 s) / 331 | 17.55 fps, pso 0.04 s, 203 misses, 0 fxc, 251 hits | 10260: 22, 15, 0 | 21 |
| sc_async2 | + `PSO_ASYNC=2` | 261 (10.9 s) / 417 | 19.28 fps, pso 0.04 s, 205 misses, 0 fxc, 252 hits | 7740: 129, 87, 40 (3 windows); 10080: 344, 174, 49 (5 windows, 18.9 fps) | 286 |
| sc_async1_wait8b | `PSO_ASYNC=1 PSO_ASYNC_WAIT=8` | 28 (1.6 s) / 345 | 17.53 fps, pso 0.04 s, 201 misses, 249 hits | 9240: 42, 11, 7 | 28 |

(`shadercache\` file counts from `tools\shcache_stat.py`, which groups the files by write time.)

1. **The match start is a full hit from the second run on.** 0 compiles, the pso share of the
   window 10.4 s -> 0.18 s, 1.5 fps -> 17 fps, in every cached run. So the key (the final HLSL
   text) is the same from run to run; the 8-16 s stall at every match start is gone with a warm
   cache, which was the point of the patch.
2. **What still compiles are bursts later in the fight, at a different point in each run.** The
   306-miss window the AI assistant took for the match start is batch ~7860 of run 2, 45 windows
   (~50 s) after it; run 3 had none, run 4 two (batch 7740 and 10080). Each is a set of fragment
   programs never compiled before in any run: the number of new `ps_` files a run writes equals
   its fxc count (259 = 259, 21 vs 22, 286 vs 261 + the cut-off last window), so no compile was
   ever repeated and no file re-written, which a drifting key would show. No vertex shader was
   compiled after run 1 (71 `vs_` files, all from run 1).
3. **They are distinct programs, not variants of a few.** Run 2's 259 files have HLSL lengths
   from 787 to 22,387 bytes (median 6,341), at most 5 within any 64-byte span and 14 exact
   repeats; variants of one program under different gamma/expand/shadow masks would cluster
   within a few dozen bytes. They are small (median blob 2.5 KB vs 5.3 KB at match start,
   33 ms per compile vs 54 ms).
4. **What they are.** Same teams in every run (Naruto/Goku/Toriko vs Luffy/Ichigo/..., the
   `15_fight_start.png` HUD); at 35 s run 4 has Goku active where run 2 still has Naruto. The
   bursts arrive with character switches and specials, which the AI and the auto input do
   differently per run; a character's materials bring ~250 programs of their own. They cache
   like the rest: run 4 already hit 40 of the 129 at its first switch.
5. **Code audit of what goes into the text.** Fragment side: inline constants are read from
   `fp_constants[]` (`FP_CONSTBUF` on), the alpha test reads `fp_alpha` from b1, nothing prints
   an address or a counter; the parts that do vary the text (sRGB lane masks, expand masks,
   shadow descriptors, cube mask, depth bitcast, `FP_TEX`/`FP_FORCE`/`FP_SHOW` debug rewrites)
   derive from the bound textures' formats and env switches, fixed for a given draw. Vertex
   side: branch targets are made relative to the program start (`vp_branch_target`) and the
   `/* FLOW */` comment prints relative indices, so a program loaded at another slot gives the
   same text (the in-memory VS key folds the slot, the disk key does not need to). There is
   nothing to normalise; a key on microcode + state would miss on exactly the same draws.

**Consequences.** Keep the key. `SHADER_CACHE=1` should become default-on (`SHADER_CACHE=0`
off). Keep `shadercache\` across builds: a backend change that leaves the generated text alone
keeps its files valid; bump `SHCACHE_VERSION` only when the `D3DCompile` flags or profile
change. It is content-addressed, so a cache filled by a one-time tour of every character and
support on one stage warms every later fight; after 6 Namek runs it holds 931 files, 4.4 MB
(the whole game is probably a few thousand files, tens of MB). For first-time content
`PSO_ASYNC` is the answer and it works: run 4's 344-miss burst ran at 18.9 fps instead of 1.9,
at the cost of skipped draws over 5 windows (in flight 299 -> 238 -> 200 -> 153 -> 101; skipped
397, 13, 121, 114, 2), because 2 workers finish 30-45 compiles per window.
`PSO_ASYNC_THREADS=4` should halve that tail on a 6+ core CPU (untested). Not worth doing:
de-duplicating identical HLSL in flight (no duplicate compile was observed: files == compiles)
and compiler-flag tricks for faster first compiles.

## Shadows: on, but invisible (2026-10-21) — STUDY, diagnostic patch drafted

See `patches/shadow-show/README.md`. Shadow passes are on by default, casters land in the
per-zeta maps, and materials read them (environment programs compare DEPTH24_D8 with zfunc GREATER;
the character material reads bytes like RPCS3). But `ZPASS_CMP_OFF=1` changed almost nothing in v5,
and our Yoshiwara fight shots show no floor shadows, while the RPCS3 video does (frames 210 s,
240 s). Likely: the reference and stored depths are on different scales, or TEX9 is wired to the
wrong vertex output. Diagnostic `ZPASS_SHOW=1..4` makes the comparison helpers return
shadow / stored depth / reference depth / lit, to tell which.
- Round 3 (~16:30): v2's "20x11 always 0" was a log-cadence artefact (only boot frames were
  printed) and the copy went to the local alias 0xC39FFA40. The target is at IO offset 0x039FFA40
  = the last 1.4 KB of the 0x3A00000 IO window, i.e. main memory, where the PPU reads it. The
  GPU draw itself is correct (capture dump of the 20x11 rect has data). `lum-readback-3.patch`
  records SET_CONTEXT_DMA_COLOR and resolves through it; applies to cc50482. `COLOR_EXPOSURE.md`
  "Round 3".

## 38. Hitch patches tested; shader cache + texture pool default (2026-10-21)
Commit d8de15f (patch), defaults eea13f6 (SHADER_CACHE=0 / TEX_POOL=0 turn them off). Namek tours
in hitches\<run>, summary `python3 tools/hitchsum.py hitches/<run>` (fight = after the last 100+ fps
loading window).
- base: fxc 415 compiles 18.3 s vs pso create 0.7 s; tex new 5658 (5.7 s), created 2.9 s, again 4570.
- SHADER_CACHE warm: match start full disk hit (see §39); defaults_check: <20 fps windows 22 -> 4,
  fxc 4.2 s (new characters only). Cache in jstars\run\shadercache (keep it across builds).
- TEX_POOL=1: tex new 5658 -> ~1400, tex time 5.7 -> ~1.1 s, policy 2. Demo pictures match base
  (only timing shifts, demo runs ahead).
- PSO_ASYNC=1 (2 threads, with cache): match start 17 fps, 176 skipped draws; =2: 948 skipped /
  136 sibling; WAIT=8: 65 skipped; THREADS=4: 515 skipped (other characters). Nothing visibly missing
  in tour or demo shots. Stays opt-in (objects can appear a frame late).

## 40. Color round 2/3 runs (2026-10-21)
- lum-readback-2 (92a51dd): `[LUMRB] off=0x039FFA40 20x11 (res 1280x720) ... meanRGB=0`; the AI assistant:
  wrote to local 0xC39FFA40 instead of main memory -> lum-readback-3 (1ef9684), test queued.
- Yoshiwara TONEMAP_EXPOSURE=5/10/20/50 (color\yoshi_tm*): 10, 20, 50 look the same as the default
  7200 (yoshi_lumrb2): the floor tiles and shoji panels stay burned out at every value; 5 is only a
  little darker. So the exposure constant does not bring the panels back to RPCS3's levels in this
  range; the tone map seems saturated above ~10.
- RSX_CAP of the Yoshiwara fight (color\yoshi_lumrb2_cap, frames 10000-10900): 0x0E590000 is only
  CLEARed to (0,0,0,1) each frame and then sampled as t0 by the composite fp 0x0FD00281 (DRAW
  rt=0x0DE10000); no draw ever renders into it. The glare layer is black every frame.
- Round 4 (~17:30): the exposure sweep is explained by the tone-map (`Lm` saturates above E~10);
  the burned panels need scene.rgb/scene.a >= 3 before the tone-map, i.e. scene alpha <= 0.33 on
  them (the HDR encode divides by the scene alpha). `cap-alpha.patch` (RSX_CAP_ALPHA=1 dumps
  alpha as `<name>_a.bmp`) shows it. For the glare layer, `rt-drawlog.patch` (RT_DRAWLOG=...)
  logs FIFO-level binds/clears/begins/draws for given surfaces, which tells "never issued" from
  "dropped by the backend" (two silent drop paths exist). `COLOR_EXPOSURE.md` "Round 4".

## 41. ZPASS_SHOW shadow diagnostic on Yoshiwara (2026-10-21)
Commit cc50482. Tours shadows\show1..4 (16_fight_20s.png, same hall view in all four; characters
differ). `[ZPASS] show mode N` logged on ~10+ ground programs (compare units 0x8180). Mean brightness
of the left floor (x 20-520, y 420-500), the dark half that ends in a sharp diagonal edge:
| run | left floor | right floor |
|---|---|---|
| normal (yoshi_lumrb2 / yoshi_tm50) | 48 | ~100 |
| mode 4 always lit | 48 | 93 |
| mode 1 always in shadow | **76** | 120 |
| mode 2 stored map depth | 47 | (character in the way) |
| mode 3 reference depth | **76** | 107 |
- Normal == mode 4: today the comparison always says "lit".
- Mode 1 ("in shadow") makes the dark left floor BRIGHTER, to the level of the right side: the dark
  half with the diagonal edge behaves like the inverse of a shadow term (sense inverted, or the
  helper's 0/1 convention is the opposite of what the material expects). Inferred, not proven.
- Mode 2 (stored depth) gives the same dark level as lit (map ~1.0 where sampled); mode 3 (reference)
  the same bright level as mode 1 (reference small). So reference < stored nearly everywhere, which
  fits cause 1 (different depth scales) as well.
- lum-readback-3 demo (color\lumrb3): every line prints `dma=0x00000000` (neither 0xFEED0000 nor
  0xFEED0001), so the colour DMA context was never recorded and the copy still goes to local
  0xC39FFA40. The 20x11 rect now has non-zero means after boot (78-124, changing between scenes),
  so the draw does land there. c1.w stays 7200; picture unchanged.
- Round 5 (~18:00): lum-readback-3 logged dma=0 because `rsx_process_method` only routes methods
  0x200-0x23C / 0x280-0x28C to `process_surface_method`; the DMA handles (0x18C/0x194/0x1B4/
  0x1B8) never reached v3's cases. `lum-readback-4.patch` adds them to the range gate (7 lines,
  applies to de12d74, syntax-checked). `COLOR_EXPOSURE.md` "Round 5".
- Round 6 (~19:50): the PPU side is decoded (COLOR_EXPOSURE.md Round 6). func_0055C5A4 reads the 20x11
  as A8R8G8B8, decodes RGBM (rgb*0.25/A), takes the log-average luminance Lgeo, adapts La towards it by
  1-0.98^(15*dt) per frame; func_0055D1F8 builds c1 = (63.75, max(La,1e-4), clamp(La+0.5,0,63.75),
  0.72/max(La,1e-4)). So E = 0.72/La (~7-25 at the Ace card), darker than the 7200 ceiling by design, and
  RPCS3's extra brightness is not exposure (same code, same E) but the glare layer / scene term. `[TONEMAP]`
  went quiet because its match required c1.y < 0.001; `tonemap-log.patch` fixes the match, logs c1 periodically
  and prints the PPU's Lgeo / predicted E in `[LUMRB]`.
- Round 6 follow-up: the AI assistant's Yoshiwara tonemap-log numbers are the stage's HDR parameters, not a bug:
  key = 0.36 (E x c1.y = 0.36) and a luminance offset of 0.06 (thr - c1.y = 0.44 = 0.5 - 0.06), so the logged
  La is La_real + 0.06 and E = 0.36/(Lgeo + 0.06) = 2.3-3.9, which the log shows; La_real tracks Lgeo. The
  demo/menus use key 0.72, offset 0. Brightness slider = the last pass (option_brightness, out = in^gamma),
  gamma 1.0 in all our captures; see COLOR_EXPOSURE.md 'Brightness slider' for the cheap tests.
- Round 7/8 (~21:50): the glare layer 0x0E590000 is the particle server's RenderLuminance target
  (`func_003A45E0`, ppu_recomp_019.cpp). Its gate passes (the AI assistant: pass_flags=0x3F, aux=0; aux=0 is a normal
  pre-pass that does the bind+clear). After it, the pass walks the server's active-emitter list
  (`[server+0x1880C]`) and calls each emitter's virtual slot 9 `render(node, arg2, pass, layer, params)` for
  (0,0),(2,0),(0,1),(2,1),(3,1); the sibling passes that do draw (distortion 24 draws, RenderOut) use the same
  list, virtual and params with other (pass, layer) and another arg2. RenderLuminance issues no draw itself, so
  either the emitter virtual draws nothing for its arguments or the draw is dropped later; `lumflag-trace-2.patch`
  logs the virtual's target and arg2 at both call sites to decide. Burned Yoshiwara panels confirmed from
  `color\yoshi_passes_cap` (encode = op678, scene = `pass_op0683_pre_rt_0ED10000`): scene alpha 43/70/255
  (5/50/95 pct) behind burned pixels, decoded value 0.91; written by the opaque stage geometry pass (ops 221-498),
  so it is the stage material's own alpha, not particles or post. Details: COLOR_EXPOSURE.md Round 8.
- Round 9 (~22:40): lumflag-trace-2 showed both particle passes call the same emitter render `func_00626790`
  (vtable 0x884488), differing only in the render-pass descriptor (lum 0x48433A20, out 0x4843A6E0, obtained from
  `func_0052647C`). Decoded: `func_00626790` walks the node's elements through the server filter (mode 0 = all)
  and calls `func_0063A1AC(elem, rctx, .., pass, layer)`; there `layer` picks the element type (1 = type != 2,
  else type 2) and the pass is a bit in the element's mask `[desc+4]`; no luminance-specific flag or texture check.
  So the difference must be in the submit functions that take the descriptor; `lumflag-trace-3.patch` logs the
  element draws per ctx and the five submit entries. FP_GAIN_KEYS: the hashes cannot be mapped from the existing
  files (per-draw fp addresses are not stable, [FPGAIN] has no draw context); `cap-code.patch` adds `code=` to the
  capture manifest. Characters in yoshi_passes_cap = the 71 opaque draws with textures below 0x02000000 (67 of them
  shadow casters), four groups of ops (171-190, 200-216, 332-350, 356-370 in frame_00). COLOR_EXPOSURE.md Round 9.
- FP_GAIN_KEYS picked (~23:10) from the AI assistant's cap-code captures (color\capcode): character programs = codes of
  opaque scene draws with textures below 0x0A000000 (96% shadow casters, zero overlap with stage codes over 11
  frames). Yoshiwara 47 programs, demo 38, union 73 (lines in color\capcode\fp_gain_keys.txt and COLOR_EXPOSURE.md);
  fp-gain-keys.patch raises the key cap to 128. Goku = the 8 programs shared by both scenes.
- Round 10 (~23:45) from lumflag-trace-3 (color\yoshi_lumflag3): the luminance pass does not stop. The particle
  render state G=[[0x96CF90]] has flag bit 0x2 = luminance mode (set by RenderLuminance at loc_003A4CC4); in that
  mode func_0063A1AC draws only "glow" children (header flag 0x1000, intensity > 0) through func_00634968 (VP/FP
  from G+0x2D0/G+0x2F8, intensity constant) - the elem-draw lines without a submit are normal children being
  skipped, as designed. func_00634968 fired 40+ times in the fights, so glow children exist. The submit
  (func_00719B1C -> func_00526374) only appends a deferred record to the game's per-frame "pe::luminance" child
  pass (func_0052647C from func_0002FAA4 stage 3); the capture has only the CLEAR on 0x0E590000 and the manifest
  cannot see draws the backend guards drop. Module 033 uses TOC 0x8E3858 (constants re-read). Next:
  lumflag-trace-4.patch + lum-rt-drawlog.patch (LUM_RT=0E590000) in one run. COLOR_EXPOSURE.md Round 10.
- Round 11 (2026-10-22 ~00:45): trace-4's "only one glow=1 record" was the 300-line log cap (303 of 305 record
  lines are menu-era pre-pass quads; the glow=1 line is a 1-in-1000 sample) - glow records ARE appended. Found the
  executor: records run at frame flush (`func_0052577C` -> `func_00525488` prepare / `func_00525538` execute over
  the 11 root passes, recursing `pass+0x1C`), `func_00525FFC` calls the command descriptor stored in each record's
  state snapshot (`[snap+0x38]`; 0x8BBE50 = draw `func_00527008`, 0x8BBF10/EF8/E68 = clears) or skips the record
  when it is 0. The draw command ends in `func_00572E60` (029), the only caller of the EBOOT's only FIFO draw
  emitters (`func_00656F0C`/`func_00657248`), walking 8 vertex batches at `snap+0x20..0x3C`. No pass filter
  anywhere, so luminance draws vanish either as fn=0, as empty batches, or in the backend. Next test:
  `lumflag-trace-5.patch` (`[LUMEXEC]` exec/record/drawcmd/draw lines for `pe::lum*` passes) + confirm `LUM_RT`
  is exported. COLOR_EXPOSURE.md Round 11.

## §41 follow-up: the dark floor half is "always in shadow"; casters land near depth 0 (2026-10-21) — patch not yet run

- Reading the ZPASS_SHOW numbers with the game's convention (a comparison result of 1 = lit, since
  forcing 1 brightens the floor): today the comparison returns 0 ("in shadow") over the whole dark
  half. The stored map depth is ~0 under the floor (mode 2 dark) while receivers' reference is ~1
  (mode 3 bright), so the receivers' check (reference <= stored) always fails. The floor is in the
  map itself, so its stored depth should match its reference: the casters land near 0.
- Likely: the shadow pass's viewport z mapping (scale 0 falls back to raw clip z, and the GL-style
  negative half is clamped to 0), or polygon offset. Patch `patches/shadow-depthmap/`: candidate fix
  `ZPASS_CASTER_Z01=1` (GL 0.5/0.5 mapping on casters), a `ZPASS_DBG` log of each caster z mapping,
  `ZPASS_SHOW_UNIT=<n>`, and `ZPASS_SHOW=5` (reference minus stored).

## 42. STRIP_INDEXED tested: same picture, slower (2026-10-21)
Commit de12d74 (opt-in). strips\demo_base|demo_ix, s04_base|s04_ix (Vegetable Sky), s06_base|s06_ix
(Namek). Last 150 [PERF] windows:
| run | fps | vtx s/window |
|---|---|---|
| demo base / ix | 28.9 / 25.1 | 0.229 / 0.297 |
| s04 base / ix | 28.0 / 26.8 | 0.199 / 0.293 |
| s06 base / ix | 27.6 / 24.0 | 0.222 / 0.343 |
- Vertices processed drop to a third (demo 2.46G -> 0.80G over the run), but vertex time goes UP
  ~40%, so fps falls 1-4. Demo frames at the same moment match base (t190 diff 1.5, t210 2.7);
  no missing triangles seen on Vegetable Sky or Namek. Log shows `[VFETCH] async strip fetch on, 3
  worker thread(s)` too; inferred: the indexed path's dedup/fetch doesn't get the async workers'
  benefit (needs a look). Stays opt-in.
- lum-readback-4 (85c4157) demo (color\lumrb4): now `off=0x039FFA40 dma=0xFEED0001 20x11 ... ->
  guest 0x48CFFA40 (main) meanRGB=82-124` as predicted. `[TONEMAP]` lines still read 7200, but they
  are only printed for the first draws, so they don't show later values. The PICTURE changes:
  the Ace card (t190) and Goku card (t200) are clearly darker and more contrasty than base
  (whole-frame diff 27-34 where two base runs differ by 1.5-2.7), i.e. the exposure now adapts,
  but towards darker, further from the RPCS3 Ace screenshot (which is brighter than our base).
  Stays opt-in. Next: log c1.w over time, and check the luminance encoding the game expects.

## Why STRIP_INDEXED=1 was slower; version 2 drafted, not yet run (2026-10-21)

Write-up, patch and test: `patches/strip-indexed-v2/`.
- §42 runs: vertices fell to a third, but present-thread cycles in `vtx` rose ~270 M per window
  (s06 581-597 -> 800-835 Mcyc; demo 492-503 -> 771-792). Version 1's dedup map (768 KB, up to 8
  probes in 3 arrays per index) runs on the present thread, the bottleneck. The work it saves
  (decode, upload, vertex shading) was on the async workers and the GPU, which are not.
- `STRIP_INDEXED=2`: the present thread only copies the indices (restart -> 0xFFFFFFFF), reserves
  slots and posts a job. A worker builds the strip indices (per-call dedup) and fetches the
  distinct vertices. A call continuing the record reuses its fetch plan (`STRIP_IX2_PLAN_REUSE=0`
  turns that off). Offline: identical triangles in 50/50 streams; loop cost per index 0.1 ns vs
  1.0 ns (padded) and 2.0 ns (v1).
- Expected break-even to a few ms per fight frame (inferred). The loop is a small part of `vtx`;
  per-call costs (plan build hashing the vertex program, posting, PERF's cycle timer) dominate.
  Next if it doesn't help: count vertex-program uploads in `rsx_commands.c` and key the input-mask
  cache on that instead of hashing the program per call.

## 43. Upstream v0.17 cherry-picks (2026-10-21)
v0.17.0 fetched from upgrade-0.17\*.bundle as tag v0.17.0 (no full upgrade).
- `_bx2`/biased signed (122690f): already covered by the tex-expand patch (2a73fc2); no [FPEXPAND]
  lines in any run, so nothing to change in this game. Not picked again.
- SPU float fast paths 517fc71 + ef060b3 -> 081f1f2, dbe9dba: demo 28.94 -> 29.02 fps, Namek
  27.6-27.9 -> 27.4 (noise). The AVX2 path needs `__AVX2__`, which the port build doesn't set
  (no /arch:AVX2), so only the SSE2 compares are active. Neutral; kept.
- 31-bit texture offsets e3f7872 -> c8d2329: demo 29.08, Namek 27.1, demo frames match base (diff
  3.8). Neutral; kept (correctness fix).
- tonemap-log (929b000), Yoshiwara fight with LUM_READBACK=1 (color\yoshi_tmlog):
  in the fight La moves 0.09-0.15 (whole run: mean 0.107, max 0.231) while the readback's
  Lgeo = 0.060-0.092 (E~ 7.9-12.1). La does not settle on Lgeo; it stays ~1.5-2x higher.
  E x La = 0.36 on every line (387 of 388), so E = 0.36/La, half the 0.72 expected; logged E is
  2.3-3.9 vs E~ 8-12. So the loop runs, but neither La -> Lgeo nor E -> E~ matches.
- **Round 3 (read-only):** casters are fine (`shadows\dbg`: 0.5/0.5, no polygon offset, ~107 caster
  draws per cascade, cleared to 1.0). The floor program compares **t15, the scene's own depth
  buffer**, as well as the cascades, and the earlier ZPASS_SHOW runs changed all three at once. Top
  suspect: the t15 self-comparison (our depth copy already holds the floor's own depth). Next runs:
  `ZPASS_SHOW=1 ZPASS_SHOW_UNIT=15`, `ZPASS_SHOW=2 ZPASS_SHOW_UNIT=7`. Details:
  `patches/shadow-depthmap/README.md` "Round 3".

## 44. Shadow round 2 runs (2026-10-21, commits 8c85231, b14e960)
Yoshiwara, shots in shadows\<run>\16_fight_20s.png (same hall view unless noted). Left floor =
mean of x 20-520, y 420-500 (the dark half); normal = 48, all-units mode 1 = 76.
- `ZPASS_DBG=1` (shadows\dbg): one mapping for the whole run: `[ZPASS] caster z map: viewport
  scale.z=0.5 offset.z=0.5 clip=[0,1] polyoffset fill=0 scale=0 bias=0 zstate=0x113`.
- `ZPASS_CASTER_Z01=1` (z01): left floor 48, unchanged (expected from the log).
- `ZPASS_NO_POLYOFFSET=1` (nopoly): camera ended elsewhere; polygon offset is 0 anyway.
- `ZPASS_SHOW=5 UNIT=7`: left floor 35 (darker; right floor 179, much brighter).
- `ZPASS_SHOW=5 UNIT=8`: 48, unchanged.
- `ZPASS_SHOW=1 UNIT=15` (15_fight_start, 20s shot was another view): 52, about unchanged.
- `ZPASS_SHOW=2 UNIT=7`: **74**, the dark half mostly gone (a lighter grey band remains); no caster
  shapes visible.
- So the dark half follows unit 7 (cascade 0x0CE40000), not the scene depth (15) or unit 8.
  Mode 5 on 7 going darker means 0.5+4*(ref-depth) < 0.5 there: reference below stored depth.
## 45. Glare layer draw log (color\yoshi_rtdraw, RT_DRAWLOG=0E590000)
400 lines (cap) of a repeating `surface A=0x0E590000 target=0x1 clip=1280x720 dma=0xFEED0000` /
`color_target=0x1` / `clear ... mask=0xF0 color=0xFF000000`; no BEGIN or draw lines between them.
So the game itself binds and clears the glare layer and issues no draws into it (not a backend
filter). RSX_CAP_ALPHA captures (alpha BMPs) are in color\yoshi_alpha_cap\frame_0*\*_a.bmp.

## §44 read: unit 7's comparison runs the wrong way round (2026-10-21) — patch drafted, not applied

With the game's convention (1 = lit): unit 7 (cascade `0x0CE40000`) stores ~1 under the dark left
floor (SHOW=2: 74, empty map) and the floor's reference is smaller (SHOW=5: 35). That is physically
lit, but our helper computes GL's `reference > stored` for zfunc GREATER, so it returns "in shadow".
RSX compares `stored OP reference` (RPCS3 also reverses texture compares). Draft
`patches/shadow-cmp-swap/` (`ZPASS_CMP_SWAP=1`: GREATER becomes LESS, GEQUAL becomes LEQUAL), to be
run together with `S_CLAMP_UNITS=0x180` so lookups outside a cascade stop wrapping. Run plan in its
README.

## 46. ZPASS_CMP_SWAP (309574b) first runs (2026-10-21)
- `ZPASS_CMP_SWAP=1`, Yoshiwara (shadows\swap): at 20 s the camera was at the stairs; the floor is
  even light grey like RPCS3 and **characters cast soft shadows at their feet** (Luffy, Gintoki,
  Goku; 16_fight_20s.png). At the fight start (hall view, 15_fight_start.png) the dark left floor
  half is still there (left floor 57 vs normal 48).
- `+ S_CLAMP_UNITS=0x180` (swap_clamp): both shots are the hall view; the dark left half is still
  there in both.
- `+ clamp`, demo (swap_clamp_demo): Ace card matches base (t190 diff 4.2); on the Goku card (t210)
  the lower-left Namek ground and the grass under Goku turn dark (diff 77/88 in those cells), which
  looks like a wrong shadow, not RPCS3.
- fps: demo 28.5 (base 28.9), Yoshiwara swap 28.7, swap+clamp 27.2 (base ~27.9). Stays off.
- **After §46 (read-only):** the Namek ground darkening with `S_CLAMP_UNITS` fits out-of-map
  shadow lookups. CLAMP reads the map's edge (casters there), WRAP reads random texels (mostly
  lit), and the PS3 most likely uses clamp-to-border with border depth 1 (lit). Next candidate:
  `S_BORDER_UNITS=<hex>` (static sampler BORDER + opaque white) with `ZPASS_CMP_SWAP=1`, needing a
  small code change. The hall view's remaining darker half may be the gallery's real shadow drawn
  too hard and dark, or out-of-map lookups; t15 is the next check. See
  `patches/shadow-cmp-swap/README.md` "Round 5".

## 47. lumflag trace (jstars\port\recompiled\ppu_recomp_019.cpp, backup .pre-lumflag), Yoshiwara
color\yoshi_lumflag: `[LUMFLAG] set_luminance_pass(1) lr=0x0002FB04` 200 times (cap), and
`[LUMFLAG] RenderLuminance: pass_flags=0x0000003F aux=0x00000000` on all 15 samples. So the game
requests the pass and the flags say it is on (0x3F): the draw is lost deeper, not skipped by a flag.

## 48. fp-gain (character-only exposure) runs (2026-10-21, 3cef758 + fix ba84d89)
- The patch first changed nothing: the character programs write several targets, so the
  single-output anchor never matched. Fixed by also adjusting `_o0` before `_out.t0 =` (ba84d89).
- Demo, FP_GAIN_MININSTR=150 (color\fpgain\ace_demo_compare.png): x0.5 makes Ace much darker and
  browner, x1 and x2 a little darker than normal; sky and Namek ground unchanged; one palm trunk at
  the right edge also darkens. fps 28.3-28.8 (base 28.9).
- Yoshiwara, same selector (color\fpgain\yoshi_compare.png): at x3 and x5 large parts of the hall
  (floor, walls) go black, so 150 instructions also catches many Yoshiwara stage programs; x10-x50
  look close to normal. fps 27.2-27.9 (normal ~28). Next: pick exact character code hashes with
  FP_GAIN_KEYS (or a higher threshold) before comparing looks.

## 49. lumflag-trace-2 (jstars port, backup ppu_recomp_019.cpp.pre-lumflag2), Yoshiwara
color\yoshi_lumflag2: 60 `lum-vcall` and 60 `out-vcall` lines; every one has vt=0x00884488
fn=0x00626790 (nodes 0x492BE130 and 0x492BED70..0x492C1540, r4 varies, e.g.
`lum-vcall #0 node=0x492BE130 vt=0x00884488 fn=0x00626790 r4=0x48433A20`). pass_flags 0x3F on all
16 samples. So the emitter list is not empty and the luminance pass calls the same render function
(0x00626790) as the normal output pass.

## 50. cap-code captures and lumflag-trace-3 (2026-10-21)
- cap-code (e0709b6): every capture DRAW line ends in `code=0x...`. Captures:
  color\capcode\yoshi_cap\frame_00..02 (Yoshiwara fight, frames 10000/10300/10600) and
  color\capcode\demo_cap\frame_00..07 (frames 6000-7400): 00-01 Ace card, 02-04 Son Goku,
  05-07 Misogi Kumagawa (contact sheet color\capcode\demo_frames.png).
- lumflag-trace-3 (jstars port ppu_recomp_033.cpp, backup .pre-lumflag3), Yoshiwara
  (color\yoshi_lumflag3): `func_00631C24` (submit) lines, 40, carry r4 = 0x48432830 / 0x484329E0 /
  0x487329E0 / 0x48A32830 / 0x48A329E0: all of them out-vcall contexts, none a lum-vcall context.
  The 136 `elem-draw` lines have 92 distinct ctx values: 7 match lum-vcall contexts, 5 match out-vcall contexts (e.g. `elem-draw #0 ctx=0x48433A20 pass=0 layer=0
  elem=0x4903B5B4 desc=0x409AF250 mask=0x11 type=0`). Also `func_00634968` and `func_006329BC` lines.

## 51. fp-gain with the AI assistant's 73 character keys (d89b94e, keys from color\capcode\fp_gain_keys.txt union)
Montages: color\fpgain\keys\yoshi_keys.png (Yoshiwara fight start, normal / x3 / x5 / x10 /
gain 1.5 / RPCS3), ace_demo_keys.png and goku_demo_keys.png (demo, normal / x0.5 / x1 / x2).
- Only the fighters change; the Yoshiwara hall (floor, walls, gallery) is identical to normal in
  every run, and the demo sky, Namek ground and the palm trunk no longer change.
- 50 programs log `(adjusted)` on Yoshiwara. x3 makes the fighters very dark (Goku nearly black),
  x5 dark, x10 close to normal; gain 1.5 brighter. Demo: x0.5 dark brown, x1 and x2 slightly darker.
- fps unchanged (Yoshiwara 27-28, demo ~28.5-29).

## 52. lumflag-trace-4 + LUM_RT=0E590000 (fdce642; port 027/033 backups .pre-lumflag4), Yoshiwara
color\yoshi_lumflag4: no `[LUMRT]` line at all. `record pass=... name=pe::luminance count=0
key=3.40282e+38 extra=0 glow=0` on all records but one: `record pass=0x4844FCB0 name=pe::luminance count=1 key=0.98619 extra=0 glow=1`. `glow-setup` lines have
vp=0x492B8C80 fp=0x492B9970 (non-zero) with intensities 0.1-0.6. pass-create lines name
pe::opaque, pe::transparency (parent OpGameMain::DrawEntryTa). So the luminance pass gets records
with count=0 except one glow=1 record, and the backend never sees a draw for the glare target.

## 53. FP_EXPOSURE=2 with the 73 character keys on five stages (the maintainer liked x2 on the demo)
color\fpgain\x2\sNN_x2 (00 Yoshiwara, 01 Kameari, 04 Vegetable Sky, 06 Namek, 07 Hidden Leaf),
normals s01_normal/s07_normal (others reuse earlier normal runs). Labelled NORMAL-vs-x2 pictures:
color\fpgain\labeled\{yoshiwara,kameari,vegetable_sky,namek,hidden_leaf,demo_cards}_x2.png.
- Stages unchanged in every run; 44-50 programs adjusted per fight; fps 26.4-28.4 (normal range).
- In fights x2 is much darker than on the demo cards: Naruto (Vegetable Sky, Namek, Kameari) turns
  dark brown, Luffy (Hidden Leaf) nearly black in face/hair, Ichigo almost solid black (Yoshiwara).
  Gintoki stays readable. Inferred cause: Lm = L*E/(L*E+1) depends on the pixel's own brightness, so
  characters in darker fight lighting are darkened more than on the bright demo cards.

## Shadow border switch drafted (2026-10-22) — patch not yet run

The maintainer: shadows and textures before brightness. `patches/shadow-border/` (on fdce642):
`S_BORDER_UNITS=<hex>` (static samplers clamp-to-border, opaque white) so lookups outside a shadow
map read depth 1.0 = lit, and `ZPASS_DBG` now prints each zeta sampler's ADDRESS wrap modes
(4 = border would confirm it is the game's own setting). Test with `ZPASS_CMP_SWAP=1
S_BORDER_UNITS=0x180` on the Namek demo (ground under Goku should stay lit) and the Yoshiwara fight
start (the darker left hall floor may be out-of-cascade lookups, a real gallery shadow, or the t15
self-comparison; the README says how to tell them apart).

## 54. S_BORDER_UNITS=0x180 + ZPASS_CMP_SWAP=1 (315b801), shadows\border\
- ZPASS_DBG: the game asks for border on both shadow maps: `unit 7 samples zeta 0x0CE40000 ...
  addr=0x40040404 (wrap s=4 t=4)` and the same for unit 8 (0x0CA30000); scene depth unit 15 uses
  wrap 3 (clamp).
- Yoshiwara fight start (hall view): left floor 80 and 75 (two runs) vs 48 normal and 76 for
  "all lit": the dark half is gone, floor even. Stairs/side views fine (shadows\labeled\
  yoshiwara_shadow_fix.png, with an RPCS3 frame).
- Demo: Ace card unchanged (diff 4.2); Goku card lower-left Namek ground darker than normal (diff
  77/89 in those cells, same as the clamp run). No RPCS3 reference of that card, so it may be a real
  shadow or not (shadows\labeled\demo_shadow_fix.png).
- fps: demo 28.8, Yoshiwara 28.5 / 29.1 (normal ~28-29). Still off by default.

## After §54: Namek shadow is real; Athena streaks and Dark Tournament rubble (2026-10-22) — read-only

See `STAGE_TEXTURES.md`.
- The Namek ground that darkens with `ZPASS_CMP_SWAP=1 S_BORDER_UNITS=0x180` matches RPCS3's shot of
  the same Goku card (`reference\rpcs3\Screenshot 2026-10-20 184154.png`): darker ground beside the
  blue bush. Real shadow. Together with the game asking for border (§54), propose both as the
  default after a check on two more outdoor stages.
- Athena sky lines: not mipmaps. Ranked: a streak/star effect stretched by a misread per-vertex
  value (TEX9 = o[6] suspect, `FP_TC89=0`); line/point primitives falling to the untransformed
  fixed-function path; or authored. Name the program with `FP_IDCOLOR=1`.
- Dark Tournament flat/streaky rubble faces: planar-projected texturing; RPCS3's Yoshiwara rubble
  looks similar, so likely authored. Otherwise a normal-weighted triplanar with misread normals.
  Needs RPCS3 shots.

## RPCS3 Athena and Dark Tournament screenshots (2026-10-22) — read-only

Details in `STAGE_TEXTURES.md` §4. Athena's parallel sky lines are not in RPCS3: there the sky is a
dense star field with a nebula and a moon, so ours is the star texture smeared along one axis (the
constellation lines in the intro are designed). Dark Tournament rubble in RPCS3 is fully textured,
so our flat grey and streaked faces are a bug, likely the same collapsed texture coordinates. Our
darker floor and rock speckle may already be fixed by the shadow switches and mips (captures
predate them). Next: `FP_TC89=0` on both stages, recapture with current defaults.

## 55. Shadow defaults checked; FP_TC89=0 on Athena and Dark Tournament (2026-10-22)
- Shadow default (fc26535: ZPASS_CMP_SWAP on, S_BORDER_UNITS=0x180): Kameari gets soft building
  shadows on the road, Vegetable Sky ground lit with shadows, Yoshiwara floor even
  (shadows\labeled\outdoor_shadow_default.png).
- FP_TC89=0 (stages\athena\athena_tc89.png, stages\darktourn\darktourn_tc89.png, with RPCS3 shots):
  both stages turn almost entirely black (stage geometry unlit) and the Athena sky lines stay.
  Dark Tournament rubble can't be judged (everything black). So TEX8/TEX9 wiring is NOT the cause of
  the lines, and the stage materials need TC8/9.
- FP_IDCOLOR did nothing with the current decompiler (anchor `_po.c0 = ` gone); fixed in 9ffcec9
  (colours _o/_o0). With it the whole screen turns one colour (the final full-screen pass is painted
  too) and the tour's screen recognition fails, so it can't drive a menu tour. A plain
  RSX_CAP_PASSES=1 Athena capture (stages\athena\normal_cap\frame_00) caught a stairs view with little
  sky. Next: capture a frame with open sky (intro card), or limit FP_IDCOLOR to scene programs.

## Athena star lines traced to unit 0's CLAMP sampler (2026-10-22) — read-only, inferred

Details in `STAGE_TEXTURES.md` §5. The lines come from the blended sky layers (intro frame
ops 383-395: a 1024x1024 DXT5 star layer, a 512x512 layer, and 64x64 star sprites). All of them
sample unit 0, which is our static POINT+CLAMP sampler. With a tiled star texture, CLAMP repeats
the edge texels into straight parallel lines and leaves an empty sky elsewhere, which matches every
capture. Test: `S0_LINEAR=1` on Athena, and on Dark Tournament, since its rubble fits the same
cause. Real fix: per-draw sampler addressing (STAGE_PLAN #9).

## 56. Athena sky: which draws make the lines (2026-10-22, FP_IDCOLOR_RT 077fea4)
- Captures: stages\athena\intro_cap\frame_00..09 (RSX_CAP_PASSES=1, frames 8000-9800 every 200;
  intro cards Luffy / Gintoki / Goku / Naruto with open sky, lines visible in 02-05, 09) and
  stages\athena\idrt_cap\frame_00..09 (same with FP_IDCOLOR=1 FP_IDCOLOR_RT=0ED10000).
- FP_IDCOLOR_RT still turns the final picture one colour: ops 393/394 are full-screen post passes
  that draw INTO the scene target 0x0ED10000 (vs=0 n=6), so the tour can't see the fight HUD.
- In idrt_cap\frame_03 the sky before op393 is one flat colour (the sky dome, no lines). The lines
  appear between op394 and op409 (faint in pass_op0409_pre). Ops 395-408 are the blended sky
  effect draws, all after the post composite:
  - 395-402, 403, 409: fp 0x09CA8181 / 0x09CA7DC1 / 0x09CA7841, code=0xA4F33A9B, vs=29, n 57-518,
    t0 0x0B8DD580 / 0x0BCC4400 / 0x0BD04400
  - 404-406: fp 0x09CA7F81, code=0xCF90795D, vs=29, n 479-578, t0=0x0BBC4400
  - 407-408: fp 0x09CA7BC1, code=0x4C16FC33, vs=27, n=569, t0=0x0BA1D600

## 57. S0_LINEAR=1 (slot 0 tiling) on Athena and Dark Tournament (2026-10-22)
Pictures with RPCS3 shots: stages\athena\athena_tiling.png, stages\darktourn\darktourn_tiling.png.
- Athena: the parallel sky lines are GONE in both the intro card and the fight; the sky shows a dark
  blue star field with dots, close to RPCS3 (moon and nebula glow not in our view). Confirms cause 1
  of STAGE_TEXTURES §5: unit 0 forced to CLAMP while the star layer tiles.
- Dark Tournament: the camera landed on the broken-wall side (matches RPCS3 013909's open view); the
  rubble blocks show rock texture with dark faces, no flat-grey slab in view. Not conclusive.
- S0_LINEAR also switches unit 0 to linear filtering, which caused the black boxes around plants on
  Penguin Village (§36). Proper fix: take unit 0's wrap mode from each draw's ADDRESS word, keep its
  filter as now.

## Unit 0 per-draw wrap patch drafted (2026-10-22) — not applied

`patches\unit0-wrap\`: s16..s23 copy s0's filter with the game's S/T wrap (wrap, mirror or
clamp), chosen per draw from `tex[0].addr`. Switches: `U0_WRAP=0` (off) and `U0_WRAP_LOG=1`. It
applies to HEAD 077fea4, not to the current working tree, which already holds the AI assistant's
equivalent `T0_WRAP`. The README lists three points to fold into `T0_WRAP` (mirror, depth-helper
units, filter copy) and a five-step test plan.

## 58. T0_WRAP (unit 0 follows the draw's wrap mode), default on (2026-10-22)
- da47404/b42bca4 used extra static samplers s16-s23: every shader failed (ps_5_0 has 16 sampler
  registers, X4509) and all stages went black (stages\t0wrap2_broken). Rewritten in 4b2e7c4: unit
  0's 2D lookups go through `_t0w()` (frac / mirror of the UV, SampleGrad with the original
  gradients), for both texture declaration forms. T0_WRAP=0 off, T0_WRAP_LOG=1 lists programs.
- Athena: star field and moon, no parallel lines (stages\labeled\athena_wrap_fix.png); off brings
  the lines back. No compile failures on any stage.
- Yoshiwara floor: even light-grey tiles instead of the dark teal half
  (stages\labeled\yoshiwara_floor_compare.png). The maintainer noticed it looks better.
- Namek ground: now a tiled detail texture, but very grainy/noisy (unit 0 is point-filtered, so the
  repeated detail shimmers) (stages\labeled\namek_wrap_compare.png). Possible regression; smoothing
  unit 0 would help but brings back the Penguin Village plant boxes (§36).
- Penguin Village plant boxes still there (A2C on, filter unchanged). Kameari intro the same on/off.
- fps: Penguin 28.6, Dark Tournament 29.0, Yoshiwara 28.4, Namek 27.5, Kameari 27.1 (off 24.9).
- Sky flash check, Penguin Village (stages\labeled\penguin_sky_switches.png): default, T0_WRAP=0
  and shadow fix off (ZPASS_CMP_SWAP=0 S_BORDER_UNITS=0) all show a normal blue sky in the intro and
  20 s fight shots; the flash is not caught by single screenshots. The maintainer says it predates these fixes
  (also on Kameari). Exposure tests on Kameari (TONEMAP_EXPOSURE=24, LUM_READBACK=1) queued.
- 2026-10-22 ~12:00: the maintainer reports the alpha-test build (e78f5c1, ALPHA_TEST default on) fixes the
  "Press any button" title screen: the correct textures now show behind the characters.
- The maintainer (2026-10-22 12:12) prefers the T0_WRAP Namek look (namek_wrap_compare.png): keep T0_WRAP default on.

## 59. Leaf fix (alpha test, e78f5c1), Kameari exposure, Penguin sky burst (2026-10-22)
Pictures: stages\labeled\leaf_fix.png, kameari_exposure.png, penguin_burst.png.
- `[ALPHATEST] enable func=4 ref=60` logged. Hidden Leaf fight: trees are cut-out leaves with the fix;
  ALPHA_TEST=0 gives jagged black leaf cards. Title screen background fixed (the maintainer).
- Penguin Village plant boxes: still there WITH MENU_A2C=1, GONE without it (flowers and plants
  correct, s05_noa2c). So MENU_A2C (opt-in, but set in every tour so far) causes the boxes now that
  the alpha test works. Tours should drop MENU_A2C=1.
- fps: Hidden Leaf 28.4 (ALPHA_TEST=0 run 21.0, different camera), Penguin 28.7, Yoshiwara 28.2,
  Athena 27.7.
- Kameari: TONEMAP_EXPOSURE=24 and LUM_READBACK=1 fight shots look like default (sky pale both);
  the exposure-24 intro shot was black (caught a fade). Not conclusive from stills.
- Penguin sky burst (10 shots 0.2 s apart in the fight): frames 00, 01, 05, 09 have a completely
  WHITE sky, the others a normal blue sky with clouds. So the sky layer is missing on some frames,
  not overexposed.
- The maintainer (2026-10-22 13:12): with T0_LINEAR (c3fe813) some small missing UI textures in the Main Menu and one in the in-game match UI now show.

## 60. T0_LINEAR (c3fe813, default on, T0_LINEAR=0 off), stages\labeled\smoothing.png
- Namek ground smoother, far less grain than T0_WRAP alone. Athena sky still dark blue, no lines;
  Yoshiwara unchanged; Penguin intro camera differed, no plant boxes in view (run had MENU_A2C=1).
- The maintainer: Main Menu bottom bar now reaches the right edge, one in-match UI texture back
  (stages\labeled\menu_ui_t0lin.png).
- fps: Namek 27.0, Penguin 27.8, Athena 27.6, Yoshiwara 27.7. No compile failures.
- The maintainer (13:31): smoothing.png also shows the in-match HUD fixed (white patch at the bottom-left portrait area gone in Namek, Athena, Yoshiwara) and smoother character models. T0_LINEAR stays default on.

## 61. Penguin 10-frame capture and loading-screen texture log (2026-10-22)
- RSX_CAP_PASSES=1 RSX_CAP=10000:10:1 on Penguin (stages\pv_flash\cap): only 6 frames written
  (10000-10005), none with a white sky (stages\labeled\penguin_cap_frames.png); frame 10003 has 1476
  draws and 10004 only 251 (a mid-frame flush split). The capture slows the game, which may change
  the timing of the flash.
- Loading screen with TEX_BINDDBG=300 TEXFMTDBG=1 TEXFMT_SKIP=1 (stages\loading\texdbg): no
  `[TEXBIND] null SRV` and no `[TEXFMT-SKIP]` lines. The unit-0 textures seen are linear DXT5 (0xA8)
  NPOT images (826x360, 805x32, 711x42, 671x92, 648x344, 640x360, 490x500, 512x78, 52x25), all
  bound. So cause 1 (no SRV) is ruled out; next a capture spanning the loading screen (cause 2).
- Loading-screen capture RSX_CAP=4500:30:150 (stages\loading\cap, contact sheet
  stages\labeled\loading_frames.png): frames 13-15 (6450-6750) are the loading screen, white in all
  three, the last one already "Now PRESS x". So the art never appears late either. frame_14 manifest
  has only 15 ops / 11 draws: the post chain on 0x0ED10000 (white), then three blended 6-vertex
  quads into the UI layer 0x0C6ABF40 (t0=0x0082C900 fp 0x0D64FF01; t0=0x00194180 and 0x0018A180,
  fp 0x0FC00541/0x0FC00581, cmask 7) and the display copy. The tips panels, pictures and banner are
  never DRAWN: the game issues no draws for them, so neither texture setup nor timing is the cause.
  Inferred: the game skips the tips page (e.g. its data/file load fails or a state check), to look
  at on the PPU/file side.
- Loading screen TEX_SRCDBG=200 TEXUP_DBG=1, with and without MENU_TEX_PITCH (stages\loading\srcdbg,
  srcdbg_nopitch): identical logs. All linear DXT5 (0xA8) NPOT images seen have data in their source
  memory, e.g. `826x360 nonzero=2535/4298`, `640x360 nonzero=1721/3778`, `648x344` x4 ~2900/3655,
  `671x92 889/1012`, `805x32 ~190/423`, `711x42 256/490`. Only the shadow maps (0xA5 1024/2048) read
  0. The log hit its 200-line cap; 490x500 and 52x25 were not logged. No [TEXUP] lines appeared
  (TEXUP_DBG only prints a per-second upload rate). So: source memory is not empty for these, and the
  pitch switch changes nothing.

- **Correction (AI assistant thread, 2026-10-22):** the tips page IS drawn: frame_13 (6450) has its 14
  quads into 0x0C6ABF40 and that layer is pure white (every pixel 255), so they draw with alpha 0
  or zero area; frame_14 is after the page was removed. See STAGE_TEXTURES.md §11 update.

## 62. Hidden Leaf leaf cards: which shader, and where the cut-out comes from (2026-10-22)
a2c-log (1565a6b). Coloured capture stages\leafcap\id_cap\frame_00 (FP_IDCOLOR_RT=0ED10000,
RSX_CAP_PASSES=2, frame 10000; picture stages\leafcap\id_scene.png = pass_op0337_pre). The normal
capture wrote nothing (normal_cap empty). Colours mapped through [FPID] lines:
- tree leaf cards: code=0x8E610328 (fp 0x0FC375C1/0x0FC18A41 in this run; fp_op0218.hlsl: 17
  instrs, t0 DXT1 0x86 256x256, remap 0xAAE4, vs=19)
- grass cards: code=0x70B1A414 (fp_op0215.hlsl: 74 instrs, t0 DXT5 0x88 512x256, 3 colour targets)
- Header for both: `alpha=(0,0,7,0)` (alpha test OFF, func ALWAYS, a2c=0), blend=0. So they are not
  alpha-test or A2C draws. The output alpha is overwritten (max of rgb, then a constant).
- What they do instead: `cc0.x = c.x >= tex0.a*2*c'` then `r1.w = position.z` only where cc0 == 0,
  and finally `r1.z = r1.w`: the texture alpha decides a DEPTH value in r1.z. On RSX r1.z is the
  depth export (SHADER_CONTROL depth bit), so transparent texels get a far depth and fail the depth
  test. Our HLSL has no SV_Depth for these programs (no `_out.depth`), so the cards draw whole.
  Inferred: the depth-export bit is not set/seen for these programs (FP export control), or the
  decompiler's auto mode drops it. Next: check SHADER_CONTROL for fp 0x0FC10F81 / code 0x8E610328.

## 63. FP_DEPTH_EXPORT (2a32615, default on, FP_DEPTH_EXPORT=0 off), stages\dexp
Pictures: stages\labeled\depth_fix_hidden_leaf.png (with RPCS3), depth_fix_other.png.
- Hidden Leaf: trees are proper leaves against the sky, grass in blades, very close to RPCS3;
  FP_DEPTH_EXPORT=0 brings back the black jagged cards.
- Penguin Village plants, Alabasta palms, Kameari intro: no black cards. Yoshiwara floor and Athena
  sky/fog unchanged. The Penguin fight shot happened to catch a white-sky frame (the known flash).
- One capture program (fp_op0437) now has `_out.depth`. No compile failures.
- fps: Hidden Leaf 28.4 (off 28.9), Penguin 28.9, Alabasta 22.6, Kameari 23.6 (27.1 in §58, other
  characters/camera), Yoshiwara 27.5, Athena 28.2.
- The maintainer (2026-10-22 15:08): keep FP_DEPTH_EXPORT on by default.
- The maintainer (15:12): FP_DEPTH_EXPORT also removed a black square behind text on a building in Penguin Village; likely fixes other decals/signs too. Look for sign/fence/window/decal differences in future stage pictures.

## 64. Loading screen runs A and B for the AI assistant (2026-10-22)
- Run A (stages\loading\runA, captures stages\loading\cap2): RSX_CAP=6400:2:100 RSX_CAP_PASSES=2
  RSX_DBG_VTX=3422080 RSX_DBG_VP=3422080 RSX_VTXDBG=300 BLENDDBG=1. cap2\frame_00 = frame 6400 with
  134 draws (the tips page frame, 162 files incl. pass images and fp_op*.hlsl); frame_01 = 6500, 12
  draws. No retry needed. duck_vp.hlsl was NOT written: no [DUCKVP] line (RSX_DBG_VP compares
  cur_texs[0].raw at VP-compile time; the UI vertex program was probably compiled earlier, before
  that texture was bound, so it never matched). No [VTX] lines from RSX_VTXDBG/RSX_DBG_VTX either.
  18 [BLEND] lines in stderr.
- Run B (stages\loading\runB): TEX_A1_UNITS=1 broke the title screen enough that the tour never
  recognised it ("waiting for title: best=85.1", "no title"); no 11_loading.png. Needs a manual look
  or a different unit mask.
- The maintainer watched run B (TEX_A1_UNITS=1): characters' eyes turned black and other earlier-fixed problems came back. Not a fix; switch stays off.

## 65. White sky flash root cause: split "Lens Godray" fan (2026-10-22, AI assistant, read-only)

- The quarter-size white sky layer (§59/§61, STAGE_TEXTURES §10) is the sun-shaft post effect: mask
  (op712, sun colour on the sky) -> 3 radial blurs -> a 42-vertex TRIANGLE_FAN (sun position to 41
  screen-edge points, `func_005567AC`) that rewrites the target with the scaled blur -> additive
  composite (op717). White sky = fan did not cover the sky, raw mask added.
- Captures: fan recorded n=120 in frame_00/04 (clean), n=114 in frame_01/02/03/05, and only those have a
  211-px raw-mask wedge at rows 0-10 cols 125-173 of rt_0D7A0000. 114 = 3*(42-4) = two batches, each
  drawn as its own fan. The game's indexed emitter `func_00657248` cuts the first batch at a 128-byte
  index-buffer boundary; the backend joins batches only for TRIANGLES and indexed strips.
- Patch `patches\sky-flash\fan-join.patch` (FAN_JOIN, FAN_LOG) joins STRIP/FAN batches of one
  BEGIN/END into one stream. Details and test plan: STAGE_TEXTURES §13 and the patch README.
- ZPASS/occlusion query ruled out statically: the only cellGcmSetReport caller (`func_00576808`) asks
  for zcull statistics (types 2, 3); `patches\zpass-report` is not needed.

## 66. Loading-screen tips page: the game shows a DXT1 snapshot of the page, taken from guest memory the backend never writes (2026-10-22, AI assistant, read-only)

Corrects §61 ("never drawn") and completes STAGE_TEXTURES §11 (details and file names there).

- **The page is composed correctly.** `stages\loading\cap2\frame_00` (frame 6400, RSX_CAP_PASSES=2):
  the tips quads (fp 0x06251137, vs=4, DXT5 linear parts at 0x03xxxxxx) change the UI layer
  0x0C6ABF40 with the real art (paper tile halves at op012/013, comic-panel pieces op014-019), then
  op024 (fp 0xE71D59BE, vertex-colour gradient, no texture) and op025 cover it and the "Battle
  Characters" summary draws over that. At 6450 the page draws again and a white fade ends the
  frame (so `rt_0C6ABF40.bmp` of `cap\frame_13` being pure white is the fade, not a lost draw).
- **The loading screen draws a different thing.** Frames 6500 (`cap2\frame_01`) and 6600
  (`cap\frame_14`): no part textures; one full-screen draw of a 1280x720 DXT1 linear texture at
  0x0082C900 (fmt 0xA6, fp 0x3C263AA6 `out = col0 * tex`), then "Now loading..." (512x78, which
  shows). `pass_op0011_post` is 255 everywhere outside the text: the snapshot texture is white.
- **It is a snapshot.** EBOOT.elf strings: `loading_game_tips_main/mes/title/chara_%03d_%s_m.pak`
  (the parts) and "Screenshot". The game composes the page with the RSX, encodes the result to one
  DXT1 texture and shows that while the stage loads. The encoder's input is the rendered surface
  read from guest memory.
- **Why it is white here.** The D3D12 backend never writes a render target back to guest memory
  (same gap as the luminance target, COLOR_EXPOSURE.md; `LUM_READBACK` is the only write-back and
  is limited to `LUM_READBACK_MAXPX` pixels, one frame late). `cellGcmSys.c`: NV3089 is a CPU copy
  of guest memory (a render-target source copies whatever is in guest memory), NV0039 (m2mf) is
  unimplemented (`[GCM2D-UNH]`). Whichever path the game takes, it encodes a surface we never
  filled. No `[GCM2D-UNH]` and no `[NV3089] SKIPPED` lines in `color\base\stdout.txt`, so the
  path is either a format-supported NV3089 copy (silent without NV3089_DBG) or a CPU/SPU read
  after a fence; SPU jobs are rare in this title (4 job runs in `stages\alpha\s00`).
- **Open: which path and when.** Decides where the write-back goes. Requested: run C
  (`GCM2D_DBG=100000 GCM2D_TRACE=1 NV3089_GATE=<gate file>`), run D (`LUM_READBACK=1
  LUM_READBACK_MAXPX=921600`, a brute-force end-of-frame write-back of every 1280x720 target: if
  the page shows, the timing is fine and the fix is a targeted version). Diagnostic patch
  `patches\loading-tips\loading-tips-diag.patch` (opt-in `MEMPEEK_FRAMES`, `VRAMSCAN_FRAMES`,
  `RSX_CAP_TEX`; `tools\texbin.py` decodes the dumps) needs the user's yes before the AI assistant builds it.
- **Fix shape (draft, not written yet):** write the composed surface back to guest memory at the
  point the game reads it: for an NV3089/NV0039 source that is a live render target, a GPU
  readback of the source rectangle before the CPU copy (generic, helps the next game too); for a
  CPU read after a fence, a write-back of the UI layer when a snapshot is pending (gated on the
  loading-page draw pattern, or on cellGcmFinish). Cost: one 3.7 MB readback per snapshot, not
  per frame.

**Reading of runs C, D and E (2026-10-22 16:30):**
- Run C: no `[NV3089G]`, no `[GCM2D-UNH]` from Stage Select to the fight HUD, so the game uses
  no RSX 2D transfer for the snapshot: the CPU (or an SPU job) reads the surface itself.
- Run D (`LUM_READBACK=1 LUM_READBACK_MAXPX=921600`, every used 1280x720 offscreen target written
  back one frame late): loading screen still white. Consistent with a read that happens right
  after a fence inside the frame that composes the page: a write-back that lands at the next
  drain is too late. (It also does not cover the display buffer, which lives only in the
  swapchain; if the snapshot reads the display surface, a write-back must come from there.)
- Run E: `MEMPEEK` shows the snapshot texture at 0xC082C900 holding the same DXT1 bytes on all
  141 frames 6380-6520 (`E3 20 A3 20 AA AA AA AA` repeated: c0=0x20E3, c1=0x20A3, a dark brown,
  index 2 everywhere), so it was not encoded inside that window. Run E's route reached the
  loading screen later than runs A/first capture: `cap3\frame_02` (6500) still has the summary
  screen (141 draws, no 0x0082C900 draw). `VRAMSCAN` printed nothing because no 64 KB block of
  0xC0800000-0xC2800000 changed in those frames (the switch works; the window was wrong for the
  surfaces and the time was before the encode).
- The white therefore comes from what the CPU reads out of guest memory for the composed
  surface: white, not zeros. Likely the game fills that surface's memory itself (a CPU clear to
  the page's white before the RSX composes the page on top), then reads it back after the fence;
  on hardware the RSX wrote the page over the fill, here only the GPU copy has it.
- Next: run F (`MEMPEEK_FRAMES=6300:7500`, `VRAMSCAN_BASE=0xCC600000 VRAMSCAN_FRAMES=6300:7500`,
  `GCM_REFLOG=1`, `RSX_CAP=6500:12:30 RSX_CAP_TEX=1`) to see the encode frame, the CPU writes to
  the UI layer / scene surfaces around it, and what the snapshot decodes to.
- Fix shape: at the fence that follows the page composition (or on the CPU fill, if VRAMSCAN
  shows one), run the recorded batch without present (the backend already does this in
  `vp_flush_if_full`), read the surface back synchronously (as `cap_readback_write` does) and
  write it into guest memory before the fence value is published. Gate it so a match frame never
  pays for it (only while a 1280x720 colour target was drawn by UI quads alone, or an explicit
  `RT_SNAPSHOT=<off>` list).

## 65. FAN_JOIN (6afba53) first test: sky still flashes (2026-10-22)
- Penguin burst with FAN_LOG=0D7A0000 (stages\labeled\skyflash_burst_s05.png): white wedges at the
  top of the sky in 4 of 10 frames (00, 01, 04, 07), same as before; FAN_JOIN=0 run
  (skyflash_burst_s05_off.png) for comparison. stderr copied to stages\pv_flash\fanjoin\stderr.txt.
- 612 [FANLOG] lines, 0 JOINED. The split pairs arrive in the same frame/epoch but the second half
  is treated as "new": e.g. `f=8555 indexed prim=7 first=0 count=8 epoch=1108546 rt=0x0D7A0000
  fp=0x0FC92381 new emitted=18 rec=528 n=6` then `f=8555 indexed prim=7 first=8 count=34
  epoch=1108546 ... fp=0x0FC92381 new emitted=96 rec=529 n=18`. Also seen: first=16 count=26,
  first=24 count=18, first=40 count=2 (emitted=0).
- fps: Penguin 28.0 (off 27.5), Kameari 28.3, Yoshiwara 28.2, Athena 29.1. Nothing else changed.
- The AI assistant's FAN_LOG run (stages\pv_flash\fanjoin): indexed fan, 42 indices, splits 8+34 / 16+26 /
  24+18 / 32+10 / 40+2 or whole, 0 JOINED, wedge still flashes. v1 refused indexed fans whenever
  primitive restart is enabled (always, in J-Stars). fan-join-v2.patch checks the indices for the
  restart value instead and logs `why=N` for a refused join.

## 66. Loading screen runs C, D, E for the AI assistant (2026-10-22)
- Run C (stages\loading\runC, GCM2D_DBG=100000 GCM2D_TRACE=1 NV3089_GATE, gate on from START on Stage
  Select 18:01:46 to fight HUD 18:03:39, via tools\menu_tour_gate.sh GATE_FILE): **zero [NV3089G] and
  zero [GCM2D] lines** during the loading. So no NV3089 blit and no inline transfer writes the
  snapshot.
- Run D (stages\loading\runD, LUM_READBACK=1 LUM_READBACK_MAXPX=921600): loading screen still white
  with only "Now PRESS x" (stages\labeled\loading_writeback.png, with RPCS3); 2563 [LUMRB] lines;
  fps 13.5 (write-back is slow); fight looks normal. A plain write-back of render targets does not
  fill it.
- Run E (stages\loading\runE, cap3 frames 6400/6450/6500 with RSX_CAP_TEX=1, 54-55 texture dumps
  each): MEMPEEK C082C900 (the 1280x720 DXT1 at 0x0082C900) gives the SAME 64 bytes on all 141
  frames 6380-6520: `E3 20 A3 20 AA AA AA AA` repeated, i.e. DXT1 blocks c0=0x20E3 c1=0x20A3 (almost
  black), never changing. No VRAMSCAN lines were printed (switch VRAMSCAN=1 may not be the name the
  patch reads; it reads VRAMSCAN_BASE/VRAMSCAN_FRAMES).

## 67. FAN_JOIN v2 (27b2548) fixes the Penguin Village sky flash (2026-10-22)
Penguin burst with FAN_LOG=0D7A0000 (stages\labeled\skyflash_burst_v2.png, stderr in
stages\pv_flash\fanjoin2): **all 10 frames have a normal blue sky** (before: white wedge in 4 of 10).
234 `JOINED why=0` lines (e.g. `f=7640 indexed prim=7 first=8 count=34 ... JOINED why=0 emitted=102
rec=451 n=120 restart=1/0xFFFF dma=0x11`), 379 why=2 (other fans not continued).
- The maintainer (16:29) watched the Penguin run live: no white sky flashes. FAN_JOIN stays default on.
- Kameari burst with FAN_JOIN v2 (stages\labeled\kameari_burst_v2.png): camera mostly looked down at
  the street, little sky in view; no white sky frames. fps Kameari 27.5, Yoshiwara 28.3.

## 68. Loading run F: the snapshot texture is correct in guest memory (2026-10-22)
stages\loading\runF, cap4 (frames 6500-6830 every 30, RSX_CAP_TEX=1).
- MEMPEEK C082C900:64 over frames 6300-7500 (1201 lines): the first 64 bytes never change
  (`E3 20 A3 20 AA AA AA AA`).
- The full dumped texture cap4\frame_05\tex_C082C900_1280x720_A6.bin (460800 bytes) decodes as DXT1
  to the COMPLETE tips page: "Victory Gauge" banner, two framed game pictures, red borders, text
  (stages\loading\snapshot_decoded.png). The dark first blocks are just the top-left corner. The dump
  says fmt A6 (DXT1).
- So the snapshot is already in memory when the loading screen shows (6500+), yet it draws white.
  The problem is between memory and screen: likely our texture cache still holds an older (white)
  copy of 0x0082C900 (checksum/csum-same skip on a sampled region), or the draw samples another
  offset. VRAMSCAN on 0xCC600000 showed 4-8 blocks changing only at f=6576/6577. No GCM_REFLOG lines.
- Runs G/H (loading screen, stages\loading\runG\nocache, runG\verify, runH): TEX_NOCACHE=1,
  TEX_VERIFY=frame and TEX_MIPS=0 (with TEXFMTDBG=1) all still show the white loading screen with
  "Now PRESS x" (11_loading.png ~23.8 KB each). TEXFMTDBG hit its 64-line cap before the loading
  screen: no "[TEXFMT] unit=0 fmt=0xA6 1280x720" line was printed. So neither cache reuse nor the mip
  chain is the cause.

## 69. Loading-screen tips page, root cause: the "screen as texture" heuristic steals the snapshot (2026-10-22, AI assistant, read-only + patch)

Closes §66/§68. The snapshot at 0x0082C900 is right in guest memory (§68). What goes wrong is the
bind, not the upload or the cache:

- `rsx_d3d12_backend.c`, per-draw SRV window fill (`/* Display-sized sampler source -> the
  rendered frame ... */`): when a unit's texture is `s_d3d.width x s_d3d.height` and
  `off_rt_find(raw) < 0`, and `SCREEN_AS_TEX` is on (default 1), the draw gets `s_screen_copy`
  (the copy of the last completed frame, made for the water reflection, `screen_copy_capture`)
  bound at that unit and the guest-texture upload path is never reached (`continue`).
- The tips snapshot is a 1280x720 DXT1 texture that is not an offscreen RT, so every loading-
  screen draw of it sampled the previous frame. At that point the frame is the white fade (6450's
  end state), and from then on the loading screen draws itself: white in, white out. "Now
  loading..." (512x78) does not match the size test and shows.
- Consistent with every run: the guest bytes were right (run F), the cache re-checksums and
  `TEX_NOCACHE`/`TEX_VERIFY`/`TEX_MIPS=0` changed nothing (runs G, H), no 2D transfer (run C),
  `LUM_READBACK` irrelevant (run D), and the page quads themselves paint fine (run A).
- Fix: `patches\loading-tips\loading-tips-fix.patch` (default on, `SCREEN_AS_TEX_ANYFMT=1`
  reverts): the heuristic skips compressed formats (DXT1/3/5). The RSX cannot write compressed
  data, so a display-sized DXT texture is never a frame readback; the reflection's A8R8G8B8
  surface keeps the old path. No-code confirmation: `SCREEN_AS_TEX=0` (page shows, reflection
  lost for that run).
- Reusable lesson (REUSABLE.md candidate): a size-only heuristic that substitutes a backend
  resource for guest memory must at least exclude formats the GPU cannot produce, and should
  prefer the guest bytes whenever they are non-empty.
- Seen in run I (SCREEN_AS_TEX=0): the start-up logos are visible now; they never showed before. The screen-as-texture shortcut also hid those.
- **Addendum (17:20):** the AI assistant's run I (`SCREEN_AS_TEX=0`) also made the **boot logos** appear
  for the first time, so the size-only rule swallowed them too; they are uncompressed 1280x720
  (the `[TEXFMT]` key is per unit/format/width, so they share the RT's entry and never got a line
  of their own), which a compressed-only exclusion would miss. The patch now uses the rule the
  heuristic needed from the start: frame copy only for a texture the RSX could have written
  (uncompressed) whose guest bytes are empty (256 samples over the whole image); anything the CPU
  wrote keeps its own bytes. Switch renamed to `SCREEN_AS_TEX_FORCE=1` (old size-only rule).
- **Tip variety (18:30, read-only check):** the AI assistant's 5 fresh-boot tours all showed "Victory
  Gauge". What the choice could read: the HLE time sources are real (`cellRtcGetCurrentTick` /
  `GetCurrentClock` return host wall time, `sys_time_get_system_time` real microseconds since
  boot), so a seed from either would differ per boot; a frame or match counter would not. The
  pak names are built in `func_0041A698` (recompiled `ppu_recomp_021.cpp`): `snprintf(buf, 0x100,
  fmt, dir, index, ...)` with the formats from the TOC table at 0x8D8AA8-0x8D8AD4 and the index
  (r5) validated against a table at `[TOC+0x51E8]+0x48` (int32 pairs, -1 ends). The index comes
  in through `func_00420AFC(obj, type, index, x)`, vtable 0x885458 slot 2 (descriptor 0x8AF8A0),
  called only indirectly, so the chooser was not traced further. Decision rests on the user's
  test: several matches in ONE session; if the tip changes there, it is a per-session sequence or
  counter and the 5-boot result is expected, not a bug.
- **Closed (19:05):** the maintainer confirms the boot logos show on every boot with 3b68f46. The AI assistant's
  multi-match session (next section) shows the tip changing per match within one session (Victory
  Gauge, Locking On, Dashing), the same order RPCS3 shows; a per-session sequence, not a bug.
  Nothing open on the loading screen.

## 69. Loading tips page and boot logos: the screen-as-texture shortcut (2026-10-22)
- Run I (SCREEN_AS_TEX=0): the maintainer saw the boot logos for the first time. Run I2 with timed shots
  (tools\menu_tour_shots.sh LOADING_SHOTS=12, stages\loading\runI2, sheet
  stages\labeled\loading_runI2.png): the full tips page ("Victory Gauge", two framed pictures,
  borders, "PRESS x") shows on the loading screen, matching RPCS3's layout.
- Cause (the AI assistant): the 1280x720 non-RT texture heuristic bound the previous frame instead of the
  game's own picture. Fix 3b68f46: frame copy only for an empty uncompressed display-sized texture;
  SCREEN_AS_TEX_FORCE=1 reverts. Check run (logos, tips page, Namek water) queued.
- Tour fix: screen_match "press_x" now also accepts the tips page (refs\press_x_tips.bmp, from
  runI2\11_load_11.png); the white-page ref is kept as press_x_white.bmp.
- Fix check (3b68f46, stages\loading\fix, sheet stages\labeled\loading_fix_check.png): tips page
  shows on the loading screen; Namek water and demo cards look the same as before (reflection kept);
  Vegetable Sky fine. Boot logos not caught by the 20/40/60 s demo shots (white / black frames).
- The maintainer (17:43): boot logos show with the fix (3b68f46). All three checks pass: logos, tips page, water reflection.
- Tip variety (stages\loading\tips\s0,s1,s3,s7,s11, sheet stages\labeled\loading_tips_5runs.png):
  all five matches show the same "Victory Gauge" tip (RPCS3 shots show "Locking On" and "Dashing").
  Caveat: every tour is a fresh boot with identical inputs, so the game's random seed may simply be
  the same each boot (time or system-counter based). Not yet tested: several matches in one session.
- Several matches in one session (tools\menu_tour_multi.sh MATCHES=4, the maintainer's route: Pause Menu ->
  Re-select character(s) -> Yes -> START -> START; stages\loading\multi, sheet
  stages\labeled\loading_tips_session.png): match 1 "Victory Gauge", match 2 "Locking On", match 3
  "Dashing" — the tip changes, same order as RPCS3's (the maintainer saw "Locking On" first there after a fresh
  boot). Not a bug. Match 3's tour stopped waiting for "Now PRESS x" (the press_x refs don't match
  every tips page); the tips question is answered anyway.

## 70. Dark Tournament A/B on the current build (2026-10-22, STAGE_TEXTURES §13)
stages\dt13\{default,fanjoin_off,lit,t0wrap_off,t0lin_off}, sheets stages\labeled\dt_ab_20s.png,
dt_ab_start.png, dt_closeups.png (with RPCS3 014020 / 013909).
- Default, ZPASS_SHOW=4 (always lit) and T0_WRAP=0 runs faced the stands: the floor in front of the
  stairs is uniform dark brown; no light-grey ring tiles in view in any of our shots (RPCS3 014020
  shows grey tiles at the ring edge). Always-lit looks the same as default there, so the dark steps
  and floor are not shadow acne. No blue-white speckle visible at this distance.
- FAN_JOIN=0 and T0_LINEAR=0 runs faced the broken wall: rubble has rock texture on its faces and
  matches RPCS3 013909's look (dark, textured); no flat grey slab or streaks in these shots.
- So the rubble looks right now (most likely fixed by T0_WRAP/T0_LINEAR/depth export earlier);
  the ring floor is still unverified: our camera never showed the grey tiles. fps 28.1-29.5.
- Hidden Leaf intro (stages\dt13\s07): cliff behind Gintoki textured, no streaks.

## 71. Dark Tournament ring floor, the maintainer playing (2026-10-22, stages\dt_ring, sheet dt_ring\sheet.png)
tools\menu_tour_play.sh: tour to the fight, then a screenshot every 4 s for 240 s while the maintainer plays.
60 shots. The character stood still for the first ~110 s, then moved around the arena. In every
shot the floor is brown dirt with darker rubble; no light-grey ring tiles appear anywhere (RPCS3
014020 shows grey stone tiles under Goku). The minimap shows a lighter region on one side that may
be the ring; not confirmed whether the character reached it.
- The maintainer (20:47): was away at first; when moving, the ring floor got destroyed before it could be checked. The fight-start shot (p_000, nothing broken yet) also shows a brown floor near the stairs. Retry offered: move onto the ring right at fight start.
- Second try (stages\dt_ring2, every 2 s, sheet dt_ring2\sheet.png; labelled
  stages\labeled\dt_ring_tiles.png): with Toriko on the ring, the floor shows the light-grey stone
  tiles with joints and cracks, like RPCS3 014020. The ring is fine; the brown floor elsewhere is the
  arena dirt. Nothing to fix here.

## CPU profile, first pass (2026-10-22) — read-only, see `CPU_PROFILE.md`

- Steady fights (~29 fps, 34 ms): present thread busy ~23 ms (vtx ~8, other drain ~9, render ~6),
  idle ~10; the game's main thread spends ~37% in its flip/vblank pacing loop.
- High CPU % with little real work (*partly inferred*): short waits spin instead of sleeping.
  `sys_timer_usleep` < 1 ms loops on `SwitchToThread` (CRI file server ~10,500 polls/s, main thread
  ~1,800/s), plus yields in lwmutex/event/cellSync/SPU coherency, and the present thread's
  `GCM_PUT_POLL` idle wait.
- Freezes in the latest run: 28 intervals > 87 ms. Loading/scene switches (renderer idle), and
  mid-fight stalls where `drain` jumps to 0.81 s per 20 frames with `vtx` unchanged, i.e. untimed
  drain work (cause not identified).
- GPU candidates: vertex pulling (decode in the vertex shader; biggest), strip cut
  (`STRIP_INDEXED=2`, untested), compute-shader texture decode, direct GPU access to guest memory.
- Runs requested: `tools\thread_cpu.ps1` + `PS3_SAMPLE=2` + `-Commands threads`; `GCM_PUT_POLL=0`
  A/B; a `/Zi` build for WPR/WPA (or named `[samp] rva`); else a drain-split timer patch (needs yes).

## 72. The idle demo reaches a real fight; flat-colour screens were a one-off (2026-10-23)
- tools\demo_shots_long.sh (run_test 600 s): the attract demo shows the Goku / Madara / Jonathan
  cards, then a real Namek fight from ~6:00 to ~7:00, then the title, then the Naruto/Hiei demo
  (stages\demo_long_ab\default2\sheet.png). Usable as a repeatable fight with no menu inputs.
- First long run (stages\demo_long\sheet.png): from ~6:30 every shot was a single flat colour
  while fps stayed ~29. Not reproduced: the three A/B runs (SCREEN_AS_TEX_FORCE=1, FAN_JOIN=0, and
  FP_DEPTH_EXPORT/ALPHA_TEST/T0_WRAP/T0_LINEAR off) and a second all-defaults run all played on
  normally (stages\demo_long_ab). Intermittent; the maintainer remembers something similar from an older
  game patch. Watch for it in future long demo runs.

## Precise sleep patch drafted (2026-10-23, the maintainer's yes) — not yet run

`patches/precise-sleep/` (`precise-sleep.patch`, README, test). One file: `runtime/syscalls/sys_timer.c`.
- `sys_timer_usleep` < 1 ms now sleeps on a per-thread high-resolution waitable timer instead of
  spinning on `SwitchToThread`. On by default; `PS3_PRECISE_SLEEP=0` = old spin;
  `PS3_PRECISE_SLEEP_MAX_US=<n>` limits it to requests up to n us; `_SPIN_US=<n>` spins the tail.
- Offline on this PC: a 20/100/200 us sleep costs 1-3% of a core instead of ~100%, never wakes
  early, and wakes late by p50 ~0.3-0.5 ms, p99 ~1 ms, p99.9 3-7 ms. Risk: the main thread's flip
  wait wakes later, so watch `[PERF4]`; fallback `PS3_PRECISE_SLEEP_MAX_US=50`.

## CPU round 2: function-level picture (2026-10-23) — read-only, `CPU_PROFILE.md` §10-11

Run B's 30 thread dumps, resolved with the build-pdb PDB:
- drawing thread: `rsx_fp_code_hash` (FP code hash per draw) and `vp_record_cb` (8 KB constant
  copy per draw), ~40% each of its code time;
- main game thread: `spu_coh_is_reserved` out-of-line call per guest load, 27%;
- SPU job threads: `spu_workload_fingerprint` (byte FNV over each job image per dispatch), 20-36%;
- debug hooks on hot paths: `barrier_watch_hit` (per store) 37% of one PPU thread, `spu_depth_guard`
  (shared ring) 55% of one SPU thread.
Ranked plan: (1) job fingerprint cache, (2) inline reservation check, (3) debug hooks behind flags,
as one exact runtime patch, then (4) FP hash memo, (5) constant-bank reuse, (6) GPU vertex
pulling, (7) SSE VMX. Code changes await the maintainer's yes.

## Runtime hot-path patch (CPU_PROFILE §11 items 1-3) drafted (2026-10-23, the maintainer's yes) — not yet run

`patches/runtime-overheads/`, on eee9efe, runtime only. Files: `runtime/spu/spu_workload.c`,
`runtime/spu/spu_coherency.c`, `runtime/ppu/ppu_loader.cpp`, `runtime/spu/spu_drain.c`.
- `SPU_FP_CACHE`: per-thread fingerprint cache with memcmp, exact. Offline: 0 mismatches in 200k
  calls; 30.1 -> 1.1 us per 37 KB dispatch.
- `PS3_COH_INLINE`: the store-path reservation test inlined (exported bitmap and flag).
- `PS3_FAST_DEBUG_HOOKS`: `barrier_watch_hit` only when a watch is armed; `spu_depth_guard` ring
  per thread.
- All default on; `=0` restores each. Compiles with the same warnings as before. Before/after run
  plan in the README.

## GPU vertex pulling scoped (2026-10-23) — read-only, `CPU_PROFILE.md` §12
- Demo fight now: ~720k output vertices and ~1,100 draws per frame. `vtx` ~10 ms/frame on the
  drawing thread is the strip list loop + per-call setup, not decode (3 async workers, ~1 core,
  inferred); ~185 MB/frame of 256-byte slots uploaded.
- `vf_input_mask` re-hashes the whole vertex program per FIFO batch: 0.5-4.1k cycles per call
  measured offline (64-512 instrs), ~1-8 Mcyc/frame (inferred ~2,000 calls).
- Vertex pulling (VS reads guest bytes, D3D strip cuts on the guest indices) would remove the
  workers and the 185 MB/frame and cut `vtx` to ~1-3 ms *only with the strip cut*; ~600-900 lines,
  medium risk, opt-in `VPULL`. fps ceiling ~30 (13% of frames at 3 vblanks now).
- Recommended order: FP hash memo + VP input-mask memo (exact, small), item 5, `STRIP_INDEXED=2`,
  then pulling. Runs requested: FRAME_BUDGET=2, VFETCH_ASYNC_THREADS=1 A/B, build-pdb thread dumps.

## Shader-hash memo patch drafted (2026-10-23, the maintainer's yes) - not yet run
`patches/shader-hash-memo/`, on 6020215, opt-out `SHADER_HASH_MEMO=0`. Files: rsx_commands.c/.h
(new `rsx_state.vp_gen`, renewed on every VP word / start-slot change), rsx_fp_decompiler.c/.h
(`rsx_fp_code_hash_memo`, content-keyed, memcmp-verified), rsx_d3d12_backend.c (call sites in
`vp_get_fp_pso`, `vp_get_vs`, `vf_input_mask`; for the AI assistant to apply).
- Offline: 0 mismatches in 300k calls vs `rsx_fp_code_hash`/`rsx_fp_program_size`; FP lookup
  4.06 -> 0.46 us (235 instrs), 0.70 -> 0.09 us (40). Same compiler warnings as before.
- Expected ~1 ms/frame (FP) + ~1-3 ms (VP), inferred. Before/after demo run plan in the README.

## VPULL patch drafted (2026-10-23, the maintainer's yes) - not yet run
`patches/vpull/`, on 5f73fea, opt-in `VPULL=1` (off = unchanged path). +449/-6 lines: new
`rsx_vpull.h` (HLSL decoder, decode tables, layout check, guest byte copy), rsx_vp_decompiler.c/.h
(pulling mode), rsx_d3d12_backend.c (+247/-6: byte ring 64 MB x2, per-record table b2, root SRVs
t8/t9, upload per index batch with strip cuts in the STRIP_INDEXED index buffer, replay, `[VPULL]`
stats line after `[PERF4]`; for the AI assistant to apply).
- Covers indexed strips with one interleaved stream (no divider, one stride, one memory). Others,
  and a sparse first batch, keep the CPU path.
- Offline GPU test (D3D11 compute, same HLSL vs `rsx_fetch_plan_range`): 18.0M attribute values,
  all 7 types, 0 mismatches. Known difference only when an attribute straddles a 1 MB IO page whose
  next IO page is not the next guest page (CPU reads on linearly; not expected in the game).
- Pulled VS compiles as vs_5_0 with SV_VertexID as its only input (816 static instrs for 3 attrs).
- Run plan: demo fight VPULL=0 / 1 / 1 with PERF=1; watch `[VPULL] dropped` = 0 and screenshots.

## Slow maps and freezes (2026-10-23) - read-only, `CPU_PROFILE.md` §16
- Fight fps across the 16-stage survey follows draws per frame: Hidden Leaf 2,659 draws 17.8 fps,
  Alabasta 1,963 / 19.5, Kameari 1,766 / 20.2 ... Penguin 1,329 / 29.2, Tenraikyo 1,052 / 29.3.
  Hidden Leaf jumps ~850 -> 2,400-3,100 draws at fight start, vertices unchanged. ~15 us per draw
  on the drawing thread (inferred fit); VPULL took the vertex part, the per-draw part remains.
- Freezes: nearly every frame over 70 ms in perf\vpull\on1 coincides with pipeline creation on the
  drawing thread (642 creates, 17.3 s, ~27 ms each; 81 fxc compiles). PSO_ASYNC exists, is off.
- Runs requested: 3 maps with PERF=1 FRAME_BUDGET=2; Leaf with build-pdb + 40 thread dumps;
  PSO_ASYNC=1 A/B on the 3 maps and the demo.

## fps counters count batches, not game frames; VPULL splits heavy frames (2026-10-23) - `CPU_PROFILE.md` §17
- Title-bar FPS and [PERF] fps count render_frame calls (batches), including mid-frame flushes.
  Before VPULL, FRAMELOG shows 1 batch per flip (s07, psleep, runB), so old fps = game rate.
- VPULL copies ~47 MB per batch (median 934 MB per 20-batch window in perf\vpull\on1) into a
  64 MB ring that flushes at 56 MB, so heavy frames become 2 batches: the "48-50 fps" was batches/s
  (inferred). The maintainer's Hidden Leaf slow-motion with the counter at 30-40 fits this.
- Measure: Hidden Leaf, GCM_FLIPCOUNT=1 RSX_FRAMELOG=20 PERF=1, VPULL on vs off, camera to the city
  and away. Fixes to propose after: flip-based fps counter; copy each vertex range once per record.
- Kameari (CPU_PROFILE §18): in the stage survey it created ~2x the textures of Hidden Leaf/Alabasta
  (3,462 vs ~1,870 in 41-44 fight windows), bursts of 150-583 per window, and hit `no-slot 128`
  (all 512 texture slots taken). Its freezes = texture churn + pipeline builds (inferred). Runs asked:
  Kameari with GCM_FLIPCOUNT/FRAMELOG/PERF, and with PSO_ASYNC=1; watch [PERF3] new/created/no-slot.

## Destruction slow-downs (2026-10-23) - read-only, `CPU_PROFILE.md` §21
- No log marks a destruction yet. Stalls in the slow-map runs are pipeline builds, texture bursts,
  or "other": drain up 50-130% with draws, textures and pipelines unchanged (untimed renderer work:
  FIFO decode, records, RSX copies, readbacks). New debris streamed in by RSX copies would look like
  this (inferred); game physics would show as `rest`, not `drain`.
- Run asked: the maintainer breaks buildings on purpose (screenshots every 5 s), build-pdb,
  GCM_FLIPCOUNT=1 RSX_FRAMELOG=20 PERF=1 PSO_ASYNC=1, thread_cpu, `threads` every 2 s.

## Slow-map fixes drafted (2026-10-23, the maintainer's yes) - not yet run, `patches/draw-cost/README.md`
- Hidden Leaf pdb dumps (s07_pdb): drawing thread in vp_record_cb ~40% (+memcpy 13%), FIFO decode
  23%, texture keys 15%; vp_record_cb's hot lines are uncached reads back from upload memory.
- draw-cost.patch (FP_INFO_CACHE=0 off): per-program cache of hash/size/gamma/TXP/constant slots,
  no read-backs, switches read once, texture masks cached. Offline: 0 mismatches in 400k calls;
  235-instr program 10.0 -> 0.78 us per draw. Expected ~4-6 ms per heavy frame (inferred).
- tex-slots.patch: 2048 slots, 1024 default (TEX_SLOTS=512 old) for Kameari's churn.
- fps-flips.patch: title FPS = game flips/s, batches beside (TITLE_FPS_BATCHES=1 old).
- vpull-once.patch (VPULL_ONCE=0 off; only with VPULL=1): a record's batches reuse the vertices
  already copied and append only new ones. Offline: 2.58M attribute reads, 0 wrong; 72.6 vs 238.6
  bytes copied per index on a synthetic strip pattern. Target: no frame splitting (1.59 -> ~1.0
  batches per flip), checked by [FRAMELOG] and [VPULL] copied MB.
- Akainu intro triangle (CPU_PROFILE §22): by code review draw-cost changes no geometry and gives
  the original values (vertex constants identical; FP answers exact, drawing-thread only). Likely
  tex-slots: textures stay cached longer (sparse-checksum reuse can show stale composited images)
  or draws that got no slot now draw. Isolate with TEX_SLOTS=512 vs FP_INFO_CACHE=0 alone, then
  TEX_VERIFY=frame / TEX_POOL=0 and an RSX_CAP of the frame.

## PRIM_LOG demo capture (2026-10-24, 77c4672, PRIM_LOG=1) - perf\scratch\primlog\demo, picture perf\primlog\demo_skills.png
- Attract demo, 600 s, screenshots every 4 s 280-480 s. This session's demo: Namek fight Goku vs Madara
  (324-384 s, light-streak skill effects visible at 344/356/360 s), then Hidden Leaf/Kameari intro cards.
- **Zero `[PRIM]` lines**: no draw with POINTS, LINES, LINE_LOOP, LINE_STRIP, QUAD_STRIP or POLYGON
  reached the backend (draw_arrays or draw_indexed) in the whole run. The only primitive types in the
  [RSX] samples are 8 (QUADS, 32 lines) and 6 (TRIANGLE_FAN, 8). GCM_FLIPCOUNT in the same env worked,
  so the switch reached the game.
- So in this demo the skill effects are not lost to unsupported primitive types; missing effects (VFX.md)
  must be dropped elsewhere (shader, blend, texture, depth or a draw skipped before the backend).

## VFX: unsupported primitives ruled out (2026-10-24) — read-only, see `VFX.md`

PRIM_LOG demo (77c4672, 600 s): 0 non-triangle draws, while light-streak skill effects show on Namek
(`perf\primlog\demo_skills.png`). So missing skill effects are not point sprites, lines or quad strips. The
remaining places they can be lost: particle-server records dropped before the FIFO (fn=0 or empty batches,
the same suspects as the glow layer), a pipeline that is not ready, an invisible draw (alpha, blend, layer,
depth) in the backend, or misplaced instanced/SPU vertices. Next: one player-fight run with Goku's laser,
RSX_CAP 4 consecutive frames (before and during the beam), [PERF5], and trace-5 widened to all `pe::*` passes.

## Goku's Kamehameha: the draws arrive but the effect layers stay empty (2026-10-24, build 5a77662) - perf\scratch\laser\demo_cap70
- Attract demo, `RSX_CAP=10000:70:30` (one capture every 30 batches), PSO_ASYNC on (default), VPULL on.
  The maintainer identified f08 (frame_count 10240) as the Kamehameha start-up and f09 (10270) as the beam firing;
  the green look after f33 is underwater (normal); the yellow ground lines are the map edge.
- f09 has 791 draws vs 644 (f08), 669 (f10). Draws in f09 whose (rt, code, t0) never occur in f07/f08/f11: 53,
  mostly `rt=0x0DB30000` (640x360, blend=1, cmask=F, fp code 0x9A64BF1C, ~20 different textures, n=6..150)
  and `rt=0x0DC20000` (cmask=7, same code), plus 19 x code 0x7B938E3D into the scene target 0x0ED10000.
  Draws into 0x0DB30000: f08 34, **f09 100**, f10 27.
- But the captured layers are unchanged: **0x0DB30000 is flat grey and 0x0DC20000 black in both f08 and f09**
  (perf\laser\kame_layers.png). 0x0DB30000 is read once per frame (op786 `t0=0x0DB30000` -> 0x0DA40000).
- So the beam's draws reach the backend and produce no pixels: VFX.md cause 2/3 (ours), not cause 1.
  Next: same window with PSO_ASYNC=0 and RSX_CAP_PASSES=1 to see whether async skipping hides them.
- **PSO_ASYNC=0 run** (perf\scratch\laser\demo_async0, `RSX_CAP=10150:16:12 RSX_CAP_PASSES=1`, +2.0 GB): beam
  activity at frame_count 10210-10270 (draws into 0x0DB30000: 9-22 before, 59/105/29/64/87/52 during; into
  0x0DC20000: 1 before, up to 40 during). Per-draw pass snapshots of frame_06 (10222): every post-draw image of
  0x0DB30000 is still uniform (127,127,127) and of 0x0DC20000 uniform (0,0,0). So **async shader building is not
  the cause**: the effect draws run and change nothing.
- Notes for the reader: the 0x0DB30000 draws are `blend=1 cmask=F` after a clear to 0.498 grey (a distortion
  offset layer: grey = no offset); the 0x0DC20000 draws are `cmask=8` (alpha only) after a clear to 0, and the
  capture bmps carry no alpha (A=255 everywhere), so 0x0DC20000 changes would not show in these images.
  fp HLSL per op is in each frame_NN\fp_opNNNN.hlsl (e.g. frame_06 ops 569-646).

## Kamehameha draws: output alpha comes from vertex colour alpha, which arrives as 0 (2026-10-24) — read-only, `VFX.md`

From `perf\scratch\laser\demo_async0\cap\frame_06` shaders: the distortion draws (code 0x9A64BF1C, additive
SRC_ALPHA/ONE) and the alpha-layer draws (code 0xAD204EA4, cmask=8) both scale by `tex.a × col0.a × 2`, with t0
DXT1 (alpha 1). Layers that stay exactly flat mean col0.a = 0. Suspects: the colour attribute fetch (type,
byte order, frequency divider), the VP building the colour from per-particle constants (a0 relative
addressing), and depth culling against the rebuilt half-res depth. Next: one laser run with
`VP_ATTRS=1 RSX_DBG_VP=04EEE600`, and optionally `DEPTH_OFF=1`.
- **2026-10-24 runs** (build 5a77662): `VP_ATTRS=1 RSX_DBG_VP=04EEE600 / 04732D80 / 04FC4600` (perf\scratch\laser\
  vpattr2_*): no [DUCKVP] in any run (the beam's texture addresses change between runs) and only 4 early
  [VPATTR] masks, none from the beam. `DEPTH_OFF=1 RSX_CAP=10200:8:12` (perf\scratch\laser\depthoff, 260 MB):
  24-53 draws per frame into 0x0DB30000 and 4-12 into 0x0DC20000, layers still uniform in all 8 frames, so
  depth is not what hides them. Next: `VP_CODE=9A64BF1C,AD204EA4` (vp-code patch, a5420e0).
- **VP_CODE=9A64BF1C,AD204EA4 VP_CODE_N=40** (a5420e0, perf\scratch\laser\vpcode; demo with X at 40 s): 40 draws
  logged, all code 9A64BF1C, all `draw_arrays prim=8 (QUADS) count=8`, layout a0 pos (float3), a3 COLOR0
  (ubyte4, type 4), a8 uv (float2), stride 24, main memory 0x8313xxxx-0x8314xxxx. **Every vertex byte of every
  logged draw is zero** (position, colour and uv of v0/v1; 0 of 240 rows non-zero). The logger reads through
  the same cellGcmResolveLocated path as the real fetch (line ~9643). The VP copies COLOR0 straight from v[3]
  (vp_code_9A64BF1C_1.hlsl, 6 instructions), so alpha = 0 and the quad is degenerate anyway.
- So the particle vertex buffers are still empty when we draw them: the producer (probably an SPU particle job,
  or PPU code writing after the FIFO is consumed) has not written them. This is VFX.md cause 1/3 territory
  (game-side data missing in our run), not shaders, blend, depth or async.

## Beam vertex buffers empty at draw: the FIFO does not wait on semaphore ACQUIRE (2026-10-24) — read-only, `VFX.md`

VP_CODE: every byte of the Kamehameha's particle quads (main memory, IO 0x0314xxxx) is zero at draw time. In
`cellGcmSys.c`, NV406E ACQUIRE (0x68) only blocks with `GCM_SEMA_ACQUIRE=1` (off by default), so the walker
runs past `cellGcmSetWaitLabel` and draws particle buffers before the SPU job fills them (inferred). Proposed
test: laser demo with `GCM_SEMA_ACQUIRE=1 GCM_RECDBG=1 VP_CODE=9A64BF1C VP_CODE_N=40`.
- **GCM_SEMA_ACQUIRE test** (2026-10-24, perf\scratch\laser\sema, sema2, nosema2, ~300 MB; demo with X at 40 s,
  `GCM_RECDBG=1 VP_CODE=9A64BF1C`): ACQUIRE=1 runs did not hang (29.2 real f/s). VP_CODE_N=3000 over the whole
  fight: ACQUIRE=1 17,992 byte rows, 32 with any non-zero byte; ACQUIRE=0 17,998 rows, 16 non-zero (the few
  non-zero ones are other 9A64BF1C quads with +-1.0 corners, not the beam). No new effects on screen (the maintainer: the
  blue in sema t244/t256 is a Madara effect that always showed). Only 12 [SEMA] lines (0x64 release v=0x10,
  0x68 acquire v=0, off 0x10). => the AI assistant case (c): still zero, no hang -> address mapping or producer.
- Separate: with the 40 s skip, 600 s demo runs reach the 2nd demo fight, where the game sometimes stops
  flipping (last flip at 540 / 576 / 584 s in sema / vpcode / nosema2, 595 s in sema2); the maintainer saw that fight freeze.
- **MEMPEEK/VRAMSCAN** (2026-10-24, perf\scratch\laser\mempeek_48A47130 and mempeek_48445820, 76 MB;
  `VRAMSCAN_BASE=0x48000000 VRAMSCAN_FRAMES=MEMPEEK_FRAMES=6800:8300 VP_CODE_N=200`): VRAMSCAN: ~60 of 512 blocks
  change every frame; the blocks holding the beam arrays change often (+0x430000..+0x450000 in 25-30% of frames,
  +0xA30000..+0xA50000 in 16-23%). MEMPEEK: 0x48445820 non-zero in ~95% of window frames, sometimes
  vertex-like floats (1289.0, 1.0, 386.2, 597.4 / colour 0x000000FF), later pointer-like words (0x4844xxxx,
  0x4840xxxx) and occasionally all zero; 0x48A47130 similar (non-zero ~96%, mostly pointer-like). But all 200
  VP_CODE draws happened before frame 6800 (pre-fight, e.g. a0=0x83445820 read as zeros), so no draw and peek
  of the same frame and address exists: inconclusive between "timing" and "wrong address". The arrays look
  like a reused heap (contents change type between frames).
- **VP_CODE_LATER** (5d799a1, 2026-10-24, perf\scratch\laser\later0 / later2, 72 MB; `VP_CODE=9A64BF1C VP_CODE_N=300
  VP_CODE_FROM=6800`): 300 beam-time draws (frame_count 7239-7259 / 7298-7320). At draw time all 1,800 byte rows
  are zero in both runs. LATER=0 (end of the same frame): 300/300 still zero. LATER=2 (two frames later): 165/300
  non-zero, but the contents are not vertex data: pointer-like words 0x4875xxxx / 0x48A5xxxx / 0x489Fxxxx, small
  integers (4, 1, 3, 0x0702, 0x0100), FF FF FF FF, the odd 1.0 or 640.0 / 360.0 - i.e. heap structures.
  => "zero at draw, junk later": the arrays we read are freshly zeroed heap that is reused for other objects
  afterwards; the beam's vertices are never written at those addresses in our run (wrong address or the
  producer writes elsewhere / not at all), not a late write.

## Beam vertices never land at the fetched EAs (2026-10-24) — read-only, `VFX.md` (15:10)

VP_CODE_LATER (5d799a1): zero at draw and at the end of the frame, and reused heap junk two frames later. No IO
remaps after init (linear map). Remaining: written in the 1-frame gap (`VP_CODE_LATER=1`), a wrong array
offset or base, or a producer that never runs. Next: `VP_CODE_LATER=1`, and `SPU_DMA_RANGE=48300000-48D00000`
to see whether an SPU job PUTs particle vertices into that region (EAs compared with `[VPCODE] ea=`).
- **SPU_DMA_RANGE=48300000-48D00000 + VP_CODE_LATER=1** (2026-10-24, perf\scratch\laser\dmarange): the 64 logged
  transfers are all early in the run (before the fight): GETs (cmd 0x40) of 32 bytes from pc 0x410/0x4A0 at
  0x4840xxxx / 0x4870xxxx / 0x48A0xxxx (lsa 0x0A320/0x0A340) and PUTs (cmd 0x20) of 16 bytes from pc 0x628 to
  0x483DFAx0 / 0x483BFAx0 (lsa 0x01C00); no ~192-byte PUTs among them. Rates (put/get per 5 s, printed per
  thread): before the beam ~11.3 MB/s put / 0.55 MB/s get, **during the beam window ~1.35 MB/s put / 0.07 MB/s
  get**, after ~10.6 MB/s put. (The logging slows the game during the window: flips 6620 -> 6630 in 2.5 s, which
  lowers all rates.) LATER=1: 176/300 draws hold heap-like junk one frame later (pointers 0x48A5xxxx, 1.0, ...),
  the rest still zero; draw-time bytes all zero. Per-transfer lines during the beam would need a later start
  for the 64-entry log.
- **SPU_DMA_RANGE_AFTER** (66d1e1e, 2026-10-24): run 1 (dmaafter, whole 48300000-48D00000, AFTER=218, N=400)
  filled its 400 lines in 0.2 s before the beam (pc 0x1568 / 0x628 PUTs at 0x482FE3E0-0x483E0290). Run 2
  (dmaafter2, range narrowed to **48440000-48A60000, AFTER=200, N=3000, PUT=1**): 3,007 PUTs, t=200.0-229.6 s,
  **all from one SPU program pc 0x02EC0, lsa 0x06400, sizes multiples of 96 bytes** (96 x1287, 288, 192, 384,
  576, ... 2112) = 4 vertices x 24-byte stride, i.e. whole particle quads; EAs 0x484475C0-0x48A5EA80.
  **All 300 beam vertex EAs from [VPCODE] are covered by these PUTs**, each by PUTs logged both before and after
  its draw (the buffers are reused); the first covering PUT after a draw is 12-164 log lines later, mostly
  within the same 10-flip window. => The SPU particle job does write the beam's vertices at the addresses we
  read, but (for this frame) after we have drawn: a **timing/synchronisation problem**. GCM_SEMA_ACQUIRE=1 did
  not fix it, so the game's wait is something else (label wait, JTS/jump patching, or a reference wait).

## Beam: right data, right address, drawn too early (2026-10-24) — `VFX.md` (15:50)

dmaafter2: the SPU job (pc 0x02EC0) PUTs every beam vertex EA after the draw. ACQUIRE is not the guard. Most
likely the game relies on the real RSX lagging behind the PPU, which our walker doesn't. Fix candidate A:
hold the FIFO drain one flip behind with a timeout (opt-in `GCM_FIFO_LAG`). First a read-only clock check,
`patches\beam-clock` (`ms=`/`flip=` on [VPCODE] and [dma-range]).
- **beam-clock** (3573ab4, ms=/flip= on [VPCODE] and [dma-range]; perf\scratch\laser\dmaclock, same switches as
  dmaafter2, VP_CODE_N=50): 3,005 PUTs (pc 0x02EC0), 50 beam draws at flips 6573-6582. For **every one of the 50
  draws the last covering SPU PUT came exactly 2 flips before the draw** (and the vertex bytes are zero at
  draw time); the next covering PUT after the draw comes 1 flip later in 12/50 draws, otherwise 2-59 flips
  later (buffer reuse). => We are not drawing too early: our RSX side consumes the particle quads two flips
  after the SPU wrote them, and by then the game has already cleared/recycled the buffer (it expects the GPU
  to have read it within one frame). The fix is on the latency side (draw the FIFO sooner / at most one frame
  behind), not a longer wait. (Corrects the "after we draw" reading of dmaafter2.)

## Beam drawn two flips late: flips complete on the 60 Hz tick, not at the walker (2026-10-24) — `VFX.md` (16:10)

dmaclock: the SPU wrote every beam quad 2 flips before our draw, and the buffers were recycled by then.
`cellGcm_request_tick` marks the flip done on every tick, regardless of FIFO progress, so the guest runs ahead.
Quick test: `GCM_FLIP_NEEDS_FIFO=1` (existing). Fix drafted: `patches\flip-at-word\` (`GCM_FLIP_AT_WORD=1`,
done only once the walker decodes the flip marker, with a 100 ms safety release).

## Beam: not a flip lag; something zeroes the quads between PUT and draw (2026-10-24) — `VFX.md` (16:40)

GCM_FLIP_NEEDS_FIFO and GCM_FLIP_AT_WORD changed nothing (the gate never held). [VPCODE] reads at FIFO decode,
so there is no replay lag in these numbers. The game draws in frame F quads written in F-2, and they are zero
by F. SPU zero-fills (SDCRZ) bypass the SPU_DMA_RANGE log. Next: `patches\sdcrz-log` (`SPU_SDCRZ_RANGE`).
- **Flip-latency fixes (2026-10-24, perf\scratch\flipfix)**: `GCM_FLIP_NEEDS_FIFO=1` (existing) and `GCM_FLIP_AT_WORD=1`
  (7cbe7e7, opt-in, default off): beam vertex bytes still all zero (300/300 rows each), no beam. AT_WORD never
  logged a "walker owed" release in any run, and real f/s was unchanged on stage 0 (29.4 vs 28.2), Hidden Leaf
  (29.0 vs 29.2), Alabasta (29.6 vs 27.8) - so the walker is not behind on flip markers; the
  2-flip lag is elsewhere (decode -> backend vertex fetch). Both demo runs still stop flipping before 600 s
  (2nd-fight freeze unchanged). AT_WORD stays off.
- **sdcrz-log** (3ab2a5b, 2026-10-24, perf\scratch\laser\sdcrz and sdcrz_novpull, 137 MB): with
  `SPU_SDCRZ_RANGE=48440000-48A60000 SPU_SDCRZ_AFTER=200` there were **zero [sdcrz] lines** in either run: no
  SPU zero-fill touches the beam arrays during the fight. Lag check on the same runs: last covering SPU PUT
  2 flips before the draw in 47/50 (1 flip in 3/50) with defaults, and 50/50 with `VPULL=0 VFETCH_ASYNC=0`
  (so neither VPULL nor async vertex fetch causes the lag); beam vertex bytes zero in both (300/300 rows).
  VPULL=0 VFETCH_ASYNC=0 runs slower (22.8 real f/s, 11 slow 10-flip groups). => The buffers are cleared by
  PPU-side code or by our emulation, not by an SPU zero-fill; a PPU write watch is the next step.
- **fifo-queued** (c80d0f1, 2026-10-24, perf\scratch\laser\fifoqueued): at all 50 beam draws `queued_flips=0`
  (queued_bytes avg 0.56 MB, max 1.6 MB, i.e. less than a frame of commands). Our walker is not behind the guest:
  the game itself puts quads into the FIFO that its SPU job filled two flips earlier, and they are already
  zero when the RSX side reads them. So the clearing happens on the PPU side (or in our emulation of it)
  between the SPU write and the draw; next step is a PPU write watch on the beam arrays (re-lift or page guard).
- **beam-guard** (b9e2b2d, 2026-10-24, perf\scratch\laser\guard, 40 MB; `VP_CODE_FROM=7200 VP_CODE_GUARD=1
  PPU_GUARD_MAX=20000` + dmaclock switches): page of 0x48A553B0 guarded at the first beam draw (flip 6576);
  20,014 [GUARD] lines. Writers: (1) the SPU DMA path (guest-fn 0x00734310, worker tids, lr=0), 1,781 writes in
  32-byte chunks at the flips of the [dma-range] PUTs (slot 0x48A553B0 at flips 6578, 6580, 6584 = PUTs of 96 B);
  (2) the PPU main thread (tid 44880) through heap/list code: 0x0063A1AC (lr 0x0063C1C0 / 0x0063B1C0 /
  0x0063BA90), 0x00594E5C (memset/memcpy-like, lr 0x005280D4 / 0x00528090 / 0x00525AD0 / 0x003B2BC0),
  0x00529AF4, 0x003B27CC, 0x00637718, 0x0055D46C, 0x00702570 - i.e. the page is general heap that the PPU
  reuses every few flips (CLEARED by 0x00594E5C at flip 6602, call chain func_00526D80 <- func_00528028 <-
  func_00526374 <- func_00304A20 ...).
- **Key point: every one of the 1,781 SPU writes left zero behind** (the next write to the same word, by anyone,
  always saw old=0). So the SPU particle job (pc 0x02EC0) DMAs **zero-filled quads**: the vertices are empty when
  they leave the SPU, not cleared afterwards. The producer is our SPU job (its inputs or its execution), not PPU
  timing; the 2-flip "lag" was just heap reuse after the empty write.

## Beam: the SPU particle job (image 17, spujob_857F80) PUTs zero quads (2026-10-24) — `VFX.md` (18:35)

beam-guard: every SPU write to the guarded beam page is zero. pc 0x2EC0 in job 0x857F80 is its generic PUT
routine (LSA 0x6400). Next: `patches\dmatrace-after` (`SPU_DMATRACE=17 SPU_DMATRACE_AFTER=~220
SPU_DMATRACE_N=3000`) to see whether its inputs are empty or its output computation fails.
- **dmatrace-after** (9c4cb08, 2026-10-24, perf\scratch\laser\dmatrace; `SPU_DMATRACE=17 SPU_DMATRACE_AFTER=220
  SPU_DMATRACE_N=3000`): image 17 is the beam's job. From flip 6604 on: GETs (cmd 0x40) of 0x130 = 304 bytes from
  pc 0x009E0 (885), 0x00FB8 (875), 0x00760 (410), 0x00F18 (405), EAs 0x4924xxxx; **inputs are non-zero** (typically
  25-41 of 304 bytes non-zero; 0 all-zero GETs). PUTs (cmd 0x20) from pc 0x02EC0, 0x60-0x5A0 bytes (96-1440):
  **399 PUTs, 0 with any non-zero byte** (one 288-B PUT had a non-zero sample). => The job receives data but
  outputs zeros: the lifted SPU image 17's computation is failing (branch, float or channel issue). Next: an SPU
  step trace or LS dump of image 17 between the GETs and pc 0x02EC0.
- **spu-lsdump** (b8958b6, 2026-10-24): `SPU_DMATRACE=17 SPU_DMATRACE_AFTER=220 SPU_DMATRACE_N=600
  SPU_DMATRACE_LSDUMP=4` -> perf\scratch\laser\lsdump (9.2 MB with stderr/stdout): spu17_ls_1..4.bin (256 KB each)
  and spu17_regs_1/3/4.txt. Note: the log says "LS dump 3 written" twice and no "dump 2", and spu17_regs_2.txt
  is missing: two SPU threads raced on the dump counter, so ls_2/ls_3 may come from different threads and
  regs_3 may belong to either. Numbering starts at 1, not 0.
- Beam LS dumps: image 17 writes its quads (r80 = 0x6400 + PUT size) but all values are zero; inputs are
  non-zero. Drafted `patches\interp-img` (SPU_INTERP_IMG=<id>) to run the job interpreted and split lift bug
  from input problem.
- Particle records (0x49245840..) have no SPU writer; the PPU fills them and leaves +0x00..+0x3F (likely a
  4x4 matrix) zero. Next run: PPU_GUARD_EA=49245840 to name the PPU writers.
- **SPU_INTERP_IMG=17** (a7fef41 + build fix 89df764, 2026-10-24, perf\scratch\laser\interp): image 17's jobs ran on
  the interpreter ("[spurs-job] img=17 job 0x4B71E680: interpreted"), and **all 72 traced PUTs from pc 0x02EC0 were
  still all-zero**. So the lifted code is not the cause: the job computes zeros from its inputs. LS dump records
  (0xAA30/0xAB60) have their first 0x40 bytes zero (the AI assistant: likely the 4x4 emitter/particle world matrix);
  recwriters (SPU_DMA_RANGE=49200000-49300000 PUT): SPU PUTs only at 0x492E7xxx-0x492FCxxx (pc 0x3150, 0x26A8,
  0x3740, 0x37C0), none at the 0x49245xxx records, so the PPU writes the records. Next: PPU_GUARD_EA on a record.
- **recguard** (2026-10-24, perf\scratch\laser\recguard): `PPU_GUARD_EA=49245840` armed at process start and logged
  no writes in 600 s, though the records there are non-zero later; the boot-time guard is probably lost when the
  game maps/allocates that memory. Records this run: 0x49246A10 ... 0x492474C0 (304-B stride). Needs a guard armed
  at the first image-17 GET instead.
- **recguard2** (PPU_GUARD_AT_DMA, 9e43000, 2026-10-24, perf\scratch\laser\recguard2): armed on record 0x49245840 at the
  first image-17 GET (flip 6614). All writers are the PPU main thread (tid 4776): 0x00594E5C (memset/memcpy-like,
  lr 0x0063A698 / 0x0063E934) and 0x0063D5A0; the 600-line WRITE cap was used up by flip 6675 on list-link words
  (+0x124 of each record). **CLEARED 0x49245840 three times: 0x3FC90FDB (+pi/2) -> 0 and 0xBFC90FDB (-pi/2) -> 0**,
  each with the call chain 0x00594E5C <- func_00704094+0x1C1 <- func_0063E840+0x804 <- func_006313AC+0x1E5A <-
  func_006317CC+0x2F4B <- func_0063276C+0x30B <- func_0039FC0C+0x73C <- func_003A013C <- func_003A15E8 <-
  func_003A2274 <- func_0039DBD0 <- func_0039C888. So the record's first word holds an angle (+-pi/2) that PPU code
  zeroes (via func_00704094) before image 17 reads it.
- **recguard3** (a32ea92: PPU_GUARD_OFF_LO/_HI/_STRIDE filter + stored value + setter stacks; perf\scratch\laser\recguard3):
  `PPU_GUARD_AT_DMA=1 SPU_DMATRACE=17 PPU_GUARD_OFF_LO=0 PPU_GUARD_OFF_HI=40 PPU_GUARD_MAX=2000`. Armed on the first
  image-17 GET (0x4920FEE0, flip 2029); writes in that page only from flip 6646 to 6964 (the beam fight), all PPU
  (tid 13944). Writers of record offsets 0x00-0x3F (offset taken mod 0x130 from the armed ea; other per-particle
  blocks share the page): memset 0x00594E5C from lr 0x0063E91C / 0x0063E934 (spawn, func_0063E840) mostly zero;
  **func_0063D5A0** (the updater) via lr 0x0063DB70 / 0x0063DAC0 storing **+-pi/2 (0x3FC90FDB / 0xBFC90FDB)** at
  +0x00/+0x10/+0x20, plus lr 0x0063DB10/0x0063DB40/0x0063DB58/0x0063DA40/0x0063DA78/0x0063D6B4 storing 1.0, 128.0,
  rotations (-0.97, 0.79 ...) and positions (25.0, 100.0, 75.0 ...); func_0063E19C (lr 0x006314B4) storing
  -1.0, 0.025, 0.0565, -4.07, 6.06, 29.1; func_0063D050 (lr 0x0063DB10/0x0063DBAC). 854 of the 2000 logged writes
  stored non-zero values. Setter stacks (first 6): func_0063E840+0x24C2/+0x3BB4/+0x3BC7/+0x3D55/+0x3D68 <-
  func_006313AC <- func_006317CC <- func_0063276C <- func_0039FC0C ... So the PPU does write angles/matrices into
  these per-particle blocks during the fight; whether they land in the 304-B record image 17 reads (vs. a sibling
  block) is the next question.
- Image 17 renders a record only if record+0x121 & 2; all beam records have it clear (interp run also zero, so not a job-lift bug). Test patch patches\rec-flag.
- **Record flag +0x121 bit 0x02** (the AI assistant read of image 17: clear -> particle gets size 0, colour 0, default
  basis = the zero quads). **SPU_FORCE_REC_FLAG=17** (2cd9b46, ORs 0x02 into LS +0x121 after each 0x130 GET;
  perf\scratch\laser\recflag): image-17 PUTs become non-zero (32/32, e.g. 120/192 and 36/288 bytes) and a blue-white
  glow appears around Goku at t244 (perf\laser\recflag_frames.png). **flagguard** (PPU_GUARD_AT_DMA at a beam-time GET,
  record 0x49245AA0 flip 6610, PPU_GUARD_OFF_LO=0x120 _HI=0x124, MAX 2000; perf\scratch\laser\flagguard): 784 writes to
  record words +0x120 from flip 6768 on, **every one storing 0**: memset 0x00594E5C from lr 0x0063E934 (the spawn,
  func_0063E840) 758x and from lr 0x0062534C 26x. Nothing ever stores a non-zero +0x120..+0x123 during the fight,
  so the code that should set the flag never runs (or writes another copy).
- Arena run: at beam time only image 17 GETs (304 B) touch the record arena 0x49200000-0x49300000; no other image, no PUTs. Records are PPU-only.
- **arena** (img= on [dma-range], 1814b63): over 0x49200000-0x49300000 from t=220 s the 3000 lines (t 220-228.75,
  flips ~6620-6634) are all image-17 GETs of 304 B; no other image and no PUT touched the record arena.
- **flagls** (SPU_FORCE_REC_FLAG=17 + VP_CODE N=30 FROM 7200, perf\scratch\laser\flagls): draw-time beam vertex bytes are
  now non-zero (156/180 rows), image-17 PUTs non-zero (36/36). But each quad's corners are identical (v0 == v1:
  e.g. pos (-0.262, 0.083, -0.255), colour bytes 80 76 66 00; (-0.007, 0.002, -0.007); (0.465, -0.340, 0.583)), i.e.
  zero-size quads at small, direction-like positions near the origin, not near the emitter (~(-28, 7, -28)). No LS
  dump (LSDUMP fires only on all-zero PUTs). Screenshot at the draw time (flip 6628 ~ t242): perf\laser\flagls_t242.png.
- **b8log / d100log** (logging in recompiled func_0063E19C, ppu_recomp_033.cpp, backup .pre-b8log; B8LOG_FLIP=6600):
  *(B8+0x74) = 3 in all 40 calls (bit 0x08 clear). Around func_0063D100 (lr 0x63E49C), 40 calls at flips 6613-6614:
  pre B8+0x58 byte = 0x01 every time, post 0x00 (D100 clears it); r8 = 0x01; curve defs r6 = 0x409BAF10 (30x),
  0x409BB5C0, 0x409BBBB0, 0x409BC200, 0x409BC7F0 (5x), all with first 32 bytes = 00000000 3C23D70A (0.01) then zeros.
  B4+0x100..+0x117 pre = (0, 0, 0, 1.0, 0, 1.0); post unchanged in 35/40, and in 5/40 (curve 0x409BC7F0) B4+0x100 =
  1.0 and B0+0xC8 = 1.0 afterwards. So D100 runs (not the early exit) and mostly produces 0 from a curve whose
  values are 0 apart from 0.01.
- **g110** (PPU_GUARD_AT_DMA at a beam GET, record 0x49245D00 flip 6615, OFF 0x110..0x114, MAX 2000): 1,429 writes to
  record +0x110 from flip 6684 to 15642, **all storing 0** (memset 0x00594E5C from lr 0x0063E934 1,416x and lr
  0x0062534C 13x). Nothing ever writes a non-zero size/scale into +0x110.
- **tscale2** ([TSCALE] at entry of func_0063E19C / func_006313AC, flips 6611-6613, TOC r2 = 0x008E3858): f1 = 1.0,
  f31 = 1.0; P = *(r2-0x5034) = 0x0096CF90, Q = *P = 0x4903B270, *(Q+4) = 30.0 (0x41F00000), *(Q+8) = 0.0333
  (0x3D088889) in all 24 calls. So the global time scale (30 fps / 1/30 s) is set normally, not 0.
- **track** ([TRACK]: one particle, obj 0x490FBCB0 / record B4 0x49245840 / state B8+0x58 0x4929D458, followed from flip
  6609; perf\scratch\laser\track): the particle **lives for many frames** (life obj+0x14 23.8 -> 22.3 -> 20.8 ... ->
  -0 at flip 6925, then respawned at flips 6748/6802/6873 per [TRACK] SPAWN, life 108.5 after), it is not respawned
  every frame. D100 runs every frame: state goes 01000000 -> 02000100 (bytes 0x58=0x02, 0x5A=0x01), time +0x5C
  advances 0, 0.05, 0.10, 0.15 ... 0.70 (+0.05/frame), rate +0x60 = 0x3D088889 (= 1/30 = *(Q+8)); later 00000000 /
  0 / 1.0. **But record B4+0x100/+0x104 stays 0 in every call**: D100 advances its own state yet never copies the
  value into B4+0x100. Particle flags obj+0x0C = 9.
- **Pickup (2026-10-25, parked "needs RPCS3 ground truth")**: the beam job (SPU image 17, job 857F80) draws nothing
  because the 304-B particle records it reads have (1) record +0x121 bit 0x02 clear (the job then takes path 0x1080:
  size 0, colour 0, default basis) - no code in the EBOOT ever sets it, only spawn/pool memsets write +0x120; and (2)
  with the flag forced (SPU_FORCE_REC_FLAG=17, opt-in), every quad is still degenerate because the (progress, max)
  pairs the flag path multiplies by stay (0, 1.0): +0x108/+0x10C, +0x110/+0x114 and record +0x60/+0x64; nothing writes a
  non-zero +0x110 (g110), D100 advances its own state (time +0.05/frame, rate 1/30) but only copies into +0x100 on
  key changes. Global time scale is normal (T = 30). Next: read a beam record in RPCS3 during the Kamehameha (record
  +0x60/+0x64, +0x100..+0x11F, +0x120) to see what the real values are. Diagnostics are all opt-in; recompiled-code
  logging is in jstars\port\recompiled\ppu_recomp_033.cpp (B8LOG_FLIP: [B8LOG]/[D100]/[TSCALE]/[TRACK]), backup
  ppu_recomp_033.cpp.pre-b8log.
- **Image 1 checked and ruled out (2026-10-25)**: SPU_DMATRACE=1 at beam time (perf\scratch\laser\img1): image 1 only GETs
  (485 lines) plus 8 rejected size-0 PUTs (pc 0x3150, ea 0x49F8D240/0x49F8D290/0x4A205970). SPURS_JOB_LSDUMP_IMG=1
  (new, 8264d47/9b38605; perf\scratch\laser\img1dump): the AI assistant identified image 1 (job 7E7E00) as the
  skeletal-animation sampler (2 items x 18 bones, frames 290.0/260.0), so its size-0 PUTs are empty animation
  outputs, unrelated to the beam. Note: spu_run_spurs_job never copies a job's output buffer back to guest memory;
  image 1 does its own output DMA, so that is fine for it.
- **Image 18 = particle simulation job (the AI assistant); it runs but reads from wrong addresses (2026-10-25,
  perf\scratch\laser\img18; SPURS_JOB_COUNT=1 new, b8cf6fa)**: image 16 and image 18 are dispatched equally often
  (e.g. 10,562 each by flip 7269); image 17 far less. Image 18's descriptor (job 0x4B6F8B80): dma list 0x80 from
  0x492B8B00 -> ioBuffer 0x6C00 (in-out, size_out 0x130); cache list 0x10 from **0x492B8C50** -> LS 0x9400 and 0x160 from
  0x4920FEC0 (a B0 block) -> LS 0x9800. **All 400 traced image-18 transfers are GETs from pc 0x1330 with ea = 0x00000000
  (size 0x80 = a B8 block) and ea = 0x0003FF00 (size 0x160 = a B0 block)**, no PUTs. Those two EAs are exactly the words
  in its cache buffer 0 at LS 0x9400 (`0003FF00 00000000`), i.e. guest memory at 0x492B8C50 holds 0x0003FF00 / 0 instead
  of the B0/B8 EAs it should list. So image 18 simulates garbage and never writes the records back.
- **Root cause found (2026-10-25, perf\scratch\laser\listguard; SPURS_JOB_GUARD_IMG, 6cc6695)**: guest memory is fine. The
  guard on image 18's cache-list entry 0x492B8C50 shows the PPU (func_00624484, via memcpy 0x00594E5C, lr 0x006245EC)
  writing the correct EAs every frame: +0 = 0x4920FEC0 (B0 block), +8 = 0x492B8B00 (B8 block). The junk is created
  inside our SPURS job runner: spurs_job.c places the first cache buffer right at stack_top (here LS 0x9400, scratch
  0x7400 + 0x2000) and starts the job with **sp = stack_top - 16** (spurs_job.c:380). The job's first function saves its
  link register at sp+16 (SPU ABI LR slot in the caller's frame) = LS 0x9400, overwriting the first word of cache
  buffer 0 with 0x0003FF00 (= SPU_JOB_RETURN_LS) and the next word with 0. Image 18 then GETs its B0/B8 blocks from
  EA 0x3FF00 / 0, simulates nothing and never writes the beam records. Any SPURS job with a cache list is hit the same way.
  Proposed fix: start jobs with sp = stack_top - 32 (or leave a 32-byte gap before the cache buffers), behind a switch.
- FIXED skill effects (8516777): SPURS job sp = stack_top-32; the old -16 let the saved LR overwrite the first cache-list buffer (image 18's particle pointer list), so particle records were never filled. All jobs with cache lists were affected; re-check glow layer and other SPU-driven effects.
- **FIXED (2026-10-25, 8516777, default on, SPURS_JOB_SP_GAP=0 = old)**: SPURS jobs start with sp = stack_top - 32.
  Demo (perf\scratch\spgap\fix vs old): skill effects appear for the first time - Goku's blue charge sphere and beam
  streaks (t233, t280), Ace's fire explosion (t249), sparks/slashes (t236-t239, t257); image-17 PUTs 27/27 non-zero
  without any forced flag; real f/s 28.2 (unchanged). Pictures: perf\laser\fixed_effects.png, perf\laser\fix_vs_old.png.
  Stage check with the fix (perf\scratch\spgap\stage_*, 90 s automatic fights; perf\laser\stages_fix.png): stage 0
  (Yoshiwara) 29.3, Hidden Leaf 21.1, Alabasta 29.1, Kameari 28.9 real f/s, no slow 10-flip groups apart from one on Leaf;
  pictures normal, with fire, dust, slash and hit effects now visible on all four stages.

## CPU heat on Hidden Leaf (2026-10-25) - read-only, `CPU_PROFILE.md` §25-26
- perf\heat\leaf1 (thread_cpu2): jc_thread x20 = 417% (SPU jobs + Sleep(1) guard polls, 14k
  wakes/s), frame_clock 90% (33% kernel: GCM_PUT_POLL SwitchToThread spin), two guest event-queue
  pollers 38%+21% (sys_event_queue_receive returns at once for timeouts < 1 ms), main 41%. GPU cool.
- clang-cl ignores -ffp-contract=off (needs /clang:-ffp-contract=off); with /arch:AVX2 it fuses.
- AVX2 bench (tools\bench_avx.c, bit-exact): spu_fm 2.5x, spu_fma ~1.3x, vmaddfp ~1.1x faster.
- Ranked fixes: event-queue sub-ms waits, guard events, AVX2 build (+contract off), put-poll sleep.
  ~1.5-2 of 6.5 busy cores expected (inferred). Need the maintainer's yes.
- Heat fixes drafted (patches\heat, the maintainer's yes; not run): EVQ_SUBMS_WAIT (sub-ms receive loops wait),
  SPURS_GUARD_EVENT (guards wake on notify), 3-avx2-build (port CMake: /arch:AVX2 + /clang:-ffp-contract=off),
  GCM_PUT_POLL_SLEEP (put-poll sleeps). Same warnings; 1/2/4 git-apply on 8516777, 3 patch -p1.

## Hidden Leaf below a steady 30 f/s (2026-10-25) - read-only, `CPU_PROFILE.md` §28
- The drawing thread is the limit: ~30-33 ms busy per heavy frame (drain 20-25, render_frame 9-11);
  flips on 60 Hz ticks, so a slip means 3 vblanks -> 21-25 f/s average.
- Drain samples (perf\heat\leaf1): rsx_process_method ~51%, FIFO loop ~21%, spu_coh_is_reserved 16%
  (a call per guest word read by the walker).
- Ranked: heat patches default (+1.6 f/s measured), FIFO decode fast path, render_frame on its own
  thread, remaining per-draw costs.
- FIFO_FAST drafted (patches\fifo-fast, the maintainer's yes; not run): constant words skip ~70 dispatch tests
  and the walker's per-word diagnostics. Offline identical state, 27.4 -> 17.4 ns/word. Run: TCONST_FIFO=1
  histogram once, then Hidden Leaf + demo FIFO_FAST on/off with real flips.

## Hidden Leaf after fifo-fast (2026-10-25) - read-only, `CPU_PROFILE.md` §30
- FIFO decode was a few ms at most (+0.6 f/s demo with 63% of data words fast). The drawing thread's
  remaining busy time is not split by [PERF] (drain and render_frame are single blocks; s_perf_gpu
  is measured but never printed).
- Proposed: a [PERF6] split (diagnostic, needs the maintainer's yes) and a no-code build-pdb dump run at HEAD;
  candidates ranked after that (render thread, constant/SRV reuse, prepass key skip, vertex, GPU wait).
- Fixed-character test sequence for Hidden Leaf A/B (character select by cursor, no input, 3 runs).

## Vertex helper hand-off costs ~4 us per job (2026-10-25) - `CPU_PROFILE.md` §33
- drawcost2 was -0.5 ms at 2000 draws (the AI assistant). Fit: ~5.7 ms fixed + ~10.8 us/draw; 3000 draws needs ~5 ms less.
- The drawing thread is ~25% kernel time; exe-only fixes miss it. [PERF2] async fetch wait is 0, so §31's vfa_sync line is likely mis-symbolized.
- Each queued vertex job wakes a helper with ReleaseSemaphore: 3.7-4.3 us of drawing-thread time per job on this PC (bench), ~512 jobs per batch.
- Drafted patches\vfa-wake: VFA_WAKE (on), VFA_SPIN, VFA_INLINE (off), [VFA] counters; queue test 0 errors. Free test now: VFETCH_ASYNC=0.
- Paused 2026-10-25: waiting on the maintainer's yes for vfa-wake and the AI assistant's VFETCH_ASYNC=0 test; next steps at the end of CPU_PROFILE.md §33.

## Measure first: drawing-thread stack sampler (2026-10-26) - `CPU_PROFILE.md` §35
- vfa-wake neutral like drawcost2; next fixes will be sized from in-game measurements. Drafted patches\draw-sample (DRAW_SAMPLE=1, diagnostic): charges ntdll/driver time to the exe caller. Offline check OK; waits under-counted ~2x.
- 2026-10-26: drafted patches\frame-tl (FRAME_TL timeline + GAME_SAMPLE on the game main thread), the maintainer approved; for the AI assistant to run on Hidden Leaf.

## The 60 Hz tick runs late (2026-10-26) - `CPU_PROFILE.md` §39
- FRAME_TL: request -> first tick averaged 20-23 ms (max on time 16.7). The drawing thread fires the tick only between drains, so the game's vblank wait (and its next flush) ends late; its build is only ~1 ms. Drafted patches\vblank-thread (VBLANK_THREAD=1, tick on its own thread), waiting for the maintainer.

## PERF=1 overstated the drawing cost (2026-10-26) - `CPU_PROFILE.md` §36, §42
- The per-draw vertex timer (two QueryThreadCycleTime syscalls + QPC per draw) was ~20% of the drawing thread's samples. Drafted perf-cheap.patch (cycles only with PERF_CYCLES=1) and quiet-logs.patch (draw printf, EBUSY line). Judge 30 f/s with real flips and no PERF.

## Opening movie plays (2026-10-26, the AI assistant)
- Build: PS3RECOMP_FFMPEG_RUNTIME=ON with FFmpeg 9.0.1 headers (third_party\ffmpeg-9.0.1, headers only;
  hand-written libavutil\avconfig.h), matching RPCS3's avcodec-63 / avutil-61 / swresample-7 DLLs, now
  copied next to JStarsRecomp.exe in jstars\port\build. CMakeCache backup: CMakeCache.txt.pre-ffmpeg.
- e02020e: a full-path PS3RECOMP_FFMPEG_AVCODEC is loaded with LOAD_WITH_ALTERED_SEARCH_PATH (deps found).
- 5c7b81e VDEC_DEFER (default on, =0 old): decoding is synchronous, so a BUSY from DecodeAu was never
  followed by an AUDONE and the movie feeder waited forever (stopped after 25-48 of 2,946 AUs). AUs that
  arrive while 8 pictures wait are kept (up to 16) and decoded when GetPicture makes room.
- The game asks GetPicture for ARGB (type 0, matrix 1 = BT.709) with alpha 0; drawn that way the frames
  are invisible (black). VDEC_ALPHA=<n> (cellVdec, diagnostic) overrides; jstars\port\main.cpp sets
  VDEC_ALPHA=255 unless set (backup main.cpp.pre-movie-alpha; VDEC_ALPHA=-1 keeps the game's 0).
- Result (perf\movie\try10, no env): the movie plays from ~45 s to ~125 s, then the J-Stars logo and title.
- Diagnostics added: VDEC_DBG=<n>, VDEC_STEP=1, VDEC_LOW_DELAY=1 (cellVdec / vdec_ffmpeg).

## Audio: silent (2026-10-27, the AI assistant; perf\audio\a1-a4)
- cellAudio side works: 1 port (8 ch, 8 blocks) at guest 0x58000000, read index u64 at 0x58100000;
  the WASAPI mixer runs at real time (187.5 blocks/s, 0 dropped) but 100% of blocks are silent through
  the movie and title (AUDIO_RATE / AUDIO_PEAK / AUDIO_WAV). AUDIO_PEAK ring map stays "........": nothing
  ever writes the port buffer.
- No SPU DMA touches 0x58000000-0x58200000 (SPU_DMA_RANGE). cellAudioAddData (imported) is never called;
  the game never sets a notify event queue (not imported). GetPortConfig is called ~24 times.
- CRI threads: "CriThread" (tid 16) sits in sys_lwcond_wait the whole time (3 dumps); "CRI Server Manager"
  (tid 17) loops sys_timer_usleep(16000 / 17000 us) at ~60 Hz (PS3_SCTRACE) and makes no other syscall,
  i.e. it never wakes CriThread. Callsite func_006ABEE8+0x182F0 <- func_004D518C+0x5B.
- Unresolved NIDs seen once each: cellSysutilGetBgmPlaybackStatus2 (0x6CFD856F),
  _sys_spu_printf_initialize, cellSpursAttributeEnableSystemWorkload, cellSpursInitializeWithAttribute2,
  cellSaveDataEnableOverlay. GetBgmPlaybackStatus2 leaves its out-struct untouched (inferred: may read as
  "user BGM playing", which mutes game music in many titles).
- jstars\port\stubs.cpp now answers cellSysutilGetBgmPlaybackStatus2 with "stopped" (backup
  stubs.cpp.pre-bgmstatus). Called once; audio still 100% silent (perf\audio\a5), so not the cause.
- Next: why CRI Server Manager (tid 17) never signals CriThread (tid 16): read func_006ABEE8 /
  func_004D518C (the server loop) for the condition it checks each 16 ms tick.
- Correction (HLE_BT_EVERY=200, perf\audio\a6): tid 17 is not idle. Between its 16 ms sleeps it runs the
  CRI Atom/ASR server (~260k HLE calls in 70 s; chain func_004D52BC -> func_004D429C -> func_004D4D10 ->
  func_00670C30 -> func_00659E74 -> ... -> func_006CCFA0 -> func_006C99BC -> func_006F1998). The ASR voice
  SPU job (image 3) PUTs to 0x40002820 (0x800 bytes) ~200x/s. GetPortConfig's log is capped at 24, so the
  game may poll it continuously. Still nothing reaches the port ring at 0x58000000.
- Probe in jstars\port\recompiled\ppu_recomp_035.cpp func_006A5BA4 (AUDIO_CRI_DBG=<n>; backup
  ppu_recomp_035.cpp.pre-audiodbg): CRI's cellAudio output driver DOES run: GetPortConfig at lr 0x006A5BD8,
  status 2, nBlock 8, lead 5, portAddr 0x58000000, W advances with R, it memcpys 0x2000 bytes per block from
  src 0x451730F0. But the source is all zero: 0 of 32,500 output calls had a nonzero word (150 s run through
  movie and title). So the port is fed, with silence: the ASR mix itself is silent. Next: the voice / decoder
  stage (AsrVoice SPU job image 3 PUTs to 0x40002820; AtomDecoder job; their inputs).
- SPURS_JOB_COUNT=1 (perf\audio\a7_stderr.txt, 130 s): img3=1734 (stops early), img4~33k, img6~35k,
  **img10 (AtomDecoder) = 5 for the whole run**, img11 (AsrVoice) ~24.6k (~200/s). The voice mixer runs, but
  almost nothing is decoded, so the ASR mix stays silent. No SPU PUT lands in 0x45170000-0x45180000.
  partition_op_sound_ps3.cpk is opened (bdvd and hdd0). The 0x40002820 buffer (img3 target) is nonzero.
- JQ_DBG=<n> in jstars\port\stubs.cpp (backup stubs.cpp.pre-jqdbg): all job-queue pushes come from tid 17;
  AsrVoice bin 0x00829A80 ~200/s, AtomDecoder bin 0x00819780 exactly 5 times (Port2CopyPushJobBody, desc
  0x0096E380, 256 bytes; header eaBinary 0x819780, sizeBinary 0x102A x16, sizeDmaList 0 (genuinely 0, so the
  "0 dma" is right), sizeOut 0x18000, sizeStack 0x80; user data p1=0x4512D3E0 @+0x30, p2=0x4B7842D0 @+0x60).
  p2 is the HCA voice context: "HCA " tag, read ptr 0x44E6D200 -> 0x44E6D50A -> 0x44E6D7B4 (advances by the
  0x30A header, then 0x2AA frames), end 0x44E75200, output ptrs +0x20/+0x24 (0x600 apart). Input is real
  HCA ("HCA\0" header, then 0xFFFF frame syncs). Outputs after the job: 0/384, 0, 0, 128/384, 0 nonzero words.
  The job does not change p1/p2 itself. After 5 decodes no more are pushed.
- Round 4 (perf\audio\a8-a10): HCA header 48434100 02000060 "fmt" 0200BB80 (2 ch, 48 kHz) frames 0x117B
  (4475 frames = ~95 s: the opening movie's audio) "comp" 02AA010F 01008080, "ciph" 0000 (not encrypted).
  The voice context p2 (0x4B7842D0) is cleared to 0094E7CC 0 0 ... within the first second and stays so: the
  voice is released after the 5 decodes. **SPU_DMATRACE=10 and =11 print no DMA at all**: neither the
  AtomDecoder nor the AsrVoice job issues a single DMA (same with SPU_INTERP_IMG=10, so not a lift bug in the
  decoder). Both jobs seem to return without doing any work. AsrVoice also hits an SMC fallback 8x:
  "[spu-smc] microstep img=11 pc=0x03FF4 word 0x28000000 ... handing to the interpreter".
- Round 5 (perf\audio\a11, SPURS_JOB_DUMP=1): the CRI AsrVoice job (desc 0x45187580) ends with stop=0x3FFF at
  pc 0x03FF8 every time = CRI JOBCRT's error exit before its main (an AI assistant: entry 0xE0 -> 0x5D0 -> check 0x660 on
  ctx+0x14 & 0x0FFFFFFF). Game jobs end normally (stop 0, pc 0x3FF00). Our job context writes ctx+0x14 =
  sizeJobDescriptor<<28 only (0x20000000 for 256 B), low 28 bits 0. Waiting on an AI assistant for what 0x660 expects.
- **FIX (8d34572, SPURS_CTX_PAD=1, opt-in for now):** ctx+0x14 low 28 bits = LS address of the job descriptor
  (0x2003FC00 for 256 B). With it AtomDecoder runs ~80/s (12,109 in 150 s), the CRI mix is nonzero in 72% of
  output blocks, and the output is real audio (lag-1 autocorrelation 0.95-0.98, L/R correlation 0.6-0.8, RMS
  ~0.08-0.12) from ~35 s (the movie) through the title (perf\audio\a12, clips clip_movie.wav 40-70 s,
  clip_title.wav 125-150 s). Hidden Leaf fight with it: sound present (9-14% silent blocks), 27.6 f/s no PERF
  (29.1 in the earlier survey; camera or decode cost, not yet compared A/B).

## Audio glitches (2026-10-27, the AI assistant; perf\audio\b1-b8)
- SPURS_CTX_PAD now default on (954fd8e, the maintainer). Hidden Leaf no-PERF with sound: 29.0-29.2 f/s (b3, b4, b6).
- Glitches = unwritten blocks: AUDIO_HOLD_UNWRITTEN=2 counted 110-128 unwritten of ~937 blocks per 5 s,
  matching 12% silent. CRI's blocks themselves always carry sound. CRI server ticks (output-driver call gaps,
  AUDIO_CRI_DBG [cri-gap]) mostly 10-18 ms but 22-50 ms several times every 2.7 s; its lead is only 5 blocks
  (26.7 ms) and it treats read == write as FULL, so a late tick costs ~4 blocks.
- No help: AUDIO_QPC_PACE=1 (f7a0afb, steady 5.33 ms reader), USLEEP_HIRES=1 (bba4d33, high-res waitable
  timer for ms usleep), PPU_PRIO_BOOST_MAX=900 (9de67ba, CRI threads above normal), AUDIO_HOLD_UNWRITTEN=1
  (1e460be + d4a3f2f, hold the read index on an unwritten block, publishing the previous block) - all opt-in.
- **Fix: CRI_LEAD=7** (jstars\port\recompiled\ppu_recomp_035.cpp, in func_006A5BA4 after GetPortConfig: writes
  CRI's lead obj+0x2008, capped at nBlock-1). Silent blocks 12% -> 0.9-2.6% (b8). Default via
  jstars\port\main.cpp (CRI_LEAD=0 keeps CRI's 5). Clip: perf\audio\clip_fight_lead7.wav.
- **Round 8 (perf\audio\b9, b10):** AUDIO_NBLOCK=16 (cellAudio, opt-in in the runtime; e547f31)
  + CRI_LEAD=12: 0.0% silent blocks in movie, title and fight (only loading screens are silent), Hidden Leaf
  29.0 f/s. Now the J-Stars default in jstars\port\main.cpp (AUDIO_NBLOCK=8 / CRI_LEAD=0 revert). Latency ~64 ms.

## Deck rename / on-screen keyboard (2026-10-27, the AI assistant; perf\menus4)
- J-Customize, Square (Edit deck name) locked up: the game sets OSK options (AddSupportLanguage,
  SetKeyLayoutOption, Ext SetInitialScale / Register*Callback - all unresolved), then cellOskDialogLoadAsync,
  which failed with NOT_INITIALIZED (no Init is imported) and sent no sysutil events.
- Fix: d94a1e8 + 71284b9 (ps3recomp-src cellOskDialog.c / cellSysutil.c): LoadAsync starts without Init; a
  Windows text box (class ps3recomp_osk, title = the game's prompt, pre-filled, limited to limit_length,
  Enter/OK, Esc/Cancel) stands in for the keyboard; LOADED / FINISHED / UNLOADED go out on the next
  cellSysutilCheckCallback. OSK_DIALOG=0 = old instant answer. jstars\port\stubs.cpp answers the 6 option
  calls with CELL_OK (backup stubs.cpp.pre-oskext). Test rename2: prompt "J-Stars Deck 01", text typed
  ("decker"), the deck shows the new name and the menu keeps working.
- 29906e8: OSK_DIALOG=2 (default) draws the text panel inside the game window instead (rsx_d3d12_backend.c
  oskp_draw: GDI-rendered 760x190 box in the menu colours, copied over the back buffer before present; the game
  window's WM_CHAR / WM_KEYDOWN feed it: Enter OK, Esc cancel, Backspace delete; cellPad's keyboard fallback is
  muted while it is up). OSK_DIALOG=1 = the separate Windows box. Test rename3: "decker" -> "the maintainer" typed in the
  game window, the deck shows "the maintainer", menus keep working (perf\menus4\ingame_rename_panel.png).

## Save windows, PS3 wording, rename panel look, frozen picture (2026-10-27, the AI assistant; perf\menus5)
- Save/load/autosave boxes are the game's own message box (same art as the shop's "Purchase this item?");
  the port draws no system UI. The maintainer: keep them, and give the rename window that look.
- 919d5e1: rename panel (OSK_DIALOG=2) redrawn like that box (dark frame, metal gradient, halftone corners,
  button strip, yellow highlight; measured from a 1280x720 frame and scaled). OK/Cancel buttons: arrows or Tab
  choose, Enter presses, Esc cancels. Test ren7: typed "PC Port", Right, Enter -> result 1 (cancel), name kept
  (perf\menus5\rename_shopbox.png).
- PS3 wording: the texts are UTF-16LE entries in the packed resident data (save/load text near 0x4016A000), unpacked
  by the decompressor job spujob_80B700 (image 3). 2408fb1 adds g_spu_put_hook_img (PUT hook with the image id);
  jstars\port\jstars_game.cpp (backup .pre-pctext) rewrites "turn off your PlayStation(R)3 system" /
  "switch off the PlayStation(R)3 system" / "remove the hard disk drive or turn off the PlayStation(R)3 system"
  to "close the game" in place, NUL-padded. 8 strings (autosave notice, creating/loading/saving, install, trophies).
  PC_TEXT=0 off, PC_TEXT_DBG=1 logs them. PS4/Vita variants are left alone.
- Frozen picture after the movie (the maintainer saw it, run ren6): FRAMELOG clear_presents went 0 -> 1 once and stayed,
  flip_skips climbed: one clear-boundary present disables the ticker present, the clear path (gated on the
  running max batch size) never presents again. Seen before in survey_noperf\s6, vblankv2\vt1, vfa\on2.
  Fix 2a20f4c: CLEAR_PRESENT=0 (runtime switch, default unchanged); jstars\port\main.cpp sets it (backup
  .pre-clearpresent). Test match8: clear_presents 0 throughout, Hidden Leaf 29.0 f/s.
- Debug helpers: PS3_DEBUG console "dumpall <file>" (all committed guest memory, [u32 ea][u32 size][bytes]
  records) and "cap" (rsx_d3d12_cap_now, 964a3b2) in jstars\port\jstars_debug.cpp (backup .pre-dumpall);
  tools\menu_seq2.sh (SEQ tokens pX press only, fN N quick shots, D dumpall); tools\osk_type_post.ps1 types into
  the game window with PostMessage (SendKeys needed focus and could reach another window).
- Options test opt1 found 1P vertical camera = Inverted at start: the 10-26 options_change tour (menus2) had
  flipped it from Standard. opt1 toggled it and back, so it is still Inverted.

## Environment shadows: atlas ruled out (2026-10-27, the AI assistant; perf\shadows_env)
- The AI assistant's patch shadow-vpxy applied as b0ed18b (opt-in ZPASS_VPXY, ZPASS_DBG logs caster placements).
- ZPASS_DBG=1 on the dojo stage (rights 0), Hidden Leaf (7) and Kameari (1), full tour + 45 s fight each: one
  placement per map only. 0x0CE40000 2048x1024 scale (1024,-512) offset (1024,512) = the whole map;
  0x0CA30000 1024x1024 (512,-512)/(512,512); 0x0D940000 640x360 (320,-180)/(320,180). Not a cascade atlas, so
  ZPASS_VPXY cannot help; step 2 skipped. Lines: perf\shadows_env\*\zpass_lines.txt.
- Flicker reproduced on Hidden Leaf with BURST=20 (~0.6 s between shots): tree shadows on the ground in shot 1,
  none in shot 2 when the camera pulls back, back in shot 3 (perf\shadows_env\flicker_example.png). Shadows vanish
  with a camera change, not randomly per frame.
- CULL_OFF=1 run (leaf_culloff) is not a fair A/B: the tour's fights differ run to run (no FIXED_CHARS in
  menu_tour_multi). Tree shadows show in most shots there too; inconclusive.
- The AI assistant's checks (Update 2/3), Hidden Leaf, BURST=30 (~0.6 s apart), perf\shadows_env\show*, kill9c2, idcolor2:
  ZPASS_SHOW=2 unit 7: ground evenly lit, no silhouettes. ZPASS_SHOW=2 unit 8: leaf dapples gone b00-b03,
  present b04-b13 with a static camera. ZPASS_SHOW=5 unit 7: looks like a normal render (no grey floor).
  FP_KILL was broken (anchor "PSOut _po;" gone from the decompiler): fixed in ede600e. FP_KILL=9CA80C1: no leaf
  dapples in any of 16 frames at the same gate view where they show otherwise, so the environment shadows come
  from fp 0x9CA80C1 (reads units 7/8/15 as A8R8G8B8 bitcasts; an AI assistant: a screen-space shadow pass).
  FP_IDCOLOR_RT=0ED10000 paints the whole scene one flat colour (a full-screen pass is the last draw into it):
  not informative.
- ZPASS_FPLOG (3ebcc8c) printed nothing for 9CA80C1 because that program is a CASTER: ZPASS_DBG run fplog2 logs
  "caster on target 2: zstate=0x113 (test 1 write 1 func 1) cull=0x3 fp=0x9CA80C1 vp=0,0 2048x1024 verts=25".
  It draws depth-only into the 2048x1024 scenery map (likely the alpha-tested foliage caster); the hook only
  looks at colour draws. Leaf dapples blink -> look at the caster side (alpha test in depth-only PSOs, its
  texture, zn_skip when the PSO is missing).
- Leaf shadows = the alpha-tested caster sampling a small mip (an AI assistant, SHADOWS_ENV "Update 4"). The AI assistant's patch
  shadow-lod0 applied as c1d6364 (opt-in): ZPASS_LOD0=1 casters see mip 0 only, =2 alternates every 3 s,
  TEX_LODCLAMP=1 honours a guest max LOD < 1. Run lod0ab (Hidden Leaf, ZPASS_LOD0=2, 40 shots mapped to ON/off by
  flip via the PS3_DEBUG stat poll): with a static camera, ON frames show dense sharp leaf dapples, off frames
  fainter/fewer (perf\shadows_env\lod0ab_crop.png). Leaf caster texture: fp 0x9CA80C1 unit 0 tex 0xCB84EB80
  128x128 mips=8 ctl0=0x80038000 lod=[0.00,7.00]: the guest allows all levels, so TEX_LODCLAMP alone won't fix it.
- Environment (tree) shadows blinking with camera zoom: leaf casters sampled small mips and lost alpha-test coverage; casters now sample mip 0 (patches/shadow-lod0, default on). SHADOWS_ENV.md.
- Default: jstars\port\main.cpp sets ZPASS_LOD0=1 (backup main.cpp.pre-lod0); ZPASS_LOD0=0 reverts. Check runs
  def_leaf / def_kame / def_namek (stage rights 7/1/5, BURST=10): 26.7 / 28.7 / 27.3 f/s incl. screenshot bursts
  (earlier Hidden Leaf burst runs 27.2-28.3). def_namek (rights 5): from the 10th burst shot on, the window capture
  was one solid colour (0,20,34) for 18 s while FRAMELOG kept presenting (present=1, display_draws=1); cause
  unknown; rerun penguin2 (no bursts) had no blank shots, 28.9 f/s. Clean Hidden Leaf fps (BURST=0): ZPASS_LOD0 on
  28.5 f/s vs off 28.1 f/s (fps_lod1 / fps_lod0off): no cost.
- 5896aa6: keyboard 3/4 = L3/R3 (the maintainer). Compile-checked only (GetAsyncKeyState needs real key presses).

## Higher resolution: RES_SCALE (2026-10-27, the AI assistant)
- 0114cf0: RES_SCALE=n (1-4) renders every colour target, the scene zetas and the swap chain n times the guest
  size; guest sizes stay guest sizes, viewports/scissors/copy boxes are scaled, WPOS is divided back, the
  luminance readback decimates, new targets' guest-memory contents are widened (in cached memory: widening in
  the write-combined upload heap made each target creation take seconds). Window: WINDOW_SIZE=<w>x<h> (shrunk to
  the work area), FULLSCREEN=1 / F11 borderless, 16:9 kept while resizing; the swap chain is stretched to the
  client area (a 16:10 screen in fullscreen stretches).
- First runs: 1440p 14-16 f/s, 4K 5-9 f/s, GPU-bound ([PERF6] gpu-wait 41-139 ms). [PERF7]: the game samples
  the depth it is drawing into, so dt_snapshot copies zetas ~1000 times per batch (scene zeta 0x0E960000 ~580,
  char shadow map ~330, scenery map ~70; 1 Gpx/batch already at 1x). Fixes: shadow maps (depth-only caster
  targets) stay unscaled; scaled zetas are sampled through a guest-size R32_FLOAT copy from a compute pass
  (zdown, one sample per n x n block; ZSNAP_FULL=1 = full copies). Result: 1440p 29.0 f/s, 4K 28.4 f/s (z2/z3),
  720p unchanged.
- Rejected: marking a zeta dirty only on depth-writing draws (ZSNAP rule): stale copies, the ground near the
  camera turned into a flat light-blue/teal plane (the maintainer saw it in the 4K test; 720p A/B ab_all vs ab_new).
- Player settings: "J-Stars Settings.bat" (jstars\settings.ps1) writes jstars\play_settings.txt (RES_SCALE,
  FULLSCREEN); play.ps1 (backup .pre-settings) applies it.
- c063880 (the AI assistant's fp-bem): TEXBEM/TXPBEM decompile as TEX/TXP and TXD as SampleGrad, also on gamma units;
  fp 0x0FCB3F81 (TXPBEM on gamma unit 0, the one failure in the maintainer's 720p log) no longer fails the program.
  FP_BEM=0 = old reject. Unit test : all five opcodes
  decompile with _rsx_gamma_input; FP_BEM=0 fails the three. Hidden Leaf match bem1b: 28.0 f/s, 0 decompile
  failures. 1c68eb8: window title shows the render size. 073dc29: d-pad keys (see STATUS).

## Exit Game replaces Cross-Save in Options (2026-10-27, the AI assistant)
- The maintainer: replace Cross-Save with an Exit button plus a Yes/No box. All in jstars\port\jstars_game.cpp (not in git;
  backup .pre-exitlabel is from before the label swap, .pre-pctext from before all of it). EXIT_OPTION=0 turns all of it off.
- Game state (static EBOOT data, found by diffing dumpall dumps on each screen): 0x008E63FC Options cursor counted
  from the bottom (Cross-Save = 0); 0x0096CD60 and 0x0096CCB8 are both 2 while the Options list is up; 0x0095654C is
  5 on the Options list, 9 in the Cross-Save submenu, 0 in the main menu and shop.
- ed2a7e6: cellOskDialog_confirm_open/poll (the rename panel in Yes/No mode, No highlighted), g_cellpad_filter in
  cellPadGetData, rsx_d3d12_request_close (WM_CLOSE). The pad filter swallows Cross on the entry, opens the box,
  drives it from the pad (d-pad/stick, Cross, Circle = No), then holds input until release. exit1: Yes closed the game.
- Help texts: pc_text rules "Upload or download game progress data." -> "Exit the game." and "Change game settings
  or Cross-Save." -> "Change game settings or quit."
- The label is a picture, two 512x64 DXT5 (0x88) textures in RSX memory: white letters normally (0xC4C6DC80 in cap2,
  FNV-1a 79C18A8B), black ones while highlighted (0xC4D4DC80 in cap1, FNV-1a 9ACC2F24). The other entries sit at
  0xC4C75C80 + n*0x8000. eb1c57d adds g_tex_patch_hook (start of vp_upload_tex_slot_impl); exit_label_patch
  compares block 0x18A0, then the whole-image hash, and copies exit_label_light.inc / exit_label_dark.inc over the
  guest bytes. Drawn by tools\exit_label.ps1 (Arial Black, shear -0.32, rims as measured), packed by tools\dxt5enc.py
  (needs tools\png.py). jstars\port\exit_label_dxt5.inc is an older copy of the dark one, no longer included.
- exit3: both labels read "Exit Game". Note: in exit2/exit3 the tour's down presses on the Options list did not
  always register (keyboard arrows drive the stick only since PAD_KEYS_SPLIT); exit1 reached the entry.

## Arrow keys press the d-pad outside fights (2026-10-28, the AI assistant)
- The maintainer: since PAD_KEYS_SPLIT (073dc29) the shop cart (d-pad right) needed L; wants the arrows to press the d-pad in
  menus but not while moving characters in a match.
- 2d0c8cb: cellPad g_pad_arrows_dpad (nonzero = with PAD_KEYS_SPLIT the arrows press the d-pad as well as the stick).
  jstars_game.cpp arrows_press_dpad (backup .pre-arrows): d-pad unless 0x0094DEC4 == 0x14. MENU_ARROWS_DPAD=0 off.
- 0x0094DEC4 is the game's scene step: 0/1/2 boot and title, 4 in the menus (main menu, shop, Options,
  J-Customize, character select: 14 dumps), 0xF stage select/loading, 0x10-0x13 "Press X" page and intro,
  0x14 the fight and its pause menu, back to 0xF/2/3/4 on Re-select. 0x0096E3D8 (the first pick from a 2-dump
  diff) is a busy counter, and 0x0096EC48 flickers: don't use them.
- Method: jstars_debug.cpp "save <hex> <hexlen> <file>" (backup .pre-save) sampled 0x00900000+0x80000 every
  1.5 s through a match . The scratch tour copy xs/tour_x.sh presses CROSS
  through loading because the tips page scores 28.5 against press_x (threshold 25), so menu_tour_multi.sh
  times out on "Now PRESS x" with the current build; fight_hud also sat at 25.7 for a while. Dumping on stage
  select or in the pause menu stalls the game long enough to derail the tour.
- Not tested with real key presses (I did not inject global keystrokes on the maintainer's PC); the pause menu gets stick-only arrows.

## Launcher test, key rebinds, window close ends the game (2026-10-28, the AI assistant)
- 7e3e780: the launcher's cellpad_keymap.patch (PAD_KEY_<NAME>=0xNN), applied and built as is.
- Bug found: closing the game window (X, Esc, Options Exit Game, the launcher's Exit/WM_CLOSE) only set rsx_ok=0
  in main.cpp's frame clock; the guest ran on with no window (process alive, disc mounted, launcher waiting until
  its 20 s force-stop). main.cpp (backup .pre-closeexit) now prints "[boot] game window closed, exiting" and
  TerminateProcess(0) when the pump reports the window closed. WINDOW_CLOSE_EXIT=0: old behaviour.
- Launcher (UI Automation, no global input): no console window for the game (only ps3recomp_d3d12), settings
  written with PAD_KEY_* lines, disc mounted, log fine. Exit button with the fix: "Game exited with code 0" 1 s
  after Yes, boost off, launcher back. Two starts failed "Could not mount the disc image" right after a previous
  instance/unmount (test sequencing, likely). WM_CLOSE posted from the test script did not reach the window
  (window still there; the launcher's own WM_CLOSE works) - harness issue, unexplained. Not done: rebind with
  real keys (step 5), fullscreen check; the 1920x1080 run actually got the 4K settings of a still-open instance.
- The test script's Add-Type compiles a helper DLL into %TEMP%; Norton flagged something as IDP.HELU.PSE95 and
  deleted it at the time of these runs (path not known yet).
- Retest of test 6 with the rebuilt launcher (00:37, mount retry): not run. Without Add-Type (Norton) my pwsh 7 UI
  Automation sees every launcher control as a Pane with no Invoke pattern, so it cannot press Start Game (the first
  run at 00:17 could). Windows PowerShell 5.1 would need -ExecutionPolicy Bypass, which I did not use. Left for the maintainer by hand.

## Exit Game opened Cross-Save on the maintainer's run (2026-10-28, the AI assistant)
- The maintainer's play log (keyboard) shows the labels swapped but no Exit Game line: the press-time check missed and the
  game opened its Cross-Save submenu. Cause not known (his key may have been Enter = START, which the check ignores).
- jstars_game.cpp (backup .pre-exitfallback): backup path. When 0x0095654C goes 5 -> 9 (submenu opened), the filter
  pulses Circle until it is 5 again, then opens the Yes/No box. It also logs "Cross at cursor 0 not taken (...)"
  with the check's values. EXIT_OPTION_FALLBACK_ONLY=1 skips the press-time check (for testing).
- exitfb (fallback only): submenu backed out, box shown (exitfb\q18_w3.png), Circle = No, again, Left + Cross = Yes,
  "[boot] game window closed, exiting".
- The maintainer's next run (Enter) opened the box: Enter is START and the press-time check only knew Cross, so START was
  added (jstars_game.cpp). exitst: Start opened the box directly (no back-out), Yes closed the game.
- Z/X did nothing in the box: cellPad skipped the keyboard while the panel was up. 5056888: Yes/No mode keeps the
  keyboard pad mapping (Z = Cross = press, X = Circle = No), an Enter within 400 ms of opening is ignored, auto-
  repeated Enter/Esc never answer the panel, and a held Esc no longer quits.

## Input choice: Keyboard / Keyboard & Mouse / Controller (2026-10-28, the AI assistant)
- The maintainer: launcher choice of input type with its own bindings; KB&M = mouse camera + bindable mouse buttons; mouse
  menus option 1 first (simple: cursor, wheel, clicks), point-and-click per screen maybe later.
- ps3recomp (see commit after 5056888): INPUT_MODE, PAD_BTN_*, MOUSE_SENS, g_pad_mouse_look / g_pad_mouse_menus /
  g_pad_cursor_hidden / g_pad_wheel. jstars_game.cpp: mouse look while scene step 0x0094DEC4 == 0x14, mouse menus
  on 3, 4, 0xF. The launcher wrote the Controls page ("Play with").
- Tested: match tour (default mode) ok; KB&M menu run with Exit Game ok. Not tested (needs real mouse / a
  controller): clicks, wheel, mouse camera and sensitivity, controller remap, mouse ignored in keyboard mode.
- DualSense (the maintainer's only working pad, a DualSense Edge 054C:0DF2 on USB): raw HID reader in cellPad (commit after
  ce38c65). Test ds1 (INPUT_MODE=controller, pad plugged in): "[cellPad] DualSense connected (USB)", menus and Exit
  Game ran through PAD_FILE. Button/stick mapping itself not verified (needs real presses).
- 10-29: The maintainer saw the Cross-Save menu again after opening another Options submenu first. His log: "not taken
  (CD60=3 CCB8=1 654C=5)": 0x0096CD60/CCB8 are 2/2 only until any Options submenu has been opened. Inside the other
  submenus (sub1 dumps) 654C stays 5 and the cursor keeps that entry's index 1-4. exit_option_on_entry is now
  cursor 0 + 654C 5 + scene step 4. sub2 (Control Settings, back, Exit Game): box opened directly, Yes closed.

## Point-and-click menus, main menu (2026-10-29, the AI assistant)
- Main menu cursor 0x00956550 (0 J-Adventure .. 8 Options; it stays at the entry that was opened, e.g. 8 in
  Options, 3 in Free Battle). Main menu open: 0x0096CCB0 == 0x40D98E98 (menu state machine's current state; shop
  ..E50, Options ..E88). 0x0095654C stays 0 in the shop and Arcade's character select; 0x0094E078 varies per run.
- Item rects (1280x720) in jstars_game.cpp k_main_menu_rects; cellPad g_pad_menu_layout steps the cursor with
  d-pad presses. pc3 (PAD_MOUSE_SIM): hover Options moved 0 -> 8, click on Free Battle moved 8 -> 3 and opened it.
- Also fixed: INPUT_MODE keyboard/kbm ignored keys and mouse while a DualSense was plugged in.

## UI size: how the HUD is drawn (2026-10-29, the AI assistant)
- Fight frame (fc1, 1329 ops): op1179 copies the finished scene (0DE10000) into the 1280x720 composite
  0x0C6ABF40 with a fullscreen n=6 draw, then ops 1180-1327 (148 draws, blend on, vs 4/5/6, fp 0FD2D7C1..,
  0D64F201 for 216-vertex text) draw the HUD into it; op1328 presents it. Menus (cap1, Options): all 72 draws go
  into 0C6ABF40 the same way, so in menus the whole screen is UI.
- So a UI draw = a draw into the display composite after its first fullscreen copy. Shrinking needs a per-element
  anchor (corner/edge/centre), i.e. each draw's screen bounding box before it runs: the HUD VPs (vs 4/5/6) need
  checking for a plain position * constant-matrix transform that the CPU can evaluate.
- The maintainer 10-29: Exit Game missed after a battle: scene step 0x0094DEC4 is 3 after a fight, not 4. in_menu_scene()
  takes 3 or 4 (Exit Game check and main-menu layout).
- Plan for per-element anchors (not started): give each HUD draw a slot (its order after the composite copy);
  in the translated VS, atomic-min/max the clip-space xy into a per-slot UAV; read the bbox back one frame late
  (HUD is static frame to frame), pick the anchor from the 3x3 screen grid the bbox centre falls in, and pass
  (anchor, UI_SCALE) as a per-draw constant: clip.xy = anchor + (clip.xy - anchor) * scale. Gate: fight only
  (scene step 0x14) via a port hook; UI_SCALE in play_settings, slider in the launcher (launcher).
- The maintainer 10-29: UI size for fights only; menus skipped.
- Done (aaacd8e + jstars_game.cpp ui_scale): HUD VPs vs 4/5/9 pass clip-space positions through (o[0].xyw =
  v[0].xyw), vs 6 is the pixel-space text program (x = v.x/c466.x*c464.x - c464.y, y negated). The backend reads
  each HUD draw's box from attribute 0 in vp_vb and the port picks the anchor by cluster: top (cy > 0.65) -> top
  centre; bottom (cy < -0.45) with cx < 0.2 -> bottom-left, cx > 0.45 -> bottom-right; everything else (fighter
  name tags, markers, dialogs, the full-screen copy) unscaled. A per-draw 3x3-grid anchor (ui2) split the top bar
  and the player block and moved the name tags off the fighters. ui3 at UI_SCALE=0.7: clusters intact.
  Note: the tours' fight_hud screen match fails when the HUD is scaled (the TIME label moves).

## 2-player split screen (2026-10-29, the AI assistant)
- Test harness: cellPad PAD_FILE2 (virtual port-1 pad, 9ccebda); tour_2p.sh joins P2 (Cross, START) and P1 START on
  character select; the 2P stage select does not match the "stage" ref, so it just presses START. Use
  INPUT_MODE=keyboard for tests (kbm grabbed the maintainer's mouse).
- Real 2P: with INPUT_MODE keyboard/kbm a controller on port 0 moves to port 1; with INPUT_MODE=controller the
  keyboard is port 1 (PAD_PAD_P2=0 off both).
- Different teams = left/right split screen: each half's 3D view is a 640x360 strip (y 128-487) with a team-
  coloured box above (top bars) and below (portraits, health, map, timer). HUD draws in 0C6ABF40 start with
  four boxes x -1..0 / 0..1, y 0.647..1 and -1..-0.353 plus two full-width separator strips: a split-screen
  signature the port can detect per frame. The 1P cluster scaling scatters these, so UI_SCALE must stay off in
  split screen (todo) or use per-half clusters.
- The maintainer wants: no boxes, solo-style HUD (P1 panel bottom left, P2 bottom right, map/timer middle) and taller views.
  The view size needs the 3D strips themselves made taller (viewport + clip y scale per half), which risks the
  game's own frustum culling and screen-space passes; the HUD move alone does not give more view.
- The maintainer 10-29: split screen parked for later. ui_scale() detects split screen from the team boxes (bottom box drawn
  first, top box, separator strips) and keeps that HUD at full size; the flag resets only on the exact -1..1 scene
  copy (later +-1.05 quads must not reset it).

## Training mode step 1: health (2026-10-29, the AI assistant) - working behind TRAINING_TEST=1
- The maintainer approved the TRAINING_MODE.md plan with my order: no-death + refill first, then the 0-star dummy, then menus.
- hp1: Goku stood still in a Hidden Leaf fight; dumps at fight start, +30 s, +60 s. The only clean hits for "150 then
  104" (u16, max 150 at +8) are three copies at 0x0FEFC5F4/6C4/794: main-thread stack temporaries of the HUD, not the
  fighter (hp2: writing them changed nothing and they were overwritten at once). No heap field with the same ratio
  was found as f32/u32/u16/u8 (the real value may use another scale, or be a damage counter).
- Lead: the game's debug "Hp : %d" print (string 0x7BA960, TOC r2-0x3FEC, lifted ppu_recomp_006.cpp ~32600, function
  around 0x0013C6F8) gets HP from a virtual getter: obj = func_00063018(...), vtable[0x4C](obj). Finding that getter
  gives the health field offset. jstars_debug.cpp now has "w16/w32 <ea> <val>" to poke memory in a running game.
- 10-29 later: runtime probe PS3_ICALL_RET=<v> / PS3_ICALL_MATCH16=<ea> in ppu_loader.cpp (uncommitted) logs indirect
  calls returning a value. Getters 0x00085CC4/0x00085EAC/... are a family "field/100 rounded up" (fields +0x90,
  +0x94, +0x98, +0x9C, +0xA0, +0xA4 of one object: stats kept as hundredths). 0x00085EAC (+0x9C) returned 150 at
  fight start and 104 later in hp8, matching the HUD copy, on obj 0x4A8164D0 (objects move between runs).
  hp9: wrote 15000 into +0x9C of the first such object: it was 55200 and kept changing, and a Victory Burst
  followed, so that object/field is not settled. 0x0FEFC5F4 "104" may be a stack coincidence ('h').
  Next: log every 0x00085EAC call's obj per frame with its value to separate the fighters, then write test.
  The maintainer saw a test window freeze once (hp5, probe + memory reads): watch for it.
- hp10/hp11 (PS3_ICALL_TRACE=<target>, per-object value changes): the getter family covers 4-6 objects per fight
  (fighters + supports). +0x9C (0x85EAC) starts at 1000 and +0x90 (0x85CC4) at 3500-5000 per character; both drop
  in steps and then climb back about 1-2 per frame: gauges that regenerate (energy/dash), not health. The earlier
  "150 / 104" matches were these gauges at other scales. Health is still unidentified; next idea: the HUD's red
  health bar quad (vs4, box around x -0.58..-0.16, y -0.71..-0.62 in 1P) shrinks with health, so trace the draw that
  builds it, or trace the remaining getters (+0x94, +0x98, +0xA0, +0xA4) for a value that only ever drops.
- hp12 + lifted code: health is per fighter object, in hundredths: +0x90 current (regenerates up to +0x94), +0x94
  the recoverable cap, +0x98 max (e.g. 400000 = 4000). Damage is the virtual call 0x00085B84(obj, dmg90, dmg94): it
  subtracts both, clamps via 0x00085AE4 / 0x00085B54, and checks for the KO right away (vtable +0x18, +0x4C, +0x5C),
  so the cap must happen before the call. No direct callers: it always goes through ps3_indirect_call.
- Build: ppu_loader.cpp g_ppu_icall_hook(target, gpr) (sees and may change r3..r10 of every indirect call).
  jstars_game.cpp training_icall shrinks the damage so +0x90/+0x94 stay >= 100 (1 health) and records the hit time;
  training_tick (run from the pad filter, wrapper game_pad_filter) writes max into +0x90/+0x94 1.5 s after the last
  hit; the table resets when the scene leaves 0x13/0x14. TRAINING_TEST_LOW=N (test only) starts every fighter at N.
- tr1 (TRAINING_TEST_LOW=300): all 4 fighters were hit down to 1 health, 128 hits logged, many at 1 health with the
  damage cut to 0; nobody was knocked out (KO gauges empty at 100 s) and each refilled to max 1.5 s later.

## Training mode step 2: the dummy (2026-10-29, the AI assistant) - mechanism works
- "pad dummy" / "pad set" (TOC -0x7F04/-0x7F00) are only labels of the developers' debug menu (func_00543138 builds
  the window, func_000120E0 resets it); not used.
- pr1 (TRAINING_PROBE=1: indirect calls 10-30 s into a fight, per target and object): each fighter has a pad object
  (vtable 0x00875858). Its getters 0x9D5AC/0x9D680/0x9D6A8/0x9D6D0/0x9D6D8/0x9D728/0x9D750 test bits of
  +0x204/+0x20C/+0x210/+0x214 (all leaf functions, always called virtually). The human's pad is filled from the
  controller, a CPU's by the AI.
- pr2 dump of the 4 pads: +0x14 = slot (0 P1, 1 P1's partner, 2-3 the other team), +0x18 = driver (1 human, 3 CPU).
- ppu_loader.cpp g_ppu_icall_hook now returns int: nonzero skips the call (the hook sets gpr[3] = result).
  training_icall answers 0 ("nothing pressed") for those getters on a CPU pad whose slot is in the dummy mask
  (TRAINING_DUMMY_TEST=<hex mask>, test switch).
- dm1 (every pad): 100 s, 0 hits, enemies never left their start. dm2 (mask C = other team): P1's CPU partner
  fought them (126 hits, all on the two enemies), nobody hit P1 or the partner; they still take hit reactions.
- Step 3 below: the 0-star choice sets the mask; pads count only fighters that take part.

## Training mode step 3: menus (2026-10-30, the AI assistant) - done, waiting for the maintainer's test
- Free Battle (main menu cursor 3) opens the port's two-button box (cellOskDialog_choice_open, ps3recomp 6510ac7):
  Free Battle / Training Mode, Back stays on the menu. Then the game's own 1 Player / 2 Players and character select.
  Training turns off once the main menu shows again (tb1: "[training] main menu: Training off"). TRAINING_MODE=0 = no box.
- Battle setup records: 0x009540E4 + 0x2C*slot (slot 0 P1, 1 P1's partner, 2 opponent, 3 opponent's partner):
  +0x00 driver (0 P1, 2 CPU), +0x08 state (0 picked, 1 random, 2 "Don't participate"), +0x0C character,
  +0x10 team, +0x18 COM level 0-4. Character select resets them when it opens (cs6), so writing them early does nothing.
- Character select objects at fixed heap addresses: screen 0x48FAD920 (vtable 0x0087C3D0), slot cursor 0x48FAD958
  (order on screen 0 P1, 1 partner, 4 P1 support, 2 opponent, 3 opp. partner, 5 opp. support; Down from 4 goes to
  2 or 3 by the column it came from), settings panel 0x48FADD80 (vtable 0x0087C4A0; +0x2C == 2 open, +0x9C row,
  1 = COM Level, +0x48 the level, written through to the record live). The panel opens after a character is picked.
- Slot locks: the port presses Right, Triangle, Down, Down(, Right), Triangle, Up, Up(, Left) checking the cursor or
  record after each (training_lock_run), then Triangle/Cross on locked slots and Select (Default) are swallowed.
- 0-star: on the COM Level row of a CPU, Left at 1 star sets g_dummy[slot] (game level stays 0 = 1 star), Right
  clears it. A level of -1 draws 5 stars (cs3), so the look is a frame patch: g_rsx_copy_rects copies the 2nd (empty)
  star over the 1st in the panel row (792,281 35x35 from 832) and the slot row (slot 1/3: 429,237/500; slot 2:
  212,413; 24x22 from +30).
- In a fight the pads number only the fighters that take part (cs7: 1v1 opponent = pad slot 1), so the dummy mask
  counts records with state != 2.
- Time limit: stage select screen 0x48FAD1A0 (vtable 0x0087FCF8), focus +0x58 (0 stage, 2 music, 3 time), time
  choice 0x48FAD530 (1 = 300 s, 0 = Unlimited; the screen starts at 300 each time). 0x0096D038 only follows the
  display (writing it did nothing, tf1). The port presses Right, Down, Cross, Right, Cross (training_time_run).
- tf2: box -> locks (slots 1, 3) -> 0-star opponent -> stage select Unlimited -> fight TIME infinity, dummy mask 2,
  no hits on P1 for 40 s -> pause, Return to main menu -> Training off. JP stayed 007100 after quitting (tf1): no rewards.
- Seen twice before cs6 (same build, not reproduced since): one boot hung with "cellSpurs guard ... chain gives up",
  one stayed on "Save data loaded" ignoring Cross.
- 10-30 fix (the maintainer: after choosing Free Battle and backing out, Free Battle went straight in): backing out of
  character select leaves the menu state 0x0096CCB0 at 0x40D98E50 on the main list, and after a fight the main menu's
  screen object 0x48FAD1A0 has vtable 0x0087D368 instead of 0x0087D600 (md5, fb3-fb6). The box and "leaving Training"
  now only need 0x0095654C == 0 (Free Battle's 1P/2P, character select and stage select are all 2) and not the
  character/stage select objects. fb7: box shows after backing out of 1P/2P, of character select, after a fight, and
  after a fight plus backing out of character select. Point-and-click (main_list_open) accepts 0x0087D600 or the old
  state value; after a fight plus backing out it still falls back to keys.
- 10-30 fixes (the maintainer: the 0-star CPU still jumps and attacks, health never refills):
  - A Training fight (1 on 1, Unlimited) stays at scene step 0x10 the whole time (td1: TRAINING_DEBUG=1 logs the step
    every 3 s), so the refill table was cleared every poll. training_fight_scene now takes 0x10-0x14; mouse look,
    the arrow keys and the HUD size treat step 0x10 as the fight in Training while the HUD reads health (0x85CEC
    stamps g_hud_ms).
  - Emptying the pad getters only stops the AI from walking: close up it still attacked (tw1, the dummy hit P1 in
    long chains). Handing the dummy's pad to a human driver (+0x18 and +0x1FC 3 -> 1) when the fight first reads it
    stops that: tw2/tw3 (P1 walks up and attacks, stick input "stick 128 0 128 128 150" in PAD_FILE) hit the dummy
    12 and 17 times and it never hit back; refills logged. The getters still answer "nothing pressed" for that pad.
    Not checked: whether a controller on port 2 (Player 2 in keyboard modes) reaches the dummy some other way.
- 10-30 star loop (the maintainer: 0 between 5 and 1): Left from 1 and Right from 5 give 0; from 0 Right gives 1, Left gives 5
  (the game's own 1 -> 5 wrap is let through). Watched on the panel level, not single presses: a held Left used to
  run past 1 into the game's wrap to 5. The panel's open/row flags blink for a poll while the stars change (st3),
  so the state carries over those polls. st4: 1 -L-> 0 -L-> 5 -R-> 0 -R-> 1, checked on screenshots.
- 10-30 padlocks (the maintainer: slots 2 and 4 should look like Arcade's, no button script): compared Arcade with Free Battle
  (ar1 against cs2). The real record starts 4 bytes after my base: real +0 flags, bit 0 = locked (my +0x04), then
  state, character, ... The screen keeps two copies of the records (0x48FAD958, 0x48FADA88). training_lock_slots
  writes my +0x04 1, +0x08 2, +0x0C 0, +0x18 1, +0x20 0 in all three for the first 90 polls of the screen: padlock, no
  presses (dl1, ex3: also on re-entry, and a Free Battle in between is unlocked). The old script ran only when the
  cursor was on P1, so a re-entry could leave a partner in (the maintainer's log, dummy "fighter 2").
  Still different from Arcade: no "CPU" text on the locked slots (that screen builds it from the start; copying the
  opponent's label showed a lighter box, so it was removed).
- 10-30 Exit Game: The maintainer's log had "Cross at cursor 0 not taken ... scene step 5", so Options can be at step 5. Not
  reproduced by visiting the shop, gallery, J-Customize or BGM editor (all 4). in_menu_scene and in_mouse_menus take
  5 too; exit_option_on_entry takes any step below 0xB. ex2: step forced to 5, the Yes/No box opened directly.
- 10-30 box skipped after other menus (the maintainer, same session as the Exit Game log): same cause, scene step 5 in the
  menus, which in_menu_scene did not accept. Options, Arcade, the shop, stage select and back all stayed at 4 here
  (mb1), so what sets 5 is still unknown. in_menu_scene is now any step below 0xB, and main_menu_open needs 654C 0
  plus the main menu object (0x0087D600, or 0x0087D368 after a fight) or state 0x40D98E98, so the shop and title stay
  out. fb8: box after 1P/2P, character select, fight, fight + character select. mb2: step forced to 5, box and Exit
  Game's Yes/No both opened. A skipped box now logs "[training] box not offered: step, CCB0, screen".
- 10-30 own screen tracking (the maintainer: box gone after Online Mode, locks gone after a Training match; the log: "box not
  offered: step 4 CCB0 40D98EC8 screen 00884A78"): the menu objects are on the heap and move (pv1: main menu at
  0x48FEF150 after Online instead of 0x48FAD1A0), so every fixed heap address was wrong from then on. TRAINING_PROBE_FLAG
  windows per screen show each screen calling its own virtual methods every frame. screens_note (from the icall hook)
  stamps object and time: main 0x001DE098 (vtable 0x0087D600), character select 0x0019AA74 (0x0087C3D0), settings panel
  0x0019E2E8 (0x0087C4A0), stage 0x0026EF94 (0x0087FCF8), its time box 0x00279F0C (0x00880018). screen_up = stamped in
  the last 300 ms and the vtable still there. All fields are now relative: cursor and record copies screen +0x38/+0x168,
  panel +0x2C/+0x48/+0x9C, stage focus +0x58, time +0x40, menu jump copies main +0x46.. Locks are written every poll of
  the screen. af2: Online error -> box -> Training -> locks, 0 stars, Unlimited -> fight -> Re-select -> locks and 0
  stars kept, Unlimited -> fight -> main menu -> Training off -> box -> Free Battle unlocked.

## Port message boxes look like the game's (2026-10-30, ps3recomp a85c7fa)
- The maintainer compared our "Choose a mode" box with the game's "Return to the previous screen?". Measured on a 1280x720 frame
  of the game's box (pv1/w_online) and the maintainer's screenshot: box 307 units tall at y 201 (ours was 318, centred), panel
  from y 12, halftone dots only top left (spacing 8.33 units, ours also had them bottom right), a swirl lower right,
  no key hints, white text with a dark edge also on the yellow button, buttons of flat vertical stripes.
- oskp_render now does all of that. The swirl is drawn by GDI paths, not game art, so no game texture is in the source.
  Key hints stay on the typing panel only. Rounded outer corners are not done (the panel is a plain opaque copy).
- Tested: bx2 (RES_SCALE=2) screenshots of "Choose a mode", bx1 of "Exit the game?" before the last size tweak. The
  typing panel (deck rename) uses the same code and was not opened after the change.

## Stuck "Return to the previous screen?" in 2-player Training (2026-10-30)
- The maintainer: only Left/Right worked on that box. Character select in 2 Players asks this box on Back; in 1 Player it goes
  straight back (rb1). Only Cross answers it, Circle does nothing.
- Cause: training_charsel removed Cross and Triangle whenever the slot cursor was on a padlocked slot, also while the box
  was up. So it got stuck only if Back was pressed with the cursor on a padlocked slot, which is why earlier exits worked.
- Fix: filter removed. The game itself ignores Cross and Triangle on a padlocked slot (tp2 screenshots), so it was not
  needed. tp3: from a padlocked slot No closes the box and Yes goes back to 1P/2P; from Player 1's slot too.

## Unlocking every character (2026-10-30)
- Save struct in memory near 0x40903B8C (SYS-DATA +0x29A8C): u32 J-Points, then the three friendship gauges (+4, +8, +C).
  Slots Left follows the gauge levels: 0 at Lv.1, 44 with all three MAX, one per item bought (shared by the Battle and
  Support tabs; the J-Adventure tab has no slots). 44 is exactly what was left to buy: 31 battle, 13 support.
- SYS-DATA +0 changes with every save and may be a checksum (not verified), so I used memory plus a real in-game save
  instead of editing the file. Battle list 39 entries, Support 13. The shop cart takes a whole tab in one purchase.

## Fight freeze: signed counter overflow in a capped log (2026-10-31) - fixed, `FREEZE_FIX.md`
- Fights froze the window after about 6 minutes (busy fights) to 14 minutes (scripted fights). Cause: a `static int` counter
  in `cellGcmSys.c` that was meant to stop printing after 6 lines kept counting once per inline word (~88,000 per frame in fights),
  passed 2^31 within minutes and went negative, so every word then did a `printf` and the FIFO walker fell to ~120 words/s.
- Fix (`toolkit/ps3recomp-local.patch`, in `libs/video` and `libs/spurs`): 132 capped counters now stop at their cap. Needs the patch at this fix or later.
- Evidence: scripted run 30 min with ~26 min in a fight, no freeze, message printed 6 times; a 20-minute normal session closed normally.
  The overflow itself was not watched directly. Ruled out: perf/frame/flip-count switches, the indirect-call hook.

## Balance work: where combat numbers live (2026-10-30) - in progress
- Damage path: virtual 0x00085B84(obj, dmg, rec_dmg) called from func_00082944 (0x82B24), via func_00082ED4 (0x82F34) from the
  fighter's message handler func_00084FF0 (0x85220). func_00082944 adds up pending hit records (obj +0x22C count, 0xB0 each from +0x230).
- Data files: partition_op_character_ps3.cpk, character/battle_character/battle_character_NNN_m.pak, compressed ($CMP/$CL0)
  on disc. In memory during a fight, as STPK packs: NNN_<name>_mp_param_attack, _param_move, _param_action, _param_eff_bullet,
  _param_assist_perf and more (seen: 000 Luffy, 028 Gintoki). Support: NNN_support_move_table.
- Gintoki's param_attack (68656 bytes) looks like a 16-byte header plus 156 records of 0x1B8 bytes (first u32 is 0x1B8).
  Which field is damage is NOT known yet; the log's damage values (12000 etc.) are not stored as such, 100/50/300 appear often.
  Inferred only. Next: match the maintainer's Gintoki session (run/balance_log.txt) to records.
- 10-30 the maintainer's session (gin1): 105 hits, no pad or stats lines and no refill, because 0x0094DEC4 was outside 0x10-0x14 in that fight (bl2 read 12). Fights are now told by the HUD (fight_hud_up). Table layout: rows of 0x618 with 10 hit slots of 0x9C, +0x28 u32 damage, +0x2C f32 recoverable ratio; the hit sequences match. Details: docs/balance/README.md. Unexplained: 25/10 and 500/200 in the log are not in the table, so damage is scaled sometimes.
- 10-30 $CMP format solved (jstars-notes/tools/cmp_unpack.py): 0x20 header, then chunks with tag, unpacked size, chunk size including the header. $CL0: LZ tokens (even t: t>>1 literals; odd t: copy (t>>1)&0x1F from ((t>>6)<<8)|next byte). $CLH: a $CH0 Huffman block holding a $CL0 chunk. $CH0 alone: Huffman data, table entries of 4 bytes (u16 LE code, length in the top 5 bits of byte 2, symbol), bits MSB first; one entry means a run of that byte. $CR0: stored. Character 028 unpacks byte-identical to the memory copy (4407696 bytes). Stamina is fighter +0x9C, its limit +0xA0, base +0xA4.
- 10-30 Hit records (fighter owner +0x230, 0xB0 each, count +0x22C): +0x64 damage, +0x68 recoverable, +0x6C gauge gain, +0x94 reaction entry, +0x98 attack slot, +0x9C projectile row, +0xA0 action row. Attack slot +0x30 first 16 bits = gauge gain, proven by bt1 (20 raised to 300: R3 prompt after about 7 strings, so full is inferred near 2000, between about 1905 and 2225). Projectile row +0xA4 = gauge gain. param_action +0x0C = Stamina cost (matched 50/100/200/300 presses; action = row/4; action 21 holds -150 on 7 characters, untested). Picks are read when leaving character select: writing the setup records on the stage screen does nothing to who loads. 0x4902AC68 in fight vb1 = running total of damage dealt by P1 (600, 1400, 1400, 2200 against the hits), its use is unknown. 0x4A5F60D8 in vb1 moved with gauge gain (-97.65 to -120.2) and looks like the marker's screen position. Half damage: not explained by either fighter's Stamina; whole strings can be halved.
- 10-30 Freeze about 13 minutes after launch in scripted runs (808, 776, about 824 s; different fighters and moves). Thread dump freeze3: guest main at func_005383D4+0x19C, which loops func_005382EC (job handle still in the table at 0x4B7FE7F0, 12-byte entries, 512) with a 20 us sleep, called from func_00335DA4 <- func_00334D58. Job sequence counter was 0x15B073 then. Other threads sit in func_0060AE04, which is sys_event_queue_receive (syscall 0x82), so they look idle. Cause unknown: which job, and why it never ends. Guards then report 'still closed after 20 s' for all 20 chains, and the window stops responding.
- 10-31 Cast run: support files repeat leftover rows, so table numbers do not prove damage (Neuro lists 240 and shows no hit). Max health seen: Luffy 4500; Sasuke, Goku and Ichigo 3500; others 4000. Charged state: Luffy shows an orange Stamina bar and Ace a white one; that these are the power-up states is inferred. The number by L2 is the support cooldown, not a power-up timer. Boot: screen_match.py takes one name per call.
- 10-31 Freeze capture ftc (fz1, fz2, dbg_out; log stopped 13 m 1 s after launch, runs now 776 to 828 s): without PERF, RSX_FRAMELOG and GCM_FLIPCOUNT it still froze (ftb). Job table 0x4B7FE7F0: entries are {live flag, job object, sequence}, 35 live in a row (sequence 0x1549ED to 0x154A0F) with the next sequence 0x154A39, and no change between dumps 25 s apart, so jobs stop finishing while the game is waiting on them. Sequence at freeze: 0x154A39 (ftc), 0x15B073 (freeze3), so about 1.4 million jobs, about 70,000 per worker if even. A 16-bit limit per worker is a guess: no counter near 65,536 is in the worker blocks (+0x18 = 0x10000 on all 20 is a constant). Main thread was in func_005383D4 at freeze3 and func_006507DC <- func_0032DDD8 at ftc, so the top of the stack differs. Cause not found.
- 10-31 Freeze, more evidence. Normal play froze too: launcher session up 03:54:14, balance log stopped 04:00:32 (6 m 18 s, about 4.5 minutes into a Medaka fight, last press L2). With the indirect-call hook removed 15 s into the fight (ICALL_OFF_IN_FIGHT, run fth, light walking and no hits) it still froze, later: launched 04:10:49, stderr stopped 04:28:36 (17 m 47 s). So the hook is not needed for the freeze. Busier fights freeze sooner, which points at something used up per job or per effect rather than at a clock. After a freeze the RSX side keeps logging inline uploads ([GCM2D], 122,000 lines in the 50 s after the maintainer's), flips stop, and 20 s later all 20 job chains report 'still closed', which is a result and not the cause. Next ideas: (1) test an older build from before 10-30 to see if it is a regression; (2) add a job-count log to compare sequence numbers at the freeze across busy and light fights; (3) find which job the oldest live table entry is, from ftc fz1 (job objects at the entries' second word); (4) watch heap or event-queue use over time.
- 10-31 **Fight freeze: cause found and fixed.** cellGcmSys.c logged `[GCM2D] inline write` under `static int _it; if (_it++ < 6)`. _it counted every inline word, about 88,000 per frame in fights (FP uploads per draw), so it passed 2^31 and went negative after minutes, and the check was true on every word from then on. Each word then did a printf, so the walker fell to about 120 words per second and flips stopped. The main thread then waited on a flip or a job, which is why the top of its stack varied, and the all-chains 'still closed' lines came 20 s later as a result. This matches: busier fights freeze sooner (6 to 14 minutes), the log flood starts at the freeze (2,102 lines before the give-up in ftc, 76,000 after), the 10-26 build freezes (old1, 12 m 37 s), and demo runs stopped flipping at 540 to 595 s on 10-24. Fix: 132 capped counters in libs/video and libs/spurs now stop at their cap (`n < 8 ? (n++, 1) : 0`). Test fix1: no freeze in 1805 s after launch, about 26 minutes of fight, 685 hits, exactly 6 upload lines in the log. Not converted: 4 counters of the form `n++ < 8 || n % k == 0`, and counters in runtime/ppu and port stubs, none on the upload path; they would overflow only after far longer.

## What starts a special (2026-10-31)
A fighter's `.ocm` file is its move graph. Entering a state sends message 23 with the action to `0x0007A4D4`; Stamina is spent through `0x000877E4`. `battle_move_table` isn't needed mid-fight (mv1). Jotaro's specials start but don't hit; his Star Platinum has motions and actions but a 0-byte model (sw2). Full write-up: WHAT_STARTS_A_SPECIAL.md. Test switches `ACT_TRACE` and `ACT_SWAP`, debug verb `zero`.

## Test Fight (2026-10-31)
`AUTO_FIGHT` walks the game into a Training match on its own, about 72 s from launch. The scene word `0x0094DEC4` only reports progress. Start + Cross skips the intros. A directly written time limit is not honoured, so the auto-pilot uses the same presses as Training. Full write-up: TEST_FIGHT.md.

## Training settings menu (2026-10-31)
The pause menu is data-driven: pointing its pack entry at a longer table adds a real row, and the panel resizes. Unlimited Stamina works. The Burst setting isn't done: the gauge value and R3's check aren't found. Write-up: TRAINING_MODE.md.

## Name tag split at UI_SCALE below 1 (2026-11-02, the AI assistant)
- The maintainer's 10-30 picture: Koro-sensei's bars and name at the top edge, the empty plate below. Their play_settings has UI_SCALE=0.70.
- Cause (log pf1, UI_SCALE_DBG=0C6ABF40): `ui_scale` scales anything with box centre y > 0.65 toward the top centre. A tag's strip and bars are short and sit high, so their centres crossed 0.65 before the taller plate's did: plate box 0.066,0.496..0.346,0.702 sc=1.00, bars 0.122,0.607..0.322,0.696 sc=0.70. The same rule could also catch a tag low on screen in the bottom-left cluster.
- Fix: tags are drawn right after the scene copy, before every other HUD part. `ui_scale` keeps pieces at full size from the copy until the first piece that isn't tag-shaped (width under 0.7; height about 0.037 text, 0.089 strip and bars, 0.206 plate).
- Result, counted by a local script: pf1 271 frames with a tag near the top, 237 mixed; pf3 297 and 0. pf4, normal fight: tags untouched, top bars still scaled. pf5, normal Training fight walking up to the opponent: 28 frames with a tag near the top, 0 mixed, top bars still scaled (Turn-based hides the top bar, so pf3 alone did not cover this).
- Reading: a tag's plate could change height during its in or out animation and end the run early for that frame; not seen in pf3 (0 mixed). Other parts that follow fighters, such as markers, still go by position.

## Name text losing thin strokes: unit 0 ignored the game's filter on clamped textures (2026-11-02, the AI assistant)
- Symptom: in test pictures "Koro-sensei" lost its hyphen, its "e"s broke up, and "Taro" read like "Iaro", changing as the tag moved.
- Cause: unit 0's static sampler is point + clamp. T0_LINEAR only took the game's linear filter on draws asking for wrap or mirror, so clamped textures such as the font atlas stayed point-sampled.
- Fix: `dr_t0_wrap` returns the linear mode for clamped draws too (`T0_CLAMP_LINEAR`, default on). The existing `_t0w` keeps those coordinates half a texel inside the edge.
- Result: over 16 pictures each, letters are clean with the filter on, at 720p and with RES_SCALE=3; broken letters in several pictures with it off. Menus and fight unchanged by eye (pg0, pg1), 30.0 flips/s both ways.
- Still open: the hyphen itself is missing in 2 of 16 pictures with the filter on (1 of 16 off at 4K, 4 of 16 off at 720p). Same count at 4K, so target pixels are not the whole story. Inferred, not checked: mip choice, or the HUD being drawn at 720p.

## Replays work offline (2026-11-02, the AI assistant)
- The end-of-fight menu has Fight again, Re-select character(s), View replay, Save Replay, Return to main menu. "View the replay from the beginning?" defaults to No.
- View replay ran the whole fight with its intros, KO and Victory Burst. Save Replay shows no box in rp8 and writes `NPUB31647-B2_000` (ICON0.PNG, PARAM.PFD, PARAM.SFO, SYS-DATA 400,000 bytes).
- Gallery > Battle Replay has Watch Replay and Delete Replay Data. Watch Replay loaded that save and played to the win pose. Its pause menu has Close, View replay from beginning, Return to battle replay menu.
- For scripts: the results screen waits for a press, so a long fixed wait is safe, and the first run of fights showed they end at different times.

## Replay frame rate against the same fight live (2026-11-02, the AI assistant)
- The maintainer saw about 20 FPS in a replay during a test run, and which counter they read is not known yet. The window title counts batches, not frames.
- rm3, RES_SCALE=3, one Free Battle with P1 attacking, then View replay of it, 190 s of each: live median 30.5, 1% 22.1, lowest 17.3, 31 windows under 27; replay median 30.4, 1% 26.7, lowest 21.3, 7 windows under 27.
- Both parts dip about 70-90 s after the fight starts. So the dip belongs to that moment of the fight, not to replay mode. Inferred: the live part may also have had first-time shader builds, which would explain why it was lower. Not checked.
- One run of each, with few screenshots. Each screenshot can cost frames, and a run taking them often shows more dips.

## Frame rate lows: unused depth copies (2026-11-02, the AI assistant)
- The maintainer asked for stable 30 FPS on all maps. Dips at RES_SCALE=3 were of two kinds: render_frame gpu-wait about 20 ms for up to a minute (rn2: 17-25 f/s), and frames with no wait on the GPU where snapshots rose to about 790 per batch (rn3: 23 f/s).
- Cause of the extra load: `dt_snapshot` ran for every depth bound on a texture unit, whether the program samples it or not. `[PERF7s]` (rn4): 934 dirty depths per batch on unused units, 1 sampled without depth write, 3 sampled and written.
- Fix: this is idea 1 of patches/res-scale-speed. Only copy when `fp_info(fp)->tex_units` has that unit, or when the program is unknown. `dirty` stays set, so the next draw that really samples it still gets the current depth. That keeps the rule from the flat-ground bug. `ZSNAP_ALL=1` turns it off.
- Result: snapshots about 790 to about 4 per batch, record 2.3-3.0 to 1.3 ms. 2-on-2 Free Battles with PERF on: rn2 lowest 10.7, 1% 17.8; rn3 21.3, 22.9; rx1-rx3 25.6-26.7, 27.8-29.1. These are different random fights, three runs after and two before.
- Remaining: hitches when a fight first loads, such as rx1 at 70.4 s (15.2 f/s) with 143 PSO creates and 219 textures created in one window, and stage 8 (sv8 10.3, sw1 12.5, sw2 27.8), not looked into further. The game side still takes vbl about 21 ms of each 33 ms frame (FTL), which leaves little slack (inferred).
- Without PERF, as the maintainer plays (ry1-ry3, RES_SCALE=3, 2-on-2): mean 29.6-30.0, 1% 27.9, 27.9, 25.6, lowest 24.6, 17.8, 23.7. Before the fix without PERF: rm2 1% 24.6, lowest 22.8; rm3 live 1% 22.1, lowest 17.3. In sw1 a 350 ms frame came with `fxc 7 0.419s` and `pso create 21` in the same window, so one-off hitches fit a fight's first shader builds (inferred; run 3 of that map had none).
- DRAW_SAMPLE at 5 s per window (rz1, RES_SCALE=3, 2-on-2, build-pdb, fight lowest 25.6, 1% 27.8): the drawing thread sits in `cellGcm_fifo_kick_wait` 54.4% of the time over windows 20-40, and 48.5% over windows 43-46, the ones covering the 26-27 f/s drops. So it still has spare time on average and is waiting for the game to give it work. Five-second windows can't resolve a 300 ms drop, so what the game side does in those moments isn't known. Next would be GAME_SAMPLE and the SPU threads with shorter windows.
- Seen once, not looked into: rn3 f11, Yoshiwara: a black screen with bright blue lines and the HUD normal, at time 274. The maintainer 11-02: this is one of Aizen's skills, not a bug.

## Jagged character edges and the Inferno Crags sky (2026-11-02, the AI assistant)
- The maintainer's report, three screenshots of Sasuke on Inferno Crags. AUTO_FIGHT stage 8 is that map. The maintainer's feedback: frame rate in play looks fine, the rest of the lows can wait, and skill effects look less pixelated since the clamped-texture filter fix (the maintainer's impression, not measured).
- Edges: no anti-aliasing exists in the port (every target has SampleDesc.Count 1). At RES_SCALE=3 the 3840x2160 swap chain is stretched to the window by DXGI. Guest-size depth copies are not the cause: ZSNAP_FULL=1 (cr2) gives the same edges as cr1, and runs at 27.8 lowest, 30.0 mean over 33 s.
- Sky: blurry and blotchy with dark smoke plumes in it (cr1 c3). No reference picture of this map exists among the RPCS3 reference captures, so whether it matches the real game is not known. Asked the maintainer.
- Smoke gap around characters (the maintainer's first screenshot; xs/sp1, sp2, sp3): reproduced with the hair in front of smoke. Same rim at RES_SCALE=1, at RES_SCALE=3 and with ZSNAP_FULL=1, at about the same screen size, so neither today's copies nor guest-size depth cause it. VFX.md says effect layers are depth-tested against the half-res 0x0D940000, which fits (inferred). Asked the maintainer for an RPCS3 picture.

## Edge smoothing (2026-11-02, the AI assistant)
- The scene copy is the first exact -1..1 draw into 0x0C6ABF40 in a fight. `vp_record_cb` asks `g_rsx_aa_mode` on that box, and the replay runs `aa_pass` right after that draw, once per frame: copy the target, then a fullscreen FXAA draw back into it. It uses the game root signature with its own SRV window at AA_SRV_BASE, and the replay rebinds the target and forgets its cached PSO, viewport and topology.
- Result: edges a little softer, HUD text unchanged by eye. Speed unchanged in these runs. The 22.1 low in ab1 matches lows seen without FXAA (ry2 17.8), so not attributed to it.

## Effect layers and the "missing" glow (2026-11-02, the AI assistant)
- Pass order in a fight (gl1): particles into 0x0DB30000 (640x360) tested against 0x0D940000 (rebuilt by the depth copy op), colour effects into 0x0DC20000; 0x09CA87C1 (code 0xF01DA36B) copies 0DB3 to 0DA4, blurring alpha over 8 samples; 0x09CA8741 composites it: scene * alpha + colour.
- Smoke rim: that alpha blur is the main part of it. FX_NOBLUR=1 narrows it a lot (sq1). The maintainer's RPCS3 screenshot shows the rim too.
- FX_FULL=1 doubles those layers' scale (RES_SCALE 3: 3840x2160). Unscaled sampling constants are unaffected because everything uses normalized coordinates; none of the layer programs read the pixel position. The write-back to guest memory skips these targets.
- Glow, reading corrected: lumflag-trace-5 had a bug (it read the pass name inline; the name is a pointer at +8). Fixed, it shows pe::luminance executing every frame, with draw commands (fn 0x8BBE50) running: 422 logged drawcmd lines (gl2). Frame captures (gl1, 6 frames) show 0x0E590000 cleared and never drawn, but the first group of 0x0DC20000 draws straight after that clear is alpha-only with vertex colour, which fits being those luminance draws (inferred from order and count, not matched one by one). So the idea that the glow never draws came from looking only at 0x0E590000. The bloom chain 0DC2 -> 0DA4 -> 0D65..0D89 -> added to the scene also runs; in frame_03 0D89 peaks at 14 of 255. Whether it is as strong as on RPCS3 is not known: needs one skill compared side by side. The trace patch was taken out again (.pre-lumtrace restored).
