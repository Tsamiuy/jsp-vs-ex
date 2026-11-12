# VBLANK_QPC on by default, 2026-10-20

`jstars\port\main.cpp`: `VBLANK_QPC` now defaults on (`VBLANK_QPC=0` = old GetTickCount64 tick).
Backups: `main.cpp.pre-vblank` (before the patch), `main.cpp.pre-vblank-default` (patch, opt-in).
ps3recomp-src at 0f8af23. No switches set, 300 s demo run, `PERF=1 RSX_FRAMELOG=20`.

- Log confirms `[rsx] VBLANK_QPC: vblank tick from QPC, 16.6667 ms`.
- **~28.0 guest fps, ~36 ms per frame** (windows up to 30.1).
- `[PERF4]` last two windows: **17 and 19 of 20** batch intervals at 20-37 ms (30 fps pace).
- `[PERF2]` drain 58%, present 1%, rest 28%; vtx 28% at 29 cycles/vertex.
- Pictures: title and Ace card unchanged (`shots_title\`, `shots_demo\`).

Progress today: 16.0 (baseline) → 25.1 (speed defaults) → **28.0 fps** (+ vblank timer).
