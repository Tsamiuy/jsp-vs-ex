#!/usr/bin/env python3
"""The optional re-psn desktop app in the installer (ENHANCEMENT, the unticked task "repsnapp"): pin, fetch, staging and source checks.

    python repsn_app_bundle_test.py [REPO_ROOT]

No network, no Windows: fetch_toolchain.fetch_one runs through its injected opener against fixture zips made here.
  * toolchain.json: the re-psn-app entry has exactly the pinned url, SHA-256 and SHA256SUMS list, stage_only, pin confirmed,
    and the manifest passes fetch_toolchain's strict check;
  * fetch: a matching hash is accepted and unpacked; a mismatch is refused and the download deleted; a SHA256SUMS that disagrees
    is refused; a '..' or absolute zip member is refused;
  * stage_repsn_app.py: the six-file zip folder is checked whole, and its five files (not re-psn-window.exe) are copied byte for byte; a missing exe, another folder name, an extra
    file or a cache entry without the manifest's hash is refused, and nothing is written into the payload folder;
  * jsp.iss, installer.yml, assemble_payload.py, assemble_portable.py and NOTICE.md: the task is unchecked and only exists with
    /DRepsnApp, nothing starts the app or adds a firewall rule, the caveats are there, the payload and the portable zip never
    carry the app, the licence lines are in NOTICE.md (the two Go programs only), the caveat
    label sizes itself from its text and is anchored for a resizable wizard, and the zip itself is not in git.
"""
import contextlib
import hashlib
import io
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import fetch_toolchain as ft  # noqa: E402
import stage_repsn_app as sra  # noqa: E402

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else HERE.parents[1]).resolve()
fails = []

PIN_URL = "https://github.com/undecuple/re-psn/releases/download/v0.9.0/re-psn-app-v0.9.0-windows-amd64.zip"
PIN_LIST = "https://github.com/undecuple/re-psn/releases/download/v0.9.0/SHA256SUMS"
PIN_SHA = "6849472d54a7c5489625c0bfcf0a3bef63a47b702209dacae52588325aeb50c8"
FOLDER = "re-psn-app-v0.9.0-windows-amd64"
ZIPNAME = FOLDER + ".zip"


def check(name, ok):
    print(("ok   " if ok else "FAIL ") + name)
    if not ok:
        fails.append(name)


# ---------------------------------------------------------------------------------------------------- manifest
manifest = ft.load(str(root / "installer/tools/toolchain.json"))
raw = [c for c in manifest["components"] if c.get("name") == "re-psn-app"]
check("toolchain.json has one re-psn-app entry", len(raw) == 1)
raw = raw[0] if raw else {}
e = ft.expand(raw) if raw else {}
check("pinned url", e.get("url") == PIN_URL)
check("pinned sha256", e.get("sha256") == PIN_SHA)
check("SHA256SUMS list is cross-checked", e.get("sha256_list") == PIN_LIST)
check("stage_only zip, folder kept (no strip_top)", e.get("stage_only") is True and e.get("archive") == "zip" and not e.get("strip_top"))
check("pin confirmed, version 0.9.0", e.get("pin") == "confirmed" and e.get("version") == "0.9.0")
check("licence and notice name MIT and both copyright holders",
      "MIT" in e.get("license", "") and "sp00nz" in e.get("notice", "") and "undecuple" in e.get("notice", ""))
check("release page is the v0.9.0 tag", e.get("release_page") == "https://github.com/undecuple/re-psn/releases/tag/v0.9.0")
check("manifest passes the strict check", ft.validate(manifest, ("https",), strict=True) == [])
check("stage script's folder name follows the manifest version", sra.folder_name(e.get("version", "")) == FOLDER)


# ---------------------------------------------------------------------------------------------------- fetch (fixtures)
FIXTURE = {n: ("fixture %s\n" % n).encode() * 3 for n in sra.ARCHIVE_FILES}


def make_zip(members):
    """members: list of (name, bytes); a name ending in '/' is a folder entry."""
    b = io.BytesIO()
    with zipfile.ZipFile(b, "w", zipfile.ZIP_DEFLATED) as z:
        for n, data in members:
            zi = zipfile.ZipInfo(n)
            zi.external_attr = ((0o40755 if n.endswith("/") else 0o100644) << 16)
            z.writestr(zi, data)
    return b.getvalue()


