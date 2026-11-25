#!/usr/bin/env python3
"""Stage the optional re-psn desktop app for the installer (ENHANCEMENT: the unticked task "repsnapp" in installer/jsp.iss).
Standard library only, Python 3.8+. Runs on the machine that builds the installer, after fetch_toolchain.py; the only network use is the licence texts of the Go modules (below).

    stage_repsn_app.py [--manifest FILE] [--cache DIR] [--out DIR] [--payload DIR]

  --cache    fetch_toolchain.py's cache (default build/installer/toolchain-cache): the entry "re-psn-app" must be in
             fetched.json with the manifest's SHA-256, i.e. downloaded and checked by fetch_toolchain.py
  --out      default build/installer/re-psn-app; ISCC gets it as /DRepsnApp=<full path>
  --payload  default build/installer/payload; --out may not be inside it, so the payload, the portable zip and the
             setup's recursive payload copy never carry the app

The release zip unpacks (no strip_top) to exactly one folder, re-psn-app-v<version>-windows-amd64, holding exactly the six
files of ARCHIVE_FILES. Anything else is refused: a missing, extra or renamed file, a sub-folder, a link, another folder name.
Only the five files of FILES are copied unchanged to <out>/<folder>/ (via <out>.partial, so a refused run leaves no half copy).
re-psn-window.exe (the Rust/Tauri window shell) is verified in the archive but deliberately NOT staged: its crates' licence
texts are not in the v0.9.0 archive, so the app opens the player's browser. It returns in a later pin.
It then adds THIRD-PARTY-LICENSES.txt to the staged folder: the licence text of every Go module linked into the Windows re-psn-app.exe and
re-psn-host.exe, each at its v0.9.0 version (the table installer/tools/repsn_app_licenses.json: each text is downloaded from its URL into
<cache>/re-psn-licenses and checked against its pinned SHA-256, a different one is refused; the archive carries none of them).
Exit 0 = staged, 1 = a check failed, 2 = usage.
"""
import argparse
import fnmatch
import hashlib
import json
import os
import shutil
import stat
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import fetch_toolchain as ft  # noqa: E402

NAME = "re-psn-app"
WINDOW = "re-psn-window.exe"  # in the archive, deliberately not staged
FILES = ("re-psn-app.exe", "re-psn-host.exe", "NOTICE.md", "LICENSE", "README.md")  # staged
ARCHIVE_FILES = FILES + (WINDOW,)  # exactly what the pinned zip must hold
TABLE = os.path.join(HERE, "repsn_app_licenses.json")
GENERATED = "THIRD-PARTY-LICENSES.txt"
OUT = os.path.join(REPO, "build", "installer", "re-psn-app")
PAYLOAD = os.path.join(REPO, "build", "installer", "payload")


def folder_name(version):
    """The one top folder of the release zip, as the launcher's RepsnApp.Folder names it."""
    return "re-psn-app-v%s-windows-amd64" % version


def entry(manifest):
    for raw in manifest.get("components", []):
        if raw.get("name") == NAME:
            return ft.expand(raw)
    return None


def inside(path, parent):
    """True when path is parent or below it (both made absolute and real)."""
    p, q = os.path.realpath(path), os.path.realpath(parent)
    return p == q or p.startswith(q.rstrip(os.sep) + os.sep)


def check_tree(tree, folder):
    """-> list of problems with the unpacked tree (empty = exactly <folder>/ with the six ARCHIVE_FILES, plain files only)."""
    if not os.path.isdir(tree) or os.path.islink(tree):
        return ["%s: not unpacked (run fetch_toolchain.py --only %s)" % (tree, NAME)]
    bad = []
    top = sorted(os.listdir(tree))
    if top != [folder]:
        bad.append("expected exactly the folder %s in %s, found %s" % (folder, tree, top))
        return bad
    d = os.path.join(tree, folder)
    if os.path.islink(d) or not os.path.isdir(d):
        return ["%s is not a plain folder" % d]
    have = sorted(os.listdir(d))
    missing = sorted(set(ARCHIVE_FILES) - set(have))
    extra = sorted(set(have) - set(ARCHIVE_FILES))
    if missing:
        bad.append("missing in %s: %s" % (folder, ", ".join(missing)))
    if extra:
        bad.append("not expected in %s: %s" % (folder, ", ".join(extra)))
    for n in sorted(set(ARCHIVE_FILES) & set(have)):
        st = os.lstat(os.path.join(d, n))
        if not stat.S_ISREG(st.st_mode):
            bad.append("%s/%s is not a plain file" % (folder, n))
    return bad


