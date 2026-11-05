# Training Mode for J-Stars: plan (2026-10-29)

Plan for the maintainer, written before the build. **Built 2026-10-29/30**: what was actually done differs in places (slot locks, 0-star dummy, screen tracking); see FINDINGS "Training mode step 1/2/3" and the table at the top of STATUS.md.

Sources:
- `FINDINGS.md`: Exit Game, Point-and-click main menu, Arrow keys (memory sampler method), Deck rename / on-screen keyboard.
- `menu\*.png`; the EBOOT's own strings; the lifted code in `jstars\port\recompiled`.

## The design (the maintainer)

1. Pressing **Free Battle** on the Main Menu opens a small box: **Free Battle** (as before) or **Training Mode**.
2. Training Mode opens the game's **character select with Training's own setup**, the way J-Adventure and Arcade
   each have their own:
   - player 2 and player 4 slots are locked;
   - all other options are locked;
   - the time limit is Off/infinite;
   - the player picks their character and the opponent;
   - then Stage Select and start as normal.
3. The CPU star rating gets a **0-star option = training dummy**: the opponent doesn't move or attack.
4. **Nobody dies.** When health reaches 0 the fighter isn't KO'd and can keep moving. After 1-2 seconds without being
   hit, health refills to full.
5. **No rewards**, since a Training fight can't be won.
6. **1 Player / 2 Players (the maintainer, 20:05):** Training keeps Free Battle's own "1 Player / 2 Players" step before
   character select. The maintainer confirmed that 2-player split screen works.
   - With 2 Players, a human Player 2 takes a slot exactly as in Free Battle.
   - Slot locks only apply to slots that are CPU in the chosen setup.
   - The no-death and refill rules work per fighter, so they cover humans and CPUs alike.
   - This makes step 3 a little simpler: the game's own 1P/2P setup decides who is human, and the port only adds the
     Training rules on top.

## How it fits the game

- **Every mode already uses the same character-select screen.** The EBOOT's file names show one
  `progress_scene_character_select` and one `progress_scene_map_select` (stage select), used by Free Battle, Arcade,
  J-Adventure and others.
  - Each mode hands that screen its own **battle setup**: which slots exist or are locked, human or CPU, CPU level per
    slot, characters, stage, time limit.
  - J-Adventure's locked slots come from its setup (its battles are pre-defined, `mm_battlerule`), not from a
    different screen.
- **So Training is "Free Battle's screens with Training's setup".** Choosing Training sets a port-side Training flag.
  When character select opens with the flag on, the port writes Training's setup into the game's battle-setup data:
  - J-Adventure's lock flags on slots 2 and 4;
  - the time limit Off/infinite;
  - the other options fixed.

  The screen then shows and enforces them itself, as it does for J-Adventure. Anything the screen doesn't enforce on its
  own, the port enforces by ignoring presses on locked rows (the same input filter as the Exit Game box).
- To make it read as its own mode, the screen's title and help line can say "Training", using the same label and text
  tricks as Exit Game.

## Building blocks to find in game memory

All are found the way the AI assistant found the menu states: full memory dumps (`dumpall`) diffed between screens and
settings, and sampling during a fight. None of these addresses are known yet.

| What | How to find it | Used for |
|---|---|---|
| Battle setup: mode id, per-slot lock flags, human/CPU, **CPU level per slot** | character select reached from Free Battle vs J-Adventure vs Arcade; then one slot's stars changed 1 → 5, one dump each | presets, locks, the 0-star option |
| Time limit | Stage Select, Time Limit changed 300 → Off/∞ | Training locks it to Unlimited (see "Time limit lock on Stage select") |
| Fighter health, current and max | dumps before/after a hit, compared with the bar | no-death and refill |
| The game's damage / KO code | from the health address: the lifted code that writes it, and the check that starts the KO | no-death, without visual glitches |
| AI debug "pad dummy" | lifted code: where that field is read in the fight | 0-star dummy |

## The 0-star training dummy

