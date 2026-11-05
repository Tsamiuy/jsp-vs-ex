# Medaka (character 029)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `029_medaka_mp_param_attack_PS3.d` inside `battle_character_029_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 80 / 32 | 80 | 2 |
| 1 | 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 160 | 16 |
| 2 | 100 / 40, 100 / 40, 40 / 16, 40 / 16, 40 / 16, 40 / 16, 40 / 16 | 400 | 36 |
| 3 | 100 / 40, 100 / 40, 100 / 40, 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 500 | 90 |
| 4 | 30 / 12, 30 / 12, 30 / 12, 30 / 12, 30 / 12, 30 / 12, 30 / 12 | 210 | 70 |
| 5 | 300 / 120 | 300 | 0 |
| 6 | 150 / 60, 150 / 60, 150 / 60 | 450 | 120 |
| 7 | 300 / 120 | 300 | 0 |
| 8 | 300 / 120 | 300 | 0 |
| 9 | 300 / 120 | 300 | 0 |
| 10 | 300 / 120 | 300 | 0 |
| 11 | 300 / 120 | 300 | 0 |
| 12 | 200 / 80 | 200 | 100 |
| 13 | 150 / 60, 150 / 60, 150 / 60 | 450 | 150 |
| 14 | 100 / 40, 100 / 40, 100 / 40, 100 / 40 | 400 | 200 |
| 15 | 300 / 120, 100 / 40, 100 / 40 | 500 | 200 |
| 16 | 300 / 120 | 300 | 0 |
| 17 | 300 / 120 | 300 | 0 |
| 18 | 150 / 60 | 150 | 10 |
| 19 | 300 / 120 | 300 | 0 |
| 20 | 300 / 120 | 300 | 0 |
| 21 | 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 200 | 20 |
| 22 | 100 / 40 | 100 | 10 |
| 23 | 100 / 40 | 100 | 10 |
| 25 | 400 / 160 | 400 | 0 |
| 26 | 1200 / 480 | 1200 | 50 |
| 27 | 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 250 | 250 |
| 28 | 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 250 | 1250 |
| 29 | 100 / 40, 100 / 40 | 200 | 600 |
| 30 | 500 / 200, 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 700 | 1500 |
| 31 | 1700 / 680, 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 1900 | 1500 |
| 32 | 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 250 | 1600 |
| 33 | 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 200 | 1200 |
| 34 | 150 / 60 | 150 | 120 |
| 35 | 250 / 200 | 250 | 0 |
| 36 | 250 / 100, 100 / 40, 100 / 40, 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 650 | 10 |
| 38 | 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10 | 160 | 30 |
| 39 | 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10 | 200 | 10 |
| 40 | 140 / 84, 62 / 37.2, 62 / 37.2, 64 / 38.4 | 328 | 0 |
| 41 | 140 / 56 | 140 | 50 |
| 42 | 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 250 | 250 |
| 43 | 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 250 | 1250 |
| 44 | 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 250 | 1600 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 0 | 50 / 20 | 12 |
| 1 | 25 / 10 | 12 |
| 2 | 50 / 20 | 12 |
| 3 | 25 / 10 | 12 |
| 4 | 100 / 40 | 100 |
| 5 | 50 / 20 | 12 |
| 6 | 25 / 10 | 12 |
| 7 | 50 / 20 | 12 |
| 8 | 25 / 10 | 12 |
| 9 | 100 / 40 | 100 |

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
| 88 | 200 |
| 89 | 300 |
| 90 | 300 |
| 91 | 200 |
| 92 | 10 |
| 106 | 10 |
| 131 | 50 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen 2000. Hits logged 66.

Seen so far: 11 of 43 attack rows, 2 of 10 projectiles, 3 of 16 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 80 / 32 | 2 | Square, Square [charged] |
| 1 | 0 | 20 / 8 | 2 | Square, Square [charged] |
| 1 | 1 | 20 / 8 | 2 | Square, Square [charged] |
| 1 | 2 | 20 / 8 | 2 | Triangle, Square [charged] |
| 1 | 3 | 20 / 8 | 2 | Triangle, Square [charged] |
| 1 | 4 | 20 / 8 | 2 | Triangle, Square [charged] |
| 1 | 5 | 20 / 8 | 2 | Triangle, Square [charged] |
| 1 | 6 | 20 / 8 | 2 | Triangle, Square [charged] |
| 1 | 7 | 20 / 8 | 2 | Triangle, Square [charged] |
| 2 | 0 | 100 / 40 | 10 | Square [charged] |
| 2 | 1 | 100 / 40 | 10 | Square [charged] |
| 3 | 0 | 100 / 40 | 80 | Triangle, Triangle [charged] |
| 12 | 0 | 200 / 80 | 100 | Triangle, Triangle [charged] |
| 13 | 0 | 150 / 60 | 50 | Triangle |
| 13 | 1 | 150 / 60 | 50 | Triangle |
| 13 | 2 | 150 / 60 | 50 | Triangle |
| 22 | 0 | 100 / 40 | 10 | Triangle |
| 26 | 0 | 1200 / 480 | 50 | Up + Circle |
| 27 | 0 | 50 / 20 | 50 | Up + Circle |
| 28 | 0 | 50 / 20 | 50 | Up + Circle |
| 32 | 0 | 50 / 20 | 400 | Circle + R1, Circle + R1 [charged] |