def load_table(table=None):
    """-> (rows, problems): the rows (dicts of module, version, licence, copyright, file, sha256, url) of the licence table."""
    rows, bad = [], []
    try:
        with open(table or TABLE, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError) as ex:
        return [], ["licence table: %s" % ex]
    keys = ("module", "version", "licence", "copyright", "file", "sha256", "url")
    for row in data.get("modules", []):
        if not isinstance(row, dict) or sorted(row) != sorted(keys) or not all(isinstance(row[k], str) and row[k] for k in keys):
            bad.append("licence table: bad row %r" % str(row)[:60])
            continue
        if not ft.HEX64.fullmatch(row["sha256"]) or not ft.safe_rel(row["file"]) or "/" in row["file"] or not row["url"].startswith("https://"):
            bad.append("licence table: bad sha256, file or url for %s" % row["module"])
        rows.append(row)
    if not rows:
        bad.append("licence table has no modules")
    return rows, bad


def fetch_texts(rows, cache, opener=ft.open_url):
    """Make sure every module's licence text is in <cache>/re-psn-licenses with its pinned SHA-256 (download if not).
    -> (dict file -> text, problems). A text with another hash is deleted and refused."""
    d = os.path.join(cache, "re-psn-licenses")
    os.makedirs(d, exist_ok=True)
    texts, bad = {}, []
    for r_ in rows:
        path = os.path.join(d, r_["file"])
        if not (os.path.isfile(path) and sha256(path) == r_["sha256"]):
            try:
                got = ft.download(r_["url"], path, opener)
            except (OSError, ValueError) as ex:
                bad.append("licence text of %s (%s): %s" % (r_["module"], r_["url"], ex))
                continue
            if got != r_["sha256"]:
                os.unlink(path)
                bad.append("licence text of %s: SHA-256 %s is not the pinned %s; refused and deleted" % (r_["module"], got, r_["sha256"]))
                continue
        with open(path, encoding="utf-8", newline="") as f:
            texts[r_["file"]] = f.read()
    return texts, bad


def third_party_text(rows, texts):
    out = ["Third-party licences of the Go modules linked into the Windows builds of re-psn-app.exe and re-psn-host.exe (re-psn v0.9.0).",
           "re-psn's own licence is in LICENSE and its notes are in NOTICE.md, beside this file.", ""]
    for r_ in rows:
        out += ["=" * 78, "%s %s  (%s)" % (r_["module"], r_["version"], r_["licence"]), r_["copyright"], r_["url"], "=" * 78, "",
                texts[r_["file"]].rstrip("\n"), ""]
    return "\n".join(out) + "\n"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


MARKER = ".jsp-stage-output"


def is_stage_output(path):
    """True when path is a folder this stager made (the marker file), is empty, or holds exactly the one re-psn-app-v*-windows-amd64
    folder; anything else (a link, a file, other content) is not ours and is never deleted."""
    if os.path.islink(path) or not os.path.isdir(path):
        return False
    names = os.listdir(path)
    if not names or MARKER in names:
        return True
    return len(names) == 1 and fnmatch.fnmatch(names[0], "re-psn-app-v*-windows-amd64") and os.path.isdir(os.path.join(path, names[0])) \
        and not os.path.islink(os.path.join(path, names[0]))