- **Lead:** the game still contains its developers' debug system: "Debug Menu", "Debug Dip Switch & Param", "debug hp
  font" / "Hp : %d". The AI has debug settings named **"pad dummy"**, **"pad set"** and **"ai::debug"**:
  - strings at 0x7B3420 / 0x7B3430 / 0x7B3438, TOC entries 0x8CB984 / 0x8CB988 / 0x8CBA90;
  - "pad dummy" is copied into a named field in the constructor at `func_000120E0` (lifted
    `ppu_recomp_000.cpp:2826`, string reads at :3039 and :3075).
  - It looks like a switch that gives a fighter a **dummy controller**, which would make it stand still. This is
    inferred from the names: finding where that field is read and testing it on one CPU is the first job.
- **The 0-star choice:** in Training only, pressing left on the stars at 1 star goes to 0. The port remembers "slot N =
  dummy" and keeps the game's own level at 1 underneath, so the game never sees a level it doesn't know.
  - If the screen draws 0 stars cleanly when given 0, use that. Otherwise draw the empty star row over it, as the
    rename box is drawn.
  - In the fight, that slot gets "pad dummy" switched on.
- **Fallbacks** if "pad dummy" doesn't do it: clear that fighter's AI controller input every frame (find the
  per-fighter input structure), or give the slot to an idle Player 2 controller.

## Nobody dies, health refills after 1-2 seconds

**Possible, with one rule: the cleanest way is to stop the KO at its source, not to fix the health bar afterwards.**

- **Simple version (first try):**
  - every frame in a Training fight, for each fighter, if health is below 1, set it to 1;
  - remember when health last went down; once 1.5 s pass with no drop, set health to max. (Replaced on 11-03 by the combo-number rule, see "Refill timing" below.)
  - This only needs the health address (current and max).
- **The catch:** the game probably decides "KO" in the same frame the damage lands, before our per-frame fix runs. A
  hit that takes health to 0 could still start the KO animation, end the round or add to the victory gauge.
- **The robust version:** hook the game's own damage or KO code. Either let damage stop at 1 health, or skip the "health
  is 0 → KO" branch while Training is on. Then the fighter takes the hit and its reaction (stagger, knockdown) as
  normal, and simply never goes into KO.
  - This needs the code address: found from the health address by reading the lifted code that writes it. Once found,
    the change is a small override in the port (the port already overrides game functions this way, e.g. the pad and
    text hooks).
  - The refill timer stays in the port.
- **"Health shows 0 but they keep moving":** with the robust version, health can be allowed to reach exactly 0 on the
  bar while the KO branch is skipped, so it looks like 0. With the simple version it stops at 1, which is effectively
  invisible on the bar.
- **Refill timing (11-03, the maintainer):** health refills 1 s after the combo number goes away, not 1.5 s after the last hit. The gauges over the characters (class `0x00879DA0`) hold the combo number: +0x3EC is 1 while it shows and +0x3F0 the hit count; both drop to 0 when it goes (pf3: 0.9 s after a 3-hit string, 0.77 s after one hit, 1.4 s after two). `training_icall` collects those gauges and `training_tick` waits for all of them to be 0, then 1 s. With no combo number seen after a hit, the old 1.5 s rule applies. Tested (cf1): both refills came 1000 ms after the number went.
- **Works for both sides:** the player and the dummy both refill. The player can practise combos on the dummy forever,
  and the dummy never goes down.

## Rewards

A Training fight never ends with a win: nobody is KO'd, and there's no timer. The way out is the pause menu, which
normally gives no J-Points or records. That needs one check. If quitting from pause still counts something, the port
skips it while the Training flag is on.

## Build order and effort (one thread that can build and run J-Stars)

| Step | What | Sessions |
|---|---|---|
| 1 | Free Battle box (Free Battle / Training), Training flag, both continue into the game's screens | 0.5-1 |
| 2 | Memory dumps: battle setup (mode, slot locks, CPU level), time limit | 1 |
| 3 | Training setup on character select and stage select: slots 2/4 locked, options locked, time Off, "Training" title | 0.5-1 |
| 4 | 0-star dummy: confirm "pad dummy", the 0-star choice in the stars row, switch it on in the fight (or a fallback) | 1-1.5 |
| 5 | No death and the 1-2 s refill: health address, then the damage/KO hook | 1-1.5 |
| 6 | Rewards check after quitting from pause | 0.5 |

