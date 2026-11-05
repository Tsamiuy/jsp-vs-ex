# Hancock (character 002)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `002_hancock_mp_param_attack_PS3.` inside `battle_character_002_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 70 / 21 | 70 | 5 |
| 1 | 70 / 21, 70 / 21 | 140 | 10 |
| 2 | 140 / 42 | 140 | 10 |
| 3 | 70 / 21, 70 / 21, 70 / 21 | 210 | 75 |
| 4 | 70 / 21, 70 / 21, 70 / 21 | 210 | 75 |
| 5 | 50 / 15, 200 / 60 | 250 | 150 |
| 6 | 50 / 15 | 50 | 10 |
| 7 | 50 / 15 | 50 | 10 |
| 8 | 200 / 60 | 200 | 50 |
| 9 | 30 / 9, 30 / 9, 30 / 9, 30 / 9, 30 / 9 | 150 | 150 |
| 10 | 30 / 9, 30 / 9, 30 / 9, 30 / 9, 30 / 9 | 150 | 150 |
| 11 | 30 / 9, 30 / 9, 30 / 9, 30 / 9, 30 / 9 | 150 | 150 |
| 12 | 140 / 42 | 140 | 140 |
| 13 | 210 / 63 | 210 | 140 |
| 14 | 280 / 84 | 280 | 200 |
| 18 | 50 / 15 | 50 | 10 |
| 19 | 50 / 15, 60 / 18, 50 / 15 | 160 | 20 |
| 21 | 100 / 30, 40 / 12, 40 / 12, 10 / 3, 10 / 3, 10 / 3, 10 / 3, 10 / 3, 10 / 3, 10 / 3 | 250 | 10 |
| 22 | 100 / 30 | 100 | 0 |
| 23 | 80 / 24 | 80 | 0 |
| 24 | 250 / 75 | 250 | 0 |
| 25 | 10 / 3, 10 / 3, 10 / 3, 10 / 3, 10 / 3, 10 / 3, 10 / 3, 10 / 3, 10 / 3, 10 / 3 | 100 | 0 |
| 26 | 20 / 6, 20 / 6, 20 / 6, 20 / 6 | 80 | 0 |
| 27 | 20 / 6, 20 / 6, 20 / 6, 20 / 6, 20 / 6, 20 / 6, 20 / 6, 20 / 6 | 160 | 0 |
| 28 | 20 / 6, 20 / 6, 20 / 6, 20 / 6, 20 / 6, 20 / 6, 20 / 6, 20 / 6 | 160 | 0 |
| 29 | 10 / 3 | 10 | 0 |
| 30 | 10 / 3, 10 / 3, 10 / 3, 10 / 3, 10 / 3, 10 / 3, 10 / 3, 10 / 3, 10 / 3, 10 / 3 | 100 | 0 |
| 31 | 60 / 18, 60 / 18 | 120 | 0 |
| 32 | 250 / 75, 40 / 12, 40 / 12, 10 / 3, 10 / 3, 10 / 3, 10 / 3, 10 / 3, 10 / 3, 10 / 3 | 400 | 0 |
| 33 | 150 / 45 | 150 | 120 |
| 34 | 240 / 72 | 240 | 0 |
| 35 | 50 / 15, 50 / 15, 50 / 15 | 150 | 0 |
| 36 | 160 / 48, 50 / 15 | 210 | 0 |
| 37 | 500 / 150 | 500 | 0 |
| 38 | 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15 | 450 | 0 |
| 39 | 50 / 15, 500 / 150 | 550 | 0 |
| 40 | 100 / 30 | 100 | 0 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 0 | 100 / 30 | 100 |
| 1 | 10 / 3 | 10 |
| 6 | 60 / 18 | 10 |
| 11 | 60 / 18 | 10 |
| 16 | 60 / 18 | 10 |
| 20 | 10 / 3 | 10 |
| 21 | 10 / 3 | 10 |
| 22 | 10 / 3 | 10 |
| 23 | 10 / 3 | 10 |
| 24 | 10 / 3 | 10 |
| 25 | 100 / 30 | 100 |
| 26 | 10 / 3 | 10 |
| 30 | 100 / 30 | 100 |
| 31 | 10 / 3 | 10 |
| 35 | 100 / 30 | 100 |
| 36 | 10 / 3 | 10 |
| 41 | 75 / 22.5 | 10 |
| 45 | 300 / 90 | 100 |
| 46 | 10 / 3 | 10 |
| 50 | 100 / 30 | 100 |
| 51 | 10 / 3 | 10 |
| 55 | 100 / 30 | 100 |
| 56 | 10 / 3 | 10 |

## Stamina costs

From `param_action`: +0x0C is the cost, on 48-byte rows in groups of 4. Action = row / 4. Which skill uses each action is only known after a logged session. A negative number has not been tested; it may give Stamina back.

| Action | Stamina |
|---|---|
| 11 | 50 |
| 16 | 80 |
| 29 | 50 |
| 30 | 50 |
| 31 | 50 |
| 32 | 50 |
| 85 | 50 |
| 86 | 50 |
| 87 | 50 |
| 88 | 200 |
| 89 | 100 |
| 90 | 500 |
| 91 | 100 |
| 106 | 10 |
| 131 | 50 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen 2000. Hits logged 48.