def stage(manifest_path, cache, out, payload, stream=sys.stdout, table=None, opener=ft.open_url):
    """-> exit code (0 staged, 1 refused). A refused run removes an older --out and a stale .partial, so an old folder is not taken
    for this run's result, but only one this stager made (is_stage_output); any other existing path is refused and left untouched,
    as is anything overlapping the payload or the cache."""
    out = os.path.abspath(out)   # a trailing slash on a symlinked --out would hide the link from os.path.islink
    part = out + ".partial"
    for d in (out, part):
        if os.path.lexists(d) and not is_stage_output(d):
            print("FAIL %s exists and is not the output of this stager: refused, left untouched" % d, file=stream)
            return 1
    if any(inside(x, y) or inside(y, x) for x in (out, part) for y in (payload, cache)):
        print("FAIL --out %s (or its .partial) overlaps the payload %s or the cache %s" % (out, payload, cache), file=stream)
        return 1
    rc = _stage(manifest_path, cache, out, payload, stream, table, opener)
    if rc != 0:
        for d in (out, part):
            if os.path.lexists(d) and is_stage_output(d):
                shutil.rmtree(d)
    return rc


def _stage(manifest_path, cache, out, payload, stream, table, opener):
    def fail(msg):
        print("FAIL " + msg, file=stream)
        return 1
    try:
        m = ft.load(manifest_path)
    except (OSError, ValueError) as ex:
        return fail("manifest %s: %s" % (manifest_path, ex))
    problems = ft.validate(m, ("https",), strict=True)
    if problems:
        return fail("manifest: " + "; ".join(problems))
    e = entry(m)
    if e is None:
        return fail("no %s entry in %s" % (NAME, manifest_path))
    if not e.get("stage_only") or e.get("archive") != "zip" or e.get("strip_top"):
        return fail("%s: expected a stage_only zip entry without strip_top" % NAME)
    if inside(out, payload) or inside(payload, out):
        return fail("--out %s overlaps the payload %s: the app must stay outside the payload" % (out, payload))
    if inside(out, cache) or inside(cache, out):
        return fail("--out %s overlaps the cache %s" % (out, cache))
    got = ft.read_fetched(cache).get(NAME, {})
    if got.get("sha256") != e["sha256"] or got.get("version") != e["version"]:
        return fail("%s: not in the cache with the manifest's hash and version (run fetch_toolchain.py --only %s)" % (NAME, NAME))
    folder = folder_name(e["version"])
    tree = os.path.join(cache, "trees", NAME)
    bad = check_tree(tree, folder)
    if bad:
        for b in bad:
            print("FAIL " + b, file=stream)
        return 1
    rows, bad = load_table(table)
    texts = {}
    if not bad:
        texts, bad = fetch_texts(rows, cache, opener)
    if bad:
        for b in bad:
            print("FAIL " + b, file=stream)
        return 1
    part = out.rstrip("/\\") + ".partial"
    if os.path.lexists(part):
        shutil.rmtree(part)
    os.makedirs(os.path.join(part, folder))
    open(os.path.join(part, MARKER), "w").close()
    for n in FILES:
        src, dst = os.path.join(tree, folder, n), os.path.join(part, folder, n)
        shutil.copyfile(src, dst)
        if sha256(src) != sha256(dst):
            shutil.rmtree(part)
            return fail("%s: copy differs from the cache" % n)
        print("ok   %s/%s (%d bytes)" % (folder, n, os.path.getsize(dst)), file=stream)
    with open(os.path.join(part, folder, GENERATED), "w", encoding="utf-8", newline="\n") as f:
        f.write(third_party_text(rows, texts))
    print("ok   %s/%s (%d modules)" % (folder, GENERATED, len(rows)), file=stream)
    if os.path.lexists(out):
        shutil.rmtree(out)
    os.replace(part, out)
    print("staged %s %s (%d files + %s) in %s: pass /DRepsnApp=%s to ISCC" % (NAME, e["version"], len(FILES), GENERATED, out, os.path.abspath(out)), file=stream)
    return 0


def main(argv=None, stream=sys.stdout):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--manifest", default=ft.MANIFEST)
    ap.add_argument("--cache", default=ft.CACHE)
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--payload", default=PAYLOAD)
    try:
        a = ap.parse_args(argv)
    except SystemExit as ex:
        return 2 if ex.code else 0
    return stage(a.manifest, os.path.abspath(a.cache), os.path.abspath(a.out), os.path.abspath(a.payload), stream)


if __name__ == "__main__":
    sys.exit(main())
