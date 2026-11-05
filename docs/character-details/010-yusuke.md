# Yusuke (character 010)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `010_yusuke_mp_param_attack_PS3.d` inside `battle_character_010_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 120 / 48 | 120 | 5 |
| 1 | 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 200 | 24 |
| 2 | 250 / 100 | 250 | 30 |
| 3 | 22 / 8.8, 22 / 8.8, 22 / 8.8, 22 / 8.8, 22 / 8.8, 22 / 8.8, 22 / 8.8, 22 / 8.8, 22 / 8.8 | 198 | 140 |
| 4 | 200 / 80 | 200 | 300 |
| 5 | 300 / 120 | 300 | 0 |
| 6 | 75 / 30, 75 / 30 | 150 | 10 |
| 7 | 100 / 40, 100 / 40 | 200 | 10 |
| 8 | 250 / 100 | 250 | 10 |
| 9 | 300 / 120 | 300 | 0 |
| 10 | 300 / 120 | 300 | 0 |
| 11 | 300 / 120 | 300 | 0 |
| 12 | 240 / 96 | 240 | 150 |
| 13 | 320 / 128 | 320 | 250 |
| 14 | 420 / 168 | 420 | 400 |
| 15 | 100 / 40, 100 / 40, 100 / 40 | 300 | 300 |
| 16 | 340 / 136 | 340 | 300 |
| 17 | 300 / 120 | 300 | 0 |
| 18 | 200 / 80 | 200 | 10 |
| 19 | 200 / 80 | 200 | 10 |
| 20 | 200 / 80 | 200 | 10 |
| 21 | 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 250 | 20 |
| 22 | 200 / 80 | 200 | 10 |
| 23 | 200 / 80 | 200 | 10 |
| 25 | 100 / 40, 200 / 80, 200 / 80, 200 / 80, 200 / 80, 200 / 80, 200 / 80, 140 / 56 | 1440 | 800 |
| 26 | 500 / 200, 700 / 280 | 1200 | 1000 |
| 27 | 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 200 | 0 |
| 28 | 100 / 40, 100 / 40, 100 / 40, 100 / 40 | 400 | 0 |
| 29 | 200 / 80, 200 / 80, 200 / 80, 200 / 80, 200 / 80, 200 / 80, 200 / 80, 70 / 28, 140 / 56 | 1610 | 1610 |
| 30 | 100 / 40, 100 / 40, 200 / 80, 200 / 80, 200 / 80, 200 / 80, 200 / 80, 140 / 56 | 1340 | 770 |
| 31 | 200 / 80, 200 / 80, 200 / 80, 200 / 80, 200 / 80, 200 / 80, 200 / 80, 200 / 80, 140 / 56 | 1740 | 840 |
| 32 | 150 / 60 | 150 | 120 |
| 33 | 250 / 100 | 250 | 500 |
| 34 | 250 / 100 | 250 | 300 |
| 35 | 20 / 8, 62 / 24.8, 62 / 24.8, 64 / 25.6 | 208 | 240 |
| 36 | 20 / 8, 62 / 24.8, 62 / 24.8, 64 / 25.6 | 208 | 240 |
| 37 | 450 / 180, 62 / 24.8, 62 / 24.8, 64 / 25.6 | 638 | 240 |
| 38 | 150 / 60, 150 / 60, 150 / 60, 150 / 60, 150 / 60, 150 / 60, 150 / 60, 150 / 60 | 1200 | 770 |
| 39 | 500 / 200, 500 / 200 | 1000 | 1000 |
| 40 | 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 200 | 0 |
| 41 | 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 200 | 0 |
| 42 | 100 / 40, 100 / 40, 100 / 40, 100 / 40 | 400 | 0 |
| 43 | 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 200 | 0 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 0 | 400 / 160 | 100 |
| 1 | 400 / 300 | 100 |
| 2 | 400 / 300 | 100 |
| 5 | 2800 / 0 | 1000 |
| 6 | 10 / 0 | 0 |
| 10 | 150 / 60 | 50 |
| 11 | 250 / 187.5 | 100 |
| 12 | 250 / 187.5 | 100 |
| 15 | 200 / 80 | 50 |
| 16 | 250 / 187.5 | 100 |
| 17 | 250 / 187.5 | 100 |
| 20 | 400 / 160 | 300 |
| 21 | 400 / 300 | 100 |
| 22 | 400 / 300 | 100 |
| 25 | 400 / 160 | 300 |
| 26 | 400 / 300 | 100 |
| 27 | 400 / 300 | 100 |
| 30 | 1100 / 440 | 1000 |
| 31 | 300 / 225 | 100 |
| 32 | 300 / 225 | 100 |
| 35 | 400 / 160 | 300 |
| 36 | 400 / 300 | 100 |
| 37 | 400 / 300 | 100 |
| 40 | 400 / 160 | 300 |
| 41 | 400 / 300 | 100 |
| 42 | 400 / 300 | 100 |

