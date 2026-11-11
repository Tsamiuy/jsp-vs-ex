# Layout

| Path | What |
|---|---|
| `jstars/port/` | The default port, on ps3recomp v0.15.0: `main.cpp`, HLE stubs and game-specific handlers, CMake and build scripts. `recompiled/`, `spu/` and `build/` are generated locally and not stored |
| `jstars/*.ps1`, `Play J-Stars.bat` | Run, test and play scripts ([SCRIPTS.md](SCRIPTS.md)) |
| `launcher/` | Source of the launcher window (`JStarsLauncher.cs`) and `build_launcher.bat`; the built exe is not stored |
| `installer/` | Windows installer (Inno Setup script, `jsp-launcher.exe` source, seven patches (`installer/patches/`) for the bundled compiler setup and the toolkit, and `installer/tools/` with the payload, portable zip, toolchain fetch, build-step and art scripts); [installer/README.md](../installer/README.md) |
| `installer/port/` | The second port, on ps3recomp v0.19.0, which the installer builds with `--tree v18`: game code in `jsp/`, toolkit patches in `patches/`, scripted test routes in `test/`. `installer/tools/export_toolkit.py` exports the v0.19.0 commit and applies the patches |
| `jstars/spu_jobs/*.py` | SPU job lifting pipeline |
| `docs/notes/` | The notes (STATUS, FINDINGS, SETUP, ...). Start with `docs/notes/README.md` |
| `third_party/ps3recomp` | The ps3recomp toolkit as a submodule, pinned to `v0.15.0` (the default port); only the pointer is stored here ([BUILDING.md](BUILDING.md#toolkit)) |
| `toolkit/`, `tools/toolkit/` | `ps3recomp-local.patch`, every change the port makes to the toolkit, and `apply_toolkit.py`, which builds the patched toolkit folder from the submodule |
| `jstars-notes/` | Patches with READMEs (`patches/`), analysis tools (`tools/`), and menu, perf, reference and sample material |
| `docs/balance/`, `docs/character-details/` | Balance notes in progress: how combat numbers are stored and read, shared rules, and one page per character (damage, Stamina, skills). Start with `docs/balance/README.md` |
| `docs/` | Features, building, scripts and this layout; the notes are in `docs/notes/` |
| `tools/boost/` | Performance "boost" helper scripts (they change the Windows power plan; read `tools/boost/README.txt` first) |
| `.github/workflows/ci.yml`, `tools/ci/` | Checks run on every pull request; run `python3 tools/ci/repo_checks.py` before you push |
| `assets/`, `tools/site/` | Banner, social preview and icon (our own ASCII-style art); the fan-site generator, which builds a static page from `README.md` and `NOTICE.md` (`python3 tools/site/site.py --check`) |
| `.github/workflows/pages.yml` | Publishes that page with GitHub Pages when run by hand (Actions > pages) |
| `CHANGELOG.md`, `tools/release.py` | Release notes and the tool that keeps `installer/VERSION`, the CMake project versions and the changelog in step (`--check`, `bump`, `notes`); the workflows `prepare-release.yml` and `release.yml` use it. See [CONTRIBUTING.md](../CONTRIBUTING.md) |
| `LICENSE`, `NOTICE.md` | MIT licence; third-party software and licences |
| `SECURITY.md`, `CONTRIBUTING.md` | How to report a problem and how to send a change |
| `AGENTS.md` | Rules for AI coding agents: branches, pull requests, CI, who merges |

## Branches

Only `main`. The default port's toolkit changes are one patch, `toolkit/ps3recomp-local.patch`; the v0.19.0 port carries its toolkit patches in `installer/port/patches/` and `installer/patches/`; the files in `jstars-notes/patches` are the record of single experiments.
