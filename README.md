<p align="center"><img src="assets/banner-2x.png" width="100%" alt="J-Stars Victory VS-EX, Fan Revival. White ASCII art on black: a star with a 3D J inside on the left; on the right the title J-Stars Victory, the letters VS-EX and Fan Revival; below them: unofficial fan project, no game files included, you need your own copy."></p>

# J-Stars Victory VS-EX, Fan Revival

**J-Stars Victory VS+ running natively on Windows: the game's own code, recompiled to x64 on your PC from your own copy.**

[![CI](https://github.com/Tsamiuy/jsp-vs-ex/actions/workflows/ci.yml/badge.svg)](https://github.com/Tsamiuy/jsp-vs-ex/actions/workflows/ci.yml) [![licence: MIT](https://img.shields.io/badge/licence-MIT-333333?labelColor=000000&style=flat-square)](LICENSE) [![status: work in progress](https://img.shields.io/badge/status-work%20in%20progress-333333?labelColor=000000&style=flat-square)](#status)

**[Status](docs/notes/STATUS.md)** · **[Features](docs/FEATURES.md)** · **[Roadmap](docs/ROADMAP.md)** · **[Installer](installer/README.md)** · **[Building](docs/BUILDING.md)** · **[Notes](docs/notes/README.md)** · **[Contributing](CONTRIBUTING.md)**

> **Bring your own game.** This repository holds no game code, no game assets and no firmware: no ISO, EBOOT,
> recompiled game code, captures or screenshots. You need your own copy of the game (US BLUS31519 or EU BLES02136) and
> its decrypted EBOOT, from your own copy. Please do not post game files in issues or pull requests.

| ~30 fps | ~29 fps | 34,092 | 0 |
|:---:|:---:|:---:|:---:|
| fights, newer v19 tree (opt-in) | fights, default v15 tree | game functions recompiled | game files in this repository |

The frame rates were measured on one Windows machine; see [Status](#status).

## How to play

**Windows 10 or 11, x64**

1. Have your game's disc folder (the one with `PS3_GAME`) and its decrypted `EBOOT.elf` ready.
2. Build `jsp-setup.exe` once: [installer/README.md, "Building the installer"](installer/README.md#building-the-installer-windows).
   It needs Git, Python, Inno Setup and an internet connection for the pinned compiler.
3. Run `jsp-setup.exe` (no admin rights) and point it at the disc folder and `EBOOT.elf`. It builds the port on your PC.
4. Start **JSP Fan Revival** from the Start menu.

Caveat: this installer has not been run on a real Windows PC yet (see its [status table](installer/README.md#status)); the
first build takes a while and needs several GB of free space, and `Build again` in the Start menu resumes it.

**A ready-made installer download:** coming soon (no release is published yet).

**Linux and Steam Deck (Proton):** coming soon. Not supported yet; the newer tree has reached fights under Proton in tests.

## What makes it different

| | |
|---|---|
| Native x64 code, not an emulator: the game's own code translated ahead of time with [ps3recomp](https://github.com/sp00nznet/ps3recomp) | [docs/LAYOUT.md](docs/LAYOUT.md) |
| Built on your PC from your own copy; nothing from the game is shipped | [installer/README.md](installer/README.md) |
| No server contact unless you turn Online Battle on: no update check, no crash report, no telemetry | [installer/README.md](installer/README.md) |
| Higher resolutions, a resizable window and borderless fullscreen | [Features: Display](docs/FEATURES.md#resolution) |
| Keyboard, mouse or controller, with a launcher to rebind keys | [Features: Input](docs/FEATURES.md#input-modes) |
| Training Mode in Free Battle | [Features: Training](docs/FEATURES.md#training-mode) |
| The game's own Online Battle over a self-hosted re-psn server (experimental, opt-in, newer tree) | [docs/ONLINE.md](docs/ONLINE.md) (quick start) |

## Coming soon

- A published installer and portable zip.
- The newer v19 tree as the default, once it has had the tests the v15 tree had ([issue #9](https://github.com/Tsamiuy/jsp-vs-ex/issues/9)).
- Online matches with rollback netcode, spectating and lobbies ([Roadmap, section 4](docs/ROADMAP.md)).
- More match options and Training options ([Roadmap, section 3](docs/ROADMAP.md)).
- Linux through Proton ([Roadmap, Platforms](docs/ROADMAP.md#platforms)).

## Status

> **Work in progress, not a release.** The default build (the v15 tree) lifts and builds on Windows (Direct3D 12) and
> runs through the opening movie, the title screen, the menus and fights, with sound, at about 29 frames per second on
> the maps that were tested. It has a launcher and a Training mode. These runs were made on one Windows machine.
>
> An opt-in newer tree (v19, built on ps3recomp v0.19.0; the installer switch is still called `--tree v18`) also boots and plays fights with sound
> and the opening movie, at about 30 frames per second in the demo and in Free Battle on the same machine (its earlier
> v18 build, on toolkit v0.18.0, ran at 16 to 22). It is not the
> default yet: it has had far fewer tests (the EU disc, the first-start window and long runs are not re-checked on it)
> and has known problems, such as washed-out lighting on some floors (see [issue #9](https://github.com/Tsamiuy/jsp-vs-ex/issues/9)).
>
> Dated table (top refreshed 2026-10-30, for the v15 tree): [docs/notes/STATUS.md](docs/notes/STATUS.md).

**How it works:** the installer lifts the game's PowerPC and SPU code from your `EBOOT.elf` to C, compiles it with a
bundled compiler and links it against the ps3recomp runtime, which provides the PS3 system libraries and draws through
Direct3D 12. Details: [installer/README.md](installer/README.md) and [docs/LAYOUT.md](docs/LAYOUT.md).

## Known problems

- Online and Cross-Save upload need PSN and do nothing in the default build (Cross-Save is now "Exit Game"). The v19 tree (`--tree v18`) has
  an experimental Online Battle over a re-psn server, off unless turned on: see [installer/README.md](installer/README.md).
- A hand build from this repository (the pinned ps3recomp v0.15.0 submodule plus `toolkit/ps3recomp-local.patch`, see
  [docs/BUILDING.md](docs/BUILDING.md)) has not been tried on a clean machine; the notes build from a local checkout
  with the same changes. The installer applies the same patch for you.
- Read [docs/SCRIPTS.md](docs/SCRIPTS.md) before running the scripts: they change your power plan and mount the disc image.

## Contributing

Pull requests are welcome: [CONTRIBUTING.md](CONTRIBUTING.md). Only `main` is used; rules for AI coding agents are in
[AGENTS.md](AGENTS.md). Community: [Discord](https://discord.gg/h3j2wFwqvn).

## AI, licence, disclaimer

Developed with AI agent assistance under the maintainers' direction.

The port's own code, tools and documentation: MIT, see [LICENSE](LICENSE). Third-party software and licences:
[NOTICE.md](NOTICE.md). Security problems: [SECURITY.md](SECURITY.md).

Unofficial fan project. Not affiliated with, authorized or endorsed by the game's publisher, developer or rights
holders; trademarks belong to their owners. **You need your own copy of the game.**
