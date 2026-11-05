# Akainu (character 003)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `003_akainu_mp_param_attack_PS3.d` inside `battle_character_003_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 110 / 22 | 110 | 5 |
| 1 | 100 / 20, 70 / 14, 70 / 14 | 240 | 25 |
| 2 | 215 / 43 | 215 | 30 |
| 3 | 280 / 56 | 280 | 200 |
| 4 | 110 / 22, 110 / 22, 110 / 22, 110 / 22, 110 / 22, 110 / 22 | 660 | 300 |
| 5 | 300 / 60 | 300 | 0 |
| 6 | 50 / 10 | 50 | 50 |
| 7 | 50 / 10 | 50 | 50 |
| 8 | 100 / 20 | 100 | 100 |
| 9 | 300 / 60 | 300 | 0 |
| 10 | 300 / 60 | 300 | 0 |
| 11 | 300 / 60 | 300 | 0 |
| 12 | 100 / 20 | 100 | 100 |
| 13 | 200 / 40 | 200 | 200 |
| 14 | 300 / 60 | 300 | 0 |
| 15 | 300 / 60 | 300 | 0 |
| 16 | 300 / 60 | 300 | 0 |
| 17 | 300 / 60 | 300 | 0 |
| 18 | 100 / 20, 100 / 20, 100 / 20, 100 / 20 | 400 | 20 |
| 19 | 300 / 60 | 300 | 0 |
| 20 | 300 / 60 | 300 | 0 |
| 21 | 220 / 44 | 220 | 115 |
| 22 | 300 / 60 | 300 | 0 |
| 23 | 300 / 60 | 300 | 0 |
| 24 | 300 / 60, 300 / 60 | 600 | 300 |
| 25 | 700 / 140, 300 / 60 | 1000 | 300 |
| 26 | 100 / 20, 100 / 20, 100 / 20, 100 / 20, 100 / 20, 100 / 20 | 600 | 250 |
| 27 | 300 / 60, 300 / 60 | 600 | 50 |
| 28 | 400 / 80, 400 / 80, 100 / 20, 100 / 20, 100 / 20 | 1100 | 2000 |
| 29 | 300 / 60, 300 / 60 | 600 | 0 |
| 30 | 500 / 100, 300 / 60 | 800 | 0 |
| 31 | 600 / 120, 300 / 60 | 900 | 300 |
| 32 | 600 / 120, 300 / 60 | 900 | 300 |
| 33 | 50 / 10, 50 / 10, 50 / 10, 50 / 10, 50 / 10, 100 / 20 | 350 | 0 |
| 34 | 300 / 60, 300 / 60 | 600 | 0 |
| 35 | 300 / 60, 150 / 30, 150 / 30, 150 / 30, 150 / 30, 150 / 30, 150 / 30, 150 / 30, 150 / 30, 150 / 30 | 1650 | 0 |
| 36 | 300 / 60, 150 / 30, 150 / 30, 150 / 30, 150 / 30, 150 / 30, 150 / 30, 150 / 30, 150 / 30, 150 / 30 | 1650 | 0 |
| 37 | 300 / 60, 100 / 20, 100 / 20, 100 / 20, 100 / 20, 100 / 20 | 800 | 0 |
| 38 | 300 / 60, 300 / 60 | 600 | 0 |
| 39 | 150 / 30 | 150 | 120 |
| 40 | 250 / 50, 1 / 0.2, 1 / 0.2, 1 / 0.2, 1 / 0.2, 1 / 0.2 | 255 | 20 |
| 41 | 250 / 50, 100 / 20, 100 / 20, 50 / 10, 50 / 10, 50 / 10, 50 / 10 | 650 | 30 |
| 42 | 110 / 22, 30 / 6, 30 / 6, 30 / 6, 30 / 6, 1 / 0.2 | 231 | 10 |
| 43 | 110 / 22, 70 / 14, 70 / 14 | 250 | 30 |
| 44 | 160 / 32, 70 / 14, 70 / 14 | 300 | 30 |
| 45 | 110 / 22 | 110 | 5 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 0 | 200 / 150 | 50 |
| 1 | 100 / 75 | 10 |
| 2 | 100 / 75 | 10 |
| 3 | 100 / 75 | 10 |
| 4 | 100 / 75 | 10 |
| 5 | 200 / 150 | 50 |
| 6 | 100 / 75 | 10 |
| 7 | 100 / 75 | 10 |
| 8 | 100 / 75 | 10 |
| 9 | 100 / 75 | 10 |
| 10 | 300 / 225 | 50 |
| 11 | 300 / 225 | 50 |
| 12 | 100 / 75 | 10 |
| 13 | 100 / 75 | 10 |
| 14 | 100 / 75 | 10 |
| 15 | 400 / 300 | 250 |
| 16 | 500 / 375 | 250 |
| 17 | 200 / 150 | 10 |
| 18 | 200 / 150 | 10 |
| 19 | 200 / 150 | 10 |
| 20 | 250 / 187.5 | 200 |
| 21 | 250 / 187.5 | 200 |
| 22 | 250 / 187.5 | 200 |
| 23 | 250 / 187.5 | 200 |
| 24 | 250 / 187.5 | 200 |
| 25 | 250 / 187.5 | 200 |
| 26 | 250 / 187.5 | 200 |
| 27 | 250 / 187.5 | 200 |
| 28 | 250 / 187.5 | 200 |
| 29 | 250 / 187.5 | 200 |
| 30 | 250 / 187.5 | 200 |
| 31 | 250 / 187.5 | 200 |
| 32 | 250 / 187.5 | 200 |
| 33 | 250 / 187.5 | 200 |
| 34 | 250 / 187.5 | 200 |
| 35 | 800 / 600 | 0 |
| 36 | 800 / 600 | 0 |
| 37 | 800 / 600 | 0 |
| 38 | 800 / 600 | 0 |
| 39 | 800 / 600 | 0 |
| 40 | 100 / 75 | 10 |
| 41 | 100 / 75 | 10 |
| 42 | 100 / 75 | 10 |
| 43 | 100 / 75 | 10 |
| 44 | 100 / 75 | 10 |
| 45 | 100 / 75 | 10 |
| 46 | 100 / 75 | 10 |
| 47 | 100 / 75 | 10 |
| 48 | 100 / 75 | 10 |
| 49 | 100 / 75 | 10 |

