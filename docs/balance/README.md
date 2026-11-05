# Balance notes

Every character's combat numbers and where they live. Started 2026-10-30, in progress.

| File | What is in it |
|---|---|
| [Shared.md](Shared.md) | Health, Stamina, damage, the Victory Burst gauge and the KO count to win |
| [Knockdowns.md](Knockdowns.md) | The reaction number of each hit (what knocks down), and every fighter's knockdown hits by row |
| [Support.md](Support.md) | Support characters, draft |
| [Gintoki](../character-details/028-gintoki.md) | Character 028: skills linked to rows, projectiles and Stamina from logged sessions |
| [Luffy](../character-details/000-luffy.md) | Character 000, 76 rows. Two play sessions: 42 of 66 rows tied to moves, 11 of 18 Stamina actions, modes split where the log allows |
| [Ace](../character-details/001-ace.md) | Character 001. One play session: 24 of 45 attack rows, 12 of 64 projectile rows, 9 of 14 Stamina actions |
| [Hancock](../character-details/002-hancock.md) | Character 002. One play session: 23 of 37 attack rows, 5 of 23 projectile rows, 9 of 15 Stamina actions |
| [Akainu](../character-details/003-akainu.md) | Character 003, 46 rows, draft |
| [Naruto](../character-details/004-naruto.md) | Character 004, 75 rows, draft |
| [Sasuke](../character-details/005-sasuke.md) | Character 005, 44 rows, draft |
| [Madara](../character-details/006-madara.md) | Character 006, 44 rows, draft |
| [Goku](../character-details/007-goku.md) | Character 007, 72 rows, draft |
| [Frieza](../character-details/008-frieza.md) | Character 008, 37 rows, draft |
| [Vegeta](../character-details/009-vegeta.md) | Character 009, 40 rows, draft |
| [Yusuke](../character-details/010-yusuke.md) | Character 010, 44 rows, draft |
| [Toguro](../character-details/011-toguro.md) | Character 011, 42 rows, draft |
| [Hiei](../character-details/012-hiei.md) | Character 012, 50 rows, draft |
| [Toriko](../character-details/013-toriko.md) | Character 013, 63 rows, draft |
| [Zebra](../character-details/014-zebra.md) | Character 014, 36 rows, draft |
| [Ichigo](../character-details/015-ichigo.md) | Character 015, 56 rows, draft |
| [Aizen](../character-details/016-aizen.md) | Character 016, 35 rows, draft |
| [Gon](../character-details/017-gon.md) | Character 017, 39 rows, draft |
| [Killua](../character-details/018-killua.md) | Character 018, 44 rows, draft |
| [Jotaro](../character-details/019-jotaro.md) | Character 019, 40 rows, draft |
| [Dio](../character-details/020-dio.md) | Character 020, not read yet |
| [Dai](../character-details/021-dai.md) | Character 021, not read yet |
| [Kenshin](../character-details/022-kenshin.md) | Character 022, 60 rows, draft |
| [Shishio](../character-details/023-shishio.md) | Character 023, 50 rows, draft |
| [Seiya](../character-details/024-seiya.md) | Character 024, 53 rows, draft |
| [Kenshiro](../character-details/025-kenshiro.md) | Character 025, 52 rows, draft |
| [Raoh](../character-details/026-raoh.md) | Character 026, 41 rows, draft |
| [Oga & Baby Beel](../character-details/027-tatsumi.md) | Character 027, 46 rows, draft |
| [Medaka](../character-details/029-medaka.md) | Character 029, 46 rows, draft |
| [Ryotsu](../character-details/030-kankichi.md) | Character 030, 33 rows, draft |
| [Arale & Gatchan](../character-details/031-arale.md) | Character 031, 40 rows, draft |
| [Koro-sensei](../character-details/032-korosensei.md) | Character 032, 44 rows, draft |
| [Saiki](../character-details/033-kusuo.md) | Character 033, 39 rows, draft |
| [Momotaro](../character-details/034-momotaro.md) | Character 034, 49 rows, draft |
| [Bo-bobo](../character-details/035-bobobo.md) | Character 035, 49 rows, draft |
| [Tsuna & Reborn](../character-details/036-tsuna.md) | Character 036, 43 rows, draft |
| [Luckyman](../character-details/037-luckyman.md) | Character 037, 46 rows, draft |
| [Taro](../character-details/038-yamada.md) | Character 038, 58 rows, draft |
| [Jonathan](../character-details/039-jonathan.md) | Character 039, 52 rows, draft |
| [Joseph](../character-details/040-joseph.md) | Character 040, 55 rows, draft |
| [Nūbē](../character-details/041-nueno.md) | Character 041, 46 rows, draft |

