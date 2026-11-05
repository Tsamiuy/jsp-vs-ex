# Nūbē (character 041)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `041_nueno_mp_param_attack_PS3.da` inside `battle_character_041_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 108 / 97.2 | 108 | 8 |
| 1 | 40 / 12, 108 / 97.2, 100 / 30 | 248 | 58 |
| 2 | 216 / 194.4, 40 / 12, 40 / 12, 40 / 12, 40 / 12, 40 / 12, 40 / 12 | 456 | 26 |
| 3 | 280 / 84, 40 / 12, 40 / 12, 40 / 12, 40 / 12, 40 / 12, 40 / 12 | 520 | 35 |
| 4 | 300 / 270, 45 / 13.5, 45 / 13.5, 45 / 13.5, 45 / 13.5, 45 / 13.5, 45 / 13.5 | 570 | 118 |
| 5 | 300 / 90 | 300 | 0 |
| 6 | 300 / 90, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15 | 550 | 325 |
| 7 | 300 / 90 | 300 | 0 |
| 8 | 300 / 90 | 300 | 0 |
| 9 | 300 / 90 | 300 | 0 |
| 10 | 300 / 90 | 300 | 0 |
| 11 | 300 / 90 | 300 | 0 |
| 12 | 216 / 194.4, 100 / 30, 100 / 30 | 416 | 400 |
| 13 | 360 / 324 | 360 | 50 |
| 14 | 444 / 399.6, 85 / 25.5, 85 / 25.5, 85 / 25.5 | 699 | 425 |
| 15 | 324 / 291.6, 100 / 30, 100 / 30 | 524 | 150 |
| 16 | 300 / 90 | 300 | 0 |
| 17 | 300 / 90 | 300 | 0 |
| 18 | 40 / 12, 40 / 12, 40 / 12, 40 / 12, 40 / 12 | 200 | 25 |
| 19 | 300 / 90 | 300 | 0 |
| 20 | 300 / 90 | 300 | 0 |
| 21 | 160 / 48 | 160 | 5 |
| 22 | 300 / 90 | 300 | 0 |
| 23 | 300 / 90 | 300 | 0 |
| 24 | 300 / 90 | 300 | 2000 |
| 25 | 300 / 90 | 300 | 200 |
| 26 | 150 / 45 | 150 | 0 |
| 27 | 150 / 45 | 150 | 0 |
| 28 | 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15 | 350 | 350 |
| 29 | 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 50 / 15, 1500 / 450 | 1850 | 850 |
| 30 | 150 / 45 | 150 | 120 |
| 31 | 250 / 75 | 250 | 20 |
| 32 | 250 / 75, 100 / 30, 100 / 30, 50 / 15, 50 / 15, 50 / 15, 50 / 15 | 650 | 30 |
| 33 | 100 / 30, 1 / 0.3, 1 / 0.3, 1 / 0.3 | 103 | 10 |
| 34 | 100 / 30, 70 / 21, 70 / 21 | 240 | 30 |
| 35 | 100 / 30, 1 / 0.3, 1 / 0.3, 1 / 0.3 | 103 | 10 |
| 36 | 200 / 60, 70 / 21, 70 / 21 | 340 | 30 |
| 37 | 300 / 90 | 300 | 2000 |
| 38 | 300 / 90 | 300 | 2000 |
| 40 | 200 / 60, 200 / 60 | 400 | 500 |
| 41 | 30 / 9, 300 / 90 | 330 | 0 |
| 42 | 30 / 9, 300 / 90 | 330 | 0 |
| 43 | 50 / 15, 250 / 75 | 300 | 2000 |
| 44 | 50 / 15, 250 / 75 | 300 | 2000 |
| 45 | 30 / 9, 300 / 90 | 330 | 0 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 25 | 35 / 26.2 | 450 |
| 26 | 10 / 7.5 | 450 |
| 27 | 10 / 7.5 | 450 |
| 28 | 10 / 7.5 | 450 |

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
| 89 | 200 |
| 90 | 300 |
| 91 | 400 |
| 93 | 100 |
| 106 | 10 |
| 131 | 50 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen at least 1720 (my hold ended before the top). Hits logged 66.

Seen so far: 14 of 45 attack rows, 0 of 4 projectiles, 5 of 16 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 108 / 97.2 | 8 | Square, Square [charged] |
| 1 | 0 | 40 / 12 | 50 | Square |
| 1 | 1 | 108 / 97.2 | 8 | Triangle |
| 2 | 0 | 216 / 194.4 | 8 | Square [charged] |
| 4 | 0 | 300 / 270 | 100 | Triangle, Triangle [charged] |
| 12 | 0 | 216 / 194.4 | 50 | Triangle, Triangle [charged] |
| 13 | 0 | 360 / 324 | 50 | Triangle |
| 21 | 0 | 160 / 48 | 5 | Square, Square [charged] |
| 24 | 0 | 300 / 90 | 2000 | Triangle + R1, Triangle + R1 [charged] |
| 25 | 0 | 300 / 90 | 200 | Triangle + R1, Triangle + R1 [charged] |
| 40 | 0 | 200 / 60 | 0 | Up + Circle, Up + Circle [charged] |
| 40 | 1 | 200 / 60 | 500 | Up + Circle, Up + Circle [charged] |
| 41 | 0 | 30 / 9 | 0 | Circle, Circle [charged] |
| 42 | 0 | 30 / 9 | 0 | Circle, Circle [charged] |
| 43 | 0 | 50 / 15 | 0 | Circle + R1 |
| 43 | 1 | 250 / 75 | 2000 | Circle + R1 |
| 45 | 0 | 30 / 9 | 0 | Circle |

