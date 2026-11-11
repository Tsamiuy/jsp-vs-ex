/* jsp_online.cpp: ENHANCEMENT: reads the online settings files at startup and sets the environment the
 * runtime's np_psnr already uses (PS3_NET_ONLINE, PSNR_SERVER, PS3_NP_ONLINE_ID, PS3_NET_P2P_PORT), so the player never
 * sets environment variables. File format: see docs/ONLINE.md.
 *
 * Hook: a static constructor in this file, run before main(), hence before the first np_psnr_* call (np_psnr_setup runs
 * lazily from np_psnr_identity(); the runtime's getenv cache keeps the first answer, so the env must be final before it).
 * The port's main() does not call np_psnr_setup itself, so no toolkit patch is needed.
 *
 * Files (untrusted input: 64 KB per file, 512 bytes per line, only known keys, nothing executed):
 *   shared folder: PS3RECOMP_SHARED_DIR, else <install folder>\shared when jsp-install.ini [install] portable=1, else
 *                  %APPDATA%\PS3Recomp. The reader never creates it; the in-game page (edit_save) does when it saves, as the launcher does.
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
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

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

/* What the page in the game shows (jsp_port_menu.cpp): a snapshot of what apply() read and decided at start.
 * Read only (editing works on its own working copy, EditState, below). method: 0 off, 1 re-psn, 2 community (not supported yet), 3 both enabled (refused). */
struct View {
    int files = 0, rejected = 0, rep_enabled = 0, com_enabled = 0, online = 0, presets = 0, method = 0, valid = 0;
    char server[160] = "", name[64] = "", active[64] = "", via[24] = "file", name_via[24] = "file";   /* via/name_via: where the effective method+server / name came from: "--psnr flag", "environment", "--username flag", "file" */
};
static View g_view;

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
    const char* via = "file", *nvia = "file";

    if (flag_online) {
        if (valid_hostport(psnr_flag)) {
            env.set("PS3_NET_ONLINE", "1"); env.set("PSNR_SERVER", with_port(psnr_flag).c_str());
            env.log("[online] --psnr " + psnr_flag + " given: wins over the settings files");
            online = 1; via = "--psnr flag";
        } else env.log("[online] --psnr value is not host[:port], ignored");
    } else if (env_online) {
        env.log("[online] PSNR_SERVER is set in the environment: wins over the settings files");
        online = 1; via = "environment";
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
        env.set("PS3_NP_ONLINE_ID", user_flag.c_str()); nvia = "--username flag";
        env.log("[online] --username given: wins over the settings files");
    } else if (has(env, "PS3_NP_ONLINE_ID")) {
        env.log("[online] PS3_NP_ONLINE_ID is set in the environment: wins over the settings files"); nvia = "environment";
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
    {   /* snapshot for the in-game page */
        View& v = g_view;
        v.valid = 1; v.files = st.files; v.rejected = st.rejected; v.rep_enabled = st.rep_enabled; v.com_enabled = st.com_enabled;
        v.online = online; v.presets = (int)st.presets.size();
        /* effective values: what apply() exported, with where each came from */
        v.method = via[0] != 'f' ? 1 : (st.rep_enabled && st.com_enabled) ? 3 : st.com_enabled ? 2 : st.rep_enabled ? 1 : 0;
        std::string eff_server = st.server;
        if (!strcmp(via, "--psnr flag")) eff_server = with_port(psnr_flag);
        else if (!strcmp(via, "environment")) { const char* e = env.get("PSNR_SERVER"); eff_server = e ? e : ""; }
        std::string eff_name = st.name;
        if (nvia[0] == '-') eff_name = user_flag;
        else if (nvia[0] == 'e') { const char* e = env.get("PS3_NP_ONLINE_ID"); eff_name = e ? e : ""; }
        snprintf(v.server, sizeof v.server, "%s", eff_server.c_str());
        snprintf(v.name, sizeof v.name, "%s", eff_name.c_str());
        snprintf(v.active, sizeof v.active, "%s", st.active.c_str());
        snprintf(v.via, sizeof v.via, "%s", via);
        snprintf(v.name_via, sizeof v.name_via, "%s", nvia);
    }
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
    // The installer layout keeps jsp-install.ini in the install folder and the exe in its game\ subfolder, so the
    // install root is the exe folder or its parent, whichever holds an ini saying portable=1 (the settings window's rule).
    std::string root = exe_dir;
    bool portable = !exe_dir.empty() && is_portable_ini(join(exe_dir, "jsp-install.ini"));
    if (!portable && !exe_dir.empty()) {
        size_t sl = exe_dir.find_last_of("\\/");
        if (sl != std::string::npos && sl > 0 && is_portable_ini(join(exe_dir.substr(0, sl), "jsp-install.ini"))) { portable = true; root = exe_dir.substr(0, sl); }
    }
    const char* ov = env.get("PS3RECOMP_SHARED_DIR");
    if (ov && *ov) shared = ov;
    else if (portable) shared = join(root, "shared");
    else shared = appdata.empty() ? "" : join(appdata, "PS3Recomp");
    game = portable ? root : (appdata.empty() ? "" : join(appdata, "JSP Fan Revival"));
}

