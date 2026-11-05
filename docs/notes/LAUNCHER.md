# Launcher window (2026-10-27)

The maintainer asked for a real window instead of the terminal launcher: mouse-driven options, Start Game and
Exit buttons, a resolution drop-down with every choosable resolution, a key-binding page, and room for
DLSS / Frame Generation later.

## Files (all new; nothing existing was changed)

| File | What |
|---|---|
| `PS3Recomp\J-Stars Launcher.exe` | The launcher (33 KB). Double-click it. No console window. |
| `PS3Recomp\launcher\JStarsLauncher.cs` | Its source, one file, C# 5 (Windows' own .NET Framework 4 compiler). |
| `PS3Recomp\launcher\build_launcher.bat` | Rebuilds the exe with `%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe`. No installs. |
| `jstars\play_settings.txt.pre-launcher` | Copy of the settings file before the launcher first saves it. |
| `jstars-notes\patches\cellpad_keymap.patch` | cellPad change so the game reads the key bindings (for the AI assistant). |

`Play J-Stars.bat`, `J-Stars Settings.bat`, `jstars\play.ps1` and `jstars\settings.ps1` are unchanged and
stay as the fallback. Caveat: the old `J-Stars Settings.bat` rewrites `play_settings.txt` with only
RES_SCALE and FULLSCREEN, so using it drops the launcher's key bindings and window size.
The launcher folder is outside the git repo (`ps3recomp-src`), so it has no commit; the .cs file is the record.

## What it does (same steps as play.ps1)

Start Game: save settings, run `tools\boost\boost_on.bat` (hidden), mount the ISO read-only
(`Mount-DiskImage` in a hidden PowerShell; reuses an already-mounted disc and then does not unmount it),
start `jstars\port\build\JStarsRecomp.exe "jstars\EBOOT.elf"` in `jstars\run` with the same environment
as play.ps1 (PS3_VFS_ROOT, PS3_HDD0_ROOT, PS3_DEV_FLASH, PS3_TITLE, PS3_VERBOSE=0, plus every KEY=VALUE
line of play_settings.txt), stdout/stderr to `run\play_stdout.txt` / `run\play_stderr.txt`. When the game
exits: `boost_off.bat` (if "Turn CPU boost off again" is ticked, default on), unmount, window comes back.
Exit or closing the launcher while the game runs: asks, then posts WM_CLOSE to the game's windows (same
as clicking its X: the D3D12 window sets window_closed). Only if the game is still alive after 20 s it
offers a forced stop. Log of each start: `jstars\run\launcher_log.txt`.

Differences from play.ps1: the game runs without a console (CreateNoWindow; output still goes to the
same files). Not yet verified that the game behaves the same without a console: first thing to check.

## Settings file format (jstars\play_settings.txt)

```
RES_SCALE=1..4          render scale of 1280x720: 720p, 1440p, 4K, 5K (rsx_d3d12_backend.c clamps to 1..4)
FULLSCREEN=0|1          borderless fullscreen at start (F11 toggles in game)
WINDOW_SIZE=WxH         optional window client size (create_window in rsx_d3d12_backend.c; shrunk to the work area)
BOOST_OFF_ON_EXIT=0|1   launcher only
PAD_KEY_<NAME>=0xNN     Windows virtual-key code per pad input (needs the cellPad patch)
```
Lines the launcher does not know are kept. Resolution list: RES_SCALE is an integer scale, so 720p/1440p/
4K/5K are all the render sizes the game can produce today (5K labelled "not tested"). The window-size list
is every 16:9 mode the screens report (EnumDisplaySettings) plus 1280/1600/1920/2560/3200/3840-wide 16:9
sizes that fit the biggest screen. On the maintainer's PC (measured 2026-10-27): 1280x720, 1600x900, 1920x1080,
2560x1440, 3200x1800, 3840x2160 (screens DISPLAY6 up to 2560x1600 and DISPLAY7 up to 3840x2160).

## Key bindings

