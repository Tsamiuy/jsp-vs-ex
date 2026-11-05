<p align="center"><img src="assets/banner-2x.png" width="100%" alt="J-Stars Victory VS-EX, Fan Revival. White ASCII art on black: a star with a 3D J inside on the left; on the right the title J-Stars Victory, the letters VS-EX and Fan Revival; below them: unofficial fan project, no game files included, you need your own copy."></p>

# J-Stars Victory VS-EX, Fan Revival (work in progress)

**J-Stars Victory VS+: unofficial PC port (static recompilation)**

[![CI](https://github.com/Tsamiuy/jsp-vs-ex/actions/workflows/ci.yml/badge.svg)](https://github.com/Tsamiuy/jsp-vs-ex/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue)](LICENSE)
![Status: work in progress](https://img.shields.io/badge/status-work%20in%20progress-orange)
![Windows x64](https://img.shields.io/badge/platform-Windows%20x64-0078D6)
![Direct3D 12](https://img.shields.io/badge/graphics-Direct3D%2012-555)

**[Status](docs/notes/STATUS.md)** · **[Features](docs/FEATURES.md)** · **[Roadmap](docs/ROADMAP.md)** · **[Building](docs/BUILDING.md)** · **[Scripts](docs/SCRIPTS.md)** · **[Layout](docs/LAYOUT.md)** · **[Notes](docs/notes/README.md)** · **[Contributing](CONTRIBUTING.md)**

A PC port of **J-Stars Victory VS+** (PS3, BLUS31519) to Windows x64, built with the
[ps3recomp](https://github.com/sp00nznet/ps3recomp) toolkit (MIT). It is a static recompilation: the game's own code,
translated ahead of time to native x64. Unofficial fan project.

## Status

> **Work in progress, not a release.** As of the notes' last entries (2026-10-30) the game lifts and builds on Windows
> (Direct3D 12) and runs through the opening movie, the title screen and the menus; fights run at about 29 frames per
> second on the maps that were tested, with sound. These runs were made on one Windows machine. Dated table:
> [docs/notes/STATUS.md](docs/notes/STATUS.md). What the port adds: [docs/FEATURES.md](docs/FEATURES.md).

## Requirements

- **Your own copy of the game** (the notes use the US disc, BLUS31519).
- **A Windows x64 build environment** for a hand build: Visual Studio 2022 Build Tools with clang-cl, CMake, Ninja and
  Python 3. The [installer](installer/README.md) brings its own compiler and needs none of these.
- **A ps3recomp checkout with this project's patches**, and an **RPCS3 installation** (the port reads its `dev_flash`
  folder at run time, read only).

Full list and steps: [docs/BUILDING.md](docs/BUILDING.md).

## No game data

This repository holds no game data: no ISO, EBOOT, firmware, recompiled game code, captures or screenshots. Please do
not post any of these in issues or pull requests. The EBOOT comes from your own copy of the game; for how to obtain
the decrypted file, see RPCS3 and its documentation.

## Known problems

- Online and Cross-Save upload need PSN and do nothing (Cross-Save is now "Exit Game").
- A hand build from this repository (the pinned ps3recomp v0.15.0 submodule plus `toolkit/ps3recomp-local.patch`, see
  [docs/BUILDING.md](docs/BUILDING.md)) has not been tried on a clean machine; the notes build from a local checkout
  with the same changes. The installer applies the same patch for you.
- Read [docs/SCRIPTS.md](docs/SCRIPTS.md) before running the scripts: they change your power plan and mount the disc image.

## Contributing

Pull requests are welcome: [CONTRIBUTING.md](CONTRIBUTING.md). Only `main` is used; rules for AI coding agents are in
[AGENTS.md](AGENTS.md). Community: [Discord](https://discord.gg/h3j2wFwqvn).

## AI, licence, disclaimer

Developed with AI agent assistance under the maintainer's direction.

The port's own code, tools and documentation: MIT, see [LICENSE](LICENSE). Third-party software and licences:
[NOTICE.md](NOTICE.md). Security problems: [SECURITY.md](SECURITY.md).

Unofficial fan project. Not affiliated with, authorized or endorsed by the game's publisher, developer or rights
holders; trademarks belong to their owners. **You need your own copy of the game.**