/* ---- Editing: what the in-game page writes. Same files, same keys and same format as the launcher's Online tab
 * (JStarsLauncher.cs OnlineIni / SaveOnline): only the keys owned here change, every other line (comments, unknown keys, presets)
 * stays as it was; CRLF line ends; at most 64 KB / 512-byte lines or the file is neither read nor overwritten; a temp file beside the
 * target is flushed and then renamed over it (MoveFileEx with REPLACE_EXISTING | WRITE_THROUGH on Windows, rename(2) elsewhere); the side
 * being switched off goes to disk first, so the files never both say enabled=1. The values are checked with the reader's own
 * validators (valid_hostport, valid_name), so a file written here is never one the reader would reject. Folder: the reader's shared folder.
 * Presets are not edited here (the launcher owns that); the page only picks the active one. */
struct IniDoc {
    struct Sec { std::string name; bool named = false; std::vector<std::string> lines; };
    std::vector<Sec> secs;
    std::string path;
    bool exists = false;

    /* "" when read (or absent), else why the file cannot be used. */
    std::string load(const std::string& p)
    {
        path = p; secs.clear(); secs.push_back(Sec()); exists = false;
        FILE* f = fopen(p.c_str(), "rb");
        if (!f) return "";
        std::vector<char> buf(kMaxFile + 1);
        size_t n = fread(buf.data(), 1, buf.size(), f);
        fclose(f);
        std::string fn = p.substr(p.find_last_of("\\/") == std::string::npos ? 0 : p.find_last_of("\\/") + 1);
        if (n > kMaxFile) return fn + " is larger than 64 KB; not read.";
        exists = true;
        std::string text(buf.data(), n);
        if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t e = text.find('\n', pos); bool last = e == std::string::npos;
            if (last) e = text.size();
            std::string line = text.substr(pos, e - pos);
            while (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.size() > kMaxLine) return fn + " has a line longer than 512 bytes; not read.";
            std::string t = trim(line);
            if (t.size() > 2 && t[0] == '[' && t.back() == ']') { Sec s; s.named = true; s.name = trim(t.substr(1, t.size() - 2)); secs.push_back(s); }
            secs.back().lines.push_back(line);
            if (last) break;
            pos = e + 1;
        }
        if (!secs.back().lines.empty() && secs.back().lines.back().empty()) secs.back().lines.pop_back();   /* trailing newline */
        return "";
    }
    Sec* find(const std::string& name) { for (auto& s : secs) if (s.named && lower(s.name) == lower(name)) return &s; return nullptr; }
    static bool key_line(const std::string& line, const std::string& key)
    {
        std::string t = trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#' || t[0] == '[') return false;
        size_t eq = t.find('=');
        return eq != std::string::npos && eq > 0 && lower(trim(t.substr(0, eq))) == lower(key);
    }
    bool get(const std::string& sec, const std::string& key, std::string& out)
    {
        Sec* s = find(sec); bool got = false;
        if (!s) return false;
        for (auto& l : s->lines) if (key_line(l, key)) { out = trim(l.substr(l.find('=') + 1)); got = true; }   /* the last one wins */
        return got;
    }
    void set(const std::string& sec, const std::string& key, const std::string& val)
    {
        Sec* s = find(sec);
        if (!s) { Sec n; n.named = true; n.name = sec; n.lines.push_back("[" + sec + "]"); secs.push_back(n); s = &secs.back(); }
        for (size_t i = s->lines.size(); i-- > 0;) if (key_line(s->lines[i], key)) { s->lines[i] = key + "=" + val; return; }
        size_t at = s->lines.size();
        while (at > 1 && trim(s->lines[at - 1]).empty()) at--;   /* before the blank lines that end the section */
        s->lines.insert(s->lines.begin() + at, key + "=" + val);
    }
    void preamble(const std::string& comment) { if (!exists && secs[0].lines.empty()) secs[0].lines.push_back(comment); }
    std::vector<std::string> preset_labels()
    {
        std::vector<std::string> r;
        for (auto& s : secs) if (s.named && lower(s.name).compare(0, 7, "preset ") == 0) r.push_back(trim(s.name.substr(7)));
        return r;
    }
    std::string render() const { std::string o; for (auto& s : secs) for (auto& l : s.lines) o += l + "\r\n"; return o; }
    /* "" when saved, else an error text; on failure the old file stays as it was. */
    std::string save()
    {
        std::string text = render();
        std::string fn = path.substr(path.find_last_of("\\/") == std::string::npos ? 0 : path.find_last_of("\\/") + 1);
        if (text.size() > kMaxFile) return fn + " would be larger than 64 KB; not saved.";
        size_t sl = path.find_last_of("\\/");
        if (sl != std::string::npos) {   /* create the folder (every level) */
            std::string d = path.substr(0, sl);
            for (size_t i = 1; i <= d.size(); i++)
                if (i == d.size() || d[i] == '/' || d[i] == '\\') {
                    std::string part = d.substr(0, i);
#ifdef _WIN32
                    _mkdir(part.c_str());
#else
                    mkdir(part.c_str(), 0700);
#endif
                }
        }
#ifdef _WIN32
        std::string tmp = path + ".tmp" + std::to_string((unsigned long)GetCurrentProcessId());
#else
        std::string tmp = path + ".tmp" + std::to_string((long)getpid());
#endif
        FILE* f = fopen(tmp.c_str(), "wb");
        if (!f) return "Cannot save " + fn + ": cannot create the temp file.";
        bool ok = fwrite(text.data(), 1, text.size(), f) == text.size() && fflush(f) == 0;
#ifndef _WIN32
        ok = ok && fsync(fileno(f)) == 0;
#endif
        ok = (fclose(f) == 0) && ok;
#ifdef _WIN32
        ok = ok && MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
        ok = ok && rename(tmp.c_str(), path.c_str()) == 0;
#endif
        if (!ok) { remove(tmp.c_str()); return "Cannot save " + fn + "."; }
        exists = true;
        return "";
    }
};

