# Toguro (character 011)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `011_toguro_mp_param_attack_PS3.d` inside `battle_character_011_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 140 / 28 | 140 | 12 |
| 1 | 80 / 16, 110 / 22 | 190 | 24 |
| 2 | 250 / 50 | 250 | 40 |
| 3 | 260 / 52 | 260 | 350 |
| 4 | 370 / 74 | 370 | 350 |
| 6 | 430 / 86, 40 / 8, 40 / 8, 40 / 8, 40 / 8 | 590 | 50 |
| 12 | 340 / 68, 180 / 36 | 520 | 220 |
| 13 | 260 / 52, 180 / 36 | 440 | 180 |
| 14 | 500 / 100, 180 / 36 | 680 | 350 |
| 15 | 90 / 18, 90 / 18, 90 / 18, 90 / 18 | 360 | 360 |
| 16 | 490 / 98 | 490 | 350 |
| 18 | 220 / 44, 100 / 20 | 320 | 20 |
| 21 | 220 / 44, 100 / 20, 100 / 20 | 420 | 40 |
| 22 | 150 / 30 | 150 | 10 |
| 25 | 500 / 100 | 500 | 1000 |
| 26 | 700 / 140 | 700 | 1000 |
| 28 | 750 / 150 | 750 | 750 |
| 29 | 250 / 50, 80 / 16, 80 / 16 | 410 | 400 |
| 30 | 150 / 30 | 150 | 120 |
| 31 | 250 / 50 | 250 | 300 |
| 32 | 20 / 4 | 20 | 400 |
| 33 | 240 / 48 | 240 | 500 |
| 34 | 40 / 8, 40 / 8, 40 / 8, 40 / 8, 40 / 8, 40 / 8, 40 / 8, 40 / 8, 40 / 8, 40 / 8 | 400 | 200 |
| 35 | 1250 / 250, 2000 / 400, 20 / 4, 20 / 4, 20 / 4, 20 / 4, 20 / 4 | 3350 | 3200 |
| 36 | 20 / 4, 20 / 4 | 40 | 400 |
| 37 | 250 / 50 | 250 | 250 |
| 38 | 30 / 6, 30 / 6, 30 / 6, 30 / 6, 30 / 6, 30 / 6, 30 / 6, 30 / 6, 30 / 6, 30 / 6 | 300 | 200 |
| 39 | 10 / 2, 10 / 2, 10 / 2, 10 / 2, 10 / 2, 10 / 2, 10 / 2, 10 / 2, 10 / 2, 10 / 2 | 100 | 200 |
| 40 | 5 / 1, 5 / 1, 5 / 1, 5 / 1, 5 / 1, 5 / 1, 5 / 1, 5 / 1, 5 / 1, 5 / 1 | 50 | 200 |
| 41 | 30 / 6, 30 / 6, 30 / 6, 30 / 6, 30 / 6, 30 / 6, 30 / 6, 30 / 6, 30 / 6, 30 / 6 | 300 | 100 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 0 | 40 / 8 | 40 |
| 1 | 10 / 2 | 10 |
| 5 | 40 / 8 | 40 |
| 6 | 10 / 2 | 10 |
| 10 | 40 / 8 | 40 |
| 11 | 10 / 2 | 10 |
| 15 | 40 / 8 | 40 |
| 16 | 10 / 2 | 10 |
| 20 | 40 / 8 | 40 |
| 21 | 10 / 2 | 10 |
| 25 | 40 / 8 | 40 |
| 26 | 10 / 2 | 10 |

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
| 88 | 200 |
| 89 | 400 |
| 90 | 200 |
| 91 | 100 |
| 92 | 150 |
| 106 | 10 |
| 131 | 420 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen at least 1720 (my hold ended before the top). Hits logged 63.

Seen so far: 11 of 30 attack rows, 1 of 12 projectiles, 4 of 16 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 140 / 28 | 12 | Square, Square [charged] |
| 1 | 0 | 80 / 16 | 12 | Square, Square [charged] |
| 1 | 1 | 110 / 22 | 12 | Square, Triangle, Square [charged] |
| 2 | 0 | 250 / 50 | 40 | Square, Square [charged] |
| 3 | 0 | 260 / 52 | 350 | Triangle, Triangle [charged] |
| 6 | 0 | 430 / 86 | 50 | Square + R1, Square + R1 [charged] |
| 12 | 0 | 340 / 68 | 220 | Triangle, Triangle [charged] |
| 13 | 0 | 260 / 52 | 180 | Triangle |
| 21 | 0 | 220 / 44 | 20 | Square, Square [charged] |
| 22 | 0 | 150 / 30 | 10 | Square, Square [charged] |
| 28 | 0 | 750 / 150 | 750 | Triangle + R1, Triangle + R1 [charged] |
| 34 | 0 | 40 / 8 | 20 | Up + Circle, Up + Circle [charged] |
| 34 | 1 | 40 / 8 | 20 | Up + Circle, Up + Circle [charged] |
| 34 | 2 | 40 / 8 | 20 | Up + Circle, Up + Circle [charged] |
| 34 | 3 | 40 / 8 | 20 | Up + Circle, Up + Circle [charged] |
| 34 | 4 | 40 / 8 | 20 | Up + Circle, Up + Circle [charged] |
| 34 | 5 | 40 / 8 | 20 | Up + Circle, Up + Circle [charged] |
| 34 | 6 | 40 / 8 | 20 | Up + Circle, Up + Circle [charged] |
| 34 | 7 | 40 / 8 | 20 | Up + Circle, Up + Circle [charged] |
| 34 | 8 | 40 / 8 | 20 | Up + Circle, Up + Circle [charged] |
| 34 | 9 | 40 / 8 | 20 | Up + Circle, Up + Circle [charged] |

| Projectile | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|
| 0 | 40 / 8 | 40 | Circle, Circle [charged] |

Actions with a Stamina cost that landed a hit: 29: 100, 88: 200, 89: 400, 131: 420.

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| You Think You're Invincible? | Press R3 while in Toguro's 100% Form. |
| You Call That Power? | R1 + Circle (Activates when the opponent attacks) |
| Finger Flip Bullets | Circle (Tap repeatedly to continue firing) |
| Get Away! | Left stick up + Circle (Hold to prolong attack) |
| You Call That Power? |  |
| Finger Flip Bullets |  |
| Get Away! |  |
| 100% Form | Press R3 during a Victory Burst. |
| Stamina Charge | R1 + Cross Keep holding to continue charging slowly past the Stamina Gauge limit. |
