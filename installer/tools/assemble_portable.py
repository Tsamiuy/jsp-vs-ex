#!/usr/bin/env python3
"""Pack the installer payload into the portable zip: jsp-<version>-portable.zip.

Layout adapted from the installer of undecuple's Killzone 2 port, Project Pyrrhus (same MIT licence): one top folder, sorted entries,
fixed dates, Deflate 9, so the same payload gives the same bytes with the same zlib (ASSUMPTION: another zlib build may
compress differently; the listed SHA-256 of each file stays the same). Same payload as the installer (assemble_payload.py:
port sources, patched toolkit, bundled Windows Python, launcher, licences), plus three files written here (support-bundle.cmd and support_bundle.py come with the payload):

  run.cmd            (CRLF) the entry: bundled Python on firstrun.py --interactive --portable
  jsp-install.ini    [install] portable=1: saves (hdd0) and settings stay in the extracted folder
  README-portable.txt  what to do, in the fan site's plain ASCII style

    python installer/tools/assemble_portable.py [--payload build/installer/payload] [--out build/installer/out] [--top NAME]

Refuses (exit 1) when the payload holds a file of a kind the game ships (.elf .bin .sfo .psarc .edat .self .sprx), when it lacks the bundled Python, firstrun.py, the launcher
or the licence texts, or when it already holds first-run output (build, hdd0, game, paths.txt, logs). Writes <zip>.sha256
beside the zip. Nothing is downloaded and nothing game-derived goes in. Exit 0 = written.
"""
import argparse
import hashlib
import pathlib
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from assemble_payload import FORBIDDEN  # noqa: E402

FIXED_DATE = (1980, 1, 1, 0, 0, 0)
REQUIRED = ("firstrun.py", "support_bundle.py", "support-bundle.cmd", "jsp-launcher.exe", "toolchain/python/python.exe", "toolchain/llvm-mingw/bin/clang.exe", "toolchain/cmake/bin/cmake.exe", "toolchain/ninja/ninja.exe", "cmake/llvm-mingw.cmake", "VERSION.txt", "NOTICE.txt",
            "licenses/jstars-port-LICENSE.txt", "licenses/ps3recomp-LICENSE.txt", "port/CMakeLists.txt",
            "ps3recomp/tools/ppu_lifter.py")
FIRST_RUN_OUTPUT = ("build", "hdd0", "game", "logs", "paths.txt", "jsp-install.ini", "play_settings.txt", "run.cmd")

RUN_CMD = """@echo off
rem J-Stars Victory VS-EX: Fan Revival, portable zip: first run.
rem Asks for your disc folder and your decrypted EBOOT.elf, checks them and builds the game here
rem from your own copy with the bundled compiler (nothing to install). Saves, settings, logs and the
rem build stay in this folder; the folder may be moved or renamed afterwards. Start it again to resume. Nothing is
rem downloaded and no server is contacted.
setlocal
cd /d "%~dp0"
set "PY=%~dp0toolchain\\python\\python.exe"
if not exist "%PY%" (
  echo The bundled Python is missing: %PY%
  echo Unpack the whole zip again, keeping its folders.
  if not defined JSP_NOPAUSE pause
  exit /b 2
)
"%PY%" -X utf8 "%~dp0firstrun.py" --interactive --portable --root "%~dp0." %*
set "RC=%ERRORLEVEL%"
echo.
if not defined JSP_NOPAUSE pause
exit /b %RC%
"""