def good_members():
    return [(FOLDER + "/", b"")] + [(FOLDER + "/" + n, d) for n, d in FIXTURE.items()]


def opener_for(zip_bytes, sums_text):
    def opener(url):
        if url == PIN_URL:
            return io.BytesIO(zip_bytes)
        if url == PIN_LIST:
            return io.BytesIO(sums_text.encode())
        raise OSError("unexpected url " + url)
    return opener


def sums(digest):
    return "%s  re-psn-v0.9.0-windows-amd64.zip\n%s  %s\n" % ("1" * 64, digest, ZIPNAME)


def fetch(zip_bytes, manifest_sha, list_sha):
    cache = tempfile.mkdtemp(prefix="repsnfetch")
    entry = dict(raw, sha256=manifest_sha)
    out = io.StringIO()
    ok = ft.fetch_one(entry, cache, False, os.devnull, opener_for(zip_bytes, sums(list_sha)), out)
    return ok, cache, out.getvalue()


good = make_zip(good_members())
gh = hashlib.sha256(good).hexdigest()
ok, cache, log = fetch(good, gh, gh)
tree = os.path.join(cache, "trees", "re-psn-app")
check("fetch: matching hash accepted (%s)" % log.strip().splitlines()[-1:], ok)
check("fetch: tree unpacked with the folder kept",
      os.path.isdir(os.path.join(tree, FOLDER)) and sorted(os.listdir(os.path.join(tree, FOLDER))) == sorted(sra.ARCHIVE_FILES))
check("fetch: fetched.json records the hash", ft.read_fetched(cache).get("re-psn-app", {}).get("sha256") == gh)
shutil.rmtree(cache, ignore_errors=True)

ok, cache, log = fetch(good, "0" * 64, gh)
dl = os.path.join(cache, "downloads", "re-psn-app", ZIPNAME)
check("fetch: hash mismatch refused", not ok and "mismatch" in log)
check("fetch: refused download deleted", not os.path.exists(dl))
check("fetch: nothing unpacked after a mismatch", not os.path.exists(os.path.join(cache, "trees", "re-psn-app")))
shutil.rmtree(cache, ignore_errors=True)

ok, cache, log = fetch(good, gh, "2" * 64)
check("fetch: SHA256SUMS disagreeing with the download refused", not ok and "not the published one" in log)
check("fetch: download deleted after the list disagreed", not os.path.exists(os.path.join(cache, "downloads", "re-psn-app", ZIPNAME)))
shutil.rmtree(cache, ignore_errors=True)
ok, cache, log = fetch(good, "4" * 64, "4" * 64)
check("fetch: manifest and list agreeing with each other but not the download refused", not ok and "not the published one" in log)
shutil.rmtree(cache, ignore_errors=True)

for what, name in (("'..' member", FOLDER + "/../evil.exe"), ("absolute member", "/evil.exe"), ("drive letter member", "C:/evil.exe")):
    bad = make_zip(good_members() + [(name, b"x")])
    bh = hashlib.sha256(bad).hexdigest()
    ok, cache, log = fetch(bad, bh, bh)
    check("fetch: %s refused" % what, not ok and "unsafe zip member" in log)
    check("fetch: %s leaves no tree" % what, not os.path.exists(os.path.join(cache, "trees", "re-psn-app")))
    shutil.rmtree(cache, ignore_errors=True)


# ---------------------------------------------------------------------------------------------------- staging
LIC_TEXTS = {"https://example.invalid/%d/LICENSE" % i: ("licence text %d\nCopyright (c) 200%d Someone\n" % (i, i)).encode() for i in range(3)}


