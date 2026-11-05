# Goku (character 007)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `007_goku_mp_param_attack_PS3.dat` inside `battle_character_007_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 120 / 36 | 120 | 4 |
| 1 | 100 / 30, 100 / 30 | 200 | 7 |
| 2 | 200 / 60 | 200 | 10 |
| 3 | 150 / 45, 150 / 45, 200 / 60, 200 / 60 | 700 | 400 |
| 4 | 200 / 60, 200 / 60 | 400 | 600 |
| 5 | 250 / 75, 250 / 75 | 500 | 800 |
| 6 | 125 / 37.5 | 125 | 10 |
| 7 | 125 / 37.5 | 125 | 20 |
| 8 | 150 / 45 | 150 | 30 |
| 9 | 100 / 30, 100 / 30 | 200 | 40 |
| 12 | 220 / 66 | 220 | 80 |
| 13 | 550 / 165 | 550 | 350 |
| 14 | 1100 / 330 | 1100 | 1000 |
| 15 | 216 / 64.8 | 216 | 600 |
| 16 | 250 / 75 | 250 | 750 |
| 17 | 300 / 90 | 300 | 900 |
| 18 | 180 / 54 | 180 | 10 |
| 19 | 180 / 54 | 180 | 10 |
| 20 | 180 / 54 | 180 | 10 |
| 21 | 100 / 30 | 100 | 10 |
| 22 | 100 / 30 | 100 | 10 |
| 23 | 180 / 54, 180 / 54 | 360 | 20 |
| 25 | 100 / 30 | 100 | 0 |
| 26 | 100 / 30 | 100 | 0 |
| 27 | 180 / 54 | 180 | 0 |
| 28 | 180 / 54 | 180 | 0 |
| 29 | 180 / 54 | 180 | 0 |
| 30 | 180 / 54 | 180 | 0 |
| 31 | 180 / 54 | 180 | 0 |
| 32 | 180 / 54 | 180 | 0 |
| 34 | 250 / 50 | 250 | 0 |
| 35 | 250 / 75 | 250 | 0 |
| 36 | 50 / 15 | 50 | 0 |
| 37 | 50 / 15 | 50 | 0 |
| 38 | 100 / 30 | 100 | 10 |
| 39 | 120 / 36 | 120 | 600 |
| 40 | 200 / 0 | 200 | 0 |
| 41 | 100 / 0 | 100 | 0 |
| 42 | 100 / 0 | 100 | 0 |
| 43 | 180 / 0 | 180 | 0 |
| 44 | 20 / 0, 20 / 0, 20 / 0, 20 / 0, 20 / 0, 20 / 0 | 120 | 0 |
| 45 | 20 / 0, 20 / 0, 20 / 0, 20 / 0, 20 / 0, 20 / 0 | 120 | 0 |
| 46 | 20 / 0, 20 / 0, 20 / 0, 20 / 0, 20 / 0, 20 / 0 | 120 | 0 |
| 47 | 40 / 0, 40 / 0, 40 / 0, 40 / 0, 40 / 0, 40 / 0 | 240 | 0 |
| 48 | 100 / 0, 100 / 0, 100 / 0, 100 / 0, 100 / 0 | 500 | 0 |
| 49 | 100 / 0, 100 / 0, 100 / 0, 100 / 0 | 400 | 0 |
| 50 | 400 / 0 | 400 | 0 |
| 52 | 400 / 800, 600 / 1200 | 1000 | 0 |
| 54 | 180 / 54 | 180 | 0 |
| 55 | 180 / 54 | 180 | 0 |
| 56 | 180 / 54 | 180 | 0 |
| 57 | 180 / 54 | 180 | 800 |
| 58 | 50 / 15 | 50 | 0 |
| 59 | 50 / 15 | 50 | 0 |
| 60 | 10 / 3 | 10 | 50 |
| 61 | 10 / 3 | 10 | 50 |
| 62 | 40 / 12 | 40 | 50 |
| 63 | 40 / 12 | 40 | 50 |
| 64 | 40 / 12 | 40 | 50 |
| 65 | 40 / 12 | 40 | 50 |
| 66 | 200 / 60 | 200 | 0 |
| 68 | 350 / 105 | 350 | 0 |
| 69 | 5 / 0, 5 / 0, 5 / 0, 5 / 0, 20 / 0, 20 / 0 | 60 | 0 |
| 70 | 350 / 0 | 350 | 0 |
| 71 | 240 / 192 | 240 | 20 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 5 | 600 / 60 | 10 |
| 10 | 120 / 12 | 10 |
| 11 | 120 / 12 | 10 |
| 20 | 50 / 0 | 0 |
| 30 | 300 / 30 | 10 |
| 31 | 10 / 7.5 | 10 |
| 35 | 100 / 75 | 400 |
| 36 | 100 / 75 | 400 |
| 37 | 100 / 75 | 400 |
| 38 | 100 / 75 | 400 |
| 40 | 30 / 22.5 | 400 |
| 41 | 30 / 22.5 | 400 |
| 42 | 30 / 22.5 | 400 |
| 43 | 30 / 22.5 | 400 |
| 44 | 30 / 22.5 | 400 |
| 45 | 45 / 33.8 | 400 |
| 46 | 45 / 33.8 | 400 |
| 47 | 100 / 75 | 400 |
| 50 | 40 / 4 | 290 |
| 51 | 55 / 41.2 | 100 |
| 52 | 70 / 52.5 | 100 |
| 53 | 85 / 63.8 | 100 |
| 54 | 100 / 75 | 100 |
| 55 | 400 / 40 | 100 |
| 56 | 1 / 0.8 | 100 |
| 57 | 1 / 0.8 | 100 |
| 58 | 1 / 0.8 | 100 |
| 59 | 1 / 0.8 | 100 |
| 60 | 80 / 8 | 290 |
| 61 | 110 / 82.5 | 100 |
| 62 | 140 / 105 | 100 |
| 63 | 170 / 127.5 | 100 |
| 64 | 200 / 150 | 100 |
| 65 | 500 / 50 | 100 |
| 66 | 1 / 0.8 | 100 |
| 67 | 1 / 0.8 | 100 |
| 68 | 1 / 0.8 | 100 |
| 69 | 1 / 0.8 | 100 |
| 85 | 1200 / 120 | 10 |
| 86 | 10 / 7.5 | 10 |
| 90 | 10 / 7.5 | 450 |
| 91 | 10 / 7.5 | 450 |
| 92 | 10 / 7.5 | 450 |
| 93 | 10 / 7.5 | 450 |
| 95 | 10 / 7.5 | 450 |
| 96 | 10 / 7.5 | 450 |
| 97 | 10 / 7.5 | 450 |
| 98 | 10 / 7.5 | 450 |
| 100 | 30 / 22.5 | 450 |
| 101 | 30 / 22.5 | 450 |
| 102 | 30 / 22.5 | 450 |
| 103 | 30 / 22.5 | 450 |
| 104 | 30 / 22.5 | 10 |

