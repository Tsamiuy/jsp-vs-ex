# Changelog

All notable changes are listed here, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions are `X.Y.Z` (the number in `installer/VERSION`).
0.0.1 is the first version and has not been released yet, so everything so far is under Unreleased.

## [Unreleased]

What `main` has so far; `docs/FEATURES.md` lists each item with how it was tested.

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
- Opt-in "Unlock everything" cheat (launcher Cheats tab, `JSP_UNLOCK_ALL=1`): save backup first, one start only.
- The features page (`docs/FEATURES.md`), generated from `docs/features.tsv`.
