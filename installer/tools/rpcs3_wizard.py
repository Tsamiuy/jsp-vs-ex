#!/usr/bin/env python3
"""Wizard helper for "Use your RPCS3 install?": finds the player's RPCS3 folders and game, and imports a save (Windows or Linux, Python 3.11+).

jsp.iss runs this with the bundled Python, before anything is installed. It is a thin layer over rpcs3_locate.py (vendored unchanged, read
only except for the one save copy below). Real paths go only into the --ini file that the wizard reads into its fields; stdout, with
--json --redacted, is the form that may be logged.

    rpcs3_wizard.py find [--rpcs3 DIR] --ini FILE [--json] [--redacted]
        pre-fill: disc folder, dev_flash folder, an already decrypted EBOOT.elf, and whether a save exists
    rpcs3_wizard.py save-plan --rpcs3 DIR --hdd0 DIR --ini FILE --list FILE [--json] [--redacted]
        dry run of the save import: writes the list (copies, replaces, stale) for the page and a plan hash
    rpcs3_wizard.py save-copy --rpcs3 DIR --hdd0 DIR --plan HASH [--json] [--redacted]
        copies the save, but only when the plan computed now has the hash that save-plan showed (else nothing is written)

Exit codes: 0 done, 1 nothing to do (not found / no save), 2 bad arguments, 3 the plan changed since it was shown, 4 copy failed.
The save's PARAM.SFO is copied as a file and never read here, and its content (it can hold the PS3 account id) is never printed.
"""
import argparse
import hashlib
import json
import os
import shutil
import stat
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rpcs3_locate as L  # noqa: E402

# ---- J-Stars block: the values that differ per game (shared installer profile: titles, data_dirs) ----
TITLES = {"BLUS31519": None, "BLES02136": None}   # disc only: whether to use game update 1.02 is a separate question
SAVE_PREFIX = "NPUB31647-B0"                     # the game's save folder name in RPCS3 and in this port
SAVE_USER = "00000001"                           # the port's single user (ps3recomp keeps saves in hdd0/home/00000001/savedata)
GAME_NAME = "J-Stars Victory VS+"
# ---- end of the J-Stars block ----

# What the wizard says for each problem code: plain words, no paths, no module text.
SAY = {
    "not_found": "No RPCS3 folder was found. Enter the folder of your RPCS3 install and click Find, or fill in the next pages yourself.",
    "not_rpcs3": "That folder does not look like an RPCS3 install (no rpcs3.exe and no config folder). Pick the folder that holds rpcs3.exe, "
                 "or fill in the next pages yourself.",
    "no_titles": "Nothing to look for.",
    "missing_disc": "RPCS3 does not list " + GAME_NAME + " (BLUS31519 or BLES02136) with a game folder that exists, so the disc folder was not filled in.",
    "no_decrypted_eboot": "No decrypted EBOOT.elf was found beside the game. Setup needs one from your own copy: see RPCS3 and its documentation.",
    "missing_flash": "RPCS3's dev_flash folder was not found, so that page was not filled in.",
    "wrong_version": "RPCS3 lists a different version of the game than this port supports.",
}


def say(code):
    return SAY.get(code, "RPCS3 reported a problem (" + code + ") with this game.")


def pick(loc):
    """The first title that has a disc, else None."""
    for tid in TITLES:
        t = loc["titles"].get(tid)
        if t and t["disc"].get("ok"):
            return tid, t
    return None, None


def find(rpcs3):
    """-> (fields, problems, loc): fields hold real paths (for the ini only), problems are codes."""
    loc = L.locate(rpcs3, dict(TITLES))
    f = {"found": 0, "root": "", "title_id": "", "disc": "", "devflash": "", "eboot": "", "saves": 0}
    probs = []
    if not loc["ok"]:
        return f, [loc.get("code") or "not_found"], loc
    f["found"] = 1
    f["root"] = loc["root"]
    tid, t = pick(loc)
    if t is None:
        probs.append("missing_disc")
    else:
        f["title_id"] = tid
        f["disc"] = t["disc"]["disc_dir"]
        if t["decrypted_eboot"].get("ok"):
            f["eboot"] = t["decrypted_eboot"]["path"]
        else:
            probs.append("no_decrypted_eboot")
        probs += [p["code"] for p in t["problems"] if p["code"] not in ("missing_disc", "no_decrypted_eboot", "missing_update")]
    f["saves"] = len(chosen_saves(loc["root"]))   # the save has its own title id (NPUB...), so it is looked up by prefix
    fl = loc.get("dev_flash")
    if fl and os.path.isdir(os.path.join(fl, "sys")):
        f["devflash"] = fl
    else:
        probs.append("missing_flash")
    return f, probs, loc