**Total about 4.5-6.5 sessions.** the maintainer can test after each step from step 3 on.
- The work is in `jstars\port\jstars_game.cpp` and the input filter, not the renderer.
- I can do much of steps 2, 4 and 5 read-only from the dumps and the lifted code, once someone who runs J-Stars captures
  the dumps.

## Training settings menu (2026-10-31)

In a Training fight, the game's own pause menu has a sixth row, **Training settings**. It opens a second menu in the same panel.

Run names below (pj1, cam5, tm4 and the like) are local runs and are not in this repository.

**Settings:** Unlimited Stamina (on/off). To add one, add a line to `k_settings` in `port\jstars_game.cpp` and what it does to `settings_tick`. Text slots still free: 14, 18, 21, 23, 25, 33 and 34.

**How it works:** it's a real sub-menu of the game's menu, opened the way Restart's Yes/No box is, so the game stays paused and the selector doesn't move.

- The pause menu object has vtable `0x00876710`. It keeps a stack of 0x58-byte levels from +0x3C: +0x3AC is the depth, level +4 the selected row counting from 1, and +0x30 the current table.
- `0x0049A490` opens the menu with a table number in r4 (3 here). `0x00498D48` finds a table as pack + the offset in that number's STPK entry. The pack is held by the parent object (+4). There are 37 tables, named in the pack.
- A table is a count at +0, then 0x4C-byte rows from +0x10: +0 type (0 root with the Pause Menu title, 1 text title, 2 row), +4 text number in the V_Pause pack, +8 next, +0x48 flags (bit 2: selected at the start). The panel resizes to the row count.
- **Next** decides what Cross does, in `0x0049A284`: 0x54 or less opens that table in place; 94 goes back; other values go to the game (93 opens Skill table, a separate screen).
- At each open in Training, `pause_open` points table 3 at a copy at `0x4B8FF000` with one more row, whose next is 12. Table 12 is an empty slot (`012_battle_replay_finish`), pointed at our table at `0x4B8FF400`. Outside Training both entries go back.
- Our table is built from Restart's Yes/No: a title, one row per setting, then Back. `pause_tick` catches Cross on a setting, flips it, and hides the press from the game. Back is the game's own go-back.
- Row text comes from the V_Pause pack (`PAU_NNN`, UTF-16 little-endian) and updates live. We overwrite replay and world map texts this mode never shows.
- `0x00498EE0` runs each frame while the menu is open, which tells `pause_up` it's open.

**First version, replaced:** it closed the menu and opened it again with another table. The maintainer saw the game unpause and the selector jump to Close menu.

**Tested** (scripted, keyboard input): pj1 for the sub-menu; ph1 and pi1 from the first version.

- pj1: Training settings opens with the game still paused, and On/Off updates live. Back and Circle return to the pause menu with the selector still on Training settings.
- pj1: Unlimited Stamina: Circle cost 100 before it was on and nothing after. Skill table still opens.
- ph1 (first version): Restart with Yes worked, and the row was still there after it. Not repeated on the sub-menu version.
- pi1 (first version): Free Battle shows the normal 5 rows. Not repeated on the sub-menu version; the code still only changes the tables in Training.
- **Not tested:** Re-select character(s), or a real controller.

### No knockdowns, opponent held close (11-03)

Off by default since 11-05: normal Training Mode is back. `TRAINING_HOLD=1` and `TRAINING_NO_KNOCKDOWN=1` turn the experiment on. The code is still in the game file.

