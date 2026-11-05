# Sasuke (character 005)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `005_sasuke_mp_param_attack_PS3.d` inside `battle_character_005_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 50 / 25 | 50 | 5 |
| 1 | 70 / 35, 70 / 35 | 140 | 10 |
| 2 | 150 / 75 | 150 | 25 |
| 3 | 150 / 75 | 150 | 50 |
| 4 | 180 / 90 | 180 | 50 |
| 5 | 320 / 16 | 320 | 100 |
| 6 | 75 / 37.5, 75 / 37.5, 75 / 37.5, 75 / 37.5 | 300 | 60 |
| 12 | 280 / 140 | 280 | 200 |
| 13 | 150 / 75, 150 / 75 | 300 | 400 |
| 14 | 150 / 75, 150 / 75 | 300 | 400 |
| 15 | 320 / 160 | 320 | 600 |
| 16 | 110 / 55, 110 / 55, 110 / 55 | 330 | 940 |
| 17 | 400 / 20 | 400 | 1000 |
| 19 | 240 / 120 | 240 | 500 |
| 22 | 120 / 60, 120 / 60 | 240 | 20 |
| 23 | 100 / 5, 40 / 20 | 140 | 10 |
| 24 | 100 / 5 | 100 | 10 |
| 25 | 200 / 100 | 200 | 900 |
| 27 | 80 / 40, 80 / 40, 80 / 40, 80 / 40, 80 / 40, 80 / 40, 80 / 40 | 560 | 140 |
| 28 | 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10 | 180 | 180 |
| 29 | 80 / 40, 80 / 40, 80 / 40, 80 / 40, 80 / 40, 80 / 40, 80 / 40, 5 / 2.5 | 565 | 0 |
| 31 | 250 / 125 | 250 | 200 |
| 32 | 120 / 60, 120 / 60 | 240 | 400 |
| 33 | 80 / 40, 80 / 40, 80 / 40 | 240 | 940 |
| 34 | 10 / 5 | 10 | 20 |
| 35 | 300 / 15, 300 / 150 | 600 | 4000 |
| 36 | 10 / 5, 40 / 20, 10 / 5, 40 / 20, 10 / 5, 40 / 20, 10 / 5 | 160 | 140 |
| 37 | 250 / 12.5 | 250 | 100 |
| 38 | 300 / 15 | 300 | 100 |
| 39 | 450 / 22.5, 40 / 2, 40 / 2, 40 / 2, 40 / 2, 40 / 2, 40 / 2 | 690 | 700 |
| 40 | 40 / 20, 40 / 20, 40 / 20, 40 / 20, 40 / 20, 40 / 20, 40 / 20, 40 / 20, 40 / 20, 40 / 20 | 400 | 140 |
| 41 | 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10, 20 / 10 | 200 | 180 |
| 42 | 150 / 90 | 150 | 120 |
| 43 | 40 / 20, 40 / 20, 125 / 62.5, 40 / 20, 125 / 62.5, 40 / 20, 40 / 20 | 450 | 140 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 0 | 300 / 225 | 5 |
| 1 | 20 / 15 | 5 |
| 2 | 190 / 142.5 | 5 |
| 3 | 300 / 225 | 5 |
| 5 | 100 / 75 | 0 |
| 6 | 100 / 50 | 0 |
| 10 | 200 / 100 | 5 |
| 11 | 20 / 10 | 5 |
| 15 | 200 / 100 | 5 |
| 16 | 10 / 5 | 5 |
| 20 | 300 / 150 | 5 |
| 21 | 300 / 150 | 5 |
| 22 | 300 / 150 | 5 |
| 23 | 300 / 150 | 5 |
| 24 | 300 / 150 | 5 |
| 25 | 300 / 150 | 5 |
| 26 | 300 / 150 | 5 |
| 27 | 300 / 150 | 5 |
| 28 | 300 / 150 | 5 |
| 29 | 300 / 150 | 5 |
| 30 | 300 / 150 | 5 |
| 31 | 300 / 150 | 5 |
| 32 | 300 / 150 | 5 |
| 33 | 300 / 150 | 5 |
| 34 | 300 / 150 | 5 |
| 35 | 300 / 150 | 5 |
| 36 | 300 / 150 | 5 |
| 37 | 300 / 150 | 5 |
| 38 | 300 / 150 | 5 |
| 39 | 300 / 150 | 5 |
| 40 | 20 / 10 | 0 |
| 41 | 20 / 10 | 0 |
| 42 | 20 / 10 | 0 |
| 43 | 20 / 10 | 0 |
| 44 | 20 / 10 | 0 |
| 45 | 20 / 10 | 0 |
| 46 | 20 / 10 | 0 |
| 47 | 20 / 10 | 0 |
| 48 | 20 / 10 | 0 |
| 49 | 20 / 10 | 0 |
| 50 | 50 / 25 | 0 |
| 51 | 20 / 10 | 5 |
| 52 | 190 / 95 | 5 |
| 55 | 100 / 75 | 0 |
| 56 | 100 / 50 | 0 |
| 60 | 500 / 25 | 100 |
| 61 | 300 / 150 | 5 |
| 62 | 25 / 12.5 | 0 |
| 63 | 25 / 12.5 | 0 |
| 64 | 25 / 12.5 | 0 |
| 65 | 300 / 150 | 100 |
| 66 | 300 / 150 | 5 |
| 67 | 25 / 12.5 | 0 |
| 68 | 25 / 12.5 | 0 |
| 69 | 25 / 12.5 | 0 |
| 70 | 35 / 17.5 | 100 |
| 71 | 10 / 5 | 100 |
| 72 | 10 / 5 | 100 |
| 73 | 10 / 5 | 100 |
| 80 | 150 / 112.5 | 5 |
| 81 | 150 / 112.5 | 5 |
| 82 | 150 / 112.5 | 5 |
| 83 | 150 / 112.5 | 5 |
| 84 | 150 / 112.5 | 5 |

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
| 89 | 400 |
| 90 | 300 |
| 91 | 100 |
| 106 | 10 |
| 131 | 50 |

