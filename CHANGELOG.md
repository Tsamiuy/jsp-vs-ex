# Changelog

All notable changes are listed here, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions are `X.Y.Z` (the number in `installer/VERSION`).
0.0.1 is the first version and has not been released yet, so everything so far is under Unreleased.

## [Unreleased]

### Added

- Port Settings page (opt-in, `JSP_PORT_MENU=1`, US 1.00 build): toolkit patches 2516-2518 (menu label replacement, present-time overlay layer, label mask by open panel), `jsp_port_menu.cpp`, the lifter's `--insn-patches` for the Options row count (`installer/port/jsp/lift_insn_patches.json`), and the label and font art drawn at installer build time (`menu_label_art.py`, `menu_font_art.py`). The Online page edits Method, Server, Online name and Community preset with a character picker and saves them to the shared settings folder (`repsn.ini`, `community.ini`, as the launcher does), applied on the next start; `JSP_PORT_MENU_EDIT=0` keeps it read only. Overlay text is blitted in two passes (rims, then bodies) so letters no longer cover each other. Checked in game under Proton with a player-side build of the US disc: the page opens, edits, saves and closes; without `JSP_PORT_MENU` the Options list keeps its five rows. See `docs/notes/PORT_SETTINGS.md`.

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