## Stamina costs

From `param_action`: +0x0C is the cost, on 48-byte rows in groups of 4. Action = row / 4. Which skill uses each action is only known after a logged session. A negative number has not been tested; it may give Stamina back.

| Action | Stamina |
|---|---|
| 11 | 50 |
| 16 | 50 or 100 |
| 29 | 50 |
| 30 | 50 |
| 31 | 50 |
| 32 | 50 |
| 85 | 50 |
| 86 | 50 |
| 87 | 50 |
| 88 | 100 |
| 89 | 200 or 400 |
| 90 | 300 |
| 91 | 100 |
| 92 | 150 |
| 93 | 1 |
| 94 | 300 |
| 96 | 300 or 2000 |
| 106 | 10 |
| 131 | 50 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen at least 1720 (my hold ended before the top). Hits logged 129.

Seen so far: 17 of 43 attack rows, 2 of 26 projectiles, 4 of 19 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 120 / 48 | 5 | Square, Square [charged] |
| 1 | 0 | 20 / 8 | 2 | Square, Square [charged] |
| 1 | 1 | 20 / 8 | 2 | Triangle, Square [charged] |
| 1 | 2 | 20 / 8 | 2 | Triangle, Square [charged] |
| 1 | 3 | 20 / 8 | 2 | Triangle, Square [charged] |
| 1 | 4 | 20 / 8 | 4 | Triangle, Square [charged] |
| 1 | 5 | 20 / 8 | 4 | Triangle, Square [charged] |
| 1 | 6 | 20 / 8 | 4 | Triangle, Square [charged] |
| 1 | 7 | 20 / 8 | 0 | Triangle, Square [charged] |
| 1 | 8 | 20 / 8 | 0 | Triangle, Square [charged] |
| 1 | 9 | 20 / 8 | 4 | Triangle, Square [charged] |
| 2 | 0 | 250 / 100 | 30 | Square [charged] |
| 3 | 0 | 22 / 8.8 | 20 | Triangle, Triangle [charged] |
| 3 | 1 | 22 / 8.8 | 20 | Triangle, Triangle [charged] |
| 3 | 2 | 22 / 8.8 | 20 | Triangle, Triangle [charged] |
| 3 | 3 | 22 / 8.8 | 20 | Triangle, Triangle [charged] |
| 3 | 4 | 22 / 8.8 | 20 | Triangle, Triangle [charged] |
| 3 | 5 | 22 / 8.8 | 20 | Triangle, Triangle [charged] |
| 3 | 6 | 22 / 8.8 | 20 | Triangle, Triangle [charged] |
| 3 | 7 | 22 / 8.8 | 0 | Triangle, Triangle [charged] |
| 3 | 8 | 22 / 8.8 | 0 | Triangle, Triangle [charged] |
| 6 | 0 | 75 / 30 | 5 | Square + R1, Square + R1 [charged] |
| 6 | 1 | 75 / 30 | 5 | Square + R1, Square + R1 [charged] |
| 12 | 0 | 240 / 96 | 150 | Triangle, Triangle [charged] |
| 13 | 0 | 320 / 128 | 250 | Triangle |
| 21 | 0 | 50 / 20 | 4 | Square |
| 21 | 1 | 50 / 20 | 4 | Square |
| 21 | 2 | 50 / 20 | 4 | Square |
| 21 | 3 | 50 / 20 | 4 | Square |
| 21 | 4 | 50 / 20 | 4 | Square |
| 25 | 0 | 100 / 40 | 100 | Circle + R1, Circle + R1 [charged] |
| 26 | 0 | 500 / 200 | 800 | Circle + R1 |
| 26 | 1 | 700 / 280 | 200 | Circle + R1 |
| 27 | 0 | 50 / 20 | 0 | Triangle + R1, Triangle + R1 [charged] |
| 27 | 1 | 50 / 20 | 0 | Triangle + R1, Triangle + R1 [charged] |
| 27 | 2 | 50 / 20 | 0 | Triangle + R1, Triangle + R1 [charged] |
| 27 | 3 | 50 / 20 | 0 | Triangle + R1, Triangle + R1 [charged] |
| 28 | 0 | 100 / 40 | 0 | Triangle + R1, Triangle + R1 [charged] |
| 28 | 1 | 100 / 40 | 0 | Triangle + R1, Triangle + R1 [charged] |
| 30 | 0 | 100 / 40 | 70 | Circle + R1, Circle + R1 [charged] |
| 30 | 2 | 100 / 40 | 70 | Circle + R1, Circle + R1 [charged] |
| 40 | 0 | 50 / 20 | 0 | Triangle + R1, Triangle + R1 [charged] |
| 40 | 1 | 50 / 20 | 0 | Triangle + R1, Triangle + R1 [charged] |
| 40 | 2 | 50 / 20 | 0 | Triangle + R1, Triangle + R1 [charged] |
| 40 | 3 | 50 / 20 | 0 | Triangle + R1, Triangle + R1 [charged] |
| 41 | 0 | 50 / 20 | 0 | Triangle |
| 41 | 1 | 50 / 20 | 0 | Triangle |
| 41 | 2 | 50 / 20 | 0 | Triangle |
| 41 | 3 | 50 / 20 | 0 | Triangle |
| 42 | 0 | 100 / 40 | 0 | Triangle |
| 42 | 1 | 100 / 40 | 0 | Triangle |
| 43 | 0 | 50 / 20 | 0 | Triangle |
| 43 | 1 | 50 / 20 | 0 | Triangle |
| 43 | 2 | 50 / 20 | 0 | Triangle |
| 43 | 3 | 50 / 20 | 0 | Triangle |

