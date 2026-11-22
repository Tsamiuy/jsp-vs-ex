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
enum : int { R_METHOD = 0, R_SERVER = 1, R_NAME = 2, R_PRESET = 3, R_SAVE = 4, R_DISCARD = 5, R_NETCODE = 6, R_ROLE = 7, R_PORT = 8, R_JOIN = 9, R_CONN = 10, R_RELAY = 11 };

static const char* const kKeyOn = "JSP_ROLLBACK";
static const char* const kKeyListen = "JSP_ROLLBACK_LISTEN";
static const char* const kKeyJoin = "JSP_ROLLBACK_JOIN";
static const char* const kKeyVia = "JSP_ROLLBACK_VIA";       // "repsn": the session is arranged through a re-psn room (jsp_net_repsn.h); absent or "direct": direct IP (LISTEN / JOIN)
static const char* const kKeyRole = "JSP_ROLLBACK_ROLE";     // re-psn only: "host" (create the room) or "join"
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

inline const char* name_of(int nc)
{
    return nc == NC_OFF ? "Off" : nc == NC_DELAY ? "Delay (the game's own online)" : nc == NC_ROLLBACK ? "Rollback (experimental)"
         : nc == NC_NATIVE ? "Online Battle + rollback" : "Conflict";
}

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

// The Rollback working copy of the page.
struct Rollback {
    bool host = true;
    std::string port = kDefaultPort;   // host: the UDP port to bind
    std::string join;                  // client: IP:PORT of the host
    bool repsn = false, relay = false;  // Connection: re-psn rendezvous (host = create the room, !host = join one) and "allow relay"
    bool both_set = false;             // the file held LISTEN and JOIN (the game refuses that); Save keeps only the chosen role

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
        if (repsn && f.get(kKeyRole, v)) host = !ieq_(v, "join");
        relay = f.get(kKeyRelay, v) && flag_on(v.c_str());
    }
    static bool ieq_(const std::string& a, const char* b) { return jps::ieq(a, b); }
    // "" when it can be saved, else the player-visible reason
    std::string check() const
    {
        if (repsn) return "";   // the server and online name come from repsn.ini (checked by the page)
        if (host) return valid_port(port) ? "" : "Rollback host: the port is a number from 1 to 65535.";
        return valid_join(join) ? "" : "Rollback join: the host address is IP:port, for example 192.0.2.10:36200.";
    }
    // Writes the keys. enable: JSP_ROLLBACK=1 and only the chosen role's key. Otherwise JSP_ROLLBACK=0 is written when the file already has the line or
    // `zero` says a variable of this name is inherited (the launcher's file line wins over an inherited variable, so a removed line could leave it on); else the
    // line stays absent. The remembered port / address stay (inert without JSP_ROLLBACK=1). "0" is off for every reader (flag_on, jsp_net_game.cpp env_on).
    void store(jps::File& f, bool enable, bool zero = false) const
    {
        if (!enable) { std::string v; if (zero || f.get(kKeyOn, v)) f.set(kKeyOn, "0"); return; }
        f.set(kKeyOn, "1");
        if (repsn) {
            f.set(kKeyVia, "repsn"); f.set(kKeyRole, host ? "host" : "join"); f.set(kKeyRelay, relay ? "1" : "0");
            f.erase(kKeyListen); f.erase(kKeyJoin);
            return;
        }
        f.erase(kKeyVia); f.erase(kKeyRole); f.erase(kKeyRelay);
        if (host) { f.set(kKeyListen, port); f.erase(kKeyJoin); }
        else { f.set(kKeyJoin, join); f.erase(kKeyListen); }
    }
    // NC_NATIVE: JSP_ROLLBACK=1 with no role key (the lobby start; the game's own room pairs the players) and the relay choice. LISTEN, JOIN, VIA and ROLE are
    // removed: a role key with the game's own online on is refused at start.
    void store_native(jps::File& f) const
    {
        f.set(kKeyOn, "1"); f.set(kKeyRelay, relay ? "1" : "0");
        f.erase(kKeyVia); f.erase(kKeyRole); f.erase(kKeyListen); f.erase(kKeyJoin);
    }
};

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
inline std::vector<int> rows_for(int nc, bool host, bool repsn = false)
{
    std::vector<int> r{ R_NETCODE };
    if (nc == NC_DELAY) { r.push_back(R_METHOD); r.push_back(R_SERVER); r.push_back(R_NAME); r.push_back(R_PRESET); }
    else if (nc == NC_NATIVE) { r.push_back(R_SERVER); r.push_back(R_NAME); r.push_back(R_RELAY); }   // always re-psn (the community method has no client)
    else if (nc == NC_ROLLBACK) {
        r.push_back(R_CONN); r.push_back(R_ROLE);
        if (repsn) r.push_back(R_RELAY); else r.push_back(host ? R_PORT : R_JOIN);
    }
    r.push_back(R_SAVE); r.push_back(R_DISCARD);
    return r;
}

