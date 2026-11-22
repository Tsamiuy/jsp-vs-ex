# Roadmap

What is planned, in rough order. ✅ done · 🔧 in progress · 📋 planned. No dates: the order is the plan, the timing is
not promised. Options that change gameplay are opt-in; display and input improvements may default on.
What the port adds today: [FEATURES.md](FEATURES.md). Dated state and evidence: [notes/STATUS.md](notes/STATUS.md).
The ✅ items were first checked on the earlier port on the ps3recomp v0.15 toolkit (git tag `legacy-v15`). The build is now the port on the v0.19.0 toolkit in `installer/port` (#2); what is not re-checked on it is listed in [issue #9](https://github.com/Tsamiuy/jsp-vs-ex/issues/9).

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
| ✅ | Tagged pre-releases: 0.1.0 to 0.4.0 ([releases](https://github.com/Tsamiuy/jsp-vs-ex/releases)) |

## 2. Display and controls  🔧

- ✅ Higher internal resolution (720p, 1440p, 4K), resizable window and borderless fullscreen ([details](FEATURES.md#resolution), [more](FEATURES.md#window-fullscreen))
- ✅ Graphics options: edge smoothing, shadow quality and distance, model detail, object draw distance ([details](FEATURES.md#graphics-options))
- ✅ Smoother busy scenes on slower PCs: two-thread drawing on by default, frames shown without waiting for the monitor's vblank, and a faster 2 Players split screen ([details](FEATURES.md#two-thread-drawing), [more](FEATURES.md#present-no-vblank-wait), [and](FEATURES.md#split-screen-speed))
- ✅ Keyboard, keyboard and mouse, or controller, each with its own bindings; a keyboard or a second controller as player 2 ([details](FEATURES.md#input-modes), [more](FEATURES.md#second-player))
- 🔧 PlayStation pads (DualSense, DualSense Edge, DualShock 4; up to four, USB or Bluetooth) read directly; hands-on tests with real pads are pending ([details](FEATURES.md#dualsense))
- ✅ Launcher with resolution, window size, input choice, rebinds and the fight HUD size ([details](FEATURES.md#launcher), [more](FEATURES.md#hud-size))
- 🔧 "EX" in place of the "+" in the title logo, matching the port's name (optional, `JSP_EX_LOGO=0` keeps the "+"; merged; seen on the title screen in a Windows 11 test, the smaller pre-scaled copy of the logo is not yet confirmed) ([details](FEATURES.md#ex-logo))
- 📋 Split-screen layout, brightness and glare, DLSS (parked in the notes) ([details](FEATURES.md#split-screen-layout), [more](FEATURES.md#brightness-glare), [more](FEATURES.md#dlss))
- 🔧 Add to Steam from the installer (merged; not yet tried with a real Steam) ([details](FEATURES.md#steam-shortcut))
- 🔧 The launcher's settings editable in the game, on the Port Settings page: Display, Graphics and Controls are in (on by default); the rest are not moved yet ([details](FEATURES.md#ingame-settings))
- 📋 Wide and ultrawide screens (16:10, 21:9, 32:9) without stretching ([details](FEATURES.md#wide-screens))
- 📋 VR through OpenXR: first a virtual screen, later 3D fights (research) ([details](FEATURES.md#vr))

## 3. Modes and match options (opt-in)  🔧

- ✅ **Two-page Main Menu** (on by default): the top page has Battle Modes, J-Customize, J-Point Shop, Gallery and Options; Battle Modes opens a second page with the game's battle rows plus Training Mode and Impossible Mode ([details](FEATURES.md#main-menu-pages))
- ✅ **Training Mode** in Free Battle: a standing dummy, no KO with health refill, unlimited time ([details](FEATURES.md#training-mode))
- 🔧 Training options: Unlimited Stamina, Free Camera and Hide HUD are in; infinite support and unlimited Victory Burst are planned ([details](FEATURES.md#training-options))
- 📋 Two local players in Training ([details](FEATURES.md#training-2p))
- 📋 Arcade for two players on one PC ([details](FEATURES.md#arcade-2p))
- 🔧 KO count, 1 to 5, as a match option: Free Battle has it (L1 / R1 on the Stage Select); the other modes are planned ([details](FEATURES.md#ko-count))
- ✅ **Impossible mode** in Free Battle: a computer opponent that guards and Just Guards whenever it can, as a 2 against 1 fight (opt-in, chosen in a window when Free Battle's Character Select opens) ([details](FEATURES.md#impossible-ai))
- ✅ **3v3 Free Battle:** a window on Free Battle's list asks for 2 vs 2 or 3 vs 3 (three fighters per team, six in the fight; with the two-page Main Menu, on by default; Free Battle only, not tried online) ([details](FEATURES.md#free-battle-3v3))
- 📋 1v1 matches beside the usual 2v2, and 3v3 in the other modes (the game is built around 2v2 with a support character);
  all of them online too, see section 4 ([details](FEATURES.md#match-sizes))
- 📋 Your own music files in place of the game's tracks (your files, never shipped) ([details](FEATURES.md#own-music))
- ✅ An unlock-all switch for cards, progression bars, J-Points, characters, titles and more, as a save edit with a backup (launcher: Cheats tab) ([details](FEATURES.md#unlock-all))
- 📋 **Cross-Save with RPCS3, without PSN:** one save that moves both ways between the port and RPCS3 as a plain copy,
  since RPCS3 keeps this game's save unencrypted. Setup can already import an RPCS3 save once; not tested yet: an RPCS3
  save in the newer tree (`--tree current`) and a port save in RPCS3. Saves from a real PS3 are encrypted and are a separate
  question (findings in [issue #30](https://github.com/Tsamiuy/jsp-vs-ex/issues/30), closed)

## 4. Online with rollback netcode  🔧

An experimental first version is in the build. A new install turns it on for Online Battle (setup writes the online settings when there are none; the settings
window's Online tab turns it off). It needs fights that run the same on every machine and a way to save and restore the fight state each frame. How it works and
the plan, with what we use and fall back to: [ROLLBACK.md](ROLLBACK.md). The port connects only for online play, and only to the server and players in the
settings (none by default: online is off until the player chooses a server).
Every gameplay option in a match is the same for all players; ranked uses one fixed rule set.

- 🔧 **The game's own Online Battle over a [re-psn](https://github.com/undecuple/re-psn) server** (a fork of psnr) (experimental, on after a new install; how
  to try it: `installer/README.md`, "Online Battle"): in a test of two copies on one PC, both reached a Friendly Match
  room and played into the fight. Play testers report online matches on their Windows PCs since 0.1.0 (no logs yet); the EU build, two real PCs with different settings or versions, and the Impossible mode online are not tried
- 🔧 **Online matches with rollback netcode** (experimental, on after a new install; [ROLLBACK.md](ROLLBACK.md)): the game's own Friendly Match room
  pairs the two players (one per PC, 2 vs 2) and rollback runs the fight, US 1.00 disc only; tried with two copies on one Windows 11 PC, not between two PCs.
  The earlier direct-IP and re-psn role starts stay for tests ([ROLLBACK.md, section 5](ROLLBACK.md#5-try-it-experimental-direct-ip-rollback)); one such fight ran between two PCs on one home network.
  Then 2v2 and 3v3 (3v3 Free Battle works offline, see section 3; not tried online); lobbies over re-psn ([details](FEATURES.md#online-rollback))
- 📋 **Spectating** (opt-in): the host opens the lobby to watchers; each watcher's camera changes only what they see ([details](FEATURES.md#spectating))
- 📋 Lobbies, matchmaking, ranked ([details](FEATURES.md#lobbies-ranked))
- 📋 Online Training for up to 4 players ([details](FEATURES.md#online-training))
- 📋 Online Arcade for two players, the smaller co-op step before Victory Road ([details](FEATURES.md#online-arcade-2p))
- 📋 Online co-op Victory Road (the largest online item) ([details](FEATURES.md#online-victory-road))

## 5. After 1.0  📋

- 📋 **Colour palettes** for characters, made on your PC from your own game's textures (for example Super Saiyan Blue
  for Goku and Vegeta, Kurama mode for Naruto); packs made by other people stay outside this repository
  ([details](FEATURES.md#colour-palettes))
- 📋 **Modding:** a mods folder for textures, HUDs, music and data, and export tools for models and scenes that run on
  your PC from your own copy ([details](FEATURES.md#modding))

## Platforms

- Windows 11, x64, with Direct3D 12: the target today (Windows 10 partly: the menu fix is not yet confirmed there)
- Linux through Proton or Wine: not supported yet. The author's runs of the default build under Proton reached the menus, fights and
  an online room on one PC ([issue #9](https://github.com/Tsamiuy/jsp-vs-ex/issues/9)); it is not tested on other setups

## Not planned

- Distributing any game files: no disc image, EBOOT, firmware, recompiled game code or game assets. You bring your own
  copy of the game, and the port is built from it on your PC
- Anything that needs PSN: the PSN Cross-Save stays unavailable (Cross-Save with RPCS3 is planned, section 3), and online
  play goes through a re-psn server (section 4), never PSN

## In short

First a port that installs and plays reliably on Windows from your own copy, then opt-in modes and match options, then
online play with rollback netcode. No dates.
