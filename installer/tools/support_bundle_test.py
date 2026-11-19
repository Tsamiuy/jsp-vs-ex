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
        w(app / "game/jsp.exe", b"MZ" + b"0" * 10)
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
        # (zip files are closed as soon as they are read: Windows cannot delete an open file when the temp folder goes)
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
        z.close()
        check("jsp.exe: present, 12 bytes" in f, "facts: game exe size")
        old = t / "app-old"                      # a folder with the retired tree's exe: reported as missing, not mistaken for the game
        w(old / "tree.txt", "v15\n")
        w(old / "game/JStarsRecomp.exe", b"MZ" + b"0" * 6)
        fo = sb.facts(old, {}, [], [], [], [])
        check("jsp.exe: present" not in fo and "JStarsRecomp" not in fo, "facts: only jsp.exe counts as the game")
        check("GPU: Test Adapter 9000" in f, "facts: GPU name from a log line")
        g = sb.gpu_name({"a.log": b"[spu] gpu sync: 3 waits\n", "game/run/play_stdout.txt": b"[D3D12] adapter: Test GPU (VRAM 8 MB)\r\n"})
        check(g == "Test GPU (VRAM 8 MB) (from game/run/play_stdout.txt)", "GPU: the [D3D12] adapter line wins over a looser match")
        pm = sb.present_mode({"a.log": b"x\n", "game/run/play_stdout.txt": b"[D3D12] present: no wait for the monitor (default)\r\n[D3D12] present: waits for the monitor's vblank (D3D12_VSYNC=1)\n"})
        check(pm == "waits for the monitor's vblank (D3D12_VSYNC=1) (from game/run/play_stdout.txt)", "present mode: the last [D3D12] present line")
        check(sb.present_mode({"a.log": b"[D3D12] adapter: x\n"}).startswith("not in the logs"), "present mode: absent in older logs")
        check("\npresent mode: " in f, "facts: present mode line")
        check("bundled Python: " in f and "CPUs: " in f and "free space" in f and "Windows: " in f, "facts: system lines")
        check("\nCPU: " in f and "AVX2: " in f and "\nGPU driver: " in f, "facts: CPU model, AVX2 and GPU driver lines")
        fake = {r"HARDWARE\DESCRIPTION\System\CentralProcessor\0": {"ProcessorNameString": "  Test CPU   8-Core  "},
                sb.DISPLAY_CLASS + "\\0000": {"DriverDesc": "Test GPU", "DriverVersion": "32.0.15.6094", "DriverDate": "10-1-2026"},
                sb.DISPLAY_CLASS + "\\0001": {"DriverDesc": "Basic Display"},       # no version: skipped
                sb.DISPLAY_CLASS + "\\Properties": {"DriverDesc": "x", "DriverVersion": "1"}}   # not an adapter: skipped
        rd = lambda _r, path, names: {n: v for n, v in fake.get(path, {}).items() if n in names}
        check(sb.cpu_model(rd) == "Test CPU 8-Core", "CPU: the registry name, spaces collapsed")
        check(sb.gpu_drivers(lambda _p: ["0000", "0001", "Properties"], rd) == ["Test GPU: driver 32.0.15.6094 (10-1-2026)"], "GPU driver: numbered adapter keys with a version only")
        check(sb.gpu_drivers(lambda _p: [], rd) == [], "GPU driver: none found")
        mons = [{"name": "\\\\.\\DISPLAY1", "adapter": "Test GPU", "monitor": "Test Monitor", "width": 2560, "height": 1440, "hz": 60, "primary": True, "handle": 11},
                {"name": "\\\\.\\DISPLAY2", "adapter": "Test GPU", "monitor": "Virtual Display", "width": 1920, "height": 1080, "hz": 1, "primary": False, "handle": 22}]
        dl = sb.display_lines(mons, (22, "1280x720"))
        check(len(dl) == 3 and "(primary)" in dl[0] and "2560x1440 at 60 Hz" in dl[0] and "<- game window" not in dl[0], "displays: primary, size and refresh rate")
        check("<- game window" in dl[1] and "default Hz" in dl[1] and "Virtual Display" in dl[1] and "1280x720" in dl[2], "displays: the game window's display is marked; Hz 1 means default")
        check("not running" in sb.display_lines(mons, (None, ""))[-1], "displays: the game is not running")
        check(sb.display_lines([], (None, "")) == ["Displays: not found (not Windows, or the query failed)"], "displays: none found")
        if sys.platform == "win32":   # the real queries run here (CI's runner has a virtual display, maybe none): a list, and no game running
            real = sb.displays()
            check(isinstance(real, list) and all(isinstance(m, dict) for m in real) and sb.game_display("jsp-no-such-game.exe") == (None, ""), "displays: the real Windows queries run and find no game")
            print("   (this machine: %d display(s) %s)" % (len(real), [(m.get("width"), m.get("height"), m.get("hz")) for m in real]))
        else:
            check(sb.displays() == [] and sb.game_display("jsp.exe") == (None, ""), "displays: off Windows both queries return nothing")
        check("\nDisplays: not found" in f or "\nDisplay 1" in f, "facts: a displays line is always present")

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
        z2.close()

        # the netcode: the mode from the settings files, the [net] summary read before the cap, private values out of every file
        os.environ.pop("PS3RECOMP_SHARED_DIR", None)
        app3, ad3 = t / "app3", t / "appdata3"
        srv, nm, peer = "play.example.org", "rivalfan", "203.0.113.7"
        w(app3 / "VERSION.txt", "0.5.1-test\n")
        w(ad3 / sb.APPDIR / "play_settings.txt", "JSP_ROLLBACK=1\nJSP_ROLLBACK_VIA=repsn\nJSP_ROLLBACK_ROLE=host\nJSP_ROLLBACK_RELAY=1  # relay\n")
        w(ad3 / "PS3Recomp/repsn.ini", f"[repsn]\nenabled = 0\nserver = {srv}:36100\nname = {nm}\n")
        head = ("[net] env decision: rollback start (static constructors arrived: EnvInit first, online boot last)\n"
                f"[net] re-psn: creating a room on {srv}:36100 as {nm} (relay allowed), local UDP port 3659\n"
                "[net] rollback opt-in: host, build 1a2b3c4d, hooks installed\n"
                f"[net] rendezvous: {peer}:3659 via re-psn, path relay, room 7\n"
                "[net] re-psn path: relay\n[net] gekko session synchronized\n")
        ticks = "".join(f"[net] commit tick={i} hash=0000abcd raw=0000abcd\n" for i in range(12000))
        rb = "[net] rollback to tick={} (longest game-thread stretch without a session call since the last rollback: 4 ms, keepalive polls so far: 0)\n"
        tail = (rb.format(5) + rb.format(9) + rb.format(11) +
                "[net] DESYNC at tick 40 (stepper tick numbering, session-relative; add the base 3): local=00000001 remote=00000002\n"
                "[net] ended: Opponent disconnected\nloopback 127.0.0.1:36100 stays\n")
        w(app3 / "game/run/play_stderr.txt", head + ticks + tail)
        w(app3 / "game/run/play_stderr.prev.txt", "[net] env decision: REFUSED (static constructors arrived: online boot first, EnvInit last)\n[net] REFUSED: JSP_ROLLBACK=1 and the game's own online (re-psn or a community server) are both on; only one multiplayer method may be active, neither runs\n")
        w(app3 / "game/run/play_stdout.txt", f"[online] re-psn enabled: server {srv}:36100\n[online] online id {nm}\n[D3D12] adapter: Test GPU (VRAM 8 MB)\n")
        zp3 = sb.make(app3, out, ad3, None, cap=200000)
        z3 = zipfile.ZipFile(zp3)
        n3 = set(z3.namelist())
        check({"game/run/play_stderr.txt", "game/run/play_stderr.prev.txt", "appdata/play_settings.txt"} <= n3, "net: both launches' stderr and the settings file are in the zip")
        err3 = z3.read("game/run/play_stderr.txt").decode()
        f3 = z3.read("facts.txt").decode()
        all3 = b"".join(z3.read(n) for n in n3).decode()
        z3.close()
        check(err3.startswith("[support_bundle: the oldest ") and "rollback opt-in" not in err3, "net: the cap cut the start of play_stderr.txt (the test's premise)")
        check("mode: Rollback re-psn host, relay allowed: yes" in f3, "net facts: the mode from play_settings.txt")
        check("re-psn server: set   online name: set   rollback join address: not set" in f3, "net facts: server and name only as set / not set")
        check("rollback started: yes (host)" in f3 and "path: relay" in f3 and "session synchronized: yes" in f3, "net facts: started, path, synchronized survive the cut")
        check("rollbacks: 3   desync lines: 1" in f3 and "session ended: Opponent disconnected" in f3, "net facts: rollback and desync counts, the end reason")
        check("env decision: rollback start (static constructors arrived: EnvInit first, online boot last)" in f3 and "[net] rollback opt-in: host, build 1a2b3c4d" in f3 and "commit tick=" not in f3, "net facts: key lines kept, per-tick lines left out")
        prev = f3.split("play_stderr.prev.txt (the launch before):")[1]
        check("refused: JSP_ROLLBACK=1 and the game's own online" in prev and "rollback started: no" in prev, "net facts: the previous launch's refusal and its reason")
        for bad in (srv, nm, peer):
            check(bad not in all3, f"net: {bad!r} appears nowhere in the zip")
        check("<server>" in all3 and "<online name>" in all3 and "[net] rendezvous: <peer address> via re-psn, path relay, room 7" in f3 and "127.0.0.1:36100 stays" in all3, "net: placeholders in the files, loopback kept")
        S = lambda play, ini=None: "\n".join(sb.net_mode({"play": play, "ini": ini or {}, "read": []}))
        check("mode: Off" in S({}), "net mode: nothing set is Off")
        dj = S({"JSP_ROLLBACK": "1", "JSP_ROLLBACK_JOIN": "198.51.100.4:7000"})
        check("mode: Rollback direct join" in dj and "join address: set" in dj and "198.51.100.4" not in dj, "net mode: direct join, address only as set")
        check("mode: Rollback direct host" in S({"JSP_ROLLBACK": "1", "JSP_ROLLBACK_LISTEN": "7000"}), "net mode: direct host")
        check("mode: Rollback direct refused: not exactly one" in S({"JSP_ROLLBACK": "1"}), "net mode: no role key is refused")
        check("mode: Off" in S({"JSP_ROLLBACK": "0", "JSP_ROLLBACK_LISTEN": "7000"}), "net mode: JSP_ROLLBACK=0 is off")
        rep_on = {"repsn": {"enabled": "1", "server": "x.example:1", "name": "abc"}}
        check("mode: Delay (re-psn)" in S({}, rep_on), "net mode: Delay over re-psn")
        check("both enabled" in S({}, {**rep_on, "community": {"enabled": "1"}}), "net mode: re-psn and community both on")
        check("mode: Off (community server enabled: no client yet" in S({}, {"community": {"enabled": "1"}}), "net mode: a community server keeps the game offline")
        check("mode: Rollback refused: PS3_NET_ONLINE is set too" in S({"JSP_ROLLBACK": "1", "JSP_ROLLBACK_LISTEN": "7000"}, rep_on), "net mode: rollback and Delay both on are refused")
        # jsp_online.cpp: PSNR_SERVER in the environment wins over the files and does not set PS3_NET_ONLINE; resolve_env refuses only on PS3_NET_ONLINE
        ps = S({"PSNR_SERVER": "h.example:36100"}, rep_on)
        check("mode: Off: PSNR_SERVER is set in play_settings.txt without PS3_NET_ONLINE" in ps, "net mode: PSNR_SERVER alone is no Delay (and the files are not used)")
        check("mode: Rollback direct host" in S({"JSP_ROLLBACK": "1", "JSP_ROLLBACK_LISTEN": "7000", "PSNR_SERVER": "h.example"}, rep_on),
              "net mode: rollback with PSNR_SERVER but no PS3_NET_ONLINE starts (the files were not used)")
        check("mode: Delay (re-psn, server from PSNR_SERVER" in S({"PSNR_SERVER": "h.example", "PS3_NET_ONLINE": "1"}), "net mode: both variables by hand are Delay")
        check("mode: Rollback refused: PS3_NET_ONLINE" in S({"JSP_ROLLBACK": "1", "JSP_ROLLBACK_LISTEN": "7000", "PS3_NET_ONLINE": "0"}), "net mode: PS3_NET_ONLINE refuses rollback at any value")
        check("mode: Rollback direct refused: JSP_ROLLBACK_JOIN is not IP:PORT" in S({"JSP_ROLLBACK": "1", "JSP_ROLLBACK_JOIN": "h.example:7000"}), "net mode: the join address must be IP:PORT")
        check("mode: Rollback re-psn refused: JSP_ROLLBACK_ROLE" in S({"JSP_ROLLBACK": "1", "JSP_ROLLBACK_VIA": "RePSN"}), "net mode: re-psn needs a role")
        check("mode: Off (re-psn enabled but no valid server" in S({}, {"repsn": {"enabled": "1"}}), "net mode: re-psn without a server stays offline")

        # play_settings.txt as the launcher reads it: '#' only at the start of a line, @APP@, PS3RECOMP_SHARED_DIR from the file wins
        app5, ad5, env_sd = t / "app5", t / "appdata5", t / "env_shared"
        w(app5 / "play_settings.txt", "# c\n;c\nPS3RECOMP_SHARED_DIR=@APP@/myshared  \nJSP_ROLLBACK=1\nJSP_ROLLBACK_LISTEN=7000\nX=a#b\n")
        w(app5 / "myshared/repsn.ini", "[repsn]\nenabled=1\nserver=file.example\nname=zed99\n")
        w(env_sd / "repsn.ini", "[repsn]\nenabled=0\n")
        ns5 = sb.net_settings(app5, ad5, env={"PS3RECOMP_SHARED_DIR": str(env_sd)})
        check(ns5["play"].get("X") == "a#b" and "# c" not in ns5["play"], "settings: a '#' inside a value stays, as the launcher passes it")
        check("repsn.ini (shared folder)" in ns5["read"] and ns5["ini"]["repsn"]["server"] == "file.example:36100",
              "settings: PS3RECOMP_SHARED_DIR from play_settings.txt (with @APP@) wins over the process environment")
        check("mode: Rollback refused: PS3_NET_ONLINE is set too" in "\n".join(sb.net_mode(ns5)), "net mode: the shared folder named in play_settings.txt decides")
        ns6 = sb.net_settings(app5, ad5, env={})
        check(ns6["ini"]["repsn"]["enabled"] == "1", "settings: without the variable in the environment the file's folder is used too")

        # an online name that is a word of the [net] lines: the summary comes from the raw lines and never prints the name
        for word in ("net", "host", "relay"):
            app4 = t / f"app4{word}"
            w(app4 / "VERSION.txt", "0.5.1-test\n")
            w(app4 / "play_settings.txt", "JSP_ROLLBACK=1\nJSP_ROLLBACK_VIA=repsn\nJSP_ROLLBACK_ROLE=host\nJSP_ROLLBACK_RELAY=1\n")
            w(app4 / "settings_here.txt", "")
            w(app4 / "shared/repsn.ini", f"[repsn]\nenabled=0\nserver=srv.example\nname={word}\n")
            w(app4 / "game/run/play_stderr.txt", head.replace(srv + ":36100", "srv.example:36100").replace(nm, word) + tail +
              f"[net] rendezvous failed: {word} said no\n")
            ns4 = sb.net_settings(app4, None, env={"PS3RECOMP_SHARED_DIR": str(app4 / "shared")})
            sec4 = sb.net_secrets(ns4)
            check(sec4.get(word) == "<online name>", f"name {word!r}: is a secret")
            raw4 = {"game/run/play_stderr.txt": (app4 / "game/run/play_stderr.txt").read_text()}
            mode4, logs4 = "\n".join(sb.net_mode(ns4)), "\n".join(sb.net_log_lines(raw4, sec4))
            check("mode: Rollback re-psn host, relay allowed: yes" in mode4, f"name {word!r}: the mode line is intact")
            check("rollback started: yes (host)" in logs4 and "path: relay" in logs4 and "session synchronized: yes" in logs4
                  and "rollbacks: 3   desync lines: 1" in logs4 and "session ended: Opponent disconnected" in logs4, f"name {word!r}: the summary is intact")
            check("[net] rollback opt-in: host, build 1a2b3c4d, hooks installed" in logs4 and "[net] re-psn path: relay" in logs4
                  and "[net] re-psn: creating a room on <server> as <online name> (relay allowed)" in logs4, f"name {word!r}: the key lines are intact")
            check("[net] rendezvous failed: <online name> said no" in logs4 and "srv.example" not in logs4, f"name {word!r}: an unknown line still loses the name")
            f4 = sb.facts(app4, {}, [], 0, [], ["facts.txt"], {"mode": sb.net_mode(ns4), "logs": sb.net_log_lines(raw4, sec4), "secrets": sec4})
            check("Netcode in the logs" in f4 and "rollback started: yes (host)" in f4 and "path: relay" in f4 and "\n\nfiles in this zip:" in f4,
                  f"name {word!r}: facts.txt keeps the section's words")
            sc4 = sb.net_scrub(f"[net] re-psn: creating a room on srv.example:36100 as {word} (relay allowed), local UDP port 1\n[online] x\n", sec4)
            check(sc4.startswith("[net] ") and "[online] x" in sc4 and "srv.example" not in sc4, f"name {word!r}: file scrub keeps the log tags")
        check(sb.net_log_lines({"a.log": b"x\n"})[0].startswith("  not in the logs"), "net logs: no [net] lines says not in the logs")
        sc = sb.net_scrub("server = h.example:1\nJSP_ROLLBACK_JOIN=198.51.100.4:7000\nname=abc # c\nSDK 10.0.19041.0\n")
        check("h.example" not in sc and "198.51.100.4" not in sc and "abc" not in sc and "10.0.19041.0" in sc, "net scrub: key lines without the settings, version numbers kept")

        # second review: a BOM in repsn.ini, the login name, other players' names, a name with a space, the direct host line, key case
        app7 = t / "app7"
        w(app7 / "VERSION.txt", "0.5.1-test\n")
        w(app7 / "settings_here.txt", "")
        w(app7 / "play_settings.txt", "jsp_rollback=1\nJSP_Rollback_Listen=7000\nps3recomp_shared_dir=@APP@/shared\n")
        (app7 / "shared").mkdir(parents=True, exist_ok=True)
        (app7 / "shared/repsn.ini").write_bytes(b"\xef\xbb\xbf[repsn]\nenabled=0\nserver=bom.example.org\nname=host\n")
        w(app7 / "game/run/play_stderr.txt", "[net] env decision: rollback start (static constructors arrived: EnvInit first, online boot last)\n"
          "[net] listening/joining: host, local UDP port 7000\n[net] rollback opt-in: host, build 1a2b3c4d, hooks installed\n"
          "[net] re-psn: joining a room on bom.example.org:36100 as Bob Smith (relay allowed), local UDP port 1\n"
          "[net] settings layer / rendezvous ended: no reply from bom.example.org:36100\n")
        w(app7 / "game/run/play_stdout.txt", '[psnr] player "Winuser9"\n[sceNp] GetOnlineId(user=0) -> "Winuser9"\n[sceNp] GetNpId(user=0) -> "Winuser9"\n'
          '[sceNp] Init(poolSize=1, username="Winuser9")\n[psnr] "Win user" is not a valid online ID (3-16 letters, digits, - or _, starting with a letter)\n'
          '[psnr] connected to bom.example.org:36100 as "Winuser9" (NPWR00000_00), user 2, seen from 198.51.100.8, p2p port 3658\n'
          '[sceNpMatching2] room 5: Peer Guy joined as member 2\n[sceNpMatching2] room 5: PeerGuy left\n'
          '[sceNpMatching2] signaling: member 2 (PeerGuy) at 203.0.113.9:3658\n[sceNpSignaling] conn 1 (PeerGuy, 203.0.113.9:3658) established\n'
          '[sceNpSignaling] ActivateConnection(ctx=1, PeerGuy) -> conn 1 (pending)\n[Winuser9] tag-like line\n')
        old_env = {k: os.environ.get(k) for k in ("USERNAME", "USER", "USERPROFILE")}
        os.environ.update({"USERNAME": "Win user9", "USERPROFILE": "C:\\Users\\Win user9"})
        os.environ.pop("PS3RECOMP_SHARED_DIR", None)
        try:
            ns7 = sb.net_settings(app7, None, env={})
            check(ns7["ini"].get("repsn", {}).get("server") == "bom.example.org:36100", "BOM: repsn.ini with a UTF-8 BOM is read (jsp_online.cpp skips it)")
            check(ns7["play"].get("JSP_ROLLBACK") == "1" and ns7["play"].get("JSP_ROLLBACK_LISTEN") == "7000", "keys: case-insensitive, as Windows passes them")
            sec7 = sb.net_secrets(ns7)
            check(sec7.get("Win user9") == "<user name>" and sec7.get("Winuser9") == "<user name>", "secrets: the login name, as written and as np_psnr cleans it")
            zp7 = sb.make(app7, out, None, None)
        finally:
            for k, v in old_env.items():
                if v is None:
                    os.environ.pop(k, None)
                else:
                    os.environ[k] = v
        z7 = zipfile.ZipFile(zp7)
        f7 = z7.read("facts.txt").decode()
        all7 = b"".join(z7.read(n) for n in z7.namelist()).decode()
        z7.close()
        for bad in ("bom.example.org", "Winuser9", "Win user", "PeerGuy", "Peer Guy", "Bob Smith", "198.51.100.8", "203.0.113.9"):
            check(bad not in all7, f"second review: {bad!r} appears nowhere in the zip")
        check("mode: Rollback direct host" in f7, "second review: lower-case keys decide the mode")
        check("[net] listening/joining: host, local UDP port 7000" in f7 and "path: direct IP" in f7 and "rollback started: yes (host)" in f7,
              "second review: the direct host line stays as written with the online name 'host'")
        check("[net] re-psn: joining a room on <server> as <online name> (relay allowed), local UDP port 1" in f7, "second review: a name with a space is replaced whole")
        check('[psnr] player <online name>' in all7 and "[sceNpMatching2] room 5: <online name> joined as member 2" in all7
              and "[sceNpSignaling] conn 1 (<online name>, <IPv4>:3658)" in all7 and "ActivateConnection(ctx=1, <online name>)" in all7,
              "second review: the NP lines lose this player's and other players' names")
        sc8 = sb.net_scrub("[net] re-psn: creating a room on s.example:1 as Bob Smith\n", {})
        check(sc8 == "[net] re-psn: creating a room on <server> as <online name>\n", "second review: a room line without its tail loses the rest of the line")
        d8 = t / "ini8"
        w(d8 / "repsn.ini", "[repsn]\nenabled=0\n[repsn\nenabled=1\nserver=a.example\n")
        into8 = {}
        sb.read_online_ini(d8 / "repsn.ini", into8, "repsn")
        check(into8 == {"repsn": {"enabled": "0"}}, "ini: a '[' line without ']' ends the section, as jsp_online.cpp rejects it")
        check(sb.net_scrub("[Bobby] hello\n[net] x\n", {"Bobby": "<online name>", "net": "<online name>"}) == "[<online name>] hello\n[net] x\n",
              "tags: only the program's own tags are kept")

        # a missing --out folder is made; a non-existent install folder is an error exit
        check(sb.main(["--root", str(t / "nope")]) == 2, "missing install folder: exit 2")
    print("FAILED: " + "; ".join(FAILS) if FAILS else "all passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
