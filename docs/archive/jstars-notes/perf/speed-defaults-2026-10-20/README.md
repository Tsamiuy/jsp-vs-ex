# Speed patches on by default, 2026-10-20

Build: jstars-port (env cache, async fetch, strip carry now default-on; `RSX_ENV_CACHE=0`,
`VFETCH_ASYNC=0`, `STRIP_CARRY=0` turn each off). No switches set, 300 s demo run,
`PERF=1 RSX_FRAMELOG=20`, `PS3_VERBOSE=0`.

- **~25.1 guest fps, ~40 ms per frame** (windows up to 28.5); vtx 27% at 30 cycles/vertex;
  `[PERF2]` drain 49%, present 1%, rest 35%.
- `[PERF4]` last windows: 14-15 of 20 batch intervals in 20-37 ms (30 fps pace), 5-6 in 37-54 ms.
- Pictures: title, Ace card and Goku card identical in look to the default build before the change
  (`shots_title\`, `shots_demo\`).
