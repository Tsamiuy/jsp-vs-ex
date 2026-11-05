# Arale & Gatchan (character 031)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `031_arale_mp_param_attack_PS3.da` inside `battle_character_031_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 150 / 15 | 150 | 25 |
| 1 | 150 / 15 | 150 | 25 |
| 2 | 250 / 25 | 250 | 50 |
| 3 | 350 / 35 | 350 | 350 |
| 4 | 300 / 30 | 300 | 0 |
| 5 | 300 / 30 | 300 | 0 |
| 6 | 250 / 25 | 250 | 25 |
| 7 | 250 / 25 | 250 | 25 |
| 8 | 300 / 30 | 300 | 0 |
| 9 | 300 / 30 | 300 | 0 |
| 10 | 300 / 30 | 300 | 0 |
| 11 | 300 / 30 | 300 | 0 |
| 12 | 260 / 26 | 260 | 180 |
| 13 | 220 / 22 | 220 | 160 |
| 14 | 500 / 50 | 500 | 500 |
| 15 | 420 / 42 | 420 | 400 |
| 16 | 300 / 30 | 300 | 0 |
| 17 | 300 / 30 | 300 | 0 |
| 18 | 200 / 20 | 200 | 50 |
| 19 | 300 / 30 | 300 | 0 |
| 20 | 300 / 30 | 300 | 0 |
| 21 | 160 / 16 | 160 | 25 |
| 22 | 300 / 30 | 300 | 20 |
| 23 | 300 / 30 | 300 | 0 |
| 24 | 750 / 75 | 750 | 200 |
| 25 | 50 / 5, 50 / 5, 50 / 5 | 150 | 1200 |
| 26 | 250 / 25 | 250 | 350 |
| 27 | 150 / 15 | 150 | 120 |
| 28 | 250 / 25 | 250 | 500 |
| 29 | 450 / 45 | 450 | 750 |
| 30 | 500 / 50 | 500 | 1000 |
| 32 | 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5 | 500 | 1000 |
| 33 | 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5 | 500 | 1000 |
| 34 | 350 / 35, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5 | 800 | 1000 |
| 35 | 1800 / 180, 900 / 90, 900 / 90, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5 | 3950 | 3400 |
| 36 | 600 / 60, 500 / 50, 500 / 50, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5, 50 / 5 | 1950 | 1300 |
| 37 | 450 / 45 | 450 | 200 |
| 38 | 50 / 5, 50 / 5, 50 / 5 | 150 | 1200 |
| 39 | 50 / 5, 50 / 5, 50 / 5 | 150 | 1200 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 0 | 100 / 10 | 200 |
| 1 | 10 / 1 | 0 |
| 5 | 600 / 60 | 0 |
| 6 | 10 / 1 | 0 |
| 7 | 10 / 1 | 0 |
| 10 | 500 / 50 | 0 |
| 11 | 10 / 1 | 0 |
| 12 | 10 / 1 | 0 |
| 15 | 100 / 10 | 200 |
| 16 | 10 / 1 | 0 |
| 20 | 100 / 10 | 200 |
| 21 | 100 / 10 | 0 |
| 22 | 100 / 10 | 0 |
| 23 | 100 / 10 | 0 |
| 24 | 100 / 10 | 0 |
| 25 | 400 / 40 | 200 |
| 26 | 10 / 1 | 0 |
| 30 | 600 / 60 | 200 |
| 31 | 100 / 10 | 0 |
| 32 | 100 / 10 | 0 |
| 33 | 100 / 10 | 0 |
| 34 | 100 / 10 | 0 |
| 35 | 30 / 3 | 200 |
| 36 | 10 / 1 | 0 |
| 40 | 150 / 15 | 200 |
| 41 | 10 / 1 | 0 |
| 45 | 50 / 5 | 200 |
| 46 | 5 / 0.5 | 0 |
| 50 | 50 / 5 | 200 |
| 51 | 50 / 5 | 0 |
| 52 | 50 / 5 | 0 |
| 53 | 50 / 5 | 0 |
| 54 | 50 / 5 | 0 |
| 55 | 100 / 10 | 200 |
| 56 | 10 / 1 | 0 |

