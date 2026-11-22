// jsp_net_online.h -- ENHANCEMENT (in-game netcode option): the logic of the in-game Online page's "Netcode" choice: Off / Delay / Rollback.
// Header only, no OS calls, no game memory: installer/tools/net_online_test.cpp runs it on the host.
//
//   Delay     the game's own online netcode (over re-psn or a community server): the settings of jsp_online.cpp (repsn.ini / community.ini)
//   Rollback  our experimental GekkoNet rollback (jsp_net_game.cpp): JSP_ROLLBACK=1 plus JSP_ROLLBACK_LISTEN=PORT (host) or JSP_ROLLBACK_JOIN=IP:PORT (join)
//             in play_settings.txt (the launcher turns every line into an environment variable; env vars set by hand work the same way)
//
// One multiplayer method at a time (the game's own online and the rollback path must not both run): Netcode is ONE choice, so the page never writes both; Save writes the method that is
// switched OFF first, so a failed or interrupted save never leaves both on. A hand-edited pair (JSP_ROLLBACK=1 and the game's online both on) shows as
// Off with a message and is refused at start (jsp_net_game.cpp): the page never picks for the player.
//
// ENHANCEMENT (one online choice): the page's one choice is Off / re-psn (standard) / re-psn (rollback) / Community, the same four as the
// launcher's Online tab. re-psn (standard) and Community are the two methods of Delay (the game's own online); re-psn (rollback) is Rollback over re-psn. Rollback over a direct IP
// address (JSP_ROLLBACK_LISTEN / JSP_ROLLBACK_JOIN) stays: it is the Connection row under re-psn (rollback), marked "advanced", and the choice then reads "Rollback (direct IP)".
//
// Everything here applies at the NEXT START: jsp_net_game.cpp's EnvInit and jsp_online.cpp's boot read the environment before main().
#ifndef JSP_NET_ONLINE_H
#define JSP_NET_ONLINE_H

#include "jsp_settings.h"
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <utility>

