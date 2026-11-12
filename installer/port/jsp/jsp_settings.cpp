/* jsp_settings.cpp: ENHANCEMENT: the OS side of the in-game settings pages (jsp_port_menu.cpp, jsp_settings.h):
 *   - which settings file the launcher read for this start, and an atomic write of it;
 *   - the values that can change while the game runs ("live"): the readers in jsp_game.cpp ask jsp_setting_get() and re-read when
 *     jsp_setting_gen() has moved, instead of caching getenv for the whole run.
 *
 * The file: JSP_SETTINGS_FILE (set by jsp-launcher.exe and both settings windows to the file they loaded), else the first existing
 * of <exe folder>\..\play_settings.txt (installer layout: the exe is in game\), <exe folder>\play_settings.txt,
 * %APPDATA%\JSP Fan Revival\play_settings.txt; none exists: the AppData one is created (launcher.c's fourth rule reads it).
 * ASSUMPTION: the launcher that started this process read that same file; JSP_SETTINGS_FILE makes it certain.
 * Size limit 64 KB (a larger file is neither read nor overwritten). Save: temp file beside it, then renamed over it.
 */
#include "jsp_settings.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <direct.h>
#include <stdlib.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
std::mutex g_mu;
std::map<std::string, std::string> g_live;   // key -> value set from the in-game page
std::atomic<int> g_gen{0};
std::string g_path;                          // resolved once
bool g_path_done = false;

bool exists(const std::string& p) { FILE* f = std::fopen(p.c_str(), "rb"); if (!f) return false; std::fclose(f); return true; }

std::string exe_folder()
{
#ifdef _WIN32
    char p[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameA(NULL, p, sizeof p);
    if (!n || n >= sizeof p) return "";
    std::string s(p, n);
    const size_t sl = s.find_last_of("\\/");
    return sl == std::string::npos ? "" : s.substr(0, sl);
#else
    return "";
#endif
}
}  // namespace

extern "C" int jsp_setting_gen(void) { return g_gen.load(std::memory_order_relaxed); }

// 1 = a value is known (an in-game change, else the environment), copied to out; 0 = unset (out is empty)
extern "C" int jsp_setting_get(const char* key, char* out, int n)
{
    if (n > 0) out[0] = 0;
    { std::lock_guard<std::mutex> lk(g_mu);
      const auto it = g_live.find(key);
      if (it != g_live.end()) { std::snprintf(out, (size_t)n, "%s", it->second.c_str()); return 1; } }
    const char* e = std::getenv(key);
    if (!e) return 0;
    std::snprintf(out, (size_t)n, "%s", e);
    return 1;
}

// An in-game change of a live setting: the readers see it at their next call. "" = back to unset.
extern "C" void jsp_setting_set_live(const char* key, const char* value)
{
    { std::lock_guard<std::mutex> lk(g_mu);
      if (value && *value) g_live[key] = value; else g_live.erase(key); }
    // other getenv readers of the same name (and a child process) see the new value too; "" removes it on Windows
#ifdef _WIN32
    _putenv_s(key, value ? value : "");
#else
    if (value && *value) setenv(key, value, 1); else unsetenv(key);
#endif
    g_gen.fetch_add(1, std::memory_order_relaxed);
}

namespace jps {

std::string io_path()
{
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_path_done) return g_path;
    g_path_done = true;
    if (const char* e = std::getenv("JSP_SETTINGS_FILE")) if (*e) { g_path = e; return g_path; }
    const std::string exe = exe_folder();
    std::string cand[3];
    if (!exe.empty()) {
        const size_t sl = exe.find_last_of("\\/");
        if (sl != std::string::npos && sl > 0) cand[0] = exe.substr(0, sl) + "\\play_settings.txt";
        cand[1] = exe + "\\play_settings.txt";
    }
    std::string app;
    if (const char* a = std::getenv("APPDATA")) if (*a) app = std::string(a) + "\\JSP Fan Revival";
    if (!app.empty()) cand[2] = app + "\\play_settings.txt";
    for (const auto& c : cand) if (!c.empty() && exists(c)) { g_path = c; return g_path; }
    g_path = cand[2];   // none yet: the AppData one (may be empty without %APPDATA%)
    return g_path;
}

// false: the file exists but cannot be read (note says why). A missing file is fine (text stays empty).
bool io_load(std::string& text, std::string& note)
{
    text.clear(); note.clear();
    const std::string p = io_path();
    if (p.empty()) { note = "No settings file is known (no JSP_SETTINGS_FILE and no %APPDATA%)."; return false; }
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return true;
    char b[4096]; size_t n;
    while ((n = std::fread(b, 1, sizeof b, f)) > 0) {
        text.append(b, n);
        if (text.size() > 65536) { std::fclose(f); text.clear(); note = "The settings file is larger than 64 KB: left alone."; return false; }
    }
    std::fclose(f);
    return true;
}

bool io_save(const std::string& text, std::string& err)
{
    err.clear();
    const std::string p = io_path();
    if (p.empty()) { err = "No place to save the settings."; return false; }
    if (text.size() > 65536) { err = "The settings would be larger than 64 KB: not saved."; return false; }
    const size_t sl = p.find_last_of("\\/");
    if (sl != std::string::npos) {
        const std::string dir = p.substr(0, sl);
        if (!dir.empty() && !exists(p)) {   // the AppData folder may not exist yet
#ifdef _WIN32
            _mkdir(dir.c_str());
#else
            mkdir(dir.c_str(), 0755);
#endif
        }
    }
#ifdef _WIN32
    const std::string tmp = p + ".tmp" + std::to_string((unsigned long)GetCurrentProcessId());
#else
    const std::string tmp = p + ".tmp" + std::to_string((long)getpid());
#endif
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) { err = "Could not save the settings (the folder is not writable)."; return false; }
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size() && std::fflush(f) == 0;
    ok = (std::fclose(f) == 0) && ok;
#ifdef _WIN32
    ok = ok && MoveFileExA(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
    ok = ok && std::rename(tmp.c_str(), p.c_str()) == 0;
#endif
    if (!ok) { std::remove(tmp.c_str()); err = "Could not save the settings."; return false; }
    return true;
}

}  // namespace jps
