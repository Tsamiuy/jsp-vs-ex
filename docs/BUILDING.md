# Building

The full steps are in `docs/notes/SETUP.md`; this page lists what you need and the order.

The hand build below is the earlier v15 tree (`jstars/port`, toolkit v0.15.0). The installer builds the current port (`installer/port`, today ps3recomp v0.19.0, `--tree current`) by default and the v15 tree with `--tree legacy`.

## Requirements

- Your own copy of the game (the notes use the US disc, BLUS31519).
- For a hand build, a Windows x64 build environment as `docs/notes/SETUP.md` describes: Visual Studio 2022 Build Tools
  with the C++ workload and clang-cl, CMake and Ninja, and Python 3 with the ps3recomp requirements. The installer
  (below) does not need these: it carries its own compiler.
- The ps3recomp toolkit: a submodule at `third_party/ps3recomp`, pinned to `v0.15.0`, plus this repository's
  changes to it in `jstars/toolkit/ps3recomp-local.patch` (see [Toolkit](#toolkit) below). The port has not yet been
  built from that folder on a clean machine; the notes build from a local checkout with the same changes.
- Optional: an RPCS3 installation, only if you want to set `PS3_DEV_FLASH` to its `dev_flash` folder; the game reads nothing from
  `dev_flash` in testing.

## Steps

1. The EBOOT is taken from your own copy of the game; for how to obtain the decrypted file, see RPCS3 and its
   documentation.
2. Lift and build as `docs/notes/SETUP.md` describes: `ppu_loader.py`, `ppu_lifter.py`, then `jstars/port/build.bat`.
   `recompiled/`, `spu/` and `build/` under `jstars/port/` are generated locally and never stored here.

## Toolkit

The submodule stays unchanged. `jstars/toolkit/apply_toolkit.py` copies its pinned commit to `build\ps3recomp` and
applies `jstars/toolkit/ps3recomp-local.patch` there. It needs Python 3.8 or newer and Git, nothing else.

```
git clone --recurse-submodules https://github.com/Tsamiuy/jsp-vs-ex.git
cd jsp-vs-ex
python jstars\toolkit\apply_toolkit.py
```

Then build the port with `-DPS3RECOMP_DIR=<checkout>\build\ps3recomp` (the script prints the path). Notes:

- After a plain `git clone`, run `git submodule update --init` once. GitHub's "Download ZIP" leaves
  `third_party\ps3recomp` empty; use Git.
- `--check` only checks that the patch applies and writes nothing (the CI job `toolkit` runs it on Windows).
  `--force` replaces an existing `build\ps3recomp`; `--out DIR` writes somewhere else.
- Line endings: the patch must keep its bytes (`*.patch -text` in `.gitattributes`). The script reads the toolkit with
  line-ending conversion off, so Git for Windows' default setting does not matter. If the patch file itself was
  converted, the script says so and how to restore it.

## Launcher

`launcher/build_launcher.bat` builds `J-Stars Launcher.exe` with the C# compiler that ships with Windows (.NET
Framework 4, nothing to install). The launcher does the same steps as `jstars/play.ps1`: it turns CPU boost on,
mounts the disc image read-only, starts the game, and when the game exits turns boost off again (tick-box) and
unmounts. Settings live in `jstars/play_settings.txt` (resolution scale, fullscreen, window size, input mode,
key and button bindings, fight HUD size). Details are in `docs/notes/LAUNCHER.md`. What the scripts change on your
machine: [SCRIPTS.md](SCRIPTS.md).

## Installer

An installer that runs the steps above on a player's PC (disc folder and decrypted EBOOT.elf chosen in the wizard; a
`dev_flash` folder is optional and only accepted as the silent `/DEVFLASH=` argument) is built with Inno Setup on Windows: see [installer/README.md](../installer/README.md). It carries a pinned llvm-mingw,
CMake and Ninja, so the player needs no Visual Studio. The same payload is also packed as a portable zip. Its build step has
not been run on a real Windows PC yet.
