# Jotaro, Dio, Dai and Asakura Yoh: can they be made playable? (2026-10-30; Yoh added 2026-10-31)

Read-only check of the game's data. **Not tested in the game**: nothing was built, launched or changed.

## Answer

**None of the three has a body model or character textures on the disc, so none can be made playable as itself from the game's own files.** The model files exist but are empty placeholders.

- **Jotaro (019)** is the only one with real fighter work left: his own motions, attack table and effects. He could fight using another fighter's body, as a placeholder. Looking like Jotaro would need a new model from outside the game.
- **Dio (020) and Dai (021)** are empty templates: no body motions, no real skills. They could only ever be a copy of another fighter under a new name.

Labels: **measured** = read from the files; **inferred** = my reading.

## What is on the disc (measured)

Sizes are unpacked bytes. Jonathan (039) is a normal fighter, for comparison.

| Data | Jotaro 019 | Dio 020 | Dai 021 | Jonathan 039 |
|---|---|---|---|---|
| Body and texture model, `character_model_NNN_m/_i/_v` | **empty** (all `.srd`/`.srdi`/`.srdv` are size 0) | **empty** | **empty** | `.srd` 460,896, `.srdi` 1,869,420, `.srdv` 3,780,976 |
| Story mode model, `character_model_mainmode_NNN` | empty, except one 22,608-byte motion file | empty, `_m` file missing | empty, `_m` file missing | full |
| Body motions, `.san` set in `battle_character_NNN_m` | **137 files, 2.79 MB**, plus a second set of 51 files, 0.98 MB | **none** | **none** | 158 files, 3.09 MB |
| Attack table, `mp_param_attack` | 62,416 bytes, his own | 40,336 bytes, same bytes as Dai | 40,336 bytes, same bytes as Dio | 81,136 bytes |
| Action table, `mp_param_action` | 25,744 bytes | same bytes as Jotaro | same bytes as Jotaro | its own |
| Projectiles, `mp_param_eff_bullet` | 1,176 bytes | none | none | 2,336 bytes |
| `battle_move_table` | **empty** | empty | empty | 1,936 bytes |
| Voice and sound, `stream_voice`, `battle_character_sound` | **empty** | empty | empty | 11,916 bytes; 812,896 bytes |
| Blast effects, `op_ef_blast` | 175,476 bytes | 38,820 bytes | 22,072 bytes | 407,656 bytes |
| Skill pack, `battle_character_assist_skill` | **missing** | missing | missing | present |
| Name and skill text (US) | **empty** | empty | empty | present |
| Arcade picture, gallery background | empty | empty | empty | present |
| Name image, `common_chara_name` | 2,688 bytes | 1,408 bytes | 1,504 bytes | present (Killua 018: 2,800 bytes). Pictures not looked at |

### Searched everywhere else (measured)
- All 18,958 packs in the character, progress and battle archives were unpacked and searched for files named jotaro, dio or dai.
  - **No model file with data turned up.** All hits with data are the three's own packs above, plus Dio-themed reaction motions on other fighters.
- Map and sound archives: file names only, not unpacked. Nothing for these three (the 019-021 map hits are stage objects).
- `game` and `patch` archives: **not checked**. They stay on the disc and are not in the install folder.

### Clues that they were cut late (inferred)
- Jotaro's motions move bones named `OPTION_CAP`, `OPTION_POCKET_L/R` and `CAPE_Z_01`, so they were made for his model.
- Other fighters carry `04_08_dio_damage_loop` and `dmg_dio_blood_suck`, so Dio had a grab planned.
- Momotaro's model has a material named `jotaro_acc_C`.

## What making Jotaro playable would need

| Step | State | Note |
|---|---|---|
| A body | **Missing** | Point 019's model load at another fighter's pack, or add a new model. His stand-pose motion shares 65 bones with each of the 5 fighters I compared with (Jonathan, Joseph, Killua, Luffy, Kenshiro), so a borrowed body should move. The 5 cap and cape bones would have nothing to move (inferred, untested) |
| Roster | **Tables found, code not traced** | See below |
| Shop entry | Data file | See below |
| Character select name, icon | Missing | A test run showed `なし` in the slot when forcing him. Name text 19 is empty, which likely explains it (inferred) |
| Skill names and inputs | Missing | Text slots 399-419 are empty |
| `battle_move_table` | **Missing** | 1,936 bytes on normal fighters; its use is unknown. Could be copied from Joseph or Jonathan, but it may not match his actions |
| Skill pack | Missing | Whether a fight needs it is unknown |
| Voice | Missing | Would be silent, or borrow another fighter's |
| Gauge gain | All 0 in his attack rows | Victory Burst would not charge from his hits until set (docs/character-details/019-jotaro.md) |
| Balance | Rough | 40 rows against 50-odd on Jonathan and Joseph |

