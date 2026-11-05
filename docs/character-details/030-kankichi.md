# Ryotsu (character 030)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `030_kankichi_mp_param_attack_PS3` inside `battle_character_030_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 70 / 35 | 70 | 5 |
| 1 | 100 / 50, 100 / 50, 100 / 50 | 300 | 5 |
| 2 | 40 / 20, 40 / 20, 40 / 20, 40 / 20, 40 / 20, 40 / 20, 40 / 20 | 280 | 28 |
| 3 | 50 / 25, 50 / 25, 50 / 25, 50 / 25, 50 / 25, 50 / 25, 50 / 25 | 350 | 15 |
| 4 | 50 / 25, 50 / 25, 50 / 25, 50 / 25, 50 / 25, 50 / 25, 50 / 25 | 350 | 0 |
| 5 | 300 / 150 | 300 | 0 |
| 6 | 100 / 50, 100 / 50 | 200 | 20 |
| 7 | 300 / 150 | 300 | 0 |
| 8 | 300 / 150 | 300 | 0 |
| 9 | 300 / 150 | 300 | 0 |
| 10 | 300 / 150 | 300 | 0 |
| 11 | 300 / 150 | 300 | 0 |
| 12 | 100 / 50, 250 / 125 | 350 | 0 |
| 13 | 320 / 160 | 320 | 400 |
| 14 | 300 / 150 | 300 | 0 |
| 15 | 100 / 50, 100 / 50, 100 / 50 | 300 | 0 |
| 16 | 300 / 150 | 300 | 0 |
| 17 | 300 / 150 | 300 | 0 |
| 18 | 200 / 100, 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10 | 300 | 45 |
| 19 | 300 / 150 | 300 | 0 |
| 20 | 300 / 150 | 300 | 0 |
| 21 | 100 / 50 | 100 | 20 |
| 22 | 100 / 50 | 100 | 0 |
| 23 | 300 / 150 | 300 | 0 |
| 24 | 200 / 100 | 200 | 50 |
| 26 | 2000 / 1000 | 2000 | 0 |
| 27 | 250 / 75 | 250 | 250 |
| 28 | 250 / 75 | 250 | 500 |
| 29 | 40 / 12, 40 / 12, 40 / 12, 70 / 21, 70 / 21, 70 / 21, 70 / 21, 70 / 21, 140 / 42 | 610 | 700 |
| 30 | 150 / 45 | 150 | 120 |
| 31 | 30 / 9, 30 / 9, 30 / 9, 30 / 9, 30 / 9, 30 / 9 | 180 | 420 |
| 32 | 30 / 9, 30 / 9, 30 / 9, 30 / 9, 30 / 9, 30 / 9, 30 / 9 | 210 | 490 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 0 | 400 / 40 | 0 |
| 1 | 400 / 40 | 0 |
| 2 | 400 / 40 | 0 |
| 3 | 400 / 40 | 0 |
| 4 | 400 / 40 | 0 |
| 5 | 50 / 40 | 0 |
| 6 | 50 / 40 | 0 |
| 7 | 50 / 40 | 0 |
| 8 | 30 / 24 | 0 |
| 9 | 30 / 24 | 0 |
| 10 | 70 / 7 | 5 |
| 11 | 70 / 7 | 0 |
| 12 | 10 / 1 | 0 |
| 15 | 300 / 30 | 0 |
| 17 | 300 / 30 | 0 |
| 20 | 35 / 17.5 | 50 |
| 21 | 10 / 5 | 0 |
| 22 | 10 / 5 | 0 |
| 23 | 10 / 5 | 0 |
| 25 | 35 / 17.5 | 0 |
| 26 | 10 / 5 | 0 |
| 27 | 10 / 5 | 0 |
| 28 | 10 / 5 | 0 |
| 31 | 35 / 17.5 | 0 |
| 32 | 10 / 5 | 0 |
| 33 | 10 / 5 | 0 |
| 35 | 300 / 30 | 0 |
| 36 | 300 / 30 | 0 |
| 37 | 300 / 30 | 0 |
| 38 | 300 / 30 | 0 |
| 39 | 300 / 30 | 0 |
| 40 | 10 / 1 | 0 |
| 41 | 5 / 0.5 | 0 |
| 42 | 5 / 0.5 | 0 |
| 43 | 5 / 0.5 | 0 |
| 45 | 10 / 5 | 0 |
| 46 | 10 / 1 | 0 |
| 47 | 5 / 2.5 | 0 |
| 48 | 5 / 2.5 | 0 |

