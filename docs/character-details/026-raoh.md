# Raoh (character 026)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `026_raoh_mp_param_attack_PS3.dat` inside `battle_character_026_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 180 / 72 | 180 | 25 |
| 1 | 180 / 72 | 180 | 25 |
| 2 | 250 / 100 | 250 | 50 |
| 3 | 400 / 160, 350 / 140 | 750 | 320 |
| 4 | 340 / 136 | 340 | 200 |
| 5 | 300 / 120 | 300 | 0 |
| 6 | 200 / 80 | 200 | 20 |
| 7 | 300 / 120 | 300 | 0 |
| 8 | 300 / 120 | 300 | 0 |
| 9 | 300 / 120 | 300 | 0 |
| 10 | 300 / 120 | 300 | 0 |
| 11 | 300 / 120 | 300 | 0 |
| 12 | 300 / 120 | 300 | 300 |
| 13 | 200 / 80, 200 / 80 | 400 | 400 |
| 14 | 500 / 200 | 500 | 500 |
| 15 | 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 50 / 20, 70 / 28 | 520 | 500 |
| 16 | 300 / 120 | 300 | 0 |
| 17 | 300 / 120 | 300 | 0 |
| 18 | 200 / 80 | 200 | 20 |
| 19 | 300 / 120 | 300 | 0 |
| 20 | 300 / 120 | 300 | 0 |
| 21 | 200 / 80 | 200 | 20 |
| 22 | 200 / 80 | 200 | 20 |
| 23 | 300 / 120 | 300 | 0 |
| 24 | 600 / 240, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 720 | 30 |
| 25 | 300 / 120, 300 / 120, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 700 | 25 |
| 26 | 300 / 120, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 420 | 30 |
| 27 | 300 / 120, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 420 | 30 |
| 28 | 750 / 300, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 870 | 780 |
| 29 | 750 / 300, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 870 | 780 |
| 30 | 50 / 20, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 170 | 105 |
| 31 | 1300 / 520, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 1420 | 730 |
| 32 | 150 / 60 | 150 | 120 |
| 33 | 250 / 100 | 250 | 20 |
| 34 | 250 / 100, 100 / 40, 100 / 40, 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 650 | 30 |
| 35 | 50 / 20, 1 / 0.4, 1 / 0.4, 1 / 0.4, 1 / 0.4, 1 / 0.4 | 55 | 10 |
| 36 | 50 / 20, 50 / 20, 70 / 28 | 170 | 30 |
| 37 | 50 / 20, 70 / 28, 70 / 28 | 190 | 30 |
| 38 | 50 / 20, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 170 | 80 |
| 39 | 400 / 160, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 520 | 80 |
| 40 | 400 / 160, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8, 20 / 8 | 520 | 80 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 0 | 750 / 300 | 750 |
| 1 | 750 / 300 | 750 |
| 5 | 450 / 180 | 0 |
| 6 | 10 / 4 | 0 |
| 10 | 1450 / 580 | 2000 |
| 11 | 1450 / 580 | 0 |
| 15 | 300 / 120 | 0 |
| 20 | 150 / 60 | 750 |
| 21 | 150 / 60 | 0 |
| 22 | 150 / 60 | 0 |
| 23 | 150 / 60 | 0 |
| 24 | 150 / 60 | 0 |
| 26 | 200 / 0 | 0 |

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
| 89 | 200 |
| 90 | 500 |
| 106 | 10 |
| 131 | 200 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen 2000. Hits logged 49.

Seen so far: 14 of 41 attack rows, 2 of 13 projectiles, 4 of 14 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 180 / 72 | 25 | Square, Square [charged] |
| 1 | 0 | 180 / 72 | 25 | Triangle, Square [charged] |
| 2 | 0 | 250 / 100 | 50 | Square [charged] |
| 3 | 1 | 350 / 140 | 300 | Triangle, Triangle [charged] |
| 6 | 0 | 200 / 80 | 20 | Square + R1, Square + R1 [charged] |
| 12 | 0 | 300 / 120 | 300 | Triangle, Triangle [charged] |
| 13 | 0 | 200 / 80 | 200 | Triangle |
| 13 | 1 | 200 / 80 | 200 | Triangle |
| 22 | 0 | 200 / 80 | 20 | Triangle |
| 24 | 0 | 600 / 240 | 0 | Circle + R1, Circle + R1 [charged] |
| 25 | 0 | 300 / 120 | 0 | Circle + R1, Circle + R1 [charged] |
| 26 | 0 | 300 / 120 | 0 | Triangle + R1, Triangle + R1 [charged] |
| 27 | 0 | 300 / 120 | 0 | Triangle + R1, Triangle + R1 [charged] |
| 30 | 0 | 50 / 20 | 75 | Up + Circle, Up + Circle [charged], ? |
| 31 | 0 | 1300 / 520 | 700 | Cross, ? [charged] |

