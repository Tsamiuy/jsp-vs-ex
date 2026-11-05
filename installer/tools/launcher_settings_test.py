#!/usr/bin/env python3
"""Checks the order in which jsp-launcher.exe picks its settings file (see the header of installer/launcher/launcher.c). Windows, or
Wine/Proton with a Windows Python. Never touches the power plan (CPU_BOOST=0 is set for the launcher).

    python launcher_settings_test.py JSP-LAUNCHER.exe FAKE_TOOL.exe SCRATCH_DIR

FAKE_TOOL.exe (fake_tool.c) stands in for the game: it writes the value of RES_SCALE it was started with. Each file in a case sets
RES_SCALE to its own letter, so the letter says which file won; launcher_log.txt must name the rule.
"""
import os
import pathlib
import shutil
import subprocess
import sys
import time


def case(tool, launcher, base, name, files, flag=False, want="", rule=""):
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
        places[k].write_text(f"RES_SCALE={letter}\n" if k != "marker" else "")
    rec = base / name / "rec.txt"
    env = dict(os.environ, APPDATA=str(appdata), CPU_BOOST="0", FAKE_OUT=str(rec), FAKE_ENV="RES_SCALE")
    cmd = [str(inst / "jsp-launcher.exe")] + (["--settings", str(places["flag"])] if flag else [])
    subprocess.run(cmd, env=env, cwd=str(inst))
    for _ in range(100):
        if rec.is_file() and b"ENV" in rec.read_bytes():
            break
        time.sleep(0.1)
    got = rec.read_text(errors="replace").split("RES_SCALE=")[-1].strip() if rec.is_file() else "(game not started)"
    log = (inst / "game" / "run" / "launcher_log.txt").read_text().strip() if (inst / "game" / "run" / "launcher_log.txt").is_file() else "(no log)"
    ok = got == want and rule in log
    print(f"{'ok  ' if ok else 'FAIL'} {name:26} RES_SCALE={got!r} want {want!r}; {log}")
    return ok


def main(launcher, tool, scratch):
    base = pathlib.Path(scratch).resolve()
    r = [case(tool, launcher, base, "none", {}, want="", rule="none found"),
         case(tool, launcher, base, "appdata-only", {"appdata": "A"}, want="A", rule="AppData"),
         case(tool, launcher, base, "install-beats-appdata", {"install": "I", "appdata": "A"}, want="I", rule="install folder"),
         case(tool, launcher, base, "marker-ignores-appdata", {"marker": "M", "appdata": "A"}, want="", rule="marker file"),
         case(tool, launcher, base, "marker-uses-install", {"marker": "M", "install": "I", "appdata": "A"}, want="I", rule="marker file"),
         case(tool, launcher, base, "flag-beats-all", {"marker": "M", "install": "I", "appdata": "A", "flag": "F"}, flag=True, want="F", rule="command line"),
         case(tool, launcher, base, "flag-file-missing", {"install": "I"}, flag=True, want="", rule="not found")]
    print("RESULT:", "ok" if all(r) else "FAILED")
    return 0 if all(r) else 1


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:4]))