## Stamina costs

From `param_action`: +0x0C is the cost, on 48-byte rows in groups of 4. Action = row / 4. Which skill uses each action is only known after a logged session. A negative number has not been tested; it may give Stamina back.

| Action | Stamina |
|---|---|
| 11 | 50 |
| 16 | 100 |
| 29 | 150 |
| 30 | 150 |
| 31 | 150 |
| 32 | 50 |
| 35 | 100 |
| 36 | 100 |
| 37 | 100 |
| 85 | 50 |
| 86 | 50 |
| 87 | 50 |
| 88 | 500 |
| 89 | 500 |
| 90 | 500 |
| 91 | 100 |
| 92 | 100 |
| 106 | 10 |
| 131 | 50 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen 2000. Hits logged 104.

Seen so far: 14 of 46 attack rows, 3 of 50 projectiles, 7 of 19 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 110 / 22 | 5 | Square, Square [charged] |
| 1 | 0 | 100 / 20 | 10 | Square, Square [charged] |
| 1 | 1 | 70 / 14 | 10 | Square, Triangle, Square [charged] |
| 2 | 0 | 215 / 43 | 30 | Square [charged] |
| 3 | 0 | 280 / 56 | 200 | Triangle, Triangle [charged] |
| 6 | 0 | 50 / 10 | 50 | Square + R1 [charged] |
| 12 | 0 | 100 / 20 | 100 | Triangle, Triangle [charged] |
| 13 | 0 | 200 / 40 | 200 | Triangle |
| 21 | 0 | 220 / 44 | 115 | Square, Square [charged] |
| 22 | 0 | 300 / 60 | 0 | Square, Square [charged] |
| 24 | 0 | 300 / 60 | 300 | Up + Circle, Up + Circle [charged] |
| 26 | 0 | 100 / 20 | 50 | Circle |
| 26 | 1 | 100 / 20 | 50 | Circle |
| 26 | 2 | 100 / 20 | 50 | Circle |
| 26 | 3 | 100 / 20 | 50 | Circle |
| 26 | 4 | 100 / 20 | 50 | Circle |
| 27 | 0 | 300 / 60 | 50 | Circle |
| 30 | 0 | 500 / 100 | 0 | Triangle + R1, Triangle + R1 [charged] |
| 31 | 0 | 600 / 120 | 300 | Circle |

| Projectile | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|
| 1 | 100 / 75 | 10 | Triangle, Triangle [charged] |
| 5 | 200 / 150 | 50 | Triangle |
| 16 | 500 / 375 | 250 | Circle + R1, Circle + R1 [charged] |

Actions with a Stamina cost that landed a hit: 29: 150, 35: 100, 36: 100, 88: 500, 89: 500, 90: 500, 131: 50.

## Knockdowns

Hits that knock the target down, from the attack data (2026-11-03). The number is the hit's reaction (attack slot +0x39, projectile row +0xAF); build-up is its "too many hits" number (the target goes down at 150). What each number does and the get-up are in [Knockdowns.md](../balance/Knockdowns.md).

