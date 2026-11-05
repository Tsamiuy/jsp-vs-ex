# Madara (character 006)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `006_madara_mp_param_attack_PS3.d` inside `battle_character_006_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 120 / 6 | 120 | 5 |
| 1 | 80 / 4, 120 / 6 | 200 | 10 |
| 2 | 180 / 9 | 180 | 5 |
| 3 | 300 / 15, 300 / 15, 100 / 5 | 700 | 330 |
| 4 | 240 / 12 | 240 | 10 |
| 5 | 300 / 15 | 300 | 100 |
| 6 | 400 / 20, 300 / 15 | 700 | 210 |
| 12 | 220 / 11 | 220 | 300 |
| 13 | 200 / 10, 150 / 7.5 | 350 | 500 |
| 14 | 200 / 10 | 200 | 300 |
| 15 | 100 / 5, 200 / 10, 110 / 5.5 | 410 | 1100 |
| 16 | 400 / 20 | 400 | 1000 |
| 18 | 75 / 3.8, 75 / 3.8 | 150 | 10 |
| 21 | 350 / 17.5, 250 / 12.5 | 600 | 110 |
| 22 | 260 / 13, 40 / 2 | 300 | 20 |
| 23 | 300 / 15 | 300 | 20 |
| 24 | 250 / 12.5 | 250 | 900 |
| 25 | 150 / 7.5 | 150 | 120 |
| 26 | 250 / 12.5 | 250 | 200 |
| 27 | 120 / 6, 120 / 6 | 240 | 400 |
| 28 | 80 / 4, 80 / 4, 80 / 4 | 240 | 940 |
| 29 | 240 / 12 | 240 | 20 |
| 30 | 25 / 1.2, 25 / 1.2, 25 / 1.2, 25 / 1.2, 300 / 15, 300 / 15, 300 / 15 | 1000 | 14000 |
| 31 | 350 / 17.5, 300 / 15, 300 / 15, 300 / 15, 300 / 15, 300 / 15, 300 / 15 | 2150 | 14000 |
| 32 | 60 / 3, 10 / 0.5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 10 / 0.5 | 120 | 310 |
| 33 | 60 / 3, 10 / 0.5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 280 / 14, 10 / 0.5 | 410 | 310 |
| 34 | 800 / 40, 10 / 0.5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 280 / 14, 10 / 0.5 | 1140 | 300 |
| 35 | 10 / 0.5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 280 / 14, 10 / 0.5 | 340 | 250 |
| 36 | 50 / 2.5, 50 / 2.5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 280 / 14, 10 / 0.5 | 430 | 300 |
| 37 | 50 / 2.5, 200 / 10, 500 / 25, 10 / 0.5, 5 / 0.2, 5 / 0.2, 10 / 0.5, 280 / 14, 10 / 0.5 | 1070 | 350 |
| 38 | 100 / 5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 10 / 0.5 | 160 | 310 |
| 39 | 100 / 5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 280 / 14, 10 / 0.5 | 450 | 310 |
| 40 | 80 / 4, 80 / 4, 80 / 4 | 240 | 940 |
| 41 | 50 / 2.5, 50 / 2.5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 280 / 14, 10 / 0.5 | 440 | 270 |
| 42 | 10 / 0.5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 10 / 0.5, 280 / 14, 10 / 0.5 | 340 | 250 |
| 43 | 400 / 20, 200 / 10, 500 / 25, 10 / 0.5, 5 / 0.2, 10 / 0.5, 10 / 0.5, 280 / 14, 10 / 0.5 | 1425 | 350 |


## Projectiles

No projectile damage file for this character.

## Stamina costs

From `param_action`: +0x0C is the cost, on 48-byte rows in groups of 4. Action = row / 4. Which skill uses each action is only known after a logged session. A negative number has not been tested; it may give Stamina back.

