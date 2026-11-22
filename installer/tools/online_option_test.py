#!/usr/bin/env python3
"""Source checks for the launcher's Online tab (no compiler needed; runs on Linux or Windows).

    python online_option_test.py [REPO_ROOT]

The C# launcher needs a C# compiler to build, so this checks the rules the tab must keep in the source: the ONE choice
(Off / re-psn (standard) / re-psn (rollback) / Community: a save writes repsn.ini, community.ini and the JSP_ROLLBACK keys of
play_settings.txt so that exactly one method is on, and keeps every other line), what a save writes to each file, the shared-folder rule, the safe-writer rules (temp file in the same
folder, flush, replace; 64 KB and 512-byte limits; re-read before saving; unknown lines kept), the input checks, and
that the launcher sets no online environment variable and never touches the game's own keys.
"""
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2])
fails = []


def check(name, ok):
    print(("ok   " if ok else "FAIL ") + name)
    if not ok:
        fails.append(name)




def run_checks(rel):
    global cs
    print("== " + rel)
    cs = (root / rel).read_text(encoding="utf-8")
    ini = cs[cs.index("class OnlineIni"):cs.index("// LAN discovery for re-psn")]
    save = cs[cs.index("string SaveOnline()"):]
    save = save[:save.index("\n        }\n")]
    tab = cs[cs.index("TabPage BuildOnlineTab()"):cs.index("string SaveOnline()")]
    start = cs[cs.index("void StartGame()"):cs.index("void RunGame()")]
    run = cs[cs.index("void RunGame()"):]

    check("tab is added", "tabs.TabPages.Add(BuildOnlineTab());" in cs)
    check("four choices, two re-psn names", all(t in tab for t in ('"Off"', '"re-psn (standard)"', '"re-psn (rollback)"', '"Community"')))
    check("shared folder env override", 'GetEnvironmentVariable("PS3RECOMP_SHARED_DIR")' in ini)
    check("portable: jsp-install.ini portable=1 gives <root>\\shared",
          '"jsp-install.ini"' in ini and '"portable=1"' in ini and 'Path.Combine(installRoot, "shared")' in ini)
    check("installed: %APPDATA%\\PS3Recomp", 'SpecialFolder.ApplicationData), "PS3Recomp")' in ini)
    check("file names", '"repsn.ini"' in cs and '"community.ini"' in cs)
    check("temp file beside the target", 'FilePath + ".tmp"' in ini)
    check("flushed to disk before the replace", "WriteThrough" in ini and "Flush(true)" in ini and ini.index("Flush(true)") < ini.index("File.Replace(tmp"))
    check("atomic replace of an existing file", "File.Replace(tmp, FilePath, null)" in ini)
    check("failed write removes the temp file and keeps the old", "File.Delete(tmp)" in ini and ini.index("File.Replace(tmp") < ini.index("File.Delete(tmp)"))
    check("64 KB and 512-byte caps on read and write",
          "MaxFileBytes = 64 * 1024" in ini and "MaxLineBytes = 512" in ini
          and ini.count("MaxFileBytes") >= 3 and ini.count("MaxLineBytes") >= 2)
    check("an unusable file is not overwritten", re.search(r"string e = OnlineIni\.Load\(RepsnPath\(\), out ri\);\s*if \(e != null\) return e;", save) is not None
          and re.search(r"e = OnlineIni\.Load\(CommunityPath\(\), out ci\);\s*if \(e != null\) return e;", save) is not None)
    check("files are read again inside the save", "OnlineIni.Load(RepsnPath()" in save and "OnlineIni.Load(CommunityPath()" in save)
    check("other lines kept: edits are per key, not a rewrite from scratch", "public void Set(string section, string key" in ini and "Lines[i] = key" in ini)
    check("method sets enabled in both files",
          'ri.Set("repsn", "enabled", (m == 1 || native) ? "1" : "0")' in save and 'ci.Set("community", "enabled", m == 3 ? "1" : "0")' in save)
    check("rollback is the third choice: it needs the ini files off (enabled 0) and nothing else on",
          "bool native = m == 4, rollback = m == 2" in save and "int OnlineMethod() { return rbOnRepsn.Checked ? 1 : rbOnRb.Checked ? 2 : rbOnComm.Checked ? 3 : rbOnNative.Checked ? 4 : 0; }" in cs)
    check("the method switched off is saved first (rollback on: ini files off, then JSP_ROLLBACK=1; else JSP_ROLLBACK=0, then the ini files)",
          re.search(r"if \(rollback\)\s*\{\s*if \(writeC\) err = ci\.Save\(\);\s*if \(err == null && writeR\) err = ri\.Save\(\);\s*if \(err == null\) err = WriteRollback\(true,", save) is not None
          and re.search(r"err = WriteRollback\(false,.*?\n\s*if \(err == null\)\s*\{\s*if \(m == 1\) \{ if \(writeC\) err = ci\.Save\(\); if \(err == null && writeR\) err = ri\.Save\(\); \}\s*else \{ if \(writeR\) err = ri\.Save\(\); if \(err == null && writeC\) err = ci\.Save\(\); \}", save, re.S) is not None)
    check("only presets deleted in the window are removed (new ones from outside stay)", "loadedLabels" in save and "RemovePreset(old)" in save)
    check("never mixed: no server or preset key in the other file",
          '"server"' not in re.sub(r'ri\.Set\("repsn", "server"[^;]*;', "", save.split("if (writeC)")[1])
          and "ri.Set(OnlineIni.PresetSection" not in save and 'ci.Set("community", "server"' not in save)
    check("community.ini top comment about port-only features",
          re.search(r'SetPreamble\("; Community server settings\. Features that exist only in this port do not work on a community server\."\)', save) is not None)
    check("disclaimer line in the tab", "Features that exist only in this port do not work on a community server." in tab)
    check("no preset shipped", "presets.Add(new Preset" in cs and cs.count("presets.Add(") == 2 and "example" not in tab.lower())
    check("name rule: 3 to 16, letter first", "n.Length < 3 || n.Length > 16" in ini and "(n[0] >= '0' && n[0] <= '9')" in ini)
    check("address rule: port 1..65535", "port < 1 || port > 65535" in ini)
    check("re-psn default port named in the tab", "port 36100 when left out" in tab)
    check("per-game keys never touched", not any(k in cs for k in ("comm_id", "p2p_port", "report_rtt")))
    ro = cs[cs.index("string RbValue(string key)"):]
    ro = ro[:ro.index("\n        }\n")]
    check("saving no longer refuses re-psn or Community because rollback is on (one choice: the save turns rollback off)",
          "RollbackOn()" not in save and "Rollback is on (Port Settings > Online). Turn it off" not in cs)
    check("RbValue reads JSP_ROLLBACK* the way the game does (column 0, letter case ignored, last wins, trailing space/CR/LF trimmed) and never writes",
          'StartsWith(key + "=", StringComparison.OrdinalIgnoreCase)' in ro and "TrimEnd(' ', '\\r', '\\n')" in ro
          and "WriteAll" not in ro and "File.Replace" not in ro and "break" not in ro)
    check("the flag test is the game's (not empty, first character not 0)", "v != null && v.Length != 0 && v[0] != '0'" in cs)
    # the launcher writes JSP_ROLLBACK* only through RbApply / WriteRollback (INVARIANT: one method on after any save; other lines kept)
    rb_lines = [l for l in cs.splitlines() if "JSP_ROLLBACK" in l and not l.strip().startswith("//")]
    apply_src = cs[cs.index("static void RbApply("):cs.index("string WriteRollback(")]
    relay_read = 'string relv = SettingsValue("JSP_ROLLBACK_RELAY");'
    check("Rollback setting writes stay in RbApply and RbNative", all(l in apply_src for l in rb_lines if "RbSet(" in l))
    check("Rollback settings are written by the owned writers or read by the settings loaders",
          all(l.strip() == relay_read or "static readonly string[] RollbackRoleKeys" in l or "RbSet(" in l or "RbValue(" in l
              or "RbFlag" in l or 'StartsWith("JSP_ROLLBACK' in l or "JSP_UNLOCK_ALL" in l for l in rb_lines))
    load_online = cs[cs.index("void LoadOnline()"):cs.index("string SaveOnline()")]
    check("Native relay checkbox reads the saved key", relay_read in load_online
          and "relayBox.Checked = relv == null || (relv.Length != 0 && relv[0] != '0');" in load_online)
    check("RbApply keys: JSP_ROLLBACK 1 / 0 (0 only when the line exists), VIA, ROLE, RELAY, LISTEN, JOIN",
          all(k in apply_src for k in ('"JSP_ROLLBACK", "1"', '"JSP_ROLLBACK", "0"', '"JSP_ROLLBACK_VIA", "repsn"', '"JSP_ROLLBACK_ROLE"', '"JSP_ROLLBACK_RELAY"', '"JSP_ROLLBACK_LISTEN"', '"JSP_ROLLBACK_JOIN"', '"JSP_ROLLBACK_CODE"', 'auto ? "auto" : host ? "host" : "join"')))
    check("WriteRollback re-reads the file and keeps otherLines (what SaveSettings writes back) in step",
          "File.ReadAllLines(settingsFile)" in cs[cs.index("string WriteRollback("):] and "otherLines.RemoveAll(" in cs and "otherLines.Add(" in cs[cs.index("string WriteRollback("):])
    check("direct IP rollback checks: port 1..65535, a dotted IP:port",
          "static bool ValidRbPort(" in cs and "n >= 1 && n <= 65535" in cs and "static bool ValidRbJoin(" in cs and "o.Length != 4" in cs)
    check("the code is checked 4 to 8 letters or digits in the launcher source", "static bool ValidRbCode(" in cs and "c.Length < 4 || c.Length > 8" in cs and "!ValidRbCode(rcode)" in save)
    check("direct IP with the Auto role is refused (it needs Host or Join)", "if (direct && auto) return" in save)
    check("the role is a dropdown with Auto first, Role radios are gone", "rbRoleBox" in tab and "rbRbHost" not in cs and "Auto (recommended)" in tab)
    check("the Advanced box keeps the direct IP rollback reachable (choice then reads Rollback (direct IP))",
          'Text = "Advanced: connect by IP address instead of through re-psn"' in tab and '"Rollback (direct IP)"' in cs)
    load = cs[cs.index("void LoadOnline()"):cs.index("string SaveOnline()")]
    check("a file with both on, or one on beside rollback, shows a note",
          "RollbackOn()" in load and "were both on; shown as Off" in load and "note + \" \" + rn" in load and "shown as Off. Pick one and save." in load)
    check("Server and Name can be saved while Off is chosen (panel stays enabled, valid values are written)",
          "repsnPanel.Enabled = rbOnRepsn.Checked" not in cs
          and "bool writeR = ri.Exists || m == 1 || native || (rollback && !direct) || (OnlineIni.ValidAddress(server) && OnlineIni.ValidName(rname))" in save)
    check("launcher sets no online environment variable",
          not any(k in cs for k in ("PSNR_SERVER", "PS3_NET_ONLINE", "PS3_NP_ONLINE_ID")))
    # JStarsLauncher.cs saves through SaveOnlineIfDirty() on both start paths (installed and from source), v19 inline
    helper = "bool SaveOnlineIfDirty()" in cs
    if helper:
        h = cs[cs.index("bool SaveOnlineIfDirty()"):]
        h = h[:h.index("\n        }\n")]
        check("start helper: Online edits saved, a bad value shows the Online tab and stops the start",
              "if (!onlineDirty) return true;" in h and "SaveOnline()" in h and "return false;" in h and "MessageBox.Show" in h)
        check("both start paths save the Online tab first, before SaveSettings",
              start.count("if (!SaveOnlineIfDirty()) return;") == 2
              and all(start.index("SaveOnlineIfDirty()", i) < start.index("SaveSettings();", i)
                      for i in (0, start.index("SaveSettings();") + 1)))
    else:
        check("unsaved tab edits are saved before the start, a bad value stops it",
              start.index("SaveOnline()") < start.index("new Thread(RunGame)") and "return;" in start[start.index("SaveOnline()"):start.index("new Thread(RunGame)")])
    check("no real server named", not re.search(r"\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b", tab + ini))
    # Find on LAN
    lan = cs[cs.index("class LanServer"):cs.index("class Preset {")]
    find = cs[cs.index("void FindOnLan()"):cs.index("int OnlineMethod()")]
    check("Find on LAN button is in the re-psn panel", 'Text = "Find on LAN"' in tab and "repsnPanel.Controls.Add(lanBtn)" in tab)
    check("request: magic, type 0x04, big-endian nonce, 64 bytes",
          "RequestLen = 64" in lan and "r[4] = 0x04" in lan and "(byte)(nonce >> 24)" in lan and "r[8] = (byte)nonce" in lan)
    check("reply: exactly 52 bytes, type 0x84, nonce matched, port 0 refused",
          "ReplyLen = 52" in lan and "len != ReplyLen" in lan and "b[4] != 0x84" in lan and "n != nonce" in lan and "TcpPort == 0" in lan)
    check("reply text: NUL-terminated, non-printable becomes ?", "b[off + i] != 0" in lan and "0x20" in lan and "0x7f" in lan and "'?'" in lan)
    check("UdpClient with broadcast, 255.255.255.255 and each interface's directed broadcast",
          "UdpClient" in lan and "EnableBroadcast = true" in lan and "IPAddress.Broadcast" in lan and "a[i] | ~m[i]" in lan)
    check("default port 36100, about 1200 ms, results capped", "DefaultPort = 36100" in lan and "LanTimeoutMs = 1200" in cs and "MaxFound = 16" in lan)
    check("search runs on a background thread and returns to the UI thread",
          "new Thread(delegate" in find and "IsBackground = true" in find and "BeginInvoke(" in find and "LanDiscover.Find(" in find)
    check("button disabled while searching", "lanBtn.Enabled = false" in find and "lanBtn.Enabled = true" in find)
    check("none found: the required message",
          "No server found on this network. The host must start re-psn with -discover, and both PCs must be on the same network." in find)
    check("one found fills Server, several are picked, found addresses are validated",
          "repsnServerBox.Text = pick.Ip" in find and "PickLanServer(ok)" in find and "OnlineIni.ValidAddress(s.Ip" in find)
    check("listing shows name, address:port, players, relay yes/no, version",
          all(t in find for t in ("s.Ip", "s.TcpPort", "s.Players", '"yes" : "no"', "s.Version")))
    check("no 'auto' server value is saved by the launcher (the only \"auto\" is the role)", not re.search(r'"server"[^;\n]*"auto"', cs) and cs.count('"auto"') == 2)


