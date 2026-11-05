# Hiei (character 012)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `012_hiei_mp_param_attack_PS3.dat` inside `battle_character_012_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 40 / 20, 40 / 20 | 80 | 4 |
| 1 | 40 / 20, 40 / 20, 50 / 25 | 130 | 9 |
| 2 | 200 / 100 | 200 | 20 |
| 3 | 250 / 125 | 250 | 250 |
| 4 | 200 / 100 | 200 | 20 |
| 6 | 100 / 50, 100 / 50, 50 / 25, 50 / 25 | 300 | 30 |
| 7 | 100 / 50, 100 / 50 | 200 | 20 |
| 12 | 150 / 75 | 150 | 80 |
| 13 | 120 / 60, 120 / 60 | 240 | 160 |
| 14 | 400 / 200 | 400 | 250 |
| 15 | 300 / 150 | 300 | 200 |
| 18 | 120 / 60, 240 / 120 | 360 | 10 |
| 21 | 40 / 20, 40 / 20, 40 / 20 | 120 | 15 |
| 22 | 120 / 60 | 120 | 10 |
| 23 | 120 / 60 | 120 | 10 |
| 24 | 200 / 100 | 200 | 200 |
| 25 | 200 / 100, 600 / 300 | 800 | 1100 |
| 26 | 200 / 100 | 200 | 500 |
| 27 | 200 / 100 | 200 | 500 |
| 28 | 120 / 60 | 120 | 0 |
| 29 | 30 / 15, 30 / 15, 30 / 15, 30 / 15, 30 / 15, 30 / 15, 30 / 15, 30 / 15, 30 / 15, 30 / 15 | 300 | 300 |
| 30 | 200 / 100 | 200 | 200 |
| 31 | 40 / 20, 40 / 20, 40 / 20, 40 / 20, 40 / 20, 40 / 20, 40 / 20, 40 / 20 | 320 | 160 |
| 32 | 40 / 20, 40 / 20, 40 / 20, 40 / 20, 40 / 20, 40 / 20, 40 / 20, 40 / 20, 80 / 40, 80 / 40 | 480 | 160 |
| 33 | 150 / 90 | 150 | 120 |
| 34 | 250 / 125 | 250 | 300 |
| 36 | 250 / 125 | 250 | 500 |
| 37 | 150 / 75, 150 / 75 | 300 | 500 |
| 38 | 400 / 200 | 400 | 750 |
| 39 | 100 / 50, 600 / 300 | 700 | 1200 |
| 40 | 20 / 10, 30 / 15, 30 / 15, 30 / 15, 30 / 15, 30 / 15, 30 / 15, 30 / 15, 30 / 15, 30 / 15 | 290 | 500 |
| 41 | 300 / 150 | 300 | 1000 |
| 42 | 40 / 20, 40 / 20, 40 / 20, 40 / 20, 40 / 20, 40 / 20, 40 / 20, 40 / 20, 80 / 40, 80 / 40 | 480 | 160 |
| 43 | 200 / 100, 300 / 150 | 500 | 800 |
| 44 | 300 / 150, 300 / 150 | 600 | 600 |
| 45 | 320 / 160, 300 / 150 | 620 | 300 |
| 46 | 40 / 24, 40 / 24, 40 / 24, 40 / 24, 40 / 24, 40 / 24, 40 / 24, 40 / 24, 80 / 48, 80 / 48 | 480 | 160 |
| 47 | 10 / 6, 10 / 6, 10 / 6, 10 / 6, 10 / 6, 10 / 6, 10 / 6, 10 / 6, 10 / 6, 10 / 6 | 100 | 300 |
| 48 | 100 / 60 | 100 | 0 |
| 49 | 99999 / 99999 | 99999 | 0 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 0 | 200 / 150 | 10 |
| 1 | 10 / 0 | 0 |

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
| 89 | 200 |
| 90 | 500 |
| 106 | 10 |
| 131 | 50 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen at least 1124 (my hold ended before the top). Hits logged 115.

