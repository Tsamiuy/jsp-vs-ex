#!/usr/bin/env python3
"""Source checks for the launcher's Online tab (no compiler needed; runs on Linux or Windows).

    python online_option_test.py [REPO_ROOT]

The C# launcher needs a C# compiler to build, so this checks the rules the tab must keep in the source: the three
methods and what a save writes to each file, the shared-folder rule, the safe-writer rules (temp file in the same
folder, flush, replace; 64 KB and 512-byte limits; re-read before saving; unknown lines kept), the input checks, and
that the launcher sets no online environment variable and never touches the game's own keys. FIX (offer only): a packaged
online-service.ini is only offered: the "Use <label>" button (" (TEST BUILD)" for a test build) turns it on when clicked, and
nothing else (the first start, LoadOnline, LoadService) writes anything for it.
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
    ro = cs[cs.index("bool RollbackOn()"):]
    ro = ro[:ro.index("\n        }\n")]
    # ENHANCEMENT (Online Battle + rollback): only rollback WITH a role key conflicts; re-psn with the box ticked is the lobby start and removes the role keys
    check("rollback with a role key refuses Community (the error path stops Start Game too)",
          re.search(r"if \(m == 2 && RollbackOn\(\) && RollbackRoleKey\(\)\)\s*return \"Rollback is on \(Port Settings > Online\)\.", save) is not None
          and save.index("RollbackOn()") < save.index("OnlineIni.Load(RepsnPath()"))
    check("role keys are LISTEN, JOIN, VIA and BENCH",
          '{ "JSP_ROLLBACK_LISTEN", "JSP_ROLLBACK_JOIN", "JSP_ROLLBACK_VIA", "JSP_ROLLBACK_BENCH" }' in cs)
    check("RollbackOn only reads JSP_ROLLBACK (0 or empty is off) and never writes",
          'StartsWith("JSP_ROLLBACK=", StringComparison.OrdinalIgnoreCase)' in ro and 'TrimEnd(\' \', \'\\r\', \'\\n\')' in ro
          and "v.Length != 0 && v[0] != '0'" in ro and "break" not in ro and "WriteAll" not in ro and "File.Replace" not in ro)
    # the rollback lines are written only by the Online tab's save and the offered service's click (UseService), through otherLines and SaveSettings
    # (never a file write of their own), after the ini files
    use = cs[cs.index("void UseService()"):]
    use = use[:use.index("\n        }\n")]
    writes = [l.strip() for l in cs.splitlines() if "OtherSet(" in l and "void OtherSet" not in l]
    check("JSP_ROLLBACK is written only by the Online tab's save (or the service button's click)",
          all(l in save or l in use for l in writes) and 'OtherSet("JSP_ROLLBACK", rollbackBox.Checked ? "1" : "0");' in save
          and 'OtherSet("JSP_ROLLBACK_RELAY", relayBox.Checked ? "1" : "0");' in save
          and save.index('OtherSet("JSP_ROLLBACK"') > save.index("err = ri.Save()") and "SaveSettings();" in save)
    check("Off or Community turns only a lobby rollback off", 'else if (RollbackOn() && !RollbackRoleKey()) { OtherSet("JSP_ROLLBACK", "0"); SaveSettings(); }' in save)
    load = cs[cs.index("void LoadOnline()"):cs.index("string SaveOnline()")]
    state = cs[cs.index("void ShowOnlineState()"):cs.index("void LoadOnline()")]
    # FIX (review): a first start is Off with no server (a suggested Player name only) and saves nothing by itself
    check("first start: Off, no server, a random Player name (no user name), nothing marked to save",
          "onlineFresh = !ri.Exists && !ci.Exists" in load and 'repsnServerBox.Text = ri.Get("repsn", "server") ?? "";' in load
          and "on1 = true" not in load and "onlineDirty = true" not in load
          and '"Player" + new Random().Next(1000, 10000)' in cs and "UserName" not in cs)
    check("first start says no online service is selected", "No online service selected yet, so online is off." in load
          and "no online service selected" in cs[cs.index("void ShowOnlineState()"):cs.index("void LoadOnline()")])
    check("the launcher names no server: no IP address, no PublicServer", "PublicServer" not in cs
          and [x for x in re.findall(r"\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b", cs) if x != "255.255.255.255"] == [])
    svc = cs[cs.index("void LoadService()"):]
    svc = svc[:svc.index("\n        }\n")]
    check("a packaged service (online-service.ini in the install folder) is only offered: button hidden without one, Off until the player saves",
          'Path.Combine(root, "online-service.ini")' in svc and "OnlineIni.ValidAddress(s)" in svc and "Visible = serviceServer != null" in tab
          and "Set(" not in svc and "Save(" not in svc and "serviceServer" not in load.split("onlineMsg.Text = \"No online service")[0])
    # FIX (offer only): the packaged service is offered by one button and applied only by its click
    check("offer: a button at the top of the tab, labelled by ServiceButtonText, hidden without a valid service, bold, no & mnemonic",
          'serviceUseBtn = new Button { Text = serviceServer == null ? "" : ServiceButtonText()' in tab and "UseMnemonic = false, Visible = serviceServer != null }" in tab
          and tab.index("serviceUseBtn = new Button") < tab.index("repsnPanel = new Panel") and tab.index("LoadService();") < tab.index("serviceUseBtn = new Button"))
    check("offer: the button says Use <service name>, and (TEST BUILD) for a test build",
          'string ServiceButtonText() { return "Use " + serviceLabel + (serviceTest ? " (TEST BUILD)" : ""); }' in cs
          and 'serviceTest = si.Get("service", "test_build") == "1";' in svc)
    check("offer: applying is click-only (UseService is called by the button's Click and nowhere else)",
          "serviceUseBtn.Click += delegate { UseService(); };" in tab and cs.count("UseService()") == 2)
    check("offer: nothing is written at load (LoadOnline, LoadService and ShowOnlineState write nothing and do not apply the service)",
          not any(w in load + svc + state for w in ("UseService", "OtherSet(", ".Save()", "SaveSettings(", "File.Write", "File.Append"))
          and "onlineDirty = false;" in load and "onlineDirty = true" not in load)
    check("offer: the service file is read with firstrun.py read_service's rules (label 1-64 printable, server, numeric app id, flags)",
          "ServiceHostOk(s)" in svc and "l.Length < 1 || l.Length > 64" in svc and "c < ' ' || c > '~'" in svc and "app.Length > 20" in svc
          and "c < '0' || c > '9'" in svc and 'serviceRollback = si.Get("repsn", "rollback") != "0"; serviceRelay = si.Get("repsn", "relay") != "0";' in svc)
    check("offer: the click keeps existing online files (shared repsn.ini, community.ini, the game's repsn.ini) and writes repsn.ini only without them",
          "new string[] { RepsnPath(), CommunityPath(), GameRepsnPath() }" in use and "if (present.Count > 0)" in use
          and ".Save()" not in use[use.index("if (present.Count > 0)"):use.index("else\n")]
          and 'ri.Set("repsn", "enabled", "1");' in use and 'ri.Set("repsn", "server", serviceServer);' in use)
    check("offer: rollback lines only after this click turned re-psn on, never over a JSP_ROLLBACK line or a role key",
          "if (repsnOn)" in use and use.index('SettingsHas("JSP_ROLLBACK")') < use.index('OtherSet("JSP_ROLLBACK", "1")')
          and "foreach (string k in RollbackRoleKeys) if (SettingsHas(k)) role = true;" in use
          and 'bool relay = !SettingsHas("JSP_ROLLBACK_RELAY");' in use and 'if (relay) { OtherSet("JSP_ROLLBACK_RELAY", serviceRelay ? "1" : "0");' in use)
    check("offer: the Discord app id only when the settings file has no JSP_DISCORD_APP_ID or JSP_DISCORD line",
          'if (SettingsHas("JSP_DISCORD_APP_ID") || SettingsHas("JSP_DISCORD")) kept.Add(' in use and 'OtherSet("JSP_DISCORD_APP_ID", serviceAppId);' in use)
    # FIX (review): with the player's own online settings kept (no re-psn turned on by this click), the service's Discord app id is not added either
    check("offer: the Discord app id only when this click turned the service on (never beside kept online settings)",
          "if (serviceAppId.Length > 0 && repsnOn)" in use and "if (serviceAppId.Length > 0)\n" not in use
          and use.index("repsnOn = true;") < use.index("if (serviceAppId.Length > 0 && repsnOn)") < use.index('OtherSet("JSP_DISCORD_APP_ID", serviceAppId);'))
    check("offer: the click saves through SaveSettings, checks what landed and shows the result",
          "SaveSettings();" in use and "SettingsValue(k) == null" in use and "MessageBox.Show(this, msg, Text);" in use and "Kept as they are: " in use)
    check("offer: the game's repsn.ini path is the install folder's game\\ when portable, else %APPDATA%\\JSP Fan Revival",
          'PortableInstall(root) ? Path.Combine(Path.Combine(root, "game"), "repsn.ini")' in cs and '"JSP Fan Revival"), "repsn.ini")' in cs)
    check("offer: the first start and the Off line name the button", r'click \"" + ServiceButtonText() + "\" at the top of this tab, or ' in load
          and r'click \"" + ServiceButtonText() + "\" below, or ' in state)
    check("a file with both on, or one on beside rollback, shows a note",
          "RollbackOn()" in load and "Both files were switched on" in load and "note + \" \" + rn" in load)
    check("Server and Name can be saved while Off is chosen (panel stays enabled, valid values are written)",
          "repsnPanel.Enabled = rbOnRepsn.Checked" not in cs
          and "bool writeR = ri.Exists || m == 1 || (OnlineIni.ValidAddress(server) && OnlineIni.ValidName(rname))" in save)
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


# the installer builds jsp-settings.exe from JStarsLauncher.cs; the v19 launcher is generated from it: both need the tab
for rel in ("launcher/JStarsLauncher.cs", "launcher/JStarsLauncher19.cs"):
    run_checks(rel)

# FIX (offer only): the same button words in firstrun.py's log, the setup finish page and the launchers; both launchers keep the Online tab's offer in step
print("== firstrun.py, jsp.iss, both launchers")
sys.path.insert(0, str(root / "installer" / "tools"))
import firstrun   # noqa: E402
check("firstrun.py names the same button", firstrun.service_button({"label": "Example", "test": True}) == "Use Example (TEST BUILD)"
      and firstrun.service_button({"label": "Example", "test": False}) == "Use Example")
iss = (root / "installer" / "jsp.iss").read_text(encoding="utf-8")
fin = iss[iss.index("if CurPageID = wpFinished then begin"):]
check("setup finish page: an offered service is turned on with one click in the settings window's Online tab",
      "SvcIni := ExpandConstant('{app}\\online-service.ini');" in fin and "if FileExists(SvcIni) then begin" in fin
      and "SvcBtn := 'Use ' + GetIniString('service', 'label', '', SvcIni);" in fin and "SvcBtn := SvcBtn + ' (TEST BUILD)';" in fin and "Online tab" in fin and "one click" in fin and "sets up online play" not in fin)


def offer_part(text):
    a = text.index("        // FIX (review): no built-in server.")
    return text[a:text.index("        static readonly string[] RollbackRoleKeys")]


c1 = (root / "launcher/JStarsLauncher.cs").read_text(encoding="utf-8")
c2 = (root / "launcher/JStarsLauncher19.cs").read_text(encoding="utf-8")
check("both launchers have the same offer code (LoadService, ServiceButtonText, UseService)", offer_part(c1) == offer_part(c2))
print("RESULT:", "ok" if not fails else "FAILED")
sys.exit(1 if fails else 0)
