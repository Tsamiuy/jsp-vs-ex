#!/usr/bin/env python3
"""Source checks for the launcher's Online tab (no compiler needed; runs on Linux or Windows).

    python online_option_test.py [REPO_ROOT]

The C# launcher needs a C# compiler to build, so this checks the rules the tab must keep in the source: the three
methods and what a save writes to each file, the shared-folder rule, the safe-writer rules (temp file in the same
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
    check("three methods", all(t in tab for t in ('"Off"', '"re-psn"', '"Community server"')))
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
          'ri.Set("repsn", "enabled", m == 1 ? "1" : "0")' in save and 'ci.Set("community", "enabled", m == 2 ? "1" : "0")' in save)
    check("the file switched off is saved first",
          re.search(r"if \(m == 1\) \{ if \(writeC\) err = ci\.Save\(\); if \(err == null && writeR\) err = ri\.Save\(\); \}\s*else \{ if \(writeR\) err = ri\.Save\(\); if \(err == null && writeC\) err = ci\.Save\(\); \}", save) is not None)
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
    check("no 'auto' server value is saved by the launcher", not re.search(r'"auto"', cs))


# the installer builds jsp-settings.exe from JStarsLauncher.cs; the v19 launcher is generated from it: both need the tab
for rel in ("launcher/JStarsLauncher.cs", "launcher/JStarsLauncher19.cs"):
    run_checks(rel)
print("RESULT:", "ok" if not fails else "FAILED")
sys.exit(1 if fails else 0)
