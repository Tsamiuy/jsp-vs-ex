# Building

The port is built on the player's PC by the installer's first-run step, from the player's own copy of the game. This page lists what
that needs and how to run the same steps by hand. The earlier v0.15 hand build (`jstars/port`) is gone from the tree; its last
buildable state is the git tag `legacy-v15`.

## Requirements

- Your own copy of the game (US disc BLUS31519, or EU BLES02136) and its decrypted `EBOOT.elf`: how to get the decrypted file is
  described by RPCS3 and its documentation.
- Windows x64. No Visual Studio: the compiler (llvm-mingw, with CMake and Ninja) and a Python come with the package.
- Optional: an RPCS3 installation, only to set `PS3_DEV_FLASH` to its `dev_flash` folder; the game reads nothing from it in testing.

## Steps

1. Install with `jsp-setup.exe`, or unpack the portable zip and run `run.cmd`. Both copy the port's sources, the toolchain and a Python
   to the install folder and then run `installer/tools/firstrun.py`: checks, PPU lift, SPU jobs, compile. A log of each run is written
   to `logs\firstrun-<time>.log`. The wizard steps are in [INSTALL.md](INSTALL.md), what can go wrong in [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
2. To build again without the wizard, run `build-again.cmd` in the install folder, or call the step directly:

   ```
   python firstrun.py --root APP --disc DISC_FOLDER --eboot EBOOT.elf --hdd0 DIR [--until checks|lift|spu|build|all] [--jobs N] [--force STEP]
   ```

   Finished steps are kept; `--force STEP` repeats one.

## Toolkit

`third_party/ps3recomp` is a submodule pinned to ps3recomp v0.19.0 and stays unchanged. `installer/tools/export_toolkit.py` exports that
commit with `git archive` and applies `installer/port/patches/*.patch` to it (needs Python 3 and Git). After a plain `git clone`, run
`git submodule update --init` once; GitHub's "Download ZIP" leaves `third_party/ps3recomp` empty, so use Git. The patches must keep their
bytes (`*.patch -text` in `.gitattributes`). The patches are listed in `installer/port/patches/INDEX.md`
(`python3 installer/tools/patch_index.py --check` verifies it).

## Launcher

The installer's shortcuts open the settings window (`jsp-settings.exe`, built from `launcher/JStarsLauncher.cs` with the C# compiler that
ships with Windows) and start the game through `jsp-launcher.exe` (`installer/launcher/launcher.c`). Settings live in `play_settings.txt`.
What the scripts change on your machine: [SCRIPTS.md](SCRIPTS.md); details: [notes/LAUNCHER.md](notes/LAUNCHER.md).

## Installer

How the installer, the portable zip and the payload are built, and the checks that run on the host: [installer/README.md](../installer/README.md).
