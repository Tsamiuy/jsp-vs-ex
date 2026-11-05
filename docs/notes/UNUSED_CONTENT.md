# Unused content: fighters, supports and the rest of the game data (2026-10-31)

Read-only check of the 39 working fighters' data. **Not tested in the game**: nothing was built, launched or changed.

## Answer

**I found no complete hidden move.** Every move's motion set has an action file pointing to at least one of its motions, apart from the two groups in section 2 (the 31_30 start motions and the gallery poses).

What's left are single motions that nothing points to, and most of those look like variants the game picks by itself. The real open question is whether any *action file* is never triggered. That needs the game running; see section 5.

Labels: **measured** = read from the files; **inferred** = my reading.

## 1. How I checked (measured)

- Each fighter's `NNN_character_animation_san` pack has 367 numbered slots. Filled slots hold `.san` motions.
- Each action file (`.oam`, starting `OAFB`) plays motions through records beginning `SSSSffff`, where SSSS is the slot number. For example Jonathan's `atk_1` plays slots 82, 83, 84, which are `rush1_in`, `rush1_hit`, `rush1_out`.
- A motion counts as "pointed to" if any of that fighter's action files holds its slot number in that form. This errs towards "used": a stray matching number would hide a truly unused motion.

## 2. Result (measured)

**6,601 motions in the main motion packs of the 39 fighters; 489 (7.4%) have no action file pointing to them.**

| Kind | Motions | What it probably is (inferred) |
|---|---|---|
| Shared motions: fly versions of dash and step, win poses, intro waits, `31_30_start` | 181 | Played by the game's own code or chained from the motion before. Most fighters have the same gaps |
| Skill motions named up or down | 162 | Aiming a shot up or down. The game likely picks these by angle |
| Throw variants: `throw_tatsu`, `throw_gin`, `throw_koro` | 75 | A throw version for particular targets, probably chosen by who is thrown. Which targets is a guess (Tatsumi, Gintoki, Koro-sensei?) |
| `gallery_stand` | 6 | Probably a Gallery pose: Naruto, Ichigo, Seiya, Oga, Saiki, Taro |
| Other skill motions | 65 | Listed in section 3. Many have names cut short by the file format, so some are probably aim variants too |

## 3. The 65 other motions (measured; meaning unknown)

File names are cut at 32 characters, so several names below end early. These are the best leads if you want to look for cut content.

| Fighter | Motion group and name | Motions | Size |
|---|---|---|---|
| 000 Luffy | `30_05` jet_rush_ | 1 | 10 KB |
| 002 Hancock | `30_00` pistol_kiss | 6 | 83 KB |
| 002 Hancock | `30_01` pistol_kiss | 6 | 85 KB |
| 002 Hancock | `30_03` slave_arrow | 1 | 18 KB |
| 008 Frieza | `30_00` daichi_retsu | 4 | 65 KB |
| 010 Yusuke | `30_00` reigan_run_l | 2 | 23 KB |
| 010 Yusuke | `30_00` reigan_run_b | 1 | 12 KB |
| 010 Yusuke | `30_00` reigan_run_r | 1 | 11 KB |
| 011 Toguro | `30_03` throw_loop | 1 | 29 KB |
| 013 Toriko | `30_00` kugi_step_ri | 1 | 7 KB |
| 014 Zebra | `30_00` voice_missile | 13 | 201 KB |
| 022 Kenshin | `30_01` hirameki_ef | 1 | 4 KB |
| 024 Seiya | `30_02` rolling_crash | 6 | 120 KB |
| 026 Raoh | `30_01` shinkessyu_thr | 3 | 73 KB |
| 028 Gintoki | `30_02` kameha_succ | 6 | 94 KB |
| 031 Arale & Gatchan | `30_00` ncha_fly_in | 1 | 10 KB |
| 031 Arale & Gatchan | `30_00` ncha_fly_fire | 1 | 8 KB |
| 031 Arale & Gatchan | `30_00` ncha_fly_loop | 1 | 10 KB |
| 031 Arale & Gatchan | `30_00` ncha_fly_out | 1 | 10 KB |
| 032 Koro-sensei | `30_00` mach20_o | 1 | 33 KB |
| 035 Bo-bobo | `30_04` normal_guard | 1 | 31 KB |
| 036 Tsuna & Reborn | `30_00` xburner_fly_i | 1 | 17 KB |
| 039 Jonathan | `30_04` zoom_punch | 2 | 27 KB |
| 040 Joseph | `30_04` rush4_in | 1 | 12 KB |
| 040 Joseph | `30_06` ult_run_out | 1 | 6 KB |
| 041 Nūbē | `30_02` reibakujyu_su | 1 | 13 KB |

