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
   scripts, shell and PowerShell parsing, the launcher and toolkit builds); run the relevant ones too when you touch those areas.
4. **Open the pull request and wait for CI.** All checks must be green. If one fails, push a fix to the same
   branch; do not open a new pull request for the same change.
5. **A maintainer merges.** Ask for a review and stop. Do not merge your own pull request, even when you have
   the rights to, and do not ask to merge before CI has finished.
6. If `main` moved and the pull request conflicts, rebase the branch on `main` and push it again. A pull request
   with no conflict does not need a rebase.

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
