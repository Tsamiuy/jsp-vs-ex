# Changelog

All notable changes are listed here, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions are `X.Y.Z` (the number in `installer/VERSION`).
0.1.0 to 0.5.1 are pre-releases; the first full release will be 1.0.0. Changes not in a release yet are under Unreleased.

## [Unreleased]

## [0.5.1] - 2026-11-19

### Added

- Choose the netcode in the game, not yet tried in a running game (Port Settings, `JSP_PORT_MENU=1`, Online page): a Netcode row with Off, Delay (the game's own online) or Rollback (experimental, direct IP: Role, Port or Host address). Only one can be on; saving applies on the next start; a hand-edited pair with both on is refused and neither runs. Host-tested only.
- Rollback over a re-psn server instead of a typed address, **tested on one PC only, not yet across real routers** (experimental, opt-in; Online page: Netcode = Rollback, Connection = re-psn; toolkit patch 7918 brings in the re-psn v0.8.0 client): one player creates a room, the other joins it, the two PCs punch a direct path and use the server's relay only if allowed. Host-tested against a fake server; two copies on one PC through a local re-psn v0.8.0 server created and joined a room, took the direct path and stayed in sync for 2,017 ticks with a simulated 30 ms delay. The relay path has not run.

### Fixed

- Rollback between two PCs: the build id the two games compare now comes from the release content (version, game region, glue sources and the patched toolkit's sources), not from the compile time. The installer builds the game on each PC, so two installs of the same release refused each other as "a different game build". Host-tested (same content in two folders and a CR-LF copy give one id; a changed glue or toolkit source, version or region gives another; a changed source re-runs the configure step, so an update in place gets the new id); not yet tried on two PCs. A 0.5.1 game refuses a 0.5.0 one as a different build.

## [0.5.0] - 2026-11-18

### Added

- Rollback netcode, experimental and opt-in; tested on one PC only, not yet on two PCs (off unless `JSP_ROLLBACK=1`; when off, only the small always-on bookkeeping listed in the docs runs): two PCs connect by IP address (`JSP_ROLLBACK_LISTEN=PORT` on the host, `JSP_ROLLBACK_JOIN=IP:PORT` on the client), the host's match settings apply to the client for that match only, and a Free Battle fight runs with rollback from its first tick ("Waiting for host...", "Reconnecting... N s" with F9 held to end the match, "Opponent disconnected"). Built on GekkoNet (BSD 2-Clause) and toolkit patches 7910-7917. Two copies on one PC with a simulated 30 ms delay stayed in sync for 2,697 to 3,495 ticks per fight (over 100 rollbacks each, no desync; not played to the end screen). One player per PC, menus not linked, a fixed match seed, setup through `play_settings.txt` only; a warm pipeline cache is recommended (a first-launch pipeline build can stall it for seconds). Details: [docs/ROLLBACK.md](docs/ROLLBACK.md#5-try-it-experimental-direct-ip-rollback).
- PlayStation pads, not yet tested on real pads (toolkit patch 7909): up to four DualSense, DualSense Edge or DualShock 4 (v1, v2) pads over USB or Bluetooth are read directly, each its own player next to Xbox-style pads and in the player-order join; checked only against the documented reports; no rumble or lightbar yet (`PAD_NO_PLAYSTATION=1` turns them off, `PAD_NO_DUALSENSE=1` still works).
- Choose the player order, not yet tried with real pads (toolkit patch 7908): each pad or the keyboard takes the next free player by its first button press, started from the Port Settings Controls page (`JSP_PORT_MENU=1`) or by holding Select + L3 + R3 for 2 s; the order is unchanged until used (`PAD_JOIN=0` turns it off). Host tested only.

### Changed

- Rumble goes to the pad the game sees on the port, on by default even without a reorder (toolkit patch 7908; Keyboard mode with a pad as player 2: the pad rumbles for player 2). Not yet tried with real pads.
- Toolkit export: the patches are applied with Git line-ending conversion off, so a Windows Git set to convert line endings no longer breaks patches made against files with CR-LF lines.
- Releases: the Discord post pings the project's own Discord role (and nobody else), so people who follow the project get a notification (`.github/workflows/release-notify.yml`).
- Releases: the Discord message now opens a new thread in the project's Discord forum and carries the release notes (new features, fixes and changes), cut to 1,800 characters, with a "Full notes and download" link; an empty notes field keeps the short message, and mentions stay disabled (`.github/workflows/release-notify.yml`).
- Docs brought up to date after 0.4.0: ROADMAP (releases, Training options, KO count, the Impossible mode, Port Settings pages, online and Proton wording), the feature list (new Impossible mode row; KO count, Training options and in-game settings now in progress), README and INSTALL (`SHA256SUMS` from 0.4.0, speed numbers now in 0.4.0), KNOWN_ISSUES (0.4.0, a row for the new Free Battle options), `.recomp.json` notes and help list.

## [0.4.0] - 2026-11-16

### Added

- Free Battle: "KOs to win" on the Stage Select (L1 / R1, 1 to 5; 3 is the game's own) and an Impossible mode. On Character Select, Right at 5 stars in a CPU slot turns Impossible on (orange-red stars; AI level 19, guards every frame it has no defence of its own, Just Guards every just-guardable hit, holds its guard against counters). A window at the start of Free Battle's Character Select offers Free Battle or Impossible (2 vs 1: blue leader slot is the Impossible CPU, Player 2 takes the red partner slot, KOs fixed at red 1 and blue 2). Needs three more lifted entry hooks (`installer/port/jsp/lift_game_hooks.json`, lifted with the LOD hooks only when their program check accepts the EBOOT, so the first run re-lifts). 4 and 5 KOs keep the game's 3-KO bars for now. Not tested: a real second controller, Impossible on the partner slots in a fight, online play with the Impossible mode.
- Toolkit patch 2920: a title port can tint rectangles of the finished frame (`g_rsx_tint_rects`: up to 8 rectangles and a colour each; only saturated yellow pixels move towards the colour, so white, grey and skin stay). Nothing happens until the port sets it; the Impossible mode uses it for the orange-red stars.

### Changed

- Docs brought up to date with what changed since 0.3.0: README status table (frame rate on slower CPUs), KNOWN_ISSUES (frame-rate row, issue #9 date), TROUBLESHOOTING (what the support bundle's `facts.txt` shows), and three new Features rows (graphics options, two-thread drawing, frames shown without the vblank wait) with ROADMAP lines.
- Two per-draw cuts on the drawing thread (toolkit patches 2918 and 2919): strips and fans with 16-bit indices take a tight loop instead of the general index loop, the vertex-constant copy no longer reads back from write-combined upload memory, and a run of NV308A inline-transfer words inside one IO page is written with one address resolve and one tiling check (`VPULL_FAST16=0` and `GCM2D_RUN=0` turn them off). With the CPU held at its base clock the title demo went from about 28.0 to 29.5 flips per second and the drawing cost per frame from about 21.8 to 17.1 ms; the pictures are unchanged.
- Two-thread drawing is on by default (toolkit patch 7907, `PIPE`): the preparing half of drawing runs on its own thread unless `PIPE=0`. The settings window's "Two-thread drawing" box starts ticked and now writes `PIPE_SEEN=2` next to `PIPE`; a `PIPE=0` in a settings file without that marker (every file written while it was off by default) no longer counts, so existing installs get the new default. The experimental online session rules (`jsp_session_settings.h`) now ask for PIPE on in the conservative profile. Online matches between players with different PIPE settings, and between old and new versions, are not tested.
- Frames are presented without waiting for the monitor's vblank (toolkit patch 2915). The port paces itself at 30 FPS on its own 60 Hz clock, and the vblank wait put a second clock behind it: on a 60 Hz monitor every frame that took between 33 and 50 ms was shown after 50 ms, which read as exactly 20 FPS in busy scenes. The window and the borderless fullscreen window are composed, so nothing tears. `D3D12_VSYNC=1` waits for the vblank as before; `D3D12_VSYNC=0` (no wait, tearing allowed) is unchanged. The mode is printed once in the log (`[D3D12] present: ...`).
- Support bundle: `facts.txt` now lists every display (monitor name, adapter, size, refresh rate, which one is the primary) and, when the game is running while the bundle is made, the display its window is on. A 60 Hz screen turns missed 30 fps frames into 20 fps, and a virtual display (Parsec, a headset) can be the one in use. It also shows the game's present mode (the `[D3D12] present:` start-up line, printed since toolkit patch 2915) when the log has it. Read only; no serial numbers or device IDs.
- Docs describe the current tree only (issue #152, step 3): `docs/LAYOUT.md`, `docs/BUILDING.md`, `docs/SCRIPTS.md` and `docs/notes/SETUP.md` are rewritten for `installer/port` on ps3recomp v0.19.0; the notes README says that `jstars/...` paths in dated notes refer to the git tag `legacy-v15`.
- The earlier v0.15 port is removed from the tree (issue #152, step 2): `jstars/`, the old toolkit patches `installer/patches/0001-0007`, `installer/tools/jsp_spu.py`, `launcher/make_launcher19.py` and `launcher/build_launcher.bat`. It stays reachable in the git tag `legacy-v15`. The `third_party/ps3recomp` submodule now points at ps3recomp v0.19.0, the toolkit the current port is built on. `NOTICE.md`, `.gitignore`, `.gitattributes`, `tools/release.py` and the CI checks no longer name the removed files.
- First-run step (portable zip, and "update in place"): `jsp-settings.exe` is now rebuilt when `tools/jsp-settings.cs` changed, not only when the exe is missing, so an updated folder no longer keeps the old settings window. The source's SHA-256 is kept in `jsp-settings.exe.src-sha256` beside the exe (file dates are not used). The new exe is built under a temporary name and replaces the old one only when it compiled; if the window is still open the old one stays and the step says so.
- In-game Port Settings: two Graphics labels were cut off at the value column, so "Full-size smoke and effects" is now "Full-size effects" and "FPS counter (bottom left)" is "FPS counter". The root page shows the settings file path with `%APPDATA%`, `%LOCALAPPDATA%` or `%USERPROFILE%` in place of the user's folder, so a screenshot no longer carries the Windows user name (display only; the path used is unchanged).
- Releases carry one checksum file, `SHA256SUMS` (the `sha256sum` format with LF line ends, so `sha256sum -c SHA256SUMS` works), instead of `SHA256SUMS.txt` plus a `.sha256` file for the portable zip. Applies from the next release; v0.3.0 keeps its old files. `CITATION.cff` is new, for the citation box on the repository page.
- Docs: the README follows the shared port skeleton (Status first, then Download and play, with "How AI was used" as the AI heading), and new docs/INSTALL.md and docs/TROUBLESHOOTING.md hold requirements, setup refusals, troubleshooting by symptom, the support bundle, saves, updating, uninstalling and SHA-256 checks.

## [0.3.0] - 2026-11-13

### Added

- Settings window: a Graphics tab after Display with four drop-downs: Shadows (`SHADOW_SCALE`), Shadow distance (`SHADOW_RANGE`), Model detail (`MODEL_DETAIL`) and Object draw distance (`OBJECT_DISTANCE`), the same keys and choices as the in-game Graphics page. A missing or unknown value shows the default.
- Settings window: a Cheats tab with one box, "Unlock everything (cheat)". Ticking it makes a checked backup of the save first, then starts the next game with `JSP_UNLOCK_ALL=1` for that one start only and unticks itself; it refuses when the game has run but no save can be found or copied. A `JSP_UNLOCK_ALL` line written by hand into `play_settings.txt` is dropped when the window loads and saves the file.
- Two-thread drawing, an option for busy fights: with `PIPE=1` (the settings window's new "Two-thread drawing" box on the Display page, off by default) the preparing half of drawing runs on its own thread while the other half submits (toolkit patch 2912); the fragment program constants are written once per draw batch instead of once per draw (toolkit patch 2914, always on). Needs one more CPU core. Measured on the title demo: 29.65 flips per second (29.84 with the box ticked).
- Releases: publishing a release (or pre-release) posts one message with its link to the project's Discord channel (`.github/workflows/release-notify.yml`); the webhook URL is kept only in the repository secret `DISCORD_RELEASE_WEBHOOK`, and without it the workflow sends nothing.
- Docs: `docs/ROLLBACK.md` step 3 updated: the fight-state snapshot restores and replays 60 ticks with the same state hashes; a full copy is too slow, so copying only changed pages is next.
- Docs: `docs/ROLLBACK.md` explains rollback netcode and this port's plan for it: the library (GekkoNet, with GGPO and GGRS as fallbacks), the transport (direct UDP, then a relay), the two ways to save the fight state, and the steps so far.
- Installer: unchecked task "Unlock everything (cheat)" with a choice between a separate TEST save (recommended) and the normal save; it only writes `unlock=` and `savemode=` into `jsp-install.ini`. The launcher's Cheats tab gets "Save in use" (Normal / Test, shown in the window title): the test save is `<hdd0>_gamedata_test` (`PS3_GAMEDATA_ROOT`, first start copies the normal save in, installed game data shared), a normal save is asked about before it is unlocked, and the dated backup covers the save in use. Not built or run on Windows yet; source checks in `unlock_option_test.py`.
- Port Settings (opt-in, `JSP_PORT_MENU=1`): Display, Graphics and Controls pages edit the launcher's settings in the game. They write the same `play_settings.txt` with the same keys; fight HUD size, edge smoothing, FPS counter, shadow distance, model detail and object distance apply at once, the rest on the next start; the launchers and `jsp-launcher.exe` pass the file they loaded in `JSP_SETTINGS_FILE`. A "Launcher only" page lists what stays in the launcher and why. Host test `installer/tools/settings_test.sh`; not built or run yet.
- Installer: unchecked box "Add to Steam" on the last page adds the game as a non-Steam shortcut for each Steam account of the user (refuses while Steam runs, backs up `shortcuts.vdf`, changes nothing twice); `installer/tools/steam_shortcut.py` and its test (ENHANCEMENT).

### Changed

- Drawing costs less CPU in busy scenes: the vertex shader lookup is remembered while the vertex program is unchanged (`VS_MEMO=0` turns it off, toolkit patch 2909), the fragment program's hash, size and texture units are worked out once per program and frame (`FP_FRAME_MEMO=0`, patch 2910), and the off-screen target lookup answers from a small cache while the target table is unchanged (`OFFRT_CACHE=0`, patch 2911). New diagnostics, off by default: `HITCH=<ms>` prints where the time went for a present slower than that (patch 2905) and `GPU_TIME=1` logs the costliest render-target passes (patch 2906); `GPU_OVERLAP=1` (patch 2907, off by default) lets the CPU build the next frame while the graphics card draws.
- Busy fights no longer fall from 30 to about 20 frames per second. The draw list holds 8192 draws instead of 4096: a 2 against 2 fight with the town in view has about 4,200 to 7,000 draws a frame, so every such frame was cut into two batches and the second waited for the graphics card to finish the first (toolkit patch 2913). The frame clock now lets a flip that became ready just after its 30 frames-per-second slot finish at once and brings the next vblank 2.5 ms later instead of a full 60 Hz period, and no flip completes sooner than 33.3 ms after the previous one, so nothing runs above 30 (`LATE_FLIP=0` turns it off, `LATE_VBLANK_MS` sets the delay; toolkit patch 6011 and the port's `main.cpp`). Measured on one Windows machine: the title demo 28.3 -> 29.65 flips per second, a 2 against 2 on Hidden Leaf at 4K about 22 -> 28.
- Training: the Yes/No box of Restart asks "Fight the battle from the beginning?" again (the Hide HUD row had borrowed its title text slot), and the dummy's health refills 1.5 s after it stands up again, not 1.5 s after the last hit while it is still on the ground. The test auto-pilot (`AUTO_MENU=title`, `AUTO_FB`) recovers when a menu step does not move on; these are test-only switches and off in normal play.
- Setup: when RPCS3 lists the game as a single disc image (.iso file), the locate helper recognises it (`is_iso`) instead of refusing it as "not a folder", never searches it for an EBOOT.elf, and the wizard says plainly that setup needs an extracted disc folder (the disc folder page stays empty); setup has no ISO reader (`rpcs3_locate.py`, `rpcs3_wizard.py`).
- Version line on the main menu: it is drawn only on the idle Main Menu (flow state 4, menu mode 0) and hides when the menu starts to leave, and it is smaller (scale 0.65) and placed inside the button bar, clear of the bar's slanted right end (`jsp_version_line.h`, `jsp_port_menu.cpp`; host test `installer/tools/version_line_test.sh`).
- Installer: `assemble_payload.py --tree legacy` (and `v15`) no longer builds the earlier v0.15 tree and exits with a pointer to the git tag `legacy-v15` (issue #152); `firstrun.py` builds only the current tree and stops with an error when `tree.txt` is missing or unknown; CI no longer builds or tests the legacy tree (the `toolkit` job and the legacy patch series check are gone, the `tree` input of `installer.yml` too).
- Repository layout: `Play J-Stars.bat` and `Test Fight.bat` (legacy v0.15 tree only) moved from the root into `jstars/`, next to `play.ps1` which they start; the installer never shipped them.
- Port Settings page and the main-menu version line at a resolution above 720p (`RES_SCALE` above 1, for example 4K): the overlay layer (toolkit patch 2517) set its viewport and scissor to the game's 1280x720 size instead of the back buffer's size, so both were drawn into the top-left corner of the picture. They now cover the whole back buffer.
- Repository layout: `jstars-notes/` is now `docs/archive/jstars-notes/`, `toolkit/` and `tools/toolkit/apply_toolkit.py` are now in `jstars/toolkit/`, and `docs/README.md` lists the documentation by audience. `docs/LAYOUT.md` marks every root folder current or legacy. No files deleted; the installer, CI and the checks use the new paths.
- Releases: `.recomp.json` now names the current version (`"x-version"`), which `tools/release.py bump` rewrites and `--check` (CI) compares with `installer/VERSION`; from 1.0.0 on the bump also sets its status to `released`. Its notes no longer say that nothing has been released.
- CI: both patch series are applied to scratch copies of the pinned ps3recomp, the current port's sources are compiled (syntax only, no game data) with the installer's llvm-mingw, and every host test runs from one list (`tools/ci/host-tests.txt`), also on Windows; `portable_selftest.py` is repaired for the current `firstrun.py`.
- Settings window (`jsp-settings.exe`): Start Game, its "already running" check and the Controls page's key-support notice looked for `game\JStarsRecomp.exe`, but a current-tree install places `game\jsp.exe`, so Start Game answered "The game is not built yet" and the Controls page said the game does not read key changes. The window now takes the exe name from `paths.txt` (`EXE=`), and falls back to `jsp.exe` when that file is there.
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