def fixture_table(base, bad_hash=False):
    rows = []
    for i, (url, data) in enumerate(LIC_TEXTS.items()):
        h = hashlib.sha256(data).hexdigest()
        if bad_hash and i == 1:
            h = "5" * 64
        rows.append(dict(zip(("module", "version", "licence", "copyright", "file", "sha256", "url"),
                             ("example.invalid/mod%d" % i, "v1.%d.0" % i, "MIT", "Copyright (c) 200%d Someone" % i, "mod%d-LICENSE.txt" % i, h, url))))
    t = base / "table.json"
    t.write_text(json.dumps({"modules": rows}), encoding="utf-8")
    return str(t)


class Opener:
    """Serves the fixture texts; counts the requests; a URL not in the dict is a 404."""
    def __init__(self, texts=LIC_TEXTS):
        self.texts, self.calls = texts, []

    def __call__(self, url):
        self.calls.append(url)
        if url not in self.texts:
            raise OSError("404 " + url)
        return io.BytesIO(self.texts[url])


def stage_case(mutate=None, fetched=True, sha=PIN_SHA, out_in_payload=False, bad_hash=False, opener=None):
    """A fake cache (fetched.json + tree) for the real manifest entry; -> (exit code, out dir, payload dir, log)."""
    base = pathlib.Path(tempfile.mkdtemp(prefix="repsnstage"))
    cache, payload = base / "cache", base / "payload"
    (payload / "toolchain").mkdir(parents=True)
    (payload / "NOTICE.txt").write_text("payload\n", encoding="utf-8")
    d = cache / "trees" / "re-psn-app" / FOLDER
    d.mkdir(parents=True)
    for n, data in FIXTURE.items():
        (d / n).write_bytes(data)
    if mutate:
        mutate(cache / "trees" / "re-psn-app")
    if fetched:
        ft.write_fetched(str(cache), {"re-psn-app": {"sha256": sha, "version": "0.9.0", "url": PIN_URL, "files": 6}})
    out = (payload / "re-psn-app") if out_in_payload else (base / "out")
    before = sorted(str(p.relative_to(payload)) for p in payload.rglob("*"))
    log = io.StringIO()
    opener = opener or Opener()
    rc = sra.stage(str(root / "installer/tools/toolchain.json"), str(cache), str(out), str(payload), log, fixture_table(base, bad_hash), opener)
    stage_case.last_opener = opener
    after = sorted(str(p.relative_to(payload)) for p in payload.rglob("*"))
    check("stage: payload folder unchanged", before == after)
    return rc, out, base, log.getvalue()


rc, out, base, log = stage_case()
check("stage: the six-file zip is staged (exit 0)", rc == 0)
STAGED = ["LICENSE", "NOTICE.md", "README.md", "re-psn-app.exe", "re-psn-host.exe"]
check("stage: exactly the folder with the five release files and the generated licence file",
      sorted(os.listdir(out)) == sorted([FOLDER, sra.MARKER]) and sorted(os.listdir(out / FOLDER)) == sorted(STAGED + ["THIRD-PARTY-LICENSES.txt"])
      and sorted(sra.FILES) == STAGED and len(os.listdir(out / FOLDER)) == 6)
STAGED_OUT = out
check("stage: re-psn-window.exe is not staged (it is in the archive, and left out)",
      "re-psn-window.exe" in FIXTURE and not (out / FOLDER / "re-psn-window.exe").exists() and "re-psn-window.exe" not in sra.FILES)
check("stage: the five are copied byte for byte", all((out / FOLDER / n).read_bytes() == FIXTURE[n] for n in STAGED))
check("stage: no .partial left", not os.path.exists(str(out) + ".partial"))
tp = (out / FOLDER / "THIRD-PARTY-LICENSES.txt").read_text(encoding="utf-8")
check("licences: THIRD-PARTY-LICENSES.txt holds every module's name, version, copyright line and full text",
      all(("example.invalid/mod%d v1.%d.0" % (k, k)) in tp and d.decode().rstrip("\n") in tp for k, d in enumerate(LIC_TEXTS.values())))
check("licences: each text was fetched once from its pinned URL", sorted(stage_case.last_opener.calls) == sorted(LIC_TEXTS))
shutil.rmtree(base, ignore_errors=True)