| Projectile | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|
| 0 | 400 / 160 | 100 | Circle, Circle [charged] |
| 40 | 400 / 160 | 300 | Up + Circle [charged] |

Actions with a Stamina cost that landed a hit: 29: 50, 89: 400, 90: 300, 94: 300.

## Knockdowns

Hits that knock the target down, from the attack data (2026-11-03). The number is the hit's reaction (attack slot +0x39, projectile row +0xAF); build-up is its "too many hits" number (the target goes down at 150). What each number does and the get-up are in [Knockdowns.md](../balance/Knockdowns.md).

| Row | Move | Hit | Number | What it does | Damage | Build-up |
|---|---|---|---|---|---|---|
| 1 |  | 1 | 21 | launched, then down | 20 | 2 |
| 2 |  | 0 | 10 | knocked down, rolling bounce | 250 | 30 |
| 4 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 200 | 300 |
| 8 |  | 0 | 10 | knocked down, rolling bounce | 250 | 10 |
| 12 |  | 0 | 12 | pierced, then down (dmg_penetration) | 240 | 150 |
| 14 |  | 0 | 6 | knocked away (dmg_blow) | 420 | 400 |
| 15 |  | 2 | 17 | big knock away (dmg_conspicuous_blow) | 100 | 100 |
| 16 |  | 0 | 6 | knocked away (dmg_blow) | 340 | 300 |
| 18 |  | 0 | 10 | knocked down, rolling bounce | 200 | 10 |
| 19 |  | 0 | 6 | knocked away (dmg_blow) | 200 | 10 |
| 22 |  | 0 | 6 | knocked away (dmg_blow) | 200 | 10 |
| 23 |  | 0 | 5 | spun away (dmg_blow_spin) | 200 | 10 |
| 25 |  | 7 | 6 | knocked away (dmg_blow) | 200 | 140 |
| 25 |  | 8 | 6 | knocked away (dmg_blow) | 140 | 140 |
| 26 |  | 0 | 21 | launched, then down | 500 | 800 |
| 28 |  | 1 | 6 | knocked away (dmg_blow) | 100 | 0 |
| 29 |  | 6 | 5 | spun away (dmg_blow_spin) | 200 | 200 |
| 29 |  | 8 | 6 | knocked away (dmg_blow) | 140 | 140 |
| 30 |  | 2 | 6 | knocked away (dmg_blow) | 100 | 70 |
| 30 |  | 7 | 6 | knocked away (dmg_blow) | 200 | 140 |
| 30 |  | 8 | 6 | knocked away (dmg_blow) | 140 | 140 |
| 31 |  | 2 | 6 | knocked away (dmg_blow) | 200 | 70 |
| 31 |  | 7 | 6 | knocked away (dmg_blow) | 200 | 140 |
| 31 |  | 8 | 6 | knocked away (dmg_blow) | 140 | 140 |
| 33 |  | 0 | 19 | knocked away (dmg_kizuna1_hit_blow) | 250 | 500 |
| 34 |  | 0 | 20 | big bounce (dmg_conspicuous_bound 2) | 250 | 300 |
| 36 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 20 | 60 |
| 37 |  | 0 | 6 | knocked away (dmg_blow) | 450 | 60 |
| 38 |  | 2 | 6 | knocked away (dmg_blow) | 150 | 70 |
| 38 |  | 7 | 6 | knocked away (dmg_blow) | 150 | 140 |
| 38 |  | 8 | 6 | knocked away (dmg_blow) | 150 | 140 |
| 39 |  | 0 | 4 | knocked down, big bounce | 500 | 800 |
| 42 |  | 1 | 6 | knocked away (dmg_blow) | 100 | 0 |

