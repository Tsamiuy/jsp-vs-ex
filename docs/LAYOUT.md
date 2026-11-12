# Layout

## What sits at the root

| Path | For | Current or legacy |
|---|---|---|
| `README.md`, `LICENSE`, `NOTICE.md`, `CHANGELOG.md`, `CONTRIBUTING.md`, `CONTRIBUTORS.md`, `SECURITY.md`, `AGENTS.md`, `.recomp.json` | Everyone: start here, licences, release notes, how to send a change | current |
| `installer/` | Players and the build: Windows installer, the build-on-your-PC steps and the port on ps3recomp v0.19.0 (`installer/port/`) | current |
| `launcher/` | The launcher window source: `JStarsLauncher19.cs` (current) and `JStarsLauncher.cs` (v0.15 tree) | both |
| `docs/` | Players and contributors, see [docs/README.md](README.md) | current, plus `docs/archive/` (legacy) |
| `tools/` | CI checks (`tools/ci/`), release tool, fan-site generator, feature and contributor generators, `boost/` power-plan scripts | current |
| `assets/` | Banner, social preview and icon (generated art) | current |
| `third_party/ps3recomp` | The toolkit as a submodule | current (pinned v0.15.0; the v0.19.0 tree exports its own) |
| `jstars/` | The earlier port on ps3recomp v0.15.0 (`port/`, `spu_jobs/`, `play.ps1`, `run_test.ps1`, the double-click scripts `Play J-Stars.bat` and `Test Fight.bat`) and its toolkit patch (`toolkit/`). Also the run folder the `.bat` files and the launcher write to (`jstars\run`, `play_settings.txt`) | legacy (`--tree legacy`) |

## Moved on 2026-11-12

Nothing was deleted. The root loses two folders and `tools/` loses one; later the same day the two `.bat` files moved into `jstars/`.

| Old | New | Why |
|---|---|---|
| `jstars-notes/` (309 files: patches, analysis tools, menu, perf, reference, samples of the v0.15 work) | `docs/archive/jstars-notes/` | A historical knowledge base, the biggest folder at the root, read only by `docs/notes/`; now labelled as an archive under docs |
| `toolkit/` (`ps3recomp-local.patch`, `third_party_smaa/`, README) and `tools/toolkit/apply_toolkit.py` | `jstars/toolkit/` | They only serve the legacy v0.15 tree (`--tree legacy`), so they sit with it; one root folder fewer and no one-file `tools/toolkit/` |
| `Play J-Stars.bat`, `Test Fight.bat` | `jstars/Play J-Stars.bat`, `jstars/Test Fight.bat` | Double-click scripts for the legacy tree only (they run `jstars\play.ps1`); the installer does not ship them and the README does not name them, so they sit beside the script they start |

Kept where they are, on purpose: `jstars/` (its name is also the run folder written by the launcher, the `.bat` files and the game, and the legacy build recipe is keyed on it), `launcher/` and the installer files (in use by open work), `docs/notes/`, `docs/balance/`, `docs/character-details/`, `docs/FEATURES.md` and the other top-level docs (linked from README, the site, issues and `.recomp.json`), `assets/` (README banner), `tools/*.py` (named by the workflows).

| Path | What |
|---|---|
| `jstars/port/` | The default port, on ps3recomp v0.15.0: `main.cpp`, HLE stubs and game-specific handlers, CMake and build scripts. `recompiled/`, `spu/` and `build/` are generated locally and not stored |
| `jstars/*.ps1`, `jstars/*.bat` | Run, test and play scripts ([SCRIPTS.md](SCRIPTS.md)) |
| `launcher/` | Source of the launcher window (`JStarsLauncher.cs`) and `build_launcher.bat`; the built exe is not stored |
| `installer/` | Windows installer (Inno Setup script, `jsp-launcher.exe` source, seven patches (`installer/patches/`) for the bundled compiler setup and the toolkit, and `installer/tools/` with the payload, portable zip, toolchain fetch, build-step and art scripts); [installer/README.md](../installer/README.md) |
| `installer/port/` | The second port, on ps3recomp v0.19.0, which the installer builds by default (`--tree current`): game code in `jsp/`, toolkit patches in `patches/`, scripted test routes in `test/`. `installer/tools/export_toolkit.py` exports the v0.19.0 commit and applies the patches |
| `jstars/spu_jobs/*.py` | SPU job lifting pipeline |
| `docs/notes/` | The notes (STATUS, FINDINGS, SETUP, ...). Start with `docs/notes/README.md` |
| `third_party/ps3recomp` | The ps3recomp toolkit as a submodule, pinned to `v0.15.0` (the default port); only the pointer is stored here ([BUILDING.md](BUILDING.md#toolkit)) |
| `jstars/toolkit/` | `ps3recomp-local.patch`, every change the port makes to the toolkit, and `apply_toolkit.py`, which builds the patched toolkit folder from the submodule |
| `docs/archive/jstars-notes/` | Patches with READMEs (`patches/`), analysis tools (`tools/`), and menu, perf, reference and sample material |
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

Only `main`. The default port's toolkit changes are one patch, `jstars/toolkit/ps3recomp-local.patch`; the v0.19.0 port carries its toolkit patches in `installer/port/patches/` and `installer/patches/`; the files in `docs/archive/jstars-notes/patches` are the record of single experiments.
