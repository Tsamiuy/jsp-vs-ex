#!/usr/bin/env python3
"""Write installer/port/patches/*.patch from a toolkit branch with one commit per patch (the patches' only source).

    python installer/tools/series_export.py --toolkit DIR --branch fix [--base v0.19.0] [--check] [--no-index]

Each commit between the pinned tag and the branch tip becomes one patch: its subject is the file name ("2042-d3d12-..."),
the rest of its message is the patch's text, then the diff. Patches whose numbers are no longer in the branch are listed,
not deleted (remove them with git rm). --check compares without writing and exits 1 on a difference. Afterwards INDEX.md
is regenerated (patch_index.py) unless --no-index. Lines with a commit trailer, or with a word from PATCH_BANNED_WORDS
(names kept out of this file), are refused, so the text is clean before it is ever exported.
Needs Git on PATH; standard library only.
"""
import argparse
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
PATCHES = ROOT / "installer" / "port" / "patches"
# Commit trailers are always refused. Names (people, private projects, tools) are not listed in this public file:
# put them, comma separated, in the environment variable PATCH_BANNED_WORDS on the machine that exports.
_extra = [w.strip() for w in os.environ.get("PATCH_BANNED_WORDS", "").split(",") if w.strip()]
BANNED = re.compile(r"\b(" + "|".join([re.escape(w) for w in _extra] + ["Co-Authored-By"]) + r")\b", re.I)


def git(repo, *a):
    return subprocess.run(["git", "-C", str(repo), "-c", "core.autocrlf=false", *a], capture_output=True, text=True,
                          encoding="utf-8", check=True).stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--toolkit", required=True)
    ap.add_argument("--branch", required=True)
    ap.add_argument("--base", default="v0.19.0")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--no-index", action="store_true")
    a = ap.parse_args()
    rows = git(a.toolkit, "log", "--reverse", "--format=%H", f"{a.base}..{a.branch}").split()
    seen, bad, diff_files = set(), [], []
    for sha in rows:
        msg = git(a.toolkit, "log", "-1", "--format=%B", sha)
        subj, _, body = msg.partition("\n")
        subj = subj.strip()
        if not re.match(r"^\d{4}-[a-z0-9-]+$", subj):
            print(f"skipped {sha[:10]} ({subj!r}): the subject is not a patch name")
            continue
        for ln in msg.splitlines():
            if BANNED.search(ln):
                bad.append(f"{subj}: {ln.strip()[:100]}")
        diff = git(a.toolkit, "diff", "--no-color", f"{sha}^", sha)
        text = body.strip("\n") + "\n\n" + diff
        fn = PATCHES / f"{subj}.patch"
        seen.add(fn.name)
        old = fn.read_text(encoding="utf-8") if fn.exists() else None
        if old != text:
            diff_files.append(fn.name)
            if not a.check:
                fn.write_text(text, encoding="utf-8", newline="\n")
    if bad:
        print("refused, private words in commit messages:\n  " + "\n  ".join(bad))
        return 2
    gone = sorted(p.name for p in PATCHES.glob("*.patch") if p.name not in seen)
    print(f"{len(seen)} patches from {a.branch}; {'differ' if a.check else 'written'}: {len(diff_files)}"
          + (f" ({', '.join(diff_files[:8])}{' ...' if len(diff_files) > 8 else ''})" if diff_files else ""))
    if gone:
        print("not in the branch (git rm them if they were dropped): " + ", ".join(gone))
    if not a.check and not a.no_index:
        subprocess.run([sys.executable, str(ROOT / "installer" / "tools" / "patch_index.py")], check=True)
    return 1 if (a.check and (diff_files or gone)) else 0


if __name__ == "__main__":
    sys.exit(main())
