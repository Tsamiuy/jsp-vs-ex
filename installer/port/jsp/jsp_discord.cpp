// ENHANCEMENT (Discord Rich Presence and Join/Invite; FIX (review): off unless JSP_DISCORD_APP_ID=<id> names the Discord application to
// show it under, set by the player or a package (there is no built-in one); JSP_DISCORD=0 turns it off even then).
//
// What it does: a background thread connects to the LOCAL Discord app over its IPC pipe (\\.\pipe\discord-ipc-0..9, the documented RPC
// transport of the Discord client; no network access of our own, no SDK DLL) and keeps the player's activity up to date every 2 s
// (the texts are built in jsp_discord_state.h):
//   offline  the screen (Title screen, Main menu, Character select, Stage select, Training, a fight "Gintoki Sakata & Goku vs Luffy & Naruto")
//   online   Online Battle menu, Hosting a Friendly Match (1/2), In a Friendly Match room, Online character select, the online fight
//            "A & B vs C & D" with "Online vs <the other player's online name>", After the fight. The online state is the runtime's
//            Matching2 room (patch 7924: room id, members, owner, the other member's name), read only; the screens are jsp_game_presence.
//   assets   large image key "logo" (the VS EX logo, every screen); no small image unless JSP_DISCORD_CHAR_ART=1: then key "char_NNN" of
//            the player's fighter, offline (one picture per fighter, uploaded by the application's owner)
//   timer    the elapsed time since the fight started in a fight (reset for every fight), else since the game started
//   fighters FIX: from the fight's own battle units (jsp_game_presence), not the menus' setup records (all zero in the native online fight)
//   party    the room: party id "jsp-room-<room id>" on both games, size 1/2 or 2/2, and while it has space a join secret
//            "jsp3|<re-psn server>|<room id>" (no IP address), so Discord shows Join / Ask to Join and the chat "+" Invite.
// Join (ACTIVITY_JOIN on the friend's game): the secret is checked (same re-psn server, not the room we are in), the runtime's room search
// is pointed at that room (ps3_np_matching2_set_join_target: a search lists that one room only) and one pending join is kept for the
// online-menu autopilot (jsp_discord_take_join / _join_state / _join_done in jsp_discord.h). No restart: the game may be in any menu.
// The pending join is dropped 15 s after the room is joined, 20 s after the search found the room gone / full / filtered out or the
// join failed, and after 3 minutes in any case; the search then lists every room again.
// Ask to Join (ACTIVITY_JOIN_REQUEST on the host's game) is left to Discord's own prompt, which asks the host (FIX: the default is now off);
// JSP_DISCORD_AUTO_ACCEPT=1 accepts it automatically while the room has space (a full room always goes to the prompt).
// Launch from Discord: HKCU\Software\Classes\discord-<id> is pointed at jsp-launcher.exe (what the Discord SDK's register call does),
// unless JSP_DISCORD_REGISTER=0, so Join on a profile can start the game; Discord then delivers ACTIVITY_JOIN once the game has
// connected, into the same pending join. JSP_DISCORD_JOIN=<secret> in the game's environment does the same at start (one time).
// Nothing here writes game state; online names are shown on the profile, never logged.
#include "jsp_discord.h"
#include "jsp_discord_state.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <ctime>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

extern "C" int jsp_setting_get(const char* key, char* out, int n);
extern "C" int jsp_online_repsn_identity(char* server, int sn, char* name, int nn, char* comm, int cn, int* report_rtt);   // jsp_online.cpp
// the runtime's Matching2 emulation (toolkit patch 7924), any thread
extern "C" int ps3_np_matching2_room(uint64_t* room, int* members, int* max, int* is_owner, char* peer, int peer_n);
extern "C" void ps3_np_matching2_set_join_target(uint64_t room);
extern "C" int ps3_np_matching2_join_target(uint64_t* room);

namespace {

std::mutex g_join_m;
jdp::JoinSlot g_join;   // under g_join_m

std::string esc(const std::string& s)
{
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((unsigned char)c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", (unsigned char)c); o += b; }
        else o += c;
    }
    return o;
}
std::string setting(const char* k) { char b[256] = {}; return jsp_setting_get(k, b, sizeof b) ? std::string(b) : std::string(); }
// The value of "key":"..." after position `from` in a JSON text (Discord's frames are small; no general parser needed).
std::string json_str(const std::string& j, const char* key, size_t from = 0)
{
    const std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k, from); if (p == std::string::npos) return {};
    p = j.find(':', p + k.size()); if (p == std::string::npos) return {};
    p = j.find('"', p); if (p == std::string::npos) return {};
    std::string out;
    for (size_t i = p + 1; i < j.size(); i++) { if (j[i] == '\\' && i + 1 < j.size()) { out += j[++i]; continue; } if (j[i] == '"') break; out += j[i]; }
    return out;
}

