# Notices

## ps3recomp

This project is built on [ps3recomp](https://github.com/sp00nznet/ps3recomp) by Ned Heller (sp00nznet) and
contributors, licensed under the MIT licence. Parts of `installer/port/jsp/` started as copies of the ps3recomp project
template (for example `main.cpp`, `stubs.cpp` and `CMakeLists.txt`). The patches in `installer/port/patches/` and in
`docs/archive/jstars-notes/patches/` are written against ps3recomp source files. Keep ps3recomp's copyright and licence
notice with any of these files that you copy.

The toolkit itself is not part of this tree: `third_party/ps3recomp` is a submodule that points at ps3recomp's
`v0.19.0` commit, and its files come from the upstream repository. Changes to ps3recomp are kept only as patches:
`installer/port/patches/` (the set the port builds with) and the single-experiment patches under
`docs/archive/jstars-notes/patches`. The earlier v0.15 port (`jstars/`, toolkit v0.15.0) is kept in the git tag `legacy-v15`.

## SMAA

Toolkit patch `installer/port/patches/2511-d3d12-edge-smoothing-fxaa-smaa.patch` adds four files from SMAA (Enhanced Subpixel Morphological Antialiasing) by Jorge
Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro and Diego Gutierrez, <https://github.com/iryoku/smaa>,
licensed under the MIT licence. `AreaTex.h`, `SearchTex.h` and `SMAA.hlsl` are the upstream files with their
copyright and licence header (it names the authors), unchanged. `SMAA_hlsl.inc` is generated from `SMAA.hlsl` and
carries a one-line attribution. The patch puts them into the patched toolkit, where the
patched video library uses them for the "SMAA" edge smoothing option. Keep the header with any copy of these files.

## GekkoNet

The experimental rollback netcode (off unless `JSP_ROLLBACK=1`) is built on [GekkoNet](https://github.com/HeatXD/GekkoNet)
by Jamie Meyer, licensed under the BSD 2-Clause licence. `installer/port/jsp/gekkonet/` is a copy of its library
sources at commit `45dac7e5` with two small local additions, each marked "jsp-vs-ex local addition" in the code
(`installer/port/jsp/gekkonet/PIN.md` lists what was taken and what was added), with its `LICENSE`. It includes
[zpp_bits](https://github.com/eyalz800/zpp_bits) by Eyal Z (MIT, `installer/port/jsp/gekkonet/thirdparty/zpp/LICENSE`). Both
licence texts travel with the sources into the installer package (`port\gekkonet\`) and are compiled into the game exe on the
player's PC. Keep both licence texts with any copy of these files.

## rpcs3_locate

`installer/tools/rpcs3_locate.py` and its test `installer/tools/test_rpcs3_locate.py` are a helper shared between several unofficial ports of PS3 games,
among them undecuple's Project Pyrrhus (see below), under the same MIT licence, vendored from the shared copy of 2026-11-03 (sha256 of the module then
5f98c82632f1e8d6837171080afd62057db2431a1153554f429678d5c0003703), with one change since: a games.yml entry that is an .iso file is recognised (`is_iso`) instead of refused as "not a folder", and is never searched for an EBOOT.elf. It only reads an RPCS3 install's configuration and game folders; its one write is the
save copy that the installer asks the player to confirm. `installer/tools/rpcs3_wizard.py` is this repository's glue around it.

## Project Pyrrhus, Project Helghast and Qwark's Awakening

Parts of the installer, the tools and the fan site started from three other unofficial PS3 ports by undecuple, all under
the MIT licence (Copyright (c) 2026 undecuple): [Project Pyrrhus](https://github.com/undecuple/project-pyrrhus)
(Killzone 2), [Project Helghast](https://github.com/undecuple/project-helghast) (Killzone 3), from which Project Pyrrhus's
installer was made, and [Qwark's Awakening](https://github.com/undecuple/qwarks-awakening) (Ratchet & Clank).

- From Project Pyrrhus's installer: the layout of `installer/jsp.iss`, `installer/launcher/launcher.c`,
  `installer/tools/firstrun.py`, `assemble_payload.py`, `assemble_portable.py`, `fetch_toolchain.py`, `installer_art.py`
  and `llvm-mingw.cmake`. The game-specific parts are this repository's.
- From Qwark's Awakening, which took them from Project Helghast: `tools/site/site.js` (the section switching, the menu
  keys and the game pad polling of the fan site) and `tools/site/check_site.py` (the page parser, the rules that the menu
  page and the plain page agree, the script checks and the class-to-selector rule). The page itself, its drawings, the
  palette and the other site tools are this repository's.
- From Project Helghast: `tools/features.py`. Also the approach of the pinned Inno Setup step in
  `.github/workflows/installer.yml` and of `installer/tools/support_bundle.py`, with no code copied for these two.

Each of these files names its source in a comment.

## RPCS3

[RPCS3](https://rpcs3.net) is a separate tool (GPL-2.0). The notes mention it for reference runs and
comparison screenshots, and the port reads files from an RPCS3 installation at run time. No RPCS3 code is
included in this tree.

## Installer

`installer/` builds a Windows installer with [Inno Setup](https://jrsoftware.org/isinfo.php) (Jordan Russell and Martijn
Laan; its licence allows installers made with it to be distributed). Nothing of Inno Setup is in this tree.
The installer package carries, besides this repository's own files, the embeddable Windows package of
[Python](https://www.python.org) (Python Software Foundation licence, text kept in the package as `toolchain\python\LICENSE.txt`),
which whoever builds the installer downloads from python.org; a pinned build of
[llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (Martin Storsjö; LLVM: Apache-2.0 with LLVM exception; MinGW-w64: ZPL-2.1 and
public-domain parts, some headers derived from Wine under LGPL-2.1 or later; winpthreads: MIT and BSD-3-Clause),
[CMake](https://cmake.org) (Kitware, BSD-3-Clause) and [Ninja](https://ninja-build.org) (Apache-2.0), which whoever builds the
installer downloads with `installer/tools/fetch_toolchain.py` (versions and SHA-256 in `installer/tools/toolchain.json`;
their licence texts go into the package under `licenses\`, and `installer/licenses/LGPL-2.1.txt` is the one text kept in this
tree); the ps3recomp toolkit with `installer/port/patches/`
applied (MIT, see above; SMAA as above); and the licence texts of this repository. The wizard images and icons are drawn at build
time from the project's ASCII star mark and polygon lettering (`installer/tools/installer_art.py`, no font). The Port Settings menu labels and glyphs are drawn at build time in Fira Sans Heavy Italic 4.301
(bBoxType/FiraSans, SIL Open Font License 1.1); the font file is not included here or in the package, and the art contains
only the rendered text.
The two-page Main Menu pictures (row labels, page title and help-bar watermark in English, Spanish, French, German and Italian, `installer/port/jsp/mp_*.inc`, made by `installer/tools/mp_label_langs.py`) are rendered from the same font (Fira Sans Heavy Italic 4.301, SIL OFL 1.1, file SHA-256 a30804a1...faee); the files hold only the rendered text, no font data.

## Installer music

The installer has an "Enable Music?" box (off by default). The six tracks are bytebeat and floatbeat formulas in
`installer/music/`, taken from the [bytebeat-composer](https://github.com/SthephanShinkufag/bytebeat-composer) library
(Copyright (c) 2021 SthephanShi, MIT licence, text below). `installer/tools/installer_music.py` renders them to sound
files at build time; no sound file is stored here. Mode, rate and source of each are in `installer/music/tracks.tsv`.

- "true phonk indeed" by eSlashMachine, https://dollchan.net/btb/res/6946.html#11537 (the library's code is a close
  but not identical version of the formula kept here)
- "difficult level" by computerman113, https://www.reddit.com/r/bytebeat/comments/1hfy2uj/difficult_level/
- "Changsha wip" by D3nschøt, https://www.reddit.com/r/bytebeat/comments/1df4gxo/idk/
- "a new adventure" by Demari Smith, https://dollchan.net/btb/res/3.html#5895
- "generator" by eSlashMachine, https://dollchan.net/btb/res/6946.html#14913
- "dystopia" by eSlashMachine, https://dollchan.net/btb/res/6946.html#12004

```
MIT License

Copyright (c) 2021 SthephanShi

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## re-psn desktop app (optional installer task)

The installer has an unticked task for the [re-psn](https://github.com/undecuple/re-psn) desktop app, a pre-release. When it
is ticked, setup copies the folder `re-psn-app-v0.9.0-windows-amd64` from the public re-psn v0.9.0 release, unchanged but for the window program, into the
install folder: `re-psn-app.exe`, `re-psn-host.exe`, `README.md`, and the app's own `LICENSE` and
`NOTICE.md`, which stay in that folder, and a `THIRD-PARTY-LICENSES.txt` that this project adds (below). The release's native window program is not shipped; the app opens your web browser. Nothing of it is in this tree: whoever builds the installer downloads the release zip
with `installer/tools/fetch_toolchain.py` (version and SHA-256 in `installer/tools/toolchain.json`, checked against the
release's `SHA256SUMS` too). The app is licensed under the MIT licence:

```
Copyright (c) 2026 sp00nz
Copyright (c) 2026 undecuple
```

The full text is the app folder's `LICENSE`. The app's `NOTICE.md` lists its third-party parts: the Go standard library
(BSD-3-Clause); [Bubble Tea](https://github.com/charmbracelet/bubbletea), [Lip Gloss](https://github.com/charmbracelet/lipgloss),
`github.com/charmbracelet/x/term` and the modules they need (MIT, Charmbracelet, Inc. and the authors of each module);
`golang.org/x/sync` and `golang.org/x/sys` (BSD-3-Clause, Go); and the light colours of
[gruvbox-material](https://github.com/sainnhe/gruvbox-material) (MIT, Copyright (c) 2020 sainnhe). re-psn began as a fork of
[psnr](https://github.com/sp00nznet/psnr) by sp00nz (MIT).

The app's own `NOTICE.md` in v0.9.0 names the server (`server/`) and the desktop app (`app/`) and their main dependencies; it does not
carry the licence texts of the Go modules linked into the Windows programs `re-psn-app.exe` and `re-psn-host.exe`, and the release archive has none. This project
adds them: the folder also holds `THIRD-PARTY-LICENSES.txt`, made at build time from the texts named in
[`installer/tools/repsn_app_licenses.json`](installer/tools/repsn_app_licenses.json) (each checked against its SHA-256; each text is the module's own
licence file at the exact version in re-psn's `app/go.mod` and `go.sum` at the v0.9.0 tag). The modules, versions and copyright lines:

| Module | Version | Licence | Copyright line |
|---|---|---|---|
| `Go standard library` | `go1.27.4` | BSD-3-Clause | Copyright 2009 The Go Authors. |
| `golang.org/x/sync` | `v0.22.0` | BSD-3-Clause | Copyright 2009 The Go Authors. |
| `golang.org/x/sys` | `v0.47.0` | BSD-3-Clause | Copyright 2009 The Go Authors. |
| `charm.land/bubbletea/v2` | `v2.1.1` | MIT | Copyright (c) 2020-2026 Charmbracelet, Inc. |
| `charm.land/lipgloss/v2` | `v2.0.6` | MIT | Copyright (c) 2021-2026 Charmbracelet, Inc. |
| `github.com/charmbracelet/x/term` | `v0.2.2` | MIT | Copyright (c) 2023 Charmbracelet, Inc. |
| `github.com/charmbracelet/x/ansi` | `v0.11.9` | MIT | Copyright (c) 2023 Charmbracelet, Inc. |
| `github.com/charmbracelet/x/windows` | `v0.2.2` | MIT | Copyright (c) 2023 Charmbracelet, Inc. |
| `github.com/charmbracelet/colorprofile` | `v0.4.3` | MIT | Copyright (c) 2020-2024 Charmbracelet, Inc |
| `github.com/charmbracelet/ultraviolet` | `v0.0.0-20261008173134-6b8d4baf91b4` | MIT | Copyright (c) 2025 Charmbracelet, Inc |
| `github.com/clipperhouse/displaywidth` | `v0.11.0` | MIT | Copyright (c) 2025 Matt Sherman |
| `github.com/clipperhouse/uax29/v2` | `v2.7.0` | MIT | Copyright (c) 2020 Matt Sherman |
| `github.com/lucasb-eyer/go-colorful` | `v1.4.1` | MIT | Copyright (c) 2013 Lucas Beyer |
| `github.com/mattn/go-runewidth` | `v0.0.30` | MIT | Copyright (c) 2016 Yasuhiro Matsumoto |
| `github.com/muesli/cancelreader` | `v0.2.2` | MIT | Copyright (c) 2022 Erik Geiser and Christian Muehlhaeuser |
| `github.com/rivo/uniseg` | `v0.4.7` | MIT | Copyright (c) 2019 Oliver Kuederle |
| `github.com/xo/terminfo` | `v1.0.0` | MIT | Copyright (c) 2016 Anmol Sethi |

Modules that only tests use (`go-udiff`, `bitset`, `x/exp/golden`, `stringish`, `golang.org/x/exp`) are not linked into the programs, and `charmbracelet/x/termios` (Unix only) is not linked into the Windows builds; they have no entry.

## Other software used at build or run time (not included)

- For a hand build: Visual Studio Build Tools, clang-cl, CMake and Ninja. The installer uses the bundled llvm-mingw, CMake and Ninja instead.
- FFmpeg libraries are loaded at run time from DLLs that you supply. None are included here.

## Images and fonts of the README and the site

The PNG files in `assets/` (banner, social preview, logo mark, divider strips and the README's menu buttons) are the
project's own drawings, made by `tools/site/repo_art.py`: the project mark is the project's own ASCII art of a star with
a J (`tools/site/ascii_logo.py`), its characters drawn as strokes, and the lettering is the project's own polygon
alphabet (`tools/site/wordmark.py`), not a font. The installer images and icons are drawn the same way. Nothing in them comes from the game. The fan site uses the visitor's
system fonts and loads no font, script or image from anywhere else.

## The game

J-Stars Victory VS+ and its data belong to their rights holders. No game data is included here.
This is an unofficial fan project; trademarks belong to their owners.

## Licence of this repository's own files

MIT, see `LICENSE`. This matches the licence of ps3recomp.
