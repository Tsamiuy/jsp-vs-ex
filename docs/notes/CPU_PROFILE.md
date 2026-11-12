# Where the CPU time goes (2026-10-22, read-only first pass)

Goal from the maintainer: understand the 20-50% CPU (spikes >60%, lower during freezes) on a 16-core laptop CPU
(16 cores / 32 threads, so 1 busy thread ≈ 3% of the whole CPU) + laptop GPU, find the
freezes, and say what could move to the GPU to cut CPU use and raise fps (now ~27-29).
Sources: `jstars\run\stderr.txt` (latest run, 22 Oct, 695 `[PERF]` windows), the sampler runs
(`perf\vblank-qpc-2026-10-20`, `perf\main-wait-2026-10-20`), FINDINGS §37-38 (hitches) and the code.
Nothing was run for this; numbers below are measured unless marked *inferred*.

## 1. What the frame is made of now (latest run, steady fight windows ~29 fps, 34 ms/frame)

| present thread (draws + presents), per frame | ms |
|---|---|
| vertex work (`vtx`, ~440k strip vertices at ~40 cycles) | ~8 |
| rest of the FIFO drain (command decode, draw records) | ~9 |
| render_frame (prepass ~3.5, includes the GPU wait) | ~6 |
| idle (`rest`, waiting for the game to hand over the next frame) | ~10 |

- The present thread is busy ~23 ms of 34. Most frames hit 2 vblanks (30 fps), ~30% hit 3.
- The **game's main thread** spends ~37% of its time in its own frame-pacing loop (wait for the
  previous flip, then for a vblank; `func_006507DC`, FINDINGS "main thread waits on the previous
  flip"). So in steady state neither side is saturated: the frame is a hand-off chain
  (game builds frame N → flush → renderer drains/draws N → flip at the next 60 Hz tick).

## 2. Why the CPU % is high even though few threads do real work (code + sampler; *partly inferred*)

The sampler (`PS3_SAMPLE`) showed 95% of all samples "in ntdll" and only ~2% in game code: almost
every thread is waiting. But many of those waits **spin** instead of sleeping, and spinning costs a
full core:
- `sys_timer_usleep` below 1 ms (`runtime/syscalls/sys_timer.c`) loops on `SwitchToThread()` until
  the deadline. The game polls with short sleeps everywhere: the CRI file-server thread
  (`usleep(20)`, ~10,500 calls/s even when idle), the main thread's flip/vblank waits
  (`usleep(200)`, ~1,800/s), the CRI loader waits (`usleep(100)`).
- `sys_ppu_thread_yield`, lwmutex/event-queue short timeouts, cellSync spins and the SPU coherency
  loop also use `SwitchToThread` (`sys_ppu_thread.c`, `sys_mutex.c`, `sys_event.c`,
  `libs/sync/cellSync.c`, `spu_coherency.c`).
- The present thread's idle wait itself spins since `GCM_PUT_POLL=200` became the default
  (checks the FIFO every 200 µs with `SwitchToThread`).
- Each such poller shows ~100% of one core in Task Manager while doing almost nothing. On a laptop
  this also eats the shared power/thermal budget that the 2-3 threads doing real work (present
  thread, async fetch workers, SPU job threads) need to boost. *Inferred*: a few spinning pollers
  explain most of the 20-50%; the per-thread run below will put numbers on each.

## 3. The freezes (measured in the latest run)

28 batch intervals over 87 ms in the run. Two kinds:
1. **Loading / scene switches** (e.g. line 682: 8 fps, `rest` 56%, 540 drain calls with only 12 with
   work): the renderer waits for the game, which is loading. CPU drops because the game threads
   block on file I/O and pacing. Expected; the game also stalls on real hardware, just shorter.
2. **Mid-fight stalls inside the drain** (lines 11942, 11976: 14-17 fps, up to 5 intervals >87 ms
   in one window): `drain` jumps to 0.81 s per 20 frames while `vtx` stays at 0.16 s, so ~0.65 s
   is spent in **untimed drain work** (command decode, recording, blits, readbacks, compiles called
   from the drain). One of them also shows 6 fxc compiles + 29 PSO creates (0.23 s), which explains
   part of one window but not the other. The timers do not split the drain further, so the cause is
   not identified yet. *Candidates (inferred)*: a GPU readback or `wait_for_gpu` inside the drain
   (surface copies to guest memory, luminance readback), a blit of a large surface, or a shader
   compile on the drain path.
3. Texture and shader hitches (FINDINGS §37-38) are now small: shader disk cache and texture pool
   are on; new textures cost ~1 ms each in bursts of 50-230 at scene changes.

## 4. What could move to the GPU (ranked by expected CPU saving)

| # | work now on the CPU | GPU version | saves | size / risk |
|---|---|---|---|---|
| 1 | Vertex fetch/decode: present thread builds lists (~8 ms/frame), 3 async workers decode every vertex into 256-byte slots, ~110 MB/frame written to the upload heap | **Vertex pulling**: upload (or map) the game's raw vertex arrays and indices; the vertex shader reads and converts them (big-endian, F32/half/UB/S16/CMP) itself | most of the 8 ms on the present thread + the workers' cores + upload bandwidth | big: per-format shader variants in the VP decompiler; medium risk |
| 2 | Strip index handling (restart joins, duplicate vertices) | GPU strip cut (`STRIP_INDEXED=2`, drafted, untested) | part of #1 now, folds into #1 later | small; patch ready |
| 3 | Texture decode (unswizzle, 16-bit unpack, BE swaps) on the present thread, ~1 ms per new texture | compute-shader decode from a raw upload | the 50-230-texture bursts at scene changes | medium |
| 4 | Guest memory → GPU copies of whole buffers | `OpenExistingHeapFromAddress` (GPU reads guest RAM directly) | upload copies for #1 and #3 | medium-high: coherence with the game rewriting buffers (§26) |

Not movable to the GPU: the game's own PPU/SPU code (recompiled CPU code, incl. the SPU jobs
`spu_LS_00000968`/`000032F8` that are the hottest guest code), shader compilation, the FIFO walk.
For those the CPU wins are: stop the spinning pollers (§2), time and fix the drain stalls (§3),
and keep compiles off the present thread (`PSO_ASYNC`, exists, opt-in).

## 5. Profiling runs needed (for the AI assistant; all on the current build, a fight on Namek s06 + the demo)

1. **Per-thread CPU** (no code change): run `docs\archive\jstars-notes\tools\thread_cpu.ps1 -Start <fight s>
   -Window 5 -Count 12 -Out thread_cpu.txt` alongside a run with `PS3_SAMPLE=2` and
   `-Commands threads`. Gives real CPU % per thread (spinning counts) and, via the sampler's tid
   lines and the guest call chains, which game thread each one is. Also note Task Manager's total.