Controls page: click a box, press a key (arrows, Enter, Tab, Shift, Ctrl, Alt included; Esc cancels;
F11 refused, Esc is the game's quit key). A key already used by another input swaps with it.
"Reset to defaults" restores the cellPad defaults. The page checks whether the game exe contains the
string `PAD_KEY_` and, if not, says the bindings are saved but not read by this game build yet.

Patch for the AI assistant (`jstars-notes\patches\cellpad_keymap.patch`, made against the working tree of
`libs/input/cellPad.c` that already has the AI assistant's uncommitted g_pad_arrows_dpad change; `git apply --check`
passes): the two key tables become non-const with a name per entry, and on the first poll each entry's
key is replaced by `PAD_KEY_<NAME>` if set (strtol base 0, 1..255). Names: LSTICK_UP/DOWN/LEFT/RIGHT,
DPAD_UP/DOWN/LEFT/RIGHT, CROSS, CIRCLE, SQUARE, TRIANGLE, L1, R1, L2, R2, L3, R3, START, SELECT.
Nothing changes when no PAD_KEY_ variable is set. Not compiled yet (no compiler run by this thread).

## Test plan for the AI assistant (not run yet)

1. Build with the patch. Start the game from the launcher at RES_SCALE=1 in a window. Expect: no console,
   game window in front, `launcher_log.txt` shows "Mount output: DRIVE=<x> | MOUNTED_HERE=True".
2. In the menu, press Z (Cross) and arrows: same as before.
3. Close the game with its X: launcher log shows exit code, boost off, disc unmounted; launcher comes back.
4. Start again, press Exit in the launcher: game closes within 20 s with no force prompt.
5. Rebind Cross to Space in the launcher, start, check Space confirms in a menu and Z no longer does.
6. Try 4K + Fullscreen, and a 1920x1080 window size.

## DLSS / Frame Generation (rough note; shown disabled, "coming later")

- DLSS Super Resolution needs the NVIDIA Streamline (or NGX) SDK in the D3D12 backend, a lower internal
  render size plus jittered projection, per-pixel motion vectors and depth for the scene, and a hook
  point before the HUD is drawn. The game has no motion vectors, so they would have to be derived
  (camera reprojection from depth is possible; moving characters would ghost without real per-object
  vectors). Weeks of renderer work, inside rsx_d3d12_backend.c (the AI assistant's file).
- Frame Generation (DLSS 3/4 FG) needs everything above plus Reflex and a present path that lets
  Streamline insert frames; the game's own 30/60 fps pacing (flip/vblank gate) would need rework.
- A cheaper option for "sharper" first: FSR 1 / NIS spatial upscale of the final image (no motion
  vectors), or just RES_SCALE, which already works.

## The AI assistant's first test (2026-10-27 23:35Z, game commit 7e3e780 with the cellPad patch)

- PASS: no console window, settings written with PAD_KEY_* lines, disc mounted, log fine.
- PASS after a game fix by the AI assistant: closing the game window used to leave the process running, so Exit
  always reached the 20 s force prompt. Now the game exits on window close (WINDOW_CLOSE_EXIT=0 = old
  behaviour). Exit -> Yes -> "Game exited with code 0" after 1 s, boost off, launcher back.
- Inconclusive: two starts right after the previous run unmounted failed with "Could not mount the disc
  image". One run came up at 3840x2160.
- Not tested: key rebinding (needs real key presses; the maintainer to try).
- The game exe now contains `PAD_KEY_`, so the Controls page no longer shows the "not read yet" notice.

Launcher fixes after it (2026-10-28, `launcher\JStarsLauncher.cs.pre-mountretry` = before):
- Mount retried once after 2 s; log lines "Mount attempt 1/2: ...".
- PowerShell now runs as a plain `-NoProfile -NonInteractive -Command "..."` (no `-EncodedCommand`, no
  `-ExecutionPolicy Bypass`) with `$ProgressPreference='SilentlyContinue'`, so the CLIXML progress blob is
  gone from the log. Checked read-only with the same call form: stdout `ATTACHED=False`, stderr empty.
- Norton flagged something as IDP.HELU.PSE95 on the maintainer's PC. Candidates: hidden encoded PowerShell (removed
  from the launcher), the Add-Type helpers in my screenshot script (they compile a temp DLL; not used
  again), or the new unsigned launcher exe itself. Quarantine path still to be read from Norton.

## Launcher hides while the game runs (2026-10-28, the maintainer's request; before = `JStarsLauncher.cs.pre-hide`)

After Start Game the launcher polls (every 250 ms, up to 90 s) for a visible window of the game process,
then hides its own window (log: "Game window is up, launcher hidden."). The launcher process stays alive,
invisible, only to wait for the game to exit; it then runs boost_off, unmounts the disc and closes itself,
so no launcher or game process is left. If the game never opens a window (crash at start), the launcher
is not hidden and comes back with the status/exit code instead. Not yet re-tested with the game.

## Paused 2026-10-28 02:13Z (the maintainer: continue tomorrow). Open items, in order

1. The AI assistant re-test of the hide change: Start Game -> launcher disappears when the game window shows ->
   quit with the in-game Exit Game option -> no JStarsRecomp.exe and no "J-Stars Launcher.exe" left in
   Task Manager, disc unmounted, `jstars\run\launcher_log.txt` ends with "CPU boost off." and
   "Disc unmounted.". Then a back-to-back second start to exercise the mount retry.
2. The maintainer tries a key rebind (Controls page: Cross -> Space, check Space confirms in a menu).
3. Norton IDP.HELU.PSE95: get the quarantine path from the maintainer, then decide what triggered it.
4. Later: the old `J-Stars Settings.bat` drops PAD_KEY_/WINDOW_SIZE lines; retire it or make it keep them
   once the launcher is accepted (ask the maintainer first). DLSS/FG stay "coming later".
State: launcher source `launcher\JStarsLauncher.cs` (current, built into `J-Stars Launcher.exe`);
older versions `.pre-mountretry`, `.pre-hide`. No launcher code changes pending.

## Request 2026-10-28 11:46Z: input types, mouse, controller remap (plan sent, waiting for the maintainer's OK)

The maintainer asked (in the AI assistant's thread) for a Keyboard / Keyboard & Mouse / Controller choice on the Controls page,
each with its own bindings; KB&M = mouse camera + bindable mouse buttons; Controller = remap pad buttons;
mouse clicking in menus (cursor hidden until the main menu, click Free Battle to enter it).
Plan sent (no code changed yet):
1. Easy: launcher drop-down with per-type binding sets (INPUT_MODE=kb|kbm|pad; the active set is written as
   PAD_KEY_*). Mouse buttons are VK 0x01/0x02/0x04/0x05/0x06, which GetAsyncKeyState already reads, so the
   existing cellPad key patch handles them; cellPad still needs INPUT_MODE to ignore the mouse in kb mode.
   Controller remap: small cellPad patch on the XInput table (PAD_XBTN_<NAME>=...).
2. Medium: mouse movement -> right stick (cursor hidden/clipped in fights). First check with a controller
   whether J-Stars uses the right stick for the camera at all (camera may be automatic).
3. Big: menu clicks. Menus have no pointer; needs per-menu item screen rects and the guest-memory address of
   the selected index, then click = move selection + Cross. Main menu first. Days of the AI assistant work.
The AI assistant's proposed setting names (2026-10-28 11:47Z, to be confirmed when the cellPad side lands):
- `INPUT_MODE=keyboard|kbm|controller`
- Keyboard and KB&M: `PAD_KEY_<NAME>=0xNN`; mouse buttons as VK 0x01/0x02/0x04/0x05/0x06 (the KB&M page
  must capture clicks as bindings). KB&M: mouse moves the right stick, so no RSTICK binding; optional
  `MOUSE_SENS=<float>`.
- Controller: `PAD_BTN_<NAME>=<A|B|X|Y|LB|RB|LT|RT|LS|RS|START|BACK|DUP|DDOWN|DLEFT|DRIGHT>`, NAME in CROSS,
  CIRCLE, SQUARE, TRIANGLE, L1, R1, L2, R2, L3, R3, START, SELECT, DPAD_UP/DOWN/LEFT/RIGHT.
  Defaults A=CROSS, B=CIRCLE, X=SQUARE, Y=TRIANGLE.
Launcher work still waits for the maintainer's OK.

## Part 1 done in the launcher (2026-10-28, the maintainer said "1 first"; before = `JStarsLauncher.cs.pre-inputmodes`)

Controls page: "Play with" drop-down (Keyboard / Keyboard & Mouse / Controller), each page with its own
bindings (shots: `launcher_controls_keyboard.png`, `launcher_controls_kbm.png`, `launcher_controls_controller.png`).
- Keyboard: as before; mouse clicks are not accepted as bindings.
- Keyboard & Mouse: a box waiting for input also takes a mouse click (left/right/middle/side buttons, via an
  IMessageFilter on WM_*BUTTONDOWN -> VK 0x01/0x02/0x04/0x05/0x06). "Mouse speed" slider 0.25x-5x (default 1x).
- Controller: one drop-down per PS3 button with the 16 XInput names; picking a used button swaps.
- Reset to defaults resets only the page shown.
Settings written (names match the AI assistant's cellPad code in the working tree, checked 2026-10-28):
`INPUT_MODE=keyboard|kbm|controller`; `PAD_KEY_*` = the Keyboard page, or the KB&M page when INPUT_MODE=kbm
(controller mode writes the Keyboard page, which fills in when no controller is connected);
`MOUSE_SENS=1.00` (always with a '.', InvariantCulture); `PAD_BTN_<NAME>=<A..DRIGHT>`; launcher-only copies
of both key pages as `KB_KEY_*` / `KBM_KEY_*`. Old files with only PAD_KEY_* load into both key pages.
The notice under the page checks the game exe for `PAD_KEY_`, `INPUT_MODE`, `PAD_BTN_`; the build of
2026-10-28 12:49 has all three.
New: `"J-Stars Launcher.exe" --screenshots <folder>` saves a PNG of every page and closes without saving
settings (replaces the Add-Type screenshot script, which compiled a temp DLL).
Settings backup before this change: `jstars\play_settings.txt.pre-inputmodes`.
Not tested with the game yet: needs the AI assistant (mouse camera, a mouse-button binding, a controller remap).

## Graphics tab removed (2026-10-29, the maintainer: no graphics changes planned, 4K looks fine)

Before = `launcher\JStarsLauncher.cs.pre-nographics`. The tab only showed DLSS / Frame Generation greyed out
and wrote no settings, so nothing the game reads changes (RES_SCALE etc. stay on the Display tab; the maintainer's
file has RES_SCALE=3 = 4K). Removed BuildGraphicsTab and its --screenshots page. Rebuilt; `--screenshots`
run shows Display and Controls only, settings file hash unchanged. `launcher_graphics.png` in this folder
is now outdated. The DLSS/FG note above stays for reference. The launcher folder is not in a git repo,
so there is no commit; the .pre-* copies are the history.

## Fight HUD size slider (2026-10-29, the maintainer via the AI assistant; before = `launcher\JStarsLauncher.cs.pre-hudsize`)

Display page, under the boost option: "Fight HUD size" slider 50%-100% in 5 % steps, default 100 %.
Writes `UI_SCALE=0.50..1.00` (two decimals, InvariantCulture, so always a '.' whatever the Windows locale;
100 % = `UI_SCALE=1.00`). The AI assistant's game side clamps to 0.5..1.0 and shrinks the fight HUD only. Loading
rounds any stored value to the nearest 5 % step. Other keys are kept as before. The line appears in
play_settings.txt on the next save (any change on a page, or Start Game); until then the game default is 1.0.
Rebuilt; `--screenshots` check shows the slider at 100 % and left the settings file unchanged
(`launcher_display.png` updated). Not run with the game.

2026-11-02: Display page has Edge smoothing (AA_MODE in play_settings.txt: off, fxaa, fxaa2; default fxaa). Window 80 units taller to fit it.
2026-11-02: Edge smoothing also offers SMAA (AA_MODE=smaa).
2026-11-02: Display page has Full-size smoke and effects (FX_FULL in play_settings.txt, default on).
