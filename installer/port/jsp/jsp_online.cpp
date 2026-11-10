/* jsp_online.cpp: ENHANCEMENT: reads the online settings files at startup and sets the environment the
 * runtime's np_psnr already uses (PS3_NET_ONLINE, PSNR_SERVER, PS3_NP_ONLINE_ID, PS3_NET_P2P_PORT), so the player never
 * sets environment variables. File format: see docs/ONLINE.md.
 *
 * Hook: a static constructor in this file, run before main(), hence before the first np_psnr_* call (np_psnr_setup runs
 * lazily from np_psnr_identity(); the runtime's getenv cache keeps the first answer, so the env must be final before it).
 * The port's main() does not call np_psnr_setup itself, so no toolkit patch is needed.
 *
 * Files (untrusted input: 64 KB per file, 512 bytes per line, only known keys, nothing executed):
 *   shared folder: PS3RECOMP_SHARED_DIR, else <exe folder>\shared when jsp-install.ini [install] portable=1, else
 *                  %APPDATA%\PS3Recomp. Never created here.
 *     repsn.ini      [repsn] enabled=0|1, server=host[:port], name=ONLINEID
 *     community.ini  [community] enabled=0|1, active=PRESET ; [preset NAME] address=, name=
 *   per-game folder: <exe folder> when portable, else %APPDATA%\JSP Fan Revival (where play_settings.txt and hdd0_gamedata live)
 *     repsn.ini      [repsn] comm_id, p2p_port, report_rtt (+ may override enabled/server/name)
 * Shared is read first, the game's own second; a later key wins, presets merge by name. repsn.ini never holds a
 * [preset] and community.ini never holds server/name keys of repsn: rejected and logged.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>
#include <vector>
#include <functional>

namespace jsp_online {

struct Env {
    std::function<const char*(const char*)> get;
    std::function<void(const char*, const char*)> set;
    std::function<void(const std::string&)> log;
};

struct Settings {
    int rep_enabled = 0;               /* repsn enabled */
    std::string server, name, comm_id, p2p_port, report_rtt;
    int com_enabled = 0;
    std::string active;
    std::map<std::string, std::map<std::string, std::string>> presets;   /* name -> {address,name} */
    int rejected = 0;
    int files = 0;
};

static const size_t kMaxFile = 64 * 1024, kMaxLine = 512;

static std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
    return s.substr(a, b - a);
}

static std::string lower(std::string s) { for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 32; return s; }

/* host[:port] -> host:port, 36100 when no port (re-psn and community alike). Caller validated. */
static std::string with_port(const std::string& s) { return s.find(':') == std::string::npos ? s + ":36100" : s; }

static bool valid_label(const std::string& n)
{
    if (n.empty() || n.size() > 32) return false;
    for (char ch : n) if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return false;
    return true;
}

static bool valid_name(const std::string& n)
{
    if (n.size() < 3 || n.size() > 16) return false;
    for (size_t i = 0; i < n.size(); i++) {
        char c = n[i];
        bool al = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        bool ok = al || (i && ((c >= '0' && c <= '9') || c == '_' || c == '-'));
        if (!ok) return false;
    }
    return true;
}

/* host[:port], port 1..65535, host of letters/digits/./-/_ (no IPv6 literals). */
static bool valid_hostport(const std::string& s)
{
    std::string host = s;
    size_t c = s.find(':');
    if (c != std::string::npos) {
        host = s.substr(0, c);
        std::string p = s.substr(c + 1);
        if (p.empty() || p.size() > 5) return false;
        for (char ch : p) if (ch < '0' || ch > '9') return false;
        int v = atoi(p.c_str());
        if (v < 1 || v > 65535) return false;
    }
    if (host.empty() || host.size() > 253) return false;
    for (char ch : host) {
        bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_';
        if (!ok) return false;
    }
    return true;
}

static bool valid_port(const std::string& p)
{
    if (p.empty() || p.size() > 5) return false;
    for (char ch : p) if (ch < '0' || ch > '9') return false;
    int v = atoi(p.c_str());
    return v >= 1 && v <= 65535;
}

