#!/usr/bin/env python3
"""Host test for support_bundle.py: a synthetic install tree in a temp folder, no game data, no network.
    python3 -I installer/tools/support_bundle_test.py
"""
import os
import pathlib
import sys
import tempfile
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import support_bundle as sb  # noqa: E402

FAILS = []
USERS = "C:\\" + "Users"      # built from parts: a literal user path would trip the repo checks
USERS_FWD = "c:/" + "users"


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        FAILS.append(what)


def w(p, text="x\n"):
    p = pathlib.Path(p)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(text.encode() if isinstance(text, str) else text)


def main():
    os.environ["USERPROFILE"] = USERS + "\\tester42"
    with tempfile.TemporaryDirectory() as t:
        t = pathlib.Path(t)
        app, out, ad, tmp = t / "app", t / "out", t / "appdata", t / "temp"
        w(app / "logs/firstrun-20260101-000000.log", f"opened {USERS}\\tester42\\Desktop\\JSP and {USERS_FWD}/TESTER42/x\nGPU: Test Adapter 9000\n")
        w(app / "build/configure.log", "cfg\n")
        w(app / "build/analysis/lift.log", "lift\n")
        w(app / "build/analysis/out.c", "int lifted;\n")
        w(app / "build/markers/lift.done", "fp1 {}\n")
        w(app / "build/markers/spu.done", "fp1 {}\n")
        w(app / "game/run/launcher_log.txt", "hello\n")
        w(app / "game/run/EBOOT.elf", "ELF")
        w(app / "game/run/shaders/cache.log", "cache\n")
        w(app / "game/JStarsRecomp.exe", b"MZ" + b"0" * 10)
        w(app / "hdd0/home/00000001/savedata/SLOT.sav", "save")
        w(app / "hdd0/notes.txt", "in a saves folder\n")
        w(app / "jsp-install.ini", f"[install]\neboot={USERS}\\tester42\\EBOOT.elf\n")
        w(app / "build-again.cmd", "@echo off\n")
        w(app / "VERSION.txt", "0.18.0-test\n")
        w(app / "EBOOT.log", "named like an EBOOT\n")
        w(app / "root.sav", "save")
        w(ad / sb.APPDIR / "play_settings.txt", "FULLSCREEN=0\n")
        w(tmp / "Setup Log 2026-01-01 #001.txt", "Log opened\nAppDir: C:\\Program Files\\JSP Fan Revival\n")
        w(tmp / "Setup Log 2026-01-01 #002.txt", "an unrelated installer\n")
        # no tree.txt, no paths.txt, no settings_here.txt: optional files that are absent

        zp = sb.make(app, out, ad, tmp)
        check(zp.parent == out and zp.name.startswith("jsp-support-") and zp.suffix == ".zip", "zip named jsp-support-*.zip in --out")
        z = zipfile.ZipFile(zp)
        names = set(z.namelist())
        want = {"facts.txt", "logs/firstrun-20260101-000000.log", "build/configure.log", "build/analysis/lift.log", "game/run/launcher_log.txt",
                "jsp-install.ini", "build-again.cmd", "VERSION.txt", "appdata/play_settings.txt", "setup-logs/Setup Log 2026-01-01 #001.txt"}
        check(names == want, f"zip lists the expected names (extra {sorted(names - want)}, missing {sorted(want - names)})")
        alltext = b"".join(z.read(n) for n in names).decode()
        check("tester42" not in alltext.lower(), "the user name is redacted everywhere")
        check("%USERPROFILE%" in z.read("logs/firstrun-20260101-000000.log").decode(), "the profile path became %USERPROFILE%")
        body = b"".join(z.read(n) for n in names if n != "facts.txt").decode()
        for bad in ("ELF", "SLOT", "root.sav", "cache.log", "out.c", "in a saves folder", "unrelated", "named like an EBOOT"):
            check(bad not in body and not any(bad in n for n in names), f"nothing of {bad!r} in the zip")
        f = z.read("facts.txt").decode()
        check("EBOOT.elf (EBOOT)" in f or "game/run/EBOOT.elf (" in f, "facts.txt names the refused EBOOT")
        check("root.sav" in f and "SLOT.sav" not in body, "facts.txt names a refused save; the hdd0 folder is never entered")
        check("VERSION.txt: 0.18.0-test" in f and "tree.txt: absent" in f, "facts: version, and tree.txt absent is fine")
        check("markers: lift, spu" in f, "facts: step markers")
        check("JStarsRecomp.exe: present, 12 bytes" in f, "facts: game exe size")
        cur = t / "app-current"                  # the current tree (tree.txt v18) places jsp.exe, not JStarsRecomp.exe
        w(cur / "tree.txt", "v18\n")
        w(cur / "game/jsp.exe", b"MZ" + b"0" * 6)
        fc = sb.facts(cur, {}, [], [], [], [])
        check("jsp.exe: present, 8 bytes" in fc and "JStarsRecomp" not in fc, "facts: the current tree's exe is jsp.exe")
        check("GPU: Test Adapter 9000" in f, "facts: GPU name from a log line")
        g = sb.gpu_name({"a.log": b"[spu] gpu sync: 3 waits\n", "game/run/play_stdout.txt": b"[D3D12] adapter: Test GPU (VRAM 8 MB)\r\n"})
        check(g == "Test GPU (VRAM 8 MB) (from game/run/play_stdout.txt)", "GPU: the [D3D12] adapter line wins over a looser match")
        check("bundled Python: " in f and "CPUs: " in f and "free space" in f and "Windows: " in f, "facts: system lines")

        # the cap: two big logs, the oldest lines of the largest go first, with a note
        w(app / "logs/firstrun-20260102-000000.log", "".join(f"old line {i}\n" for i in range(50000)) + "NEWEST LINE\n")
        w(app / "build/big.log", "".join(f"big {i}\n" for i in range(20000)) + "BIG NEWEST\n")
        zp2 = sb.make(app, out, ad, tmp, cap=300000)
        z2 = zipfile.ZipFile(zp2)
        tot = sum(i.file_size for i in z2.infolist())
        check(tot <= 300000, f"total stays under the cap ({tot} bytes)")
        big = z2.read("logs/firstrun-20260102-000000.log").decode()
        check(big.startswith("[support_bundle: the oldest ") and big.rstrip().endswith("NEWEST LINE") and "old line 0\n" not in big, "oldest lines cut, newest kept, note on top")
        check("cut" in z2.read("facts.txt").decode().split("shortened:")[1].split("\n")[0], "facts.txt says what was cut")
        check(z2.read("VERSION.txt") == b"0.18.0-test\n", "small files are untouched by the cap")

        # a missing --out folder is made; a non-existent install folder is an error exit
        check(sb.main(["--root", str(t / "nope")]) == 2, "missing install folder: exit 2")
    print("FAILED: " + "; ".join(FAILS) if FAILS else "all passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
