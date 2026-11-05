# Shared by all characters

Stored values are x100 in the fighter object: 4000 health is 400000 in memory. Measured on Gintoki and Raoh (2026-10-30).

## Fields in a fighter object

The object's address changes every fight. The port finds it from the health getter `0x00085CEC`.

| Offset | What | Gintoki |
|---|---|---|
| +0x90 | Current health | 4000 |
| +0x94 | Recoverable health limit | 4000 |
| +0x98 | Max health | 4000 |
| +0x9C | Stamina | 1000 at the start |
| +0xA0 | Stamina limit, raised by charging | 1000 to 2000 |
| +0xA4 | Base Stamina | 1000 |

The fight data says other characters have other max health (3500, 4000 and 4500 have been seen).

## Stamina

- Each skill's cost is in the character's `param_action` file, at +0x0C of its 48-byte row. It matched every logged press: 50, 100, 200 and 300.
- Charging (R1 + Cross held) raises Stamina and its limit, up to 2000 seen, at about 170 per second.
- Dash (R2) costs about 50 at the start, then about 30 per second while held. That number was measured, and its place in the data is not found.
- Stamina recovers by itself when not spending. The rate is not measured yet.

## Damage

- Applied by `0x00085B84` (fighter, damage, recoverable damage).
- Recoverable damage = damage x the hit's ratio. With damage changed to 999 and ratio 0.4, the game applied 399.
- Each pending hit is a 0xB0-byte record at fighter owner +0x230, count at +0x22C. Fields: +0x64 damage, +0x68 recoverable, +0x6C gauge gain, +0x98 attack slot pointer, +0x9C projectile row pointer, +0xA0 action row pointer.

## Victory Burst gauge

- There is one gauge, shared by both teams. It moves toward the team doing better, and only that team can activate it (the maintainer, 2026-10-30).
- Each hit carries a gauge number: the first 16 bits at attack slot +0x30, or +0xA4 in a projectile row.
- Proof: in a test fight, that number on Square x3's hit was raised from 20 to 300. The marker moved far faster than usual and the R3 prompt appeared after about 7 strings.
- Full is inferred to be near 2000 in those units: the prompt was not up at about 1905 and was up at about 2225. The prompt was gone in the next screenshot and back in the one after. the maintainer saw why: when the gauge is full, a yellow bar with the R3 prompt appears over it for a few seconds, drains in about 4 or 5 chunks, then the normal gauge returns. the maintainer also reports the gauge drifts back to the middle when a team stops attacking. The marker did not move during a 10 second pause at a lower level , so that drift's rate and timing are not measured.
- Whether the opponent's hits push it back by the same numbers is inferred, because the dummy never attacked.
- The value itself is not found in memory. Only the marker's screen position was seen moving with it.

## KO count to win

The match ends when a team's KO count reaches its limit, 3 by default. Tested 2026-10-31 on red's side, Gintoki vs Koro-sensei.

| What | Where | Value |
|---|---|---|
| Red's KOs scored | `0x490238E0` | 0, 1, 2 with each KO |
| Red's limit | `0x490238E4` | 3 |
| Blue's KOs scored | `0x49023918` | Inferred: same layout, not seen moving |
| Blue's limit | `0x4902391C` | 3 |
| Second copy, red's count and limit | `0x492D1F1C`, `0x492D1F20` | 0, 1, 2 and 3 |
| Possible second copy, blue's limit | `0x492D2034` | 3, inferred |

- The first pairs live in objects with vtable `0x00876558`: +0x24 is the count and +0x28 the limit. The two objects sit 0x38 apart.
- `0x000C7624` looks like the check: it compares +0x24 (plus +0x2C if asked) with +0x28. Inferred from the code, not traced in a fight.
- These addresses held across the Gintoki vs Koro-sensei fights. They may move with other setups, so a real change should find the objects by vtable.

Proof, with `KO_TEST_MAX` writing both red limits:

| Limit | Result | Evidence |
|---|---|---|
| 5 | Still fighting after 4 KOs, Results after the 5th | Screenshots |
| 2 | Results after the 2nd KO | Screenshots |
| 3 (unchanged) | Results after the 3rd KO | Screenshots |

The KO bars follow the limit: the maintainer saw red show 2 bars at limit 2 while blue kept 3, because the test only changed red. Which of the two red copies controls the match, and which only the bars, is not separated yet. Nothing permanent is built: `KO_TEST_MAX` is a test switch, off in normal play.

## Not found yet

- Why some hits do half damage: 25 / 10 for 50 / 20. Whole strings can be halved. Stamina level does not explain it, and the cause is unknown. It also halved the test damage: a 9999 hit logged as 4999 in a test fight.
- Where the dash's Stamina cost is stored.

## Reading hits in a log

One logged hit can be two hit records applied in the same frame, so its damage can look like double the table's value. The records' own numbers at +0x64 add up to the logged damage (Luffy 8 of 877 hits, Ace 32 of 291, 2026-10-31). Half-damage hits are different: the record itself already holds the halved number.

A character's Just Guard appears as a motion started with action 21, not as a state, and Ace's has its own attack rows.