namespace jpno {

// NC_NATIVE (ENHANCEMENT, the tester default): the game's own Online Battle menus over re-psn (repsn.ini enabled=1) WITH JSP_ROLLBACK=1 in the lobby start (no role key):
// players meet in a Friendly Match room and our rollback takes over the fight (native-room pairing, jsp_net_game.cpp). JSP_ROLLBACK must be in the process
// environment from the start (jsp_thread_registry.cpp reads it at process attach), so it is a play_settings.txt line: installer/tools/firstrun.py writes it with
// the online defaults and the settings window's Online tab shows and changes it. JSP_ROLLBACK=0 (or no line) with re-psn on is Delay.
enum Netcode { NC_CONFLICT = -1, NC_OFF = 0, NC_DELAY = 1, NC_ROLLBACK = 2, NC_NATIVE = 3 };
static const int kNetcodeChoices = 4;   // the Netcode row cycles Off, Delay, Rollback, Online Battle + rollback

// Row ids of the Online page. 0..5 are the page's older rows (jsp_port_menu.cpp: method, server, name, preset, save, discard).
enum : int { R_METHOD = 0, R_SERVER = 1, R_NAME = 2, R_PRESET = 3, R_SAVE = 4, R_DISCARD = 5, R_NETCODE = 6, R_ROLE = 7, R_PORT = 8, R_JOIN = 9, R_CONN = 10, R_RELAY = 11, R_CODE = 12 };

static const char* const kKeyOn = "JSP_ROLLBACK";
static const char* const kKeyListen = "JSP_ROLLBACK_LISTEN";
static const char* const kKeyJoin = "JSP_ROLLBACK_JOIN";
static const char* const kKeyVia = "JSP_ROLLBACK_VIA";       // "repsn": the session is arranged through a re-psn room (jsp_net_repsn.h); absent or "direct": direct IP (LISTEN / JOIN)
static const char* const kKeyRole = "JSP_ROLLBACK_ROLE";     // re-psn only: absent or "auto" (the default: search first, join a matching room or open one), or the advanced override "host" / "join"
static const char* const kKeyCode = "JSP_ROLLBACK_CODE";     // re-psn only (ENHANCEMENT, match code): 4 to 8 letters or digits, case ignored; only games with the same code meet; absent = none
static const char* const kKeyRelay = "JSP_ROLLBACK_RELAY";   // re-psn only: 1 = the player allows the server's relay when a direct path fails
// ENHANCEMENT (rollback bench, test tooling): JSP_ROLLBACK=1 with JSP_ROLLBACK_BENCH=N (1..8) runs the fight through a LOCAL stress session that rolls back N ticks on every
// tick, with no peer and no socket; it logs the cost (jsp_net_game.cpp, bench_report). It replaces the role keys: with LISTEN, JOIN or VIA also set it is refused.
static const char* const kKeyBench = "JSP_ROLLBACK_BENCH";
inline int bench_depth(const char* v) { return (v && v[0] >= '1' && v[0] <= '8' && !v[1]) ? v[0] - '0' : 0; }
// The real psnr transport (PsnrClientTransport over the re-psn v0.8.0 client, jsp_net_repsn_client.h) is wired into jsp_net_game.cpp: a re-psn start is allowed.
static const bool kRepsnWired = true;
static const char* const kKeyModsOff = "JSP_MODS_ONLINE_OFF";   // set to 1 by jsp_online.cpp when a gameplay mod set turned the game's own online off (ENHANCEMENT, mods)
static const char* const kKeyRefused = "JSP_NET_REFUSED";   // set to 1 by resolve_env whenever it refused a rollback start (both methods on, or no valid role key)
static const char* const kDefaultPort = "36200";   // ASSUMPTION: any free UDP port will do; 36200 is only the suggested default

// the same test as jsp_net_game.cpp env_on(): set, not empty, first character not '0'
inline bool flag_on(const char* v) { return v && *v && v[0] != '0'; }

// The one choice's cycle order (Left / Right on the Netcode row).
enum Mode { M_OFF = 0, M_REPSN = 1, M_REPSN_RB = 2, M_COMMUNITY = 3, M_NATIVE = 4, kModes = 5 };
inline int mode_of(int nc, int method) { return nc == NC_NATIVE ? M_NATIVE : nc == NC_ROLLBACK ? M_REPSN_RB : nc == NC_DELAY ? (method == 2 ? M_COMMUNITY : M_REPSN) : M_OFF; }
// method: the backend's (1 re-psn, 2 community); direct: the rollback connection is a direct IP address
inline const char* name_of(int nc, int method = 1, bool direct = false)
{
    return nc == NC_NATIVE ? "Online Battle + rollback" : nc == NC_OFF ? "Off" : nc == NC_DELAY ? (method == 2 ? "Community" : "re-psn (standard)")
         : nc == NC_ROLLBACK ? (direct ? "Rollback (direct IP)" : "re-psn (rollback)") : "Conflict";
}

// The match code: empty (none) or 4 to 8 of A-Z 0-9 (any letter case; stored upper case). Returns false when not valid; *out gets the upper-case form.
inline bool normalize_code(const std::string& in, std::string* out)
{
    std::string r;
    for (char c : in) { if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A'); if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false; r += c; }
    if (!r.empty() && (r.size() < 4 || r.size() > 8)) return false;
    if (out) *out = r;
    return true;
}

inline bool valid_port(const std::string& p)
{
    if (p.empty() || p.size() > 5) return false;
    for (char c : p) if (c < '0' || c > '9') return false;
    const long v = std::strtol(p.c_str(), nullptr, 10);
    return v >= 1 && v <= 65535;
}
// "a.b.c.d:port" (what parse_endpoint of jsp_net_session.cpp accepts; the port must also be 1..65535 here)
inline bool valid_join(const std::string& s)
{
    const size_t c = s.rfind(':');
    if (c == std::string::npos || !valid_port(s.substr(c + 1))) return false;
    const std::string ip = s.substr(0, c);
    int parts = 0; size_t i = 0;
    while (i <= ip.size()) {
        size_t j = ip.find('.', i); if (j == std::string::npos) j = ip.size();
        const std::string o = ip.substr(i, j - i);
        if (o.empty() || o.size() > 3) return false;
        for (char ch : o) if (ch < '0' || ch > '9') return false;
        if (std::atoi(o.c_str()) > 255) return false;
        parts++;
        if (j == ip.size()) break;
        i = j + 1;
    }
    return parts == 4;
}

// The rollback configuration as one comparable text (connection, role, relay, code, port or address): the saved one against what this process started with decides
// "Restart to apply" together with the method. Over re-psn the LISTEN pin is not part of it (the page never writes it there). The text holds the match code, so it is never printed.
inline std::string rb_signature(bool repsn, bool role_auto, bool host, bool relay, const std::string& code, const std::string& port, const std::string& join)
{
    return repsn ? std::string("repsn;role=") + (role_auto ? "auto" : host ? "host" : "join") + ";relay=" + (relay ? "1" : "0") + ";code=" + code
                 : std::string("direct;") + (host ? "listen=" + port : "join=" + join);
}
// the same from the environment the process started with (std::getenv or a fake), read like Rollback::load and the game do
template <class Get> std::string running_rb_signature(Get get)
{
    const char* v = get(kKeyVia); const char* ro = get(kKeyRole); const char* rl = get(kKeyRelay); const char* cd = get(kKeyCode);
    const char* l = get(kKeyListen); const char* j = get(kKeyJoin);
    const bool hl = l && *l, hj = j && *j;
    if (v && jps::ieq(v, "repsn")) {
        const bool a = !(ro && (jps::ieq(ro, "host") || jps::ieq(ro, "join")));
        std::string code; normalize_code(cd ? cd : "", &code);
        return rb_signature(true, a, !(ro && jps::ieq(ro, "join")), flag_on(rl), code, "", "");
    }
    return rb_signature(false, false, hl || !hj, false, "", hl ? l : "", hj ? j : "");
}

// The Rollback working copy of the page.
struct Rollback {
    bool host = true;
    std::string port = kDefaultPort;   // host: the UDP port to bind
    std::string join;                  // client: IP:PORT of the host
    bool repsn = false, relay = false;  // Connection: re-psn rendezvous (host = create the room, !host = join one) and "allow relay"
    bool role_auto = true;             // re-psn: nobody picks Host or Join (key absent or "auto"); false = the advanced override (host / join below)
    std::string code;                  // re-psn: the match code (upper case), "" = none
    bool both_set = false;             // the file held LISTEN and JOIN (the game refuses that); Save keeps only the chosen role
    std::string signature() const { return rb_signature(repsn, role_auto, host, relay, code, port, join); }

    // the file's saved state (a missing key leaves the default)
    void load(const jps::File& f)
    {
        std::string l, j;
        const bool hl = f.get(kKeyListen, l) && !l.empty(), hj = f.get(kKeyJoin, j) && !j.empty();
        both_set = hl && hj;
        if (hl) port = l;
        if (hj) join = j;
        host = hl || !hj;
        std::string v;
        repsn = f.get(kKeyVia, v) && ieq_(v, "repsn");
        role_auto = true;
        if (repsn && f.get(kKeyRole, v) && (ieq_(v, "host") || ieq_(v, "join"))) { role_auto = false; host = !ieq_(v, "join"); }
        if (f.get(kKeyCode, v)) { if (!normalize_code(v, &code)) code = v; }   // an invalid saved code stays as it is (check() says so)
        relay = f.get(kKeyRelay, v) && flag_on(v.c_str());
    }
    static bool ieq_(const std::string& a, const char* b) { return jps::ieq(a, b); }
    // "" when it can be saved, else the player-visible reason
    std::string check() const
    {
        if (repsn) { std::string c; return normalize_code(code, &c) ? "" : "Match code: 4 to 8 letters or digits (or leave it empty)."; }   // the server and online name come from repsn.ini (checked by the page)
        if (host) return valid_port(port) ? "" : "Rollback host: the port is a number from 1 to 65535.";
        return valid_join(join) ? "" : "Rollback join: the host address is IP:port, for example 192.0.2.10:36200.";
    }
    // Writes the keys. enable: JSP_ROLLBACK=1 and only the chosen role's key. Otherwise JSP_ROLLBACK=0 is written when the file already has the line or
    // `zero` says a variable of this name is inherited (the launcher's file line wins over an inherited variable, so a removed line could leave it on); else the
    // line stays absent. The remembered port / address stay (inert without JSP_ROLLBACK=1). "0" is off for every reader (flag_on, jsp_net_game.cpp env_on).
    void store(jps::File& f, bool enable, bool zero = false) const
    {
        if (!enable) { std::string v; if (zero || f.get(kKeyOn, v)) f.set(kKeyOn, "0"); return; }
        f.set(kKeyOn, "1"); f.erase(kKeyBench);
        if (repsn) {
            std::string c; normalize_code(code, &c);
            f.set(kKeyVia, "repsn"); f.set(kKeyRole, role_auto ? "auto" : host ? "host" : "join"); f.set(kKeyRelay, relay ? "1" : "0");
            if (c.empty()) f.erase(kKeyCode); else f.set(kKeyCode, c);
            f.erase(kKeyListen); f.erase(kKeyJoin);
            return;
        }
        f.erase(kKeyVia); f.erase(kKeyRole); f.erase(kKeyRelay); f.erase(kKeyCode);
        if (host) { f.set(kKeyListen, port); f.erase(kKeyJoin); }
        else { f.set(kKeyJoin, join); f.erase(kKeyListen); }
    }
    // NC_NATIVE: JSP_ROLLBACK=1 with no role key (the lobby start; the game's own room pairs the players) and the relay choice. LISTEN, JOIN, VIA and ROLE are
    // removed: a role key with the game's own online on is refused at start.
    void store_native(jps::File& f) const
    {
        f.set(kKeyOn, "1"); f.set(kKeyRelay, relay ? "1" : "0"); f.erase(kKeyCode); f.erase(kKeyBench);
        f.erase(kKeyVia); f.erase(kKeyRole); f.erase(kKeyListen); f.erase(kKeyJoin);
    }
};

// A rollback ROLE key is set (a start-up session: direct LISTEN / JOIN, VIA=repsn, or the bench). Without one, JSP_ROLLBACK=1 is the lobby start, which may run
// next to the game's own online (NC_NATIVE). Get: const char*(const char*) (std::getenv or a fake).
template <class Get> bool role_keys(Get get)
{
    for (const char* k : { kKeyListen, kKeyJoin, kKeyVia, kKeyBench }) { const char* v = get(k); if (v && *v) return true; }
    return false;
}
inline bool role_keys(const jps::File& f)
{
    std::string v;
    for (const char* k : { kKeyListen, kKeyJoin, kKeyVia, kKeyBench }) if (f.get(k, v) && !v.empty()) return true;
    return false;
}

// what the saved files hold: method = the backend's saved method (0 off, 1 re-psn, 2 community)
// With re-psn (method 1): JSP_ROLLBACK=1 without a role key is NC_NATIVE; with a role key it is the refused pair.
inline int saved_netcode(const jps::File& f, int method)
{
    std::string v;
    const bool rb = f.get(kKeyOn, v) && flag_on(v.c_str());
    if (rb && method == 1 && !role_keys(f)) return NC_NATIVE;
    if (rb && method != 0) return NC_CONFLICT;
    return rb ? NC_ROLLBACK : method != 0 ? NC_DELAY : NC_OFF;
}
// what this process started with (get = std::getenv or a fake): both set is the refused state
template <class Get> int running_netcode(Get get)
{
    // the runtime's own tests (cellNetCtl.c netctl_online, np_psnr.c np_psnr_enabled): ANY value of PS3_NET_ONLINE, even "0", is on; JSP_ROLLBACK is jsp_net_game.cpp's flag_on
    if (flag_on(get(kKeyRefused))) return NC_CONFLICT;   // a refused pair (resolve_env) cleared both: the page still tells why nothing runs
    const bool rb = flag_on(get(kKeyOn)), delay = get("PS3_NET_ONLINE") != nullptr;
    if (rb && delay) return role_keys(get) ? NC_CONFLICT : NC_NATIVE;
    return rb ? NC_ROLLBACK : delay ? NC_DELAY : NC_OFF;
}

// the rows of the page for the working choice (Online page row ids), top to bottom
// method: the backend's (1 re-psn, 2 community). re-psn (standard): Server and Online name; Community: the preset; re-psn (rollback): Connection, Role, Server, Online name, Relay
// (at most 9 rows: the page draws them at a pitch of 30 when there are more than 8, 34 for 8); direct IP rollback: Connection, Role, Port or Host address.
inline std::vector<int> rows_for(int nc, bool host, bool repsn = false, int method = 1, bool role_auto = true)
{
    std::vector<int> r{ R_NETCODE };
    if (nc == NC_NATIVE) { r.push_back(R_SERVER); r.push_back(R_NAME); r.push_back(R_RELAY); }
    else if (nc == NC_DELAY) { if (method == 2) r.push_back(R_PRESET); else { r.push_back(R_SERVER); r.push_back(R_NAME); } }
    else if (nc == NC_ROLLBACK) {
        r.push_back(R_CONN);
        if (repsn) { if (!role_auto) r.push_back(R_ROLE); r.push_back(R_SERVER); r.push_back(R_NAME); r.push_back(R_CODE); r.push_back(R_RELAY); }   // the Role row only shows an advanced override that is set
        else { r.push_back(R_ROLE); r.push_back(host ? R_PORT : R_JOIN); }
    }
    r.push_back(R_SAVE); r.push_back(R_DISCARD);
    return r;
}

inline std::string row_text(int row, int nc, const Rollback& rb, int method = 1)
{
    switch (row) {
    case R_NETCODE: return std::string("Online: ") + name_of(nc, method, !rb.repsn) + (nc == NC_ROLLBACK && (!rb.repsn || !rb.role_auto) ? (rb.host ? ", Host" : ", Join") : "");   // the role, plainly (two Joins never meet)
    case R_CONN: return std::string("Connection: ") + (rb.repsn ? (kRepsnWired ? "re-psn" : "re-psn (not wired yet)") : "Direct IP (advanced)");
    case R_CODE: return "Match code: " + (rb.code.empty() ? std::string("(none)") : rb.code);
    case R_ROLE: return std::string("Role: ") + (rb.repsn ? (rb.role_auto ? "Auto (advanced)" : rb.host ? "Host (advanced)" : "Join (advanced)")
                                                         : (rb.host ? "Host (others join you)" : "Join (a host's address)"));
    case R_RELAY: return std::string("Allow relay: ") + (rb.relay ? "Yes" : "No");
    case R_PORT: return "Port: " + rb.port;
    case R_JOIN: return "Host address: " + (rb.join.empty() ? std::string("(not set)") : rb.join);
    case R_SAVE: return "Save (applies on the next start)";
    default: return "Discard changes";
    }
}

// the one-line statement of the exclusion rule for the working choice (shown under the rows)
inline const char* exclusion_line(int nc)
{
    return nc == NC_NATIVE ? "Use Online Battle > Friendly Match for native rollback."
         : nc == NC_ROLLBACK ? "Rollback matches start from Free Battle; the Online menu is for re-psn (standard)."
         : nc == NC_DELAY ? "One choice: Rollback is off once saved."
         : "Online play is off. Only one online method can be on at a time.";
}

// "This session: X. Saved: Y." and whether a restart is needed (the saved choice differs from what this process started with)
// run_m / saved_m: the game's own online method of each side (1 re-psn, 2 community); saved_direct: the saved rollback is a direct IP one.
// A restart is needed when the one choice differs (the saved choice as a Mode against what this process runs).
inline std::string status_line(int running, int saved, bool* restart, int run_m = 1, int saved_m = 1, bool saved_direct = false, const std::string& run_sig = "", const std::string& saved_sig = "")
{
    auto nm = [](int n, int m, bool d) -> std::string { return n == NC_CONFLICT ? "refused" : name_of(n, m, d); };
    // Rollback saved and running: any difference of the saved configuration (connection, role, relay, port or address) needs a restart too, not only the method
    const bool differ = running != saved || (running == NC_DELAY && run_m != saved_m) || (running == NC_ROLLBACK && run_sig != saved_sig);
    if (restart) *restart = differ;
    return std::string("This session: ") + nm(running, run_m, running == NC_ROLLBACK && run_sig.rfind("direct;", 0) == 0) + ".  Saved: " + nm(saved, saved_m, saved_direct) + (differ ? ".  Restart to apply." : ".");
}
// The same, when a gameplay mod set turned the game's own online off for this session (kKeyModsOff): with Delay saved, a restart would not
// turn it on while the mods are on, so the line names the cause instead of "Restart to apply".
inline std::string status_line(int running, int saved, bool* restart, bool mods_off, int run_m = 1, int saved_m = 1, bool saved_direct = false, const std::string& run_sig = "", const std::string& saved_sig = "")
{
    if (mods_off && (saved == NC_DELAY || saved == NC_NATIVE)) {
        if (restart) *restart = false;
        return "This session: off (gameplay mods are on); turn the mods off to play online.";
    }
    return status_line(running, saved, restart, run_m, saved_m, saved_direct, run_sig, saved_sig);
}

// ---- the start decision (jsp_net_game.cpp EnvInit side; host-tested) -------------------------------------------------------------------------------------------
// The defaults JSP_ROLLBACK=1 gives the snapshot ring, the per-tick fight hash and the turbo stepper. Set only when the decision is Go and the name is unset or empty.
struct KV { const char* k; const char* v; };
static const KV kDefaults[] = {
    {"JSP_FIGHT_HASH", "1000000"}, {"JSP_FIGHT_HASH_START", "1"}, {"JSP_SNAPSHOT", "1"}, {"JSP_SNAPSHOT_EXT", "1"}, {"JSP_SNAPSHOT_WW", "1"},
    {"JSP_SNAPSHOT_WW_DEPTH", "12"}, {"JSP_SNAPSHOT_SUSPEND", "1"},
    {"JSP_STEP_TURBO", "1"},   // turbo: replays run on the virtual clock and are not presented (stepper contract)
    // Quiesce enforced, polled gate, registry threads, job-server gate.
    {"JSP_SNAPSHOT_THREADS", "registry"}, {"JSP_SNAPSHOT_QUIESCE", "2"}, {"JSP_SNAPSHOT_GATE", "poll"}, {"JSP_SNAPSHOT_GATE_JOBSERVER", "1"},
    // The commit gate gives up after 15 s of gate time between two session updates; the frozen-write check stays off.
    {"JSP_SNAPSHOT_SYNC_MS", "15000"}, {"JSP_SNAPSHOT_FROZEN_WRITES", "0"},
    // Save cost (measured with JSP_ROLLBACK_BENCH, docs/ROLLBACK.md): the lwmutex census keeps its page-readability cache while the layout is unchanged;
    // the per-tick layout walk is skipped while the toolkit's layout generation (patch 7920) is unchanged; a frozen sample whose job words block is
    // refused before the full census, with no unfrozen wait; the thread audit's Toolhelp listing runs on its own thread (no 45 ms save every 2 s).
    {"JSP_SNAPSHOT_LWM_CACHE", "keep"}, {"JSP_SNAPSHOT_LAYOUT_GEN", "1"}, {"JSP_SNAPSHOT_QUICK", "1"},
    {"JSP_SNAPSHOT_AUDIT_ASYNC", "1"},
    // 2026-11-21, replay-driven depth 2, same build: before the freeze the save waits UNFROZEN (up to 20 ms) for the job words to go idle, instead of
    // freezing, finding them busy and retrying (each try ~1.75 ms and it halts the jobs it waits for); the frozen layout walk (~2 ms per freeze) runs every
    // 30th gate while the layout generation is unchanged. Together: 13.1 -> 16.1 ticks/s, save 12.9 -> 8.3 ms, restore 16.4 -> 12.7 ms, 0 desyncs.
    {"JSP_SNAPSHOT_QUICK_WAIT_US", "20000"}, {"JSP_SNAPSHOT_FROZEN_WALK_EVERY", "30"},
    // Saves GekkoNet can never load again (frames below its final frame: gekko_final_frame) take no snapshot, only the checksum (jsp_net_glue.h). Host tests:
    // the skip_final_* scenarios (loss, blackout, cheat at exactly 300, replays). Two copies over a 66 ms link: 58% / 8% of the saves skipped, 301 rollbacks.
    {"JSP_ROLLBACK_SKIP_FINAL", "1"},
    // A restore waits (at most JSP_SNAPSHOT_SITE_MS, 50 ms) until every helper thread is parked where it was at the target's save (jsp_snapshot_sync.h, SiteMoved). Soak
    // 2026-11-21 (replay-driven depth 2, ~3,200 restores): a CRI loader worker was caught inside its file read at a restore; 0 desyncs, restore 10.7 ms avg (no cost).
    {"JSP_SNAPSHOT_SITE", "1"},
    // WORKAROUND TO REPLACE (crash, 2026-11-21): with the D3D12 backend's asynchronous fragment-program builds on (PSO_ASYNC, 4 workers), every depth-2 bench
    // run on a cold pipeline cache ended in heap corruption (0xC0000374, detected in D3DCompile's free on vp_fp_async_worker) within 15-60 s; with PSO_ASYNC=0
    // two runs of 14 windows (~200 s) did not. The writer is not found yet. Off in rollback until it is: a new program is built on the drawing thread (a hitch
    // once per program; the pipeline cache on disk removes it the next time).
    {"PSO_ASYNC", "0"},
    // Re-run cost (2026-11-21, two copies, --rollback --link 80,10,10, one 10,000-tick match each): the frames of a rollback re-run are never shown, so the
    // RSX command handler skips their draws, clears and state flushes (patch 7919; labels, reports, flips still run; the D3D12 backend only reads guest memory).
    // Re-run FIFO drain 14-22 -> 0.6-0.8 ms per tick, re-run tick 18.6 -> 10.4 ms (222 rollbacks), 0 differing of 9,863 compared ticks.
    {"JSP_STEP_SKIP_DRAWS", "1"},
};
enum class Boot { NotRequested, Go, Refused };

// ENHANCEMENT (display only, default on): why a rollback start did not happen, for the on-screen notice (jsp_port_menu.cpp draws it on the main menu's top list).
// The first block is decided by resolve_env before main(); the second by jsp_net_game_install after the lifted table is registered (its ": off" log lines).
enum class Refusal : int {
    None = 0,
    BothOn,           // JSP_ROLLBACK=1 and the game's own online (PS3_NET_ONLINE) both on: neither runs
    RepsnNoRole,      // JSP_ROLLBACK_VIA=repsn with a JSP_ROLLBACK_ROLE that is not auto, host or join
    BadListen,        // JSP_ROLLBACK_LISTEN is not a port from 1 to 65535
    RoleKeys,         // direct IP: both JSP_ROLLBACK_LISTEN and JSP_ROLLBACK_JOIN (neither is the lobby start, see resolve_env); a bench with a role key or a bad depth
    BadJoin,          // JSP_ROLLBACK_JOIN is not IP:PORT
    NotWired,         // a build without the re-psn transport
    RepsnNoIdentity,  // over re-psn: repsn.ini has no valid server= or name=
    RepsnLongName,    // over re-psn: the online name is longer than 16 characters
    NoSocket,         // the UDP socket could not be opened
    // FIX (region): not a US 1.00 disc (jsp_native_fight_supported() == 0): the Online Battle + rollback takeover is off and the game's own netcode plays.
    // jsp_net_native_fight.cpp turns native-room pairing off itself; jsp_net_game.cpp sets this code for the notice (note_off) when it reads that.
    NativeUsOnly,
    BadCode,          // over re-psn: JSP_ROLLBACK_CODE is not 4 to 8 letters or digits
    Count
};
// Where the game's own online came from (jsp_online.cpp's start-up reading), for the BothOn text.
enum OnlineSource : int { SRC_OTHER = 0, SRC_REPSN_INI = 1, SRC_PSNR_FLAG = 2, SRC_ENVIRONMENT = 3 };

struct Notice { std::string line1, line2; bool any() const { return !line1.empty(); } };

// The player-facing text: what is wrong (line 1) and the fix (line 2). ASCII only (the overlay font). Empty for None or an unknown code.
inline Notice notice_text(Refusal r, int src = SRC_REPSN_INI)
{
    Notice n;
    auto put = [&](const char* a, const char* b) { n.line1 = std::string("Rollback is off: ") + a; n.line2 = b; };
    switch (r) {
    case Refusal::BothOn:
        if (src == SRC_PSNR_FLAG) put("the --psnr launch option turns on the game's own online.", "Remove --psnr to use rollback. Only one online method can be on.");
        else if (src == SRC_ENVIRONMENT) put("PSNR_SERVER is set (the game's own online).", "Remove PSNR_SERVER to use rollback. Only one online method can be on.");
        else if (src == SRC_REPSN_INI) put("repsn.ini has enabled=1 (the game's own online) and a rollback role key is set.", "Remove JSP_ROLLBACK_LISTEN/JOIN/VIA for Online Battle + rollback, or set enabled=0 in repsn.ini.");
        else put("the game's own online is on and a rollback role key is set.", "Turn the game's own online off to use custom rollback.");
        break;
    case Refusal::RepsnNoRole: put("JSP_ROLLBACK_ROLE is not auto, host or join.", "Use JSP_ROLLBACK_ROLE=auto (or no line) in play_settings.txt, or Port Settings > Online."); break;
    case Refusal::BadCode: put("JSP_ROLLBACK_CODE is not 4 to 8 letters or digits.", "Fix or remove JSP_ROLLBACK_CODE in play_settings.txt, or Port Settings > Online."); break;
    case Refusal::BadListen: put("JSP_ROLLBACK_LISTEN is not a port from 1 to 65535.", "Set JSP_ROLLBACK_LISTEN=36200 (any free port) in play_settings.txt, or Port Settings > Online."); break;
    case Refusal::RoleKeys: put("set at most one of JSP_ROLLBACK_LISTEN (host) or JSP_ROLLBACK_JOIN (join).", "Keep only one of the two lines in play_settings.txt, or Options > Port Settings > Online."); break;
    case Refusal::BadJoin: put("JSP_ROLLBACK_JOIN is not IP:PORT (for example 192.0.2.10:36200).", "Set JSP_ROLLBACK_JOIN=IP:PORT of the host in play_settings.txt, or Port Settings > Online."); break;
    case Refusal::NotWired: put("this build cannot reach re-psn rooms.", "Use a direct IP connection (JSP_ROLLBACK_LISTEN or JSP_ROLLBACK_JOIN)."); break;
    case Refusal::RepsnNoIdentity: put("re-psn (rollback) needs a valid server= and name= in repsn.ini.", "Set server= and name= in repsn.ini (keep enabled=0 for rollback), or Port Settings > Online."); break;
    case Refusal::RepsnLongName: put("the online name for re-psn (rollback) is longer than 16 characters.", "Use a shorter name= in repsn.ini, or Port Settings > Online."); break;
    case Refusal::NoSocket: put("the UDP port could not be opened.", "Close the program that uses it, or pick another JSP_ROLLBACK_LISTEN port."); break;
    case Refusal::NativeUsOnly: put("rollback needs the US disc (BLUS31519).", "Online Battle still works on this disc, with the game's own netcode."); break;
    default: break;
    }
    return n;
}

// How long the notice stays each time the main menu's top list comes up (it comes back the next time the top list is up).
static constexpr uint64_t kNoticeMs = 15000;
// Shown: the top list is up and it came up less than kNoticeMs ago. up_since_ms = when the top list last came up.
inline bool notice_visible(bool have_notice, bool top_up, uint64_t up_since_ms, uint64_t now_ms)
{
    return have_notice && top_up && now_ms - up_since_ms < kNoticeMs;
}

// FIX (an F9 tap cancelled rollback at fight start; recording hotkeys use F9): the rollback cancel key (jsp_net_game.cpp) counts only once held
// kCancelHoldMs, in every phase (waiting, armed and running). since: when the key went down (0 = up; the caller keeps it).
static constexpr uint64_t kCancelHoldMs = 1500;
inline bool cancel_held(bool down, uint64_t now_ms, uint64_t& since)
{
    if (!down) { since = 0; return false; }
    if (!since) since = now_ms;
    return now_ms - since > kCancelHoldMs;
}

// ONE place decides whether a rollback start happens, and it runs after the online files were applied (so PS3_NET_ONLINE is final):
//   - JSP_ROLLBACK off: nothing.
//   - the game's own online on (PS3_NET_ONLINE set to anything, the runtime's test) AND a role key: REFUSED, neither runs (one multiplayer method at a time):
//     JSP_ROLLBACK is set to 0, PS3_NET_ONLINE removed, JSP_NET_REFUSED=1 for the page.
//   - an invalid role (both LISTEN and JOIN, a bad port or address, VIA=repsn without ROLE): REFUSED the same way for Rollback only (the game's own online is
//     not on, nothing else changes).
//   - ENHANCEMENT (lobby): NO role key at all (no LISTEN, no JOIN, no VIA=repsn, no BENCH): Go with *lobby = true: rollback-capable (the defaults are set and the hooks
//     are installed, exactly as for a match start) but no session yet; the in-game lobby opens one at runtime (jsp_net_lobby.h). Before the lobby this was the
//     RoleKeys refusal. The game's own online may be on too (NC_NATIVE: its Online Battle room pairs the players, jsp_net_game.cpp native_poll).
//   - else Go: the defaults above are set, `own` collects the names this call set.
// Get: const char*(const char*); Set: void(const char* key, const char* value or nullptr = remove).
template <class Get, class Set>
Boot resolve_env(Get get, Set set, std::vector<std::string>* own, std::string* why, Refusal* code = nullptr, bool* lobby = nullptr)
{
    if (code) *code = Refusal::None;
    if (lobby) *lobby = false;
    if (!flag_on(get(kKeyOn))) return Boot::NotRequested;
    auto refuse = [&](Refusal c, const char* w) {
        const bool conflict = c == Refusal::BothOn;
        if (code) *code = c;
        if (why) *why = w;
        set(kKeyOn, "0");
        set(kKeyRefused, "1");   // every refusal: JSP_ROLLBACK is 0 for this process now, so the page needs this to know the file or launcher still holds a non-zero value
        if (conflict) set("PS3_NET_ONLINE", nullptr);
        return Boot::Refused;
    };
    const char* l = get(kKeyListen); const char* j = get(kKeyJoin);
    const bool hl = l && *l, hj = j && *j;
    const char* via = get(kKeyVia);
    // ENHANCEMENT (native-room pairing, NC_NATIVE): the lobby start (no role key) runs NEXT to the game's own online: the game's Online Battle room pairs the
    // players and rollback takes over the fight. A start-up role session (a role key) with the game's own online on is still refused: neither runs.
    if (get("PS3_NET_ONLINE") != nullptr && role_keys(get))
        return refuse(Refusal::BothOn, "JSP_ROLLBACK=1 with a role key (LISTEN, JOIN, VIA or BENCH) and the game's own online (re-psn or a community server) are both on; only one multiplayer method may be active, neither runs");
    if (const char* bn = get(kKeyBench); flag_on(bn)) {   // ENHANCEMENT (rollback bench): a local test, no role
        if (hl || hj || (via && *via)) return refuse(Refusal::RoleKeys, "JSP_ROLLBACK_BENCH is a local test without a peer: remove JSP_ROLLBACK_LISTEN, JOIN and VIA: rollback off");
        if (!bench_depth(bn)) return refuse(Refusal::RoleKeys, "JSP_ROLLBACK_BENCH must be 1 to 8 (the ticks rolled back on every tick): rollback off");
        goto defaults;
    }
    if (via && jps::ieq(via, "repsn")) {
        const char* role = get(kKeyRole);
        if (role && *role && !(jps::ieq(role, "auto") || jps::ieq(role, "host") || jps::ieq(role, "join"))) return refuse(Refusal::RepsnNoRole, "JSP_ROLLBACK_ROLE is not auto, host or join: rollback off");   // absent or auto: nobody picks, the games find each other
        const char* code = get(kKeyCode);
        if (code && *code && !normalize_code(code, nullptr)) return refuse(Refusal::BadCode, "JSP_ROLLBACK_CODE is not 4 to 8 letters or digits: rollback off");   // the value is not logged
        if (hl && !valid_port(l)) return refuse(Refusal::BadListen, "JSP_ROLLBACK_LISTEN is not a port from 1 to 65535: rollback off");   // optional over re-psn (0 = any port when absent)
        if (!kRepsnWired) return refuse(Refusal::NotWired, "JSP_ROLLBACK_VIA=repsn: the re-psn transport is not wired in this build: rollback off");
        goto defaults;
    }
    if (!hl && !hj) { if (lobby) *lobby = true; goto defaults; }   // ENHANCEMENT (lobby): no role key: rollback-capable, the session is opened at runtime
    if (hl == hj) return refuse(Refusal::RoleKeys, "JSP_ROLLBACK=1 needs exactly one of JSP_ROLLBACK_LISTEN=PORT (host) or JSP_ROLLBACK_JOIN=IP:PORT (client): rollback off");
    if (hl ? !valid_port(l) : !valid_join(j)) return refuse(hl ? Refusal::BadListen : Refusal::BadJoin, hl ? "JSP_ROLLBACK_LISTEN is not a port from 1 to 65535: rollback off" : "JSP_ROLLBACK_JOIN is not IP:PORT: rollback off");
defaults:
    for (const KV& e : kDefaults) {
        const char* c = get(e.k);
        if (!c || !*c) { set(e.k, e.v); if (own) own->push_back(e.k); }
    }
    return Boot::Go;
}

// The decision needs two static constructors to have run: jsp_online.cpp's boot (sets PS3_NET_ONLINE from the files and --psnr) and jsp_net_game.cpp's EnvInit. Their order
// across translation units is unspecified, so each reports in and the LAST one to arrive decides, once. part: 1 = EnvInit, 2 = the online boot. Returns true on the call that decided.
struct Rendezvous {
    unsigned seen = 0; bool decided = false; int order[2] = { 0, 0 };
    template <class Decide> bool arrive(int part, Decide decide)
    {
        if (part < 1 || part > 2 || (seen & (1u << part))) return false;
        order[__builtin_popcount(seen)] = part;
        seen |= 1u << part;
        if (seen == 6u && !decided) { decided = true; decide(); return true; }
        return false;
    }
};

}  // namespace jpno

#endif