uint64_t now_ms()
{
#ifdef _WIN32
    return GetTickCount64();
#else
    return (uint64_t)time(nullptr) * 1000u;
#endif
}

// This game's re-psn server ("" when no server or online name is set).
std::string my_server()
{
    char srv[160] = {}, nm[64] = {}, cid[24] = {}; int rtt = 0;
    if (!jsp_online_repsn_identity(srv, sizeof srv, nm, sizeof nm, cid, sizeof cid, &rtt) || !nm[0]) return {};
    return srv;
}

jdp::Online read_online()
{
    jdp::Online o;
    uint64_t room = 0; char peer[20] = {};
    o.m2 = ps3_np_matching2_room(&room, &o.members, &o.max, &o.owner, peer, sizeof peer);
    o.room = room; o.peer = peer;
    int players = 0, host = 0; o.rollback = jsp_net_presence(&players, &host);
    std::lock_guard<std::mutex> l(g_join_m);
    o.joining = g_join.active() && !(o.m2 == 2 && o.room == g_join.req.room) ? 1 : 0;
    return o;
}

// A join secret from Discord (or JSP_DISCORD_JOIN): checked, then the pending join and the runtime's search target.
void offer_join(const std::string& secret)
{
    uint64_t room = 0; int members = 0, max = 0, own = 0;
    const int m2 = ps3_np_matching2_room(&room, &members, &max, &own, nullptr, 0);
    jdp::JoinReq req;
    const jdp::Verdict v = jdp::decode_join(secret, my_server(), m2 == 2 ? room : 0, &req);
    if (v != jdp::Verdict::Join) { std::fprintf(stderr, "[discord] join: %s: not joined\n", jdp::verdict_text(v)); return; }
    {
        std::lock_guard<std::mutex> l(g_join_m);
        g_join.offer(req, now_ms());
    }
    ps3_np_matching2_set_join_target(req.room);
    std::fprintf(stderr, "[discord] join: room %llu on the same re-psn server: waiting for the online menus (Online Battle > Friendly Match > Search)%s\n",
                 req.room, m2 == 2 ? "; this game is in another room now and must leave it first" : "");
}

// Every update: drop the pending join once it is settled, failed for good or too old.
void step_join()
{
    uint64_t troom = 0;
    const int st = ps3_np_matching2_join_target(&troom);
    jdp::Step s;
    uint64_t room = 0;
    {
        std::lock_guard<std::mutex> l(g_join_m);
        room = g_join.req.room;
        s = g_join.step(troom == room ? st : jdp::kJoinNone, now_ms());
        if (s == jdp::Step::Done || s == jdp::Step::GiveUp) g_join.clear();
    }
    if (s == jdp::Step::Done) std::fprintf(stderr, "[discord] join: in room %llu\n", (unsigned long long)room);
    else if (s == jdp::Step::GiveUp) std::fprintf(stderr, "[discord] join: room %llu not joined (%s)\n", (unsigned long long)room,jdp::join_state_text(troom == room ? st : jdp::kJoinNone));
    if ((s == jdp::Step::Done || s == jdp::Step::GiveUp) && troom == room) ps3_np_matching2_set_join_target(0);
}

