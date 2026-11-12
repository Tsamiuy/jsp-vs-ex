#!/usr/bin/env python3
"""Tests of steam_shortcut.py on fixture files built here (no real Steam data, no Windows, no Steam). Plain Python 3:

    python3 -I installer/tools/steam_shortcut_test.py
"""
import os
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import steam_shortcut as S  # noqa: E402

fails = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        fails.append(what)


OTHER = S.make_entry(0, r"C:\Games\Other\other.exe", r"C:\Games\Other", "", name="Other Game")
OTHER2 = S.make_entry(1, r"C:\Games\Two\two.exe", r"C:\Games\Two", "", name="Second")
FIX_TWO = b"\x00shortcuts\x00" + OTHER + OTHER2 + b"\x08\x08"
EXE, DIR, ICO = r"C:\Apps\JSP\jsp-launcher.exe", r"C:\Apps\JSP", r"C:\Apps\JSP\jsp-launcher.ico"

# parse round trip
ents, at = S.parse_shortcuts(FIX_TWO)
check(len(ents) == 2 and S._field(ents[0][2], b"appname") == b"Other Game", "parse: two entries, names read")
check(FIX_TWO[at:] == b"\x08\x08", "parse: end offset is the shortcuts end byte")
ents, at = S.parse_shortcuts(S.FRESH)
check(ents == [] and at == len(S.FRESH) - 2, "parse: empty shortcuts map")

# append keeps old bytes, adds index 2
new = S.append_entry(FIX_TWO, EXE, DIR, ICO)
check(new.startswith(FIX_TWO[:at]) and new.endswith(b"\x08\x08") and len(new) > len(FIX_TWO), "append: old entries byte for byte, same ending")
e2, _ = S.parse_shortcuts(new)
check(len(e2) == 3 and e2[2][1] == b"2", "append: new entry has index 2")
f = e2[2][2]
check(S._field(f, b"appname") == S.APP_NAME.encode() and S._field(f, b"exe") == b'"' + EXE.encode() + b'"'
      and S._field(f, b"startdir") == b'"' + DIR.encode() + b'"' and S._field(f, b"icon") == ICO.encode(), "append: AppName, Exe, StartDir, icon")
check(S._field(f, b"appid") >= 0x80000000, "append: appid has the non-Steam bit")
check(S.append_entry(new, EXE, DIR, ICO) is None, "append twice: second is a no-op (None)")
check(S.append_entry(S.FRESH, EXE, DIR, ICO) == b"\x00shortcuts\x00" + S.make_entry(0, EXE, DIR, ICO) + b"\x08\x08", "append: empty map gets index 0")
check(S.parse_shortcuts(new)[0][0][2] == S.parse_shortcuts(FIX_TWO)[0][0][2], "round trip: first entry parses the same")

# bad files
for name, bad in (("empty", b""), ("garbage", b"hello world"), ("truncated", FIX_TWO[:-3]), ("trailing byte", FIX_TWO + b"\0"),
                  ("unknown type", b"\x00shortcuts\x00\x07x\0\x08\x08"), ("no end", b"\x00shortcuts\x00")):
    try:
        S.append_entry(bad, EXE, DIR, ICO)
        check(False, "refuse: " + name)
    except S.VdfError:
        check(True, "refuse: " + name)

# whole-run tests on a fake Steam tree
msgs = []
with tempfile.TemporaryDirectory() as t:
    t = pathlib.Path(t)
    app = t / "app"
    app.mkdir()
    (app / "jsp-launcher.exe").write_bytes(b"x")
    steam = t / "steam"
    for uid, content in (("1001", FIX_TWO), ("1002", None), ("1003", b"not a vdf")):
        (steam / "userdata" / uid / "config").mkdir(parents=True)
        if content is not None:
            (steam / "userdata" / uid / "config" / "shortcuts.vdf").write_bytes(content)
    (steam / "userdata" / "0").mkdir()
    (steam / "userdata" / "notanid").mkdir()
    vdf = lambda u: steam / "userdata" / u / "config" / "shortcuts.vdf"  # noqa: E731

    rc = S.run(app, steam, running=lambda: True, out=msgs.append)
    check(rc == 1 and vdf("1001").read_bytes() == FIX_TWO and not vdf("1002").exists() and vdf("1003").read_bytes() == b"not a vdf"
          and not list(steam.rglob("*.jsp-*")), "Steam running: nothing touched, no backup")
    rc = S.run(app, steam, dry=True, running=lambda: False, out=msgs.append)
    check(rc == 1 and vdf("1001").read_bytes() == FIX_TWO, "dry run: no write")
    rc = S.run(app, steam, running=lambda: False, out=msgs.append)
    check(rc == 1, "run: exit 1 because account 1003 was refused")
    check(vdf("1003").read_bytes() == b"not a vdf" and len(list(vdf("1003").parent.glob("*.jsp-*"))) == 0, "bad file: left alone, no backup")
    b1 = list(vdf("1001").parent.glob("shortcuts.vdf.jsp-backup-*"))
    check(len(b1) == 1 and b1[0].read_bytes() == FIX_TWO, "backup of the old file is identical")
    check(vdf("1001").read_bytes().startswith(FIX_TWO[:at]) and len(S.parse_shortcuts(vdf("1001").read_bytes())[0]) == 3, "account 1001: appended, old bytes kept")
    check(S.parse_shortcuts(vdf("1002").read_bytes())[0][0][1] == b"0" and not list(vdf("1002").parent.glob("*.jsp-backup-*")), "account 1002: new file, nothing to back up")
    after = {u: vdf(u).read_bytes() for u in ("1001", "1002", "1003")}
    S.run(app, steam, running=lambda: False, out=msgs.append)
    check({u: vdf(u).read_bytes() for u in after} == after and len(list(vdf("1001").parent.glob("*.jsp-backup-*"))) == 1, "second run: files and backups unchanged (idempotent)")
    check(S._field(S.parse_shortcuts(vdf("1002").read_bytes())[0][0][2], b"exe") == ('"%s"' % (app / "jsp-launcher.exe")).encode(), "exe: launcher used when no settings exe")
    check(not list(steam.rglob("*.jsp-tmp")), "no temp file left")
    (t / "empty").mkdir()
    check(S.run(app, t / "empty", running=lambda: False, out=msgs.append) == 2, "no userdata: exit 2")

print("%d failed" % len(fails) if fails else "all passed")
sys.exit(1 if fails else 0)
