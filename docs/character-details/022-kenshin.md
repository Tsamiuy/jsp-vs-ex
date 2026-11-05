# Kenshin (character 022)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `022_kenshin_mp_param_attack_PS3.` inside `battle_character_022_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 80 / 32 | 80 | 15 |
| 1 | 80 / 32, 80 / 32, 100 / 40 | 260 | 20 |
| 2 | 40 / 16, 40 / 16, 40 / 16, 40 / 16 | 160 | 60 |
| 3 | 250 / 100, 100 / 40, 100 / 40 | 450 | 400 |
| 4 | 300 / 120 | 300 | 0 |
| 5 | 300 / 120 | 300 | 0 |
| 6 | 100 / 40, 50 / 20, 25 / 10, 25 / 10, 25 / 10, 25 / 10, 25 / 10 | 275 | 25 |
| 7 | 300 / 120 | 300 | 0 |
| 8 | 300 / 120 | 300 | 0 |
| 9 | 300 / 120 | 300 | 0 |
| 10 | 300 / 120 | 300 | 0 |
| 11 | 300 / 120 | 300 | 0 |
| 12 | 160 / 64 | 160 | 150 |
| 13 | 240 / 96, 150 / 60 | 390 | 250 |
| 14 | 320 / 128 | 320 | 400 |
| 15 | 240 / 96 | 240 | 400 |
| 16 | 100 / 40, 100 / 40 | 200 | 300 |
| 17 | 300 / 120 | 300 | 0 |
| 18 | 100 / 40 | 100 | 50 |
| 19 | 300 / 120 | 300 | 0 |
| 20 | 300 / 120 | 300 | 0 |
| 21 | 80 / 32 | 80 | 20 |
| 22 | 40 / 16, 40 / 16 | 80 | 20 |
| 23 | 80 / 32 | 80 | 20 |
| 24 | 300 / 120 | 300 | 300 |
| 25 | 250 / 100 | 250 | 400 |
| 26 | 100 / 40 | 100 | 400 |
| 27 | 50 / 20, 100 / 40, 100 / 40, 100 / 40 | 350 | 50 |
| 28 | 40 / 16, 40 / 16, 40 / 16, 40 / 16, 40 / 16 | 200 | 200 |
| 31 | 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 400 | 160 |
| 32 | 400 / 160, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 100 / 40 | 900 | 40 |
| 33 | 25 / 10, 25 / 10, 25 / 10, 25 / 10, 25 / 10, 25 / 10, 25 / 10, 25 / 10 | 200 | 160 |
| 34 | 200 / 80, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 100 / 40 | 700 | 40 |
| 36 | 1000 / 400 | 1000 | 400 |
| 38 | 150 / 90 | 150 | 120 |
| 39 | 250 / 150 | 250 | 500 |
| 40 | 250 / 100, 150 / 60 | 400 | 350 |
| 41 | 40 / 16, 40 / 16, 100 / 40 | 180 | 16 |
| 42 | 40 / 16, 40 / 16, 40 / 16, 60 / 24 | 180 | 40 |
| 43 | 30 / 12, 30 / 12, 30 / 12, 30 / 12, 30 / 12, 30 / 12, 30 / 12 | 210 | 0 |
| 44 | 90 / 36 | 90 | 0 |
| 45 | 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 400 | 160 |
| 46 | 100 / 40, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 100 / 40 | 600 | 40 |
| 47 | 100 / 40 | 100 | 100 |
| 48 | 80 / 32, 80 / 32, 80 / 32, 80 / 32 | 320 | 500 |
| 49 | 1000 / 400 | 1000 | 9999 |
| 50 | 700 / 280 | 700 | 200 |
| 53 | 250 / 100 | 250 | 400 |
| 54 | 100 / 40 | 100 | 400 |
| 55 | 50 / 20, 100 / 40, 100 / 40, 100 / 40 | 350 | 50 |
| 56 | 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 200 | 200 |
| 58 | 750 / 300 | 750 | 400 |
| 59 | 250 / 100, 100 / 40, 100 / 40 | 450 | 400 |


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
| 88 | 200 |
| 89 | 500 |
| 90 | 500 |
| 91 | 100 |
| 93 | 100 |
| 106 | 10 |
| 131 | 50 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen 2000. Hits logged 48.

Seen so far: 11 of 53 attack rows, 0 of 0 projectiles, 4 of 16 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 80 / 32 | 15 | Square, Square [charged] |
| 1 | 0 | 80 / 32 | 10 | Square |
| 1 | 1 | 80 / 32 | 10 | Square |
| 2 | 3 | 40 / 16 | 15 | Square [charged] |
| 3 | 0 | 250 / 100 | 400 | Triangle, Triangle [charged] |
| 6 | 1 | 50 / 20 | 0 | Square + R1, Square + R1 [charged] |
| 12 | 0 | 160 / 64 | 150 | Triangle, Triangle [charged] |
| 13 | 0 | 240 / 96 | 250 | Triangle |
| 25 | 0 | 250 / 100 | 400 | Circle, Circle [charged] |
| 31 | 0 | 50 / 20 | 20 | Up + Circle, Up + Circle [charged] |
| 31 | 1 | 50 / 20 | 20 | Up + Circle, Up + Circle [charged] |
| 31 | 2 | 50 / 20 | 20 | Up + Circle, Up + Circle [charged] |
| 31 | 3 | 50 / 20 | 20 | Up + Circle, Up + Circle [charged] |
| 31 | 4 | 50 / 20 | 20 | Up + Circle, Up + Circle [charged] |
| 31 | 5 | 50 / 20 | 20 | Up + Circle, Up + Circle [charged] |
| 31 | 6 | 50 / 20 | 20 | Up + Circle, Up + Circle [charged] |
| 31 | 7 | 50 / 20 | 20 | Up + Circle, Up + Circle [charged] |
| 32 | 0 | 400 / 160 | 40 | Up + Circle, Up + Circle [charged] |
| 36 | 0 | 1000 / 400 | 400 | Circle + R1, Circle + R1 [charged] |