for what, mutate in (
        ("re-psn-host.exe missing", lambda t: (t / FOLDER / "re-psn-host.exe").unlink()),
        ("re-psn-window.exe missing (the archive must still hold it)", lambda t: (t / FOLDER / "re-psn-window.exe").unlink()),
        ("LICENSE missing", lambda t: (t / FOLDER / "LICENSE").unlink()),
        ("wrong folder name", lambda t: (t / FOLDER).rename(t / "re-psn-app-v0.8.0-windows-amd64")),
        ("renamed file", lambda t: (t / FOLDER / "README.md").rename(t / FOLDER / "README.txt")),
        ("extra file", lambda t: (t / FOLDER / "extra.dll").write_bytes(b"x")),
        ("extra sub-folder", lambda t: (t / FOLDER / "sub").mkdir()),
        ("second top folder", lambda t: (t / "other").mkdir()),
        ("a folder in place of a file", lambda t: ((t / FOLDER / "NOTICE.md").unlink(), (t / FOLDER / "NOTICE.md").mkdir()))):
    rc, out, base, log = stage_case(mutate)
    check("stage: %s refused (exit %d)" % (what, rc), rc == 1 and "FAIL" in log)
    check("stage: %s leaves no output" % what, not os.path.exists(out))
    shutil.rmtree(base, ignore_errors=True)

rc, out, base, log = stage_case(bad_hash=True)
check("stage: a licence text with another SHA-256 is refused, nothing staged", rc == 1 and "pinned" in log and not os.path.exists(out))
shutil.rmtree(base, ignore_errors=True)
rc, out, base, log = stage_case(opener=Opener({}))
check("stage: a licence text that cannot be fetched is refused, nothing staged", rc == 1 and "licence text" in log and not os.path.exists(out))
shutil.rmtree(base, ignore_errors=True)

# an older --out from a previous run does not survive a refused one
base = pathlib.Path(tempfile.mkdtemp(prefix="repsnold"))
rc0, out0, base0, _ = stage_case()
shutil.rmtree(base, ignore_errors=True)
check("stage: (setup) a first run stages", rc0 == 0 and os.path.isdir(out0))
cache0, payload0 = base0 / "cache", base0 / "payload"
(base0 / "out.partial").mkdir()
rc1 = sra.stage(str(root / "installer/tools/toolchain.json"), str(cache0), str(out0), str(payload0), io.StringIO(),
                fixture_table(base0, bad_hash=True), Opener())
check("stage: a refused run removes the older --out and a stale .partial", rc1 == 1 and not os.path.exists(out0) and not os.path.exists(str(out0) + ".partial"))
shutil.rmtree(base0, ignore_errors=True)

# an existing --out that this stager did not make survives every refused run, and so does an overlapping one
def refused_keeps(what, make_out, **kw):
    rc, out_, base_, log_ = stage_case(**kw)
    shutil.rmtree(out_, ignore_errors=True)
    make_out(out_)
    cache_, payload_ = base_ / "cache", base_ / "payload"
    tgt = kw.get("target") or out_
    rc_ = sra.stage(str(root / "installer/tools/toolchain.json"), str(cache_), str(tgt), str(payload_), io.StringIO(),
                    fixture_table(base_, bad_hash=True), Opener())
    return rc_, tgt, base_


def precious(o):
    o.mkdir()
    (o / "keep.txt").write_text("mine\n")


rc_, tgt, base_ = refused_keeps("unrelated", precious)
check("stage: an unrelated existing --out survives a refused run", rc_ == 1 and (tgt / "keep.txt").read_text() == "mine\n")
shutil.rmtree(base_, ignore_errors=True)
rc_, tgt, base_ = refused_keeps("unrelated, bad hash and a good first run", precious)
check("stage: an unrelated existing --out is refused even when a run would succeed",
      rc_ == 1 and sorted(os.listdir(tgt)) == ["keep.txt"])