/* The page's working copy. rep_enabled / com_enabled are the two files' switches; the page's Method keeps at most one of them set. */
struct EditState {
    int rep_enabled = 0, com_enabled = 0;
    std::string server, name, active, note;
    std::vector<std::string> presets;   /* labels in file order */
    bool loaded = false;
};
static const size_t kMaxPresets = 64;

static std::string repsn_path(const std::string& dir) { return join(dir, "repsn.ini"); }
static std::string community_path(const std::string& dir) { return join(dir, "community.ini"); }

/* Reads both files into s (what the launcher's LoadOnline shows). "" or an error text. */
static std::string edit_load(const std::string& dir, EditState& s)
{
    s = EditState();
    if (dir.empty()) return "No folder for the settings files (no %APPDATA%).";
    IniDoc ri, ci;
    std::string e = ri.load(repsn_path(dir)); if (!e.empty()) return e;
    e = ci.load(community_path(dir)); if (!e.empty()) return e;
    std::string v;
    ri.get("repsn", "enabled", v); s.rep_enabled = v == "1";
    v.clear(); ci.get("community", "enabled", v); s.com_enabled = v == "1";
    if (s.rep_enabled && s.com_enabled) { s.note = "Both were switched on; saving turns one off."; s.rep_enabled = s.com_enabled = 0; }
    ri.get("repsn", "server", s.server); ri.get("repsn", "name", s.name);
    for (auto& l : ci.preset_labels()) if (valid_label(l) && s.presets.size() < kMaxPresets) s.presets.push_back(l);
    ci.get("community", "active", s.active);
    s.loaded = true;
    return "";
}