## Stamina costs

From `param_action`: +0x0C is the cost, on 48-byte rows in groups of 4. Action = row / 4. Which skill uses each action is only known after a logged session. A negative number has not been tested; it may give Stamina back.

| Action | Stamina |
|---|---|
| 11 | 50 |
| 16 | 100 |
| 29 | 150 |
| 30 | 150 |
| 31 | 50 |
| 32 | 50 |
| 85 | 50 |
| 86 | 50 |
| 87 | 50 |
| 88 | 600 |
| 89 | 400 |
| 90 | 300 |
| 91 | 100 |
| 92 | 150 |
| 106 | 10 |
| 131 | 150 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen 2000. Hits logged 42.

Seen so far: 12 of 39 attack rows, 5 of 35 projectiles, 4 of 16 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 150 / 15 | 25 | Square [charged], Square |
| 1 | 0 | 150 / 15 | 25 | Square, Square [charged] |
| 2 | 0 | 250 / 25 | 50 | Square [charged] |
| 3 | 0 | 350 / 35 | 350 | Triangle, Triangle [charged] |
| 12 | 0 | 260 / 26 | 180 | Triangle, Triangle [charged] |
| 13 | 0 | 220 / 22 | 160 | Triangle |
| 14 | 0 | 500 / 50 | 500 | Triangle |
| 21 | 0 | 160 / 16 | 25 | Triangle |
| 24 | 0 | 750 / 75 | 200 | Up + Circle [charged] |
| 32 | 0 | 50 / 5 | 100 | Circle + R1 [charged] |
| 33 | 0 | 50 / 5 | 100 | Circle + R1 [charged] |
| 38 | 0 | 50 / 5 | 400 | Triangle + R1 [charged] |

| Projectile | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|
| 0 | 100 / 10 | 200 | Circle, Circle [charged] |
| 20 | 100 / 10 | 200 | Circle, Circle [charged] |
| 21 | 100 / 10 | 0 | Circle, Circle [charged] |
| 22 | 100 / 10 | 0 | Circle, Circle [charged] |
| 25 | 400 / 40 | 200 | Circle, Circle [charged] |

Actions with a Stamina cost that landed a hit: 88: 600, 89: 400, 90: 300, 131: 150.

## Knockdowns

Hits that knock the target down, from the attack data (2026-11-03). The number is the hit's reaction (attack slot +0x39, projectile row +0xAF); build-up is its "too many hits" number (the target goes down at 150). What each number does and the get-up are in [Knockdowns.md](../balance/Knockdowns.md). "Hit" is the attack slot, counting slots that deal no damage, so it can be higher than the position in this page's own damage list. Build-up is the "Gauge" value in the tables above; it drains by itself and applies to hits only, not to projectiles. Rows with an empty Move are not tied to an input yet.

