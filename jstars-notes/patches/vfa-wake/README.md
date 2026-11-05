# Vertex helper wake-ups: VFA_WAKE, VFA_SPIN, VFA_INLINE (2026-10-25)

`vfa-wake.patch` (+93/-33, rsx_d3d12_backend.c only) applies to `ps3recomp-src` at 0a30088
(`git apply --check` passes). It compiles with the same warnings as before. **Not built or run.**

## Why
When VPULL can't take a draw (512 "CPU path" draws per batch on Hidden Leaf, `[VPULL] ... layout
10240` per 20-batch window), the drawing thread queues the vertex fetch for the 3 VFETCH_ASYNC
helper threads and calls `ReleaseSemaphore` for **every** job, waking a sleeping helper each time.

Measured on the maintainer's PC (`bench/bench_vfa.c`, 512 jobs per batch, ~10 us of other work between jobs,
4 us jobs, 3 helpers):

| scheme | drawing thread's cost per job (post + sync) |
|---|---|
| today: one ReleaseSemaphore per job | **3.7-4.3 us** |
| wake only a sleeping helper, no spin | 4.0 us (helpers are always asleep by the next job) |
| + one helper polls ~1024 pauses (~30-70 us) before sleeping | 1.5-1.7 us |
| + one helper polls ~4096 pauses | 0.3-0.6 us |

4 us is about what a helper saves by doing the job (a job is one 256-index strip batch), so today
the hand-off may cost the drawing thread as much as doing the fetch itself. At 512 jobs per batch
that is ~2 ms per batch (*inferred*). `async fetch wait` in [PERF2] is ~0, so the cost is in posting,
not in waiting.

## What the patch does
| switch | default | what |
|---|---|---|
| `VFA_WAKE=0` | on | old behaviour exactly. On: a job wakes a helper only if one is asleep; helpers empty the queue before they sleep; the sync helper no longer calls `WaitForSingleObject(sem, 0)` per job it takes; the queue counters never reset (wrap-safe), so a helper still looking at the queue can't mistake a new job for an old one |
| `VFA_SPIN=<n>` | 0 | one helper at a time polls n pauses before sleeping (~30-70 ns per pause here). Costs CPU (heat) on one core while the batch draws, so off by default |
| `VFA_INLINE=<n>` | 0 | jobs under n vertices are fetched on the drawing thread right away, no hand-off. `VFETCH_ASYNC=0` (already in the build) is the "everything inline" end of this |

New per-window line: `[VFA] jobs N (V vertices avg) | worker wakes W | inline I`. It tells how many
jobs and how big they are, which decides between SPIN and INLINE.

## Offline check
`test/test_vfa_queue.c`: the patched queue logic (copied), 2,000,000 jobs, 63,106 random syncs,
3 helpers, counters starting just below the 32-bit wrap. Every job ran exactly once, every sync saw
all its jobs finished, no job ran from an unposted slot: **0 errors** in all four modes
(old; wake; wake+spin 50; wake+spin 2000). Producer wake-ups 2,000,000 (old) vs 163k-259k (wake).

## Run for the AI assistant (same power mode; compare busy at equal draws with tools\drawfit.py)
Hidden Leaf, FIXED_CHARS=1, `PERF=1 GCM_FLIPCOUNT=1`, alternating:
1. `VFA_WAKE=0` (old);
2. defaults (wake only);
3. `VFA_INLINE=100000` (all inline; same as `VFETCH_ASYNC=0`);
4. `VFA_SPIN=2000`.

Also note CPU % / temperature for 4, since it spins. If 3 wins, a later step could drop the
helpers for these small jobs; if 4 wins, SPIN can be tuned down for heat.
