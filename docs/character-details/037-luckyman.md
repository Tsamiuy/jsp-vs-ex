# Luckyman (character 037)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `037_luckyman_mp_param_attack_PS3` inside `battle_character_037_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 20 / 4 | 20 | 5 |
| 1 | 20 / 4, 100 / 20, 100 / 20 | 220 | 5 |
| 2 | 20 / 4, 40 / 8, 40 / 8, 40 / 8, 40 / 8, 40 / 8, 40 / 8 | 260 | 23 |
| 3 | 40 / 8, 40 / 8, 40 / 8, 40 / 8, 40 / 8, 40 / 8, 40 / 8 | 280 | 35 |
| 4 | 20 / 4, 20 / 4, 77 / 15.4, 77 / 15.4, 77 / 15.4, 45 / 9, 45 / 9 | 361 | 251 |
| 5 | 1 / 0.2, 1 / 0.2, 1 / 0.2, 1 / 0.2 | 4 | 4 |
| 6 | 77 / 15.4, 77 / 15.4, 77 / 15.4, 77 / 15.4, 50 / 10, 50 / 10 | 408 | 318 |
| 7 | 300 / 60 | 300 | 0 |
| 8 | 300 / 60 | 300 | 0 |
| 9 | 300 / 60 | 300 | 0 |
| 10 | 300 / 60 | 300 | 0 |
| 11 | 300 / 60 | 300 | 0 |
| 12 | 20 / 4, 100 / 20, 500 / 100 | 620 | 160 |
| 13 | 500 / 100 | 500 | 400 |
| 14 | 85 / 17, 85 / 17, 85 / 17, 85 / 17 | 340 | 500 |
| 15 | 360 / 72, 100 / 20, 100 / 20 | 560 | 600 |
| 16 | 300 / 60 | 300 | 0 |
| 17 | 300 / 60 | 300 | 0 |
| 18 | 40 / 8, 40 / 8, 40 / 8, 40 / 8, 40 / 8, 50 / 10 | 250 | 25 |
| 19 | 300 / 60, 1 / 0.2, 1 / 0.2 | 302 | 0 |
| 20 | 300 / 60 | 300 | 0 |
| 21 | 150 / 30 | 150 | 10 |
| 22 | 300 / 60 | 300 | 0 |
| 23 | 300 / 60 | 300 | 0 |
| 24 | 20 / 4 | 20 | 5 |
| 26 | 50 / 10, 50 / 10 | 100 | 0 |
| 27 | 1200 / 240 | 1200 | 0 |
| 28 | 600 / 120 | 600 | 0 |
| 29 | 1000 / 200 | 1000 | 0 |
| 31 | 400 / 80 | 400 | 400 |
| 32 | 200 / 40 | 200 | 200 |
| 33 | 240 / 48, 160 / 32 | 400 | 40 |
| 34 | 160 / 32, 100 / 20, 100 / 20 | 360 | 20 |
| 35 | 30 / 6, 30 / 6 | 60 | 0 |
| 36 | 250 / 50 | 250 | 250 |
| 37 | 250 / 50 | 250 | 500 |
| 38 | 70 / 14, 70 / 14, 70 / 14, 70 / 14, 70 / 14, 70 / 14, 70 / 14, 70 / 14, 140 / 28 | 700 | 700 |
| 39 | 150 / 30 | 150 | 120 |
| 40 | 200 / 40 | 200 | 0 |
| 41 | 150 / 30, 70 / 14, 70 / 14, 70 / 14, 70 / 14, 70 / 14, 70 / 14, 70 / 14, 140 / 28 | 780 | 700 |
| 42 | 200 / 40, 20 / 4 | 220 | 0 |
| 44 | 600 / 120 | 600 | 0 |
| 45 | 300 / 60 | 300 | 0 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 0 | 100 / 20 | 500 |
| 5 | 100 / 20 | 500 |
| 10 | 777 / 155.4 | 777 |
| 11 | 777 / 155.4 | 777 |
| 15 | 777 / 155.4 | 777 |

## Stamina costs

From `param_action`: +0x0C is the cost, on 48-byte rows in groups of 4. Action = row / 4. Which skill uses each action is only known after a logged session. A negative number has not been tested; it may give Stamina back.

| Action | Stamina |
|---|---|
| 11 | 50 |
| 16 | 100 |
| 29 | 100 |
| 30 | 50 |
| 31 | 50 |
| 32 | 50 |
| 85 | 50 |
| 86 | 50 |
| 87 | 50 |
| 88 | 400 |
| 89 | 777 |
| 90 | 300 |
| 106 | 10 |
| 131 | 50 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen 2000. Hits logged 49.

Seen so far: 11 of 43 attack rows, 2 of 5 projectiles, 4 of 14 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 20 / 4 | 5 | Square, Square [charged] |
| 1 | 0 | 20 / 4 | 5 | Square [charged] |
| 3 | 0 | 40 / 8 | 5 | Square |
| 4 | 0 | 20 / 4 | 5 | Triangle |
| 4 | 1 | 20 / 4 | 5 | Triangle |
| 4 | 2 | 77 / 15.4 | 77 | Triangle |
| 4 | 3 | 77 / 15.4 | 77 | Triangle |
| 4 | 4 | 77 / 15.4 | 77 | Triangle |
| 6 | 0 | 77 / 15.4 | 77 | Square + R1, Square + R1 [charged] |
| 6 | 1 | 77 / 15.4 | 77 | Square + R1, Square + R1 [charged] |
| 6 | 2 | 77 / 15.4 | 77 | Square + R1, Square + R1 [charged] |
| 6 | 3 | 77 / 15.4 | 77 | Square + R1, Square + R1 [charged] |
| 12 | 0 | 20 / 4 | 5 | Triangle, Triangle [charged] |
| 12 | 2 | 500 / 100 | 5 | Triangle, Triangle [charged] |
| 21 | 0 | 150 / 30 | 10 | Square, Square [charged] |
| 26 | 0 | 50 / 10 | 0 | Triangle + R1, Triangle + R1 [charged] |
| 26 | 1 | 50 / 10 | 0 | Triangle + R1, Triangle + R1 [charged] |
| 31 | 0 | 400 / 80 | 400 | Up + Circle |
| 33 | 1 | 160 / 32 | 20 | Triangle, Square, Square [charged] |
| 34 | 0 | 160 / 32 | 20 | Square, Square [charged] |

| Projectile | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|
| 0 | 100 / 20 | 500 | Circle |
| 15 | 777 / 155.4 | 777 | Circle, Circle [charged] |

Actions with a Stamina cost that landed a hit: 29: 100, 88: 400, 90: 300, 131: 50.

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Lucky Finishing Beam | Press R3 during a Victory Burst. |
| Lucky Star Wish | R1 + Circle |
| Lucky Beam | Circle (Also possible while jumping) |
| Lucky Punch | Left stick up + Circle |
| Strong Attack | Triangle |
| Stamina Charge | R1 + Cross Keep holding to charge beyond the Stamina Gauge limit. |