shutil.rmtree(base_, ignore_errors=True)
base1 = pathlib.Path(tempfile.mkdtemp(prefix="repsnempty"))
(base1 / "precious").mkdir()
(base1 / "precious" / "keep.txt").write_text("mine\n")
rc_ = sra.stage(str(root / "installer/tools/toolchain.json"), str(base1 / "emptycache"), str(base1 / "precious"), str(base1 / "payload"), io.StringIO())
check("stage: an early refusal (empty cache) leaves an unrelated --out untouched", rc_ == 1 and (base1 / "precious" / "keep.txt").exists())
(base1 / "payload").mkdir()
(base1 / "payload" / "keep.txt").write_text("payload\n")
rc_ = sra.stage(str(root / "installer/tools/toolchain.json"), str(base1 / "emptycache"), str(base1 / "payload" / "sub"), str(base1 / "payload"), io.StringIO())
rc2 = sra.stage(str(root / "installer/tools/toolchain.json"), str(base1 / "payload"), str(base1 / "payload"), str(base1 / "payload"), io.StringIO())
check("stage: an --out overlapping the payload or the cache is refused and its folder survives", rc_ == 1 and rc2 == 1 and (base1 / "payload" / "keep.txt").exists())
shutil.rmtree(base1, ignore_errors=True)
rc_, _o, base_, _l = stage_case()
(base_ / "out" / "re-psn-app-v0.0.0-windows-amd64").mkdir(exist_ok=True)
check("stage: a real old stage is recognised as ours", rc_ == 0 and sra.is_stage_output(str(base_ / "out")))
check("stage: a folder with other content or a plain file is not ours",
      not sra.is_stage_output(str(base_ / "cache")) and not sra.is_stage_output(str(base_ / "cache" / "fetched.json")))
shutil.rmtree(base_, ignore_errors=True)
# a symlinked --out given with a trailing slash is still seen as a link: refused, and the link target keeps its contents
base2 = pathlib.Path(tempfile.mkdtemp(prefix="repsnlnk"))
(base2 / "tgt" / "re-psn-app-v0.9.0-windows-amd64").mkdir(parents=True)
(base2 / "tgt" / "re-psn-app-v0.9.0-windows-amd64" / "x").write_text("keep\n")
(base2 / "emptycache").mkdir()
os.symlink(str(base2 / "tgt"), str(base2 / "lnk"))
rc_ = sra.stage(str(root / "installer/tools/toolchain.json"), str(base2 / "emptycache"), str(base2 / "lnk") + os.sep,
                str(base2 / "payload"), io.StringIO())
check("stage: a symlinked --out with a trailing slash is refused and its target survives",
      rc_ == 1 and (base2 / "tgt" / "re-psn-app-v0.9.0-windows-amd64" / "x").exists() and os.path.islink(str(base2 / "lnk")))
shutil.rmtree(base2, ignore_errors=True)


rc, out, base, log = stage_case(fetched=False)
check("stage: not fetched refused", rc == 1 and not os.path.exists(out))
shutil.rmtree(base, ignore_errors=True)
rc, out, base, log = stage_case(sha="3" * 64)
check("stage: cache entry without the manifest hash refused", rc == 1 and "manifest's hash" in log and not os.path.exists(out))
shutil.rmtree(base, ignore_errors=True)
rc, out, base, log = stage_case(out_in_payload=True)
check("stage: --out inside the payload refused", rc == 1 and "payload" in log)
shutil.rmtree(base, ignore_errors=True)
with contextlib.redirect_stderr(io.StringIO()):
    rc = sra.main(["--no-such-flag"], io.StringIO())
check("stage: bad arguments give exit 2", rc == 2)
check("stage: default output is build/installer/re-psn-app, outside the default payload",
      pathlib.Path(sra.OUT).parts[-3:] == ("build", "installer", "re-psn-app") and not sra.inside(sra.OUT, sra.PAYLOAD))


# ---------------------------------------------------------------------------------------------------- sources
iss = (root / "installer/jsp.iss").read_text(encoding="utf-8")
lines = iss.splitlines()


def blocks():
    """Line index ranges inside '#ifdef RepsnApp' ... '#endif' (no nesting in this script)."""
    r, start = [], None
    for i, l in enumerate(lines):
        s = l.strip()
        if s == "#ifdef RepsnApp":
            start = i
        elif s == "#endif" and start is not None:
            r.append((start, i))
            start = None
    return r


def in_block(i):
    return any(a < i < b for a, b in blocks())


