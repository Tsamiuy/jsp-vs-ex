# Vegeta (character 009)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `009_vegeta_mp_param_attack_PS3.d` inside `battle_character_009_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 80 / 16 | 80 | 5 |
| 1 | 100 / 20 | 100 | 5 |
| 2 | 150 / 30 | 150 | 10 |
| 3 | 200 / 40 | 200 | 200 |
| 12 | 200 / 40 | 200 | 100 |
| 13 | 500 / 100 | 500 | 400 |
| 14 | 1000 / 200 | 1000 | 1000 |
| 15 | 160 / 32 | 160 | 0 |
| 16 | 200 / 40 | 200 | 0 |
| 17 | 250 / 50 | 250 | 0 |
| 18 | 80 / 16 | 80 | 10 |
| 19 | 80 / 16 | 80 | 10 |
| 20 | 80 / 16 | 80 | 10 |
| 21 | 200 / 40 | 200 | 10 |
| 23 | 100 / 20 | 100 | 5 |
| 24 | 100 / 20 | 100 | 5 |
| 25 | 80 / 16 | 80 | 0 |
| 26 | 80 / 16 | 80 | 0 |
| 27 | 80 / 16 | 80 | 0 |
| 28 | 80 / 16 | 80 | 0 |
| 29 | 80 / 16 | 80 | 0 |
| 30 | 80 / 16 | 80 | 0 |
| 31 | 150 / 30, 150 / 30, 150 / 30, 150 / 30, 150 / 30, 150 / 30, 150 / 30 | 1050 | 700 |
| 32 | 75 / 15, 150 / 30, 150 / 30, 150 / 30, 150 / 30, 150 / 30, 150 / 30 | 975 | 700 |
| 33 | 250 / 50 | 250 | 0 |
| 35 | 200 / 40 | 200 | 0 |
| 36 | 150 / 90 | 150 | 120 |
| 37 | 250 / 200, 100 / 80, 100 / 80, 50 / 40, 50 / 40, 50 / 40, 50 / 40 | 650 | 30 |
| 38 | 120 / 60, 1 / 0.5, 1 / 0.5, 1 / 0.5, 1 / 0.5, 1 / 0.5 | 125 | 10 |
| 39 | 70 / 35, 70 / 35, 70 / 35 | 210 | 30 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 0 | 180 / 54 | 3 |
| 1 | 180 / 54 | 3 |
| 2 | 180 / 54 | 3 |
| 3 | 180 / 54 | 3 |
| 4 | 50 / 15 | 3 |
| 5 | 50 / 15 | 3 |
| 6 | 50 / 15 | 3 |
| 7 | 50 / 15 | 3 |
| 8 | 50 / 15 | 3 |
| 9 | 50 / 15 | 3 |
| 10 | 50 / 15 | 3 |
| 11 | 50 / 15 | 3 |
| 12 | 50 / 15 | 3 |
| 13 | 50 / 15 | 3 |
| 14 | 50 / 15 | 3 |
| 15 | 500 / 150 | 200 |
| 16 | 10 / 3 | 0 |
| 20 | 500 / 150 | 0 |
| 21 | 500 / 150 | 0 |
| 25 | 500 / 150 | 0 |
| 26 | 500 / 150 | 0 |
| 30 | 50 / 15 | 20 |
| 31 | 50 / 15 | 20 |
| 32 | 50 / 15 | 20 |
| 33 | 50 / 15 | 20 |
| 34 | 50 / 15 | 0 |
| 35 | 50 / 15 | 20 |
| 36 | 50 / 15 | 20 |
| 37 | 50 / 15 | 20 |
| 38 | 50 / 15 | 20 |
| 39 | 50 / 15 | 0 |
| 40 | 50 / 15 | 20 |
| 41 | 50 / 15 | 20 |
| 42 | 50 / 15 | 20 |
| 43 | 50 / 15 | 20 |
| 44 | 50 / 15 | 0 |
| 45 | 200 / 60 | 500 |
| 46 | 50 / 15 | 0 |
| 47 | 50 / 15 | 0 |
| 48 | 50 / 15 | 0 |
| 49 | 50 / 15 | 0 |
| 50 | 50 / 15 | 20 |
| 51 | 50 / 15 | 20 |
| 52 | 50 / 15 | 20 |
| 53 | 50 / 15 | 20 |
| 54 | 50 / 15 | 0 |
| 55 | 50 / 15 | 20 |
| 56 | 50 / 15 | 20 |
| 57 | 50 / 15 | 20 |
| 58 | 50 / 15 | 20 |
| 59 | 50 / 15 | 0 |
| 60 | 50 / 15 | 20 |
| 61 | 50 / 15 | 20 |
| 62 | 50 / 15 | 20 |
| 63 | 50 / 15 | 20 |
| 64 | 50 / 15 | 0 |
| 65 | 50 / 15 | 0 |
| 66 | 50 / 15 | 0 |
| 67 | 50 / 15 | 0 |
| 68 | 50 / 15 | 0 |
| 69 | 50 / 15 | 0 |
| 70 | 50 / 15 | 0 |
| 71 | 50 / 15 | 0 |
| 72 | 50 / 15 | 0 |
| 73 | 50 / 15 | 0 |
| 75 | 200 / 60 | 0 |
| 76 | 10 / 3 | 0 |
| 80 | 200 / 60 | 0 |
| 81 | 10 / 3 | 0 |
| 85 | 50 / 0 | 20 |
| 86 | 50 / 0 | 20 |
| 87 | 50 / 0 | 20 |
| 88 | 50 / 0 | 20 |
| 89 | 50 / 0 | 0 |
| 90 | 50 / 0 | 20 |
| 91 | 50 / 0 | 20 |
| 92 | 50 / 0 | 0 |
| 93 | 50 / 0 | 0 |
| 94 | 50 / 0 | 0 |
| 95 | 50 / 0 | 2 |
| 96 | 50 / 0 | 2 |
| 97 | 50 / 0 | 2 |
| 98 | 50 / 0 | 2 |
| 99 | 50 / 15 | 2 |
| 100 | 50 / 15 | 20 |
| 101 | 50 / 15 | 20 |
| 102 | 50 / 15 | 20 |
| 103 | 50 / 15 | 20 |
| 104 | 50 / 15 | 0 |
| 105 | 50 / 15 | 20 |
| 106 | 50 / 15 | 20 |
| 107 | 50 / 15 | 0 |
| 108 | 50 / 15 | 0 |
| 109 | 50 / 15 | 0 |
| 110 | 50 / 15 | 2 |
| 111 | 50 / 15 | 2 |
| 112 | 60 / 18 | 2 |
| 113 | 60 / 18 | 2 |
| 116 | 12 / 3.6 | 1 |
| 117 | 60 / 18 | 20 |
| 118 | 60 / 18 | 20 |
| 120 | 16 / 4.8 | 14 |
| 125 | 50 / 15 | 20 |
| 126 | 50 / 15 | 20 |
| 127 | 50 / 15 | 20 |
| 128 | 50 / 15 | 20 |
| 129 | 50 / 15 | 0 |
| 130 | 380 / 114 | 0 |
| 131 | 1500 / 450 | 450 |

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
| 85 | 50 |
| 86 | 50 |
| 87 | 50 |
| 88 | 400 |
| 89 | 800 |
| 90 | 600 |
| 104 | 30 |
| 105 | 50 |
| 106 | 50 |
| 107 | 50 |
| 131 | 50 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen 2000. Hits logged 60.