| Row | Move | Hit | Number | What it does | Damage | Build-up |
|---|---|---|---|---|---|---|
| 2 |  | 0 | 9 | knocked down, big bounce | 250 | 50 |
| 3 |  | 0 | 4 | knocked down, big bounce | 350 | 350 |
| 4 |  | 0 | 10 | knocked down, rolling bounce | 300 | 0 |
| 6 |  | 0 | 9 | knocked down, big bounce | 250 | 25 |
| 7 |  | 0 | 4 | knocked down, big bounce | 250 | 25 |
| 13 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 220 | 160 |
| 14 |  | 0 | 5 | spun away (dmg_blow_spin) | 500 | 500 |
| 15 |  | 0 | 17 | big knock away (dmg_conspicuous_blow) | 420 | 400 |
| 18 |  | 0 | 6 | knocked away (dmg_blow) | 200 | 50 |
| 21 |  | 0 | 9 | knocked down, big bounce | 160 | 25 |
| 24 |  | 0 | 6 | knocked away (dmg_blow) | 750 | 200 |
| 25 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 50 | 400 |
| 25 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 50 | 400 |
| 25 |  | 2 | 3 | lifted off the feet; lands and goes down unless hit again | 50 | 400 |
| 26 |  | 0 | 19 | knocked away (dmg_kizuna1_hit_blow) | 250 | 350 |
| 28 |  | 0 | 20 | big bounce (dmg_conspicuous_bound 2) | 250 | 500 |
| 30 |  | 0 | 20 | big bounce (dmg_conspicuous_bound 2) | 500 | 1000 |
| 32 |  | 0 | 11 | shocked, then down (dmg_shock) | 50 | 100 |
| 32 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 50 | 100 |
| 32 |  | 2 | 3 | lifted off the feet; lands and goes down unless hit again | 50 | 100 |
| 32 |  | 7 | 5 | spun away (dmg_blow_spin) | 50 | 100 |
| 33 |  | 0 | 11 | shocked, then down (dmg_shock) | 50 | 100 |
| 33 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 50 | 100 |
| 33 |  | 2 | 3 | lifted off the feet; lands and goes down unless hit again | 50 | 100 |
| 34 |  | 0 | 6 | knocked away (dmg_blow) | 350 | 100 |
| 34 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 50 | 100 |
| 34 |  | 2 | 3 | lifted off the feet; lands and goes down unless hit again | 50 | 100 |
| 36 |  | 0 | 5 | spun away (dmg_blow_spin) | 600 | 200 |
| 36 |  | 1 | 5 | spun away (dmg_blow_spin) | 500 | 200 |
| 36 |  | 2 | 5 | spun away (dmg_blow_spin) | 500 | 200 |
| 36 |  | 3 | 5 | spun away (dmg_blow_spin) | 50 | 100 |
| 36 |  | 4 | 5 | spun away (dmg_blow_spin) | 50 | 100 |
| 36 |  | 5 | 5 | spun away (dmg_blow_spin) | 50 | 100 |
| 36 |  | 6 | 5 | spun away (dmg_blow_spin) | 50 | 100 |
| 36 |  | 7 | 5 | spun away (dmg_blow_spin) | 50 | 100 |
| 36 |  | 8 | 5 | spun away (dmg_blow_spin) | 50 | 100 |
| 36 |  | 9 | 5 | spun away (dmg_blow_spin) | 50 | 100 |
| 37 |  | 0 | 6 | knocked away (dmg_blow) | 450 | 200 |
| 38 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 50 | 400 |
| 38 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 50 | 400 |
| 38 |  | 2 | 3 | lifted off the feet; lands and goes down unless hit again | 50 | 400 |
| 39 |  | 0 | 3 | lifted off the feet; lands and goes down unless hit again | 50 | 400 |
| 39 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 50 | 400 |
| 39 |  | 2 | 3 | lifted off the feet; lands and goes down unless hit again | 50 | 400 |

| Projectile row | Number | What it does | Damage |
|---|---|---|---|
| 0 | 4 | knocked down, big bounce | 100 |
| 5 | 3 | lifted off the feet; lands and goes down unless hit again | 600 |
| 6 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 7 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 10 | 3 | lifted off the feet; lands and goes down unless hit again | 500 |
| 11 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 12 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 15 | 10 | knocked down, rolling bounce | 100 |
| 25 | 5 | spun away (dmg_blow_spin) | 400 |
| 30 | 6 | knocked away (dmg_blow) | 600 |
| 40 | 6 | knocked away (dmg_blow) | 150 |
| 55 | 5 | spun away (dmg_blow_spin) | 100 |

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Earth-Breaker | Press R3 during a Victory Burst. |
| Go, Go, Gatchan! | R1 + Circle Let go quickly to make Gatchan follow, and input again to throw him. |
| N'cha Cannon | Circle |
| Kiiin! | Left stick up + Circle Press Triangle during a "Kiiin!" dash to hurl Poop-Boy at the opponent. |
| Area Attack | R1 + Square ,  Square |
| Stamina Charge | R1 + Cross Keep holding to charge beyond the Stamina Gauge limit. |
