# Killua (character 018)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `018_killua_mp_param_attack_PS3.d` inside `battle_character_018_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 100 / 90 | 100 | 5 |
| 1 | 70 / 63, 70 / 63 | 140 | 10 |
| 2 | 40 / 4, 40 / 4, 40 / 4, 40 / 4, 40 / 4, 40 / 4 | 240 | 12 |
| 3 | 300 / 270 | 300 | 50 |
| 4 | 300 / 270 | 300 | 0 |
| 5 | 300 / 270 | 300 | 0 |
| 6 | 70 / 35 | 70 | 10 |
| 7 | 70 / 35, 70 / 35 | 140 | 20 |
| 8 | 70 / 35, 70 / 35, 70 / 35 | 210 | 30 |
| 9 | 300 / 270 | 300 | 0 |
| 10 | 300 / 270 | 300 | 0 |
| 11 | 300 / 270 | 300 | 0 |
| 12 | 200 / 180 | 200 | 50 |
| 13 | 100 / 90, 100 / 90, 100 / 90 | 300 | 150 |
| 14 | 200 / 180, 200 / 180 | 400 | 200 |
| 15 | 300 / 270 | 300 | 150 |
| 16 | 300 / 270 | 300 | 0 |
| 17 | 300 / 270 | 300 | 0 |
| 18 | 50 / 45, 150 / 135, 50 / 45 | 250 | 6 |
| 19 | 300 / 270 | 300 | 0 |
| 20 | 300 / 270 | 300 | 0 |
| 21 | 150 / 135 | 150 | 5 |
| 22 | 300 / 270 | 300 | 0 |
| 23 | 300 / 270 | 300 | 0 |
| 25 | 800 / 720 | 800 | 100 |
| 27 | 1200 / 1080 | 1200 | 400 |
| 28 | 175 / 87.5, 175 / 87.5, 150 / 75, 3 / 1.5 | 503 | 20 |
| 29 | 175 / 157.5, 150 / 135, 3 / 2.7 | 328 | 10 |
| 30 | 200 / 180, 100 / 90, 100 / 90, 100 / 90, 100 / 90, 100 / 90, 100 / 90, 100 / 90, 100 / 90, 100 / 90 | 1100 | 90 |
| 31 | 100 / 90, 100 / 90, 175 / 157.5, 175 / 157.5, 175 / 157.5, 175 / 157.5, 175 / 157.5, 175 / 157.5, 175 / 157.5, 175 / 157.5 | 1600 | 100 |
| 32 | 50 / 45, 175 / 157.5, 175 / 157.5, 175 / 157.5, 175 / 157.5, 175 / 157.5, 175 / 157.5, 175 / 157.5, 175 / 157.5 | 1450 | 90 |
| 35 | 150 / 135 | 150 | 400 |
| 36 | 200 / 160, 175 / 140, 175 / 140, 175 / 140, 175 / 140, 175 / 140, 175 / 140, 175 / 140, 175 / 140, 175 / 140 | 1775 | 90 |
| 37 | 250 / 75 | 250 | 250 |
| 38 | 50 / 15, 200 / 60 | 250 | 500 |
| 39 | 50 / 15, 70 / 21, 70 / 21, 70 / 21, 70 / 21, 70 / 21, 70 / 21, 70 / 21, 140 / 42 | 680 | 700 |
| 40 | 150 / 45 | 150 | 120 |
| 41 | 50 / 15, 50 / 15, 50 / 15, 70 / 21, 70 / 21, 70 / 21, 70 / 21, 70 / 21, 140 / 42 | 640 | 700 |
| 42 | 50 / 15, 70 / 21, 70 / 21, 70 / 21, 70 / 21, 70 / 21, 70 / 21, 70 / 21, 140 / 42 | 680 | 700 |
| 43 | 250 / 75, 70 / 21, 70 / 21, 70 / 21, 70 / 21, 70 / 21, 70 / 21, 70 / 21, 140 / 42 | 880 | 700 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 0 | 200 / 20 | 10 |
| 1 | 500 / 50 | 10 |
| 5 | 200 / 20 | 10 |
| 6 | 500 / 50 | 10 |
| 10 | 25 / 2.5 | 10 |
| 11 | 500 / 50 | 10 |
| 15 | 100 / 10 | 10 |
| 20 | 100 / 10 | 10 |
| 25 | 100 / 10 | 10 |
| 30 | 100 / 10 | 10 |
| 35 | 100 / 10 | 10 |
| 40 | 100 / 10 | 10 |
| 45 | 500 / 375 | 10 |
| 46 | 500 / 375 | 10 |
| 50 | 500 / 375 | 10 |
| 51 | 500 / 375 | 10 |
| 55 | 25 / 18.8 | 10 |
| 56 | 500 / 375 | 10 |

