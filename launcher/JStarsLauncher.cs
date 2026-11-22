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
    // BEGIN MODS MANAGER: identical in both launcher variants; no game or GUI calls.
    class ModsManager
    {
        public class Entry { public string Id; public bool Enabled, Installed; public override string ToString() { return Id + (Installed ? "" : " (missing folder)"); } }
        public readonly List<Entry> Entries = new List<Entry>();
        public string DirectoryPath, ListPath;
        public bool GlobalOff;
        string rootPath, settingsPath, workingPath; bool installedPaths; Func<string, string> environment;
        byte[] original;
        string[] lines;
        string newline;
        bool bom;
        static bool Valid(string id) { if (id.Length < 1 || id.Length > 40 || id[0] == '-') return false; foreach (char c in id) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false; return true; }
        static byte[] Read(string path)
        {
            try { using (var f = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read)) { if (f.Length > 1048576) throw new IOException("mods.txt is larger than 1 MiB."); byte[] b = new byte[(int)f.Length]; int n = 0, k; while (n < b.Length && (k = f.Read(b, n, b.Length - n)) > 0) n += k; if (n != b.Length) throw new IOException("The file changed while it was read."); return b; } }
            catch (FileNotFoundException) { return null; }
            catch (DirectoryNotFoundException) { return null; }
        }
        static bool Same(byte[] a, byte[] b) { if (a == null || b == null) return a == b; if (a.Length != b.Length) return false; for (int i = 0; i < a.Length; i++) if (a[i] != b[i]) return false; return true; }
        static string Decode(byte[] b) { string text = new UTF8Encoding(false, true).GetString(b ?? new byte[0]); return text.Length > 0 && text[0] == '\uFEFF' ? text.Substring(1) : text; }
        static string[] Lines(string text) { if (text.Length == 0) return new string[0]; var result = new List<string>(text.Split('\n')); if (text.EndsWith("\n")) result.RemoveAt(result.Count - 1); return result.ToArray(); }
        static void Overrides(string path, IDictionary<string, string> values, bool installed, string root)
        {
            byte[] bytes = Read(path);
            if (bytes == null) return;
            foreach (string raw in (installed ? Encoding.Default.GetString(bytes) : Decode(bytes)).Split('\n')) {
                string t = raw;
                if (installed) { if (t.Length > 1022) throw new IOException("A settings line is too long for the game launcher."); if (t.StartsWith("#") || t.StartsWith(";")) continue; }
                else { int h = t.IndexOf('#'); if (h >= 0) t = t.Substring(0, h); }
                int eq = t.IndexOf('='); if (eq <= 0) continue;
                string k = installed ? t.Substring(0, eq) : t.Substring(0, eq).Trim();
                string v = installed ? t.Substring(eq + 1).TrimEnd('\r', ' ') : t.Substring(eq + 1).Trim();
                if (k.Equals("JSP_MODS_DIR", StringComparison.OrdinalIgnoreCase) || k.Equals("JSP_MODS_LIST", StringComparison.OrdinalIgnoreCase) || k.Equals("JSP_MODS", StringComparison.OrdinalIgnoreCase)) {
                    if (installed && v.StartsWith("@APP@", StringComparison.Ordinal)) v = root + v.Substring(5);
                    values[k.ToUpperInvariant()] = v;
                }
            }
        }
        public static ModsManager Load(string root, string settings, string working, bool installed, Func<string, string> env)
        {
            var values = new Dictionary<string, string>();
            values["JSP_MODS"] = env("JSP_MODS"); values["JSP_MODS_DIR"] = env("JSP_MODS_DIR"); values["JSP_MODS_LIST"] = env("JSP_MODS_LIST");
            if (installed) Overrides(Path.Combine(root, "paths.txt"), values, true, root);
            if (!string.IsNullOrEmpty(settings)) Overrides(settings, values, installed, root);
            string dir = values["JSP_MODS_DIR"], list = values["JSP_MODS_LIST"];
            if (string.IsNullOrEmpty(dir)) dir = Path.Combine(root, "mods");
            // The settings window saves this active file before starting the game; the C launcher then selects it.
            if (string.IsNullOrEmpty(list)) list = Path.Combine(string.IsNullOrEmpty(settings) ? root : Path.GetDirectoryName(Path.GetFullPath(settings)), "mods.txt");
            var m = new ModsManager(); m.GlobalOff = values["JSP_MODS"] == "0"; m.rootPath = root; m.settingsPath = settings; m.workingPath = working; m.installedPaths = installed; m.environment = env;
            m.DirectoryPath = Path.GetFullPath(Path.IsPathRooted(dir) ? dir : Path.Combine(working, dir));
            m.ListPath = Path.GetFullPath(Path.IsPathRooted(list) ? list : Path.Combine(working, list));
            m.original = Read(m.ListPath); string text = Decode(m.original); m.bom = m.original != null && m.original.Length >= 3 && m.original[0] == 239 && m.original[1] == 187 && m.original[2] == 191;
            m.newline = text.Contains("\r\n") ? "\r\n" : "\n"; m.lines = Lines(text);
            var seen = new HashSet<string>(StringComparer.Ordinal);
            foreach (string raw in m.lines) { string t = raw; int h = t.IndexOf('#'); if (h >= 0) t = t.Substring(0, h); t = t.Trim(); if (t.Length == 0) continue; bool on = t[0] != '-'; if (!on) t = t.Substring(1).TrimStart(); if (!Valid(t)) throw new IOException("mods.txt has an invalid ID; fix the file before saving here."); if (seen.Add(t)) m.Entries.Add(new Entry { Id = t, Enabled = on }); }
            string[] dirs;
            try { dirs = Directory.GetDirectories(m.DirectoryPath); } catch (DirectoryNotFoundException) { dirs = new string[0]; }
            Array.Sort(dirs, StringComparer.Ordinal);
            foreach (string path in dirs) {
                string id = Path.GetFileName(path); if (!Valid(id) || (File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0) continue;
                string manifest = Path.Combine(path, "mod.json"); FileAttributes a;
                try { a = File.GetAttributes(manifest); } catch (FileNotFoundException) { continue; } catch (DirectoryNotFoundException) { continue; }
                if ((a & (FileAttributes.Directory | FileAttributes.ReparsePoint)) != 0) continue;
                Entry item = m.Entries.Find(delegate(Entry e) { return e.Id == id; });
                if (item == null) { item = new Entry { Id = id, Enabled = true }; m.Entries.Add(item); }
                item.Installed = true;
            }
            return m;
        }
        public void Move(int from, int to) { if (from < 0 || to < 0 || from >= Entries.Count || to >= Entries.Count) return; Entry e = Entries[from]; Entries.RemoveAt(from); Entries.Insert(to, e); }
        string Render()
        {
            var b = new StringBuilder(); bool inserted = false;
            foreach (string raw in lines) {
                string line = raw.TrimEnd('\r'), t = line; int h = t.IndexOf('#'); if (h >= 0) t = t.Substring(0, h);
                if (t.Trim().Length == 0) { b.Append(line).Append(newline); continue; }
                if (!inserted) { foreach (Entry e in Entries) b.Append(e.Enabled ? "" : "-").Append(e.Id).Append(newline); inserted = true; }
                if (h >= 0) b.Append(line.Substring(h)).Append(newline);
            }
            if (!inserted) foreach (Entry e in Entries) b.Append(e.Enabled ? "" : "-").Append(e.Id).Append(newline);
            return b.ToString();
        }
        public void Save()
        {
            VerifyPaths();
            if (!Same(original, Read(ListPath))) throw new IOException("mods.txt changed in another program. Refresh before saving.");
            if (File.Exists(ListPath) && (File.GetAttributes(ListPath) & FileAttributes.ReparsePoint) != 0) throw new IOException("mods.txt is a link; edit its target explicitly instead.");
            string text = Render(); var enc = new UTF8Encoding(bom); byte[] body = enc.GetBytes(text), pre = enc.GetPreamble();
            if (body.Length + pre.Length > 1048576) throw new IOException("mods.txt would be larger than 1 MiB.");
            string temp = ListPath + ".tmp-" + Guid.NewGuid().ToString("N");
            Directory.CreateDirectory(Path.GetDirectoryName(ListPath));
            try { using (var f = new FileStream(temp, FileMode.CreateNew, FileAccess.Write, FileShare.None)) { f.Write(pre, 0, pre.Length); f.Write(body, 0, body.Length); f.Flush(true); } if (!Same(original, Read(ListPath))) throw new IOException("mods.txt changed while saving. Refresh before saving."); if (original == null) File.Move(temp, ListPath); else File.Replace(temp, ListPath, null); }
            finally { if (File.Exists(temp)) File.Delete(temp); }
            original = new byte[pre.Length + body.Length]; Buffer.BlockCopy(pre, 0, original, 0, pre.Length); Buffer.BlockCopy(body, 0, original, pre.Length, body.Length); lines = Lines(text);
        }
        public void VerifyPaths()
        {
            ModsManager current = Load(rootPath, settingsPath, workingPath, installedPaths, environment);
            if (current.DirectoryPath != DirectoryPath || current.ListPath != ListPath) throw new IOException("Mod paths changed. Refresh before installing or saving.");
        }
        public static string QuoteArgument(string value)
        {
            var b = new StringBuilder("\""); int slashes = 0;
            foreach (char c in value) { if (c == '\\') { slashes++; continue; } b.Append('\\', c == '"' ? slashes * 2 + 1 : slashes); b.Append(c); slashes = 0; }
            return b.Append('\\', slashes * 2).Append('"').ToString();
        }
        public string PackageArguments(string root, string archive)
        {
            VerifyPaths();
            return "-I -X utf8 " + QuoteArgument(Path.Combine(root, "tools", "mod_package.py")) + " --archive " + QuoteArgument(Path.GetFullPath(archive)) + " --mods " + QuoteArgument(DirectoryPath);
        }
        public string InstallZip(string root, string archive)
        {
            string python = Path.Combine(root, "toolchain", "python", "python.exe"), helper = Path.Combine(root, "tools", "mod_package.py");
            if (!File.Exists(python) || !File.Exists(helper)) throw new IOException("The bundled mod installer is missing. Restore the complete installer or portable folder.");
            var start = new ProcessStartInfo(python, PackageArguments(root, archive)) { UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = root, RedirectStandardOutput = true, RedirectStandardError = true };
            using (Process process = Process.Start(start)) {
                string error = ""; Exception readError = null;
                var drain = new Thread(delegate() { try { error = process.StandardError.ReadToEnd(); } catch (Exception e) { readError = e; } });
                drain.IsBackground = true; drain.Start();
                string output = process.StandardOutput.ReadToEnd(); process.WaitForExit(); drain.Join();
                if (readError != null) throw new IOException("Could not read the mod installer's result.", readError);
                if (process.ExitCode != 0) throw new IOException(error.Trim().Length > 0 ? error.Trim() : "The mod package could not be installed.");
                return output.Trim();
            }
        }
        public void Export(System.Collections.Specialized.StringDictionary env)
        {
            if (Directory.Exists(DirectoryPath)) env["JSP_MODS_DIR"] = DirectoryPath;
            if (File.Exists(ListPath)) env["JSP_MODS_LIST"] = ListPath;
        }
    }
    // END MODS MANAGER

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
        readonly string root, here, iso, exe, elf, settingsFile, runDir, boostDir;
        readonly string gameProc;   // the game's process name (the exe's file name without .exe), for the "already running" check
        // Installed by setup or the portable zip (paths.txt / jsp-install.ini beside this exe): only the settings and
        // a Start Game button that hands over to jsp-launcher.exe (disc check, paths, boost); no CPU boost option.
        readonly bool installed;
        readonly string launcherExe;

        // ---- settings ----
        static readonly string[] ResNames = {
            "720p   (1280 x 720)   the PS3's own",
            "1440p  (2560 x 1440)",
            "4K     (3840 x 2160)",
            "5K     (5120 x 2880)   heavy, not tested yet",
        };
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
        ComboBox resBox, winBox, modeBox, aaBox;
        ComboBox shadowBox, shadowRangeBox, modelDetailBox, objectDistBox;   // the Graphics tab
        static readonly string[] ShadowNames = {
            "Low      (the game's own: 2048 x 1024)",
            "Medium   (2x)",
            "High     (3x)",
            "Ultra    (4x)",
        };
        static readonly string[] ModelDetailIds = { "1", "2", "4" };          // MODEL_DETAIL
        static readonly string[] ObjectDistIds = { "0.75", "1", "2", "0" };   // OBJECT_DISTANCE (0 = never hidden)
        CheckBox fxFullBox;
        CheckBox statsBox;
        CheckBox fpsBox;
        CheckBox pipeBox;   // PIPE for the game: two-thread drawing (toolkit patches 2912, 7907), on by default
        CheckBox menuBox;   // MENU_PAGES for the game: the two-page Main Menu (and 3v3 with it), on by default; only the off choice is written
        CheckBox unlockBox;                  // ENHANCEMENT (opt-in): one start with everything unlocked; never saved to the settings file
        volatile bool unlockThisRun;
        string backupNote = "";              // what BackupSave did, written to the log once RunGame has opened it
        const string SaveName = "NPUB31647-B0";                          // US save folder (BLUS31519)
        static readonly string[] SaveNames = { SaveName, "NPEB02204-B0" };   // US, EU (BLES02136): the launcher does not know the disc's region
        static readonly string[] AaIds = { "off", "fxaa", "fxaa2", "smaa" };
        Panel kbPanel, kbmPanel, padPanel;
        TabControl tabs;
        TabPage onlineTab;
        TabPage displayPage, graphicsPage, controlsPage, modsPage, cheatsPage;
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

        // The game exe setup placed in game\: paths.txt names it (EXE=jsp.exe on the current tree, EXE=JStarsRecomp.exe on the
        // legacy one; installer/tools/firstrun.py writes it). Without paths.txt: jsp.exe when that file is there.
        static string InstalledExeName(string root)
        {
            try
            {
                foreach (var line in File.ReadAllLines(Path.Combine(root, "paths.txt")))
                {
                    if (!line.StartsWith("EXE=")) continue;
                    var n = line.Substring(4).Trim();
                    if (n.Length > 0 && n.IndexOfAny(new[] { '\\', '/', ':' }) < 0) return n;
                }
            }
            catch { }
            return File.Exists(Path.Combine(root, "game", "jsp.exe")) ? "jsp.exe" : "JStarsRecomp.exe";
        }

        public LauncherForm()
        {
            root = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
            installed = File.Exists(Path.Combine(root, "jsp-launcher.exe")) &&
                        (File.Exists(Path.Combine(root, "paths.txt")) || File.Exists(Path.Combine(root, "jsp-install.ini")));
            if (installed)
            {
                here = root;
                iso = elf = boostDir = "";
                exe = Path.Combine(root, "game", InstalledExeName(root));   // jsp.exe on the current tree, JStarsRecomp.exe on the legacy one
                runDir = Path.Combine(root, @"game\run");
                launcherExe = Path.Combine(root, "jsp-launcher.exe");
                settingsFile = InstalledSettingsFile(root);
            }
            else
            {
                if (!Directory.Exists(Path.Combine(root, "jstars")) && Directory.Exists(Path.Combine(Path.GetDirectoryName(root), "jstars")))
                    root = Path.GetDirectoryName(root);   // run from the launcher folder itself
                here = Path.Combine(root, "jstars");
                iso = Path.Combine(root, @"RPCS3\games\J-Stars Victory Vs+.iso");
                exe = Path.Combine(here, @"port\build\JStarsRecomp.exe");
                elf = Path.Combine(here, "EBOOT.elf");
                settingsFile = Path.Combine(here, "play_settings.txt");
                runDir = Path.Combine(here, "run");
                boostDir = Path.Combine(root, @"tools\boost");
                launcherExe = "";
            }

            gameProc = Path.GetFileNameWithoutExtension(exe);
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
            LoadMods();
            loading = false;
            RefreshKeyBoxes();
            RefreshPadBoxes();
            CheckGameReadsKeys();
            FormClosing += delegate(object sender, FormClosingEventArgs e) { if (modsZipBusy) { e.Cancel = true; MessageBox.Show(this, "Please wait for the mod installation to finish.", Text); } };
            Application.AddMessageFilter(this);   // mouse clicks become bindings on the Keyboard & Mouse page
        }

        // The file jsp-launcher.exe reads (installer/launcher/launcher.c, same order): the install folder's play_settings.txt
        // when settings_here.txt marks it, when it already exists or when the install is portable; else the per-user data folder.
        static string InstalledSettingsFile(string dir)
        {
            string local = Path.Combine(dir, "play_settings.txt");
            bool portable = false;
            try
            {
                string ini = Path.Combine(dir, "jsp-install.ini");
                if (File.Exists(ini))
                    foreach (string line in File.ReadAllLines(ini))
                        if (line.Replace(" ", "").ToLowerInvariant() == "portable=1") portable = true;
            }
            catch { }
            if (portable || File.Exists(local) || File.Exists(Path.Combine(dir, "settings_here.txt"))) return local;
            string data = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "JSP Fan Revival");
            try { Directory.CreateDirectory(data); } catch { }
            return Path.Combine(data, "play_settings.txt");
        }

        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            if (ShotDir == null) return;
            loading = true;   // switching pages below must not save anything
            int mode = modeBox.SelectedIndex;
            Directory.CreateDirectory(ShotDir);
            Shot("display", displayPage, -1);
            Shot("graphics", graphicsPage, -1);
            Shot("controls_keyboard", controlsPage, 0);
            Shot("controls_kbm", controlsPage, 1);
            Shot("controls_controller", controlsPage, 2);
            Shot("online", tabs.TabPages.IndexOf(onlineTab), -1);
            Shot("mods", modsPage, -1);
            Shot("cheats", cheatsPage, -1);
            modeBox.SelectedIndex = mode;
            Close();
        }

        void Shot(string name, TabPage page, int mode)
        {
            Shot(name, tabs.TabPages.IndexOf(page), mode);
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
            ClientSize = Z(640, 748);
            FormBorderStyle = FormBorderStyle.FixedSingle;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            KeyPreview = true;
            try { if (File.Exists(exe)) Icon = Icon.ExtractAssociatedIcon(exe); } catch { }

            // Header
            var header = new Panel { BackColor = HeaderBack, Location = P(0, 0), Size = Z(640, 64) };
            var title = new Label {
                Text = "J-STARS VICTORY VS+", ForeColor = Color.White, AutoSize = true,
                Font = new Font("Segoe UI", 18F, FontStyle.Bold), Location = P(16, 8) };
            var sub = new Label {
                Text = "PC port launcher", ForeColor = Color.FromArgb(255, 170, 120), AutoSize = true,
                Font = new Font("Segoe UI", 9F), Location = P(19, 41) };
            header.Controls.Add(title); header.Controls.Add(sub);
            // The project mark (the ASCII star with the J, drawn by installer/tools/installer_art.py into jsp-launcher.ico,
            // which setup puts next to this exe) at the header's right end; nothing is shown when the file is not there.
            try {
                string ico = Path.Combine(Path.GetDirectoryName(Application.ExecutablePath), "jsp-launcher.ico");
                if (File.Exists(ico))
                    using (var mark = new Icon(ico, S(48), S(48)))
                        header.Controls.Add(new PictureBox { Image = mark.ToBitmap(), SizeMode = PictureBoxSizeMode.Zoom,
                            BackColor = HeaderBack, Location = P(640 - 64, 8), Size = Z(48, 48) });
            } catch { }
            Controls.Add(header);

            tabs = new TabControl { Location = P(12, 76), Size = Z(616, 604) };
            // Match the in-game hub order; capture pages by identity instead of position.
            tabs.TabPages.Add(displayPage = BuildDisplayTab());
            tabs.TabPages.Add(graphicsPage = BuildGraphicsTab());
            tabs.TabPages.Add(controlsPage = BuildControlsTab());
            tabs.TabPages.Add(BuildOnlineTab());
            tabs.TabPages.Add(modsPage = BuildModsTab());
            tabs.TabPages.Add(cheatsPage = BuildCheatsTab());
            Controls.Add(tabs);

            // Bottom bar
            statusLabel = new Label { Location = P(14, 700), Size = Z(340, 36), Text = "Ready.", TextAlign = ContentAlignment.MiddleLeft };
            startBtn = new Button {
                Text = "Start Game", Location = P(366, 696), Size = Z(150, 40),
                BackColor = Accent, ForeColor = Color.White, FlatStyle = FlatStyle.Flat,
                Font = new Font("Segoe UI", 11F, FontStyle.Bold) };
            startBtn.FlatAppearance.BorderSize = 0;
            startBtn.Click += delegate { StartGame(); };
            exitBtn = new Button { Text = "Exit", Location = P(526, 696), Size = Z(100, 40), Font = new Font("Segoe UI", 10F) };
            exitBtn.Click += delegate { Close(); };
            Controls.Add(statusLabel); Controls.Add(startBtn); Controls.Add(exitBtn);
            AcceptButton = null;   // Enter must stay bindable on the Controls page

            ResumeLayout(false);
        }

        TabPage BuildDisplayTab()
        {
            var p = new TabPage("Display") { Padding = new Padding(12) };
            int y = 16;

            p.Controls.Add(new Label { Text = "Render resolution", Location = P(16, y), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
            y += 22;
            resBox = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Location = P(16, y), Width = S(360), Font = new Font("Consolas", 9.5F) };
            resBox.Items.AddRange(ResNames);
            resBox.SelectedIndexChanged += delegate { SaveIfReady(); };
            p.Controls.Add(resBox);
            y += 28;
            p.Controls.Add(new Label {
                Text = "The size the game draws at. The game can only scale 720p by whole steps,\r\nso these are all the sizes it can render. Higher looks sharper and needs more GPU.",
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

            // TITLE_STATS for the game: FPS, draw count and size after the window title (off in an install, on in the hand-built tree).
            statsBox = new CheckBox { Text = "Show FPS in the window title", Location = P(16, y), AutoSize = true, Checked = !installed };
            statsBox.CheckedChanged += delegate { SaveIfReady(); };
            p.Controls.Add(statsBox);
            // FPS_COUNTER for the game: frames per second, small, in the bottom-left corner. Same row, so the window keeps its size.
            fpsBox = new CheckBox { Text = "FPS counter (bottom left)", Location = P(300, y), AutoSize = true, Checked = false };
            fpsBox.CheckedChanged += delegate { SaveIfReady(); };
            p.Controls.Add(fpsBox);
            y += 32;

            // PIPE for the game: the drawing work runs on a second thread (toolkit patches 2912, 7907). On by default.
            pipeBox = new CheckBox { Text = "Two-thread drawing", Location = P(16, y), AutoSize = true, Checked = true };
            pipeBox.CheckedChanged += delegate { SaveIfReady(); };
            p.Controls.Add(pipeBox);
            p.Controls.Add(new Label {
                Text = "Uses one more CPU core; helps in busy fights. Off = one drawing thread.",
                Location = P(170, y + 2), AutoSize = true, ForeColor = SystemColors.GrayText });
            y += 30;

            // MENU_PAGES for the game: the two-page Main Menu, which also brings 3v3 Free Battle. On by default; unticked writes MENU_PAGES=0.
            menuBox = new CheckBox { Text = "Two-page Main Menu and 3v3", Location = P(16, y), AutoSize = true, Checked = true };
            menuBox.CheckedChanged += delegate { SaveIfReady(); };
            menuBox.CheckedChanged += delegate { if (onlineState != null) ShowOnlineState(); };   // FIX: the Online tab's menu path follows it
            p.Controls.Add(menuBox);
            p.Controls.Add(new Label {
                Text = "Off = the game's own Main Menu, and no 3v3.",
                Location = P(250, y + 2), AutoSize = true, ForeColor = SystemColors.GrayText });
            y += 30;

            boostOffBox = new CheckBox { Text = "Turn CPU boost off again when the game closes (keeps the PC cool)", Location = P(16, y), AutoSize = true, Checked = true };
            boostOffBox.CheckedChanged += delegate { SaveIfReady(); };
            if (!installed)   // the CPU boost option is for the hand-built tree only
            {
                p.Controls.Add(boostOffBox);
                y += 24;
                p.Controls.Add(new Label {
                    Text = "Boost is always turned on while the game runs.",
                    Location = P(34, y), AutoSize = true, ForeColor = SystemColors.GrayText });
                y += 36;
            }

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
            return p;
        }

        // ENHANCEMENT (opt-in, off by default): "Unlock everything". JSP_UNLOCK_ALL=1 makes the game edit its loaded save
        // (see installer\port\jsp\jsp_unlock.cpp). The game writes the unlocks into the save itself and cannot undo them,
        // so Start Game first copies the save folder, refuses to start if that fails, and the box unticks after one start.
        TabPage BuildCheatsTab()
        {
            var p = new TabPage("Cheats") { Padding = new Padding(12) };
            int y = 16;
            unlockBox = new CheckBox { Text = "Unlock everything (cheat)", Location = P(16, y), AutoSize = true, Checked = false };
            p.Controls.Add(unlockBox);
            y += 28;
            p.Controls.Add(new Label {
                Text = "Next start only: backs up your save first; cannot be undone except by restoring the backup.",
                Location = P(34, y), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
            y += 34;
            p.Controls.Add(new Label {
                Text = "All cards, J-Points, characters, supports, titles, icons, decks and coins are unlocked in the save\r\n" +
                       "the game has loaded, and the game writes them out itself at its next save. The backup is the save folder\r\n" +
                       "(" + string.Join(" or ", SaveNames) + ") copied beside it as BACKUP-<folder>-<date>-<time>.\r\n" +
                       "Unlocked titles and icons are visible to online opponents; accepted online for now, which may change\r\n" +
                       "when online play is ready. To undo it, close the game and put the backup folder back under the\r\n" +
                       "original name.",
                Location = P(34, y), AutoSize = true, ForeColor = SystemColors.GrayText });
            return p;
        }

        // The save folders this port may use (the game's data folder is <hdd0>_gamedata; the plain hdd0 layout is checked too).
        string[] SaveFolders()
        {
            string hdd0 = Hdd0Dir();
            var l = new System.Collections.Generic.List<string>();
            foreach (string n in SaveNames)
            {
                l.Add(Path.Combine(hdd0 + "_gamedata", @"dev_hdd0\home\00000001\savedata\" + n));
                l.Add(Path.Combine(hdd0, @"home\00000001\savedata\" + n));
            }
            return l.ToArray();
        }

        // True when the game has run here before: it wrote its own data, the <hdd0>_gamedata folder (created by the game's runtime,
        // not by this launcher) or its first-boot data install <hdd0>\game\<id>-INSTALL.
        bool GameRanBefore()
        {
            string hdd0 = Hdd0Dir();
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

        // The game's hdd0 folder: PS3_HDD0_ROOT of paths.txt in an installed tree (@APP@ = the install folder, as in
        // installer/launcher/launcher.c), else the per-user data folder; <tree>\jstars\hdd0 in the hand-built tree.
        string Hdd0Dir()
        {
            if (!installed) return Path.Combine(here, "hdd0");
            try
            {
                string f = Path.Combine(root, "paths.txt");
                if (File.Exists(f))
                    foreach (string line in File.ReadAllLines(f))
                    {
                        if (!line.StartsWith("PS3_HDD0_ROOT=")) continue;
                        string v = line.Substring(14).Trim();
                        if (v.StartsWith("@APP@")) v = root + v.Substring(5);
                        if (v.Length > 0) return v.Replace('/', '\\').TrimEnd('\\');
                    }
            }
            catch { }
            return Path.Combine(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "JSP Fan Revival"), "hdd0");
        }

        // Graphics: shadow quality and distance, model detail, object draw distance. The game reads the four keys from its
        // environment or through the settings file (SHADOW_SCALE in the toolkit's D3D12 backend, the others in
        // installer/port/jsp/jsp_game.cpp); installer/port/jsp/jsp_settings.h lists the same keys and values.
        TabPage BuildGraphicsTab()
        {
            var p = new TabPage("Graphics") { Padding = new Padding(12), AutoScroll = true };
            int y = 16;
            p.Controls.Add(new Label { Text = "Shadows", Location = P(16, y), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
            y += 22;
            shadowBox = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Location = P(16, y), Width = S(360) };
            shadowBox.Items.AddRange(ShadowNames);
            shadowBox.SelectedIndex = 1;
            shadowBox.SelectedIndexChanged += delegate { SaveIfReady(); };
            p.Controls.Add(shadowBox);
            y += 28;
            p.Controls.Add(new Label {
                Text = "Sharper, steadier shadows on characters and stages. Higher costs a little GPU.",
                Location = P(16, y), AutoSize = true, ForeColor = SystemColors.GrayText });
            y += 30;
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
            p.Controls.Add(new Label { Text = "Model detail", Location = P(16, y), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
            y += 22;
            modelDetailBox = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Location = P(16, y), Width = S(360) };
            modelDetailBox.Items.AddRange(new object[] { "Normal   (the game's own)", "High     (full detail 2x further)", "Ultra    (full detail 4x further)" });
            modelDetailBox.SelectedIndex = 0;
            modelDetailBox.SelectedIndexChanged += delegate { SaveIfReady(); };
            p.Controls.Add(modelDetailBox);
            y += 28;
            p.Controls.Add(new Label {
                Text = "Characters and stage models keep their detailed version further from the camera.",
                Location = P(16, y), AutoSize = true, ForeColor = SystemColors.GrayText });
            y += 30;
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
            return p;
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
        RadioButton rbOnOff, rbOnRepsn, rbOnRb, rbOnComm, rbOnNative;
        ComboBox rbRoleBox;   // Auto (recommended) / Host / Join: the role is only an advanced override
        CheckBox rbRelayBox, rbDirectBox;
        TextBox rbPortBox, rbJoinBox, rbCodeBox;
        Panel rbPanel;
        TextBox repsnServerBox, repsnNameBox, presetLabelBox, presetAddrBox, presetNameBox;
        ListBox presetList;
        Button lanBtn, presetAddBtn, presetUpdBtn, presetDelBtn, presetActiveBtn, onlineSaveBtn;
        Label onlineMsg, onlineWhere, onlineState;
        Panel repsnPanel, commPanel;
        // ENHANCEMENT (optional Online Battle with rollback): the game's own Online Battle > Friendly Match over re-psn, and our rollback runs the
        // fight (the game pairs the two players by their room; jsp_net_online.h NC_NATIVE). The two boxes are play_settings.txt lines that jsp-launcher.exe
        // turns into environment variables: JSP_ROLLBACK (1 = the lobby start, no role key) and JSP_ROLLBACK_RELAY. They must be in the environment when the
        // game starts (its thread registry reads JSP_ROLLBACK before any game code runs), so they are settings lines and not a default inside the game.
        CheckBox relayBox;
        static readonly string[] RollbackRoleKeys = { "JSP_ROLLBACK_LISTEN", "JSP_ROLLBACK_JOIN", "JSP_ROLLBACK_VIA", "JSP_ROLLBACK_BENCH" };
        readonly List<Preset> presets = new List<Preset>();
        readonly List<string> loadedLabels = new List<string>();
        string activeLabel = "";
        bool onlineDirty;

        string RepsnPath() { return Path.Combine(OnlineIni.SharedDir(root), "repsn.ini"); }
        string CommunityPath() { return Path.Combine(OnlineIni.SharedDir(root), "community.ini"); }

        ModsManager modsManager;
        CheckedListBox modsList;
        Label modsMessage;
        Button modsSave;
        bool modsDirty, modsLoading, modsZipBusy;
        TabPage BuildModsTab()
        {
            var p = new TabPage("Mods") { Padding = new Padding(12) };
            p.Controls.Add(new Label { Text = "Tick mods to enable them. Later mods take priority. Changes apply on the next start.", Location = P(12, 12), Size = Z(560, 42) });
            modsList = new CheckedListBox { Location = P(12, 60), Size = Z(560, 330), CheckOnClick = true };
            modsList.ItemCheck += delegate(object sender, ItemCheckEventArgs e) { if (!modsLoading && modsManager != null) { modsManager.Entries[e.Index].Enabled = e.NewValue == CheckState.Checked; modsDirty = true; } };
            p.Controls.Add(modsList);
            var up = new Button { Text = "Move up", Location = P(12, 404), Size = Z(95, 30) };
            var down = new Button { Text = "Move down", Location = P(114, 404), Size = Z(95, 30) };
            up.Click += delegate { MoveMod(-1); }; down.Click += delegate { MoveMod(1); };
            modsSave = new Button { Text = "Save mod choices", Location = P(216, 404), Size = Z(145, 30) };
            modsSave.Click += delegate { SaveMods(); };
            var refresh = new Button { Text = "Refresh", Location = P(368, 404), Size = Z(90, 30) };
            refresh.Click += delegate { if (modsZipBusy) return; if (!modsDirty || MessageBox.Show(this, "Discard unsaved mod choices?", Text, MessageBoxButtons.YesNo) == DialogResult.Yes) LoadMods(); };
            var folder = new Button { Text = "Open mods folder", Location = P(12, 444), Size = Z(160, 30) };
            folder.Click += delegate { if (modsManager == null) return; try { Directory.CreateDirectory(modsManager.DirectoryPath); Process.Start(new ProcessStartInfo(modsManager.DirectoryPath) { UseShellExecute = true }); } catch (Exception e) { modsMessage.Text = e.Message; } };
            var install = new Button { Text = "Install ZIP...", Location = P(184, 444), Size = Z(145, 30) };
            install.Click += delegate {
                if (modsZipBusy || !SaveModsIfDirty() || modsManager == null) return;
                using (var choose = new OpenFileDialog { Filter = "Mod ZIP files (*.zip)|*.zip", CheckFileExists = true, Title = "Install a mod ZIP" }) {
                    if (choose.ShowDialog(this) != DialogResult.OK) return;
                    string archive = choose.FileName; ModsManager manager = modsManager;
                    try { manager.VerifyPaths(); } catch (Exception e) { modsMessage.Text = e.Message; return; }
                    modsZipBusy = true; install.Enabled = modsSave.Enabled = modsList.Enabled = false;
                    modsMessage.Text = "Installing the package. Existing mods will not be replaced.";
                    var job = new Thread(delegate() {
                        string message; bool success = false;
                        try { message = manager.InstallZip(root, archive); success = true; } catch (Exception e) { message = e.Message; }
                        try { BeginInvoke((MethodInvoker)delegate {
                            modsZipBusy = false; install.Enabled = modsList.Enabled = true; modsSave.Enabled = modsManager != null;
                            if (success) { LoadMods(); modsMessage.Text = message + ". Changes apply on the next start." + (modsManager != null && modsManager.GlobalOff ? " All mods are disabled in advanced settings." : ""); }
                            else modsMessage.Text = "Mod was not installed: " + message;
                        }); } catch (InvalidOperationException) { }
                    });
                    job.IsBackground = true; job.Start();
                }
            };
            p.Controls.Add(install);
            p.Controls.Add(up); p.Controls.Add(down); p.Controls.Add(modsSave); p.Controls.Add(refresh); p.Controls.Add(folder);
            modsMessage = new Label { Location = P(12, 485), Size = Z(560, 78) };
            p.Controls.Add(modsMessage); return p;
        }
        void ShowMods(int selected)
        {
            modsLoading = true; modsList.Items.Clear();
            if (modsManager != null) foreach (ModsManager.Entry e in modsManager.Entries) modsList.Items.Add(e, e.Enabled);
            if (selected >= 0 && selected < modsList.Items.Count) modsList.SelectedIndex = selected;
            modsLoading = false;
        }
        void LoadMods()
        {
            modsDirty = false; modsManager = null;
            try { modsManager = ModsManager.Load(here, settingsFile, runDir, installed, Environment.GetEnvironmentVariable); modsMessage.Text = "Folder: " + modsManager.DirectoryPath + "\nList: " + modsManager.ListPath + (modsManager.GlobalOff ? "\nAll mods are disabled in advanced settings." : ""); }
            catch (Exception e) { modsMessage.Text = "Mods could not be read: " + e.Message; }
            modsSave.Enabled = modsManager != null; ShowMods(-1);
        }
        void MoveMod(int offset)
        {
            if (modsZipBusy || modsManager == null) return; int from = modsList.SelectedIndex, to = from + offset;
            if (from < 0 || to < 0 || to >= modsManager.Entries.Count) return;
            modsManager.Move(from, to); modsDirty = true; ShowMods(to);
        }
        bool SaveMods()
        {
            if (modsZipBusy || modsManager == null) return false;
            try { modsManager.Save(); modsDirty = false; modsMessage.Text = "Mod choices saved. They apply on the next start."; return true; }
            catch (Exception e) { modsMessage.Text = "Mod choices were not saved: " + e.Message; return false; }
        }
        bool SaveModsIfDirty()
        {
            if (modsZipBusy) { MessageBox.Show(this, "Please wait for the mod installation to finish.", Text); return false; }
            if (!modsDirty) { LoadMods(); return true; }
            DialogResult r = MessageBox.Show(this, "Save mod choices before starting?", Text, MessageBoxButtons.YesNoCancel);
            if (r == DialogResult.Cancel) return false;
            if (r == DialogResult.Yes) return SaveMods();
            LoadMods(); return true;
        }

        TabPage BuildOnlineTab()
        {
            var p = onlineTab = new TabPage("Online") { Padding = new Padding(12), AutoScroll = true };
            int y = 16;
            p.Controls.Add(new Label { Text = "Online play", Location = P(16, y), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
            y += 24;
            rbOnOff = new RadioButton { Text = "Off", Location = P(16, y), AutoSize = true, Checked = true };
            rbOnRepsn = new RadioButton { Text = "re-psn (standard)", Location = P(90, y), AutoSize = true };
            rbOnRb = new RadioButton { Text = "re-psn (rollback)", Location = P(250, y), AutoSize = true };
            rbOnComm = new RadioButton { Text = "Community", Location = P(420, y), AutoSize = true };
            rbOnNative = new RadioButton { Text = "Friendly Match + rollback", Location = P(535, y), AutoSize = true };
            EventHandler pick = delegate { OnlineChanged(); };
            rbOnOff.CheckedChanged += pick; rbOnRepsn.CheckedChanged += pick; rbOnRb.CheckedChanged += pick; rbOnComm.CheckedChanged += pick; rbOnNative.CheckedChanged += pick;
            p.Controls.Add(rbOnOff); p.Controls.Add(rbOnRepsn); p.Controls.Add(rbOnRb); p.Controls.Add(rbOnComm); p.Controls.Add(rbOnNative);
            y += 26;
            p.Controls.Add(new Label {
                Text = "One choice: saving it turns everything else off. Rollback is experimental.",
                Location = P(16, y), AutoSize = true, ForeColor = SystemColors.GrayText });
            y += 24;
            onlineState = new Label { Location = P(16, y), Size = Z(700, 36), Font = new Font("Segoe UI", 9F, FontStyle.Bold) };
            p.Controls.Add(onlineState);
            y += 44;

            repsnPanel = new Panel { Location = P(0, y), Size = Z(730, 150) };
            repsnPanel.Controls.Add(new Label { Text = "re-psn server (standard and rollback)", Location = P(16, 0), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
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
            relayBox = new CheckBox { Text = "If the two PCs cannot connect directly, pass the match through the server (relay)", Location = P(16, 122), AutoSize = true };
            relayBox.CheckedChanged += delegate { OnlineChanged(); };
            repsnPanel.Controls.Add(relayBox);
            p.Controls.Add(repsnPanel);
            y += 158;

            rbPanel = new Panel { Location = P(0, y), Size = Z(730, 140) };
            rbPanel.Controls.Add(new Label { Text = "re-psn (rollback)", Location = P(16, 0), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
            rbPanel.Controls.Add(new Label { Text = "Match code", Location = P(16, 28), Size = Z(110, 20) });
            rbCodeBox = new TextBox { Location = P(130, 25), Width = S(110), MaxLength = 8, CharacterCasing = CharacterCasing.Upper };
            rbCodeBox.TextChanged += delegate { OnlineChanged(); };
            rbPanel.Controls.Add(rbCodeBox);
            rbPanel.Controls.Add(new Label { Text = "optional, 4 to 8 letters or digits: only players with the same code meet", Location = P(250, 28), AutoSize = true, ForeColor = SystemColors.GrayText });
            rbRelayBox = new CheckBox { Text = "Allow the server's relay when a direct path fails", Location = P(130, 52), AutoSize = true };
            rbRelayBox.CheckedChanged += delegate { OnlineChanged(); };
            rbPanel.Controls.Add(rbRelayBox);
            rbDirectBox = new CheckBox { Text = "Advanced: connect by IP address instead of through re-psn", Location = P(16, 80), AutoSize = true };
            rbDirectBox.CheckedChanged += delegate { OnlineChanged(); };
            rbPanel.Controls.Add(rbDirectBox);
            rbPanel.Controls.Add(new Label { Text = "Role", Location = P(470, 82), Size = Z(34, 20) });
            rbRoleBox = new ComboBox { Location = P(506, 78), Width = S(150), DropDownStyle = ComboBoxStyle.DropDownList };
            rbRoleBox.Items.AddRange(new object[] { "Auto (recommended)", "Host", "Join" });
            rbRoleBox.SelectedIndex = 0;
            rbRoleBox.SelectedIndexChanged += delegate { OnlineChanged(); };
            rbPanel.Controls.Add(rbRoleBox);
            rbPanel.Controls.Add(new Label { Text = "Host port", Location = P(36, 108), Size = Z(70, 20) });
            rbPortBox = new TextBox { Location = P(110, 105), Width = S(70), MaxLength = 5 };
            rbPortBox.TextChanged += delegate { OnlineChanged(); };
            rbPanel.Controls.Add(rbPortBox);
            rbPanel.Controls.Add(new Label { Text = "Host address", Location = P(210, 108), Size = Z(80, 20) });
            rbJoinBox = new TextBox { Location = P(294, 105), Width = S(160), MaxLength = 21 };
            rbJoinBox.TextChanged += delegate { OnlineChanged(); };
            rbPanel.Controls.Add(rbJoinBox);
            rbPanel.Controls.Add(new Label { Text = "IP:port, for a join (advanced; with an IP address the role must be Host or Join)", Location = P(464, 108), AutoSize = true, ForeColor = SystemColors.GrayText });
            p.Controls.Add(rbPanel);
            y += 150;

            commPanel = new Panel { Location = P(0, y), Size = Z(730, 300) };
            commPanel.Controls.Add(new Label { Text = "Community", Location = P(16, 0), AutoSize = true, Font = new Font("Segoe UI", 9F, FontStyle.Bold) });
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

        // The one choice: 0 Off, 1 re-psn (standard), 2 re-psn (rollback), 3 Community. (The Advanced box turns 2 into a direct-IP rollback.)
        int OnlineMethod() { return rbOnRepsn.Checked ? 1 : rbOnRb.Checked ? 2 : rbOnComm.Checked ? 3 : rbOnNative.Checked ? 4 : 0; }

        void OnlineChanged()
        {
            commPanel.Enabled = rbOnComm.Checked;
            relayBox.Enabled = rbOnNative.Checked;
            rbPanel.Enabled = rbOnRb.Checked;
            rbPortBox.Enabled = rbDirectBox.Checked && rbRoleBox.SelectedIndex == 1;
            rbJoinBox.Enabled = rbDirectBox.Checked && rbRoleBox.SelectedIndex == 2;
            rbRelayBox.Enabled = !rbDirectBox.Checked;
            rbCodeBox.Enabled = !rbDirectBox.Checked;
            rbOnRb.Text = rbDirectBox.Checked ? "Rollback (direct IP)" : "re-psn (rollback)";
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

        // The last value of a rollback key in play_settings.txt, read the way jsp-launcher.exe (launcher.c load_env) and the
        // game do: a line is "KEY=" at column 0 (# and ; lines are comments; letter case is ignored, like Windows
        // environment names), the last such line wins and only trailing space, CR and LF are trimmed. null when absent.
        // Read at the time of the call because the in-game Port Settings page writes the same keys.
        string RbValue(string key)
        {
            string v = null;
            try
            {
                if (!File.Exists(settingsFile)) return null;
                foreach (string line in File.ReadAllLines(settingsFile))
                    if (line.StartsWith(key + "=", StringComparison.OrdinalIgnoreCase)) v = line.Substring(key.Length + 1).TrimEnd(' ', '\r', '\n');
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
            return v;
        }

        // True when the value turns rollback on: not empty and its first character is not '0' (the game's test).
        static bool RbFlag(string v) { return v != null && v.Length != 0 && v[0] != '0'; }
        bool RollbackOn() { return RbFlag(RbValue("JSP_ROLLBACK")); }

        // Sets (value) or removes (null) one rollback key in a list of lines: the column-0 lines of that key are dropped and the new line
        // goes where the first one was, else at the end. Every other line is left as it is.
        static void RbSet(List<string> lines, string key, string value)
        {
            int at = -1;
            for (int i = lines.Count - 1; i >= 0; i--)
                if (lines[i].StartsWith(key + "=", StringComparison.OrdinalIgnoreCase)) { lines.RemoveAt(i); at = i; }
            if (value == null) return;
            if (at < 0) lines.Add(key + "=" + value); else lines.Insert(at, key + "=" + value);
        }

        // The rollback keys for the one choice (same keys and values the in-game Online page writes, jsp_net_online.h Rollback::store).
        // enable false: write JSP_ROLLBACK=0 explicitly, including when an inherited environment value enabled rollback.
        static void RbApply(List<string> lines, bool enable, bool direct, bool auto, bool host, string port, string join, bool relay, string code)
        {
            if (!enable)
            {
                RbSet(lines, "JSP_ROLLBACK", "0");
                return;
            }
            RbSet(lines, "JSP_ROLLBACK", "1"); RbSet(lines, "JSP_ROLLBACK_BENCH", null);
            if (!direct)
            {
                RbSet(lines, "JSP_ROLLBACK_VIA", "repsn"); RbSet(lines, "JSP_ROLLBACK_ROLE", auto ? "auto" : host ? "host" : "join"); RbSet(lines, "JSP_ROLLBACK_RELAY", relay ? "1" : "0");
                RbSet(lines, "JSP_ROLLBACK_CODE", code.Length == 0 ? null : code);
                RbSet(lines, "JSP_ROLLBACK_LISTEN", null); RbSet(lines, "JSP_ROLLBACK_JOIN", null);
                return;
            }
            RbSet(lines, "JSP_ROLLBACK_VIA", null); RbSet(lines, "JSP_ROLLBACK_ROLE", null); RbSet(lines, "JSP_ROLLBACK_RELAY", null); RbSet(lines, "JSP_ROLLBACK_CODE", null);
            if (host) { RbSet(lines, "JSP_ROLLBACK_LISTEN", port); RbSet(lines, "JSP_ROLLBACK_JOIN", null); }
            else { RbSet(lines, "JSP_ROLLBACK_JOIN", join); RbSet(lines, "JSP_ROLLBACK_LISTEN", null); }
        }

        // Writes the rollback keys into play_settings.txt (read again just before, so the in-game page's changes stay) and keeps otherLines,
        // which SaveSettings writes back, the same. Returns null when written, else the reason.
        static void RbNative(List<string> lines, bool relay)
        {
            foreach (string key in RollbackRoleKeys) RbSet(lines, key, null);
            RbSet(lines, "JSP_ROLLBACK_ROLE", null);
            RbSet(lines, "JSP_ROLLBACK_CODE", null);
            RbSet(lines, "JSP_ROLLBACK", "1");
            RbSet(lines, "JSP_ROLLBACK_RELAY", relay ? "1" : "0");
        }

        string WriteRollback(bool enable, bool direct, bool auto, bool host, string port, string join, bool relay, string code, bool native = false)
        {
            var lines = new List<string>();
            try { if (File.Exists(settingsFile)) lines.AddRange(File.ReadAllLines(settingsFile)); }
            catch (Exception ex) { return "Could not read the settings file: " + ex.Message; }
            if (native) RbNative(lines, relay);
            else RbApply(lines, enable, direct, auto, host, port, join, relay, code);
            try { File.WriteAllLines(settingsFile, lines.ToArray(), Encoding.ASCII); }
            catch (Exception ex) { return "Could not save the settings file: " + ex.Message; }
            otherLines.RemoveAll(delegate (string l) { return l.StartsWith("JSP_ROLLBACK", StringComparison.OrdinalIgnoreCase); });
            foreach (string l in lines)
            {
                string t = l.Trim(); int eq = t.IndexOf('=');
                if (eq > 0 && t.StartsWith("JSP_ROLLBACK", StringComparison.OrdinalIgnoreCase) && !t.StartsWith("#")) otherLines.Add(t.Substring(0, eq).Trim() + "=" + t.Substring(eq + 1).Trim());
            }
            return null;
        }

        // Same rule as the game's valid_port / valid_join (jsp_net_online.h): 1 to 65535, and a dotted IPv4 address with the port.
        // The match code: empty, or 4 to 8 of A-Z 0-9 (jsp_net_online.h normalize_code); stored upper case.
        static bool ValidRbCode(string c)
        {
            if (c.Length == 0) return true;
            if (c.Length < 4 || c.Length > 8) return false;
            foreach (char ch in c) if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))) return false;
            return true;
        }
        static bool ValidRbPort(string p)
        {
            int n;
            return p.Length >= 1 && p.Length <= 5 && int.TryParse(p, System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture, out n) && n >= 1 && n <= 65535;
        }
        static bool ValidRbJoin(string s)
        {
            int c = s.LastIndexOf(':');
            if (c < 0 || !ValidRbPort(s.Substring(c + 1))) return false;
            string[] o = s.Substring(0, c).Split('.');
            if (o.Length != 4) return false;
            foreach (string q in o)
            {
                int n;
                if (q.Length < 1 || q.Length > 3 || !int.TryParse(q, System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture, out n) || n > 255) return false;
            }
            return true;
        }

        // The last "KEY=" line of play_settings.txt, read like RollbackOn (null when absent). Read only.
        string SettingsValue(string key)
        {
            string v = null;
            try
            {
                if (!File.Exists(settingsFile)) return null;
                foreach (string line in File.ReadAllLines(settingsFile))
                    if (line.StartsWith(key + "=", StringComparison.OrdinalIgnoreCase)) v = line.Substring(key.Length + 1).TrimEnd(' ', '\r', '\n');
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
            return v;
        }

        // A rollback role key is set (a start-up session of Port Settings > Online > Rollback: direct IP, VIA=repsn, or the bench). Rollback WITH a role
        // key and the game's own online cannot run together (the game refuses both); rollback without one is the Online Battle + rollback start.
        bool RollbackRoleKey()
        {
            foreach (string k in RollbackRoleKeys) { string v = SettingsValue(k); if (!string.IsNullOrEmpty(v)) return true; }
            return false;
        }

        // The lines of play_settings.txt this window does not own (otherLines, written back by SaveSettings): one key set, every older line of it dropped.
        void OtherSet(string key, string value)
        {
            otherLines.RemoveAll(l => l.StartsWith(key + "=", StringComparison.OrdinalIgnoreCase));
            if (value != null) otherLines.Add(key + "=" + value);
        }

        // One line in plain words: what the next start does online (the boxes as they are now, saved or not).
        void ShowOnlineState()
        {
            int m = OnlineMethod();
            if (m == 4)
            {
                onlineState.ForeColor = Color.FromArgb(0, 110, 40);
                // FIX (two-page Main Menu): Online Battle sits on its Battle Modes page; the fights are 2 vs 2 (3 vs 3 is offline Free Battle only)
                bool pages = menuBox == null || menuBox.Checked;
                onlineState.Text = "Online Battle + rollback (2 vs 2). In the game: Main Menu > " + (pages ? "Battle Modes > " : "") +
                    "Online Battle > Friendly Match; one player picks Create a Room, the other Search for a Room.";
            }
            else if (m == 1) { onlineState.ForeColor = SystemColors.ControlText; onlineState.Text = "Online Battle with the game's own netcode (rollback off)."; }
            else if (m == 2) { onlineState.ForeColor = SystemColors.ControlText; onlineState.Text = "Custom rollback session."; }
            else if (m == 3) { onlineState.ForeColor = SystemColors.ControlText; onlineState.Text = "Community server: no client in this port yet, the game stays offline."; }
            else { onlineState.ForeColor = SystemColors.GrayText; onlineState.Text = "Online play is off. Choose Online server (re-psn) to play Online Battle."; }
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
            bool onR = RollbackOn();
            bool native = on1 && !on2 && onR && !RollbackRoleKey();
            // MIGRATION: more than one method on (a hand edit, or an older version that kept re-psn and rollback apart) shows as Off with a note;
            // saving writes the one choice, which turns the rest off. The game itself refuses such a start (jsp_net_game.cpp).
            if (on1 && on2) { note = "re-psn (standard) and Community were both on; shown as Off. Pick one and save."; on1 = on2 = onR = false; }
            else if ((on1 || on2) && onR && !native)
            {
                string rn = "Rollback and " + (on1 ? "re-psn (standard)" : "Community") + " were both on (the game refuses to start both); shown as Off. Pick one and save.";
                note = note == null ? rn : note + " " + rn;
                on1 = on2 = onR = false;
            }
            string via = RbValue("JSP_ROLLBACK_VIA"), role = RbValue("JSP_ROLLBACK_ROLE"), lis = RbValue("JSP_ROLLBACK_LISTEN"), joi = RbValue("JSP_ROLLBACK_JOIN");
            bool direct = !(via != null && string.Equals(via, "repsn", StringComparison.OrdinalIgnoreCase));
            bool hl = lis != null && lis.Length != 0, hj = joi != null && joi.Length != 0;
            bool host = hl || !hj;
            bool roleAuto = true;   // the role key absent or "auto": nobody picks, the games find each other
            if (!direct && role != null && (string.Equals(role, "host", StringComparison.OrdinalIgnoreCase) || string.Equals(role, "join", StringComparison.OrdinalIgnoreCase)))
            { roleAuto = false; host = string.Equals(role, "host", StringComparison.OrdinalIgnoreCase); }
            rbRoleBox.SelectedIndex = !direct && roleAuto ? 0 : host ? 1 : 2;   // with a direct IP connection the role is Host or Join
            string cv = RbValue("JSP_ROLLBACK_CODE");
            rbCodeBox.Text = (cv ?? "").ToUpperInvariant();
            rbDirectBox.Checked = onR && !native && direct;
            rbRelayBox.Checked = RbFlag(RbValue("JSP_ROLLBACK_RELAY"));
            rbPortBox.Text = hl ? lis : "36200";
            rbJoinBox.Text = hj ? joi : "";
            rbOnNative.Checked = native; rbOnRepsn.Checked = on1 && !native; rbOnRb.Checked = onR && !native; rbOnComm.Checked = on2; rbOnOff.Checked = !on1 && !on2 && !onR;
            repsnServerBox.Text = ri.Get("repsn", "server") ?? "";
            repsnNameBox.Text = ri.Get("repsn", "name") ?? "";
            string relv = SettingsValue("JSP_ROLLBACK_RELAY");
            relayBox.Checked = relv == null || (relv.Length != 0 && relv[0] != '0');
            relayBox.Enabled = rbOnNative.Checked;
            ShowOnlineState();
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
            repsnPanel.Enabled = true; commPanel.Enabled = rbOnComm.Checked; rbPanel.Enabled = rbOnRb.Checked;
            OnlineChanged();   // enabled states of the fields (loading is set: it only updates them)
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
            int m = OnlineMethod();   // 0 Off, 1 re-psn (standard), 2 re-psn (rollback), 3 Community, 4 Friendly Match + rollback
            bool native = m == 4, rollback = m == 2, direct = rollback && rbDirectBox.Checked, auto = rbRoleBox.SelectedIndex == 0, host = rbRoleBox.SelectedIndex == 1;
            string rcode = rbCodeBox.Text.Trim().ToUpperInvariant();
            string rport = rbPortBox.Text.Trim(), rjoin = rbJoinBox.Text.Trim();
            string server = repsnServerBox.Text.Trim(), rname = repsnNameBox.Text.Trim();
            if (m == 1 || native || (rollback && !direct))
            {
                if (server.Length == 0) return "Enter your re-psn server address to enable online play.";
                if (!OnlineIni.ValidAddress(server)) return "re-psn: the server is a host name or IP address, optionally :port (1 to 65535).";
                if (!OnlineIni.ValidName(rname)) return "re-psn: the online name is 3 to 16 of A-Z a-z 0-9 _ -, starting with a letter.";
            }
            if (rollback && !direct && !ValidRbCode(rcode)) return "Match code: 4 to 8 letters or digits (or leave it empty).";
            if (direct && auto) return "Rollback with an IP address needs a role: choose Host or Join next to the Advanced box.";
            if (direct && host && !ValidRbPort(rport)) return "Rollback host: the port is a number from 1 to 65535.";
            if (direct && !host && !ValidRbJoin(rjoin)) return "Rollback join: the host address is IP:port, for example 192.0.2.10:36200.";
            Preset act = null;
            foreach (Preset q in presets) if (string.Equals(q.Label, activeLabel, StringComparison.OrdinalIgnoreCase)) act = q;
            if (m == 3 && act == null) return "Community: pick a preset and press \"Use this one\" (add one first if the list is empty).";

            OnlineIni ri, ci;
            string e = OnlineIni.Load(RepsnPath(), out ri);
            if (e != null) return e;
            e = OnlineIni.Load(CommunityPath(), out ci);
            if (e != null) return e;

            bool writeR = ri.Exists || m == 1 || native || (rollback && !direct) || (OnlineIni.ValidAddress(server) && OnlineIni.ValidName(rname)), writeC = ci.Exists || m == 3 || presets.Count > 0;
            if (writeR)
            {
                ri.SetPreamble("; re-psn server settings. Written by the launcher; the game reads it.");
                ri.Set("repsn", "enabled", (m == 1 || native) ? "1" : "0");
                if (m == 1 || native || (rollback && !direct) || (OnlineIni.ValidAddress(server) && OnlineIni.ValidName(rname)))
                { ri.Set("repsn", "server", server); ri.Set("repsn", "name", rname); }
            }
            if (writeC)
            {
                ci.SetPreamble("; Community server settings. Features that exist only in this port do not work on a community server.");
                ci.Set("community", "enabled", m == 3 ? "1" : "0");
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
            // The method being switched off goes to disk first, so that a failed or interrupted save never leaves two on:
            // rollback on: both ini files (enabled=0) before JSP_ROLLBACK=1; anything else: JSP_ROLLBACK=0 first, then the ini files.
            string err = null;
            if (native)
            {
                if (writeC) err = ci.Save();
                if (err == null && writeR) err = ri.Save();
                if (err == null) err = WriteRollback(true, false, false, false, "", "", relayBox.Checked, "", true);
            }
            else if (rollback)
            {
                if (writeC) err = ci.Save();
                if (err == null && writeR) err = ri.Save();
                if (err == null) err = WriteRollback(true, direct, auto, host, rport, rjoin, rbRelayBox.Checked, rcode);
            }
            else
            {
                err = WriteRollback(false, false, auto, host, rport, rjoin, rbRelayBox.Checked, rcode);
                if (err == null)
                {
                    if (m == 1) { if (writeC) err = ci.Save(); if (err == null && writeR) err = ri.Save(); }
                    else { if (writeR) err = ri.Save(); if (err == null && writeC) err = ci.Save(); }
                }
            }
            if (err != null) { onlineMsg.ForeColor = Color.Firebrick; onlineMsg.Text = err; return err; }
            LoadOnline();
            onlineMsg.ForeColor = SystemColors.GrayText;
            onlineMsg.Text = native ? "Saved. Friendly Match with rollback is on." : m == 0 ? "Saved. Online is off." : m == 1 ? "Saved. re-psn (standard) is on." : m == 2 ? (direct ? "Saved. Rollback (direct IP) is on." : "Saved. re-psn (rollback) is on.") : "Saved. Community is on.";
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
                    if (k == "RES_SCALE" || k == "FULLSCREEN" || k == "WINDOW_SIZE" || k == "BOOST_OFF_ON_EXIT" || k == "UI_SCALE" || k == "AA_MODE" || k == "FX_FULL" || k == "TITLE_STATS" || k == "FPS_COUNTER" || k == "PIPE" || k == "PIPE_SEEN" || k == "MENU_PAGES" ||
                        k == "SHADOW_SCALE" || k == "SHADOW_RANGE" || k == "MODEL_DETAIL" || k == "OBJECT_DISTANCE" ||
                        k == "INPUT_MODE" || k == "MOUSE_SENS" || k.StartsWith("PAD_KEY_") || k.StartsWith("KB_KEY_") ||
                        k.StartsWith("KBM_KEY_") || k.StartsWith("PAD_BTN_"))
                        cur[k] = v;
                    else
                        if (k != "JSP_UNLOCK_ALL") otherLines.Add(k + "=" + v);   // kept as they are; the cheat is never taken from the file
                }
            }
            int scale; string s;
            if (!cur.TryGetValue("RES_SCALE", out s) || !int.TryParse(s, out scale)) scale = 1;
            resBox.SelectedIndex = Math.Max(1, Math.Min(4, scale)) - 1;
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
            statsBox.Checked = cur.TryGetValue("TITLE_STATS", out s) ? s == "1" : !installed;
            fpsBox.Checked = cur.TryGetValue("FPS_COUNTER", out s) && s == "1";
            // Two-thread drawing is on by default (toolkit patch 7907). A file written before that has PIPE=0 for everyone who never touched the box:
            // PIPE counts only when the same file carries PIPE_SEEN=2 (written by this window from now on), otherwise the box shows the default (on).
            pipeBox.Checked = !(cur.TryGetValue("PIPE_SEEN", out s) && s == "2" && cur.TryGetValue("PIPE", out s) && s != "1");
            // The two-page Main Menu is on unless the file says MENU_PAGES=0 (the same rule the game uses).
            menuBox.Checked = !(cur.TryGetValue("MENU_PAGES", out s) && s == "0");
            // Graphics tab: an unknown or missing value shows the default (Medium, Normal, Normal, Normal).
            int sh;
            if (!cur.TryGetValue("SHADOW_SCALE", out s) || !int.TryParse(s, out sh)) sh = 2;
            shadowBox.SelectedIndex = Math.Max(1, Math.Min(4, sh)) - 1;
            shadowRangeBox.SelectedIndex = cur.TryGetValue("SHADOW_RANGE", out s) && s.Trim() == "2" ? 1 : 0;
            int mdi = cur.TryGetValue("MODEL_DETAIL", out s) ? Array.IndexOf(ModelDetailIds, s.Trim()) : 0;
            modelDetailBox.SelectedIndex = mdi < 0 ? 0 : mdi;
            int odi = cur.TryGetValue("OBJECT_DISTANCE", out s) ? Array.IndexOf(ObjectDistIds, s.Trim()) : 1;
            objectDistBox.SelectedIndex = odi < 0 ? 1 : odi;
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
            lines.Add("# Written by the J-Stars launcher (also read by Play J-Stars.bat).");
            lines.Add("RES_SCALE=" + (resBox.SelectedIndex + 1));
            lines.Add("FULLSCREEN=" + (rbFull.Checked ? "1" : "0"));
            if (winBox.SelectedIndex > 0) lines.Add("WINDOW_SIZE=" + ((string)winBox.SelectedItem).Replace(" ", ""));
            if (!installed) lines.Add("BOOST_OFF_ON_EXIT=" + (boostOffBox.Checked ? "1" : "0"));
            lines.Add("UI_SCALE=" + (hudBar.Value / 20.0).ToString("0.00", System.Globalization.CultureInfo.InvariantCulture));
            lines.Add("AA_MODE=" + AaIds[aaBox.SelectedIndex]);
            lines.Add("FX_FULL=" + (fxFullBox.Checked ? "1" : "0"));
            lines.Add("TITLE_STATS=" + (statsBox.Checked ? "1" : "0"));
            lines.Add("FPS_COUNTER=" + (fpsBox.Checked ? "1" : "0"));
            lines.Add("PIPE_SEEN=2");   // marks PIPE below as a choice made with the on-by-default window
            lines.Add("PIPE=" + (pipeBox.Checked ? "1" : "0"));
            if (!menuBox.Checked) lines.Add("MENU_PAGES=0");   // on is the default: nothing is written for it
            lines.Add("SHADOW_SCALE=" + (shadowBox.SelectedIndex + 1));
            lines.Add("SHADOW_RANGE=" + (shadowRangeBox.SelectedIndex == 1 ? "2" : "1"));
            lines.Add("MODEL_DETAIL=" + ModelDetailIds[modelDetailBox.SelectedIndex]);
            lines.Add("OBJECT_DISTANCE=" + ObjectDistIds[objectDistBox.SelectedIndex]);
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

        // Unsaved Online tab edits are saved with the rest, or the start waits (shows the Online tab and the reason).
        bool SaveOnlineIfDirty()
        {
            if (!onlineDirty) return true;
            string oerr = SaveOnline();
            if (oerr == null) return true;
            tabs.SelectedTab = onlineTab;
            MessageBox.Show(this, oerr, Text);
            return false;
        }

        void StartGame()
        {
            if (gameRunning) return;
            if (!SaveModsIfDirty()) return;
            if (installed)   // jsp-launcher.exe does the rest (disc check, paths.txt, these settings, boost)
            {
                if (!File.Exists(exe)) { MessageBox.Show(this, "The game is not built yet. Run \"Build again\" from the Start menu.", Text); return; }
                if (Process.GetProcessesByName(gameProc).Length > 0) { MessageBox.Show(this, "The game is already running.", Text); return; }
                if (!SaveOnlineIfDirty()) return;
                SaveSettings();
                unlockThisRun = false;
                if (unlockBox.Checked)
                {
                    string why = BackupSave();
                    if (why != null) { Status("Not started: no save backup."); MessageBox.Show(this, why + "\n\nThe game was not started.", Text); return; }
                    unlockThisRun = true;
                    unlockBox.Checked = false;   // one start only: the unlocks are written into the save and cannot be undone
                    Status("Save backed up. Unlock everything is on for this start only.");
                }
                var lpsi = new ProcessStartInfo(launcherExe) { WorkingDirectory = root, UseShellExecute = false };
                if (modsManager != null) modsManager.Export(lpsi.EnvironmentVariables);
                lpsi.EnvironmentVariables.Remove("JSP_UNLOCK_ALL");   // only the Cheats box, after its backup
                if (unlockThisRun) lpsi.EnvironmentVariables["JSP_UNLOCK_ALL"] = "1";   // jsp-launcher.exe passes its environment on to the game
                try { Process.Start(lpsi); }
                catch (Exception ex) { MessageBox.Show(this, "Could not start the game:\n" + ex.Message, Text); return; }
                Close();
                return;
            }
            if (!File.Exists(exe)) { MessageBox.Show(this, "The game is not built yet:\n" + exe, Text); return; }
            if (!File.Exists(iso)) { MessageBox.Show(this, "The game disc image was not found:\n" + iso, Text); return; }
            if (Process.GetProcessesByName(gameProc).Length > 0) { MessageBox.Show(this, "The game is already running.", Text); return; }
            if (!SaveOnlineIfDirty()) return;
            SaveSettings();
            unlockThisRun = false;
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
            var t = new Thread(RunGame); t.IsBackground = false; t.Start();
        }

        // Background thread: mount, boost, run, wait, boost off, unmount.
        void RunGame()
        {
            Directory.CreateDirectory(runDir);
            Directory.CreateDirectory(Path.Combine(here, "hdd0"));
            try { log = new StreamWriter(Path.Combine(runDir, "launcher_log.txt"), false); } catch { log = null; }
            Log("Launcher start. Settings saved.");
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

                var psi = new ProcessStartInfo(exe, "\"" + elf + "\"");
                psi.WorkingDirectory = runDir;
                psi.UseShellExecute = false;
                psi.CreateNoWindow = true;
                psi.RedirectStandardOutput = true;
                psi.RedirectStandardError = true;
                psi.EnvironmentVariables["PS3_VFS_ROOT"] = drive + ":/";
                psi.EnvironmentVariables["PS3_HDD0_ROOT"] = Path.Combine(here, "hdd0").Replace('\\', '/');
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
                if (modsManager != null) modsManager.Export(psi.EnvironmentVariables);
                boostOff = psi.EnvironmentVariables["BOOST_OFF_ON_EXIT"] != "0";
                psi.EnvironmentVariables.Remove("JSP_UNLOCK_ALL");   // never from the settings file: only the Cheats box, after its backup
                if (unlockThisRun) { psi.EnvironmentVariables["JSP_UNLOCK_ALL"] = "1"; Log("Unlock everything: on for this start (save backed up)."); }

                psi.EnvironmentVariables["JSP_SETTINGS_FILE"] = settingsFile;   // the in-game settings pages write this file (jsp_settings.cpp)

                Status("Starting the game...");
                game = Process.Start(psi);
                Thread o = Pump(game.StandardOutput.BaseStream, Path.Combine(runDir, "play_stdout.txt"));
                Thread e = Pump(game.StandardError.BaseStream, KeepPrevious(Path.Combine(runDir, "play_stderr.txt")));
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
                    Status("Game closed" + (code != 0 ? " (exit code " + code + ", see jstars\\run\\play_stderr.txt)." : "."));
                try { if (log != null) log.Close(); } catch { }
                log = null;
                try {
                    BeginInvoke((MethodInvoker)delegate {
                        startBtn.Enabled = true;
                        if (closingAfterGame || hiddenForGame) { Close(); return; }   // nothing left running
                        if (WindowState == FormWindowState.Minimized) WindowState = FormWindowState.Normal;
                        Activate();
                    });
                } catch { }
            }
        }

        // ENHANCEMENT: the previous launch's log becomes NAME.prev.txt (one kept) before this launch rewrites NAME.txt, so a relaunch after a
        // freeze or a failed online match keeps the evidence; the support bundle takes both (installer/tools/support_bundle.py). Returns file.
        static string KeepPrevious(string file)
        {
            try
            {
                if (File.Exists(file))
                {
                    string prev = Path.Combine(Path.GetDirectoryName(file), Path.GetFileNameWithoutExtension(file) + ".prev" + Path.GetExtension(file));
                    if (File.Exists(prev)) File.Delete(prev);
                    File.Move(file, prev);
                }
            }
            catch { }   // a locked or read-only file: this launch still writes NAME.txt (FileMode.Create), only the copy is lost
            return file;
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
