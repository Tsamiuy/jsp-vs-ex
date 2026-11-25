#!/usr/bin/env python3
"""Source checks for the settings window's "Two-page Main Menu and 3v3" box (no compiler needed; runs on Linux or Windows).

    python menu_option_test.py [REPO_ROOT]

The window needs Windows to build, so this checks the rules the box must keep in the source: it starts ticked (the menu is on by
default); unticking writes MENU_PAGES=0 and ticking writes nothing; the file is read with the game's rule (only the value 0 means
off); the key is one the window owns (not kept twice among the other lines); a change saves at once. It also checks that the game
treats only MENU_PAGES=0 as off (feat_menu_pages.cpp mp_on) and that 3v3 follows the menu (feat_3v3.cpp g_six_defaults).
"""
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2])
cs = (root / "launcher/JStarsLauncher.cs").read_text(encoding="utf-8")
menu = (root / "installer/port/jsp/feat_menu_pages.cpp").read_text(encoding="utf-8")
six = (root / "installer/port/jsp/feat_3v3.cpp").read_text(encoding="utf-8")
fails = []


def check(name, ok):
    print(("ok   " if ok else "FAIL ") + name)
    if not ok:
        fails.append(name)


save = cs[cs.index("void SaveSettings()"):cs.index("void StartGame()")]
check("box starts ticked", re.search(r"menuBox = new CheckBox \{[^}]*Checked = true \}", cs) is not None)
check("a change of the box saves at once", re.search(r"menuBox\.CheckedChanged \+= delegate \{ SaveIfReady\(\); \};", cs) is not None)
check("unticked writes MENU_PAGES=0", re.search(r'if \(!menuBox\.Checked\) lines\.Add\("MENU_PAGES=0"\);', save) is not None)
check("ticked writes nothing (no MENU_PAGES=1 anywhere)", "MENU_PAGES=1" not in cs and save.count("MENU_PAGES") == 1)
check("read: off only for the value 0",
      re.search(r'menuBox\.Checked = !\(cur\.TryGetValue\("MENU_PAGES", out s\) && s == "0"\);', cs) is not None)
check("the key is owned by the window (not kept twice in the other lines)", 'k == "MENU_PAGES"' in cs)
check("game: the menu is off only for MENU_PAGES=0 (and on the EU disc, where it is US only: jsp_region.h)",
      re.search(r'bool mp_on\(\)\s*\{\s*static const bool on = \[\] \{ const char\* e = std::getenv\("MENU_PAGES"\); if \(e && e\[0\] == \'0\'\) return false;\s*if \(!jsp_game_is_us\(\)\) return false;', menu) is not None)
check("game: 3v3 is off with MENU_PAGES=0 or SIX=0 and on otherwise",
      re.search(r"if \(\(mp && mp\[0\] == '0'\) \|\| \(off && off\[0\] == '0'\)\) return false;", six) is not None)
if fails:
    print("%d check(s) failed" % len(fails))
    sys.exit(1)
print("menu option: all checks passed")
