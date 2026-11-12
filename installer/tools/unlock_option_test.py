#!/usr/bin/env python3
"""Source checks for the opt-in "Unlock everything" option (no compiler needed; runs on Linux or Windows).

    python unlock_option_test.py [REPO_ROOT]

The C# launcher needs Windows' csc.exe to build, so this checks the rules the option must keep in the source:
the backup runs before the start and a failed backup returns without starting; the box is unticked after use; the
settings file never carries the switch and never turns it on; the game gets JSP_UNLOCK_ALL=1 only for that start;
the port registers the hook only when the variable is set. Also that the C++ register call and CMake line exist.

Installer task and separate test save (ENHANCEMENT): the "unlock" task is unchecked by default with an exclusive test/normal
choice (test is the default), the choice is stored only in jsp-install.ini (unlock=, savemode=) and never unlocks a normal save by
itself; the launcher keeps a second save set by pointing PS3_GAMEDATA_ROOT at <hdd0>_gamedata_test (the game's savedata and trophy
root, set after the settings file), seeds it from the normal save without writing to the normal side, asks before it unlocks a
normal save, and shows which save is active.
"""
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2])
cs = (root / "launcher/JStarsLauncher19.cs").read_text(encoding="utf-8")
cpp = (root / "installer/port/jsp/jsp_unlock.cpp").read_text(encoding="utf-8")
stubs = (root / "installer/port/jsp/stubs.cpp").read_text(encoding="utf-8")
iss = (root / "installer/jsp.iss").read_text(encoding="utf-8")
mainc = (root / "installer/port/jsp/main.cpp").read_text(encoding="utf-8")
cmake = (root / "installer/port/jsp/CMakeLists.txt").read_text(encoding="utf-8")
fails = []


def before(text, a, b):
    """True when both strings are in text and a comes first."""
    return a in text and b in text and text.index(a) < text.index(b)


def check(name, ok):
    print(("ok   " if ok else "FAIL ") + name)
    if not ok:
        fails.append(name)


start = cs[cs.index("void StartGame()"):cs.index("void RunGame()")]
save = cs[cs.index("void SaveSettings()"):cs.index("void StartGame()")]
run = cs[cs.index("void RunGame()"):]
# Changed on purpose (installer task): the box starts unticked unless the installer's "Unlock everything" task wrote unlock=1 into
# jsp-install.ini, and a normal save is then still asked about at Start Game (checked below). Nothing else may tick it.
check("box starts unticked unless the installer's ini key says so",
      re.search(r'unlockBox = new CheckBox \{[^}]*Checked = IniValue\("unlock"\) == "1" \}', cs) is not None and cs.count("unlockBox.Checked = true") == 0)
check("backup comes before the thread start", start.index("BackupSave()") < start.index("new Thread(RunGame)"))
check("failed backup returns before the start", re.search(r"if \(why != null\) \{[^}]*return;", start) is not None)
check("box unticks after use", "unlockBox.Checked = false" in start)
check("switch not written to settings", "JSP_UNLOCK_ALL" not in save and "unlockBox" not in save)
check("settings file value removed before launch",
      run.index('Remove("JSP_UNLOCK_ALL")') < run.index('["JSP_UNLOCK_ALL"] = "1"'))
check("set to 1 only when ticked", re.search(r'if \(unlockThisRun\) \{ psi\.EnvironmentVariables\["JSP_UNLOCK_ALL"\] = "1"', run) is not None)
check("backup name is dated and outside the game's save prefix", '"BACKUP-" + Path.GetFileName(src) + "-" + stamp' in cs)
check("port hook only when set", re.search(r'getenv\("JSP_UNLOCK_ALL"\);\s*if \(!e \|\| !\*e \|\| e\[0\] == \'0\'\) return;', cpp) is not None)
check("backup refuses when the game ran before and no save folder is found", re.search(r"if \(!any && GameRanBefore\(\)\)\s*return \"[^;]*SaveFolders\(\)", cs) is not None)
check("run-before signal is the game's own data (_gamedata or -INSTALL)", '"_gamedata"' in cs and '"*-INSTALL"' in cs)
check("US and EU save folder names", '"NPUB31647-B0"' in cs and '"NPEB02204-B0"' in cs)
check("hook address is in the source", "0x0014E898u" in cpp)
check("register call in stubs.cpp", "jsp_register_unlock_overrides();" in stubs)
check("source in CMake", "jsp_unlock.cpp" in cmake)

