# Frieza (character 008)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `008_frieza_mp_param_attack_PS3.d` inside `battle_character_008_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 100 / 60 | 100 | 4 |
| 1 | 75 / 45, 75 / 45 | 150 | 7 |
| 2 | 100 / 60, 100 / 60 | 200 | 20 |
| 3 | 300 / 180 | 300 | 20 |
| 12 | 200 / 120 | 200 | 100 |
| 13 | 500 / 300 | 500 | 400 |
| 14 | 1000 / 600 | 1000 | 1000 |
| 15 | 160 / 96 | 160 | 0 |
| 16 | 200 / 120 | 200 | 0 |
| 17 | 250 / 150 | 250 | 0 |
| 18 | 80 / 48 | 80 | 10 |
| 19 | 80 / 48, 80 / 48 | 160 | 20 |
| 21 | 75 / 45, 150 / 90 | 225 | 15 |
| 22 | 100 / 60, 100 / 60 | 200 | 20 |
| 25 | 120 / 72 | 120 | 0 |
| 26 | 120 / 72 | 120 | 0 |
| 27 | 120 / 72 | 120 | 0 |
| 28 | 120 / 72 | 120 | 0 |
| 29 | 120 / 72 | 120 | 0 |
| 30 | 120 / 72 | 120 | 0 |
| 31 | 125 / 75, 125 / 75 | 250 | 400 |
| 33 | 250 / 150 | 250 | 800 |
| 34 | 150 / 90 | 150 | 120 |
| 35 | 120 / 60, 1 / 0.5, 1 / 0.5, 1 / 0.5, 1 / 0.5, 1 / 0.5 | 125 | 10 |
| 36 | 70 / 35, 70 / 35, 70 / 35 | 210 | 30 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 0 | 250 / 25 | 100 |
| 1 | 250 / 25 | 100 |
| 2 | 30 / 22.5 | 10 |
| 3 | 30 / 22.5 | 10 |
| 5 | 250 / 25 | 100 |
| 6 | 250 / 25 | 100 |
| 7 | 30 / 22.5 | 10 |
| 8 | 30 / 22.5 | 10 |
| 10 | 30 / 3 | 0 |
| 11 | 10 / 7.5 | 10 |
| 15 | 30 / 3 | 0 |
| 16 | 10 / 7.5 | 10 |
| 20 | 30 / 3 | 0 |
| 21 | 10 / 7.5 | 10 |
| 30 | 1000 / 100 | 0 |
| 31 | 1000 / 100 | 0 |
| 35 | 100 / 10 | 10 |
| 36 | 100 / 75 | 10 |
| 40 | 100 / 10 | 10 |
| 41 | 100 / 10 | 10 |
| 42 | 100 / 75 | 10 |
| 43 | 100 / 75 | 10 |
| 45 | 100 / 10 | 10 |
| 46 | 100 / 10 | 10 |
| 47 | 100 / 10 | 10 |
| 50 | 100 / 10 | 10 |
| 51 | 100 / 10 | 10 |
| 52 | 100 / 10 | 10 |
| 55 | 50 / 37.5 | 50 |
| 56 | 50 / 37.5 | 50 |
| 57 | 50 / 37.5 | 50 |
| 60 | 30 / 22.5 | 200 |
| 61 | 10 / 7.5 | 10 |
| 65 | 100 / 75 | 200 |
| 66 | 100 / 75 | 200 |
| 67 | 30 / 22.5 | 10 |
| 68 | 30 / 22.5 | 10 |
| 70 | 300 / 225 | 1000 |
| 71 | 10 / 7.5 | 10 |
| 75 | 30 / 3 | 0 |
| 76 | 10 / 7.5 | 10 |
| 80 | 30 / 3 | 0 |
| 81 | 10 / 7.5 | 10 |
| 85 | 30 / 3 | 0 |
| 86 | 10 / 7.5 | 10 |
| 90 | 125 / 12.5 | 100 |
| 91 | 125 / 12.5 | 100 |
| 92 | 30 / 22.5 | 10 |
| 93 | 30 / 22.5 | 10 |
| 95 | 15 / 11.2 | 50 |
| 96 | 10 / 7.5 | 200 |
| 97 | 10 / 7.5 | 10 |
| 98 | 10 / 7.5 | 10 |
| 99 | 10 / 7.5 | 10 |
| 100 | 10 / 7.5 | 450 |
| 101 | 10 / 7.5 | 450 |
| 102 | 10 / 7.5 | 450 |
| 103 | 10 / 7.5 | 450 |
| 104 | 10 / 7.5 | 10 |
| 105 | 250 / 25 | 0 |
| 106 | 10 / 7.5 | 10 |
| 110 | 30 / 3 | 0 |
| 111 | 10 / 7.5 | 10 |