2. **Same run with the pollers calmed** (env only, if the switches exist): `GCM_PUT_POLL=0`.
   Compare total CPU and fps. (A sleep-instead-of-spin switch for `sys_timer_usleep` does not exist
   yet; it would need a small patch: user's yes.)
3. **Freeze capture with Windows' own profiler** (no game code change, one build flag): build once
   with debug info (`/Zi` + `/DEBUG` link flag, Release optimisation kept) so the exe has a PDB,
   then `wpr -start CPU -start GeneralProfile -filemode` before a fight, `wpr -stop fight.etl` after
   a freeze. Opened in WPA this gives every thread's CPU by function and, for the freezes, what the
   present thread was doing or waiting on. If WPR is not wanted, a `/Zi` build alone still lets the
   sampler's `[samp] rva` lines be named.
4. If 3 is not possible: a small opt-in timer patch that splits `drain` into FIFO decode / draw
   recording / blits+transfers / readbacks / compiles (`[PERF6]`), to name the mid-fight stalls.
   Code change: needs the user's yes.

## 6. Results of runs 1 and 2 (the AI assistant, 2026-10-22, Namek fight, perf\cpuprof\)
Each run: tours\menu_tour_play.sh to the fight, then thread_cpu.ps1 -Window 5 -Count 12 starting at
fight start, "threads" debug command 20 s in (PS3_DEBUG file), PS3_SAMPLE=2.
- default (GCM_PUT_POLL=200): process total avg 887% of one core (12 windows, 70 threads),
  fps 23.6 (last 40 [PERF] windows). Three threads near 95% each the whole time
  (tids 43204, 7252, 29988), one at ~41% (17876), then ~15 threads at 18-30% each
  (mostly Wait/ExecutionDelay = sleeping in short sleeps / yielding).
- GCM_PUT_POLL=0: avg 863%, fps 24.8. Same shape: two at ~95%, one at ~68-70%, one at 35-45%,
  ~16 at 20-25%. So the put poller costs ~25% of a core; turning it off did not lower fps here.
- "threads" answer (perf\cpuprof\default\threads_answer.txt): tid 29988 is a guest thread
  (func_006507DC+0x188 <- func_0032DDD8 ...), 17876 is in func_0060AE04; 43204 and 7252 show only
  ntdll (host threads, probably the present thread and an SPU/RSX worker). guest-tids 10-16 are all
  in sys_lwcond_wait.
- PS3_SAMPLE=2 found 0 samples in guest code: "[samp] pdata self-check: func_000BD8F0 ... NO .pdata
  entry -- exact mode cannot see guest code". So the sampler can't attribute guest time in this build;
  the /Z7 build (run 3) should help.
- Run 3 prep: jstars\port\build_pdb.bat builds into jstars\port\build-pdb with /Z7 + /DEBUG
  (Release optimisation /O2 /Ob2 kept, no source change): build-pdb\JStarsRecomp.exe (95.9 MB) +
  JStarsRecomp.pdb (183.9 MB). The normal build\JStarsRecomp.exe is untouched. wpr.exe is present
  (C:\Windows\System32). To run the pdb build with the tours, run_test.ps1 would need to start
  build-pdb\JStarsRecomp.exe instead (not changed yet). The WPR capture itself needs an elevated
  prompt, so it is the maintainer's to start.

## 7. Who the three ~95% threads are, and the plan (2026-10-22, read-only)

From `perf\cpuprof\default|putpoll0` (`thread_cpu.txt`, `threads_answer.txt`, the `[samp] tid` lines):

| thread (default / putpoll0 tid) | CPU | evidence | what it is |
|---|---|---|---|
| 29988 / 39424 | ~92-96% | guest chain `func_006507DC <- func_0032DDD8`, `[samp]` guest 24-39, ntdll 43-66 | **the game's main thread**, in its flip/vblank pacing loop. It polls `cellGcmGetFlipStatus` with `usleep(200)`, which this runtime turns into a `SwitchToThread` spin, so waiting costs a whole core |
| 7252 / 48700 | ~95% → ~70% with `GCM_PUT_POLL=0` | in `nvwgf2umx.dll` (NVIDIA D3D12 driver) at dump time; `[samp]` other 20-31 | **the drawing/present thread** (FIFO walk + D3D12 calls). ~25% of it is the put poller's spin |
| 43204 / 34316 | ~95% | in ntdll; the only busy thread with no `[samp] tid` line in either run | *inferred*: **the sampler thread itself** (`PS3_SAMPLE=2` walks all 70 threads every 2 ms: snapshot, suspend, read context, resume; it skips itself). Run A below confirms it |
| 17876 / 49512 | ~40-45% | guest chain `func_0060AE04 <- func_00627878` | a game thread in the common wait wrapper (spinning/yielding) |
| ~15 others | 18-30% each | `Wait/ExecutionDelay`, `[samp]` ntdll ~90 | game/runtime threads in short sleep/yield loops |

So most of the ~870% is waiting that burns CPU: one diagnostic thread, one game thread spinning in
its frame pacing, part of the present thread, and ~15 pollers at ~20% each. The real work is the
present thread's ~70%, the async fetch workers and the SPU job threads.

**WPR is not needed for this.** It would need an elevated prompt *and* Windows Performance Analyzer
to read the trace (not installed here: no WPA/xperf; only `wpr.exe` and `tracerpt.exe`, and
tracerpt does not resolve stacks), i.e. a Windows ADK download. Keep it for the mid-fight freezes
if the cheaper runs below don't explain them.

### Next runs for the AI assistant (no admin, no code change)
- **Run A: confirm the threads, without the sampler.** Use the `build-pdb` exe (needs only the exe
  path switch in `run_test.ps1`), Namek fight, **no `PS3_SAMPLE`**. Run `thread_cpu.ps1` (12 × 5 s)
  and send the `threads` debug command three times, ~10 s apart. Expected: the third ~95% thread is
  gone, and the total drops by ~95%. Every `tid ... in exe rva=0x...` line can then be named
  offline against `build-pdb\JStarsRecomp.pdb` (checked: `llvm-symbolizer
  --obj=build-pdb\JStarsRecomp.exe 0x140000000+rva` resolves to function + file:line).
- **Run B: the freeze.** Same as A, but with the `threads` command sent every 2 s for 60 s (or
  right when a freeze is seen). A stalled drain shows the present thread's position (`exe rva`) in
  the dumps taken during the freeze.

### Fix to propose after A (code change: needs the maintainer's yes)
Make short guest sleeps real sleeps: in `sys_timer_usleep` (< 1 ms) wait on a
high-resolution waitable timer (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`, ~0.5 ms precision on Windows
10/11) and spin only the last few tens of µs, behind a switch. Expected: the main thread drops from
~95% to a few %, and most of the ~15 pollers drop with it, so the total CPU falls by several cores.
fps should stay the same or rise slightly (more boost headroom on a laptop). *Inferred*; the A/B run
measures both.

## 8. Runs A and B (the AI assistant, 2026-10-22, build-pdb, no PS3_SAMPLE, perf\cpuprof\runA|runB)
run_test.ps1 got a switch JSTARS_BUILD=build-pdb (default still build\). Namek fight, thread_cpu
12 x 5 s from fight start; "threads" 3 x 10 s apart (A, threads_00..02.txt) and 30 x 2 s (B,
threads_00..29.txt).
- Run A: avg 809% of one core (63 threads), fps 22.8. Busy: tid 40772 ~94% (only ntdll, host:
  likely the present thread), tid 32224 ~93% (guest main thread: func_006507DC+0xB8B <-
  func_0032DDD8+0x358, once func_00530140 <- func_0053130C <- func_005383D4), tid 10000 ~44%
  (func_0060AE04+0x95 / func_00587004+0xB1). So without the sampler the third ~95% thread is gone
  and the total drops from ~887% to ~809% (the sampler thread was ~80-95%).
- Run B: avg 800%, fps 25.7. First window only 537% (one thread 95%, one 59% Standby). 30 thread
  dumps 2 s apart in runB\threads_00..29.txt for freeze hunting; exe rva lines resolve against
  build-pdb\JStarsRecomp.pdb (llvm-symbolizer is in the VS BuildTools LLVM bin).

## 9. Precise sleep (eee9efe, default on, PS3_PRECISE_SLEEP=0 off), Namek fight, perf\psleep
| run | total CPU (avg of 12 x 5 s) | fps (last 40 windows) | [PERF4] sum <20/20-37/37-54/54-70/70-87/>87 | top threads |
|---|---|---|---|---|
| before (PS3_PRECISE_SLEEP=0) | 778% | 23.6 | 0 / 270 / 419 / 105 / 5 / 1 | 93.6, 90.2, 40.4, 26.7 |
| after (default) | 689% | **28.6** | 1 / 551 / 228 / 19 / 1 / 0 | 91.0, 48.9, 45.5, 29.9 |
- One of the two ~90% threads drops to ~49%; total CPU -0.9 core; fps +5; frames in the 37-54 ms
  bucket roughly halve and 54-70 ms drop from 105 to 19; no frame over 87 ms. Run 3 (MAX_US=50)
  not needed. Loading time to "Now PRESS x" unchanged (17 s both). Screenshots normal (the green
  haze in the after shots is the fight happening high up in Namek's green sky).

## 10. What the remaining CPU does, by function (2026-10-23, read-only)

Method: Run B's 30 thread dumps (`perf\cpuprof\runB\threads_*.txt`, build-pdb, 2 s apart) list, for
each host thread caught in exe code, its RVA. Resolved against `build-pdb\JStarsRecomp.pdb` with
llvm-symbolizer (`perf\cpuprof\runB_rva_symbols.txt`). Shares below are each thread's samples **in
exe code**: ntdll samples are its waits and yields. Run B predates precise sleep, but these
functions do not depend on it. The thread roles match `[PERF]`/§7.

| thread (Run B tid) | top functions in exe code | what it is |
|---|---|---|
| drawing/present (16816) | `rsx_fp_code_hash` 40%, `vp_record_cb` 40%, `vp_get_fp_pso` 11% (plus 49 win32u / 24 NVIDIA driver lines) | per draw: the fragment-program **code hash** (byte-wise FNV over the whole program, for the pipeline lookup) and the **8 KB vertex-constant bank copy** into the per-draw constant buffer |
| main game thread (14060) | `vm_read32_inl` 31%, `spu_coh_is_reserved` 27%, game code | every guest load calls the SPU-reservation check **out of line** (`spu_coherency.c`), a function call per memory read |
| a PPU game thread (18228) | `barrier_watch_hit` 37%, game code, `ppu_gcm_pump` | debug store-watch checks (`PPU_WVAL`/`PPU_WWATCH`) run on **every out-of-line guest store** (`ppu_loader.cpp`) |
| SPU job threads (3672, 12268, 22996, 43132, ...) | lifted SPU job code (`spu_func_000032F8`, `spu_shufb`, `spu_fma`, ...) plus **`spu_workload_fingerprint` 20-36%** | each SPURS job dispatch hashes the **whole job image byte by byte** (FNV) to find its lifted code, ~2,200 jobs/s |
| one SPU thread (21252) | `spu_depth_guard` 55% | a diagnostic ring of recent drain sites, a static array that **every SPU thread writes** on every drain (shared cache line) |
| async fetch workers (35672, 39424, 41520) | `rsx_fetch_plan_vertex`, `rsx_rd_bef`, `rsx_fetch_store` | real work: vertex decode |
| two threads (31304, 50520) | `vmx_flush` 100% (inlined VMX float helpers) | real work: the game's PPU vector maths, done lane by lane |

## 11. Ranked next steps (gain and risk; none applied, code changes need the maintainer's yes)

| # | change | where | expected gain | risk |
|---|---|---|---|---|
| 1 | **Job-image fingerprint cache**: keep each dispatched image's bytes per (address, size); on dispatch `memcmp` against the copy and reuse the fingerprint (FNV only when the bytes differ) | `runtime/spu/spu_workload.c` | removes 20-36% of the SPU job threads' code time; SPU jobs gate the game's frame | low: exact (same bytes, same answer) |
| 2 | **Inline the reservation check** for guest loads (read the armed flag and bitmap in the header, no call) | `runtime/spu/spu_coherency.{h,c}`, `runtime/ppu/ppu_memory.h` | a large part of the main thread's 27%; the main thread is on the frame's critical path | low: same logic, inlined |
| 3 | **Debug hooks off the hot paths**: one global "any watch armed" flag in front of `barrier_watch_hit`, and the `spu_depth_guard` ring per thread or only with its debug switch | `runtime/ppu/ppu_loader.cpp`, `runtime/spu/spu_drain.c` | up to 37% / 55% of the affected threads' code time | low: diagnostics only |
| 4 | **FP code-hash memo**: per program address keep the program bytes and hash; per draw `memcmp` (SIMD) instead of re-hashing | `libs/video/rsx_d3d12_backend.c` (vp_get_fp_pso) | ~40% of the drawing thread's code time, the frame-setting thread | low: exact |
| 5 | **Constant-bank reuse**: when the 512×16 B bank did not change since the previous draw, point the draw at the previous copy instead of copying 8 KB again (needs a "constants changed" counter in `rsx_commands.c`) | `rsx_commands.c`, `rsx_d3d12_backend.c` | most of `vp_record_cb`'s 40% | medium: replay uses the shared slot |
| 6 | **GPU vertex pulling** (decode in the vertex shader) | VP decompiler + backend | the 3 fetch workers + part of the drawing thread | big change, medium risk |
| 7 | VMX float helpers with SSE (as done for SPU float compares, dbe9dba) | `runtime/ppu/ppu_vmx.h` | the two `vmx_flush` threads | medium: bit-exact NaN/denormal rules |

1-4 are exact (same results) and small; together they target most of the non-waiting CPU that is
not real game work, on the threads the frame depends on. Suggested order: 1+2+3 as one runtime
patch (A/B: total CPU, fps, `[PERF4]`, SPU job rate), then 4, then 5.

**No new run needed before patching.** After each patch: the demo fight with `thread_cpu.ps1`
(no `PS3_SAMPLE`), plus, for the build-pdb exe, 30 `threads` dumps as in Run B to re-check the
function shares.

## 10. Runtime overheads (6020215, default on; off = SPU_FP_CACHE=0 PS3_COH_INLINE=0 PS3_FAST_DEBUG_HOOKS=0)
- Free Battle Namek (perf\rto\before|after): before 688% CPU, 24.3 fps, [PERF4] 0/309/417/73/1/0;
  after 761%, 25.5 fps, 0/448/310/23/3/16. Random characters/camera make these vary too much.
- Demo fight, same every time (perf\rto\demo_*; thread_cpu over 360-410 s; fps/[PERF4] over the
  whole 600 s run):
  | run | fight CPU | fps | [PERF4] <20/20-37/37-54/54-70/70-87/>87 |
  |---|---|---|---|
  | before 1 | 672% | 28.9 | 712/14516/2304/158/16/13 |
  | before 2 | 772% | 28.8 | 706/14413/2191/137/17/35 |
  | after 1 | 773% | 28.8 | 712/14347/2239/147/11/43 |
  | after 2 | 747% | 28.9 | 752/14376/2277/181/14/19 |
- No measurable difference in fps or frame-time spread; fight CPU varies 672-773% between
  identical runs, so a change below ~1 core can't be seen this way. Top threads the same
  (~92 / ~50 / ~45-50%). The patch stays on (no harm seen); its effect, if any, is below the noise.

## 12. Scope: decoding vertices on the GPU ("vertex pulling") (2026-10-23, read-only)

### What the vertex time is today (measured: perf\rto\demo_after2\stderr.txt, demo fight)
- Per frame: **~720k output vertices** (14.4-15.3M per 20-frame `[PERF]` window), **~1,100 draws**
  (`pso` 20.5-24k calls per window), `vtx` 0.20 s per window = **~10 ms/frame on the drawing
  thread at 29-30 cycles/vertex**. `drain` ~20 ms (incl. vtx), render_frame ~7 ms, idle `rest`
  ~7 ms. fps 27.8-29.4.
- **The drawing thread does not decode.** With `VFETCH_ASYNC` (default) `upload_strip_vp_native`
  (rsx_d3d12_backend.c ~9464) walks the guest index batch (byte swap, restart, strip joins/parity),
  writes a u32 list of guest vertex indices, and `vfa_fetch_list` (~8815) posts it. The 3
  `vfa_worker` threads decode (`rsx_fetch_plan_range`) into 256-byte slots of `vp_vb`.
  *Inferred*: the workers are tids 25716/35480/10960 in demo_after2\thread_cpu.txt (30-37% each,
  Wait/UserRequest), i.e. **~1 core**.
- Upload traffic: 720k x 256 B = **~185 MB/frame (~5 GB/s)** written to the upload heap and read
  by the GPU over PCIe.
- Per call (one FIFO batch of <=256 indices; *inferred* ~2,000 calls/frame) the drawing thread
  also runs `rsx_fetch_plan_init` -> `vf_input_mask`, which re-scans and word-FNVs the whole vertex
  program. Measured offline (tools\vphash_bench.c, the same loop): **500 / 980 / 1,980 / 4,120
  cycles per call for 64 / 128 / 256 / 512-instruction programs**, so ~1-8 Mcyc of the ~22 Mcyc
  per frame. The rest of the 29 cyc/vertex is the list loop itself.

### What vertex pulling is
Draw with **no input layout**. The vertex shader gets `SV_VertexID` (= the guest vertex index) and
reads the game's own vertex bytes from a raw buffer, doing what `rsx_fetch_plan_vertex` does now:
- per attribute: address = base + offset + index x stride (index / freq or % freq for divided
  streams; `vertex_data_base_offset`/`base_index` folded in on the CPU);
- big-endian loads and the 7 types: F32, half (SF), UB (normalised), UB256, S1 (s16 normalised),
  S32K, CMP (11/11/10); attributes that are off read the constant `vertex_data4f`;
- byte addresses that are not 4-aligned (UB/half at odd offsets): load 2 dwords and shift.

Paired with **D3D strip cuts** (the STRIP_INDEXED idea): the index buffer is the guest's own
indices, byte-swapped to u32 with the restart value turned into 0xFFFFFFFF, drawn as a triangle
strip. No joins, no repeats, no dedup, no 256-byte slots. Per call the drawing thread does a SIMD
swap copy of the indices (with min/max) and copies the guest vertex bytes the batch spans.

### Files
| file | change |
|---|---|
| `rsx_vp_decompiler.c` (VSInput, ~line 565) | a pulling prologue instead of the `ATTRn` inputs: one generic table-driven decoder (no per-format shader variants), only for attributes in the existing input mask |
| `rsx_d3d12_backend.c` | VP root signature (~1376) gets a raw-buffer SRV and a 16-entry attribute table (256 B, in the per-draw CB or root constants); PSOs without input layout and with `IBStripCutValue`; a per-frame byte ring (parity like `vp_vb`) for copied vertex spans + the u32 index ring; record/replay use DrawIndexedInstanced; join/carry/dedup code bypassed when on. **Owned by the AI assistant: delivered as a patch.** |
| `rsx_vertex_fetch.c` | `rsx_fetch_plan_init` stays the one source of attribute layout; a helper turns a plan into the GPU table and the byte span to copy (page by page for MAIN/IO memory, as the CPU fetch resolves it) |
| test | offline: the HLSL decoder run in a compute shader on random bytes for every type, size, alignment and divider, compared **bit for bit** with `rsx_fetch_attrib` |

Size: ~600-900 lines plus the test. Medium risk.

### Risks
- **Exact decode**: S1 normalisation, CMP sign extension, half NaN/denormals must match the CPU
  code exactly. The offline bit-exact test covers it before any game run.
- **Span size**: copying "min..max index x stride" per 256-index batch could cost more than today
  if the batches of one record span a big model buffer. Reuse the span for batches continuing the
  same record with unchanged arrays; fall back to the CPU path above a size cap. **Unknown until
  measured** (a stats line first).
- **Timing of guest data (§26 race)**: bytes are copied at record time, when today's decode reads
  them, so the picture sees the same data. Mapping guest memory straight to the GPU would read it
  later; not proposed.
- Debug paths that read decoded vertices back from `vp_vb` (VTX_POS, RSX_DBG_VTX, DBG_LOCK,
  IDX_DBG) and the non-strip paths (quads, fans, lists) keep the CPU path at first.

### Off switch and picture checks
- `VPULL=1` opt-in until it matches; then default on, `VPULL=0` back to today's path. Any draw the
  pulling path can't take (unknown type, span over the cap, debug switches) uses the CPU path.
- Checks: (1) the offline bit-exact decoder test; (2) demo t360/t380/t400 screenshots vs `VPULL=0`
  (two identical runs already differ 1.5-3.8 in the whole-frame diff, so the bar is "within that");
  (3) Vegetable Sky and Namek by eye (as for STRIP_INDEXED).

### Expected gain (*inferred*)
- **CPU**: the ~1 core of decode workers, and ~185 MB/frame of upload writes and PCIe reads, go
  away; in exchange the GPU decodes (cheap) and reads raw bytes + 4-byte indices.
- **Drawing thread**: `vtx` ~10 ms -> ~1-3 ms (index swap copy ~0.5 ms, span copies, per-call
  setup), **only with the strip cut**; pulling alone keeps the 29 cyc/vertex list loop.
- **fps**: the game flips on 60 Hz ticks and the demo is already at 28.9 fps with ~13% of frames
  at 3 vblanks (`[PERF4]` 37-54 ms: 2,277 of ~17,500). The ceiling from this work is **~30 fps**
  (those frames back to 2 vblanks). Above 30 needs the whole frame chain under 16.7 ms, which
  this alone does not reach.

### Shader-hash memo (item 4) first? Yes, together with a vertex-program memo
- Both are exact, small, local to rsx_d3d12_backend.c, and measurable in one run: the FP code hash
  per draw (~1,100/frame, `22-26 MB hashed` per window, ~1.1 MB/frame) and the `vf_input_mask`
  hash per call (above). Memo: keep the program bytes per address, `memcmp`, reuse the result.
  Expected **~1-4 ms/frame** off the drawing thread, the same order as pulling's drawing-thread
  gain for a fraction of the work. Then constant-bank reuse (item 5).

### Runs for the AI assistant (demo fight, as perf\rto: thread_cpu 360-410 s, fps and [PERF4] over 600 s)
1. **No code:** `FRAME_BUDGET=2` (prints `[BUDGET] peak/300f: N verts (M draws)`, M = upload
   calls): confirms the calls per frame.
2. **No code:** `VFETCH_ASYNC_THREADS=1` vs default. If fps and `vtx` don't move, the decode
   workers are not on the critical path and pulling's fps gain is ~0 (CPU gain only).
3. **No code:** build-pdb at HEAD, 30 `threads` dumps 2 s apart during the fight (as Run B), to
   split the drawing thread's exe time between the list loop, `vf_input_mask`, `rsx_fp_code_hash`
   and `vp_record_cb`.
4. After item 4 + VP memo (needs yes): before/after, same as runtime-overheads.
5. `STRIP_INDEXED=2` (patch drafted, never applied; needs yes): the strip-cut half on its own.

## 13. §12 runs (the AI assistant, 2026-10-23, demo fight, perf\s12)
| run | fight CPU (360-410 s) | fps (600 s run) | vtx s / [PERF] window | [PERF4] <20/20-37/37-54/54-70/70-87/>87 |
|---|---|---|---|---|
| FRAME_BUDGET=2 | 768% | 28.9 | 0.149 | 1038/13291/2158/171/36/65 |
| VFETCH_ASYNC_THREADS=1 | 760% | 29.3 | 0.103 | 890/13969/2608/267/9/16 |
| default (3 workers) | 727% | 29.3 | 0.129 | 997/14907/1809/101/7/18 |
- Run 1: `[BUDGET] peak/300f` up to **600-620k verts (3,041-3,225 draws)** per frame in the fight
  parts, ~444-460k verts (2,287-2,369) elsewhere; never truncated (buffer 1,048,576).
- Run 2: one decode worker instead of three: same fps (29.3), vtx time not higher (0.103 vs 0.129
  s/window), but more 37-54 ms (2,608 vs 1,809) and 54-70 ms (267 vs 101) frames. So the workers
  matter a little for frame pacing, not for average fps.
- Run 3 (build-pdb rebuilt at 6020215 = before the shader-hash memo; 30 "threads" dumps 2 s apart
  during the fight, perf\s12\pdb_threads\threads_00..29.txt, top_symbols.txt): most frequent host
  positions across all threads: rsx_rd_bef<-rsx_fetch_plan_vertex<-rsx_fetch_plan_range (153
  samples, rsx_vertex_fetch.c:14), rsx_fetch_plan_vertex lines 269/311 (30, 28), rsx_fetch_plan_range
  :369 (30), vp_record_cb (rsx_d3d12_backend.c:5294, 30), vp_get_fp_pso (:3445, 29), then game code
  (ppu_recomp_001/017/018) and SPU job code (spujob_813580) ~27-30 each. Counts are over all 69
  threads, so they mix the drawing thread with the decode workers.

## 14. Shader-hash memo (5f73fea, default on, SHADER_HASH_MEMO=0 off), demo fight, perf\memo
| run | fight CPU | fps | prepass s/window | "KB hashed"/window | [PERF4] <20/20-37/37-54/54-70/70-87/>87 |
|---|---|---|---|---|---|
| off | 626% | 29.1 | 0.054 | 16,922 | 429/14933/1790/101/22/64 |
| on | 568% | 29.2 | 0.047 | 17,392 | 699/15094/1911/118/5/12 |
- fps unchanged (already at the 30 ceiling in the demo); prepass ~13% lower; fight CPU -0.6 core
  (within the ~1 core run-to-run noise seen in §10); fewer >87 ms frames (12 vs 64) and 70-87 ms
  (5 vs 22), though that also varies between identical runs. The "KB hashed" counter in [PERF]
  did not drop, so it may count something the memo doesn't skip.
- Pictures normal (stages\labeled\hash_memo.png); the demo fight isn't frame-exact, so shots at the
  same second show different moments.

## 15. VPULL drafted (2026-10-23): `patches/vpull/` (opt-in `VPULL=1`, +449/-6 lines; indexed strips with one interleaved stream; GPU decode bit-exact vs the CPU in an offline D3D11 test). Not run yet; run plan in its README.

## 15. VPULL (279487f, opt-in VPULL=1), demo fight, perf\vpull\off|on1|on2
| run | fight CPU | fps (600 s) | vtx s/window | [PERF4] <20/20-37/37-54/54-70/70-87/>87 |
|---|---|---|---|---|
| VPULL=0 | 609% | 29.1 | 0.132 | 718/14843/2188/115/5/10 |
| VPULL=1 | 687% | **48.3** | 0.077 | 11046/13549/433/27/8/36 |
| VPULL=1 again | 626% | **49.7** | 0.074 | 11814/14486/521/20/5/13 |
- `[VPULL] vertex pulling on: 64 MB byte ring x2 parity`; every stats line `dropped 0`; e.g.
  `records 3800 batches 21850 | indices 4300440 | copied 759.1 MB | CPU path: layout 6990 sparse 0`.
  No shader compile failures.
- Pictures look right (stages\labeled\vpull.png): no missing or broken geometry; the demo fight's
  moments differ between runs as usual.
- vtx time on the drawing thread ~-43%; fps jumps from ~29 to ~49 with ~11k frames under 20 ms.
  NOT yet checked: whether game speed is still correct at ~49 fps (i.e. the game really renders more
  frames, vs. something presenting extra frames or the game running fast). Needs a person to watch.
- The maintainer (2026-10-23) watched the VPULL demo runs: game speed normal. Hidden Leaf: ~30 fps
  with VPULL vs just over 20 without. Leaf, Kameari and Alabasta still slow down at fight start when
  the camera faces a lot of scenery; the occasional freezes are unchanged.
- The maintainer said yes: VPULL default on since b7be61b (VPULL=0 off). Check run perf\vpull\default_check: '[VPULL] vertex pulling on' printed with no env set, no dropped draws.

## 16. Slow maps (Hidden Leaf, Kameari, Alabasta) and the freezes (2026-10-23, read-only)

### The slow-down follows the number of draws per frame
Stage survey (stages\sNN\stderr.txt, 2026-10-21 build, last 20 [PERF] windows of each fight;
"draws" = [PERF] `pso ... calls` / 20, one per draw record):

| stage | fps | draws/frame (max) | k vertices/frame |
|---|---|---|---|
| s07 Hidden Leaf | 17.8 | 2,659 (3,145) | 704 |
| s11 Alabasta | 19.5 | 1,963 (2,160) | 645 |
| s14 | 19.2 | 1,910 (2,109) | 676 |
| s01 Kameari | 20.2 | 1,766 (2,091) | 619 |
| s10 Inferno Crags | 21.2 | 1,752 (2,230) | 565 |
| s06 Namek | 21.6 | 1,595 (1,959) | 799 |
| s04 Vegetable Sky | 22.9 | 1,727 (1,813) | 794 |
| s09 Dark Tournament | 24.2 | 1,476 (1,819) | 444 |
| s05 Penguin Village | 29.2 | 1,329 (1,444) | 483 |
| s02 Tenraikyo | 29.3 | 1,052 (1,504) | 375 |

- The 3 slow maps are the 3 with the most draws. On Hidden Leaf the draw count jumps from
  ~850 to 2,400-3,100 per frame when the fight starts (windows 431 -> 432 in s07), with vertices
  about the same (12-14M per window). So "camera faces lots of scenery" = many small draws.
- Per draw on the drawing thread (same logs; s07 and s05 agree): FIFO drain without vertex work
  ~6.5 µs, prepass SRV setup ~2.5 µs, pipeline lookup ~0.8 µs, texture setup ~1.5 µs (3 texture
  binds per draw), plus replay. Fitting frame time against draws across stages gives ~15 µs per
  draw in all (*inferred*). At 2,700 draws that is ~40 ms, i.e. under 30 fps no matter how fast
  vertices are. VPULL removed most of the vertex part (Leaf ~20 -> ~30 fps, the maintainer); the per-draw part
  is what is left.
- **Other maps:** the ones next in line by draw count (Inferno Crags, Namek, Vegetable Sky, s14)
  should improve with the same fix; Penguin Village and Tenraikyo are already at 30.

### The freezes are pipeline builds on the drawing thread
perf\vpull\on1 (demo, VPULL=1, current build): of the [PERF4] windows with a frame over 70 ms,
nearly all have `[PERF5] pso create` > 0 in the same window. Over the run: **642 pipeline
creations, 17.3 s, ~27 ms each**, plus 81 first-time shader compiles (fxc 0.2-0.6 s each; 539
disk-cache hits). Windows with 1-5 creates have 1-2 frames over 87 ms; the fight-load window had 185
creates (7.4 s) + 46 compiles (12.9 s). `PSO_ASYNC` (build pipelines on worker threads, FINDINGS §39:
a 344-miss burst ran at 18.9 fps instead of 1.9) exists but is off (`async posted 0`). New pipelines
keep appearing mid-fight because the key includes blend/cull/mask/depth state, so a new combination
(a special move, a new camera angle) costs ~27 ms the first time in every run. The D3D12 pipeline
is not cached across runs (only the shader bytecode is).

### Candidate fixes (none applied; code changes need the maintainer's yes)
1. **Freezes:** `PSO_ASYNC=1` by default (exists, tested in §39; a new object may be missing for a
   frame or two the first time). Later: a pipeline library on disk (`ID3D12PipelineLibrary`) so
   pipelines load in ~1 ms from the second run.
2. **Per-draw cost:** constant-bank reuse (§11 item 5: 8 KB copy per draw), a memo of the inline
   fragment constants' positions (`rsx_fp_extract_consts` walks the program every draw), SRV
   descriptor reuse for an unchanged texture set, and whatever the function split below shows in
   the FIFO drain. Expected (*inferred*): ~15 -> ~10 µs per draw, Hidden Leaf ~2,700 draws -> ~27 ms.

### Runs needed from the AI assistant (current HEAD, no code)
1. Free Battle on Hidden Leaf, Kameari and Alabasta, `PERF=1 FRAME_BUDGET=2`, thread_cpu.ps1
   (as the stage survey tour). Gives today's per-draw split with VPULL and the shader memo.
2. The same Hidden Leaf fight with the build-pdb exe and 40 `threads` dumps 1 s apart from fight
   start, to name the per-draw functions on the drawing thread.
3. `PSO_ASYNC=1` on the same 3 maps and the demo: count frames over 87 ms and windows with
   `pso create`, and check screenshots for missing objects.

## 17. The fps counters count render batches, not game frames (2026-10-23, read-only)
The maintainer: on Hidden Leaf with VPULL the title-bar FPS stays ~30-40, yet the game itself slows
down when the camera faces the city and runs at normal speed when it turns away.
- **What the counters count:** the title-bar FPS (`s_tframes`, rsx_d3d12_backend.c ~8058) and the
  `[PERF]` fps (one per render_frame, ~7650) both count *render_frame calls*, i.e. batches,
  including mid-frame flushes that present nothing. They equal the game's frame (flip) rate only
  when there is one batch per flip.
- **Before VPULL there was one batch per flip:** `[FRAMELOG]` in stages\s07 (Hidden Leaf, every 60
  batches) shows +60 batches / +60 flips throughout the fight (records 1,235-1,606 per batch);
  likewise psleep\after and cpuprof\runB. So the old "18 fps on Hidden Leaf" was the real game rate.
- **With VPULL, frames are split:** VPULL copies each 256-index batch's whole vertex span. In
  perf\vpull\on1 that is **median 934 MB per 20-batch window (~47 MB per batch, ~190 bytes per
  index; max 1,112 MB)**, against a 64 MB ring that forces a mid-frame flush at 56 MB. The same run
  has `mid-frame render` time in most windows (VPULL=0: almost none). So a heavy game frame becomes
  2 batches: the counter shows ~2x, and each flush also waits for the GPU. **The demo's "48-50 fps"
  with VPULL is therefore batches per second, not game frames** (*inferred*; the game flips are not
  in those logs). The maintainer saw normal game speed there because the demo's real rate stayed ~30.
- **The slow-down is real game slow-motion:** the game advances its simulation per frame, so when
  the city is in view and the frame takes over 33 ms, the game runs slower (*inferred* from the maintainer's
  observation plus the above), while the split batches keep the counter at 30-40.
- Corrections to §16: "draws" there were `pso` calls, about 2x the draw records (s07 FRAMELOG:
  1,250-1,600 records per frame), so per-record costs are ~2x the per-"draw" figures. The ranking of
  maps stands.

### Measure (the AI assistant, no code): Hidden Leaf fight, look at the city, then away
`GCM_FLIPCOUNT=1 RSX_FRAMELOG=20 PERF=1`, VPULL default (on), then the same with `VPULL=0`.
- `[flip] N at T ms` (every 10th flip): the game's real frame rate over time.
- `[FRAMELOG] batch ... records=... flips=...`: batches per flip (above 1 = split frames) and draw
  records per frame.
- `[VPULL] ... copied MB` per window, `[PERF2]` mid-frame render time.
- Note the times (or take screenshots) when the camera faces the city and when it faces away.

### Fixes to propose after that run (need the maintainer's yes)
1. Count flips in the title bar and `[PERF]` (the real frame rate), keeping batches as a second number.
2. VPULL copy size: copy each vertex range once per record or frame instead of per 256-index batch,
   and/or a bigger ring, so heavy frames are not split (and no GPU wait mid-frame).
3. Then the per-draw cost (§16) for the remaining time when the city is in view.
- Play observation (2026-10-23 15:49), Kameari run: slows down AND freezes much more than Hidden Leaf, feels worse to move around; the fps counter reached 89 in a character intro.

## 18. Kameari: more freezes than Hidden Leaf (2026-10-23, read-only)
The maintainer: Kameari also slows down, freezes much more and is not nice to move around in; the
counter showed 89 fps in a character intro with VPULL (= batches, §17; an intro can have several
batches per flip, so it does not show a real 89).
Stage survey, fight windows only (stages\s01|s07|s11, [PERF] windows with >25k pso calls, 2026-10-21
build, before TEX_POOL became default):

| stage | fight windows | new pipelines | textures created | worst windows |
|---|---|---|---|---|
| s01 Kameari | 41 | 48 | **3,462** | 583 created + 5 pipelines -> 10.8 fps; 323 created -> 17.9 |
| s07 Hidden Leaf | 41 | 30 | 1,863 | 121 created -> 15.4 |
| s11 Alabasta | 44 | 30 | 1,877 | 412 created + 4 pipelines -> 11.8 |

- Kameari creates ~2x the textures of the other two, in bursts of 150-583 per window, and is the
  only one with `no-slot 128` in [PERF3] (w540): all 512 texture slots in use, so binds got no
  texture and textures were evicted and re-created as the camera moved (*inferred*: the street has
  more distinct textures in view than there are slots). Each create+upload ~0.7-1 ms on the drawing
  thread, so a burst of 300-600 is a 0.2-0.6 s freeze.
- So Kameari's freezes are texture churn plus pipeline builds; Hidden Leaf's slow-down is mostly
  per-frame work (draws, VPULL split frames).
- TEX_POOL (default since eea13f6) reuses resources of the same size, which cut creates ~25x on
  Namek (FINDINGS §43 area), but it adds no slots: if Kameari needs >512 textures in view it still
  churns. Needs today's [PERF3] on Kameari to tell.

### Add to the AI assistant runs
Kameari fight with `GCM_FLIPCOUNT=1 RSX_FRAMELOG=20 PERF=1` (as the Hidden Leaf run), and the same
with `PSO_ASYNC=1`. Compare [PERF3] `new` / `created` / `no-slot` per window, [PERF5] pso creates,
[PERF4] >87 ms, and the [flip] rate when moving the camera. If `no-slot` > 0 or `created` stays in
the hundreds: the fix is more texture slots (VP_TEX_SLOTS 512 -> 2048, a code change).

## 19. Slow-map runs (the AI assistant, 2026-10-23, HEAD b7be61b incl. VPULL default; raw in perf\scratch\slowmaps)
fps = [PERF] batch rate (counts render batches, see §17), last 60 windows; CPU = thread_cpu over the fight.
| run | fps | 70-87 / >87 ms | windows with pso create>0 | tex new / created / no-slot | peak draws | CPU |
|---|---|---|---|---|---|---|
| Hidden Leaf | 42.1 | 8 / 15 | 43 | 1,646 / 1,153 / 0 | 2,883 | 536% |
| Kameari | 47.0 | 26 / **64** | 67 | **10,874 / 7,943 / 6** | 2,337 | 564% |
| Alabasta | 44.4 | 15 / 38 | 72 | 2,911 / 1,617 / 0 | 2,787 | 519% |
| Hidden Leaf PSO_ASYNC=1 | 30.3 | 11 / 23 | 48 | 1,517 / 1,030 / 0 | 3,282 | 423% |
| Kameari PSO_ASYNC=1 | 47.7 | 16 / **16** | 61 | 10,799 / 8,587 / 0 | 2,291 | 564% |
| Alabasta PSO_ASYNC=1 | 33.5 | 14 / 19 | 74 | 4,584 / 1,719 / 0 | 2,960 | 547% |
| demo fight | 51.3 | 20 / 22 | 65 | 1,212 / 779 / 0 | - | - |
| demo fight PSO_ASYNC=1 | 43.0 | 4 / 12 | 67 | 1,235 / 835 / 0 | - | - |
- Kameari uploads ~4-7x the textures of the other maps and is the only one to hit no-slot (6).
- PSO_ASYNC=1 cuts >87 ms frames on Kameari 64->16 and Alabasta 38->19 (demo 22->12); no missing
  objects seen in its screenshots (perf\slowmaps\async_check.png). Its batch-fps numbers are lower,
  but fps here is batches, not game frames, so compare with the [flip] runs below.
- build-pdb at HEAD, Hidden Leaf, 40 "threads" dumps 1 s apart from fight start:
  perf\scratch\slowmaps\s07_pdb\threads_00..39.txt (not yet symbolized).
- Play observation: Hidden Leaf presented fps ~30-40 but the game slows when facing the city;
  Kameari slows AND freezes far more.

## 20. Real game frame rate ([flip], GCM_FLIPCOUNT=1 RSX_FRAMELOG=20), perf\scratch\slowmaps\*flip*
- **Demo fight, VPULL on vs off, twice each** (flips/s in 10 s windows of the flip clock 280-440 s):
  on1  29.8 27.2 29.0 29.6 24.3 26.2 24.8 24.9 27.5 29.2 29.1 29.5 30.0 28.5 29.9 29.9 (whole 28.6)
  off1 29.6 29.5 28.6 28.7 29.0 29.0 28.8 22.7 28.8 29.4 29.5 29.2 30.0 29.8 28.6 29.7 (whole 28.5)
  on2  29.7 29.6 29.6 29.6  6.3 29.1 26.0 29.0 27.0 27.5 29.0 29.5 30.0 29.7 29.8 27.1 (whole 28.5)
  off2 27.6 28.7 30.0 30.0 28.9 28.1 28.7 29.2 29.3 29.1 28.8 28.8 29.6 29.7 26.9 29.3 (whole 26.6)
  Batches per flip: **1.59 with VPULL, 1.04 without.** So the "29 -> 49 fps" in §15 was the batch
  counter: VPULL splits frames into more batches; the real frame rate is the same (~28.5-29).
- **Hidden Leaf fight** (95 s, random characters): VPULL on 22.9 flips/s, off 25.0; batches/flip
  1.41 vs 1.00. With VPULL the rate dips to 12-14 flips/s for ~40 s (city view per the maintainer).
- **Kameari** (VPULL on): 2.02 batches/flip; flips/s per 5 s at fight start 16 14 4 10, then ~26.
  With PSO_ASYNC=1: 1.99 batches/flip; 20 14 14 20, then 26-28 (fewer deep drops).
- Conclusion: VPULL does not raise the real frame rate (demo: equal; Hidden Leaf: possibly lower);
  it cuts drawing-thread vtx time but adds ~0.5 batch per frame. PSO_ASYNC=1 softens Kameari's
  fight-start freezes. Real slow-downs (12-14 flips/s on Hidden Leaf's city view) remain.
- 2026-10-23: VPULL back to opt-in (a7b6501; VPULL=1 turns it on). Kept because lower CPU
  use was seen on Hidden Leaf with it on (CPU often <20%, up to 30%, vs 20-30%+ off; GPU 30-40% vs
  30-50%). Try VPULL=1 in later speed tests too.

## 21. Destruction slow-downs (2026-10-23, read-only)
The maintainer: when buildings or other things get destroyed in a fight, the game slows down or stutters.
- **No log has a destruction moment marked yet**, so this is about what the existing runs can and
  can't tell. Every stall in the AI assistant's slow-map runs (perf\scratch\slowmaps, fight windows, a frame
  over 70 ms in [PERF4]) falls into one of three kinds (a local classify.awk, not in the repo):
  | run | pipeline builds | texture bursts (>=50 created) | "other" |
  |---|---|---|---|
  | Kameari | 5 windows (23 frames) | 2 (5) | 0 |
  | Kameari PSO_ASYNC=1 | 1 (1) | 0 | 0 |
  | Hidden Leaf | 2 (2) | 0 | 1 (1) |
  | Hidden Leaf PSO_ASYNC=1 | 1 (1) | 0 | **5 (11)** |
  | Alabasta | 6 (11) | 0 | 1 (2) |
  | Alabasta PSO_ASYNC=1 | 4 (8) | 0 | 1 (4) |
- The "other" stalls: `drain` rises from ~0.30 to 0.46-0.69 s per 20 batches with the same draw
  count, vtx, textures and no pipeline build (e.g. s07_async w602-604, three windows in a row at
  22-25 fps). That is renderer work [PERF] does not time: FIFO decode, record building, blits and
  transfers, readbacks (CPU_PROFILE §3 saw the same before). A destruction that streams new meshes
  or textures into VRAM through RSX copies, or spawns many new objects, would show exactly like this
  (*inferred*). Semaphore acquires don't block in the drain (cellGcmSys.c ~1920), so it isn't the
  renderer waiting for the game.
- The other likely costs of destruction: new debris materials (pipeline builds; PSO_ASYNC=1 helps),
  new textures (texture bursts), more draws while debris is on screen (§16), and the game's own
  physics on SPU/PPU threads. If the physics is the cause, the drawing thread waits instead: [PERF2]
  `rest` goes up, not `drain`.

### Run for the AI assistant
The maintainer plays one fight on a stage where buildings break (the one where the maintainer saw it), destroying
things on purpose, and notes roughly when (or the AI assistant takes a screenshot every 5 s). Build-pdb exe:
- `GCM_FLIPCOUNT=1 RSX_FRAMELOG=20 PERF=1 PSO_ASYNC=1` (PSO_ASYNC removes the pipeline stalls so
  others stand out; VPULL off, the default).
- thread_cpu.ps1 for the whole fight (SPU job threads and the main thread spiking = game physics).
- `threads` dumps every 2 s during the fight (as Run B): the drawing thread's function during a
  stall names the untimed drain work.
Read out per stall: [flip] rate (the real slow-down), [PERF2] drain vs rest, [PERF3] created,
[PERF5] pso create/fxc, draws, and which threads spiked.

If the "other" kind dominates and the dumps don't name it, a small timing patch (a [PERF6] line that
splits the drain into decode / record / copies / readbacks) would. That is a code change for the maintainer.

## 21. Kameari destruction run (2026-10-23 20:30, build-pdb at a7b6501+PSO_ASYNC default, VPULL off)
The maintainer played Ichigo on Kameari and broke buildings/cars on purpose; fight ~0-135 s (lost), results
by 185 s. GCM_FLIPCOUNT=1 PERF=1 PSO_ASYNC=1, thread_cpu 38 x 5 s, `threads` every 2 s (94 dumps,
dbg.txt.out is cumulative, so threads_NN holds dumps 0..NN), screenshots every 5 s.
Raw: perf\scratch\destruction\kameari; overview picture perf\destruction\kameari_overview.png.
- Real frames/s per 5 s, fight: 22 20 18 20 24 20 20 22 20 20 18 18 20 22 28 22 20 18 20 18 18 24 26
  26 20 18 18 (avg ~20.6; whole 258 s 21.6). **No deep freezes in this run**: worst 10-flip groups
  1.1 s (95 s, 210 s), 0.8 s (164 s, 102 s); the rest <= 0.74 s. Slow all the time, not spiky.
- Destruction raises CPU, not the stall: process total 640-860% of a core, peak **1,113%** at 115 s
  (car smashed, debris flying) with 26 frames/s in the same window.
- Busiest threads: tid 39132 **92% every window** (host only: ntdll / nvwgf2umx / win32u = the
  drawing/present thread); guest main thread 23412 46% (func_006507DC <- func_0032DDD8,
  func_005383D4); 10996 41% (func_0060AE04+0x95, func_0064FAB0); ~5 more at ~20% (SPU/pollers).
- Drawing thread per 20 batches: vtx 0.19-0.28 s for **10-13.5 M vertices (~650k verts/frame, CPU
  decode at ~45 cyc/vert, ~13 ms/frame)**, prepass 0.07-0.20 s (srv 0.05-0.16 s), tex 0.02-0.11 s
  (95k tex calls, all memo/csum-same, no new textures), pso ~0.02 s. Pipelines: ~150 built at fight
  load, then only a handful; async skipped up to 70 draws in one window (no visible holes).
- Conclusion: Kameari's slowness is the drawing thread being full (~50 ms/frame), mostly CPU vertex
  decode of a very heavy city + SRV setup, not shader/texture hitches. VPULL=1 moves exactly that
  vertex work to the GPU, so Kameari is the place to try VPULL=1 next (real flips, same route).

## 22. Kameari destruction run with VPULL=1 (2026-10-23 20:44, same build/route/switches as §21)
The maintainer played Goku this time and the fight ran the whole 190 s (run §21 ended at ~135 s), with
buildings broken (picture: perf\destruction\kameari_vpull_overview.png; raw perf\scratch\destruction\kameari_vpull).
| first 135 s of fight        | VPULL off (§21) | VPULL on |
| real frames/s (avg)          | 20.7 | **24.2** |
| frames/s per 5 s, range      | 18-28 | 16-30 (mostly 22-28) |
| worst 10-flip group          | 1.13 s | 0.94 s |
| process CPU (avg of 27 x 5 s)| 776% of a core (peak 1,113%) | **637%** (peak 779%) |
| drawing thread               | 92% | 86% |
| guest main / func_0060AE04   | 46% / 41% | 43% / 41% |
| per 20 batches vtx/render/prepass | 0.23 / 0.18 / 0.12 s | 0.09 / 0.11 / 0.05 s |
- On this heavy map VPULL helps for real: ~+17% real frame rate and ~1.4 cores less CPU (matches
  the lower CPU use seen on Hidden Leaf). Not a strict A/B (other character, longer fight,
  different destruction), so a second pair would firm it up. Demo (light map) was equal (§20).

## 23. VPULL on/off, Hidden Leaf and Alabasta, real flips (2026-10-23, build b8c179a, automatic fights, 120 s, tools\flipsum.py)
| run | flips/s | min 5 s | 10-flip p50/p95/max ms | groups > 600 ms | batches/flip | CPU |
| leaf VPULL=1 #1 | 23.2 | 16 | 360/625/672 | 21 | 1.49 | 536% |
| leaf VPULL=1 #2 | 20.5 | 16 | 516/656/781 | 30 | 1.75 | 494% |
| leaf VPULL=0 #1 | 27.2 | 22 | 344/468/516 | 0 | 1.00 | 676% |
| leaf VPULL=0 #2 | 20.2 | 16 | 500/531/1032 | 7 | 1.00 | 593% |
| alab VPULL=1 #1 | 28.4 | 24 | 344/406/844 | 1 | 1.03 | 635% |
| alab VPULL=1 #2 | 23.7 | 16 | 406/532/890 | 8 | 1.54 | 541% |
| alab VPULL=0 #1 | 26.3 | 20 | 375/484/953 | 3 | 1.00 | 634% |
| alab VPULL=0 #2 | 24.1 | 18 | 390/531/1375 | 5 | 1.00 | 702% |
- Hidden Leaf: VPULL avg 21.9 vs 23.7 flips/s and clearly more uneven frames (51 vs 7 slow
  10-flip groups); stutter follows batches/flip > 1.4. Alabasta: about equal (26.1 vs 25.2).
  CPU is always lower with VPULL (~1 core). Kameari (§22) was better with VPULL.
- Decision per the working rule: VPULL stays opt-in (worse on Hidden Leaf). The vpull-once
  patch (c1ef556, aims at 1.0 batches/flip) is being tested next; re-check after it.

## 24. Patches 1-4 (2026-10-24, build 77c4672: draw-cost 32d70fc, tex-slots ff7e593, fps-flips 9dee1ca, vpull-once c1ef556, + prim-log off)
Automatic 120 s fights (random characters, so ±3 f/s between runs of the same map, Hidden Leaf flips
between a ~20 and a ~27 f/s mode), demo last 160 s. new = defaults, old = FP_INFO_CACHE=0 TEX_SLOTS=512,
newv = defaults + VPULL=1 (VPULL_ONCE on). Raw perf\scratch\drawcost.
| run | flips/s | min 5 s | 10f p95/max ms | >600 ms | b/f | tex created / no-slot |
| kam new  | 24.5 | 20 | 516/641 | 1 | 1.00 | 1,872 / 0 |
| kam old  | 24.1 | 20 | 531/750 | 6 | 0.99 | 5,014 / **4,429** |
| kam newv | **28.5** | 26 | 391/484 | 0 | 1.00 | 1,530 / 0 |
| leaf new | 20.8 | 18 | 531/546 | 0 | 1.00 | 1,140 / 0 |
| leaf old | 27.4 | 22 | 453/500 | 0 | 1.00 | 1,469 / 0 |
| leaf newv| 22.2 | 16 | 578/656 | 6 | 1.00 | 1,130 / 0 |
| leaf newv VPULL_ONCE=0 | 24.3 | 16 | 594/671 | 10 | 1.52 | |
| alab new | 25.8 | 20 | 500/610 | 1 | 1.00 | 1,593 / 0 |
| alab old | 26.6 | 22 | 485/546 | 0 | 1.00 | 2,883 / 0 |
| alab newv| **28.2** | 20 | 437/641 | 1 | 1.00 | 1,600 / 0 |
| demo new | 28.2 | 20 | 485/563 | 0 | 1.00 | |
| demo old | 28.9 | 24 | 406/515 | 0 | 1.00 | |
| demo newv| **29.6** | 24 | 360/437 | 0 | 1.00 | (VPULL copied 289 MB) |
| demo newv VPULL_ONCE=0 | 27.5 | 14 | 579/703 | 21 | 1.57 | (copied 1,022 MB) |
- tex-slots: Kameari no-slot 4,429 -> 0 and 2.7x fewer texture creations. Clear win.
- draw-cost: [FPINFO] ~95-99% hits; per-batch prepass/render not measurably lower in these noisy
  fights (Leaf new vs old differ mostly in vtx = different geometry). No gain shown, no loss shown.
- vpull-once: batches/flip 1.5 -> 1.00, VPULL copy 3.5x less, stutter groups 21 -> 0 on the demo.
  With it VPULL=1 is the best config on Kameari, Alabasta and demo, and above "new" on Leaf.
- **Glitch**: the maintainer saw a big flat triangle over the top-right quarter of the screen (cream on the
  Akainu intro card, black in a Hidden Leaf fight) in demo_new only, not in demo_old. Hunt runs in
  perf\scratch\trianglebug (demo x2 new, FP_INFO_CACHE=0, TEX_SLOTS=512; screenshots every 5 s).

## 22. Review: the flat triangle on the Akainu intro card with draw-cost + tex-slots (2026-10-24, read-only)
The AI assistant: with the patches on defaults, a big flat triangle covers the top-right
quarter of the screen on the Akainu intro card (Hidden Leaf demo). It is gone with
`FP_INFO_CACHE=0 TEX_SLOTS=512`. There are no [VPFB] lines in either run.

**draw-cost: no change that can move geometry, by code review.**
- Vertex positions come from the VP constants (`vp_c` bank) and the VS. The patch changes neither:
  - the bank is still the same 8 KB memcpy;
  - `c00` is read from the copy's source `st->vertex_constants[0][0]`. Nothing writes `dst`
    between the memcpy and that read (checked lines 5478-5571), so the value is identical;
  - it only feeds the projection fallback, which logs [VPFB] and didn't fire.
- The fragment side: the hash, gamma/TXP masks and inline constants come from `fp_info`.
  - Every value is the original function's result for the same instruction bytes. The offline test
    found 0 mismatches in 400k calls, with programs moved, edited and re-patched.
  - The cache is used only on the drawing thread; the PSO_ASYNC workers don't call it (pso_worker
    only builds the PS and the pipeline).
  - The alpha-test values (`ts[16..19]`) are unchanged, and the switches are only read once.
  - The one behaviour change is that TONEMAP's log/override now needs at least 2 constants. That
    only affects the tone-map pass, not a shape.

**tex-slots is the likely cause (*inferred*).** More slots change which textures a draw gets:
- A texture now stays in its slot longer. A re-bind at the same address/size/format is trusted
  when its *sparse* checksum matches (`csum-same`). An image the game rewrites in place (intro
  cards are composited from such surfaces) can then show its old contents for longer. With 512
  slots, eviction forced fresh uploads more often.
- A draw that used to find no slot drew with a null SRV: untextured, and discarded if alpha-tested.
  With more slots it gets its texture and becomes visible. A full-screen/card layer drawn as one
  triangle of a quad, or a card's diagonal panel, would then show as a flat triangle (*inferred*;
  Hidden Leaf showed no-slot 0 in §19, but intro cards weren't in that count).
- The SRV heap layout moved (RT SRVs start at 2049 instead of 513). Every use goes through
  `RT_SRV_BASE`/`DRAW_SRV_BASE`/`SRV_HEAP_SIZE` (checked), so no stale index.

**To isolate (the AI assistant):**
1. `TEX_SLOTS=512` alone vs `FP_INFO_CACHE=0` alone on the demo.
2. If it's tex-slots: `TEX_VERIFY=frame` (full-frame texture verification) and `TEX_POOL=0`, to tell
   stale content from slot reuse.
3. An `RSX_CAP` capture of that intro frame (tools\capdraws.py) to name the draw (its FP, texture
   and RT).
- 2026-10-24: VPULL on by default again (5a77662, VPULL=0 off), per the maintainer's yes, after vpull-once.
- Triangle glitch hunt (perf\scratch\trianglebug, +699 MB, tools\tri_scan.ps1 finds it in the maintainer's two
  pictures): demo x2 defaults, FP_INFO_CACHE=0, TEX_SLOTS=512; screenshots every 5 s 40-595 s
  (covering the Akainu card and the Hidden Leaf fight): **not seen in any of the 4 runs.** Rare or
  triggered by something else; unresolved.

## Pause point (2026-10-24 01:2x, the maintainer: continue tomorrow)
- With the AI assistant: draw-cost, tex-slots, fps-flips, vpull-once (patches\*, run plans in their READMEs).
- Open: the Akainu intro triangle (§22): isolate TEX_SLOTS=512 vs FP_INFO_CACHE=0, then
  TEX_VERIFY=frame / TEX_POOL=0 and an RSX_CAP of the frame. Destruction run (§21) not done yet.
- Tools saved: tools\symdumps.py (symbolize `threads` dumps per thread: `python symdumps.py <dir>
  <build-pdb exe> <out>`), tools\stall_classify.awk (classify >70 ms windows: pipeline / textures
  / other). Scratch clone branches (temp, may be lost): drawcost, texslots, fpscount, vpullonce,
  slowfix (all four together) on b8c179a.

## 25. CPU heat on Hidden Leaf: per-thread picture and plan (2026-10-25, read-only)
Report: Hidden Leaf with VPULL on, total CPU ~20% (sometimes 30-40%), but the CPU stays hot even at low total use.
The machine is a laptop with 16 cores / 32 threads (nvidia-smi for the GPU).
- **20% of 32 threads is ~6.5 cores busy.** From thread_cpu in perf\scratch\drawcost (build 77c4672,
  fight windows):

  | run | busy pattern |
  |---|---|
  | leaf_new | 1 thread ~95% (always Running), 2 at ~35%, 3 at ~21% (Wait/UserRequest), **~20 threads at 10-17% each in Wait/ExecutionDelay** (short Sleep loops); total ~617% |
  | leaf_newv (VPULL) | 1 at ~88%, 2 at ~37%, then ~20 at ~17% in ExecutionDelay; total ~587% |

  So about 3 of the ~6.5 busy cores are many threads that each wake very often and sleep briefly
  (ExecutionDelay = Sleep/NtDelayExecution). Plus one thread pinned near 100%. Frequent wake-ups
  and a pinned thread keep cores at high boost clocks, which is what drives a laptop CPU to its
  thermal limit even at low total use (*inferred*).
- From the Hidden Leaf pdb dumps (§19, s07_pdb), the threads caught in exe code are:
  - the game's main PPU thread (game code, `vmx_vmaddfp`: vector math done lane by lane);
  - the drawing thread;
  - ~10 SPU job threads in lifted SPU code (`spu_fma`/`spu_fm`: real work, scalar float
    emulation);
  - the vertex-fetch workers, `yz_lockstep_tick`, `spu_lockline_unlock`.
  The ExecutionDelay threads are waiting when caught, so their wake loop isn't named yet.
- Candidates in code for many short sleeps:
  - `Sleep(0)` in `sys_spinlock_lock` (ppu_sysprx.cpp ~809), `cellSpursJoinJobChain` (cellSpurs.c
    ~2818), the raw-SPU mailbox write (spu_channels.c ~835) and `ts_lock` (spu_workload.c ~424);
  - `Sleep(1)` polls in `jg_wait` (cellSpurs.c ~2553), audio (cellAudio.c ~392/563) and
    lv2_register;
  - the SPURS kernel thread's 1 ms tick (cellSpurs.c ~1698);
  - the 10 ms channel-wait timeout (spu_channels.c ~977);
  - guest `usleep` loops through precise sleep.
  Which of these the ~20 threads sit in needs the run below.

### New tool: tools\thread_cpu2.ps1 (read-only, no admin)
Per thread: CPU % (user+kernel), kernel %, **wakes per second** (context switches), the **entry
point** (module+rva, exe rvas named with llvm-symbolizer and the exe's PDB), and the wait state.
Polling threads show up as many wakes/s and high kernel %, real work as user % with few wakes.
Tested on a PowerShell process: 42,061 wakes/s and 35.5% kernel for a busy pipeline thread.

### Run for the AI assistant (Hidden Leaf fight, VPULL default on, build-pdb exe so entry points resolve)
1. `tools\thread_cpu2.ps1 -Start <fight start> -Window 5 -Count 12 -Top 40` (output thread_cpu2.txt).
2. At the same time: `nvidia-smi --query-gpu=timestamp,utilization.gpu,power.draw,temperature.gpu,clocks.gr --format=csv -l 1 > gpu.csv`.
3. `GCM_FLIPCOUNT=1 PERF=1` for the real frame rate, and 20 `threads` dumps 2 s apart.

### Ranked ideas so far (*inferred*, to confirm with that run; code changes need the maintainer's yes)
1. Replace the hot short-sleep/yield loops the run names with real waits (events/condition
   variables, or wait-on-address on the guest word). The aim is to cut most of the ~3 cores of
   wake-ups without changing timing, which a spin can't guarantee anyway.
2. If the ~95% thread is a wait loop (e.g. the main thread's flip/vblank pacing, §7), make it block
   on the flip event instead. If it is the drawing thread doing real work, it stays.
3. Lifted SPU and VMX float math done lane by lane (`spu_fma`, `vmx_vmaddfp`): SSE versions
   (§11 item 7) cut real work on ~10 SPU threads and the main thread. This is the biggest "real
   work" item, at medium risk (bit-exact NaN/denormal rules).
4. User-side, no code, reversible: Windows' "Maximum processor state" at 99% turns CPU boost off
   (typically -15 to -25 °C on laptops at some fps cost), or the laptop vendor's balanced/quiet
   mode. That is a quick check of how much is boost heat vs. load.

## 26. Heat run perf\heat\leaf1: where the 6.5 cores go, and ranked fixes (2026-10-25, read-only)
The AI assistant's build-pdb at 8516777, Hidden Leaf, GCM_FLIPCOUNT=1 PERF=1, thread_cpu2 for 12 × 5 s, plus
20 `threads` dumps. Real rate 15.9 f/s (debug exe + dumps), so the absolute numbers are for a slower
frame than normal; the split is what matters. GPU (gpu.csv): 38% use, 70.5 W, so the heat is
on the CPU side.

| thread kind (entry point) | CPU (% of a core) | kernel | wakes/s | what it is |
|---|---|---|---|---|
| `jc_thread` × ~20 | **417%** | 59% | 14,290 (~714 each) | SPURS job-chain walkers (cellSpurs.c ~2711). They run the game's SPU jobs (lifted code: `spu_fma`/`spu_fm` in the dumps) and **poll the chain's guard with `Sleep(1)`** (`jg_wait`, ~2553) between laps |
| `frame_clock` | 90% | **33%** | 427 | drawing/present thread: real work (FIFO, records, D3D12) plus the **`GCM_PUT_POLL` idle loop, which spins on `SwitchToThread`** (cellGcmSys.c ~1530). `GCM_PUT_POLL=0` once cut this thread 95% -> 70% (§7) |
| ucrt threads (guest PPU) | 88% | 31% | 6,911 | two of them are **event-queue pollers**: tids 48960 (38%) and 43188 (21%), parked in `func_0060AE04`, the game's wrapper for syscall 0x82 `sys_event_queue_receive` |
| `mainCRTStartup` | 41% | 5% | 1,546 | the game's main PPU thread: game code, VMX maths |
| `vfa_worker` × 3 | 10% | 5% | 3,317 | vertex-fetch workers |

**Why the event pollers spin.** `sys_event_queue_receive` (sys_event.c ~481) returns `ETIMEDOUT` *at
once* for any timeout under 1 ms. The guest asked to wait up to that long, so a guest loop of
"receive(timeout = some µs); on timeout do a little, retry" runs flat out instead of sleeping.
Those two threads match that: high CPU, high kernel share, parked in the receive wrapper. The
timeout value is *inferred*; a short `PS3_VERBOSE` run prints `[WAIT] event_queue_receive(q=..
timeout=..)`.

**Why the job threads cost so much.** Mostly real work: the game's SPU jobs in lifted C. The build
has no AVX2/FMA (`/arch` unset), so:
- `spu_fa/fm/fma` take the SSE/scalar exact path (spu_helpers.h ~831 needs `__AVX2__`);
- `fmaf` (PPU `vmx_vmaddfp`) is a library call.

Offline bench (tools\bench_avx.c, same headers; every result bit-identical to the `_ref` path):

| per 4-lane op | current build | `/arch:AVX2` |
|---|---|---|
| `spu_fm` | 7.1-8.0 ns | **2.8-3.2 ns** |
| `spu_fma` | 12.5-15.3 ns | **10.7-10.8 ns** |
| `vmx_vmaddfp` | 9.6-10.9 ns | 9.2 ns |

(the maintainer's boost was at 99% during the bench, so the absolute times are low-clock.) Their guard waits
add ~14k wake-ups a second and 0.6 core of kernel time.

**Found while checking:** clang-cl **ignores** `-ffp-contract=off` ("unknown argument ignored").
Today that is harmless: without FMA there is nothing to fuse into. With `/arch:AVX2` clang-cl
fuses `a*b+c` by default (checked: `vfmadd213ss` for plain `a*b+c`), which would change the lifted
PPU/SPU float results. The working spelling is `/clang:-ffp-contract=off` (checked: separate
`vmulss`+`vaddss`, while `fmaf` still becomes one `vfmadd`).

### Ranked fixes (same fps; code changes need the maintainer's yes; gains *inferred* unless marked)
| # | change | where | expected CPU saved | risk |
|---|---|---|---|---|
| 1 | **Event-queue receive honours sub-ms timeouts**: wait on the queue (a per-queue event) with a high-resolution timer for the timeout, instead of returning at once | runtime/syscalls/sys_event.c | ~0.5 core (the two pollers' ~59%) | low: the guest gets what it asked for; returns early when an event arrives |
| 2 | **Job-chain guards wait on an event**: `cellSpursJobGuardNotify` wakes the walker; no `Sleep(1)` poll | libs/spurs/cellSpurs.c | most of the 0.6 core of kernel time and ~14k wakes/s; jobs also start sooner (up to 1 ms) | low |
| 3 | **Build with AVX2+FMA**, `/clang:-ffp-contract=off` on every target (port, recompiled, runtime) | jstars\port\CMakeLists.txt, ps3recomp-src\CMakeLists.txt | SPU job work -15 to -30% (measured per op above) = ~0.5-1 core | low-medium: exact by the helper tests and the contract flag; the exe then needs an AVX2 CPU (2013+), so it is a build option |
| 4 | **Put-poll without the spin**: sleep each 200 µs step on a high-resolution timer instead of `SwitchToThread` loops | libs/video/cellGcmSys.c | ~0.25-0.3 core (most of frame_clock's 33% kernel) | low: same 200 µs polling, same latency |

1-4 together: about **1.5-2 of the ~6.5 busy cores** (~25-30% less CPU work at the same fps,
*inferred*). On temperature: a quarter less load should lower the CPU temperature and let
the fans calm down (*inferred*, depends on how the boost governs). The run after the patches
measures it.

### Run after the patches (the AI assistant)
Hidden Leaf with the normal (non-pdb) exe, thread_cpu2 + gpu.csv + GCM_FLIPCOUNT=1, each patch's
switch on vs off. Note the CPU temperature, fan noise and fps.
- 2026-10-25: the four fixes are drafted as patches\heat\1-4 (+162 lines in all: sys_event.c +69/-2,
  cellSpurs.c +46, port CMakeLists +29, cellGcmSys.c +18), off switches EVQ_SUBMS_WAIT,
  SPURS_GUARD_EVENT, -DJSTARS_AVX2, GCM_PUT_POLL_SLEEP. Run plan in patches\heat\README.md.

## 27. Heat patches test (2026-10-25, perf\heat; patches fa13a0d, da1f48d, d66c8da + CMakeLists AVX2, backup .pre-avx2)
Hidden Leaf automatic fights (random characters), 60 s thread_cpu2 + nvidia-smi:
| run | real f/s | CPU (kernel) | wakes/s | GPU |
| C before (old build) | 29.7 | 671% (196%) | 34,135 | 33% 65 W |
| A all four | 25.0 | 458% (198%) | 55,709 | 50% 86 W |
| B AVX2 only (three switches 0) | 24.7 | 527% (192%) | 33,496 | 51% 83 W |
- The f/s drop in A/B may be content (random characters, GPU much busier); demo comparison running.
- The laptop power/fan mode changed during these runs. The high-performance fan mode (Extreme Performance) boosts the
  system; it was OFF for the current tests, which alone can explain lower f/s in A/B vs C. Comparisons must use
  the same mode; rerun C in the same mode as A/B.
- Same power mode (no Extreme Performance) for C2/A/B and the demo runs:
  | run | Hidden Leaf f/s | demo f/s (215-265 s) | demo CPU (kernel) | demo wakes/s |
  | before (AVX2 off, 3 switches off) | 23.4 (C2) | 28.2 | 863% (188%) | 34,278 |
  | AVX2 only | 24.7 (B) | 28.8 | 672% (219%) | 37,489 |
  | all four | 25.0 (A) | 29.4 | 495% (214%) | 64,337 |
  Hidden Leaf CPU: C2 632%, B 527%, A 458%. Frame rate equal or slightly better; CPU about 1.9 cores (AVX2) and
  3.7 cores (all four) lower on the demo. Wakes/s go up with 1+2+4 (shorter sleeps), kernel time about the same.
  Pictures normal with effects (perf\heat\demo_heat_compare.png). Build left as before until the maintainer decides:
  -DJSTARS_AVX2=OFF, and the three switches made opt-in (b310762; =1 enables).
- Main goal now: find why some maps (Hidden Leaf ~21-25 f/s) can't hold a steady 30 f/s. Heat patches stay opt-in
  (no default decision yet).

## 28. Why Hidden Leaf misses a steady 30 f/s (2026-10-25, read-only)
Sources: perf\heat\C2_before and A_all ([PERF]/[PERF2]; same power mode, Extreme Performance off),
perf\heat\leaf1 (thread_cpu2 + 20 `threads` dumps, symbolized: perf\heat\leaf1\symbols.txt;
caveat: build-pdb was rebuilt at 12:29, after that run, but the names fit each thread's role).

**The drawing thread is the frame's bottleneck.** Per batch (= per frame here, 1 batch/flip, VPULL default):

| run | f/s | drain (vtx) | render_frame (prepass) | idle |
|---|---|---|---|---|
| C2_before, typical fight windows | 21-27 | 20-25 ms (6-7) | 9-11 ms (5-6) | 7-12 ms |
| A_all (heat patches on) | 23-30 | 14-23 ms (4-6) | 6-11 ms (3.5-7) | 9-18 ms |

- The thread is busy ~30-33 ms per frame on the heavy windows. Flips land on 60 Hz ticks, so 30 f/s
  needs every frame's renderer work under ~33 ms *including* the hand-off from the game. A frame at
  32 ms that slips by a millisecond waits for the 3rd vblank (50 ms), which gives the 21-25 f/s
  average. The light windows (14-16 ms drain) are the ones at 28-30 f/s.
- thread_cpu2 agrees: `frame_clock` at ~90% (leaf1), while the game's main thread sits at ~41%. The
  SPU job threads are busy too, but they aren't on the per-frame path the way the drawing thread is.
- **Where the drain goes** (frame_clock's exe samples in leaf1):
  - `rsx_process_method` ~51%: the per-FIFO-word method dispatch, a long chain of range tests per
    word (rsx_commands.c ~384);
  - the FIFO loop itself ~21%;
  - **`spu_coh_is_reserved` 16%**: an out-of-line call for every guest word the FIFO walker reads
    through `vm_read32`;
  - `vfa_sync` 6%.

  The record building, constants and D3D12 calls are in the driver/ntdll and code-cache samples
  (the 129 non-exe samples).

### Ranked fixes (gains *inferred* except #0; code changes need the maintainer's yes)
| # | change | expected | effort / risk |
|---|---|---|---|
| 0 | **Turn the heat patches on by default** (EVQ_SUBMS_WAIT, SPURS_GUARD_EVENT, GCM_PUT_POLL_SLEEP, AVX2 build). Measured same power mode: Hidden Leaf 23.4 -> 25.0 f/s, demo 28.2 -> 29.4, CPU -3.7 cores (§27) | **+1.6 f/s measured** | none: already built and tested, just defaults |
| 1 | **FIFO decode fast path**: the walker reads command words straight from guest memory (one coherency check per batch of words, not a call per word), and a jump table on the method for the common ones (transform constants, vertex/texture registers, draw) in front of the range-test chain | ~3-6 ms off the 14-25 ms drain | medium; exact (same state changes) |
| 2 | **render_frame on its own thread**: the drawing thread records frame N+1's draws while a second thread builds and submits frame N's D3D12 commands (records and upload heaps are already double-buffered by parity) | takes ~6-11 ms off the critical path, enough for 2 vblanks on most frames | large; mid-frame flushes and debug paths need care |
| 3 | Fewer per-draw costs left (constant-bank reuse §11 #5: 8 KB copy per draw; SRV descriptor reuse ~2.5 µs/draw) | ~2-4 ms | medium |

0 + 1 is the quick route to "mostly 30" on Hidden Leaf. 2 is what makes it steady with headroom.

### Run that would confirm (the AI assistant, same power mode)
Hidden Leaf with all of #0 on, `GCM_FLIPCOUNT=1 PERF=1`: the share of flips at 2 vs 3 vblanks
(tools\flipsum.py), with [PERF2] drain/render per window. Then the same after #1.
- 2026-10-25 13:50 the maintainer: yes, use the heat patches. Now default on: EVQ_SUBMS_WAIT, SPURS_GUARD_EVENT,
  GCM_PUT_POLL_SLEEP (db2a45d; =0 turns each off) and the AVX2 build (jstars\port\CMakeLists.txt option JSTARS_AVX2,
  default ON; build\ reconfigured ON; -DJSTARS_AVX2=OFF + rebuild = old build; CMakeLists backup .pre-avx2).
- 2026-10-25 fix #1 drafted: patches\fifo-fast (+70/-1; FIFO_FAST=0 off). SET_TRANSFORM_CONSTANT words go
  straight to their handler (rsx_process_method's first check + a tight run in the FIFO walker). Offline:
  identical constant state on 4M random words, 27.4 -> 17.4 ns per word. Correction to the §28 table: the
  walker's word reads are already the inline load (ppu_memory.h), so "direct reads" is not a gain; the
  spu_coh_is_reserved samples on that thread come from elsewhere (not pinned down).
- fifo-fast as committed (f4784cd), the AI assistant's check: constants arrive as blocks starting at 0x1EFC
  (SET_TRANSFORM_CONSTANT_LOAD, then the data words), so the walker's fast loop (which tests the block's
  first method) never runs; only the early check in rsx_process_method helps. Menu histogram
  (perf\fifofast\leaf_hist, TCONST_FIFO=1 fires at 200k blocks, i.e. before any fight), dwords:
  0x0B80 transform program 78k, 0x0A20 70k, 0x1A00 28k, 0x0208 26k, 0x1EFC block 23k (~6.3/block),
  0x0280 18k, 0x1740/0x1680 15k each. Proposed v2 (needs the maintainer's yes): word 0 of a 0x1EFC block through
  rsx_process_method, words 1.. in the fast loop; TCONST_FIFO=<blocks> threshold for a fight histogram;
  then early dispatch for whatever dominates in the fight (0x0B80 program words likely).
- 2026-10-25 fifo-fast v2 drafted (patches\fifo-fast\fifo-fast-v2.patch on f4784cd, +51/-5): walker handles
  0x1EFC-led constant blocks, vertex-program words (0x0B80..) take the fast path too, TCONST_FIFO=<n>
  periodic histogram for fights. Offline identical state on 4M words incl. program words.

## 29. FIFO_FAST test, v1 f4784cd + v2 5160f10 (2026-10-25, the AI assistant; perf\fifofast, perf\fifofast2)
Extreme Performance off, boost on during runs. Default-on; FIFO_FAST=0 = old path.
- **Fight histogram** (Hidden Leaf, TCONST_FIFO=20000, perf\fifofast2\leaf_hist): per 20,000 blocks
  ~78k data words; 0x0B80 vertex-program words 38.6%, 0x1EFC-led constant blocks 24.2%, textures 0x1A00
  4%, attrib offset/format 3.5% each. Constants never arrive in blocks that start at 0x1F00 (v1's walker
  loop was dead code; v2 fixes that). The one-shot TCONST_FIFO=1 histogram fires in the menu.
- **Demo, last 120 s, 4 pairs (fast vs old):** v1 28.2/27.0, 27.4/26.4; v2 28.0/27.8, 27.3/27.1.
  Fast ahead in 4/4 pairs, mean +0.6 f/s (27.7 vs 27.1); 2-vblank share 90.1/85.3 vs 89.5/83.3 (v2).
- **Hidden Leaf, 90 s, 3 pairs:** 21.2/24.9, 28.1/21.4, 23.7/26.1 - random characters dominate; no
  conclusion possible. [PERF2] drain swings 14-52% between runs of one setting: not usable.
- Screens: no difference. One v1 fast demo run showed a cyan wedge top-right in all 4 shots; not in the
  fast rerun, v2 runs or any old run - treated as the known intermittent triangle glitch, watch it.
- Take-away (inferred): decoding is not the big cost; 63% of data words now skip the method chain for
  ~+0.6 f/s on the demo. Hidden Leaf A/B needs fixed characters to measure anything under ~3 f/s.

## 30. Hidden Leaf after fifo-fast: what is known about the drawing thread's 30-33 ms, and the next step (2026-10-25, read-only)
**Known (measured):**
- The drawing thread (`frame_clock`) is the frame's limit. On heavy Hidden Leaf frames the [PERF]
  split per batch was drain 20-25 ms (vertex work 6-7 of it), render_frame 9-11 (prepass 5-6), idle
  7-12 (§28, C2_before/A_all).
- fifo-fast now takes 63% of the fight's FIFO data words past the method chain (§29: vertex-program
  words 38.6%, constant blocks 24.2%) for about +0.6 f/s on the demo. So FIFO decoding was a few
  ms at most, not the bulk.
- Where that thread's samples land (`threads` dumps):

  | run | ntdll | exe | NVIDIA driver | D3D12Core | VCRUNTIME (memcpy) |
  |---|---|---|---|---|---|
  | leaf1 (210, pre-heat-patches) | 46% | 39% | 11% | 4% | - |
  | s07_pdb (820, older build) | 60% | 27% | 4.5% | 4% | 5% |

  thread_cpu2 had this thread at 90% CPU with 33% kernel. So a good part of the ntdll share was the
  put-poll spin (now `GCM_PUT_POLL_SLEEP`, default on) and real waits (GPU fence, the game's
  hand-off), which are not CPU.

**Not known:** the split of the remaining busy time. [PERF] times the drain as one block (FIFO
decode + draw-record building + vertex work + texture setup) and render_frame as one block (GPU-fence
wait + prepass + command recording + submit/present). `s_perf_gpu` already measures the record loop
but is never printed. Without that split, the next fix would be a guess; the candidates are below.

### Next step: measure, then fix
1. **[PERF6] split (diagnostic, needs the maintainer's yes, printed only with PERF=1).** Per [PERF] window:
   - drain = FIFO decode | draw callbacks (record building, incl. vtx) | texture setup | waits for
     async vertex fetch;
   - render_frame = wait for the previous GPU frame | prepass (srv / pso) | command recording (the
     existing `s_perf_gpu`) | ExecuteCommandLists + Present.

   About 30 lines in rsx_d3d12_backend.c and cellGcmSys.c, no behaviour change.
2. **No-code run now (the AI assistant):** build-pdb at HEAD, Hidden Leaf fight (fixed characters, below), 40
   `threads` dumps 1 s apart + thread_cpu2, then `tools\symdumps.py` on the drawing thread's samples.
   That shows the current function split while [PERF6] is pending.

### Candidate fixes (rank after the split; gains *inferred*)
| candidate | where it would show in [PERF6] | size |
|---|---|---|
| render_frame on its own thread (§28 #2) | render_frame's recording + submit leave the critical path | large |
| constant-bank reuse (8 KB copy per draw, §11 #5) and SRV descriptor reuse (~2.5 µs/draw) | draw callbacks / prepass | medium |
| skip the prepass PSO pass for draws whose key didn't change (prepass and replay both compute the key) | prepass | small-medium |
| vertex work with VPULL tuned (6-7 ms) | draw callbacks | medium |
| GPU-fence wait, if large: one more frame of upload buffers in flight (triple parity) | render_frame wait | medium |

### Fixed-character test sequence (so Hidden Leaf A/B resolves < 3 f/s)
Today's tour presses START at character select, which picks random characters, and characters
change the load a lot (Hidden Leaf runs of one setting span 21-28 f/s). Proposed (the AI assistant to
calibrate the presses from screenshots, `PAD_FILE`/`press` as in menu_tour.sh):
1. Character select: move the cursor to a fixed fighter (e.g. Naruto: RIGHT ×n from the start
   cursor), CROSS; the same for the partner and the support; then fixed picks for the CPU side, or
   the same team mirrored.
2. Stage select: Hidden Leaf (RIGHTS=7 as today); start the match.
3. No input during the fight; measure flips from fight HUD + 10 s for 60 s (the opening has the
   same camera pass every time).
4. Three runs per setting, alternating A/B, same power mode (Extreme Performance off). Report each
   run's flips/s and the 2- vs 3-vblank share (tools\flipsum.py).

If the CPU AI still varies too much, a fight where both sides are idle helps: Training/Practice
mode, if J-Stars has one reachable from the menu, or a 2P Free Battle with no input on either pad.
- 2026-10-25 [PERF6] drafted (patches\perf6, +46/-5, PERF=1 only): render_frame = gpu-wait + prepass
  (textures) + record + other; then submit, next-frame fence wait, present; drain = draw callbacks (vtx,
  mid-frame) + decode/other. Found while adding it: render_frame starts with wait_for_gpu() (full GPU
  drain every frame), and move_to_next_frame()'s fence wait and the submit were outside every timer,
  i.e. counted as "idle rest" in [PERF2] (inferred: part of the 7-12 ms "idle" may be GPU waits).

## 31. [PERF6] split + build-pdb profile, Hidden Leaf fixed characters (2026-10-25, the AI assistant; perf\perf6, 3f6d509)
Fixed characters: `FIXED_CHARS=1` in tools\menu_tour_play.sh = Ichigo in all six slots (probe sheets
perf\perf6\probe1-2). Same characters still gave 23.3 / 26.0 / 28.9 f/s (leaf_a, leaf_pdb, leaf_b):
the camera decides. Village/gate view ~2400-3000 draws per batch, park/trees ~1400-1800.
- **[PERF6] ms per batch, fight (heaviest 20% of windows):**
  leaf_a  R 9.6 (11.8) = gpu-wait 0.3, prepass 5.4 (tex 1.7), record 3.8 | D 21.4 (25.4) = draws 13.8 (vtx 6.3) + decode 7.6 | busy 31.4 (37.6)
  leaf_b  R 5.5 (7.7) | D 14.6 (18.1) = draws 10.9 + decode 3.7 | busy 20.5 (26.2);  demo busy 19.8 (29.7)
  gpu-wait, next-frame wait and submit are ~0: the CPU and GPU are NOT serialized; the drawing
  thread's own CPU work is the limit (37.6 ms on heavy windows vs 33 budget).
- **Per draw (tools\drawcost.py, view-independent):** 12.3-16.3 us/draw in every run =
  draw callbacks 5.3-7.9 (vertex work 2.3-3.3) + decode/other 2.7-3.4 + prepass 2.2-2.9 + record
  1.5-1.9. Holding 30 f/s at ~3000 draws needs about <=10 us/draw (inferred), i.e. ~25% less per draw.
  us/draw is the metric for future A/B on Hidden Leaf (fps follows the camera).
- **Profile (leaf_pdb, 40 dumps):** drawing thread (frame_clock) 64-71% CPU, kernel 25%, ~1,100
  wakes/s. Samples: ntdll 56%, exe 30%, D3D12Core 8.5%, NVIDIA 5.5%. In the exe:
  cellGcm_rsx_process_fifo 36%, render_frame 20%, vfa_sync 16%, vpull_prepare 13%,
  vp_upload_tex_slot 9%, vp_get_fp_pso 3%, rsx_process_method 2%. symdumps: perf\perf6\leaf_pdb\symdumps.txt.

## 32. Per-draw cost: ranked next fixes (2026-10-25)
From §31 (12.3-16.3 us/draw: callbacks 5.3-7.9 incl. vertex 2.3-3.7, decode/other 2.7-3.4, prepass
2.2-2.9, record 1.5-1.9; target ~10 at 3000 draws) and the drawing thread's exe samples in
perf\perf6\leaf_pdb\symdumps.txt (246 samples):
- `cellGcm_rsx_process_fifo` (the walker, inlined) 36%;
- `vfa_sync` 16%: the drawing thread waiting for or helping the vertex workers at sync points;
- `vpull_prepare` 13%;
- `render_frame` texture prep in the prepass (lines 7073/7083) 20%;
- `vp_upload_tex_slot` 9%;
- `vp_get_fp_pso` 3%, `rsx_process_method` 2%.

The thread's samples are ntdll 56% / exe 30% / D3D12Core 8.5% / NVIDIA 5.5%.

| # | fix | targets | est. us/draw | status |
|---|---|---|---|---|
| 1 | **VPULL_MEMO**: reuse the fetch plan while vp_gen/vattr_gen unchanged | vpull_prepare 13% | 0.5-1 | **drafted** (patches\drawcost2) |
| 2 | **TEX_HINT**: texture-slot scan starts at the remembered slot | vp_upload_tex_slot 9% | 0.2-0.7 | **drafted** |
| 3 | fewer CPU-path vertex jobs, so fewer waits in vfa_sync: find why ~512 VPULL prepares per batch fall back ("layout": divider, mixed strides or memory) and support the common case; or skip the sync where no job reads the memory being synced | vfa_sync 16% | 0.5-1.3 | needs a reason counter first (diagnostic) |
| 4 | prepass texture loop: reuse the previous draw's SRV window when the bound texture set is the same | render_frame 20% | 0.5-1 | next draft |
| 5 | walker per-block overhead for the words fifo-fast doesn't cover (trace ring, ~10 debug-flag loads, per-word IO translation) | walker 36% | 0.3-1 | medium |
| 6 | constant bank: copy only the constant ranges the vertex program reads (8 KB -> ~1-2 KB per draw) | callbacks | 0.3-0.5 | needs constant-use analysis in the VP decompiler |
| 7 | record: skip D3D12 calls whose state didn't change since the previous draw (PSO, root CBVs, topology, viewport) | record 1.5-1.9 | 0.3-0.7 | needs a look at the replay loop |

1+2 are exact and small; 3 and 4 are the next biggest.

## 32. drawcost2 test: VPULL_MEMO + TEX_HINT (2026-10-25, the AI assistant; perf\drawcost2, 0a30088)
Hidden Leaf FIXED_CHARS=1, 3 alternating pairs, defaults vs VPULL_MEMO=0 TEX_HINT=0, boost on.
- f/s on 28.7 / 25.6 / 28.6 vs off 25.3 / 24.3 / 24.1, but the on runs happened to see fewer draws
  (1327-1935 vs 2169-2272 per batch): camera, not the patch.
- New tools\drawfit.py (busy = fixed + slope x draws per run, then compared at equal draw counts):
  busy at 2000 draws on 26.1 / 27.0 / 28.4 vs off 27.2 / 27.6 / 27.8 ms; pooled 27.1 vs 27.6 (-0.5 ms,
  ~2%), slope 10.7 vs 10.9 us/draw. Within run-to-run spread: neutral to slightly better, below the
  0.7-1.7 us/draw estimated.
- Pictures: no artefacts in 12 shots (dc2_sheet.png). [VPULL] CPU-path layout counts are of the same
  order in both (8192-10240 vs 8832-12032; depends on the draws seen).
- drawcost.py's plain us/draw falls as draws rise (fixed ~5-6 ms per batch), so compare settings
  with drawfit.py at equal draws, not plain us/draw.

## 33. After drawcost2: re-ranking, and the vertex-helper hand-off cost (2026-10-25)
The AI assistant's fit (§32 drawcost2): busy = ~5.7 ms fixed + ~10.8 us per draw. 3000 draws -> ~38 ms vs the
33 ms budget, so ~5 ms per batch must go: ~1.7 us/draw, or less fixed time. drawcost2 (memo + hint)
gave only -0.5 ms at 2000 draws, far below its estimate: the profile shares were exe-only samples.
- **Where the drawing thread's samples really are** (perf\perf6\leaf_pdb\threads_39.txt, tid 31836,
  40 dumps): ntdll 23, exe 13, D3D12Core 2, NVIDIA 2. The thread is 64-71% busy with 25% kernel
  (§31), so ntdll = idle waits (~30%) + kernel calls (~25%), *inferred*. Exe-only fixes miss the
  kernel quarter.
- **[PERF2] `async fetch wait` is 0.000 s in every fight window** (perf\drawcost2\on1\stderr.txt):
  the drawing thread never waits for the vertex helpers. So the `vfa_sync` 16% sample line in §31 is
  probably a mis-symbolized address (the build-pdb exe was rebuilt after that run).
- **One kernel cost found:** every queued vertex job (`vfa_fetch_list`, the 512 per batch VPULL
  can't take) calls `ReleaseSemaphore` and wakes a sleeping helper. Measured on this PC
  (patches\vfa-wake\bench\bench_vfa.c, 512 jobs per batch, 10 us apart, 4 us each, 3 helpers):
  **3.7-4.3 us of drawing-thread time per job**. Waking only a sleeping helper changes nothing on
  its own (helpers are asleep again by the next job). One helper polling ~1024 / ~4096 pauses
  first: 1.5-1.7 / 0.3-0.6 us. At 512 jobs that is up to ~2 ms per batch (*inferred*), about
  the size of the hand-off's own benefit, since one job is one <=256-index strip batch.
- **Drafted:** patches\vfa-wake (VFA_WAKE default on, VFA_SPIN=<n> and VFA_INLINE=<n> default off,
  new [VFA] jobs/size/wakes line). Queue test: 2M jobs, 63k syncs, across the 32-bit wrap,
  0 errors in all modes. Same 12 compile warnings; `git apply --check` OK on 0a30088.
- **Free test, no code:** `VFETCH_ASYNC=0` (fetch on the drawing thread, no hand-off) vs default,
  on the current build.

| # | next fix | targets | status |
|---|---|---|---|
| 1 | vertex hand-off: VFA_WAKE / SPIN / INLINE (or VFETCH_ASYNC=0) | ~4 us x 512 jobs per batch | **drafted** (patches\vfa-wake) |
| 2 | find the rest of the 25% kernel time: an ETW/WPR CPU capture with kernel stacks of tid frame_clock during a Hidden Leaf fight | kernel ~25% | needs the AI assistant (admin capture) |
| 3 | prepass SRV window reuse when the texture set is unchanged | prepass 2.2-2.9 us/draw | not drafted |
| 4 | record: skip unchanged D3D12 state calls | record 1.5-1.9 us/draw | not drafted |
| 5 | the ~5.7 ms fixed per batch: [PERF6] shows submit 0.1, present 0.2, gpu-wait 0.1-1.1, so most of it sits in the drain's decode/other and the prepass; needs a per-batch split of those | fixed part | diagnostic first |

**Paused 2026-10-25 (the maintainer: continue tomorrow).** State: patches\vfa-wake drafted, waiting for the maintainer's yes before the AI assistant applies it; the no-code VFETCH_ASYNC=0 A/B and a WPR kernel-stack capture were asked of the AI assistant. Scratch clone commit 43dfe77 holds the patch. Next: read the AI assistant's results (drawfit.py busy at equal draws, [VFA] job counts/sizes), pick SPIN vs INLINE, then #3 (prepass SRV reuse) or whatever the kernel capture shows.

## 34. VFETCH_ASYNC=0 test + vfa-wake (2026-10-25/26, the AI assistant; perf\vfa, perf\vfawake)
- **VFETCH_ASYNC=0 vs default** (Hidden Leaf FIXED_CHARS=1, 3 pairs; on2 never reached the title, so
  5 usable runs). All off runs sat at ~2500-2670 draws/batch (village view), busy 37.7 / 38.2 / 38.1 ms,
  20.0 f/s. Default runs' fits at 2600 draws: 35.1 (on1), 34.4 (on3) ms. So turning the helpers off
  costs ~3 ms per batch at equal draws: the helpers help, despite the per-job wake cost.
- **vfa-wake** committed as d1ea59b (VFA_WAKE=0 = old), built. Only run old1 (VFA_WAKE=0) finished
  before the maintainer paused for the night; the batch (old/wake/inline/spin x2, thread_cpu2 per run) was
  stopped during wake1. Next: rerun the 4 variants x2 (the local batch145.sh logic, minus the
  commit/build steps), drawfit.py at equal draws, CPU% for VFA_SPIN=2000 from thread_cpu2
  (CPU temperature is not readable without admin; note GPU/CPU% only).

**2026-10-26 (read-only):** perf\vfawake\old1 [VFA]: 7,680-15,500 jobs per window = ~380-775 per batch, 242 vertices avg, one wake per job. With §34 (helpers save ~3 ms/batch vs inline at ~600 jobs, so a job is ~5 us more than its hand-off), VFA_INLINE cannot win: skip that variant. Expected from VFA_SPIN (bench: hand-off 4 -> 0.3-1.7 us): ~1.5-2 ms per batch at ~600 jobs (*inferred*), plus lower kernel % for frame_clock in thread_cpu2 if the wakes are a big part of the kernel time. Kernel time read-only: no per-draw resource creation found (CreateCommittedResource sites are in texture/RT caches, captures and present); the rest needs a stack capture.
- **vfa-wake results (2026-10-26, perf\vfawake, after the maintainer's PCIe power-saving change):** Hidden Leaf
  FIXED_CHARS=1, old (VFA_WAKE=0) / wake (default) / spin (VFA_SPIN=2000), 2 rounds; VFA_INLINE dropped.
  drawfit busy at 2000 draws: old 23.9 / 26.4, wake 26.3 / 26.7, spin 25.9 / 26.7 ms; pooled
  old 26.0, wake 26.5, spin 26.2 (slopes 10.1 / 11.1 / 11.0 us/draw). [VFA]: ~10,800 jobs per window
  (243 vertices avg); helper wake-ups 10,800 (old) -> 1,100-1,700 (wake/spin). frame_clock CPU 52-63%
  (old), 62-64% (wake), 44-59% (spin), kernel 15-21% in all; vertex helpers 13-16% (old/wake) vs
  15-19% (spin); process total 471-539% in all. CPU temperature not readable without admin.
  => 7-9x fewer wake-ups but no measurable change in drawing-thread busy time: neutral. The per-job
  semaphore cost from the bench does not show in the game (inferred). No visual differences.
  Same-setting comparison vs yesterday (drawcost2 "on", busy@2000 pooled 27.1 ms) is within noise
  of today's old 26.0, so no visible PCIe effect.

## 35. Measure before the next fix: DRAW_SAMPLE stack sampler (2026-10-26, AI assistant; patches\draw-sample)
- drawcost2 and vfa-wake were both neutral (§32, §34): sized from top-address dumps and a bench, which cannot show where the drawing thread's ~25% kernel / ntdll time comes from (*inferred* lesson: size fixes from in-game measurements only).
- Measured per-draw buckets at ~2600 draws ([PERF6], perf\drawcost2\on1): decode/other ~3.2 us, vtx ~2.3, callbacks without vtx ~2.1, prepass ~1.7 (textures 0.5, SRV loop incl. 16 CreateShaderResourceView per draw ~0.8), record ~1.5.
- Drafted patches\draw-sample: DRAW_SAMPLE=1 suspends/walks/resumes the drawing thread every 0.5-1.5 ms and charges ntdll/driver samples to their exe caller (first two exe frames). Offline workload check: callers right; running split within ~7 points; blocked waits under-counted ~2x (ntwait = lower bound). Same 12 warnings, apply --check OK on d1ea59b. Waiting for the maintainer's yes (diagnostic code).
- Next: rank fixes from its output; candidates already in view: the 16 SRV writes per draw (~0.8 us), the 8 KB constant-bank copy per draw, the walker's per-word cost.

## 36. DRAW_SAMPLE results (2026-10-26, the AI assistant; perf\drawsample, edf0bad, build-pdb)
Hidden Leaf FIXED_CHARS=1, PERF=1 GCM_FLIPCOUNT=1; ds1/ds2 with DRAW_SAMPLE=1, nods without.
Fight windows (ds1 35-41, ds2 37-43, ~48k samples each; ds_sym output ds1_sym.txt / ds2_sym.txt):
- Classes: ntdll 38-40%, ntwait 28-32% (lower bound), exe 14-18%, nvidia 6-8%, other 6%, d3d12 2-3%.
- Charged to (ds1 / ds2):
  cellGcm_fifo_kick_wait <- frame_clock 44.2 / 38.9% (ntwait 31.7%, ntdll 12.4%): waiting for the
    game to kick the FIFO, i.e. the drawing thread is starved for ~1/3+ of the fight (blocked waits are
    under-counted ~2x), so the game (PPU) side also limits the frame;
  render_frame 11.0 / 13.2% (incl. nvidia 6%, d3d12 2%);
  **vf_timed_end 10.3 / 9.8% + vf_cycles 9.7 / 9.9%, all in ntdll, from d3d12_draw_indexed_impl:
    the PERF=1 vertex timer (QueryThreadCycleTime + QPC per draw call). ~20% of the drawing thread's
    samples are PERF instrumentation, so every PERF=1 run overstates busy time (inferred: by several
    ms per batch); runs without PERF should be faster.**
  cellGcm_rsx_process_fifo 8.1 / 9.5%;
  **printf <- d3d12_draw_arrays_impl 3.9 / 3.7%: the unconditional "[D3D12] draw_arrays #N" every
    1000th draw (rsx_d3d12_backend.c ~10909), likely stdout lock/file I/O.**
  vp_upload_tex_slot 2.6-2.8%, fp_info 1.6-1.9%, upload_strip_vpull 1.5%, vfa_sync 1.0-1.5%.
- Sampler cost: drawfit busy@2000 draws ds 27.3 / 27.6 vs nods 26.3 ms (~1 ms, ~4%).

## 37. Why the drawing thread waits for the game (2026-10-26, AI assistant, read-only)
- The loop: game main thread (func_006507DC) per frame = wait flip DONE -> reset, SetFlip + flush (put) -> wait a vblank -> build the next frame (game logic, SPU jobs, commands) -> wait flip DONE. DONE is set on the first 60 Hz tick after the request (cellGcm_request_tick; GCM_FLIP_AT_WORD off), not when we finish drawing; the game can run 2 flips ahead (FINDINGS 2026-10-24).
- So the game makes a frame every 33.3 ms only if its build time B (after the vblank wait, before the next flip poll) stays under ~16.7 ms; B > 16.7 ms gives a 50 ms frame however fast we draw (*inferred* from the loop).
- Evidence the game side limits too: perf\drawsample\nods fight windows: drawing thread busy only ~18-22 ms per batch ([PERF6] render_frame 4.4-6.4 + drain 12.6-16.2), yet [PERF4] puts 3-9 of every 20 batches at 37-54 ms. perf6 leaf_pdb: main thread tid 44444 38-41% CPU, in 27/40 dumps waiting in func_006507DC (flip/vblank polls), 8/40 running (exe top: vmx_vmaddfp 32%, lane-by-lane fmaf, ~10 ns per op, CPU_PROFILE §26).
- Not yet measured: B per frame and what it waits on (SPU jobs, labels). Proposed diagnostic FRAME_TL=1 (cellGcmSys.c): per frame the request time, first flip poll (= end of build), DONE seen, drain start/end; histograms per 60 frames. Then, if B is the cause, point the DRAW_SAMPLE sampler at the main thread.

**Paused 2026-10-26 (the maintainer).** State: waiting for the maintainer's yes on the FRAME_TL=1 per-frame timeline diagnostic (§37, not drafted yet). Next after a yes: draft it in cellGcmSys.c (request time, first flip poll = end of build, DONE seen, drain start/end; histograms per 60 frames), offline check, hand to the AI assistant. If build time >16.7 ms is the cause: sample the game main thread with the DRAW_SAMPLE code (needs a per-thread table) and fix what it shows (vmx_vmaddfp is a candidate). Open for the maintainer: VPULL_MEMO/TEX_HINT default-on; also §36 notes the PERF=1 vertex timers cost ~20% of the drawing thread's samples and the every-1000th-draw printf ~4% (cheap fixes, not drafted).

- **Drafted 2026-10-26 (the maintainer yes): patches\frame-tl\game-wait.patch** = FRAME_TL=1 ([FTL] per 60 flips: interval, vbl, build incl. main-thread CPU, flip wait, walker/present lag; long frames split by cause) + GAME_SAMPLE=1 (DRAW_SAMPLE sampler on the frame-loop thread, one sampler thread for both). Offline model and two-thread checks in its README; same warnings; apply --check OK on edf0bad. Handed to the AI assistant.

## 38. FRAME_TL + GAME_SAMPLE results (2026-10-26, the AI assistant; perf\gamewait, 6c7887a, build-pdb)
Hidden Leaf FIXED_CHARS=1 PERF=1 GCM_FLIPCOUNT=1 FRAME_TL=1; gs1/gs2 also GAME_SAMPLE=1. f/s 24.0 / 27.3 (ftl) / 28.4.
- [FTL] (last 4 lines per run): build 0.0-3.1 ms (cpu 0.0-2.5, max 27, 0-3 of 60 over 16.7) in all runs.
  ftl: interval 33.9-35.8 ms, 3-8 of 60 over 37 (cause mostly "neither", 1-3 walker>33), walker 15.9-18.4 ms
  (max 38-48), flip wait 2.9-4.2, present 92-98. gs1/gs2: walker 111-235 ms with all 60 "over 33" -
  looks wrong when GAME_SAMPLE runs (inferred; the 6-request offset or the sampler), flip wait 0.6-11.6.
- Game frame-loop thread (GAME_SAMPLE, fight windows gs1 34-40, gs2 33-39, ~35k samples):
  sys_timer_usleep 43.1 / 54.3% (ntwait 23 + ntdll 19 in gs1), cellGcmGetFlipStatus 19.0 / 9.9%,
  sys_lwmutex_lock 8.5 / 9.9% (mostly ntwait), sys_lwmutex_unlock 3.3 / 3.8%,
  fprintf <- sys_event_port_send 2.5-3.0% (the "[evt] port_send(port=2): queue 2 FULL -> EBUSY" log,
  ~4,100-4,300 lines per run), game code (func_*) only a few %.
  => The frame-loop thread mostly sleeps/polls; its build is ~0-3 ms. It is not what holds the
  drawing thread up (inferred). The FIFO work must come from elsewhere (other PPU threads / SPU jobs).
  Files: perf\gamewait\gs1_sym.txt, gs2_sym.txt.

## 39. The late tick: why the frame loop waits (2026-10-26, AI assistant; patches\vblank-thread)
- §38 [FTL] vbl (flip request -> first vblank tick after it) averaged 20.1-23.2 ms; on-time 60 Hz ticks allow 16.7 at most, so ticks are late. Cause (code, jstars\port\main.cpp frame_clock): the tick is fired by the drawing thread only between drains, so no tick fires while it decodes/draws a frame (walker 16-18 ms); late ticks then burst.
- J-Stars waits for a vblank after each flip request before flushing the next frame, so its flush is late by the same amount; its build is only 0.3-1.5 ms and its thread mostly sleeps (GAME_SAMPLE). The game is not slow: it waits for a tick the busy drawing thread has not fired (inferred from the code plus vbl > 16.7). Fits the mostly "neither" cause of the over-37 ms frames.
- Drafted patches\vblank-thread (VBLANK_THREAD=1, opt-in): the tick on its own QPC/high-res-timer thread, which kicks the drawing thread; frame_clock then only drains and presents. Compiles, same warnings; patch --dry-run OK. Waiting for the maintainer's yes. Watch the game running further ahead (beam VFX, FIFO ring).
- FRAME_TL present lag and the walker lag under GAME_SAMPLE are counting artifacts (flips sharing one present; flips with no FIFO marker).

## 40. VBLANK_THREAD=1 test (2026-10-26, the AI assistant; perf\vblankthread; jstars\port\main.cpp, backup main.cpp.pre-vblank-thread)
Hidden Leaf FIXED_CHARS=1 PERF=1 GCM_FLIPCOUNT=1 FRAME_TL=1. Stopped after 4 runs: **unsafe as is.**
- vt1 (VBLANK_THREAD=1): two tries, both stuck before the fight ("fight HUD never appeared"; the maintainer saw a
  black screen).
- vt2 (VBLANK_THREAD=1): 29.9 f/s, but b/f 0.93 (flips without a drawn batch) and heavy glitches seen by
  The maintainer when looking at the city: HUD drawn as black boxes, broken city geometry
  (vt2_hud_black.png, vt2_city.png). [FTL] vbl 14.9-15.4 ms (was 20-23), interval 33.3-34.2,
  1-5 of 60 over 37; [PERF4] 17 of 20 batches 20-37 ms.
- off1 / off2: 25.4 / 27.7 f/s, vbl 20.3-23.4, 15-31 of 60 over 37.
- Reading (inferred): on-time ticks do remove the late-vblank stall (the frame-rate goal is reachable),
  but the game then runs ahead and reuses/overwrites buffers the drawing thread hasn't consumed yet.
  A safe version needs a limit on how far the game may run ahead of the drawing thread.
- The opt-in patch is still in main.cpp (off by default, VBLANK_THREAD unset = old loop).

## 41. VBLANK_THREAD v2: present gate (2026-10-26, AI assistant; patches\vblank-thread README "v2")
- With the tick on its own thread, a FIFO flip now completes only after the frame clock has presented it (cellGcmSys present gate, 100 ms release, PRESENT_GATE=0 = v1). Bounds run-ahead to one frame, the PS3 double-buffer contract. Offline model: 0 run-ahead violations in 400 flips; 33.2 ms flips at 20 ms draw, 50.1 at 40; a 300 ms stall released at 100 ms.
- vt1 black screen: game kept flipping and drawing (18,370 flips, ~600 objects/batch), screen black from the intro, port-3 FULL logged 2x as often; cause unproven, most likely the run-ahead (inferred).
- present-gate.patch (cellGcmSys, on 6c7887a) + vblank-thread-v2.patch (main.cpp, on the v1 code). Waiting for the maintainer's yes before the AI assistant tests.

## 41b. VBLANK_THREAD v2 (present gate) test (2026-10-26, the AI assistant; perf\vblankv2\vt1; ccf1363 + main.cpp v2, backup main.cpp.pre-vblank-v2)
Stopped during the first VBLANK_THREAD=1 run (Hidden Leaf FIXED_CHARS=1, FRAME_TL=1), at the maintainer's report:
boot logos to title ran at ~8 f/s, menus fine, glitches again once the match started.
- [FTL] (loading/intro part, the run was stopped before the fight HUD): interval 125-127 ms, all 60 over
  37; **flip wait 100.9-101.2 ms = the 100 ms PRESENT_GATE_MS timeout on almost every flip**; vbl 16.6-17.6;
  build 7.5-8.4 ms. Only 9 "present gate ... releasing" lines logged (log is probably limited).
- port-3 FULL logged 5,202 times by the intro.
- Reading (inferred): most flips are never reported as presented to the gate (several flips share one
  present, or a flip without a FIFO marker never gets a present), so each waits out the 100 ms valve:
  ~8 f/s. And glitches still appear in the match, so the gate does not stop the run-ahead that breaks
  the HUD/city. Still opt-in, default off.

## 41c. VBLANK_THREAD v3: gate lag 1 (2026-10-26, AI assistant)
- v2 waited out the 100 ms valve on every flip (interval 125 ms): J-Stars' flip-N marker reaches the walker only with a flush it makes after flip N completes, so "presented before done" waits on itself. A model with that order gives exactly 125.0 ms at lag 0 (test_gate3.c), matching the AI assistant's 125.0.
- present-gate-v3.patch: PRESENT_GATE_LAG (default 1) = flip N completes once N-1 is presented (double buffering); release log shows requested/decoded/presented. Model lag 1: 33.3 ms flips at 20 ms draw, 0 run-ahead violations. Waiting for the maintainer's yes.

## 41d. VBLANK_THREAD v3 test (2026-10-26, the AI assistant; perf\vblankv3; 28bc468, main.cpp v2)
Both runs auto-stopped at ~9-11 s by a slow-watch (an [FTL] window over 60 ms per flip), during the boot
logos, before any menu:
- PRESENT_GATE_LAG=1 (default): interval 137.7 ms, flip wait 113.4, 41 releases by 9 s, e.g.
  "flips requested 42 decoded 39 presented 40, owed 100 ms, releasing".
- PRESENT_GATE_LAG=2 (env only, same build): interval 137.9 ms, flip wait 111.3, 41 releases by 11 s,
  e.g. "flips requested 43 decoded 40 presented 40, owed 100 ms, releasing".
- Reading (inferred): requested always runs (lag + 1) ahead of decoded/presented, whatever the lag. The
  game seems to flush the FIFO that carries flip N's marker only after an earlier flip completes, so any
  gate on presents waits on itself until the 100 ms valve. Not tested further (no menus/fight reached).

## 41e. VBLANK_THREAD v4: catch-up gate (2026-10-26, AI assistant)
- v3 run: "flips requested 42 decoded 39 presented 40": markers reach the walker up to 3 requests late, so a fixed "N-k presented" lag is a guess; lag 2 is not shown safe (inferred).
- present-gate-v4.patch (default PRESENT_GATE_MODE=catchup): flip DONE only on a tick when get == put and all decoded flips are presented, i.e. what the old loop guaranteed by ticking between drains (never glitched), while the vblank count stays on time (the measured old cost). 100 ms release logs get/put/requested/decoded/presented. Not modelled offline. Waiting for the maintainer's yes.

## 42. VBLANK_THREAD v4 first look + next steps (2026-10-26, AI assistant, read-only on perf\vblankv4)
- v4 (catchup gate) reached the fight in vt1/vt2; HUD and geometry fine in the 2 shots checked (vt2 p_030, p_055; the radial blur in p_055 is a skill effect). Releases: 17-21 per run, all at boot (put 0x0/0x28, flips requested 2-3), none in the fight.
- [FTL] vbl 16.0-16.5 ms (on time) vs off 21-27; intervals vt 34.0-36.7 vs off1 34.4-37.8 (light view) / off2 41.9-47.1 (heavy view). [PERF4] 20-37 ms batches: vt1/vt2 13-17 of 20 at busy ~19-21 ms; off1 13-19 of 20 at busy ~19-22 ms. => safe-looking, but no clear f/s gain at equal view yet; the camera decides busy time (off2 busy ~35 ms -> 18-24 f/s). FTL walker/over-37 causes are unreliable under v4 (markers flushed ~1.4 intervals late).
- PERF=1 itself costs ~20% of the drawing thread (vf_timed_end/vf_cycles, §36): all f/s so far are with PERF=1. The 30 f/s goal must be judged with GCM_FLIPCOUNT=1 only (real flips, no PERF).
- Drafted patches\quiet-logs (draw_arrays liveness printf only with DRAW_LOG_EVERY; EBUSY line rate-limited): logging only, same warnings, apply --check OK on 1a4a29d.
- Next for heavy views (drawing busy 35 ms): per-draw cost (drain decode/other ~10 ms, draws 15 incl. vtx 7, prepass 6, record 4).

## 41f. VBLANK_THREAD v4 (catchup gate) test (2026-10-26, the AI assistant; perf\vblankv4; 1a4a29d + main.cpp v2)
Hidden Leaf FIXED_CHARS=1, alternating: vt1 22.4 f/s (2369 draws/batch), off1 27.6 (1986), vt2 28.6 (1557),
off2 22.7 (2332), vt3 27.6 (1529). **Safe: boot at full speed, fight starts, no glitches in 18 shots
(vt1_sheet.png, vt23_sheet.png).** But neutral: at similar draw counts vt1 22.4 vs off2 22.7 f/s; drawfit
pooled busy@2000 vt 27.5 vs off 26.9 ms. [FTL]: vbl drops 21.7-27.5 -> 16.1-17.0 ms but flip wait rises
1.0-9.4 -> 11.4-17.3 ms (the gate), so the interval is unchanged (34-41 vs 34.5-47). 16-23 "catchup
releasing" lines per run (mostly boot), e.g. "get 0xACEC put 0xAD08, flips requested 7678 decoded 8007
presented 8006, held 100 ms". Stays opt-in, default off.
- Also drafted patches\quiet-logs\perf-cheap.patch: with PERF=1, QueryThreadCycleTime per draw only with PERF_CYCLES=1; perf_now caches QPF. PERF=1 numbers (10.8 us/object, busy ms) were overstated by the timer syscalls (~20% of drawing-thread samples, §36). Asked the AI assistant for real-flip runs without PERF on Hidden Leaf light/heavy view, Kameari, Alabasta.
- Drafted patches\rec-dedup (REC_DEDUP, default on): skip repeated per-object PSO/viewport/scissor/topology calls in the record loop, CB GPU addresses read once; targets the ~8% driver/runtime samples in render_frame (§36). Not run.

## 43. No-PERF survey first look (2026-10-26, AI assistant, read-only on perf\survey_noperf)
- Real flips, last 60 s of each run (fight vs loading not separable: no tour logs/shots): s0 29.3, s1 Kameari 28.8 (worst 10-flip window 12.8), s6 Namek 22.1, s7 29.5, s11 28.5 f/s. s6 has a slow stretch ~162-182 s into the run at 3-8 f/s (10 flips per 1.2-3.3 s). Asked the AI assistant for per-stage fight-only f/s (+Hidden Leaf, Alabasta) and, if in the fight, a Namek DRAW_SAMPLE run.
- v4 (§42): safe but neutral, stays off by default.

## 42. Real frame rates without PERF (2026-10-26, the AI assistant; perf\survey_noperf2; HEAD f33a079, REC_DEDUP on)
TOUR_NOPERF=1 (new switch in menu_tour_play.sh / demo_shots_long.sh; both scripts had always forced PERF=1),
FIXED_CHARS=1, GCM_FLIPCOUNT=1, 70 s of fight, boost on:
Hidden Leaf 29.1 f/s (city view, 100% 2-vblank frames), Yoshiwara 29.3, Kameari 29.6, Namek 29.6,
Alabasta 29.3, demo 29.2 (last 120 s). min5s 28 everywhere, p95 of 10 flips 359-375 ms, no stalls.
Pictures clean (survey_sheet.png). So the game holds ~30 f/s on all tested maps in normal play;
the low numbers of the last days were PERF's own per-draw timers (§36) plus the log lines (2cfb5e0).
(The first survey, perf\survey_noperf, is invalid: a leftover game process fed all runs.)

## 43. REC_DEDUP test (2026-10-26, the AI assistant; perf\recdedup; f33a079, PERF=1 with perf-cheap fa111b6)
Hidden Leaf FIXED_CHARS=1, 2 pairs: on 29.1 / 29.6 f/s, off 29.0 / 28.1. [PERF6] record at similar draws
(on1 1861 vs off1 1857 draws/batch): 3.0 vs 2.9 ms; drawfit pooled busy@2000 on 20.0 vs off 19.6 ms.
Neutral, pictures clean (rd_sheet.png). With perf-cheap, PERF=1 runs now show ~29 f/s and ~9 us/draw
(was 11-14 with the cycle-time calls).
- Paused 2026-10-27 01:00: audio ctx+0x14 fix with the AI assistant (AUDIO.md pass 6); speed next steps in §43.
- Paused 2026-10-27 (project paused by the maintainer). Open speed items: §42-43.
- Paused 2026-10-28 (the maintainer). Done today: patches\res-scale-speed (4K: already 27-30 f/s; ranked exact speed-ups, flat-ground rule). Nothing pending.
