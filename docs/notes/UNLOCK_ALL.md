# Unlock everything (cheat)

ENHANCEMENT (opt-in, off by default). Unlocks, in the game's loaded save: all 276 cards, J-Points (999999), the three
gauges (Friendship, Effort, Triumph at their cap of 36000), the battle characters and support characters, 156 titles,
134 player icons, the deck slot sets and J-Stars decks 02-05, and the Bronze, Silver and Gold coins (99 each).

## How to use it

Launcher, "Cheats" tab: tick "Unlock everything (cheat)" and press Start Game. Or set `JSP_UNLOCK_ALL=1` in the
environment when running the game yourself (then nothing backs the save up: copy the folder first).

- Before the game starts the launcher copies the save folder (`NPUB31647-B0` on the US disc, `NPEB02204-B0` on the EU disc; the launcher looks for both) beside itself as
  `BACKUP-<folder>-<date>-<time>` and checks the copy (file count and bytes). If the copy fails the game is not started.
- The switch is for one start: the box unticks after use and is never written to the settings file. A `JSP_UNLOCK_ALL`
  line in the settings file is ignored by the launcher.
- The game writes the unlocks into the save itself, with its own checksum, at its next save. It cannot undo them. The
  only way back is the backup: close the game and put the backup folder back under the original name.
- If the game has been run here before (its own data exists: the `<hdd0>_gamedata` folder or the first-boot install `<hdd0>\game\<id>-INSTALL`)
  but no save folder is found, the launcher refuses to start the game with the cheat on. The message names every folder it looked for.
- A fresh install where the game has never run starts normally with the box ticked (nothing to back up; the log says so). The
  cheat edits a save that has been loaded, so it takes effect from the second start on, once the game has made a first save.

## Installer option and the separate test save

ENHANCEMENT (opt-in, unchecked by default). The installer has the task "Unlock everything (cheat)" with an exclusive choice below it:

- **Use a separate TEST save** (recommended, selected when the task is ticked): the unlocked play uses a second save set. Your normal save is not read or written by the game while it is in use.
- **Apply it to the normal save**: the launcher asks (default No) before it unlocks, then makes the dated backup as above.

The task writes only `jsp-install.ini` `[install]`: `unlock=1|0` (the launcher's box is ticked at start when 1) and `savemode=test|normal`. It unlocks nothing
itself: the unlock still happens only at Start Game, with the backup, and never silently on a normal save. Silent installs: `/TASKS="unlock,unlock\testsave"` or
`/TASKS="unlock,unlock\normalsave"`. Reinstalling resets `savemode` to what the wizard chose (normal when the task is not ticked).

**How the two saves sit side by side.** The game picks its save folder as `<PS3_GAMEDATA_ROOT>\dev_hdd0\home\00000001\savedata\<NPUB31647-B0 or NPEB02204-B0>` (ps3recomp `cellSaveData.c`
`adopt_gamedata_root`: the user id folder `00000001` is fixed in the runtime, cellUserInfo reports that one user), and the same root holds the trophy files. The port defaults the root to
`<hdd0>_gamedata` (`installer/port/jsp/main.cpp`, an explicit `PS3_GAMEDATA_ROOT` wins). The first-boot data install lives under `PS3_HDD0_ROOT` (`<hdd0>\game\...-INSTALL`), not under that root. So the
launcher gives a test start `PS3_GAMEDATA_ROOT=<hdd0>_gamedata_test` and the 2.5 GB install is shared; no runtime change was needed. The value is set after the settings file lines, so no line there can
point a test start at the normal save. Checked: the path in the runtime source and the port default in `main.cpp` (read, and covered by `installer/tools/unlock_option_test.py`).
Not run: the launcher (it needs Windows to build) and a game start on the test save; ASSUMPTION: the game writes its saves only through cellSaveData, none is found in the port sources doing it with plain file calls.

In the launcher, "Cheats" tab: **Save in use**: Normal save / Test save, with the active folder named under it and `[TEST SAVE]` in the window title. The choice is kept (`savemode`) and cannot change while the game
runs. The first start on the test save copies your normal save folder into `<hdd0>_gamedata_test` (the normal side is only read; an existing test save is never overwritten), so the test begins from your progress.
Delete `<hdd0>_gamedata_test` to begin again from a fresh copy. The backup before an unlock covers the save in use. The log (`jstars19\run\launcher_log.txt`) names the save in use.

## Online

Unlocked titles and icons are visible to online opponents. The cheat is accepted online by the project's choice (confirmed by the maintainer). This may change when online play is ready.

## How it works

`installer/port/jsp/jsp_unlock.cpp` hooks the save manager's update (`func_0014E898`). After each update, when the
live save buffer passes the game's own SYS-DATA check (`func_003F91E0`, mode 0) and is not yet fully unlocked, the bytes
are written into the buffer and the checksum is restamped (mode 1, what the game does before every save). A buffer that
fails the check is left alone. The file on disk is only ever written by the game's own save. The game's debug
unlock function (`func_003FD568`) is not used (it wipes progress) and the per-item setters are not used (they fire
trophy checks).