The most interesting to look at first (inferred from the names only):
- **Yusuke `reigan_run`**: 4 motions, looks like running while aiming.
- **Arale `ncha_fly`**: 4 motions, looks like a mid-air version.
- **Gintoki `kameha_succ`**: 6 motions, maybe the "success" outcomes of his Kamehameha.
- **Raoh `shinkessyu_thr`**: 3 motions, more throw parts.
- **Joseph `rush4_in`**: 1 motion.

## 4. What this can't tell us
- **Whether an action file is ever triggered.** Files such as `atk_heavy_3` or `atk_special_N` have motions, but what starts them is in tables I haven't decoded (`battle_move_table`, cancel data). Having more action files than skill names is not proof of anything: Raoh has 7 special files and 5 names, but his extras look like the steps of one move (`touki_in`, `touki_loop`, `touki_run`).
- **Whether a motion plays without a slot record.** The game's own code, or a motion chained from the one before, could play it.
- **Other packs.** I counted only each fighter's main motion pack; the smaller packs of second forms were read but not listed here. The separate support-skill packs were not checked.
- **The Dio motions.** All 39 fighters have `04_08_dio_damage_loop` and a `dmg_dio_blood_suck` action, and Dio isn't in the game, so these probably never play (inferred).
- **Second models.** Many fighters have a second `_1_01p` model. They look like forms or props, but I didn't check that each is used.

## 5. How to settle it (needs the game, so after a test run)
Add one line to the Training log that records each fighter's action number, then run a scripted move session over the cast. Comparing that list with the action files shows which are never reached by those inputs. Then any candidate can be forced with a test switch to see what it looks like.

## Evidence
Scripts, kept in a local work folder that is not part of this repository:
- `u4.py`: the slot-number link.
- `u8.py`, `u10.py`: motion groups and the records that point to them.
- `u12.py`, `u13.py`: the counts and the table above.
- `u9.py`: special files against skill names, second models.

## 6. The 13 working supports, added 2026-10-31

**No hidden skill found: each support has exactly one skill pack with data.** Read-only, not tested in the game.

- **Skill packs (measured):** every support has 21 numbered skill packs, `battle_character_assist_skill_NNN_00` to `_20`. Only `_01` holds data; the other 20 are empty placeholders.
- **Motions (measured):** a support's motions are all in that one pack: 11 shared ones (intro, jump, charge and so on) plus its skill motions, listed below.
- **Unlike the fighters, I could not link motions to action files.** A support's one real action, `assist_1`, has no motion-slot records in the form fighters use, and I did not decode what it uses instead. So I can't count what is "pointed to"; this is only a list of what exists.

Motions are grouped by name, with the group's motion count and size. Names are cut at 32 characters.

