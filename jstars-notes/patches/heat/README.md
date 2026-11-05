# Heat fixes: less polling, AVX2 build (2026-10-25, the maintainer's yes)

Four patches from CPU_PROFILE §26. Base: `ps3recomp-src` 8516777 and `jstars\port\CMakeLists.txt` as of
2026-10-25. **Not built or run.** Each change has an off switch or a build option.

| patch | lines | file | what | off |
|---|---|---|---|---|
| `1-evq-subms-wait.patch` | +69/-2 | runtime/syscalls/sys_event.c | a thread that loops on an event-queue receive with a timeout under 1 ms now waits its timeout, instead of getting ETIMEDOUT at once and spinning | `EVQ_SUBMS_WAIT=0` |
| `2-spurs-guard-event.patch` | +46 | libs/spurs/cellSpurs.c | job-chain walkers at a closed guard sleep on a condition variable that `cellSpursJobGuardNotify` wakes, instead of `Sleep(1)` polling | `SPURS_GUARD_EVENT=0` |
| `3-avx2-build.patch` | +29 | jstars/port/CMakeLists.txt (apply from the PS3Recomp folder, `patch -p1` or `git apply`) | build for AVX2+FMA, and `/clang:-ffp-contract=off` on every C/C++ target | `-DJSTARS_AVX2=OFF` (the contract flag stays) |
| `4-put-poll-sleep.patch` | +18 | libs/video/cellGcmSys.c | the present thread's put-poll sleeps each 200 µs step on a high-resolution timer (and wakes on a kick) instead of a `SwitchToThread` spin | `GCM_PUT_POLL_SLEEP=0` |

`git apply --check` passes for 1, 2 and 4 on the main checkout; `patch --dry-run -p1` passes for 3.
1, 2 and 4 compile with clang-cl `/Zs` with the same warnings as before.

## Details
1. **EVQ_SUBMS_WAIT.** The first empty poll of a queue by a thread still returns at once, so a
   one-off per-frame poll keeps zero latency. When the same thread polls the same queue empty again
   within 2 ms, it is a loop, and the receive then waits:
   - on a high-resolution timer for its timeout;
   - and on a per-queue auto-reset event that every push sets, so an event arriving meanwhile ends
     the wait at once.

   Targets J-Stars' two pollers in `func_0060AE04` (syscall 0x82): 38% + 21% of a core on Hidden
   Leaf.
2. **SPURS_GUARD_EVENT.** Notify updates the guard's credits under a lock and wakes all waiters.
   - The wait re-checks with a 50 ms cap, so a release that doesn't notify (Initialize, Reset) is
     still seen.
   - It gives up after 20 s, as before.
   - The credit logic is the same as the old loop.

   Targets ~14,000 wake-ups a second over ~20 `jc_thread`s. Jobs also start as soon as a notify
   lands, where the old loop could be up to 1 ms late.
3. **AVX2 build.**
   - clang-cl ignores `-ffp-contract=off` (checked: "unknown argument ignored"). With FMA available
     it fuses `a*b+c` by default (checked: `vfmadd213ss`), so the patch passes
     `/clang:-ffp-contract=off` to every target; the checked output is separate `vmulss`+`vaddss`.
   - Then `/arch:AVX2`, which turns on the existing exact AVX2 path of `spu_fa/fm/fma` and makes
     `fmaf` a single instruction.
   - Bench (`jstars-notes\tools\bench_avx.c`, bit-identical to the reference path, run with boost
     at 99%):

     | per 4-lane op | before | after |
     |---|---|---|
     | `spu_fm` | 7.1-8.0 ns | 2.8-3.2 ns |
     | `spu_fma` | 12.5-15.3 ns | 10.7 ns |
     | `vmx_vmaddfp` | ~10 ns | ~9 ns |

   - The exe then needs an AVX2 CPU (2013+); `-DJSTARS_AVX2=OFF` builds the old way.
   - It is a CMake option, so the AI assistant must reconfigure (a clean configure or `-DJSTARS_AVX2=ON`)
     and rebuild everything.
4. **GCM_PUT_POLL_SLEEP.** Same 200 µs polling of the guest's `put`, but asleep in between.
   - The timer's wake-up is ~0.5 ms typical on this machine (precise-sleep measurements), so a new
     frame can be noticed up to a few hundred µs later than with the spin. A kick still wakes it at
     once.
   - Watch fps for this one.

## Expected (*inferred*)
About 1.5-2 of the ~6.5 busy cores on Hidden Leaf:
- #1 ~0.5;
- #2 most of 0.6 kernel and the wake-ups;
- #3 ~0.5-1 of the SPU job work;
- #4 ~0.3.

The fps should be the same. CPU temperature should drop a few degrees, with quieter fans; how much
depends on the boost.

## Run for the AI assistant (normal exe, not build-pdb)
Hidden Leaf fight, `GCM_FLIPCOUNT=1 PERF=1`, `tools\thread_cpu2.ps1 -Window 5 -Count 12 -Top 40`,
and `nvidia-smi --query-gpu=timestamp,utilization.gpu,power.draw,temperature.gpu,clocks.gr
--format=csv -l 1 > gpu.csv`:
- A: all four (defaults, AVX2 build);
- B: the same exe with `EVQ_SUBMS_WAIT=0 SPURS_GUARD_EVENT=0 GCM_PUT_POLL_SLEEP=0` (shows 1+2+4
  without 3);
- C: the old build (`-DJSTARS_AVX2=OFF`) with those three switches = before. Or reuse
  perf\heat\leaf1, but that was a pdb build at 15.9 f/s.

Compare:
- flips/s, which must stay the same;
- total CPU, kernel % and wakes/s;
- the `jc_thread`, `frame_clock` and event-poller rows;
- screenshots (the AVX2 build must look identical);
- the demo fight once for the picture.
