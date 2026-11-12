# Why the game side is slow: what run1 shows (2026-10-20)

Source: `run1\stderr.txt`, a 330 s demo run. Build c3eaf8b plus kick-wait, with
`PERF=1 RSX_FRAMELOG=60 PS3_SAMPLE=2`. **The run has no sampler output**: `jstars\port\main.cpp`
never calls `ps3_sampler_start()` (see `run1\README.md`). So this is what the existing logs can
answer, ranked by likely impact. Nothing here was run by me.

## The renderer is waiting on the game (measured)

These are the last windows of the run, match late in the demo, about 2.5-3 s each:

- **Speed:** `[PERF]` shows 6.5-8.3 batches/s and `guest` 96%. `vtx` is 16-19%, at about
  115 cycles per vertex.
- **Where the present thread's time goes** (`[PERF2]`):
  - **rest 66-68%**: idle waiting;
  - drain 28-29%, of which vtx 0.44 s and render_frame 4%.
  - Only **20 of ~520 drain calls per window find new commands**.

The present thread spends two-thirds of its time with nothing to do. The game's threads produce
about 7-8 batches a second, and that is the limit now.

## Ranked causes and next steps

### 1. Verbose logging is on in every measured run (measured; cost inferred)

- `run_test.ps1` redirects stderr to a file and does not set `PS3_VERBOSE`. `ps3_log_verbose()`
  (`runtime/ps3_log.h`) then defaults to **verbose**.
- In a match window that produces **~14,100 stderr lines per window, ~4,900 lines/s**:
  - **~12,750 `[spurs-job]` lines**: a `dispatch HIT` line and a `RETURNED` line for **each of
    ~2,200 SPURS jobs per second**, each followed by an explicit `fflush(stderr)`
    (`runtime/spu/spu_workload.c:858-867`);
  - ~940 `[WAIT] event_queue_receive`;
  - ~800 `[evt] port_send`.
- These are written from the guest's PPU/SPU threads through one FILE lock.
- The toolkit's own notes on this exact gate:
  - **"the logging changes what the title does"**;
  - Tokyo Jungle presented 4 frames in 40 s with the log discarded, and 1 with it redirected;
  - **"Measure timing-sensitive behaviour with PS3_VERBOSE=0."**
- The SPURS jobs are presumably the work each frame waits on (skinning and the rest), so their
  logging sits on the critical path.
- **Next step, no code change:** rerun the same demo with `PS3_VERBOSE=0` and compare `[PERF]`
  batches/s and `[PERF2]` rest.
  - If it moves a lot, every timing in FINDINGS §19-29 includes this overhead.
  - Add `$env:PS3_VERBOSE = "0"` to `run_test.ps1` for perf runs, and set it to `1` only for
    debugging.

### 2. The sampler cannot run in this port yet (measured)

- `samples\jstars-main-sampler.patch` adds the one call to `jstars\port\main.cpp`, right after
  `ppu_recomp_register()`. The sampler needs the lifted function table. `boot_main.cpp` makes
  the same call. `git apply --check` passes.
- It is a no-op unless `PS3_SAMPLE` is set.
- Run it **with `PS3_VERBOSE=0`**. Otherwise the profile mostly shows threads blocked on the
  stderr lock.
- What it reports every 10 s:
  - per OS thread: guest / ntdll / other sample counts. `ntdll` means waiting or locked; `guest`
    means game logic;
  - the top guest functions, and `spu_LS_` entries for lifted SPU jobs;
  - host modules.
- Caveat: the exe has no PDB or map, so `[samp] rva` lines can't be named. Building once with
  `/Zi` (or `-g` plus `/DEBUG`) would fix that.

### 3. SPURS job volume (measured count; cost unknown)

About 6,400 jobs per window, roughly 2,200 jobs/s or **~300 jobs per batch**. Whether they run in
parallel, or serialise on one host thread the frame waits on, is the main thing the sampler should
answer. `[spurs-job]` lines have no thread or time stamp, so the log can't tell.

### 4. Not the cause (measured)

- **Texture setup:** `[PERF3]` shows ~0.02 s per window.
- **Present / vsync:** 0.01 s.
- **Mid-frame flushes:** 0.
- **Vertex fetch:** 0.44-0.49 s of a ~2.6 s window. That is real, but the present thread is idle
  for 1.6-1.7 s of the same window.
