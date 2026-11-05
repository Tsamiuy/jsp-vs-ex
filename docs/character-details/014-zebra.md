# Zebra (character 014)

**Draft.** Read straight from the game's data with the layout worked out on Gintoki. Gauge gain is the first number at slot +0x30. Not yet checked in a fight for this character, and rows are not tied to skills yet. The name comes from the data file. How to read this: [README.md](../balance/README.md).

Data file: `014_zebra_mp_param_attack_PS3.da` inside `battle_character_014_m.pak`.

| Row | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|
| 0 | 120 / 36 | 120 | 10 |
| 1 | 120 / 36, 120 / 36 | 240 | 20 |
| 2 | 240 / 72 | 240 | 20 |
| 3 | 280 / 84 | 280 | 140 |
| 4 | 350 / 105, 80 / 24, 80 / 24, 80 / 24, 80 / 24 | 670 | 290 |
| 6 | 200 / 60, 80 / 24, 80 / 24, 40 / 12, 40 / 12 | 440 | 70 |
| 12 | 250 / 75, 180 / 54 | 430 | 200 |
| 13 | 350 / 105, 180 / 54 | 530 | 250 |
| 14 | 350 / 105, 180 / 54 | 530 | 140 |
| 15 | 350 / 105 | 350 | 350 |
| 18 | 300 / 90, 100 / 30 | 400 | 20 |
| 19 | 300 / 90, 100 / 30 | 400 | 20 |
| 22 | 80 / 24, 80 / 24, 80 / 24 | 240 | 15 |
| 23 | 100 / 30 | 100 | 20 |
| 24 | 120 / 36 | 120 | 5 |
| 25 | 250 / 75, 80 / 24, 80 / 24 | 410 | 0 |
| 28 | 600 / 180 | 600 | 0 |
| 29 | 240 / 72, 270 / 81, 270 / 81, 270 / 81, 270 / 81, 270 / 81, 270 / 81, 270 / 81, 270 / 81, 270 / 81 | 2670 | 1000 |
| 30 | 150 / 45 | 150 | 120 |
| 31 | 300 / 90 | 300 | 40 |
| 32 | 250 / 75, 150 / 45 | 400 | 80 |
| 33 | 150 / 60000, 150 / 45 | 300 | 80 |


## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 1 | 50 / 15 | 0 |
| 6 | 50 / 15 | 0 |
| 10 | 250 / 75 | 200 |
| 11 | 10 / 3 | 0 |
| 12 | 10 / 3 | 0 |
| 15 | 250 / 75 | 200 |
| 16 | 10 / 3 | 0 |
| 17 | 10 / 3 | 0 |
| 20 | 250 / 75 | 200 |
| 21 | 10 / 3 | 0 |
| 22 | 10 / 3 | 0 |
| 25 | 250 / 75 | 200 |
| 26 | 200 / 60 | 0 |
| 27 | 10 / 3 | 0 |
| 30 | 300 / 90 | 300 |
| 31 | 10 / 3 | 0 |
| 32 | 10 / 3 | 0 |
| 35 | 300 / 90 | 300 |
| 36 | 10 / 3 | 0 |
| 37 | 10 / 3 | 0 |
| 40 | 300 / 90 | 300 |
| 41 | 10 / 3 | 0 |
| 42 | 10 / 3 | 0 |
| 45 | 300 / 90 | 300 |
| 46 | 10 / 3 | 0 |
| 47 | 10 / 3 | 0 |
| 50 | 100 / 30 | 500 |
| 51 | 10 / 3 | 400 |
| 52 | 10 / 3 | 300 |
| 55 | 250 / 75 | 0 |
| 56 | 10 / 3 | 0 |
| 57 | 10 / 3 | 0 |
| 60 | 250 / 75 | 200 |
| 61 | 10 / 3 | 0 |
| 62 | 10 / 3 | 0 |
| 65 | 250 / 75 | 200 |
| 66 | 10 / 3 | 0 |
| 67 | 10 / 3 | 0 |
| 70 | 250 / 75 | 200 |
| 71 | 10 / 3 | 0 |
| 72 | 10 / 3 | 0 |
| 75 | 250 / 75 | 200 |
| 76 | 10 / 3 | 0 |
| 77 | 10 / 3 | 0 |
| 80 | 50 / 15 | 0 |
| 81 | 10 / 3 | 0 |
| 82 | 10 / 3 | 0 |
| 85 | 50 / 15 | 0 |
| 86 | 10 / 3 | 0 |
| 87 | 10 / 3 | 0 |
| 90 | 350 / 105 | 0 |
| 91 | 10 / 3 | 0 |
| 92 | 10 / 3 | 0 |
| 95 | 300 / 90 | 0 |
| 96 | 300 / 90 | 0 |
| 97 | 300 / 90 | 0 |
| 98 | 300 / 90 | 0 |
| 99 | 300 / 90 | 0 |
| 100 | 300 / 90 | 0 |
| 101 | 300 / 90 | 0 |
| 102 | 300 / 90 | 0 |
| 103 | 300 / 90 | 0 |
| 104 | 300 / 90 | 0 |
| 105 | 300 / 90 | 300 |
| 106 | 300 / 90 | 300 |
| 107 | 300 / 90 | 300 |
| 108 | 300 / 90 | 300 |
| 109 | 300 / 90 | 300 |
| 110 | 20 / 6 | 20 |
| 111 | 1000 / 300 | 450 |
| 115 | 50 / 15 | 0 |
| 116 | 10 / 3 | 0 |

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
| 89 | 300 |
| 90 | 300 |
| 91 | 100 |
| 92 | 100 |
| 106 | 10 |
| 131 | 400 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen 2000. Hits logged 59.

