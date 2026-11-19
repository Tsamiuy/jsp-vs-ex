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

enum Netcode { NC_CONFLICT = -1, NC_OFF = 0, NC_DELAY = 1, NC_ROLLBACK = 2 };

// Row ids of the Online page. 0..5 are the page's older rows (jsp_port_menu.cpp: method, server, name, preset, save, discard).
enum : int { R_METHOD = 0, R_SERVER = 1, R_NAME = 2, R_PRESET = 3, R_SAVE = 4, R_DISCARD = 5, R_NETCODE = 6, R_ROLE = 7, R_PORT = 8, R_JOIN = 9, R_CONN = 10, R_RELAY = 11 };

static const char* const kKeyOn = "JSP_ROLLBACK";
static const char* const kKeyListen = "JSP_ROLLBACK_LISTEN";
static const char* const kKeyJoin = "JSP_ROLLBACK_JOIN";
static const char* const kKeyVia = "JSP_ROLLBACK_VIA";       // "repsn": the session is arranged through a re-psn room (jsp_net_repsn.h); absent or "direct": direct IP (LISTEN / JOIN)
static const char* const kKeyRole = "JSP_ROLLBACK_ROLE";     // re-psn only: "host" (create the room) or "join"
static const char* const kKeyRelay = "JSP_ROLLBACK_RELAY";   // re-psn only: 1 = the player allows the server's relay when a direct path fails
// The real psnr transport (PsnrClientTransport over the re-psn v0.8.0 client, jsp_net_repsn_client.h) is wired into jsp_net_game.cpp: a re-psn start is allowed.
static const bool kRepsnWired = true;
static const char* const kKeyRefused = "JSP_NET_REFUSED";   // set to 1 by resolve_env whenever it refused a rollback start (both methods on, or no valid role key)
static const char* const kDefaultPort = "36200";   // ASSUMPTION: any free UDP port will do; 36200 is only the suggested default

// the same test as jsp_net_game.cpp env_on(): set, not empty, first character not '0'
inline bool flag_on(const char* v) { return v && *v && v[0] != '0'; }

inline const char* name_of(int nc)
{
    return nc == NC_OFF ? "Off" : nc == NC_DELAY ? "Delay (the game's own online)" : nc == NC_ROLLBACK ? "Rollback (experimental)" : "Conflict";
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
};

// what the saved files hold: method = the backend's saved method (0 off, 1 re-psn, 2 community)
inline int saved_netcode(const jps::File& f, int method)
{
    std::string v;
    const bool rb = f.get(kKeyOn, v) && flag_on(v.c_str());
    if (rb && method != 0) return NC_CONFLICT;
    return rb ? NC_ROLLBACK : method != 0 ? NC_DELAY : NC_OFF;
}
// what this process started with (get = std::getenv or a fake): both set is the refused state
template <class Get> int running_netcode(Get get)
{
    // the runtime's own tests (cellNetCtl.c netctl_online, np_psnr.c np_psnr_enabled): ANY value of PS3_NET_ONLINE, even "0", is on; JSP_ROLLBACK is jsp_net_game.cpp's flag_on
    if (flag_on(get(kKeyRefused))) return NC_CONFLICT;   // a refused pair (resolve_env) cleared both: the page still tells why nothing runs
    const bool rb = flag_on(get(kKeyOn)), delay = get("PS3_NET_ONLINE") != nullptr;
    if (rb && delay) return NC_CONFLICT;
    return rb ? NC_ROLLBACK : delay ? NC_DELAY : NC_OFF;
}

// the rows of the page for the working choice (Online page row ids), top to bottom
inline std::vector<int> rows_for(int nc, bool host, bool repsn = false)
{
    std::vector<int> r{ R_NETCODE };
    if (nc == NC_DELAY) { r.push_back(R_METHOD); r.push_back(R_SERVER); r.push_back(R_NAME); r.push_back(R_PRESET); }
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
    return nc == NC_ROLLBACK ? "Rollback replaces Delay: the game's own online is off once saved."
         : nc == NC_DELAY ? "Delay is the game's own online: Rollback is off once saved."
         : "Online play is off. Only one netcode can be on at a time.";
}

// "This session: X. Saved: Y." and whether a restart is needed (the saved choice differs from what this process started with)
inline std::string status_line(int running, int saved, bool* restart)
{
    auto nm = [](int n) { return n == NC_CONFLICT ? "refused" : n == NC_ROLLBACK ? "Rollback" : n == NC_DELAY ? "Delay" : "Off"; };
    if (restart) *restart = running != saved;
    return std::string("This session: ") + nm(running) + ".  Saved: " + nm(saved) + (running != saved ? ".  Restart to apply." : ".");
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
};
enum class Boot { NotRequested, Go, Refused };

