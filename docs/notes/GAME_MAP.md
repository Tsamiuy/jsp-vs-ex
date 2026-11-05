# Game map: what is where in J-Stars (first version, 2026-10-31)

One index of the game's data, triggers and code. It points to the detailed notes and doesn't replace them.

**Status of each entry:**
- **tested** = seen in the running game (a logged run or a play session by the maintainer). The evidence is named.
- **reading** = from reading files or code only, not seen in the game. Treat it as a good guess until tested.

**To add to this file:** put one line in the right table with its status and evidence. When something is tested, change "reading" to "tested" and name the run. Addresses are guest addresses in BLUS31519 v01.00. Heap addresses (0x4xxxxxxx) can move with the setup.

**Not in this repository:** the test switches named here (`FIGHTER_PACK`, `MODEL_BODY`, `TRAINING_FORCE`, `ACT_SWAP`, `PS3_FS_ALIAS_DIR`), the helper scripts under `tools/balance/` (for example `act_read.py`), the short run names (jo4, sw2, at_a and so on) and the notes `TEST_FIGHT.md` and `NEW_MENU.md` belong to the author's local game build and work folders. They are named so the evidence can be followed. The run names point to logged runs that are not published.

## 1. Data files

### Archives and packs
| What | Details | Status |
|---|---|---|
| Archives | `USRDIR\data\PS3\partition_op_{battle,character,map,progress,sound}_ps3.cpk`, plus `game` and `patch`, which stay on the disc. Contents table is XOR-masked @UTF | tested: the game loads them; GAME_INTERNALS.md |
| Compression | `$CMP` file: `$CL0`, `$CLH`, `$CH0`, `$CR0` chunks. `jstars-notes/tools/cmp_unpack.py` unpacks it | tested: Gintoki's pack byte-identical to game memory (FINDINGS "Balance work") |
| Pack | `STPK`: count at +8, then 48-byte entries from +16: offset, size, name at +0x10 (32 characters, longer names are cut). Packs nest. Entries of size 0 are placeholders | reading: every pack unpacks |
| `_m` / `_v` / `_i` | Main data / textures and graphics data / index data, judging by contents | reading |
| Language | `_US_`, `_FR_`, `_NS_` for text and art; `_ALL_` for everything else | reading |
| Numbers | All big-endian. Stored health and Stamina are x100 in memory | tested: docs/balance/Shared.md |

### Fighter: `character/battle_character/battle_character_NNN_m.pak`
Parameter files have a 16-byte header with the row count first, except where noted. Row sizes hold across all 42 fighter packs (`g1.py`).