## Stamina costs

From `param_action`: +0x0C is the cost, on 48-byte rows in groups of 4. Action = row / 4. Which skill uses each action is only known after a logged session. A negative number has not been tested; it may give Stamina back.

| Action | Stamina |
|---|---|
| 11 | 50 |
| 16 | 80 |
| 20 | 200 |
| 21 | -150 |
| 29 | 50 |
| 30 | 50 |
| 31 | 50 |
| 32 | 100 |
| 33 | 100 |
| 34 | 100 |
| 85 | 50 |
| 86 | 50 |
| 87 | 50 |
| 88 | 700 |
| 89 | 400 |
| 90 | 200 |
| 91 | 100 |
| 104 | 30 |
| 105 | 50 |
| 106 | 200 |
| 107 | 50 |
| 131 | 50 |

## Logged session (2026-10-30)

Health 3500. Highest Stamina seen 2000. Hits logged 54.

Seen so far: 15 of 65 attack rows, 6 of 53 projectiles, 4 of 21 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 120 / 36 | 4 | Square, Square [charged] |
| 1 | 0 | 100 / 30 | 3 | Square |
| 1 | 1 | 100 / 30 | 4 | Square |
| 3 | 0 | 150 / 45 | 200 | Triangle |
| 3 | 1 | 150 / 45 | 200 | Triangle |
| 6 | 0 | 125 / 37.5 | 10 | Square + R1 [charged] |
| 12 | 0 | 220 / 66 | 80 | Triangle, Triangle [charged] |
| 23 | 0 | 180 / 54 | 10 | Square |
| 41 | 0 | 100 / 0 | 0 | Up + Circle [charged] |
| 42 | 0 | 100 / 0 | 0 | Up + Circle [charged] |
| 44 | 0 | 20 / 0 | 0 | Up + Circle [charged] |
| 44 | 1 | 20 / 0 | 0 | Up + Circle [charged] |
| 44 | 2 | 20 / 0 | 0 | Up + Circle [charged] |
| 44 | 3 | 20 / 0 | 0 | Up + Circle [charged] |
| 44 | 4 | 20 / 0 | 0 | Up + Circle [charged] |
| 45 | 0 | 20 / 0 | 0 | Up + Circle [charged] |
| 45 | 1 | 20 / 0 | 0 | Up + Circle [charged] |
| 45 | 2 | 20 / 0 | 0 | Up + Circle [charged] |
| 45 | 3 | 20 / 0 | 0 | Up + Circle [charged] |
| 46 | 0 | 20 / 0 | 0 | Up + Circle [charged] |
| 46 | 1 | 20 / 0 | 0 | Up + Circle [charged] |
| 46 | 2 | 20 / 0 | 0 | Up + Circle [charged] |
| 46 | 3 | 20 / 0 | 0 | Up + Circle [charged] |
| 46 | 4 | 20 / 0 | 0 | Up + Circle [charged] |
| 47 | 0 | 40 / 0 | 0 | Up + Circle [charged] |
| 47 | 1 | 40 / 0 | 0 | Up + Circle [charged] |
| 47 | 2 | 40 / 0 | 0 | Up + Circle [charged] |
| 47 | 3 | 40 / 0 | 0 | Up + Circle [charged] |
| 48 | 0 | 100 / 0 | 0 | Up + Circle [charged] |
| 49 | 0 | 100 / 0 | 0 | Up + Circle [charged] |
| 68 | 0 | 350 / 105 | 0 | Triangle + R1, Triangle + R1 [charged] |

