# White sky flashes (Penguin Village, Kameari): sun-visibility query test switches

Patch: `zpass-report.patch` (`libs/video/cellGcmSys.c`, +17/-1), on HEAD 4b2e7c4 (`git apply --check`
passes). Syntax-only compile with the build's warnings: no errors, nothing new beyond the file's usual
kinds. Logging and test switches only; default behaviour is unchanged. Not built or run.

## Why (inferred, see STAGE_TEXTURES.md §9)

Our GET_REPORT handler answers every ZPASS (occlusion) query with 0xFFFF samples, "fully visible".
Sunny outdoor stages typically scale a sun glare / lens flare by how much of the sun is visible: the
visible sample count divided by the count expected for the sun's size on screen. 0xFFFF is far above
any real count, so the glare would jump to full white whenever the query result is read. If the
game reads it only on some frames (results arrive late and it keeps the last good value otherwise),
that is a flash. Athena's night sky has no sun, which fits it not flashing there.

## Switches

- `REPORT_LOG=1`: `[ZPASSREP] #n off=... value=... dma=...` for the first 64 ZPASS reports, then
  every 600th. This shows whether J-Stars uses ZPASS queries at all in a fight (the existing
  `[GET_REPORT]` line only prints the first six reports of the run, all zcull stats at boot).
- `REPORT_ZPASS=<n>`: report n samples instead of 0xFFFF. `0` = sun fully hidden (glare off);
  a few hundred = a plausible partly visible sun.

## Test plan

1. Penguin Village fight with `REPORT_LOG=1`: are there `[ZPASSREP]` lines during the fight? If none,
   this cause is ruled out; go to the frame-sequence capture below.
2. If there are: `REPORT_ZPASS=0`. The flashing should stop (and the sun glare disappear). Then try
   `REPORT_ZPASS=200` to see a steady, moderate glare.
3. Same on Kameari.
4. Real fix if confirmed: count samples for real, or return a value from the bounding size of the
   queried draws (ZPASS_PIXEL_COUNT_ENABLE ranges). Drafted after the test.

## Superseded (2026-10-22 evening)

Not needed: the EBOOT's only `cellGcmSetReport` caller (`func_00576808`, ppu_recomp_029) requests
zcull statistics (types 2 and 3), never a ZPASS count, so the 0xFFFF answer is never read by the
game. The real cause is the split sun-shaft fan: see `patches\sky-flash\README.md`.