def rollback_on_model(text):
    """Model of RollbackOn() (launcher.c load_env + the game's env_on), checked on the cases found in review."""
    v = ""
    for line in text.split("\n"):
        if line.upper().startswith("JSP_ROLLBACK="):
            v = line[13:].rstrip(" \r")
    return v != "" and v[0] != "0"


for txt, want in (("JSP_ROLLBACK=1", True), ("JSP_ROLLBACK=0", False), ("JSP_ROLLBACK=", False), ("JSP_ROLLBACK=off", True),
                  ("#JSP_ROLLBACK=1", False), (";JSP_ROLLBACK=1", False), ("JSP_ROLLBACK= 0", True), ("JSP_ROLLBACK=0\nJSP_ROLLBACK=1", True),
                  ("JSP_ROLLBACK=1\nJSP_ROLLBACK=0", False), ("JSP_ROLLBACK=00", False), (" JSP_ROLLBACK=1", False),
                  ("JSP_ROLLBACK =1", False), ("JSP_ROLLBACK=1\r", True), ("jsp_rollback=1", True), ("Jsp_Rollback=0", False), ("JSP_ROLLBACK=0\r", False)):
    check("parser model: %r -> %s" % (txt, "on" if want else "off"), rollback_on_model(txt) == want)


# ---- the one choice, as a model of RbApply + the ini files (the same rules as the source checks above) -----------------------
def rb_set(lines, key, value):
    at = -1
    for i in range(len(lines) - 1, -1, -1):
        if lines[i].upper().startswith(key + "="):
            del lines[i]
            at = i
    if value is None:
        return
    if at < 0:
        lines.append(key + "=" + value)
    else:
        lines.insert(at, key + "=" + value)


