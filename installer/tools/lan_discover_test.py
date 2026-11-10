#!/usr/bin/env python3
"""Wire-format test for the launcher's Find on LAN (LanDiscover in launcher/JStarsLauncher19.cs).

    python lan_discover_test.py [REPO_ROOT]

Extracts the LanDiscover code from the launcher, builds it with a small console harness (embedded below) (needs mcs; mono to run it;
set MCS and MONO if they are not on PATH, otherwise the test says SKIP and exits 0) and checks:
  * the request bytes against re-psn's golden layout (64 bytes: "PSNR", 0x04, nonce big-endian, zeros; the same bytes
    its server test builds for nonce 0xdeadbeef);
  * the reply parser against a reply built here field by field from re-psn's documented layout (52 bytes), and that
    malformed replies (wrong length, magic, type, nonce, port 0, non-printable text) are rejected or sanitised;
  * a real search over loopback against a fake responder in this file, which also sends junk and a duplicate;
  * optionally (REPSN_SERVER=path to a re-psn server built from the pinned source) a search against the real server
    started with -discover -name on loopback.
"""
import os, pathlib, shutil, socket, struct, subprocess, sys, tempfile, threading, time

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2])
mcs, mono = os.environ.get("MCS") or shutil.which("mcs"), os.environ.get("MONO") or shutil.which("mono")
if not mcs or not mono:
    print("SKIP: mcs/mono not found (set MCS and MONO)")
    sys.exit(0)

fails = []

# Console harness: calls the launcher's LanDiscover class so the packet layout can be checked without a window.
# Modes: req NONCE_HEX | parse REPLY_HEX NONCE_HEX FROM_IP | find PORT TIMEOUT_MS DEST_IP...
HARNESS = r'''
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;

namespace JStarsLauncher
{
    static class Harness
    {
        static string Hex(byte[] b) { var sb = new StringBuilder(); foreach (byte x in b) sb.Append(x.ToString("x2")); return sb.ToString(); }
        static byte[] Unhex(string s) { var b = new byte[s.Length / 2]; for (int i = 0; i < b.Length; i++) b[i] = Convert.ToByte(s.Substring(i * 2, 2), 16); return b; }
        static string Show(LanServer s)
        {
            return s.Ip + "|" + s.TcpPort + "|" + s.UdpPort + "|" + (s.Relay ? 1 : 0) + "|" + s.Players + "|" + s.Name + "|" + s.Version;
        }
        static int Main(string[] a)
        {
            if (a[0] == "req") { Console.WriteLine(Hex(LanDiscover.BuildRequest(Convert.ToUInt32(a[1], 16)))); return 0; }
            if (a[0] == "parse")
            {
                byte[] b = Unhex(a[1]);
                var s = LanDiscover.ParseReply(b, b.Length, Convert.ToUInt32(a[2], 16), a[3]);
                Console.WriteLine(s == null ? "null" : Show(s));
                return 0;
            }
            if (a[0] == "find")
            {
                var d = new List<System.Net.IPAddress>();
                for (int i = 3; i < a.Length; i++) d.Add(System.Net.IPAddress.Parse(a[i]));
                var r = LanDiscover.Find(int.Parse(a[1]), int.Parse(a[2]), d);
                Console.WriteLine("count=" + r.Count);
                foreach (var s in r) Console.WriteLine(Show(s));
                return 0;
            }
            return 2;
        }
    }
}
'''
def check(name, ok):
    print(("ok   " if ok else "FAIL ") + name)
    if not ok:
        fails.append(name)

cs = (root / "launcher/JStarsLauncher19.cs").read_text(encoding="utf-8")
code = cs[cs.index("    // LAN discovery for re-psn"):cs.index("    class Preset {")]
tmp = pathlib.Path(tempfile.mkdtemp(prefix="lantest"))
head = "using System;\nusing System.Collections.Generic;\nusing System.Diagnostics;\nusing System.Text;\nnamespace JStarsLauncher {\n"
(tmp / "lan.cs").write_text(head + code + "\n}\n", encoding="utf-8")
exe = tmp / "h.exe"
(tmp / "harness.cs").write_text(HARNESS, encoding="utf-8")
r = subprocess.run([mcs, "-out:" + str(exe), str(tmp / "lan.cs"), str(tmp / "harness.cs")],
                   capture_output=True, text=True)
check("harness compiles with no errors or warnings", r.returncode == 0 and "warning" not in r.stdout + r.stderr)
if r.returncode:
    print(r.stdout[-800:], r.stderr[-800:])
    sys.exit(1)

def run(*a, timeout=20):
    return subprocess.run([mono, str(exe)] + [str(x) for x in a], capture_output=True, text=True, timeout=timeout).stdout.strip().splitlines()

def reply(nonce, tcp=4242, udp=36100, flags=1, players=3, name=b"lan box", ver=b"v0.5.0", magic=b"PSNR", typ=0x84):
    return (magic + bytes([typ]) + struct.pack(">IHHBH", nonce, tcp, udp, flags, players)
            + name.ljust(24, b"\0")[:24] + ver.ljust(12, b"\0")[:12])