#ifdef _WIN32
class Ipc {
public:
    bool connected() const { return h_ != INVALID_HANDLE_VALUE; }
    bool connect(const std::string& app_id)
    {
        for (int i = 0; i < 10 && !connected(); i++) {
            char p[64]; std::snprintf(p, sizeof p, "\\\\.\\pipe\\discord-ipc-%d", i);
            h_ = CreateFileA(p, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        }
        if (!connected()) return false;
        if (!send(0, "{\"v\":1,\"client_id\":\"" + esc(app_id) + "\"}")) { close(); return false; }
        std::string ready;
        for (int t = 0; t < 50 && ready.empty(); t++) { poll(&ready); if (ready.empty()) Sleep(20); }
        if (ready.find("\"READY\"") == std::string::npos) { std::fprintf(stderr, "[discord] no READY from the Discord app (wrong JSP_DISCORD_APP_ID?): %.200s\n", ready.c_str()); close(); return false; }
        return true;
    }
    void close() { if (connected()) CloseHandle(h_); h_ = INVALID_HANDLE_VALUE; }
    bool send(int op, const std::string& json)
    {
        std::string f(8, '\0');
        const uint32_t o = (uint32_t)op, n = (uint32_t)json.size();
        std::memcpy(&f[0], &o, 4); std::memcpy(&f[4], &n, 4); f += json;
        DWORD w = 0;
        if (!WriteFile(h_, f.data(), (DWORD)f.size(), &w, nullptr) || w != f.size()) { close(); return false; }
        return true;
    }
    // One frame if one is waiting (never blocks): out = its JSON. False when the pipe is gone.
    bool poll(std::string* out)
    {
        out->clear();
        DWORD avail = 0;
        if (!PeekNamedPipe(h_, nullptr, 0, nullptr, &avail, nullptr)) { close(); return false; }
        if (avail < 8) return true;
        uint32_t hdr[2]; DWORD r = 0;
        if (!ReadFile(h_, hdr, 8, &r, nullptr) || r != 8 || hdr[1] > (1u << 20)) { close(); return false; }
        out->resize(hdr[1]);
        for (DWORD got = 0; got < hdr[1]; got += r) if (!ReadFile(h_, &(*out)[got], hdr[1] - got, &r, nullptr) || !r) { close(); return false; }
        if (hdr[0] == 2) { close(); return false; }   // CLOSE from Discord
        if (hdr[0] == 3) send(4, *out);               // PING -> PONG
        return true;
    }
private:
    HANDLE h_ = INVALID_HANDLE_VALUE;
};

void register_protocol(const std::string& app_id)
{
    char exe[MAX_PATH] = {};
    if (!GetModuleFileNameA(nullptr, exe, sizeof exe)) return;
    std::string dir(exe); const size_t s = dir.find_last_of("\\/");
    if (s == std::string::npos) return;
    dir.resize(s);                                              // ...\game
    const size_t s2 = dir.find_last_of("\\/");
    const std::string launcher = (s2 == std::string::npos ? dir : dir.substr(0, s2)) + "\\jsp-launcher.exe";
    if (GetFileAttributesA(launcher.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    const std::string key = "Software\\Classes\\discord-" + app_id;
    HKEY k;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    const std::string v = "URL:Run game " + app_id + " protocol";
    RegSetValueExA(k, nullptr, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)v.size() + 1);
    RegSetValueExA(k, "URL Protocol", 0, REG_SZ, (const BYTE*)"", 1);
    HKEY c;
    if (RegCreateKeyExA(k, "shell\\open\\command", 0, nullptr, 0, KEY_WRITE, nullptr, &c, nullptr) == ERROR_SUCCESS) {
        const std::string cmd = "\"" + launcher + "\"";
        RegSetValueExA(c, nullptr, 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)cmd.size() + 1);
        RegCloseKey(c);
    }
    RegCloseKey(k);
}

std::string activity_json(const jdp::Activity& a, uint32_t start_s, int nonce)
{
    std::string j = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(GetCurrentProcessId()) + ",\"activity\":{";
    j += "\"details\":\"" + esc(a.details) + "\"";
    if (!a.state.empty()) j += ",\"state\":\"" + esc(a.state) + "\"";
    j += ",\"timestamps\":{\"start\":" + std::to_string(a.start ? a.start : (uint64_t)start_s) + "}";   // the fight's start in a fight, else the session's
    j += ",\"assets\":{\"large_image\":\"" + esc(a.large) + "\",\"large_text\":\"" + esc(a.large_text) + "\"";
    if (!a.small.empty()) j += ",\"small_image\":\"" + esc(a.small) + "\",\"small_text\":\"" + esc(a.small_text) + "\"";
    j += "}";
    if (!a.party_id.empty()) j += ",\"party\":{\"id\":\"" + esc(a.party_id) + "\",\"size\":[" + std::to_string(a.size) + "," + std::to_string(a.max) + "]}";
    if (!a.join.empty()) j += ",\"secrets\":{\"join\":\"" + esc(a.join) + "\"}";
    j += ",\"instance\":false}},\"nonce\":\"" + std::to_string(nonce) + "\"}";
    return j;
}

void on_event(Ipc& ipc, const std::string& f, int& nonce, bool auto_accept)
{
    const std::string evt = json_str(f, "evt"), cmd = json_str(f, "cmd");
    if (evt == "ERROR") { std::fprintf(stderr, "[discord] error from the Discord app (%s): %.300s\n", cmd.c_str(), f.c_str()); return; }
    if (cmd == "SET_ACTIVITY") {   // the reply to an update: logged once, so a run shows Discord took the activity
        static bool said = false;
        if (!said) { said = true; std::fprintf(stderr, "[discord] activity accepted by the Discord app\n"); }
        return;
    }
    if (cmd != "DISPATCH") return;   // replies to SUBSCRIBE carry the event's name too: only a dispatched event is an event
    if (evt == "ACTIVITY_JOIN") {
        std::fprintf(stderr, "[discord] join from Discord\n");
        offer_join(json_str(f, "secret"));
    } else if (evt == "ACTIVITY_JOIN_REQUEST") {
        const size_t u = f.find("\"user\"");
        const std::string id = u == std::string::npos ? std::string() : json_str(f, "id", u);
        const bool yes = jdp::auto_accept(auto_accept, read_online());
        std::fprintf(stderr, "[discord] join request from a Discord user%s\n", yes ? " (accepted)" : " (left to Discord's prompt)");
        if (yes && !id.empty())
            ipc.send(1, "{\"cmd\":\"SEND_ACTIVITY_JOIN_INVITE\",\"args\":{\"user_id\":\"" + esc(id) + "\"},\"nonce\":\"" + std::to_string(++nonce) + "\"}");
    }
}

void run(std::string app_id, std::string start_secret)
{
    const bool auto_accept = jdp::auto_accept_setting(setting("JSP_DISCORD_AUTO_ACCEPT"));   // FIX: off unless =1 (Discord's own prompt asks the host)
    if (setting("JSP_DISCORD_REGISTER") != "0") register_protocol(app_id);
    const uint32_t start_s = (uint32_t)time(nullptr);
    Ipc ipc; int nonce = 0; std::string last; ULONGLONG last_sent = 0, next_try = 0;
    jdp::Track track;
    jdp::FightClock clock;
    uint32_t last_sig = 0;
    JspPresenceRaw last_fight;   // the last update with the fight HUD up (hold_fight)
    jdp::Options opt;
    opt.char_art = jdp::char_art_setting(setting("JSP_DISCORD_CHAR_ART"));   // FIX: the char_NNN small picture only when asked for
    for (;;) {
        if (!start_secret.empty() && !my_server().empty()) {   // JSP_DISCORD_JOIN: once the online settings are read
            std::fprintf(stderr, "[discord] join from the launch (JSP_DISCORD_JOIN)\n");
            offer_join(start_secret); start_secret.clear();
        }
        step_join();
        const ULONGLONG now = GetTickCount64();
        if (!ipc.connected()) {
            if (now < next_try) { Sleep(500); continue; }
            next_try = now + 15000;
            if (!ipc.connect(app_id)) continue;
            std::fprintf(stderr, "[discord] connected to the Discord app\n");
            for (const char* e : {"ACTIVITY_JOIN", "ACTIVITY_JOIN_REQUEST"})
                ipc.send(1, std::string("{\"cmd\":\"SUBSCRIBE\",\"evt\":\"") + e + "\",\"nonce\":\"" + std::to_string(++nonce) + "\"}");
            last.clear();
        }
        std::string f;
        while (ipc.connected() && ipc.poll(&f) && !f.empty()) on_event(ipc, f, nonce, auto_accept);
        if (!ipc.connected()) continue;
        JspPresenceRaw r; jsp_game_presence(&r);
        const jdp::Online o = read_online();
        jdp::note(track, r, o);
        const uint32_t sig = jdp::lineup_sig(r);
        if (r.fight && sig && sig != last_sig) {   // the fight's units (id/team/role/CPU) once per line-up, to check the names against
            last_sig = sig;
            char b[200]; int p = std::snprintf(b, sizeof b, "[discord] fight units:");
            for (int i = 0; i < r.units && i < 6 && p > 0 && p < (int)sizeof b; i++)
                p += std::snprintf(b + p, sizeof b - (size_t)p, " %d/t%d/r%d%s", r.u_chr[i], r.u_team[i], r.u_role[i], r.u_cpu[i] ? "/cpu" : "");
            std::fprintf(stderr, "%s\n", b);
        }
        uint64_t fight_s = jdp::fight_start(clock, r.fight != 0, sig, now, (uint64_t)time(nullptr));
        // FIX: the HUD drops for a moment inside a fight (rollback catch-up, cut-ins); the last fight screen is kept a few seconds so the
        // profile does not flip to "After the fight" and back (seen in a two-copy online match)
        if (r.fight) last_fight = r;
        else if (last_fight.fight && jdp::hold_fight(clock, r, now)) { r = last_fight; fight_s = clock.start_s; }
        const jdp::Activity act = jdp::build(r, o, track, my_server(), opt, fight_s);
        const std::string j = activity_json(act, start_s, 0);
        if (j != last || now - last_sent > 60000) {   // only on a change (Discord rate-limits updates), and once a minute as a keepalive
            if (j != last) {   // the activity as sent, once per change; the other player's online name is never logged
                const std::string d = jdp::redact(act.details, o.peer), s = jdp::redact(act.state, o.peer);
                std::fprintf(stderr, "[discord] activity: details=\"%s\" state=\"%s\" large_image=\"%s\" large_text=\"%s\" small_image=\"%s\" party=%d/%d join=%s\n",
                             d.c_str(), s.c_str(), act.large.c_str(), act.large_text.c_str(), act.small.c_str(), act.size, act.max,
                             act.join.empty() ? "no" : "yes");
            }
            last = j; last_sent = now;
            std::string s = j; const std::string n0 = "\"nonce\":\"0\""; const size_t p = s.rfind(n0);
            if (p != std::string::npos) s.replace(p, n0.size(), "\"nonce\":\"" + std::to_string(++nonce) + "\"");
            ipc.send(1, s);
        }
        Sleep(2000);
    }
}
#endif

}  // namespace