Seen so far: 10 of 22 attack rows, 5 of 72 projectiles, 2 of 16 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 120 / 36 | 10 | Square, Square [charged] |
| 1 | 0 | 120 / 36 | 10 | Square, Square [charged] |
| 1 | 1 | 120 / 36 | 10 | Triangle, Square [charged] |
| 2 | 0 | 240 / 72 | 20 | Square, Square [charged] |
| 3 | 0 | 280 / 84 | 140 | Triangle, Triangle [charged] |
| 6 | 0 | 200 / 60 | 50 | Square + R1, Square + R1 [charged] |
| 12 | 0 | 250 / 75 | 200 | Triangle, Triangle [charged] |
| 13 | 0 | 350 / 105 | 250 | Triangle |
| 22 | 0 | 80 / 24 | 5 | Triangle |
| 23 | 0 | 100 / 30 | 20 | Square, Square [charged] |
| 24 | 0 | 120 / 36 | 5 | Triangle |

| Projectile | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|
| 10 | 250 / 75 | 200 | Circle, Circle [charged] |
| 45 | 300 / 90 | 300 | Circle |
| 55 | 250 / 75 | 0 | Square |
| 60 | 250 / 75 | 200 | Circle |
| 110 | 20 / 6 | 20 | Circle |

Actions with a Stamina cost that landed a hit: 29: 50, 88: 100.

## Knockdowns

Hits that knock the target down, from the attack data (2026-11-03). The number is the hit's reaction (attack slot +0x39, projectile row +0xAF); build-up is its "too many hits" number (the target goes down at 150). What each number does and the get-up are in [Knockdowns.md](../balance/Knockdowns.md). "Hit" is the attack slot, counting slots that deal no damage, so it can be higher than the position in this page's own damage list. Build-up is the "Gauge" value in the tables above; it drains by itself and applies to hits only, not to projectiles. Rows with an empty Move are not tied to an input yet.