## Stamina costs

From `param_action`: +0x0C is the cost, on 48-byte rows in groups of 4. Action = row / 4. Which skill uses each action is only known after a logged session. A negative number has not been tested; it may give Stamina back.

| Action | Stamina |
|---|---|
| 11 | 50 |
| 16 | 100 |
| 29 | 50 |
| 30 | 50 |
| 31 | 50 |
| 32 | 50 |
| 85 | 50 |
| 86 | 50 |
| 87 | 50 |
| 88 | 400 |
| 89 | 300 |
| 90 | 100 |
| 93 | 200 |
| 106 | 10 |
| 131 | 50 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen 1000. Hits logged 69.

Seen so far: 8 of 32 attack rows, 11 of 39 projectiles, 5 of 15 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 70 / 35 | 5 | Square, Square [charged] |
| 1 | 0 | 100 / 50 | 5 | Square [charged] |
| 2 | 0 | 40 / 20 | 4 | Square [charged] |
| 2 | 1 | 40 / 20 | 4 | Square [charged] |
| 2 | 2 | 40 / 20 | 4 | Square [charged] |
| 2 | 3 | 40 / 20 | 4 | Square [charged] |
| 2 | 4 | 40 / 20 | 4 | Square [charged] |
| 2 | 5 | 40 / 20 | 4 | Square [charged] |
| 2 | 6 | 40 / 20 | 4 | Square [charged] |
| 3 | 2 | 50 / 25 | 5 | Triangle |
| 3 | 4 | 50 / 25 | 5 | Triangle |
| 4 | 0 | 50 / 25 | 0 | Triangle |
| 4 | 1 | 50 / 25 | 0 | Triangle |
| 4 | 2 | 50 / 25 | 0 | Triangle |
| 4 | 3 | 50 / 25 | 0 | Triangle |
| 4 | 4 | 50 / 25 | 0 | Triangle |
| 4 | 5 | 50 / 25 | 0 | Triangle |
| 4 | 6 | 50 / 25 | 0 | Triangle |
| 6 | 0 | 100 / 50 | 10 | Square + R1 |
| 6 | 1 | 100 / 50 | 10 | Square + R1 |
| 21 | 0 | 100 / 50 | 20 | Square, Square [charged] |
| 24 | 0 | 200 / 100 | 50 | Circle + R1, Circle + R1 [charged] |
| 25 | 0 | 0 / 0 | 0 | Up + Circle, Up + Circle [charged] |

| Projectile | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|
| 0 | 400 / 40 | 0 | Circle, Circle + R1 |
| 1 | 400 / 40 | 0 | Circle [charged] |
| 5 | 50 / 40 | 0 | Up + Circle, Up + Circle [charged] |
| 6 | 50 / 40 | 0 | Up + Circle, Up + Circle [charged] |
| 7 | 50 / 40 | 0 | Up + Circle, Up + Circle [charged] |
| 8 | 30 / 24 | 0 | Up + Circle, Up + Circle [charged] |
| 9 | 30 / 24 | 0 | Up + Circle, Up + Circle [charged] |
| 11 | 70 / 7 | 0 | Square [charged], Triangle [charged] |
| 25 | 35 / 17.5 | 0 | Circle, Circle + R1, Circle [charged] |
| 26 | 10 / 5 | 0 | Circle, Circle + R1, Circle [charged] |
| 27 | 10 / 5 | 0 | Circle, Circle + R1, Circle [charged] |

Actions with a Stamina cost that landed a hit: 29: 50, 88: 400, 89: 300, 90: 100, 93: 200.

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Section Chief's Wrath | Press R3 during a Victory Burst. The Section Chief's wrath rains down upon Ryotsu when the timer ends. |
| Spin Top Maestro | R1 + Circle |
| Rocket Firework Launcher | Circle |
| Helicopter Assault | Left stick up + Circle |
| Stamina Charge | R1 + Cross Keep holding to charge stamina (charges quicker when health is less than 50%). |