extern "C" void jsp_discord_start(void)
{
#ifdef _WIN32
    if (setting("JSP_DISCORD") == "0") return;
    const std::string id = setting("JSP_DISCORD_APP_ID");
    switch (jdp::app_id_setting(id)) {   // FIX (review): no built-in application; off until one is configured
    case jdp::AppId::Unset: std::fprintf(stderr, "[discord] Rich Presence off: no Discord application set (JSP_DISCORD_APP_ID)\n"); return;
    case jdp::AppId::Invalid: std::fprintf(stderr, "[discord] JSP_DISCORD_APP_ID must be the application's numeric id: off\n"); return;
    case jdp::AppId::Ok: break;
    }
    const char* js = std::getenv("JSP_DISCORD_JOIN");   // the environment only: a settings-file value would join again on every start
    std::fprintf(stderr, "[discord] Rich Presence on (application %s)\n", id.c_str());
    std::thread(run, id, std::string(js ? js : "")).detach();
#endif
}

extern "C" int jsp_discord_take_join(uint64_t* room, char* server, int n)
{
    jdp::JoinReq r;
    {
        std::lock_guard<std::mutex> l(g_join_m);
        if (!g_join.take(&r)) return 0;
    }
    if (room) *room = r.room;
    if (server && n > 0) std::snprintf(server, (size_t)n, "%s", r.server.c_str());
    std::fprintf(stderr, "[discord] join: room %llu handed to the online menus\n", (unsigned long long)r.room);
    return 1;
}

extern "C" int jsp_discord_join_state(uint64_t* room)
{
    uint64_t mine = 0;
    {
        std::lock_guard<std::mutex> l(g_join_m);
        mine = g_join.req.room;
    }
    if (room) *room = mine;
    if (!mine) return jdp::kJoinNone;
    uint64_t troom = 0;
    const int st = ps3_np_matching2_join_target(&troom);
    return troom == mine ? st : jdp::kJoinNone;   // FIX: the search is no longer pointed at this room (it lists every room): none, not pending
}

extern "C" void jsp_discord_join_done(int ok)
{
    uint64_t room = 0;
    {
        std::lock_guard<std::mutex> l(g_join_m);
        room = g_join.req.room;
        g_join.clear();
    }
    if (!room) return;
    uint64_t troom = 0;
    ps3_np_matching2_join_target(&troom);
    if (troom == room) ps3_np_matching2_set_join_target(0);
    std::fprintf(stderr, "[discord] join: room %llu %s\n", (unsigned long long)room, ok ? "joined (online menus)" : "given up by the online menus");
}
