#!/usr/bin/env python3
"""The settings window's button for the optional re-psn desktop app (RepsnApp in launcher/JStarsLauncher.cs and the v19 launcher).

    python launcher_repsn_app_test.py [REPO_ROOT]

Extracts the RepsnApp class (with its RepsnAppState enum) from both launchers, which must carry the same RepsnApp class, builds it with a
small console harness (embedded below) and checks it on temporary folders (needs mcs; mono to run it; set MCS and MONO if they
are not on PATH, otherwise the test says SKIP and exits 0, which CI's --strict-skip counts as a failure; on Windows the C#
compiler that ships with .NET Framework is used):
  * Find: an empty install folder is Missing; re-psn-app.exe and re-psn-host.exe is Ready (re-psn-window.exe is not required and
    is ignored if present); either of the two missing is Incomplete; a file named like the folder is Missing;
  * StartInfo: <root>/<Folder>/re-psn-app.exe with the arguments "ui --browser", the app folder as working folder, UseShellExecute false;
  * Folder is re-psn-app-v<version>-windows-amd64 for the version in installer/tools/toolchain.json.
Source checks (no compiler needed, always run): the button exists only for an installed launcher, the app starts with "ui --browser"
(only) and is never waited for, no firewall or netsh anywhere, and the caveat words are the installer's.
"""
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2]).resolve()
fails = []


def check(name, ok):
    print(("ok   " if ok else "FAIL ") + name)
    if not ok:
        fails.append(name)


MARK = "    // ENHANCEMENT: the optional re-psn desktop app (the installer's unticked task"


def repsn_class(rel):
    cs = (root / rel).read_text(encoding="utf-8").replace("\r\n", "\n")
    a = cs.index(MARK)
    return cs[a:cs.index("    class LauncherForm", a)]


main_cs = (root / "launcher/JStarsLauncher.cs").read_text(encoding="utf-8").replace("\r\n", "\n")
v19_cs = (root / "launcher/JStarsLauncher19.cs").read_text(encoding="utf-8").replace("\r\n", "\n")
code = repsn_class("launcher/JStarsLauncher.cs")
check("v19 launcher carries the same RepsnApp code", code == repsn_class("launcher/JStarsLauncher19.cs"))

# ---- source checks ----
tc = json.loads((root / "installer/tools/toolchain.json").read_text(encoding="utf-8"))
ver = [c for c in tc["components"] if c.get("name") == "re-psn-app"][0]["version"]
folder = "re-psn-app-v%s-windows-amd64" % ver
check("Folder constant follows toolchain.json (%s)" % folder, 'public const string Folder = "%s";' % folder in code)
check("the button is made only for an installed launcher",
      "RepsnAppState repsnState = installed ? RepsnApp.Find(root) : RepsnAppState.Missing;" in main_cs
      and "if (repsnState != RepsnAppState.Missing)" in main_cs)
check("the button is enabled only when Ready", "Enabled = repsnState == RepsnAppState.Ready" in main_cs)
check("an incomplete folder says to reinstall with the option ticked", main_cs.count("reinstall with the option ticked") >= 2)
check("the button sits on the Online tab, under the save row",
      'p.Controls.Add(onlineSaveBtn);' in main_cs and 'Text = "Open the re-psn app", Location = P(16, y + 76)' in main_cs and "p.Controls.Add(appBtn);" in main_cs)
opener = main_cs[main_cs.index("void OpenRepsnApp()"):]
opener = opener[:opener.index("\n        }\n") + 10]
check("the app starts only through StartInfo, with the arguments ui --browser", "Process.Start(RepsnApp.StartInfo(root))" in opener
      and len(re.findall(r"Process\.Start\(", opener)) == 1 and 'Arguments = "ui --browser"' in code)
check("the app is never waited for", "WaitForExit" not in opener and "WaitForExit" not in code and "WaitForInputIdle" not in opener)
check("the app is started only from the button", main_cs.count("OpenRepsnApp()") == 2 and "RepsnApp.StartInfo(" not in v19_cs.replace(code, ""))
check("the v19 launcher (never installed) has no button", "OpenRepsnApp" not in v19_cs)
for name, text in (("JStarsLauncher.cs", main_cs), ("JStarsLauncher19.cs", v19_cs)):
    check("%s: no firewall or netsh" % name, not re.search(r"netsh|advfirewall|New-NetFirewallRule|INetFwPolicy|HNetCfg", text, re.I))
note = main_cs[main_cs.index("const string RepsnAppNote ="):]
note = note[:note.index(";\n")]
iss = (root / "installer/jsp.iss").read_text(encoding="utf-8")
iss_note = iss[iss.index("RepsnNote.Caption :="):]
iss_note = iss_note[:iss_note.index(";\n")]


def literal(s, q):
    """The text of the string literals in s (quote q), joined, spaces normalised."""
    return " ".join("".join(re.findall(q + "([^" + q + "]*)" + q, s)).split())


check("the caveat is the installer's, word for word (setup adds one last sentence)",
      literal(iss_note, "'").startswith(literal(note, '"')) and len(literal(note, '"')) > 200)
