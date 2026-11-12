# vblank-qpc, 2026-10-20

`patches\vblank-qpc` applied to `jstars\port\main.cpp` (not in git; old file `main.cpp.pre-vblank`).
Opt-in `VBLANK_QPC=1`. ps3recomp-src at 1a8618e (speed patches on by default). 300 s demo runs,
`PERF=1 RSX_FRAMELOG=20`, `PS3_VERBOSE=0`.

| run | guest fps | ms/frame | `[PERF2]` drain / rest | `[PERF4]` last two windows (20-37 / 37-54 ms) |
|---|---|---|---|---|
| defaults (`..\speed-defaults-2026-10-20`) | 25.1 | 40 | 49% / 35% | 14/6, 15/5 |
| `VBLANK_QPC=1` | **26.0** | **38** | 55% / 28% | 14/6, **18/2** |
| `VBLANK_QPC=1 PS3_SAMPLE=2 -Commands threads` | 23.2 | 43 | 52% / 31% | 15/4, 12/7 (+1 at 54-70, 1 at 70-87) |

- About +1 fps and fewer 3-vblank frames; the sampler itself costs ~3 fps. One run each.
- Pictures (title, Goku card) look the same as the default build (`shots_qpc*`).
- Sampler (`stderr_qpc_sample.txt`, last report at 299.7 s): 6,461 samples, only 1.7% in guest
  code; 94.8% in ntdll (threads waiting). Hottest guest: SPU job code `spu_LS_00000968` 28%,
  `spu_LS_000032F8` 24%, PPU `func_004FCE3C` 11%.
- Thread dump (`threads_qpc_sample.txt`): guest-tids 10-16 all in `sys_lwcond_wait`; the busiest
  guest thread (host tid 8508) is in ntdll under `func_00571528` <- `func_005282DC` <-
  `func_004AD298` <- `func_00529AF4` <- `func_00527D44` <- `func_00444E44`.
- For analysis by the Vertex fetch thread.
