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

## Online

Unlocked titles and icons are visible to online opponents. The cheat is accepted online by the project's choice (confirmed by the maintainer). This may change when online play is ready.

## How it works

`installer/port/jsp/jsp_unlock.cpp` hooks the save manager's update (`func_0014E898`). After each update, when the
live save buffer passes the game's own SYS-DATA check (`func_003F91E0`, mode 0) and is not yet fully unlocked, the bytes
are written into the buffer and the checksum is restamped (mode 1, what the game does before every save). A buffer that
fails the check is left alone. The file on disk is only ever written by the game's own save. The game's debug
unlock function (`func_003FD568`) is not used (it wipes progress) and the per-item setters are not used (they fire
trophy checks).