for w in ("pre-release", "partial / in progress", "play first, host if it works for you", "unsigned", "SmartScreen",
          "native Windows is not verified", "Wine", "no public server", "no accounts", "private server for friends",
          "The game connects without the app", "opens in your web browser"):
    check("caveat %r" % w, w.lower() in note.lower())

# ---- compile and run ----
mcs, mono = os.environ.get("MCS") or shutil.which("mcs"), os.environ.get("MONO") or shutil.which("mono")
if os.name == "nt" and not mcs:
    for d in ("Framework64", "Framework"):
        csc = pathlib.Path(os.environ.get("WINDIR", "C:\\Windows")) / "Microsoft.NET" / d / "v4.0.30319" / "csc.exe"
        if csc.is_file():
            mcs, mono = str(csc), ""
            break
if not mcs or (mono is None):
    print("SKIP: mcs/mono not found (set MCS and MONO); source checks:", "ok" if not fails else "FAILED (%d)" % len(fails))
    sys.exit(1 if fails else 0)

HARNESS = r'''
using System;
using System.Diagnostics;

namespace JStarsLauncher
{
    static class Harness
    {
        static int Main(string[] a)
        {
            if (a[0] == "find") { Console.WriteLine(RepsnApp.Find(a[1])); return 0; }
            if (a[0] == "start")
            {
                ProcessStartInfo p = RepsnApp.StartInfo(a[1]);
                Console.WriteLine(p.FileName);
                Console.WriteLine("[" + p.Arguments + "]");
                Console.WriteLine(p.WorkingDirectory);
                Console.WriteLine(p.UseShellExecute ? "shell" : "noshell");
                return 0;
            }
            if (a[0] == "folder") { Console.WriteLine(RepsnApp.Folder); return 0; }
            return 2;
        }
    }
}
'''
tmp = pathlib.Path(tempfile.mkdtemp(prefix="repsnapptest"))
head = "using System;\nusing System.Diagnostics;\nusing System.IO;\nnamespace JStarsLauncher {\n"
(tmp / "app.cs").write_text(head + code + "\n}\n", encoding="utf-8")
(tmp / "harness.cs").write_text(HARNESS, encoding="utf-8")
exe = tmp / "h.exe"
r = subprocess.run([mcs] + (["-nologo"] if not mono else []) + ["-out:" + str(exe), str(tmp / "app.cs"), str(tmp / "harness.cs")],
                   capture_output=True, text=True)
check("harness compiles with no errors or warnings", r.returncode == 0 and "warning" not in r.stdout + r.stderr)
if r.returncode:
    print(r.stdout[-1500:], r.stderr[-1500:])
    sys.exit(1)


def run(*a):
    return subprocess.run(([mono] if mono else []) + [str(exe)] + [str(x) for x in a], capture_output=True, text=True, timeout=60).stdout.splitlines()


EXES = ("re-psn-app.exe", "re-psn-host.exe")


def case(present, as_file=False):
    d = pathlib.Path(tempfile.mkdtemp(dir=tmp, prefix="root"))
    if as_file:
        (d / folder).write_bytes(b"not a folder")
    elif present is not None:
        (d / folder).mkdir()
        for n in present:
            (d / folder / n).write_bytes(b"MZ")
    return run("find", d), d


check("Find: empty install folder is Missing", case(None)[0] == ["Missing"])
check("Find: the two exes is Ready", case(EXES)[0] == ["Ready"])
check("Find: the five shipped files is Ready", case(EXES + ("LICENSE", "NOTICE.md", "README.md"))[0] == ["Ready"])
check("Find: re-psn-window.exe present is ignored (still Ready)", case(EXES + ("re-psn-window.exe",))[0] == ["Ready"])
check("Find: re-psn-window.exe alone is Incomplete", case(("re-psn-window.exe",))[0] == ["Incomplete"])
check("Find: re-psn-host.exe missing is Incomplete (window present or not)", case(("re-psn-app.exe", "re-psn-window.exe"))[0] == ["Incomplete"] and case(("re-psn-app.exe",))[0] == ["Incomplete"])
check("Find: re-psn-app.exe missing is Incomplete", case(("re-psn-host.exe", "re-psn-window.exe"))[0] == ["Incomplete"])
check("Find: an empty app folder is Incomplete", case(())[0] == ["Incomplete"])
check("Find: a file named like the folder is Missing", case(None, as_file=True)[0] == ["Missing"])

d = pathlib.Path(tempfile.mkdtemp(dir=tmp, prefix="root"))
out = run("start", d)
sep = "\\" if os.name == "nt" else "/"
app_dir = str(d) + sep + folder
check("StartInfo: FileName is <root>%s%s%sre-psn-app.exe (%s)" % (sep, folder, sep, out[:1]), out[:1] == [app_dir + sep + "re-psn-app.exe"])
check("StartInfo: the arguments ui --browser (flags only work after the ui subcommand)", out[1:2] == ["[ui --browser]"])
check("StartInfo: working folder is the app folder", out[2:3] == [app_dir])
check("StartInfo: UseShellExecute is false", out[3:4] == ["noshell"])
check("Folder from the built class", run("folder") == [folder])

shutil.rmtree(tmp, ignore_errors=True)
print("RESULT:", "ok" if not fails else "FAILED (%d)" % len(fails))
sys.exit(1 if fails else 0)