| File | Rows | Fields known | Status |
|---|---|---|---|
| `mp_param_attack` | 0x618 bytes: 10 hit slots of 0x9C | Slot +0x28 damage (int), +0x2C recoverable ratio (float), +0x30 gauge gain (first 16 bits). Hit h of row i = 0x10 + i*0x618 + h*0x9C | tested: Gintoki 33 of 33 melee hits; gauge in fight bt1; `BAL_TEST` changed a hit from 200 to 999. Other fighters reading |
| `mp_param_eff_bullet` | 232 bytes | +0x9C damage, +0xA0 recoverable ratio, +0xA4 gauge gain. Some packs have a second small `_t` file with 36-byte rows | tested on Gintoki (docs/character-details/028-gintoki.md); others reading |
| `mp_param_action` | 48 bytes, 4 rows per action, 134 actions | +0x0C Stamina cost. Action = row / 4 | tested: costs 50, 100, 200, 300 matched presses (docs/balance/Shared.md) |
| `NNN_name_PS3.ocm` | `OAFB`: states, links between them, one action + mode pair per state | The fighter's move graph. Changing a pair in memory changes the move started | tested: sw2. Layout in WHAT_STARTS_A_SPECIAL.md |
| `mp_param_move` | 28 bytes | Not known | reading (size only) |
| `mp_param_action_voice` | 8 bytes, one per action (134) | Not known; the name suggests a voice per action | reading |
| `mp_param_sound` | 20 bytes | Not known | reading (size only) |
| `mp_param_xl_cam` | 100 bytes | Not known; the name suggests cameras | reading (size only) |
| `mp_param_anime_id_off` | 36 bytes | Not known | reading (size only) |
| `mp_param_form_effect` | 28 bytes, 7 fighters | Not known | reading (size only) |
| `mp_param_camera` (44 bytes), `mp_param_assist_perf` (80 bytes) | single block, no row count | Not known | reading |
| `MP_INNEN_NNN` | 28 bytes | "Innen" means a rivalry, so maybe special pairings | reading (size and name only) |
| `NNN_stream_voice` | 50 bytes | Not known | reading (size only) |
| `NNN_battle_move_table` | 48 bytes, 40 rows, 4 groups per row | The in-game Move List. Each group is a type pair plus two ids. Type 1/2 = `BSkill` text (0 Weak attack, 6 Dash, 9 Jump, 12 Victory Burst). Type 3/4 = `Skill` text (Gintoki 0x24D = Justaway Artisan, 0x257 = Stamina Charge; 0x492 = Guarding) | reading: ids match the names. See section 2 |
| Action files `.oam` in `NNN_aft_act_module` | `OAFB` format, 134 slots | One file per action; see the action table below. Plays motions through records starting `SSSSffff`, where SSSS is the motion slot | tested: order matches logged actions. Motion link reading |
| Motions in `NNN_character_animation_san` | 367 slots | `.san` motions, names like `02_00_rush1_in`. Group 00 stand and pose, 01 movement, 02 attacks, 03 and 04 damage, 30 skills, 31 ultimate start | reading (UNUSED_CONTENT.md) |
| Second actor files `NNN_1_*` | Action files, motions and a model slot `NNN_name_1_01p` | Used by Naruto and Bobobo with data; Jotaro's motions are named `100_starplatinum`. What brings the second body out isn't found | reading; stand-in models load but no body appeared (tested: sb1, sc1, sc3) |
| `.fei`, `.feff`, `.fsnd` | Same 367 slots | Face, effect and sound timing per motion, judging by the names | reading |
| `op_ef_basic`, `op_ef_equip`, `op_ef_blast` | | Effects: `.efd2r` and `.srd`. `skill_NNN_NN` per skill | reading |
| `battle_character_img_NNN` | | Battle icons: `chara_NNN.sfl`, `kizuna_chara`, `stamina` | reading |

**Action numbers (the order of the `.oam` files, same for every fighter):**

| Action | Name | Status |
|---|---|---|
| 0-7 | stand, guard, guard_hit, guard_break, turn, appear_intro, appear_intro_fate, appear_win | reading |
| 8-22 | jump, jump_geppo, run, **11 dash**, dash_razor, dash_fly, in_dash_crash(2), **16 step**, step_back/left/right, step_razor, **21 just_guard**, free_fall | reading; dash cost 50 agrees with docs/balance/Shared.md's measured dash start |
| 23-28 | atk_1, 2, 3, atk_branch_1, 2, 3 | tested: Gintoki logged 23, 24, 25, 26 |
| 29-34 | atk_area_1, 2, 3, atk_area_branch_1, 2, 3 | tested: Gintoki logged 29 |
| 35-40 | atk_heavy_1, 2, 3, atk_heavy_branch_1, 2, 3 | tested: Gintoki logged 35 |
| 41-46 | atk_dash_1, 2, 3, atk_jump_1, 2, 3 | tested: Gintoki logged 41, 44 |
| 47-81 | dmg_* reactions, including dio_blood_suck, seiya_fall, gurenkaina, spear_hit | reading |
| 82-87 | provoke, burning_heart, kizuna_action_finish, kizuna_action_1, 2, 3 | reading |
| 88-107 | atk_special_1 to 20 | tested: Gintoki logged 88 Justaway Artisan, 89 Soy Sauce Surprise, 90 his combo ender |
| 108 | atk_ultimate | reading |
| 109-128 | assist_1 to 20 | reading |
| 129-133 | burst_voltage, dmg_down_forward_rear, atk_guard_crash, dmg_middle_front, dmg_middle_rear | reading |

Action 21 holding -150 Stamina on 7 fighters (docs/balance notes) therefore looks like a refund for just guard (reading).