Seen so far: 18 of 40 attack rows, 0 of 2 projectiles, 5 of 14 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 40 / 20 | 2 | Square [charged], Square |
| 0 | 1 | 40 / 20 | 2 | Square, Square [charged], Triangle [charged] |
| 1 | 0 | 40 / 20 | 2 | Square, Square [charged] |
| 1 | 1 | 40 / 20 | 2 | Square, Triangle, Square [charged] |
| 1 | 2 | 50 / 25 | 5 | Square, Triangle, Square [charged] |
| 2 | 0 | 200 / 100 | 20 | Square, Square [charged] |
| 3 | 0 | 250 / 125 | 250 | Triangle, Triangle [charged] |
| 6 | 0 | 100 / 50 | 10 | Square + R1, Square + R1 [charged] |
| 6 | 1 | 100 / 50 | 10 | Square + R1, Square + R1 [charged] |
| 12 | 0 | 150 / 75 | 80 | Triangle, Triangle [charged] |
| 13 | 0 | 120 / 60 | 80 | Triangle |
| 13 | 1 | 120 / 60 | 80 | Triangle |
| 14 | 0 | 400 / 200 | 250 | Triangle |
| 21 | 0 | 40 / 20 | 5 | Triangle |
| 21 | 1 | 40 / 20 | 5 | Triangle |
| 21 | 2 | 40 / 20 | 5 | Triangle |
| 23 | 0 | 120 / 60 | 10 | Square [charged] |
| 24 | 0 | 200 / 100 | 200 | Square [charged] |
| 25 | 0 | 200 / 100 | 500 | Circle, Circle [charged] |
| 29 | 0 | 30 / 15 | 30 | Circle + R1, Circle + R1 [charged] |
| 29 | 1 | 30 / 15 | 30 | Circle + R1, Circle + R1 [charged] |
| 29 | 2 | 30 / 15 | 30 | Circle + R1, Circle + R1 [charged] |
| 29 | 3 | 30 / 15 | 30 | Circle + R1, Circle + R1 [charged] |
| 29 | 4 | 30 / 15 | 30 | Circle + R1, Circle + R1 [charged] |
| 29 | 5 | 30 / 15 | 30 | Circle + R1, Circle + R1 [charged] |
| 30 | 0 | 200 / 100 | 200 | Circle + R1, Circle + R1 [charged] |
| 31 | 0 | 40 / 20 | 20 | Up + Circle |
| 31 | 1 | 40 / 20 | 20 | Up + Circle |
| 31 | 2 | 40 / 20 | 20 | Up + Circle |
| 31 | 3 | 40 / 20 | 20 | Up + Circle |
| 31 | 4 | 40 / 20 | 20 | Up + Circle |
| 31 | 5 | 40 / 20 | 20 | Up + Circle |
| 31 | 6 | 40 / 20 | 20 | Up + Circle |
| 31 | 7 | 40 / 20 | 20 | Up + Circle |
| 32 | 0 | 40 / 20 | 20 | Up + Circle |
| 32 | 1 | 40 / 20 | 20 | Up + Circle |
| 32 | 2 | 40 / 20 | 20 | Up + Circle |
| 32 | 3 | 40 / 20 | 20 | Up + Circle |
| 32 | 4 | 40 / 20 | 20 | Up + Circle |
| 32 | 5 | 40 / 20 | 20 | Up + Circle |
| 32 | 6 | 40 / 20 | 20 | Up + Circle |
| 32 | 7 | 40 / 20 | 20 | Up + Circle |
| 42 | 0 | 40 / 20 | 20 | Square |
| 42 | 1 | 40 / 20 | 20 | Square |
| 42 | 2 | 40 / 20 | 20 | Square |
| 42 | 3 | 40 / 20 | 20 | Square |
| 42 | 4 | 40 / 20 | 20 | Square |
| 42 | 5 | 40 / 20 | 20 | Square |
| 42 | 6 | 40 / 20 | 20 | Square |
| 42 | 7 | 40 / 20 | 20 | Square |
| 45 | 0 | 320 / 160 | 0 | Triangle + R1, Triangle + R1 [charged] |