| Action | Stamina |
|---|---|
| 11 | 50 |
| 16 | 80 |
| 26 | 100 |
| 29 | 200 |
| 30 | 50 |
| 31 | 50 |
| 32 | 50 |
| 36 | 100 |
| 37 | 100 |
| 38 | 100 |
| 85 | 50 |
| 86 | 50 |
| 87 | 50 |
| 89 | 300 |
| 90 | 500 |
| 91 | 100 |
| 106 | 10 |
| 131 | 200 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen 2000. Hits logged 57.

Seen so far: 11 of 36 attack rows, 0 of 0 projectiles, 3 of 18 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 120 / 6 | 5 | Square, Square [charged] |
| 1 | 0 | 80 / 4 | 5 | Square, Square [charged] |
| 1 | 1 | 120 / 6 | 5 | Triangle, Square [charged] |
| 2 | 0 | 180 / 9 | 5 | Square [charged] |
| 12 | 0 | 220 / 11 | 300 | Triangle, Triangle [charged] |
| 13 | 0 | 200 / 10 | 300 | Triangle, Triangle [charged] |
| 14 | 0 | 200 / 10 | 300 | Triangle |
| 21 | 0 | 350 / 17.5 | 100 | Square |
| 30 | 1 | 25 / 1.3 | 2000 | Triangle + R1, Triangle + R1 [charged] |
| 30 | 2 | 25 / 1.3 | 2000 | Triangle + R1, Triangle + R1 [charged] |
| 30 | 3 | 25 / 1.3 | 2000 | Triangle + R1, Triangle + R1 [charged] |
| 31 | 0 | 350 / 17.5 | 2000 | Triangle + R1, Triangle + R1 [charged] |
| 32 | 0 | 60 / 3 | 10 | Circle, Circle [charged] |
| 33 | 0 | 60 / 3 | 10 | Circle, Circle [charged] |

Actions with a Stamina cost that landed a hit: 36: 100, 37: 100, 131: 200.

## Knockdowns

Hits that knock the target down, from the attack data (2026-11-03). The number is the hit's reaction (attack slot +0x39, projectile row +0xAF); build-up is its "too many hits" number (the target goes down at 150). What each number does and the get-up are in [Knockdowns.md](../balance/Knockdowns.md). "Hit" is the attack slot, counting slots that deal no damage, so it can be higher than the position in this page's own damage list. Build-up is the "Gauge" value in the tables above; it drains by itself and applies to hits only, not to projectiles. Rows with an empty Move are not tied to an input yet.

