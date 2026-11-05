# What starts a special (2026-10-31)

How a button press becomes a move, and where to look next time. **Tested** means seen in a running game, and the run is named. **Reading** means taken from the code or files only.

Addresses are guest addresses in BLUS31519 v01.00. The run names (at_a, sw2 and so on) point to logged runs kept in a local work folder. They are not in this repository. The same goes for the test switches (`ACT_TRACE`, `ACT_SWAP`, `FIGHTER_PACK`, `MODEL_BODY`, `PS3_FS_ALIAS_DIR`) and the scripts named below: they belong to the author's local build.

## The chain

| Step | What happens | Where | Status |
|---|---|---|---|
| 1 | The fighter's state machine checks what it can move to from the state it's in | Object with vtable `0x00873C60` holding the `.ocm` file (see below). Handlers run through `0x0004355C` from `0x000474D0` | Reading; the calls were seen in at_a to at_d |
| 2 | Entering a state sends "start this action" | `0x0004296C` takes the state's entry from +0x34 and sends message type 23, request `{0, action, mode}` | Tested: at_c, at_d, sw2 |
| 3 | The request joins a list | `0x0007A4D4` is the message handler, and type 23 goes to `0x00079014`. The list is at +0x70 with 0x90 bytes per item, count at +0x64 | Tested: logged in at_c, at_d |
| 4 | The highest priority request wins | `0x00078EC0` picks the largest float at item +0x0C. `0x00079C40` then runs it through `0x00079AD4` | Reading |
| 5 | The action's data is looked up | `0x0007A880` accepts actions 0 to 0x85 and calls `0x00029C10`. `0x00029E04` looks up a motion in a table of 48-byte rows | Tested for the values passed (at_a); purpose of the 48-byte table is reading |
| 6 | Stamina is spent | `0x0008B64C` -> `0x000877E4` -> `0x00087658` (amount from fighter +0xE4) -> `0x00085E0C` sets the value at fighter +0x9C | Tested: the virtual call inside `0x00087658` logged 50, 100 and 400 units on the matching presses (at_a) |

## Buttons to actions on Jonathan

Tested in at_a and at_c, twice each. Action names are from GAME_MAP.md.

| Press | Action | Name | Stamina seen |
|---|---|---|---|
| Square | 23 | atk_1 | None |
| Circle | 88 | atk_special_1 | 100 |
| R1 + Circle | 90 | atk_special_3 | 400 |
| R1 + Square | 29 | atk_area_1 | 100 |
| R1 + Triangle | 131 | atk_guard_crash | 50 |

## The `.ocm` file: each fighter's move graph

File `NNN_name_PS3.ocm` in `battle_character_NNN_m.pak`, header `OAFB`. Jonathan's is 0x510 bytes and Jotaro's is 0x450. Read it with `ocm2.py` (a local script, not published).

| Offset | Holds |
|---|---|
| +4 | Number of states |
| +8 | States, 8 bytes each: group, index, first link, link count |
| After the states | Link count, then 8-byte links: target state number, a second byte, `FFFF`, a zero word |
| After the links | Count at a variable offset, then one 8-byte pair per state: action number, mode |

