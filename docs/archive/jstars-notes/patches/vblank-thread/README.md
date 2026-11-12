# VBLANK_THREAD: the 60 Hz tick on its own thread (2026-10-26)

`vblank-thread.patch` (+~60 lines, `jstars\port\main.cpp` only, opt-in with `VBLANK_THREAD=1`)
applies to the current main.cpp (`patch --dry-run -p1` OK from jstars\port). It compiles (clang-cl
/Zs) with the same warnings as before (none). **Not built or run.** Needs the maintainer's yes before it is
applied.

## Why (CPU_PROFILE §38-39)
`frame_clock` is the drawing thread. It also fires the 60 Hz vblank/flip tick, but only between
drains: while it decodes and draws a frame (walker 16-18 ms on Hidden Leaf), no tick fires. The late
ticks then come in a burst.

J-Stars waits for a vblank after every flip request before it flushes the next frame.

**FRAME_TL measured `vbl` (request -> first tick after it) at 20.1-23.2 ms on average. On-time ticks
allow at most 16.7 ms**, so the ticks are late, and the game's flush of the next frame is late with
them. Meanwhile:
- the game's own build was only 0.3-1.5 ms;
- the main thread mostly sleeps (GAME_SAMPLE: sys_timer_usleep 43-54%).

So the frame loop isn't slow. It waits for a tick that the busy drawing thread hasn't fired.
Intervals averaged 34-36 ms, with 3-13 of every 60 over 37 ms. Most of those had neither cause
FRAME_TL can see (build or walker), which fits a late tick (*inferred*).

## What it does
`VBLANK_THREAD=1`:
- A new thread fires `cellGcmTickVBlank` + `cellGcmTickFlip` every 1000/60 ms by QPC, on a
  high-resolution timer, and kicks the drawing thread. This is the same work the frame clock's QPC
  tick does now.
- The frame clock then only waits for a kick (tick or guest) and drains and presents as before.
  The present order is unchanged, because the outer cadence presents a pending flip before draining.
- Off (default): exactly the current loop.

## Risks to watch
- Ticks no longer wait for the drain, so the game may run further ahead of the drawing thread.
  J-Stars can already run 2 flips ahead (FINDINGS 2026-10-24, beam VFX).
- Check skills and effects, and the FIFO ring (no "recycled" or ring warnings), not just f/s.

## Run for the AI assistant
Hidden Leaf FIXED_CHARS=1, `PERF=1 GCM_FLIPCOUNT=1 FRAME_TL=1`, alternating `VBLANK_THREAD=1` vs
off, 2-3 each. Compare:
- real f/s (flips);
- [FTL] `vbl` (should drop to ~8 ms average, max 16.7), intervals, and over-37 counts;
- [PERF4] batch intervals;
- screenshots: a skill or beam, if the tour has one.

## Also noted, not drafted
- FRAME_TL `present` lag and the `walker` lag under GAME_SAMPLE are counting artifacts. Several
  flips can share one present, and a flip that doesn't go into the FIFO has no marker, so the
  counters drift until the 8-frame catch-up.
- `[evt] port_send(port=2): queue 2 FULL -> EBUSY` is logged ~4,100-4,300 times per run, ~3% of the
  frame-loop thread (fprintf). A rate limit would be a cheap fix.

## v2 (2026-10-26, after §40): present gate
v1 was unsafe (§40): one run stuck on a black screen, one with a black-box HUD and broken geometry.
v2 adds a **present gate**. With `VBLANK_THREAD=1`, a FIFO flip completes (flip status DONE) only
after the frame clock has **presented** that frame, not just on the next tick.
- That is the hardware contract the game double-buffers on. When flip N is done, frame N has been
  drawn and its buffers may be rewritten. So the game can be at most one frame ahead, as on a PS3.
- Flips that go unpresented for `PRESENT_GATE_MS` (default 100) are released, so the game can never
  hang on the gate. The same safety valve exists in GCM_FLIP_AT_WORD. It logs
  `[cellGcmSys] present gate: ... releasing`.
- `PRESENT_GATE=0` turns the gate off (= v1), for comparison only.

Files:
- `present-gate.patch`: ps3recomp-src `cellGcmSys.c` +36/-1, on 6c7887a (`git apply --check` OK, same
  warnings).
- `vblank-thread-v2.patch`: `jstars\port\main.cpp`, on top of the v1 code that is in main.cpp now
  (`patch --dry-run -p1` OK, compiles with no warnings). It turns the gate on with the thread and
  reports presents to it after each present.

Offline (`test/test_gate.c`: the gate as patched, inside a model of the tick, the game loop and a
drawer):
- 0 run-ahead violations in 400 flips. Every flip was requested only after the one before it was
  presented, or released.
- Drawing 20 ms per frame gave flips 33.2-33.3 ms apart. Drawing 40 ms per frame gave 50.1-50.2 ms.
- A 300 ms drawer stall was released after 100 ms, twice (it lasted 300 ms).

