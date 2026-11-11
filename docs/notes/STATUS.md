# Status (top refreshed 2026-10-30; dated log below, newest at the end)

## Right now (2026-10-30)

From the entries at the end of this file and FINDINGS.md; nothing re-tested for this refresh.

| Area | State | Evidence |
|---|---|---|
| Boot, movie, menus | Opening movie with sound; every Main Menu entry opens and works (Online / Cross-Save need PSN; Cross-Save is now Exit Game) | entries 2026-10-26 ~21:00 / ~22:00, FINDINGS "Opening movie plays", "Exit Game" |
| Speed | ~29-30 real f/s on all tested maps without PERF; 4K 28.4 f/s | CPU_PROFILE §42, FINDINGS "Higher resolution" |
| Audio | Music and effects, no gaps (AUDIO_NBLOCK=16, CRI_LEAD=12) | entries 2026-10-27 14:05 / 12:55Z |
| Graphics | Skill effects, leaf-shadow flicker, ground (FP_BEM) fixed; RES_SCALE 720p/1440p/4K, fullscreen | FINDINGS "Environment shadows", entries 2026-10-27 18:40 / 21:40 / 22:00 |
| Input / UI | Launcher (keyboard, KB&M, controller; the DualSense is read directly, hands-on button tests pending), key rebinds, point-and-click main menu, fight HUD size (UI_SCALE), 2 players | FINDINGS "Input choice", "Point-and-click menus", "UI size" |
| Training mode | Built and in the maintainer's hands: Free Battle / Training box, padlocks on slots 2 and 4, 0-star dummy, no KO, refill after 1.5 s, Unlimited time, Training off at the main menu. Keyboard scripted runs pass; 2 Players not tested by script (the maintainer reported it working, see the entry that notes it) | FINDINGS "Training mode step 1/2/3", change table below |
| Fight freeze | Fixed in `toolkit/ps3recomp-local.patch` (capped log counters overflowed after minutes in a fight); one 30-minute scripted run and one 20-minute normal session had no freeze | FREEZE_FIX.md, FINDINGS "Fight freeze" |
| Message boxes | Port boxes drawn like the game's (Exit Game, Choose a mode) | FINDINGS "Port message boxes" |
| Next | Balance notes (Gintoki first, then the cast). 3rd fighter per team is still queued | docs/balance |
| Parked | Split-screen layout, brightness/glare, DLSS | entries 2026-10-21 ~22:41, 2026-10-29 21:00 |

Toolkit: `ps3recomp-src` `jstars-port` = v0.15.0 + 183 commits, tip a85c7fa (ORGANIZE_2026-10-30.md).

## Changes 2026-10-29 / 10-30 (rule from the maintainer: every change gets a line here with its test and evidence)

Run logs and captures are kept locally and are not in this repository; port changes are in jstars\port\jstars_game.cpp, engine changes
are in the private toolkit checkout (not in this repository). Now: the toolkit changes are in `toolkit/ps3recomp-local.patch` in this repository.