## Stamina costs

From `param_action`: +0x0C is the cost, on 48-byte rows in groups of 4. Action = row / 4. Which skill uses each action is only known after a logged session. A negative number has not been tested; it may give Stamina back.

| Action | Stamina |
|---|---|
| 11 | 50 |
| 16 | 80 |
| 21 | -150 |
| 29 | 100 |
| 30 | 200 |
| 31 | 200 |
| 32 | 300 |
| 85 | 50 |
| 86 | 50 |
| 87 | 50 |
| 88 | 300 |
| 89 | 600 |
| 90 | 500 |
| 91 | 100 |
| 131 | 50 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen 2000. Hits logged 97.

Seen so far: 6 of 25 attack rows, 5 of 63 projectiles, 5 of 14 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 100 / 60 | 4 | Square, Square [charged] |
| 1 | 0 | 75 / 45 | 3 | Square |
| 1 | 1 | 75 / 45 | 4 | Square |
| 2 | 0 | 100 / 60 | 10 | Square [charged] |
| 2 | 1 | 100 / 60 | 10 | Square [charged] |
| 12 | 0 | 200 / 120 | 100 | Triangle, Triangle [charged] |
| 15 | 0 | 160 / 96 | 0 | Triangle + R1, Triangle + R1 [charged] |
| 21 | 1 | 150 / 90 | 10 | Triangle |

| Projectile | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|
| 0 | 250 / 25 | 100 | Up + Circle |
| 1 | 250 / 25 | 100 | Up + Circle |
| 10 | 30 / 3 | 0 | Circle, Circle [charged] |
| 31 | 1000 / 100 | 0 | Circle + R1, Up + Circle [charged] |
| 35 | 100 / 10 | 10 | Square + R1, Square + R1 [charged] |

Actions with a Stamina cost that landed a hit: 29: 100, 88: 300, 89: 600, 90: 500, 131: 50.

## Knockdowns

Hits that knock the target down, from the attack data (2026-11-03). The number is the hit's reaction (attack slot +0x39, projectile row +0xAF); build-up is its "too many hits" number (the target goes down at 150). What each number does and the get-up are in [Knockdowns.md](../balance/Knockdowns.md).

| Row | Move | Hit | Number | What it does | Damage | Build-up |
|---|---|---|---|---|---|---|
| 2 |  | 1 | 6 | knocked away (dmg_blow) | 100 | 10 |
| 3 |  | 0 | 6 | knocked away (dmg_blow) | 300 | 20 |
| 12 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 200 | 100 |
| 13 |  | 0 | 6 | knocked away (dmg_blow) | 500 | 400 |
| 14 |  | 0 | 6 | knocked away (dmg_blow) | 1000 | 1000 |
| 15 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 160 | 0 |
| 16 |  | 0 | 6 | knocked away (dmg_blow) | 200 | 0 |
| 17 |  | 0 | 6 | knocked away (dmg_blow) | 250 | 0 |
| 18 |  | 0 | 4 | knocked down, big bounce | 80 | 10 |
| 19 |  | 1 | 6 | knocked away (dmg_blow) | 80 | 10 |
| 21 |  | 1 | 21 | launched, then down | 150 | 10 |
| 22 |  | 1 | 6 | knocked away (dmg_blow) | 100 | 10 |
| 25 |  | 0 | 4 | knocked down, big bounce | 120 | 0 |
| 26 |  | 0 | 4 | knocked down, big bounce | 120 | 0 |
| 27 |  | 0 | 4 | knocked down, big bounce | 120 | 0 |
| 28 |  | 0 | 5 | spun away (dmg_blow_spin) | 120 | 0 |
| 29 |  | 0 | 4 | knocked down, big bounce | 120 | 0 |
| 30 |  | 0 | 6 | knocked away (dmg_blow) | 120 | 0 |
| 31 |  | 1 | 19 | knocked away (dmg_kizuna1_hit_blow) | 125 | 200 |
| 33 |  | 0 | 20 | big bounce (dmg_conspicuous_bound 2) | 250 | 800 |
| 36 |  | 0 | 6 | knocked away (dmg_blow) | 70 | 10 |