| Projectile | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|
| 0 | 750 / 300 | 750 | Circle |
| 26 | 200 / 0 | 0 | Cross + R1 |

Actions with a Stamina cost that landed a hit: 29: 50, 88: 500, 90: 500, 131: 200.

## Knockdowns

Hits that knock the target down, from the attack data (2026-11-03). The number is the hit's reaction (attack slot +0x39, projectile row +0xAF); build-up is its "too many hits" number (the target goes down at 150). What each number does and the get-up are in [Knockdowns.md](../balance/Knockdowns.md). "Hit" is the attack slot, counting slots that deal no damage, so it can be higher than the position in this page's own damage list. Build-up is the "Gauge" value in the tables above; it drains by itself and applies to hits only, not to projectiles. Rows with an empty Move are not tied to an input yet.

| Row | Move | Hit | Number | What it does | Damage | Build-up |
|---|---|---|---|---|---|---|
| 1 |  | 0 | 21 | launched, then down | 180 | 25 |
| 2 |  | 0 | 5 | spun away (dmg_blow_spin) | 250 | 50 |
| 3 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 350 | 300 |
| 4 |  | 0 | 17 | big knock away (dmg_conspicuous_blow) | 340 | 200 |
| 6 |  | 0 | 6 | knocked away (dmg_blow) | 200 | 20 |
| 12 |  | 0 | 12 | pierced, then down (dmg_penetration) | 300 | 300 |
| 13 |  | 0 | 21 | launched, then down | 200 | 200 |
| 14 |  | 0 | 10 | knocked down, rolling bounce | 500 | 500 |
| 15 |  | 4 | 12 | pierced, then down (dmg_penetration) | 50 | 50 |
| 15 |  | 7 | 12 | pierced, then down (dmg_penetration) | 50 | 50 |
| 15 |  | 8 | 9 | knocked down, big bounce | 50 | 50 |
| 15 |  | 9 | 5 | spun away (dmg_blow_spin) | 70 | 50 |
| 18 |  | 0 | 12 | pierced, then down (dmg_penetration) | 200 | 20 |
| 22 |  | 0 | 10 | knocked down, rolling bounce | 200 | 20 |
| 25 |  | 0 | 9 | knocked down, big bounce | 300 | 0 |
| 25 |  | 1 | 9 | knocked down, big bounce | 300 | 0 |
| 27 |  | 0 | 6 | knocked away (dmg_blow) | 300 | 0 |
| 28 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 750 | 750 |
| 29 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 750 | 750 |
| 30 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 50 | 75 |
| 31 |  | 0 | 6 | knocked away (dmg_blow) | 1300 | 700 |
| 33 |  | 0 | 19 | knocked away (dmg_kizuna1_hit_blow) | 250 | 20 |
| 34 |  | 0 | 20 | big bounce (dmg_conspicuous_bound 2) | 250 | 20 |
| 37 |  | 0 | 6 | knocked away (dmg_blow) | 50 | 10 |
| 38 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 50 | 50 |
| 39 |  | 0 | 6 | knocked away (dmg_blow) | 400 | 50 |
| 40 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 400 | 50 |

| Projectile row | Number | What it does | Damage |
|---|---|---|---|
| 0 | 5 | spun away (dmg_blow_spin) | 750 |
| 1 | 5 | spun away (dmg_blow_spin) | 750 |
| 5 | 5 | spun away (dmg_blow_spin) | 450 |
| 10 | 5 | spun away (dmg_blow_spin) | 1450 |
| 11 | 5 | spun away (dmg_blow_spin) | 1450 |
| 20 | 5 | spun away (dmg_blow_spin) | 150 |
| 21 | 5 | spun away (dmg_blow_spin) | 150 |
| 26 | 4 | knocked down, big bounce | 200 |

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Heaven's Command Charge | Press R3 during a Victory Burst. |
| Fighting Spirit Discharge | Press Left stick up + Circle to ready the attack, and Circle to activate. |
| Iron Palm Wave | Circle (Also possible while jumping) |
| Dawning Blood Sorrow | R1 + Circle |
| Stamina Charge | R1 + Cross Keep holding to charge beyond the Stamina Gauge limit. |
