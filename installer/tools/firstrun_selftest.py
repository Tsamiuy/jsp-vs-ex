#!/usr/bin/env python3
"""Host test of firstrun.py up to the compile (any OS with Python 3; needs a real disc folder and EBOOT.elf): resume markers, determinism of the lift, SPU fingerprints,
the generated build-port.cmd. It does not run the compiler; a fake toolchain of do-nothing scripts stands in for the check (POSIX hosts).

    python3 -I installer/tools/firstrun_selftest.py APP_ROOT DISC_FOLDER EBOOT.elf SCRATCH_DIR
    python3 -I installer/tools/firstrun_selftest.py --units      (no game data: step fingerprints, path refusals, log rotation)

APP_ROOT is an assembled payload (assemble_payload.py --out), fresh or already run through `firstrun.py --until spu`; SCRATCH_DIR gets a
second lift (other worker count) for the byte comparison.
"""
import hashlib
import os
import pathlib
import subprocess
import sys

# From docs/notes/FINDINGS.md: the dispatch fingerprints of three known jobs.
KNOWN_FP = ("37F7DF42BD146C53", "411E7CDFC589113B", "01E73637D84700F4")


def h(p):
    return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()


def tree(p):
    p = pathlib.Path(p)
    return {str(f.relative_to(p)): h(f) for f in sorted(p.rglob("*")) if f.is_file() and f.suffix in (".cpp", ".h", ".c")}


