# Changelog

All notable changes are listed here, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions are `X.Y.Z` (the number in `installer/VERSION`).
0.0.1 is the first version and has not been released yet, so everything so far is under Unreleased.

## [Unreleased]

What `main` has so far; `docs/FEATURES.md` lists each item with how it was tested.

- Setup checks the decrypted EBOOT against the known 1.00 builds and refuses the other region's. An unknown build can
  be allowed with `--allow-unknown-eboot` (installer: `/ALLOWUNKNOWNEBOOT=1`); it is unsupported.
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
- D3D12: the GPU wait only gives up on a removed device, and the deferred-release table grows instead of freeing its
  oldest entry early (toolkit patch 2066).
- RPCS3 save import stages incoming files before replacing progress, restores the old files after an I/O error,
  and refuses linked destinations that could overwrite files outside the selected save.
- The game is built on your PC from your own copy (ps3recomp, Direct3D 12); no game files are included.
- A Windows installer (`jsp-setup.exe`) and a portable zip that do that build, and a launcher to start the game.
- Higher resolutions, a resizable window and borderless fullscreen, and EX in the title logo.
- Keyboard, keyboard and mouse, or controller, each with its own bindings; a second player on keyboard or a second
  controller.
- Fight HUD size, Exit Game in Options, Training Mode, a Hide HUD row in Training and an FPS counter.
- The features page (`docs/FEATURES.md`), generated from `docs/features.tsv`.