def write_ini(path, section, fields, problems=()):
    lines = [f"[{section}]"] + [f"{k}={v}" for k, v in fields.items()]
    lines.append(f"problems={len(problems)}")
    lines += [f"problem{i + 1}={say(c)}" for i, c in enumerate(problems)]
    Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")


def emit(a, obj, roots):
    """stdout: JSON, with paths redacted when asked (the form that may be logged)."""
    if a.redacted:
        obj = L._redact_obj(obj, roots)
    json.dump(obj, sys.stdout, indent=2)
    print()


def chosen_saves(rpcs3):
    """-> list of save entries to import: those of the player's user 00000001 when it has any, else of the newest user. [] when none."""
    r = L.saves(rpcs3, SAVE_PREFIX)
    users = r.get("users", {})
    if not users:
        return []
    uid = SAVE_USER if SAVE_USER in users else max(users, key=lambda u: max(e["mtime"] for e in users[u]))
    return users[uid]


def plan(rpcs3, hdd0):
    """-> (dry runs, hash, error). overwrite=True only so that a save that is already in the port is listed as 'replaces'."""
    runs = []
    for e in chosen_saves(rpcs3):
        dst = Path(hdd0) / "home" / SAVE_USER / "savedata" / e["name"]
        r = L.copy_save(e["path"], dst, overwrite=True, dry_run=True)
        if not r["ok"]:
            return [], "", r
        err = destination_error(dst, r["files"])
        if err:
            return [], "", err
        runs.append(r)
    h = hashlib.sha256()
    for r in runs:
        h.update(json.dumps([r["src"], r["dst"], r["files"], r["replaces"], r["stale"]], sort_keys=True).encode())
        for f in r["files"]:   # a changed file with the same size must change the plan too
            h.update(L.sha256(os.path.join(r["src"], f["rel"])).get("sha256", "").encode())
    return runs, h.hexdigest()[:16], None


