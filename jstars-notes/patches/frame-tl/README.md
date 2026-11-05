# Game/renderer hand-off: FRAME_TL timeline + GAME_SAMPLE sampler (2026-10-26, diagnostic)

**`game-wait.patch`** has both parts:
- `cellGcmSys.c` +188;
- `rsx_d3d12_backend.c` +125/-87 (it reworks the DRAW_SAMPLE block).

It applies to `ps3recomp-src` at edf0bad (`git apply --check` passes). Each file compiles with the
same warnings as before. **Not built or run in the game.** the maintainer approved both (2026-10-26).
Everything is off unless switched on. `frame-tl.patch` is the FRAME_TL part alone.

## Why (CPU_PROFILE §37)
The game's frame loop (func_006507DC):
1. waits for flip DONE;
2. requests the flip and flushes;
3. waits a vblank;
4. builds the next frame;
5. polls for DONE again.

DONE comes on the first 60 Hz tick after the request. So a build longer than ~16.7 ms costs a whole
extra vblank (a 50 ms frame), however fast the drawing thread is. In drawsample\nods the drawing
thread was busy only 18-22 ms per batch, yet 3-9 of every 20 batches took 37-54 ms.

## FRAME_TL=1: one `[FTL]` line per 60 flips (stderr)
`[FTL] 60 flips | interval A ms (max), N over 37 ms: build>16.7 a, walker>33 b, both c, neither d | vbl | build B (cpu C, max, n over 16.7) | flip wait | walker W (max, n over 33, n not seen) | present P`
- **interval**: from one flip request to the next. Over 37 ms means a missed 30 f/s slot. The
  over-37 frames are split by cause:
  - the game's build was over 16.7 ms;
  - the drawing thread reached the flip marker more than 33 ms after the request;
  - both;
  - neither.
- **vbl**: from the request to the first tick after it.
- **build**: from that tick to the main thread's first flip-status poll.
  - **cpu** is the main thread's own CPU time in that span (thread cycle time).
  - build minus cpu is time blocked: SPU jobs, labels, sleeps.
- **flip wait**: from the first poll to the poll that sees DONE.
- **walker / present**: from the request to the drawing thread decoding this flip's marker, and to
  the flip being taken for present. Each frame is counted 6 requests later, so a game running ahead
  still gets its walker time.

Offline (`test/test_ftl.c`: the same block driving a model of the loop with known times; the build
is 12 ms, or 22 ms for 1 frame in 4, with 2/3 of it CPU spin, and the walker is 20 ms after each
request):
- build averaged 15.6-15.9 ms. That is the model's 14.5 plus Sleep overshoot;
- cpu averaged 11.3-12.0 ms, with ~1 in 4 frames over 16.7 ms;
- walker averaged 20.0-20.1 ms, with 0 frames not seen.

## GAME_SAMPLE=1: the DRAW_SAMPLE sampler on the game's main thread
- Started once, from the first `cellGcmGetFlipStatus` poll after the backend renders, on the
  polling thread. That is the frame loop's thread, which also flushes the FIFO.
- Output goes to `GAME_SAMPLE_OUT` (default `game_samples.txt`), in the same format as
  `draw_samples.txt`.
- `ds_sym.py game_samples.txt <exe> first last` names the recompiled functions (func_XXXXXXXX)
  and the HLE calls the waits come from.
- One sampler thread now serves both threads, each with its own table. `DRAW_SAMPLE=1` works as
  before, and the two can run together.

Offline (`test/test_ds2.c`, both instances at once, 2 windows of ~1,950 samples each):
- draw side as before: spin 66.7% / post 25.2% / wait 8%, against measured 52.6 / 27.0 / 20.4.
  Waits are under-counted, as before;
- game side, a test thread doing 3 ms of spin then a 2 ms Sleep: 37.6% ntwait, all charged to that
  thread.

## Run for the AI assistant
Hidden Leaf FIXED_CHARS=1, `PERF=1 GCM_FLIPCOUNT=1 FRAME_TL=1 GAME_SAMPLE=1`, 1-2 runs:
- the `[FTL]` lines from the fight part (stderr);
- `python ds_sym.py game_samples.txt <same build's exe> <first fight window> <last>`.

One run with only `FRAME_TL=1` would show whether the sampler changes the build times.
