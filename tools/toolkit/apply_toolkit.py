#!/usr/bin/env python3
"""Build the patched ps3recomp toolkit from the submodule and toolkit/ps3recomp-local.patch.

The submodule third_party/ps3recomp stays unchanged. This script exports its pinned commit (v0.15.0) into a build
folder and applies the patch there; the port is then built with -DPS3RECOMP_DIR pointing at that folder.

    python tools/toolkit/apply_toolkit.py            # writes build/ps3recomp (refuses if it exists)
    python tools/toolkit/apply_toolkit.py --force    # replaces build/ps3recomp
    python tools/toolkit/apply_toolkit.py --out DIR  # another folder
    python tools/toolkit/apply_toolkit.py --check    # only checks that the patch applies (CI); writes nothing

Needs Python 3.8+ and Git on PATH; standard library only, so it runs the same on Windows and Linux. The files come
from `git archive` of the pinned commit with line-ending conversion off, so a Windows checkout with core.autocrlf on
gets the same bytes the patch was made against. The four SMAA files in toolkit/third_party_smaa/ are copied into
libs/video/third_party_smaa/ of the patched folder, because the patched video library includes them.
"""
import argparse
import hashlib
import io
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SUB = ROOT / "third_party" / "ps3recomp"
PATCH = ROOT / "toolkit" / "ps3recomp-local.patch"
SMAA_SRC = ROOT / "toolkit" / "third_party_smaa"   # SMAA 1x (MIT) files the patched video library includes
SMAA_DEST = pathlib.PurePosixPath("libs/video/third_party_smaa")
SMAA_FILES = ("AreaTex.h", "SearchTex.h", "SMAA.hlsl", "SMAA_hlsl.inc")
PIN = "0fe7dbf5db9a467aca268865fb26170ea113c541"   # ps3recomp v0.15.0, the commit the patch is made against
DEFAULT_OUT = ROOT / "build" / "ps3recomp"
STAMP = ".toolkit-stamp"


def fail(msg):
    print("apply_toolkit: " + msg, file=sys.stderr)
    sys.exit(1)


def git(*args, cwd=None, binary=False):
    r = subprocess.run(["git", "-c", "core.autocrlf=false", *args], cwd=cwd, capture_output=True)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.decode(errors="replace").strip() or "git " + " ".join(args) + " failed")
    return r.stdout if binary else r.stdout.decode(errors="replace").strip()


def check_inputs():
    if not PATCH.is_file():
        fail(f"{PATCH.relative_to(ROOT)} is missing")
    for n in SMAA_FILES:
        if not (SMAA_SRC / n).is_file():
            fail(f"toolkit/third_party_smaa/{n} is missing")
    if not (SUB / ".git").exists():
        fail("third_party/ps3recomp is empty. Clone with `git clone --recurse-submodules`, or run "
             "`git submodule update --init` in this checkout. A \"Download ZIP\" from GitHub does not include it.")
    try:
        head = git("rev-parse", "HEAD", cwd=SUB)
        git("cat-file", "-e", PIN + "^{commit}", cwd=SUB)
    except RuntimeError as e:
        fail(f"third_party/ps3recomp does not have the pinned commit {PIN[:8]} ({e}). Run `git submodule update --init`.")
    if head != PIN:
        print(f"apply_toolkit: note: the submodule is checked out at {head[:8]}, not the pin {PIN[:8]}; "
              "using the pinned commit", file=sys.stderr)
    data = PATCH.read_bytes()
    if b"\r\n" not in data:
        fail(f"{PATCH.relative_to(ROOT)} has no CRLF lines, so its line endings were converted (it needs its 492 CRLF "
             "lines as stored). Check that .gitattributes has `*.patch -text`, then restore it: "
             "`git checkout -- toolkit/ps3recomp-local.patch`.")
    return hashlib.sha256(data).hexdigest()


def export(dest):
    """Write the pinned commit's files into dest (no .git, no line-ending conversion)."""
    tar = git("archive", "--format=tar", PIN, cwd=SUB, binary=True)
    with tarfile.open(fileobj=io.BytesIO(tar)) as t:
        for m in t.getmembers():
            p = pathlib.PurePosixPath(m.name)
            if p.is_absolute() or ".." in p.parts:
                fail(f"unexpected path in the archive: {m.name}")
        if sys.version_info >= (3, 12):
            t.extractall(dest, filter="data")
        else:
            t.extractall(dest)


def apply(dest, check_only):
    # dest gets its own throwaway repository so `git apply` works on dest and never on this checkout.
    git("init", "-q", cwd=dest)
    args = ["apply", "--whitespace=nowarn"] + (["--check"] if check_only else []) + [str(PATCH)]
    try:
        git(*args, cwd=dest)
    except RuntimeError as e:
        fail("the patch does not apply to the pinned toolkit:\n" + str(e))
    if not check_only:
        shutil.rmtree(dest / ".git")
    # The patched video library includes these files; the patch itself does not carry them.
    target = dest / SMAA_DEST
    if not target.parent.is_dir():
        fail(f"the patched toolkit has no {SMAA_DEST.parent} folder to put the SMAA files in")
    if not check_only:
        target.mkdir(exist_ok=True)
        for n in SMAA_FILES:
            shutil.copyfile(SMAA_SRC / n, target / n)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", type=pathlib.Path, default=DEFAULT_OUT, help="output folder (default build/ps3recomp)")
    ap.add_argument("--force", action="store_true", help="replace the output folder if it exists")
    ap.add_argument("--check", action="store_true", help="only check that the patch applies; write nothing")
    a = ap.parse_args()
    sha = check_inputs()

    if a.check:
        with tempfile.TemporaryDirectory(prefix="ps3recomp-check-") as tmp:
            export(pathlib.Path(tmp))
            apply(pathlib.Path(tmp), check_only=True)
        print(f"apply_toolkit: OK, toolkit/ps3recomp-local.patch applies to ps3recomp {PIN[:8]} (v0.15.0)")
        return

    out = a.out.resolve()
    sub = SUB.resolve()
    if out in (ROOT, sub) or out in ROOT.parents or out in sub.parents or sub in out.parents:
        fail(f"refusing to write into {out}: it holds this checkout or the submodule")
    if out.exists():
        if not a.force:
            fail(f"{out} exists; use --force to replace it")
        if not (out / STAMP).is_file():
            fail(f"{out} exists but was not made by this script (no {STAMP}); remove it yourself if you mean to")
        shutil.rmtree(out)
    out.mkdir(parents=True)
    export(out)
    apply(out, check_only=False)
    (out / STAMP).write_text(f"ps3recomp {PIN}\npatch sha256 {sha}\n", encoding="utf-8")
    print(f"apply_toolkit: patched toolkit in {out}")
    print(f"Build the port with: cmake ... -DPS3RECOMP_DIR={out.as_posix()}")


if __name__ == "__main__":
    main()
