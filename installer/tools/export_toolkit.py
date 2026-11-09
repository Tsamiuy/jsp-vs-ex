#!/usr/bin/env python3
"""Export the pinned ps3recomp v0.19.0 toolkit and apply installer/port/patches to it (run when the installer is built).

    python installer/tools/export_toolkit.py --out DIR [--repo GIT_DIR] [--no-fetch]

The toolkit is MIT-licensed and stays upstream: this script takes `git archive` of the pinned commit (line-ending conversion off, so a
Windows checkout with core.autocrlf on gets the bytes the patches were made against) from a clone of
https://github.com/sp00nznet/ps3recomp, and applies installer/port/patches/*.patch in name order. The default repository is the submodule
third_party/ps3recomp; it may be at another commit (the v0.15 tree's pin), in which case the v0.19.0 tag is fetched into it first
(this needs the network once, on the machine that builds the installer, never on the player's PC). The result goes into the payload.

Needs Python 3.8+ and Git on PATH; standard library only.
"""
import argparse
import io
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
TAG = "v0.19.0"
PIN = "ae92407bb1d899e3d32143e360fe6e6d4d8fa1b5"   # ps3recomp v0.19.0, the commit installer/port/patches are made against
PATCHES = ROOT / "installer" / "port" / "patches"


def git(*args, cwd=None, binary=False):
    r = subprocess.run(["git", "-c", "core.autocrlf=false", *args], cwd=cwd, capture_output=True)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.decode(errors="replace").strip() or "git " + " ".join(args) + " failed")
    return r.stdout if binary else r.stdout.decode(errors="replace").strip()


def have_commit(repo):
    try:
        git("cat-file", "-e", PIN + "^{commit}", cwd=repo)
        return True
    except RuntimeError:
        return False


def export(repo, out, fetch=True, say=print):
    """-> list of patch names applied. Raises RuntimeError with a plain message."""
    repo, out = pathlib.Path(repo), pathlib.Path(out)
    if not (repo / ".git").exists() and not (repo / "HEAD").exists():
        raise RuntimeError(f"{repo} is not a git checkout (run `git submodule update --init` first)")
    if not have_commit(repo):
        if not fetch:
            raise RuntimeError(f"{repo} does not have ps3recomp {TAG} ({PIN[:12]}) and --no-fetch was given")
        say(f"fetching ps3recomp {TAG} into {repo}")
        git("fetch", "origin", "tag", TAG, "--no-tags", cwd=repo)
        if not have_commit(repo):
            raise RuntimeError(f"tag {TAG} of {repo} is not commit {PIN}")
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    data = git("archive", "--format=tar", PIN, cwd=repo, binary=True)
    with tarfile.open(fileobj=io.BytesIO(data)) as tf:
        try:
            tf.extractall(out, filter="data")
        except TypeError:           # Python before 3.12 has no extraction filters
            tf.extractall(out)
    applied = []
    for p in sorted(PATCHES.glob("*.patch")):
        # git apply works outside a repository; paths a/ b/ are relative to the toolkit folder
        # GIT_CEILING_DIRECTORIES: the folder may lie inside a checkout of this repository (build/installer/payload), where git apply
        # would resolve the paths against that checkout and skip the files outside the current folder without failing
        env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(out.resolve().parent))
        r = subprocess.run(["git", "apply", "--verbose", "-p1", "--whitespace=nowarn", str(p)], cwd=out, capture_output=True, text=True, env=env)
        # and require git's confirmation anyway
        if r.returncode or "Applied patch" not in r.stderr + r.stdout:
            raise RuntimeError(f"patch {p.name} does not apply to ps3recomp {TAG}:\n{r.stdout}{r.stderr}")
        applied.append(p.name)
    return applied


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--repo", default=str(ROOT / "third_party" / "ps3recomp"))
    ap.add_argument("--no-fetch", action="store_true")
    a = ap.parse_args(argv)
    try:
        names = export(a.repo, a.out, fetch=not a.no_fetch)
    except RuntimeError as e:
        print("export_toolkit: " + str(e), file=sys.stderr)
        return 1
    print(f"ps3recomp {TAG} ({PIN[:12]}) exported to {a.out}, {len(names)} patches applied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
