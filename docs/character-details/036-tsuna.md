# Tsuna & Reborn (character 036)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `036_tsuna_mp_param_attack_PS3.da` inside `battle_character_036_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 80 / 24 | 80 | 5 |
| 1 | 80 / 24, 80 / 24, 100 / 30 | 260 | 10 |
| 2 | 200 / 60, 40 / 12, 40 / 12, 40 / 12, 40 / 12, 40 / 12, 40 / 12 | 440 | 48 |
| 3 | 200 / 60, 40 / 12, 40 / 12, 40 / 12, 40 / 12, 40 / 12, 40 / 12 | 440 | 130 |
| 4 | 350 / 105, 45 / 13.5, 45 / 13.5, 45 / 13.5, 45 / 13.5, 45 / 13.5, 45 / 13.5 | 620 | 368 |
| 5 | 300 / 90 | 300 | 0 |
| 6 | 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15 | 300 | 30 |
| 7 | 300 / 90 | 300 | 0 |
| 8 | 300 / 90 | 300 | 0 |
| 9 | 300 / 90 | 300 | 0 |
| 10 | 300 / 90 | 300 | 0 |
| 11 | 300 / 90 | 300 | 0 |
| 12 | 200 / 60, 100 / 30, 100 / 30 | 400 | 450 |
| 13 | 300 / 90 | 300 | 200 |
| 14 | 100 / 30, 100 / 30, 100 / 30, 100 / 30 | 400 | 320 |
| 15 | 250 / 75, 100 / 30, 100 / 30 | 450 | 250 |
| 16 | 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15 | 250 | 250 |
| 17 | 400 / 120 | 400 | 300 |
| 18 | 100 / 30, 150 / 45 | 250 | 20 |
| 19 | 150 / 45 | 150 | 10 |
| 20 | 300 / 90 | 300 | 0 |
| 21 | 100 / 30 | 100 | 10 |
| 22 | 100 / 30 | 100 | 10 |
| 23 | 100 / 30 | 100 | 10 |
| 25 | 300 / 90, 150 / 45 | 450 | 0 |
| 26 | 150 / 45, 150 / 45 | 300 | 0 |
| 27 | 150 / 45 | 150 | 0 |
| 29 | 100 / 30 | 100 | 0 |
| 30 | 300 / 90 | 300 | 0 |
| 31 | 1000 / 300, 1000 / 300 | 2000 | 400 |
| 34 | 500 / 150 | 500 | 0 |
| 35 | 250 / 75 | 250 | 250 |
| 36 | 250 / 75 | 250 | 500 |
| 37 | 70 / 21, 70 / 21, 70 / 21, 70 / 21, 70 / 21, 70 / 21, 70 / 21, 70 / 21, 140 / 42 | 700 | 700 |
| 38 | 150 / 45 | 150 | 120 |
| 39 | 300 / 90, 300 / 90 | 600 | 0 |
| 40 | 150 / 45, 150 / 45 | 300 | 0 |
| 41 | 150 / 45 | 150 | 0 |
| 42 | 150 / 45 | 150 | 0 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 0 | 60 / 18 | 10 |
| 1 | 60 / 18 | 10 |
| 2 | 60 / 18 | 10 |
| 3 | 60 / 18 | 10 |
| 4 | 100 / 30 | 10 |
| 7 | 560 / 168 | 0 |
| 8 | 1 / 0.3 | 0 |
| 9 | 1 / 0.3 | 0 |
| 15 | 1200 / 360 | 400 |
| 25 | 50 / 15 | 0 |
| 30 | 250 / 75 | 0 |
| 36 | 200 / 60 | 0 |
| 40 | 30 / 9 | 10 |
| 41 | 30 / 9 | 10 |
| 42 | 30 / 9 | 10 |
| 43 | 30 / 9 | 10 |
| 44 | 50 / 15 | 10 |
| 45 | 50 / 15 | 0 |
| 46 | 50 / 15 | 0 |
| 47 | 50 / 15 | 0 |
| 48 | 50 / 15 | 0 |
| 49 | 100 / 30 | 0 |

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
| 88 | 300 |
| 89 | 600 |
| 90 | 200 |
| 91 | 10 |
| 92 | 10 |
| 106 | 10 |
| 131 | 250 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen 1000. Hits logged 41.

Seen so far: 9 of 39 attack rows, 5 of 22 projectiles, 4 of 16 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 80 / 24 | 5 | Square, Square [charged] |
| 6 | 0 | 50 / 15 | 5 | Square + R1 [charged] |
| 12 | 0 | 200 / 60 | 100 | Triangle, Triangle [charged] |
| 13 | 0 | 300 / 90 | 200 | Triangle |
| 14 | 1 | 100 / 30 | 80 | Triangle |
| 14 | 2 | 100 / 30 | 80 | Triangle |
| 14 | 3 | 100 / 30 | 80 | Triangle |
| 23 | 0 | 100 / 30 | 10 | Square, Square [charged] |
| 25 | 1 | 150 / 45 | 0 | Up + Circle |
| 29 | 0 | 100 / 30 | 0 | Triangle + R1, Triangle + R1 [charged] |
| 30 | 0 | 300 / 90 | 0 | Triangle + R1, Triangle + R1 [charged] |