| Row | Move | Hit | Number | What it does | Damage | Build-up |
|---|---|---|---|---|---|---|
| 2 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 215 | 30 |
| 3 |  | 0 | 4 | knocked down, big bounce | 280 | 200 |
| 4 |  | 5 | 5 | spun away (dmg_blow_spin) | 110 | 50 |
| 8 |  | 0 | 17 | big knock away (dmg_conspicuous_blow) | 100 | 100 |
| 13 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 200 | 200 |
| 14 |  | 0 | 17 | big knock away (dmg_conspicuous_blow) | 300 | 0 |
| 18 |  | 2 | 3 | lifted off the feet; lands and goes down unless hit again | 100 | 10 |
| 24 |  | 0 | 6 | knocked away (dmg_blow) | 300 | 300 |
| 24 |  | 1 | 6 | knocked away (dmg_blow) | 300 | 0 |
| 25 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 700 | 300 |
| 25 |  | 1 | 6 | knocked away (dmg_blow) | 300 | 0 |
| 27 |  | 0 | 12 | pierced, then down (dmg_penetration) | 300 | 50 |
| 28 |  | 1 | 6 | knocked away (dmg_blow) | 400 | 2000 |
| 29 |  | 0 | 5 | spun away (dmg_blow_spin) | 300 | 0 |
| 30 |  | 0 | 9 | knocked down, big bounce | 500 | 0 |
| 30 |  | 1 | 6 | knocked away (dmg_blow) | 300 | 0 |
| 31 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 600 | 300 |
| 31 |  | 1 | 6 | knocked away (dmg_blow) | 300 | 0 |
| 32 |  | 0 | 4 | knocked down, big bounce | 600 | 300 |
| 32 |  | 1 | 6 | knocked away (dmg_blow) | 300 | 0 |
| 33 |  | 2 | 3 | lifted off the feet; lands and goes down unless hit again | 50 | 0 |
| 34 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 300 | 0 |
| 35 |  | 0 | 6 | knocked away (dmg_blow) | 300 | 0 |
| 35 |  | 1 | 6 | knocked away (dmg_blow) | 150 | 0 |
| 36 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 300 | 0 |
| 36 |  | 1 | 6 | knocked away (dmg_blow) | 150 | 0 |
| 38 |  | 0 | 5 | spun away (dmg_blow_spin) | 300 | 0 |
| 40 |  | 0 | 19 | knocked away (dmg_kizuna1_hit_blow) | 250 | 20 |
| 40 |  | 5 | 19 | knocked away (dmg_kizuna1_hit_blow) | 1 | 0 |
| 41 |  | 0 | 20 | big bounce (dmg_conspicuous_bound 2) | 250 | 20 |
| 44 |  | 0 | 6 | knocked away (dmg_blow) | 160 | 10 |

| Projectile row | Number | What it does | Damage |
|---|---|---|---|
| 10 | 5 | spun away (dmg_blow_spin) | 300 |
| 11 | 5 | spun away (dmg_blow_spin) | 300 |
| 12 | 5 | spun away (dmg_blow_spin) | 100 |
| 13 | 5 | spun away (dmg_blow_spin) | 100 |
| 14 | 5 | spun away (dmg_blow_spin) | 100 |
| 16 | 4 | knocked down, big bounce | 500 |
| 17 | 3 | lifted off the feet; lands and goes down unless hit again | 200 |
| 20 | 5 | spun away (dmg_blow_spin) | 250 |
| 21 | 5 | spun away (dmg_blow_spin) | 250 |
| 22 | 5 | spun away (dmg_blow_spin) | 250 |
| 23 | 5 | spun away (dmg_blow_spin) | 250 |
| 24 | 5 | spun away (dmg_blow_spin) | 250 |
| 30 | 5 | spun away (dmg_blow_spin) | 250 |
| 31 | 5 | spun away (dmg_blow_spin) | 250 |
| 32 | 5 | spun away (dmg_blow_spin) | 250 |
| 33 | 5 | spun away (dmg_blow_spin) | 250 |
| 34 | 5 | spun away (dmg_blow_spin) | 250 |
| 39 | 5 | spun away (dmg_blow_spin) | 800 |
| 41 | 4 | knocked down, big bounce | 100 |
| 42 | 3 | lifted off the feet; lands and goes down unless hit again | 100 |

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Meteor Volcano | Press R3 during a Victory Burst. |
| Volcano Splitter | R1 + Circle |
| Hell Hound | After hitting successfully with Left stick up + Circle, press Circle to follow up with another strike (Also possible while jumping). |
| Great Eruption | Circle (Hold to charge) |
| Hound Blaze | After a Great Eruption, hold Circle to charge. |
| Area Attack | R1 + Square ,  Square |
| Stamina Charge | R1 + Cross Keep holding to charge beyond the Stamina Gauge limit. |
