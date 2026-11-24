# Instructions for AI coding agents

Read this before changing anything in this repository. It applies to every automated contributor (automated
coding tools of any kind) and to the people who run them. The rules for people are in
[CONTRIBUTING.md](CONTRIBUTING.md); this file adds the details for automated contributors.

## How a change reaches `main`

`main` only changes through a reviewed pull request.

1. **Never push to `main`** and never merge into it yourself. Work on a branch named for the topic
   (for example `fix/menu-exit` or `notes/shadow-maps`).
2. **One topic per pull request.** Split a large upload into several pull requests (code, notes, patches), each
   one easy to read. As a guide, keep a pull request under about 400 changed lines, or say in its description why
   it has to be larger.
3. **Run the checks before you push:** `python3 tools/ci/repo_checks.py --commits origin/main..HEAD`
   (Python 3 and git only). CI runs these and more on every pull request (`.github/workflows/ci.yml`: the features page, the installer and release
   scripts, shell and PowerShell parsing, the launcher build, the toolkit patch series and a syntax check of the port's sources); run the relevant ones too when you touch those areas.
4. **Open the pull request and wait for CI.** All checks must be green. If one fails, push a fix to the same
   branch; do not open a new pull request for the same change.
5. **A maintainer merges.** Ask for a review and stop. Do not merge your own pull request, even when you have
   the rights to, and do not ask to merge before CI has finished.
6. If `main` moved and the pull request conflicts, rebase the branch on `main` and push it again. A pull request
   with no conflict does not need a rebase.

## Where things are

Netcode, menus and other in-game UI, the launcher, docs and tools each go in a pull request of their own. Every
pull request adds its own line to `CHANGELOG.md` under `## [Unreleased]`. The full folder list is
[docs/LAYOUT.md](docs/LAYOUT.md); file by file, with entry points and line numbers, see
[docs/code-map/README.md](docs/code-map/README.md). The last column names the docs that describe each area; changes to
them go in a docs pull request of their own, like any other docs change.

| Area | Path | What lives there | Check or test to run | Docs that describe it |
|---|---|---|---|---|
| Game glue | `installer/port/jsp/` | the port's C++ around the recompiled game: netcode (`jsp_net_*`, `jsp_session_*`, `jsp_snapshot*`, `jsp_stepper*`, the vendored `gekkonet/`), menus and in-game UI (`jsp_port_menu*`, `feat_*`), `main.cpp`, `CMakeLists.txt` | `tools/ci/port_syntax_check.py` (needs the exported toolkit), the matching rows of `tools/ci/run_host_tests.py` | `docs/code-map/layers.md` (one row per file), `docs/code-map/netcode.md` for netcode, `docs/ROLLBACK.md` or `docs/ONLINE.md` when players see it |
| Toolkit patches | `installer/port/patches/` | the numbered patch series on the pinned toolkit, `INDEX.md`, `upstream.tsv` | `installer/tools/patch_index.py --check`, `tools/ci/check_patch_series.py` | `INDEX.md` (run `patch_index.py`), `docs/code-map/patches.md` |
| Setup and release | `installer/` | the installer, `jsp-launcher.exe` source (`installer/launcher/`), the first-run build, the release version | `tools/release.py --check`, `installer/tools/firstrun_selftest.py --units` | `installer/README.md`, `docs/code-map/build-and-test.md` |
| Host tests and helpers | `installer/tools/` | payload, first run, art, and the host tests (`*_test.*`) | `python3 tools/ci/run_host_tests.py --check-coverage`; list a new test in `tools/ci/host-tests.txt` | `docs/code-map/build-and-test.md` |
| Repository tools | `tools/`, `tools/ci/` | CI scripts (`repo_checks.py`, `run_host_tests.py`, `host-tests.txt`), the release and features tools | `tools/ci/test_*.py`, `tools/test_*.py` | `docs/code-map/build-and-test.md` |
| Settings window | `launcher/` | `JStarsLauncher.cs`, the settings window: CI's `launcher` job builds `J-Stars Launcher.exe` from it, and the installer builds `jsp-settings.exe` from the same source | CI's `launcher` job; `installer/tools/settings_test.sh` compares its keys with the game's | `docs/notes/LAUNCHER.md` |
| Docs | `docs/` | player pages (`ONLINE.md`, `ROLLBACK.md`, ...), research notes in `docs/notes/`, the code map in `docs/code-map/`, the features page | `python3 tools/features.py --check` | `docs/features.tsv`, then `python3 tools/features.py` (it writes `docs/FEATURES.md`) |
| CI | `.github/workflows/` | `ci.yml` and the release, installer, site and contributors workflows | pin every action by commit SHA, as the existing steps do | `docs/code-map/build-and-test.md` |

## What the pull request says

- What changed and why, in a few lines.
- What was built or run, and on what (Windows version, GPU, the scene that was checked). If nothing was built or
  run, say so plainly.
- No tool-generated footers, session links or attribution comments in the description.

## Commits

- Short, plain messages: what changed and why.
- Author and committer use the GitHub noreply address (`<id>+<login>@users.noreply.github.com`). Check with
  `git log --format='%an <%ae> / %cn <%ce>' origin/main..HEAD`.
- No co-author, session or tool-credit trailers.

## Rules for everyone

- **No game material.** No ISO, EBOOT, decrypted ELF, firmware, saves, recompiled game code, captures or
  screenshots of the game, and no lifted game code in the context lines of a patch. Numbers, names and
  structure layouts recovered from the game are fine.
- **No personal details.** No user names, home folders, machine names or e-mail addresses; use variables or
  relative paths.
- **AI help** is stated in general terms in public text ("developed with AI agent assistance"). This file is the
  only place that names agent tools.
- The `.gitignore` is an allow-list: a new top-level file or folder needs an allow line, or it never reaches git.
  Check with `git ls-files` after you commit.