| Projectile | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|
| 0 | 60 / 18 | 10 | Circle, Circle [charged] |
| 1 | 60 / 18 | 10 | Circle, Circle [charged] |
| 2 | 60 / 18 | 10 | Circle, Circle [charged] |
| 3 | 60 / 18 | 10 | Circle, Circle [charged] |
| 4 | 100 / 30 | 10 | Circle, Circle [charged] |

Actions with a Stamina cost that landed a hit: 29: 50, 88: 300, 89: 600, 131: 250.

## Knockdowns

Hits that knock the target down, from the attack data (2026-11-03). The number is the hit's reaction (attack slot +0x39, projectile row +0xAF); build-up is its "too many hits" number (the target goes down at 150). What each number does and the get-up are in [Knockdowns.md](../balance/Knockdowns.md).

| Row | Move | Hit | Number | What it does | Damage | Build-up |
|---|---|---|---|---|---|---|
| 0 |  | 0 | 21 | launched, then down | 80 | 5 |
| 1 |  | 0 | 21 | launched, then down | 80 | 5 |
| 2 |  | 0 | 6 | knocked away (dmg_blow) | 200 | 30 |
| 2 |  | 6 | 6 | knocked away (dmg_blow) | 40 | 8 |
| 3 |  | 0 | 6 | knocked away (dmg_blow) | 200 | 100 |
| 4 |  | 0 | 6 | knocked away (dmg_blow) | 350 | 350 |
| 4 |  | 6 | 6 | knocked away (dmg_blow) | 45 | 8 |
| 6 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 50 | 5 |
| 6 |  | 5 | 6 | knocked away (dmg_blow) | 50 | 5 |
| 13 |  | 0 | 21 | launched, then down | 300 | 200 |
| 14 |  | 3 | 6 | knocked away (dmg_blow) | 100 | 80 |
| 15 |  | 2 | 17 | big knock away (dmg_conspicuous_blow) | 100 | 0 |
| 17 |  | 0 | 17 | big knock away (dmg_conspicuous_blow) | 400 | 300 |
| 18 |  | 1 | 10 | knocked down, rolling bounce | 150 | 10 |
| 19 |  | 0 | 4 | knocked down, big bounce | 150 | 10 |
| 21 |  | 0 | 4 | knocked down, big bounce | 100 | 10 |
| 22 |  | 0 | 5 | spun away (dmg_blow_spin) | 100 | 10 |
| 23 |  | 0 | 9 | knocked down, big bounce | 100 | 10 |
| 25 |  | 1 | 6 | knocked away (dmg_blow) | 150 | 0 |
| 26 |  | 1 | 6 | knocked away (dmg_blow) | 150 | 0 |
| 27 |  | 0 | 6 | knocked away (dmg_blow) | 150 | 0 |
| 30 |  | 0 | 6 | knocked away (dmg_blow) | 300 | 0 |
| 31 |  | 0 | 6 | knocked away (dmg_blow) | 1000 | 400 |
| 31 |  | 1 | 6 | knocked away (dmg_blow) | 1000 | 0 |
| 34 |  | 0 | 5 | spun away (dmg_blow_spin) | 500 | 0 |
| 35 |  | 0 | 19 | knocked away (dmg_kizuna1_hit_blow) | 250 | 250 |
| 36 |  | 0 | 20 | big bounce (dmg_conspicuous_bound 2) | 250 | 500 |
| 37 |  | 8 | 3 | lifted off the feet; lands and goes down unless hit again | 140 | 140 |
| 39 |  | 0 | 6 | knocked away (dmg_blow) | 300 | 0 |
| 39 |  | 1 | 6 | knocked away (dmg_blow) | 300 | 0 |
| 40 |  | 1 | 6 | knocked away (dmg_blow) | 150 | 0 |
| 41 |  | 0 | 6 | knocked away (dmg_blow) | 150 | 0 |

| Projectile row | Number | What it does | Damage |
|---|---|---|---|
| 0 | 4 | knocked down, big bounce | 60 |
| 2 | 3 | lifted off the feet; lands and goes down unless hit again | 60 |
| 4 | 5 | spun away (dmg_blow_spin) | 100 |
| 7 | 5 | spun away (dmg_blow_spin) | 560 |
| 9 | 4 | knocked down, big bounce | 1 |
| 30 | 6 | knocked away (dmg_blow) | 250 |
| 36 | 5 | spun away (dmg_blow_spin) | 200 |
| 44 | 5 | spun away (dmg_blow_spin) | 50 |
| 45 | 4 | knocked down, big bounce | 50 |
| 49 | 5 | spun away (dmg_blow_spin) | 100 |

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Burning Axle | Press R3 during a Victory Burst. |
| X-Burst | Left stick up + Circle (Also possible while jumping) |
| X-Burner | Circle (Also possible while jumping) |
| Point Zero Break: Modified | R1 + Circle |
| Jumping Attack 2 | Press Triangle ,  Triangle ,  Triangle while jumping. |
| Stamina Charge | R1 + Cross Keep holding to charge stamina (charges quicker when health is less than 50%). |
