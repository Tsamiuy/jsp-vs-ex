# Speed baseline, 2026-10-20 (target: steady 30 fps)

Build: `ps3recomp-src` jstars-port **62ec701** (all current defaults on: native strips, mid-frame
flush, FP export bit, texture gamma, 512 slots, zeta targets v5, shadow passes, FACING) +
`jstars\port\main.cpp` with kick-wait and the sampler start call.
Runs: `run_test.ps1 300`, `PAD_SCRIPT=31:0x4000 PERF=1 RSX_FRAMELOG=20`, `PS3_VERBOSE=0` (script
default). Summary: `python3 docs\archive\jstars-notes\tools\perfbase.py stderr_runN.txt 5800` (windows after
backend batch 5800 = demo intro cards).

| | run 1 | run 2 |
|---|---|---|
| batches/s (mean) | 16.03 | 16.01 |
| guest frames per batch | 0.999 | 1.000 |
| **guest fps** | **~16.0** | **~16.0** |
| frame time | ~62 ms | ~62 ms |
| VP vertices per batch | ~841k | ~842k |
| `[PERF]` vtx share / cycles per vertex | 50% / 88 | 53% / 93 |
| `[PERF]` render_frame | 19% | 15% |
| `[PERF2]` drain / present / rest | 77% / 0% / 4% | 82% / 0% / 3% |

Reading:
- One batch is one guest frame here (the mid-frame flush rarely triggers now), so batches/s is the
  game's fps: **~16 fps, 62 ms per frame. 30 fps needs ~33 ms.**
- Half the frame is vertex work: ~840k VP vertices per frame at ~90 cycles each.
- The present loop barely waits (`rest` 3-4%); the drain (command decode + vertex fetch) is ~80%.
- The two runs agree within 0.1 fps, so a 1 fps change from a patch is measurable.
