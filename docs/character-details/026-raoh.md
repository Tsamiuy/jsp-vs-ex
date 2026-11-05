# Raoh (character 026)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `026_raoh_mp_param_attack_PS3.dat` inside `battle_character_026_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 180 / 72 | 180 | 25 |
| 1 | 180 / 72 | 180 | 25 |
| 2 | 250 / 100 | 250 | 50 |
| 3 | 400 / 160, 350 / 140 | 750 | 320 |
| 4 | 340 / 136 | 340 | 200 |
| 5 | 300 / 120 | 300 | 0 |
| 6 | 200 / 80 | 200 | 20 |
| 7 | 300 / 120 | 300 | 0 |
| 8 | 300 / 120 | 300 | 0 |
| 9 | 300 / 120 | 300 | 0 |
| 10 | 300 / 120 | 300 | 0 |
| 11 | 300 / 120 | 300 | 0 |
| 12 | 300 / 120 | 300 | 300 |
| 13 | 200 / 80, 200 / 80 | 400 | 400 |
| 14 | 500 / 200 | 500 | 500 |
| 15 | 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 70 / 28 | 520 | 500 |
| 16 | 300 / 120 | 300 | 0 |
| 17 | 300 / 120 | 300 | 0 |
| 18 | 200 / 80 | 200 | 20 |
| 19 | 300 / 120 | 300 | 0 |
| 20 | 300 / 120 | 300 | 0 |
| 21 | 200 / 80 | 200 | 20 |
| 22 | 200 / 80 | 200 | 20 |
| 23 | 300 / 120 | 300 | 0 |
| 24 | 600 / 240, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 720 | 30 |
| 25 | 300 / 120, 300 / 120, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 700 | 25 |
| 26 | 300 / 120, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 420 | 30 |
| 27 | 300 / 120, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 420 | 30 |
| 28 | 750 / 300, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 870 | 780 |
| 29 | 750 / 300, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 870 | 780 |
| 30 | 50 / 20, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 170 | 105 |
| 31 | 1300 / 520, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 1420 | 730 |
| 32 | 150 / 60 | 150 | 120 |
| 33 | 250 / 100 | 250 | 20 |
| 34 | 250 / 100, 100 / 40, 100 / 40, 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 650 | 30 |
| 35 | 50 / 20, 1 / 0.4, 1 / 0.4, 1 / 0.4, 1 / 0.4, 1 / 0.4 | 55 | 10 |
| 36 | 50 / 20, 50 / 20, 70 / 28 | 170 | 30 |
| 37 | 50 / 20, 70 / 28, 70 / 28 | 190 | 30 |
| 38 | 50 / 20, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 170 | 80 |
| 39 | 400 / 160, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 520 | 80 |
| 40 | 400 / 160, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 520 | 80 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 0 | 750 / 300 | 750 |
| 1 | 750 / 300 | 750 |
| 5 | 450 / 180 | 0 |
| 6 | 10 / 4 | 0 |
| 10 | 1450 / 580 | 2000 |
| 11 | 1450 / 580 | 0 |
| 15 | 300 / 120 | 0 |
| 20 | 150 / 60 | 750 |
| 21 | 150 / 60 | 0 |
| 22 | 150 / 60 | 0 |
| 23 | 150 / 60 | 0 |
| 24 | 150 / 60 | 0 |
| 26 | 200 / 0 | 0 |

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
| 89 | 200 |
| 90 | 500 |
| 106 | 10 |
| 131 | 200 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen 2000. Hits logged 49.

Seen so far: 14 of 41 attack rows, 2 of 13 projectiles, 4 of 14 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 180 / 72 | 25 | Square, Square [charged] |
| 1 | 0 | 180 / 72 | 25 | Triangle, Square [charged] |
| 2 | 0 | 250 / 100 | 50 | Square [charged] |
| 3 | 1 | 350 / 140 | 300 | Triangle, Triangle [charged] |
| 6 | 0 | 200 / 80 | 20 | Square + R1, Square + R1 [charged] |
| 12 | 0 | 300 / 120 | 300 | Triangle, Triangle [charged] |
| 13 | 0 | 200 / 80 | 200 | Triangle |
| 13 | 1 | 200 / 80 | 200 | Triangle |
| 22 | 0 | 200 / 80 | 20 | Triangle |
| 24 | 0 | 600 / 240 | 0 | Circle + R1, Circle + R1 [charged] |
| 25 | 0 | 300 / 120 | 0 | Circle + R1, Circle + R1 [charged] |
| 26 | 0 | 300 / 120 | 0 | Triangle + R1, Triangle + R1 [charged] |
| 27 | 0 | 300 / 120 | 0 | Triangle + R1, Triangle + R1 [charged] |
| 30 | 0 | 50 / 20 | 75 | Up + Circle, Up + Circle [charged], ? |
| 31 | 0 | 1300 / 520 | 700 | Cross, ? [charged] |

| Projectile | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|
| 0 | 750 / 300 | 750 | Circle |
| 26 | 200 / 0 | 0 | Cross + R1 |

Actions with a Stamina cost that landed a hit: 29: 50, 88: 500, 90: 500, 131: 200.

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Heaven's Command Charge | Press R3 during a Victory Burst. |
| Fighting Spirit Discharge | Press Left stick up + Circle to ready the attack, and Circle to activate. |
| Iron Palm Wave | Circle (Also possible while jumping) |
| Dawning Blood Sorrow | R1 + Circle |
| Stamina Charge | R1 + Cross Keep holding to charge beyond the Stamina Gauge limit. |