def rb_apply(lines, enable, direct=False, host=True, port="36200", join="", relay=False, auto=False, code=""):
    if not enable:
        if any(l.upper().startswith("JSP_ROLLBACK=") for l in lines):
            rb_set(lines, "JSP_ROLLBACK", "0")
        return
    rb_set(lines, "JSP_ROLLBACK", "1")
    if not direct:
        rb_set(lines, "JSP_ROLLBACK_VIA", "repsn"); rb_set(lines, "JSP_ROLLBACK_ROLE", "auto" if auto else "host" if host else "join"); rb_set(lines, "JSP_ROLLBACK_RELAY", "1" if relay else "0"); rb_set(lines, "JSP_ROLLBACK_CODE", code or None)
        rb_set(lines, "JSP_ROLLBACK_LISTEN", None); rb_set(lines, "JSP_ROLLBACK_JOIN", None)
        return
    rb_set(lines, "JSP_ROLLBACK_VIA", None); rb_set(lines, "JSP_ROLLBACK_ROLE", None); rb_set(lines, "JSP_ROLLBACK_RELAY", None)
    if host:
        rb_set(lines, "JSP_ROLLBACK_LISTEN", port); rb_set(lines, "JSP_ROLLBACK_JOIN", None)
    else:
        rb_set(lines, "JSP_ROLLBACK_JOIN", join); rb_set(lines, "JSP_ROLLBACK_LISTEN", None)


