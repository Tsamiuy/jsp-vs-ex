# Shishio (character 023)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `023_shishio_mp_param_attack_PS3.` inside `battle_character_023_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 100 / 80 | 100 | 5 |
| 1 | 60 / 48, 60 / 48, 120 / 96 | 240 | 15 |
| 2 | 240 / 192 | 240 | 15 |
| 3 | 320 / 256, 100 / 80, 100 / 80 | 520 | 200 |
| 4 | 300 / 240 | 300 | 0 |
| 5 | 300 / 240 | 300 | 0 |
| 6 | 350 / 280, 150 / 120 | 500 | 5 |
| 7 | 300 / 240 | 300 | 0 |
| 8 | 300 / 240 | 300 | 0 |
| 9 | 300 / 240 | 300 | 0 |
| 10 | 300 / 240 | 300 | 0 |
| 11 | 300 / 240 | 300 | 0 |
| 12 | 250 / 200 | 250 | 80 |
| 13 | 150 / 120, 150 / 120 | 300 | 200 |
| 14 | 500 / 400 | 500 | 320 |
| 15 | 350 / 280 | 350 | 280 |
| 16 | 300 / 240 | 300 | 0 |
| 17 | 300 / 240 | 300 | 0 |
| 18 | 180 / 144 | 180 | 5 |
| 19 | 300 / 240 | 300 | 0 |
| 20 | 300 / 240 | 300 | 0 |
| 21 | 220 / 176 | 220 | 8 |
| 22 | 300 / 240 | 300 | 10 |
| 23 | 300 / 240 | 300 | 0 |
| 24 | 250 / 200, 400 / 320 | 650 | 275 |
| 25 | 500 / 400, 200 / 160, 200 / 160, 400 / 320 | 1300 | 625 |
| 26 | 400 / 320, 200 / 160, 200 / 160, 200 / 160 | 1000 | 625 |
| 27 | 600 / 480, 250 / 200 | 850 | 275 |
| 28 | 60 / 48, 60 / 48, 60 / 48, 60 / 48, 60 / 48, 60 / 48, 60 / 48, 10 / 8, 10 / 8, 10 / 8 | 450 | 420 |
| 29 | 60 / 48, 60 / 48, 60 / 48, 60 / 48, 60 / 48, 60 / 48, 60 / 48, 60 / 48, 60 / 48, 60 / 48 | 600 | 600 |
| 31 | 250 / 200 | 250 | 300 |
| 32 | 150 / 90 | 150 | 120 |
| 33 | 400 / 320 | 400 | 500 |
| 34 | 250 / 200, 250 / 200 | 500 | 700 |
| 35 | 250 / 200 | 250 | 1000 |
| 36 | 500 / 400, 150 / 120 | 650 | 500 |
| 37 | 150 / 120, 100 / 80, 100 / 80, 100 / 80 | 450 | 400 |
| 38 | 70 / 56, 70 / 56, 70 / 56, 70 / 56, 70 / 56, 70 / 56, 70 / 56, 10 / 8, 10 / 8, 10 / 8 | 520 | 490 |
| 39 | 50 / 40, 70 / 56, 70 / 56, 50 / 40, 10 / 8, 100 / 80, 100 / 80, 100 / 80 | 550 | 510 |
| 40 | 700 / 560 | 700 | 1000 |
| 42 | 250 / 200, 250 / 200 | 500 | 500 |
| 43 | 200 / 160, 200 / 160, 200 / 160, 400 / 320 | 1000 | 800 |
| 44 | 25 / 20, 25 / 20, 25 / 20, 25 / 20, 25 / 20, 25 / 20, 25 / 20, 25 / 20, 100 / 80, 60 / 48 | 360 | 600 |
| 45 | 140 / 112, 125 / 100, 140 / 112, 125 / 100, 100 / 80, 25 / 20, 25 / 20, 25 / 20, 100 / 80, 60 / 48 | 865 | 660 |
| 46 | 280 / 224, 280 / 224, 280 / 224, 280 / 224, 280 / 224, 280 / 224, 300 / 240, 300 / 240, 300 / 240, 300 / 240 | 2880 | 300 |
| 47 | 200 / 160, 300 / 240, 300 / 240, 300 / 240, 300 / 240, 300 / 240, 300 / 240, 300 / 240, 300 / 240, 300 / 240 | 2900 | 540 |
| 48 | 30 / 24, 30 / 24, 30 / 24, 30 / 24, 30 / 24 | 150 | 300 |
| 49 | 350 / 280 | 350 | 1000 |


