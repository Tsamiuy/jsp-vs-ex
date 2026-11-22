/* jsp-launcher.exe: starts the port that the installer built. Small stand-in used only by the installer's shortcuts; it does
 * not replace launcher/JStarsLauncher.cs, which stays the settings window for a hand-built tree. Layout after the
 * launcher of undecuple's Killzone 2 port, Project Pyrrhus (same MIT licence), and jstars/play.ps1 and tools/boost/boost.ps1 (their
 * environment variables, log files and the CPU boost), without the ISO mount: the disc is a folder here (PS3_VFS_ROOT).
 *
 * From the Windows test in issue #49:
 *  - The disc folder (PS3_VFS_ROOT) must exist and hold PS3_GAME before the game starts: a mounted image can come back under
 *    another drive letter, and without the check the game ran on and logged to stderr without end. A message box names the path.
 *  - CPU boost, as tools/boost/boost_on.bat and boost_off.bat do: the active power plan's "Maximum processor state" (powercfg
 *    PROCTHROTTLEMAX, AC and battery) is set to 100 % while the game runs; boost_off.bat sets 99 % afterwards whatever it was.
 *    ENHANCEMENT: this launcher puts back the values it found (kept in game\run\boost-restore.txt, so a crashed session is
 *    restored at the next start) and does nothing when both are already 100. Off by default since 2026-11-04 (players would not
 *    know their power plan changed); CPU_BOOST=1 in play_settings.txt turns it on (sound tested fine without it, test 4).
 *    The launcher now waits for the game to end (it needs to restore the plan). ASSUMPTION: the boost is the one part
 *    of the hand-built launcher that this one lacked; it has not been run on Windows, and whether it fixes the audio
 *    problem reported in #49 is untested.
 *
 * Settings (play_settings.txt, KEY=VALUE) come from the first of these that applies, highest rule first, and the launcher writes one
 * line naming the rule that won to game\run\launcher_log.txt ("settings: install folder (play_settings.txt)"). Order idea from the
 * launcher of undecuple's Killzone 2 port (Project Pyrrhus); the names are this project's:
 *   1. a one-run flag:      jsp-launcher.exe --settings <file>     (that file only, this start only)
 *   2. a marker file:       <install folder>\settings_here.txt    (then only <install folder>\play_settings.txt, never AppData)
 *   3. the install folder:  <install folder>\play_settings.txt
 *   4. AppData:             %APPDATA%\JSP Fan Revival\play_settings.txt   (answers the #52 note that this copy was ignored)
 * mods.txt (the mods folder's enable list) is looked for next to the file that won, else in the install folder; a second log line says
 * "mods: N listed (<path>)" (mods_env below). No rule merges with another; paths.txt is always read first. The file that won is also put in JSP_SETTINGS_FILE for the game: the in-game
 * settings pages (jsp_settings.cpp) save to that same file.
 *
 * Reads <exe folder>\paths.txt (KEY=VALUE lines written by firstrun.py: PS3_VFS_ROOT, PS3_HDD0_ROOT, PS3_DEV_FLASH, PS3_TITLE,
 * PS3_VERBOSE, EXE, ELF), then the settings file chosen above (KEY=VALUE, e.g. RES_SCALE=3, FULLSCREEN=1; the
 * names play.ps1 uses), puts them in the environment and starts game\<EXE> game\<ELF> with game\run as the working folder,
 * stdout/stderr to game\run\play_stdout.txt and play_stderr.txt (the last play_stderr.txt is kept as play_stderr.prev.txt). Plain Win32, no network.
 * Values may start with @APP@ (a path inside the install folder, the portable zip's case): this exe's folder replaces it.
 * Build: x86_64-w64-mingw32-gcc (or the bundled llvm-mingw's ...-clang) -O2 -mwindows -s -o jsp-launcher.exe launcher.c
 * (installer\README.md). */
#include <windows.h>
#include <stdio.h>
#include <string.h>