// ENHANCEMENT (display only, default on): why a rollback start did not happen, for the on-screen notice (jsp_port_menu.cpp draws it on the main menu's top list).
// The first block is decided by resolve_env before main(); the second by jsp_net_game_install after the lifted table is registered (its ": off" log lines).
enum class Refusal : int {
    None = 0,
    BothOn,           // JSP_ROLLBACK=1 and the game's own online (PS3_NET_ONLINE) both on: neither runs
    RepsnNoRole,      // JSP_ROLLBACK_VIA=repsn without JSP_ROLLBACK_ROLE=host or join
    BadListen,        // JSP_ROLLBACK_LISTEN is not a port from 1 to 65535
    RoleKeys,         // direct IP: not exactly one of JSP_ROLLBACK_LISTEN / JSP_ROLLBACK_JOIN
    BadJoin,          // JSP_ROLLBACK_JOIN is not IP:PORT
    NotWired,         // a build without the re-psn transport
    RepsnNoIdentity,  // over re-psn: repsn.ini has no valid server= or name=
    RepsnLongName,    // over re-psn: the online name is longer than 16 characters
    NoSocket,         // the UDP socket could not be opened
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
        else if (src == SRC_REPSN_INI) put("repsn.ini has enabled=1 (the game's own online).", "Set enabled=0 in repsn.ini to use rollback (keep server= and name=).");
        else put("the game's own online (PS3_NET_ONLINE) is on too.", "Turn the game's own online off to use rollback. Only one online method can be on.");
        break;
    case Refusal::RepsnNoRole: put("JSP_ROLLBACK_VIA=repsn needs JSP_ROLLBACK_ROLE=host or join.", "Add the line to play_settings.txt, or pick a Role in Port Settings > Online."); break;
    case Refusal::BadListen: put("JSP_ROLLBACK_LISTEN is not a port from 1 to 65535.", "Fix the line in play_settings.txt, or set the Port in Port Settings > Online."); break;
    case Refusal::RoleKeys: put("set exactly one of JSP_ROLLBACK_LISTEN (host) or JSP_ROLLBACK_JOIN (join).", "Fix play_settings.txt, or pick a Role in Port Settings > Online."); break;
    case Refusal::BadJoin: put("JSP_ROLLBACK_JOIN is not IP:PORT (for example 192.0.2.10:36200).", "Fix the line in play_settings.txt, or set the Host address in Port Settings > Online."); break;
    case Refusal::NotWired: put("this build cannot reach re-psn rooms.", "Use a direct IP connection (JSP_ROLLBACK_LISTEN or JSP_ROLLBACK_JOIN)."); break;
    case Refusal::RepsnNoIdentity: put("repsn.ini has no valid server= or name=.", "Set both in repsn.ini (keep enabled=0 for rollback), or on Port Settings > Online."); break;
    case Refusal::RepsnLongName: put("the online name in repsn.ini is longer than 16 characters.", "Use a shorter name= in repsn.ini."); break;
    case Refusal::NoSocket: put("the UDP port could not be opened.", "Close the program that uses it, or pick another JSP_ROLLBACK_LISTEN port."); break;
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

// ONE place decides whether a rollback start happens, and it runs after the online files were applied (so PS3_NET_ONLINE is final):
//   - JSP_ROLLBACK off: nothing.
//   - the game's own online on (PS3_NET_ONLINE set to anything, the runtime's test) too: REFUSED, neither runs (one multiplayer method at a time):
//     JSP_ROLLBACK is set to 0, PS3_NET_ONLINE removed, JSP_NET_REFUSED=1 for the page.
//   - not exactly one valid role key (LISTEN port or JOIN IP:port): REFUSED the same way for Rollback only (the game's own online is not on, nothing else changes).
//   - else Go: the defaults above are set, `own` collects the names this call set.
// Get: const char*(const char*); Set: void(const char* key, const char* value or nullptr = remove).
template <class Get, class Set>
Boot resolve_env(Get get, Set set, std::vector<std::string>* own, std::string* why, Refusal* code = nullptr)
{
    if (code) *code = Refusal::None;
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
    if (get("PS3_NET_ONLINE") != nullptr)
        return refuse(Refusal::BothOn, "JSP_ROLLBACK=1 and the game's own online (re-psn or a community server) are both on; only one multiplayer method may be active, neither runs");
    const char* l = get(kKeyListen); const char* j = get(kKeyJoin);
    const bool hl = l && *l, hj = j && *j;
    const char* via = get(kKeyVia);
    if (via && jps::ieq(via, "repsn")) {
        const char* role = get(kKeyRole);
        if (!role || !(jps::ieq(role, "host") || jps::ieq(role, "join"))) return refuse(Refusal::RepsnNoRole, "JSP_ROLLBACK_VIA=repsn needs JSP_ROLLBACK_ROLE=host or join: rollback off");
        if (hl && !valid_port(l)) return refuse(Refusal::BadListen, "JSP_ROLLBACK_LISTEN is not a port from 1 to 65535: rollback off");   // optional over re-psn (0 = any port when absent)
        if (!kRepsnWired) return refuse(Refusal::NotWired, "JSP_ROLLBACK_VIA=repsn: the re-psn transport is not wired in this build: rollback off");
        goto defaults;
    }
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