## Logged session (2026-10-30)

Health 3500. Highest Stamina seen 2000. Hits logged 72.

Seen so far: 14 of 34 attack rows, 3 of 64 projectiles, 5 of 15 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 50 / 25 | 5 | Square [charged], Square |
| 1 | 0 | 70 / 35 | 5 | Square, Square [charged] |
| 1 | 1 | 70 / 35 | 5 | Square, Square [charged] |
| 2 | 0 | 150 / 75 | 25 | Square, Square [charged] |
| 3 | 0 | 150 / 75 | 50 | Triangle, Triangle [charged] |
| 6 | 0 | 75 / 37.5 | 15 | Square + R1, Square + R1 [charged] |
| 6 | 1 | 75 / 37.5 | 15 | Square + R1, Square + R1 [charged] |
| 6 | 2 | 75 / 37.5 | 15 | Square + R1, Square + R1 [charged] |
| 6 | 3 | 75 / 37.5 | 15 | Square + R1, Square + R1 [charged] |
| 12 | 0 | 280 / 140 | 200 | Triangle |
| 13 | 0 | 150 / 75 | 200 | Triangle |
| 13 | 1 | 150 / 75 | 200 | Triangle |
| 15 | 0 | 320 / 160 | 600 | Triangle |
| 23 | 0 | 100 / 5 | 10 | Square, Square [charged] |
| 24 | 0 | 100 / 5 | 10 | Square, Square [charged] |
| 27 | 0 | 80 / 40 | 20 | Up + Circle, Up + Circle [charged] |
| 27 | 2 | 80 / 40 | 20 | Up + Circle, Up + Circle [charged] |
| 27 | 4 | 80 / 40 | 20 | Up + Circle, Up + Circle [charged] |
| 27 | 5 | 80 / 40 | 20 | Up + Circle, Up + Circle [charged] |
| 27 | 6 | 80 / 40 | 20 | Up + Circle, Up + Circle [charged] |
| 28 | 0 | 20 / 10 | 20 | Up + Circle, Up + Circle [charged] |
| 28 | 1 | 20 / 10 | 20 | Up + Circle, Up + Circle [charged] |
| 28 | 2 | 20 / 10 | 20 | Up + Circle, Up + Circle [charged] |
| 28 | 3 | 20 / 10 | 20 | Up + Circle, Up + Circle [charged] |
| 28 | 4 | 20 / 10 | 20 | Up + Circle, Up + Circle [charged] |
| 28 | 5 | 20 / 10 | 20 | Up + Circle, Up + Circle [charged] |
| 28 | 6 | 20 / 10 | 20 | Up + Circle, Up + Circle [charged] |
| 28 | 7 | 20 / 10 | 20 | Up + Circle, Up + Circle [charged] |
| 28 | 8 | 20 / 10 | 20 | Up + Circle, Up + Circle [charged] |
| 29 | 0 | 80 / 40 | 0 | Square |
| 29 | 2 | 80 / 40 | 0 | Square |
| 29 | 4 | 80 / 40 | 0 | Square |
| 29 | 6 | 80 / 40 | 0 | Square |
| 35 | 0 | 300 / 15 | 2000 | Triangle + R1, Triangle + R1 [charged] |