/* JSP_RECOVERY_PARSER_BEGIN: same bounded wire contract as the C# launchers. */
#include <stdint.h>
struct recovery_parser {
    uint64_t highest, current;
    int stopped, discard;
    size_t used;
    char line[97];
};
static void recovery_byte(struct recovery_parser *p, unsigned char c)
{
    char *s, *digits;
    uint64_t id = 0;
    int kind;
    if (c != '\n') {
        if (p->discard) return;
        if (p->used == 96 || c == 0 || c > 127 || (c < 32 && c != '\r')) {
            p->discard = 1; return;
        }
        p->line[p->used++] = (char)c;
        return;
    }
    if (p->discard) { p->discard = 0; p->used = 0; return; }
    if (p->used && p->line[p->used - 1] == '\r') --p->used;
    p->line[p->used] = 0;
    p->used = 0;
    if (strncmp(p->line, "JSP_SESSION/1 ", 14)) return;
    s = p->line + 14;
    if (!strncmp(s, "START ", 6)) { kind = 1; s += 6; }
    else if (!strncmp(s, "RETIRE ", 7)) { kind = 2; s += 7; }
    else if (!strncmp(s, "STOPPED ", 8)) { kind = 3; s += 8; }
    else return;
    digits = s;
    if (*s < '1' || *s > '9') return;
    for (; *s >= '0' && *s <= '9'; ++s) {
        unsigned d = (unsigned)(*s - '0');
        if (id > (UINT64_MAX - d) / 10) return;
        id = id * 10 + d;
    }
    if (s == digits || (kind == 3 ? strcmp(s, " CLOCK") : *s != 0)) return;
    if (kind == 1 && id > p->highest) { p->highest = p->current = id; p->stopped = 0; }
    else if (kind == 2 && id == p->current) { p->current = 0; p->stopped = 0; }
    else if (kind == 3 && id == p->current) p->stopped = 1;
}
/* JSP_RECOVERY_PARSER_END */

