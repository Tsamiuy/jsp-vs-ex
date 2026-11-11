# Contributing

Thanks for looking. This is a small work-in-progress fan project. Keep changes small and easy to review.

## Rules

- No game files. That means no ISO, EBOOT, decrypted ELF, firmware, recompiled game code, captures or
  screenshots of the game.
- No keys, tokens or personal data. Use variables or relative paths instead of your own user folder.
- Fixes to ps3recomp itself belong in the ps3recomp repository. See `NOTICE.md`.
- If you used AI help, say so in general terms in the pull request. Naming a tool is not needed.

## Sending a change

1. Open an issue first for anything bigger than a small fix or a wording change.
2. Describe what the change does and why.
3. Add a short note on how you tested it: what you built or ran, and on which Windows version.
   If you did not test it, say so.
4. One topic per pull request. Changes reach `main` only through a pull request that a maintainer has
   read and merged, after CI is green. AI coding agents follow [AGENTS.md](AGENTS.md).

## Notes and patches

The notes in `docs/notes/` are a working log. When you add to them, state what was measured and what is
only a guess, and keep game data out of them.

## Features page

`docs/FEATURES.md` is generated: edit `docs/features.tsv` (its format is at the top of `tools/features.py`), then run
`python3 tools/features.py`. A ROADMAP line that covers a row links to it as `FEATURES.md#<id>` and carries the
marker for its status; `python3 tools/features.py --check` (also run by CI) reports any mismatch.

## Releases

The version number lives in `installer/VERSION`; the two CMake `project(... VERSION ...)` lines follow it (those folders are
built on the player's PC and cannot read a file outside themselves), and `CHANGELOG.md` has the notes. Add a line under
`## [Unreleased]` in `CHANGELOG.md` with each change a player would notice. `python3 tools/release.py --check` (also run by
CI) reports any version that disagrees. To release, either way:

- On GitHub: run the `prepare-release` workflow (Actions tab) with the new version (`X.Y.Z`, or `major`, `minor`, `patch`).
  It opens a `release/vX.Y.Z` pull request whose body is the notes. CI does not start on a pull request opened by a
  workflow: close and reopen it, then merge.
- Locally: `python3 tools/release.py bump X.Y.Z` (or `major|minor|patch`) writes the version everywhere and turns Unreleased
  into `## [X.Y.Z] - date`; commit it and open a pull request as usual.

Merging a change to `installer/VERSION` into `main` starts the `release` workflow: it tags `vX.Y.Z`, builds the installer
and creates a **draft** GitHub release with the files and the notes (`python3 tools/release.py notes X.Y.Z`). Look at the
draft and publish it by hand; a 0.x version is created as a pre-release. If a build fails after tagging, rerun the release workflow: a tag at the same commit with no release resumes
the build. Existing drafts and published releases are left alone, and tags at another commit are never moved.
