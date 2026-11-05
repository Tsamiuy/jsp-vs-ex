# Aizen (character 016)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `016_aizen_mp_param_attack_PS3.da` inside `battle_character_016_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 70 / 63 | 70 | 5 |
| 1 | 70 / 63, 70 / 63, 70 / 63 | 210 | 15 |
| 2 | 190 / 171 | 190 | 15 |
| 3 | 70 / 63, 70 / 63, 70 / 63, 70 / 63, 1 / 0.9, 70 / 63, 1 / 0.9, 70 / 63, 1 / 0.9 | 423 | 205 |
| 4 | 260 / 234 | 260 | 20 |
| 5 | 240 / 216 | 240 | 0 |
| 6 | 150 / 135, 150 / 135, 150 / 135, 75 / 67.5 | 525 | 20 |
| 7 | 240 / 216 | 240 | 0 |
| 8 | 240 / 216 | 240 | 0 |
| 9 | 240 / 216 | 240 | 0 |
| 10 | 240 / 216 | 240 | 0 |
| 11 | 240 / 216 | 240 | 0 |
| 12 | 200 / 180 | 200 | 70 |
| 13 | 300 / 270, 100 / 90, 120 / 108 | 520 | 140 |
| 14 | 400 / 360 | 400 | 210 |
| 16 | 300 / 270 | 300 | 210 |
| 17 | 240 / 216 | 240 | 0 |
| 18 | 240 / 216 | 240 | 0 |
| 19 | 225 / 202.5, 75 / 67.5, 75 / 67.5, 75 / 67.5 | 450 | 20 |
| 20 | 240 / 216 | 240 | 0 |
| 21 | 240 / 216 | 240 | 0 |
| 22 | 120 / 108, 120 / 108 | 240 | 20 |
| 23 | 300 / 270 | 300 | 20 |
| 24 | 120 / 108 | 120 | 5 |
| 26 | 450 / 405 | 450 | 0 |
| 27 | 80 / 72, 80 / 72, 80 / 72, 80 / 72, 80 / 72, 80 / 72, 80 / 72, 80 / 72, 100 / 90, 100 / 90 | 840 | 600 |
| 28 | 100 / 90, 100 / 90, 110 / 99, 110 / 99, 110 / 99, 110 / 99, 120 / 108, 120 / 108, 120 / 108, 120 / 108 | 1120 | 600 |
| 30 | 240 / 216 | 240 | 20 |
| 31 | 150 / 135 | 150 | 120 |
| 32 | 250 / 225 | 250 | 0 |
| 33 | 250 / 225, 100 / 90, 100 / 90, 50 / 45, 50 / 45, 50 / 45, 50 / 45 | 650 | 10 |
| 34 | 100 / 90, 100 / 90, 300 / 270 | 500 | 10 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 5 | 100 / 90 | 100 |
| 6 | 20 / 18 | 0 |
| 12 | 550 / 495 | 0 |
| 17 | 550 / 495 | 0 |
| 20 | 300 / 270 | 300 |
| 21 | 5 / 4.5 | 300 |
| 22 | 5 / 4.5 | 300 |
| 27 | 300 / 270 | 100 |
| 32 | 300 / 270 | 100 |
| 37 | 300 / 270 | 0 |
| 42 | 300 / 270 | 0 |
| 50 | 100 / 90 | 100 |
| 51 | 20 / 18 | 0 |
| 57 | 300 / 270 | 100 |
| 62 | 500 / 450 | 100 |
| 70 | 100 / 90 | 100 |
| 71 | 20 / 18 | 0 |
| 77 | 1900 / 1710 | 0 |
| 82 | 1900 / 1710 | 0 |

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
| 88 | 500 |
| 89 | 600 |
| 90 | 300 |
| 91 | 100 |
| 106 | 10 |
| 131 | 200 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen at least 1720 (my hold ended before the top). Hits logged 57.

Seen so far: 9 of 32 attack rows, 5 of 19 projectiles, 4 of 15 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 70 / 63 | 5 | Square, Square [charged] |
| 1 | 0 | 70 / 63 | 5 | Triangle |
| 1 | 1 | 70 / 63 | 5 | Triangle |
| 1 | 2 | 70 / 63 | 5 | Square, Triangle |
| 2 | 0 | 190 / 171 | 15 | Square |
| 3 | 0 | 70 / 63 | 40 | Triangle, Triangle [charged] |
| 3 | 1 | 70 / 63 | 40 | Triangle, Triangle [charged] |
| 3 | 3 | 70 / 63 | 40 | Triangle, Triangle [charged] |
| 3 | 5 | 70 / 63 | 40 | Triangle, Triangle [charged] |
| 3 | 7 | 70 / 63 | 40 | Triangle, Triangle [charged] |
| 6 | 0 | 150 / 135 | 5 | Square + R1, Square + R1 [charged] |
| 6 | 1 | 150 / 135 | 5 | Square + R1, Square + R1 [charged] |
| 6 | 2 | 150 / 135 | 5 | Square + R1, Square + R1 [charged] |
| 12 | 0 | 200 / 180 | 70 | Triangle, Triangle [charged] |
| 13 | 0 | 300 / 270 | 140 | Triangle |
| 14 | 0 | 400 / 360 | 210 | Triangle |
| 24 | 0 | 120 / 108 | 5 | Square, Square [charged] |

| Projectile | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|
| 5 | 100 / 90 | 100 | Circle, Circle [charged] |
| 6 | 20 / 18 | 0 | Circle, Circle [charged] |
| 17 | 550 / 495 | 0 | Up + Circle, Up + Circle [charged] |
| 20 | 300 / 270 | 300 | Circle + R1 |
| 32 | 300 / 270 | 100 | Square |

Actions with a Stamina cost that landed a hit: 29: 50, 88: 500, 89: 600, 90: 300.

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Hogyoku Subordination | Press R3 during a Victory Burst. |
| Hado #90: Kurohitsugi | Left stick up + Circle (Hold to perform a Complete Incantation) Performing a Complete Incantation increases the attack's effect. |
| Bakudo #61: Rikujokoro | Circle (Hold to perform a Complete Incantation / Possible while jumping) Performing a Complete Incantation increases the attack's effect. |
| Bakudo #81: Danku | R1 + Circle |
| Stamina Charge | R1 + Cross Keep holding to continue charging slowly past the Stamina Gauge limit. |