Actions with a Stamina cost that landed a hit: 29: 50, 88: 100, 89: 200, 90: 500, 131: 50.

## Knockdowns

Hits that knock the target down, from the attack data (2026-11-03). The number is the hit's reaction (attack slot +0x39, projectile row +0xAF); build-up is its "too many hits" number (the target goes down at 150). What each number does and the get-up are in [Knockdowns.md](../balance/Knockdowns.md).

| Row | Move | Hit | Number | What it does | Damage | Build-up |
|---|---|---|---|---|---|---|
| 0 |  | 1 | 4 | knocked down, big bounce | 40 | 2 |
| 2 |  | 0 | 5 | spun away (dmg_blow_spin) | 200 | 20 |
| 3 |  | 0 | 9 | knocked down, big bounce | 250 | 250 |
| 4 |  | 0 | 10 | knocked down, rolling bounce | 200 | 20 |
| 12 |  | 0 | 21 | launched, then down | 150 | 80 |
| 13 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 120 | 80 |
| 13 |  | 1 | 5 | spun away (dmg_blow_spin) | 120 | 80 |
| 14 |  | 0 | 6 | knocked away (dmg_blow) | 400 | 250 |
| 15 |  | 0 | 6 | knocked away (dmg_blow) | 300 | 200 |
| 18 |  | 0 | 5 | spun away (dmg_blow_spin) | 120 | 10 |
| 18 |  | 1 | 5 | spun away (dmg_blow_spin) | 240 | 0 |
| 21 |  | 2 | 4 | knocked down, big bounce | 40 | 5 |
| 22 |  | 0 | 9 | knocked down, big bounce | 120 | 10 |
| 25 |  | 0 | 5 | spun away (dmg_blow_spin) | 200 | 500 |
| 29 |  | 0 | 4 | knocked down, big bounce | 30 | 30 |
| 30 |  | 0 | 17 | big knock away (dmg_conspicuous_blow) | 200 | 200 |
| 31 |  | 0 | 4 | knocked down, big bounce | 40 | 20 |
| 32 |  | 7 | 5 | spun away (dmg_blow_spin) | 40 | 20 |
| 34 |  | 0 | 19 | knocked away (dmg_kizuna1_hit_blow) | 250 | 300 |
| 36 |  | 0 | 20 | big bounce (dmg_conspicuous_bound 2) | 250 | 500 |
| 38 |  | 0 | 20 | big bounce (dmg_conspicuous_bound 2) | 400 | 750 |
| 39 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 600 | 600 |
| 41 |  | 0 | 6 | knocked away (dmg_blow) | 300 | 1000 |
| 42 |  | 0 | 4 | knocked down, big bounce | 40 | 20 |
| 42 |  | 7 | 5 | spun away (dmg_blow_spin) | 40 | 20 |
| 44 |  | 0 | 4 | knocked down, big bounce | 300 | 300 |
| 44 |  | 1 | 5 | spun away (dmg_blow_spin) | 300 | 300 |
| 45 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 320 | 0 |
| 46 |  | 7 | 3 | lifted off the feet; lands and goes down unless hit again | 40 | 20 |
| 48 |  | 0 | 17 | big knock away (dmg_conspicuous_blow) | 100 | 0 |

| Projectile row | Number | What it does | Damage |
|---|---|---|---|
| 0 | 3 | lifted off the feet; lands and goes down unless hit again | 200 |

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Dragon of the Darkness Flame | Press R3 during a Victory Burst. |
| Dragon Absorption | Press R3 while summoning the Black Dragon. |
| Sword of the Darkness Flame | Circle (Sets Hiei's katana ablaze for a short period) |
| Lightning Slash | Left stick up + Circle |
| Fist of the Mortal Flame | R1 + Circle |
| Jumping Attack 2 | Press Triangle ,  Triangle while jumping. |
| Stamina Charge | R1 + Cross (Opens Hiei's Jagan Eye) Keep holding to continue charging slowly past the Stamina Gauge limit. |
