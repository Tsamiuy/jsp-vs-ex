#!/usr/bin/env python3
"""Host test of the portable path of firstrun.py (any POSIX host with Python 3, CPU only, no compiler): the interactive questions with scripted
answers, jsp-install.ini, paths stored relative (@APP@), the folder moved or renamed after the first run, a stale CMake cache,
the zip from assemble_portable.py. With DISC and EBOOT (the real ones, read only) it also runs `--interactive --until spu`
in a copy, renames the folder and runs again (must skip lift and spu).

    python3 -I installer/tools/portable_selftest.py SCRATCH_DIR [PAYLOAD_DIR [DISC_FOLDER EBOOT.elf]]

PAYLOAD_DIR (assemble_payload.py --out) is needed for the real-input part and the zip checks; without it only the synthetic
checks run. The synthetic disc folder and ELF stand-in come from make_fixture.py next to it.
"""
import io
import pathlib
import shutil
import subprocess
import sys
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import firstrun  # noqa: E402

ok = True


def check(name, cond, extra=""):
    global ok
    ok &= bool(cond)
    print(f"{'ok  ' if cond else 'FAIL'} {name} {extra}")


def scripted(answers):
    it = iter(answers)

    def ask(prompt=""):
        try:
            return next(it)
        except StopIteration:
            raise EOFError
    return ask


def run_main(argv, answers=()):
    out = io.StringIO()
    old = sys.stdout
    sys.stdout = out
    try:
        rc = firstrun.main(argv, ask=scripted(answers))
    finally:
        sys.stdout = old
        if firstrun._log:
            firstrun._log.close()
            firstrun._log = None
    return rc, out.getvalue()


