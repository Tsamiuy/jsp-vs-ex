// ENHANCEMENT (Discord Rich Presence and Join): the pure parts of jsp_discord.cpp, no Windows and no game state, so a host test can
// check them (installer/tools/discord_test.sh): the presence text from a given state, the join secret, and the join-request rules.
#pragma once
#include "jsp_discord.h"
#include <cstdint>
#include <cstdio>
#include <string>

namespace jdp {

// FIX (review): no built-in Discord application. The presence runs only under the application named by JSP_DISCORD_APP_ID (the
// settings file or the environment; docs/DISCORD.md), which the player or a package configured; unset = Rich Presence off.
enum class AppId { Unset, Invalid, Ok };
inline AppId app_id_setting(const std::string& v)
{
    if (v.empty()) return AppId::Unset;
    if (v.size() > 20) return AppId::Invalid;   // Discord ids are 64-bit numbers: 20 digits at most
    for (char c : v) if (c < '0' || c > '9') return AppId::Invalid;
    return AppId::Ok;
}
constexpr size_t kSecretMax = 128;   // Discord's limit for a join secret
constexpr size_t kTextMax = 128;     // and for details / state

inline const char* char_name(int id)
{
    static const char* const k[42] = {
        "Luffy", "Ace", "Hancock", "Akainu", "Naruto", "Sasuke", "Madara", "Goku", "Frieza", "Vegeta",
        "Yusuke", "Toguro", "Hiei", "Toriko", "Zebra", "Ichigo", "Aizen", "Gon", "Killua", "Jotaro",
        "Dio", "Dai", "Kenshin", "Shishio", "Seiya", "Kenshiro", "Raoh", "Oga & Baby Beel", "Gintoki Sakata", "Medaka",
        "Ryotsu", "Arale & Gatchan", "Koro-sensei", "Saiki", "Momotaro", "Bo-bobo", "Tsuna & Reborn", "Luckyman", "Taro", "Jonathan",
        "Joseph", "Nube"};
    return id >= 0 && id < 42 ? k[id] : nullptr;
}
// The Support characters, ids 200..213 (docs/balance/Support.md; their CharacterName text is id - 200 + 42). 208 (Yoh) is a stub the
// game never offers (13 Supports in the shop for 14 files); named anyway in case a save or a mod puts it in.
inline const char* support_name(int id)
{
    static const char* const k[14] = {
        "Neuro", "Kumagawa", "Rukia", "Chitoge", "Hisoka", "Edajima", "Kagura & Sadaharu", "Jaguar", "Yoh", "Kuroko",
        "SKET Dance", "Lala", "Allen", "Hinata"};
    return id >= 200 && id < 214 ? k[id - 200] : nullptr;
}
inline const char* stage_name(int i)
{
    static const char* const k[12] = {
        "Yoshiwara Paradise", "Kameari Police Station", "Tenraikyousintojo", "Temple of Athena", "Vegetable Sky", "Penguin Village",
        "Planet Namek", "Hidden Leaf Village", "Soul Society", "Dark Tournament Stadium", "Inferno Crags", "Alabasta"};
    return i >= 0 && i < 12 ? k[i] : nullptr;
}

// The online side of the presence, read each update: the runtime's Matching2 room (patch 7924), our rollback, a pending Discord join.
struct Online {
    int m2 = 0;                    // ps3_np_matching2_room: 0 no started Matching2 context, 1 started outside a room (Online Battle menus), 2 in a room
    unsigned long long room = 0;   // the room id (m2 == 2)
    int members = 0, max = 0;      // members in the room, its slots
    int owner = 0;                 // this game owns (created) the room
    std::string peer;              // the other player's online name, "" when alone
    int rollback = 0;              // our GekkoNet rollback runs the fight (jsp_net_presence)
    int joining = 0;               // a Discord join is on its way to a room
};
inline int slots(const Online& o) { return o.max > 0 ? o.max : 2; }

// What happened in the current room: "After the fight" needs to know a fight was seen there.
struct Track { unsigned long long room = 0; bool fought = false; };
inline void note(Track& t, const JspPresenceRaw& r, const Online& o)
{
    if (o.m2 != 2 || !o.room) { t = Track{}; return; }
    if (o.room != t.room) { t.room = o.room; t.fought = false; }
    if (r.fight) t.fought = true;
}

enum class Phase { Offline, OnlineMenu, Joining, Hosting, Room, Select, Fight, After };
inline Phase phase(const JspPresenceRaw& r, const Online& o, const Track& t)
{
    if (o.m2 == 2) {
        if (r.fight) return Phase::Fight;
        if (r.charsel || r.stagesel) return Phase::Select;
        if (t.fought && t.room == o.room) return Phase::After;
        if (o.members < 2 && o.owner) return Phase::Hosting;
        return Phase::Room;
    }
    if (o.joining) return Phase::Joining;
    if (o.m2 == 1 && !r.fight && !r.charsel && !r.stagesel && !r.title) return Phase::OnlineMenu;
    return Phase::Offline;
}

// FIX: cut at a UTF-8 character boundary (an online name may hold any character; half a character is invalid JSON text for Discord).
inline std::string clip(std::string s, size_t n = kTextMax)
{
    if (s.size() <= n) return s;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    s.resize(n);
    return s;
}
// A presence text for the log: the other player's online name replaced by "<opponent>" (online names are shown on the profile, never
// logged), also when the 128-character cut left only the start of it at the end.
inline std::string redact(std::string s, const std::string& peer)
{
    if (peer.empty()) return s;
    static const std::string k = "<opponent>";
    const bool cut = s.size() + 4 >= kTextMax;   // the cut is at kTextMax (or a few bytes before it, at a UTF-8 boundary)
    for (size_t p = s.find(peer); p != std::string::npos; p = s.find(peer, p + k.size())) s.replace(p, peer.size(), k);
    if (cut)
        for (size_t len = peer.size() - 1; len > 0; len--)
            if (s.size() >= len && s.compare(s.size() - len, len, peer, 0, len) == 0) { s.replace(s.size() - len, len, k); break; }
    return s;
}

// "jsp3|<re-psn server>|<room id>": the native Matching2 room on the host's re-psn server; no IP address of a player. "" when it
// cannot be made (no server, no room, a '|' in the server name, or longer than Discord takes).
// jsp1 (a lobby name) and jsp2 (a rollback-only rendezvous room, joined by a restart) are the formats of older builds.
inline std::string encode_secret(const std::string& server, unsigned long long room)
{
    if (server.empty() || !room || server.find('|') != std::string::npos) return {};
    const std::string s = "jsp3|" + server + "|" + std::to_string(room);
    return s.size() <= kSecretMax ? s : std::string();
}

struct JoinReq { unsigned long long room = 0; std::string server; };
enum class Verdict { Join, NotOurs, OldHost, BadRoom, NoServer, WrongServer, AlreadyThere };
inline const char* verdict_text(Verdict v)
{
    switch (v) {
    case Verdict::Join: return "joining";
    case Verdict::NotOurs: return "not a J-Stars room secret";
    case Verdict::OldHost: return "the host's game is an older build (update both games)";
    case Verdict::BadRoom: return "bad room id";
    case Verdict::NoServer: return "no re-psn server / online name set here (Port Settings > Online)";
    case Verdict::WrongServer: return "the host plays on another re-psn server";
    case Verdict::AlreadyThere: return "already in that room";
    }
    return "?";
}
inline bool same_server(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}
// A secret Discord delivered (ACTIVITY_JOIN), checked against this game: its re-psn server and the room it is in now (0 = none).
inline Verdict decode_join(const std::string& secret, const std::string& my_server, unsigned long long my_room, JoinReq* out)
{
    if (secret.compare(0, 5, "jsp1|") == 0 || secret.compare(0, 5, "jsp2|") == 0) return Verdict::OldHost;
    if (secret.compare(0, 5, "jsp3|") != 0 || secret.size() > kSecretMax) return Verdict::NotOurs;
    const size_t b = secret.rfind('|');
    if (b <= 4) return Verdict::NotOurs;
    const std::string srv = secret.substr(5, b - 5), id = secret.substr(b + 1);
    if (srv.empty() || srv.find('|') != std::string::npos) return Verdict::NotOurs;
    if (id.empty() || id.size() > 20 || id.find_first_not_of("0123456789") != std::string::npos) return Verdict::BadRoom;
    unsigned long long room = 0;
    for (char c : id) {
        const unsigned d = (unsigned)(c - '0');
        if (room > (~0ull - d) / 10) return Verdict::BadRoom;
        room = room * 10 + d;
    }
    if (!room) return Verdict::BadRoom;
    if (my_server.empty()) return Verdict::NoServer;
    if (!same_server(srv, my_server)) return Verdict::WrongServer;
    if (my_room == room) return Verdict::AlreadyThere;
    if (out) { out->room = room; out->server = srv; }
    return Verdict::Join;
}

// The host's side of Ask to Join (ACTIVITY_JOIN_REQUEST): accepted at once only while the room has space; otherwise Discord's own prompt.
inline bool auto_accept(bool on, const Online& o) { return on && o.m2 == 2 && o.room && o.members < slots(o); }
// FIX: JSP_DISCORD_AUTO_ACCEPT is off unless "1" (unset: Discord's own prompt asks the host).
inline bool auto_accept_setting(const std::string& v) { return v == "1"; }

// The runtime's join target states (patch 7924: PS3_M2_JOIN_*).
enum { kJoinNone = 0, kJoinPending = 1, kJoinListed = 2, kJoinJoined = 3, kJoinGone = -1, kJoinFull = -2, kJoinFiltered = -3, kJoinFailed = -4 };
inline const char* join_state_text(int st)
{
    switch (st) {
    case kJoinNone: return "none";
    case kJoinPending: return "waiting for a room search";
    case kJoinListed: return "listed by the search";
    case kJoinJoined: return "joined";
    case kJoinGone: return "the room is gone";
    case kJoinFull: return "the room is full";
    case kJoinFiltered: return "the room is not in this search (another mode?)";
    case kJoinFailed: return "the join failed";
    }
    return "?";
}

// One pending Discord join, from the secret to the room. The autopilot takes it once (jsp_discord_take_join); the presence thread
// steps it with the runtime's target state and drops it when it is settled, has failed for good, or is too old.
enum class Step { Idle, Wait, Done, GiveUp };
struct JoinSlot {
    JoinReq req;
    uint64_t since = 0, joined_at = 0;
    uint64_t failed_at = 0;   // FIX: when the target state first turned negative (0 = not failing): the give-up grace runs from there, not from the offer
    bool taken = false;
    bool active() const { return req.room != 0; }
    void offer(const JoinReq& r, uint64_t now) { req = r; since = now; joined_at = 0; failed_at = 0; taken = false; }
    bool take(JoinReq* out)
    {
        if (!active() || taken) return false;
        taken = true;
        if (out) *out = req;
        return true;
    }
    void clear() { *this = JoinSlot{}; }
    Step step(int st, uint64_t now, uint64_t timeout = 180000, uint64_t grace = 20000, uint64_t settle = 15000)
    {
        if (!active()) return Step::Idle;
        if (st == kJoinJoined) {
            if (!joined_at) joined_at = now ? now : 1;
            return now - joined_at >= settle ? Step::Done : Step::Wait;
        }
        joined_at = 0;
        if (st >= 0) failed_at = 0;
        else if (!failed_at) failed_at = now ? now : 1;
        if (st < 0 && now - failed_at >= grace) return Step::GiveUp;   // gone, full, other mode, failed: the autopilot had its retries
        if (now - since >= timeout) return Step::GiveUp;
        return Step::Wait;
    }
};

struct Activity {
    std::string details, state, large, large_text, small, small_text, party_id, join;
    int size = 0, max = 0;
    uint64_t start = 0;   // the elapsed-time start (unix seconds): the fight's start in a fight, 0 = the caller's session start
};

// The name of a fighter (0..41) or a Support (200..213); nullptr for any other id (never shown).
inline const char* unit_name(int id) { const char* n = char_name(id); return n ? n : support_name(id); }

// One team of the fight from the battle units: leader, then partner, joined with " & "; with_support adds " (+Support)".
// "" when no fighter of that team has a known name.
inline std::string team_text(const JspPresenceRaw& r, int team, bool with_support)
{
    std::string t, sup;
    const int n = r.units < 0 ? 0 : r.units > 6 ? 6 : r.units;
    for (int role : {1, 2})
        for (int i = 0; i < n; i++) {
            if (r.u_team[i] != team || r.u_role[i] != role) continue;
            const char* nm = char_name(r.u_chr[i]);   // a fighter slot holds a fighter id; anything else is not shown
            if (!nm) continue;
            if (!t.empty()) t += " & ";
            t += nm;
        }
    for (int i = 0; i < n && with_support; i++)
        if (r.u_team[i] == team && r.u_role[i] == 3) {
            const char* nm = unit_name(r.u_chr[i]);   // Supports: the 200+ ids, or a fighter put in the Support slot
            if (nm) { sup = nm; break; }
        }
    if (!t.empty() && !sup.empty()) t += " (+" + sup + ")";
    return t;
}
// "Luffy & Naruto vs Goku & Ichigo", red (team 0) first, with the Supports when that fits Discord's 128 characters; "" when a team is unknown.
inline std::string matchup(const JspPresenceRaw& r)
{
    for (bool sup : {true, false}) {
        const std::string a = team_text(r, 0, sup), b = team_text(r, 1, sup);
        if (a.empty() || b.empty()) return {};
        const std::string s = a + " vs " + b;
        if (s.size() <= kTextMax) return s;
    }
    return {};
}
// Who is in the fight, as one number (0 = no units): a new line-up starts a new fight for the timer.
inline uint32_t lineup_sig(const JspPresenceRaw& r)
{
    uint32_t h = 2166136261u;
    const int n = r.units < 0 ? 0 : r.units > 6 ? 6 : r.units;
    for (int i = 0; i < n; i++)
        for (int v : {r.u_chr[i], r.u_team[i], r.u_role[i]}) { h ^= (uint32_t)v; h *= 16777619u; }
    return n ? (h ? h : 1u) : 0u;
}

// The fight's elapsed-time start. A fight starts when the HUD comes up after none for `gap` ms (a fight's own cut-ins hide it for less),
// or when the line-up changes; so it resets for every fight. Returns the fight's start (seconds), or 0 outside a fight.
struct FightClock { bool in = false; uint64_t start_s = 0, last_ms = 0; uint32_t sig = 0; };
inline uint64_t fight_start(FightClock& c, bool fight, uint32_t sig, uint64_t now_ms, uint64_t now_s, uint64_t gap = 10000)
{
    if (!fight) {
        if (c.in && now_ms - c.last_ms >= gap) c.in = false;
        return 0;
    }
    if (!c.in || now_ms - c.last_ms >= gap || (sig && c.sig && sig != c.sig)) { c.start_s = now_s; c.sig = 0; }
    c.in = true; c.last_ms = now_ms;
    if (sig) c.sig = sig;
    return c.start_s;
}

// FIX: keep showing the fight while the HUD is gone for less than `hold` ms (and no select screen or title is up): a fight's HUD can
// drop for a moment (rollback catch-up, cut-ins), which made the profile flip to "After the fight" and back.
inline bool hold_fight(const FightClock& c, const JspPresenceRaw& r, uint64_t now_ms, uint64_t hold = 6000)
{
    return !r.fight && c.in && !r.charsel && !r.stagesel && !r.title && now_ms - c.last_ms < hold;
}

// The offline mode: Training, or the main menu entry that was opened (only the tested rows are named).
inline const char* offline_mode(const JspPresenceRaw& r)
{
    if (r.training) return "Training";
    if (r.menu_row == 0) return "J-Adventure";
    if (r.menu_row == 3) return "Free Battle";
    return "Offline battle";
}

// Settings of the presence (play_settings.txt).
struct Options {
    bool char_art = false;   // JSP_DISCORD_CHAR_ART=1: the small picture is the player's fighter, key char_NNN (one uploaded picture per fighter)
};
inline bool char_art_setting(const std::string& v) { return v == "1"; }

// The activity for one update. server: this game's re-psn server (for the join secret), "" when none is set. fight_s: the fight's start
// (fight_start; 0 = none).
inline Activity build(const JspPresenceRaw& r, const Online& o, const Track& t, const std::string& server, const Options& opt = Options(),
                      uint64_t fight_s = 0)
{
    Activity a;
    const Phase ph = phase(r, o, t);
    const char* st = stage_name(r.stage_focus);
    const std::string stage = st ? st : "";
    const std::string vs = o.peer.empty() ? std::string() : "vs " + o.peer;
    if (r.fight) {
        const std::string m = matchup(r);
        a.details = m.empty() ? "In battle" : m;
        std::string mode;
        if (ph == Phase::Fight) mode = "Online Friendly Match" + (vs.empty() ? std::string() : " " + vs) + (o.rollback ? " (rollback)" : "");
        else if (o.rollback) mode = "Online (rollback)";
        else mode = offline_mode(r);
        std::string ko;
        if (r.ko[0] >= 0 && r.ko[1] >= 0 && r.ko[0] <= 20 && r.ko[1] <= 20) ko = std::to_string(r.ko[0]) + "-" + std::to_string(r.ko[1]) + " KOs";
        a.state = clip(mode);
        for (const std::string* extra : {(const std::string*)&ko, &stage})   // as much as fits: the KOs, then the stage
            if (!extra->empty() && a.state.size() + 3 + extra->size() <= kTextMax) a.state += " - " + *extra;
        a.start = fight_s;
    } else {
        switch (ph) {
        case Phase::Select: a.details = "Online character select"; a.state = vs.empty() ? "Friendly Match" : vs; break;
        case Phase::After: a.details = "After the fight"; a.state = vs.empty() ? "Friendly Match" : vs; break;
        case Phase::Hosting: a.details = "Hosting a Friendly Match"; a.state = "Waiting for an opponent"; break;
        case Phase::Room: a.details = "In a Friendly Match room"; a.state = o.peer.empty() ? std::string() : "with " + o.peer; break;
        case Phase::Joining: a.details = "Joining a friend's room"; break;
        case Phase::OnlineMenu: a.details = "Online Battle menu"; break;
        default:
            if (r.charsel) a.details = "Character select";
            else if (r.stagesel) a.details = "Stage select";
            else if (r.title) a.details = "Title screen";
            else if (r.menu) a.details = r.training ? "Training menu" : "Main menu";
            else a.details = "Loading";
            if ((r.charsel || r.stagesel) && !o.rollback) a.state = offline_mode(r);
        }
    }
    a.details = clip(a.details);
    a.state = clip(a.state);
    // ENHANCEMENT (branding): the VS EX logo is the large image on every screen, fights included (no stage_sNN picture); the stage name is in the
    // state text only when jsp_game.cpp knows it for sure (stage_focus -1 otherwise).
    a.large = "logo"; a.large_text = "J-Stars Victory VS EX";
    // FIX: no small picture by default (it needs one uploaded picture per fighter). JSP_DISCORD_CHAR_ART=1: the player's fighter, key char_NNN,
    // offline only (online, which unit is this player's is not known here): in a fight red's first human leader (else red's leader), on the
    // character select player 1's pick.
    if (opt.char_art && o.m2 != 2 && !o.rollback) {
        int id = -1;
        if (r.fight) {
            const int n = r.units < 0 ? 0 : r.units > 6 ? 6 : r.units;
            for (int pass = 0; pass < 2 && id < 0; pass++)
                for (int i = 0; i < n; i++)
                    if (r.u_team[i] == 0 && r.u_role[i] == 1 && (pass || !r.u_cpu[i]) && char_name(r.u_chr[i])) { id = r.u_chr[i]; break; }
        } else if (r.charsel && r.part[0] != 2 && char_name(r.chr[0])) {
            id = r.chr[0];
        }
        if (id >= 0) { char k[16]; std::snprintf(k, sizeof k, "char_%03d", id); a.small = k; a.small_text = char_name(id); }
    }
    if (o.m2 == 2 && o.room) {   // the room is the party: both games show the same id, so Discord groups them
        a.max = slots(o);
        a.size = o.members < 1 ? 1 : o.members > a.max ? a.max : o.members;
        a.party_id = "jsp-room-" + std::to_string(o.room);
        if (a.size < a.max) a.join = encode_secret(server, o.room);
    }
    return a;
}

}  // namespace jdp
