# Reaching the main menu (2026-10-21)

## Method (no code changes)

`cellPad.c` already has `PAD_FILE=<path>`: each poll, a button mask written to that file is pressed
(default 40 polls; `"0x0040 6"` = 6 polls for single menu steps) and the file is emptied.
1. Run: `PAD_SCRIPT=30:0x4000,33:0x4000,36:0x4000,39:0x4000` (skips the movie) +
   `PAD_FILE=jstars\run\pad.txt`, `run_test.ps1 1200`.
2. Screenshot any time: `powershell -File jstars-notes\tools\snap_now.ps1 -Out <png>`.
3. The demo cards start on their own; **START** (`0x0008`) leaves the demo to the title.
4. On the title, **START does not open the menu** (the demo restarts); **CROSS** (`0x4000`) does.
5. With no save: "There is no save data for this game. Create new save data?". **No** returns to
   the title. **Yes** (LEFT `0x0080` to select, then CROSS) → "Save data created successfully." →
   CROSS → **Main Menu**.
6. Masks: START 0x0008, UP 0x0010, RIGHT 0x0020, DOWN 0x0040, LEFT 0x0080, CROSS 0x4000,
   CIRCLE 0x2000, TRIANGLE 0x1000.

**Save data now exists**: `jstars\run\gamedata\dev_hdd0\home\00000001\savedata\NPUB31647-B0\`
(1.4 MB). With it, later runs skip the save prompt and the demo/title path may differ. Move that
folder aside to get the old first-boot behaviour back.

## What it looks like (`menu\*.png`)

| screen | file | fps | notes |
|---|---|---|---|
| Save prompt / created | `save_prompt.png`, `save_created.png` | ~30 | render correctly |
| Main Menu | `main_menu.png`, `nav_*_down.png` | **30** | all 9 items, art and help text correct; **top-right logo is scrambled red blocks**; DOWN moves the highlight, art and help text update |
| Free Battle | `freebattle_3.png` | 30 | 1 Player / 2 Players; same scrambled logo |
| Character select (1 Player) | `charsel_1.png`, `charsel_4.png` | 30 | portraits, slots, button bar correct; **top-left title bar is empty**; **black speckle noise next to the CPU difficulty stars** |
| Demo card, Naruto (forest) | `demo_naruto_1.png`, `demo_naruto_2.png` | | tree foliage drawn as hard-edged black-rimmed leaf cards |

Input and navigation work (CROSS confirm, DOWN/LEFT move). Nothing stuck or crashed in ~20 min.
For analysis: scrambled logo (main menu, Free Battle), empty title bar and star noise (character
select), Naruto foliage.

## Into a Free Battle match (2026-10-21)

Path: character select → **START** (Finish, all random) → **Stage Select** (Yoshiwara Paradise,
BGM, time limit; renders cleanly, `stage_select.png`) → **START** → loading screen → "Now PRESS ✕" →
**CROSS** → intro cards for all six characters (~60 s) → fight.

| screen | file | fps | notes |
|---|---|---|---|
| Loading | `match_1.png`, `loading_press_x.png` | 30 | **plain white background**, only "Now loading…" / "Now PRESS ✕" (compare with RPCS3) |
| Match intro cards | `fight_3.png`, `fight_6.png` | 30 | Goku has dark slivers through him, **Luffy blotchy**, **Gintoki's face shattered into shards**; thin dark lines on stage geometry |
| Fight | `fight_hud.png`, `fight_later_*.png` | **13-27, mostly ~20-27** | HUD complete (health bars, portraits, minimap, timer counting down); **large black slab over part of the floor**; Goku partly drawn behind the floor plane; dark lines across the room |

Input in the fight: **works**. CROSS = Goku jumps (`fight_jump.png`); SQUARE = he turns/attacks
(minimap rotates); D-pad RIGHT switches the lock-on mode Normal → Focus (`in_5_right.png`).
Walking needs the **analog stick**: `PAD_FILE` only presses buttons, and `PAD_STICK` is a fixed
value set at startup. Driving the stick live would need a small `cellPad.c` change (ask first).
The game kept running (~20 min, ~1 GB RAM), no crash.