/* "" when s can be saved, else the player-visible reason (the launcher's wording). Both methods on is refused here too. */
static std::string edit_check(const EditState& s)
{
    if (s.rep_enabled && s.com_enabled) return "re-psn and community cannot both be on; pick one method.";
    if (s.rep_enabled) {
        if (!valid_hostport(s.server)) return "re-psn: the server is host or host:port, port 1 to 65535.";
        if (!valid_name(s.name)) return "re-psn: the online name is 3 to 16 of A-Z a-z 0-9 _ -, starting with a letter.";
    }
    if (s.com_enabled) {
        bool found = false;
        for (auto& p : s.presets) if (lower(p) == lower(s.active)) found = true;
        if (!found) return "Community server: pick a preset first (the launcher adds presets).";
    }
    return "";
}

/* Writes both files (SaveOnline of the launcher). "" when saved, else why not; nothing is written when edit_check refuses. */
static std::string edit_save(const std::string& dir, const EditState& s)
{
    std::string why = edit_check(s);
    if (!why.empty()) return why;
    if (dir.empty()) return "No folder for the settings files (no %APPDATA%).";
    IniDoc ri, ci;
    std::string e = ri.load(repsn_path(dir)); if (!e.empty()) return e;
    e = ci.load(community_path(dir)); if (!e.empty()) return e;
    const int m = s.rep_enabled ? 1 : s.com_enabled ? 2 : 0;
    const bool writeR = ri.exists || m == 1, writeC = ci.exists || m == 2;
    if (writeR) {
        ri.preamble("; re-psn server settings. Written by the launcher; the game reads it.");
        ri.set("repsn", "enabled", m == 1 ? "1" : "0");
        if (m == 1 || (valid_hostport(s.server) && valid_name(s.name))) { ri.set("repsn", "server", s.server); ri.set("repsn", "name", s.name); }
    }
    if (writeC) {
        ci.preamble("; Community server settings. Features that exist only in this port do not work on a community server.");
        ci.set("community", "enabled", m == 2 ? "1" : "0");
        ci.set("community", "active", s.active);
    }
    std::string err;
    if (m == 1) { if (writeC) err = ci.save(); if (err.empty() && writeR) err = ri.save(); }   /* the side switched off goes first */
    else { if (writeR) err = ri.save(); if (err.empty() && writeC) err = ci.save(); }
    return err;
}

static std::string g_shared;   /* the folder resolve_dirs chose at start (set by jsp_online_boot) */
static EditState g_es;

}  // namespace jsp_online

/* Text lines for the in-game page, read only: the settings read at start, one line per call (line 0, 1, ...).
 * Returns the number of lines left to ask for after this one (0 = this was the last), or -1 when `line` is out of range
 * or nothing was read yet. ASCII only (the page's font). */
extern "C" int jsp_online_describe(int line, char* out, int n)
{
    using namespace jsp_online;
    const View& v = g_view;
    if (!v.valid || line < 0 || line > 5 || n < 2) return -1;
    static const char* const methods[] = {"Off", "re-psn", "Community (not supported yet)", "REFUSED: both enabled"};
    switch (line) {
    case 0: snprintf(out, n, "Method: %s (%s)", methods[v.method], v.via); break;
    case 1: snprintf(out, n, "Server: %s (%s)", v.server[0] ? v.server : "(none)", v.via); break;
    case 2: snprintf(out, n, "Online name: %s (%s)", v.name[0] ? v.name : "(none)", v.name_via); break;
    case 3: snprintf(out, n, "Community preset: %s (%d saved)", v.active[0] ? v.active : "(none)", v.presets); break;
    case 4: snprintf(out, n, "Settings files: %d read, %d rejected", v.files, v.rejected); break;
    default: snprintf(out, n, "Online play this session: %s", v.online ? "yes" : "no"); break;
    }
    return 5 - line;
}