Seen so far: 6 of 30 attack rows, 6 of 109 projectiles, 4 of 18 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 80 / 16 | 5 | Square, Square [charged] |
| 1 | 0 | 100 / 20 | 5 | Square |
| 2 | 0 | 150 / 30 | 10 | Square |
| 3 | 0 | 200 / 40 | 200 | Triangle, Triangle [charged] |
| 12 | 0 | 200 / 40 | 100 | Triangle, Triangle [charged] |
| 31 | 0 | 150 / 30 | 100 | Circle + R1, Circle + R1 [charged] |

| Projectile | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|
| 0 | 180 / 54 | 3 | Circle, Circle [charged] |
| 10 | 50 / 15 | 3 | Square |
| 20 | 500 / 150 | 0 | Up + Circle [charged] |
| 32 | 50 / 15 | 20 | Square + R1, Square + R1 [charged] |
| 33 | 50 / 15 | 20 | Square + R1, Square + R1 [charged] |
| 120 | 16 / 4.8 | 14 | Up + Circle [charged] |

Actions with a Stamina cost that landed a hit: 29: 50, 88: 400, 89: 800, 90: 600.

## Knockdowns

Hits that knock the target down, from the attack data (2026-11-03). The number is the hit's reaction (attack slot +0x39, projectile row +0xAF); build-up is its "too many hits" number (the target goes down at 150). What each number does and the get-up are in [Knockdowns.md](../balance/Knockdowns.md).

