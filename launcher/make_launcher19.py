"""Generates JStarsLauncher19.cs (the v19 launcher) from JStarsLauncher.cs (the v15 one).

The v19 game runs from PS3Recomp\\jstars19 (jsp.exe, its FFmpeg DLLs and EBOOT.elf, copied there by
tools\\publish19.ps1) with the game data of the installer layout in %APPDATA%\\JSP Fan Revival (hdd0, saves in
hdd0_gamedata). Run: python make_launcher19.py, then build_launcher19.bat."""
import os
here = os.path.dirname(os.path.abspath(__file__))
s = open(os.path.join(here, 'JStarsLauncher.cs'), encoding='utf-8').read()

def sub(a, b, n=1):
    global s
    assert s.count(a) == n, (s.count(a), a[:80])
    s = s.replace(a, b)

# paths
sub('''            if (!Directory.Exists(Path.Combine(root, "jstars")) && Directory.Exists(Path.Combine(Path.GetDirectoryName(root), "jstars")))
                root = Path.GetDirectoryName(root);   // run from the launcher folder itself
            here = Path.Combine(root, "jstars");''',
'''            if (!Directory.Exists(Path.Combine(root, "jstars19")) && Directory.Exists(Path.Combine(Path.GetDirectoryName(root), "jstars19")))
                root = Path.GetDirectoryName(root);   // run from the launcher folder itself
            here = Path.Combine(root, "jstars19");
            appData = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "JSP Fan Revival");''')
sub('''            exe = Path.Combine(here, @"port\\build\\JStarsRecomp.exe");''', '''            exe = Path.Combine(here, "jsp.exe");''')
sub('''        readonly string root, here, iso, exe, elf, settingsFile, runDir, boostDir;''',
    '''        readonly string root, here, iso, exe, elf, settingsFile, runDir, boostDir, appData;''')
sub('''Process.GetProcessesByName("JStarsRecomp")''', '''Process.GetProcessesByName("jsp")''')
sub('''see jstars\\\\run\\\\play_stderr.txt''', '''see jstars19\\\\run\\\\play_stderr.txt''')

# start arguments and data folders
sub('''                var psi = new ProcessStartInfo(exe, "\\"" + elf + "\\"");''',
'''                string hdd0 = Path.Combine(appData, "hdd0").Replace('\\\\', '/');
                string flash = Path.Combine(root, @"RPCS3\\dev_flash").Replace('\\\\', '/');
                var psi = new ProcessStartInfo(exe, "\\"" + elf + "\\" --disc " + drive + ":/ --hdd0 \\"" + hdd0 + "\\" --flash \\"" + flash + "\\"");''')
sub('''                psi.EnvironmentVariables["PS3_HDD0_ROOT"] = Path.Combine(here, "hdd0").Replace('\\\\', '/');''',
    '''                psi.EnvironmentVariables["PS3_HDD0_ROOT"] = hdd0;   // saves: <hdd0>_gamedata''')
sub('''            Directory.CreateDirectory(Path.Combine(here, "hdd0"));''', '''            Directory.CreateDirectory(Path.Combine(appData, "hdd0"));''')

# bigger window
sub('''            ClientSize = Z(640, 718);''', '''            ClientSize = Z(780, 820);''')
sub('''            var header = new Panel { BackColor = HeaderBack, Location = P(0, 0), Size = Z(640, 64) };''',
    '''            var header = new Panel { BackColor = HeaderBack, Location = P(0, 0), Size = Z(780, 64) };''')
sub('''                Text = "PC port launcher", ForeColor''', '''                Text = "PC port launcher  -  v19", ForeColor''')
sub('''            tabs = new TabControl { Location = P(12, 76), Size = Z(616, 574) };''',
    '''            tabs = new TabControl { Location = P(12, 76), Size = Z(756, 676) };''')
sub('''            statusLabel = new Label { Location = P(14, 670), Size = Z(340, 36), Text = "Ready.", TextAlign = ContentAlignment.MiddleLeft };''',
    '''            statusLabel = new Label { Location = P(14, 772), Size = Z(480, 36), Text = "Ready.", TextAlign = ContentAlignment.MiddleLeft };''')
sub('''                Text = "Start Game", Location = P(366, 666), Size = Z(150, 40),''',
    '''                Text = "Start Game", Location = P(506, 768), Size = Z(150, 40),''')
sub('''            exitBtn = new Button { Text = "Exit", Location = P(526, 666), Size = Z(100, 40), Font = new Font("Segoe UI", 10F) };''',
    '''            exitBtn = new Button { Text = "Exit", Location = P(666, 768), Size = Z(100, 40), Font = new Font("Segoe UI", 10F) };''')

