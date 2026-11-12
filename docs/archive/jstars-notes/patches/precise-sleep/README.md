# Precise sleep: short guest sleeps become real sleeps (2026-10-23)

`precise-sleep.patch` applies to `ps3recomp-src` at `3b68f46`; `git apply --check` passed on the
main checkout. **One file: `runtime/syscalls/sys_timer.c`.** It is runtime only: no renderer, no
`jstars\port` change. Compiled syntax-only with no warnings. **Not built or run in the game.**
**Default on.**

## What it changes

`sys_timer_usleep` with less than 1 ms used to spin on `SwitchToThread()` until the deadline, so
every guest thread polling with short sleeps used a full core. Those threads are:
- the game's main thread in its flip-pacing loop (`usleep(200)`);
- the CRI file server idling (`usleep(20)`, ~10,000 times a second);
- the CRI loader waits (`usleep(100)`).

With the patch, the thread waits on its own high-resolution waitable timer
(`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`, Windows 10 1803+) and really sleeps. Sleeps of 1 ms and
longer are unchanged.

| switch | effect |
|---|---|
| `PS3_PRECISE_SLEEP=0` | old behaviour (spin) |
| `PS3_PRECISE_SLEEP_MAX_US=<n>` | use the timer only for requests up to n µs (default 999 = all sub-ms); e.g. `50` sleeps the 20 µs idle polls but keeps the exact spin for the 100/200 µs waits |
| `PS3_PRECISE_SLEEP_SPIN_US=<n>` | spin only the last n µs of each sleep (default 0) |

If the timer can't be created (older Windows), the old spin is used.

## Measured on this PC (`test/test_psleep.c`, 2,000 calls each, with `timeBeginPeriod(1)` as the game sets)

| request | spin (old): p50 / p99 / p99.9, CPU | timer (new): p50 / p99 / p99.9, CPU |
|---|---|---|
| 20 µs | 20 / 20 / 21 µs, 77-108% of a core | 513 / 1,007 / 3,189 µs, 1-3% |
| 100 µs | 100 / 101 / 372 µs, 100% | 516 / 1,272 / 3,707 µs, 0-1% |
| 200 µs | 200 / 200 / 214 µs, 98-100% | 519 / 1,089 / 6,749 µs, 1% |

Neither path ever wakes early. The timer wakes **~0.3-0.5 ms late** typically, ~1 ms late at p99,
and 3-7 ms late at p99.9.

## What to expect, and the risk (inferred)

- **CPU:** each spinning poller drops from ~100% to a few %. CPU_PROFILE §7 counts the main thread
  (~95%) plus ~15 pollers at ~20% each, so the total should fall by several cores, from ~870% of a
  core toward ~300-400%.
- **fps:** probably unchanged, or slightly up from more boost headroom on a laptop.
- **Risk:** the main thread's flip wait now wakes ~0.3-0.5 ms after the flip, and its rare 3-7 ms
  late wakes (p99.9, about 2 a second at its poll rate) could push a frame to a third vblank. If
  `[PERF4]` shows more 37-54 ms intervals, rerun with `PS3_PRECISE_SLEEP_MAX_US=50`, which keeps
  the spin for the 200 µs pacing waits and still sleeps the file server's idle polls.

## Run for the AI assistant: demo fight before/after

Same tour as `perf\cpuprof` (Namek fight), no `PS3_SAMPLE` (the sampler costs a core), with
`tools\thread_cpu.ps1 -Start <fight s> -Window 5 -Count 12`:
1. `PS3_PRECISE_SLEEP=0` (before)
2. default (after)
3. if 2 shows more 3-vblank frames: `PS3_PRECISE_SLEEP_MAX_US=50`

Compare:
- the process total and the top threads in `thread_cpu.txt`;
- fps and `[PERF4]` over the last 40 windows;
- intervals >87 ms (freezes);
- screenshots: no visible change is expected;
- loading time into the fight: the file server now wakes up to ~0.5 ms later per poll.

## Not changed

Other spin-yields (`sys_ppu_thread_yield`, lwmutex/event short timeouts, cellSync, SPU coherency)
and the present thread's `GCM_PUT_POLL` loop still spin. They are the next candidates if the
per-thread run still shows busy pollers.