| Projectile | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|
| 60 | 80 / 8 | 290 | Circle, Circle [charged] |
| 61 | 110 / 82.5 | 100 | Circle, Circle [charged] |
| 62 | 140 / 105 | 100 | Circle, Circle [charged] |
| 63 | 170 / 127.5 | 100 | Circle, Circle [charged] |
| 64 | 200 / 150 | 100 | Circle, Circle [charged] |
| 65 | 500 / 50 | 100 | Circle, Circle [charged] |

Actions with a Stamina cost that landed a hit: 29: 50, 88: 700, 89: 400, 131: 50.

## Knockdowns

Hits that knock the target down, from the attack data (2026-11-03). The number is the hit's reaction (attack slot +0x39, projectile row +0xAF); build-up is its "too many hits" number (the target goes down at 150). What each number does and the get-up are in [Knockdowns.md](../balance/Knockdowns.md).

| Row | Move | Hit | Number | What it does | Damage | Build-up |
|---|---|---|---|---|---|---|
| 1 |  | 1 | 21 | launched, then down | 100 | 4 |
| 2 |  | 0 | 6 | knocked away (dmg_blow) | 200 | 10 |
| 3 |  | 1 | 6 | knocked away (dmg_blow) | 150 | 200 |
| 4 |  | 1 | 6 | knocked away (dmg_blow) | 200 | 300 |
| 5 |  | 1 | 6 | knocked away (dmg_blow) | 250 | 400 |
| 6 |  | 0 | 21 | launched, then down | 125 | 10 |
| 7 |  | 0 | 21 | launched, then down | 125 | 20 |
| 8 |  | 0 | 6 | knocked away (dmg_blow) | 150 | 30 |
| 9 |  | 0 | 6 | knocked away (dmg_blow) | 100 | 20 |
| 9 |  | 1 | 6 | knocked away (dmg_blow) | 100 | 20 |
| 12 |  | 0 | 12 | pierced, then down (dmg_penetration) | 220 | 80 |
| 13 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 550 | 350 |
| 14 |  | 0 | 6 | knocked away (dmg_blow) | 1100 | 1000 |
| 16 |  | 0 | 6 | knocked away (dmg_blow) | 250 | 750 |
| 17 |  | 0 | 6 | knocked away (dmg_blow) | 300 | 900 |
| 18 |  | 0 | 21 | launched, then down | 180 | 10 |
| 19 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 180 | 10 |
| 20 |  | 0 | 6 | knocked away (dmg_blow) | 180 | 10 |
| 21 |  | 0 | 5 | spun away (dmg_blow_spin) | 100 | 10 |
| 22 |  | 0 | 4 | knocked down, big bounce | 100 | 10 |
| 23 |  | 0 | 9 | knocked down, big bounce | 180 | 10 |
| 23 |  | 1 | 9 | knocked down, big bounce | 180 | 10 |
| 25 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 100 | 0 |
| 26 |  | 0 | 6 | knocked away (dmg_blow) | 100 | 0 |
| 27 |  | 0 | 5 | spun away (dmg_blow_spin) | 180 | 0 |
| 29 |  | 0 | 6 | knocked away (dmg_blow) | 180 | 0 |
| 31 |  | 0 | 5 | spun away (dmg_blow_spin) | 180 | 0 |
| 32 |  | 0 | 6 | knocked away (dmg_blow) | 180 | 0 |
| 34 |  | 0 | 19 | knocked away (dmg_kizuna1_hit_blow) | 250 | 0 |
| 35 |  | 0 | 20 | big bounce (dmg_conspicuous_bound 2) | 250 | 0 |
| 37 |  | 0 | 6 | knocked away (dmg_blow) | 50 | 0 |
| 39 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 120 | 600 |
| 41 |  | 0 | 4 | knocked down, big bounce | 100 | 0 |
| 42 |  | 0 | 6 | knocked away (dmg_blow) | 100 | 0 |
| 44 |  | 0 | 4 | knocked down, big bounce | 20 | 0 |
| 46 |  | 4 | 3 | lifted off the feet; lands and goes down unless hit again | 20 | 0 |
| 48 |  | 0 | 6 | knocked away (dmg_blow) | 100 | 0 |
| 49 |  | 0 | 6 | knocked away (dmg_blow) | 100 | 0 |
| 50 |  | 0 | 6 | knocked away (dmg_blow) | 400 | 0 |
| 52 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 400 | 0 |
| 52 |  | 1 | 17 | big knock away (dmg_conspicuous_blow) | 600 | 0 |
| 56 |  | 0 | 6 | knocked away (dmg_blow) | 180 | 0 |
| 57 |  | 0 | 4 | knocked down, big bounce | 180 | 800 |
| 58 |  | 0 | 6 | knocked away (dmg_blow) | 50 | 0 |
| 59 |  | 0 | 6 | knocked away (dmg_blow) | 50 | 0 |
| 61 |  | 0 | 4 | knocked down, big bounce | 10 | 50 |
| 63 |  | 0 | 6 | knocked away (dmg_blow) | 40 | 50 |
| 66 |  | 0 | 6 | knocked away (dmg_blow) | 200 | 0 |
| 68 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 350 | 0 |
| 69 |  | 3 | 4 | knocked down, big bounce | 5 | 0 |
| 70 |  | 0 | 6 | knocked away (dmg_blow) | 350 | 0 |