static bool valid_bool(const std::string& v) { return v == "0" || v == "1"; }

static bool valid_comm_id(const std::string& v)
{
    if (v.empty() || v.size() > 16) return false;
    for (char ch : v) if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return false;
    return true;
}

static bool read_file(const std::string& path, std::string& out, const Env& env, bool& exists)
{
    exists = false;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    exists = true;
    std::vector<char> buf(kMaxFile + 1);
    size_t n = fread(buf.data(), 1, buf.size(), f);
    fclose(f);
    if (n > kMaxFile) {
        env.log("[online] " + path + ": larger than 64 KB, file rejected");
        return false;
    }
    out.assign(buf.data(), n);
    return true;
}

enum Kind { REPSN, COMMUNITY };

static void parse(const std::string& path, Kind kind, bool game, Settings& st, const Env& env)
{
    std::string text; bool exists;
    if (!read_file(path, text, env, exists)) { if (exists) st.rejected++; return; }
    st.files++;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);   /* tolerate a UTF-8 BOM */
    std::string sec, preset;
    int sec_kind = 0;    /* 0 none/unknown, 1 repsn, 2 community, 3 preset */
    int rej = 0;
    size_t pos = 0, lineno = 0;
    auto reject = [&](const std::string& why) {
        rej++; st.rejected++;
        env.log("[online] " + path + ":" + std::to_string(lineno) + ": rejected (" + why + ")");
    };
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string raw = text.substr(pos, e - pos);
        pos = e + 1; lineno++;
        if (raw.size() > kMaxLine) { reject("line over 512 bytes"); continue; }
        std::string l = trim(raw);
        if (l.empty() || l[0] == ';' || l[0] == '#') continue;
        if (l[0] == '[') {
            if (l.back() != ']') { reject("bad section line"); sec_kind = 0; continue; }
            sec = trim(l.substr(1, l.size() - 2));
            sec_kind = 0;
            std::string ls = lower(sec);
            if (kind == REPSN && ls == "repsn") sec_kind = 1;
            else if (kind == COMMUNITY && ls == "community") sec_kind = 2;
            else if (ls.compare(0, 7, "preset ") == 0) {
                if (kind == REPSN) reject("[preset] in repsn.ini, presets belong in community.ini");
                else {
                    preset = trim(sec.substr(7));
                    if (!valid_label(preset)) reject("bad preset label, want 1-32 of A-Za-z0-9_-");
                    else sec_kind = 3;
                }
            } else if (kind == REPSN && ls == "community") reject("[community] in repsn.ini");
            else if (kind == COMMUNITY && ls == "repsn") reject("[repsn] in community.ini");
            else reject("unknown section [" + sec + "]");
            continue;
        }
        size_t eq = l.find('=');
        if (eq == std::string::npos) { reject("no '='"); continue; }
        std::string k = lower(trim(l.substr(0, eq))), v = trim(l.substr(eq + 1));
        if (sec_kind == 0) { reject("key outside a known section"); continue; }
        if (sec_kind == 1) {
            if (k == "enabled") { if (valid_bool(v)) st.rep_enabled = v == "1"; else reject("enabled must be 0 or 1"); }
            else if (k == "server") { if (valid_hostport(v)) st.server = with_port(v); else reject("bad server, want host[:port]"); }
            else if (k == "name") { if (valid_name(v)) st.name = v; else reject("bad name, want 3-16 of A-Za-z0-9_- starting with a letter"); }
            else if (game && k == "comm_id") { if (valid_comm_id(v)) st.comm_id = v; else reject("bad comm_id"); }
            else if (game && k == "p2p_port") { if (valid_port(v)) st.p2p_port = v; else reject("bad p2p_port"); }
            else if (game && k == "report_rtt") { if (valid_bool(v)) st.report_rtt = v; else reject("report_rtt must be 0 or 1"); }
            else reject("unknown key '" + k + "'");
        } else if (sec_kind == 2) {
            if (k == "enabled") { if (valid_bool(v)) st.com_enabled = v == "1"; else reject("enabled must be 0 or 1"); }
            else if (k == "active") { if (v.empty() || valid_label(v)) st.active = v; else reject("bad active preset name"); }
            else if (k == "server" || k == "name") reject("repsn key '" + k + "' in community.ini");
            else reject("unknown key '" + k + "'");
        } else {
            if (k == "address") { if (valid_hostport(v)) st.presets[preset]["address"] = with_port(v); else reject("bad address"); }
            else if (k == "name") { if (valid_name(v)) st.presets[preset]["name"] = v; else reject("bad name"); }
            else reject("unknown key '" + k + "'");
        }
    }
    env.log("[online] " + path + ": read, " + std::to_string(rej) + " line(s) rejected");
}

