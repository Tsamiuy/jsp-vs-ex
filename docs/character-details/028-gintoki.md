# Gintoki Sakata (character 028)

Numbers are read from the game's data. Rows and actions marked as logged come from hit records in three scripted sessions (2026-10-30), where 33 of 33 melee hits matched the table. How to read this: [README.md](../balance/README.md).

Health 4000, Stamina 1000 at the start. A hit is damage / recoverable, in health points.

## Skills

| Skill | Input | Hits | Stamina | Row or projectile | Action | How we know |
|---|---|---|---|---|---|---|
| Weak attack | Square | 100 / 40 | none | Row 0 | 23 | Logged |
| Weak combo 2 | Square, Square | 50 / 20 x3 | none | Row 1 | 24 | Logged |
| Weak combo 3 | Square x3 | 200 / 80, then 40 / 16 x6 | none | Row 2 | 25 | Logged, first hit only |
| Combo ender | Square x4 or x5 | 10 / 4 x8, 20 / 8 | 200 | Row 24 | 90 | Logged, hits 2 to 8 |
| Square, then Triangle | Square x1 or x2, then Triangle | 300 / 120, then 100 / 40 x2, 50 / 20 x4 | none | Row 3 | 26 | Logged, first hit only |
| Strong Attack | Triangle | 400 / 160 | none | Row 12 | 35 | Logged |
| Jump attack | Cross, then Square | 150 / 60 | none | Row 21 | 44 | Logged |
| Dash attack | Dash (R2), then Square | 100 / 40 | dash only | Row 18 | 41 | Logged |
| Area attack | R1 + Square | 300 / 120 x2 | 50 | Projectiles 30, 35 | 29 | Logged |
| Justaway Artisan | Circle | 300 / 120 | 100 | Projectile 25 | 88 | Logged |
| Soy Sauce Surprise | R1 + Circle | 50 / 5 x4, 100 / 200 | 200 | Row 28 | 89 | Logged, hit 2 only |
| Thigh Burn | R1 + Triangle | 400 / 400 | 50 | Row 26 | not logged | Stamina logged, row matched by damage |
| Kamehameha Power! | Left stick up + Circle | 500, 200, 150 x3 (recoverable 0.4 of each), and maybe 800 | 200, then 300 | Projectiles 45, 50 to 53, maybe 54 | 90, then 91 | Inferred from damage and Stamina; my scripted stick input did not trigger it |
| Trespassers Beware! | R3 during a Victory Burst | 200 / 400 x3, 50 / 100 x18, 20 / 16, 200 / 400 | none seen | Rows 29 to 33 | not logged | Matched by damage |
| Second Victory Burst attack, not named | Not clear | 200 / 400, 50 / 100, 50 / 40 x4, 450 / 360 | none seen | Rows 35 to 36 | not logged | Matched by damage |
| Stamina Charge | R1 + Cross, hold | none | Raises Stamina and its limit, up to 2000 | none | not logged | Logged |

Not tied to rows yet: Front, Rear and Sideways Justaway Artisan, I am the Future Sugar King!, and By the Power of...Something!.

## All attack rows