| Projectile | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|
| 1 | 25 / 10 | 12 | Circle |
| 3 | 25 / 10 | 12 | Circle |

Actions with a Stamina cost that landed a hit: 88: 200, 89: 300, 90: 300.

## Knockdowns

Hits that knock the target down, from the attack data (2026-11-03). The number is the hit's reaction (attack slot +0x39, projectile row +0xAF); build-up is its "too many hits" number (the target goes down at 150). What each number does and the get-up are in [Knockdowns.md](../balance/Knockdowns.md).

| Row | Move | Hit | Number | What it does | Damage | Build-up |
|---|---|---|---|---|---|---|
| 2 |  | 1 | 12 | pierced, then down (dmg_penetration) | 100 | 10 |
| 2 |  | 6 | 6 | knocked away (dmg_blow) | 40 | 8 |
| 3 |  | 0 | 6 | knocked away (dmg_blow) | 100 | 80 |
| 4 |  | 6 | 6 | knocked away (dmg_blow) | 30 | 10 |
| 6 |  | 2 | 6 | knocked away (dmg_blow) | 150 | 40 |
| 13 |  | 0 | 21 | launched, then down | 150 | 50 |
| 13 |  | 1 | 4 | knocked down, big bounce | 150 | 50 |
| 13 |  | 2 | 4 | knocked down, big bounce | 150 | 50 |
| 14 |  | 3 | 5 | spun away (dmg_blow_spin) | 100 | 50 |
| 15 |  | 0 | 17 | big knock away (dmg_conspicuous_blow) | 300 | 200 |
| 15 |  | 2 | 17 | big knock away (dmg_conspicuous_blow) | 100 | 0 |
| 18 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 150 | 10 |
| 19 |  | 0 | 6 | knocked away (dmg_blow) | 300 | 0 |
| 21 |  | 0 | 21 | launched, then down | 50 | 5 |
| 21 |  | 1 | 5 | spun away (dmg_blow_spin) | 50 | 5 |
| 21 |  | 2 | 21 | launched, then down | 50 | 5 |
| 21 |  | 3 | 21 | launched, then down | 50 | 5 |
| 23 |  | 0 | 6 | knocked away (dmg_blow) | 100 | 10 |
| 25 |  | 0 | 9 | knocked down, big bounce | 400 | 0 |
| 26 |  | 0 | 4 | knocked down, big bounce | 1200 | 50 |
| 28 |  | 0 | 5 | spun away (dmg_blow_spin) | 50 | 50 |
| 29 |  | 1 | 4 | knocked down, big bounce | 100 | 300 |
| 29 |  | 2 | 4 | knocked down, big bounce | 100 | 300 |
| 30 |  | 0 | 4 | knocked down, big bounce | 500 | 300 |
| 31 |  | 0 | 6 | knocked away (dmg_blow) | 1700 | 300 |
| 32 |  | 3 | 20 | big bounce (dmg_conspicuous_bound 2) | 50 | 300 |
| 35 |  | 0 | 19 | knocked away (dmg_kizuna1_hit_blow) | 250 | 0 |
| 36 |  | 0 | 20 | big bounce (dmg_conspicuous_bound 2) | 250 | 0 |
| 39 |  | 1 | 12 | pierced, then down (dmg_penetration) | 20 | 0 |
| 39 |  | 6 | 6 | knocked away (dmg_blow) | 20 | 0 |
| 40 |  | 0 | 4 | knocked down, big bounce | 140 | 0 |
| 41 |  | 0 | 6 | knocked away (dmg_blow) | 140 | 50 |
| 43 |  | 0 | 5 | spun away (dmg_blow_spin) | 50 | 50 |
| 44 |  | 3 | 20 | big bounce (dmg_conspicuous_bound 2) | 50 | 300 |

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Kurokami Phantom: War God Mode | Press R3 during a Victory Burst. |
| Syringe Slinger | Circle |
| Kurokami Phantom | Left stick up  +  Circle (Keep holding to delay activation) Reduces Medaka's health slightly after use. |
| Weighted Words | R1 + Circle |
| Syringe Slinger | Syringe Slinger |
| Kurokami Phantom | Kurokami Phantom |
| Weighted Words | Weighted Words |
| Jumping Attack 2 | Press Triangle ,  Triangle while jumping. |
| Stamina Charge | Press R1 + Cross while in War God Mode Keep holding to charge beyond the Stamina Gauge limit. |
| War God Mode | R1 + Cross Keep holding to continue charging until Medaka goes into War God Mode. |
