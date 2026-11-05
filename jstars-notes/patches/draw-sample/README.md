# DRAW_SAMPLE: a stack sampler for the drawing thread (2026-10-26, diagnostic)

`draw-sample.patch` (+208, rsx_d3d12_backend.c only, off unless `DRAW_SAMPLE=1`) applies to
`ps3recomp-src` at d1ea59b (`git apply --check` passes). It compiles with the same 12 warnings.
**Not built or run in the game.**

## Why
Two drafts in a row (drawcost2, vfa-wake) were neutral in the game. Both were sized from the 40
`threads` dumps (top address only) or from a bench. Those can't say where the drawing thread's
~25% kernel time and its ntdll samples come from. This sampler measures it in the game, so the next
fix is picked from a measurement.

## What it does
- Every 0.5-1.5 ms (random, so it can't lock onto the game's own 1 ms timer waits), a sampler thread
  suspends the drawing thread (the one running `render_frame`), walks its stack and resumes it.
- Each sample is counted by:
  - where the thread was: `exe`, `ntdll` (kernel calls and ntdll code), `ntwait` (blocked in an ntdll
    wait stub), `d3d12`, `nvidia`, `other`;
  - the first two exe frames on the stack. So time in ntdll or the driver is charged to the exe
    code that called it.
- No locks while the thread is suspended:
  - unwind tables come from a module list made between samples;
  - `RtlVirtualUnwind` only reads memory;
  - the walk never reads outside the thread's own stack.
- Every `DRAW_SAMPLE_SEC` (default 10) it appends a window of counts to `DRAW_SAMPLE_OUT`
  (default `draw_samples.txt`), as exe RVAs.
- `ds_sym.py <draw_samples.txt> <exe> [first last window]` names them with llvm-symbolizer.
  Use the exe of the same build, with its pdb.
- Cost: one suspend/resume of the drawing thread per sample, ~1,000 per second (~1-2%, *inferred*).

## Offline check (test/test_ds.c: the same block on a known workload, 4,029 samples)
| | measured by the workload's own timers | sampled |
|---|---|---|
| spin_work (exe + QPC in ntdll) | 52.7% | 65.9% |
| post_jobs (SetEvent/ResetEvent syscalls) | 27.1% | 25.2% |
| blocked wait | 20.2% | 8.9% (ntwait) |

Callers come out right: spin_work's QPC time shows as ntdll charged to spin_work, and the
syscalls as ntdll charged to post_jobs. Blocked waits are **under-counted** about 2x (the sampler
and a waking thread are woken by the same timer), so read ntwait as a lower bound. Use [PERF6] for
busy time, and this for how the running time splits.

## Run for the AI assistant
Hidden Leaf FIXED_CHARS=1, `PERF=1 GCM_FLIPCOUNT=1 DRAW_SAMPLE=1`, one or two runs (fight part):
`python ds_sym.py draw_samples.txt build\...\JStarsRecomp.exe <first fight window> <last>`.
Also one run without DRAW_SAMPLE, to see the sampler's own cost in busy time at equal draws.