def section_of(i):
    for j in range(i, -1, -1):
        m = re.match(r"^\[(\w+)\]\s*$", lines[j])
        if m:
            return m.group(1)
    return ""


task = [i for i, l in enumerate(lines) if l.startswith('Name: "repsnapp";')]
check("jsp.iss: one repsnapp task", len(task) == 1)
if task:
    tl = lines[task[0]]
    check("jsp.iss: the task is 'Flags: unchecked'", re.search(r";\s*Flags:\s*unchecked\s*$", tl) is not None)
    check("jsp.iss: the task sits in [Tasks] inside #ifdef RepsnApp", section_of(task[0]) == "Tasks" and in_block(task[0]))
    check("jsp.iss: the task's short line", 'Description: "re-psn desktop app (pre-release): play first, host if it works for you"' in tl)
files = [i for i, l in enumerate(lines) if "Tasks: repsnapp" in l]
check("jsp.iss: one [Files] line with Tasks: repsnapp, inside the block", len(files) == 1 and section_of(files[0]) == "Files" and in_block(files[0]))
if files:
    fl = lines[files[0]]
    check("jsp.iss: source is the staged folder, DestDir exact",
          'Source: "{#RepsnApp}\\' + FOLDER + '\\*"' in fl and 'DestDir: "{app}\\' + FOLDER + '"' in fl)
    check("jsp.iss: the files line is not a dontcopy/temp copy", "dontcopy" not in fl and "deleteafterinstall" not in fl)
code = [i for i, l in enumerate(lines) if not l.lstrip().startswith(";") and re.search(r"repsn|re-psn", l, re.I) and l.strip() != "#ifdef RepsnApp"]
check("jsp.iss: every non-comment line about the app is inside #ifdef RepsnApp", all(in_block(i) for i in code) and len(code) >= 5)
run_lines = [l for i, l in enumerate(lines) if section_of(i) in ("Run", "UninstallRun") and not l.lstrip().startswith(";")]
check("jsp.iss: nothing in [Run] or [UninstallRun] runs the app", not any(re.search(r"repsn|re-psn", l, re.I) for l in run_lines))
check("jsp.iss: no netsh, firewall command or Run key", not re.search(r"netsh|advfirewall|New-NetFirewallRule|CurrentVersion\\Run", iss, re.I))
check("jsp.iss: no [Registry] section", "[Registry]" not in iss)
execs = re.findall(r"\b(?:Exec|ShellExec|ExecAsOriginalUser)\s*\(([^;]*)", iss)
check("jsp.iss: no Exec of the app", not any(re.search(r"repsn|re-psn", x, re.I) for x in execs))
note = iss[iss.index("RepsnNote.Caption :="):]
note = note[:note.index(";\n")]
for w in ("pre-release", "partial", "in progress", "play first, host if it works for you", "unsigned", "SmartScreen",
          "native Windows is not verified", "Wine", "no public server", "no accounts", "private server for friends",
          "The game connects without the app", "opens in your web browser", "nothing is started", "no firewall rule"):
    check("jsp.iss: caveat %r on the tasks page" % w, w.lower() in note.lower())
check("jsp.iss: the caveat label sits on the tasks page", "RepsnNote.Parent := WizardForm.SelectTasksPage;" in iss)
# Layout of the caveat label: WizardStyle=modern makes the wizard resizable (Inno 6 default) and TasksList is anchored on all four
# sides, so a fixed-height label with the default anchors would be overlapped by the growing list or clip its last caveat.
nb = iss[iss.index("RepsnNote := TNewStaticText.Create"):]
nb = nb[:nb.index("#endif")]
nb_lines = [l.strip() for l in nb.splitlines() if l.strip() and not l.strip().startswith(("{", "'"))]


def at(prefix):
    hits = [i for i, l in enumerate(nb_lines) if l.startswith(prefix)]
    return hits[0] if len(hits) == 1 else -1