### Other character files
| File | Holds | Status |
|---|---|---|
| `character/model/character_model_NNN_{m,i,v}` | Body: `NNN_name_01p_PS3.srd`, `_shader`, `_ability`; second model `_1_01p` on some; facial presets; colour effects | reading |
| `character/main_mode_model/character_model_mainmode_NNN` | Model for J-Adventure | reading |
| `character/battle_character_sound/jp/battle_character_sound_NNN_jp_m` | `.acb` voices and more | reading |
| `sound/JP/CV_NNN_NN_ST_JP`, `PV_NNN_NN_ST_JP`, `BGM_JP`, `Z_VOS_ST_JP` | Voices and music | reading; music plays in the game |
| `battle/ultimate_blast/ultimate_blast_NNN` | Ultimate art | reading |
| Text | `Skill_N` and `SkillManu_N`: 21 slots per fighter, slot = c*21 + i. `CharacterName_N`: support s is name s-200+42. `BSkill`, `USkill`, `SSkill`, `MapName`, `BGMALL`, `BGMLMT`, `Trophy`, `Quiz` | tested: Raoh's skill screen shows slot 26's names (sk1) |

### Support: `character/battle_character_assist/NNN/battle_character_assist_NNN_{m,i,v}`
| File | Holds | Status |
|---|---|---|
| Parameter files | Same names as a fighter's. `param_action` has 22,864 bytes on real supports | reading; **support damage tables hold leftover rows, so they don't prove damage**: Neuro lists 240 and shows no hit (FINDINGS 10-31 cast run) |
| `NNN_support_move_table` | 256 bytes | reading (size only) |
| `battle_character_assist_skill_NNN_NN_{m,i,v}` | 21 packs. On supports only `_01` holds data: 11 shared motions plus the skill motions | reading (UNUSED_CONTENT.md section 6) |
| Action files | 44-45 with data, `assist_1` is the skill. It doesn't use the fighter's motion records | reading |

Fighters also have an assist pack (same art) and skill packs.

### Stage: `map/battle_map/mapNN_*`
Stage ids follow `MapName`: 00 Namek, 01 Alabasta, 02 Hidden Leaf, 03 Vegetable Sky, 04 Kameari, 05 Athena, 06 Soul Society, 07 Inferno Crags, 08 Dark Tournament, 09 Tenraikyousintojo, 10 Penguin Village, 11 Yoshiwara (reading). Stage select's order is different; STAGES.md has it.

| File | Holds | Status |
|---|---|---|
| `mapNN_m`, `_v`, `_i` | Model `mapNN.srd`, `_shader`, `_texture`, `_lightmap_texture`, `_locator_obj`; breakable `_knock_*` with `.san`; animated parts | reading |
| `mapNN_2p_*` | Split-screen versions | reading |
| `mapNN_param_m` | `_param.dat` (248 bytes), `_astar.nod` (paths), `_break.bkd`, `_light.ld`, `_locator.srd`, `_positioning.srd`, `_ON_SE.acb`, `_sct.eop`, `.feff` per part | reading |
| `battle/map_ui`, `map_ui_2p` | Stage UI | reading |
| `progress/map_img`, `online/map_ico`, `mainmode/tex_mapimg`, `tex_maplogo` | Stage pictures | reading |
| `map/battle_map_gimmick/map11_*` | Extra parts, only for stage 11 | reading |
| `map99` | Probable developer test stage (UNUSED_CONTENT.md section 7) | reading |

### Tables in the game program and shop
| What | Where | Status |
|---|---|---|
| Fighter + support list | `0x782630`, `0x7827E8`, `0x7829A0`: 39 + 13 ids, skipping 19, 20, 21, 208 | reading; code not traced |
| Id with value 0-2 | `0x757108`, 8-byte rows | reading; meaning unknown |
| Id with number | `0x764FE4`, 8-byte rows | reading; meaning unknown |
| Cards | 276: 4 per character for 60 characters, plus 3 J-Adventure cards for 12 of them. Names in `F_CardName_US.pak`, effects in `F_CustomCombi`. Save: `SYS-DATA` 0x49810, one bit per card row, lowest bit first. The file has a checksum in its first 4 bytes | reading, save and list tested: cd3 to cd5, ms3. See CARDS.md |
| Shop rows | `jp_shop_buy_ALL_m.pak/00_battle_character_jp_shop_PS3` (39) and `01_support_...` (13). 24 bytes: id, name text, second text, price, 0, flags | reading; purchase itself tested in the game |