Actions with a Stamina cost that landed a hit: 29: 50, 88: 200, 89: 500, 90: 500.

## Knockdowns

Hits that knock the target down, from the attack data (2026-11-03). The number is the hit's reaction (attack slot +0x39, projectile row +0xAF); build-up is its "too many hits" number (the target goes down at 150). What each number does and the get-up are in [Knockdowns.md](../balance/Knockdowns.md).

| Row | Move | Hit | Number | What it does | Damage | Build-up |
|---|---|---|---|---|---|---|
| 1 |  | 1 | 21 | launched, then down | 80 | 10 |
| 2 |  | 3 | 10 | knocked down, rolling bounce | 40 | 15 |
| 3 |  | 0 | 12 | pierced, then down (dmg_penetration) | 250 | 400 |
| 3 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 100 | 0 |
| 3 |  | 2 | 3 | lifted off the feet; lands and goes down unless hit again | 100 | 0 |
| 4 |  | 0 | 6 | knocked away (dmg_blow) | 300 | 0 |
| 7 |  | 0 | 6 | knocked away (dmg_blow) | 300 | 0 |
| 14 |  | 0 | 10 | knocked down, rolling bounce | 320 | 400 |
| 15 |  | 0 | 12 | pierced, then down (dmg_penetration) | 240 | 400 |
| 16 |  | 0 | 21 | launched, then down | 100 | 300 |
| 16 |  | 1 | 21 | launched, then down | 100 | 0 |
| 18 |  | 0 | 12 | pierced, then down (dmg_penetration) | 100 | 50 |
| 22 |  | 0 | 21 | launched, then down | 40 | 20 |
| 22 |  | 1 | 21 | launched, then down | 40 | 0 |
| 24 |  | 0 | 19 | knocked away (dmg_kizuna1_hit_blow) | 300 | 300 |
| 27 |  | 3 | 10 | knocked down, rolling bounce | 100 | 0 |
| 28 |  | 4 | 10 | knocked down, rolling bounce | 40 | 40 |
| 32 |  | 0 | 6 | knocked away (dmg_blow) | 400 | 40 |
| 32 |  | 8 | 17 | big knock away (dmg_conspicuous_blow) | 50 | 0 |
| 33 |  | 0 | 4 | knocked down, big bounce | 25 | 20 |
| 34 |  | 0 | 6 | knocked away (dmg_blow) | 200 | 40 |
| 34 |  | 8 | 17 | big knock away (dmg_conspicuous_blow) | 50 | 0 |
| 36 |  | 0 | 4 | knocked down, big bounce | 1000 | 400 |
| 39 |  | 0 | 19 | knocked away (dmg_kizuna1_hit_blow) | 250 | 500 |
| 40 |  | 0 | 20 | big bounce (dmg_conspicuous_bound 2) | 250 | 350 |
| 42 |  | 3 | 10 | knocked down, rolling bounce | 60 | 10 |
| 44 |  | 0 | 6 | knocked away (dmg_blow) | 90 | 0 |
| 45 |  | 0 | 4 | knocked down, big bounce | 50 | 20 |
| 46 |  | 0 | 6 | knocked away (dmg_blow) | 100 | 40 |
| 46 |  | 8 | 17 | big knock away (dmg_conspicuous_blow) | 50 | 0 |
| 47 |  | 0 | 17 | big knock away (dmg_conspicuous_blow) | 100 | 100 |
| 48 |  | 3 | 3 | lifted off the feet; lands and goes down unless hit again | 80 | 125 |
| 49 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 1000 | 9999 |
| 50 |  | 0 | 4 | knocked down, big bounce | 700 | 200 |
| 55 |  | 3 | 10 | knocked down, rolling bounce | 100 | 0 |
| 58 |  | 0 | 4 | knocked down, big bounce | 750 | 400 |
| 59 |  | 0 | 12 | pierced, then down (dmg_penetration) | 250 | 400 |
| 59 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 100 | 0 |
| 59 |  | 2 | 3 | lifted off the feet; lands and goes down unless hit again | 100 | 0 |

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Two-Staged Flash of the Heavenly Flying Dragon | Press R3 during a Victory Burst. |
| Nine-Headed Dragon Flash | Left stick up + Circle |
| Dragon Coiling Flash: Wintry Wind | Circle |
| Dragon Coiling Flash: Whirlwind | Press Circle after Dragon Coiling Flash: Wintry Wind hits target. |
| Dragon Coiling Flash: Storm | Press Circle after Dragon Coiling Flash: Whirlwind hits target. |
| Flash of the Heavenly Flying Dragon | R1 + Circle (Activates when an enemy approaches) |
| Dragon Rising Flash | Press Square after a jumping attack. |
| Stamina Charge | R1 + Cross Keep holding to charge beyond the Stamina Gauge limit. |