static bool has(const Env& e, const char* n) { const char* v = e.get(n); return v && *v; }

static std::string join(const std::string& d, const char* f)
{
    if (d.empty()) return f;
    char last = d.back();
    return d + ((last == '/' || last == '\\') ? "" : "/") + f;
}

/* argv: explicit --psnr/--username (the toolkit harness's flags) win over the files; they are set here because
 * this port's main() does not parse them. Returns 1 when online play ends up configured. */
static int apply(const Env& env, const std::string& shared_dir, const std::string& game_dir,
                 const std::string& psnr_flag, const std::string& user_flag)
{
    Settings st;
    if (!shared_dir.empty()) {
        parse(join(shared_dir, "repsn.ini"), REPSN, false, st, env);
        parse(join(shared_dir, "community.ini"), COMMUNITY, false, st, env);
    }
    if (!game_dir.empty()) parse(join(game_dir, "repsn.ini"), REPSN, true, st, env);
    if (!st.files) env.log("[online] no settings files found (shared \"" + shared_dir + "\", game \"" + game_dir + "\")");

    bool env_online = has(env, "PSNR_SERVER");
    bool flag_online = !psnr_flag.empty();
    int online = 0;

    if (flag_online) {
        if (valid_hostport(psnr_flag)) {
            env.set("PS3_NET_ONLINE", "1"); env.set("PSNR_SERVER", with_port(psnr_flag).c_str());
            env.log("[online] --psnr " + psnr_flag + " given: wins over the settings files");
            online = 1;
        } else env.log("[online] --psnr value is not host[:port], ignored");
    } else if (env_online) {
        env.log("[online] PSNR_SERVER is set in the environment: wins over the settings files");
        online = 1;
    } else if (st.rep_enabled && st.com_enabled) {
        env.log("[online] ERROR: re-psn and community are both enabled; online play refused, enable one method only "
                "(repsn.ini [repsn] enabled / community.ini [community] enabled)");
        /* ASSUMPTION: no player-visible message facility exists in the port yet; the log line is the only report. */
    } else if (st.com_enabled) {
        std::string a = st.active.empty() ? "(none)" : st.active;
        env.log("[online] community method enabled (preset " + a + "): not supported yet, staying offline "
                "(ENHANCEMENT pending: no client for a community PSN-replacement server in this port)");
    } else if (st.rep_enabled) {
        if (st.server.empty()) env.log("[online] repsn enabled but no valid server= given: staying offline");
        else {
            env.set("PS3_NET_ONLINE", "1"); env.set("PSNR_SERVER", st.server.c_str());
            env.log("[online] re-psn enabled: server " + st.server);
            online = 1;
        }
    }

    if (!user_flag.empty()) {
        env.set("PS3_NP_ONLINE_ID", user_flag.c_str());
        env.log("[online] --username given: wins over the settings files");
    } else if (has(env, "PS3_NP_ONLINE_ID")) {
        env.log("[online] PS3_NP_ONLINE_ID is set in the environment: wins over the settings files");
    } else if (online && !st.name.empty()) {
        env.set("PS3_NP_ONLINE_ID", st.name.c_str());
        env.log("[online] online id " + st.name);
    }

    if (has(env, "PS3_NET_P2P_PORT")) {
        if (!st.p2p_port.empty()) env.log("[online] PS3_NET_P2P_PORT is set in the environment: wins over p2p_port");
    } else if (!st.p2p_port.empty()) {
        env.set("PS3_NET_P2P_PORT", st.p2p_port.c_str());
        env.log("[online] p2p port " + st.p2p_port);
    }
    /* ASSUMPTION: the runtime has no reader for comm_id / report_rtt yet; exported under these names for the code that will. */
    if (!st.comm_id.empty() && !has(env, "PSNR_COMM_ID")) env.set("PSNR_COMM_ID", st.comm_id.c_str());
    if (!st.report_rtt.empty() && !has(env, "PSNR_REPORT_RTT")) env.set("PSNR_REPORT_RTT", st.report_rtt.c_str());
    if (st.rejected) env.log("[online] " + std::to_string(st.rejected) + " setting(s) rejected in total");
    return online;
}