| Row | Move | Hit | Number | What it does | Damage | Build-up |
|---|---|---|---|---|---|---|
| 2 |  | 0 | 12 | pierced, then down (dmg_penetration) | 180 | 5 |
| 3 |  | 0 | 5 | spun away (dmg_blow_spin) | 300 | 20 |
| 3 |  | 1 | 5 | spun away (dmg_blow_spin) | 300 | 300 |
| 3 |  | 2 | 10 | knocked down, rolling bounce | 100 | 10 |
| 5 |  | 0 | 6 | knocked away (dmg_blow) | 300 | 100 |
| 6 |  | 0 | 5 | spun away (dmg_blow_spin) | 400 | 200 |
| 6 |  | 1 | 6 | knocked away (dmg_blow) | 300 | 10 |
| 12 |  | 0 | 6 | knocked away (dmg_blow) | 220 | 300 |
| 13 |  | 0 | 5 | spun away (dmg_blow_spin) | 200 | 300 |
| 13 |  | 1 | 5 | spun away (dmg_blow_spin) | 150 | 200 |
| 14 |  | 0 | 12 | pierced, then down (dmg_penetration) | 200 | 300 |
| 15 |  | 0 | 4 | knocked down, big bounce | 100 | 0 |
| 15 |  | 1 | 17 | big knock away (dmg_conspicuous_blow) | 200 | 200 |
| 15 |  | 2 | 9 | knocked down, big bounce | 110 | 900 |
| 16 |  | 0 | 10 | knocked down, rolling bounce | 400 | 1000 |
| 18 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 75 | 5 |
| 21 |  | 0 | 6 | knocked away (dmg_blow) | 350 | 100 |
| 22 |  | 0 | 10 | knocked down, rolling bounce | 260 | 20 |
| 22 |  | 1 | 18 | big bounce (dmg_conspicuous_bound) | 40 | 0 |
| 23 |  | 0 | 10 | knocked down, rolling bounce | 300 | 20 |
| 24 |  | 0 | 19 | knocked away (dmg_kizuna1_hit_blow) | 250 | 900 |
| 26 |  | 0 | 20 | big bounce (dmg_conspicuous_bound 2) | 250 | 200 |
| 27 |  | 1 | 5 | spun away (dmg_blow_spin) | 120 | 200 |
| 28 |  | 2 | 9 | knocked down, big bounce | 80 | 900 |
| 29 |  | 0 | 6 | knocked away (dmg_blow) | 240 | 20 |
| 31 |  | 0 | 4 | knocked down, big bounce | 350 | 2000 |
| 32 |  | 6 | 3 | lifted off the feet; lands and goes down unless hit again | 10 | 50 |
| 33 |  | 6 | 3 | lifted off the feet; lands and goes down unless hit again | 10 | 50 |
| 34 |  | 1 | 5 | spun away (dmg_blow_spin) | 800 | 50 |
| 34 |  | 6 | 3 | lifted off the feet; lands and goes down unless hit again | 10 | 50 |
| 35 |  | 6 | 3 | lifted off the feet; lands and goes down unless hit again | 10 | 50 |
| 36 |  | 0 | 4 | knocked down, big bounce | 50 | 50 |
| 36 |  | 1 | 11 | shocked, then down (dmg_shock) | 50 | 50 |
| 36 |  | 6 | 3 | lifted off the feet; lands and goes down unless hit again | 10 | 50 |
| 37 |  | 0 | 11 | shocked, then down (dmg_shock) | 50 | 50 |
| 37 |  | 1 | 11 | shocked, then down (dmg_shock) | 200 | 50 |
| 37 |  | 2 | 11 | shocked, then down (dmg_shock) | 500 | 50 |
| 37 |  | 4 | 11 | shocked, then down (dmg_shock) | 5 | 50 |
| 37 |  | 5 | 11 | shocked, then down (dmg_shock) | 5 | 50 |
| 37 |  | 6 | 5 | spun away (dmg_blow_spin) | 10 | 50 |
| 38 |  | 6 | 3 | lifted off the feet; lands and goes down unless hit again | 10 | 50 |
| 39 |  | 6 | 3 | lifted off the feet; lands and goes down unless hit again | 10 | 50 |
| 40 |  | 2 | 9 | knocked down, big bounce | 80 | 900 |
| 41 |  | 6 | 3 | lifted off the feet; lands and goes down unless hit again | 10 | 50 |
| 42 |  | 6 | 3 | lifted off the feet; lands and goes down unless hit again | 10 | 50 |
| 43 |  | 0 | 5 | spun away (dmg_blow_spin) | 400 | 50 |
| 43 |  | 1 | 11 | shocked, then down (dmg_shock) | 200 | 50 |
| 43 |  | 2 | 11 | shocked, then down (dmg_shock) | 500 | 50 |
| 43 |  | 4 | 11 | shocked, then down (dmg_shock) | 5 | 50 |
| 43 |  | 5 | 11 | shocked, then down (dmg_shock) | 10 | 50 |
| 43 |  | 6 | 5 | spun away (dmg_blow_spin) | 10 | 50 |

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Shattered Heaven | Press R3 during a Victory Burst. |
| Uchiha Reflection | R1 + Circle (Activates when the opponent attacks) |
| Flame Style: Majestic Destroyer Flame | Circle (Hold to charge / Also possible while jumping) |
| Wood Style: Deep Forest Emergence | Left stick up + Circle |
| Stamina Charge | R1 + Cross Remaining stamina doubles when you start charging. |