| Row | Move | Hit | Number | What it does | Damage | Build-up |
|---|---|---|---|---|---|---|
| 2 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 150 | 10 |
| 3 |  | 0 | 6 | knocked away (dmg_blow) | 200 | 200 |
| 13 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 500 | 400 |
| 14 |  | 0 | 6 | knocked away (dmg_blow) | 1000 | 1000 |
| 16 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 200 | 0 |
| 17 |  | 0 | 6 | knocked away (dmg_blow) | 250 | 0 |
| 19 |  | 0 | 5 | spun away (dmg_blow_spin) | 80 | 10 |
| 20 |  | 0 | 6 | knocked away (dmg_blow) | 80 | 10 |
| 23 |  | 0 | 6 | knocked away (dmg_blow) | 100 | 5 |
| 24 |  | 0 | 6 | knocked away (dmg_blow) | 100 | 5 |
| 25 |  | 0 | 6 | knocked away (dmg_blow) | 80 | 0 |
| 26 |  | 0 | 5 | spun away (dmg_blow_spin) | 80 | 0 |
| 27 |  | 0 | 4 | knocked down, big bounce | 80 | 0 |
| 28 |  | 0 | 5 | spun away (dmg_blow_spin) | 80 | 0 |
| 29 |  | 0 | 6 | knocked away (dmg_blow) | 80 | 0 |
| 30 |  | 0 | 6 | knocked away (dmg_blow) | 80 | 0 |
| 31 |  | 0 | 6 | knocked away (dmg_blow) | 150 | 100 |
| 31 |  | 6 | 5 | spun away (dmg_blow_spin) | 150 | 100 |
| 32 |  | 0 | 5 | spun away (dmg_blow_spin) | 75 | 100 |
| 32 |  | 6 | 5 | spun away (dmg_blow_spin) | 150 | 100 |
| 33 |  | 0 | 19 | knocked away (dmg_kizuna1_hit_blow) | 250 | 0 |
| 35 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 200 | 0 |
| 37 |  | 0 | 20 | big bounce (dmg_conspicuous_bound 2) | 250 | 20 |
| 39 |  | 0 | 6 | knocked away (dmg_blow) | 70 | 10 |

| Projectile row | Number | What it does | Damage |
|---|---|---|---|
| 0 | 5 | spun away (dmg_blow_spin) | 180 |
| 1 | 5 | spun away (dmg_blow_spin) | 180 |
| 2 | 5 | spun away (dmg_blow_spin) | 180 |
| 3 | 5 | spun away (dmg_blow_spin) | 180 |
| 6 | 5 | spun away (dmg_blow_spin) | 50 |
| 7 | 5 | spun away (dmg_blow_spin) | 50 |
| 10 | 4 | knocked down, big bounce | 50 |
| 11 | 4 | knocked down, big bounce | 50 |
| 15 | 5 | spun away (dmg_blow_spin) | 500 |
| 20 | 5 | spun away (dmg_blow_spin) | 500 |
| 21 | 5 | spun away (dmg_blow_spin) | 500 |
| 25 | 5 | spun away (dmg_blow_spin) | 500 |
| 26 | 5 | spun away (dmg_blow_spin) | 500 |
| 45 | 17 | big knock away (dmg_conspicuous_blow) | 200 |
| 46 | 5 | spun away (dmg_blow_spin) | 50 |
| 65 | 5 | spun away (dmg_blow_spin) | 50 |
| 66 | 5 | spun away (dmg_blow_spin) | 50 |
| 67 | 5 | spun away (dmg_blow_spin) | 50 |
| 68 | 5 | spun away (dmg_blow_spin) | 50 |
| 69 | 5 | spun away (dmg_blow_spin) | 50 |
| 71 | 5 | spun away (dmg_blow_spin) | 50 |
| 75 | 3 | lifted off the feet; lands and goes down unless hit again | 200 |
| 80 | 6 | knocked away (dmg_blow) | 200 |
| 116 | 5 | spun away (dmg_blow_spin) | 12 |
| 120 | 5 | spun away (dmg_blow_spin) | 16 |
| 130 | 5 | spun away (dmg_blow_spin) | 380 |
| 131 | 5 | spun away (dmg_blow_spin) | 1500 |

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Final Flash | Press R3 during a Victory Burst. |
| Explosive Wave | R1 + Circle |
| Energy Blast Volley | Circle (Tap repeatedly to keep firing / Also possible while jumping) |
| Big Bang Attack | Left stick up + Circle (Also possible while jumping) |
| Super Explosive Wave | Press R1 + Circle while in Super Saiyan mode (Also possible while jumping) |
| Super Energy Blast Volley | Press Circle while in Super Saiyan mode (Tap repeatedly to keep firing / Also possible while jumping) |
| Big Bang Volley | While in Super Saiyan mode, press Left stick up Circle |
| Stamina Charge | R1 + Cross Charge past Stamina Gauge limit and hit max to turn into Super Saiyan. |
| Strong Attack | Triangle (Hold to charge) |
| Dash Attack | Press Square ,  Square ,  Square while dashing. |
| Jumping Attack 1 | Press Square while jumping. |
| Jumping Attack 2 | Press Triangle ,  Triangle ,  Triangle while jumping. (Alternatively, press Triangle ,  Square / Triangle ,  Triangle ,  Square) |
| Ki Blast | R1 + Square |
| Ki Blast Combo | R1 + Square ,  Triangle / R1 + Square ,  Square ,  Triangle / R1 + Square ,  Square ,  Square ,  Triangle |
