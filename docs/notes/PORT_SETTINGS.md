# Port Settings page (opt-in)

ENHANCEMENT, off by default. `JSP_PORT_MENU=1` gives the Options list a sixth row, "Port Settings". Cross on it opens a page drawn by
the port over the game (Up/Down select, Cross opens, Circle goes back). Its pages: Display, Graphics and Controls (the launcher's settings, below), Online (below), and a list of what stays launcher-only. To get the row
without the launcher's window, put the line `JSP_PORT_MENU=1` in `play_settings.txt` (every line of that file becomes an environment variable for the game; the settings window keeps lines it does not own). US 1.00 build only: the hooks sit at fixed addresses of that code,
and the glue refuses to start on another build (it reads the instruction it needs to find).

## What it is made of

| Part | Where |
|---|---|
| Menu label replacement: a table of (hash of a 512 x 64 label texture, art file), switched by the port | toolkit patch 2516 |
| Present-time overlay: a CPU-drawn RGBA layer blended onto the back buffer, closed (one flag test per frame) unless a page is open | toolkit patch 2517 |
| Label switch as a bit mask gated on the open panel; the "Port Settings" label | toolkit patch 2518 |
| Pad hooks the page reads and swallows input through | toolkit patch 6501 (already in the series) |
| The page: state machine, the two hooks on the game's functions, drawing | `installer/port/jsp/jsp_port_menu.cpp`, API in `jsp_overlay_api.h` |
| Online page text and the writer behind it | `jsp_online_describe`, `jsp_online_edit_*` in `installer/port/jsp/jsp_online.cpp` |
| The game's own row count (`li r25,5` at 0x001E71C0 becomes 6 behind a switch) | the lifter's `--insn-patches` (upstream ps3recomp v0.19.0 has it), file `installer/port/jsp/lift_insn_patches.json`, passed by `firstrun.py` for the US build |
| Label and glyph art, drawn from Fira Sans Heavy Italic (SIL OFL 1.1) when the installer is built; nothing from the game | `installer/tools/menu_label_art.py`, `menu_font_art.py`, copied next to the exe by `firstrun.py` |

The lift: with the instruction patch the lifted code reads the switch `g_port_menu_six_rows` (defined in `jsp_port_menu.cpp` whether or not the
page is on), so a US setup lifts again once (the lift fingerprint includes the patch file) and the build follows. With the switch off the game
runs the instruction as before. The EU build lifts without the patch (its code differs at that address and the lifter would stop).

## The settings pages (Display, Graphics, Controls)

The launcher's settings, editable in the game. Same file (`play_settings.txt`), same keys, same format: only the lines a page changes are
rewritten, comments and unknown lines stay, the file's own line ends are kept, a temp file beside it is renamed over it. Every change is saved at
once (as the launcher does), the page says "Saved. Applied now." or "Saved. Applies on the next start." A `*` after a value marks a saved value the
running game has not picked up yet. A failed save is shown on the page. `JSP_PORT_MENU_EDIT=0` removes these pages (and keeps Online read only).

Which file: the launcher (`jsp-launcher.exe` and both settings windows) names the file it loaded in `JSP_SETTINGS_FILE`. Started another way, the game uses
the first of `<install folder>\play_settings.txt`, `<exe folder>\play_settings.txt`, `%APPDATA%\JSP Fan Revival\play_settings.txt`, else it creates the
AppData one. The main page shows the path. The launcher's window closes with the game, so it never writes an older copy over a change made in the game.

| Page | Setting (file key) | When it applies |
|---|---|---|
| Display | Render resolution (`RES_SCALE`), Display mode (`FULLSCREEN`), Window size (`WINDOW_SIZE`), FPS in the window title (`TITLE_STATS`) | next start (read once by the toolkit; F11 still switches fullscreen in the game) |
| Graphics | Fight HUD size (`UI_SCALE`), Edge smoothing (`AA_MODE`), FPS counter (`FPS_COUNTER`), Shadow distance (`SHADOW_RANGE`), Model detail (`MODEL_DETAIL`), Object draw distance (`OBJECT_DISTANCE`) | now (the readers in `jsp_game.cpp` re-read when a page changes them; model detail and object distance are rewritten into the map objects already seen) |
| Graphics | Shadows (`SHADOW_SCALE`), Full-size smoke and effects (`FX_FULL`) | next start (static caches in the toolkit) |
| Controls | Input (`INPUT_MODE`), Mouse sensitivity (`MOUSE_SENS`) | next start (static caches in toolkit patch 6500) |
| Controls > Keys | the 20 key bindings of the current mode's page: press the key (Esc or Circle cancels; F11 stays fullscreen); a key in use swaps; Keyboard and Keyboard-and-mouse have a page each (`KB_KEY_*`, `KBM_KEY_*`, and `PAD_KEY_*` for the game, as the launcher writes them) | next start |
| Controls > Controller buttons | which Xbox button presses each PS3 button (`PAD_BTN_*`; a button in use swaps) | next start |

Each page ends in "Reset this page to the defaults" (the launcher's defaults). Where a key is unset the page shows what the game does then (for example Edge smoothing Off,
where the launcher would write FXAA).

Launcher only (the page "Launcher only" says why): Unlock everything (the save is backed up before the game starts and the game loads its save first; the key is never taken from the
file), CPU boost and "boost off at exit" (the launcher changes the power plan around the game), the game disc folder, EBOOT and rebuild options (chosen at install), and the Online tab's preset list and Find on LAN.

Code: `installer/port/jsp/jsp_settings.h` (key table, file model, key and button rules; header only), `jsp_settings.cpp` (file path, atomic save, live values),
the pages in `jsp_port_menu.cpp`. Host test: `bash installer/tools/settings_test.sh` (file round trip, the rules the launcher applies, the pages against a fake file, and a check
of the key table against `launcher/JStarsLauncher*.cs`, the readers and the defaults).

## The Online page (editable)

Rows: Method, Server, Online name, Community preset, Save, Discard changes. `JSP_PORT_MENU_EDIT=0` keeps the earlier read-only page.

- Up/Down select a row. Left/Right cycle Method (off, re-psn, community) and Community preset (the presets the launcher saved).
- Cross on Server or Online name opens the character picker: Up/Down change the letter at the position (L1/R1 by ten), Left/Right move the
  position (the empty slot after the last letter appends on Up/Down), Square deletes, Cross accepts, Circle cancels. The picker offers the
  characters the reader accepts; an invalid server or name is stated on the page and not written.
- Save writes `repsn.ini` and `community.ini` in the same shared folder the reader and the launcher use (`PS3RECOMP_SHARED_DIR`, the
  portable install's `shared` folder, else `%APPDATA%\PS3Recomp`), same keys and format as the launcher's Online tab: only the owned keys change,
  comments and other keys stay, CRLF line ends, a temp file beside the target renamed over it, the side being switched off written first so both
  never say enabled. It takes effect on the next start (the reader runs once at start). Circle on the page goes back to the first page and drops unsaved changes (the log line says so); Discard re-reads the files.
- The validators are the reader's own, so a file written here is never one the reader would reject.

Overlay text is drawn in two passes per string (every rim, then every body; toolkit patch 2517) from a 6-plane glyph atlas
(`menu_font_art.py`, tracking 1.0 px), so a letter's black rim no longer covers the previous letter's body. A file with the older 2-plane atlas still draws in one pass.

## State of the tests

- Host tests: `bash installer/tools/settings_test.sh` (above), `bash installer/tools/port_menu_test.sh` (page state: pad edges, release, swallow flag, the editable page and picker against a fake backend), `bash installer/tools/online_settings_test.sh` (reader and the writer: round trip, other lines kept, refusals), `bash installer/tools/overlay_font_test.sh` (no body pixel of any of 258 sample letter pairs is covered), `installer/tools/port_overlay_test.c`
  (canvas, rectangles, text, dirty rectangles; run against the exported and patched toolkit), a series export of all patches in order
  (`installer/tools/export_toolkit.py`), and syntax-only compiles of the backend, `jsp_port_menu.cpp`, `jsp_online.cpp` and `stubs.cpp`.
- In game (this tree, built with the player's setup steps, US disc, Proton on Linux; not yet on Windows): with `JSP_PORT_MENU=1` a
  scripted pad run opened Options > Port Settings > Online, set Method, Server and Online name with the picker, saved and closed;
  `repsn.ini` then held the new values with its other lines, comment and CRLF kept, `community.ini` stayed off and no temporary
  file was left. Without `JSP_PORT_MENU` the Options list showed its five rows and no file was written. The first run of a fresh
  build (no shader cache yet) reached the menu too late for the scripted route; the second run passed.

## Open

- The settings pages are not built or run yet (host test and syntax checks only): open each page, change one live and one next-start setting, rebind a key, close, and start again to see the saved values arrive.
- Live input rebinding would need a generation counter in toolkit patch 6500 (its key, button and mode tables are read once).
- Build the v19 tree with the feature and run the page (open it from the Options list, both sub-pages, edit and save, close it, check the menu keeps working, and that the written `repsn.ini` is read back at the next start).
- The port's "Exit Game" option shares the Options list and tests the cursor from the bottom; check both options with the sixth row present.
- The overlay is drawn before 2508's and 2509's pictures, so those land on top (a page open with the FPS counter or the time-limit stamp).
- The sixth row's parts after the game's own per-update loop with the instruction patch (the earlier tree needed the patch to keep them shown).
- Pressure-capable pads: the game's menu reading pressure words behind the page.
