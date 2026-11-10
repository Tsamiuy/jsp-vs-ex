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
        ComboBox resBox, winBox, modeBox, aaBox, shadowBox;
        static readonly string[] ShadowNames = {
            "Low      (the game's own: 2048 x 1024)",
            "Medium   (2x)",
            "High     (3x)",
            "Ultra    (4x)",
        };
        CheckBox fxFullBox;
        CheckBox fpsBox;
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
                    if (k == "RES_SCALE" || k == "SHADOW_SCALE" || k == "FULLSCREEN" || k == "WINDOW_SIZE" || k == "BOOST_OFF_ON_EXIT" || k == "UI_SCALE" || k == "AA_MODE" || k == "FX_FULL" || k == "FPS_COUNTER" ||
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
            SaveSettings();
            gameRunning = true;
            startBtn.Enabled = false;
            var t = new Thread(RunGame); t.IsBackground = false; t.Start();
        }

        // Background thread: mount, boost, run, wait, boost off, unmount.
        void RunGame()
        {
            Directory.CreateDirectory(runDir);
            Directory.CreateDirectory(Path.Combine(appData, "hdd0"));
            try { log = new StreamWriter(Path.Combine(runDir, "launcher_log.txt"), false); } catch { log = null; }
            Log("Launcher start. Settings: " + string.Join(" | ", File.ReadAllLines(settingsFile)));

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
