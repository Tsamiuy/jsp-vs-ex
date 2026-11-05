# GCM put-pointer poll, 2026-10-20/21

`patches\frame-pacing\put-poll.patch` applied as ps3recomp-src commit 941d257 (opt-in), then made
default-on (`GCM_PUT_POLL=200`; `GCM_PUT_POLL=0` = off) in the next commit. 300 s demo runs,
`PERF=1 RSX_FRAMELOG=20`, `PS3_VERBOSE=0`, `VBLANK_QPC` default-on. CPU via
`jstars-notes\tools\cpu_sample.ps1` (5 s windows, % of one core; 60 s = title, 210/250 s = demo).

| run | guest fps | ms/frame | `[PERF4]` last two windows (20-37 ms of 20) | CPU title / demo |
|---|---|---|---|---|
| default before (no poll) | 27.7 | 36 | 19, 18 | 297% / 935-1010% |
| `GCM_PUT_POLL=200` | 29.0 | 35 | 18, **20** | 386% / 1038-1071% |
| new default (no switches) | **29.5** (min 22.6) | **34** | 17 (+1 >87 ms), 19 | not sampled |

- Pictures: title, Ace and Goku cards unchanged in all runs.
- Cost: about one more core busy while the renderer idles (title screen); little change during the
  demo.
- Day total: 16.0 → 25.1 → 28.0 → **29.5 fps**.