# request: golden bytes
want = b"PSNR" + b"\x04" + bytes.fromhex("deadbeef") + bytes(64 - 9)
check("request is the 64-byte DISCOVER of re-psn (nonce 0xdeadbeef)", run("req", "deadbeef") == [want.hex()])
check("request is 64 bytes", len(want) == 64)

# reply parsing
N = 0xdeadbeef
good = reply(N)
check("reply is 52 bytes", len(good) == 52)
check("good reply parsed", run("parse", good.hex(), "deadbeef", "192.0.2.7") == ["192.0.2.7|4242|36100|1|3|lan box|v0.5.0"])
check("relay off, 65535 players", run("parse", reply(N, flags=0, players=65535).hex(), "deadbeef", "192.0.2.7")[0] == "192.0.2.7|4242|36100|0|65535|lan box|v0.5.0")
check("ports above 32767 read as unsigned", run("parse", reply(N, tcp=65535, udp=40000).hex(), "deadbeef", "192.0.2.7")[0].startswith("192.0.2.7|65535|40000|"))
bad = {
    "short": good[:51], "long": good + b"\0", "empty": b"",
    "magic": reply(N, magic=b"PSNX"), "type": reply(N, typ=0x04), "nonce": reply(N + 1), "port 0": reply(N, tcp=0),
}
for k, v in bad.items():
    out = run("parse", v.hex() if v else "", "deadbeef", "192.0.2.7") if v else ["null"]
    check("rejected: " + k, out == ["null"])
check("control and non-ASCII bytes in text become ?", run("parse", reply(N, name=b"a\x01b\xffc\x7fd", ver=b"1\x1b2").hex(), "deadbeef", "192.0.2.7")[0].endswith("|a?b?c?d|1?2"))
check("text stops at the first NUL", run("parse", reply(N, name=b"ab\0cd").hex(), "deadbeef", "192.0.2.7")[0].endswith("|ab|v0.5.0"))
check("full 24-byte name with no NUL", run("parse", reply(N, name=b"x" * 24).hex(), "deadbeef", "192.0.2.7")[0].split("|")[5] == "x" * 24)

# fake responder over loopback
class Fake(threading.Thread):
    def __init__(self):
        super().__init__(daemon=True)
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.bind(("127.0.0.1", 0))
        self.port = self.s.getsockname()[1]
        self.got = []
    def run(self):
        self.s.settimeout(5)
        try:
            data, addr = self.s.recvfrom(256)
        except OSError:
            return
        self.got.append(data)
        if len(data) != 64 or data[:5] != b"PSNR\x04":
            return
        n = struct.unpack(">I", data[5:9])[0]
        self.s.sendto(b"junk", addr)
        self.s.sendto(reply(n + 7), addr)                       # wrong nonce
        self.s.sendto(reply(n, tcp=0), addr)                    # port 0
        self.s.sendto(reply(n, name=b"first", tcp=36100), addr)
        self.s.sendto(reply(n, name=b"again", tcp=36100), addr)  # duplicate ip:port
        self.s.sendto(reply(n, name=b"second", tcp=36200, flags=0, players=0), addr)
f = Fake()
f.start()
out = run("find", f.port, 800, "127.0.0.1")
f.join(3)
check("fake responder received one 64-byte DISCOVER", len(f.got) == 1 and len(f.got[0]) == 64 and f.got[0][:5] == b"PSNR\x04" and f.got[0][9:] == bytes(55))
check("search keeps the two valid distinct servers", out == ["count=2", "127.0.0.1|36100|36100|1|3|first|v0.5.0", "127.0.0.1|36200|36100|0|0|second|v0.5.0"])
t0 = time.time()
silent = run("find", 9, 400, "127.0.0.1")
check("no server: count=0 after about the timeout", silent == ["count=0"] and 0.3 < time.time() - t0 < 3)

# the real server, if given
srv = os.environ.get("REPSN_SERVER")
if srv and os.path.exists(srv):
    s0 = socket.socket(); s0.bind(("127.0.0.1", 0)); port = s0.getsockname()[1]; s0.close()
    p = subprocess.Popen([srv, "-addr", "127.0.0.1:%d" % port, "-http", "", "-db", "off", "-discover", "-name", "lan box"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(1.0)
        out = run("find", port, 1200, "127.0.0.1")
        check("real re-psn server found: " + (out[1] if len(out) > 1 else "none"), out[:1] == ["count=1"] and out[1].startswith("127.0.0.1|%d|%d|0|0|lan box|" % (port, port)))
    finally:
        p.terminate(); p.wait()
else:
    print("note REPSN_SERVER not set: real-server check not run")

shutil.rmtree(tmp, ignore_errors=True)
print("FAILED: " + ", ".join(fails) if fails else "all ok")
sys.exit(1 if fails else 0)