# Display tab: resolution not in v19 yet; Shadows option; v15-only fight options greyed out until ported
sub('''            resBox.SelectedIndexChanged += delegate { SaveIfReady(); };
            p.Controls.Add(resBox);''', '''            resBox.SelectedIndexChanged += delegate { SaveIfReady(); };
            p.Controls.Add(resBox);
''')
sub('''            // UI_SCALE=0.50..1.00 for the game; the bar counts in 5 % steps (10..20).''', '''            // v19: the graphics options get their own tab (the maintainer: more room)
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
                Text = "Sharper, steadier shadows on characters and stages. Higher costs a little GPU;\\r\\nyour RPCS3 setup (300% scale) draws them like High.",
                Location = P(16, y), AutoSize = true, ForeColor = SystemColors.GrayText });
            y += 48;

            // UI_SCALE=0.50..1.00 for the game; the bar counts in 5 % steps (10..20).''')
sub('''            p.Controls.Add(fpsBox);
            return p;''', '''            p.Controls.Add(fpsBox);
            // v19: the fight HUD size, edge smoothing, full-size effects and FPS counter live in the J-Stars game code,
            // which is not ported to v19 yet.
            return displayTab;''')
sub('''            var p = new TabPage("Display") { Padding = new Padding(12) };''', '''            var p = new TabPage("Display") { Padding = new Padding(12), AutoScroll = true };''')
sub('''            tabs.TabPages.Add(BuildDisplayTab());''', '''            tabs.TabPages.Add(BuildDisplayTab());
            tabs.TabPages.Add(graphicsTab);''')
sub('''        ComboBox resBox, winBox, modeBox, aaBox;''', '''        TabPage displayTab, graphicsTab;
        ComboBox resBox, winBox, modeBox, aaBox;''')
sub('''        TabPage displayTab, graphicsTab;
        ComboBox resBox, winBox, modeBox, aaBox;''', '''        TabPage displayTab, graphicsTab;
        ComboBox resBox, winBox, modeBox, aaBox, shadowBox;
        static readonly string[] ShadowNames = {
            "Low      (the game's own: 2048 x 1024)",
            "Medium   (2x)",
            "High     (3x)",
            "Ultra    (4x)",
        };''')

# settings
sub('''                    if (k == "RES_SCALE" || k == "FULLSCREEN"''', '''                    if (k == "RES_SCALE" || k == "SHADOW_SCALE" || k == "FULLSCREEN"''')
sub('''            bool full = cur.TryGetValue("FULLSCREEN", out s) && s == "1";''',
'''            int sh;
            if (!cur.TryGetValue("SHADOW_SCALE", out s) || !int.TryParse(s, out sh)) sh = 2;
            shadowBox.SelectedIndex = Math.Max(1, Math.Min(4, sh)) - 1;
            bool full = cur.TryGetValue("FULLSCREEN", out s) && s == "1";''')
sub('''            lines.Add("FULLSCREEN=" + (rbFull.Checked ? "1" : "0"));''',
'''            lines.Add("SHADOW_SCALE=" + (shadowBox.SelectedIndex + 1));
            lines.Add("FULLSCREEN=" + (rbFull.Checked ? "1" : "0"));''')
sub('''            lines.Add("# Written by the J-Stars launcher (also read by Play J-Stars.bat).");''',
    '''            lines.Add("# Written by the J-Stars v19 launcher.");''')


# resolution: the in-between sizes (patch 2504 takes any factor)
sub('''        static readonly string[] ResNames = {
            \"720p   (1280 x 720)   the PS3's own\",
            \"1440p  (2560 x 1440)\",
            \"4K     (3840 x 2160)\",
            \"5K     (5120 x 2880)   heavy, not tested yet\",
        };''', '''        static readonly string[] ResNames = {
            \"720p   (1280 x 720)   the PS3's own\",
            \"900p   (1600 x 900)\",
            \"1080p  (1920 x 1080)\",
            \"1440p  (2560 x 1440)\",
            \"1800p  (3200 x 1800)\",
            \"4K     (3840 x 2160)\",
            \"5K     (5120 x 2880)   heavy\",
        };
        static readonly string[] ResScales = { \"1\", \"1.25\", \"1.5\", \"2\", \"2.5\", \"3\", \"4\" };''')
sub('''            int scale; string s;
            if (!cur.TryGetValue(\"RES_SCALE\", out s) || !int.TryParse(s, out scale)) scale = 1;
            resBox.SelectedIndex = Math.Max(1, Math.Min(4, scale)) - 1;''', '''            string s;
            int ri = cur.TryGetValue(\"RES_SCALE\", out s) ? Array.IndexOf(ResScales, s.Trim()) : 0;
            resBox.SelectedIndex = ri < 0 ? 0 : ri;''')
sub('''            lines.Add(\"RES_SCALE=\" + (resBox.SelectedIndex + 1));''', '''            lines.Add(\"RES_SCALE=\" + ResScales[resBox.SelectedIndex]);''')
sub('''                Text = \"The size the game draws at. The game can only scale 720p by whole steps,\\r\\nso these are all the sizes it can render. Higher looks sharper and needs more GPU.\",''',
    '''                Text = \"The size the game draws at. Higher looks sharper and needs more GPU.\\r\\nThe window or screen shows it stretched to its own size.\",''')

open(os.path.join(here, 'JStarsLauncher19.cs'), 'w', encoding='utf-8').write(s)
print('ok')
