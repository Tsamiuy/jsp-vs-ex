# Debugging techniques that worked

## Reading the logs

- `run\stdout.txt` = HLE module prints; `run\stderr.txt` = runtime diagnostics. Both are huge;
  filter with `grep -v` on the per-frame noise (`SetDefaultCommandBuffer`, `NetCtl] GetState`,
  `event_queue_receive(q=2 timeout=20)`, `cellSpurs]`, `RunFibers`).
- `[hle] unresolved NID 0x…` = import faked as CELL_OK. Name it via `jstars\out\hle_coverage.json`
  or by hashing candidate names: `python3 -c "import sys; sys.path.insert(0,'ps3recomp-src/tools');
  from nid_database import compute_nid; print(hex(compute_nid('cellFooBar')))"`.
- Distinct-line histogram: `sed -E 's/0x[0-9A-Fa-f]+/#/g; s/[0-9]+/N/g' | sort | uniq -c | sort -rn`.

## Debug console (`port\jstars_debug.cpp`)

`run_test.ps1 <sec> -Commands stat,threads,"mem 48E03570 64"` →
`run\dbg.txt.out`. `stat` = uptime/flips/last HLE; `threads` = which guest thread is inside which
HLE call, each guest thread's lr/r3/sp (`[WATCHDOG] guest-tid N lr=… r3=…` lines go to stderr),
and each host thread's lifted call chain; `mem` = hexdump (≤4096 bytes); `res` = the post-movie
resource/scene state (registry, containers, handles, tasks, scene state machine) plus the
`jstars_dbg[]` counters fed by the `JSTARS-DIAG4` hooks in the lifted code. Pointer chains take one
run per hop (addresses were stable between runs).

## Environment knobs (runtime)

