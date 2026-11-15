# Setup, build and run

How the game gets from your disc to a running port. The installer does all of it; this page names the pieces so you can follow or repeat
a step by hand. The earlier v0.15 recipe (the maintainer's working folder, `jstars\port\build.bat`, `run_test.ps1`) is in the git tag
`legacy-v15`, in `docs/notes/SETUP.md` there.

## What you need

- Your own game disc folder (the one that holds `PS3_GAME`) and your own decrypted `EBOOT.elf` (how to get it: RPCS3 and its documentation).
- The installer (`jsp-setup.exe`) or the portable zip. Both carry the compiler, CMake, Ninja and a Python; nothing is downloaded and no
  Visual Studio is needed. Steps for players: [../INSTALL.md](../INSTALL.md); problems: [../TROUBLESHOOTING.md](../TROUBLESHOOTING.md).

## What the build step does

`installer/tools/firstrun.py` runs after setup has copied the files. Checks come first (named codes, for example a wrong disc folder or an
EBOOT of another version), then resumable steps whose markers hold a fingerprint of their input:

| Step | What |
|---|---|
| `checks` | The disc folder, the EBOOT (US BLUS31519 or EU BLES02136), the toolchain, the movie decoder DLLs |
| `lift` | `ppu_loader` and `ppu_lifter` of the ps3recomp v0.19.0 tools turn the EBOOT into C++ (`build\recompiled`); the code end differs per disc (`0x734330` US, `0x734528` EU) |
| `spu` | `installer/tools/lift_jobs.py` finds the JOBCRT SPU jobs in the EBOOT, wraps and lifts them, and writes `spu_workloads.c` |
| `build` | CMake and Ninja with the bundled llvm-mingw compile the port (`installer/port/jsp`) on the exported, patched toolkit; the first build takes several minutes |
| `place` | Copies the built game and its helper files into the game folder, writes `paths.txt` for the launcher and builds `jsp-settings.exe` if it is missing or older than its source (a hash of `tools/jsp-settings.cs` is kept in `jsp-settings.exe.src-sha256`) |

A log of each run is `logs\firstrun-<time>.log`. Run one part by hand with `--until checks|lift|spu|build|all` and repeat one with
`--force STEP` (usage in [../BUILDING.md](../BUILDING.md)). `build-again.cmd` in the install folder repeats the build.

## Running

`jsp-launcher.exe` starts the game. It reads `paths.txt` (written by the build step) and `play_settings.txt` (written by the settings
window, [LAUNCHER.md](LAUNCHER.md)) and turns each line into an environment variable of the game:

| Variable | What |
|---|---|
| `PS3_VFS_ROOT` | The disc folder (the one that holds `PS3_GAME`) |
| `PS3_HDD0_ROOT` | The host folder for the game's `/dev_hdd0` (saves, game data install) |
| `PS3_DEV_FLASH` | The `dev_flash` folder (optional) |
| `PS3_TITLE` | The window title |
| `PS3_VERBOSE` | `0` normally; `1` logs a lot and runs a match at less than half speed (FINDINGS section 30) |

The first start installs the game data into the HDD folder, which takes about five minutes. Later starts reuse it. Antivirus programs scan
each new build, so the first start after a rebuild is slow. The other settings (resolution, window, input, graphics) are in
[PORT_SETTINGS.md](PORT_SETTINGS.md) and [LAUNCHER.md](LAUNCHER.md).