Actions with a Stamina cost that landed a hit: 88: 300, 89: 200, 90: 300, 91: 400, 131: 50.

## Knockdowns

Hits that knock the target down, from the attack data (2026-11-03). The number is the hit's reaction (attack slot +0x39, projectile row +0xAF); build-up is its "too many hits" number (the target goes down at 150). What each number does and the get-up are in [Knockdowns.md](../balance/Knockdowns.md).

| Row | Move | Hit | Number | What it does | Damage | Build-up |
|---|---|---|---|---|---|---|
| 2 |  | 0 | 12 | pierced, then down (dmg_penetration) | 216 | 8 |
| 2 |  | 6 | 6 | knocked away (dmg_blow) | 40 | 8 |
| 3 |  | 0 | 10 | knocked down, rolling bounce | 280 | 5 |
| 3 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 40 | 5 |
| 4 |  | 0 | 6 | knocked away (dmg_blow) | 300 | 100 |
| 4 |  | 6 | 6 | knocked away (dmg_blow) | 45 | 8 |
| 6 |  | 0 | 10 | knocked down, rolling bounce | 300 | 300 |
| 6 |  | 5 | 6 | knocked away (dmg_blow) | 50 | 5 |
| 14 |  | 0 | 6 | knocked away (dmg_blow) | 444 | 50 |
| 14 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 85 | 125 |
| 14 |  | 2 | 3 | lifted off the feet; lands and goes down unless hit again | 85 | 125 |
| 14 |  | 3 | 3 | lifted off the feet; lands and goes down unless hit again | 85 | 125 |
| 15 |  | 0 | 17 | big knock away (dmg_conspicuous_blow) | 324 | 150 |
| 15 |  | 2 | 17 | big knock away (dmg_conspicuous_blow) | 100 | 0 |
| 18 |  | 4 | 12 | pierced, then down (dmg_penetration) | 40 | 5 |
| 19 |  | 0 | 6 | knocked away (dmg_blow) | 300 | 0 |
| 25 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 300 | 200 |
| 27 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 150 | 0 |
| 29 |  | 9 | 6 | knocked away (dmg_blow) | 1500 | 500 |
| 31 |  | 0 | 19 | knocked away (dmg_kizuna1_hit_blow) | 250 | 20 |
| 32 |  | 0 | 20 | big bounce (dmg_conspicuous_bound 2) | 250 | 20 |
| 36 |  | 0 | 6 | knocked away (dmg_blow) | 200 | 10 |
| 37 |  | 0 | 6 | knocked away (dmg_blow) | 300 | 2000 |
| 38 |  | 0 | 6 | knocked away (dmg_blow) | 300 | 2000 |
| 40 |  | 0 | 12 | pierced, then down (dmg_penetration) | 200 | 0 |
| 40 |  | 1 | 12 | pierced, then down (dmg_penetration) | 200 | 500 |
| 41 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 300 | 0 |
| 42 |  | 0 | 11 | shocked, then down (dmg_shock) | 30 | 0 |
| 42 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 300 | 0 |
| 43 |  | 0 | 11 | shocked, then down (dmg_shock) | 50 | 0 |
| 43 |  | 1 | 6 | knocked away (dmg_blow) | 250 | 2000 |
| 44 |  | 0 | 11 | shocked, then down (dmg_shock) | 50 | 0 |
| 44 |  | 1 | 6 | knocked away (dmg_blow) | 250 | 2000 |
| 45 |  | 0 | 6 | knocked away (dmg_blow) | 30 | 0 |
| 45 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 300 | 0 |

| Projectile row | Number | What it does | Damage |
|---|---|---|---|
| 25 | 5 | spun away (dmg_blow_spin) | 35 |
| 26 | 5 | spun away (dmg_blow_spin) | 10 |
| 27 | 5 | spun away (dmg_blow_spin) | 10 |
| 28 | 5 | spun away (dmg_blow_spin) | 10 |

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Demonic Liberation | Press R3 during a Victory Burst. |
| Crystal Memories | R1 + Circle |
| Guardian Scroll | Hold Circle |
| Accursed Tome | Release Circle quickly. |
| Devil's Hand | Left stick up + Circle |
| Guard Breaking | After hitting the enemy with R1 + Triangle, press Triangle to strike again. |
| Stamina Charge | R1 + Cross Keep holding to charge beyond the Stamina Gauge limit. |