| Var | Use |
|---|---|
| `HLE_BT_EVERY=N` | guest stack of each thread every Nth HLE call |
| `PPU_WWATCH=<hex>` `PPU_WWATCH_LEN=0x60` `PPU_WWATCH_MAX=0` | every guest store into a range, with writing function — found the memcpy overrun |
| `SPU_ATOMTRACE=N` | SPU atomic ops (cmd 0xD0 getllar, 0xB4 putllc) with EA — found the fiber signal |
| `SPU_DMA_WATCH=<hex>` | SPU DMA covering an address (also reports GETs) |
| `SPURS_JOB_MBOX=1` | mailbox values posted by SPU jobs |
| `SPU_DUMP_MISS=<dir>` | dump unlifted job binaries (set by run_test.ps1) |
| `FS_READ_ALL=1` | every cellFsRead (now with `pos=` and `t=` ms — local edit) |
| `FS_CALLER=1` | log lseek/close (host-stack caller, often just the CRI wrapper) |
| `PS3_FS_GSTACK=1` / `=2` | guest call chain on every close + failed open / + every open (local edit) |
| `JSTARS_DATACHECK_MS` | port: minimum DataCheck hold (default 1000; then waits for 750 ms of quiet file I/O) |
| `JSTARS_FIBER_LOG=1` | port: every fiber signal pickup / park / resume |
| `PAD_SCRIPT="sec:mask,…"` | timed button presses (START 0x0008, CROSS 0x4000, …) — skips the movie |
| `PAD_FILE=<path>` | write a button mask into the file to press it once |
| `RSX_TRACE=N` | first N RSX methods (`[rsxm]`), histogram them to see what the GPU gets |
| `GCM_CTXDBG=1` | which GCM context the title is driving (begin/end/current) |
| `GCM_DRAINDBG=1` | FIFO walker passes: get/put, why it stopped |
| `GCM_CIRCLE_DUMP=1` | last 64 non-zero FIFO words when the walker circles (local edit) |
| `GCM_FIFO_SNAP=N` | raw words at get and put for N drains |
| `AUDIO_SILENCE=1` | toolkit patch 3900: `[audio-silence]` running total of all-zero mixed audio blocks, every 1000 blocks (crackling shows as silent blocks) |
| `PS3_FSLOG_EOF=fd,bytes` | fake EOF on a descriptor (J-Stars' movie player just retries — not useful) |

`docs\DIAGNOSTICS.md` in ps3recomp lists ~470 more.

## Inserting a temporary diagnostic into lifted code

Lifted code is plain C++, one `func_XXXXXXXX(ppu_context*)` per guest function. To see register
values at a guest address: find `ctx->lr = 0x<return addr>;` (the call site) or a `loc_XXXXXXXX:`
label in `port\recompiled\ppu_recomp_NNN.cpp`, insert a rate-limited `fprintf(stderr, …)` reading
`ctx->gpr[n]` / `vm_read32(...)`, rebuild (only that file recompiles, ~20 s). Use a Python script
file for the edit — shell quoting turns `\n` into real newlines. A re-lift removes the edits.

## Static reading tips

- Outline a function: `grep -nE "^loc_|func_|ps3_indirect|lv2_syscall|goto"`.
- `ctx->gpr[11] = 0xNN; lv2_syscall(ctx)` = syscall NN (0x8D usleep, 0x8A event_port_send).
- Import stub address → name: `jstars\out\EBOOT.imports.json` (`stub` field).
- Jump tables / vtables are static data: read them from `EBOOT.elf` (segment 0 file offset =
  vaddr − 0x10000).
- Decode SPU code from the lifted C (`spu_rdch MFC_RdAtomicStat` = atomic, `WrOutMbox` +
  `WrOutIntrMbox` + `RdInMbox` = `sys_spu_thread_send_event`).

## Approach that kept working

1. Find what the stalled thread waits on (lr, r3, HLE in flight).
2. Read the waiting code; name the state it polls.
3. Find who should change that state (callers / stores / SPU atomics).
4. Watch that memory at run time.
5. Fix in the port where possible (context-handler overrides registered from
   `ps3_load_prx_modules()` win over the runtime's), in `ps3recomp-src` only for real bugs.

## Second opinion

When stuck, write a self-contained brief and have a second, read-only reviewer look at it. Round 1
(2026-10-19) did two things:
- It corrected a wrong premise: `_v.pak` was loaded, before `_m.pak`.
- It mapped the resource-completion predicates, which the `res` console command then measured
  directly.

## Reference run in RPCS3

`RPCS3\rpcs3.exe --no-gui E:\PS3_GAME\USRDIR\EBOOT.BIN` (ISO mounted as E:) boots the game in
RPCS3. Its log (`RPCS3\log\RPCS3.log`, copied to `jstars\run\rpcs3_reference.log`) lists
`sys_fs_open` calls and HLE warnings, which gives a working boot to diff against.

## Rendering (D3D12 backend) debugging

- Frame capture: `RSX_CAP=start:count:stride` with `RSX_CAP_DIR=<dir>`.
  - It writes `frame_NN\manifest.txt`, listing every clear/draw in order with target RT,
    viewport, fragment program, VS index and textures.
  - It also writes every render target as a BMP.
  - The frame number counts backend frames, which run ahead of flips. Capture a range.
  - Convert BMPs with System.Drawing (see the session's commands) to view them.
- Screenshots of the game window: `jstars\snap_window.ps1 -At "60,90" [-Out dir]`. It uses
  PrintWindow, so it works while covered. A 1x1 PNG means the window was gone or hung.
- `PERF=1`: per-second `[PERF]` line showing fps, texture/vertex upload, pipeline time and misses, and (toolkit patch
  2900) `texres N`, the texture resources created in that window.
- Shader tools:
  - `FP_LIST=1`: every FP compile;
  - `FP_PICK=<hex>`: writes `run\duck_fp.hlsl`;
  - `PSOMISSDBG=N`: why a pipeline missed;
  - `FP_FAILDUMP=1`: the first FP that fails to decompile.
- Surfaces: `RT_OFFDBG=1` prints each distinct surface setup (format, colour target, offsets, clip).
- `D3D12_DBG=1` turns on the debug layer only if Windows "Graphics Tools" is installed:
  Settings, then System, then Optional features, then Graphics Tools.
- More rendering switches added on 2026-10-20 (runtime):
  - `RSX_FRAMELOG=N`: per-batch records, display draws, vertices, drops and how presenting runs.
  - `STRIP_NATIVE=1`: native strip path (renders nothing yet).
  - `STRIP_DUMP=<frame>`: `[STRIP]` record-side and `[STRIPDRAW]` replay-side dumps.
  - `RSX_VBASE_OFF=1` and `VBASE_DBG=1`: vertex base offset/index.
  - `VP_NO_BRANCH_BASE=1`.
  - `FP_IDCOLOR=1`: a flat colour per fragment program, to see what rasterizes.
  - `CULL_OFF` / `CULL_FLIP` / `DEPTH_OFF`.
- Inspecting captures: use the scripts in `tools\` (README there): `capstat.py` (lit % per RT),
  `capops.py` (`--summary`, `--chain=<RT>`, filters), `capcompare.py` (A/B), `bmp2png.ps1`,
  `capture_match.ps1` (the whole demo-match capture run). They replace the old ad-hoc pixel count.
- Only end-of-frame RT content is saved; to find which op blackens a target you need per-op
  snapshots (backend change, see FINDINGS §20).

