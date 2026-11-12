// J-Stars Victory VS+ launcher: a small window that replaces "Play J-Stars.bat"
// and "J-Stars Settings.bat". Built with the C# compiler that ships with Windows
// (see build_launcher.bat), so nothing needs installing. C# 5 only: that compiler
// knows no string interpolation, ?. or nameof.
//
// What it does when you press Start Game (same steps as jstars\play.ps1):
//   save the settings to jstars\play_settings.txt, mount the ISO read-only,
//   turn CPU boost on, start the game with the same environment as play.ps1,
//   hide the launcher once the game's window is up, wait until the game closes,
//   turn boost off (if ticked), unmount, and end the launcher. If the game never
//   opened a window, the launcher comes back instead and shows what happened.
// Exit (or closing the launcher) while the game runs closes the game window the
// normal way (WM_CLOSE, like clicking its X) and only offers a forced stop if the
// game has not closed after 20 seconds.
// Log of each start: jstars\run\launcher_log.txt (overwritten each time).

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace JStarsLauncher
{
    static class Program
    {
        [DllImport("user32.dll")] static extern bool SetProcessDPIAware();

        [STAThread]
        // "J-Stars Launcher.exe --screenshots <folder>" saves a picture of every page
        // and closes again, without saving settings (for checking the layout).
        static void Main(string[] args)
        {
            try { SetProcessDPIAware(); } catch { }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            var f = new LauncherForm();
            if (args.Length == 2 && args[0] == "--screenshots") f.ShotDir = args[1];
            Application.Run(f);
        }
    }

    // One remappable pad input on the Keyboard or Keyboard & Mouse page. The game
    // reads the active page's keys as PAD_KEY_<Id>.
    class PadAction
    {
        public string Id, Label; public int DefaultVk, Vk; public Button Box; public List<PadAction> Set;
        public PadAction(string id, string label, Keys def) { Id = id; Label = label; DefaultVk = (int)def; Vk = DefaultVk; }
    }

    // One PS3 button on the Controller page: the Xbox-style button that presses it (PAD_BTN_<Id>).
    class PadButton
    {
        public string Id, Label, Default, Value; public ComboBox Box;
        public PadButton(string id, string label, string def) { Id = id; Label = label; Default = def; Value = def; }
    }

    // The two online settings files shared by the ports, in the shared folder (see SharedDir): repsn.ini ([repsn]
    // enabled, server, name) and community.ini ([community] enabled, active; [preset LABEL] address, name). The
    // launcher only writes the keys it owns and keeps every other line (comments, unknown keys) as it was.
    // Files are untrusted input: at most 64 KB, lines of at most 512 bytes; a file outside that is not read and not
    // overwritten. Saves go to a temp file in the same folder, flushed to disk, then replace the old file in one step.
    class OnlineIni
    {
        public const int MaxFileBytes = 64 * 1024, MaxLineBytes = 512, MaxPresets = 64;

        class Sec { public string Name; public List<string> Lines = new List<string>(); }   // Name == null: lines before the first header
        readonly List<Sec> secs = new List<Sec>();
        public readonly string FilePath;
        public bool Exists;

        public OnlineIni(string path) { FilePath = path; secs.Add(new Sec()); }

        // Returns null when read, else why the file cannot be used.
        public static string Load(string path, out OnlineIni ini)
        {
            ini = new OnlineIni(path);
            if (!File.Exists(path)) return null;
            string fn = Path.GetFileName(path);
            if (new FileInfo(path).Length > MaxFileBytes) return fn + " is larger than 64 KB; not read.";
            string text;
            try { text = File.ReadAllText(path, Encoding.UTF8); }
            catch (Exception e) { return "Cannot read " + fn + ": " + e.Message; }
            ini.Exists = true;
            Sec cur = ini.secs[0];
            foreach (string raw in text.Split('\n'))
            {
                string line = raw.TrimEnd('\r');
                if (Encoding.UTF8.GetByteCount(line) > MaxLineBytes) return fn + " has a line longer than 512 bytes; not read.";
                string t = line.Trim();
                if (t.Length > 2 && t[0] == '[' && t[t.Length - 1] == ']')
                {
                    cur = new Sec { Name = t.Substring(1, t.Length - 2).Trim() };
                    ini.secs.Add(cur);
                }
                cur.Lines.Add(line);
            }
            // a trailing newline made one empty last line: drop it so a rewrite does not grow the file
            Sec last = ini.secs[ini.secs.Count - 1];
            if (last.Lines.Count > 0 && last.Lines[last.Lines.Count - 1].Length == 0) last.Lines.RemoveAt(last.Lines.Count - 1);
            return null;
        }

        Sec Find(string name)
        {
            foreach (Sec s in secs)
                if (s.Name != null && string.Equals(s.Name, name, StringComparison.OrdinalIgnoreCase)) return s;
            return null;
        }

        static bool KeyLine(string line, string key)
        {
            string t = line.Trim();
            if (t.Length == 0 || t[0] == ';' || t[0] == '#' || t[0] == '[') return false;
            int eq = t.IndexOf('=');
            return eq > 0 && string.Equals(t.Substring(0, eq).Trim(), key, StringComparison.OrdinalIgnoreCase);
        }

        public string Get(string section, string key)
        {
            Sec s = Find(section);
            if (s == null) return null;
            string v = null;
            foreach (string l in s.Lines)
                if (KeyLine(l, key)) v = l.Substring(l.IndexOf('=') + 1).Trim();   // the last one wins
            return v;
        }

        public void Set(string section, string key, string value)
        {
            Sec s = Find(section);
            if (s == null) { s = new Sec { Name = section }; s.Lines.Add("[" + section + "]"); secs.Add(s); }
            for (int i = s.Lines.Count - 1; i >= 0; i--)   // the last one wins, so that is the one to change
                if (KeyLine(s.Lines[i], key)) { s.Lines[i] = key + "=" + value; return; }
            int at = s.Lines.Count;
            while (at > 1 && s.Lines[at - 1].Trim().Length == 0) at--;   // before the blank lines that end the section
            s.Lines.Insert(at, key + "=" + value);
        }

        // A comment at the top of a file that does not exist yet.
        public void SetPreamble(string comment)
        {
            if (!Exists && secs[0].Lines.Count == 0) secs[0].Lines.Add(comment);
        }

        public List<string> PresetLabels()
        {
            var r = new List<string>();
            foreach (Sec s in secs)
                if (s.Name != null && s.Name.StartsWith("preset ", StringComparison.OrdinalIgnoreCase)) r.Add(s.Name.Substring(7).Trim());
            return r;
        }

        public void RemovePreset(string label)
        {
            for (int i = secs.Count - 1; i > 0; i--)
                if (secs[i].Name != null && string.Equals(secs[i].Name, PresetSection(label), StringComparison.OrdinalIgnoreCase)) secs.RemoveAt(i);
        }

        public static string PresetSection(string label) { return "preset " + label; }

        string Render()
        {
            var sb = new StringBuilder();
            foreach (Sec s in secs) foreach (string l in s.Lines) sb.Append(l).Append("\r\n");
            return sb.ToString();
        }

        // Temp file beside the target, flushed to disk, then File.Replace (or a move for a new file). On any failure
        // the temp file is removed and the old file stays as it was. Returns null or an error text.
        public string Save()
        {
            string fn = Path.GetFileName(FilePath);
            string text = Render();
            if (Encoding.UTF8.GetByteCount(text) > MaxFileBytes) return fn + " would be larger than 64 KB; not saved.";
            string tmp = FilePath + ".tmp" + Process.GetCurrentProcess().Id;
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(FilePath));
                byte[] data = new UTF8Encoding(false).GetBytes(text);
                using (var fs = new FileStream(tmp, FileMode.Create, FileAccess.Write, FileShare.None, 4096, FileOptions.WriteThrough))
                {
                    fs.Write(data, 0, data.Length);
                    fs.Flush(true);
                }
                if (File.Exists(FilePath)) File.Replace(tmp, FilePath, null);
                else File.Move(tmp, FilePath);
                Exists = true;
                return null;
            }
            catch (Exception e)
            {
                try { if (File.Exists(tmp)) File.Delete(tmp); } catch { }
                return "Cannot save " + fn + ": " + e.Message;
            }
        }

        // The shared folder: PS3RECOMP_SHARED_DIR, else <install root>\shared when jsp-install.ini says portable=1,
        // else %APPDATA%\PS3Recomp.
        public static string SharedDir(string installRoot)
        {
            string env = Environment.GetEnvironmentVariable("PS3RECOMP_SHARED_DIR");
            if (!string.IsNullOrEmpty(env)) return env;
            try
            {
                string ini = Path.Combine(installRoot, "jsp-install.ini");   // same rule as the settings window
                if (File.Exists(ini))
                    foreach (string line in File.ReadAllLines(ini))
                        if (line.Replace(" ", "").ToLowerInvariant() == "portable=1") return Path.Combine(installRoot, "shared");
            }
            catch { }
            return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "PS3Recomp");
        }

        static bool Alnum(char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'); }

        // host[:port]: letters, digits, '.', '-' or '_' in the host, a port of 1..65535 when given.
        public static bool ValidAddress(string a)
        {
            if (a == null || a.Length == 0 || a.Length > 260) return false;
            string host = a;
            int c = a.LastIndexOf(':');
            if (c >= 0)
            {
                host = a.Substring(0, c);
                string ps = a.Substring(c + 1);
                int port;
                if (ps.Length == 0 || ps.Length > 5) return false;
                foreach (char ch in ps) if (ch < '0' || ch > '9') return false;
                if (!int.TryParse(ps, out port) || port < 1 || port > 65535) return false;
            }
            if (host.Length == 0 || host.Length > 253) return false;
            foreach (char ch in host)
                if (!(Alnum(ch) || ch == '.' || ch == '-' || ch == '_')) return false;
            return true;
        }

        // Online name: 3 to 16 of A-Z a-z 0-9 _ -, starting with a letter.
        public static bool ValidName(string n)
        {
            if (n == null || n.Length < 3 || n.Length > 16) return false;
            if (!Alnum(n[0]) || (n[0] >= '0' && n[0] <= '9')) return false;
            foreach (char ch in n) if (!(Alnum(ch) || ch == '_' || ch == '-')) return false;
            return true;
        }

        // Preset label: 1 to 32 of A-Z a-z 0-9 _ -.
        public static bool ValidLabel(string n)
        {
            if (n == null || n.Length < 1 || n.Length > 32) return false;
            foreach (char ch in n) if (!(Alnum(ch) || ch == '_' || ch == '-')) return false;
            return true;
        }
    }

    // LAN discovery for re-psn (the DISCOVER request and reply of re-psn's server/discover.go and client/psnr.c, as of its
    // main branch after the LAN discovery merges). The request is 64 bytes: "PSNR", type 0x04, a 4-byte nonce, zeros.
    // The reply is exactly 52 bytes: "PSNR", type 0x84, the nonce, tcp_port u16, udp_port u16, flags u8 (bit 0: relay),
    // players u16, name[24], version[12], all big-endian, text NUL-padded. Replies are untrusted input: anything of
    // the wrong length, magic, type or nonce, or with port 0, is ignored; text is cut at the first NUL and every
    // byte outside printable ASCII becomes '?'. Only servers started with -discover answer, and only on the same
    // subnet; the reply goes to the sender of the request.
    class LanServer { public string Ip, Name, Version; public int TcpPort, UdpPort, Players; public bool Relay; }

    static class LanDiscover
    {
        public const int DefaultPort = 36100, RequestLen = 64, ReplyLen = 52, MaxFound = 16;

        public static byte[] BuildRequest(uint nonce)
        {
            byte[] r = new byte[RequestLen];
            r[0] = (byte)'P'; r[1] = (byte)'S'; r[2] = (byte)'N'; r[3] = (byte)'R';
            r[4] = 0x04;
            r[5] = (byte)(nonce >> 24); r[6] = (byte)(nonce >> 16); r[7] = (byte)(nonce >> 8); r[8] = (byte)nonce;
            return r;
        }

        static string Text(byte[] b, int off, int n)
        {
            var sb = new StringBuilder();
            for (int i = 0; i < n && b[off + i] != 0; i++)
                sb.Append(b[off + i] >= 0x20 && b[off + i] < 0x7f ? (char)b[off + i] : '?');
            return sb.ToString();
        }

        // Returns null for anything that is not a well-formed reply to this nonce.
        public static LanServer ParseReply(byte[] b, int len, uint nonce, string fromIp)
        {
            if (b == null || len != ReplyLen || b.Length < ReplyLen || fromIp == null) return null;
            if (b[0] != 'P' || b[1] != 'S' || b[2] != 'N' || b[3] != 'R' || b[4] != 0x84) return null;
            uint n = ((uint)b[5] << 24) | ((uint)b[6] << 16) | ((uint)b[7] << 8) | b[8];
            if (n != nonce) return null;
            var s = new LanServer();
            s.Ip = fromIp;
            s.TcpPort = (b[9] << 8) | b[10];
            s.UdpPort = (b[11] << 8) | b[12];
            s.Relay = (b[13] & 1) != 0;
            s.Players = (b[14] << 8) | b[15];
            s.Name = Text(b, 16, 24);
            s.Version = Text(b, 40, 12);
            if (s.TcpPort == 0) return null;
            return s;
        }

        // 255.255.255.255 plus the directed broadcast address of each IPv4 interface that is up.
        public static List<System.Net.IPAddress> Destinations()
        {
            var list = new List<System.Net.IPAddress> { System.Net.IPAddress.Broadcast };
            try
            {
                foreach (var nic in System.Net.NetworkInformation.NetworkInterface.GetAllNetworkInterfaces())
                {
                    if (nic.OperationalStatus != System.Net.NetworkInformation.OperationalStatus.Up) continue;
                    if (nic.NetworkInterfaceType == System.Net.NetworkInformation.NetworkInterfaceType.Loopback) continue;
                    foreach (var ua in nic.GetIPProperties().UnicastAddresses)
                    {
                        if (ua.Address.AddressFamily != System.Net.Sockets.AddressFamily.InterNetwork || ua.IPv4Mask == null) continue;
                        byte[] a = ua.Address.GetAddressBytes(), m = ua.IPv4Mask.GetAddressBytes();
                        if (a.Length != 4 || m.Length != 4) continue;
                        byte[] d = new byte[4];
                        for (int i = 0; i < 4; i++) d[i] = (byte)(a[i] | ~m[i]);
                        var ip = new System.Net.IPAddress(d);
                        if (!list.Contains(ip) && list.Count < 32) list.Add(ip);
                    }
                }
            }
            catch { }
            return list;
        }

        // Blocks for about timeoutMs; run it on a worker thread. dests null means Destinations().
        public static List<LanServer> Find(int port, int timeoutMs, List<System.Net.IPAddress> dests)
        {
            var found = new List<LanServer>();
            byte[] g = Guid.NewGuid().ToByteArray();
            uint nonce = BitConverter.ToUInt32(g, 0);
            byte[] req = BuildRequest(nonce);
            if (dests == null) dests = Destinations();
            using (var u = new System.Net.Sockets.UdpClient(System.Net.Sockets.AddressFamily.InterNetwork))
            {
                u.EnableBroadcast = true;
                int sent = 0;
                foreach (var d in dests)
                {
                    try { if (u.Send(req, req.Length, new System.Net.IPEndPoint(d, port)) == req.Length) sent++; }
                    catch { }
                }
                if (sent == 0) return found;
                var sw = Stopwatch.StartNew();
                while (found.Count < MaxFound)
                {
                    long left = timeoutMs - sw.ElapsedMilliseconds;
                    if (left <= 0) break;
                    try
                    {
                        if (!u.Client.Poll((int)Math.Min(left, 100000) * 1000, System.Net.Sockets.SelectMode.SelectRead)) continue;
                        var from = new System.Net.IPEndPoint(System.Net.IPAddress.Any, 0);
                        byte[] buf = u.Receive(ref from);
                        var s = ParseReply(buf, buf.Length, nonce, from.Address.ToString());
                        if (s == null) continue;
                        bool dup = false;
                        foreach (var f in found) if (f.Ip == s.Ip && f.TcpPort == s.TcpPort) dup = true;
                        if (!dup) found.Add(s);
                    }
                    catch (System.Net.Sockets.SocketException) { } // e.g. a connection-reset report for an earlier send
                    catch (ObjectDisposedException) { break; }
                }
            }
            return found;
        }
    }

    class Preset { public string Label, Address, Name; }

    class LauncherForm : Form, IMessageFilter
    {
        // ---- paths (the launcher sits in the PS3Recomp folder) ----
        readonly string root, here, iso, exe, elf, settingsFile, runDir, boostDir, appData;

        // ---- settings ----
        static readonly string[] ResNames = {
            "720p   (1280 x 720)   the PS3's own",
            "900p   (1600 x 900)",
            "1080p  (1920 x 1080)",
            "1440p  (2560 x 1440)",
            "1800p  (3200 x 1800)",
            "4K     (3840 x 2160)",
            "5K     (5120 x 2880)   heavy",
        };
        static readonly string[] ResScales = { "1", "1.25", "1.5", "2", "2.5", "3", "4" };
        readonly List<PadAction> kbActions = new List<PadAction>();    // Keyboard page
        readonly List<PadAction> kbmActions = new List<PadAction>();   // Keyboard & Mouse page
        readonly List<PadButton> padButtons = new List<PadButton>();   // Controller page
        static readonly string[] ModeIds = { "keyboard", "kbm", "controller" };
        static readonly string[] XButtons = { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "START", "BACK", "DUP", "DDOWN", "DLEFT", "DRIGHT" };
        static readonly string[] XButtonNames = {
            "A", "B", "X", "Y", "LB  (left bumper)", "RB  (right bumper)", "LT  (left trigger)", "RT  (right trigger)",
            "LS  (left stick click)", "RS  (right stick click)", "Start / Menu", "Back / View",
            "D-pad up", "D-pad down", "D-pad left", "D-pad right" };
        double mouseSens = 1.0;
        bool refreshingPad;
        readonly List<string> otherLines = new List<string>();   // lines of play_settings.txt we do not own
        bool loading;

        // ---- controls ----
        TabPage displayTab, graphicsTab;
        ComboBox resBox, winBox, modeBox, aaBox, shadowBox, shadowRangeBox, modelDetailBox, objectDistBox;
        static readonly string[] ModelDetailIds = { "1", "2", "4" };
        static readonly string[] ObjectDistIds = { "0.75", "1", "2", "0" };
        static readonly string[] ShadowNames = {
            "Low      (the game's own: 2048 x 1024)",
            "Medium   (2x)",
            "High     (3x)",
            "Ultra    (4x)",
        };
        CheckBox fxFullBox;
        CheckBox fpsBox;
        CheckBox unlockBox;                  // ENHANCEMENT (opt-in): one start with everything unlocked; never saved to the settings file
        volatile bool unlockThisRun;
        // ENHANCEMENT (opt-in): a separate TEST save. The game keeps its saves under PS3_GAMEDATA_ROOT\dev_hdd0\home\00000001\savedata
        // (ps3recomp cellSaveData.c; the user id folder is fixed), the first-boot data install under PS3_HDD0_ROOT, so pointing
        // PS3_GAMEDATA_ROOT at a second folder gives a second save set and the 2.5 GB install is shared.
        // ASSUMPTION: the game writes no save-like data through plain file calls under <hdd0>\home (none is found in the port sources).
        RadioButton rbSaveNormal, rbSaveTest;
        Label saveActiveLabel;
        bool testSave;                       // the active save set; kept in jsp-install.ini [install] savemode=normal|test
        volatile bool testSaveThisRun;
        const string TestSuffix = "_gamedata_test";   // <hdd0>_gamedata_test beside <hdd0>_gamedata (the normal save root, main.cpp)
        string backupNote = "";              // what BackupSave did, written to the log once RunGame has opened it
        const string SaveName = "NPUB31647-B0";                          // US save folder (BLUS31519)
        static readonly string[] SaveNames = { SaveName, "NPEB02204-B0" };   // US, EU (BLES02136): the launcher does not know the disc's region
        static readonly string[] AaIds = { "off", "fxaa", "fxaa2", "smaa" };
        Panel kbPanel, kbmPanel, padPanel;
        TabControl tabs;
        public string ShotDir;
        TrackBar sensBar, hudBar;
        Label hudLabel;
        Label sensLabel, modeHelp;
        RadioButton rbWindow, rbFull;
        CheckBox boostOffBox;
        Label statusLabel, keysNotice;
        Button startBtn, exitBtn;
        PadAction capturing;

        // ---- running game ----
        Process game;
        volatile bool gameRunning, closingAfterGame;

        public LauncherForm()
        {
            root = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
            if (!Directory.Exists(Path.Combine(root, "jstars19")) && Directory.Exists(Path.Combine(Path.GetDirectoryName(root), "jstars19")))
                root = Path.GetDirectoryName(root);   // run from the launcher folder itself
            here = Path.Combine(root, "jstars19");
            appData = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "JSP Fan Revival");
            iso = Path.Combine(root, @"RPCS3\games\J-Stars Victory Vs+.iso");
            exe = Path.Combine(here, "jsp.exe");
            elf = Path.Combine(here, "EBOOT.elf");
            settingsFile = Path.Combine(here, "play_settings.txt");
            runDir = Path.Combine(here, "run");
            boostDir = Path.Combine(root, @"tools\boost");

            AddKeyActions(kbActions);
            AddKeyActions(kbmActions);
            // Controller defaults as in cellPad.c (Xbox layout: A = Cross, B = Circle, ...).
            padButtons.Add(new PadButton("DPAD_UP", "D-pad up", "DUP"));
            padButtons.Add(new PadButton("DPAD_DOWN", "D-pad down", "DDOWN"));
            padButtons.Add(new PadButton("DPAD_LEFT", "D-pad left", "DLEFT"));
            padButtons.Add(new PadButton("DPAD_RIGHT", "D-pad right", "DRIGHT"));
            padButtons.Add(new PadButton("L1", "L1", "LB"));
            padButtons.Add(new PadButton("R1", "R1", "RB"));
            padButtons.Add(new PadButton("L2", "L2", "LT"));
            padButtons.Add(new PadButton("R2", "R2", "RT"));
            padButtons.Add(new PadButton("CROSS", "Cross  \u2715", "A"));
            padButtons.Add(new PadButton("CIRCLE", "Circle  \u25CB", "B"));
            padButtons.Add(new PadButton("SQUARE", "Square  \u25A1", "X"));
            padButtons.Add(new PadButton("TRIANGLE", "Triangle  \u25B3", "Y"));
            padButtons.Add(new PadButton("L3", "L3", "LS"));
            padButtons.Add(new PadButton("R3", "R3", "RS"));
            padButtons.Add(new PadButton("START", "Start", "START"));
            padButtons.Add(new PadButton("SELECT", "Select", "BACK"));

            BuildUi();
            loading = true;
            LoadSettings();
            LoadOnline();
            loading = false;
            RefreshKeyBoxes();
            RefreshPadBoxes();
            CheckGameReadsKeys();
            Application.AddMessageFilter(this);   // mouse clicks become bindings on the Keyboard & Mouse page
        }

        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            if (ShotDir == null) return;
            loading = true;   // switching pages below must not save anything
            int mode = modeBox.SelectedIndex;
            Directory.CreateDirectory(ShotDir);
            Shot("display", 0, -1);
            Shot("controls_keyboard", 1, 0);
            Shot("controls_kbm", 1, 1);
            Shot("controls_controller", 1, 2);
            Shot("online", tabs.TabPages.Count - 1, -1);
            modeBox.SelectedIndex = mode;
            Close();
        }

        void Shot(string name, int tab, int mode)
        {
            tabs.SelectedIndex = tab;
            if (mode >= 0) modeBox.SelectedIndex = mode;
            Application.DoEvents(); Thread.Sleep(300); Application.DoEvents();
            using (var bmp = new Bitmap(Width, Height))
            {
                DrawToBitmap(bmp, new Rectangle(0, 0, Width, Height));
                bmp.Save(Path.Combine(ShotDir, "launcher_" + name + ".png"));
            }
        }

        // Keys as in cellPad.c today (PAD_KEYS_SPLIT=1).
        static void AddKeyActions(List<PadAction> set)
        {
            set.Add(new PadAction("LSTICK_UP", "Left stick up", Keys.Up));
            set.Add(new PadAction("LSTICK_DOWN", "Left stick down", Keys.Down));
            set.Add(new PadAction("LSTICK_LEFT", "Left stick left", Keys.Left));
            set.Add(new PadAction("LSTICK_RIGHT", "Left stick right", Keys.Right));
            set.Add(new PadAction("DPAD_UP", "D-pad up", Keys.I));
            set.Add(new PadAction("DPAD_DOWN", "D-pad down", Keys.K));
            set.Add(new PadAction("DPAD_LEFT", "D-pad left", Keys.J));
            set.Add(new PadAction("DPAD_RIGHT", "D-pad right", Keys.L));
            set.Add(new PadAction("CROSS", "Cross  \u2715", Keys.Z));
            set.Add(new PadAction("CIRCLE", "Circle  \u25CB", Keys.X));
            set.Add(new PadAction("SQUARE", "Square  \u25A1", Keys.A));
            set.Add(new PadAction("TRIANGLE", "Triangle  \u25B3", Keys.S));
            set.Add(new PadAction("L1", "L1", Keys.Q));
            set.Add(new PadAction("R1", "R1", Keys.W));
            set.Add(new PadAction("L2", "L2", Keys.D1));
            set.Add(new PadAction("R2", "R2", Keys.D2));
            set.Add(new PadAction("L3", "L3", Keys.D3));
            set.Add(new PadAction("R3", "R3", Keys.D4));
            set.Add(new PadAction("START", "Start", Keys.Return));
            set.Add(new PadAction("SELECT", "Select", Keys.Tab));
            foreach (PadAction a in set) a.Set = set;
        }

        // =====================================================================
        // UI
        // =====================================================================

        static readonly Color Accent = Color.FromArgb(232, 96, 28);
        static readonly Color HeaderBack = Color.FromArgb(28, 30, 38);

        // Layout is written for 96 dpi (100 %); S() scales it to the screen's DPI.
        // Fonts are in points and scale by themselves.
        float dpiScale = 1F;
        int S(int v) { return (int)Math.Round(v * dpiScale); }
        Point P(int x, int y) { return new Point(S(x), S(y)); }
        Size Z(int w, int h) { return new Size(S(w), S(h)); }

        void BuildUi()
        {
            SuspendLayout();
            AutoScaleMode = AutoScaleMode.None;   // layout below is in 96-dpi units, scaled by S()
            using (Graphics g = CreateGraphics()) dpiScale = g.DpiX / 96F;
            Font = new Font("Segoe UI", 9F);
            Text = "J-Stars Victory VS+";
            ClientSize = Z(780, 820);
            FormBorderStyle = FormBorderStyle.FixedSingle;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            KeyPreview = true;
            try { if (File.Exists(exe)) Icon = Icon.ExtractAssociatedIcon(exe); } catch { }

            // Header
            var header = new Panel { BackColor = HeaderBack, Location = P(0, 0), Size = Z(780, 64) };
            var title = new Label {
                Text = "J-STARS VICTORY VS+", ForeColor = Color.White, AutoSize = true,
                Font = new Font("Segoe UI", 18F, FontStyle.Bold), Location = P(16, 8) };
            var sub = new Label {
                Text = "PC port launcher  -  v19", ForeColor = Color.FromArgb(255, 170, 120), AutoSize = true,
                Font = new Font("Segoe UI", 9F), Location = P(19, 41) };
            header.Controls.Add(title); header.Controls.Add(sub);
            Controls.Add(header);

            tabs = new TabControl { Location = P(12, 76), Size = Z(756, 676) };
            tabs.TabPages.Add(BuildDisplayTab());
            tabs.TabPages.Add(graphicsTab);
            tabs.TabPages.Add(BuildControlsTab());
            tabs.TabPages.Add(BuildOnlineTab());
            tabs.TabPages.Add(BuildCheatsTab());
            Controls.Add(tabs);

            // Bottom bar
            statusLabel = new Label { Location = P(14, 772), Size = Z(480, 36), Text = "Ready.", TextAlign = ContentAlignment.MiddleLeft };
            startBtn = new Button {
                Text = "Start Game", Location = P(506, 768), Size = Z(150, 40),
                BackColor = Accent, ForeColor = Color.White, FlatStyle = FlatStyle.Flat,
                Font = new Font("Segoe UI", 11F, FontStyle.Bold) };
            startBtn.FlatAppearance.BorderSize = 0;
            startBtn.Click += delegate { StartGame(); };
            exitBtn = new Button { Text = "Exit", Location = P(666, 768), Size = Z(100, 40), Font = new Font("Segoe UI", 10F) };
            exitBtn.Click += delegate { Close(); };
            Controls.Add(statusLabel); Controls.Add(startBtn); Controls.Add(exitBtn);
            AcceptButton = null;   // Enter must stay bindable on the Controls page

            ResumeLayout(false);
        }

        TabPage BuildDisplayTab()
        {
            var p = new TabPage("Display") { Padding = new Padding(12), AutoScroll = true };
            int y = 16;

            p.Controls.Add(new Label { Text = "Render resolution", Location = P(16, y), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
            y += 22;
            resBox = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Location = P(16, y), Width = S(360), Font = new Font("Consolas", 9.5F) };
            resBox.Items.AddRange(ResNames);
            resBox.SelectedIndexChanged += delegate { SaveIfReady(); };
            p.Controls.Add(resBox);

            y += 28;
            p.Controls.Add(new Label {
                Text = "The size the game draws at. Higher looks sharper and needs more GPU.\r\nThe window or screen shows it stretched to its own size.",
                Location = P(16, y), AutoSize = true, ForeColor = SystemColors.GrayText });
            y += 48;

            p.Controls.Add(new Label { Text = "Display mode", Location = P(16, y), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
            y += 22;
            rbWindow = new RadioButton { Text = "Window", Location = P(16, y), AutoSize = true };
            rbFull = new RadioButton { Text = "Fullscreen (borderless; F11 switches in the game)", Location = P(110, y), AutoSize = true };
            rbWindow.CheckedChanged += delegate { winBox.Enabled = rbWindow.Checked; SaveIfReady(); };
            p.Controls.Add(rbWindow); p.Controls.Add(rbFull);
            y += 34;

            p.Controls.Add(new Label { Text = "Window size", Location = P(16, y), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
            y += 22;
            winBox = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Location = P(16, y), Width = S(360) };
            winBox.Items.Add("Same as render resolution (shrunk to fit the screen)");
            foreach (Size s in WindowSizes()) winBox.Items.Add(s.Width + " x " + s.Height);
            winBox.SelectedIndexChanged += delegate { SaveIfReady(); };
            p.Controls.Add(winBox);
            y += 28;
            p.Controls.Add(new Label {
                Text = "Only used in a window. Lists every 16:9 size your screens offer. A window\r\nbigger than the screen is shrunk to fit. You can also drag the window edges in the game.",
                Location = P(16, y), AutoSize = true, ForeColor = SystemColors.GrayText });
            y += 52;

            boostOffBox = new CheckBox { Text = "Turn CPU boost off again when the game closes (keeps the laptop cool)", Location = P(16, y), AutoSize = true, Checked = true };
            boostOffBox.CheckedChanged += delegate { SaveIfReady(); };
            p.Controls.Add(boostOffBox);
            y += 24;
            p.Controls.Add(new Label {
                Text = "Boost is always turned on while the game runs.",
                Location = P(34, y), AutoSize = true, ForeColor = SystemColors.GrayText });
            y += 36;

            // v19: the graphics options get their own tab (the maintainer: more room)
            displayTab = p;
            p = new TabPage("Graphics") { Padding = new Padding(12), AutoScroll = true };
            graphicsTab = p;
            y = 16;
            // SHADOW_SCALE for the game: the shadow maps' resolution (1 = the game's own size).
            p.Controls.Add(new Label { Text = "Shadows", Location = P(16, y), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
            y += 22;
            shadowBox = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Location = P(16, y), Width = S(360) };
            shadowBox.Items.AddRange(ShadowNames);
            shadowBox.SelectedIndex = 1;
            shadowBox.SelectedIndexChanged += delegate { SaveIfReady(); };
            p.Controls.Add(shadowBox);
            y += 28;
            p.Controls.Add(new Label {
                Text = "Sharper, steadier shadows on characters and stages. Higher costs a little GPU;\r\nyour RPCS3 setup (300% scale) draws them like High.",
                Location = P(16, y), AutoSize = true, ForeColor = SystemColors.GrayText });
            y += 48;
            // SHADOW_RANGE for the game: how far from the fighters shadows reach (1 Normal, 2 Far).
            p.Controls.Add(new Label { Text = "Shadow distance", Location = P(16, y), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
            y += 22;
            shadowRangeBox = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Location = P(16, y), Width = S(360) };
            shadowRangeBox.Items.AddRange(new object[] { "Normal   (the game's own)", "Far      (2x: shadows reach further)" });
            shadowRangeBox.SelectedIndex = 0;
            shadowRangeBox.SelectedIndexChanged += delegate { SaveIfReady(); };
            p.Controls.Add(shadowRangeBox);
            y += 28;
            p.Controls.Add(new Label {
                Text = "Far keeps buildings', plants' and other fighters' shadows sharp further away.",
                Location = P(16, y), AutoSize = true, ForeColor = SystemColors.GrayText });
            y += 30;
            // MODEL_DETAIL / OBJECT_DISTANCE for the game: the map models' detail switch distances and how far map
            // objects are drawn (jsp_game.cpp lod_select_hook).
            p.Controls.Add(new Label { Text = "Model detail", Location = P(16, y), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
            y += 22;
            modelDetailBox = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Location = P(16, y), Width = S(360) };
            modelDetailBox.Items.AddRange(new object[] { "Normal   (the game's own)", "High     (full detail 2x further)", "Ultra    (full detail 4x further)" });
            modelDetailBox.SelectedIndex = 0;
            modelDetailBox.SelectedIndexChanged += delegate { SaveIfReady(); };
            p.Controls.Add(modelDetailBox);
            y += 36;
            p.Controls.Add(new Label { Text = "Object draw distance", Location = P(16, y), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
            y += 22;
            objectDistBox = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Location = P(16, y), Width = S(360) };
            objectDistBox.Items.AddRange(new object[] { "Near     (a little closer than the game)", "Normal   (the game's own)", "Far      (2x)", "Max      (objects never disappear)" });
            objectDistBox.SelectedIndex = 1;
            objectDistBox.SelectedIndexChanged += delegate { SaveIfReady(); };
            p.Controls.Add(objectDistBox);
            y += 28;
            p.Controls.Add(new Label {
                Text = "Map objects (plants, props) that the game hides when the camera is far from them.",
                Location = P(16, y), AutoSize = true, ForeColor = SystemColors.GrayText });
            y += 30;

            // UI_SCALE=0.50..1.00 for the game; the bar counts in 5 % steps (10..20).
            p.Controls.Add(new Label { Text = "Fight HUD size", Location = P(16, y), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
            y += 22;
            hudBar = new TrackBar { Location = P(12, y), Size = Z(300, 30), Minimum = 10, Maximum = 20, Value = 20,
                                    SmallChange = 1, LargeChange = 1, TickFrequency = 1, TickStyle = TickStyle.BottomRight };
            hudLabel = new Label { Location = P(318, y + 6), AutoSize = true, Text = "100%" };
            hudBar.ValueChanged += delegate { hudLabel.Text = (hudBar.Value * 5) + "%"; SaveIfReady(); };
            p.Controls.Add(hudBar); p.Controls.Add(hudLabel);
            y += 46;
            p.Controls.Add(new Label {
                Text = "Shrinks the health bars and other fight HUD, from 50% to 100% (the original size).",
                Location = P(16, y), AutoSize = true, ForeColor = SystemColors.GrayText });
            y += 30;

            // AA_MODE for the game: fights only, on the 3D picture before the HUD is drawn.
            p.Controls.Add(new Label { Text = "Edge smoothing", Location = P(16, y), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
            y += 22;
            aaBox = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Location = P(16, y), Width = S(220) };
            aaBox.Items.AddRange(new object[] { "Off", "FXAA", "FXAA, stronger", "SMAA" });
            aaBox.SelectedIndex = 1;
            aaBox.SelectedIndexChanged += delegate { SaveIfReady(); };
            p.Controls.Add(aaBox);
            fxFullBox = new CheckBox { Text = "Full-size smoke and effects", Location = P(250, y + 2), AutoSize = true, Checked = true };
            fxFullBox.CheckedChanged += delegate { SaveIfReady(); };
            p.Controls.Add(fxFullBox);
            y += 30;
            p.Controls.Add(new Label {
                Text = "In fights only. The HUD, text and menus stay sharp.",
                Location = P(16, y), AutoSize = true, ForeColor = SystemColors.GrayText });
            y += 30;
            // FPS_COUNTER for the game: frames per second, small, in the bottom-left corner.
            fpsBox = new CheckBox { Text = "FPS counter (bottom left)", Location = P(16, y), AutoSize = true, Checked = false };
            fpsBox.CheckedChanged += delegate { SaveIfReady(); };
            p.Controls.Add(fpsBox);
            // v19: the fight HUD size, edge smoothing, full-size effects and FPS counter live in the J-Stars game code,
            // which is not ported to v19 yet.
            return displayTab;
        }

        // ENHANCEMENT (opt-in, off by default): "Unlock everything". JSP_UNLOCK_ALL=1 makes the game edit its loaded save
        // (see installer\port\jsp\jsp_unlock.cpp). The game writes the unlocks into the save itself and cannot undo them,
        // so Start Game first copies the save folder, refuses to start if that fails, and the box unticks after one start.
        TabPage BuildCheatsTab()
        {
            var p = new TabPage("Cheats") { Padding = new Padding(12) };
            int y = 16;
            // The save in use: a normal save or a separate test save (see SaveRoot). The first test start seeds the test save
            // from the normal one (the normal side is only read).
            testSave = IniValue("savemode") == "test";
            p.Controls.Add(new Label { Text = "Save in use", Location = P(16, y), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
            y += 24;
            rbSaveNormal = new RadioButton { Text = "Normal save", Location = P(34, y), AutoSize = true, Checked = !testSave };
            y += 24;
            rbSaveTest = new RadioButton { Text = "Test save (separate; the normal save is not touched while it is in use)", Location = P(34, y), AutoSize = true, Checked = testSave };
            rbSaveNormal.CheckedChanged += delegate { SaveModeChanged(); };
            p.Controls.Add(rbSaveNormal);
            p.Controls.Add(rbSaveTest);
            y += 26;
            saveActiveLabel = new Label { Location = P(34, y), AutoSize = true, ForeColor = SystemColors.GrayText };
            p.Controls.Add(saveActiveLabel);
            y += 44;
            // Pre-ticked only when the installer's "Unlock everything" task wrote unlock=1; on a normal save Start Game still asks first.
            unlockBox = new CheckBox { Text = "Unlock everything (cheat)", Location = P(16, y), AutoSize = true, Checked = IniValue("unlock") == "1" };
            p.Controls.Add(unlockBox);
            y += 28;
            p.Controls.Add(new Label {
                Text = "Next start only: backs up the save in use first; cannot be undone except by restoring the backup (or, on the test save, by deleting it).",
                Location = P(34, y), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
            y += 34;
            p.Controls.Add(new Label {
                Text = "All cards, J-Points, characters, supports, titles, icons, decks and coins are unlocked in the save\r\n" +
                       "the game has loaded, and the game writes them out itself at its next save. The backup is the save folder\r\n" +
                       "(" + string.Join(" or ", SaveNames) + ") copied beside it as BACKUP-<folder>-<date>-<time>. Unlocked titles and icons\r\n" +
                       "are visible to online opponents; accepted online for now, which may change when online play is ready. To undo it,\r\n" +
                       "close the game and put the backup folder back under the original name.\r\n\r\n" +
                       "The test save is a second set of saves and trophies in " + TestSuffix + " beside the game's normal save folder; the installed\r\n" +
                       "game data is shared, so nothing is installed twice. Its first start copies your normal save into it, so the test\r\n" +
                       "begins from your progress; delete the folder to begin again from a fresh copy. Switch back to the normal save here at any time.",
                Location = P(34, y), AutoSize = true, ForeColor = SystemColors.GrayText });
            UpdateSaveModeUi();
            return p;
        }

        // jsp-install.ini [install] <key>=<value>, lower-cased value, "" when absent (the same file the Portable task writes).
        string IniValue(string key)
        {
            try
            {
                string ini = Path.Combine(root, "jsp-install.ini");
                if (!File.Exists(ini)) return "";
                foreach (string line in File.ReadAllLines(ini))
                {
                    int eq = line.IndexOf('=');
                    if (eq > 0 && line.Substring(0, eq).Trim().ToLowerInvariant() == key) return line.Substring(eq + 1).Trim().ToLowerInvariant();
                }
            }
            catch { }
            return "";
        }

        // Writes [install] savemode=... into jsp-install.ini (replaces the line, else adds it under [install]). Only the choice of
        // the save in use is stored here: the unlock switch itself is never written by the launcher.
        void WriteSaveMode(string value)
        {
            try
            {
                string ini = Path.Combine(root, "jsp-install.ini");
                var lines = File.Exists(ini) ? new System.Collections.Generic.List<string>(File.ReadAllLines(ini)) : new System.Collections.Generic.List<string>();
                int at = lines.FindIndex(l => l.Split('=')[0].Trim().ToLowerInvariant() == "savemode");
                if (at >= 0) lines[at] = "savemode=" + value;
                else
                {
                    int sec = lines.FindIndex(l => l.Trim().ToLowerInvariant() == "[install]");
                    if (sec >= 0) lines.Insert(sec + 1, "savemode=" + value); else { lines.Add("[install]"); lines.Add("savemode=" + value); }
                }
                File.WriteAllLines(ini, lines.ToArray());
            }
            catch (Exception ex) { Status("Could not remember the save in use: " + ex.Message); }
        }

        void SaveModeChanged()
        {
            if (gameRunning) { rbSaveTest.Checked = testSave; rbSaveNormal.Checked = !testSave; return; }   // not while the game runs
            testSave = rbSaveTest.Checked;
            WriteSaveMode(testSave ? "test" : "normal");
            UpdateSaveModeUi();
        }

        // Shows which save is active, on the tab and in the window title.
        void UpdateSaveModeUi()
        {
            Text = testSave ? "J-Stars Victory VS+  [TEST SAVE]" : "J-Stars Victory VS+";
            if (saveActiveLabel != null)
                saveActiveLabel.Text = testSave
                    ? "ACTIVE: TEST save (" + SaveRoot(true) + "). The normal save is not read or written by the game."
                    : "ACTIVE: normal save (" + SaveRoot(false) + ").";
        }

        // The save root the game gets as PS3_GAMEDATA_ROOT: the normal one is the port's own default (main.cpp: <hdd0>_gamedata), the test one a sibling.
        string SaveRoot(bool test)
        {
            return Path.Combine(appData, "hdd0") + (test ? TestSuffix : "_gamedata");
        }

        // The save folders this port may use (the game's data folder is <hdd0>_gamedata; the plain hdd0 layout is checked too).
        string[] SaveFolders() { return SaveFolders(testSaveThisRun); }

        string[] SaveFolders(bool test)
        {
            string hdd0 = Path.Combine(appData, "hdd0");
            var l = new System.Collections.Generic.List<string>();
            foreach (string n in SaveNames)
            {
                l.Add(Path.Combine(SaveRoot(test), @"dev_hdd0\home\00000001\savedata\" + n));
                if (!test) l.Add(Path.Combine(hdd0, @"home\00000001\savedata\" + n));
            }
            return l.ToArray();
        }

        // First start on the test save: copies the normal save folders into it (the normal side is only read). Never overwrites an
        // existing test save. Returns an error text or null.
        string SeedTestSave()
        {
            if (Directory.Exists(SaveRoot(true))) return null;
            try
            {
                foreach (string n in SaveNames)
                {
                    string rel = @"dev_hdd0\home\00000001\savedata\" + n;
                    string src = Path.Combine(SaveRoot(false), rel);
                    if (Directory.Exists(src)) CopyTree(src, Path.Combine(SaveRoot(true), rel));
                }
                Directory.CreateDirectory(SaveRoot(true));
                return null;
            }
            catch (Exception ex)
            {
                try { if (Directory.Exists(SaveRoot(true))) Directory.Delete(SaveRoot(true), true); } catch { }
                return "Could not copy your save into the test save (" + ex.Message + ").";
            }
        }

        // True when the game has run here before: it wrote its own data, the <hdd0>_gamedata folder (created by the game's runtime,
        // not by this launcher) or its first-boot data install <hdd0>\game\<id>-INSTALL.
        bool GameRanBefore()
        {
            string hdd0 = Path.Combine(appData, "hdd0");
            if (Directory.Exists(hdd0 + "_gamedata")) return true;
            string g = Path.Combine(hdd0, "game");
            return Directory.Exists(g) && Directory.GetDirectories(g, "*-INSTALL").Length > 0;
        }

        static void CopyTree(string from, string to)
        {
            Directory.CreateDirectory(to);
            foreach (string f in Directory.GetFiles(from)) File.Copy(f, Path.Combine(to, Path.GetFileName(f)), false);
            foreach (string d in Directory.GetDirectories(from)) CopyTree(d, Path.Combine(to, Path.GetFileName(d)));
        }

        static long TreeBytes(string dir, out int files)
        {
            long n = 0; files = 0;
            foreach (string f in Directory.GetFiles(dir, "*", SearchOption.AllDirectories)) { n += new FileInfo(f).Length; files++; }
            return n;
        }

        // Copies every existing save folder to a dated folder beside it and checks the copy (file count and bytes).
        // Returns null when the backup is made (or the game has never run here, so there is no save yet), otherwise the reason it
        // failed: the game must not start then. Also refused: the game has run before (GameRanBefore) but no save folder is found.
        // The folder names are the US and the EU save folder (NPEB02204-B0, the string in the EU program).
        string BackupSave()
        {
            string stamp = DateTime.Now.ToString("yyyyMMdd-HHmmss");
            bool any = false;
            backupNote = "";
            foreach (string src in SaveFolders())
            {
                if (!Directory.Exists(src)) continue;
                string dst = Path.Combine(Path.GetDirectoryName(src), "BACKUP-" + Path.GetFileName(src) + "-" + stamp);
                try
                {
                    if (Directory.Exists(dst)) throw new IOException("the folder " + dst + " exists already");
                    CopyTree(src, dst);
                    int fa, fb;
                    long a = TreeBytes(src, out fa), b = TreeBytes(dst, out fb);
                    if (fa != fb || a != b) throw new IOException("the copy differs from the save (" + fa + " files, " + a + " bytes; copy " + fb + ", " + b + ")");
                    backupNote += "Save backup: " + dst + " (" + fb + " files, " + b + " bytes). ";
                    any = true;
                }
                catch (Exception ex)
                {
                    try { if (Directory.Exists(dst)) Directory.Delete(dst, true); } catch { }
                    return "Could not back up the save (" + src + "): " + ex.Message;
                }
            }
            if (!any && GameRanBefore())
                return "The game has been run before, but no save folder was found, so there is nothing to back up and the cheat is not started. Looked for:\n" +
                       string.Join("\n", SaveFolders()) + "\n\nStart the game without the cheat and let it save once, or put your save folder there.";
            if (!any) backupNote = "Save backup: the game has not run here yet, no save folder, nothing to back up.";
            return null;
        }

        TabPage BuildControlsTab()
        {
            var p = new TabPage("Controls") { Padding = new Padding(12) };
            p.Controls.Add(new Label { Text = "Play with", Location = P(12, 13), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
            modeBox = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Location = P(84, 10), Width = S(220) };
            modeBox.Items.AddRange(new object[] { "Keyboard", "Keyboard & Mouse", "Controller" });
            modeBox.SelectedIndexChanged += delegate { capturing = null; ShowModePanel(); SaveIfReady(); };
            p.Controls.Add(modeBox);
            modeHelp = new Label { Location = P(12, 42), Size = Z(590, 34) };
            p.Controls.Add(modeHelp);

            kbPanel = BuildKeyPanel(kbActions, false);
            kbmPanel = BuildKeyPanel(kbmActions, true);
            padPanel = BuildPadPanel();
            foreach (Panel x in new Panel[] { kbPanel, kbmPanel, padPanel })
            {
                x.Location = P(0, 80); x.Size = Z(604, 300); x.Visible = false;
                p.Controls.Add(x);
            }

            var reset = new Button { Text = "Reset to defaults", Location = P(12, 388), Size = Z(140, 28) };
            reset.Click += delegate {
                capturing = null;
                if (Mode == 2) foreach (PadButton b in padButtons) b.Value = b.Default;
                else foreach (PadAction a in CurrentKeys()) a.Vk = a.DefaultVk;
                if (Mode == 1) sensBar.Value = 4;
                RefreshKeyBoxes(); RefreshPadBoxes(); SaveIfReady();
            };
            p.Controls.Add(reset);
            keysNotice = new Label { Location = P(162, 386), Size = Z(430, 36), ForeColor = Color.FromArgb(170, 90, 0) };
            p.Controls.Add(keysNotice);
            return p;
        }

        // Two columns: movement on the left, buttons on the right.
        Panel BuildKeyPanel(List<PadAction> set, bool mouse)
        {
            var panel = new Panel();
            int[] colX = { 12, 310 };
            int rowH = 24, left = 0, right = 0;
            foreach (PadAction a in set)
            {
                bool move = a.Id.StartsWith("LSTICK") || a.Id.StartsWith("DPAD");
                int col = move ? 0 : 1;
                int row = move ? left++ : right++;
                if (move && row == 4) { left++; row++; }   // gap between stick and d-pad
                int x = colX[col], y = row * rowH;
                panel.Controls.Add(new Label { Text = a.Label, Location = P(x, y + 3), Size = Z(120, 20) });
                var box = new Button { Location = P(x + 124, y), Size = Z(150, 22), Tag = a, TabStop = false };
                box.Click += KeyBox_Click;
                a.Box = box;
                panel.Controls.Add(box);
            }
            if (mouse)
            {
                int y = 9 * rowH + 6;
                panel.Controls.Add(new Label { Text = "Camera (right stick)", Location = P(12, y), Size = Z(120, 20) });
                panel.Controls.Add(new Label { Text = "move the mouse", Location = P(136, y), AutoSize = true, ForeColor = SystemColors.GrayText });
                y += 30;
                panel.Controls.Add(new Label { Text = "Mouse speed", Location = P(12, y), Size = Z(110, 20) });
                sensBar = new TrackBar { Location = P(120, y - 6), Size = Z(130, 30), Minimum = 1, Maximum = 20, Value = 4, TickStyle = TickStyle.None, TabStop = false };
                sensLabel = new Label { Location = P(254, y), AutoSize = true };
                sensBar.ValueChanged += delegate {
                    mouseSens = sensBar.Value / 4.0;
                    sensLabel.Text = mouseSens.ToString("0.00", System.Globalization.CultureInfo.InvariantCulture) + "x";
                    SaveIfReady();
                };
                sensLabel.Text = "1.00x";
                panel.Controls.Add(sensBar); panel.Controls.Add(sensLabel);
            }
            return panel;
        }

        // D-pad and shoulder buttons on the left, face and other buttons on the right.
        Panel BuildPadPanel()
        {
            var panel = new Panel();
            int rowH = 24;
            for (int i = 0; i < padButtons.Count; i++)
            {
                PadButton b = padButtons[i];
                int col = i < 8 ? 0 : 1;
                int row = i < 8 ? (i < 4 ? i : i + 1) : i - 8;   // gap between d-pad and shoulders
                int x = col == 0 ? 12 : 310, y = row * rowH;
                panel.Controls.Add(new Label { Text = b.Label, Location = P(x, y + 3), Size = Z(120, 20) });
                var box = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Location = P(x + 124, y), Width = S(150), Tag = b, TabStop = false };
                box.Items.AddRange(XButtonNames);
                box.SelectedIndexChanged += PadBox_Changed;
                b.Box = box;
                panel.Controls.Add(box);
            }
            panel.Controls.Add(new Label {
                Text = "Both sticks work as on the controller and cannot be changed here.",
                Location = P(12, 9 * rowH + 10), AutoSize = true, ForeColor = SystemColors.GrayText });
            return panel;
        }

        int Mode { get { return Math.Max(0, modeBox.SelectedIndex); } }
        List<PadAction> CurrentKeys() { return Mode == 1 ? kbmActions : kbActions; }

        void ShowModePanel()
        {
            kbPanel.Visible = Mode == 0; kbmPanel.Visible = Mode == 1; padPanel.Visible = Mode == 2;
            string[] help = {
                "Keyboard only: the mouse does nothing in the game. Click a box, then press the key you want.\r\n" +
                "Esc cancels. Esc (quit) and F11 (fullscreen) are kept by the game. A key already in use swaps places.",
                "Keys and mouse buttons. Click a box, then press a key or click a mouse button (left, right, middle,\r\n" +
                "side buttons). Esc cancels. Moving the mouse turns the camera. A key already in use swaps places.",
                "An Xbox-style controller. For each PS3 button, pick the controller button that presses it.\r\n" +
                "A button already in use swaps places.",
            };
            modeHelp.Text = help[Mode];
            RefreshKeyBoxes();
            UpdateNotice();
        }

        // Every 16:9 mode the connected screens report, plus the usual 16:9 sizes
        // that fit the biggest screen.
        static List<Size> WindowSizes()
        {
            var set = new SortedSet<long>();
            int maxW = 0, maxH = 0;
            foreach (Screen s in Screen.AllScreens)
            {
                var dm = new DEVMODE(); dm.dmSize = (short)Marshal.SizeOf(typeof(DEVMODE));
                for (int i = 0; EnumDisplaySettings(s.DeviceName, i, ref dm); i++)
                {
                    int w = dm.dmPelsWidth, h = dm.dmPelsHeight;
                    if (w > maxW) { maxW = w; maxH = h; }
                    if (w * 9 == h * 16 && w >= 640) set.Add((long)w << 32 | (uint)h);
                }
            }
            int[] common = { 1280, 1600, 1920, 2560, 3200, 3840, 5120 };
            foreach (int w in common)
                if (maxW == 0 || (w <= maxW && w * 9 / 16 <= Math.Max(maxH, 720))) set.Add((long)w << 32 | (uint)(w * 9 / 16));
            var list = new List<Size>();
            foreach (long v in set) list.Add(new Size((int)(v >> 32), (int)(v & 0xffffffff)));
            return list;
        }

        // =====================================================================
        // Key bindings
        // =====================================================================

        void KeyBox_Click(object sender, EventArgs e)
        {
            capturing = (PadAction)((Button)sender).Tag;
            RefreshKeyBoxes();
        }

        // ProcessCmdKey sees arrows, Tab and Enter, which KeyDown on a button never gets.
        protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
        {
            if (capturing != null)
            {
                Keys k = keyData & Keys.KeyCode;
                if (k == Keys.Escape) { capturing = null; RefreshKeyBoxes(); return true; }
                if (k == Keys.F11) { statusLabel.Text = "F11 is kept for fullscreen in the game."; return true; }
                if (k == Keys.LWin || k == Keys.RWin || k == Keys.Apps) return true;
                int vk = (int)k;
                if (k == Keys.LShiftKey || k == Keys.RShiftKey) vk = (int)Keys.ShiftKey;
                if (k == Keys.LControlKey || k == Keys.RControlKey) vk = (int)Keys.ControlKey;
                if (k == Keys.LMenu || k == Keys.RMenu) vk = (int)Keys.Menu;
                BindCaptured(vk);
                return true;
            }
            return base.ProcessCmdKey(ref msg, keyData);
        }

        // On the Keyboard & Mouse page, while a box waits for input, a mouse click
        // anywhere in the launcher becomes the binding (and does nothing else).
        public bool PreFilterMessage(ref Message m)
        {
            if (capturing == null || capturing.Set != kbmActions) return false;
            int vk;
            switch (m.Msg)
            {
                case 0x0201: vk = 0x01; break;   // WM_LBUTTONDOWN -> VK_LBUTTON
                case 0x0204: vk = 0x02; break;   // WM_RBUTTONDOWN -> VK_RBUTTON
                case 0x0207: vk = 0x04; break;   // WM_MBUTTONDOWN -> VK_MBUTTON
                case 0x020B: vk = ((m.WParam.ToInt64() >> 16) & 0xffff) == 1 ? 0x05 : 0x06; break;   // WM_XBUTTONDOWN
                default: return false;
            }
            BindCaptured(vk);
            return true;
        }

        void BindCaptured(int vk)
        {
            PadAction a = capturing;
            foreach (PadAction o in a.Set)
                if (o != a && o.Vk == vk) o.Vk = a.Vk;   // swap with whoever had that key
            a.Vk = vk;
            capturing = null;
            RefreshKeyBoxes(); SaveIfReady();
        }

        void RefreshKeyBoxes()
        {
            foreach (List<PadAction> set in new List<PadAction>[] { kbActions, kbmActions })
                foreach (PadAction a in set)
                {
                    bool cap = a == capturing;
                    a.Box.Text = cap ? (set == kbmActions ? "key or mouse button..." : "press a key...") : KeyName(a.Vk);
                    a.Box.BackColor = cap ? Color.FromArgb(255, 226, 200) : (a.Vk != a.DefaultVk ? Color.FromArgb(232, 240, 255) : SystemColors.Control);
                    a.Box.UseVisualStyleBackColor = !cap && a.Vk == a.DefaultVk;
                }
        }

        void PadBox_Changed(object sender, EventArgs e)
        {
            if (refreshingPad) return;
            var box = (ComboBox)sender;
            var b = (PadButton)box.Tag;
            if (box.SelectedIndex < 0) return;
            string v = XButtons[box.SelectedIndex];
            foreach (PadButton o in padButtons)
                if (o != b && o.Value == v) o.Value = b.Value;   // swap with whoever had that button
            b.Value = v;
            RefreshPadBoxes(); SaveIfReady();
        }

        void RefreshPadBoxes()
        {
            refreshingPad = true;
            foreach (PadButton b in padButtons)
            {
                b.Box.SelectedIndex = Array.IndexOf(XButtons, b.Value);
                b.Box.BackColor = b.Value != b.Default ? Color.FromArgb(232, 240, 255) : SystemColors.Window;
            }
            refreshingPad = false;
        }

        static string KeyName(int vk)
        {
            Keys k = (Keys)vk;
            switch (k)
            {
                case Keys.LButton: return "Left mouse";
                case Keys.RButton: return "Right mouse";
                case Keys.MButton: return "Middle mouse";
                case Keys.XButton1: return "Mouse side (back)";
                case Keys.XButton2: return "Mouse side (forward)";
                case Keys.Up: return "Up arrow";
                case Keys.Down: return "Down arrow";
                case Keys.Left: return "Left arrow";
                case Keys.Right: return "Right arrow";
                case Keys.Return: return "Enter";
                case Keys.Space: return "Space";
                case Keys.Back: return "Backspace";
                case Keys.ShiftKey: return "Shift";
                case Keys.ControlKey: return "Ctrl";
                case Keys.Menu: return "Alt";
                case Keys.Capital: return "Caps Lock";
                case Keys.Prior: return "Page Up";
                case Keys.Next: return "Page Down";
                case Keys.OemMinus: return "-";
                case Keys.Oemplus: return "=";
                case Keys.Oemcomma: return ",";
                case Keys.OemPeriod: return ".";
            }
            if (k >= Keys.D0 && k <= Keys.D9) return ((char)('0' + (k - Keys.D0))).ToString();
            if (k >= Keys.NumPad0 && k <= Keys.NumPad9) return "Num " + (k - Keys.NumPad0);
            if (k >= Keys.A && k <= Keys.Z) return ((char)vk).ToString();
            string s = vk >= 0xBA && vk <= 0xE2 ? MapKeyName(vk) : null;   // punctuation: the label on this layout
            return s ?? k.ToString();
        }

        // The key's own label on this keyboard layout (for the OEM punctuation keys).
        [DllImport("user32.dll")] static extern uint MapVirtualKey(uint code, uint mapType);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetKeyNameText(int lParam, StringBuilder buf, int size);
        static string MapKeyName(int vk)
        {
            uint sc = MapVirtualKey((uint)vk, 0);
            if (sc == 0) return null;
            var sb = new StringBuilder(64);
            return GetKeyNameText((int)(sc << 16), sb, sb.Capacity) > 0 ? sb.ToString() : null;
        }

        // Which settings this game build reads: look for the setting names
        // ("PAD_KEY_%s", "INPUT_MODE", "PAD_BTN_%s" in cellPad.c) inside the game exe.
        bool gameHasKeys = true, gameHasMouse = true, gameHasPadMap = true;
        void CheckGameReadsKeys()
        {
            keysNotice.Text = "";
            var t = new Thread(delegate () {
                bool k = false, m = false, b = false;
                try {
                    if (File.Exists(exe))
                    {
                        k = FileContains(exe, Encoding.ASCII.GetBytes("PAD_KEY_"));
                        m = FileContains(exe, Encoding.ASCII.GetBytes("INPUT_MODE"));
                        b = FileContains(exe, Encoding.ASCII.GetBytes("PAD_BTN_"));
                    }
                } catch { }
                try { BeginInvoke((MethodInvoker)delegate { gameHasKeys = k; gameHasMouse = m; gameHasPadMap = b; UpdateNotice(); }); } catch { }
            });
            t.IsBackground = true; t.Start();
        }

        void UpdateNotice()
        {
            if (keysNotice == null) return;
            const string later = " Your choices are saved\r\nand work after the next game update.";
            if (Mode == 0) keysNotice.Text = gameHasKeys ? "" : "This game build does not read key changes yet." + later;
            else if (Mode == 1) keysNotice.Text = gameHasMouse && gameHasKeys ? "" : "This game build has no mouse support yet." + later;
            else keysNotice.Text = gameHasPadMap ? "" : "This game build does not read controller changes yet." + later;
        }

        static bool FileContains(string path, byte[] needle)
        {
            var buf = new byte[1 << 20];
            using (var f = File.OpenRead(path))
            {
                int keep = 0, n;
                while ((n = f.Read(buf, keep, buf.Length - keep)) > 0)
                {
                    int len = keep + n;
                    for (int i = 0; i + needle.Length <= len; i++)
                    {
                        if (buf[i] != needle[0]) continue;
                        int j = 1;
                        while (j < needle.Length && buf[i + j] == needle[j]) j++;
                        if (j == needle.Length) return true;
                    }
                    keep = Math.Min(needle.Length - 1, len);
                    Buffer.BlockCopy(buf, len - keep, buf, 0, keep);
                }
            }
            return false;
        }

        // =====================================================================
        // Online tab: repsn.ini and community.ini in the shared folder (OnlineIni). Not part of play_settings.txt, and
        // the launcher sets no online environment variable for the game: the game reads the files itself (a variable
        // set by hand still wins there). Keys that belong to the game itself are never shown or written here.
        // =====================================================================
        RadioButton rbOnOff, rbOnRepsn, rbOnComm;
        TextBox repsnServerBox, repsnNameBox, presetLabelBox, presetAddrBox, presetNameBox;
        ListBox presetList;
        Button lanBtn, presetAddBtn, presetUpdBtn, presetDelBtn, presetActiveBtn, onlineSaveBtn;
        Label onlineMsg, onlineWhere;
        Panel repsnPanel, commPanel;
        readonly List<Preset> presets = new List<Preset>();
        readonly List<string> loadedLabels = new List<string>();
        string activeLabel = "";
        bool onlineDirty;

        string RepsnPath() { return Path.Combine(OnlineIni.SharedDir(root), "repsn.ini"); }
        string CommunityPath() { return Path.Combine(OnlineIni.SharedDir(root), "community.ini"); }

        TabPage BuildOnlineTab()
        {
            var p = new TabPage("Online") { Padding = new Padding(12), AutoScroll = true };
            int y = 16;
            p.Controls.Add(new Label { Text = "Online play", Location = P(16, y), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
            y += 24;
            rbOnOff = new RadioButton { Text = "Off", Location = P(16, y), AutoSize = true, Checked = true };
            rbOnRepsn = new RadioButton { Text = "re-psn", Location = P(90, y), AutoSize = true };
            rbOnComm = new RadioButton { Text = "Community server", Location = P(180, y), AutoSize = true };
            EventHandler pick = delegate { OnlineChanged(); };
            rbOnOff.CheckedChanged += pick; rbOnRepsn.CheckedChanged += pick; rbOnComm.CheckedChanged += pick;
            p.Controls.Add(rbOnOff); p.Controls.Add(rbOnRepsn); p.Controls.Add(rbOnComm);
            y += 26;
            p.Controls.Add(new Label {
                Text = "Only one is used. Choosing one turns the other off when you save.",
                Location = P(16, y), AutoSize = true, ForeColor = SystemColors.GrayText });
            y += 34;

            repsnPanel = new Panel { Location = P(0, y), Size = Z(730, 130) };
            repsnPanel.Controls.Add(new Label { Text = "re-psn server", Location = P(16, 0), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
            repsnPanel.Controls.Add(new Label { Text = "Server", Location = P(16, 28), Size = Z(110, 20) });
            repsnServerBox = new TextBox { Location = P(130, 25), Width = S(260), MaxLength = 260 };
            repsnServerBox.TextChanged += delegate { OnlineChanged(); };
            repsnPanel.Controls.Add(repsnServerBox);
            repsnPanel.Controls.Add(new Label { Text = "host or host:port, port 36100 when left out", Location = P(400, 28), AutoSize = true, ForeColor = SystemColors.GrayText });
            repsnPanel.Controls.Add(new Label { Text = "Online name", Location = P(16, 60), Size = Z(110, 20) });
            repsnNameBox = new TextBox { Location = P(130, 57), Width = S(160), MaxLength = 16 };
            repsnNameBox.TextChanged += delegate { OnlineChanged(); };
            repsnPanel.Controls.Add(repsnNameBox);
            repsnPanel.Controls.Add(new Label { Text = "3 to 16 of A-Z a-z 0-9 _ -, starting with a letter", Location = P(300, 60), AutoSize = true, ForeColor = SystemColors.GrayText });
            lanBtn = new Button { Text = "Find on LAN", Location = P(130, 92), Size = Z(110, 28) };
            lanBtn.Click += delegate { FindOnLan(); };
            repsnPanel.Controls.Add(lanBtn);
            repsnPanel.Controls.Add(new Label { Text = "looks for a re-psn server started with -discover on this network", Location = P(250, 98), AutoSize = true, ForeColor = SystemColors.GrayText });
            p.Controls.Add(repsnPanel);
            y += 140;

            commPanel = new Panel { Location = P(0, y), Size = Z(730, 300) };
            commPanel.Controls.Add(new Label { Text = "Community server", Location = P(16, 0), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
            commPanel.Controls.Add(new Label {
                Text = "Features that exist only in this port do not work on a community server.",
                Location = P(16, 22), AutoSize = true, ForeColor = SystemColors.GrayText });
            presetList = new ListBox { Location = P(16, 50), Size = Z(330, 150), Font = new Font("Consolas", 9F) };
            presetList.SelectedIndexChanged += delegate { PresetPicked(); };
            commPanel.Controls.Add(presetList);
            commPanel.Controls.Add(new Label { Text = "Preset name", Location = P(366, 53), Size = Z(90, 20) });
            presetLabelBox = new TextBox { Location = P(460, 50), Width = S(200), MaxLength = 32 };
            commPanel.Controls.Add(presetLabelBox);
            commPanel.Controls.Add(new Label { Text = "Address", Location = P(366, 85), Size = Z(90, 20) });
            presetAddrBox = new TextBox { Location = P(460, 82), Width = S(200), MaxLength = 260 };
            commPanel.Controls.Add(presetAddrBox);
            commPanel.Controls.Add(new Label { Text = "Online name", Location = P(366, 117), Size = Z(90, 20) });
            presetNameBox = new TextBox { Location = P(460, 114), Width = S(160), MaxLength = 16 };
            commPanel.Controls.Add(presetNameBox);
            presetAddBtn = new Button { Text = "Add", Location = P(366, 150), Size = Z(70, 28) };
            presetUpdBtn = new Button { Text = "Update", Location = P(442, 150), Size = Z(70, 28) };
            presetDelBtn = new Button { Text = "Delete", Location = P(518, 150), Size = Z(70, 28) };
            presetActiveBtn = new Button { Text = "Use this one", Location = P(366, 184), Size = Z(110, 28) };
            presetAddBtn.Click += delegate { PresetAdd(); };
            presetUpdBtn.Click += delegate { PresetUpdate(); };
            presetDelBtn.Click += delegate { PresetDelete(); };
            presetActiveBtn.Click += delegate { PresetUse(); };
            commPanel.Controls.Add(presetAddBtn); commPanel.Controls.Add(presetUpdBtn);
            commPanel.Controls.Add(presetDelBtn); commPanel.Controls.Add(presetActiveBtn);
            commPanel.Controls.Add(new Label {
                Text = "The preset marked * is the one used. Presets are kept in community.ini;\r\nthe launcher ships none.",
                Location = P(16, 208), AutoSize = true, ForeColor = SystemColors.GrayText });
            p.Controls.Add(commPanel);
            y += 296;

            onlineSaveBtn = new Button { Text = "Save online settings", Location = P(16, y), Size = Z(170, 30) };
            onlineSaveBtn.Click += delegate {
                string err = SaveOnline();
                if (err != null) MessageBox.Show(this, err, Text);
            };
            p.Controls.Add(onlineSaveBtn);
            onlineMsg = new Label { Location = P(196, y + 2), Size = Z(520, 40), ForeColor = Color.FromArgb(170, 90, 0) };
            p.Controls.Add(onlineMsg);
            onlineWhere = new Label { Location = P(16, y + 44), Size = Z(700, 20), ForeColor = SystemColors.GrayText };
            p.Controls.Add(onlineWhere);
            return p;
        }

        // Find on LAN: the search runs on a worker thread for about 1.2 s (LanDiscover), the result comes back on the UI thread.
        const int LanTimeoutMs = 1200;

        void FindOnLan()
        {
            lanBtn.Enabled = false;
            PresetMsg("Looking for a server on this network...");
            var t = new Thread(delegate () {
                List<LanServer> r = null; bool fail = false;
                try { r = LanDiscover.Find(LanDiscover.DefaultPort, LanTimeoutMs, null); } catch { fail = true; }
                try { BeginInvoke((MethodInvoker)delegate { LanDone(r, fail); }); } catch { }
            });
            t.IsBackground = true;
            t.Start();
        }

        static string LanLine(LanServer s)
        {
            return (s.Name.Length > 0 ? s.Name : "(no name)") + "  " + s.Ip + ":" + s.TcpPort + "  players " + s.Players
                + "  relay " + (s.Relay ? "yes" : "no") + "  version " + (s.Version.Length > 0 ? s.Version : "?");
        }

        void LanDone(List<LanServer> r, bool fail)
        {
            lanBtn.Enabled = true;
            if (fail) { PresetMsg("The search could not be run (network error)."); return; }
            var ok = new List<LanServer>();
            if (r != null) foreach (var s in r) if (OnlineIni.ValidAddress(s.Ip + ":" + s.TcpPort)) ok.Add(s);
            if (ok.Count == 0)
            {
                PresetMsg("No server found on this network. The host must start re-psn with -discover, and both PCs must be on the same network.");
                return;
            }
            LanServer pick = ok[0];
            if (ok.Count > 1)
            {
                pick = PickLanServer(ok);
                if (pick == null) { PresetMsg("Found " + ok.Count + " servers; none chosen."); return; }
            }
            repsnServerBox.Text = pick.Ip + ":" + pick.TcpPort;
            PresetMsg("Server set: " + LanLine(pick) + ". Not saved yet.");
        }

        LanServer PickLanServer(List<LanServer> list)
        {
            using (var f = new Form())
            {
                f.Text = "Servers on this network";
                f.FormBorderStyle = FormBorderStyle.FixedDialog; f.StartPosition = FormStartPosition.CenterParent;
                f.MinimizeBox = false; f.MaximizeBox = false; f.ShowInTaskbar = false;
                f.ClientSize = Z(560, 220);
                var lb = new ListBox { Location = P(10, 10), Size = Z(540, 160) };
                foreach (var s in list) lb.Items.Add(LanLine(s));
                lb.SelectedIndex = 0;
                var ok = new Button { Text = "Use this server", Location = P(330, 180), Size = Z(130, 28), DialogResult = DialogResult.OK };
                var cancel = new Button { Text = "Cancel", Location = P(470, 180), Size = Z(80, 28), DialogResult = DialogResult.Cancel };
                lb.DoubleClick += delegate { if (lb.SelectedIndex >= 0) f.DialogResult = DialogResult.OK; };
                f.Controls.Add(lb); f.Controls.Add(ok); f.Controls.Add(cancel);
                f.AcceptButton = ok; f.CancelButton = cancel;
                if (f.ShowDialog(this) == DialogResult.OK && lb.SelectedIndex >= 0) return list[lb.SelectedIndex];
                return null;
            }
        }

        int OnlineMethod() { return rbOnRepsn.Checked ? 1 : rbOnComm.Checked ? 2 : 0; }

        void OnlineChanged()
        {
            repsnPanel.Enabled = rbOnRepsn.Checked;
            commPanel.Enabled = rbOnComm.Checked;
            if (loading) return;
            onlineDirty = true;
            onlineMsg.ForeColor = Color.FromArgb(170, 90, 0);
            onlineMsg.Text = "Not saved yet.";
        }

        void RefreshPresetList()
        {
            int sel = presetList.SelectedIndex;
            presetList.Items.Clear();
            foreach (Preset q in presets)
                presetList.Items.Add((string.Equals(q.Label, activeLabel, StringComparison.OrdinalIgnoreCase) ? "* " : "  ") + q.Label + "  " + q.Address);
            if (sel >= 0 && sel < presetList.Items.Count) presetList.SelectedIndex = sel;
        }

        void PresetPicked()
        {
            int i = presetList.SelectedIndex;
            if (i < 0 || i >= presets.Count) return;
            presetLabelBox.Text = presets[i].Label; presetAddrBox.Text = presets[i].Address; presetNameBox.Text = presets[i].Name;
        }

        // null when the three boxes hold a good preset, else what is wrong; except = index being edited
        string PresetFieldsProblem(int except)
        {
            string l = presetLabelBox.Text.Trim(), a = presetAddrBox.Text.Trim(), n = presetNameBox.Text.Trim();
            if (!OnlineIni.ValidLabel(l)) return "The preset name is 1 to 32 of A-Z a-z 0-9 _ -.";
            if (!OnlineIni.ValidAddress(a)) return "The address is a host name or IP address, optionally :port (1 to 65535).";
            if (!OnlineIni.ValidName(n)) return "The online name is 3 to 16 of A-Z a-z 0-9 _ -, starting with a letter.";
            for (int i = 0; i < presets.Count; i++)
                if (i != except && string.Equals(presets[i].Label, l, StringComparison.OrdinalIgnoreCase)) return "A preset with that name exists.";
            return null;
        }

        void PresetMsg(string s) { onlineMsg.ForeColor = Color.FromArgb(170, 90, 0); onlineMsg.Text = s; }

        void PresetAdd()
        {
            string why = PresetFieldsProblem(-1);
            if (why == null && presets.Count >= OnlineIni.MaxPresets) why = "At most " + OnlineIni.MaxPresets + " presets.";
            if (why != null) { PresetMsg(why); return; }
            presets.Add(new Preset { Label = presetLabelBox.Text.Trim(), Address = presetAddrBox.Text.Trim(), Name = presetNameBox.Text.Trim() });
            RefreshPresetList();
            presetList.SelectedIndex = presets.Count - 1;
            OnlineChanged();
        }

        void PresetUpdate()
        {
            int i = presetList.SelectedIndex;
            if (i < 0 || i >= presets.Count) { PresetMsg("Pick a preset in the list first."); return; }
            string why = PresetFieldsProblem(i);
            if (why != null) { PresetMsg(why); return; }
            bool wasActive = string.Equals(presets[i].Label, activeLabel, StringComparison.OrdinalIgnoreCase);
            presets[i].Label = presetLabelBox.Text.Trim(); presets[i].Address = presetAddrBox.Text.Trim(); presets[i].Name = presetNameBox.Text.Trim();
            if (wasActive) activeLabel = presets[i].Label;
            RefreshPresetList();
            OnlineChanged();
        }

        void PresetDelete()
        {
            int i = presetList.SelectedIndex;
            if (i < 0 || i >= presets.Count) { PresetMsg("Pick a preset in the list first."); return; }
            if (string.Equals(presets[i].Label, activeLabel, StringComparison.OrdinalIgnoreCase)) activeLabel = "";
            presets.RemoveAt(i);
            presetList.SelectedIndex = -1;
            RefreshPresetList();
            OnlineChanged();
        }

        void PresetUse()
        {
            int i = presetList.SelectedIndex;
            if (i < 0 || i >= presets.Count) { PresetMsg("Pick a preset in the list first."); return; }
            activeLabel = presets[i].Label;
            RefreshPresetList();
            OnlineChanged();
        }

        // Reads both files into the controls. A file that cannot be used is reported and left alone.
        void LoadOnline()
        {
            bool was = loading; loading = true;
            string dir = OnlineIni.SharedDir(root);
            onlineWhere.Text = "Files: " + dir;
            OnlineIni ri, ci;
            string e1 = OnlineIni.Load(RepsnPath(), out ri), e2 = OnlineIni.Load(CommunityPath(), out ci);
            string note = e1 != null ? e1 : e2;
            bool on1 = ri.Get("repsn", "enabled") == "1", on2 = ci.Get("community", "enabled") == "1";
            if (on1 && on2) { note = "Both files were switched on; saving turns one off."; on1 = on2 = false; }
            rbOnRepsn.Checked = on1; rbOnComm.Checked = on2; rbOnOff.Checked = !on1 && !on2;
            repsnServerBox.Text = ri.Get("repsn", "server") ?? "";
            repsnNameBox.Text = ri.Get("repsn", "name") ?? "";
            presets.Clear(); loadedLabels.Clear();
            foreach (string l in ci.PresetLabels())
            {
                string sec = OnlineIni.PresetSection(l);
                string a = ci.Get(sec, "address") ?? "", n = ci.Get(sec, "name") ?? "";
                if (!OnlineIni.ValidLabel(l) || presets.Count >= OnlineIni.MaxPresets) continue;
                presets.Add(new Preset { Label = l, Address = a, Name = n });
                loadedLabels.Add(l);
            }
            activeLabel = ci.Get("community", "active") ?? "";
            presetLabelBox.Text = presetAddrBox.Text = presetNameBox.Text = "";
            RefreshPresetList();
            repsnPanel.Enabled = rbOnRepsn.Checked; commPanel.Enabled = rbOnComm.Checked;
            onlineDirty = false;
            onlineMsg.ForeColor = Color.FromArgb(170, 90, 0);
            onlineMsg.Text = note ?? "";
            loading = was;
        }

        // Writes both files in one step: the one being switched off first, then the one switched on, so that never
        // both say enabled=1 on disk. Both files are read again just before; only the keys owned here change.
        // Returns null when saved, else the reason.
        string SaveOnline()
        {
            int m = OnlineMethod();
            string server = repsnServerBox.Text.Trim(), rname = repsnNameBox.Text.Trim();
            if (m == 1)
            {
                if (!OnlineIni.ValidAddress(server)) return "re-psn: the server is a host name or IP address, optionally :port (1 to 65535).";
                if (!OnlineIni.ValidName(rname)) return "re-psn: the online name is 3 to 16 of A-Z a-z 0-9 _ -, starting with a letter.";
            }
            Preset act = null;
            foreach (Preset q in presets) if (string.Equals(q.Label, activeLabel, StringComparison.OrdinalIgnoreCase)) act = q;
            if (m == 2 && act == null) return "Community server: pick a preset and press \"Use this one\" (add one first if the list is empty).";

            OnlineIni ri, ci;
            string e = OnlineIni.Load(RepsnPath(), out ri);
            if (e != null) return e;
            e = OnlineIni.Load(CommunityPath(), out ci);
            if (e != null) return e;

            bool writeR = ri.Exists || m == 1, writeC = ci.Exists || m == 2 || presets.Count > 0;
            if (writeR)
            {
                ri.SetPreamble("; re-psn server settings. Written by the launcher; the game reads it.");
                ri.Set("repsn", "enabled", m == 1 ? "1" : "0");
                if (m == 1 || (OnlineIni.ValidAddress(server) && OnlineIni.ValidName(rname)))
                { ri.Set("repsn", "server", server); ri.Set("repsn", "name", rname); }
            }
            if (writeC)
            {
                ci.SetPreamble("; Community server settings. Features that exist only in this port do not work on a community server.");
                ci.Set("community", "enabled", m == 2 ? "1" : "0");
                ci.Set("community", "active", act != null ? act.Label : "");
                foreach (string old in loadedLabels)
                {
                    bool kept = false;
                    foreach (Preset q in presets) if (string.Equals(q.Label, old, StringComparison.OrdinalIgnoreCase)) kept = true;
                    if (!kept) ci.RemovePreset(old);
                }
                foreach (Preset q in presets)
                {
                    ci.Set(OnlineIni.PresetSection(q.Label), "address", q.Address);
                    ci.Set(OnlineIni.PresetSection(q.Label), "name", q.Name);
                }
            }
            // the side being switched off goes to disk first
            string err = null;
            if (m == 1) { if (writeC) err = ci.Save(); if (err == null && writeR) err = ri.Save(); }
            else { if (writeR) err = ri.Save(); if (err == null && writeC) err = ci.Save(); }
            if (err != null) { onlineMsg.ForeColor = Color.Firebrick; onlineMsg.Text = err; return err; }
            LoadOnline();
            onlineMsg.ForeColor = SystemColors.GrayText;
            onlineMsg.Text = m == 0 ? "Saved. Online is off." : m == 1 ? "Saved. re-psn is on." : "Saved. The community server is on.";
            return null;
        }

        // =====================================================================
        // Settings file (jstars\play_settings.txt, KEY=VALUE lines; play.ps1
        // turns every line into an environment variable for the game)
        // =====================================================================

        void LoadSettings()
        {
            var cur = new Dictionary<string, string>();
            if (File.Exists(settingsFile))
            {
                foreach (string line in File.ReadAllLines(settingsFile))
                {
                    string t = line.Trim();
                    int eq = t.IndexOf('=');
                    if (t.Length == 0 || t.StartsWith("#") || eq <= 0) continue;
                    string k = t.Substring(0, eq).Trim(), v = t.Substring(eq + 1).Trim();
                    if (k == "RES_SCALE" || k == "SHADOW_SCALE" || k == "SHADOW_RANGE" || k == "MODEL_DETAIL" || k == "OBJECT_DISTANCE" || k == "FULLSCREEN" || k == "WINDOW_SIZE" || k == "BOOST_OFF_ON_EXIT" || k == "UI_SCALE" || k == "AA_MODE" || k == "FX_FULL" || k == "FPS_COUNTER" ||
                        k == "INPUT_MODE" || k == "MOUSE_SENS" || k.StartsWith("PAD_KEY_") || k.StartsWith("KB_KEY_") ||
                        k.StartsWith("KBM_KEY_") || k.StartsWith("PAD_BTN_"))
                        cur[k] = v;
                    else
                        otherLines.Add(k + "=" + v);   // kept as they are
                }
            }
            string s;
            int ri = cur.TryGetValue("RES_SCALE", out s) ? Array.IndexOf(ResScales, s.Trim()) : 0;
            resBox.SelectedIndex = ri < 0 ? 0 : ri;
            int sh;
            if (!cur.TryGetValue("SHADOW_SCALE", out s) || !int.TryParse(s, out sh)) sh = 2;
            shadowBox.SelectedIndex = Math.Max(1, Math.Min(4, sh)) - 1;
            shadowRangeBox.SelectedIndex = cur.TryGetValue("SHADOW_RANGE", out s) && s.Trim() == "2" ? 1 : 0;
            { int i1 = cur.TryGetValue("MODEL_DETAIL", out s) ? Array.IndexOf(ModelDetailIds, s.Trim()) : 0; modelDetailBox.SelectedIndex = i1 < 0 ? 0 : i1;
              int i2 = cur.TryGetValue("OBJECT_DISTANCE", out s) ? Array.IndexOf(ObjectDistIds, s.Trim()) : 1; objectDistBox.SelectedIndex = i2 < 0 ? 1 : i2; }
            bool full = cur.TryGetValue("FULLSCREEN", out s) && s == "1";
            rbFull.Checked = full; rbWindow.Checked = !full;
            winBox.Enabled = !full;
            winBox.SelectedIndex = 0;
            if (cur.TryGetValue("WINDOW_SIZE", out s))
            {
                string want = s.Replace("x", " x ");
                int idx = winBox.Items.IndexOf(want);
                if (idx < 0) idx = winBox.Items.Add(want);
                winBox.SelectedIndex = idx;
            }
            boostOffBox.Checked = !(cur.TryGetValue("BOOST_OFF_ON_EXIT", out s) && s == "0");
            double ui;
            if (cur.TryGetValue("UI_SCALE", out s) && double.TryParse(s, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out ui))
                hudBar.Value = Math.Max(10, Math.Min(20, (int)Math.Round(ui * 20)));
            int aa = cur.TryGetValue("AA_MODE", out s) ? Array.IndexOf(AaIds, s.ToLowerInvariant()) : 1;
            aaBox.SelectedIndex = aa < 0 ? 1 : aa;
            fxFullBox.Checked = !(cur.TryGetValue("FX_FULL", out s) && s == "0");
            fpsBox.Checked = cur.TryGetValue("FPS_COUNTER", out s) && s == "1";
            // Keyboard page: KB_KEY_*, or PAD_KEY_* from before the input types existed.
            // Keyboard & Mouse page: KBM_KEY_*, else a copy of the keyboard page.
            string kbPrefix = HasPrefix(cur, "KB_KEY_") ? "KB_KEY_" : "PAD_KEY_";
            LoadKeys(cur, kbActions, kbPrefix);
            LoadKeys(cur, kbmActions, HasPrefix(cur, "KBM_KEY_") ? "KBM_KEY_" : kbPrefix);
            foreach (PadButton b in padButtons)
                if (cur.TryGetValue("PAD_BTN_" + b.Id, out s) && Array.IndexOf(XButtons, s.ToUpperInvariant()) >= 0) b.Value = s.ToUpperInvariant();
            double sens;
            if (cur.TryGetValue("MOUSE_SENS", out s) && double.TryParse(s, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out sens))
                sensBar.Value = Math.Max(1, Math.Min(20, (int)Math.Round(sens * 4)));
            int mode = Array.IndexOf(ModeIds, cur.TryGetValue("INPUT_MODE", out s) ? s.ToLowerInvariant() : "keyboard");
            modeBox.SelectedIndex = mode < 0 ? 0 : mode;
        }

        static bool HasPrefix(Dictionary<string, string> cur, string prefix)
        {
            foreach (string k in cur.Keys) if (k.StartsWith(prefix)) return true;
            return false;
        }

        static void LoadKeys(Dictionary<string, string> cur, List<PadAction> set, string prefix)
        {
            string s;
            foreach (PadAction a in set)
            {
                if (!cur.TryGetValue(prefix + a.Id, out s)) continue;
                int vk;
                if (s.StartsWith("0x", StringComparison.OrdinalIgnoreCase) ? int.TryParse(s.Substring(2), System.Globalization.NumberStyles.HexNumber, null, out vk) : int.TryParse(s, out vk))
                    if (vk > 0 && vk < 256) a.Vk = vk;
            }
        }

        void SaveIfReady() { if (!loading) SaveSettings(); }

        void SaveSettings()
        {
            var lines = new List<string>();
            lines.Add("# Written by the J-Stars v19 launcher.");
            lines.Add("RES_SCALE=" + ResScales[resBox.SelectedIndex]);
            lines.Add("SHADOW_SCALE=" + (shadowBox.SelectedIndex + 1));
            lines.Add("SHADOW_RANGE=" + (shadowRangeBox.SelectedIndex == 1 ? "2" : "1"));
            lines.Add("MODEL_DETAIL=" + ModelDetailIds[modelDetailBox.SelectedIndex]);
            lines.Add("OBJECT_DISTANCE=" + ObjectDistIds[objectDistBox.SelectedIndex]);
            lines.Add("FULLSCREEN=" + (rbFull.Checked ? "1" : "0"));
            if (winBox.SelectedIndex > 0) lines.Add("WINDOW_SIZE=" + ((string)winBox.SelectedItem).Replace(" ", ""));
            lines.Add("BOOST_OFF_ON_EXIT=" + (boostOffBox.Checked ? "1" : "0"));
            lines.Add("UI_SCALE=" + (hudBar.Value / 20.0).ToString("0.00", System.Globalization.CultureInfo.InvariantCulture));
            lines.Add("AA_MODE=" + AaIds[aaBox.SelectedIndex]);
            lines.Add("FX_FULL=" + (fxFullBox.Checked ? "1" : "0"));
            lines.Add("FPS_COUNTER=" + (fpsBox.Checked ? "1" : "0"));
            lines.Add("INPUT_MODE=" + ModeIds[Mode]);
            lines.Add("# Keys the game uses: the active page's keys as Windows virtual-key codes (0x01-0x06 = mouse buttons).");
            foreach (PadAction a in CurrentKeys())
                lines.Add("PAD_KEY_" + a.Id + "=0x" + a.Vk.ToString("X2"));
            lines.Add("MOUSE_SENS=" + mouseSens.ToString("0.00", System.Globalization.CultureInfo.InvariantCulture));
            lines.Add("# Controller: the Xbox-style button that presses each PS3 button.");
            foreach (PadButton b in padButtons)
                lines.Add("PAD_BTN_" + b.Id + "=" + b.Value);
            lines.Add("# Launcher only: the saved Keyboard and Keyboard & Mouse pages.");
            foreach (PadAction a in kbActions)
                lines.Add("KB_KEY_" + a.Id + "=0x" + a.Vk.ToString("X2"));
            foreach (PadAction a in kbmActions)
                lines.Add("KBM_KEY_" + a.Id + "=0x" + a.Vk.ToString("X2"));
            lines.AddRange(otherLines);
            try { File.WriteAllLines(settingsFile, lines.ToArray(), Encoding.ASCII); }
            catch (Exception ex) { statusLabel.Text = "Could not save settings: " + ex.Message; }
        }

        // =====================================================================
        // Starting and stopping the game
        // =====================================================================

        StreamWriter log;
        void Log(string s)
        {
            try { if (log != null) { log.WriteLine(DateTime.Now.ToString("HH:mm:ss") + "  " + s); log.Flush(); } } catch { }
        }

        volatile string lastStatus = "";
        void Status(string s)
        {
            lastStatus = s;
            if (InvokeRequired) { try { BeginInvoke((MethodInvoker)delegate { statusLabel.Text = s; }); } catch { } }
            else statusLabel.Text = s;
            Log(s);
        }

        void StartGame()
        {
            if (gameRunning) return;
            if (!File.Exists(exe)) { MessageBox.Show(this, "The game is not built yet:\n" + exe, Text); return; }
            if (!File.Exists(iso)) { MessageBox.Show(this, "The game disc image was not found:\n" + iso, Text); return; }
            if (Process.GetProcessesByName("jsp").Length > 0) { MessageBox.Show(this, "The game is already running.", Text); return; }
            if (onlineDirty)
            {
                string oerr = SaveOnline();   // unsaved Online tab edits are saved with the rest, or the start waits
                if (oerr != null) { tabs.SelectedIndex = tabs.TabPages.Count - 1; MessageBox.Show(this, oerr, Text); return; }
            }
            SaveSettings();
            unlockThisRun = false;
            testSaveThisRun = testSave;
            if (testSave)
            {
                string seed = SeedTestSave();   // test mode only ever reads the normal save, and only to seed a test save that does not exist yet
                if (seed != null) { Status("Not started: test save not made."); MessageBox.Show(this, seed + "\n\nThe game was not started.", Text); return; }
            }
            if (unlockBox.Checked && !testSave &&
                MessageBox.Show(this, "Unlock everything will change your NORMAL save (a dated backup is made first).\n\nTo try it on a separate copy, choose the Test save on the Cheats tab.\n\nUnlock the normal save?",
                                Text, MessageBoxButtons.YesNo, MessageBoxIcon.Warning, MessageBoxDefaultButton.Button2) != DialogResult.Yes)
            { Status("Not started: unlock not confirmed."); return; }
            if (unlockBox.Checked)
            {
                string why = BackupSave();
                if (why != null) { Status("Not started: no save backup."); MessageBox.Show(this, why + "\n\nThe game was not started.", Text); return; }
                unlockThisRun = true;
                unlockBox.Checked = false;   // one start only: the unlocks are written into the save and cannot be undone
                Status("Save backed up. Unlock everything is on for this start only.");
            }
            gameRunning = true;
            startBtn.Enabled = false;
            rbSaveNormal.Enabled = rbSaveTest.Enabled = false;   // the save in use is fixed while the game runs
            var t = new Thread(RunGame); t.IsBackground = false; t.Start();
        }

        // Background thread: mount, boost, run, wait, boost off, unmount.
        void RunGame()
        {
            Directory.CreateDirectory(runDir);
            Directory.CreateDirectory(Path.Combine(appData, "hdd0"));
            try { log = new StreamWriter(Path.Combine(runDir, "launcher_log.txt"), false); } catch { log = null; }
            Log("Launcher start. Settings: " + string.Join(" | ", File.ReadAllLines(settingsFile)));
            if (unlockThisRun) Log(backupNote);

            bool mountedHere = false, boostOff = true;
            Process boostOn = null;
            try
            {
                boostOn = RunBat(Path.Combine(boostDir, "boost_on.bat"), false);
                Log(boostOn != null ? "CPU boost on requested." : "boost_on.bat not found, boost left as it is.");

                Status("Mounting the game disc...");
                string drive = null, outp = null;
                for (int attempt = 1; attempt <= 2; attempt++)
                {
                    // Right after a previous run unmounted the disc, the first mount can fail: retry once.
                    if (attempt == 2) { Status("Disc not ready, trying again..."); Thread.Sleep(2000); }
                    outp = RunPowerShell(
                        "$ErrorActionPreference='Stop'; $iso='" + iso.Replace("'", "''") + "'; " +
                        "$img=Get-DiskImage -ImagePath $iso; $here=$false; " +
                        "if (-not $img.Attached) { $img=Mount-DiskImage -ImagePath $iso -Access ReadOnly -PassThru; $here=$true }; " +
                        "'DRIVE=' + ($img | Get-Volume).DriveLetter; 'MOUNTED_HERE=' + $here");
                    foreach (string line in outp.Split('\n'))
                    {
                        string l = line.Trim();
                        if (l.StartsWith("DRIVE=")) drive = l.Substring(6);
                        if (l == "MOUNTED_HERE=True") mountedHere = true;
                    }
                    Log("Mount attempt " + attempt + ": " + outp.Trim().Replace("\r\n", " | "));
                    if (!string.IsNullOrEmpty(drive) && drive.Length == 1) break;
                    drive = null;
                }
                if (drive == null) throw new Exception("Could not mount the disc image.\n" + outp.Trim());

                if (boostOn != null) boostOn.WaitForExit(15000);

                string hdd0 = Path.Combine(appData, "hdd0").Replace('\\', '/');
                string flash = Path.Combine(root, @"RPCS3\dev_flash").Replace('\\', '/');
                var psi = new ProcessStartInfo(exe, "\"" + elf + "\" --disc " + drive + ":/ --hdd0 \"" + hdd0 + "\" --flash \"" + flash + "\"");
                psi.WorkingDirectory = runDir;
                psi.UseShellExecute = false;
                psi.CreateNoWindow = true;
                psi.RedirectStandardOutput = true;
                psi.RedirectStandardError = true;
                psi.EnvironmentVariables["PS3_VFS_ROOT"] = drive + ":/";
                psi.EnvironmentVariables["PS3_HDD0_ROOT"] = hdd0;   // saves: <hdd0>_gamedata
                psi.EnvironmentVariables["PS3_DEV_FLASH"] = Path.Combine(root, @"RPCS3\dev_flash").Replace('\\', '/');
                psi.EnvironmentVariables["PS3_TITLE"] = "J-Stars Victory VS+";
                psi.EnvironmentVariables["PS3_VERBOSE"] = "0";
                foreach (string line in File.ReadAllLines(settingsFile))   // same rule as play.ps1
                {
                    string t = line;
                    int hash = t.IndexOf('#'); if (hash >= 0) t = t.Substring(0, hash);
                    int eq = t.IndexOf('='); if (eq <= 0) continue;
                    string k = t.Substring(0, eq).Trim(), v = t.Substring(eq + 1).Trim();
                    if (k.Length > 0) psi.EnvironmentVariables[k] = v;
                }
                boostOff = psi.EnvironmentVariables["BOOST_OFF_ON_EXIT"] != "0";
                psi.EnvironmentVariables["JSP_SETTINGS_FILE"] = settingsFile;   // the in-game settings pages write this file (jsp_settings.cpp)
                psi.EnvironmentVariables.Remove("JSP_UNLOCK_ALL");   // never from the settings file: only the Cheats box, after its backup
                if (unlockThisRun) { psi.EnvironmentVariables["JSP_UNLOCK_ALL"] = "1"; Log("Unlock everything: on for this start (save backed up)."); }
                // The save in use. Test: its own root, set after the settings file so no line there can point the test at the normal save.
                // Normal: left to the settings file or the port's default (<hdd0>_gamedata), as before.
                if (testSaveThisRun) { psi.EnvironmentVariables["PS3_GAMEDATA_ROOT"] = SaveRoot(true).Replace('\\', '/'); Log("Save in use: TEST save " + SaveRoot(true)); }
                else Log("Save in use: normal save.");

                Status("Starting the game...");
                game = Process.Start(psi);
                Thread o = Pump(game.StandardOutput.BaseStream, Path.Combine(runDir, "play_stdout.txt"));
                Thread e = Pump(game.StandardError.BaseStream, Path.Combine(runDir, "play_stderr.txt"));
                Status("Game running (disc on " + drive + ":). Close the game window or press Exit to stop.");
                // Once the game's window is up the launcher hides; this thread stays to
                // clean up (boost off, unmount) and then ends the launcher.
                for (int i = 0; i < 360 && !game.HasExited; i++)
                {
                    if (GameHasWindow(game.Id))
                    {
                        hiddenForGame = true;
                        Log("Game window is up, launcher hidden.");
                        BeginInvoke((MethodInvoker)delegate { Hide(); });
                        break;
                    }
                    Thread.Sleep(250);
                }
                game.WaitForExit();
                o.Join(3000); e.Join(3000);
                Log("Game exited with code " + game.ExitCode + ".");
            }
            catch (Exception ex)
            {
                Status("Could not start: " + ex.Message.Split('\n')[0]);
                Log(ex.ToString());
                try { BeginInvoke((MethodInvoker)delegate { MessageBox.Show(this, ex.Message, Text); }); } catch { }
            }
            finally
            {
                if (boostOff)
                {
                    Process p = RunBat(Path.Combine(boostDir, "boost_off.bat"), true);
                    Log(p != null ? "CPU boost off." : "boost_off.bat not found.");
                }
                else Log("CPU boost left on (option unticked).");
                if (mountedHere)
                {
                    RunPowerShell("Dismount-DiskImage -ImagePath '" + iso.Replace("'", "''") + "' | Out-Null");
                    Log("Disc unmounted.");
                }
                int code = 0; try { code = game != null ? game.ExitCode : 0; } catch { }
                game = null;
                gameRunning = false;
                if (lastStatus.StartsWith("Game running") || lastStatus.StartsWith("Closing"))
                    Status("Game closed" + (code != 0 ? " (exit code " + code + ", see jstars19\\run\\play_stderr.txt)." : "."));
                try { if (log != null) log.Close(); } catch { }
                log = null;
                try {
                    BeginInvoke((MethodInvoker)delegate {
                        startBtn.Enabled = true;
                        rbSaveNormal.Enabled = rbSaveTest.Enabled = true;
                        if (closingAfterGame || hiddenForGame) { Close(); return; }   // nothing left running
                        if (WindowState == FormWindowState.Minimized) WindowState = FormWindowState.Normal;
                        Activate();
                    });
                } catch { }
            }
        }

        static Thread Pump(Stream from, string file)
        {
            var t = new Thread(delegate () {
                try { using (var f = new FileStream(file, FileMode.Create, FileAccess.Write, FileShare.Read)) {
                    var buf = new byte[65536]; int n;
                    while ((n = from.Read(buf, 0, buf.Length)) > 0) { f.Write(buf, 0, n); f.Flush(); }
                } } catch { }
            });
            t.IsBackground = true; t.Start();
            return t;
        }

        // Run a .bat with no console window; null when the file is missing.
        static Process RunBat(string bat, bool wait)
        {
            if (!File.Exists(bat)) return null;
            var psi = new ProcessStartInfo("cmd.exe", "/c \"\"" + bat + "\"\"") { UseShellExecute = false, CreateNoWindow = true };
            Process p = Process.Start(psi);
            if (wait) p.WaitForExit(15000);
            return p;
        }

        // A plain -Command (no -EncodedCommand: security software flags hidden encoded
        // PowerShell). Progress is silenced, or it lands on stderr as a CLIXML blob.
        static string RunPowerShell(string script)
        {
            script = "$ProgressPreference='SilentlyContinue'; " + script;
            var psi = new ProcessStartInfo("powershell.exe", "-NoProfile -NonInteractive -Command \"" + script.Replace("\"", "\\\"") + "\"") {
                UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true };
            using (Process p = Process.Start(psi))
            {
                var err = p.StandardError.ReadToEndAsync();
                string o = p.StandardOutput.ReadToEnd();
                p.WaitForExit(60000);
                return o + err.Result;
            }
        }

        // ---- closing the game gently ----
        delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
        [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
        [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
        [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
        [DllImport("user32.dll")] static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
        const uint WM_CLOSE = 0x0010;

        volatile bool hiddenForGame;

        static bool GameHasWindow(int pid)
        {
            bool found = false;
            EnumWindows(delegate (IntPtr h, IntPtr l) {
                uint p; GetWindowThreadProcessId(h, out p);
                if (p == (uint)pid && IsWindowVisible(h)) { found = true; return false; }
                return true;
            }, IntPtr.Zero);
            return found;
        }

        // Post WM_CLOSE to the game's visible windows: the same as clicking the X.
        static int CloseGameWindows(int pid)
        {
            int n = 0;
            EnumWindows(delegate (IntPtr h, IntPtr l) {
                uint p; GetWindowThreadProcessId(h, out p);
                if (p == (uint)pid && IsWindowVisible(h)) { PostMessage(h, WM_CLOSE, IntPtr.Zero, IntPtr.Zero); n++; }
                return true;
            }, IntPtr.Zero);
            return n;
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            Process g = game;
            if (gameRunning && g != null)
            {
                e.Cancel = true;
                if (closingAfterGame) return;
                if (MessageBox.Show(this, "The game is still running. Close the game and exit?", Text, MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes)
                    return;
                closingAfterGame = true;
                exitBtn.Enabled = false;
                Status("Closing the game...");
                int pid = g.Id;
                var t = new Thread(delegate () {
                    if (CloseGameWindows(pid) == 0) { try { g.CloseMainWindow(); } catch { } }
                    if (g.WaitForExit(20000)) return;
                    bool force = false;
                    Invoke((MethodInvoker)delegate {
                        force = MessageBox.Show(this, "The game did not close within 20 seconds. Force it to stop?", Text, MessageBoxButtons.YesNo, MessageBoxIcon.Warning) == DialogResult.Yes;
                        if (!force) { closingAfterGame = false; exitBtn.Enabled = true; Status("Game still running."); }
                    });
                    if (force) { Log("Forced stop."); try { g.Kill(); } catch { } }
                });
                t.IsBackground = true; t.Start();
                return;
            }
            if (gameRunning) { e.Cancel = true; Status("Still starting the game; press Exit again in a moment."); return; }
            base.OnFormClosing(e);
        }

        // ---- display modes ----
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct DEVMODE
        {
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
            public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
            public int dmFields, dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
            public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
            public short dmLogPixels;
            public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
            public int dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2, dmPanningWidth, dmPanningHeight;
        }
        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        static extern bool EnumDisplaySettings(string device, int mode, ref DEVMODE dm);
    }
}