## 2. What triggers what, and what needs what

| Trigger or need | Chain | Status |
|---|---|---|
| A hit lands | Fighter message handler `func_00084FF0` -> `func_00082ED4` -> `func_00082944` adds pending records -> `0x00085B84` (fighter, damage, recoverable) -> KO check | tested: FINDINGS "Balance work" |
| Hit numbers | Record +0x98 attack slot, +0x9C projectile row, +0xA0 action row. Gauge from slot +0x30 or row +0xA4 | tested: `BAL_TEST`, bt1 |
| A special starts | State machine reads `.ocm` -> `0x0004296C` sends message 23 `{0, action, mode}` -> `0x0007A4D4` -> `0x00079014` queues it -> `0x00078EC0` picks by priority -> Stamina via `0x000877E4`, `0x00087658`, `0x00085E0C`. Which button each `.ocm` link needs isn't found | tested: at_a to at_d, sw2, except the priority pick (reading); **button condition not found**. See WHAT_STARTS_A_SPECIAL.md |
| Move List screen | `battle_move_table` + `Skill` and `BSkill` text | reading. Zeroing it mid-fight didn't stop specials (tested: mv1). Jotaro's specials start without it (tested: sw2) |
| Stamina | Spent from `param_action` +0x0C; fighter +0x9C current, +0xA0 limit, +0xA4 base | tested |
| Match end | KO count +0x24 reaches limit +0x28 in objects with vtable `0x00876558`; `KO_TEST_MAX` limit 2 and 5 moved the end | tested: kom2, kom5. Check function `0x000C7624` reading |
| Picks | Setup records `0x009540E4 + 0x2C*slot`: +0x00 driver, +0x08 state, +0x0C character, +0x10 team, +0x18 COM level. Read when leaving character select | tested: cs6, mv28b |
| CPU or human input | Each fighter's pad object: +0x14 slot, +0x18 driver (1 human, 3 CPU). The AI fills a CPU pad | tested: pr2, dm1, dm2 |
| Scene | `0x0094DEC4`: 0/1/2 boot and title, 4 menus, 3 after a fight, 0x13/0x14 fight, 0x10 seen in Training. `fight_hud_up` is more reliable | tested: md, tw |
| Scene word is not a switch | Writing `0x0094DEC4` is accepted but skips nothing, so it only reports progress. Order into a Training fight: 1, 2, 3, 4, then 3, 4, 5, 7, 9, B, D, B, C, D, E, F, 10 | tested: af9, af10, af11 |
| P1 can move | First state P1 enters through `0x0004296C` (state +4 = 0) | tested: af12 to af17 |
| Action log | `act` and `mot` lines in `balance_log.txt` in Training; `tools/balance/act_read.py` prints them with the buttons, Stamina and hits | tested: af18. See TEST_FIGHT.md (not published) |
| Menu position | `0x0095654C` screen, `0x00956550` cursor | tested: exit runs |
| Background job | Work -> `func_00577934` -> `func_00592428` -> `cellSpursJobGuardNotify` -> SPU job 80FD00 wakes a fiber | tested: FINDINGS section 6 |

**What a fighter needs to load:**

| Need | File | Status |
|---|---|---|
| Fight data | `battle_character_NNN_{m,v,i}` + `_US_{m,v}` | tested: `FIGHTER_PACK=019` (a local test switch, not in this repository) loads another fighter's set on a body (jo4) |
| Body | `character_model_NNN_{m,i,v}` | tested: `MODEL_BODY` (a local test switch, not in this repository); empty files give no body |
| **Loading tips** | `loading_game_tips_chara_%03d_%03d`, `_title_chara_`, `_mes_chara_` | **tested: missing tips stop the load on a white screen for 019; stand-in tips crash during loading** (PLAYABLE_EXTRA_CHARACTERS.md) |
| Name and art | `common_chara_name_NNN`, `title_chara_NNN`, `_rgb8_256_chara_ico_NNN`, `progress_online_chara_ico_NNN`, `progress_scene_arcade_chara_NNN`, gallery background | reading: program names them. Whether each is required is unknown |
| Voices | `battle_character_sound_NNN_jp`, `CV_`, `PV_` | reading; whether required is unknown |
| Ultimate | `ultimate_blast_NNN`, `battle_character_assist_skill_NNN_NN` | reading |
| Roster slot | Program lists above. An id can be forced past them | tested: `TRAINING_FORCE`; a forced 019 shows `なし` |
| Heart pieces and mode UI | `heart_piece_add_load`, `battle_ui_mode` | reading |