struct recovery_ui {
    struct recovery_parser parser;
    HWND window;
    HFONT font;
    uint64_t shown, dismissed;
};
static LRESULT CALLBACK recovery_window(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    struct recovery_ui *ui = (struct recovery_ui *)GetWindowLongPtrA(window, GWLP_USERDATA);
    if (message == WM_CREATE) {
        HWND text, button;
        ui = (struct recovery_ui *)((CREATESTRUCTA *)lp)->lpCreateParams;
        SetWindowLongPtrA(window, GWLP_USERDATA, (LONG_PTR)ui);
        ui->font = CreateFontA(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, "Segoe UI");
        text = CreateWindowA("STATIC", "Online session stopped because the game stopped responding.\r\n\r\n"
            "If the game stays frozen, close its window and start the game again.",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 18, 530, 100, window, NULL, NULL, NULL);
        button = CreateWindowA("BUTTON", "Dismiss", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            452, 126, 96, 28, window, (HMENU)(INT_PTR)1, NULL, NULL);
        SendMessageA(text, WM_SETFONT, (WPARAM)ui->font, TRUE);
        SendMessageA(button, WM_SETFONT, (WPARAM)ui->font, TRUE);
        return 0;
    }
    if (ui && (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wp) == 1))) {
        ui->dismissed = ui->shown;
        DestroyWindow(window); // dismissing this notice never closes or kills the game
        return 0;
    }
    if (ui && message == WM_DESTROY) {
        ui->window = NULL;
        if (ui->font) { DeleteObject(ui->font); ui->font = NULL; }
        return 0;
    }
    return DefWindowProcA(window, message, wp, lp);
}
static void recovery_update(struct recovery_ui *ui)
{
    if (ui->window && (!ui->parser.current || !ui->parser.stopped || ui->shown != ui->parser.current))
        DestroyWindow(ui->window);
    if (!ui->parser.current || !ui->parser.stopped || ui->window || ui->dismissed == ui->parser.current) return;
    ui->shown = ui->parser.current;
    ui->window = CreateWindowExA(WS_EX_DLGMODALFRAME, "JspRecoveryNotice", "JSP - Online session stopped",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, (GetSystemMetrics(SM_CXSCREEN) - 580) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - 218) / 2, 580, 218, NULL, NULL, GetModuleHandleA(NULL), ui);
    if (ui->window) {
        FLASHWINFO flash = { sizeof flash, ui->window, FLASHW_TRAY, 3, 0 };
        ShowWindow(ui->window, SW_SHOWNORMAL);
        SetForegroundWindow(ui->window);
        FlashWindowEx(&flash);
    }
}
static void wait_game_with_recovery(HANDLE process, HANDLE records)
{
    struct recovery_ui ui;
    WNDCLASSA wc;
    MSG msg;
    memset(&ui, 0, sizeof ui);
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = recovery_window;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "JspRecoveryNotice";
    RegisterClassA(&wc);
    while (WaitForSingleObject(process, 50) == WAIT_TIMEOUT) {
        unsigned budget;
        int caught_up = 0;
        // Only this launch's newly-created stderr file, never a historical log.
        // Bound each read batch so a noisy game cannot starve process/UI checks.
        for (budget = 0; records != INVALID_HANDLE_VALUE && budget != 8; ++budget) {
            unsigned char bytes[8192]; DWORD count = 0, i;
            if (!ReadFile(records, bytes, sizeof bytes, &count, NULL)) {
                if (GetLastError() == ERROR_HANDLE_EOF) caught_up = 1;
                break;
            }
            if (!count) { caught_up = 1; break; }
            for (i = 0; i < count; ++i) recovery_byte(&ui.parser, bytes[i]);
        }
        // Avoid showing a failure while later lifecycle records are still in the batch.
        // Stream latency remains asynchronous; this is not a cross-process live-state lock.
        if (caught_up || !ui.parser.stopped) recovery_update(&ui);
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (!ui.window || !IsDialogMessageA(ui.window, &msg)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        }
    }
    if (ui.window) DestroyWindow(ui.window);
}

static int file_exists(const char *path)
{
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

/* A value that starts with @APP@ is a path inside the install folder (firstrun.py stores them so the folder can be moved or
 * renamed after the first run, the portable zip's case): the folder of this exe replaces the token. */
static char g_here[MAX_PATH];

static void load_env(const char *file, char *exe, size_t exe_n, char *elf, size_t elf_n)
{
    FILE *f = fopen(file, "r");
    char line[1024], pipe_val[16] = "";
    int pipe_seen = 0;
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '='), *e;
        if (!eq || line[0] == '#' || line[0] == ';') continue;
        *eq++ = 0;
        for (e = eq + strlen(eq); e > eq && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '); ) *--e = 0;
        if (!strcmp(line, "PIPE")) { snprintf(pipe_val, sizeof pipe_val, "%s", eq); continue; }   /* applied below, only with PIPE_SEEN=2 */
        if (!strcmp(line, "PIPE_SEEN")) { pipe_seen = !strcmp(eq, "2"); continue; }
        if (!strcmp(line, "EXE")) strncpy(exe, eq, exe_n - 1);
        else if (!strcmp(line, "ELF")) strncpy(elf, eq, elf_n - 1);
        else if (!strncmp(eq, "@APP@", 5)) {
            char full[MAX_PATH * 2];
            snprintf(full, sizeof full, "%s%s", g_here, eq + 5);
            SetEnvironmentVariableA(line, full);
        } else SetEnvironmentVariableA(line, eq);
    }
    /* Two-thread drawing is on by default (toolkit patch 7907). A file from before that holds PIPE=0 for everyone who never touched the
     * box, so PIPE counts only next to PIPE_SEEN=2 (the settings window writes both); a hand-written PIPE=0 needs the marker too. */
    if (pipe_seen && pipe_val[0]) SetEnvironmentVariableA("PIPE", pipe_val);
    fclose(f);
}