| Projectile row | Number | What it does | Damage |
|---|---|---|---|
| 5 | 5 | spun away (dmg_blow_spin) | 600 |
| 10 | 5 | spun away (dmg_blow_spin) | 120 |
| 11 | 5 | spun away (dmg_blow_spin) | 120 |
| 30 | 5 | spun away (dmg_blow_spin) | 300 |
| 35 | 11 | shocked, then down (dmg_shock) | 100 |
| 36 | 11 | shocked, then down (dmg_shock) | 100 |
| 37 | 11 | shocked, then down (dmg_shock) | 100 |
| 38 | 11 | shocked, then down (dmg_shock) | 100 |
| 40 | 11 | shocked, then down (dmg_shock) | 30 |
| 41 | 11 | shocked, then down (dmg_shock) | 30 |
| 42 | 11 | shocked, then down (dmg_shock) | 30 |
| 43 | 11 | shocked, then down (dmg_shock) | 30 |
| 45 | 11 | shocked, then down (dmg_shock) | 45 |
| 46 | 11 | shocked, then down (dmg_shock) | 45 |
| 47 | 5 | spun away (dmg_blow_spin) | 100 |
| 55 | 5 | spun away (dmg_blow_spin) | 400 |
| 56 | 5 | spun away (dmg_blow_spin) | 1 |
| 65 | 5 | spun away (dmg_blow_spin) | 500 |
| 66 | 5 | spun away (dmg_blow_spin) | 1 |
| 85 | 5 | spun away (dmg_blow_spin) | 1200 |
| 91 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 92 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 93 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 95 | 11 | shocked, then down (dmg_shock) | 10 |
| 96 | 11 | shocked, then down (dmg_shock) | 10 |
| 97 | 11 | shocked, then down (dmg_shock) | 10 |
| 98 | 11 | shocked, then down (dmg_shock) | 10 |
| 100 | 11 | shocked, then down (dmg_shock) | 30 |
| 101 | 11 | shocked, then down (dmg_shock) | 30 |
| 102 | 11 | shocked, then down (dmg_shock) | 30 |
| 103 | 11 | shocked, then down (dmg_shock) | 30 |

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Spirit Bomb | Press R3 during a Victory Burst. |
| Kamehameha | Circle (Also possible while jumping) |
| Super Kamehameha | Press Circle while in Super Saiyan mode (Also possible while jumping) |
| Meteor Smash | Left stick up + Circle (Also possible while jumping) |
| Super Meteor Smash | Press Left stick up + Circle while in Super Saiyan mode (Also possible while jumping) |
| Vanishing Counter | R1 + Circle (Activates when the opponent attacks) |
| Super Vanishing Counter | While in Super Saiyan mode, press R1 + Circle (Activates when the opponent attacks) |
| Instant Transmission Kamehameha | While in Super Saiyan mode, press Circle then Triangle (also possible while jumping). |
| Stamina Charge | R1 + Cross Charge past Stamina Gauge limit and hit max to turn into Super Saiyan. |
| Strong Attack | Triangle (Hold to charge) |
| Dash Attack | Press Square ,  Square ,  Square while dashing. |
| Jumping Attack 2 | Press Triangle ,  Triangle ,  Triangle while jumping. |