Dio or Dai would need all of the above plus motions and an attack table, so in practice a copy of a whole fighter.

### The tables (measured)
- **Shop:** `progress_scene_jp_shop_ALL_m.pak`, `jp_shop_buy_ALL_m.pak/00_battle_character_jp_shop_PS3`. Count 39 at the start, then a 16-byte header and rows of 24 bytes: id, name text id (0x580000 + id), second text id (0x5C0000 + id), price (2500, or 0 for free), 0, flags 0x10000. No rows for 19, 20 or 21. Supports are in `01_support_character_jp_shop_PS3`, 13 rows.
- **Roster lists in the game's program:** three copies of the same list, 39 fighters then 13 supports, as 32-bit ids, at `0x782630`, `0x7827E8` and `0x7829A0`. 19, 20, 21 and support 208 are skipped.
- **Id and group list:** `0x757108`, 8-byte rows of (id, a value 0-2), with 19-21 and 208 skipped, ending in 500. It may be the series or grouping (inferred).
- **Id and number list:** `0x764FE4`, 8-byte rows of (id, a number), 39 rows, ending in -1. Meaning unknown.

The code that reads these lists is **not traced**, so where their lengths are stored is unknown. Adding a row needs a longer list or a replaced one. Character select's layout (`progress_scene_character_select`) was not worked out either, so a 40th slot may need new screen art or a swap with an existing slot.

## Cheapest first test (needs the game, so after a test run)
1. `TRAINING_FORCE=19 ...` (a local test switch, not in this repository) in Training. The forced pick already gets him to the slot. **Not yet seen:** does a fight start, and does the game cope with an empty model?
2. If the fight stops at the empty model, the port's file layer could serve another fighter's `character_model_NNN_*` for 019, with no disc change. That tells us quickly whether his motions and attacks work on a borrowed body.
3. Only if that looks good: name, skills, gauge gain, then a proper roster slot.

## Tested: Jotaro's data on Jonathan's body (2026-10-31)

**Works in a fight, though only his basic attacks fired.** Local test switch `FIGHTER_PACK=019`; a local launcher script started the game with it. Neither is in this repository. Every fighter then loads Jotaro's `battle_character_019_m/_v/_i` packs on their own model, while name, sound and loading tips stay their own. Picking Jonathan gives the combination asked for. Scripted run jo4.

- Forcing fighter 019 itself does not work: its loading tips are missing, so the load stops on a white screen. Stand-in tips crashed during loading, so fighter 019 stays unplayable this way.
- **Attacks that landed match Jotaro's table exactly:** rows 0, 1, 2 at 300 / 240, row 12 at 360 / 288, row 21 at 300 / 240 twice. Ten hits in all, in the first half of the run.
- **Gauge gain is 0 on all of them,** so his hits do not move the Victory Burst gauge. This matches his table.
- **Special moves did not land:** no logged hit or Stamina drop from Circle, R1 + Circle, stick up + Circle, R1 + Square, R1 + Triangle or the charged set. His move table is empty, which may be why (inferred). Not proven: the dummy may have been out of reach for some.
- **The HUD portrait shows Luffy** while the name says Jonathan, so the pack's art looks like a placeholder (inferred).
- **What his pack holds for skills (measured):** 3 special action files with data (1,168, 1,108 and 552 bytes) plus an ultimate (1,824), the other 17 specials empty. 3 real skill effect files (50 KB, 22 KB, 85 KB), the rest placeholders; Jonathan has 4 real ones. 2 of 79 effect and ultimate texture files have data, against 11 for Jonathan, and his ultimate art files are empty.
- **Jonathan's move table probably would not give him Jonathan's moves (inferred).** Jonathan's moves live in Jonathan's action files, motions and attack table, none of which this would bring. The table's columns hold Skill table ids, and its exact use is not worked out. His own table file is size 0, so testing it needs a pack rebuild or a load hook.
- Whether the motions look right was only checked in still screenshots. The cap and cape bones have nothing to move on this body (inferred).