def save_choice(choice, files, play, **kw):
    """choice 0 Off, 1 re-psn (standard), 2 re-psn (rollback), 3 Community -> (repsn enabled, community enabled, play_settings lines)"""
    lines = list(play)
    rb_apply(lines, choice == 2, **kw)
    return choice == 1, choice == 3, lines


def methods_on(rep_on, com_on, lines):
    return int(rep_on) + int(com_on) + int(rollback_on_model("\n".join(lines)))


other = ["PIPE=1", "# comment", "INPUT_MODE=kbm", "JSP_UNLOCK_ALL_X=1"]
for start in ([], ["JSP_ROLLBACK=1", "JSP_ROLLBACK_VIA=repsn", "JSP_ROLLBACK_ROLE=host"], ["JSP_ROLLBACK=0"], ["JSP_ROLLBACK=1", "JSP_ROLLBACK_LISTEN=9"]):
    for choice in range(4):
        for direct in (False, True):
            for host in (False, True):
                r, c, out = save_choice(choice, None, other + start, direct=direct, host=host, port="36200", join="192.0.2.10:7777", relay=True)
                ok = methods_on(r, c, out) == (0 if choice == 0 else 1) and all(o in out for o in other)
                check("choice %d (direct=%s host=%s) from %r: exactly one method on (or none for Off), other lines kept" % (choice, direct, host, start), ok)
