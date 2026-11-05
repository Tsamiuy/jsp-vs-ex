# speed30 patches, 2026-10-20

Build: jstars-port 62ec701 + 445974a (`env-cache.patch`, `RSX_ENV_CACHE=1`) + 266bfc6
(`async-fetch.patch`, `VFETCH_ASYNC=1`); both opt-in. Runs as in `..\baseline-2026-10-20`
(300 s, `PAD_SCRIPT=31:0x4000 PERF=1 RSX_FRAMELOG=20`, `PS3_VERBOSE=0`), one run each.
Summary: `python3 jstars-notes\tools\perfbase.py stderr_<n>.txt 5800`.

| run | guest fps | ms/frame | vtx share / cyc per vertex | drain / rest |
|---|---|---|---|---|
| baseline (2 runs) | 16.0 | 62 | 50-53% / 88-93 | 77-82% / 3-4% |
| `RSX_ENV_CACHE=1` | **19.2** | **52** | 64% / 95 | 81% / 4% |
| `VFETCH_ASYNC=1` | 18.6 | 54 | 45% / 67 | 79% / 4% |
| both | 18.6 | 54 | 46% / 69 | 62% / **24%** |

- Each patch alone gains 2.6-3.2 fps (8-10 ms). **Together they do not add up**: 18.6 fps, and
  `[PERF2] rest` jumps from ~4% to 24%. With both on, the present thread waits a quarter of the
  time; the async fetch probably waits on something the env cache made faster (or the two fight
  over a lock/thread). To investigate before turning either on by default.
- Pictures: title, Ace and Goku cards look the same as the default build in all runs
  (`shots_*`): no flicker or stale vertex data seen in the screenshots.