check("jsp.iss: WizardStyle is modern (the reason the wizard can be resized)", re.search(r"^WizardStyle=modern\b", iss, re.M) is not None)
check("jsp.iss: no 'WizardResizable=no' (the label is laid out for a resizable wizard)", not re.search(r"^WizardResizable\s*=\s*no", iss, re.M | re.I))
check("jsp.iss: the caveat label has no fixed height", not re.search(r"RepsnNote\.Height\s*:=", nb))
check("jsp.iss: the caveat label sizes itself from its text after the caption is set",
      0 <= at("RepsnNote.Caption :=") < at("RepsnNote.AdjustHeight;"))
check("jsp.iss: the task list shrinks by the label's measured height, after AdjustHeight",
      0 <= at("RepsnNote.AdjustHeight;") < at("WizardForm.TasksList.Height := WizardForm.TasksList.Height - RepsnNote.Height"))
check("jsp.iss: the label sits right under the shortened list",
      at("WizardForm.TasksList.Height :=") < at("RepsnNote.Top := WizardForm.TasksList.Top + WizardForm.TasksList.Height"))
check("jsp.iss: the label takes the list's left edge and width",
      at("RepsnNote.Left := WizardForm.TasksList.Left;") >= 0 and at("RepsnNote.Width := WizardForm.TasksList.Width;") >= 0)
check("jsp.iss: the label is anchored left, right and bottom, set last once its bounds are final",
      at("RepsnNote.Anchors := [akLeft, akRight, akBottom];") == len(nb_lines) - 1
      and at("RepsnNote.Anchors :=") > at("RepsnNote.Top :="))
check("jsp.iss: the header says setup never starts the app", "setup never starts it" in iss[:2000])

yml = (root / ".github/workflows/installer.yml").read_text(encoding="utf-8")
check("installer.yml: stages the app after fetch_toolchain",
      "python installer\\tools\\stage_repsn_app.py" in yml
      and yml.index("installer\\tools\\fetch_toolchain.py") < yml.index("stage_repsn_app.py") < yml.index("assemble_payload.py"))
check("installer.yml: stage step checks its exit code",
      re.search(r"stage_repsn_app\.py\n\s*if \(\$LASTEXITCODE -ne 0\) \{ exit 1 \}", yml) is not None)
check("installer.yml: ISCC gets /DRepsnApp", '"/DRepsnApp=$PWD\\build\\installer\\re-psn-app"' in yml)
check("installer.yml: no new write scope", re.search(r"^permissions:\n  contents: read\n", yml, re.M) is not None and "write" not in yml.split("jobs:")[0].split("permissions:")[1])

portable = (root / "installer/tools/assemble_portable.py").read_text(encoding="utf-8")
required = portable[portable.index("REQUIRED = ("):]
required = required[:required.index(")\n") + 1]
check("assemble_portable.py: REQUIRED names no re-psn path", "re-psn" not in required and "repsn" not in required.lower())
check("assemble_portable.py: never copies the app folder", "re-psn-app" not in portable and "RepsnApp" not in portable)
payload_src = (root / "installer/tools/assemble_payload.py").read_text(encoding="utf-8")
notice_str = payload_src[payload_src.index('NOTICE = """'):]
notice_str = notice_str[:notice_str.index('"""', 12) + 3]
check("assemble_payload.py: the app is named only in the NOTICE.txt text", payload_src.count("re-psn-app") == notice_str.count("re-psn-app") == 1)
check("assemble_payload.py: NOTICE.txt points at the app folder", FOLDER + "/" in notice_str and "LICENSE (MIT)" in notice_str)

notice = (root / "NOTICE.md").read_text(encoding="utf-8")
sec = notice[notice.index("## re-psn desktop app"):]
sec = sec[:sec.index("\n## ", 3)]
for w in ("Copyright (c) 2026 sp00nz", "Copyright (c) 2026 undecuple", "MIT", "Bubble Tea", "Lip Gloss", "charmbracelet/x/term",
          "golang.org/x/sync", "golang.org/x/sys", "BSD-3-Clause", "Go standard library", FOLDER, "LICENSE", "NOTICE.md"):
    check("NOTICE.md: re-psn section names %r" % w, w in sec)
# re-psn-window.exe (Rust/Tauri) is not shipped, so no text may still disclose or name it as shipped
for w in ("re-psn-window", "Tauri", "Rust", "Cargo.lock", "crates.io", "MPL-2.0"):
    check("NOTICE.md: re-psn section no longer mentions %r" % w, w not in sec)