README = """J-Stars Victory VS-EX: Fan Revival, portable
============================================
An unofficial fan project. Nothing of the game is included or downloaded: the port is built on your PC from your own copy.
This program contacts no server: no update check, no crash report, no telemetry. The one exception is Online Battle
(experimental, v18 tree only), which is off unless you turn it on and give a re-psn server (see the installer README).

1. Unpack the whole zip to a folder you can write to (not Program Files). Keep the folders as they are.
2. Double-click run.cmd. It asks for:
     - the folder of your game disc (the one that holds PS3_GAME)
     - your decrypted EBOOT.elf, from your own copy: see RPCS3 and its documentation
   and then builds the game here. (An EBOOT.elf the port does not know is refused; to try one at your own risk, start run.cmd --allow-unknown-eboot: unsupported.) This takes a long time; if it stops, run.cmd again: finished steps are kept.
3. Start the game with jsp-settings.exe (settings: resolution, window, controls; then Start Game), or directly with
   jsp-launcher.exe. The first start installs the game data from your disc (a few minutes).

Everything stays in this folder: build, logs (logs\\firstrun.log), saves (hdd0) and settings (jsp-install.ini,
play_settings.txt). You can move or rename the folder after the first run. To remove the game, delete the folder.

If something goes wrong, double-click support-bundle.cmd: it makes one zip of the logs and settings (user name removed) on your
Desktop. Look inside, then attach it to an issue. Nothing is sent anywhere.
"""


def crlf(text):
    return text.replace("\r\n", "\n").replace("\n", "\r\n").encode("ascii")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--payload", default=str(ROOT / "build/installer/payload"))
    ap.add_argument("--out", default=str(ROOT / "build/installer/out"))
    ap.add_argument("--top", default="JSP Fan Revival", help="top folder inside the zip")
    a = ap.parse_args(argv)
    pay, out = pathlib.Path(a.payload), pathlib.Path(a.out)
    tree = (pay / "tree.txt").read_text().split()[0] if (pay / "tree.txt").is_file() else "v15"   # which port assemble_payload.py staged
    spu_tool = "tools/lift_jobs.py" if tree == "v18" else "tools/jsp_spu.py"
    problems = [f"missing from the payload: {r}" for r in (*REQUIRED, spu_tool, "tree.txt") if not (pay / r).is_file() and not (r == "tree.txt" and tree == "v15")]
    problems += [f"first-run output in the payload: {n}" for n in FIRST_RUN_OUTPUT if (pay / n).exists()]
    entries = {}
    for p in sorted(pay.rglob("*")):
        rel = p.relative_to(pay).as_posix()
        if p.is_symlink():
            problems.append(f"{rel}: symlink")
        elif p.is_file():
            if p.suffix.lower() in FORBIDDEN:
                problems.append(f"{rel}: a file kind the game ships")
            elif "__pycache__" not in rel:
                entries[rel] = p
    folded = {}
    for r in entries:
        folded.setdefault(r.lower(), []).append(r)
    problems += [f"equal under case folding: {v}" for v in folded.values() if len(v) > 1]
    if problems:
        print("refused:\n  " + "\n  ".join(problems[:20]), file=sys.stderr)
        return 1
    version = (pay / "VERSION.txt").read_text().strip()
    out.mkdir(parents=True, exist_ok=True)
    zp = out / f"jsp-{version}-portable.zip"
    extra = {"run.cmd": crlf(RUN_CMD), "jsp-install.ini": crlf("[install]\nportable=1\n"), "README-portable.txt": crlf(README)}
    bad = [n for n in extra if n in entries]
    if bad:
        print(f"refused: the payload already holds {bad}", file=sys.stderr)
        return 1
    names = sorted(list(entries) + list(extra))
    with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for n in names:
            zi = zipfile.ZipInfo(f"{a.top}/{n}", FIXED_DATE)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = 0x20            # MS-DOS archive attribute
            zi.create_system = 0
            z.writestr(zi, extra[n] if n in extra else entries[n].read_bytes(), zipfile.ZIP_DEFLATED, 9)
    h = hashlib.sha256(zp.read_bytes()).hexdigest()
    (out / (zp.name + ".sha256")).write_text(f"{h}  {zp.name}\n")
    print(f"{zp}: {len(names)} entries, {zp.stat().st_size / 1e6:.1f} MB, sha256 {h[:16]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
