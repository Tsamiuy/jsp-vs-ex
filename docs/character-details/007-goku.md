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