| Projectile | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|
| 0 | 300 / 225 | 5 | Circle, Circle [charged] |
| 6 | 100 / 50 | 0 | Circle + R1, Circle + R1 [charged] |
| 44 | 20 / 10 | 0 | Triangle |

Actions with a Stamina cost that landed a hit: 29: 50, 88: 200, 89: 400, 90: 300, 131: 50.

## Knockdowns

Hits that knock the target down, from the attack data (2026-11-03). The number is the hit's reaction (attack slot +0x39, projectile row +0xAF); build-up is its "too many hits" number (the target goes down at 150). What each number does and the get-up are in [Knockdowns.md](../balance/Knockdowns.md).

| Row | Move | Hit | Number | What it does | Damage | Build-up |
|---|---|---|---|---|---|---|
| 2 |  | 0 | 6 | knocked away (dmg_blow) | 150 | 25 |
| 3 |  | 0 | 21 | launched, then down | 150 | 50 |
| 4 |  | 0 | 5 | spun away (dmg_blow_spin) | 180 | 50 |
| 5 |  | 0 | 6 | knocked away (dmg_blow) | 320 | 100 |
| 6 |  | 1 | 21 | launched, then down | 75 | 15 |
| 6 |  | 3 | 11 | shocked, then down (dmg_shock) | 75 | 15 |
| 13 |  | 1 | 21 | launched, then down | 150 | 200 |
| 14 |  | 1 | 5 | spun away (dmg_blow_spin) | 150 | 200 |
| 15 |  | 0 | 17 | big knock away (dmg_conspicuous_blow) | 320 | 600 |
| 16 |  | 0 | 21 | launched, then down | 110 | 20 |
| 16 |  | 1 | 5 | spun away (dmg_blow_spin) | 110 | 20 |
| 16 |  | 2 | 3 | lifted off the feet; lands and goes down unless hit again | 110 | 900 |
| 17 |  | 0 | 6 | knocked away (dmg_blow) | 400 | 1000 |
| 19 |  | 0 | 12 | pierced, then down (dmg_penetration) | 240 | 500 |
| 23 |  | 0 | 9 | knocked down, big bounce | 100 | 10 |
| 23 |  | 1 | 18 | big bounce (dmg_conspicuous_bound) | 40 | 0 |
| 24 |  | 0 | 9 | knocked down, big bounce | 100 | 10 |
| 25 |  | 0 | 19 | knocked away (dmg_kizuna1_hit_blow) | 200 | 900 |
| 27 |  | 0 | 4 | knocked down, big bounce | 80 | 20 |
| 28 |  | 8 | 11 | shocked, then down (dmg_shock) | 20 | 20 |
| 29 |  | 0 | 4 | knocked down, big bounce | 80 | 0 |
| 29 |  | 6 | 6 | knocked away (dmg_blow) | 80 | 0 |
| 31 |  | 0 | 20 | big bounce (dmg_conspicuous_bound 2) | 250 | 200 |
| 32 |  | 1 | 5 | spun away (dmg_blow_spin) | 120 | 200 |
| 33 |  | 0 | 4 | knocked down, big bounce | 80 | 20 |
| 33 |  | 2 | 9 | knocked down, big bounce | 80 | 900 |
| 35 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 300 | 2000 |
| 35 |  | 1 | 10 | knocked down, rolling bounce | 300 | 2000 |
| 36 |  | 6 | 6 | knocked away (dmg_blow) | 10 | 20 |
| 39 |  | 0 | 10 | knocked down, rolling bounce | 450 | 100 |
| 39 |  | 6 | 6 | knocked away (dmg_blow) | 40 | 100 |
| 41 |  | 8 | 11 | shocked, then down (dmg_shock) | 20 | 20 |

