# Layout

What every folder and top-level file is. There is one port in the tree: `installer/port/`, on ps3recomp v0.19.0. The earlier port on
ps3recomp v0.15.0 (`jstars/`) was removed from the tree in #173 (issue #152); it is kept in the git tag `legacy-v15`
(`git checkout legacy-v15`, then `git submodule update --init`).

| Path | What |
|---|---|
| `README.md`, `LICENSE`, `NOTICE.md`, `CHANGELOG.md`, `CONTRIBUTING.md`, `CONTRIBUTORS.md`, `KNOWN_ISSUES.md`, `SECURITY.md`, `AGENTS.md`, `CITATION.cff`, `.recomp.json` | Start here: what the project is, licences and third-party notices, release notes, how to send a change, known problems, rules for AI coding agents |
| `installer/` | The Windows installer and the build-on-your-PC steps ([installer/README.md](../installer/README.md)) |
| `installer/port/jsp/` | The game glue: `main.cpp`, HLE stubs and game-specific handlers (`jsp_*.cpp`), the in-game Port Settings pages, CMake. `recompiled/`, `spu/` and `build/` are made on the player's PC and never stored |
| `installer/port/patches/` | The toolkit patches (the numbered series on ps3recomp v0.19.0) and `INDEX.md`, which lists them; `installer/tools/export_toolkit.py` exports the pinned v0.19.0 commit and applies them |
| `installer/port/test/` | Scripted pad routes for testing the installed game without a controller |
| `installer/tools/` | Payload and portable zip, toolchain fetch, first-run build steps, art and host tests |
| `launcher/` | The launcher window source: `JStarsLauncher.cs` is the installer's settings window (`jsp-settings.exe`), `JStarsLauncher19.cs` is the development launcher (not shipped) with `build_launcher19.bat` |
| `docs/` | Players and contributors, see [README.md](README.md). `docs/notes/` are the research notes, `docs/balance/` and `docs/character-details/` the balance notes, `docs/archive/jstars-notes/` the archive of the v0.15 work (patches, analysis tools, measurements) |
| `docs/code-map/` | The code map for maintainers: layers and entry points, netcode, toolkit patches to glue, build and CI ([code-map/README.md](code-map/README.md)) |
| `tools/` | CI checks (`tools/ci/`), the release tool (`release.py`), the fan site and the drawn art (`site/`: `site.py` builds the site, `repo_art.py` draws the banner, social preview, dividers and README buttons, `badges.py` writes the README's badge and button blocks, `check_site.py` checks the site; palette and button list in `data/`), the feature and contributor generators, and `boost/` (power-plan scripts: read `tools/boost/README.txt` first) |
| `assets/` | Banner, social preview and icon (our own generated art) |
| `third_party/ps3recomp` | The ps3recomp toolkit as a submodule, pinned to the v0.19.0 commit; only the pointer is stored here |
| `.github/workflows/` | `ci.yml` (checks on every pull request: `python3 tools/ci/repo_checks.py` before you push), `installer.yml`, `release.yml`, `prepare-release.yml`, `release-notify.yml`, `pages.yml`, `contributors.yml` |

## Branches

Only `main`. The toolkit changes of the port are the patches in `installer/port/patches/`; the files in `docs/archive/jstars-notes/patches`
are the record of single experiments from the v0.15 work. The earlier v0.15 port is in the tag `legacy-v15`.