| Support | Skill motions | Groups |
|---|---|---|
| 200 Neuro | 3 | blind (3, 60 KB) |
| 201 Kumagawa | 9 | neji (4, 55 KB), neji_ryoute_ (4, 52 KB), gallery_stan (1, 20 KB) |
| 202 Rukia | 4 | shirayuki (2, 63 KB), shirayuki_fir (1, 21 KB), shirayuki_loo (1, 38 KB) |
| 203 Chitoge | 3 | wait (1, 30 KB), punch (2, 38 KB) |
| 204 Hisoka | 3 | bungy (3, 55 KB) |
| 205 Edajima | 3 | wait (1, 29 KB), voice (2, 46 KB) |
| 206 Kagura & Sadaharu | 4 | dash (4, 79 KB) |
| 207 Jaguar | 4 | ramen (4, 58 KB) |
| 209 Kuroko | 9 | dribble (3, 44 KB), ignite (3, 42 KB), ignite_r (3, 42 KB) |
| 210 SKET-dan | 11 | idle (2, 71 KB), shot (4, 100 KB), combo (4, 109 KB), ap_intro_hi (1, 20 KB) |
| 211 Lala | 11 | run (3, 64 KB), ouen (3, 207 KB), normal_run (1, 28 KB), normal_ouen (1, 44 KB), normal_ouen_lo (1, 144 KB), normal_ouen_ou (1, 17 KB), gallery_stand_ (1, 23 KB) |
| 212 Allen | 7 | edge (6, 107 KB), turn (1, 17 KB) |
| 213 Hinata | 6 | dash (1, 16 KB), jump (1, 12 KB), jump_up (1, 8 KB), spike (3, 35 KB) |

Worth a look (inferred from the names only):
- **Kumagawa `neji_ryoute`**: "ryoute" means both hands, so maybe a two-handed version of his screw attack.
- **Kuroko `ignite` and `ignite_r`**: a second motion set beside his dribble. The `r` may mean a right-side version.
- **Lala `normal_run` and `normal_ouen`**: a "normal" version beside `run` and `ouen`.
- **SKET-dan `shot`, `combo` and `ap_intro_hi`**: the largest set, which may just be the team's several members acting.
- **Allen `turn_in`**: 1 motion.
- **Gallery poses:** Kumagawa and Lala have `gallery_stand` in this pack.

Any of these may simply be parts of the normal skill, so none is proven unused. All supports also carry the same damage-reaction action files as fighters, including the Dio reaction. Asakura Yoh (208) is covered in PLAYABLE_EXTRA_CHARACTERS.md.

Evidence: `s3.py` to `s7.py` in the same local folder.

## 7. Everything else in the game data, added 2026-10-31

Read-only, **not tested in the game**. I listed all five installed archives (19,445 packs and 631,731 files inside them, no read errors), compared them with the file names in the game program, and searched for leftovers. The `game` and `patch` archives and the movies are not in the install folder, so they are **not checked**.

Being "unused" here is inferred from there being no route to it that I could find. Nothing was seen running.

### Best finds

| Find | What is there (measured) | Why it looks unused |
|---|---|---|
| **Stage map99: a developer test stage** | `map/battle_map/map99_*`, about 4 MB compressed: model 1,038,080 bytes, textures 4,020,048 bytes, breakable objects A-D with animations, its own stage UI. Parts are named `ground_grid`, `grid_00`, `number_01`, `small_step_`, `step_` | Grid floor and number textures suggest a test room (inferred). It has no stage name and no stage-select picture; the 12 stage names and `map_img` pictures stop at 00-11. Its settings file starts with the same bytes as map 11, and its map image is the same size. The program only names it once, in the sound list (`map99_ON_SE`). Its 2-player model packs are also missing |
| **Debug menu text in the program** | `Debug Menu`, `Debug Dip Switch & Param`, `Debug Display`, `Debug Heap`, `Debug Meter`, `Debug Mouse`, `Debug Window`, `Texture2 Viewer`, `Render Target Viewer`, `Auto Controller`, `Light Probe`, `Svn Info`, `Screenshot`, `Print To File`, `debug hp font`. Build stamp beside them: `2015/02/13 20:44:31` | The text is in the program. Whether the code behind it still works, or how to open it, is **unknown**. I did not trace it |
| **Tokyo Game Show leftovers** | `progress/character_select_tgs/`: a whole second character select screen, about 2.3 MB compressed, in ALL, US and FR packs. Also `loading_game_tips_001_tgs` and `_002_tgs`, four loading screens | "tgs" appears nowhere in the program's text, so I found no route to them. "Tokyo Game Show" is my reading of "tgs". Its art may show a demo's roster; I have not looked at the images |
| **22 licensed anime songs: names but no music** | Text `BGMLMT_000-021`: We Are!, GO!!, Cha-La Head-Cha-La, Pegasus Fantasy and more. The program has `limited/%s/BGM_LTD_%s` and `BGM_LTD_JP` | No `limited` folder or `BGM_LTD` file is in the installed archives; the installed `BGM_JP` has 50 tracks. Probably limited-edition or download content (inferred). Might be on the disc or in the game's data elsewhere; not checked |

