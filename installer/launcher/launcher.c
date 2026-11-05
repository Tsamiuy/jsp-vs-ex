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
 * No rule merges with another; paths.txt is always read first.
 *
 * Reads <exe folder>\paths.txt (KEY=VALUE lines written by firstrun.py: PS3_VFS_ROOT, PS3_HDD0_ROOT, PS3_DEV_FLASH, PS3_TITLE,
 * PS3_VERBOSE, EXE, ELF), then the settings file chosen above (KEY=VALUE, e.g. RES_SCALE=3, FULLSCREEN=1; the
 * names play.ps1 uses), puts them in the environment and starts game\<EXE> game\<ELF> with game\run as the working folder,
 * stdout/stderr to game\run\play_stdout.txt and play_stderr.txt. Plain Win32, no network.
 * Values may start with @APP@ (a path inside the install folder, the portable zip's case): this exe's folder replaces it.
 * Build: x86_64-w64-mingw32-gcc (or the bundled llvm-mingw's ...-clang) -O2 -mwindows -s -o jsp-launcher.exe launcher.c
 * (installer\README.md). */
#include <windows.h>
#include <stdio.h>
#include <string.h>

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
    char line[1024];
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '='), *e;
        if (!eq || line[0] == '#' || line[0] == ';') continue;
        *eq++ = 0;
        for (e = eq + strlen(eq); e > eq && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '); ) *--e = 0;
        if (!strcmp(line, "EXE")) strncpy(exe, eq, exe_n - 1);
        else if (!strcmp(line, "ELF")) strncpy(elf, eq, elf_n - 1);
        else if (!strncmp(eq, "@APP@", 5)) {
            char full[MAX_PATH * 2];
            snprintf(full, sizeof full, "%s%s", g_here, eq + 5);
            SetEnvironmentVariableA(line, full);
        } else SetEnvironmentVariableA(line, eq);
    }
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

/* Picks and loads the settings file by the fixed order above; `why` gets the line for launcher_log.txt. */
static void load_settings(const char *here, const char *cmd, char *exe, size_t exe_n, char *elf, size_t elf_n, char *why, size_t why_n)
{
    char f[MAX_PATH * 2], app[MAX_PATH];
    snprintf(f, sizeof f, "%s\\settings_here.txt", here);
    if (flag_settings(cmd, f, sizeof f)) {
        if (file_exists(f)) { load_env(f, exe, exe_n, elf, elf_n); snprintf(why, why_n, "settings: command line (--settings %s)", f); }
        else snprintf(why, why_n, "settings: command line (--settings %s), file not found, defaults", f);
        return;
    }
    snprintf(f, sizeof f, "%s\\settings_here.txt", here);
    if (file_exists(f)) {
        snprintf(f, sizeof f, "%s\\play_settings.txt", here);
        if (file_exists(f)) { load_env(f, exe, exe_n, elf, elf_n); snprintf(why, why_n, "settings: marker file (settings_here.txt), install folder (play_settings.txt)"); }
        else snprintf(why, why_n, "settings: marker file (settings_here.txt), no play_settings.txt in the install folder, defaults");
        return;
    }
    snprintf(f, sizeof f, "%s\\play_settings.txt", here);
    if (file_exists(f)) { load_env(f, exe, exe_n, elf, elf_n); snprintf(why, why_n, "settings: install folder (play_settings.txt)"); return; }
    if (GetEnvironmentVariableA("APPDATA", app, sizeof app)) {
        snprintf(f, sizeof f, "%s\\JSP Fan Revival\\play_settings.txt", app);
        if (file_exists(f)) { load_env(f, exe, exe_n, elf, elf_n); snprintf(why, why_n, "settings: AppData (%%APPDATA%%\\JSP Fan Revival\\play_settings.txt)"); return; }
    }
    snprintf(why, why_n, "settings: none found, defaults");
}

static void fail(const char *msg)
{
    MessageBoxA(NULL, msg, "J-Stars Victory VS-EX: Fan Revival", MB_OK | MB_ICONWARNING);
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR cmd, int show)
{
    char here[MAX_PATH], path[MAX_PATH * 2], exe[MAX_PATH] = "JStarsRecomp.exe", elf[MAX_PATH] = "EBOOT.elf";
    char why[MAX_PATH * 3], run[MAX_PATH * 2], cl[MAX_PATH * 4], disc[MAX_PATH * 2], bfile[MAX_PATH * 2], boost[8], *s;
    int boosted = 0;
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE out, err;
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
    { FILE *lg = fopen(bfile, "w"); if (lg) { fprintf(lg, "%s\n", why); fclose(lg); } }
    snprintf(cl, sizeof cl, "\"%s\" \"%s\\game\\%s\"", path, here, elf);

    snprintf(path, sizeof path, "%s\\play_stdout.txt", run);
    out = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    snprintf(path, sizeof path, "%s\\play_stderr.txt", run);
    err = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
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
        if (boosted) boost_restore(bfile);
        fail("Could not start the game program.");
        return 1;
    }
    CloseHandle(pi.hThread);
    if (boosted) {                      /* wait for the game, then put the power plan back as it was */
        WaitForSingleObject(pi.hProcess, INFINITE);
        boost_restore(bfile);
    }
    CloseHandle(pi.hProcess);
    return 0;
}