## Projectiles

No projectile damage file for this character.

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
| 89 | 500 |
| 90 | 500 |
| 91 | 100 |
| 92 | 150 |
| 106 | 10 |
| 131 | 50 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen 2000. Hits logged 54.

Seen so far: 13 of 48 attack rows, 0 of 0 projectiles, 4 of 16 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 100 / 80 | 5 | Square, Square [charged] |
| 1 | 0 | 60 / 48 | 5 | Square, Square [charged] |
| 1 | 1 | 60 / 48 | 5 | Triangle, Square [charged] |
| 1 | 2 | 120 / 96 | 5 | Triangle, Square [charged] |
| 2 | 0 | 240 / 192 | 15 | Square, Square [charged] |
| 3 | 0 | 320 / 256 | 200 | Triangle, Triangle [charged] |
| 12 | 0 | 250 / 200 | 80 | Triangle, Triangle [charged] |
| 13 | 0 | 150 / 120 | 100 | Triangle |
| 13 | 1 | 150 / 120 | 100 | Triangle |
| 14 | 0 | 500 / 400 | 320 | Triangle |
| 22 | 0 | 300 / 240 | 10 | Triangle |
| 24 | 1 | 400 / 320 | 25 | Circle, Circle [charged] |
| 28 | 0 | 60 / 48 | 60 | Up + Circle, Up + Circle [charged] |
| 28 | 1 | 60 / 48 | 60 | Up + Circle, Up + Circle [charged] |
| 29 | 0 | 60 / 48 | 60 | Up + Circle, Up + Circle [charged] |
| 29 | 3 | 60 / 48 | 60 | Up + Circle, Up + Circle [charged] |
| 29 | 5 | 60 / 48 | 60 | Up + Circle, Up + Circle [charged] |
| 29 | 6 | 60 / 48 | 60 | Up + Circle, Up + Circle [charged] |
| 29 | 7 | 60 / 48 | 60 | Up + Circle, Up + Circle [charged] |
| 40 | 0 | 700 / 560 | 1000 | Circle + R1, Circle + R1 [charged] |
| 47 | 0 | 200 / 160 | 0 | Triangle + R1, Triangle + R1 [charged] |

Actions with a Stamina cost that landed a hit: 88: 500, 89: 500, 90: 500, 131: 50.

## Knockdowns

Hits that knock the target down, from the attack data (2026-11-03). The number is the hit's reaction (attack slot +0x39, projectile row +0xAF); build-up is its "too many hits" number (the target goes down at 150). What each number does and the get-up are in [Knockdowns.md](../balance/Knockdowns.md).