static bool is_portable_ini(const std::string& iniPath)
{
    std::string t; bool ex; Env quiet{nullptr, nullptr, [](const std::string&) {}};
    if (!read_file(iniPath, t, quiet, ex)) return false;
    bool in = false; size_t pos = 0;
    while (pos < t.size()) {
        size_t e = t.find('\n', pos); if (e == std::string::npos) e = t.size();
        std::string l = trim(t.substr(pos, e - pos)); pos = e + 1;
        if (!l.empty() && l[0] == '[') { in = (lower(l) == "[install]"); continue; }
        size_t eq = l.find('=');
        if (in && eq != std::string::npos && lower(trim(l.substr(0, eq))) == "portable") return trim(l.substr(eq + 1)) == "1";
    }
    return false;
}

/* Folder resolution. exe_dir: the folder of the exe; appdata: %APPDATA% (may be empty). */
static void resolve_dirs(const Env& env, const std::string& exe_dir, const std::string& appdata,
                         std::string& shared, std::string& game)
{
    bool portable = !exe_dir.empty() && is_portable_ini(join(exe_dir, "jsp-install.ini"));
    const char* ov = env.get("PS3RECOMP_SHARED_DIR");
    if (ov && *ov) shared = ov;
    else if (portable) shared = join(exe_dir, "shared");
    else shared = appdata.empty() ? "" : join(appdata, "PS3Recomp");
    game = portable ? exe_dir : (appdata.empty() ? "" : join(appdata, "JSP Fan Revival"));
}

}  // namespace jsp_online

#ifndef JSP_ONLINE_TEST
#ifdef _WIN32
#include <windows.h>
#include <stdlib.h>
#endif
static void jsp_online_boot(void)
{
    using namespace jsp_online;
    Env env;
    env.get = [](const char* n) { return getenv(n); };
    env.set = [](const char* k, const char* v) {
#ifdef _WIN32
        _putenv_s(k, v);
#else
        setenv(k, v, 1);
#endif
    };
    env.log = [](const std::string& s) { printf("%s\n", s.c_str()); };
    std::string exe_dir, appdata, psnr, user;
#ifdef _WIN32
    char p[MAX_PATH * 2];
    DWORD n = GetModuleFileNameA(NULL, p, sizeof p);
    if (n && n < sizeof p) { exe_dir.assign(p, n); size_t s = exe_dir.find_last_of("\\/"); exe_dir = s == std::string::npos ? "" : exe_dir.substr(0, s); }
    if (const char* a = getenv("APPDATA")) appdata = a;
    for (int i = 1; i < __argc; i++) {
        std::string a = __argv[i];
        std::string* dst = a.compare(0, 6, "--psnr") == 0 ? &psnr : a.compare(0, 10, "--username") == 0 ? &user : nullptr;
        if (!dst) continue;
        size_t eq = a.find('=');
        if (eq != std::string::npos) *dst = a.substr(eq + 1);
        else if (i + 1 < __argc) *dst = __argv[++i];
    }
#endif
    std::string shared, game;
    resolve_dirs(env, exe_dir, appdata, shared, game);
    apply(env, shared, game, psnr, user);
}
namespace { struct JspOnlineInit { JspOnlineInit() { jsp_online_boot(); } } s_jsp_online_init; }
#endif