def main(scratch, payload=None, disc=None, eboot=None):
    scratch = pathlib.Path(scratch)
    if scratch.exists():
        shutil.rmtree(scratch)
    scratch.mkdir(parents=True)
    subprocess.run([sys.executable, "-I", str(HERE / "make_fixture.py"), str(scratch / "fx")], check=True)
    fx = scratch / "fx"
    with open(fx / "EBOOT.elf", "ab") as f:   # the fixture's 64-byte stand-in is under the E2 size floor: pad it with zeros (still an unknown build for E3)
        f.write(b"\0" * (firstrun.ELF_MIN_BYTES + 1024 - 64))
    # --- path helpers
    root = pathlib.Path("/x/JSP")
    check("pack inside", firstrun.pack_path("/x/JSP/hdd0", root) == "@APP@/hdd0")
    check("pack folder itself", firstrun.pack_path("/x/JSP", root) == "@APP@")
    check("pack outside/sibling", firstrun.pack_path("/x/JSP2/hdd0", root) == "/x/JSP2/hdd0" and
          firstrun.pack_path("/y/disc", root) == "/y/disc")
    check("unpack", firstrun.unpack_path("@APP@/hdd0", pathlib.Path("/z/Renamed")) == "/z/Renamed/hdd0")
    check("disc root accepts PS3_GAME", firstrun.disc_root(fx / "disc" / "PS3_GAME") == fx / "disc")
    # the fixture's EBOOT.elf is not a known build: without --allow-unknown-eboot the portable flow refuses it (E3), with it only warns
    app0 = scratch / "app0"
    app0.mkdir()
    rc, text = run_main(["--interactive", "--portable", "--root", str(app0), "--until", "checks"], ["", str(fx / "disc"), str(fx / "EBOOT.elf")])
    check("portable flow: unknown EBOOT without the flag is refused (E3)", rc != 0 and "not a build this port knows" in text
          and "--allow-unknown-eboot" in text, f"rc={rc}")
    rc, text = run_main(["--interactive", "--portable", "--allow-unknown-eboot", "--root", str(app0), "--until", "checks"],
                        ["", str(fx / "disc"), str(fx / "EBOOT.elf")])
    check("portable flow: unknown EBOOT with --allow-unknown-eboot goes on with an UNSUPPORTED warning", rc == 0 and "UNSUPPORTED" in text, f"rc={rc}")
    # --- interactive: wrong answers first, then good ones (until checks: no compiler needed)
    app = scratch / "app1"
    app.mkdir()
    ans = ["", str(fx), str(fx / "disc" / "PS3_GAME"),                    # empty (nothing saved yet), no PS3_GAME, then good
           str(fx / "EBOOT.BIN"), str(fx / "EBOOT.elf")]                    # encrypted, then good (no dev_flash question)
    rc, text = run_main(["--interactive", "--portable", "--root", str(app), "--allow-unknown-eboot", "--until", "checks"], ans)
    check("interactive accepts after wrong answers", rc == 0 and "RESULT: ok, inputs accepted" in text, f"rc={rc}")
    check("wrong answers explained", "PARAM.SFO is missing" in text and "encrypted EBOOT.BIN" in text)
    check("no dev_flash question", "dev_flash" not in text)
    check("wording: own copy, see RPCS3 and its documentation", "see RPCS3 and its documentation" in text)
    ini = (app / "jsp-install.ini").read_text()
    check("ini keeps the answers and portable=1", "portable=1" in ini and "title_id=BLUS31519" in ini and str(fx) in ini, ini.replace("\n", "|")[:120])
    # second start: Enter keeps the saved answers
    rc, text = run_main(["--interactive", "--root", str(app), "--allow-unknown-eboot", "--until", "checks"], ["", "", ""])
    check("saved answers kept with Enter (portable read from ini)", rc == 0 and "portable=1" in text, f"rc={rc}")
    # --answers FILE (scripted runs, for scripted runs where a pipe is not forwarded)
    (scratch / "ans.txt").write_text(f"\n\n\n")
    rc, text = run_main(["--interactive", "--root", str(app), "--allow-unknown-eboot", "--until", "checks", "--answers", str(scratch / "ans.txt")])
    check("--answers file: Enter keeps the saved answers", rc == 0 and "RESULT: ok, inputs accepted" in text, f"rc={rc}")
    # q and end of input stop without changing anything more
    rc, _ = run_main(["--interactive", "--root", str(app), "--until", "checks"], ["q"])
    check("q stops with rc 3", rc == 3)
    rc, _ = run_main(["--interactive", "--root", str(app), "--until", "checks"], [])
    check("end of input stops with rc 3", rc == 3)
    # no compiler question: the bundled toolchain is checked (T1-T3), nothing is asked about Visual Studio
    rc, text = run_main(["--interactive", "--root", str(app), "--allow-unknown-eboot", "--until", "build", "--toolchain", str(scratch / "none")], ["", "", ""])
    check("no Visual Studio question; missing bundled toolchain fails T1-T3", rc == 1 and "Visual Studio" not in text and text.count("FAIL T") >= 3)
    # --- non-interactive needs disc and eboot
    try:
        run_main(["--root", str(app)])
        check("non-interactive without --disc is refused", False)
    except SystemExit as e:
        check("non-interactive without --disc is refused", e.code == 2)
    # --- data folder
    check("data_dir portable = the folder", firstrun.data_dir(app, True) == app)
    # --- place + move/rename: paths.txt relative, markers still current, CMake cache of the old place dropped
    old = scratch / "old" / "JSP Fan Revival"
    old.mkdir(parents=True)
    ns = firstrun.argparse.Namespace(root=str(old), disc=str(fx / "disc"), eboot=str(fx / "EBOOT.elf"), devflash=str(fx / "dev_flash"),
                                     hdd0=str(old / "hdd0"), force=[])
    (old / "build" / "port").mkdir(parents=True)
    (old / "build" / "port" / firstrun.EXE).write_bytes(b"MZ")
    c = firstrun.Ctx(ns, "US", "BLUS31519")
    check("place step", firstrun.step_place(c))
    pt = (old / "paths.txt").read_text()
    check("paths.txt: hdd0 relative, disc outside absolute",
          "PS3_HDD0_ROOT=@APP@/hdd0" in pt and f"PS3_VFS_ROOT={fx}/disc/" in pt.replace("\\", "/"), pt.splitlines()[1])
    (old / "build" / "port" / "CMakeCache.txt").write_text(f"CMAKE_HOME_DIRECTORY:INTERNAL={old.as_posix()}/port\n")
    (old / "build" / "location.txt").write_text(str(old) + "\n")
    c.mark("place", "k")
    new = scratch / "new" / "Renamed Folder"
    new.parent.mkdir(parents=True)
    old.rename(new)
    c2 = firstrun.Ctx(firstrun.argparse.Namespace(**{**vars(ns), "root": str(new), "hdd0": str(new / "hdd0")}), "US", "BLUS31519")
    check("marker still current after the move", c2.done("place", "k"))
    check("paths.txt unchanged and relative after the move", (new / "paths.txt").read_text() == pt)
    check("stale CMake cache dropped after the move", firstrun.drop_stale_cache(c2) and not (new / "build" / "port").exists())
    check("cache of this folder kept", not firstrun.drop_stale_cache(c2))
    (new / "build" / "port").mkdir()
    (new / "build" / "port" / "CMakeCache.txt").write_text(f"CMAKE_HOME_DIRECTORY:INTERNAL={new.as_posix()}/port\n")
    check("a cache made for this folder is kept", not firstrun.drop_stale_cache(c2) and (new / "build" / "port").exists())
    # --- the zip
    if payload:
        pay = pathlib.Path(payload)
        r = subprocess.run([sys.executable, "-I", str(HERE / "assemble_portable.py"), "--payload", str(pay), "--out", str(scratch / "zip")],
                           capture_output=True, text=True)
        check("assemble_portable runs", r.returncode == 0, r.stdout.strip()[-100:] + r.stderr.strip()[-200:])
        zs = sorted((scratch / "zip").glob("jsp-*-portable.zip"))
        if zs:
            z = zipfile.ZipFile(zs[0])
            names = z.namelist()
            top = names[0].split("/")[0]
            rc = z.read(f"{top}/run.cmd")
            check("run.cmd is CRLF and runs firstrun --interactive --portable", rc.count(b"\n") == rc.count(b"\r\n") and
                  b"--interactive --portable" in rc and b"toolchain\\python\\python.exe" in rc)
            check("zip: entry, ini, launcher, bundled Python, no game kinds", all(f"{top}/{n}" in names for n in
                  ("run.cmd", "support-bundle.cmd", "support_bundle.py", "jsp-install.ini", "jsp-launcher.exe", "firstrun.py", "toolchain/python/python.exe", "README-portable.txt")) and
                  not any(n.lower().endswith(tuple(".elf .bin .sfo .psarc .edat .self .sprx".split())) for n in names))
            check("zip: ini says portable=1", b"portable=1" in z.read(f"{top}/jsp-install.ini"))
            check("zip: no first-run output or private path", not any(n.split("/")[1:2] in (["build"], ["hdd0"], ["game"]) for n in names) and
                  b"/home/" not in z.read(f"{top}/README-portable.txt"))
            r2 = subprocess.run([sys.executable, "-I", str(HERE / "assemble_portable.py"), "--payload", str(pay), "--out", str(scratch / "zip2")],
                                capture_output=True, text=True)
            check("zip is reproducible", (next((scratch / "zip2").glob("*.zip")).read_bytes() == zs[0].read_bytes()) if r2.returncode == 0 else False)
            # a payload with first-run output is refused
            bad = scratch / "badpayload"
            shutil.copytree(pay, bad, symlinks=True, ignore=shutil.ignore_patterns("ps3recomp", "toolchain", "port"))
            (bad / "hdd0").mkdir()
            r3 = subprocess.run([sys.executable, "-I", str(HERE / "assemble_portable.py"), "--payload", str(bad), "--out", str(scratch / "zip3")],
                                capture_output=True, text=True)
            check("payload with first-run output / missing parts is refused", r3.returncode == 1 and "first-run output" in r3.stderr)
    # --- real inputs: --interactive --until spu in a copy, rename, run again
    if payload and disc and eboot:
        a1 = scratch / "real" / "JSP A"
        shutil.copytree(payload, a1, symlinks=True, ignore=shutil.ignore_patterns("toolchain"))
        rc, text = run_main(["--interactive", "--portable", "--root", str(a1), "--until", "spu"], [disc, eboot])
        check("real: steps up to spu done", rc == 0 and "RESULT: ok, steps up to spu done" in text, text.strip().splitlines()[-1])
        n = len(list((a1 / "build" / "spu").glob("*/spu_recomp.c")))
        check("real: 18 SPU job images", n == 18, str(n))
        b1 = scratch / "real" / "JSP B renamed"
        a1.rename(b1)
        rc, text = run_main(["--interactive", "--root", str(b1), "--until", "spu"], ["", "", ""])
        check("real: after the rename, answers kept and lift/spu skipped", rc == 0 and text.count("already done, skipped") == 2 and
              "moved or renamed" in text, text.strip().splitlines()[-1])
    print("RESULT:", "ok" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:5]))