## Stamina costs

From `param_action`: +0x0C is the cost, on 48-byte rows in groups of 4. Action = row / 4. Which skill uses each action is only known after a logged session. A negative number has not been tested; it may give Stamina back.

| Action | Stamina |
|---|---|
| 11 | 1 or 50 |
| 16 | 50 or 100 |
| 29 | 50 |
| 30 | 50 |
| 31 | 50 |
| 32 | 50 |
| 85 | 50 |
| 86 | 50 |
| 87 | 50 |
| 88 | 200 or 300 |
| 89 | 300 |
| 90 | 300 or 400 |
| 91 | 100 |
| 92 | 150 |
| 106 | 10 |
| 131 | 50 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen 2000. Hits logged 81.

Seen so far: 10 of 40 attack rows, 3 of 18 projectiles, 4 of 16 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 100 / 90 | 5 | Square, Square [charged] |
| 1 | 0 | 70 / 63 | 5 | Square, Square [charged] |
| 1 | 1 | 70 / 63 | 5 | Square, Square [charged] |
| 2 | 0 | 40 / 4 | 2 | Square [charged] |
| 2 | 1 | 40 / 4 | 2 | Square [charged] |
| 2 | 2 | 40 / 4 | 2 | Square [charged] |
| 2 | 3 | 40 / 4 | 2 | Square [charged] |
| 2 | 4 | 40 / 4 | 2 | Square [charged] |
| 2 | 5 | 40 / 4 | 2 | Square [charged] |
| 3 | 0 | 300 / 270 | 50 | Triangle, Triangle [charged] |
| 6 | 0 | 70 / 35 | 10 | Square + R1, Square + R1 [charged] |
| 12 | 0 | 200 / 180 | 50 | Triangle, Triangle [charged] |
| 13 | 0 | 100 / 90 | 50 | Triangle |
| 13 | 1 | 100 / 90 | 50 | Triangle |
| 13 | 2 | 100 / 90 | 50 | Triangle |
| 14 | 0 | 200 / 180 | 100 | Triangle |
| 25 | 0 | 800 / 720 | 100 | Up + Circle, Up + Circle [charged] |
| 36 | 0 | 200 / 160 | 0 | Triangle + R1, Triangle + R1 [charged] |

| Projectile | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|
| 0 | 200 / 20 | 10 | Circle |
| 5 | 200 / 20 | 10 | Circle, Circle [charged] |
| 10 | 25 / 2.5 | 10 | Circle, Circle [charged] |

Actions with a Stamina cost that landed a hit: 29: 50, 88: 300, 89: 300, 131: 50.

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Soaring Thunder | Press R3 during a Victory Burst. |
| Nen Control: Zetsu | R1 + Circle |
| Thunderbolt | Circle (Also possible while jumping) |
| Godspeed: Speed of Lightning | Left stick up + Circle (Charge until max) |
| Godspeed: Whirlwind | Left stick up + Circle (Activates when an enemy approaches, or if you release Circle) |
| Stamina Charge | R1 + Cross Keep holding to charge beyond the Stamina Gauge limit. |