| Projectile row | Number | What it does | Damage |
|---|---|---|---|
| 0 | 5 | spun away (dmg_blow_spin) | 250 |
| 1 | 5 | spun away (dmg_blow_spin) | 250 |
| 5 | 5 | spun away (dmg_blow_spin) | 250 |
| 6 | 5 | spun away (dmg_blow_spin) | 250 |
| 10 | 12 | pierced, then down (dmg_penetration) | 30 |
| 11 | 12 | pierced, then down (dmg_penetration) | 10 |
| 15 | 12 | pierced, then down (dmg_penetration) | 30 |
| 16 | 12 | pierced, then down (dmg_penetration) | 10 |
| 20 | 12 | pierced, then down (dmg_penetration) | 30 |
| 21 | 12 | pierced, then down (dmg_penetration) | 10 |
| 30 | 5 | spun away (dmg_blow_spin) | 1000 |
| 31 | 5 | spun away (dmg_blow_spin) | 1000 |
| 35 | 12 | pierced, then down (dmg_penetration) | 100 |
| 47 | 10 | knocked down, rolling bounce | 100 |
| 55 | 17 | big knock away (dmg_conspicuous_blow) | 50 |
| 56 | 17 | big knock away (dmg_conspicuous_blow) | 50 |
| 57 | 17 | big knock away (dmg_conspicuous_blow) | 50 |
| 60 | 12 | pierced, then down (dmg_penetration) | 30 |
| 70 | 6 | knocked away (dmg_blow) | 300 |
| 75 | 12 | pierced, then down (dmg_penetration) | 30 |
| 76 | 12 | pierced, then down (dmg_penetration) | 10 |
| 80 | 12 | pierced, then down (dmg_penetration) | 30 |
| 81 | 12 | pierced, then down (dmg_penetration) | 10 |
| 85 | 12 | pierced, then down (dmg_penetration) | 30 |
| 86 | 12 | pierced, then down (dmg_penetration) | 10 |
| 90 | 5 | spun away (dmg_blow_spin) | 125 |
| 91 | 5 | spun away (dmg_blow_spin) | 125 |
| 96 | 5 | spun away (dmg_blow_spin) | 10 |
| 100 | 5 | spun away (dmg_blow_spin) | 10 |
| 101 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 102 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 103 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 105 | 5 | spun away (dmg_blow_spin) | 250 |
| 110 | 12 | pierced, then down (dmg_penetration) | 30 |
| 111 | 12 | pierced, then down (dmg_penetration) | 10 |

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Death Ball | Press R3 during a Victory Burst. |
| I Won't Let You Escape With Your Life... | R1 + Circle (Press Circle after binding an enemy to detonate) |
| Death Wave | Left stick up + Circle (Also possible while jumping) |
| Death Beam | Circle (Hold to charge / Also possible while jumping) |
| Stamina Charge | R1 + Cross Keep holding to charge beyond the Stamina Gauge limit. |
| Strong Attack | Triangle (Hold to charge) |
| Dash Attack | Press Square ,  Square ,  Square while dashing. |
| Jumping Attack 1 | Press Square ,  Square while jumping. |
| Jumping Attack 2 | Press Triangle ,  Triangle ,  Triangle while jumping. |
| Ki Blast | R1 + Square |
| Ki Blast Combo | R1 + Square ,  Triangle / R1 + Square ,  Square ,  Triangle / R1 + Square ,  Square ,  Square ,  Triangle |