def units():
    """Cases that need no disc and no EBOOT: stub steps, fake files. Returns 0 when all pass."""
    import argparse
    import tempfile
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    import firstrun
    ok = True

    def check(name, cond, extra=""):
        nonlocal ok
        ok &= bool(cond)
        print(f"{'ok  ' if cond else 'FAIL'} {name} {extra}")

    tmp = pathlib.Path(tempfile.mkdtemp(prefix="jspunits"))
    root = tmp / "app"
    for rel, txt in (("ps3recomp/tools/ppu_lifter.py", "a"), ("ps3recomp/src/x.c", "x"), ("port/main.cpp", "m"), ("tools/jsp_spu.py", "j")):
        (root / rel).parent.mkdir(parents=True, exist_ok=True)
        (root / rel).write_text(txt)
    # which tree a payload holds: tree.txt, none = v15; the v18 tree has its own exe name and its own SPU tool in the step fingerprint
    check("payload_tree: no file is v15", firstrun.payload_tree(root) == "v15")
    (root / "tree.txt").write_text("v18\n")
    check("payload_tree: v18", firstrun.payload_tree(root) == "v18")
    (root / "tree.txt").write_text("nonsense\n")
    check("payload_tree: unknown value is v15", firstrun.payload_tree(root) == "v15")
    (root / "tree.txt").unlink()
    eboot = tmp / "EBOOT.elf"
    eboot.write_bytes(b"\x7fELF" + b"1")
    vs = tmp / "tc"
    for f in firstrun.toolchain_paths(vs).values():
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_bytes(b"clang1")
    # a missing EBOOT.elf on a read-only disc folder gets the hint; on a writable one the plain line (no chmod effect for root: skipped)
    wd = tmp / "wdisc"
    (wd / "PS3_GAME" / "USRDIR").mkdir(parents=True)
    plain = firstrun.check_eboot(str(tmp / "none.elf"), str(wd))[0][2]
    check("E1 missing, writable disc: plain line", "read-only" not in plain)
    check("E1 missing, writable disc: no probe file left behind", not list((wd / "PS3_GAME" / "USRDIR").iterdir()))
    (wd / "PS3_GAME" / "USRDIR").chmod(0o555)
    if not os.access(wd / "PS3_GAME" / "USRDIR", os.W_OK):
        hint = firstrun.check_eboot(str(tmp / "none.elf"), str(wd))[0][2]
        check("E1 missing, read-only disc: hint", "read-only drive" in hint and "RPCS3 and its documentation" in hint)
    (wd / "PS3_GAME" / "USRDIR").chmod(0o755)
    ran = []
    firstrun.check_disc = lambda d: ([("OK", "D1", "stub")], "BLUS31519")
    # E3 known build: a synthetic two-segment ELF (no game data) and a test table in place of the real one
    import hashlib, struct
    def elf(pad, body=b"code-bytes-1234", data=b"data-56"):
        segs, blobs, off = [], b"", 64 + 2 * 56 + pad
        for va, blob in ((0x10000, body), (0x870000, data)):
            segs.append(struct.pack(">IIQQQQQQ", 1, 5, off + len(blobs), va, va + 0x77, len(blob), len(blob), 0x10000))   # p_paddr != p_vaddr, p_filesz
            blobs += blob
        hdr = b"\x7fELF\x02\x02\x01" + b"\0" * 9 + struct.pack(">HHIQQQIHHHHHH", 2, 21, 1, 0x10000, 64, 0, 0, 64, 56, 2, 0, 0, 0)
        return hdr + b"".join(segs) + b"\0" * pad + blobs
    a_elf, b_elf, other = elf(0), elf(32), elf(0, body=b"code-bytes-9999")
    real = firstrun.known_eboots()
    check("E3 table: eboot_ids.json beside firstrun.py lists both discs", sorted(e["title_id"] for e in real) == sorted(firstrun.TITLE_IDS))
    firstrun.known_eboots = lambda: [dict(real[0], sha256=hashlib.sha256(a_elf).hexdigest(), segment_sha256=firstrun.segment_hash(a_elf))]
    check("segment hash: same program in another layout", firstrun.segment_hash(a_elf) == firstrun.segment_hash(b_elf) != "")
    check("segment hash: other code differs", firstrun.segment_hash(other) != firstrun.segment_hash(a_elf))
    check("segment hash: cut-off file is ''", firstrun.segment_hash(a_elf[:-4]) == "")
    lvl, cid, msg = firstrun.identify_eboot(a_elf, "BLUS31519")
    check("E3 known build, same disc: OK", (lvl, cid) == ("OK", "E3") and "matches the disc folder" in msg, msg)
    lvl, _c, msg = firstrun.identify_eboot(b_elf, "BLUS31519")
    check("E3 same program, other file layout: WARN, full hash", lvl == "WARN" and "should work" in msg
          and hashlib.sha256(b_elf).hexdigest() in msg, msg)
    lvl, _c, msg = firstrun.identify_eboot(a_elf, "BLES02136")
    check("E3 other region's EBOOT: FAIL", lvl == "FAIL" and "same copy as the disc folder" in msg, msg)
    lvl, _c, msg = firstrun.identify_eboot(other, "BLUS31519")
    check("E3 unknown build: FAIL with the full hash and the build-again.cmd hint", lvl == "FAIL" and hashlib.sha256(other).hexdigest() in msg
          and "build-again.cmd" in msg and "dev_hdd0" not in msg, msg)
    for upd in ("C:\\rpcs3\\dev_hdd0\\game\\BLUS31519\\USRDIR\\EBOOT.elf", "/x/RPCS3/DEV_HDD0/game/NPUB31647/USRDIR/EBOOT.elf"):
        lvl, _c, msg = firstrun.identify_eboot(other, "BLUS31519", path=upd)
        check("E3 unknown build under dev_hdd0\\game: names path, full hash, update hint", lvl == "FAIL" and upd in msg
              and hashlib.sha256(other).hexdigest() in msg and "update's EBOOT" in msg and "PS3_GAME" in msg, msg)
    lvl, _c, msg = firstrun.identify_eboot(other, "BLUS31519", allow_unknown=True)
    check("E3 unknown build + --allow-unknown-eboot: WARN, unsupported, full hash", lvl == "WARN" and "UNSUPPORTED" in msg
          and hashlib.sha256(other).hexdigest() in msg, msg)
    lvl, _c, msg = firstrun.identify_eboot(a_elf, "BLES02136", allow_unknown=True)
    check("E3 other region's known EBOOT + --allow-unknown-eboot: still FAIL", lvl == "FAIL" and "same copy as the disc folder" in msg, msg)
    lvl, _c, msg = firstrun.identify_eboot(a_elf, "BLUS31519", allow_unknown=True)
    check("E3 known build + the flag: still OK", lvl == "OK", msg)
    firstrun.known_eboots = lambda: []
    check("E3 table missing: FAIL, install incomplete", firstrun.identify_eboot(a_elf, "BLUS31519")[0] == "FAIL")
    firstrun.known_eboots = lambda: real
    check("E3 table: shared format, ids, sizes and 64-hex hashes only", all(set(e) == {"title_id", "region", "app_ver", "size", "status",
          "sha256", "self_sha256", "segment_sha256"} and e["status"] in ("tested", "untested") and isinstance(e["size"], int)
          and all(len(e[k]) == 64 and int(e[k], 16) >= 0 for k in ("sha256", "self_sha256", "segment_sha256"))
          and e["title_id"] in firstrun.TITLE_IDS for e in real))
    iss = (pathlib.Path(firstrun.__file__).resolve().parents[1] / "jsp.iss").read_text(encoding="utf-8", errors="replace")
    check("E3 table: jsp.iss lists the same EBOOT.elf hashes", all(e["sha256"] in iss and e["title_id"] in iss for e in real))
    firstrun.check_eboot = lambda e, d=None, t=None, u=False: [("OK", "E1", "stub")]
    firstrun.check_space = lambda r: [("OK", "S1", "stub")]
    for st in ("lift", "spu", "build", "place"):
        def fn(c, st=st):
            ran.append(st)
            if st == "build":
                (c.build / "port").mkdir(parents=True, exist_ok=True)
                (c.build / "port" / "CMakeCache.txt").write_text("x")
            return True
        setattr(firstrun, "step_" + st, fn)
    # main() looks the step functions up when it runs, so rebuild the dict by patching names it reads
    def go(r=root, extra=()):
        ran.clear()
        argv = ["--root", str(r), "--disc", str(tmp / "disc"), "--eboot", str(eboot), "--devflash", str(tmp), "--hdd0", str(tmp / "h"),
                "--until", "all", "--toolchain", str(vs), "--logdir", str(tmp / "logs"), *extra]
        import io, contextlib
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = firstrun.main(argv)
        return rc, buf.getvalue()

    (tmp / "sys").mkdir()
    rc, out = go()
    check("first run runs every step", rc == 0 and ran == ["lift", "spu", "build", "place"], str(ran))
    rc, out = go()
    check("same inputs: every step skipped", rc == 0 and ran == [], str(ran))
    eboot.write_bytes(b"\x7fELF" + b"2")   # a game update
    rc, out = go()
    check("EBOOT changed: lift, spu, build, place rerun", ran == ["lift", "spu", "build", "place"] and "input changed: eboot" in out, str(ran))
    rc, out = go()
    (root / "build" / "port" / "old.obj").write_text("o")   # an object of the last build
    (root / "port" / "main.cpp").write_text("m2")   # a source of the port changes
    rc, out = go()
    check("port source changed: only build and place rerun, compile tree dropped", ran == ["build", "place"] and "port_sources" in out
          and not (root / "build" / "port" / "old.obj").exists(), str(ran))
    (root / "build" / "port" / "old.obj").write_text("o")
    (root / "ps3recomp/tools/ppu_lifter.py").write_text("a2")   # a lifter change re-lifts
    rc, out = go()
    check("lifter tool changed: lift, spu, build, place rerun, compile tree dropped", ran == ["lift", "spu", "build", "place"]
          and not (root / "build" / "port" / "old.obj").exists(), str(ran))
    firstrun.toolchain_paths(vs)["T1 llvm-mingw clang"].write_bytes(b"clang-two")   # compiler update
    (root / "build" / "port" / "CMakeCache.txt").write_text("old")
    (root / "build" / "port" / "old.obj").write_text("o")
    rc, out = go()
    check("toolchain changed: build and place rerun, compile tree dropped", ran == ["build", "place"] and "toolchain" in out
          and not (root / "build" / "port" / "old.obj").exists(), str(ran))
    (root / "build" / "port" / "old.obj").write_text("o")
    rc, out = go(extra=["--force", "build"])
    check("--force build keeps the compile tree (same inputs, ninja decides)", ran == ["build"] and (root / "build" / "port" / "old.obj").exists(), str(ran))
    # old plain marker (the format before fingerprints) counts as stale
    (root / "build/markers/lift.done").write_text("eboot 0123 region US\n")
    rc, out = go()
    check("old plain marker is stale", ran == ["lift"] and "old plain marker" in out, str(ran))
    rc, out = go(extra=["--force", "spu"])
    check("--force redoes one step", ran == ["spu"], str(ran))
    # moved install folder
    moved = tmp / "moved"
    __import__("shutil").copytree(root, moved)
    rc, out = go(moved)
    check("moved install folder: WARN R1, compile tree reset, lift and spu kept", "WARN R1" in out and ran == ["build", "place"], str(ran))
    # path refusals
    for bad in ("C:\\a[b]\\app", "C:\\a;b\\app", "C:\\a&b", "C:\\100%", "C:\\x^y", "C:\\x!y", 'C:\\x"y'):
        check(f"install path refused {bad!r}", bool(firstrun.path_problems(bad, "install folder")))
    for good in ("C:\\Ann Lee\\AppData\\Local\\Programs\\JSP Fan Revival", "C:\\Program Files (x86)\\JSP", "D:\\Games\\x'y"):
        check(f"install path accepted {good!r}", not firstrun.path_problems(good, "install folder"))
    check("disc path with [ ] ; refused", all(firstrun.path_problems(p, "disc folder") for p in ("D:\\d[1]", "D:\\d]", "D:\\d;e")))
    check("disc path with & accepted", not firstrun.path_problems("D:\\a&b", "disc folder"))
    rc, out = go(tmp / "bad[dir]")
    check("run in a refused folder stops with FAIL P1 and runs nothing", rc == 1 and "FAIL P1" in out and not ran, out.strip().splitlines()[-1])
    # logs: the newest five stay; the profile path is replaced
    logs = tmp / "logs"
    for i in range(8):
        (logs / f"firstrun-2020010{i}-000000.log").write_text("x")
    firstrun.open_logdir(logs)
    check("log rotation keeps 4 old + the new one", len(list(logs.glob("firstrun-*.log"))) == 4,
          str(len(list(logs.glob("firstrun-*.log")))))
    home = os.environ.get("USERPROFILE") or str(pathlib.Path.home())
    check("profile path scrubbed", firstrun.scrub(f"open {home}\\x and {home.replace(chr(92), '/')}/y").count("%USERPROFILE%") == 2)
    print("RESULT:", "ok" if ok else "FAILED")
    return 0 if ok else 1