| Projectile row | Number | What it does | Damage |
|---|---|---|---|
| 0 | 5 | spun away (dmg_blow_spin) | 300 |
| 2 | 5 | spun away (dmg_blow_spin) | 190 |
| 3 | 5 | spun away (dmg_blow_spin) | 300 |
| 10 | 5 | spun away (dmg_blow_spin) | 200 |
| 11 | 5 | spun away (dmg_blow_spin) | 20 |
| 16 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 20 | 5 | spun away (dmg_blow_spin) | 300 |
| 21 | 5 | spun away (dmg_blow_spin) | 300 |
| 22 | 5 | spun away (dmg_blow_spin) | 300 |
| 23 | 5 | spun away (dmg_blow_spin) | 300 |
| 24 | 5 | spun away (dmg_blow_spin) | 300 |
| 25 | 5 | spun away (dmg_blow_spin) | 300 |
| 26 | 5 | spun away (dmg_blow_spin) | 300 |
| 27 | 5 | spun away (dmg_blow_spin) | 300 |
| 28 | 5 | spun away (dmg_blow_spin) | 300 |
| 29 | 5 | spun away (dmg_blow_spin) | 300 |
| 30 | 5 | spun away (dmg_blow_spin) | 300 |
| 31 | 5 | spun away (dmg_blow_spin) | 300 |
| 32 | 5 | spun away (dmg_blow_spin) | 300 |
| 33 | 5 | spun away (dmg_blow_spin) | 300 |
| 34 | 5 | spun away (dmg_blow_spin) | 300 |
| 35 | 5 | spun away (dmg_blow_spin) | 300 |
| 36 | 5 | spun away (dmg_blow_spin) | 300 |
| 37 | 5 | spun away (dmg_blow_spin) | 300 |
| 38 | 5 | spun away (dmg_blow_spin) | 300 |
| 39 | 5 | spun away (dmg_blow_spin) | 300 |
| 51 | 3 | lifted off the feet; lands and goes down unless hit again | 20 |
| 52 | 3 | lifted off the feet; lands and goes down unless hit again | 190 |
| 60 | 5 | spun away (dmg_blow_spin) | 500 |
| 61 | 5 | spun away (dmg_blow_spin) | 300 |
| 65 | 5 | spun away (dmg_blow_spin) | 300 |
| 66 | 5 | spun away (dmg_blow_spin) | 300 |
| 70 | 5 | spun away (dmg_blow_spin) | 35 |
| 71 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 72 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 73 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 80 | 5 | spun away (dmg_blow_spin) | 150 |
| 82 | 5 | spun away (dmg_blow_spin) | 150 |
| 83 | 5 | spun away (dmg_blow_spin) | 150 |

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Susano'o | Press R3 during a Victory Burst. |
| Mangekyo Sharingan: Amaterasu | R1 + Circle (Hold to charge) |
| Susano'o Inferno Style: Flame Control | Press R3 while summoning Susano'o. |
| Chidori | Left stick up + Circle |
| Fire Style: Fireball Jutsu | Circle (Also possible while jumping) When on the ground, press Left stick down + Circle to fire into the air. |
| Stamina Charge | R1 + Cross Remaining stamina doubles when you start charging. |
