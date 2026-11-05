# Oga & Baby Beel (character 027)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `027_tatsumi_mp_param_attack_PS3.` inside `battle_character_027_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 120 / 36 | 120 | 5 |
| 1 | 100 / 30, 100 / 30 | 200 | 10 |
| 2 | 280 / 84 | 280 | 20 |
| 3 | 230 / 69 | 230 | 50 |
| 4 | 320 / 96 | 320 | 100 |
| 5 | 240 / 72 | 240 | 0 |
| 6 | 100 / 30, 100 / 30, 100 / 30 | 300 | 15 |
| 7 | 240 / 72 | 240 | 0 |
| 8 | 240 / 72 | 240 | 0 |
| 9 | 240 / 72 | 240 | 0 |
| 10 | 240 / 72 | 240 | 0 |
| 11 | 240 / 72 | 240 | 0 |
| 12 | 200 / 60 | 200 | 100 |
| 13 | 120 / 36, 120 / 36, 120 / 36 | 360 | 300 |
| 14 | 440 / 132 | 440 | 300 |
| 15 | 340 / 102 | 340 | 300 |
| 16 | 240 / 72 | 240 | 0 |
| 17 | 240 / 72 | 240 | 0 |
| 18 | 200 / 60 | 200 | 10 |
| 19 | 240 / 72 | 240 | 0 |
| 20 | 240 / 72 | 240 | 0 |
| 21 | 100 / 30, 100 / 30 | 200 | 10 |
| 22 | 150 / 45 | 150 | 5 |
| 23 | 150 / 45 | 150 | 10 |
| 24 | 150 / 45 | 150 | 10 |
| 26 | 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15 | 500 | 100 |
| 27 | 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15 | 500 | 100 |
| 28 | 400 / 120 | 400 | 300 |
| 29 | 250 / 75 | 250 | 500 |
| 31 | 300 / 90 | 300 | 500 |
| 32 | 115 / 34.5, 115 / 34.5, 115 / 34.5 | 345 | 750 |
| 33 | 250 / 75 | 250 | 1000 |
| 34 | 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15 | 450 | 90 |
| 35 | 200 / 60, 200 / 60, 200 / 60, 200 / 60, 200 / 60, 1500 / 450 | 2500 | 300 |
| 37 | 100 / 50 | 100 | 10 |
| 38 | 200 / 100, 70 / 35, 70 / 35 | 340 | 30 |
| 39 | 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15 | 500 | 100 |
| 40 | 200 / 60 | 200 | 100 |
| 41 | 200 / 60 | 200 | 100 |
| 42 | 160 / 48, 80 / 24, 80 / 24, 80 / 24, 80 / 24, 80 / 24, 80 / 24, 80 / 24, 80 / 24 | 800 | 90 |
| 43 | 80 / 24, 80 / 24, 80 / 24, 80 / 24, 80 / 24, 80 / 24, 80 / 24, 80 / 24, 80 / 24, 80 / 24 | 800 | 100 |
| 44 | 10 / 3 | 10 | 100 |
| 45 | 10 / 3, 10 / 3, 20 / 6 | 40 | 300 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 7 | 400 / 0 | 20 |
| 17 | 400 / 0 | 20 |
| 19 | 400 / 0 | 20 |
| 21 | 400 / 0 | 20 |
| 37 | 400 / 0 | 20 |

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
| 88 | 400 |
| 89 | 200 |
| 90 | 400 |
| 91 | 100 |
| 92 | 150 |
| 106 | 10 |
| 131 | 100 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen 2000. Hits logged 59.

Seen so far: 13 of 43 attack rows, 0 of 5 projectiles, 3 of 16 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 120 / 36 | 5 | Square, Square [charged] |
| 1 | 0 | 100 / 30 | 5 | Square, Square [charged] |
| 1 | 1 | 100 / 30 | 5 | Triangle, Square [charged] |
| 2 | 0 | 280 / 84 | 20 | Square [charged] |
| 3 | 0 | 230 / 69 | 50 | Triangle, Triangle [charged] |
| 6 | 0 | 100 / 30 | 5 | Square + R1, Square + R1 [charged] |
| 6 | 1 | 100 / 30 | 5 | Square + R1, Square + R1 [charged] |
| 6 | 2 | 100 / 30 | 5 | Square + R1, Square + R1 [charged] |
| 12 | 0 | 200 / 60 | 100 | Triangle |
| 13 | 0 | 120 / 36 | 100 | Triangle |
| 13 | 1 | 120 / 36 | 100 | Triangle |
| 13 | 2 | 120 / 36 | 100 | Triangle |
| 14 | 0 | 440 / 132 | 300 | Triangle |
| 23 | 0 | 150 / 45 | 10 | Square, Square [charged] |
| 24 | 0 | 150 / 45 | 10 | Square, Square [charged] |
| 26 | 1 | 50 / 15 | 10 | Circle |
| 26 | 2 | 50 / 15 | 10 | Circle |
| 26 | 5 | 50 / 15 | 10 | Circle |
| 28 | 0 | 400 / 120 | 300 | Triangle + R1, Triangle + R1 [charged] |
| 42 | 1 | 80 / 24 | 10 | Circle [charged] |
| 42 | 2 | 80 / 24 | 10 | Circle [charged] |
| 42 | 5 | 80 / 24 | 10 | Circle [charged] |