lic = e.get("license", "")
check("toolchain.json: licence names the Go programs only",
      "re-psn-app.exe, re-psn-host.exe" in lic and not any(w in lic for w in ("re-psn-window", "Rust", "Tauri", "Cargo.lock", "MPL-2.0")))
check("assemble_payload.py: NOTICE.txt names no window program, Rust, Tauri or Cargo.lock",
      not any(w in notice_str for w in ("re-psn-window", "Rust", "Tauri", "Cargo.lock")))
check("jsp.iss and the installer README say the window is left out", "without its window program" in iss and "deliberately left out" in
      (root / "installer/README.md").read_text(encoding="utf-8"))

# ---- the Go modules' licence texts (re-psn v0.9.0: app/go.mod and go.sum, modules built into the two Go programs)
rows, bad = sra.load_table()
check("licences: the real table loads", rows and not bad)
EXPECT = {
    "Go standard library": "go1.27.4", "golang.org/x/sync": "v0.22.0", "golang.org/x/sys": "v0.47.0",
    "charm.land/bubbletea/v2": "v2.1.1", "charm.land/lipgloss/v2": "v2.0.6", "github.com/charmbracelet/x/term": "v0.2.2",
    "github.com/charmbracelet/x/ansi": "v0.11.9",
    "github.com/charmbracelet/x/windows": "v0.2.2", "github.com/charmbracelet/colorprofile": "v0.4.3",
    "github.com/charmbracelet/ultraviolet": "v0.0.0-20261008173134-6b8d4baf91b4", "github.com/clipperhouse/displaywidth": "v0.11.0",
    "github.com/clipperhouse/uax29/v2": "v2.7.0", "github.com/lucasb-eyer/go-colorful": "v1.4.1", "github.com/mattn/go-runewidth": "v0.0.30",
    "github.com/muesli/cancelreader": "v0.2.2", "github.com/rivo/uniseg": "v0.4.7", "github.com/xo/terminfo": "v1.0.0"}
check("licences: the table has exactly the pinned modules at their versions", {x["module"]: x["version"] for x in rows} == EXPECT and len(rows) == len(EXPECT))
check("licences: every module has a licence id, a copyright line, an https URL naming its tag or commit and a plain file name",
      all(x["licence"] in ("MIT", "BSD-3-Clause") and "Copyright" in x["copyright"] and x["url"].startswith("https://raw.githubusercontent.com/")
          and x["file"].endswith("-LICENSE.txt") and "/" not in x["file"] for x in rows))
check("licences: names of the cached texts are unique", len({x["file"] for x in rows}) == len(rows))

notice_all = (root / "NOTICE.md").read_text(encoding="utf-8")
secn = notice_all[notice_all.index("## re-psn desktop app"):]
secn = secn[:secn.index("\n## ", 3)]
check("NOTICE.md: re-psn section points to THIRD-PARTY-LICENSES.txt and the table",
      "THIRD-PARTY-LICENSES.txt" in secn and "repsn_app_licenses.json" in secn)
check("NOTICE.md: every module, version and copyright line is listed",
      all(x["module"] in secn and x["version"] in secn and x["copyright"] in secn for x in rows))
check("toolchain.json: the notice no longer says the folder is copied unchanged", "unchanged" not in e.get("notice", "")
      and "THIRD-PARTY-LICENSES.txt" in e.get("notice", ""))

# the release zip and its files are never in git
try:
    tracked = subprocess.run(["git", "-C", str(root), "ls-files"], capture_output=True, text=True, timeout=60).stdout.splitlines()
except (OSError, subprocess.SubprocessError):
    tracked = None
if tracked is None or not tracked:
    print("note: not a git checkout; the tracked-files check is skipped")
else:
    check("git: no re-psn zip or exe tracked",
          not any(re.search(r"re-psn.*\.(zip|exe)$|re-psn-app-v", p) for p in tracked))

print("RESULT:", "ok" if not fails else "FAILED (%d)" % len(fails))
sys.exit(1 if fails else 0)