### Smaller finds

| Find | What is there (measured) | Note |
|---|---|---|
| Voice banks 700 and 701 | `PV_700_00` and `PV_701_00`, about 660 KB of sound each, in the program's list | No character has these numbers. Could be a menu or narrator voice. Not listened to |
| Empty id slots | Characters 19, 20, 21, 208 have empty arcade or gallery art. Card, icon and title packs also have many empty placeholders. For example, player icons 023, 055, 093, 095 and 999 are empty | Reserved numbers; nothing to restore |
| Text for other consoles | `SL` text has PS4 and Vita save messages | The same text base as other versions. Never shown on PS3 (inferred) |
| Other regions | Logos for AS, EU and JP, plus French, Chinese and other-language packs | Each region uses its own. On this copy the others are probably never loaded |
| Stray German pack | `progress_scene_prologue_ALL_m.pak_DE_m.pak`, 528 bytes | The only German file, with a malformed name. Probably a leftover from a build script (inferred) |
| Test button text | `BUT4_084`: `<PAD=T...>test` | Single leftover string |
| Named characters who don't fight | `CharacterName` has about 100 side-cast names, such as Chopper, Kakashi and Gaara | Not unused: these are probably story and battle-partner speakers. No fighter files exist for them |

### What I did not find
- No stages other than the 12 known ones and map99.
- No other test-named packs: searching names for test, debug, dummy, sample, old, tmp and similar gave only `dummy_ps3.lps` and false matches (`psy_hold`, `small_fry`, `karin`).
- All 52 fighter and support voice sets line up with the known cast. 19, 20, 21 and 208 have no voices.

### What to try next (needs the game, so not done)
1. **map99:** set the stage id to 99 on stage select, in a test build, and see if it loads. It is the most promising thing to see.
2. **Debug menu:** trace who uses the `Debug Menu` text to see whether the code still exists, before trying to open it.
3. **Game-show screen and pictures:** convert the art to images to see what they show. This needs the game's `.srdv` texture format, which I did not decode here.
4. **Voice banks 700 and 701:** play them to hear who they are.

Evidence: `a1.py` and `all_names.txt` (full list), `a4.py`, `a5.py`, `a12.py` (program text), `a7.py` (program names against archives), `a9.py`, `a10.py`, `a11.py` (sound, stages, empty slots) in the same local folder.

## 8. Checked in the running game, added 2026-10-31

### map99 loads and plays

**Tested, one fight.** With stage select open, I wrote 99 into the current stage value and started the fight. A green test field loaded, with brown blocks labelled by size (6.5m, 7m, 15m, 21.2m, 30m and so on) and a flat grid floor. Fighters, HUD and intro cards all worked, and it ran for over 2 minutes without a crash. No attacks were tried on it. (Screenshots of the game are not published in this repository.)

- Stage value: stage select screen +0x2E4 (`0x48FAD484` in this run; screen vtable `0x0087FCF8`). The list gave 11 for Yoshiwara Paradise, then 5, 10, 6 stepping right.
- Stage select then showed the name "Alabasta" and a blank picture, because the stage has no entry of its own.
- That this field is map99 is inferred: I asked for 99, and the grid and number labels match the part names in section 7. The file log did not show the name.
- How it was done: the debug command `w32 48FAD484 63` in a test run. There is no option for it in the game.
- Not tried: its breakable objects, 2 players (its 2-player model packs are missing, section 7).

