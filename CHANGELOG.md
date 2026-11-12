# Changelog

All notable changes are listed here, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions are `X.Y.Z` (the number in `installer/VERSION`).
0.1.0 and 0.2.0 are pre-releases; the first full release will be 1.0.0. Changes not in a release yet are under Unreleased.

## [Unreleased]

- Installer: `assemble_payload.py --tree legacy` (and `v15`) no longer builds the earlier v0.15 tree and exits with a pointer to the git tag `legacy-v15` (issue #152); `firstrun.py` builds only the current tree and stops with an error when `tree.txt` is missing or unknown; CI no longer builds or tests the legacy tree (the `toolkit` job and the legacy patch series check are gone, the `tree` input of `installer.yml` too).
- Repository layout: `Play J-Stars.bat` and `Test Fight.bat` (legacy v0.15 tree only) moved from the root into `jstars/`, next to `play.ps1` which they start; the installer never shipped them.
- Repository layout: `jstars-notes/` is now `docs/archive/jstars-notes/`, `toolkit/` and `tools/toolkit/apply_toolkit.py` are now in `jstars/toolkit/`, and `docs/README.md` lists the documentation by audience. `docs/LAYOUT.md` marks every root folder current or legacy. No files deleted; the installer, CI and the checks use the new paths.
- Releases: `.recomp.json` now names the current version (`"x-version"`), which `tools/release.py bump` rewrites and `--check` (CI) compares with `installer/VERSION`; from 1.0.0 on the bump also sets its status to `released`. Its notes no longer say that nothing has been released.
- Installer: unchecked task "Unlock everything (cheat)" with a choice between a separate TEST save (recommended) and the normal save; it only writes `unlock=` and `savemode=` into `jsp-install.ini`. The launcher's Cheats tab gets "Save in use" (Normal / Test, shown in the window title): the test save is `<hdd0>_gamedata_test` (`PS3_GAMEDATA_ROOT`, first start copies the normal save in, installed game data shared), a normal save is asked about before it is unlocked, and the dated backup covers the save in use. Not built or run on Windows yet; source checks in `unlock_option_test.py`.
- CI: both patch series are applied to scratch copies of the pinned ps3recomp, the current port's sources are compiled (syntax only, no game data) with the installer's llvm-mingw, and every host test runs from one list (`tools/ci/host-tests.txt`), also on Windows; `portable_selftest.py` is repaired for the current `firstrun.py`.
- Port Settings (opt-in, `JSP_PORT_MENU=1`): Display, Graphics and Controls pages edit the launcher's settings in the game. They write the same `play_settings.txt` with the same keys; fight HUD size, edge smoothing, FPS counter, shadow distance, model detail and object distance apply at once, the rest on the next start; the launchers and `jsp-launcher.exe` pass the file they loaded in `JSP_SETTINGS_FILE`. A "Launcher only" page lists what stays in the launcher and why. Host test `installer/tools/settings_test.sh`; not built or run yet.
- Settings window (`jsp-settings.exe`): Start Game, its "already running" check and the Controls page's key-support notice looked for `game\JStarsRecomp.exe`, but a current-tree install places `game\jsp.exe`, so Start Game answered "The game is not built yet" and the Controls page said the game does not read key changes. The window now takes the exe name from `paths.txt` (`EXE=`), and falls back to `jsp.exe` when that file is there.
- Installer: unchecked box "Add to Steam" on the last page adds the game as a non-Steam shortcut for each Steam account of the user (refuses while Steam runs, backs up `shortcuts.vdf`, changes nothing twice); `installer/tools/steam_shortcut.py` and its test (ENHANCEMENT).
- Sound no longer stays off for the rest of the run when Windows takes the audio device away (for example a default-device change, a headset or another program, or after alt-tab): the port opens the current default device again within about a second, and it also follows a switch of the default output device while playing (toolkit patch 3004). The log shows `audio device lost` and `audio device re-opened` lines.

## [0.2.0] - 2026-11-12

### Added

- Port Settings page (opt-in, `JSP_PORT_MENU=1`, US 1.00 build): toolkit patches 2516-2518 (menu label replacement, present-time overlay layer, label mask by open panel), `jsp_port_menu.cpp`, the lifter's `--insn-patches` for the Options row count (`installer/port/jsp/lift_insn_patches.json`), and the label and font art drawn at installer build time (`menu_label_art.py`, `menu_font_art.py`). The Online page edits Method, Server, Online name and Community preset with a character picker and saves them to the shared settings folder (`repsn.ini`, `community.ini`, as the launcher does), applied on the next start; `JSP_PORT_MENU_EDIT=0` keeps it read only. Overlay text is blitted in two passes (rims, then bodies) so letters no longer cover each other. Checked in game under Proton with a player-side build of the US disc: the page opens, edits, saves and closes; without `JSP_PORT_MENU` the Options list keeps its five rows. See `docs/notes/PORT_SETTINGS.md`.
- Version line on the main menu (ENHANCEMENT, display only, default on; `JSP_VERSION_LINE=0` turns it off): "J-Stars Victory VS-EX X.Y.Z" in the lower right of the top list, drawn with the port overlay (`jsp_version_line.h`, `jsp_port_menu.cpp`). A release build only: `release.yml` passes the tag's version to `installer.yml`, `assemble_payload.py --release X.Y.Z` checks it against `installer/VERSION` and writes `release.txt`; a local or pull request build has none and shows no line. Nothing is contacted.

### Changed

- Installing a new version over an earlier one (setup or portable zip in the same folder) now compiles the port in full when the port or ps3recomp sources changed. Before, the build could keep the earlier version's compiled code, because the new files can carry older dates than the objects the last build left.
- Support bundle: `facts.txt` also names the CPU model, whether the CPU reports AVX2, and each display adapter's driver version (read from the Windows registry).
- Training: after a fight with Unlimited Stamina on, the port no longer keeps writing stamina into the fighters the game has already freed (for example after Re-select character(s)).
- Setup's EBOOT check now names the file it checked and shows the full SHA-256. When the file is not a build this port knows, it says whether the path looks like the game update's EBOOT (inside an RPCS3 dev_hdd0 game folder) and which file to pick instead.
- The support bundle's facts.txt checks for the exe of the installed tree (jsp.exe on the current tree), so it no longer reports the game exe as absent.

## [0.1.0] - 2026-11-11

First pre-release. J-Stars Victory VS+ for Windows: the game is built on your PC (ps3recomp, Direct3D 12) from your own copy of the disc, no game
files are included. On the Windows PC it was tested on, it boots, plays the menus and fights with sound at about 30 frames
per second, and the installer builds and starts it. It needs a 64-bit CPU with AVX2.
It is a pre-release with known problems and fewer tests than it needs:
see [issue #9](https://github.com/Tsamiuy/jsp-vs-ex/issues/9). `docs/FEATURES.md` lists each feature with how it was tested.

### What you get

- A Windows installer (`jsp-setup.exe`) and a portable zip that do that build, and a launcher to start the game.
- Higher resolutions, a resizable window and borderless fullscreen, and EX in the title logo.
- Keyboard, keyboard and mouse, or controller, each with its own bindings; a second player on keyboard or a second
  controller.
- Fight HUD size, Exit Game in Options, Training Mode, a Hide HUD row in Training and an FPS counter.
- Opt-in "Unlock everything" cheat (launcher Cheats tab, `JSP_UNLOCK_ALL=1`): save backup first, one start only.
- The features page (`docs/FEATURES.md`), generated from `docs/features.tsv`.

### Changes since the first builds

- Installer and CI: the port on ps3recomp v0.19.0 (`installer/port`) is now the default tree of
  `assemble_payload.py`, `installer.yml` (new `tree` input) and `release.yml`. The switch is `--tree current` (the default) or
  `--tree legacy` (the earlier v0.15 port); the names do not change with the toolkit version. The older `v18`, `v19` and `v15`
  are still accepted.
- Setup checks the decrypted EBOOT against the known 1.00 builds and refuses the other region's. An unknown build can
  be allowed with `--allow-unknown-eboot` (installer: `/ALLOWUNKNOWNEBOOT=1`); it is unsupported.
- Portable first run: the last line points to `jsp-settings.exe` (settings, Online tab, then Start Game) and names
  `jsp-launcher.exe` only as the direct start, or as the fallback when Windows has no C# compiler and the settings window was not built.
- Installer: the settings window (`jsp-settings.exe`, built from `launcher/JStarsLauncher.cs`) now has the Online tab
  (Off / re-psn / Community server, Find on LAN); before, only the v19 launcher had it. Start Game saves unsaved
  Online edits first. The game's reader also finds `jsp-install.ini` one folder above its `game\` folder, so a portable
  install uses `<install>\shared` as the tab does. Not yet run on Windows. `online_settings_test.sh` is now tracked, so CI can run it.
- Launcher: a Find on LAN button in the Online tab looks for a re-psn server started with `-discover` on the same
  network and fills the Server field (needs a re-psn build with LAN discovery; not tested across a real network yet).
- Launcher: an Online tab edits the shared online settings files (`repsn.ini`, `community.ini`): off, re-psn or a
  community server preset, one method at a time, written atomically; the launcher sets no online environment variable.
- Game: reads `repsn.ini` and `community.ini` at startup and sets the runtime's online variables from them (re-psn
  server and name, peer port; both methods on is refused, a community preset keeps the game offline, `--psnr`,
  `--username` and environment variables win), logging on `[online]` lines; host test in CI.
- D3D12: a vertex program seen for the first time is compiled on worker threads instead of the drawing thread, so
  new objects and camera turns no longer freeze the game for a moment (toolkit patch 2070, `VS_ASYNC=0` turns it off).
- D3D12: every pipeline a run builds is noted in `pipelines.bin` and the next start builds them all at boot, so the fight
  intros no longer show half-drawn characters while pipelines build (toolkit patch 2068, `PSO_WARMUP=0` turns it off).
- D3D12: the GPU wait only gives up on a removed device, and the deferred-release table grows instead of freeing its
  oldest entry early (toolkit patch 2066).
- RPCS3 save import stages incoming files before replacing progress, restores the old files after an I/O error,
  and refuses linked destinations that could overwrite files outside the selected save.
