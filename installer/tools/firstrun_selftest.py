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
    for rel, txt in (("ps3recomp/tools/ppu_lifter.py", "a"), ("ps3recomp/src/x.c", "x"), ("port/main.cpp", "m"), ("tools/lift_jobs.py", "j")):
        (root / rel).parent.mkdir(parents=True, exist_ok=True)
        (root / rel).write_text(txt)
    # which tree a payload holds: tree.txt says v18; none or another value is an error (the v15 tree is retired)
    def tree_error():
        try:
            firstrun.payload_tree(root)
        except SystemExit as e:
            return str(e)
        return ""
    check("payload_tree: no file is an error", "tree.txt" in tree_error() and "legacy-v15" in tree_error())
    (root / "tree.txt").write_text("nonsense\n")
    check("payload_tree: unknown value is an error", "nonsense" in tree_error())
    (root / "tree.txt").write_text("v15\n")
    check("payload_tree: v15 is an error", "'v15'" in tree_error())
    (root / "tree.txt").write_text("v18\n")
    check("payload_tree: v18", firstrun.payload_tree(root) == "v18")
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
    # This unit section mocks the entire lift/build steps. Actual emitted-hook
    # refusal and receipt verification are exercised by lod_hooks_test.py.
    for filename in ("lod_hooks.py", "lift_lod_hooks.json", "lift_game_hooks.json"):
        __import__("shutil").copy2(pathlib.Path(firstrun.__file__).resolve().parents[1] / "port/jsp" / filename,
                                  root / "port" / filename)
    firstrun.verify_lod_hooks = lambda c: False
    # the files each stub step leaves: the declared outputs of the real step, plus optional ones place copies when present
    stub_out = {"lift": ("build/recompiled/ppu_recomp.h", "build/recompiled/ppu_recomp_000.cpp", "build/recompiled/ppu_recomp_001.cpp"),
                "spu": ("build/spu/spu_workloads.c", "build/spu/job00/spu_recomp.c", "build/spu/job01/spu_recomp.c"),
                "build": ("build/port/jsp.exe",),
                "place": ("game/jsp.exe", "game/EBOOT.elf", "paths.txt", "game/ex_logo_A.rgba",
                          *(f"game/{n}" for n in firstrun.PORT_MENU_ART), "game/avcodec-61.dll", "jsp-settings.exe")}
    skip_out = set()   # files a stub step "forgets" to leave (a step that reports success without its output)
    for st in ("lift", "spu", "build", "place"):
        def fn(c, st=st):
            ran.append(st)
            if st == "build":
                (c.build / "port").mkdir(parents=True, exist_ok=True)
                (c.build / "port" / "CMakeCache.txt").write_text("x")
            for rel in stub_out[st]:
                if rel not in skip_out:
                    (c.root / rel).parent.mkdir(parents=True, exist_ok=True)
                    (c.root / rel).write_text(st)
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
    # a finished build whose files were removed afterwards (an antivirus, a clean-up tool): the step that leaves the file runs again,
    # only that one, and the log names the file; the launcher's "game program is missing, run Build again" must not loop
    rc, out = go()
    check("outputs: a finished build skips every step", rc == 0 and ran == [], str(ran))
    for st, files in stub_out.items():
        for rel in files:
            (root / rel).unlink()
            rc, out = go()
            check(f"outputs: {rel} removed: only {st} reruns and the log names the file", rc == 0 and ran == [st]
                  and f"[{st}] running: output missing: {rel}" in out, str(ran))
            rc, out = go()
            check(f"outputs: {rel} back: nothing reruns", rc == 0 and ran == [] and (root / rel).is_file(), str(ran))
    (root / "game/other.dll").write_text("x")   # a DLL place does not copy: not one of its outputs, removing it reruns nothing
    rc, out = go(extra=["--force", "place"])
    (root / "game/other.dll").unlink()
    rc, out = go()
    check("outputs: a DLL place does not copy is not listed", rc == 0 and ran == [], str(ran))
    for d, st in (("build/recompiled", "lift"), ("build/spu", "spu"), ("game", "place")):
        __import__("shutil").rmtree(root / d)
        rc, out = go()
        check(f"outputs: {d}/ removed: only {st} reruns", rc == 0 and ran == [st] and "output missing" in out, str(ran))
    # a marker written before outputs were listed: the required files still count, nothing reruns while they are there
    import json
    mk = root / "build/markers/place.done"
    fp = json.loads(mk.read_text()[len(firstrun.MARKER_TAG):])
    fp.pop(firstrun.OUTPUTS_KEY)
    mk.write_text(firstrun.MARKER_TAG + json.dumps(fp) + "\n")
    rc, out = go()
    check("outputs: marker without a file list, files there: skipped", rc == 0 and ran == [], str(ran))
    mk.write_text(firstrun.MARKER_TAG + json.dumps(fp) + "\n")
    (root / "game/jsp.exe").unlink()
    rc, out = go()
    check("outputs: marker without a file list, game/jsp.exe removed: place reruns", ran == ["place"] and "output missing: game/jsp.exe" in out, str(ran))
    # a step that says it worked but left no exe fails at once instead of running again on every start
    (root / "build/port/jsp.exe").unlink()
    skip_out.add("build/port/jsp.exe")
    rc, out = go()
    check("outputs: a step that leaves no output fails and names the file", rc == 1 and ran == ["build"]
          and "finished without build/port/jsp.exe" in out and "RESULT: failed, step build" in out, str(ran))
    skip_out.clear()
    rc, out = go()
    check("outputs: the next run redoes it", rc == 0 and ran == ["build"], str(ran))
    # the settings window's source is an input of place: an update that changes it rebuilds the window
    (root / "tools" / "jsp-settings.cs").write_text("class A {}")
    rc, out = go()
    check("settings source added: only place reruns", ran == ["place"] and "input changed: settings_source" in out, str(ran))
    (root / "tools" / "jsp-settings.cs").write_text("class B {}")
    rc, out = go()
    check("settings source changed: only place reruns", ran == ["place"] and "input changed: settings_source" in out, str(ran))
    rc, out = go()
    check("settings source unchanged: nothing reruns", ran == [], str(ran))
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
    # the settings window is rebuilt when tools/jsp-settings.cs changes (hash file beside the exe), not by file dates
    sw = tmp / "sw"
    (sw / "tools").mkdir(parents=True)
    (sw / "tools" / "jsp-settings.cs").write_text("class A {}")
    check("settings window: no exe is stale", firstrun.settings_window_stale(sw))
    (sw / "jsp-settings.exe").write_bytes(b"MZ")
    check("settings window: exe without a hash file is stale once", firstrun.settings_window_stale(sw))
    (sw / firstrun.SETTINGS_HASH).write_text(hashlib.sha256(b"class A {}").hexdigest() + "\n")
    check("settings window: exe built from this source is current", not firstrun.settings_window_stale(sw))
    (sw / "tools" / "jsp-settings.cs").write_text("class B {}")
    check("settings window: changed source is stale", firstrun.settings_window_stale(sw))
    (sw / "tools" / "jsp-settings.cs").unlink()
    check("settings window: missing source counts as stale (and the build then does nothing)", firstrun.settings_window_stale(sw))
    # FIX (review): a fresh install is offline. FIX (offer only): online-service.ini (a package's service) is only OFFERED: the first run writes
    # nothing for it (no repsn.ini, no rollback line, no JSP_DISCORD_APP_ID), it only logs the offer and the settings window's button; the
    # player's click in the Online tab turns it on (online_option_test.py checks that side). Present settings are never touched.
    import re
    SRV = "srv.example.test:36100"
    SVC = ("; TEST BUILD ONLY\n[service]\nlabel=Example test service\ntest_build=1\n[repsn]\nserver=" + SRV + "\nrollback=1\nrelay=1\n"
           "[discord]\napp_id=123456789012345678\n")

    def svc_in(d, text=SVC):
        d.mkdir(parents=True, exist_ok=True)
        (d / firstrun.SERVICE_FILE).write_text(text)
        return d

    def rd(p):
        return p.read_text() if p.is_file() else ""

    def files(d):   # every file under d (relative), to prove nothing was written
        return sorted(str(x.relative_to(d)).replace("\\", "/") for x in pathlib.Path(d).rglob("*") if x.is_file()) if pathlib.Path(d).exists() else []

    off = tmp / "off"; env0 = {"APPDATA": str(off / "roaming")}
    (off / "inst").mkdir(parents=True)
    l0 = firstrun.online_defaults(off / "inst", False, env0)
    check("online: no service file -> offline, nothing written", not (off / "roaming").exists() and "no online service selected" in l0[0], " | ".join(l0))
    check("online: the repository names no server and no Discord application",
          not hasattr(firstrun, "ONLINE_SERVER") and not re.search(r"\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}:\d+", pathlib.Path(firstrun.__file__).read_text(encoding="utf-8")))
    for bad in ("[service]\nlabel=x\n", "[service]\nlabel=x\n[repsn]\nserver=a b\n", "[service]\nlabel=x\n[repsn]\nserver=h:70000\n",
                "[service]\nlabel=x\n[repsn]\nserver=h\n[discord]\napp_id=12ab\n", "[repsn]\nserver=h\n", "[service]\nlabel=\x01bad\n[repsn]\nserver=h\n"):
        bd = svc_in(tmp / ("bad%d" % len(bad)) / "inst", bad)
        lb = firstrun.online_defaults(bd, True, {})
        check(f"online: an invalid service file offers nothing ({bad.strip()[-20:]!r})", files(bd) == [firstrun.SERVICE_FILE]
              and "no online service offered" in lb[0] and firstrun.read_service(bd)[0] is None, " | ".join(lb))
    big = svc_in(tmp / "big" / "inst", SVC + ";" + "x" * (64 * 1024) + "\n")
    check("online: a service file over 64 KB offers nothing", "over 64 KB" in firstrun.online_defaults(big, True, {})[0] and files(big) == [firstrun.SERVICE_FILE])
    # a valid test service on a fresh install (AppData layout): the log offers it, names the button and writes nothing anywhere
    od = tmp / "od"
    env = {"APPDATA": str(od / "roaming")}
    inst = svc_in(od / "inst")
    svc, why = firstrun.read_service(inst)
    check("online service: a valid file is read (label, server, test build, rollback, relay, Discord id)",
          why is None and svc == {"label": "Example test service", "server": SRV, "app_id": "123456789012345678", "test": True, "rollback": True, "relay": True}, repr((svc, why)))
    check("online service: the button text names the service and TEST BUILD", firstrun.service_button(svc) == "Use Example test service (TEST BUILD)")
    check("online service: a normal build's button has no TEST BUILD", firstrun.service_button(dict(svc, test=False)) == "Use Example test service")
    lines = firstrun.online_defaults(inst, False, env)
    check("online service (fresh): nothing written, no repsn.ini, no settings file, no AppData folder",
          not (od / "roaming").exists() and files(inst) == [firstrun.SERVICE_FILE] and not firstrun.settings_file(inst, False, env).exists(), " | ".join(lines))
    check("online service (fresh): the log offers it as a TEST BUILD and names the one click",
          lines[0].startswith("[online] TEST BUILD: ") and "offers the online service \"Example test service\"" in lines[0]
          and "Online tab, click \"Use Example test service (TEST BUILD)\" (one click)" in lines[0] and "online stays off" in lines[0], " | ".join(lines))
    check("online service (fresh): no line claims online, rollback or Discord was turned on",
          not any(w in " ".join(lines) for w in ("JSP_ROLLBACK", "JSP_DISCORD", "added to the settings file", "online on")), " | ".join(lines))
    again = firstrun.online_defaults(inst, False, env)
    check("online service: a second run still writes nothing", not (od / "roaming").exists() and files(inst) == [firstrun.SERVICE_FILE], " | ".join(again))
    # portable fresh install: the shared folder and the settings file would be in the install folder; neither appears
    pf = svc_in(tmp / "portfresh")
    lp = firstrun.online_defaults(pf, True, {}, region="US")
    check("online service (portable, fresh): no shared\\repsn.ini, no play_settings.txt", files(pf) == [firstrun.SERVICE_FILE]
          and not (pf / "shared").exists() and not (pf / "play_settings.txt").exists(), " | ".join(lp))
    # upgrade: the player's own server and Off, and a player's JSP_DISCORD=0, are kept byte for byte; the log says they are kept
    up = tmp / "up"; envu = {"APPDATA": str(up)}
    (up / "PS3Recomp").mkdir(parents=True); (up / "PS3Recomp" / "repsn.ini").write_text("[repsn]\nenabled=0\nserver=mine.example\nname=Me123\n")
    (up / firstrun.APPDIR).mkdir(parents=True); (up / firstrun.APPDIR / "play_settings.txt").write_text("JSP_DISCORD=0\n")
    lu = firstrun.online_defaults(svc_in(up / "inst"), False, envu)
    check("online service: an upgrade keeps the player's repsn.ini and Discord choice",
          rd(up / "PS3Recomp" / "repsn.ini") == "[repsn]\nenabled=0\nserver=mine.example\nname=Me123\n" and rd(up / firstrun.APPDIR / "play_settings.txt") == "JSP_DISCORD=0\n"
          and any("already present (repsn.ini): kept" in l for l in lu), " | ".join(lu))
    # an upgrade with no Discord line and an older rollback-free settings file: still nothing added (before: JSP_DISCORD_APP_ID was added on upgrades)
    od2 = tmp / "od2"; env2 = {"APPDATA": str(od2)}
    (od2 / "PS3Recomp").mkdir(parents=True); (od2 / "PS3Recomp" / "community.ini").write_text("[community]\nenabled=0\n")
    (od2 / firstrun.APPDIR).mkdir(parents=True); (od2 / firstrun.APPDIR / "play_settings.txt").write_text("RES_SCALE=2\n")
    firstrun.online_defaults(svc_in(od2 / "inst"), False, env2)
    check("online service: an upgrade gets no repsn.ini and no JSP_DISCORD_APP_ID or rollback line", not (od2 / "PS3Recomp" / "repsn.ini").exists()
          and rd(od2 / firstrun.APPDIR / "play_settings.txt") == "RES_SCALE=2\n" and rd(od2 / "PS3Recomp" / "community.ini") == "[community]\nenabled=0\n")
    # portable with the player's own lines: unchanged
    pr = svc_in(tmp / "port")
    (pr / "play_settings.txt").write_text("RES_SCALE=2\nJSP_ROLLBACK=0\n")
    firstrun.online_defaults(pr, True, {})
    check("online service: the player's settings file is not touched (JSP_ROLLBACK=0 kept, nothing appended)",
          rd(pr / "play_settings.txt") == "RES_SCALE=2\nJSP_ROLLBACK=0\n" and not (pr / "shared").exists(), repr(rd(pr / "play_settings.txt")))
    # FIX (region): an EU disc: the log says rollback is US-disc only; still nothing written
    pr4 = svc_in(tmp / "port4")
    eu = firstrun.online_defaults(pr4, True, {}, region="EU")
    check("online service (EU): the log says rollback is US-disc only, nothing written", any("US-disc only" in l for l in eu) and files(pr4) == [firstrun.SERVICE_FILE], " | ".join(eu))
    check("online: JSP_ONLINE_DEFAULTS=0 skips", "skipped" in firstrun.online_defaults(svc_in(tmp / "nope"), True, {"JSP_ONLINE_DEFAULTS": "0"})[0]
          and not (tmp / "nope" / "shared").exists())
    # the source: no seeding left (no repsn.ini write, no settings-file append) in the online step
    src = pathlib.Path(firstrun.__file__).read_text(encoding="utf-8")
    step = src[src.index("def online_defaults("):src.index("SETTINGS_HASH = ")]
    check("online: the first-run online step has no write call", not any(w in step for w in ("write_text", "write_bytes", "open(", "mkdir", "_settings_append")))
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