| Row | Move | Hit | Number | What it does | Damage | Build-up |
|---|---|---|---|---|---|---|
| 1 |  | 0 | 4 | knocked down, big bounce | 120 | 10 |
| 1 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 120 | 10 |
| 2 |  | 0 | 5 | spun away (dmg_blow_spin) | 240 | 20 |
| 3 |  | 0 | 12 | pierced, then down (dmg_penetration) | 280 | 140 |
| 4 |  | 0 | 5 | spun away (dmg_blow_spin) | 350 | 140 |
| 4 |  | 3 | 17 | big knock away (dmg_conspicuous_blow) | 80 | 50 |
| 6 |  | 0 | 6 | knocked away (dmg_blow) | 200 | 50 |
| 6 |  | 1 | 3 | lifted off the feet; lands and goes down unless hit again | 80 | 10 |
| 6 |  | 2 | 6 | knocked away (dmg_blow) | 80 | 10 |
| 12 |  | 1 | 4 | knocked down, big bounce | 180 | 0 |
| 14 |  | 0 | 6 | knocked away (dmg_blow) | 350 | 140 |
| 14 |  | 1 | 4 | knocked down, big bounce | 180 | 0 |
| 15 |  | 0 | 17 | big knock away (dmg_conspicuous_blow) | 350 | 350 |
| 18 |  | 1 | 10 | knocked down, rolling bounce | 100 | 0 |
| 19 |  | 0 | 10 | knocked down, rolling bounce | 300 | 20 |
| 19 |  | 1 | 10 | knocked down, rolling bounce | 100 | 0 |
| 22 |  | 0 | 9 | knocked down, big bounce | 80 | 5 |
| 22 |  | 1 | 6 | knocked away (dmg_blow) | 80 | 5 |
| 22 |  | 2 | 6 | knocked away (dmg_blow) | 80 | 5 |
| 24 |  | 0 | 9 | knocked down, big bounce | 120 | 5 |
| 25 |  | 0 | 19 | knocked away (dmg_kizuna1_hit_blow) | 250 | 0 |
| 25 |  | 1 | 6 | knocked away (dmg_blow) | 80 | 0 |
| 25 |  | 2 | 6 | knocked away (dmg_blow) | 80 | 0 |
| 28 |  | 0 | 9 | knocked down, big bounce | 600 | 0 |
| 29 |  | 9 | 5 | spun away (dmg_blow_spin) | 270 | 100 |
| 32 |  | 0 | 20 | big bounce (dmg_conspicuous_bound 2) | 250 | 40 |
| 32 |  | 1 | 20 | big bounce (dmg_conspicuous_bound 2) | 150 | 40 |
| 33 |  | 1 | 20 | big bounce (dmg_conspicuous_bound 2) | 150 | 40 |

| Projectile row | Number | What it does | Damage |
|---|---|---|---|
| 1 | 11 | shocked, then down (dmg_shock) | 50 |
| 6 | 11 | shocked, then down (dmg_shock) | 50 |
| 25 | 3 | lifted off the feet; lands and goes down unless hit again | 250 |
| 26 | 3 | lifted off the feet; lands and goes down unless hit again | 200 |
| 45 | 4 | knocked down, big bounce | 300 |
| 46 | 5 | spun away (dmg_blow_spin) | 10 |
| 52 | 5 | spun away (dmg_blow_spin) | 10 |
| 55 | 5 | spun away (dmg_blow_spin) | 250 |
| 60 | 3 | lifted off the feet; lands and goes down unless hit again | 250 |
| 61 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 62 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 65 | 3 | lifted off the feet; lands and goes down unless hit again | 250 |
| 66 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 67 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 70 | 3 | lifted off the feet; lands and goes down unless hit again | 250 |
| 71 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 72 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 75 | 3 | lifted off the feet; lands and goes down unless hit again | 250 |
| 76 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 77 | 3 | lifted off the feet; lands and goes down unless hit again | 10 |
| 85 | 3 | lifted off the feet; lands and goes down unless hit again | 50 |
| 90 | 6 | knocked away (dmg_blow) | 350 |
| 95 | 3 | lifted off the feet; lands and goes down unless hit again | 300 |
| 100 | 3 | lifted off the feet; lands and goes down unless hit again | 300 |
| 105 | 3 | lifted off the feet; lands and goes down unless hit again | 300 |
| 110 | 5 | spun away (dmg_blow_spin) | 20 |
| 111 | 5 | spun away (dmg_blow_spin) | 1000 |
| 115 | 11 | shocked, then down (dmg_shock) | 50 |

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. Which attack row each one uses is not linked yet.

| Skill | Input |
|---|---|
| Meteor Noise | Press R3 during a Victory Burst. |
| Thunder Noise | Left stick up + Circle |
| Voice Missile | Circle (Hold to charge) |
| Wall of Sound | R1 + Circle |
| Death Sound | Death Sound |
| Guard Breaking | R1 + Triangle (Also possible while jumping) |
| Stamina Charge | R1 + Cross Keep holding to charge beyond the Stamina Gauge limit. |
