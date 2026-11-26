# Port Settings page

ENHANCEMENT, on by default (until 0.5.1 it was off unless `JSP_PORT_MENU=1`). The Options list gets a sixth row, "Port Settings", drawn at the top of the list above Control Settings (the game draws the list bottom-up: row 0 Cross-Save at the bottom, the new row 5 on top, as a run under Proton showed); `JSP_PORT_MENU=0` turns it off. Cross on it opens a page drawn by
the port over the game (Up/Down select, Cross opens, Circle goes back). Its pages: Display, Graphics and Controls (the launcher's settings, below), Online (below), and a list of what stays launcher-only. To remove the row,
put the line `JSP_PORT_MENU=0` in `play_settings.txt` (every line of that file becomes an environment variable for the game; the settings window keeps lines it does not own). A US install set up with 0.5.1 or earlier already has the lifted row-count switch (see "The lift" below), so after an update the page needs no new lift; the update's changed port sources rebuild the exe as any update does. US 1.00 build only: the hooks sit at fixed addresses of that code,
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
| Display | Render resolution (`RES_SCALE`), Display mode (`FULLSCREEN`), Window size (`WINDOW_SIZE`), Model viewer (`MODEL_PREVIEW`, off by default), FPS in the window title (`TITLE_STATS`) | next start (read once by the toolkit; F11 still switches fullscreen in the game) |
| Graphics | Fight HUD size (`UI_SCALE`), Edge smoothing (`AA_MODE`), FPS counter (`FPS_COUNTER`), Shadow distance (`SHADOW_RANGE`), Model detail (`MODEL_DETAIL`), Object draw distance (`OBJECT_DISTANCE`) | now (the readers in `jsp_game.cpp` re-read when a page changes them; model detail and object distance are rewritten into the map objects already seen) |
| Graphics | Shadows (`SHADOW_SCALE`), Full-size effects (`FX_FULL`) | next start (static caches in the toolkit) |
| Controls | Input (`INPUT_MODE`), Mouse sensitivity (`MOUSE_SENS`) | next start (static caches in toolkit patch 6500) |
| Controls > Keys | the 20 key bindings of the current mode's page: press the key (Esc or Circle cancels; F11 stays fullscreen); a key in use swaps; Keyboard and Keyboard-and-mouse have a page each (`KB_KEY_*`, `KBM_KEY_*`, and `PAD_KEY_*` for the game, as the launcher writes them) | next start |
| Controls > Controller buttons | which Xbox button presses each PS3 button (`PAD_BTN_*`; a button in use swaps) | next start |

Each page ends in "Reset this page to the defaults" (the launcher's defaults). Where a key is unset the page shows what the game does then (for example Edge smoothing Off,
where the launcher would write FXAA).

Launcher only (the page "Launcher only" says why): Unlock everything (the save is backed up before the game starts and the game loads its save first; the key is never taken from the
file), CPU boost and "boost off at exit" (the launcher changes the power plan around the game), Two-thread drawing (`PIPE`, on by default since toolkit patch 7907; the drawing thread starts with the game; `PIPE=0` turns it off and counts only next to `PIPE_SEEN=2`, which the settings window writes, so an old file with `PIPE=0` from the time it was off by default does not switch it off), the game disc folder, EBOOT and rebuild options (chosen at install), and the Online tab's preset list and Find on LAN.

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

- Host tests: `bash installer/tools/settings_test.sh` (above), `bash installer/tools/port_menu_test.sh` (page state: pad edges, release, swallow flag, the editable page and picker against a fake backend), `bash installer/tools/net_online_test.sh` (the Online page's Netcode choice: rows, save order, validation, the rollback start decision), `bash installer/tools/online_settings_test.sh` (reader and the writer: round trip, other lines kept, refusals), `bash installer/tools/overlay_font_test.sh` (no body pixel of any of 258 sample letter pairs is covered), `installer/tools/port_overlay_test.c`
  (canvas, rectangles, text, dirty rectangles; run against the exported and patched toolkit), a series export of all patches in order
  (`installer/tools/export_toolkit.py`), and syntax-only compiles of the backend, `jsp_port_menu.cpp`, `jsp_online.cpp` and `stubs.cpp`.
- In game (this tree, built with the player's setup steps, US disc, Proton on Linux; not yet on Windows; before the page was on by default): with `JSP_PORT_MENU=1` a
  scripted pad run opened Options > Port Settings > Online, set Method, Server and Online name with the picker, saved and closed;
  `repsn.ini` then held the new values with its other lines, comment and CRLF kept, `community.ini` stayed off and no temporary
  file was left. With the page off the Options list showed its five rows and no file was written. The first run of a fresh
  build (no shader cache yet) reached the menu too late for the scripted route; the second run passed.

## Player order (ENHANCEMENT, toolkit patch 7908)

The Controls page has a row "Player order" (Cross: start). It closes the page and starts the join: the screen shows "Press a button: player N",
and each pad (or the keyboard) takes the next free player, player 1 to 4, with its first button press. The press that joins is not seen by the
game. While join runs the game keeps the layout it had before (the old slot order, or the previous player order), so a fight goes on and
only the pads that are joining are held back; the new order takes over in one step when join ends. Join ends when every connected pad has
pressed, when four players are taken, or when player 1 presses Start (that Start is swallowed too). A pad that is still holding a button when
join starts, or is plugged in holding one, stays neutral until it was released. The same join can be started without the page, also with the page off
(`JSP_PORT_MENU=0`), by holding **Select + L3 + R3** on a pad for 2 seconds (the chord needs both sticks pressed in, which the game's own
controls do not use as far as the notes say; the game's use of Select was not found in the notes, so a collision there is not excluded).
`PAD_JOIN=0` (a line in `play_settings.txt`, like the other switches) turns the chord and the row off. Until one of the two is used nothing
changes: slot order, `INPUT_MODE`, `PAD_PAD_P2` as before. After a join a pad that is unplugged frees its player and a pad that is plugged in
later takes the lowest free player; player 1 is never left empty while another player exists (the lowest one moves up), because the menus read
port 0. The keyboard is a player only where `INPUT_MODE` gives it one: Keyboard or Keyboard and Mouse, Controller with `PAD_PAD_P2` on (the
default), or no pad on slot 0; once it has a player it keeps it, and it holds the end of join open only in the two keyboard modes or when no
pad is connected. The mouse hooks (cursor clip and hide, wheel) run once per poll wherever the keyboard sits, and only a keyboard on port 0 gets the mouse look and menu clicks. Rumble goes to the pad the game sees on the port and stops when that pad
leaves the port. This one is on by default, also without a reorder: in Keyboard or Keyboard and Mouse mode with `PAD_PAD_P2` the pad is player 2 and now rumbles for port 1 (before, port 0's rumble went to XInput slot 0, the same pad, and port 1's to an empty slot). Select, L3 and R3 are hidden from the game from the first poll of the chord until the pad has released them; a chord completed while a join is already running is not a chord and reaches the game as ordinary buttons. The order logic is `libs/input/pad_join.h` in the toolkit, host tested by `installer/tools/pad_join_test.c`; the text is drawn
by `jsp_port_menu.cpp` from the toolkit's `g_pad_join_notify`. Known limits: the game takes two human players in Free Battle, so players 3 and 4 are read but not used
yet; not tried with real pads.


## PlayStation pads (ENHANCEMENT, toolkit patch 7909)

DualSense, DualSense Edge, DualShock 4 v1 (054C:05C4) and v2 (054C:09CC), USB or Bluetooth, up to four at once, read as raw HID devices
(Windows has no XInput driver for them). Each pad is a device of its own in the player-order table (devices 5 to 8; XInput slots are 0 to 3,
the keyboard 4), so the reorder above treats them like any other pad. Without a reorder the old order stays: XInput pads keep their slot as
their port, then the PlayStation pads take the lowest ports no XInput pad holds, in the order Windows listed them at plug-in. So one Xbox pad,
or one DualSense alone, behave as before (a lone DualSense is port 0); a DualSense next to an Xbox pad on slot 0 is port 1 (it used to be
ignored); a DualShock 4 works at all. Buttons: Cross is A, Circle B, Square X, Triangle Y, Share or Create is Select, Options is Start; the
`PAD_BTN_*` rebinds, the deadzone and the trigger threshold apply as for an XInput pad. The pads are looked for once a second by a background
thread and each has its own reader, so plugging in or out never stalls the game; a pad that leaves frees its player (see above).
`PAD_NO_PLAYSTATION=1` turns them off (`PAD_NO_DUALSENSE=1`, the first name, is the same switch and now also covers the DualShock 4).
No rumble, lightbar, touchpad or motion: rumble over raw HID needs the pad's output report (with a checksum over Bluetooth), not safe to get
right without a pad. Report layouts are in the header of `libs/input/pad_hid.h`, host tested by `installer/tools/pad_join_test.c` on reports
built from the documented layout. Not tried with real pads: the Bluetooth DualShock 4 read size and its switch feature report are the
least certain parts.

## Open

- The settings pages are not built or run yet (host test and syntax checks only): open each page, change one live and one next-start setting, rebind a key, close, and start again to see the saved values arrive.
- Live input rebinding would need a generation counter in toolkit patch 6500 (its key, button and mode tables are read once).
- Build the v19 tree with the feature and run the page (open it from the Options list, both sub-pages, edit and save, close it, check the menu keeps working, and that the written `repsn.ini` is read back at the next start).
- The port's "Exit Game" option shares the Options list (it takes over Cross-Save, the bottom row) and opens its box only when the Options cursor word 0x008E63FC reads 0 and the menu state word 0x0095654C reads 5. ASSUMPTION: both still read that way with the sixth row present (0x008E63FC 0 only on Cross-Save, never on Port Settings; 0x0095654C 5 on the six-row list and while the Port Settings page is open over it). Neither has been read in a run with both options on; check both options with the sixth row present.
- The overlay is drawn before 2508's and 2509's pictures, so those land on top (a page open with the FPS counter or the time-limit stamp).
- The overlay is one layer shared with the Training readout (`jsp_game.cpp`); the readout yields while this file holds it (`jsp_port_menu_overlay_held()`, as it does for the rollback glue's `jsp_net_overlay_held()`), see TRAINING_MODE.md, "Training readout". Not run.
- The sixth row's parts after the game's own per-update loop with the instruction patch (the earlier tree needed the patch to keep them shown).
- Pressure-capable pads: the game's menu reading pressure words behind the page.
- PlayStation pads (7909): try a DualShock 4 (USB and Bluetooth) and a DualSense (USB and Bluetooth) with real hardware, one alone, two together, and beside an Xbox pad; check Cross, Circle, sticks, triggers and the hot-plug.
- Player order (7908): try it with two real pads and the keyboard, both start paths, the on-screen text, unplug and re-plug, and the Select + L3 + R3 hold in a fight and in the menus.
- Rumble on the default path (7908): with Keyboard mode and a pad as player 2, check the pad rumbles in a fight and the keyboard player gets none.
- Player order (7908), known limit: after a reorder the keyboard can be sampled twice in one poll (once to join, once for the port it lands on, when the two kinds of port differ); not checked for side effects. Also check in Keyboard and Mouse mode that the cursor is freed when the keyboard is player 2 or out of the order.