| Projectile row | Number | What it does | Damage |
|---|---|---|---|
| 2 | 6 | knocked away (dmg_blow) | 400 |
| 5 | 5 | spun away (dmg_blow_spin) | 2800 |
| 12 | 6 | knocked away (dmg_blow) | 250 |
| 15 | 5 | spun away (dmg_blow_spin) | 200 |
| 17 | 6 | knocked away (dmg_blow) | 250 |
| 20 | 3 | lifted off the feet; lands and goes down unless hit again | 400 |
| 22 | 6 | knocked away (dmg_blow) | 400 |
| 25 | 3 | lifted off the feet; lands and goes down unless hit again | 400 |
| 27 | 6 | knocked away (dmg_blow) | 400 |
| 30 | 6 | knocked away (dmg_blow) | 1100 |
| 32 | 6 | knocked away (dmg_blow) | 300 |
| 35 | 12 | pierced, then down (dmg_penetration) | 400 |
| 40 | 5 | spun away (dmg_blow_spin) | 400 |
| 42 | 6 | knocked away (dmg_blow) | 400 |

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Ultimate Spirit Gun | Press R3 during a Victory Burst. |
| Spirit Shotgun | R1 + Circle (Hold to charge) |
| Spirit Wave | Hold Circle to charge after a Spirit Shotgun. |
| Spirit Gun | Circle (Hold to charge / Possible while moving) |
| Giant Spirit Gun | Hold Circle to charge after a Spirit Gun. |
| Guard Breaking | R1 + Triangle (Press Triangle to perform while jumping) |
| Mazoku Form | Get knocked out during a Victory Burst to turn into Yusuke's Mazoku Form. He'll lose all his health when the Victory Burst is over. |
| Stamina Charge | R1 + Cross Keep holding to continue charging slowly past the Stamina Gauge limit. |