| Change | Where | Tested how | Evidence | Not tested |
|---|---|---|---|---|
| No KO, health stops at 1, refill after 1.5 s | training_icall, training_tick; 6510ac7 hook | Scripted fights, all fighters started at 3 health; then the maintainer's report that refill never ran fixed (Training fights sit at scene step 0x10) | tr1 (128 hits, no KO), tw1-tw3 (refills logged) | |
| 0-star dummy: empty pad, then human driver because the AI still attacked up close | training_icall | P1 walks up and attacks | tw3: 17 hits on the dummy, none on P1 | Controller as player 2 touching the dummy |
| Free Battle / Training Mode box | free_battle_box; 6510ac7 choice box | Scripted, keyboard | tb1, fb7, fb8, af2 | Mouse click on the box |
| Stars row 1 <- 0 -> 5 | training_charsel | Scripted presses, stars read from screenshots | st4 | |
| Stuck "Return to the previous screen?" in 2-player Training fixed: the port's filter swallowed Cross while the cursor was on a padlocked slot; filter removed, the game ignores Cross and Triangle there by itself | training_charsel | Reproduced on the old build; both answers tested from a padlocked slot on the final build, scripted keyboard | tp1 (stuck), tp2, tp3 | 2-player fight with a human Player 2 after this change |
| All characters unlocked on the maintainer's save: J-Points and the three friendship gauges raised in memory (+0x40903B8C JP; +0x40903B90/94/98 gauges, MAX), then all 39 battle and 13 support characters bought in the J-Point Shop, game saved. Save edit, no code change. Backup: run\gamedata\dev_hdd0\home\00000001\savedata.backup-2026-10-30 | save data | Restarted the game: shop shows every entry Sold Out or bought, 896499 JP, 0 slots left; character select pages show the new characters | sh1-sh4 | Each of the 7 character select pages not read one by one; a fight with a newly bought character |
| Balance log: in Training the game writes run\balance_log.txt with every hit (damage, return addresses), P1 button changes and fighter stat fields | bal_hit, bal_tick, bal_note_fighter in jstars_game.cpp | Scripted Training fight, keyboard | bl1 (174 lines, 13 hits with the same call chain) | the maintainer session with Gintoki; the line sizes over a long session |
| Fight detection now uses the HUD reading health, not 0x0094DEC4: in the maintainer's Training fight that value was outside the old range, so there was no refill and the log had hits only. Also used by mouse look, arrow keys, HUD size and the menu checks | fight_hud_up in jstars_game.cpp | Scripted Training fight, keyboard: refills, buttons, stats logged, Training off at the main menu | bl2 (step value 12 there) | HUD size and mouse look in a fight on this build; the maintainer's run |
| Padlocks on slots 2 and 4 without button presses | training_lock_slots | Screenshots on each entry | dl1, ex3, pl1, af2 | "CPU" label missing compared with Arcade (left out on purpose) |
| Time Limit Unlimited in Training | training_time_run | Fight shows infinity | tf2, af2 | |
| Own screen tracking (each screen known by the code it runs; objects found wherever they moved) | screens_note, screen_up, main_list_open | Online Mode error, Training, match, Re-select, match, main menu, Free Battle / new Training | pv1, af2, af3 | Point-and-click after those menus; 2 Players; the maintainer's run on this build |
| Menu scene accepts any step below 0xB (the maintainer's log: step 5) | in_menu_scene, exit_option_on_entry | Step forced to 5: box and Exit Game Yes/No open | ex2, mb2 | What sets step 5 is unknown; never seen in my runs |
| Training off at the main menu | free_battle_box | Next Free Battle has no padlocks | fb7, af2 e3 screenshot | KOs in a normal Free Battle right after Training |
| Port boxes look like the game's | a85c7fa, oskp_render | Screenshots | bx1, bx2 | Typing panel (deck rename) |
| Balance folder started: where attack numbers live, Gintoki draft | docs/balance | Gintoki's attack rows match the hits in the maintainer's session | gin1 | Moves, names, Stamina costs; editing a value |
| Gintoki moves and Stamina added to Balance, Shared.md started | docs/balance | the maintainer's second session: 129 hits, 38 refills, buttons and stats logged; matched by hand | gin2 | Attack names, Victory Burst gauge, half-damage rule, other characters |
| Game map index, first version: file layouts, triggers and file needs, named functions. New: action numbers are the order of the action files, and battle_move_table looks like the Move List | GAME_MAP.md (notes only, no code or data changed) | Read-only: row sizes checked on 42 fighter packs; action names checked against Gintoki's logged actions (23, 24, 25, 26, 29, 35, 41, 44, 88, 89, 90) | (g1, g2, g3) | No game run. Entries marked reading are untested; the button-to-action table is not found |
| Rest of the game data checked for unused content: test stage map99, debug menu text, game-show character select and loading screens, 22 licensed songs named but with no audio installed, voice banks 700/701 | UNUSED_CONTENT.md section 7 (notes only, no code or data changed) | Read-only: listed 19,445 packs and 631,731 files, compared with the program text | (a1, all_names, a4-a12) | **Not tested in the game**; game and patch archives and movies not checked; debug menu code not traced |
| 13 working supports added to the unused-content note: one skill pack with data each, the other 20 empty; their motions listed | UNUSED_CONTENT.md section 6 (notes only, no code or data changed) | Read-only: unpacked every support pack | (s3-s7) | **Not tested in the game**; motions could not be linked to action files for supports |
| Note on unused content on the 39 working fighters: no complete hidden move; 489 of 6,601 motions have no action file pointing to them, mostly aim and throw variants | UNUSED_CONTENT.md (notes only, no code or data changed) | Read-only: matched motion slots to action files for every fighter | (u8, u12, u13) | **Not tested in the game**; whether each action file is ever triggered is unknown; support-skill packs not checked |
| Asakura Yoh (support 208) added to the note: no model, voice, moves, skills, icon or name text; attack table is the same file as Dio and Dai | PLAYABLE_EXTRA_CHARACTERS.md (notes only, no code or data changed) | Read-only: compared with Jaguar and Kuroko, searched 18,958 packs | (y1, scan_yoh) | **Not tested in the game**; game and patch archives not checked; name image not viewed |
| Note on Jotaro, Dio and Dai: no body model or textures on the disc (model packs empty); Jotaro has his own motions and attack table, Dio and Dai are templates; shop table and roster lists located | PLAYABLE_EXTRA_CHARACTERS.md (notes only, no code or data changed) | Read-only: unpacked and searched 18,958 packs in the character, progress and battle archives; map and sound file lists | (t4, t8, t12, scan_out, t17, t19) | **Not tested in the game**; game and patch archives not checked; code that reads the roster lists not traced |
| Unpacker for the game's compressed files, attack tables read for 42 battle and 14 support files, one Balance file each | jstars-notes/tools/cmp_unpack.py, docs/balance/ | Gintoki's unpacked pack is byte-identical to the game's memory; all 98 character files unpack | cast, cl0t | Other characters not checked in a fight; Dio (020), Dai (021) and Asakura Yoh (208) do not fit the layout; a few rows have odd ratios |
| Skill names and inputs from the game's text added to every battle character file; display names on files; Gintoki's moves named, and the 500 / 200 attack re-read as Kamehameha Power! (inferred) | docs/balance/, gen scripts in | Skill table screen for Raoh shows the same names as slot 26 (sk1); Gintoki names match slots 588-599 | sk1, md4 dump | Names not linked to rows except part of Gintoki; support skill names not found |
| Balance log writes hit records, attack row and hit, left stick; `TRAINING_FORCE=<me>,<opponent>` picks fighters on character select; `BAL_TEST=<row>,<hit>,<damage>,<gauge>` changes one attack slot in memory | port/jstars_game.cpp (env switches only, normal play unchanged) | Stage screen shows Gintoki vs Koro-sensei (mv28b); first attempt set the fighters too late and loaded Raoh, fixed; 999 damage applied in the fight (bt1) | mv28, mv28b, mv28c, bt1 | Normal play not re-run after these log changes |
| Balance files: gauge gain for every attack row, projectile tables, Stamina costs by action, for 41 battle files and support; Gintoki's page rewritten with skills linked to rows; Shared and README updated; session tools copied | docs/balance/, tools/balance/ | Gintoki: 33 of 33 melee hits matched the table, Stamina costs 50/100/200/300 matched logged presses. Raoh: 18 of 24 hits matched, 4 projectiles had no row then, 2 were halved. Gauge: raising one hit's number made the R3 prompt appear after about 7 strings | mv28b, mv28c, bt1, gen_bal2.py | Only Gintoki and part of Raoh checked in a fight; other characters' rows not tied to skills; half-damage rule, gauge's memory value, dash cost source and full-gauge value not settled; 13 action groups have mixed costs; no permanent balance patch exists |
| `TRAINING_FORCE` now names a file ("<me> <opponent> <support>") read on character select, so one game run walks the whole cast; scripted cast run with a charged attack set | port/jstars_game.cpp, tools/balance | Luffy then Ace in one run: stage screen showed Ace, Hancock support, Koro-sensei; 124 hits logged, 4 from the support's file (ct1) | ct1 | Charged set not yet seen working; full run in progress (cast1) |
| Freeze seen in long scripted runs, cause not fixed: the game's logic stops about 13 minutes after launch (log stopped 808 s, 776 s and about 824 s after launch), with the main thread polling `func_005383D4` for one job that never finishes. Cast run now launches the game fresh for each battle fighter and watches log size for freezes | tools/balance/tour_cast.sh, cast_run.sh | Three freezes in cast1, dump in freeze3 | cast1 freeze1, freeze2, freeze3 | Not known whether normal play freezes too, or whether the logging plays a part; restart per fighter not yet run to the end |
| Cast run: Cross at 45 and 52 s skips the opening movie (launch to title about 70 s); boot check now tests the save box and main menu as well as the title, because the extra Cross could pass the title unseen and leave the script waiting | tools/balance/tour_cast.sh | b3 and b4 launched and fought with the skip; stuck launch seen at b11 | cast1 | Fixed boot check not yet seen on a real launch |
| Seen, not fixed: flat reddish-brown screen for a whole fight on one launch (Ichigo vs Koro-sensei, Aizen support, enemy support Ace). Frames kept drawing and fight-start was logged, with no hits and no HUD. The retry on a fresh launch (enemy support Luffy) ran normally with 66 hits | none | the maintainer saw it live; screenshot | cast1 balance_log_16 and 17 | Cause unknown; not reproduced |
| Whole-cast run finished: 38 battle fighters plus Gintoki and 14 supports logged; each Balance page has its logged moves, health and highest Stamina; Support.md has a results table; README has the coverage table | docs/balance/, tools/balance/, gen_bal3.py, gen_sup.py, cast_scan.py | Attack rows seen 462 of 1686, single hits 871 of 5438, projectiles 92 of 942, Stamina costs 162 of 617 | cast1, cast2, cast3, cast_scan.json | Inputs are the last press before a hit, so later hits in a string can name the wrong button; rows are tied to inputs, and named skills only on Gintoki's page; support effects only from the maintainer's eyes; highest Stamina below 2000 means my hold ended early |
| KO count mapped: limit and count found; test switches `TRAINING_KO_TEST` (real KOs in Training) and `KO_TEST_MAX=<n>` (red's limit), both off in normal play | port/jstars_game.cpp, docs/balance/Shared.md | Limit 5: Results after the 5th KO, still fighting at 4; limit 2: Results after the 2nd; the maintainer saw red's bars drop to 2 | ko4, kom5, kom2 | Blue's fields inferred, not tested; which red copy controls what not separated; other team setups not checked; no permanent option built. The test fights added JP to the maintainer's save (the maintainer is fine with it) |
| Freeze test without PERF, RSX_FRAMELOG and GCM_FLIPCOUNT: still froze 13 m 48 s after launch, so those are not the cause. The maintainer's last normal session was 8 m 45 s by the launcher log, so normal play past that is not cleared | none | Log stopped 02:59:27, launched 02:45:39 | ftb, run/launcher_log.txt | Cause unknown; balance logging and the scripted input path were still on in this test |
| Freeze captured, not fixed: jobs stop finishing about 13 to 14 minutes after launch, after about 1.4 million jobs | FINDINGS only | Five timed freezes, 776 to 828 s after launch, across fighters and logging settings | cast1, ftb, ftc | Cause unknown. Not known whether normal play freezes: the only timed normal session was 8 m 45 s. Balance logging and scripted input were on in every freeze |
| **Paused 10-31 at the maintainer's request. Open bug: the game freezes in fights, in normal play too.** Froze at 6 m 18 s in the maintainer's session; 13 to 14 minutes in scripted fights; 17 m 47 s with the call hook off and light input. `ICALL_OFF_IN_FIGHT` test switch added, off in normal play | port/jstars_game.cpp, FINDINGS.md | Launcher log, runs ftb, ftc, fth | ftc, fth | Cause not found, not fixed. Not yet known whether builds from before 10-30 freeze too. Next ideas are in FINDINGS. Waiting for the maintainer's go |
| **Fight freeze fixed**: a capped log counter overflowed after about 2 billion graphics uploads and then printed on every upload. 132 such counters now stop at their cap | toolkit/ps3recomp-local.patch (libs/video, libs/spurs) | fix1: no freeze in 30 minutes after launch, about 26 minutes of fight; unfixed builds froze at 6 to 18 minutes. 10-26 build froze too (old1) | fix1, old1, ftd | Tested by script only, on one fighter pairing, not yet in the maintainer's normal play. Not committed and not on GitHub (Now: committed in `toolkit/ps3recomp-local.patch`) |
| Fight freeze fix confirmed in normal play; the maintainer also reports 2 Players in Training Mode works fine | none | the maintainer's session 12:44:13 to 13:04:14 (20 m 1 s), closed normally, exit code 0; balance log ran 18 m 27 s with 330 hits; upload log line exactly 6 times; no 'still closed' lines | run/launcher_log.txt, play_stdout.txt, balance_log.txt | Other fighters and support-heavy fights only covered by shorter runs |
| Unused content checked in the game: map99 test stage loads and plays when the stage value is set to 99; debug menu window has nothing pointing to it; support skill names found in the text | UNUSED_CONTENT.md section 8 | One fight on map99, over 2 minutes, screenshots | m99, st2, dbgtrace2.py, tmpl.py | Debug command only, no game option; no attacks tried on map99; debug menu not run |
| Tokyo Game Show character select opened: test switch `TGS_CHARSEL` mask: 12 (language packs) shows its Japanese title with a working cursor and an empty grid; 8 (language art only) shows the TGS art with scrambled faces; 15 and 1 show only the background | port/jstars_game.cpp, UNUSED_CONTENT.md section 8 | Screenshots; Start reached stage select | tg1, tg2, tgsdiff.py | Grid not fixed in any mode. Off in normal play |
| Jotaro's unused fighter data runs on Jonathan's body: test switches `FIGHTER_PACK`, `MODEL_BODY`, and `PS3_FS_ALIAS_DIR` for missing files; a local test script that is not in this repository | port/jstars_game.cpp, toolkit/ps3recomp-local.patch (runtime/ppu/ppu_fs.cpp), PLAYABLE_EXTRA_CHARACTERS.md | jo4: 10 hits matching his table; forcing 019 itself stays stuck or crashes (jo1, jo2) | jo1 to jo4 | Specials did not land; motions only seen in stills; everyone in the fight gets his data. All switches off in normal play |

## Right now on 2026-10-20 (history: the black demo match, long solved)

**Title screen, menus and loading screen render correctly** at ~30 fps (`title_screen.png`).
The first 3D scene is the idle CPU-vs-CPU demo match, and it is the open problem:

| Symptom | Evidence |
|---|---|
| Window stays **black** | backbuffer 0.0 % lit in `run\rsxcap_rt48` frame 5650 and 5690 |
| ~1.5-2.5 fps | `[PERF]`: vertex fetch is 85-96 % of frame time (CPU decode, single thread) |
| Native strips rasterize nothing | `STRIP_NATIVE=1`; main RT black even with cull/depth off (FINDINGS §19) |

**Best current lead (FINDINGS §20):** in the full match frame the scene is drawn into the main RT
`0x0ED10000` (73.7 % lit in the partial frame 5650), but by the end of frame 5690 that RT is 0 %
lit. The post-process chain overwrites it with blend off. The final display pass then samples
`0x0C6ABF40`, which is all black too.

**Resume here:** FINDINGS §20 "Next ideas" (first: per-op main-RT snapshot around ops 1386, 1394,
1432 of frame 5690). Helper scripts: `tools\README.md`.


## Quick recipes

Demo match (no input; title at ~140 s, match from ~190 s):
`jstars-notes\tools\capture_match.ps1 -Name <new>` which does
`PAD_SCRIPT="31:0x4000"; PERF=1; RSX_FRAMELOG=60; RSX_CAP="5650:2:40"; RSX_CAP_DIR=...; run_test.ps1 275 -Commands stat`
and window screenshots at 200/230/250 s.
Title only: `PAD_SCRIPT="30:0x4000,33:0x4000,36:0x4000,39:0x4000"`, screenshot at 55 s.
Logging: `run_test.ps1` now sets `PS3_VERBOSE=0` unless it is already set; add `-VerboseLog`
for the full runtime log (it more than halves game speed, FINDINGS §30). Old script:
`jstars\run_test.ps1.pre-verbose0`. Use `PS3_SAMPLE=2` for the per-thread sampler.

## State of the code (nothing here is pushed)

- Toolkit edits are committed on local branch `jstars-port` in `ps3recomp-src` (c0dbbd4).
- Port copy: `jstars\port-backup-2026-10-20` (without `build\`). Do not touch it.
- Only the AI assistant builds/runs the game and edits `libs\video\rsx_d3d12_backend.c`.

## What boots (history)

1. ELF load, CRT, TLS, `_cellGcmInitBody`, D3D12 backend up, video out 1280x720.
2. SPURS: 20 "chain control" job chains, 6 CPU job worker threads, the job server thread.
3. CRI middleware: FS (4 file-access threads), CPK binding of 7 partitions, ADX2 audio.
4. Game job system: all 18 SPU jobs lifted and registered (FINDINGS §15); fibers signalled from SPU.
5. File server streams `$CMP` chunked files out of the CPKs.
6. cellGame: first-boot HDD data install (2.5 GB), later boots accept it.
7. Trophy check, save data (`AutoLoad2`).
8. Opening movie plays to the end (frames black: no FFmpeg), then the frame loop.
9. cellSearch/cellMusic callbacks (§14) and the GPU hand-off (§11) fixed: real draws reach the GPU.
10. Title, menus and loading screen render (§17).

## Still faked (unresolved imports hit at boot)

| NID | Name | Note |
|---|---|---|
| 0x6CFD856F | cellSysutilGetBgmPlaybackStatus2 | harmless so far |
| 0x45FE2FCE | _sys_spu_printf_initialize | harmless |
| 0x9DCBCB5D | cellSpursAttributeEnableSystemWorkload | harmless so far |
| 0x30AA96C4 | cellSpursInitializeWithAttribute2 | faked OK; the HLE SPURS keeps no instance state that needs it |
| 0xE7FA820B | cellSysutil (unknown name) | |

Import coverage: 321 imports / 26 libraries, ~250 handled (`jstars\out\hle_coverage.json`).
Peripheral gaps (voice chat Avc2, music/search utility, NP TUS, OSK ext) have not mattered.

## Known fragile spots

- The DataCheck delay (FINDINGS §9, §13) is a timing shim. `JSTARS_DATACHECK_MS` overrides it.
- `jstars_fiber.cpp` models only the parts of libfiber J-Stars uses.
- `cellSpursJq` overrides run every pushed job synchronously on the pushing thread.
- `port\spu\spu_workloads.c` is generated by `spu_jobs\lift_all_jobs.py`; do not hand-edit.
- Temporary diagnostics live in `port\recompiled\ppu_recomp_027.cpp` and `_032.cpp`; a re-lift removes them.
- Shadow cascades (depth-only passes) are skipped on purpose (§17).
- D3D12 debug layer unavailable: Windows "Graphics Tools" is not installed.

## Update 2026-10-20 afternoon (AI assistant)

The demo is no longer black. Local commits on `ps3recomp-src` branch `jstars-port` (not pushed):
c8f408e capture passes, 299aa12 vertex fetch, b7efcb5 mid-frame flush (§22), 35b1f12 FP export
bit (§23), bce3ec9 perf timers (§24), e32a781 strip restart expansion (§25).
- The overlay, intro card and 3D scene (stage, water, characters) render.
- Open: models look cracked (mixed triangle winding, §25 follow-up) and many surfaces are pale or
  untextured; ~15 backend batches/s, 45% of it per-draw texture setup (§24).
- Held back: `patches\perf-timers\jstars-main-kick-wait.patch` until the glitches are sorted.

## Update 2026-10-20 evening (AI assistant)

More local commits on `jstars-port`: ee1079d texture-setup counters, b219262 diagnostics,
4757a62 512 texture slots, 8ee4e58 texture sRGB decode (FINDINGS §26-27). The sky renders and
materials have their real colours. Open: cracked characters, black triangle gaps in the sky dome,
shadow cascades never rendered (both shadow maps empty), vertex work is now ~77% of frame time.

## Update 2026-10-20 late (AI assistant)

Further commits on `jstars-port`: 16c9a9d native strips default (cracks and sky gaps gone, §28),
e6feaa6 strip-batch, 7c57768 native-strip batching (~8x faster, §29), bfae355 shadow passes +
c3eaf8b making them opt-in (`SHADOW_PASSES=1`; reading the maps is still wrong). `jstars\port\main.cpp`
has the kick-wait patch (`main.cpp.pre-kickwait` is the old file). The demo match runs at ~10-11
batches/s and renders cleanly apart from shadow blotches. The remaining time is mostly waiting for the
guest (PPU/SPU); profile it with `PS3_SAMPLE=2`.

## Update 2026-10-20 night (AI assistant)

Defaults now: native strips, mid-frame flush, FP export bit, texture gamma, 512 texture slots,
zeta targets v5, shadow passes, FACING input (each has an off switch, FINDINGS §22-32). The demo's
intro cards render close to RPCS3: characters smooth and correctly coloured, water blue, HUD clean,
~17 batches/s with `PS3_VERBOSE=0`. Remaining known issues: a few dark leaf patches near the water,
grass slightly neon, depth clip (`DEPTH_CLIP=1`) still blacks the frame.

## Update 2026-10-20 late night: speed (AI assistant)

Toward 30 fps (`perf\`): baseline 16.0 fps → speed patches default-on (env cache, async vertex
fetch, strip carry; commit 1a8618e) 25.1 → `VBLANK_QPC` default-on in `jstars\port\main.cpp`
**28.0 fps (~36 ms)**, 17-19 of 20 frames at 30 fps pace. Off switches: `RSX_ENV_CACHE=0`,
`VFETCH_ASYNC=0`, `STRIP_CARRY=0`, `VBLANK_QPC=0`. Remaining limit: the main thread waits ~72% of
the time, mostly on one SPU job chain (`perf\main-wait-2026-10-20`).
- 2026-10-21: `GCM_PUT_POLL=200` default-on (put-pointer poll in the idle wait): **~29.5 fps
  (34 ms)**, picture unchanged, ~1 extra core while idle. Off: `GCM_PUT_POLL=0`.

## Paused 2026-10-21 ~02:10 (AI assistant) — resume here

**State:** `ps3recomp-src` branch `jstars-port` at **22d07f7**, working tree clean, nothing pushed.
`jstars\port\build\JStarsRecomp.exe` matches it. `jstars\port\main.cpp` has kick-wait,
`ps3_sampler_start()` and `VBLANK_QPC` default-on (backups `main.cpp.pre-*`). Game closed, ISO
unmounted. Save data exists: `jstars\run\gamedata\dev_hdd0\home\00000001\savedata\NPUB31647-B0\`
(with it, CROSS on the title -> "Save data loaded" -> OK -> Main Menu).

**Where things are:**
- Demo/intro cards render close to RPCS3 at ~29.5 fps; menus, character/stage select at 30 fps.
- Free Battle match reached and playable (`menu\README.md`): fight ~20-27 fps; black floor slab and
  cracked character models on Yoshiwara; input works (keyboard and XInput already supported).
- Two patches applied **opt-in, not yet tested**: 0b43edf `FP_REGS64=1` (three FPs fail to compile
  on Yoshiwara, likely the floor slab); 22d07f7 `MENU_A2C=1`, `MENU_TEX_PITCH=1`, `MENU_FMT16=1`,
  `TEXFMT_SKIP=1` (menu logo, character-select title bar and speckles, foliage rims).

**Tools:** `jstars-notes\tools\menu_tour.sh <out_dir> [stage_rights]` drives title -> menu ->
Free Battle -> stage select -> fight by recognising each screen (`screen_match.py`, refs in
`tools\refs`); screenshots and logs go to `<out_dir>`. Verified once: `menu\tours\t0_default`
(ok, ~4 min). `stage_rights` = RIGHT presses on Stage Select, for the stage survey.

**Next steps:**
1. Run the four switch tours (one switch each): `FP_REGS64=1`, `MENU_A2C=1`, `MENU_TEX_PITCH=1`,
   `MENU_FMT16=1 TEXFMT_SKIP=1`; compare with `menu\tours\t0_default`. Count `[FP] PS compile FAIL`
   (3 on Yoshiwara with defaults) and `[TEXFMT-SKIP]`. Make clear wins default.
   (The FP_REGS64 tour was interrupted by the pause; rerun it.)
2. Stage survey (`STAGE_PLAN.md` -> `STAGES.md`): `menu_tour.sh <dir> N` for N = 0, 1, 2, ...
   until the stage name repeats, with the fixes that won in step 1.
3. Live analog stick for scripted runs needs a `cellPad.c` change: waiting for the maintainer's answer.
   The maintainer can play directly: keyboard arrows = D-pad and left stick, Z/X/A/S = cross/circle/square/
   triangle, Enter = START, Tab = SELECT.

## Paused 2026-10-21 12:50 (AI assistant) — resume here

- ps3recomp-src `jstars-port` at **e799d57** (MENU_FMT16 + FP_REGS64 default-on), clean tree, nothing
  pushed; exe matches. Game closed, ISO unmounted.
- `tools\menu_tour.sh` stage selection fixed (CROSS, RIGHT N, CROSS, START; the maintainer's instructions).
- Stage survey in progress: **s01-s06 done, s07 needs a rerun**, results in `STAGES.md`.
- Next: survey s07 onward, then RPCS3 comparison and fixes (mipmaps, per-draw samplers).

## Update 2026-10-21 afternoon (AI assistant thread: colour study) — patches not yet run

Neon grass / saturated sky traced to a **stuck auto-exposure**: the tone-map's exposure constant
`c1.w` is 7200 (= 0.72 / 1e-4) in every capture because the 20x11 average-luminance target
`0x039FFA40` is never read back to guest memory, so the PPU averages black. Four opt-in patches in
`patches\color-exposure\` (README there), write-up `COLOR_EXPOSURE.md`, FINDINGS §35. For Black
screen, cheapest first: A/B with `TONEMAP_EXPOSURE=1` on the Ace card, then `LUM_READBACK=1`
(the fix), then `TEX_EXPAND=1` only if `[FPEXPAND]` lines appear.

## Update 2026-10-21 afternoon (AI assistant thread: fight hitches) — patches not yet run

Fight dips measured from the survey logs: pipeline builds 11-39 s per fight (43-78 ms each,
120-450 at match start), new textures 7-11 s. Opt-in patch `patches\shader-hitches\`
(FINDINGS §37, README there): `SHADER_CACHE=1`, `PSO_ASYNC=1|2`, `TEX_POOL=1`, plus `[PERF5]`
timers. For the AI assistant: baseline with the patch first (the new timers say which half of a
miss matters), then `SHADER_CACHE=1` twice, then `PSO_ASYNC`, then `TEX_POOL`.

## Update 2026-10-21 evening (AI assistant thread: shader cache misses) — analysis, no new patch

The AI assistant's "low hit rate" was two different windows: the match start is a **full disk hit
from run 2 on** (0 compiles, pso 10.4 s -> 0.18 s, 1.5 -> 17 fps); the 306-miss window is a later
burst (character switch / special) of programs never seen in any run, and those differ per run.
The key does not drift (files written == compiles, no VS compiled after run 1, lengths of the
burst's 259 shaders spread flat, so distinct programs). FINDINGS §39, `tools\shcache_stat.py`.
Recommendation: `SHADER_CACHE=1` default-on, keep `shadercache\` across builds, warm it with a
one-time tour of every character; `PSO_ASYNC=1` with `PSO_ASYNC_THREADS=4` for first-time
content.

## Resume point 2026-10-21 evening (AI assistant)
- jstars-port HEAD 929b000 (58 commits on v0.15.0). New defaults today: TEX_MIPS (678d973),
  SHADER_CACHE + TEX_POOL (eea13f6). Cache lives in jstars\run\shadercache: keep it.
- Opt-in, tested, kept off: S0_LINEAR (black boxes on plants), TONEMAP_EXPOSURE, TEX_EXPAND (no
  effect), PSO_ASYNC (objects can appear a frame late), STRIP_INDEXED (slower), LUM_READBACK
  (works since v4 85c4157, darkens; La/E numbers don't match yet, FINDINGS §40), ZPASS_SHOW (diag).
- Upstream picks in: SPU float paths (081f1f2, dbe9dba), 31-bit texture offsets (c8d2329); tag
  v0.17.0 fetched from the bundle.
- Waiting: the AI assistant's shadow fix, rt-drawlog, cap-alpha; the maintainer's OK for the REUSABLE step one; live
  stick input (cellPad.c) still needs the maintainer's answer.
- Tools added: tools\demo_shots.sh (demo screenshots at fixed times), hitchsum.py, imgdiff.py.

## Reusable fixes branch (started 2026-10-21 ~22:00, the maintainer approved)
- Separate clone `PS3Recomp\generic-0.17` (from ps3recomp-src; jstars-port untouched), branch
  `generic-fixes` from tag v0.17.0. Commits: 48825fe NV308A range, b2fb8b1 RET without CALL,
  98c695a FP branch constants, d184a05 64 registers, 5c4ca9e linear DXT pitch, 29fe202 mid-frame
  flush (VP_NO_MIDFLUSH=1 off), 1bfbe31 16-bit texture formats. All always on except the flush switch.
- Build/test: `generic-0.17\build_check.bat` builds clean and 16/16 unit tests pass (after 0468f71 fixed two split log strings). Also fetched into ps3recomp-src as local branch `generic-fixes`; jstars-port stays checked out. Not run with a game.
- Not ported yet (medium): primitive restart / vertex base decode and polygon offset decode in
  rsx_commands (upstream has them only in rsx_dispatch).

## Brightness work parked (the maintainer, 2026-10-21 ~22:41): wait until near the end
- All colour/brightness switches stay off: LUM_READBACK, TONEMAP_EXPOSURE, FP_EXPOSURE/FP_GAIN
  (+ FP_GAIN_KEYS in color\capcode\fp_gain_keys.txt), TEX_EXPAND. Findings §37-§53.
- The PPU glare traces (lumflag 1-5) are removed from jstars\port\recompiled again: originals put
  back from the .pre-lumflag* backups; the traced versions are kept as *.cpp.with-lumflag.
  lumflag-trace-5 was built but its run was stopped (no results). Port rebuilt clean.
- Next focus (the maintainer): textures, shadows and other rendering bugs. Shadow border switch from an AI assistant
  analysis expected next; then Athena sky streaks, Dark Tournament rubble, blocky sky/palms.

## Resume here (paused 2026-10-22 ~02:55 at the maintainer's request)
- jstars-port HEAD: T0_WRAP mirror commit (after da47404). Nothing running, ISO unmounted, port
  built from HEAD. Brightness work stays parked; all colour switches off.
- New defaults today: ZPASS_CMP_SWAP + S_BORDER_UNITS=0x180 (fc26535, shadows), T0_WRAP (da47404,
  unit 0 follows the draw's wrap mode; fixes the Athena sky lines in the S0_LINEAR test).
  Off switches: ZPASS_CMP_SWAP=0, S_BORDER_UNITS=0, T0_WRAP=0.
- In progress: T0_WRAP test plan (patches\unit0-wrap\README.md). Done: Athena run with da47404
  (stages\t0wrap\s03, not yet looked at). Still to do, with the mirror build: Athena (lines gone?),
  Penguin Village (no plant boxes), Dark Tournament facing the ring, Yoshiwara + Namek (no change),
  T0_WRAP=0 on Athena (lines back), fps; then labelled pictures for the maintainer. Script:
  local batch31 (stages\t0wrap2\s03,s05,s09,s00,s06,s03_off).
- Diagnostics added today (off): FP_IDCOLOR fixed (9ffcec9), FP_IDCOLOR_RT (077fea4).
- Next after that: other texture items in STAGE_TEXTURES.md; reusable branch is at
  generic-0.17 (builds, 16/16 tests).

## Assistant pause point (2026-10-24 ~03:25 local, the maintainer: pause, continue tomorrow)
State: game closed, ISO not mounted, no batches running. ps3recomp-src branch jstars-port HEAD 5a77662
(no uncommitted changes); jstars\port\build\ built at 5a77662; build-pdb\ is older (b8c179a+, rebuild with
build_pdb.bat before profiling).
Defaults now on (off switch): PSO_ASYNC (=0), FP_INFO_CACHE (=0), TEX_SLOTS 1024 (=512 old), title FPS =
real flips (TITLE_FPS_BATCHES=1), VPULL (=0) with VPULL_ONCE (=0). PRIM_LOG=1 diagnostic, off.
Results: CPU_PROFILE.md §20-24; FINDINGS.md "PRIM_LOG demo capture" and "Goku's Kamehameha".
Open items, in order:
1. **Kamehameha (the AI assistant, VFX.md)**: beam draws reach the backend (0x0DB30000 additive code 9A64BF1C,
   0x0DC20000 alpha-only code AD204EA4) but produce no pixels, PSO_ASYNC=0 too. Their request, NOT yet done
   (run stopped at the pause, partial dir perf\scratch\laser\vpattr_04EEE600 is incomplete): demo with
   `VP_ATTRS=1 RSX_DBG_VP=04EEE600` (+ VP_ATTRS_DUMP=3:8), retry RSX_DBG_VP=04732D80 / 04FC4600 if no
   [DUCKVP]; duck_vp.hlsl lands in jstars\run, move it into the run dir. Then `DEPTH_OFF=1 RSX_CAP=10200:8:12`.
   Script: local batch87.sh (same steps). Note: VP_ATTRS dedupes by attribute mask over the whole run
   and the beam's fp addresses differ per draw, so VP_ATTRS_FP can't target it; the beam's [VPATTR] lines
   may be missing. Beam timing in the attract demo: frame_count ~10210-10300 (screenshot ~330-345 s).
2. **Triangle glitch** (big flat triangle over the top-right quarter, the maintainer's run demo_new): not reproduced
   in 4 demo runs (perf\scratch\trianglebug); tools\tri_scan.ps1 detects it. Asked the maintainer if they touched the
   window; unanswered. An AI assistant suspects tex-slots (stale sparse-checksum re-bind).
3. Live stick input still awaits the maintainer's answer (don't edit cellPad.c).
Disk added today (perf\scratch): laser 4.3 GB, trianglebug 0.7, drawcost 0.43, vpullmaps 0.33,
destruction 0.28, primlog 0.1 (~6.1 GB). Nothing deleted; clean-up needs the maintainer's approved list.
- 2026-10-24 afternoon: stick input committed (1f52c30, PAD_FILE "stick lx ly rx ry [polls]", tested on Hidden
  Leaf: perf\stick\leaf_walk.png); vp-code logging (a5420e0); build\ at a5420e0. demo_shots*.sh press X at 40 s
  (DEMO_SKIP= to disable): the demo's Namek fight now starts ~240 s instead of ~324 s, so RSX_CAP frame numbers
  from before (10200-10300) no longer match. Kamehameha: particle vertex buffers are all zeros at draw time
  (FINDINGS); next step is the AI assistant's.
- Parked (the maintainer 2026-10-24, after the skill effects): the 3rd attract-demo fight (game left at the title) shows a
  character with missing textures and other errors; seen only briefly before the run ended (600 s cut-off).
  To look at it: run the demo longer than 600 s with X at 40 s and screenshots past ~480 s.
- 2026-10-24 evening (the maintainer away, standing approval for the skill-effect fix): Kamehameha chain so far (FINDINGS.md,
  "Goku's Kamehameha" bullets): SPU image 17 (job 857F80) skips every particle because record byte +0x121 bit 0x02
  is never set (no setter in the EBOOT); SPU_FORCE_REC_FLAG=17 (opt-in, 2cd9b46) makes it emit non-zero but
  zero-size quads near the origin. Diagnostics added (all off by default, local commits up to 8614f08): VP_CODE*,
  SPU_DMA_RANGE_AFTER/_N/_PUT + img=, SPU_SDCRZ_RANGE, SPU_DMATRACE_AFTER/_N/_LSDUMP/_LSDUMP_ANY, SPU_INTERP_IMG,
  PPU_GUARD_AT_DMA, PPU_GUARD_OFF_LO/_HI/_STRIDE (+new= value, guard-set stacks), SPU_ITRACE_*, beam-clock, fifo-queued,
  GCM_FLIP_AT_WORD (opt-in, no effect). Recompiled-code logging: ppu_recomp_033.cpp B8LOG_FLIP ([B8LOG]/[D100]),
  backup ppu_recomp_033.cpp.pre-b8log. perf\scratch\laser is now large (see du in the thread).

## Assistant pause point (2026-10-25 ~02:05 local, the maintainer: pause; standing approval ended)
State: game closed, no runs active. ps3recomp-src jstars-port HEAD 5d95d20, working tree clean; build\ built at
5d95d20 plus the recompiled-code logging in jstars\port\recompiled\ppu_recomp_033.cpp (B8LOG_FLIP gate; backup
ppu_recomp_033.cpp.pre-b8log). Defaults unchanged since 5a77662 except none (all new switches opt-in).
1. **Kamehameha**: parked "needs RPCS3 ground truth" (FINDINGS.md "Pickup (2026-10-25 ...)" bullet): record +0x121
   bit 0x02 never set; with SPU_FORCE_REC_FLAG=17 the (progress, max) pairs +0x108/+0x10C, +0x110/+0x114, +0x60/+0x64
   stay (0, 1.0) -> zero-size quads. An AI assistant asks the maintainer whether they can read a beam record in RPCS3.
2. **2nd demo fight freeze** (just started): tools\demo_freeze.sh (X skip, 720 s, screenshots + `threads` every 10 s
   from 450 s). Run 1 (perf\scratch\freeze\run1, no GCM_FLIPCOUNT): the 2nd fight (Naruto, Hidden Leaf) ended
   normally (title cards 560-570 s, "Now loading" 580 s), a white line at the top in t590, then no screenshot after
   590 s (snap stalled; `threads` dumps at 600/610 still answered). Next: rerun with GCM_FLIPCOUNT=1 and compare
   threads_580 vs 600/610 to see what stops (the stop seems to be at the load into the 3rd fight, which matches
   The maintainer's "3rd demo fight looked buggy, then the game closed").
3. Glow layer stays parked (the maintainer). Live stick input is done (1f52c30).
Disk (perf\scratch, today): laser 5.9 GB total (was 4.3 yesterday evening), fxcheck 0.49 GB, freeze 30 MB,
stick 50 MB; nothing deleted.
- 2026-10-25 rule (the maintainer): CPU boost stays off when idle. jstars\run_test.ps1 now runs tools\boost\boost_on.bat
  before starting the game and boost_off.bat in its finally block (also on failure/stop); JSTARS_NO_BOOST=1 skips.
  Every test script goes through run_test.ps1. Backup: run_test.ps1.pre-boost.
- 2026-10-25: FIFO_FAST (fix 1; ps3recomp-src f4784cd v1 + 5160f10 v2) stays default-on, the maintainer approved.
  FIFO_FAST=0 = old path. Demo +0.6 f/s mean over 4 pairs, no visual change (CPU_PROFILE §29).
- 2026-10-26 00:xx paused for the night (the maintainer). The AI assistant state: ps3recomp-src jstars-port HEAD d1ea59b
  (vfa-wake, default on per patch, NOT approved as default yet; VFA_WAKE=0 = old). Built in jstars\port\build.
  Earlier today: f4784cd/5160f10 FIFO_FAST (approved default), 3f6d509 [PERF6], 0a30088 drawcost2
  (VPULL_MEMO/TEX_HINT, default-on pending the maintainer). Next: vfa-wake 4-variant A/B (CPU_PROFILE §34),
  then report. Nothing running, boost off, ISO unmounted.
- 2026-10-26 08:50 the maintainer changed Windows PCIe Link State Power Management from "Maximum power savings" to Off
  (expects little effect). Runs from today on (perf\vfawake) are after this change; compare with earlier
  runs with that in mind.
- 2026-10-26: vfa-wake (d1ea59b) approved by the maintainer as is: VFA_WAKE on by default (VFA_WAKE=0 = old),
  VFA_SPIN stays 0 (off). Neutral in the A/B, 7-9x fewer helper wake-ups (CPU_PROFILE §34). No build change needed.
- 2026-10-26 ~11:45 paused (the maintainer). The AI assistant state: ps3recomp-src jstars-port HEAD edf0bad (DRAW_SAMPLE sampler,
  off unless DRAW_SAMPLE=1). Both build\ and build-pdb\ built at edf0bad. Sampler results CPU_PROFILE §36.
  Open: the maintainer asked if the drawing thread's FIFO-kick waiting can be fixed; I proposed that an AI assistant extends the
  sampler to the PPU thread that kicks the FIFO, waiting for the maintainer's yes. drawcost2 default decision may
  still be open. Nothing running, boost off, ISO unmounted.
- 2026-10-26 15:50Z the maintainer away 1-2 h, temporary go-ahead: two AI assistants may test AND implement
  FPS fixes without waiting for the maintainer's yes (local commits, no push/delete; default-on only after real-flip numbers
  + clean screenshots on several maps, noted here). Goal ~30 f/s on all maps; then check every Main Menu entry
  (skip Online). Boost stays ON after runs during this window (no boost_off).
- 2026-10-26 ~19:00: without PERF, all tested maps hold ~29.1-29.6 f/s (CPU_PROFILE §42). HEAD f33a079
  (perf-cheap fa111b6, REC_DEDUP f33a079 default on, under test vs REC_DEDUP=0). Boost stays on (the maintainer).
- 2026-10-26 ~21:00 Main Menu check (perf\menus, sheet_0..8.png; one boot per entry, 3 CROSS deep then 3 CIRCLE
  back, no PERF): J-Adventure (story select -> New Game -> short black load -> "The Adventure Begins" book),
  Victory Road (intro, character select, area map), Arcade (character select, equip popup), J-Customize (deck
  editor, card slots), J-Point Shop (Battle Characters list with prices/preview), Gallery (Character Info list,
  Koro-sensei 3D model viewer), Options (Control Settings 1P/2P) all open, draw correctly and go back. No crash.
  Not covered: deeper actions (buying, saving settings, starting a J-Adventure/Victory Road battle, Battle Replay,
  Display/Sound/BGM/Cross-Save sub-pages). Online skipped. Free Battle covered by the fight runs.
- REC_DEDUP (f33a079) neutral but harmless, left on (CPU_PROFILE §43).
- 2026-10-26 ~22:00 deeper menu checks (perf\menus2, perf\menus3 sheet_*.png; save backup
  jstars\run\gamedata\dev_hdd0\home\00000001\savedata.backup-2026-10-26):
  J-Point Shop purchase works (Tsunayoshi Sawada bought, JP 10000 -> 7500, "Sold Out"; the maintainer also bought one by
  hand, and it persisted to the next boot = saving works). Options: Display, Sound, BGM Editor, Cross-Save pages
  open; camera setting Standard -> Inverted changes and sticks. Cross-Save Upload does nothing (needs PSN, like
  Online). Gallery Battle Replay: Watch Replay opens but no replays saved yet. Victory Road: area -> battle point
  -> stage select -> START -> intro cards -> fight runs normally. J-Adventure New Game: story book, then Luffy/Nami
  dialogue scenes play. No crash, no broken graphics.
- 2026-10-26 ~23:50: opening movie plays by default (FINDINGS "Opening movie plays"). HEAD includes e02020e,
  5c7b81e (VDEC_DEFER) + diagnostics; main.cpp VDEC_ALPHA=255 default; FFmpeg DLLs in jstars\port\build.
  Next: audio.
- 2026-10-27 ~01:10 paused (the maintainer). AUDIO WORKS with SPURS_CTX_PAD=1 (ps3recomp-src 8d34572, opt-in): CRI SPU jobs
  needed the job-descriptor LS address in CellSpursJobContext2's pad bits (FINDINGS "Audio: silent").
  Next: the maintainer listens (perf\audio\a12\clip_*.wav) and decides default-on; A/B fps cost (Hidden Leaf 27.6 vs 29.1
  earlier, single run). Diagnostics left in: JQ_DBG (jstars\port\stubs.cpp), AUDIO_CRI_DBG
  (ppu_recomp_035.cpp, backups *.pre-*). Boost turned OFF (the maintainer). Nothing running, ISO unmounted.
- The maintainer (2026-10-27 00:00Z) heard music and sound during the a13 run: "working but a bit glitchy". Measured 9-14%
  silent mix blocks in the fight (gaps / underruns, inferred cause). First task tomorrow: fix the glitches
  (CRI writes 5-block bursts per 16 ms tick; check underruns, read-index timing, WASAPI buffer), then default-on.
- 2026-10-27 ~11:45 paused (the maintainer: audio finished). State: ps3recomp-src jstars-port HEAD d4a3f2f (audio switches;
  SPURS_CTX_PAD default on 954fd8e). jstars\port\main.cpp defaults VDEC_ALPHA=255 and CRI_LEAD=7; CRI_LEAD hook +
  AUDIO_CRI_DBG probe in jstars\port\recompiled\ppu_recomp_035.cpp (backup .pre-audiodbg); JQ_DBG and
  GetBgmPlaybackStatus2 in stubs.cpp. Movie + sound work by default; fps ~29 on all maps without PERF.
  Raw audio captures (0.8 GB) moved to TO_DELETE (MOVED.txt). Boost off, nothing running, ISO unmounted.
- 2026-10-27: player launcher for the maintainer: PS3Recomp\Play J-Stars.bat -> jstars\play.ps1 (mount ISO, boost on, run
  build\JStarsRecomp.exe with no time limit, boost off + unmount on exit; logs run\play_*.txt). Tested: movie plays.
- 2026-10-27 14:05: sound gaps gone: main.cpp defaults AUDIO_NBLOCK=16 + CRI_LEAD=12 (0% silent in play). Awaiting
  The maintainer's play-test with Play J-Stars.bat.
- 2026-10-27 12:55Z the maintainer: sound sounded fine on the 16-block/lead-12 test, nothing wrong heard. Audio done.
- 2026-10-27 15:45: save boxes kept (game art); rename panel restyled like the shop box with OK/Cancel (919d5e1);
  PS3 wording in save/install/trophy texts -> "close the game" (2408fb1 + jstars_game.cpp, PC_TEXT=0 off);
  frozen-picture-after-movie fix CLEAR_PRESENT=0 (2a20f4c + main.cpp). Tests ren7/ren8/match8 fine (29.0 f/s).
  1P vertical camera is Inverted since the 10-26 options tour (asked the maintainer). Nothing running, boost off, ISO unmounted.
- 2026-10-27 16:30: shadow-vpxy (b0ed18b, opt-in) tested: no atlas on dojo/Hidden Leaf/Kameari; flicker caught in
  perf\shadows_env (FINDINGS "atlas ruled out"); results sent to an AI assistant. RESOLUTION.md (4K plan) awaits the maintainer's OK.
  Camera Inverted question still open. Nothing running, boost off, ISO unmounted. Added ~145 MB (perf\shadows_env).
- 2026-10-27 18:40: leaf-shadow flicker fixed: ZPASS_LOD0=1 default in main.cpp (c1d6364 patch by an AI assistant; =0 off),
  no fps cost (28.5 vs 28.1). Keyboard 3/4 = L3/R3 (5896aa6). Probes kept opt-in: ZPASS_VPXY (b0ed18b),
  ZPASS_FPLOG (3ebcc8c), FP_KILL fix (ede600e). Awaiting the maintainer's play-test. RESOLUTION.md awaits the maintainer's OK.
  Boost left on (the maintainer 15:39Z). Nothing running, ISO unmounted. perf\shadows_env ~0.7 GB.
- 2026-10-27 21:40: RES_SCALE committed (0114cf0): 1440p 29.0 f/s, 4K 28.4 f/s. "J-Stars Settings.bat" picks
  720p/1440p/4K and window/fullscreen. Queue: fp-bem patch (the AI assistant, needs the maintainer's OK), PR #11 test, d-pad keys.
- 2026-10-27 21:45: d-pad keys I/J/K/L, arrows = left stick only (073dc29, PAD_KEYS_SPLIT=1 default in main.cpp,
  backup .pre-padsplit). PR #11 tested from pr11.diff in a throwaway copy: all pass (builds with PY unset ->
  WindowsApps alias = pythoncore-3.15, PY set -> old path; tour ok). Waiting: The maintainer's OK for fp-bem (the AI assistant).
  Nothing running, boost on, ISO unmounted.
- 2026-10-27 22:00: fp-bem in (c063880, FP_BEM=0 off), window title shows render size (1c68eb8). All queued
  items done. Nothing running, boost on, ISO unmounted.
- 2026-10-27 22:30: Options Cross-Save is now Exit Game with a Yes/No box (EXIT_OPTION=0 off; ed2a7e6, eb1c57d,
  jstars_game.cpp, FINDINGS "Exit Game"). Open: The maintainer's answer on the shop (arrow keys no longer press the d-pad, so
  add-to-cart needs L). Nothing running, boost on, ISO unmounted.
- 2026-10-28 00:30: arrows press the d-pad again outside fights (2d0c8cb + jstars_game.cpp, MENU_ARROWS_DPAD=0 off;
  FINDINGS "Arrow keys"), waiting on the maintainer's try. Next: launcher patch (jstars-notes\patches\cellpad_keymap.patch) and
  LAUNCHER.md test plan. Nothing running, boost on, ISO unmounted.
- 2026-10-28 00:40: 7e3e780 key rebinds (launcher patch); closing the game window now ends the process
  (main.cpp, WINDOW_CLOSE_EXIT=0 off). Launcher test results written up. Nothing running,
  boost on, ISO unmounted, play_settings.txt restored (RES_SCALE=3, FULLSCREEN=0).
- 2026-10-28 03:15 PAUSED (the maintainer: continue tomorrow). Done tonight: Exit Game (Cross/Start, Cross-Save back-out
  fallback, Z/X in the box: 5056888), window close ends the process (main.cpp), arrows = d-pad outside fights
  (2d0c8cb + 0x0094DEC4), key rebinds (7e3e780), launcher tested (steps 1, 4 pass; 2, 5, 6 left for the maintainer by hand).
  Waiting on the maintainer: try arrows in shop/fight/pause, rebind + fullscreen in the launcher, Norton IDP.HELU.PSE95 path.
  Nothing running, boost left on (the maintainer's rule), ISO unmounted, play_settings.txt = the maintainer's (RES_SCALE=3, FULLSCREEN=0).
  All commits local on jstars-port, nothing pushed.
- 2026-10-29 02:20 PAUSED again (the maintainer). Since the last pause: input choice (INPUT_MODE, PAD_BTN_*, mouse camera,
  simple mouse menus, ce38c65), DualSense / Edge over USB+BT (a5676e7), TO_DELETE checked (evidence images returned).
  Waiting on the maintainer's hands-on tests (DualSense buttons, KB&M mouse, rebinds, arrows). Nothing running by me.
- 2026-10-29 18:00: point-and-click main menu (77b22a9), Exit Game after battle fixed (scene step 3),
  UI size for the fight HUD in research (FINDINGS "UI size"). Nothing running.
- 2026-10-29 21:00: fight HUD size (UI_SCALE, launcher slider) incl. flicker fix (scene step 0x13/0x14);
  instant main-menu hover (menu_jump); controller/keyboard as player 2; split screen parked (HUD stays full size
  there). Tests: INPUT_MODE=keyboard only. Commits 77b22a9 aaacd8e 9ccebda (+ cellPad P2 keyboard, uncommitted).
- Training mode (10-30): all three steps built and tested in scripted runs (FINDINGS "Training mode step 1/2/3"); waiting for the maintainer to test it before any new work (the maintainer 10-29: "wait with the next thing"). Step 3 requirement (the maintainer 10-29): the Training flag must turn off when leaving Training, so a later Free Battle in the same session gets normal health/KOs (the maintainer will also test it).
- Queued after Training mode (the maintainer via coordinator 10-29): try 3 fighters per team (third slot per team in the battle setup).
- 2026-10-30 ~12:50 local: own screen tracking, padlocks, Exit Game step 5, box look (table at the top). Waiting on the maintainer's test; no new feature started. Nothing running, boost on, ISO unmounted, nothing pushed.
- 2026-10-30 ~13:45 local: stuck box in 2-player Training fixed (table at the top). Nothing running, boost on, ISO unmounted, nothing pushed.
- 2026-10-30 ~13:50 local: all characters unlocked on the save (table at the top). Nothing running, boost on, ISO unmounted, nothing pushed.
- 2026-10-30 (fixed 11-02, see the entry at the end) bug (the maintainer): the enemy's health bar sometimes splits from its name plate: the bars move up and the plate is empty and black (Koro-sensei). Not looked into. The maintainer: no need to do it now.
- 2026-10-31 ~16:30 local: what starts a special mapped (WHAT_STARTS_A_SPECIAL.md). Changes: `ACT_TRACE`, `ACT_SWAP`, debug `zero` in the port (off in normal play). Tested in scripted Training runs mv1, at_a-at_d, sw2, keyboard input. Found: `.ocm` move graph; Jotaro's Star Platinum has a 0-byte model; his 3 specials start and spend Stamina without hitting. Not tested: his ultimate. Nothing pushed.
- 2026-10-31 ~17:00 local: Stand-in Star Platinum tried (WHAT_STARTS_A_SPECIAL.md, "Stand-in tests"). Scripted Training runs sb1, sc1, sc3, keyboard input: test packs load, no second body appeared, specials spend Stamina without hitting, ultimate (108) spent nothing and hit nothing. sc2 failed because my script hadn't built its pack. No game files changed. Nothing running, boost on, nothing pushed.
- 2026-10-31 ~18:00 local: Test Fight.bat added (TEST_FIGHT.md): `AUTO_FIGHT` in the port, off in normal play; `jstars\test_fight.txt` holds the default picks. Tested in scripted runs af1-af17 with keyboard input and normal startup at_n. Result: about 72-76 s from launch to a playable Training match; logos and menus still run quickly on screen. Nothing pushed.
- 2026-10-31 ~18:30 local: action log added (`act`, `mot` in balance_log.txt in Training; tools/balance/act_read.py), and Test Fight.bat keeps each session's log in jstars\run\test_fights. Tested: af18, at_n2, Test Fight.bat with and without numbers. Next: Balance one character at a time, the maintainer doing the moves. Nothing pushed.
- 2026-10-31 ~19:00 local: Luffy session read (the maintainer, Test Fight, 873 hits). docs/balance/000_luffy_new.md now ties 36 of 66 rows and 11 of 18 Stamina actions to inputs. New tools: tools/balance/char_sess.py, instances.py. Open: half-damage rule, remaining rows and skills. Nothing pushed.
- 2026-10-31 ~19:30 local: Second Luffy session read (the maintainer): docs/balance/000_luffy_new.md now has 42 of 66 rows, a mode table (inferred from play order), Second Gear refilling Stamina, and one Just Guard refund. Script: jg.py, copied to tools/balance. Nothing pushed.
- 2026-10-31 ~20:00 local: Ace session read (the maintainer, Test Fight). docs/balance/001_ace.md updated. Found: double damage is two hit records in one frame, Ace's Just Guard has its own rows, support actions aren't in the action lines. Nothing pushed.
- 2026-10-31 ~20:15 local: Second Ace session read (the maintainer): Fire Pillar is action 91, no Stamina of its own, only its first hit (100 of 750) landed in 8 uses. Test Fight set to Hancock next. Nothing pushed.
- 2026-10-31 ~20:30 local: Hancock session read (the maintainer, Test Fight). docs/balance/002_hancock.md updated. Test Fight set to Akainu next. Nothing pushed.
- 2026-10-31 ~22:00 local: Training settings menu added to the pause menu, with Unlimited Stamina (TRAINING_MODE.md). Tested in scripted Training runs pd2-ph1 and Free Battle run pi1, keyboard input. Not tested: Re-select, real controller. Burst from the start isn't done: gauge value and R3 check not found. Balance on hold (the maintainer). Nothing pushed.
- 2026-10-31 ~23:00 local: Training settings now a real sub-menu (the maintainer saw the first version unpause and move the selector). Opens like Restart's Yes/No: stays paused, selector stays. Tested in pj1, keyboard input. Restart and Free Battle checks are from the first version. Nothing pushed.
- 2026-11-01 ~00:30 local: Time limit lock in Training (the maintainer's request). Stage select holds Unlimited with no selector run; Cross, Left and Right do nothing on the box; a padlock replaces the left arrow and moves with the box. Tested in tm4, tm6 (Free Battle unchanged), tm7, and tmaf (Test Fight), keyboard input. New local renderer hook `g_rsx_stamp` in the toolkit, in toolkit/ps3recomp-local.patch. Faint patch where the right arrow was. Details in TRAINING_MODE.md. Nothing pushed.
- 2026-11-01 ~14:30 local: counted cards (CARDS.md). 276 in the game, 172 unlocked on the maintainer's save, 4 normal cards per character. Read-only: scripted runs cd1 to cd5 opened J-Customize, keyboard input, and the save was byte-identical afterwards. Save offset 0x49810 holds them as bits, matching all 60 per-character counts. Nothing pushed.
- 2026-11-01 ~15:00 local: `AUTO_MENU=<row>` and `tools/balance/menu_run.sh` reach a main menu row quickly for scripted checks (the maintainer's suggestion). Tested in mr1, row 5: 33.7 s to the row, 59 s for the whole run, save unchanged. Details in TEST_FIGHT.md. Nothing pushed.
- 2026-11-01 ~15:00 local: all 276 cards unlocked on the maintainer's save (their request). Bits set in memory, then the game saved them after a 500 JP coin purchase. Tested in mu5 and mu6: loads after a reboot, list shows 23 pages. Earlier save kept as `savedata.backup-2026-11-01`. Details in CARDS.md. Nothing pushed.
- 2026-11-01 ~16:30 local: Free Camera added to Training settings (the maintainer's request, a first step to a new mode). Camera moves while the fighters ignore the pad. Tested in cam3 to cam6, keyboard input: moves and turns, attacks blocked with it on, normal camera back when off. New local toolkit hook `g_cellpad_rstick_filter`, in toolkit/ps3recomp-local.patch. Nothing pushed.
- 2026-11-02 local: Name tag split fixed (the maintainer's 10-30 report). `ui_scale` in `jstars_game.cpp` went by position, so with UI_SCALE below 1 a tag near the top of the screen had its bars shrunk toward the top while its plate stayed. Tags are now told apart by draw order and height, and keep their size. Tested with UI_SCALE=0.7, keyboard input, Luffy against Koro-sensei: pf1 before, 237 of 271 frames with a tag near the top had mixed scales; pf3 after, 0 of 297. Normal fight pf4: top bars, player block and map still scaled. Normal Training fight with the tag up by the top bar (pf5, after the maintainer pointed out Turn-based has no top bar): 28 frames with a tag near the top of 1383 with a tag, none scaled, and the top bars scaled in 1362. Not tested: the maintainer's own stage, and split screen (the check sits after its early return).
- 2026-11-02 local: Clamped textures on unit 0 now use the filter the game asks for (`dr_t0_wrap` in `rsx_d3d12_backend.c`, local, in toolkit/ps3recomp-local.patch; `T0_CLAMP_LINEAR=0` turns it off). Name text no longer loses pieces of letters. Tested, keyboard input: 16-picture bursts at 720p (ph0 off, ph1 on) and 4K (pi0, pi1), menus and a fight (pg0, pg1), 30.0 flips/s on and off. Still open: the hyphen in Koro-sensei is missing in 2 of 16 pictures with the filter on, at 720p and 4K. Not tested: every menu; the first launch may build the extra shaders as each screen appears.
- 2026-11-02 local: Replays tested, no code changed, no bug found. Scripted 1 Player Free Battles, keyboard input: View replay plays from the intros through the KO and Victory Burst (rp6); Save Replay writes `savedata\NPUB31647-B2_000` (400,000 bytes, rp8); Gallery > Battle Replay > Watch Replay plays that replay to its end and its pause menu opens (rq2, rq3). J-Points on the save stayed 492399. That test replay is still in the maintainer's save; backup from before it: `savedata.backup-2026-11-02`. Not tested: Delete Replay Data, leaving a replay through its pause menu, replays of 2-player fights. Locked characters not tested: the maintainer's save has everything unlocked, so it would need an older save swapped in.
- 2026-11-02 local: Replay frame rate checked after the maintainer saw about 20 FPS while watching a test run. No code changed. One Free Battle at RES_SCALE=3, measured with `[flip]` lines over 10-flip windows (rm3, xs/cmp_live.py): live median 30.5, lowest 17.3; its replay median 30.4, lowest 21.3. Both have their dip at about the same point in the fight, so replays aren't slower. Other runs: saved replay lowest 23.8 (rm1), live fight lowest 22.8 (rm2). What happens at the dip was not looked into.
- 2026-11-02 local: Frame rate lows much smaller at 4K (the maintainer asked for stable 30). `rsx_d3d12_backend.c`; in toolkit/ps3recomp-local.patch): a depth bound on a texture unit the program doesn't sample is no longer copied, about 790 copies per frame down to about 4; `ZSNAP_ALL=1` turns it off. New `[PERF7s]` line. Tested at RES_SCALE=3, keyboard input: 2-on-2 Free Battles before (rn2, rn3) lowest 10.7 and 21.3, 1% 17.8 and 22.9; after (rx1-rx3) lowest 25.6-26.7, 1% 27.8-29.1. All 12 stages 1-on-1 (sv0-sv11): 11 lowest 26.7 or better; stage 8 had short hitches in two runs out of three (10.3, 12.5, then 27.8). Picture same on stages 0, 6, 7 against `ZSNAP_ALL=1` (so0, so6, so7). Different random fights, so not one-to-one. Still open: not a locked 30, short drops to about 26, and hitches when a fight's first shaders and textures load. Not tested: at 720p or 1440p, and looked at by eye only on the 12 stage pictures.
- 2026-11-02 local: Checked the maintainer's jagged edges on Inferno Crags: no anti-aliasing in the port, and full-size depth copies make no difference (cr1, cr2; FINDINGS). No code changed. Sky not judged yet, no reference picture.
- 2026-11-02 local: Edge smoothing added (the maintainer's request): `AA_MODE=off|fxaa|fxaa2`, fights only, on the scene right after it is copied into the HUD target, so the HUD and menus aren't touched (`aa_pass` in `rsx_d3d12_backend.c`, `aa_mode` in `jstars_game.cpp`, local, in toolkit/ps3recomp-local.patch). Launcher: Edge smoothing on the Display page, default FXAA. Tested at RES_SCALE=3, keyboard input: aa_off, aa_fxaa, aa_fxaa2 all 30.0 mean, lowest 29.1; 2-on-2 with FXAA ab1, ab2 mean 29.9, 30.0, lowest 22.1, 27.8. Launcher opened and pictured, not tested through Start Game. SMAA lookup textures downloaded to `ps3recomp-src/libs/video/third_party_smaa` with the maintainer's yes; SMAA not built, waiting on the maintainer's answer about `SMAA.hlsl`.
- 2026-11-02 local: SMAA added as `AA_MODE=smaa` and in the launcher (`third_party_smaa`: SMAA.hlsl, AreaTex.h, SearchTex.h, MIT, downloaded with the maintainer's yes; `smaa_ready`, `smaa_draw`, in toolkit/ps3recomp-local.patch). Tested at RES_SCALE=3, keyboard input: aa_smaa mean 30.0, lowest 27.8; 2-on-2 ac1, ac2 mean 29.9, 30.0, lowest 22.9, 27.8. First use takes 376 ms (shader compile) once per launch. Not tested: starting from the launcher's Start Game, RES_SCALE 1 or 2.
- 2026-11-02 local: Full-size smoke and effects (the maintainer's request; RPCS3 shows the same rim, so it is how the game draws). `FX_FULL=1`: the 640x360 effect layers and their depth get twice the scale, their 8-sample alpha blur (code 0xF01DA36B) is removed and depth copies are full size (`target_scale`, `rsx_d3d12_backend.c`, in toolkit/ps3recomp-local.patch). Launcher: Full-size smoke and effects, on by default. Tested, keyboard input: fx1 pan, rim far narrower; fy1, fy2 2-on-2 at RES_SCALE=3 mean 30.0, 29.9, lowest 25.6, 24.6; fz1 at RES_SCALE=1 (FINDINGS). The glow: see FINDINGS, the glow draws do run. Not tested: the launcher's Start Game with this setting, every map, menu scenes using these layers.
- 2026-11-02 local: Glow compared by eye: the maintainer's Kamehameha screenshots (taken as RPCS3 from the spinner and full-size HUD, inferred) against the port at RES_SCALE=3, FX_FULL=1, SMAA, stage 12 Kameari (xs/go2). White core, cyan halo, rays, scene tint and banding all match, so no glow fix was made. Different stage and moments, no pixel comparison. Their whole-screen white frame was not caught in the port. No code changed.
- 2026-11-02 local: Turn-based row hidden until the maintainer continues with the mode; `TURN_BASED=1` brings it back (`tb_list_rows`, `jstars_game.cpp`). Tested at the maintainer's settings (RES_SCALE=3, UI_SCALE=0.70, SMAA, FX_FULL=1), keyboard input: of5 Training list has 2 rows, a fight, Re-select, a second fight, Return to main menu, no [tb] lines, no Support packs loaded, slots locked, lowest 14.6 and 1% 25.6; of2 Arcade through character select into a fight, lowest 13.1, 1% 27.8. Free Battle ran in the same day's rx, ac, fy runs, before the row was hidden. Launcher's Start Game opened the game and wrote AA_MODE=smaa and FX_FULL=1 to play_settings.txt; closed at the menus, no fight from there. Not tested: reaching the end of an Arcade run, J-Adventure fights.
- 2026-11-03 local: Training: the time limit padlock fades with the stage select fade; health refills 1 s after the combo number goes away (tested cf1, cf4). Victory Gauge always on: not done, the marker value isn't found yet (TRAINING_MODE.md).
- 2026-11-03 local: Knockdown: the attack slot's reaction byte +0x39 (6 = knocked away) decides it; lowering it to 1 stopped Gintoki's normal knockdowns (kd3). Get-up immunity not found yet. Victory Gauge on hold. docs/balance/Shared.md.
- 2026-11-03 local: Knockdown map: every reaction number tried on the dummy (kv1); docs/balance/Knockdowns.md lists 1638 knockdown hits across 40 fighters by row. Projectile reactions not found yet.
- 2026-11-03 local: Each fighter's Balance page now has a Knockdowns section (row, move, hit, number, damage), written by jstars-notes/tools/balance/gen_kd.py.
- 2026-11-03 local: Knockdowns: projectile reaction at row +0xAF (kd4); build-up limit 150 at health obj +0x1A4 (st1); get-up immunity ends about 2.0 s after the get-up starts, dmg_recover +0x40 = 64 sets it (4 = instant stand, pin5). Map and all character pages updated. Test switches PIN_DUMMY, KD_BULLET, KD_STUNFILE, KD_CLEAR, KD_DUMP, OAM_RECOVER.
- 2026-11-03 local: Training: no knockdowns (attack and projectile files flattened at the fight start, build-up held at 0) and the opponent held 2.5 in front of Player 1. Tested nk4 on Gintoki, Goku, Naruto: no knockdowns, hits without walking (TRAINING_MODE.md).
- 2026-11-04 local: Smaller fight HUD (UI_SCALE): the Victory Burst banner no longer splits apart. A frame with its "Friendship Burst!" text keeps the bottom of the HUD at full size (the fight HUD is hidden then). Tested at 70%: 2 bursts, banner whole, normal HUD stays 70%, no false triggers (jstars_game.cpp ui_scale; FINDINGS "UI size").
- 2026-11-04 local: Training settings: new "Hide HUD" row (for screenshots). On hides every fight HUD part, name tags included; the fight and effects stay; the HUD shows again while paused. Tested by script (hud4): starts Off, on hides it, off brings it back (jstars_game.cpp ui_scale and k_settings, text slot 21; TRAINING_MODE.md "Hide HUD").
- 2026-11-04 local: Tester's empty menus: their debug log was cut off by one repeated harmless warning; our menu has the same 102 draws and draws fine on NVIDIA, AMD, WARP and with GPU validation. Toolkit backend: D3D12_WARP, D3D12_GBV and the message drain change, local.
- 2026-11-04 local: Tester's empty menus fixed: Windows 10 can't create compressed textures whose size isn't a multiple of 4 (menu pieces are 512x78 DXT5); those are now decoded on the CPU. Tested here: main menu decodes four of them and looks as before. Toolkit backend (local) and installer patch 0005.
- 2026-11-04 local: AUTO_MENU/AUTO_FIGHT walk presses Left and Cross in turn on the title, so it gets past the no-save question (the maintainer: Left then X). Tested from an empty hdd0: main menu in 90 s (gpu_test nosave1). Test only.
- 2026-11-04 22:39 local: The Windows 10 tester reported that their game works now with the new setup (menu texture fix); reported to the maintainer, not checked on a Windows 10 PC here.
- 2026-11-04 local: Installer: the game window uses the installer's icon (WINDOW_ICON, installer patch 0007) and the window title shows FPS only with TITLE_STATS=1 (settings window checkbox "Show FPS in the window title", off by default in an install). Tested on a fresh install: title "J-Stars Victory VS+", icon loaded (log).
- 2026-11-04 local: FPS counter: FPS_COUNTER=1 draws "FPS n" small in the bottom-left corner (g_rsx_stamp2, a second picture slot in the toolkit); hidden until the title screen (scene step 4), fades in over 0.5 s. J-Stars Launcher has a checkbox "FPS counter (bottom left)" (window 24 px taller). Tested: main menu crop (gpu_test fps1), boot to title (title3: no counter on logos or movie, shown on the title).
- 2026-11-05 local: 6 fighters, first look: the battle setup table has 6 slots (4 fighters + 2 Supports). Forced 2v2 works; a playable character in a Support slot loads as a Support. No room for more fighters without changing the game code (FINDINGS "Battle setup table and 6 fighters"). Test tools SIX_PROBE, FORCE_SLOTS.
- 2026-11-05 local: 3v3 research: forced 3v1 and 1v3 play; Support slots are decided in battle code not found yet (FINDINGS "Battle setup table and 6 fighters"). Test switches SIX_PROBE, FORCE_SLOTS (slot:id:ctl:stars:team), SIX_POKE, SIX_ICALL in jstars_game.cpp.
- 2026-11-05 local: Our build back to the normal Training Mode (the maintainer's call): the 11-03 hold-in-front and no-knockdown parts are off by default (TRAINING_HOLD=1 / TRAINING_NO_KNOCKDOWN=1 bring them back). Tested (hudtest normtrain): no "opponent held" or knockdown-file lines, hits land, Hide HUD on/off. The FOR_TESTERS installer (11-04 23:17) already had this Training Mode and Hide HUD.
