# Test Fight: open the game straight into a Training match (2026-10-31)

`Test Fight.bat` in the PS3Recomp folder starts the game and walks itself through to a Training match. Normal play isn't affected: `Play J-Stars.bat` doesn't set the switch.

## How to use it

Run it with numbers (fighter, opponent, support, and optionally a stage):

```
Test Fight.bat 39 28 205 2
```

With no numbers it reads the first line of `jstars\test_fight.txt`. Character numbers are in `docs/balance/README.md`. Stage numbers follow the game's own list: 2 gave Alabasta and the default is 11, Yoshiwara.

Scripted runs set `AUTO_FIGHT="<fighter> <opponent> <support> [stage]"` themselves. `tools/balance/af_run.sh` (a local script, not in this repository) is the runner, and it waits for the `[auto] fight` line in the game's log.

## Timings

From launch to being able to move: **about 72 to 76 seconds**, against 4 to 5 minutes for the old scripted route. The old route's wait times were built into the script, so that compares scripts, not what a person pressing quickly would get.

| Part | Time from the first pad read |
|---|---|
| Boot before the first pad read | About 12 s (not in the times below) |
| Logos, save load, opening | 0 to 22 s |
| Title and save dialog | 22 to 31 s |
| Main menu to stage select | 31 to 38 s |
| Time limit presses | About 5 to 9 s |
| Loading the fight | Up to about 55 s |
| Intros, with Cross + Start | Up to about 60 s |

## How it works

`auto_fight` in `jstars\port\jstars_game.cpp` runs on each pad read. It presses what a player would press and checks the screen it's on, so there are no fixed waits for a screen to appear.

- Skips: the opening movie, the Free Battle box, and the fighter and support intros (Cross + Start).
- Time limit: Training's lock holds it at Unlimited (TRAINING_MODE.md, "Time limit lock on Stage select"), so the auto-pilot passes straight through. It used to press through the box.
- Stage: Cross opens the list, the value goes into stage select +0x2E4, Cross confirms. Start does nothing while the list is open, so it then alternates Start and Cross.
- Ready signal: the first state P1 enters (`0x0004296C` with state +4 = 0). In af12 this came about 4 s after scene 0x10, once the intros had ended.

## Going to a main menu row

`AUTO_MENU=<row>` uses the same walk but stops on that main menu row and hands the pad back: 0 J-Adventure, 1 Victory Road, 2 Arcade, 3 Free Battle, 4 Online, 5 J-Customize, 6 J-Point Shop, 7 Gallery, 8 Options. Off in normal play.

`tools/balance/menu_run.sh <out dir> <row> <session script>` (a local script, not in this repository) runs it and refuses to start while the game is running. The session script can use `press` and `snap`.

**Tested:** run mr1, row 5, keyboard input. The game reached the row 33.7 s after the auto-pilot started, and the whole run opened the card list and closed in 59 s. The old tour script waited over 100 s for the title alone (run cd5). The save was byte-identical afterwards. Only row 5 was tried.

## The log of what a fighter does

Every Training session writes `jstars\run\balance_log.txt`, new each launch. `Test Fight.bat` keeps a copy in `jstars\run\test_fights` when the game closes, named after the fighter and the time. It now holds every button press, action, motion, Stamina change and hit.

To read one:

```
python tools\balance\act_read.py jstars\run\test_fights\<file>
```

`act_read.py` is a local script and is not in this repository. It prints a timeline such as `20.86 action 88 atk_special_1`, then `20.89 stamina 1000 -> 900`, then `21.42 hit ... damage 300`, and ends with a count of every action seen. `--all-slots` adds the other fighters and the motion numbers.

Tested in af18 (Gintoki, scripted presses): Square chain, Triangle, Circle, R1 + Circle, R1 + Square and a dash all appeared with the right actions, Stamina and hits. Small Stamina changes under 20 are left out of the timeline, and the raw log has them.

Just Guard doesn't appear as a state, only as a motion started with action 21, so `act_read.py` prints it from the `mot` line.

The `mode` number comes from the fighter's `.ocm` list. Its meaning isn't known yet: Gintoki's Circle shows 17 where Jonathan's shows 2.

## What it doesn't do

**It doesn't skip the logos, title and menus entirely.** They still run quickly on screen, and Cross doesn't skip the first 22 seconds. Writing the scene word `0x0094DEC4` doesn't help: the game accepts it, but the logos and intros run for the same time (af10, af11), so that word only reports progress. The code that really moves between scenes wasn't found. The most a true direct start could save is about 30 seconds of the 72, and it would also skip setup such as the save load, so bugs seen in that state might not be real ones.

## Evidence

Runs kept outside the repository (the run names are not in this repository):

| Run | Picks | Result |
|---|---|---|
| af16 | 28 32 7 | Fight at 60.5 s in the game's log, 72 s from launch |
| af17 | 39 28 205 2 | Fight at 63.7 s, 76 s from launch. Screenshot: Jonathan against Gintoki, Alabasta, support Heihachi, Time ∞ |
| af13, af14, af15 | Three matchups | Reached the fight, with the 299 timer bug that af16 and af17 fixed |
| af10, af11 | Scene word writes | No real time saved |
| af12 | Scene and action log | Found the ready signal |
| at_n, at_n2 | No switch | Normal startup and the Free Battle box still work |
| Test Fight.bat | With and without numbers | Reached the fight in 72 and 76 s |
| af18 | 28 32 7 with presses | Action log checked |
