# Notices

## ps3recomp

This project is built on [ps3recomp](https://github.com/sp00nznet/ps3recomp) by Ned Heller (sp00nznet) and
contributors, licensed under the MIT licence. Parts of `jstars/port/` started as copies of the ps3recomp project
template (for example `main.cpp`, `stubs.cpp`, `CMakeLists.txt` and `config.toml`). The patches in
`jstars-notes/patches/` are written against ps3recomp source files. Keep ps3recomp's copyright and licence
notice with any of these files that you copy.

The toolkit itself is not part of this tree: `third_party/ps3recomp` is a submodule that points at ps3recomp's
`v0.15.0` commit, and its files come from the upstream repository. Changes to ps3recomp are kept only as patches:
`toolkit/ps3recomp-local.patch` (the set the port builds with) and the single-experiment patches under
`jstars-notes/patches`.

## SMAA

`toolkit/third_party_smaa/` holds four files from SMAA (Enhanced Subpixel Morphological Antialiasing) by Jorge
Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro and Diego Gutierrez, <https://github.com/iryoku/smaa>,
licensed under the MIT licence. `AreaTex.h`, `SearchTex.h` and `SMAA.hlsl` are the upstream files with their
copyright and licence header (it names the authors), unchanged. `SMAA_hlsl.inc` is generated from `SMAA.hlsl` and
carries a one-line attribution. `tools/toolkit/apply_toolkit.py` copies them into the patched toolkit, where the
patched video library uses them for the "SMAA" edge smoothing option. Keep the header with any copy of these files.

## rpcs3_locate

`installer/tools/rpcs3_locate.py` and its test `installer/tools/test_rpcs3_locate.py` are a helper shared between several unofficial ports of PS3 games,
among them undecuple's Project Pyrrhus (see below), under the same MIT licence, vendored byte for byte unchanged from the shared copy of 2026-11-03 (sha256 of the module
5f98c82632f1e8d6837171080afd62057db2431a1153554f429678d5c0003703). It only reads an RPCS3 install's configuration and game folders; its one write is the
save copy that the installer asks the player to confirm. `installer/tools/rpcs3_wizard.py` is this repository's glue around it.

## Project Pyrrhus and Project Helghast

Parts of the installer and the tools started from two other unofficial PS3 ports by undecuple, both under the MIT licence
(Copyright (c) 2026 undecuple): [Project Pyrrhus](https://github.com/undecuple/project-pyrrhus) (Killzone 2) and
[Project Helghast](https://github.com/undecuple/project-helghast) (Killzone 3), from which Project Pyrrhus's installer was
made.

- From Project Pyrrhus's installer: the layout of `installer/jsp.iss`, `installer/launcher/launcher.c`,
  `installer/tools/firstrun.py`, `assemble_payload.py`, `assemble_portable.py`, `fetch_toolchain.py`, `installer_art.py`
  and `llvm-mingw.cmake`. The game-specific parts are this repository's.
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
tree); the ps3recomp toolkit with `toolkit/ps3recomp-local.patch`
applied (MIT, see above; SMAA as above); and the licence texts of this repository. The wizard images are drawn at build
time in IBM Plex Mono Bold (IBM Corp., SIL Open Font License 1.1; the font file is not included here and the images contain
only the rendered text). The Port Settings menu labels and glyphs are drawn at build time in Fira Sans Heavy Italic 4.301
(bBoxType/FiraSans, SIL Open Font License 1.1); the font file is not included here or in the package, and the art contains
only the rendered text.

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

## Other software used at build or run time (not included)

- For a hand build: Visual Studio Build Tools, clang-cl, CMake and Ninja. The installer uses the bundled llvm-mingw, CMake and Ninja instead.
- FFmpeg libraries are loaded at run time from DLLs that you supply. None are included here.

## Fonts in the images

The PNG files in `assets/` are set in IBM Plex Mono Bold (copyright IBM Corp., SIL Open Font License 1.1). The font
files are not included here; the images contain only the rendered text. The web page uses the system's monospace fonts.

## The game

J-Stars Victory VS+ and its data belong to their rights holders. No game data is included here.
This is an unofficial fan project; trademarks belong to their owners.

## Licence of this repository's own files

MIT, see `LICENSE`. This matches the licence of ps3recomp.