Seen so far: 11 of 37 attack rows, 2 of 23 projectiles, 5 of 15 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 70 / 21 | 5 | Square [charged], Square |
| 1 | 0 | 70 / 21 | 5 | Square, Square [charged] |
| 1 | 1 | 70 / 21 | 5 | Triangle, Square [charged] |
| 2 | 0 | 140 / 42 | 10 | Square [charged] |
| 3 | 0 | 70 / 21 | 25 | Triangle, Triangle [charged] |
| 3 | 1 | 70 / 21 | 25 | Triangle, Triangle [charged] |
| 3 | 2 | 70 / 21 | 25 | Triangle, Triangle [charged] |
| 6 | 0 | 50 / 15 | 10 | Square + R1, Square + R1 [charged] |
| 12 | 0 | 140 / 42 | 140 | Triangle, Triangle [charged] |
| 13 | 0 | 210 / 63 | 140 | Triangle |
| 14 | 0 | 280 / 84 | 200 | Triangle |
| 21 | 0 | 100 / 30 | 10 | Square, Square [charged] |
| 25 | 0 | 10 / 3 | 0 | Circle + R1, Circle + R1 [charged] |
| 25 | 1 | 10 / 3 | 0 | Circle + R1, Circle + R1 [charged] |
| 25 | 2 | 10 / 3 | 0 | Circle + R1, Circle + R1 [charged] |
| 25 | 3 | 10 / 3 | 0 | Circle + R1, Circle + R1 [charged] |
| 40 | 0 | 100 / 30 | 0 | Triangle + R1, Triangle + R1 [charged] |

| Projectile | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|
| 0 | 100 / 30 | 100 | Circle, Circle [charged] |
| 6 | 60 / 18 | 10 | Up + Circle [charged], Up + Circle |

Actions with a Stamina cost that landed a hit: 29: 50, 88: 200, 89: 100, 90: 500, 131: 50.

## First session (2026-10-31, Test Fight)

Played by the maintainer for 11 minutes. Log: the Test Fight log of that run (not published).

**Coverage:** 23 of 37 attack rows with damage, 5 of 23 projectile rows with damage, 9 of 15 Stamina actions.

| Action | Input seen | Stamina table / log | Attack rows | Projectile rows | Damage per hit seen |
|---|---|---|---|---|---|
| 11 dash | Up+R2, R2 | 50 / about 58 | - | - | - |
| 23 atk_1 | Square, Up+Square | 0 / - | 0 | - | 70 |
| 24 atk_2 | Square | 0 / - | 1 | - | 70 |
| 25 atk_3 | Square, Triangle | 0 / - | 2 | - | 140 |
| 26 atk_branch_1 | Triangle | 0 / - | 3 | - | 70 |
| 27 atk_branch_2 | Triangle | 0 / - | 4 | - | 70 |
| 28 atk_branch_3 | Triangle | 0 / - | 5 | - | 200 |
| 29 atk_area_1 | Square+R1 | 50 / about 50 | 6 | - | 50 |
| 30 atk_area_2 | Square+R1, R1 | 50 / about 50 | 7 | - | 50 |
| 31 atk_area_3 | R1 | 50 / about 50 | 8 | - | 200 |
| 32 atk_area_branch_1 | Triangle+R1, R1 | 50 / about 30 | 9 | - | 30 |
| 33 atk_area_branch_2 | Triangle+R1, R1 | 0 / - | 10 | - | 30 |
| 34 atk_area_branch_3 | R1, Triangle+R1 | 0 / - | 11 | - | 30 |
| 35 atk_heavy_1 | Triangle | 0 / - | 12 | - | 140 |
| 36 atk_heavy_2 | Triangle | 0 / - | 13 | - | 210 |
| 37 atk_heavy_3 | Triangle, Square | 0 / - | 14 | - | 280 |
| 41 atk_dash_1 | Square+R2, Up+Square+R2 | 0 / about 3 | 18 | - | 50 |
| 42 atk_dash_2 | Square+R2, R2 | 0 / - | 19 | - | 50 |
| 44 atk_jump_1 | Square, Square+R2 | 0 / - | 21 | - | 100 |
| 82 provoke | L3 | 0 / - | - | - | - |
| 83 burning_heart | Cross+R1 | 0 / - | - | - | - |
| 88 atk_special_1 | Circle | 200 / about 200 | - | 0, 50, 55 | 100, 50 |
| 89 atk_special_2 | Circle+R1, Up+Circle+R1 | 100 / about 100 | 25 | - | 10 |
| 90 atk_special_3 | Up+Circle | 500 / about 492 | - | 6, 11 | 60 |
| 108 atk_ultimate | R3 | 0 / - | 37, 38, 39 | - | 50, 500 |
| 131 atk_guard_crash | Triangle+R1 | 50 / about 50 | 40 | - | 100, 50 |

- **Skills by input (inferred):** Circle is Pistol Kiss, on the ground or in the air, using projectile rows 0, 50 and 55. R1 + Circle is Love-Love Beam at 10 per hit. Stick up + Circle is Slave Arrow on projectile rows 6 and 11 at 60 per hit. R3 is Perfume Femur, 2,600 damage in its one logged use.
- **Guard crash:** R1 + Triangle is action 131, row 40, 100 damage.
- **Her recoverable share differs:** rows 9, 10, 11 and 25 are 30% where the moves checked on Luffy and Ace are 20%.
- **Just Guard:** 11 uses, with no attack rows tied to it, so this log shows no counter hits for her.
- **Stamina Charge:** the limit rose from 1000 to 2000 here too.
- **Rows not seen:** 22, 23, 24, 26 to 36. **Projectile rows not seen:** 1, 16, 20 to 26, 30, 31, 35, 36, 41, 45, 46, 51, 56.

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Perfume Femur | Press R3 during a Victory Burst. |
| Love-Love Beam | R1 + Circle (Hold to charge) |
| Pistol Kiss | Circle (Also possible while jumping) |
| Slave Arrow | Left stick up + Circle (Hold to charge) |
| Dash Attack | While dashing, press Square ,  Square. |
| Stamina Charge | R1 + Cross Keep holding to charge beyond the Stamina Gauge limit. |