| Row | Move | Hit | Number | What it does | Damage | Build-up |
|---|---|---|---|---|---|---|
| 1 |  | 0 | 21 | launched, then down | 60 | 5 |
| 1 |  | 1 | 12 | pierced, then down (dmg_penetration) | 60 | 5 |
| 1 |  | 2 | 5 | spun away (dmg_blow_spin) | 120 | 5 |
| 2 |  | 0 | 6 | knocked away (dmg_blow) | 240 | 15 |
| 3 |  | 0 | 6 | knocked away (dmg_blow) | 320 | 200 |
| 3 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 100 | 0 |
| 3 |  | 2 | 3 | lifted off the feet; lands and goes down unless hit again | 100 | 0 |
| 4 |  | 0 | 6 | knocked away (dmg_blow) | 300 | 0 |
| 6 |  | 0 | 12 | pierced, then down (dmg_penetration) | 350 | 5 |
| 6 |  | 1 | 6 | knocked away (dmg_blow) | 150 | 0 |
| 7 |  | 0 | 6 | knocked away (dmg_blow) | 300 | 0 |
| 13 |  | 0 | 4 | knocked down, big bounce | 150 | 100 |
| 13 |  | 1 | 5 | spun away (dmg_blow_spin) | 150 | 100 |
| 14 |  | 0 | 9 | knocked down, big bounce | 500 | 320 |
| 15 |  | 0 | 17 | big knock away (dmg_conspicuous_blow) | 350 | 280 |
| 16 |  | 0 | 6 | knocked away (dmg_blow) | 300 | 0 |
| 18 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 180 | 5 |
| 21 |  | 0 | 10 | knocked down, rolling bounce | 220 | 8 |
| 22 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 300 | 10 |
| 25 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 500 | 25 |
| 25 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 200 | 200 |
| 25 |  | 2 | 3 | lifted off the feet; lands and goes down unless hit again | 200 | 200 |
| 25 |  | 3 | 3 | lifted off the feet; lands and goes down unless hit again | 400 | 200 |
| 26 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 400 | 25 |
| 26 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 200 | 200 |
| 26 |  | 2 | 3 | lifted off the feet; lands and goes down unless hit again | 200 | 200 |
| 26 |  | 3 | 3 | lifted off the feet; lands and goes down unless hit again | 200 | 200 |
| 28 |  | 0 | 4 | knocked down, big bounce | 60 | 60 |
| 29 |  | 3 | 4 | knocked down, big bounce | 60 | 60 |
| 29 |  | 5 | 4 | knocked down, big bounce | 60 | 60 |
| 29 |  | 6 | 4 | knocked down, big bounce | 60 | 60 |
| 29 |  | 7 | 4 | knocked down, big bounce | 60 | 60 |
| 29 |  | 8 | 4 | knocked down, big bounce | 60 | 60 |
| 29 |  | 9 | 4 | knocked down, big bounce | 60 | 60 |
| 31 |  | 0 | 19 | knocked away (dmg_kizuna1_hit_blow) | 250 | 300 |
| 35 |  | 0 | 20 | big bounce (dmg_conspicuous_bound 2) | 250 | 1000 |
| 37 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 100 | 100 |
| 37 |  | 2 | 3 | lifted off the feet; lands and goes down unless hit again | 100 | 100 |
| 37 |  | 3 | 3 | lifted off the feet; lands and goes down unless hit again | 100 | 100 |
| 39 |  | 5 | 6 | knocked away (dmg_blow) | 100 | 100 |
| 39 |  | 6 | 3 | lifted off the feet; lands and goes down unless hit again | 100 | 100 |
| 39 |  | 7 | 3 | lifted off the feet; lands and goes down unless hit again | 100 | 100 |
| 40 |  | 0 | 6 | knocked away (dmg_blow) | 700 | 1000 |
| 43 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 200 | 200 |
| 43 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 200 | 200 |
| 43 |  | 2 | 3 | lifted off the feet; lands and goes down unless hit again | 200 | 200 |
| 43 |  | 3 | 3 | lifted off the feet; lands and goes down unless hit again | 400 | 200 |
| 44 |  | 3 | 4 | knocked down, big bounce | 25 | 60 |
| 44 |  | 5 | 4 | knocked down, big bounce | 25 | 60 |
| 44 |  | 6 | 4 | knocked down, big bounce | 25 | 60 |
| 44 |  | 7 | 4 | knocked down, big bounce | 25 | 60 |
| 44 |  | 8 | 4 | knocked down, big bounce | 100 | 60 |
| 44 |  | 9 | 4 | knocked down, big bounce | 60 | 60 |
| 45 |  | 5 | 4 | knocked down, big bounce | 25 | 60 |
| 45 |  | 6 | 4 | knocked down, big bounce | 25 | 60 |
| 45 |  | 7 | 4 | knocked down, big bounce | 25 | 60 |
| 45 |  | 8 | 4 | knocked down, big bounce | 100 | 60 |
| 45 |  | 9 | 4 | knocked down, big bounce | 60 | 60 |
| 46 |  | 5 | 3 | lifted off the feet; lands and goes down unless hit again | 280 | 10 |
| 46 |  | 9 | 3 | lifted off the feet; lands and goes down unless hit again | 300 | 60 |
| 47 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 200 | 0 |
| 47 |  | 9 | 3 | lifted off the feet; lands and goes down unless hit again | 300 | 60 |
| 48 |  | 3 | 4 | knocked down, big bounce | 30 | 60 |
| 48 |  | 5 | 4 | knocked down, big bounce | 30 | 60 |
| 48 |  | 6 | 4 | knocked down, big bounce | 30 | 60 |
| 48 |  | 7 | 4 | knocked down, big bounce | 30 | 60 |
| 49 |  | 0 | 6 | knocked away (dmg_blow) | 350 | 1000 |

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Kaguzuchi | Press R3 during a Victory Burst. |
| Infinity Cyclone | Left stick up + Circle |
| Blazing Soul | Circle ,  Circle (First blow functions as a counterattack) |
| Crimson Lotus Arm | R1 + Circle |
| Stamina Charge | R1 + Cross Keep holding to charge beyond the Stamina Gauge limit. |