def main(app, disc, eboot, scratch):
    app, scratch = pathlib.Path(app), pathlib.Path(scratch)
    fr = [sys.executable, "-I", str(app / "firstrun.py")]
    ok = True

    def check(name, cond, extra=""):
        nonlocal ok
        ok &= bool(cond)
        print(f"{'ok  ' if cond else 'FAIL'} {name} {extra}")

    # A fresh payload has no markers: the first pass does the work (so this works on a fresh payload), the second must skip.
    subprocess.run([*fr, "--root", str(app), "--disc", disc, "--eboot", eboot, "--until", "spu"], capture_output=True, text=True)
    r = subprocess.run([*fr, "--root", str(app), "--disc", disc, "--eboot", eboot, "--until", "spu"], capture_output=True, text=True)
    check("re-run skips lift and spu", r.stdout.count("skipped") == 2, r.stdout.strip().splitlines()[-1])
    a = tree(app / "build" / "recompiled")
    check("lift output", len(a) >= 65, f"{len(a)} source/header files")
    # second lift, different worker count, other folder: must be byte identical (determinism)
    root2 = scratch / "lift2"
    (root2 / "build").mkdir(parents=True, exist_ok=True)
    for n in ("ps3recomp", "tools"):
        if not (root2 / n).exists():
            (root2 / n).symlink_to((app / n).resolve())
    r = subprocess.run([*fr, "--root", str(root2), "--disc", disc, "--eboot", eboot, "--until", "lift", "--jobs", "2"],
                       capture_output=True, text=True)
    sys.path.insert(0, str(app / "tools"))
    b = tree(root2 / "build" / "recompiled")
    # the files embed no path, but the lift log does; only sources are compared
    check("lift identical with -j2 vs -j4", a == b, f"{len(b)} files, {sum(a[k] != b.get(k) for k in a)} differ")
    src = (app / "build" / "spu" / "spu_workloads.c").read_text()
    for fp in KNOWN_FP:
        check(f"spu fingerprint {fp}", fp in src)
    check("18 registrations", src.count("spu_workload_register_img(") == 18)
    # fake toolchain (shell scripts that do nothing): T1-T3 accepted, build-port.cmd written, the build step fails (no exe made)
    tc = scratch / "faketc"
    for rel in ("llvm-mingw/bin/x86_64-w64-mingw32-clang++", "cmake/bin/cmake", "ninja/ninja"):
        (tc / rel).parent.mkdir(parents=True, exist_ok=True)
        (tc / rel).write_text("#!/bin/sh\nexit 0\n")
        (tc / rel).chmod(0o755)
    r = subprocess.run([*fr, "--root", str(app), "--disc", disc, "--eboot", eboot, "--until", "build", "--toolchain", str(tc), "--jobs", "2"],
                       capture_output=True, text=True)
    cmd = (app / "build" / "build-port.cmd").read_bytes()
    check("T1-T3 accepted", all(f"OK   T{i}" in r.stdout for i in (1, 2, 3)))
    check("build-port.cmd", b"-DPS3RECOMP_DIR=" in cmd and b"llvm-mingw.cmake" in cmd and b"\r\n" in cmd and b"clang-cl" not in cmd and b"vcvars" not in cmd)
    check("build step without an exe fails", r.returncode == 1 and "step build stopped" in r.stdout)
    r = subprocess.run([*fr, "--root", str(app), "--disc", disc, "--eboot", eboot, "--until", "build", "--toolchain", str(scratch / "none")],
                       capture_output=True, text=True)
    check("missing toolchain fails with T1-T3 FAILED", r.returncode == 1 and r.stdout.count("FAIL T") >= 3)
    # place step with a fake exe: game/ folder and paths.txt (read by the launcher)
    sys.path.insert(0, str(app))
    import argparse
    import firstrun
    (app / "build" / "port").mkdir(parents=True, exist_ok=True)
    (app / "build" / "port" / firstrun.EXE).write_bytes(b"MZ")
    ns = argparse.Namespace(root=str(app), disc=disc, eboot=eboot, devflash=str(scratch / "df"), hdd0=str(scratch / "hdd0"), force=[])
    check("place step", firstrun.step_place(firstrun.Ctx(ns, "US", "BLUS31519")))
    pt = (app / "paths.txt").read_text()
    check("paths.txt keys", all(k in pt for k in ("PS3_VFS_ROOT=", "PS3_HDD0_ROOT=", "PS3_DEV_FLASH=", "EXE=", "ELF=")), pt.splitlines()[0])
    # PS3_VFS_ROOT: a drive root must not come out as 'E://', a folder gets one trailing '/'
    want = {"E:\\": "E:/", "E:/": "E:/", "E:": "E:/", "D:\\Games\\BLUS31519": "D:/Games/BLUS31519/",
            "D:\\Games\\BLUS31519\\": "D:/Games/BLUS31519/", "/mnt/disc/": "/mnt/disc/"}
    for src, exp in want.items():
        check(f"vfs_root {src!r}", firstrun.vfs_root(src) == exp, firstrun.vfs_root(src))
    check("paths.txt VFS root has one trailing slash", pt.splitlines()[0].endswith("/") and not pt.splitlines()[0].endswith("//"))
    check("game folder", (app / "game" / firstrun.EXE).is_file() and (app / "game" / "EBOOT.elf").is_file() and (app / "game" / "run").is_dir())
    print("RESULT:", "ok" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(units() if sys.argv[1:] == ["--units"] else main(*sys.argv[1:5]))