| Row | Skill | Hits (damage / recoverable) | Total damage | Gauge gain |
|---|---|---|---|---|
| 0 | Square | 100 / 40 | 100 | 5 |
| 1 | Square, Square | 50 / 20, 50 / 20, 50 / 20 | 150 | 15 |
| 2 | Square x3 | 200 / 80, 40 / 16, 40 / 16, 40 / 16, 40 / 16, 40 / 16, 40 / 16 | 440 | 38 |
| 3 | Square then Triangle, or Square x2 then Triangle | 300 / 120, 100 / 40, 100 / 40, 50 / 20, 50 / 20, 50 / 20, 50 / 20 | 700 | 310 |
| 4 |  | 45 / 18, 45 / 18, 45 / 18, 45 / 18, 45 / 18, 45 / 18, 45 / 18 | 315 | 20 |
| 5 |  | 300 / 120 | 300 | 0 |
| 6 |  | 300 / 120 | 300 | 20 |
| 7 |  | 300 / 120 | 300 | 0 |
| 8 |  | 300 / 120 | 300 | 0 |
| 9 |  | 300 / 120 | 300 | 0 |
| 10 |  | 300 / 120 | 300 | 0 |
| 11 |  | 300 / 120 | 300 | 0 |
| 12 | Triangle (Strong Attack) | 400 / 160 | 400 | 400 |
| 13 |  | 320 / 128 | 320 | 400 |
| 14 |  | 300 / 120 | 300 | 0 |
| 15 |  | 100 / 40, 100 / 40, 100 / 40 | 300 | 0 |
| 16 |  | 300 / 120 | 300 | 0 |
| 17 |  | 300 / 120 | 300 | 0 |
| 18 | Dash, then Square | 100 / 40 | 100 | 10 |
| 19 |  | 100 / 40 | 100 | 0 |
| 20 |  | 300 / 120 | 300 | 0 |
| 21 | Jump, then Square | 150 / 60 | 150 | 10 |
| 22 |  | 300 / 120 | 300 | 0 |
| 23 |  | 300 / 120 | 300 | 0 |
| 24 | Square x4 or x5 (Stamina 200) | 10 / 4, 10 / 4, 10 / 4, 10 / 4, 10 / 4, 10 / 4, 10 / 4, 10 / 4, 20 / 8 | 100 | 160 |
| 26 | Thigh Burn, matched by damage | 400 / 400 | 400 | 0 |
| 28 | Soy Sauce Surprise (Stamina 200) | 50 / 5, 50 / 5, 50 / 5, 50 / 5, 100 / 200 | 300 | 300 |
| 29 | Trespassers Beware!, matched by damage | 200 / 400 | 200 | 10 |
| 30 | Trespassers Beware!, matched by damage | 200 / 400 | 200 | 10 |
| 31 | Trespassers Beware!, matched by damage | 200 / 400 | 200 | 10 |
| 32 | Trespassers Beware!, matched by damage | 50 / 100, 50 / 100, 50 / 100, 50 / 100, 50 / 100, 50 / 100 | 300 | 10 |
| 33 | Trespassers Beware!, matched by damage | 200 / 400, 20 / 16 | 220 | 10 |
| 35 | Second Victory Burst attack, matched by damage | 200 / 400 | 200 | 10 |
| 36 | Second Victory Burst attack, matched by damage | 50 / 100, 50 / 40, 50 / 40, 50 / 40, 50 / 40, 450 / 360 | 700 | 10 |
| 37 |  | 250 / 200 | 250 | 20 |
| 38 |  | 250 / 200, 100 / 80, 100 / 80, 50 / 40, 50 / 40, 50 / 40, 50 / 40 | 650 | 30 |
| 39 |  | 100 / 80, 100 / 80, 50 / 40, 50 / 40, 50 / 40, 50 / 40 | 400 | 10 |
| 40 |  | 100 / 80, 100 / 80, 50 / 40, 50 / 40, 50 / 40, 50 / 40 | 400 | 10 |
| 41 |  | 150 / 60 | 150 | 120 |
| 42 |  | 10 / 3 | 10 | 0 |
| 43 |  | 100 / 30 | 100 | 0 |

## Projectiles

From `param_eff_bullet`: +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Rows are 232 bytes after a 0x10 header.

| Row | Damage / recoverable | Gauge gain |
|---|---|---|
| 25 | 300 / 120 | 100 |
| 30 | 300 / 120 | 100 |
| 35 | 300 / 120 | 100 |
| 40 | 200 / 80 | 200 |
| 41 | 10 / 4 | 0 |
| 42 | 10 / 4 | 0 |
| 45 | 500 / 200 | 10 |
| 46 | 10 / 4 | 10 |
| 50 | 200 / 80 | 290 |
| 51 | 150 / 60 | 100 |
| 52 | 150 / 60 | 100 |
| 53 | 150 / 60 | 100 |
| 54 | 800 / 320 | 100 |
| 56 | 10 / 4 | 100 |
| 62 | 300 / 120 | 100 |
| 63 | 300 / 120 | 100 |
| 64 | 300 / 120 | 100 |
| 71 | 10 / 4 | 10 |
| 81 | 10 / 4 | 100 |
| 87 | 300 / 120 | 100 |
| 88 | 300 / 120 | 100 |
| 89 | 300 / 120 | 100 |
| 92 | 300 / 120 | 100 |
| 97 | 300 / 120 | 100 |
| 102 | 300 / 120 | 100 |
| 111 | 35 / 14 | 100 |
| 112 | 35 / 14 | 100 |
| 113 | 300 / 120 | 100 |
| 117 | 300 / 120 | 100 |
| 118 | 300 / 120 | 100 |
| 119 | 300 / 120 | 100 |
| 120 | 150 / 60 | 10 |
| 121 | 10 / 4 | 10 |
| 125 | 150 / 60 | 290 |
| 126 | 150 / 60 | 100 |
| 127 | 150 / 60 | 100 |
| 128 | 150 / 60 | 100 |
| 129 | 400 / 160 | 100 |
| 131 | 10 / 4 | 100 |
| 135 | 200 / 80 | 200 |
| 136 | 10 / 4 | 0 |
| 137 | 10 / 4 | 0 |
| 140 | 300 / 120 | 100 |
| 142 | 300 / 120 | 100 |
| 155 | 250 / 100 | 10 |
| 160 | 10 / 4 | 290 |
| 161 | 10 / 4 | 100 |
| 162 | 10 / 4 | 100 |
| 163 | 10 / 4 | 100 |
| 164 | 300 / 120 | 100 |

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
| 90 | 200 |
| 91 | 300 |
| 92 | 100 |
| 93 | 100 |
| 106 | 10 |
| 131 | 50 |