/* ---- the page's editing API (jsp_port_menu.cpp). Fields: 0 method, 1 server, 2 online name, 3 community preset. ---- */
static void put(char* out, int n, const std::string& s) { if (n > 0) snprintf(out, (size_t)n, "%s", s.c_str()); }

extern "C" int jsp_online_edit_begin(char* err, int n)   /* 0 loaded, -1 not (reason in err) */
{
    using namespace jsp_online;
    std::string e = edit_load(g_shared, g_es);
    put(err, n, e.empty() ? g_es.note : e);
    return e.empty() ? 0 : -1;
}

extern "C" int jsp_online_edit_row(int field, char* out, int n)   /* display text of rows 0..3, -1 otherwise */
{
    using namespace jsp_online;
    const EditState& s = g_es;
    switch (field) {
    case 0: put(out, n, std::string("Method: ") + (s.rep_enabled ? "re-psn" : s.com_enabled ? "Community server" : "Off")); return 0;
    case 1: put(out, n, "Server: " + (s.server.empty() ? std::string("(not set)") : s.server)); return 0;
    case 2: put(out, n, "Online name: " + (s.name.empty() ? std::string("(not set)") : s.name)); return 0;
    case 3: put(out, n, "Community preset: " + (s.presets.empty() ? std::string("(none saved)") : s.active.empty() ? std::string("(none picked)") : s.active)); return 0;
    }
    return -1;
}

extern "C" int jsp_online_edit_get(int field, char* out, int n)   /* the text of field 1 / 2, -1 otherwise */
{
    using namespace jsp_online;
    if (field == 1) put(out, n, g_es.server); else if (field == 2) put(out, n, g_es.name); else return -1;
    return 0;
}

extern "C" int jsp_online_edit_set(int field, const char* v, char* hint, int n)   /* stores the text; 0 valid, 1 not yet valid (reason in hint) */
{
    using namespace jsp_online;
    std::string t = v ? v : "";
    if (field == 1) { g_es.server = t; if (!t.empty() && !valid_hostport(t)) { put(hint, n, "Not a valid server: host or host:port, port 1 to 65535."); return 1; } }
    else if (field == 2) { g_es.name = t; if (!t.empty() && !valid_name(t)) { put(hint, n, "Not a valid name: 3 to 16 of A-Z a-z 0-9 _ -, starting with a letter."); return 1; } }
    put(hint, n, "");
    return 0;
}

extern "C" void jsp_online_edit_cycle(int field, int dir)   /* method (0): Off, re-psn, Community; preset (3): through the saved ones; wraps */
{
    using namespace jsp_online;
    EditState& s = g_es;
    if (field == 0) {
        int m = s.rep_enabled ? 1 : s.com_enabled ? 2 : 0;
        m = ((m + (dir < 0 ? -1 : 1)) % 3 + 3) % 3;
        s.rep_enabled = m == 1; s.com_enabled = m == 2;
    } else if (field == 3 && !s.presets.empty()) {
        int k = -1, cnt = (int)s.presets.size();
        for (int i = 0; i < cnt; i++) if (lower(s.presets[i]) == lower(s.active)) k = i;
        k = k < 0 ? (dir < 0 ? cnt - 1 : 0) : ((k + (dir < 0 ? -1 : 1)) % cnt + cnt) % cnt;
        s.active = s.presets[k];
    }
}

extern "C" int jsp_online_edit_save(char* msg, int n)   /* 1 saved (msg: the confirmation), 0 refused or failed (msg: why); then the working copy is read again */
{
    using namespace jsp_online;
    std::string e = edit_save(g_shared, g_es);
    if (!e.empty()) { put(msg, n, e); return 0; }
    EditState back = g_es;
    edit_load(g_shared, g_es);
    if (!g_es.loaded) g_es = back;
    put(msg, n, "Saved. Applies on the next start.");
    return 1;
}

#ifndef JSP_ONLINE_TEST
#ifdef _WIN32
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
    g_shared = shared;   /* the in-game page writes where the reader reads */
    apply(env, shared, game, psnr, user);
}
namespace { struct JspOnlineInit { JspOnlineInit() { jsp_online_boot(); } } s_jsp_online_init; }
#endif
