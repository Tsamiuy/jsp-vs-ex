#!/usr/bin/env python3
"""Host test for support_bundle.py: a synthetic install tree in a temp folder, no game data, no network.
    python3 -I installer/tools/support_bundle_test.py
"""
import os
import pathlib
import struct
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
        w(ad3 / sb.APPDIR / "play_settings.txt", "JSP_ROLLBACK=1\nJSP_ROLLBACK_VIA=repsn\nJSP_ROLLBACK_ROLE=host\nJSP_ROLLBACK_RELAY=1  # relay\nJSP_ROLLBACK_BADGE=1\n")
        w(ad3 / "PS3Recomp/repsn.ini", f"[repsn]\nenabled = 0\nserver = {srv}:36100\nname = {nm}\n")
        head = ("[net] env decision: rollback start (static constructors arrived: EnvInit first, online boot last)\n"
                f"[net] re-psn: creating a room on {srv}:36100 as {nm} (relay allowed), local UDP port 3659\n"
                "[net] rollback opt-in: host, build 1a2b3c4d, hooks installed\n[net] status badge: on\n"
                f"[net] rendezvous: {peer}:3659 via re-psn, path relay, room 7\n"
                "[net] re-psn path: relay\n[net] gekko session synchronized\n")
        ticks = "".join(f"[net] commit tick={i} hash=0000abcd raw=0000abcd\n" for i in range(12000))
        rb = "[net] rollback to tick={} (longest game-thread stretch without a session call since the last rollback: 4 ms, keepalive polls so far: 0)\n"
        tail = (rb.format(5) + "[net] status: path relay, delay 2f, last rollback 3f, largest 6f, rollbacks 1\n" + rb.format(9) + rb.format(11) +
                "[net] status: path relay, delay 2f, last rollback 2f, largest 6f, rollbacks 3\n" +
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
        check("rollback status badge (JSP_ROLLBACK_BADGE): on" in f3, "net facts: the badge key from play_settings.txt")
        check("last status line: path relay, delay 2f, last rollback 2f, largest 6f, rollbacks 3   status badge: on" in f3,
              "net facts: the last status line (path, delay, last and largest rollback) and the badge survive the cut")
        check("[net] status: " not in f3.split("key [net] lines of")[1], "net facts: status lines are not repeated among the key lines")
        check("env decision: rollback start (static constructors arrived: EnvInit first, online boot last)" in f3 and "[net] rollback opt-in: host, build 1a2b3c4d" in f3 and "commit tick=" not in f3, "net facts: key lines kept, per-tick lines left out")
        prev = f3.split("play_stderr.prev.txt (the launch before):")[1]
        check("refused: JSP_ROLLBACK=1 and the game's own online" in prev and "rollback started: no" in prev, "net facts: the previous launch's refusal and its reason")
        for bad in (srv, nm, peer):
            check(bad not in all3, f"net: {bad!r} appears nowhere in the zip")
        check("<server>" in all3 and "<online name>" in all3 and "[net] rendezvous: <peer address> via re-psn, path relay, room 7" in f3 and "127.0.0.1:36100 stays" in all3, "net: placeholders in the files, loopback kept")
        S = lambda play, ini=None: "\n".join(sb.net_mode({"play": play, "ini": ini or {}, "read": []}))
        check("mode: Off" in S({}), "net mode: nothing set is Off")
        check("rollback status badge (JSP_ROLLBACK_BADGE): off" in S({}) and "badge (JSP_ROLLBACK_BADGE): off" in S({"JSP_ROLLBACK_BADGE": "0"})
              and "badge (JSP_ROLLBACK_BADGE): on" in S({"JSP_ROLLBACK_BADGE": "yes"}), "net mode: the badge key, off unless non-zero (as jpnb::key_on)")
        nb = "\n".join(sb.net_log_lines({"game/run/play_stderr.txt": "[net] rollback opt-in: client, build 1a2b3c4d, hooks installed\n[net] status badge: off\n"
                                            "[net] status: path direct, delay -, last rollback -, largest -, rollbacks 0\n"
                                            "[net] status: path 198.51.100.4, delay 2f, last rollback 1f, largest 1f, rollbacks 1\n"}))
        check("last status line: path direct, delay -, last rollback -, largest -, rollbacks 0   status badge: off" in nb and "198.51.100.4" not in nb,
              "net logs: only the exact status line counts; a line with an address in it is neither summed up nor printed")
        check("last status line: not in the logs   status badge: not in the logs" in "\n".join(sb.net_log_lines({"game/run/play_stderr.txt": "[net] gekko session synchronized\n"})),
              "net logs: an older game without the status line says so")
        dj = S({"JSP_ROLLBACK": "1", "JSP_ROLLBACK_JOIN": "198.51.100.4:7000"})
        check("mode: Rollback direct join" in dj and "join address: set" in dj and "198.51.100.4" not in dj, "net mode: direct join, address only as set")
        check("mode: Rollback direct host" in S({"JSP_ROLLBACK": "1", "JSP_ROLLBACK_LISTEN": "7000"}), "net mode: direct host")
        check("mode: Rollback lobby: no session at start" in S({"JSP_ROLLBACK": "1"}), "net mode: no role key and no online is the lobby start")
        check("mode: Rollback direct refused: not exactly one" in S({"JSP_ROLLBACK": "1", "JSP_ROLLBACK_LISTEN": "7000", "JSP_ROLLBACK_JOIN": "198.51.100.4:7000"}),
              "net mode: both role keys are refused")
        check("mode: Off" in S({"JSP_ROLLBACK": "0", "JSP_ROLLBACK_LISTEN": "7000"}), "net mode: JSP_ROLLBACK=0 is off")
        rep_on = {"repsn": {"enabled": "1", "server": "x.example:1", "name": "abc"}}
        check("mode: Delay (re-psn)" in S({}, rep_on), "net mode: Delay over re-psn")
        check("both enabled" in S({}, {**rep_on, "community": {"enabled": "1"}}), "net mode: re-psn and community both on")
        check("mode: Off (community server enabled: no client yet" in S({}, {"community": {"enabled": "1"}}), "net mode: a community server keeps the game offline")
        check("mode: Rollback refused: PS3_NET_ONLINE is set too" in S({"JSP_ROLLBACK": "1", "JSP_ROLLBACK_LISTEN": "7000"}, rep_on), "net mode: rollback with a role key and Delay both on are refused")
        # ENHANCEMENT (native-room pairing): the tester default, re-psn on + JSP_ROLLBACK=1 without a role key, is Online Battle + rollback
        nat = S({"JSP_ROLLBACK": "1", "JSP_ROLLBACK_RELAY": "1"}, rep_on)
        check("mode: Online Battle + rollback (the game's Friendly Match room pairs the players; rollback runs the fight), relay allowed: yes" in nat
              and "pairing off" not in nat, "net mode: the tester default")
        check("relay allowed: no, room pairing off (JSP_ROLLBACK_NATIVE=0)" in S({"JSP_ROLLBACK": "1", "JSP_ROLLBACK_NATIVE": "0"}, rep_on), "net mode: native pairing off is named")
        check("mode: Rollback refused" in S({"JSP_ROLLBACK": "1", "JSP_ROLLBACK_VIA": "repsn", "JSP_ROLLBACK_ROLE": "host"}, rep_on), "net mode: a re-psn role start with Delay on is refused")
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
            check("last status line: path relay, delay 2f, last rollback 2f, largest 6f, rollbacks 3   status badge: on" in logs4, f"name {word!r}: the status line is intact")
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
        os.environ.update({"USERNAME": "Win user9", "USERPROFILE": "\\".join(["C:", "Users", "Win user9"])})   # a made-up profile, joined so the repo check does not read it as a real path
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

        # third review: CRLF logs, names that are words of the log lines, commented-out and earlier values, brackets, apostrophes, file names
        S1 = {"Me1": "<online name>"}
        crlf = [
            ("[sceNpMatching2] room 1: PeerGuy joined as member 2\r\n", "[sceNpMatching2] room 1: <online name> joined as member 2\r\n"),
            ("[sceNpMatching2] room 1: PeerGuy left\r\n", "[sceNpMatching2] room 1: <online name> left\r\n"),
            # the lines of toolkit patch 6019 hold no name or address and pass as they are
            ("[sceNpMatching2] room 1: member 2 left\r\n", "[sceNpMatching2] room 1: member 2 left\r\n"),
            ("[sceNpMatching2] room 1: member 2 joined\r\n", "[sceNpMatching2] room 1: member 2 joined\r\n"),
            ("[sceNpMatching2] signaling: member 2 active\r\n", "[sceNpMatching2] signaling: member 2 active\r\n"),
            ("[sceNpSignaling] conn 1 established\r\n", "[sceNpSignaling] conn 1 established\r\n"),
            ("[sceNpSignaling] ActivateConnection(ctx=1) -> conn 1 (pending)\r\n", "[sceNpSignaling] ActivateConnection(ctx=1) -> conn 1 (pending)\r\n"),
            ("[sceNp] GetNpId(user=0) -> online id set (8 chars), call 1\r\n", "[sceNp] GetNpId(user=0) -> online id set (8 chars), call 1\r\n"),
            ("[psnr] player online id set (8 chars)\r\n", "[psnr] player online id set (8 chars)\r\n"),
            ("[psnr] connected to the server (port 36100) (NPWR1_00), user 2, p2p port 3658\r\n",
             "[psnr] connected to the server (port 36100) (NPWR1_00), user 2, p2p port 3658\r\n"),
            ("[sceNpMatching2] signaling: member 2 (PeerGuy) at 203.0.113.9:3658\r\n", "[sceNpMatching2] signaling: member 2 (<online name>) at <IPv4>:3658\r\n"),
            ("[sceNpSignaling] conn 1 (PeerGuy, 203.0.113.9:3658) established\r\n", "[sceNpSignaling] conn 1 (<online name>, <IPv4>:3658) established\r\n"),
            ("[sceNpSignaling] ActivateConnection(ctx=1, PeerGuy) -> conn 1 (pending)\r\n", "[sceNpSignaling] ActivateConnection(ctx=1, <online name>) -> conn 1 (pending)\r\n"),
            ("[net] re-psn: creating a room on srv.example.org:36100 as Bob Smith (relay allowed), local UDP port 4000\r\n",
             "[net] re-psn: creating a room on <server> as <online name> (relay allowed), local UDP port 4000\r\n"),
            ("[net] re-psn: the online name 'O'Brien the Great' is longer than 16 characters (repsn.ini name=): off\r\n",
             "[net] re-psn: the online name '<online name>' is longer than 16 characters (repsn.ini name=): off\r\n"),
            ("[net] rendezvous: peer.example.org:7000 via re-psn, path direct, room 3\r\n", "[net] rendezvous: <peer address> via re-psn, path direct, room 3\r\n"),
            ('[psnr] player "Bob X"\r\n', "[psnr] player <online name>\r\n"),
            ('[psnr] "Bob "X" is not a valid online ID (3-16 letters, digits, - or _, starting with a letter)\r\n',
             "[psnr] <online name> is not a valid online ID (3-16 letters, digits, - or _, starting with a letter)\r\n"),
            ('[psnr] connected to [2001:db8::1]:36100 as "Me 1" (NPWR1_00), user 2, seen from 198.51.100.8, p2p port 3658\r\n',
             "[psnr] connected to <server>:36100 as <online name> (NPWR1_00), user 2, seen from <IPv4>, p2p port 3658\r\n"),
            ('[psnr] srv.example.org:36100: another player there is already "Me 1" -- pick another with --username. NP stays offline\r\n',
             "[psnr] <server>:36100: another player there is already <online name> -- pick another with --username. NP stays offline\r\n"),
            ("[psnr] could not reach 2001:db8::1:36100 -- NP stays offline\r\n", "[psnr] could not reach <server>:36100 -- NP stays offline\r\n"),
            ('[sceNp] GetOnlineId(user=0) -> "Me 1"\r\n', "[sceNp] GetOnlineId(user=0) -> <online name>\r\n"),
            ('[sceNp] Init(poolSize=1, username="Me 1")\r\n', "[sceNp] Init(poolSize=1, username=<online name>)\r\n"),
            ("[online] re-psn enabled: server srv.example.org:36100\r\n", "[online] re-psn enabled: server <server>\r\n"),
            ("[online] online id Me1\r\n", "[online] online id <online name>\r\n"),
            ('server = "srv.example.org"\r\n', "server = <set>\r\n"),
            ("#PSNR_SERVER=Old.Server.net\r\n", "#PSNR_SERVER=<set>\r\n"),
            ("; server = old.example\r\n", "; server = <set>\r\n"),
        ]
        for raw, want in crlf:
            got = sb.net_scrub(raw, S1)
            check(got == want, f"rule with CRLF: {raw.strip()[:60]!r} -> {got!r}")
            check(sb.net_scrub(raw.replace("\r\n", "\n"), S1) == want.replace("\r\n", "\n"), f"rule with LF: {raw.strip()[:60]!r}")
        peer = ("[sceNpMatching2] room 1: PeerGuy joined as member 2\r\n[sceNpMatching2] room 1: PeerGuy left\r\n"
                "[sceNpSignaling] conn 1 (PeerGuy, 203.0.113.9:3658) established\r\n[sceNpMatching2] signaling: member 2 (PeerGuy) at 203.0.113.9:3658\r\n")
        for word in ("member", "room", "left", "conn", "signaling", "User"):
            sc9 = sb.net_scrub(peer, {word: "<online name>"})
            check("PeerGuy" not in sc9 and "203.0.113.9" not in sc9, f"a secret {word!r} that is a word of the lines does not keep the peer's name")
        sec9 = sb.net_secrets({"play": {"PS3_NP_ONLINE_ID": "Other1"}, "play_raw": [("PSNR_SERVER", "Old.Server.net"), ("PS3_NP_ONLINE_ID", "RealName"),
                                                                                  ("PS3_NP_ONLINE_ID", "Real Name!")], "ini": {},
                               "ini_raw": {"repsn": {"server": ["old.example"], "name": ["Gone1", "Bob Z"]}}}, env={})
        for v in ("Old.Server.net", "RealName", "Real Name!", "RealName", "old.example", "Gone1", "BobZ", "Other1"):
            check(v in sec9, f"secrets: {v!r} (commented out, earlier, or cleaned as np_psnr does)")
        app9 = t / "app9"
        w(app9 / "VERSION.txt", "x\n")
        w(app9 / "settings_here.txt", "")
        w(app9 / "play_settings.txt", "#PSNR_SERVER=Old.Server.net\n# PS3_NP_ONLINE_ID=RealName\nPS3_NP_ONLINE_ID=Earlier1\nPS3_NP_ONLINE_ID=Later1\n"
          "PS3RECOMP_SHARED_DIR=@APP@/shared\n")
        w(app9 / "shared/repsn.ini", "[repsn]\n; server=retired.example\nenabled=1\nserver=srv.example.org\nname=Bobby\n")
        w(app9 / "Bobby-p2.txt", "notes of Old.Server.net and RealName and Earlier1 and retired.example\n")
        w(app9 / "game/run/play_stdout.txt", "[sceNpMatching2] room 1: PeerGuy left\r\n[Bobby] x\r\n")
        zp9 = sb.make(app9, out, None, None)
        z9 = zipfile.ZipFile(zp9)
        n9 = z9.namelist()
        all9 = "\n".join(n9) + b"".join(z9.read(n) for n in n9).decode()
        z9.close()
        for bad in ("Old.Server.net", "RealName", "Earlier1", "Later1", "retired.example", "srv.example.org", "Bobby", "PeerGuy"):
            check(bad not in all9, f"third review: {bad!r} appears nowhere in the zip, file names included")
        check("(online name)-p2.txt" in n9, "file names: a private value in a user file's name becomes a legal placeholder")
        check(sb.net_scrub("chat from [Bob] here\n[Bob] x\n[net] y\n", {"[Bob]": "<online name>", "net": "<online name>"})
              == "chat from <online name> here\n<online name> x\n[net] y\n", "tags: a secret written in brackets is replaced; [net] stays")
        d10 = t / "ini10"
        w(d10 / "repsn.ini", "[repsn]\nenabled=1\nname=" + "x" * 600 + "\nserver=a.example\n")
        into10, raw10 = {}, {}
        sb.read_online_ini(d10 / "repsn.ini", into10, "repsn", raw10)
        check(into10 == {"repsn": {"enabled": "1", "server": "a.example:36100"}} and raw10["repsn"]["name"] == ["x" * 600],
              "ini: a line over 512 bytes is rejected and the section stays, its value still a secret")

        # fourth review: '#' and ';' inside values, peer names with ',' or ')', preset labels, unknown sections, the label of the launch before
        for raw, want in [("PS3_NP_ONLINE_ID=Neo #Anderson\r\n", "PS3_NP_ONLINE_ID=<set>\r\n"), ("PSNR_SERVER=a.example;Zed.example\n", "PSNR_SERVER=<set>\n"),
                          ("JSP_ROLLBACK_JOIN=1.2.3.4:5 # note\n", "JSP_ROLLBACK_JOIN=<set>\n"), ("name=Bob;x\r\n", "name=<set>\r\n"),
                          ("server = s.example # old\n", "server = <set>\n"), ("; name = Bob#x\n", "; name = <set>\n"),
                          ("[preset Friend House]\r\n", "[preset <preset>]\r\n"), ("[Alice server]\n", "[<set>]\n"), ("[repsn]\r\n", "[repsn]\r\n"),
                          ("[community]\n", "[community]\n"),
                          ("[online] community method enabled (preset FriendHouse): not supported yet, staying offline\n",
                           "[online] community method enabled (preset <preset>): not supported yet, staying offline\n"),
                          ("[online] C:/x/repsn.ini:3: rejected (unknown section [Alice server])\r\n", "[online] C:/x/repsn.ini:3: rejected (unknown section [<set>])\r\n"),
                          ("[sceNpSignaling] conn 1 (Peer, Guy), 203.0.113.9:3658) established\r\n", "[sceNpSignaling] conn 1 (<online name>, <IPv4>:3658) established\r\n"),
                          ("[sceNpSignaling] ActivateConnection(ctx=1, Pe)er) -> conn 1 (pending)\r\n", "[sceNpSignaling] ActivateConnection(ctx=1, <online name>) -> conn 1 (pending)\r\n"),
                          ("[sceNpMatching2] signaling: member 2 (Pe) atX) at 1.2.3.4:5\r\n", "[sceNpMatching2] signaling: member 2 (<online name>) at <IPv4>:5\r\n")]:
            got = sb.net_scrub(raw, {})
            check(got == want, f"fourth review: {raw.strip()[:50]!r} -> {got!r}")
        app11 = t / "app11"
        w(app11 / "VERSION.txt", "x\n")
        w(app11 / "settings_here.txt", "")
        w(app11 / "play_settings.txt", "PS3_NP_ONLINE_ID=prev #Anderson\nPSNR_SERVER=a.example;Zed.example\nPS3RECOMP_SHARED_DIR=@APP@/shared\n")
        w(app11 / "shared/repsn.ini", "[repsn]\nenabled=0\nname=Bob;x\n")
        w(app11 / "shared/community.ini", "[community]\nenabled=1\nactive=FriendHouse\n[preset FriendHouse]\naddress=c.example\n")
        w(app11 / "game/run/play_stderr.prev.txt", "[net] rollback opt-in: host, build 1a2b3c4d, hooks installed\n")
        w(app11 / "game/run/play_stderr.txt", "[net] gekko session synchronized\n")
        w(app11 / "game/run/play_stdout.txt", "[online] community method enabled (preset FriendHouse): not supported yet\nFriendHouse again\n")
        zp11 = sb.make(app11, out, None, None)
        z11 = zipfile.ZipFile(zp11)
        all11 = "\n".join(z11.namelist()) + b"".join(z11.read(n) for n in z11.namelist()).decode()
        f11 = z11.read("facts.txt").decode()
        z11.close()
        for bad in ("Anderson", "Zed.example", "Bob;x", ";x", "FriendHouse", "c.example"):
            check(bad not in all11, f"fourth review: {bad!r} appears nowhere in the zip")
        check("(the launch before):" in f11 and "rollback started: yes (host)" in f11, "fourth review: the launch before keeps its label when a secret renames its file")

        # cache files: sizes and record counts (a pipelines.bin that passed 2 GiB stopped loading, so its size is what a report needs)
        app12 = t / "app12"
        run12 = app12 / "game" / "run"
        run12.mkdir(parents=True)

        def pipe(path, keys, head, hdr=40, vs=1 << 20, ps=1 << 20, cut=0):
            with open(path, "wb") as fh:
                if head:
                    fh.write(struct.pack("<IIQII", 0x48575350, 3, 99, hdr, 0))
                for k in keys:                                   # a sparse file: only the heads are real bytes
                    fh.write(struct.pack("<IIIIQ", 0x32575350, vs, ps, 16, k) + b"\0" * (hdr - 24))
                    fh.seek(vs + ps - 1, 1)
                    fh.write(b"\0")
                if cut:
                    fh.truncate(fh.tell() - cut)

        reps = 1100 if os.name != "nt" else 3                                 # 2.2 GB sparse on POSIX; NTFS would write the holes, so a small file there
        pipe(run12 / "pipelines.bin", [1] + [2] * reps, head=True)            # reps repeats of one key
        size12 = (run12 / "pipelines.bin").stat().st_size
        check(size12 > 2 ** 31 or os.name == "nt", "the synthetic pipelines.bin is over 2 GiB (POSIX)")
        L12 = sb.cache_lines(app12)
        check(L12 == [f"cache pipelines.bin: {size12} bytes, file head format 3, record head 40 bytes, {reps + 1} records (2 distinct, {reps - 1} repeated), 0 bytes after the last whole record"],
              f"cache line of a pipelines.bin over 2 GiB: {L12}")
        pipe(run12 / "pipelines.bin", [5, 6, 7], head=False, hdr=64, vs=300, ps=200, cut=10)   # older build: no head, record size inferred, torn tail
        s12 = (run12 / "pipelines.bin").stat().st_size
        check(sb.cache_lines(app12) == [f"cache pipelines.bin: {s12} bytes, no file head (written by an older build), 2 records (2 distinct, 0 repeated), {64 + 500 - 10} bytes after the last whole record"],
              f"cache line of a pipelines.bin without a head: {sb.cache_lines(app12)}")
        with open(run12 / "shader_cache.bin", "wb") as fh:
            for k in (1, 2, 1):
                fh.write(struct.pack("<IIQ", 0x31435344, 5, k) + b"hello")
            fh.write(struct.pack("<IIQ", 0x31435344, 50, 9) + b"cut")           # torn last record
        s13 = (run12 / "shader_cache.bin").stat().st_size
        check(sb.cache_lines(app12)[1] == f"cache shader_cache.bin: {s13} bytes, 3 records (2 distinct, 1 repeated), {16 + 3} bytes after the last whole record",
              f"cache line of shader_cache.bin: {sb.cache_lines(app12)}")
        (run12 / "pso_cache.bin").write_bytes(b"")
        check(sb.cache_lines(app12)[-1] == "cache pso_cache.bin: 0 bytes, 0 records (0 distinct, 0 repeated), 0 bytes after the last whole record", "an empty cache file")
        check(sb.cache_lines(t / "app-none") == ["cache files (pipelines.bin, shader_cache.bin, pso_cache.bin): none found"], "no cache files: said so")
        f12 = sb.facts(app12, {}, [], 0, [], ["facts.txt"])
        check("\ncache pipelines.bin: " in f12 and "\ncache shader_cache.bin: " in f12, "facts.txt carries the cache lines")
        zp12 = sb.make(app12, out, None, None)
        z12 = zipfile.ZipFile(zp12)
        check(not any(n.endswith(".bin") for n in z12.namelist()), "the cache files themselves are not in the zip")
        z12.close()

        # ENHANCEMENT (testers): the rollback match reports in game\run\reports, newest REPORTS_KEPT only; other files there are not taken
        app14 = t / "app14"
        for i in range(sb.REPORTS_KEPT + 2):
            p14 = app14 / f"game/run/reports/jsp-match-report-20261121-1200{i:02d}.txt"
            w(p14, f"report {i}\n")
            os.utime(p14, (1_700_000_000 + i, 1_700_000_000 + i))
        w(app14 / "game/run/reports/other.txt", "not a report\n")
        got14 = sorted(n for n in sb.collect(app14)[0] if n.startswith("game/run/reports/"))
        check(len(got14) == sb.REPORTS_KEPT and "game/run/reports/jsp-match-report-20261121-120000.txt" not in got14
              and f"game/run/reports/jsp-match-report-20261121-1200{sb.REPORTS_KEPT + 1:02d}.txt" in got14, f"match reports: the newest {sb.REPORTS_KEPT}: {got14}")

        # FIX (testers): a match report keeps its section headers (they were all turned into [<set>]); its private values are still scrubbed,
        # also an online name that reads like a header ("Session"); an ini's unknown section is still scrubbed in the same bundle
        app15 = t / "app15"
        w(app15 / "VERSION.txt", "x\n")
        w(app15 / "settings_here.txt", "")
        w(app15 / "play_settings.txt", "PS3_NP_ONLINE_ID=Session\nPSNR_SERVER=srv15.example:36100\nPS3RECOMP_SHARED_DIR=" + str(app15 / "shared") + "\n")
        w(app15 / "shared/repsn.ini", "[repsn]\nenabled=1\nserver=srv15.example\nname=Session\n[Alice House]\nx=1\n")
        report15 = ("# jsp-match-report 1\r\n# J-Stars Victory VS+ port: online rollback match report. Safe to share: addresses, home folders and names are replaced.\r\n"
                    "written: 2026-11-21 12:00:00 (local time)\r\nbuild: 0.7.0-test5\r\n\r\n[end]\r\nreason: desync at tick 1200\r\n\r\n"
                    "[session]\r\nlocal_slot: 1\r\nplayers: 2\r\ninput_delay: 2\r\nbase_tick: 100\r\nlink: relay via srv15.example:36100\r\n\r\n"
                    "[settings]\r\nPSNR_SERVER=srv15.example:36100\r\nJSP_ROLLBACK=1\r\n\r\n[rollbacks]\r\ncount: 1  max_depth: 3  avg_depth: 3.00  kept: 1 (oldest first: from_tick depth)\r\n1190 3\r\n\r\n"
                    "[commits]\r\ncount: 1  kept: 1 (oldest first: tick hash raw, hex)\r\n1200 00000000deadbeef 00000000deadbeef\r\n\r\n"
                    "[notes]\r\npeer Session at 203.0.113.7:3658\r\n\r\n[truncated]\r\n# end of report\r\n")
        w(app15 / "game/run/reports/jsp-match-report-20261121-120000.txt", report15)
        w(app15 / "logs/first.log", "[repsn]\r\n[Alice House]\r\n")
        z15 = zipfile.ZipFile(sb.make(app15, out, None, None))
        r15 = z15.read("game/run/reports/jsp-match-report-20261121-120000.txt").decode("utf-8")
        log15 = z15.read("logs/first.log").decode("utf-8") if "logs/first.log" in z15.namelist() else ""
        z15.close()
        heads15 = [l for l in r15.splitlines() if l.startswith("[")]
        check(heads15 == ["[end]", "[session]", "[settings]", "[rollbacks]", "[commits]", "[notes]", "[truncated]"], f"match report: section headers kept: {heads15}")
        check("[<set>]" not in r15 and "1190 3" in r15 and "1200 00000000deadbeef" in r15, "match report: the body is kept")
        check("srv15" not in r15 and "203.0.113.7" not in r15 and "peer Session" not in r15 and "PSNR_SERVER=<set>" in r15,
              f"match report: server, peer address and online name still scrubbed: {r15!r}")
        check("[repsn]" in log15 and "[<set>]" in log15 and "Alice" not in log15, f"another file in the same bundle: an unknown section is still scrubbed: {log15!r}")
        check(sb.net_scrub("[notes]\n[Bob]\n", {}) == "[<set>]\n[<set>]\n" and sb.net_scrub("[notes]\n[Bob]\n", {}, sb.REPORT_SECTIONS) == "[notes]\n[<set>]\n",
              "net_scrub: report headers kept only when asked for, other headers scrubbed")
        check(sb.is_match_report("game/run/reports/jsp-match-report-1.txt") and not sb.is_match_report("game/run/reports/other.txt")
              and not sb.is_match_report("logs/jsp-match-report-1.txt"), "is_match_report: only the reports folder's report files")

        # a missing --out folder is made; a non-existent install folder is an error exit
        check(sb.main(["--root", str(t / "nope")]) == 2, "missing install folder: exit 2")
    print("FAILED: " + "; ".join(FAILS) if FAILS else "all passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
