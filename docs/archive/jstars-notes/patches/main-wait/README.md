# What the game's main thread waits on (2026-10-20)

Source: `perf/vblank-qpc-2026-10-20` (`stderr_qpc_sample.txt`, `threads_qpc_sample.txt`). This
is read-only analysis plus one opt-in diagnostic patch. Nothing was run by me.

## What the data shows (measured)

- **Main guest thread (host tid 8508):** about **20-25% guest code and 65-75% in ntdll** in every
  10 s sampler report from 189 s to 300 s. It is the only thread with steady guest work, and it
  waits about 70% of the time.
- **Present thread (36580):** busy ~25-30%. It includes some `D3DCOMPILER_47` time; compiles are
  still happening during the run.
- **Guest-tids 10-16:** seven threads under `func_004D52BC → 004D429C → …`, the CRI FS worker
  chain (GAME_INTERNALS.md), are idle in `sys_lwcond_wait`. Most other guest threads sit in
  `func_0060AE04`, a common wait wrapper.
- **Hottest guest code:** SPU jobs `spu_LS_00000968` and `000032F8`, about half of the guest
  samples, and PPU `func_004FCE3C`. Guest code is only 1.7% of all samples, so no thread is
  CPU-bound.

## The one snapshot (thread dump)

- **Where the main thread was:** in `func_00571528`, called from `func_0057154C`. That is a
  **CRI loader wait loop**:
  - `func_005713E4` is "status == LOADING" and `func_0057136C` is the error check
    (GAME_INTERNALS.md);
  - the loop is `while (!error && loading) { func_00571528 (yield via func_00670DFC → CRI);
    sys_timer_usleep(100); }`, i.e. lv2 syscall 0x8D with 100 µs.
- **What that means:** in that instant the main thread was **waiting for a CRI file load to
  finish**. The capture is near the end of the demo (character cards), so it may be loading
  between cards and not the steady state. One sample can't say how often this happens.
- **Not the cause:** `sys_timer_usleep(100)` is exact in this runtime. Under 1 ms it spins with
  `SwitchToThread`, so the poll itself adds no latency. Its spin also shows as "ntdll", so part
  of the 70% "waiting" may be this poll.

## Patch: `sampler-chain.patch`, opt-in with `PS3_SAMPLE_CHAIN=1`

- **What it changes:** `runtime/ppu/ppu_loader.cpp` only.
- **What it records:** with `PS3_SAMPLE=<ms>`, each sample of the main thread is keyed by its
  first 4 distinct lifted functions (RIP, then return addresses on the stack) and marked waiting
  if the RIP is in ntdll.
- **What it prints:** each `[samp]` report adds:
  ```
  [samp] main thread: N samples, W% waiting (RIP in ntdll); top chains:
  [samp]   23.1% (… , … waiting)  func_00571528 <- func_0057154C <- …
  ```
- **What it doesn't change:** the other threads are sampled as before, and nothing is changed
  unless the switch is set.
- **Check:** a syntax-only compile of the patched file with the game's include paths had no
  errors. It applies to 1a8618e.

## Next run (no other code needed)

`VBLANK_QPC=1 PS3_SAMPLE=2 PS3_SAMPLE_CHAIN=1 PS3_POLLTOP=5`
- `PS3_SAMPLE_CHAIN` gives the main thread's top wait chains per 10 s.
- `PS3_POLLTOP=5` is built in. Every 5 s it prints the top `sys_timer_usleep` poll sites by
  thread and return address, so the CRI loader loop shows up as `lr=0x005715F8` if it runs
  every frame.

How to read it:
- **CRI loader chain on top:** loading is the limit. The next patch is on the file side: CRI
  server thread wake-up (`func_0052E594` sleeps 100 µs per state) and read sizes.
- **A flip or label wait chain on top** (`cellGcm*`, `func_0060AE04` from the render code): the
  pacing is the limit. Next: when flips complete.
- **SPU job waits on top** (`spurs`, event queues): the SPU job dispatch is the limit.
