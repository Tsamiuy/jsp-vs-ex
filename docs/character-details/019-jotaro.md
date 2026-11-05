# Jotaro (character 019)

**Not on the roster. Real motions and an attack table, but no body model or textures on the disc (read-only check of the disc files). See the project notes.**

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `019_jotaro_mp_param_attack_PS3.d` inside `battle_character_019_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 300 / 240 | 300 | 0 |
| 1 | 300 / 240 | 300 | 0 |
| 2 | 300 / 240 | 300 | 0 |
| 3 | 300 / 240 | 300 | 0 |
| 4 | 300 / 240 | 300 | 0 |
| 6 | 240 / 192 | 240 | 0 |
| 12 | 360 / 288 | 360 | 0 |
| 13 | 360 / 288 | 360 | 0 |
| 14 | 360 / 288 | 360 | 0 |
| 18 | 300 / 240 | 300 | 0 |
| 21 | 300 / 240, 300 / 240 | 600 | 0 |
| 22 | 150 / 120, 150 / 120 | 300 | 0 |
| 23 | 300 / 240 | 300 | 0 |
| 24 | 40 / 32, 40 / 32, 40 / 32, 40 / 32, 40 / 32, 30 / 24, 30 / 24, 30 / 24 | 290 | 0 |
| 25 | 60 / 48, 60 / 48, 60 / 48, 60 / 48, 60 / 48 | 300 | 0 |
| 26 | 80 / 64, 80 / 64, 80 / 64, 80 / 64, 80 / 64 | 400 | 0 |
| 27 | 200 / 160 | 200 | 0 |
| 30 | 150 / 120, 150 / 120 | 300 | 0 |
| 31 | 180 / 144, 180 / 144 | 360 | 0 |
| 32 | 200 / 160, 200 / 160 | 400 | 0 |
| 33 | 20 / 16, 20 / 16 | 40 | 0 |
| 34 | 20 / 16, 40 / 32 | 60 | 0 |
| 36 | 200 / 160 | 200 | 0 |
| 37 | 30 / 24, 30 / 24, 30 / 24, 30 / 24, 30 / 24, 30 / 24, 30 / 24, 30 / 24 | 240 | 0 |
| 38 | 150 / 120 | 150 | 0 |

## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 0 | 10 / 0 | 0 |
| 1 | 10 / 0 | 0 |

## Stamina costs

From `param_action`: +0x0C is the cost, on 48-byte rows in groups of 4. Action = row / 4. Which skill uses each action is only known after a logged session. A negative number has not been tested; it may give Stamina back.

| Action | Stamina |
|---|---|
| 11 | 50 |
| 16 | 80 |
| 21 | -150 |
| 29 | 50 |
| 30 | 50 |
| 31 | 50 |
| 32 | 100 |
| 83 | 1 |
| 85 | 50 |
| 86 | 50 |
| 87 | 50 |
| 88 | 100 |
| 89 | 500 |
| 90 | 600 |
| 95 | 100 |
| 96 | 200 |
| 98 | 200 |
| 106 | 10 |
| 131 | 250 |

## Logged session (2026-10-30)

Jotaro has data files but no roster slot: the game shows "なし" when he is forced in.
