# What the main thread really waits on, and the next patch (2026-10-20)

Source: `perf/main-wait-2026-10-20` (chain sampler, `PS3_POLLTOP`) plus the lifted code in
`jstars\port\recompiled`. Read-only analysis plus one opt-in patch. Nothing was run by me.

## 1. The "SPU job" chain is folded-stub noise (code)

- **The chain:** the top chain was `spu_LS_00005F8C <- func_004FCE3C <- func_0032DDD8 <- func_006507DC`.
- **The stub:** `func_004FCE3C` is `{ r3 = 0; return; }` (`ppu_recomp_026.cpp`). The linker folds
  thousands of identical stubs onto one host address, so the sampler can't tell which guest
  function that address is. `ppu_loader.cpp` itself warns: *a hot empty stub is the signature of
  folding, not a finding*. The SPU name next to it comes from the same stack scan and is not
  evidence of an SPU wait.
- **The real frame** is `func_006507DC`. The poll-site histogram confirms it independently: all
  three of the main thread's sleep sites (`lr` 0x00650820, 0x00650854, 0x006508A0) are inside
  it.

## 2. `func_006507DC` is the game's flip pacing loop (code + measured)

`ppu_recomp_033.cpp`, import NIDs resolved from `ppu_hle_nids.cpp`:
```
while (cellGcmGetFlipStatus() != 0) {        // wait: previous flip DONE
    every 5th try: func_00570888();          // pump
    sys_timer_usleep(200);
}
cellGcmResetFlipStatus();
func_00576E78(...); func_005764C8(...);      // queue this frame's flip + flush (from the call
                                             // chain in threads_qpc_sample.txt)
if (interval == 1)
    while (*vblank_counter < 1) { func_00570888(); sys_timer_usleep(200); }
*vblank_counter = 0;
```
- **Time spent:** the poll counts are 4,138 + 1,109 sleeps (~1.05 s per 5 s) in the flip wait and
  3,901 (~0.78 s per 5 s) in the vblank wait. That is **~37% of the main thread's time**, and
  ~60% of its samples are in this function.
- **Reading:** the main thread has finished its frame's work and is waiting for the **previous
  frame's flip**. That flip completes only after the present thread has drained and presented
  that frame and the next 60 Hz tick has passed. So the guest is waiting on the renderer, not on
  SPU emulation.
- **Timing per frame** (inferred from the code order):
  - the game flushes frame N right after flip N-1 is DONE;
  - the renderer drains and presents frame N (~22-26 ms busy: drain ~18-20, render ~5);
  - the next tick marks flip N DONE.
- **What that needs:** for a steady 30, *flush noticed → drained → presented → tick* has to fit
  in 33 ms every frame.

## 3. The other two findings are not on the critical path (code)

- **52,566 sleeps per 5 s on tid 18, `lr=0x00538404`:** this is `func_005383D4`, which loops
  `while (!lookup()) usleep(20)`. `lookup()` (`func_005382EC`) is lock, table check, unlock. It is
  the **CRI file-server thread** (`func_0052E594`, "sp_file_server_load_thread") idling while it
  waits for a request. That costs CPU on one core and nothing else.
- **"queue 3 FULL -> EBUSY" (5,082) and queue 2 (1,909):** queues 2-7 are created with **size 1**
  (`[evt] queue_create ... size=1`), the usual kick pattern. One pending event already wakes the
  receiver, so an extra send failing with EBUSY loses nothing. **Benign** (inferred from the
  sizes).

## The patch: `put-poll.patch`, opt-in with `GCM_PUT_POLL=<us>`, e.g. 200

- **Where:** `libs/video/cellGcmSys.c`, inside `cellGcm_fifo_kick_wait`, the present loop's idle
  wait.
- **What it fixes:** the game's flush is a plain store to the GCM control register `put`, which
  nothing can be signalled from. So the present thread only notices a new frame when its idle
  wait times out, up to 4 ms later.
- **What it does:** with the switch, the wait checks `put` every `<us>` µs and returns as soon as
  the guest has written past what the walker consumed. A `(put, get)` pair that didn't advance
  the walker last time is not reported again, so a parked walker doesn't spin the loop. The kick
  event still works as before.
- **Expected:** it removes up to 4 ms (average ~2 ms) from every frame's critical path, plus the
  bad cases where a late notice pushes a frame past the second vblank. This is inferred. The
  cost is the present thread yield-spinning while idle (one core, `SwitchToThread`).
- **Checked:** `cellGcmSys.c` compiles with no new warnings, and `git apply --check` passes on
  0f8af23.

Suggested run: `VBLANK_QPC=1 GCM_PUT_POLL=200 PERF=1`. Watch `[PERF4]`, which should move toward
all intervals in 20-37 ms, and `[PERF2] rest`.

## After that, toward a reliable margin

The renderer path (drain ~18-20 ms + render ~5 ms) is now the frame's critical path. **Indexed
native strips with GPU strip-cut** would cut the ~770k output vertices per frame roughly in half.
That shortens both the drain's list building (~9 ms) and the upload. It is the next real speed
patch, and a bigger one: replay plus PSO `IBStripCutValue`.