## Logged session (2026-10-30)

Health 4000. Highest Stamina seen at least 1670 (my hold ended before the top). Hits logged 467.

Seen so far: 10 of 41 attack rows, 8 of 50 projectiles, 7 of 17 Stamina actions. The rest have numbers above but no input tied to them yet.

The input is the last press before the hit, so a combo's later hits may show the wrong button. `[charged]` means after holding R1 + Cross.

| Row | Hit | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|---|
| 0 | 0 | 100 / 40 | 5 | Square, Square [charged], Triangle [charged] |
| 1 | 0 | 50 / 20 | 5 | Square, Square [charged] |
| 1 | 1 | 50 / 20 | 5 | Square, Square [charged], Triangle |
| 1 | 2 | 50 / 20 | 5 | Triangle, Square [charged], Square |
| 2 | 0 | 200 / 80 | 20 | Square [charged], Square |
| 3 | 0 | 300 / 120 | 300 | Triangle, Triangle [charged] |
| 12 | 0 | 400 / 160 | 400 | Triangle, Triangle [charged] |
| 18 | 0 | 100 / 40 | 10 | Square, Square [charged], Up + Square |
| 21 | 0 | 150 / 60 | 10 | Square, Square [charged] |
| 24 | 1 | 10 / 4 | 10 | Square |
| 24 | 2 | 10 / 4 | 10 | Square |
| 24 | 3 | 10 / 4 | 10 | Square |
| 24 | 4 | 10 / 4 | 10 | Square |
| 24 | 5 | 10 / 4 | 10 | Square |
| 24 | 6 | 10 / 4 | 10 | Square |
| 24 | 7 | 10 / 4 | 10 | Square |
| 26 | 0 | 400 / 400 | 0 | Triangle + R1, Triangle + R1 [charged] |
| 28 | 1 | 50 / 5 | 50 | Circle + R1, Circle + R1 [charged] |

| Projectile | Damage / recoverable | Gauge | Input seen |
|---|---|---|---|
| 25 | 300 / 120 | 100 | Circle, Circle [charged], Circle + R1 |
| 30 | 300 / 120 | 100 | Square + R1 [charged], Up + Circle [charged], Square + R1 |
| 35 | 300 / 120 | 100 | Square + R1 [charged], Square + R1 |
| 141 | 0 / 0 | 100 | Up + Circle [charged] |
| 155 | 250 / 100 | 10 | Square |
| 160 | 10 / 4 | 290 | Square |
| 161 | 10 / 4 | 100 | Square |
| 162 | 10 / 4 | 100 | Square |
| 163 | 10 / 4 | 100 | Square |

Actions with a Stamina cost that landed a hit: 29: 50, 88: 100, 89: 200, 90: 200, 91: 300, 93: 100, 131: 50.

## Skill table (the game's own list)

Names and inputs as the pause menu's Skill table gives them. The links to rows are in the Skills table at the top.

| Skill | Input |
|---|---|
| Trespassers Beware! | Press R3 during a Victory Burst. |
| Justaway Artisan | Circle (Also possible while moving and jumping) |
| Soy Sauce Surprise | R1 + Circle |
| Kamehameha Power! | Left stick up + Circle (Fires a Kamehameha when successful) |
| Thigh Burn | R1 + Triangle |
| Front Justaway Artisan | Front Justaway Artisan |
| Rear Justaway Artisan | Rear Justaway Artisan |
| Sideways Justaway Artisan | Sideways Justaway Artisan |
| I am the Future Sugar King! | Stand idle for a period of time. |
| By the Power of...Something! | R1 + Cross |
| Strong Attack | Triangle |
| Stamina Charge | R1 + Cross Keep holding to charge beyond the Stamina Gauge limit. |