# ---- installer task and ini keys ----
def task(name):
    m = re.search(r'^Name: "' + re.escape(name) + r'";[^\n]*', iss, re.M)
    return m.group(0) if m else ""


check("installer: unlock task exists and is unchecked", "unchecked" in task("unlock") and "Flags:" in task("unlock"))
check("installer: test and normal are exclusive children of unlock", "exclusive" in task("unlock\\testsave") and "exclusive" in task("unlock\\normalsave"))
check("installer: test save is the default choice, normal save is not", "unchecked" not in task("unlock\\testsave") and "unchecked" in task("unlock\\normalsave"))
check("installer: ini unlock=1 only with the task, 0 otherwise",
      'Key: "unlock"; String: "1"; Tasks: unlock\n' in iss + "\n" and 'Key: "unlock"; String: "0"; Tasks: not unlock\n' in iss + "\n")
check("installer: savemode=test only for the test-save choice, normal for everything else",
      'Key: "savemode"; String: "test"; Tasks: unlock\\testsave' in iss and 'Key: "savemode"; String: "normal"; Tasks: not unlock\\testsave' in iss)
check("installer: nothing writes the cheat switch or unlocks a save itself", "JSP_UNLOCK_ALL" not in iss and "jsp_unlock" not in iss)

# ---- test save: which folder the game gets ----
check("test save root is a sibling of the normal root (<hdd0>_gamedata_test)",
      'TestSuffix = "_gamedata_test"' in cs and '(test ? TestSuffix : "_gamedata")' in cs)
check("port default save root is <hdd0>_gamedata and an explicit PS3_GAMEDATA_ROOT wins",
      'snprintf(gd, sizeof gd, "%.*s_gamedata"' in mainc and 'getenv("PS3_GAMEDATA_ROOT") && *getenv("PS3_GAMEDATA_ROOT")' in mainc)
check("PS3_GAMEDATA_ROOT is set only for the test save, after the settings file lines",
      re.search(r'if \(testSaveThisRun\) \{ psi\.EnvironmentVariables\["PS3_GAMEDATA_ROOT"\] = SaveRoot\(true\)', run) is not None
      and before(run, 'foreach (string line in File.ReadAllLines(settingsFile))', '["PS3_GAMEDATA_ROOT"]'))
check("settings file is not written with the save choice", "PS3_GAMEDATA_ROOT" not in save and "savemode" not in save)
check("seeding copies normal to test only, and never deletes under the normal root",
      "CopyTree(src, Path.Combine(SaveRoot(true), rel))" in cs and "Directory.Delete(SaveRoot(false)" not in cs
      and "if (Directory.Exists(SaveRoot(true))) return null;" in cs)
check("test start seeds before anything else and refuses when seeding fails",
      before(start, "SeedTestSave()", "BackupSave()") and re.search(r"if \(seed != null\) \{[^}]*return;", start) is not None)
check("backup in test mode covers the test save folders only", re.search(r"SaveFolders\(\) \{ return SaveFolders\(testSaveThisRun\); \}", cs) is not None
      and "if (!test) l.Add(Path.Combine(hdd0" in cs)
check("a normal save is only unlocked after a Yes (default No) and before the backup",
      before(start, "MessageBoxDefaultButton.Button2", "BackupSave()")
      and "unlockBox.Checked && !testSave" in start)
check("box is pre-ticked only from ini unlock=1; the test/normal choice is stored as savemode only",
      'Checked = IniValue("unlock") == "1"' in cs and '"savemode=" + value' in cs and 'IniValue("savemode") == "test"' in cs)
check("the launcher never writes unlock= to the ini", '"unlock=' not in cs)
check("save in use cannot change while the game runs", "rbSaveNormal.Enabled = rbSaveTest.Enabled = false" in start and "if (gameRunning) { rbSaveTest.Checked = testSave" in cs)
check("active save is shown in the title and on the tab", '"J-Stars Victory VS+  [TEST SAVE]"' in cs and "ACTIVE: TEST save" in cs and "ACTIVE: normal save" in cs)
check("the log names the save in use", 'Log("Save in use: TEST save' in run and 'Log("Save in use: normal save.")' in run)
print("RESULT:", "ok" if not fails else "FAILED")
sys.exit(1 if fails else 0)
