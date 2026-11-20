#!/usr/bin/env python3
"""Checks the order in which jsp-launcher.exe picks its settings file (see the header of installer/launcher/launcher.c). Windows, or
Wine/Proton with a Windows Python. Never touches the power plan (CPU_BOOST=0 is set for the launcher).

    python launcher_settings_test.py JSP-LAUNCHER.exe FAKE_TOOL.exe SCRATCH_DIR

FAKE_TOOL.exe (fake_tool.c) stands in for the game: it writes the value of RES_SCALE it was started with. Each file in a case sets
RES_SCALE to its own letter, so the letter says which file won; launcher_log.txt must name the rule.
The mods cases check where the launcher looks for mods.txt (next to the settings file that won, else the install folder), the
JSP_MODS_LIST / JSP_MODS_DIR it gives the game, and its "mods: N listed (<path>)" line in launcher_log.txt.
"""
import os
import pathlib
import shutil
import subprocess
import sys
import time


def env_of(rec):
    """The fake game's ENV line as a dict (NAME=value, tab separated)."""
    out = {}
    for line in rec.read_text(errors="replace").splitlines() if rec.is_file() else []:
        if line.startswith("ENV"):
            for kv in line.split("\t")[1:]:
                k, _, v = kv.partition("=")
                out[k] = v
    return out


def case(tool, launcher, base, name, files, flag=False, want="", rule="", mods=None, mods_dir=False, mods_want=None, extra=""):
    inst = base / name / "app"
    appdata = base / name / "appdata"
    shutil.rmtree(base / name, ignore_errors=True)
    (inst / "game" / "run").mkdir(parents=True)
    (base / name / "disc" / "PS3_GAME").mkdir(parents=True)
    (appdata / "JSP Fan Revival").mkdir(parents=True)
    shutil.copy2(launcher, inst / "jsp-launcher.exe")
    shutil.copy2(tool, inst / "game" / "game.exe")
    (inst / "game" / "EBOOT.elf").write_bytes(b"x")
    (inst / "paths.txt").write_text(f"PS3_VFS_ROOT={(base / name / 'disc').as_posix()}/\nEXE=game.exe\nELF=EBOOT.elf\n")
    places = {"install": inst / "play_settings.txt", "appdata": appdata / "JSP Fan Revival" / "play_settings.txt",
              "marker": inst / "settings_here.txt", "flag": base / name / "one-run.txt"}
    for k, letter in files.items():
        places[k].write_text(f"RES_SCALE={letter}\n{extra}" if k != "marker" else "")
    for k, text in (mods or {}).items():   # mods.txt next to a settings place ("install" = the install folder)
        (places[k].parent / "mods.txt").write_text(text, encoding="utf-8")
    if mods_dir:
        (inst / "mods").mkdir()
    rec = base / name / "rec.txt"
    env = dict(os.environ, APPDATA=str(appdata), CPU_BOOST="0", FAKE_OUT=str(rec), FAKE_ENV="RES_SCALE,JSP_MODS_LIST,JSP_MODS_DIR")
    for k in ("JSP_MODS", "JSP_MODS_LIST", "JSP_MODS_DIR"):
        env.pop(k, None)
    cmd = [str(inst / "jsp-launcher.exe")] + (["--settings", str(places["flag"])] if flag else [])
    subprocess.run(cmd, env=env, cwd=str(inst))
    for _ in range(100):
        if rec.is_file() and b"ENV" in rec.read_bytes():
            break
        time.sleep(0.1)
    genv = env_of(rec)
    got = genv.get("RES_SCALE", "(game not started)")
    log = (inst / "game" / "run" / "launcher_log.txt").read_text().strip() if (inst / "game" / "run" / "launcher_log.txt").is_file() else "(no log)"
    ok = got == want and rule in log
    if mods_want is not None:   # (where mods.txt is or None, the count, the log line's tail)
        place, count, tail = mods_want
        listed = "" if place is None else str((places[place].parent / "mods.txt") if place != "install" else inst / "mods.txt")
        mods_line = [x for x in log.splitlines() if x.startswith("mods: ")]
        ok = ok and len(mods_line) == 1 and mods_line[0].startswith(f"mods: {count} ") and mods_line[0].endswith(tail)
        ok = ok and genv.get("JSP_MODS_LIST", "") == listed and genv.get("JSP_MODS_DIR", "") == (str(inst / "mods") if mods_dir and count != "off" else "")
        if listed:
            ok = ok and listed in mods_line[0]
    print(f"{'ok  ' if ok else 'FAIL'} {name:26} RES_SCALE={got!r} want {want!r}; {log}; mods env {genv.get('JSP_MODS_LIST', '')!r} {genv.get('JSP_MODS_DIR', '')!r}")
    return ok


def main(launcher, tool, scratch):
    base = pathlib.Path(scratch).resolve()
    r = [case(tool, launcher, base, "none", {}, want="", rule="none found"),
         case(tool, launcher, base, "appdata-only", {"appdata": "A"}, want="A", rule="AppData"),
         case(tool, launcher, base, "install-beats-appdata", {"install": "I", "appdata": "A"}, want="I", rule="install folder"),
         case(tool, launcher, base, "marker-ignores-appdata", {"marker": "M", "appdata": "A"}, want="", rule="marker file"),
         case(tool, launcher, base, "marker-uses-install", {"marker": "M", "install": "I", "appdata": "A"}, want="I", rule="marker file"),
         case(tool, launcher, base, "flag-beats-all", {"marker": "M", "install": "I", "appdata": "A", "flag": "F"}, flag=True, want="F", rule="command line"),
         case(tool, launcher, base, "flag-file-missing", {"install": "I"}, flag=True, want="", rule="not found"),
         # mods.txt: no list anywhere -> 0 listed, nothing passed
         case(tool, launcher, base, "mods-none", {}, rule="none found", mods_want=(None, 0, "mods.txt, not found)")),
         # no settings file: the install folder's mods.txt (ASSUMPTION P8), the mods folder passed when it exists; comments and blanks not counted
         case(tool, launcher, base, "mods-install", {}, rule="none found", mods={"install": "\ufeffswap\n# off-for-now\n\n  glogo  # the logo\n"},
              mods_dir=True, mods_want=("install", 2, "mods.txt)")),
         # the AppData settings file won: mods.txt next to it, never the install folder's
         case(tool, launcher, base, "mods-next-to-appdata", {"appdata": "A"}, want="A", rule="AppData", mods={"appdata": "one\n", "install": "x\ny\n"},
              mods_want=("appdata", 1, "mods.txt)")),
         case(tool, launcher, base, "mods-appdata-has-none", {"appdata": "A"}, want="A", rule="AppData", mods={"install": "x\n"},
              mods_want=(None, 0, "mods.txt, not found)")),
         # JSP_MODS=0 in the settings file: off, nothing passed
         case(tool, launcher, base, "mods-off", {"install": "I"}, want="I", rule="install folder", mods={"install": "swap\n"}, mods_dir=True,
              extra="JSP_MODS=0\n", mods_want=(None, "off", "(JSP_MODS=0)"))]
    print("RESULT:", "ok" if all(r) else "FAILED")
    return 0 if all(r) else 1


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:4]))