/* ---- CPU boost (powercfg, no admin) ---- */
static int run_hidden(const char *cmdline, char *out, size_t out_n)
{
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE rd = NULL, wr = NULL;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cl[512];
    DWORD got = 0, code = 1;
    size_t n = 0;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    snprintf(cl, sizeof cl, "%s", cmdline);
    if (!CreateProcessA(NULL, cl, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(rd); CloseHandle(wr);
        return -1;
    }
    CloseHandle(wr);
    while (ReadFile(rd, out && n + 1 < out_n ? out + n : cl, out && n + 1 < out_n ? (DWORD)(out_n - 1 - n) : sizeof cl, &got, NULL) && got)
        if (out && n + 1 < out_n) n += got;
    if (out) out[n] = 0;
    WaitForSingleObject(pi.hProcess, 15000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(rd);
    return (int)code;
}

/* Current AC and DC "Maximum processor state" of the active plan; -1 when unreadable. */
static int read_max(int *ac, int *dc)
{
    static char buf[16384];
    char *a, *d;
    *ac = *dc = -1;
    if (run_hidden("powercfg /query SCHEME_CURRENT SUB_PROCESSOR PROCTHROTTLEMAX", buf, sizeof buf) != 0) return -1;
    a = strstr(buf, "Current AC Power Setting Index:");
    d = strstr(buf, "Current DC Power Setting Index:");
    if (!a || !d) return -1;
    *ac = (int)strtol(strstr(a, "0x") ? strstr(a, "0x") + 2 : "", NULL, 16);
    *dc = (int)strtol(strstr(d, "0x") ? strstr(d, "0x") + 2 : "", NULL, 16);
    return 0;
}

static void set_max(int ac, int dc)
{
    char c[160];
    snprintf(c, sizeof c, "powercfg /setacvalueindex SCHEME_CURRENT SUB_PROCESSOR PROCTHROTTLEMAX %d", ac);
    run_hidden(c, NULL, 0);
    snprintf(c, sizeof c, "powercfg /setdcvalueindex SCHEME_CURRENT SUB_PROCESSOR PROCTHROTTLEMAX %d", dc);
    run_hidden(c, NULL, 0);
    run_hidden("powercfg /setactive SCHEME_CURRENT", NULL, 0);
}

/* Put back what an earlier session saved (the game or the launcher was killed before it could). */
static void boost_restore(const char *file)
{
    FILE *f = fopen(file, "r");
    int ac = -1, dc = -1;
    if (!f) return;
    if (fscanf(f, "%d %d", &ac, &dc) == 2 && ac >= 0 && ac <= 100 && dc >= 0 && dc <= 100) set_max(ac, dc);
    fclose(f);
    DeleteFileA(file);
}

/* Returns 1 when it raised the plan (and saved the old values in file). */
static int boost_on(const char *file)
{
    int ac, dc;
    FILE *f;
    if (read_max(&ac, &dc) != 0 || (ac >= 100 && dc >= 100)) return 0;
    f = fopen(file, "w");
    if (!f) return 0;
    fprintf(f, "%d %d\n", ac, dc);
    fclose(f);
    set_max(100, 100);
    return 1;
}

/* The value after "--settings" or "--settings=" in the command line (quotes removed); 0 when absent. */
static int flag_settings(const char *cmd, char *out, size_t n)
{
    const char *p = strstr(cmd, "--settings"), *e;
    if (!p) return 0;
    p += 10;
    if (*p == '=' || *p == ' ') p++;
    while (*p == ' ') p++;
    if (*p == '"') { p++; e = strchr(p, '"'); } else e = strchr(p, ' ');
    if (!e) e = p + strlen(p);
    if (e == p || (size_t)(e - p) >= n) return 0;
    memcpy(out, p, (size_t)(e - p));
    out[e - p] = 0;
    return 1;
}

/* The settings file that won (empty: none), for mods.txt next to it. */
static char g_settings[MAX_PATH * 2];

static void use_settings(const char *f, char *exe, size_t exe_n, char *elf, size_t elf_n)
{
    load_env(f, exe, exe_n, elf, elf_n);
    SetEnvironmentVariableA("JSP_SETTINGS_FILE", f);
    snprintf(g_settings, sizeof g_settings, "%s", f);
}

/* Picks and loads the settings file by the fixed order above; `why` gets the line for launcher_log.txt. */
static void load_settings(const char *here, const char *cmd, char *exe, size_t exe_n, char *elf, size_t elf_n, char *why, size_t why_n)
{
    char f[MAX_PATH * 2], app[MAX_PATH];
    snprintf(f, sizeof f, "%s\\settings_here.txt", here);
    if (flag_settings(cmd, f, sizeof f)) {
        if (file_exists(f)) { use_settings(f, exe, exe_n, elf, elf_n); snprintf(why, why_n, "settings: command line (--settings %s)", f); }
        else snprintf(why, why_n, "settings: command line (--settings %s), file not found, defaults", f);
        return;
    }
    snprintf(f, sizeof f, "%s\\settings_here.txt", here);
    if (file_exists(f)) {
        snprintf(f, sizeof f, "%s\\play_settings.txt", here);
        if (file_exists(f)) { use_settings(f, exe, exe_n, elf, elf_n); snprintf(why, why_n, "settings: marker file (settings_here.txt), install folder (play_settings.txt)"); }
        else snprintf(why, why_n, "settings: marker file (settings_here.txt), no play_settings.txt in the install folder, defaults");
        return;
    }
    snprintf(f, sizeof f, "%s\\play_settings.txt", here);
    if (file_exists(f)) { use_settings(f, exe, exe_n, elf, elf_n); snprintf(why, why_n, "settings: install folder (play_settings.txt)"); return; }
    if (GetEnvironmentVariableA("APPDATA", app, sizeof app)) {
        snprintf(f, sizeof f, "%s\\JSP Fan Revival\\play_settings.txt", app);
        if (file_exists(f)) { use_settings(f, exe, exe_n, elf, elf_n); snprintf(why, why_n, "settings: AppData (%%APPDATA%%\\JSP Fan Revival\\play_settings.txt)"); return; }
    }
    snprintf(why, why_n, "settings: none found, defaults");
}

/* ENHANCEMENT (the mods folder, the game's jsp_mods.cpp): the game reads mods.txt (JSP_MODS_LIST, the enable list) and the mods in
 * JSP_MODS_DIR. mods.txt sits next to the settings file that won; with no settings file, in the install folder (ASSUMPTION: the design's
 * proposal P8, not yet confirmed; AppData would be the other choice). The mods folder is <install folder>\mods. A value already in the
 * environment (the settings file, or the player's own) wins, and JSP_MODS=0 turns mods off. The launcher only passes paths: the game parses
 * and checks the list. `line` gets the launcher_log.txt line, "mods: N listed (<path>)", N = the lines that name something. */
/* An environment value: 1 = read into b (it fits), 0 = unset or empty (b is ""), -1 = set but longer than the buffer (b is ""). */
static int env_get(const char *k, char *b, DWORD n)
{
    DWORD r;
    b[0] = 0;
    r = GetEnvironmentVariableA(k, b, n);
    if (r == 0) { b[0] = 0; return 0; }
    if (r >= n) { b[0] = 0; return -1; }   /* the call returned the size it needs and wrote nothing usable */
    return b[0] ? 1 : 0;
}

/* The path as the log shows it: under %APPDATA% it is printed with %APPDATA% (as the settings line does), so the profile folder stays out. */
static void shown_path(const char *path, char *out, size_t n)
{
    char app[MAX_PATH] = "";
    const int r = env_get("APPDATA", app, sizeof app);
    const size_t k = strlen(app);
    if (r == 1 && k && !_strnicmp(path, app, k) && (path[k] == '\\' || path[k] == '/')) snprintf(out, n, "%%APPDATA%%%s", path + k);
    else snprintf(out, n, "%s", path);
}

static void mods_env(const char *here, char *line, size_t n)
{
    char list[MAX_PATH * 2] = "", dir[MAX_PATH * 2] = "", v[8] = "", buf[512] = "", show[MAX_PATH * 2 + 16] = "";
    const char *slash;
    DWORD a;
    FILE *f;
    int count = 0, r;
    if (env_get("JSP_MODS", v, sizeof v) == 1 && !strcmp(v, "0")) { snprintf(line, n, "mods: off (JSP_MODS=0)"); return; }
    if (env_get("JSP_MODS_DIR", dir, sizeof dir) == 0) {   /* a preset value (even one too long for this buffer) is the game's to read */
        snprintf(dir, sizeof dir, "%s\\mods", here);
        a = GetFileAttributesA(dir);
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) SetEnvironmentVariableA("JSP_MODS_DIR", dir);
    }
    r = env_get("JSP_MODS_LIST", list, sizeof list);
    if (r < 0) { snprintf(line, n, "mods: JSP_MODS_LIST is longer than %u characters: not counted here (the game reads it)", (unsigned)(sizeof list - 1)); return; }
    if (r == 0) {
        slash = strrchr(g_settings, '\\');
        if (strrchr(g_settings, '/') > slash) slash = strrchr(g_settings, '/');   /* --settings may be given with '/' */
        if (slash) snprintf(list, sizeof list, "%.*s\\mods.txt", (int)(slash - g_settings), g_settings);
        else snprintf(list, sizeof list, "%s\\mods.txt", here);
        shown_path(list, show, sizeof show);
        if (!file_exists(list)) { snprintf(line, n, "mods: 0 listed (%s, not found)", show); return; }
        SetEnvironmentVariableA("JSP_MODS_LIST", list);
    }
    shown_path(list, show, sizeof show);
    f = fopen(list, "r");
    if (!f) { snprintf(line, n, "mods: 0 listed (%s, cannot be read)", show); return; }
    while (fgets(buf, sizeof buf, f)) {
        char *s = buf, *h = strchr(buf, '#');
        if (h) *h = 0;
        if (!strncmp(s, "\xEF\xBB\xBF", 3)) s += 3;   /* a UTF-8 BOM on the first line */
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
        if (*s) count++;
    }
    fclose(f);
    snprintf(line, n, "mods: %d listed (%s)", count, show);
}

static void fail(const char *msg)
{
    MessageBoxA(NULL, msg, "J-Stars Victory VS-EX: Fan Revival", MB_OK | MB_ICONWARNING);
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR cmd, int show)
{
    char here[MAX_PATH], path[MAX_PATH * 2], exe[MAX_PATH] = "JStarsRecomp.exe", elf[MAX_PATH] = "EBOOT.elf";
    char why[MAX_PATH * 3], mods[MAX_PATH * 3] = "", run[MAX_PATH * 2], cl[MAX_PATH * 4], disc[MAX_PATH * 2], bfile[MAX_PATH * 2], boost[8], *s;
    int boosted = 0;
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE out, err, records;
    (void)h; (void)p; (void)show;

    GetModuleFileNameA(NULL, here, sizeof here);
    if ((s = strrchr(here, '\\'))) *s = 0;
    strncpy(g_here, here, sizeof g_here - 1);
    snprintf(path, sizeof path, "%s\\paths.txt", here);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        fail("The game has not been built yet (paths.txt is missing).\n\nRun \"Build again\" from the Start menu "
             "and read the newest file in the logs folder of the install folder if it stops.");
        return 1;
    }
    load_env(path, exe, sizeof exe, elf, sizeof elf);
    load_settings(here, cmd, exe, sizeof exe, elf, sizeof elf, why, sizeof why);
    mods_env(here, mods, sizeof mods);

    /* The disc folder: gone (a mounted image under another drive letter, a moved folder) means the game would run on without
     * data and flood its log. Check it here and say where the path is kept. */
    if (GetEnvironmentVariableA("PS3_VFS_ROOT", disc, sizeof disc) && disc[0]) {
        snprintf(path, sizeof path, "%s\\PS3_GAME", disc);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
            char msg[MAX_PATH * 6];
            snprintf(msg, sizeof msg, "The game disc folder was not found:\n\n%s\n\nIf your disc is a mounted image, its drive letter "
                     "may have changed: mount it again, or edit the line PS3_VFS_ROOT= in\n%s\\paths.txt\nto the folder that holds PS3_GAME, "
                     "then start the game again.", disc, here);
            fail(msg);
            return 1;
        }
    }
    snprintf(path, sizeof path, "%s\\game\\%s", here, exe);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        fail("The game program is missing. Run \"Build again\" from the Start menu.");
        return 1;
    }
    snprintf(run, sizeof run, "%s\\game\\run", here);
    CreateDirectoryA(run, NULL);
    snprintf(bfile, sizeof bfile, "%s\\launcher_log.txt", run);   /* not `path`: it still holds the game program's path for cl below */
    { FILE *lg = fopen(bfile, "w"); if (lg) { fprintf(lg, "%s\n%s\n", why, mods); fclose(lg); } }
    snprintf(cl, sizeof cl, "\"%s\" \"%s\\game\\%s\"", path, here, elf);

    snprintf(path, sizeof path, "%s\\play_stdout.txt", run);
    out = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    snprintf(path, sizeof path, "%s\\play_stderr.txt", run);
    /* ENHANCEMENT: the previous launch's stderr (the game's [net] lines, a freeze's last lines) becomes play_stderr.prev.txt, one kept, so a
       relaunch does not erase it; the support bundle takes both. A failed move (no file yet, or locked) only loses that copy. */
    snprintf(bfile, sizeof bfile, "%s\\play_stderr.prev.txt", run);
    MoveFileExA(path, bfile, MOVEFILE_REPLACE_EXISTING);
    err = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    records = err == INVALID_HANDLE_VALUE ? INVALID_HANDLE_VALUE :
        CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = out;
    si.hStdError = err;
    snprintf(bfile, sizeof bfile, "%s\\boost-restore.txt", run);
    boost_restore(bfile);
    /* Off unless CPU_BOOST=1: a power plan change that other games would also feel, which players would not know about.
       boost_restore above still puts back a plan that an older version of this launcher left boosted. */
    if (GetEnvironmentVariableA("CPU_BOOST", boost, sizeof boost) && boost[0] == '1') boosted = boost_on(bfile);
    /* The game window gets the installer's icon (installer patch 0007: WINDOW_ICON), and its title shows FPS and draw counts only
       when TITLE_STATS=1 ("Show FPS in the window title" in the settings window). */
    snprintf(path, sizeof path, "%s\\jsp-launcher.ico", here);
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) SetEnvironmentVariableA("WINDOW_ICON", path);
    if (!GetEnvironmentVariableA("TITLE_STATS", boost, sizeof boost)) SetEnvironmentVariableA("TITLE_STATS", "0");
    if (!CreateProcessA(NULL, cl, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, run, &si, &pi)) {
        if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
        if (err != INVALID_HANDLE_VALUE) CloseHandle(err);
        if (records != INVALID_HANDLE_VALUE) CloseHandle(records);
        if (boosted) boost_restore(bfile);
        fail("Could not start the game program.");
        return 1;
    }
    CloseHandle(pi.hThread);
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (err != INVALID_HANDLE_VALUE) CloseHandle(err);
    // Keep this exact game's owner alive even with the default CPU_BOOST=0.
    // The notice is nonmodal; dismissal never controls the game or delays cleanup.
    wait_game_with_recovery(pi.hProcess, records);
    if (records != INVALID_HANDLE_VALUE) CloseHandle(records);
    if (boosted) boost_restore(bfile);
    CloseHandle(pi.hProcess);
    return 0;
}