- **Tested:** the pair for the state being entered is what reaches the action chooser. Changing Jotaro's pair for action 88 in memory made Circle start other actions (sw2).
- **Tested:** the mode reaches the request (Jonathan's Circle sends mode 2, R1 + Circle sends mode 1; at_c).
- **Reading:** links target other states, because the numbers fit the pair list. For example Jonathan's idle state links to 10 (action 23), 22 (action 90) and 23 (action 131).
- **Not found:** which button each link needs. The links hold no button number, so the condition must be stored somewhere else. **This is the main gap left.**

## What is not the cause

`battle_move_table` is not needed in a running fight. Jonathan's copy was found at 0x4147C580 and zeroed mid-fight, and his Circle, R1 + Circle and R1 + Square kept working with Stamina drops and hits (mv1). It could still be read while loading, and it looks like the in-game Move List (GAME_MAP.md).

## Jotaro and Star Platinum

**His moves use a second actor, as the maintainer suspected.** His pack holds 51 motions named `100_starplatinum_*` and 17 action files in `019_1_aft_act_module`:

- Motions: guard, intro, win, charge, rush4, rush5, wide1, heavy2, heavy3, dash attack, jump attack, `oraora`, `ora_st…` (name cut off in the pack) and `star_finger`.
- Actions: `1_guard`, `1_guard_hit`, `1_guard_break`, `1_appear_intro`, `1_appear_intro_fate`, `1_appear_win`, `1_atk_branch_1`, `1_atk_branch_2`, `1_atk_area_1`, `1_atk_heavy_2`, `1_atk_heavy_3`, `1_atk_dash_1`, `1_atk_jump_3`, `1_kizuna_action_3`, `1_atk_special_1`, `1_atk_special_2`, `1_atk_special_3`.

Its model slot exists but is empty: `019_jotaro_1_01p_PS3.srd` is 0 bytes, like his main model. Other fighters use the same slot with real data, such as Goku at 55,728 bytes and Naruto at 43,792.

Test results with `FIGHTER_PACK=019` on Jonathan's body (sw2, at_b, at_d):

| Action | Started by | Stamina | Hits |
|---|---|---|---|
| 88 atk_special_1 | Circle, without any swap too | 100 | None |
| 89 atk_special_2 | `ACT_SWAP` | 500 | None |
| 90 atk_special_3 | `ACT_SWAP` | 600 | None |
| 44 atk_jump_1 | `ACT_SWAP` | None | 300 twice |

- **Why the specials don't hit:** not known. A stand-in body didn't change it (see the stand-in tests below).
- **The lock:** no forced action started after 44, a jump attack forced on the ground. The later stand-in runs left 44 out.
- **R1 combinations:** R1 + Circle, R1 + Square and R1 + Triangle give action 1 (guard) on his data, and the pair list holds no 131 (at_b, at_d). Why his R1 + Square doesn't reach 29, which is in his list, is unknown.

## Stand-in tests: no Stand appeared

Three runs, all with `FIGHTER_PACK` on a Jonathan body. The same presses each time: R1 + Cross short and long, then actions 88, 89, 90 and 108 through `ACT_SWAP`.

| Run | What was added | Result |
|---|---|---|
| sb1 | A copy of Jonathan's model in the second-actor slots | Packs load, no second body seen |
| sc1 | Kenshiro's model in those slots | Packs load, no second body seen |
| sc3 | Kenshiro, plus Jotaro's main `.ocm` copied into the empty `019_jotaro_1_PS3.ocm` | Packs load, no second body seen |

- In all three, actions 88, 89 and 90 spent 100, about 500 and 600 Stamina and landed no hits. Action 108 (ultimate) spent nothing and landed no hits.
- In the sample of screenshots checked, the fighter stays in its standing pose during the specials. Only some frames were checked, about one per second, so a short movement could be missed.
- **The empty `_1.ocm` is not the cause on its own.** Naruto and Bobobo also have that slot at 0 bytes, and filling Jotaro's changed nothing (sc3).
- **Naruto is the best comparison.** He has a real second body (`004_naruto_1_01p_PS3.srd`, 43,792 bytes), 14 second-actor action files and 69 motions. His `atk_special_1` is 8,084 bytes against Jotaro's 1,168, and no record type unique to Naruto's file appears in Jotaro's (reading, by comparing the top 16 bits of each word against Jonathan's file). So Jotaro's main file may simply never bring the Stand out (inferred).
- `.oam` action files use the same `OAFB` state layout as `.ocm` (reading).
- **Stamina bar:** the value drops and refills in the log (sb1), while the bar under his health looks empty on screen (seen in play and in screenshots). Why isn't known.

**Test files:** model packs `character_model_139_*` and fighter pack `battle_character_119_*` in a local work folder (`sb_alias`), used through `MODEL_BODY=139`, `FIGHTER_PACK=119` and `PS3_FS_ALIAS_DIR`. A pack with a name the archive lacks is read as a loose, uncompressed file. Nothing in the game folder changed. Scripts: `mk_stand2.py`, `mk_pack119.py`.

**Not found:** what brings a second body out. That's the next thing to look for, starting with Naruto's `atk_special_1`.

## Test switches added

All off in normal play. They are part of the author's local build and are not in this repository.

| Switch | What it does |
|---|---|
| `ACT_TRACE=1` | Writes `act_trace.txt`: for 700 ms after each new P1 press, each new indirect call, plus every message to the action chooser |
| `ACT_SWAP=<file>` | When P1 enters the state for action 88, the action number in that file starts instead. It changes the pair in memory, so the swap stays for the rest of the run |
| `zero <ea> <len>` (debug console) | Zeroes a memory range |

## Evidence

Runs, kept in a local work folder that is not part of this repository:

- `mv1`: move table zeroed mid-fight.
- `at_a`, `at_c`: Jonathan, every press logged.
- `at_b`, `at_d`: Jotaro's pack, the same presses.
- `sw1`: first swap attempt, which changed the wrong field, so nothing swapped.
- `sw2`: swap test with screenshots.
- `sb1`, `sc1`, `sc3`: stand-in tests. `sc2` failed to load because the test pack hadn't been built, which was a script error.

Scripts (local, not published): `ocm2.py`, `at_diff2.py`, `sw_read.py`, `stand_scan2.py`, `sw_session.sh`, `sc_session.sh`, `mk_stand2.py`, `mk_pack119.py`, `form_cmp.py`, `oam_cmp.py`, `compare_jo.py`.