inline std::string row_text(int row, int nc, const Rollback& rb)
{
    switch (row) {
    case R_NETCODE: return std::string("Netcode: ") + name_of(nc);
    case R_CONN: return std::string("Connection: ") + (rb.repsn ? (kRepsnWired ? "re-psn" : "re-psn (not wired yet)") : "Direct IP");
    case R_ROLE: return std::string("Role: ") + (rb.repsn ? (rb.host ? "Host (create a room)" : "Join (oldest room)")
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
    return nc == NC_NATIVE ? "Online Battle > Friendly Match (2 vs 2) on the online server; rollback runs the fight."
         : nc == NC_ROLLBACK ? "Rollback replaces Delay: the game's own online is off once saved."
         : nc == NC_DELAY ? "Delay is the game's own online: Rollback is off once saved."
         : "Online play is off. Only one netcode can be on at a time.";
}

// "This session: X. Saved: Y." and whether a restart is needed (the saved choice differs from what this process started with)
inline std::string status_line(int running, int saved, bool* restart)
{
    auto nm = [](int n) { return n == NC_CONFLICT ? "refused" : n == NC_NATIVE ? "Online Battle + rollback" : n == NC_ROLLBACK ? "Rollback" : n == NC_DELAY ? "Delay" : "Off"; };
    if (restart) *restart = running != saved;
    return std::string("This session: ") + nm(running) + ".  Saved: " + nm(saved) + (running != saved ? ".  Restart to apply." : ".");
}
// The same, when a gameplay mod set turned the game's own online off for this session (kKeyModsOff): with Delay saved, a restart would not
// turn it on while the mods are on, so the line names the cause instead of "Restart to apply".
inline std::string status_line(int running, int saved, bool* restart, bool mods_off)
{
    if (mods_off && saved == NC_DELAY) {
        if (restart) *restart = false;
        return "This session: off (gameplay mods are on); turn the mods off to play online.";
    }
    return status_line(running, saved, restart);
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
    // PSO_ASYNC is no longer set here (the backend's default, on, applies; PSO_ASYNC=0 still turns it off). History: with the asynchronous fragment-program
    // builds on, every depth-2 bench run on a cold pipeline cache (two runs, 2026-11-21) ended in heap corruption (0xC0000374 in D3DCompile's free on
    // vp_fp_async_worker), so rollback set PSO_ASYNC=0. The writer was found: d3dcompiler_47's blob AddRef/Release are not interlocked and the async workers
    // Released the drawing thread's vertex-shader blob (toolkit patch 2072 gives each job its own copy). Re-runs no longer queue builds either (draws skipped,
    // JSP_STEP_SKIP_DRAWS below). With PSO_ASYNC=0 a cold program was built on the drawing thread inside the tick's FIFO drain (100-400 ms each, patch 2057),
    // and the PC still building held both PCs. Async builds touch no guest memory (the decompile stays on the drawing thread); a draw whose pipeline is not
    // ready is skipped, host-side only, as in a re-run.
    // Measured 2026-11-22 (two copies, joiner cache cold, link 29,72,10, 3 runs each way): host boundary waits 51 (5.4 s) -> 0, host seconds under 29.5 fps
    // 32 -> 6-9, audio concealed gaps 66 -> 2-5, 0 differing in all runs.
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
    RepsnNoRole,      // JSP_ROLLBACK_VIA=repsn without JSP_ROLLBACK_ROLE=host or join
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
        else put("the game's own online (PS3_NET_ONLINE) is on too.", "Turn the game's own online off to use rollback. Only one online method can be on.");
        break;
    case Refusal::RepsnNoRole: put("JSP_ROLLBACK_VIA=repsn needs JSP_ROLLBACK_ROLE=host or join.", "Add JSP_ROLLBACK_ROLE=host or join to play_settings.txt, or Port Settings > Online."); break;
    case Refusal::BadListen: put("JSP_ROLLBACK_LISTEN is not a port from 1 to 65535.", "Set JSP_ROLLBACK_LISTEN=36200 (any free port) in play_settings.txt, or Port Settings > Online."); break;
    case Refusal::RoleKeys: put("set at most one of JSP_ROLLBACK_LISTEN (host) or JSP_ROLLBACK_JOIN (join).", "Keep only one of the two lines in play_settings.txt, or Options > Port Settings > Online."); break;
    case Refusal::BadJoin: put("JSP_ROLLBACK_JOIN is not IP:PORT (for example 192.0.2.10:36200).", "Set JSP_ROLLBACK_JOIN=IP:PORT of the host in play_settings.txt, or Port Settings > Online."); break;
    case Refusal::NotWired: put("this build cannot reach re-psn rooms.", "Use a direct IP connection (JSP_ROLLBACK_LISTEN or JSP_ROLLBACK_JOIN)."); break;
    case Refusal::RepsnNoIdentity: put("repsn.ini has no valid server= or name=.", "Set server= and name= in repsn.ini (keep enabled=0 for rollback), or Port Settings > Online."); break;
    case Refusal::RepsnLongName: put("the online name is longer than 16 characters.", "Pick a shorter online name: settings window, Online tab (or name= in repsn.ini)."); break;
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
        if (!role || !(jps::ieq(role, "host") || jps::ieq(role, "join"))) return refuse(Refusal::RepsnNoRole, "JSP_ROLLBACK_VIA=repsn needs JSP_ROLLBACK_ROLE=host or join: rollback off");
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