_, _, o = save_choice(2, None, ["A=1"], host=False, relay=True)
check("re-psn (rollback) writes JSP_ROLLBACK=1, VIA=repsn, ROLE=join, RELAY=1 and no LISTEN/JOIN",
      o == ["A=1", "JSP_ROLLBACK=1", "JSP_ROLLBACK_VIA=repsn", "JSP_ROLLBACK_ROLE=join", "JSP_ROLLBACK_RELAY=1"])
_, _, o = save_choice(2, None, ["A=1", "JSP_ROLLBACK_VIA=repsn", "JSP_ROLLBACK_ROLE=host"], direct=True, host=False, join="192.0.2.10:7777")
check("direct IP join keeps working: JOIN=IP:port, VIA/ROLE removed", o == ["A=1", "JSP_ROLLBACK=1", "JSP_ROLLBACK_JOIN=192.0.2.10:7777"])
_, _, o = save_choice(2, None, ["A=1", "JSP_ROLLBACK_CODE=OLD1"], auto=True, code="ZX81QQ")
check("re-psn (rollback), auto role with a code: ROLE=auto and the code, no LISTEN/JOIN",
      o == ["A=1", "JSP_ROLLBACK=1", "JSP_ROLLBACK_CODE=ZX81QQ", "JSP_ROLLBACK_VIA=repsn", "JSP_ROLLBACK_ROLE=auto", "JSP_ROLLBACK_RELAY=0"] or sorted(o) == sorted(["A=1", "JSP_ROLLBACK=1", "JSP_ROLLBACK_CODE=ZX81QQ", "JSP_ROLLBACK_VIA=repsn", "JSP_ROLLBACK_ROLE=auto", "JSP_ROLLBACK_RELAY=0"]))
_, _, o = save_choice(2, None, ["JSP_ROLLBACK_CODE=OLD1"], auto=True, code="")
check("an empty code removes the key", not any(l.startswith("JSP_ROLLBACK_CODE") for l in o))
_, _, o = save_choice(1, None, ["A=1"])
check("re-psn (standard) from a file without the line writes no JSP_ROLLBACK line", o == ["A=1"])
_, _, o = save_choice(1, None, ["JSP_ROLLBACK=1", "jsp_rollback=1"])
check("every JSP_ROLLBACK line (any letter case) becomes one 0", o == ["JSP_ROLLBACK=0"])
# MIGRATION: files with two methods on load as Off (with a note); the save of that Off fixes them
for rep_on, com_on, play in ((True, False, ["JSP_ROLLBACK=1", "JSP_ROLLBACK_LISTEN=36200"]), (False, True, ["JSP_ROLLBACK=1", "JSP_ROLLBACK_VIA=repsn"]), (True, True, [])):
    check("migration: two on (%s %s %s) is two methods before the save" % (rep_on, com_on, play), methods_on(rep_on, com_on, play) >= 2)
    r, c, o = save_choice(0, None, play)
    check("migration: saving the shown Off leaves no method on (%s %s %s)" % (rep_on, com_on, play), methods_on(r, c, o) == 0)

# the installer builds jsp-settings.exe from JStarsLauncher.cs; the v19 launcher is generated from it: both need the tab
for rel in ("launcher/JStarsLauncher.cs", "launcher/JStarsLauncher19.cs"):
    run_checks(rel)
print("RESULT:", "ok" if not fails else "FAILED")
sys.exit(1 if fails else 0)
