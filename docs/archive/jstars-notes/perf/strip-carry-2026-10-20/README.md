# strip-carry, 2026-10-20

Build: jstars-port 292bdbc (`patches\strip-carry`, active with `VFETCH_ASYNC=1`, `STRIP_CARRY=0`
= off). Run: 300 s, `RSX_ENV_CACHE=1 VFETCH_ASYNC=1`, otherwise as in `..\baseline-2026-10-20`.

| run | guest fps | ms/frame | vtx share / cyc per vertex | drain / present / rest |
|---|---|---|---|---|
| baseline | 16.0 | 62 | 50-53% / 88-93 | 77-82% / 0% / 3-4% |
| env + async (speed30) | 18.6 | 54 | 46% / 69 | 62% / 0% / 24% |
| **env + async + strip carry** | **25.6** (18-29 per window) | **39** | 25% / 28 | 47% / 1% / **38%** |

- `[PERF4]` batch intervals (last windows): roughly half in 20-37 ms (30 fps pace), half in
  37-54 ms, a few 54-70 ms. Not yet a steady 30.
- `rest` 38%: the present thread now waits a large share of each window, i.e. the renderer is often
  ahead and waits for the guest to submit.
- Pictures: title, Goku card and the next card (Kumagawa) look the same as the default build
  (`shots_carry*`).