## How complete this is (2026-10-30 run)

Numbers in the data files are read for every fighter below. "Seen" means a logged hit tied that number to an input in a fight.

| Numbers | In the data | Seen in a fight | Seen |
|---|---|---|---|
| Attack rows with damage | 1686 | 462 | 27% |
| Single hits in those rows | 5438 | 871 | 16% |
| Projectiles with damage | 942 | 92 | 10% |
| Stamina costs | 617 | 162 | 26% |

Why so many are unseen: the script runs the same inputs for every fighter against a dummy that does not fight back. It does not reach Victory Burst attacks, form-only moves, inputs special to one fighter, hits after the first in a string that missed, or moves that need the opponent to act.

## Where the numbers live

Each battle character has `battle_character_NNN_m.pak` in `partition_op_character_ps3.cpk`, compressed on the disc. Inside it:

| File | Layout | Fields |
|---|---|---|
| `NNN_<name>_mp_param_attack_PS3` | 0x10 header, rows of 0x618 bytes, 10 hit slots of 0x9C each | +0x28 damage (32-bit integer), +0x2C recoverable ratio (float), +0x30 gauge gain (first 16 bits) |
| `NNN_<name>_mp_param_eff_bullet_PS3` | 0x10 header, rows of 232 bytes. Some packs hold a second small copy, so use the big one | +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain |
| `NNN_<name>_mp_param_action_PS3` | 0x10 header, rows of 48 bytes in groups of 4 | +0x0C Stamina cost. Action = row / 4 |

All numbers are big-endian. Hit `h` of attack row `i` is at file offset `0x10 + i*0x618 + h*0x9C`.

## How a skill links to its numbers

The game's hit records name the attack slot and action of each hit. In Training Mode the port writes them to `run\balance_log.txt`, as `row`, `hit` and record words. The scripted session and the timeline tool that turn the log into press, hit, row, action and Stamina are not in this repository yet.

## Changing a number

Tested once, on Gintoki, in memory only: `BAL_TEST=<row>,<hit>,<damage>,<gauge>` changes one slot after the first logged hit. Square x3's hit went from 200 to 999 in the same fight, and the gauge followed its new number. This is a test switch (not in this repository) and nothing permanent: a real balance update still needs a patch applied when the file loads, or a rebuilt data file. Neither is built.

## Where the names come from

The game keeps its text as small files loaded into memory. `Skill_NNN_US.txt` holds a skill's name and `SkillManu_NNN_US.txt` its input, in the same order: 21 slots per character, so character `c` uses slots `c*21` to `c*21+20`. `CharacterName_NNN_US.txt` has the display names (support 200 is name 42, and so on).

## Where the code is

- `0x00085B84`: applies damage to a fighter (fighter, damage, recoverable damage).
- `0x00082944`: adds up a fighter's pending hits and calls it.
- `0x00084FF0`: the fighter's message handler, which leads there.

Compressed files: an unpack script (not in this repository yet) reads them. Its output for Gintoki was identical to the copy in the game's memory.

Not yet done: tying the remaining rows to skills (the table above shows how many), support effects and durations, the half-damage rule, form and power-up values, where the dash cost lives, the gauge's memory value and drift, a permanent way to apply balance changes, and J-Customize. Jotaro, Dio and Dai are not on the roster; see docs/notes/PLAYABLE_EXTRA_CHARACTERS.md.
