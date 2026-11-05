# The 60 Hz flip tick runs on a 15.6 ms clock (2026-10-20)

`jstars-main-vblank-qpc.patch` applies to `jstars\port\main.cpp` as of 2026-10-20 (the file with
kick-wait and the sampler call). `git apply --check` passes. A syntax-only compile of the patched
file has no errors, only the file's usual CRT deprecation warnings. **Not built or run.**
**Opt-in:** `VBLANK_QPC=1`.

## What limits the last step to a steady 30

From `perf/strip-carry-2026-10-20`:
- **Time:** about 40 ms per frame. The present thread is busy ~23 ms of that (drain 44%, render
  13%) and idle 38-43%.
- **Frame intervals:** `[PERF4]` puts **half the frames at 20-37 ms (2 vblanks) and half at
  37-54 ms (3 vblanks)**.
- **Reading:** the renderer has spare time, so frames slip to a third vblank for some other
  reason.

## The cause found (measured, plus code)

- **The tick clock.** `frame_clock` in `main.cpp` fires the vblank/flip tick
  (`cellGcmTickVBlank` / `cellGcmTickFlip`) from `GetTickCount64()` with `next_tick += 16`.
- **`GetTickCount64` only advances in 15-16 ms steps,** even with `timeBeginPeriod(1)`.
  `tick_resolution.c` measured `15 16 15 16 16 15 ...` on this machine. So ticks fire late by up
  to a step, or two at once.
- **The flip completions follow it.** Those are what J-Stars paces its frames on (flip mode
  VSYNC), and they land on an irregular 15.6/31.2 ms grid.
- **The idle wait adds to it.** It is up to 4 ms (`cellGcm_fifo_kick_wait(4)`, measured
  `Sleep(4)` ≈ 4.7 ms) and can delay a tick further.
- **Result:** a frame whose work is ready inside 33 ms can still miss its second vblank.

**The patch:** with `VBLANK_QPC=1`, the tick runs from `QueryPerformanceCounter` at exactly
1000/60 ms, and the idle wait never runs past the next tick. Everything else in the loop is
unchanged: the order of tick → present → drain and the outer cadence. Without the switch the old
loop runs.

**Expected:** `[PERF4]` shifts from the 37-54 bucket toward 20-37, closer to a steady 30. This
is inferred, not measured. Since the timing becomes exact 60 Hz, flips may also complete slightly
later than in today's early bursts on average. If fps drops, that shows.

## Still open

- **The other half of `rest`:** what the guest threads wait on. The strip-carry run had no
  sampler output. Next run: `VBLANK_QPC=1 PS3_SAMPLE=2`, plus `-Commands threads` near the end
  (`run\dbg.txt.out` gives each thread's guest call chain).
- **Indexed strips with GPU strip-cut:** they would roughly halve vertex work and upload bytes.
  But the present thread is now idle ~40% of the time, so this matters for going **above** 30,
  not for reaching it.