**vt1's black screen** (perf\vblankthread\vt1): the game did not hang. It kept flipping (18,370 flips
at 621 s) and drawing ~515-602 objects per batch. The screen stayed black from the intro on, and
`[evt] port_send(port=3) queue 3 FULL` was logged 15,511 times, against ~6,700-7,000 in vt2/off1.
The cause is not proven. Most likely it is the same run-ahead, for example a frame whose input (an
exposure or fade value) was read before it was drawn (*inferred*). The gate removes the run-ahead,
so the v2 runs should show whether it comes back.

### Run for the AI assistant (after the maintainer's yes)
Hidden Leaf FIXED_CHARS=1, `PERF=1 GCM_FLIPCOUNT=1 FRAME_TL=1`, alternating `VBLANK_THREAD=1` and
off, 3 each:
- real f/s; [FTL] vbl, intervals and over-37 counts;
- any `present gate ... releasing` lines (there should be few);
- the port-3 FULL count;
- screenshots looking at the city (HUD, geometry) and whether the fight starts.

## v3 (2026-10-26, after §41b): allow the newest flip to stay unpresented
v2 stalled every flip on the 100 ms release (§41b):
- [FTL] interval 125 ms and flip wait ~101-122 ms;
- every release logged "1 flip not presented";
- boot logos ran at ~8 f/s.

Reading: J-Stars' flip marker for frame N reaches the walker only with a later flush, and it makes
that flush after flip N completes. So "N must be presented before N completes" waits on itself until
the valve opens.

The v3 model (`test/test_gate3.c`) reproduces this: with the marker reaching the walker only after
the next request, **lag 0 gives exactly 125.0 ms intervals**, the same number the AI assistant measured.

`present-gate-v3.patch` (cellGcmSys.c +17/-5 on ccf1363, `git apply --check` OK, same warnings):
- **`PRESENT_GATE_LAG` (default 1)**: flip N completes once N-1 has been presented.
  - The game may then reuse frame N-1's buffers.
  - Frame N's buffers stay untouched until N is shown. That is double buffering, the bound v1
    lacked.
- The release line now prints flips requested / decoded / presented, so a stall shows which one lags.
  It also prints up to 40 lines instead of 8.
- main.cpp stays at v2 (no change needed).

Model, lag 1:
- 33.3 ms flips at 20 ms draw, 50.0 ms at 40 ms draw;
- 0 run-ahead violations (no flip was requested while more than one was unpresented);
- releases only during the 300 ms stall.

Model, lag 0: 125.0 ms every phase.

### Run for the AI assistant (after the maintainer's yes)
As v2: `VBLANK_THREAD=1` vs off, Hidden Leaf FIXED_CHARS=1, `PERF=1 GCM_FLIPCOUNT=1 FRAME_TL=1`.
- Expect flip wait near 0-17 ms and few or no `present gate ... releasing` lines.
- Check the HUD and city screenshots, and that the fight starts.
- If glitches remain with lag 1, the release lines and the [FTL] walker say whether the game still
  runs ahead.

## v4 (2026-10-26, after the v3 run): gate on "the walker has caught up"
The v3 run (28bc468, perf\vblankv3) logged "flips requested 42 decoded 39 presented 40". J-Stars'
flip markers reach the walker late. The AI assistant's `PRESENT_GATE_LAG=2` run (§41d) was no better: requested
always ran lag + 1 ahead of decoded, so any gate on presents waits on itself. The game seems to flush
the FIFO that carries flip N's marker only after an earlier flip completes (*inferred*). v4's gate
doesn't depend on markers the game hasn't flushed.

v4 keys the gate on what the old loop already guaranteed. The old loop never showed the run-ahead
glitches: it fired the tick only between drains, so at every flip completion the walker had
consumed everything flushed (get == put), and the frame had just been presented.

`present-gate-v4.patch` (cellGcmSys.c +39/-1 on 28bc468, `git apply --check` OK, same warnings):
- **`PRESENT_GATE_MODE=catchup` (new default)**: a flip completes only on a tick at which:
  - get == put;
  - every flip the walker decoded has been presented.

  It has the same 100 ms release. The release line prints get, put, requested, decoded and
  presented.
- The vblank count still ticks on time. That was the measured cost of the old loop (vbl 20-23 ms
  against at most 16.7), and v1 showed that removing it gives ~30 f/s.
- `PRESENT_GATE_MODE=lag` keeps v3 (`PRESENT_GATE_LAG`) for comparison. main.cpp stays at v2.

Not modelled offline: the model can't say how often the game's flushes leave get == put at a tick.
The release lines in the run will.

### Run for the AI assistant (after the maintainer's yes)
Same as v3, defaults (catchup). Watch:
- flip wait and interval in [FTL];
- the count of `present gate (catchup) ... releasing` lines;
- glitches (HUD, city).

If it releases often, the get/put in those lines show whether the drawing thread is behind (then the
release is a real stall), or the game keeps flushing (then the gate needs to look at flushed
markers instead).