**What a stage needs:** `mapNN_{m,v,i}`, `mapNN_param_m`, `map_ui_NN`; `_2p_` versions in split screen (reading). Stages load and play (tested: STAGES.md).

**Boot:** BootCheck -> DataCheck and ContentPermit -> SPURS, job chains, fibers -> CRI and archive binds -> logos -> first-boot install -> trophies -> save load -> movie (tested: GAME_INTERNALS.md).

## 3. Main functions

Names here are ours. See `port\recompiled` for the code.

### Combat
| Function | What | Status |
|---|---|---|
| `0x00085B84` | Apply damage (fighter, damage, recoverable) | tested |
| `0x00085AE4`, `0x00085B54` | Clamp health values | tested (Training notes) |
| `func_00082944` | Add up pending hit records | tested |
| `func_00082ED4` | Between message handler and the above | tested |
| `func_00084FF0` | Fighter message handler | tested |
| `0x00085CEC` | Health getter; the HUD reads every fighter through it | tested |
| `0x00085CC4`, `0x00085EAC` | Stat getters (field / 100, rounded up) | tested |
| `0x0007A4D4` | Action chooser's messages: +8 type, +0x14 request | tested: logged by the port's action probe, meaning partly known |
| Fighter buttons | Pad object +0x204 held, +0x20C pressed, +0x210 released. `0x0009D214` reads them directly, so answering the getters doesn't start attacks. Square 23 `atk_1`, Triangle 35 `atk_heavy_1`, Circle 88 or 89, R1 guard, R2 dash | tested: tb1, tb2, tb6 |
| Sub-lists on the main menu | `main_menu2.sfl`, 6 rows. `func_001E6E98` sets rows and label frames by main menu row; widget per row from menu +0x54, 0x14 each; saved cursors at `0x8E63DC` | tested: tm1b, fb2 |
| Fighter place | x, y, z, 1, copy 0x10 later. 0x70 before the health object on a 0-star opponent, up to about 0xB000 before it on a CPU, by fighter | tested: rn1, cc3 |
| Stage select +0x6C | The stage: 0 Namek, 1 Hidden Leaf, 6 Temple of Athena, 11 Yoshiwara. Not map ids. +0x2E4 is the list's copy on a first visit and a pointer on a later one | tested: st1, st2, pos1, st5 |
| Battle HUD parts | vtables `0x00879A48`-`0x0087A3xx`. Slot 0x10 updates, slot 0x14 draws, +0x14 state, 0 until in. Minimap `0x00879FA0`, timer `0x0087A160`, top bars `0x0087A2E0`, middle arc `0x0087A1E0` or `0x0087A260`, health bar `0x00879EA0` | tested: hd4 to hd7 |
| Support motions | Bank in `battle_character_assist_skill_NNN_01`, in file order: 0 `ap_intro` onward. `stand` has no data | tested: su7, su10 for Rukia's 3 |
| Fighter camera, vtable `0x00874DF8` | +0x10 view matrix, +0x50 inverse, +0x90 position, +0xA0 the fighter's place. `0x0006FF10` steers it each frame | tested: cam3 to cam6. See TRAINING_MODE.md |
| Stage select seconds, screen vtable `0x0087FCF8` +0x78 | 300, or 0 for Unlimited. `0x0026FAA0` copies it to the battle settings +0x18 (getter `0x00270A9C`) | tested: tm4, tm7. See TRAINING_MODE.md |
| Stage select slide, time box vtable `0x00880018`: +0x28, then +4, then +0x1C | Pixels right of its place, 732 to 0. Box +0x2C is its state (1 entering, 2 settled, 5 leaving, 0 done) | tested: watch log tm4, screenshots tm7 |
| Pause menu object, vtable `0x00876710` | 37 data tables in a pack; a row's next opens a sub-table (0x54 or less), goes back (94) or hands off (93 Skill table). `0x0049A490` opens, `0x00498D48` finds a table, `0x0049A284` acts on a row | tested: pj1. See TRAINING_MODE.md |
| `0x0004296C` | Action state; +0x34 entry | tested: `ACT_SWAP` probe. See WHAT_STARTS_A_SPECIAL.md |
| `0x000C7624` | KO count check | reading |
| `func_00063018` | Returns the object the HUD asks for HP | tested |