**Correction, 2026-10-31:** the empty move table is not why his specials stay silent. Zeroing Jonathan's table mid-fight changed nothing (mv1). Circle does start action 88 on his data and spends 100 Stamina; the earlier "no Stamina drop" missed it. His moves use a second actor, Star Platinum: 51 motions and 17 action files, but its model `019_jotaro_1_01p_PS3.srd` is 0 bytes. A stand-in model in that slot (a copy of Jonathan, then Kenshiro) loaded, but no Stand appeared, and what brings a second body out is still unknown. Details in [WHAT_STARTS_A_SPECIAL.md](WHAT_STARTS_A_SPECIAL.md).

## Asakura Yoh (support 208), added 2026-10-31

**Same answer as Dio and Dai: an empty template, no model.** Read-only, not tested in the game. Yoh would be a support character, not a fighter. Jaguar (207) and Kuroko (209) are normal supports, for comparison. Sizes are unpacked bytes.

| Data | Yoh 208 | Jaguar 207 | Kuroko 209 |
|---|---|---|---|
| Body and texture model, `character_model_NNN_m/_i/_v` | **empty** (`.srd`, `.srdi`, `.srdv` all size 0) | `.srd` 373,728, `.srdi` 847,800, `.srdv` 4,020,208 | `.srd` 277,904, `.srdi` 1,100,242, `.srdv` 4,063,984 |
| Story mode model | empty, `_m` file missing | full | full |
| Voice and sound, `battle_character_sound` | **empty** | 112,480 bytes | 122,720 bytes |
| Attack table, `mp_param_attack` | 40,336 bytes, **same bytes as Dio and Dai** | 4,696 bytes | 1,576 bytes |
| Action table, `mp_param_action` | 25,744 bytes, same bytes as Jotaro, Dio and Dai | 22,864 bytes | 22,864 bytes |
| Move table, `support_move_table` | **none** | 256 bytes | 256 bytes |
| Projectiles, `mp_param_eff_bullet` | none | 6,976 bytes | 6,976 bytes |
| Skill pack, `battle_character_assist_skill` | **missing** | present | present |
| Name text (US) | **empty** | Jaguar | Kuroko |
| Character icon, `_rgb8_256_chara_ico` | **missing** | present | present |
| Gallery background, story pictures | empty; story pictures missing | present | present |
| Name image, `common_chara_name` | 3,568 bytes | 2,208 bytes | 2,384 bytes |

- His attack and action tables match the template files (the MD5 hashes in `y1.py`, and the Dio/Dai/Jotaro run in `t8.py`), and his attack table has the fighter size, not a support's. So they are not his own data (inferred).
- His effect textures, `.srdv` in `battle_character_assist_208_v`, are 665,096 bytes: the same size as Dai's. Probably template too (inferred; bytes not compared).
- **Searched everywhere:** the same 18,958 packs, for asakura, yoh, shaman or `208_`. All hits with data are his own packs above, or false matches: Raoh's `gosyoha` motions, and text files numbered 208 that belong to other lists. No model data. Map and sound lists have nothing for him. Game and patch archives not checked.
- **Rosters:** absent from the three program lists, the id and group list (`0x757108`) and the shop's 13 support rows, like Dio and Dai.
- The name image was not looked at, so I can't say what it shows.

So Yoh could only be a copy of another support under a new name. There are no motions, moves or voice of his to restore.

## My recommendation
- Worth one test run for Jotaro on a borrowed body. It is cheap and answers whether his cut moveset works.
- Dio and Dai are not worth doing as "restored" characters: nothing of theirs exists beyond small effect files.
- A proper Jotaro needs a real model, and that is a separate modelling job.

## Evidence
Scripts, kept in a local work folder that is not part of this repository:
- `t4.py`: pack contents and empty counts.
- `t8.py`, `t13.py`: parameter files and hashes, motion groups.
- `t12.py`: bone names.
- `scan.py`, `scan_out.txt`: the search across 18,958 packs.
- `t15.py`: map and sound file lists.
- `t17.py`: shop tables.
- `t18.py`, `t19.py`: the program's roster lists.
- `t10.py`: name and skill text, from a text dump of the game (`us_text.json`, not in this repository).

Yoh: `y1.py` (packs, hashes, text), `scan_yoh.py`, `scan_yoh.txt` (the search).

Unpacked with `docs/archive/jstars-notes/tools/cmp_unpack.py`.
