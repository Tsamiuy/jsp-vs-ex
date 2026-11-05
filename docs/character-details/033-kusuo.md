# Saiki (character 033)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `033_kusuo_mp_param_attack_PS3.da` inside `battle_character_033_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 100 / 10 | 100 | 20 |
| 1 | 75 / 7.5, 75 / 7.5, 100 / 10 | 250 | 40 |
| 2 | 100 / 10, 100 / 10, 100 / 10, 50 / 5, 40 / 4, 40 / 4, 40 / 4 | 470 | 74 |
| 3 | 350 / 35, 160 / 16, 40 / 4, 40 / 4, 40 / 4, 40 / 4, 40 / 4 | 710 | 335 |
| 4 | 300 / 30, 45 / 4.5, 45 / 4.5, 45 / 4.5, 45 / 4.5, 45 / 4.5, 45 / 4.5 | 570 | 20 |
| 5 | 300 / 30 | 300 | 0 |
| 6 | 250 / 25, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5 | 500 | 75 |
| 7 | 300 / 30 | 300 | 0 |
| 8 | 300 / 30 | 300 | 0 |
| 9 | 300 / 30 | 300 | 0 |
| 10 | 300 / 30 | 300 | 0 |
| 11 | 300 / 30 | 300 | 0 |
| 12 | 500 / 50, 500 / 50, 100 / 10 | 1100 | 1200 |
| 13 | 320 / 32 | 320 | 400 |
| 14 | 85 / 8.5, 85 / 8.5, 85 / 8.5, 85 / 8.5 | 340 | 500 |
| 15 | 360 / 36, 100 / 10, 100 / 10 | 560 | 600 |
| 16 | 300 / 30 | 300 | 0 |
| 17 | 300 / 30 | 300 | 0 |
| 18 | 250 / 25, 150 / 15 | 400 | 35 |
| 19 | 300 / 30 | 300 | 0 |
| 20 | 300 / 30 | 300 | 0 |
| 21 | 250 / 25 | 250 | 100 |
| 22 | 300 / 30 | 300 | 0 |
| 23 | 300 / 30 | 300 | 0 |
| 24 | 30 / 3, 30 / 3, 30 / 3, 30 / 3, 30 / 3, 10 / 1, 10 / 1, 10 / 1, 10 / 1 | 190 | 100 |
| 25 | 100 / 10, 350 / 35, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1 | 520 | 0 |
| 26 | 120 / 12, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1 | 200 | 25 |
| 27 | 120 / 12, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1 | 200 | 25 |
| 28 | 120 / 12, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1 | 200 | 25 |
| 29 | 120 / 12, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1 | 200 | 25 |
| 30 | 250 / 25, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1 | 330 | 0 |
| 31 | 300 / 30, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1 | 380 | 200 |
| 32 | 150 / 15, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1 | 230 | 75 |
| 33 | 250 / 175 | 250 | 250 |
| 34 | 250 / 175 | 250 | 500 |
| 35 | 50 / 35, 70 / 49, 70 / 49, 70 / 49, 70 / 49, 70 / 49, 70 / 49, 70 / 49, 140 / 98 | 680 | 700 |
| 36 | 150 / 60 | 150 | 120 |
| 37 | 100 / 70, 70 / 49, 70 / 49, 70 / 49, 70 / 49, 70 / 49, 70 / 49, 70 / 49, 140 / 98 | 730 | 700 |
| 38 | 120 / 12, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1, 10 / 1 | 200 | 25 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 0 | 1500 / 1125 | 1000 |
| 1 | 100 / 75 | 100 |
| 5 | 100 / 75 | 100 |
| 6 | 100 / 75 | 100 |
| 10 | 150 / 15 | 150 |
| 11 | 150 / 15 | 150 |
| 12 | 150 / 15 | 150 |
| 15 | 150 / 15 | 150 |
| 16 | 150 / 15 | 150 |
| 17 | 150 / 15 | 150 |
| 25 | 30 / 3 | 10 |
| 26 | 30 / 3 | 10 |
| 27 | 30 / 3 | 10 |
| 30 | 3000 / 300 | 10 |
| 31 | 10 / 1 | 10 |
| 35 | 3000 / 300 | 10 |
| 36 | 10 / 1 | 10 |
| 40 | 3000 / 300 | 10 |
| 41 | 10 / 1 | 10 |
| 45 | 3000 / 300 | 10 |
| 46 | 10 / 1 | 10 |
| 50 | 30 / 3 | 10 |
| 51 | 30 / 3 | 10 |
| 52 | 30 / 3 | 10 |

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
| 88 | 100 |
| 89 | 400 |
| 92 | 500 |
| 93 | 100 |
| 106 | 10 |
| 131 | 50 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen 2000. Hits logged 80.

Seen so far: 10 of 39 attack rows, 0 of 24 projectiles, 2 of 15 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 100 / 10 | 20 | Square [charged], Square |
| 1 | 0 | 75 / 7.5 | 20 | Square [charged] |
| 1 | 1 | 75 / 7.5 | 20 | Square [charged] |
| 2 | 0 | 100 / 10 | 20 | Square [charged] |
| 2 | 1 | 100 / 10 | 20 | Square [charged] |
| 2 | 2 | 100 / 10 | 20 | Square [charged] |
| 3 | 0 | 350 / 35 | 300 | Triangle, Triangle [charged] |
| 12 | 1 | 500 / 50 | 500 | Triangle |
| 21 | 0 | 250 / 25 | 100 | Square, Square [charged] |
| 24 | 0 | 30 / 3 | 20 | Circle + R1, Circle + R1 [charged] |
| 24 | 1 | 30 / 3 | 20 | Circle + R1, Circle + R1 [charged] |
| 24 | 2 | 30 / 3 | 20 | Circle + R1, Circle + R1 [charged] |
| 24 | 3 | 30 / 3 | 20 | Circle + R1, Circle + R1 [charged] |
| 24 | 4 | 30 / 3 | 20 | Circle + R1, Circle + R1 [charged] |
| 25 | 1 | 350 / 35 | 0 | Triangle + R1, Triangle + R1 [charged] |
| 31 | 0 | 300 / 30 | 200 | Circle, Circle [charged] |
| 32 | 0 | 150 / 15 | 75 | Circle, Circle [charged], ? |

Actions with a Stamina cost that landed a hit: 89: 400, 131: 50.

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| No-Holds Barred Psychokinesis | Press R3 during a Victory Burst. |
| Psychic Embrace | After hitting an enemy with R1 + Circle, select the direction they go flying in with Left stick up, Left stick right, Left stick down, Left stick left, or Circle. |
| Teleport Switch | Left stick up + Circle (Also possible while jumping) Switches Saiki's position with the enemy he hits. |
| Monolith Barrier | Press Circle to make the rocks float, and Circle again to fling them. |
| Strong Attack | Triangle |
| Stamina Charge | R1 + Cross Keep holding to charge beyond the Stamina Gauge limit. |
