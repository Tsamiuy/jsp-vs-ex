# Stage survey (started 2026-10-21)

Method: `tools\menu_tour.sh stages\sNN NN` (Free Battle, random characters; on Stage Select
CROSS, RIGHT NN times, CROSS, START). Fixes on: defaults (zeta, shadows, FACING, MENU_FMT16,
FP_REGS64, speed patches) + `MENU_A2C=1 MENU_TEX_PITCH=1 TEXFMT_SKIP=1`. Per stage folder:
`10_stage.png` (name on the select screen), `13/14_intro_*.png`, `15_fight_start.png`,
`16_fight_20s.png`, `18_fight_35s.png`, `stdout/stderr.txt`. sNN = RIGHT presses from the default
stage (Yoshiwara Paradise = s00, see `menu\tours\t5_newdefault`).

All finished stages: `[FP] PS compile FAIL` 0, `[TEXFMT-SKIP]` 0.

| # | stage (from 10_stage.png) | fight fps | look (from screenshots) |
|---|---|---|---|
| s00 | Yoshiwara Paradise (Gintama) | ~23-26 | mostly fine now; a dark floor area on one side |
| s01 | Kameari Police Station (Kochikame) | ~19-25 | very clean in the fight; **intro cards: blocky, stair-stepped cloud edges and a patchy tower texture** (the maintainer saw it too) |
| s02 | Tenraikyousintojo | ~30 | dark stone walls and bars; plausible, very dark, needs RPCS3 comparison |
| s03 | Temple of Athena (Saint Seiya) | ~24-30 | **streaky diagonal lines across the night sky**; floor fine |
| s04 | Vegetable Sky (Toriko) | ~22-24 | colours and foliage right; leaves cut out cleanly |
| s05 | Penguin Village (Dr. Slump) | ~27-30 | mostly right; **sky edges and palm trees blocky/stair-stepped** |
| s06 | Planet Namek (Dragon Ball) | ~19-22 | ground and water **grainy teal**, grass neon (likely no mipmaps) |
| s07 | Hidden Leaf Village (Naruto) | ~17-19 | looks right: buildings, Hokage rock, shadows; slowest stage |
| s08 | Soul Society (Bleach) | ~28-29 | looks right: sky, rooftops, castle |
| s09 | Dark Tournament Stadium (Yu Yu Hakusho) | ~22-27 | dark arena with rubble; **some rubble faces flat black/grey**; big screen shows the fight |
| s10 | Inferno Crags | ~22-23 | looks right: cliffs, buildings, shadows |
| s11 | Alabasta (One Piece) | ~15-21 | buildings and sky right; **palm leaves blocky/stair-stepped** (same family as s01/s05) |

**12 stages in all** (s12 wrapped back to Kameari; after the last stage the list has "Random
Stage"). The maintainer: the last few stages in the list may need unlocking through gameplay; the 12 above are
what this save offers. All 12: `[FP] PS compile FAIL` 0, `[TEXFMT-SKIP]` 0.

Patterns: blocky/stair-stepped sky and palm/tree edges (s01 intro, s05, s11) look like a texture read with
wrong filtering/size (point sampling on unit 0, or a low-res mip); grainy distant ground (s06) fits
the missing-mipmaps gap.

## Next
1. Test `TEX_MIPS=1` (Namek ground, everywhere), then `S0_LINEAR=1` (s01 intro sky, s05, s11 palms).
2. Compare with RPCS3 where shots exist (s02 and s09 are very dark).

2026-10-21: TEX_MIPS is now the default; s06 Planet Namek ground is no longer grainy (stages/texmips/s06).