### Debug menu: the window code has no way in

**Read from the program, not run.** The window builder `0x00543138` and the second function `0x00543B88` have function records (`0x8BD438`, `0x8BD440`), but nothing in the program points to those records. The name table at `0x8DEA08` (`Environment`, `Debug Menu`, `Debug Dip Switch & Param`, `Debug Window` and others, 20 names) has nothing pointing to it either. So it looks cut off from the game: there is probably no button or setting that opens it (inferred). Parts may still work if called directly, which I did not try.

### Support data

- **Support skill names are in the text (found):** slots `c*21+1`, where c is 42 + (support number - 200): Evil Blind (Neuro), All Fiction / Book Maker (Kumagawa), Sodenoshirayuki: Somenomai, Tsukishiro (Rukia), D-Don't get any funny ideas! (Chitoge), Bungee Gum (Hisoka), I am Heihachi Edajima! (Edajima), Double Trouble (Kagura & Sadaharu; its input text says Yorozuya Ambush), Ramen Ambush (Jaguar), Misdirection (Kuroko), SKET-dan to the Rescue!, The cavalry's here! (Lala), Edge End (Allen), Lil' Tearaway (Hinata). Each support has one, which matches section 6. Support 208 has no name and no skill text.
- **Shared projectile file:** Chitoge, Kagura, Jaguar, Kuroko, Lala and Allen have byte-identical projectile files, so at least some of those rows are leftovers (inferred). Allen and Kagura logged real hits from their attack files, not from these rows.
- **Neuro's 240 damage row:** his attack file has one 240 damage hit, but his skill logged no hit in the test fights. It may be unused, or may need a situation the script did not create. Rukia, Hisoka, Jaguar, Kuroko, Lala and Hinata have no damage in their attack files at all, which matches the no-damage fights.
- **Kuroko:** the maintainer thinks he needs two fighters on a team, because he passes to a teammate. Not tested.

### Tokyo Game Show character select: opens, with parts missing

**Tested.** The game builds its character select pack names from 4 strings at `0x7D4F18`, `0x7D4F60`, `0x7D4FA8`, `0x7D4FF0`. The local test switch `TGS_CHARSEL` (not in this repository) points them at `progress/character_select_tgs/`, and the 72-byte slots have room for the longer name.

`TGS_CHARSEL` is a mask of the packs to swap: 1 ALL_m, 2 ALL_v, 4 language _m (layout), 8 language _v (art).

| Mask | What is swapped | Result |
|---|---|---|
| 15 | All four | Background only, no menu, no crash. The game reads through empty pointers at `0x0051C0E4` |
| 1, 3 | The ALL layout, with or without ALL art | Background only |
| 12 | Language layout and art | **The screen opens.** Japanese title and support label, working cursor; Start goes on to stage select. Fighter grid empty |
| 4 | Language layout only | English title, grid empty |
| 8 | Language art only | **Closest look at the TGS art.** Japanese title and support label. Fighter faces show in the grid, but scrambled |

- The TGS layout files are what empty the grid, since mask 4 alone does it. The scrambled faces in mask 8 fit the normal layout pointing at `window_soubi_chara` art that the TGS art pack lacks, so it reads other data (inferred).
- What the TGS packs lack (measured): the `window_soubi_chara` window, which is in the normal packs, and the logos are named `_tgs`, so the normal names the game looks for are missing. "Soubi" means equipment, so that demo may have had no J-Customize (inferred).
- Masks 8 and 12 mix TGS and normal files, so neither is exactly how the demo looked.
- "Tokyo Game Show" is still only my reading of "tgs".

### Still not checked

Which action files never run (section 5), voice banks 700 and 701, and the Tokyo Game Show loading screens.