Actions with a Stamina cost that landed a hit: 29: 50, 88: 400, 131: 100.

## Knockdowns

Hits that knock the target down, from the attack data (2026-11-03). The number is the hit's reaction (attack slot +0x39, projectile row +0xAF); build-up is its "too many hits" number (the target goes down at 150). What each number does and the get-up are in [Knockdowns.md](../balance/Knockdowns.md).

| Row | Move | Hit | Number | What it does | Damage | Build-up |
|---|---|---|---|---|---|---|
| 2 |  | 0 | 10 | knocked down, rolling bounce | 280 | 20 |
| 4 |  | 0 | 17 | big knock away (dmg_conspicuous_blow) | 320 | 100 |
| 6 |  | 2 | 11 | shocked, then down (dmg_shock) | 100 | 5 |
| 8 |  | 0 | 4 | knocked down, big bounce | 240 | 0 |
| 12 |  | 0 | 4 | knocked down, big bounce | 200 | 100 |
| 13 |  | 2 | 12 | pierced, then down (dmg_penetration) | 120 | 100 |
| 14 |  | 0 | 9 | knocked down, big bounce | 440 | 300 |
| 15 |  | 0 | 4 | knocked down, big bounce | 340 | 300 |
| 16 |  | 0 | 14 | falls (dmg_seiya_fall), then down | 240 | 0 |
| 18 |  | 0 | 10 | knocked down, rolling bounce | 200 | 10 |
| 21 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 100 | 5 |
| 22 |  | 0 | 10 | knocked down, rolling bounce | 150 | 5 |
| 26 |  | 5 | 3 | lifted off the feet; lands and goes down unless hit again | 50 | 10 |
| 26 |  | 8 | 17 | big knock away (dmg_conspicuous_blow) | 50 | 10 |
| 26 |  | 9 | 6 | knocked away (dmg_blow) | 50 | 10 |
| 27 |  | 3 | 6 | knocked away (dmg_blow) | 50 | 10 |
| 27 |  | 8 | 6 | knocked away (dmg_blow) | 50 | 10 |
| 28 |  | 0 | 11 | shocked, then down (dmg_shock) | 400 | 300 |
| 29 |  | 0 | 19 | knocked away (dmg_kizuna1_hit_blow) | 250 | 500 |
| 33 |  | 0 | 20 | big bounce (dmg_conspicuous_bound 2) | 250 | 1000 |
| 34 |  | 5 | 3 | lifted off the feet; lands and goes down unless hit again | 50 | 10 |
| 34 |  | 8 | 17 | big knock away (dmg_conspicuous_blow) | 50 | 10 |
| 34 |  | 9 | 6 | knocked away (dmg_blow) | 50 | 10 |
| 35 |  | 0 | 5 | spun away (dmg_blow_spin) | 200 | 50 |
| 35 |  | 1 | 5 | spun away (dmg_blow_spin) | 200 | 50 |
| 35 |  | 2 | 5 | spun away (dmg_blow_spin) | 200 | 50 |
| 35 |  | 3 | 5 | spun away (dmg_blow_spin) | 200 | 50 |
| 35 |  | 5 | 4 | knocked down, big bounce | 1500 | 50 |
| 38 |  | 0 | 6 | knocked away (dmg_blow) | 200 | 10 |
| 39 |  | 3 | 6 | knocked away (dmg_blow) | 50 | 10 |
| 39 |  | 8 | 6 | knocked away (dmg_blow) | 50 | 10 |
| 40 |  | 0 | 4 | knocked down, big bounce | 200 | 100 |
| 41 |  | 0 | 17 | big knock away (dmg_conspicuous_blow) | 200 | 100 |
| 42 |  | 5 | 3 | lifted off the feet; lands and goes down unless hit again | 80 | 10 |
| 42 |  | 8 | 17 | big knock away (dmg_conspicuous_blow) | 80 | 10 |
| 42 |  | 9 | 6 | knocked away (dmg_blow) | 80 | 10 |
| 43 |  | 3 | 6 | knocked away (dmg_blow) | 80 | 10 |
| 43 |  | 8 | 6 | knocked away (dmg_blow) | 80 | 10 |
| 44 |  | 0 | 4 | knocked down, big bounce | 10 | 100 |
| 45 |  | 2 | 12 | pierced, then down (dmg_penetration) | 20 | 100 |

| Projectile row | Number | What it does | Damage |
|---|---|---|---|
| 7 | 4 | knocked down, big bounce | 400 |
| 17 | 6 | knocked away (dmg_blow) | 400 |
| 19 | 6 | knocked away (dmg_blow) | 400 |
| 21 | 10 | knocked down, rolling bounce | 400 |
| 37 | 4 | knocked down, big bounce | 400 |

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Chain Conflagration | Press R3 during a Victory Burst. |
| Zebul Feast | R1 + Circle |
| Zebul Blast | Circle |
| Dark Zebul Blast | Dark Zebul Blast |
| Zebul Emblem | Press Left stick up + Circle to fire Emblems at enemies. Press Left stick up + Circle again to detonate Zebul Emblems with a punch! |
| Dark Zebul Emblem | Dark Zebul Emblem |
| Stamina Charge | R1 + Cross Keep holding to charge beyond the Stamina Gauge limit. |