For mapping every skill to its numbers (the maintainer's plan): in a Training fight, nobody gets knocked down and the opponent stands 2.5 in front of Player 1, held there, so every skill can be used on it without walking. Turn-based is left as it was. Numbers: Balance/Knockdowns.md.

- **No knockdowns:** 1 s into the fight, `kd_flatten_all` looks through 0x41000000-0x42000000 for attack files (0x9C-byte slots, reaction +0x39) and projectile files (232-byte rows, reaction +0xAF): the entry count, three zero words, and at least 85% of the entries with damage looking right. Every entry whose reaction knocks down is set to 1, a flinch, rows without damage too (Gintoki's Justaway knocked down from one, nk1). The first hit from a file the scan missed flattens it too (Dio's and Dai's attack files fail the check on disk). The old bytes are put back when the fight scene ends. The too-many-hits build-up (+0x1A4 of each hit fighter) is held at 0. Since 11-05 the game's own knockdowns are kept unless `TRAINING_NO_KNOCKDOWN` is set (the code only checks that the variable exists, so `=0` also turns it on).
- **Held close:** `pin_dummy` takes both places once per fight (`tb_fighters`) and puts the opponent on the line between the spawns, `k_train_apart` = 2.5 from Player 1, holding x and z (height free). Looking the places up every frame drifted with Player 1 (nk1). Since 11-05 `TRAINING_HOLD=1` is needed to turn it on.
- **Tested (nk4, scripted, 0-star Koro-sensei):** Gintoki, Goku and Naruto, each with Square strings, Triangle twice, Circle twice, R1 + Circle and 40 Square presses without walking: 45, 39 and 58 hits, and no knockdown, blow, bounce or get-up on the dummy in any of them (nk1 before the fixes: Gintoki's Justaway knocked it down once, then the knock-away carried it). Each run held the opponent once, at 2.5.
- **Not tested:** putting the bytes back after the fight (the scripted runs end inside the fight), Free Battle right after, a real controller, and fighters other than these three.

### Hide HUD

A setting in Training settings (the maintainer 11-04, for screenshots). While it is on, every fight HUD draw goes to a point off screen: bars, portraits, map, timer, name tags and markers. The fighters, stage and full-screen effects stay. While the pause menu is open the HUD shows again; it goes away when the menu closes. Seen in hud4 (11-04): the open menu read "Hide HUD: On" and the HUD came back while paused. The code has no pause-specific step for this.

**How it works:** `ui_scale` in jstars_game.cpp, the same hook as UI_SCALE, returns a size of 0.0001 with the anchor at (8, 8) for each draw into the HUD target `0x0C6ABF40` during a fight while `g_set_hidehud` and `training_active()` are true. HUD parts that this hook doesn't see would stay (FINDINGS "UI size"). `HIDEHUD_TEST=1` (test only) starts with it on; empty or 0 counts as off.

**Tested** (scripted, keyboard input, hud4): starts Off, the row reads "Hide HUD: Off/On", turning it on and closing the menu leaves a fight with no HUD for several seconds, turning it off brings the HUD back. hud2/hud3 started with it on because the test script left `HIDEHUD_TEST` set but empty; the code now treats that as off.

**Not tested:** Victory Burst, supports and specials while it is on; 2 players; the maintainer's own play.

### Free Camera

A setting in Training settings. While it is on, the pad moves the camera and the fighters see nothing pressed except Start, which opens the pause menu. Turning it off gives the camera back to the game.

**Controls:** left stick moves, right stick looks, L1 and R1 go down and up, and holding Cross is faster.

**How it works:**

- Each fighter has a camera object, vtable `0x00874DF8`. +0x10 is the view matrix, +0x50 its inverse (rows: right, up, the way it looks, position), and +0x90 the position again. The third row is the look direction, not its negative.
- `0x0006FF10` steers it each frame. `training_icall` skips that call while the setting is on, and `freecam_write` writes both matrices on every pad read. It writes every camera seen in the last 500 ms; the lowest address is treated as the player's (reading).
- The right stick needed a new hook in the toolkit, `g_cellpad_rstick_filter` in `cellPad.c`. That change is in `toolkit/ps3recomp-local.patch`.

**Tested** (scripted, keyboard input, Akainu against a 0-star Koro-sensei):

- cam5: stick moves forward, right stick turns both ways, and the fighter stays put. Turning it off returns the camera behind the fighter.
- cam4: R1 raises it and the right stick looks up. It can pass through walls and leave the stage, showing an empty background.
- cam6: with it off, Circle and Cross start attacks (`act` rows in the log). With it on, Circle, Cross and the left stick add no `act` or `mot` rows.

**Not done or not tested:**

- A CPU fighter above 0 stars would still act, since only the pad is blocked. Fighters also keep their idle animation.
- The HUD stays on screen, and a fighter close to the camera fades into a see-through shape.
- Not tested: a real controller, keyboard binds for the right stick in normal play, Restart with it on, or a stage other than Yoshiwara.

### Time limit lock on Stage select

In Training the time limit is always Unlimited and can't be changed. A padlock replaces the left arrow and the right arrow is hidden. Free Battle is unchanged.

**How it works:** this replaces the run that pressed Right, Down, Cross, Right, Cross, which the maintainer could undo afterwards.

- The Stage select screen (vtable `0x0087FCF8`) holds the seconds at +0x78: 300, or 0 for Unlimited. Finish and Back both run `0x0026FAA0`, which copies +0x78 to the battle settings, field +0x18. The settings object comes from the getter `0x00270A9C`, and `0x00270CD8` picks the current mode's block.
- The time limit box (vtable `0x00880018`) shows its own choice at +0x40. Its table at `0x00765CA4` turns that choice into seconds: 0 is 0, 1 is 300.
- `stage_lock` writes 0 to both on every pad read while the screen is up, and hides Cross, Left and Right while the focus (+0x58) is on the box. The selector can still rest on the box, but nothing happens there.
- The padlock is a small opaque picture from `stage_lock_stamp`, drawn by a new hook in the renderer, `g_rsx_stamp` in `rsx_d3d12_backend.c`. That toolkit change is in `toolkit/ps3recomp-local.patch`.
- The boxes slide with their scene: box +0x28, then +4, then +0x1C is how far right of their place they are, 732 off the screen and 0 in place. The game is 2 frames ahead of the frame being drawn, so the padlock and the right arrow's patch use the value from 2 frames ago (`stage_slide_tick`).
- **Fade (11-03, the maintainer):** the padlock now fades to white with the screen. The stage select screen's state at +0x48 is 9 while it is up, 0xD on Start, 0xE while the screen fades to white (500 ms, probe pf1; the port times its fade on the wall clock, not in frames, so it can drift from the game's own fade when the game runs slow) and 0xF once it is white, then 0. `stage_slide_tick` turns that into `g_lock_white` (50 ms lag for the 2 frames), and `stage_lock_stamp` mixes each pixel toward white. Tested with back-to-back screenshots (cf3, cf4): mid-fade the badge stays within about 13 of the bar beside it and the gold follows. cf3 showed the padlock popping back for about 100 ms on the white screen when the state went to 0 before the box was hidden; cf4, after the fix, has no pop.

**Wrong lead:** `0x00968F10` follows the choice (1 at 300, 0 at Unlimited), but writing it and box +0x40 changed only the label. The fight's timer still counted down (run tm3 showed 288). Its purpose is unknown.

**Tested** (scripted, keyboard input):

- tm4: Cross, Right and Left on the box change nothing, and the label stays Unlimited. After going back to character select and returning, the fight shows ∞.
- tm5: padlock at 153 with the box at 406, so 2 frames ahead. tm7, after the fix: the padlock sits where the left arrow belongs in three mid-slide shots.
- tm7: a straight fade to the fight shows no leftover padlock in the shots taken, and the fight shows ∞.
- tm6, Free Battle: starts at 300 seconds with both arrows and no padlock. Cross then Right changed it to Unlimited, and that fight shows ∞.
- tmaf, Test Fight (`3 32 0`): reaches the match in 58.8 s, and the fight shows ∞.
- **Known flaw:** a faint darker patch shows where the right arrow was hidden, most visibly when the box is selected. It's copied from the bar beside it.
- **Not tested:** a real controller, or a Free Battle right after Training in the same session. Sliding was judged from screenshots, not video.

### Burst from the start: not done

**Victory Gauge always active (the maintainer 11-03): not done.** Found but not enough:
- The middle arc (HUD part `0x0087A1E0`) keeps a running total at +0x2C (+0x58 is it / 416.7 as a float), copied from `0x4902AC68` (the same address in four runs; found by searching memory on each change, vg1). It only goes up: 4600 after 11 boosted Gintoki strings (vg4, `BAL_TEST=2,0,200,300`), so it is the sum of gauge gains, not the marker.
- Holding `0x4902AC68` at 5000 or setting it to 1950 and hitting past 2500 changed the number only: the marker stayed near the middle, no R3 prompt, and R3 did nothing (vg2, vg3).
- In vg4 the marker moved slowly toward red over 11 strings but the prompt never came. The marker's own value is not in the arc object's first 0x400 bytes or around `0x4902AC68`. The arc's update (`func_00131DA4`) drives animations (`func_0032B288`), so the marker is probably an animation frame set from elsewhere.
- Test-only switches left in: `PROBE_FADE`, `PROBE_VG`, `PROBE_VG2`, `VG_SET`, `VG_ONCE`.

the maintainer's plan: a setting that lets R3 start Victory Burst from the start, and again when it runs out, with normal behaviour when off.

- **Gauge value not found.** Writing these did nothing to the marker: `0x48E03610`, `0x48E03658`, `0x4902F0F0`, `0x968F18`, `0x4A3DEF38`, `0x4973E588`, `0x48E03598`. Most of them are allocator or counter values.
- **What R3 checks isn't found either.** Lead: the move graph's condition handlers (46 of them, listed by `tools/balance/handlers.py`, a local script that is not in this repository). The indirect-call hook can skip a handler and set its result, so if the right handler is found, it can be forced true.
- **Useful recipe:** Luffy with Unlimited Stamina, tapping Circle in 14 rounds of 16 presses, moves the marker near full in about 2 minutes. Dumps of that were taken while the game ran (g0 middle, g1 near full) and are not in this repository. That run didn't reach the R3 prompt, so it needs a few more rounds.
- **Hit record +0x6C:** with `BAL_TEST` at 5000 per hit the marker still barely moved, and the amount goes into a Stamina call on the hit fighter (`0x00082B3C`, slot +0x74). So "gauge gain" in the Balance notes may not be a direct gauge amount (reading).

## The v18 tree (installer/port, 2026-11-05)

Training is ported to the `v18` tree (`installer/port/jsp/jsp_training.cpp`), steps 1 to 3. There is no menu choice
there yet: the environment variable `TRAINING=1` makes Free Battle run as Training. Without it nothing is hooked. With the
installer, put `TRAINING=1` in the settings file: `jsp-launcher.exe` puts each of its lines in the environment
(`installer/launcher/launcher.c`).

- **How it hooks:** v0.18.0 calls every indirect call through its function table, so the file wraps the lifted functions
  there (as the trace in `jsp_hle.cpp` does). No toolkit patch is needed. The screens are found by their vtable in r3,
  not by heap addresses.
- **Step 1:** nobody is KO'd (the damage call @0x00085B84 leaves 1 health), and health refills 45 frames (about 1.5 s,
  ASSUMPTION: 30 frames per game second) after a fighter's last hit. The refill runs from the HUD's health read
  @0x00085CEC.
- **Step 2:** the CPU fighters of the other team become dummies: their pad is handed to a human driver (+0x18 and
  +0x1FC, 3 to 1) and the pad getters @0x9D5AC to 0x9D750 answer "nothing pressed".
  - Fighter numbers in a fight are 0 Player 1, 1 the opponent, 2 Player 1's partner, 3 the opponent's partner. The
    v0.15 reading held only in 1 on 1.
- **Step 3:** on character select, the CPU partner slots (1 and 3) are locked with the game's own padlock. On stage
  select the time is Unlimited. Not ported yet: the padlock picture on the time box and the blocked presses there
  (the box can be changed, but it is set back at once).
- **Test-only switches:** `TRAINING_TEST_LOW=N` starts every fighter at N/100 health; `TRAINING_NODUMMY` keeps the
  opponents as CPUs; `TRAINING_NOLOCK` leaves the partner slots free.
- **Not done in v18:** the menu choice, the rewards check (no J-Points after Training), the Training settings menu,
  the 0-star dummy, and a check with 4 fighters.

Tested under Proton on Linux, US disc, with a scripted pad route (the same file and toolkit pin, built from this
project's development tree, not from the installer): a Training fight with TIME at infinity, 16 hits all on the dummy
and a refill after them; with `TRAINING_TEST_LOW=300` hits stop at 1 health and nobody is KO'd; without `TRAINING` a
normal Free Battle with the timer counting down. Not run on Windows.