### Screens (vtables are how the port recognises them)
| Function or vtable | What | Status |
|---|---|---|
| `0x001DE098`, vtable `0x0087D600` | Main menu list and 1P/2P | tested |
| `0x0019AA74`, vtable `0x0087C3D0` | Character select | tested |
| `0x0019E2E8`, vtable `0x0087C4A0` | Its settings panel | tested |
| `0x0026EF94`, vtable `0x0087FCF8` | Stage select | tested |
| `0x00279F0C`, vtable `0x00880018` | Time limit box | tested |
| `func_00543138`, `func_000120E0` | Build and reset the developers' debug menu window | reading: a real lead for the debug menu in UNUSED_CONTENT.md section 7 |

### Files and jobs
| Function | What | Status |
|---|---|---|
| `func_0052F374` | Main thread's loader wait | tested |
| `func_0052EE60` | Loader state machine (11 states) | tested |
| `func_0052E594` | File server main loop | tested |
| `func_0060B41C` -> `func_005381B0` -> `func_003F0520` | Partition manager: disc or HDD per archive | tested |
| `func_00577934`, `func_00592428` | Give a job to a worker | tested |
| `func_005778C0`, `func_0057787C` | Fiber body and CPU half of a job | tested |
| `func_005784EC`, `func_00536C04` | Job server loop and its kick | tested |
| `func_005382EC`, `func_005383D4` | Job table lookup and insert | tested |

More: GAME_INTERNALS.md (threads, CRI chains), docs/balance/README.md and Shared.md (combat), TRAINING_MODE.md and FINDINGS.md (screens), NEW_MENU.md (menu rects, not published).

## 4. Known gaps, most useful first
1. **Which button each `.ocm` link needs.** The graph and the request chain are found (WHAT_STARTS_A_SPECIAL.md); the links hold no button number.
2. What `param_move`, `param_sound`, `MP_INNEN`, `stream_voice` hold.
3. Which of the name and art files a fighter really needs: test one at a time with stand-in files.
4. The code that reads the roster lists.
5. Why some hits do half damage (docs/balance/Shared.md). On Luffy the hit record already holds the halved number at +0x64 and +0x68 (tested: a play session on 10-31).
6. Character select's slot layout.

## Evidence for this version
New checks (read-only) are small scripts in a local work folder that is not part of this repository:
- `g1.py`: row sizes across all fighter packs, action names and Stamina.
- `g2.py`, `g3.py`: Gintoki's move table with text ids.
- `all_names.txt`: every file in the five installed archives.

Everything marked tested comes from runs recorded in the notes named above. No game was run for this file.

## Battle gauges and partners (11-02)

- Tested: class `0x00879DA0`, six instances 0x480 apart, draws the name and health bar over characters; the second is P1's partner, the fourth the opponent (pd4). Slot 0x14 is its draw.
- Tested: a partner's health object and camera sit between P1's and the opponent's by address (pc2).
- Tested: a fighter pack built from 039 with a Support's art and its `ap_intro_wait` motion in the `stand` slot loads as a real fighter in the partner slot (pc4).
- Reading: `p_enemy_gauge_pl` is loaded at `0x116680`, in that class's non-virtual setup function at `0x11609C`.

## Battle setup and fighters (11-05)
Short index only; which of these were read from the code and which were checked in a run is in FINDINGS "Battle setup table and 6 fighters".
- Character select table 0x009540E4 (6 x 0x2C: 4 fighters + 2 Supports; menu only).
- Battle world *(TOC-0x7A90): team unit lists +0x34, 6 unit records x 0x94 at +0x60 (+0x08 role 1/2/3).
- Battle builder func_000ED344 (from func_000EDFDC); fighter func_0005AEE0, Support func_0005AB70; health object
  func_0008A540 (class 0x00875198). Details: FINDINGS "Where the battle builds its fighters".
