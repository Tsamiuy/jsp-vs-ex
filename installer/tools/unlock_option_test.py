#!/usr/bin/env python3
"""Source checks for the opt-in "Unlock everything" option (no compiler needed; runs on Linux or Windows).

    python unlock_option_test.py [REPO_ROOT]

The C# launcher needs Windows' csc.exe to build, so this checks the rules the option must keep in the source:
the backup runs before the start and a failed backup returns without starting; the box is unticked after use; the
settings file never carries the switch and never turns it on; the game gets JSP_UNLOCK_ALL=1 only for that start;
the port registers the hook only when the variable is set. Also that the C++ register call and CMake line exist.
"""
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2])
cs = (root / "launcher/JStarsLauncher19.cs").read_text(encoding="utf-8")
cpp = (root / "installer/port/jsp/jsp_unlock.cpp").read_text(encoding="utf-8")
stubs = (root / "installer/port/jsp/stubs.cpp").read_text(encoding="utf-8")
cmake = (root / "installer/port/jsp/CMakeLists.txt").read_text(encoding="utf-8")
fails = []


def check(name, ok):
    print(("ok   " if ok else "FAIL ") + name)
    if not ok:
        fails.append(name)


start = cs[cs.index("void StartGame()"):cs.index("void RunGame()")]
save = cs[cs.index("void SaveSettings()"):cs.index("void StartGame()")]
run = cs[cs.index("void RunGame()"):]
check("box starts unticked", re.search(r"unlockBox = new CheckBox \{[^}]*Checked = false", cs) is not None)
check("backup comes before the thread start", start.index("BackupSave()") < start.index("new Thread(RunGame)"))
check("failed backup returns before the start", re.search(r"if \(why != null\) \{[^}]*return;", start) is not None)
check("box unticks after use", "unlockBox.Checked = false" in start)
check("switch not written to settings", "JSP_UNLOCK_ALL" not in save and "unlockBox" not in save)
check("settings file value removed before launch",
      run.index('Remove("JSP_UNLOCK_ALL")') < run.index('["JSP_UNLOCK_ALL"] = "1"'))
check("set to 1 only when ticked", re.search(r'if \(unlockThisRun\) \{ psi\.EnvironmentVariables\["JSP_UNLOCK_ALL"\] = "1"', run) is not None)
check("backup name is dated and outside the game's save prefix", '"BACKUP-" + SaveName + "-" + stamp' in cs)
check("port hook only when set", re.search(r'getenv\("JSP_UNLOCK_ALL"\);\s*if \(!e \|\| !\*e \|\| e\[0\] == \'0\'\) return;', cpp) is not None)
check("hook address is in the source", "0x0014E898u" in cpp)
check("register call in stubs.cpp", "jsp_register_unlock_overrides();" in stubs)
check("source in CMake", "jsp_unlock.cpp" in cmake)
print("RESULT:", "ok" if not fails else "FAILED")
sys.exit(1 if fails else 0)
