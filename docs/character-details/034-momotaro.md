# Momotaro (character 034)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `034_momotaro_mp_param_attack_PS3` inside `battle_character_034_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 200 / 160 | 200 | 20 |
| 1 | 200 / 160 | 200 | 20 |
| 2 | 250 / 200 | 250 | 50 |
| 3 | 100 / 80, 100 / 80, 100 / 80 | 300 | 300 |
| 4 | 300 / 240 | 300 | 300 |
| 5 | 300 / 240 | 300 | 0 |
| 6 | 50 / 40, 50 / 40 | 100 | 20 |
| 7 | 100 / 80 | 100 | 20 |
| 8 | 300 / 240 | 300 | 0 |
| 9 | 300 / 240 | 300 | 0 |
| 10 | 300 / 240 | 300 | 0 |
| 11 | 300 / 240 | 300 | 0 |
| 12 | 200 / 160 | 200 | 200 |
| 13 | 300 / 240 | 300 | 200 |
| 14 | 250 / 200, 250 / 200 | 500 | 400 |
| 15 | 350 / 280 | 350 | 350 |
| 16 | 400 / 320 | 400 | 450 |
| 17 | 300 / 240 | 300 | 0 |
| 18 | 200 / 160 | 200 | 20 |
| 19 | 300 / 240 | 300 | 0 |
| 20 | 300 / 240 | 300 | 0 |
| 21 | 200 / 160 | 200 | 20 |
| 22 | 300 / 240 | 300 | 0 |
| 23 | 300 / 240 | 300 | 0 |
| 24 | 800 / 640 | 800 | 400 |
| 25 | 50 / 40, 50 / 40, 50 / 40, 50 / 40, 1 / 200 | 201 | 200 |
| 26 | 80 / 64, 80 / 64, 80 / 64, 80 / 64, 80 / 64, 80 / 64, 80 / 64, 80 / 64 | 640 | 640 |
| 27 | 80 / 64, 80 / 64, 80 / 64, 80 / 64, 80 / 64, 80 / 64, 80 / 64, 80 / 64 | 640 | 640 |
| 28 | 1200 / 960 | 1200 | 2000 |
| 29 | 250 / 200 | 250 | 300 |
| 30 | 150 / 90 | 150 | 120 |
| 31 | 250 / 200, 100 / 80, 100 / 80, 50 / 40, 50 / 40, 50 / 40, 50 / 40 | 650 | 30 |
| 32 | 450 / 360 | 450 | 750 |
| 33 | 500 / 400 | 500 | 1000 |
| 34 | 25 / 20, 25 / 20, 25 / 20, 25 / 20, 25 / 20, 25 / 20, 25 / 20, 25 / 20 | 200 | 640 |
| 35 | 100 / 80 | 100 | 2000 |
| 36 | 80 / 64, 80 / 64, 80 / 64, 80 / 64, 80 / 64, 80 / 64, 80 / 64, 80 / 64, 80 / 64, 80 / 64 | 800 | 800 |
| 37 | 400 / 320 | 400 | 400 |
| 38 | 50 / 10000, 50 / 10000, 50 / 10000, 50 / 10000, 1 / 200 | 201 | 200 |
| 39 | 80 / 64, 80 / 64, 80 / 64, 80 / 64, 80 / 64, 80 / 64, 80 / 64, 80 / 64 | 640 | 640 |
| 40 | 400 / 320 | 400 | 100 |
| 41 | 40 / 32, 40 / 32, 40 / 32, 40 / 32, 40 / 32, 40 / 32, 40 / 32, 40 / 32, 40 / 32, 40 / 32 | 400 | 640 |
| 42 | 40 / 32, 40 / 32, 40 / 32, 40 / 32, 40 / 32, 40 / 32, 40 / 32, 40 / 32, 40 / 32, 40 / 32 | 400 | 640 |
| 43 | 400 / 320 | 400 | 2000 |
| 45 | 300 / 240 | 300 | 0 |
| 46 | 400 / 320 | 400 | 0 |
| 47 | 250 / 200 | 250 | 250 |
| 48 | 800 / 640 | 800 | 400 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 0 | 700 / 525 | 500 |
| 1 | 700 / 525 | 500 |
| 2 | 700 / 525 | 500 |
| 3 | 700 / 525 | 500 |
| 4 | 700 / 525 | 500 |
| 5 | 35 / 26.2 | 0 |
| 6 | 35 / 26.2 | 0 |
| 7 | 10 / 7.5 | 450 |
| 8 | 10 / 7.5 | 450 |

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
| 88 | 600 |
| 89 | 400 |
| 90 | 600 |
| 92 | 150 |
| 106 | 10 |
| 131 | 50 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen 2000. Hits logged 55.

Seen so far: 12 of 48 attack rows, 0 of 9 projectiles, 4 of 15 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 200 / 160 | 20 | Square, Square [charged] |
| 6 | 0 | 50 / 40 | 10 | Square + R1, Square + R1 [charged] |
| 6 | 1 | 50 / 40 | 10 | Square + R1, Square + R1 [charged] |
| 12 | 0 | 200 / 160 | 200 | Triangle, Triangle [charged] |
| 13 | 0 | 300 / 240 | 200 | Triangle |
| 14 | 0 | 250 / 200 | 200 | Triangle |
| 14 | 1 | 250 / 200 | 200 | Triangle |
| 21 | 0 | 200 / 160 | 20 | Square, Square [charged] |
| 24 | 0 | 800 / 640 | 400 | Circle, Circle [charged] |
| 25 | 0 | 50 / 40 | 50 | Circle, Circle [charged] |
| 25 | 1 | 50 / 40 | 50 | Circle, Circle [charged] |
| 25 | 2 | 50 / 40 | 50 | Circle, Circle [charged] |
| 25 | 3 | 50 / 40 | 50 | Circle, Circle [charged] |
| 27 | 2 | 80 / 64 | 80 | Circle |
| 27 | 3 | 80 / 64 | 80 | Circle |
| 27 | 4 | 80 / 64 | 80 | Circle |
| 28 | 0 | 1200 / 960 | 2000 | Circle + R1, Up + Circle [charged] |
| 39 | 0 | 80 / 64 | 80 | Up + Circle |
| 39 | 1 | 80 / 64 | 80 | Up + Circle |
| 39 | 2 | 80 / 64 | 80 | Up + Circle |
| 39 | 3 | 80 / 64 | 80 | Up + Circle |
| 39 | 4 | 80 / 64 | 80 | Up + Circle |
| 39 | 5 | 80 / 64 | 80 | Up + Circle |
| 39 | 6 | 80 / 64 | 80 | Up + Circle |
| 39 | 7 | 80 / 64 | 80 | Up + Circle |
| 45 | 0 | 300 / 240 | 0 | Triangle + R1, Triangle + R1 [charged] |

Actions with a Stamina cost that landed a hit: 29: 50, 88: 600, 89: 400, 131: 50.

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Wang-hu Temple Arcane Art: Rising Ki Tiger Spirit | Press R3 during a Victory Burst. |
| Mystic Crane Spindle | Left stick up + Circle (Tap repeatedly to keep firing / Also possible while jumping) |
| Ki Tactics: Stiff Cloth Fist Breaker | Circle |
| Infinite One-Sword Style: Mind's Eye | Press R1 + Circle to sheathe Momo's sword, and Circle to unsheathe it. |
| Stamina Charge | R1 + Cross Keep holding to charge beyond the Stamina Gauge limit. |