def destination_error(dst, files):
    """Refuse redirected destinations before the preview and again before copying."""
    d = Path(dst)
    # The shared helper checks lexical paths; also check resolved paths so an
    # alias of an RPCS3 folder cannot make the installer write into RPCS3.
    try:
        resolved = L._nc(d.resolve())
        for root, hdd0 in L._ROOTS:
            if any(L._under(resolved, L._nc(Path(p).resolve())) is not None for p in (root, hdd0)):
                return {"ok": False, "code": "dst_in_rpcs3"}
    except (OSError, RuntimeError):
        return {"ok": False, "code": "bad_dst"}
    paths = {d}
    for f in files:
        p = d / f["rel"]
        paths.add(p)
        paths.update(parent for parent in p.parents if parent == d or d in parent.parents)
    for p in paths:
        try:
            s = p.lstat()
        except FileNotFoundError:
            continue
        except OSError:
            return {"ok": False, "code": "bad_dst"}
        if stat.S_ISLNK(s.st_mode) or getattr(s, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            return {"ok": False, "code": "bad_dst"}
        is_file = p != d and any(p == d / f["rel"] for f in files)
        if not (stat.S_ISREG(s.st_mode) if is_file else stat.S_ISDIR(s.st_mode)):
            return {"ok": False, "code": "bad_dst"}
    return None


def import_save(run):
    """Stage all source bytes, then replace files with rollback on an I/O error.

    Keep stale destination files and never read existing destination contents.
    A failed or interrupted transaction retains staging for manual recovery.
    Stop signals propagate; imports require the game/emulator to be closed.
    """
    d = Path(run["dst"])
    err = destination_error(d, run["files"])
    if err:
        return err
    stage = None
    changed, created = [], []
    keep = False
    try:
        d.parent.mkdir(parents=True, exist_ok=True)
        stage = Path(tempfile.mkdtemp(prefix=".jsp-save-import-", dir=d.parent))
        staged = L.copy_save(run["src"], stage / "new", dry_run=False)
        if not staged["ok"]:
            return {"ok": False, "code": "copy_failed", "copied": 0}
        err = destination_error(d, staged["files"])
        if err:
            return err
        for f in staged["files"]:
            t = d / f["rel"]
            missing = []
            parent = t.parent
            while not parent.exists():
                missing.append(parent)
                parent = parent.parent
            t.parent.mkdir(parents=True, exist_ok=True)
            created.extend(reversed(missing))
            old = None
            if t.exists():
                old = stage / "old" / f["rel"]
                old.parent.mkdir(parents=True, exist_ok=True)
            # Until commit or rollback finishes, cleanup could delete the only
            # originals if an interruption bypasses the OSError handler.
            keep = True
            if old is not None:
                os.replace(t, old)
            changed.append((t, old))
            os.replace(stage / "new" / f["rel"], t)
        keep = False
        return {"ok": True, "copied": len(staged["files"])}
    except OSError:
        try:
            for t, old in reversed(changed):
                if old is not None:
                    os.replace(old, t)
                else:
                    t.unlink(missing_ok=True)
            for p in reversed(created):
                p.rmdir()
        except OSError:
            keep = True
            return {"ok": False, "code": "rollback_failed", "recovery": str(stage), "copied": 0}
        keep = False
        return {"ok": False, "code": "copy_failed", "copied": 0}
    finally:
        if stage is not None and not keep:
            shutil.rmtree(stage, ignore_errors=True)


def summary(runs):
    return {"saves": [{"name": Path(r["dst"]).name, "copies": len(r["files"]), "replaces": len(r["replaces"]),
                       "stale": len(r["stale"]), "skipped": len(r["skipped"])} for r in runs]}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("cmd", choices=["find", "save-plan", "save-copy"])
    ap.add_argument("--rpcs3")
    ap.add_argument("--hdd0")
    ap.add_argument("--ini")
    ap.add_argument("--list")
    ap.add_argument("--plan")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--redacted", action="store_true")
    a = ap.parse_args(argv)
    roots = L.find_rpcs3(a.rpcs3)
    if a.cmd == "find":
        if not a.ini:
            ap.error("find needs --ini")
        f, probs, loc = find(a.rpcs3)
        write_ini(a.ini, "rpcs3", f, probs)
        out = dict(f)
        if a.redacted:   # a game folder outside RPCS3's own folders is not covered by redact(): say only that it was found
            out["disc"] = "<disc folder>" if f["disc"] else ""
            out["eboot"] = "<EBOOT.elf>" if f["eboot"] else ""
            out["devflash"] = "<dev_flash folder>" if f["devflash"] else ""
        out["problems"] = probs
        emit(a, out, roots)
        return 0 if f["found"] else 1
    if not (a.rpcs3 and a.hdd0):
        ap.error(a.cmd + " needs --rpcs3 and --hdd0")
    runs, h, err = plan(a.rpcs3, a.hdd0)
    if err:
        emit(a, {"ok": False, "code": err.get("code")}, roots)
        return 4
    if not runs:
        emit(a, {"ok": False, "code": "no_save", "saves": []}, roots)
        return 1
    if a.cmd == "save-plan":
        if not (a.ini and a.list):
            ap.error("save-plan needs --ini and --list")
        text = []
        for r in runs:
            text.append("Save " + Path(r["dst"]).name)
            # One line per file: a file that is already in the game's save folder is listed as REPLACES only, not also as copy.
            rep = set(r["replaces"])
            text += [("  REPLACES " + f["rel"] + f"  ({f['size']} bytes; already in the game's save folder, the old file is overwritten)")
                     if f["rel"] in rep else ("  copy     " + f["rel"] + f"  ({f['size']} bytes)") for f in r["files"]]
            text += ["  kept     " + x + "  (only in the game's save folder; left as it is)" for x in r["stale"]]
            text += ["  skipped  " + x for x in r["skipped"]]
        Path(a.list).write_text("\r\n".join(text) + "\r\n", encoding="utf-8-sig")
        s = summary(runs)["saves"]
        write_ini(a.ini, "save", {"plan": h, "count": len(runs), "copies": sum(x["copies"] for x in s),
                                  "replaces": sum(x["replaces"] for x in s), "stale": sum(x["stale"] for x in s)})
        emit(a, {"ok": True, "plan": h, **summary(runs)}, roots)
        return 0
    if h != a.plan:
        emit(a, {"ok": False, "code": "plan_changed"}, roots)
        return 3
    copied = 0
    for r in runs:
        c = import_save(r)
        if not c["ok"]:
            out = {"ok": False, "code": c.get("code"), "copied": copied}
            if c.get("recovery"):
                out["recovery"] = c["recovery"]
            emit(a, out, roots)
            return 4
        copied += c["copied"]
    emit(a, {"ok": True, "copied": copied, "saves": len(runs)}, roots)
    return 0


if __name__ == "__main__":
    sys.exit(main())
