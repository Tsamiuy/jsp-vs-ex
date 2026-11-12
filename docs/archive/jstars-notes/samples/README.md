# Sampler runs (2026-10-20)

All: 330 s demo, `PAD_SCRIPT=31:0x4000 PERF=1 RSX_FRAMELOG=60 PS3_SAMPLE=2`, build = jstars-port
c3eaf8b + `jstars\port\main.cpp` with the kick-wait patch and the `ps3_sampler_start()` call
(old file `main.cpp.pre-sampler`).

| folder | PS3_VERBOSE | match batches/s | [PERF2] (last window) |
|---|---|---|---|
| run1 | default (on) | n/a: sampler never started | |
| run2-verbose | default (on) | ~8.0-8.8 | drain 31%, present 0%, rest 64% |
| run3 | 0 | **~18-20** | drain 83% (vtx 0.46 s), present 1%, rest 5% |

Verbose logging (~4,900 lines/s in the match, 73 MB of stderr vs 0.8 MB) more than halved the
game's speed. With it off the present loop no longer waits; the drain (vertex work about half of
it) is the main cost. Earlier timing runs in FINDINGS all had verbose logging on.
