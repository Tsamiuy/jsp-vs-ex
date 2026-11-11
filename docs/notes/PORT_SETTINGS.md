# Port Settings page (opt-in)

ENHANCEMENT, off by default. `JSP_PORT_MENU=1` gives the Options list a sixth row, "Port Settings". Cross on it opens a page drawn by
the port over the game (Up/Down select, Cross opens, Circle goes back); the first sub-page, "Online", shows the online settings read at
start (method, server, online name, preset, files read), read only. US 1.00 build only: the hooks sit at fixed addresses of that code,
and the glue refuses to start on another build (it reads the instruction it needs to find).

## What it is made of

| Part | Where |
|---|---|
| Menu label replacement: a table of (hash of a 512 x 64 label texture, art file), switched by the port | toolkit patch 2516 |
| Present-time overlay: a CPU-drawn RGBA layer blended onto the back buffer, closed (one flag test per frame) unless a page is open | toolkit patch 2517 |
| Label switch as a bit mask gated on the open panel; the "Port Settings" label | toolkit patch 2518 |
| Pad hooks the page reads and swallows input through | toolkit patch 6501 (already in the series) |
| The page: state machine, the two hooks on the game's functions, drawing | `installer/port/jsp/jsp_port_menu.cpp`, API in `jsp_overlay_api.h` |
| Online page text | `jsp_online_describe` in `installer/port/jsp/jsp_online.cpp` |
| The game's own row count (`li r25,5` at 0x001E71C0 becomes 6 behind a switch) | the lifter's `--insn-patches` (upstream ps3recomp v0.19.0 has it), file `installer/port/jsp/lift_insn_patches.json`, passed by `firstrun.py` for the US build |
| Label and glyph art, drawn from Fira Sans Heavy Italic (SIL OFL 1.1) when the installer is built; nothing from the game | `installer/tools/menu_label_art.py`, `menu_font_art.py`, copied next to the exe by `firstrun.py` |

The lift: with the instruction patch the lifted code reads the switch `g_port_menu_six_rows` (defined in `jsp_port_menu.cpp` whether or not the
page is on), so a US setup lifts again once (the lift fingerprint includes the patch file) and the build follows. With the switch off the game
runs the instruction as before. The EU build lifts without the patch (its code differs at that address and the lifter would stop).

## State of the tests

- Host tests only: `bash installer/tools/port_menu_test.sh` (page state: pad edges, release, swallow flag), `installer/tools/port_overlay_test.c`
  (canvas, rectangles, text, dirty rectangles; run against the exported and patched toolkit), a series export of all patches in order
  (`installer/tools/export_toolkit.py`), and syntax-only compiles of the backend, `jsp_port_menu.cpp`, `jsp_online.cpp` and `stubs.cpp`.
- The page has been seen in game on the maintainers' earlier tree (US disc, Proton). On this tree it has not been built or run.

## Open

- Build the v19 tree with the feature and run the page (open it from the Options list, both sub-pages, close it, check the menu keeps working).
- The port's "Exit Game" option shares the Options list and tests the cursor from the bottom; check both options with the sixth row present.
- The overlay is drawn before 2508's and 2509's pictures, so those land on top (a page open with the FPS counter or the time-limit stamp).
- The sixth row's parts after the game's own per-update loop with the instruction patch (the earlier tree needed the patch to keep them shown).
- Pressure-capable pads: the game's menu reading pressure words behind the page.
