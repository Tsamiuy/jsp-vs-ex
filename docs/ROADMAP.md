# Roadmap

What is planned, in rough order. ✅ done · 🔧 in progress · 📋 planned. No dates: the order is the plan, the timing is
not promised. Options that change gameplay are opt-in; display and input improvements may default on.
What the port adds today: [FEATURES.md](FEATURES.md). Dated state and evidence: [notes/STATUS.md](notes/STATUS.md).
The ✅ items describe the default build, the port in `jstars/port` on the ps3recomp v0.15 toolkit. A second port on the
v0.18.0 toolkit is on main in `installer/port` (#2, merged); it is an opt-in installer choice (`--tree v18`).

## 1. Playable on Windows from your own copy  🔧

| | |
|---|---|
| ✅ | Boots through the opening movie (with sound), the title screen and every Main Menu entry ([details](FEATURES.md#boot-menus)) |
| ✅ | Fights at about 29 frames per second on the maps tested, with music and effects (the maintainer's runs on one Windows machine) ([details](FEATURES.md#fights)) |
| ✅ | **Installer** with a bundled compiler, on the PCs tested so far: it builds the port on your PC from your own disc copy and your own decrypted EBOOT (from your own copy, see RPCS3 and its documentation); nothing of the game is shipped or downloaded. A portable zip is built from the same files |
| 🔧 | Menus with art and text on Windows 10: compressed textures whose size is not a multiple of 4 are now decoded on the CPU (checked on Windows 11; not yet confirmed on Windows 10) |
| 🔧 | Installer runs on more PCs: each report on an issue is checked against the logs (the Start menu's support bundle collects them) |
| ✅ | **Long sessions:** the fight freeze (capped log counters overflowed after minutes in a fight) is fixed; a 30-minute scripted run and a 20-minute normal session had no freeze ([details](FEATURES.md#long-sessions)) |
| 🔧 | A fight that stayed a flat brown screen on one launch was seen once and not reproduced |
| ✅ | The glow layer around effects was compared with RPCS3 screenshots and matches, so no fix was needed (by eye, not a pixel comparison) ([details](FEATURES.md#effects-glow)) |
| 📋 | A tagged pre-release once a fresh install on Windows reaches a fight without hand steps |

## 2. Display and controls  🔧

- ✅ Higher internal resolution (720p, 1440p, 4K), resizable window and borderless fullscreen ([details](FEATURES.md#resolution), [more](FEATURES.md#window-fullscreen))
- ✅ Keyboard, keyboard and mouse, or controller, each with its own bindings; a keyboard or a second controller as player 2 ([details](FEATURES.md#input-modes), [more](FEATURES.md#second-player))
- 🔧 DualSense and DualSense Edge read directly (USB or Bluetooth); hands-on tests of the buttons are pending ([details](FEATURES.md#dualsense))
- ✅ Launcher with resolution, window size, input choice, rebinds and the fight HUD size ([details](FEATURES.md#launcher), [more](FEATURES.md#hud-size))
- 🔧 "EX" in place of the "+" in the title logo, matching the port's name (optional, `JSP_EX_LOGO=0` keeps the "+"; merged; seen on the title screen in a Windows 11 test, the smaller pre-scaled copy of the logo is not yet confirmed) ([details](FEATURES.md#ex-logo))
- 📋 Split-screen layout, brightness and glare, DLSS (parked in the notes) ([details](FEATURES.md#split-screen-layout), [more](FEATURES.md#brightness-glare), [more](FEATURES.md#dlss))

## 3. Modes and match options (opt-in)  🔧

- ✅ **Training Mode** in Free Battle: a standing dummy, no KO with health refill, unlimited time ([details](FEATURES.md#training-mode))
- 📋 Training options: infinite Stamina, infinite support, unlimited Victory Burst ([details](FEATURES.md#training-options))
- 📋 Two local players in Training ([details](FEATURES.md#training-2p))
- 📋 KO count up to 5 as a match option in every mode except Training ([details](FEATURES.md#ko-count))
- 📋 1v1 and 3v3 matches beside the usual 2v2 (the game is built around 2v2 with a support character; 3v3 only if the
  engine allows it); all three online too, see section 4 ([details](FEATURES.md#match-sizes))
- 📋 Your own music files in place of the game's tracks (your files, never shipped) ([details](FEATURES.md#own-music))
- 📋 An unlock-all switch for the progression bars and J-Points, as a save edit with a backup ([details](FEATURES.md#unlock-all))

## 4. Online with rollback netcode  📋

A goal, not started. It needs fights that run the same on every machine and a way to save and restore the fight state
each frame. Online is opt-in: the port connects only when you choose online play, and only to the players or server you
pick.
Every gameplay option in a match is the same for all players; ranked uses one fixed rule set.

- 🔧 **The game's own Online Battle over a [psnr](https://github.com/sp00nznet/psnr) server** (experimental, opt-in; how
  to try it: `installer/README.md`, "Online Battle"): in a test of two copies on one PC, both reached a Friendly Match
  room and played into the fight. Not yet tried on Windows or between two PCs
- 📋 **Online matches with rollback netcode:** 1v1 first, then 2v2, and 3v3 if the engine allows it (see section 3);
  direct connection first ([details](FEATURES.md#online-rollback))
- 📋 **Spectating** (opt-in): the host opens the lobby to watchers; each watcher's camera changes only what they see ([details](FEATURES.md#spectating))
- 📋 Lobbies, matchmaking, ranked ([details](FEATURES.md#lobbies-ranked))
- 📋 Online Training for up to 4 players ([details](FEATURES.md#online-training))
- 📋 Online co-op Victory Road (the largest online item) ([details](FEATURES.md#online-victory-road))

## 5. After 1.0  📋

- 📋 **Colour palettes** for characters, made on your PC from your own game's textures (for example Super Saiyan Blue
  for Goku and Vegeta, Kurama mode for Naruto); packs made by other people stay outside this repository
  ([details](FEATURES.md#colour-palettes))

## Platforms

- Windows 11, x64, with Direct3D 12: the target today (Windows 10 partly: the menu fix is not yet confirmed there)
- Linux through Proton or Wine: not supported yet. The default build has not been run under Proton since the
  thread-local fix (installer patch 0003); the v0.18.0 port (`--tree v18`) reaches fights under Proton in its tests

## Not planned

- Distributing any game files: no disc image, EBOOT, firmware, recompiled game code or game assets. You bring your own
  copy of the game, and the port is built from it on your PC
- Anything that needs PSN: Cross-Save stays unavailable, and online play goes through a psnr server (section 4), never
  PSN

## In short

First a port that installs and plays reliably on Windows from your own copy, then opt-in modes and match options, then
online play with rollback netcode. No dates.
