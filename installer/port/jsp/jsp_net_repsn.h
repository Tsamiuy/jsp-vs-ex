// jsp_net_repsn.h -- ENHANCEMENT (rollback over re-psn, direct or hole-punched): the second IRendezvous source.
// Header only; talks to re-psn through IPsnrTransport (a fake in installer/tools/net_repsn_test.cpp; the real one over the psnr client is jsp_net_repsn_client.h).
// Convention: re-psn docs/api.md, section "Real-time titles such as rollback" (re-psn v0.8.0, merge 9398a557cb57):
//   - OUR UdpMux socket is THE P2P socket: its port goes in HELLO, PROBE and PUNCH go out from it (the transport's job), peers' ip/p2p_port come from the member entries.
//   - a reserved room-flag bit plus the record "rb=1;ver=<build>" in the room's external blob and the same record in every member's data; any mismatch is refused
//     (ROOM_LIST shows flags + external before joining; the member entries at join). No fallback to the game's own online.
//   - the owner hosts; room_id is the session id; the player index follows member-id order (ids count up in join order, never reused in a room).
//   - direct when reachable (PUNCH first), punched otherwise; relay only if the player allowed it AND the server offers it (HELLO_ACK flag bit 0); the path is shown.
//   - psnr_is_control datagrams belong to the psnr client, not to us (our tags G/M/S/K never start with "PSNR"): the transport's job.
//   - peer lag comes from GekkoNet; psnr_rtt_ms is only a server indicator.
// After the peer's endpoint is known the existing DirectRendezvous (HELO / PLAN over the 'M' channel) runs against it: the host is the room owner, session_id = room_id.
#ifndef JSP_NET_REPSN_H
#define JSP_NET_REPSN_H

#include "jsp_net_session.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <cstdio>
#include <functional>
#include <set>

namespace jspnet {

// ASSUMPTION: bit 30 of the room flags is "this room holds a rollback session" (the convention says "a reserved bit"; the server stores flags and does not read them).
constexpr uint32_t kRoomFlagRollback = 0x40000000u;

// The record in the external blob and in member data: "rb=1;ver=<8 hex digits of the build id>". Exactly this shape; anything else does not match.
// ENHANCEMENT (native-room pairing, jsp_net_game.cpp native_poll): an optional pair key, the id of the game's own Online Battle room (Matching2) both players are in,
// appended as ";pair=<16 hex digits>". A record with a pair key matches only the same build AND the same key: a joiner takes only the rollback room of ITS native room,
// a host refuses a member from another native room. pair 0 = the plain record (the start-up and lobby paths, unchanged).
inline std::string make_record(uint32_t build, uint64_t pair = 0)
{
    char b[64];
    if (pair) std::snprintf(b, sizeof b, "rb=1;ver=%08x;pair=%016llx", (unsigned)build, (unsigned long long)pair);
    else std::snprintf(b, sizeof b, "rb=1;ver=%08x", (unsigned)build);
    return b;
}
inline bool record_matches(const std::string& blob, uint32_t build, uint64_t pair = 0) { return blob == make_record(build, pair); }
namespace detail {
inline bool lower_hex(const std::string& s, size_t from, size_t n, uint64_t* out)
{
    uint64_t v = 0;
    for (size_t i = from; i < from + n; i++) {
        const char ch = s[i];
        const int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
        if (d < 0) return false;
        v = v << 4 | (uint64_t)d;
    }
    *out = v; return true;
}
}  // namespace detail
// The build word (and the pair key, 0 for a plain record) of a well-formed record (for the "another version was seen" hint); false for anything else.
inline bool record_build(const std::string& blob, uint32_t* build, uint64_t* pair = nullptr)
{
    const bool plain = blob.size() == 17, paired = blob.size() == 39 && blob.compare(17, 6, ";pair=") == 0;
    if ((!plain && !paired) || blob.compare(0, 9, "rb=1;ver=") != 0) return false;
    uint64_t v = 0, k = 0;
    if (!detail::lower_hex(blob, 9, 8, &v) || (paired && (!detail::lower_hex(blob, 23, 16, &k) || !k))) return false;
    *build = (uint32_t)v; if (pair) *pair = k;
    return true;
}
// ENHANCEMENT (native-room pairing): the rollback link opens its OWN re-psn client next to the game's own one (np_psnr, the Online Battle menus) on the same server.
// The server refuses a second client with the same online name (PSNR_E_NAME_TAKEN), so the rollback client takes a derived name of at most 16 characters: the name
// plus "-rb" when it fits, else its first 11 characters, "-" and 4 hex digits of the whole name's FNV-1a (two long names that share 11 characters still differ).
inline std::string rollback_name(const std::string& name)
{
    if (name.size() + 3 <= 16) return name + "-rb";
    uint32_t h = 2166136261u; for (char ch : name) { h ^= (uint8_t)ch; h *= 16777619u; }
    char t[8]; std::snprintf(t, sizeof t, "-%04x", (unsigned)(h & 0xFFFFu));
    return name.substr(0, 11) + t;
}
// ... and a communication id of its own (the title's id with its last two characters "RB"), so the game's own room search does not list the rollback rooms.
// ASSUMPTION: re-psn keeps rooms per communication id, like PSN. jsp_net_game.cpp: JSP_ROLLBACK_NATIVE_COMM=same keeps the title's id, any other value replaces it.
inline std::string rollback_comm_id(const std::string& comm) { return comm.size() < 3 ? comm : comm.substr(0, comm.size() - 2) + "RB"; }

// The rendezvous window, the same for both roles, counted by each side from its own first poll (after its startup evidence). 120 s: the two PCs
// reach that point at very different times (a cold pipeline cache adds tens of seconds; the startup-evidence wait alone may take up to 240 s) and
// the players start by hand, often while talking in another program; 30 s (0.6.0) failed in the field. Both sides keep their re-psn connection
// alive the whole time (psnr_poll's heartbeat), so the server's 90 s silence limit does not apply. Longer would only delay the message that says
// what to do. The two games meet when they start looking at most this far apart, in either order.
constexpr int kRendezvousWindowMs = 120000;
// The joiner reads the room list again this long after a list without a matching room (one request in flight at a time): at most 60 lists in a window.
constexpr int kLookupEveryMs = 2000;
// One progress line (and on-screen notice update) this often while waiting for the other player.
constexpr int kProgressEveryMs = 10000;
// "2 minutes", "1 minute", "20 seconds": the window in the player-facing texts.
inline std::string window_words(int ms)
{
    if (ms >= 60000 && ms % 60000 == 0) { const int m = ms / 60000; return std::to_string(m) + (m == 1 ? " minute" : " minutes"); }
    const int s = (ms + 999) / 1000; return std::to_string(s) + (s == 1 ? " second" : " seconds");
}
inline std::string build_hex(uint32_t build) { char b[12]; std::snprintf(b, sizeof b, "%08x", (unsigned)build); return b; }
// ENHANCEMENT (version mismatch message): the build words of the published releases (jsp_build_id.cmake, without gameplay mods), so the message names the
// release the other player runs. A word not listed (a newer release, a local build, gameplay mods) is named "build xxxxxxxx". Add each release when it is published.
struct KnownBuild { uint32_t build; const char* release; };
inline constexpr KnownBuild kKnownBuilds[] = { { 0xe9be7f43u, "0.7.0-test1" }, { 0xf1e06d4du, "0.7.0-test2" }, { 0x8a47e98au, "0.7.0-test3" },
                                                 { 0x699bd950u, "0.7.0-test4" }, { 0x42959da6u, "0.7.0-test5" } };
inline std::string release_name(uint32_t build)
{
    for (const KnownBuild& k : kKnownBuilds) if (k.build == build) return k.release;
    return "build " + build_hex(build);
}
// The message of a version mismatch (the message line and the first line of the menu notice). own_release: this game's release.txt ("" = by its build word).
// Two different words that name the same release (other gameplay mods, a changed source) are both shown as build words.
inline std::string version_mismatch_text(uint32_t other, uint32_t own, const std::string& own_release)
{
    std::string theirs = release_name(other), mine = own_release.empty() ? release_name(own) : own_release;
    if (theirs == mine) { theirs = "build " + build_hex(other); mine = "build " + build_hex(own); }
    const bool unknown = theirs.rfind("build ", 0) == 0;
    return "The other player is on " + theirs + (unknown ? " (or uses different gameplay mods)" : "") + "; you're on " + mine + ". Both players need the same version for rollback.";
}
// How long a joiner that found the other version's room waits for the server's answer to its one join (that join tells a new host at once).
constexpr int kMismatchJoinMs = 3000;
// What to check when the two games did not meet (the second line of the on-screen notice; the tail of the one-line text).
inline std::string meet_advice(uint32_t build, int window_ms)
{
    return "start both games at about the same time (the host first is best; each waits " + window_words(window_ms) + " after its own start-up); both need the same version (build " + build_hex(build) +
           ") and the same gameplay mods (or none)";
}

// Field widths follow re-psn docs/api.md "Rooms" (v0.8.0): room ids are u64, member ids u16, user ids u32. ROOM_LIST names the owner by online id (no user id).
struct PsnrRoom { uint64_t id = 0; uint32_t flags = 0; std::string external; uint32_t owner = 0 /* user id when known, 0 from ROOM_LIST */; uint8_t members = 0, max_members = 0; std::string owner_name; };
struct PsnrMember { uint32_t user = 0; uint16_t member_id = 0; Endpoint ep; std::string data; };

// ROOM_LIST entries a rollback client may join: the flag bit, the same build record, and not full. Sorted by room id (the first is the oldest).
// pair != 0 (native-room pairing): only the rooms whose record carries that pair key (another native room's rollback room is never joined).
inline std::vector<PsnrRoom> filter_rooms(const std::vector<PsnrRoom>& all, uint32_t build, uint64_t pair = 0)
{
    std::vector<PsnrRoom> r;
    for (const PsnrRoom& x : all)
        if ((x.flags & kRoomFlagRollback) && record_matches(x.external, build, pair) && x.members < x.max_members) r.push_back(x);
    std::sort(r.begin(), r.end(), [](const PsnrRoom& a, const PsnrRoom& b) { return a.id < b.id; });
    return r;
}
// the player index of `user` by member-id order (0 = lowest id); -1 when not a member
inline int player_index(const std::vector<PsnrMember>& members, uint32_t user)
{
    std::vector<const PsnrMember*> v; for (const auto& m : members) v.push_back(&m);
    std::sort(v.begin(), v.end(), [](const PsnrMember* a, const PsnrMember* b) { return a->member_id < b->member_id; });
    for (size_t i = 0; i < v.size(); i++) if (v[i]->user == user) return (int)i;
    return -1;
}

struct PsnrEvent {
    enum Kind { None, RoomJoined, MemberJoined, MemberLeft, Error } kind = None;   // RoomJoined: also the answer to CREATE_ROOM (the owner is the only member)
    uint64_t room_id = 0;
    uint32_t owner = 0, user = 0;                // owner: the OWNER'S USER id (the wire carries a member id; the transport maps it); user: who left
    std::vector<PsnrMember> members;             // RoomJoined: all members (with the caller); MemberJoined: the one who joined
    int err = 0;
};
// What the rendezvous needs of the re-psn client. All calls are non-blocking.
class IPsnrTransport {
public:
    virtual ~IPsnrTransport() {}
    virtual const char* failure() { return nullptr; }   // non-null once the connection to the server failed for good (the reason)
    virtual void kick(uint32_t user) { (void)user; }      // the owner removes a member whose record did not match (KICK_MEMBER)
    virtual bool ready() = 0;                    // HELLO acknowledged (our UdpMux port was sent in it)
    virtual uint32_t self_user() = 0;
    virtual bool relay_offered() = 0;            // HELLO_ACK flag bit 0
    virtual bool search(std::vector<PsnrRoom>& out) = 0;   // true when a ROOM_LIST answer is available (rooms searched with the rollback flag)
    virtual void create_room(uint32_t flags, const std::string& external, const std::string& member_data) = 0;
    virtual void join_room(uint64_t room_id, const std::string& member_data) = 0;
    virtual bool poll_event(PsnrEvent& ev) = 0;
    virtual void send_punch(const Endpoint& to) = 0;   // PUNCH from the P2P socket (our mux)
    virtual void leave_room() = 0;
};

enum class Path { None, Punch, Direct, Relay };
inline const char* path_name(Path p) { return p == Path::Direct ? "direct" : p == Path::Relay ? "relay" : p == Path::Punch ? "punching" : "-"; }

// ENHANCEMENT (display only): the rendezvous text of the on-screen notice (jsp_net_game.cpp jsp_net_notice; jsp_port_menu.cpp re-reads it when gen() changes).
// Written by the pump thread, read by the menu: one mutex, and a generation that counts every change. Every way the wait can end must leave it right:
// met() and ended() clear it (another part ended the session before the games met: its own text is on the message line), failed() replaces it.
class RendezvousNotice {
public:
    void waiting(const std::string& a, const std::string& b) { set(a, b); }
    void failed(const std::string& a, const std::string& b) { set(a, b); }
    void met() { set(std::string(), std::string()); }
    void ended() { set(std::string(), std::string()); }
    bool read(std::string& a, std::string& b) const { std::lock_guard<std::mutex> lk(mu_); if (l1_.empty()) return false; a = l1_; b = l2_; return true; }
    unsigned gen() const { return gen_.load(); }
private:
    void set(const std::string& a, const std::string& b) { { std::lock_guard<std::mutex> lk(mu_); if (a == l1_ && b == l2_) return; l1_ = a; l2_ = b; } gen_.fetch_add(1); }
    mutable std::mutex mu_; std::string l1_, l2_; std::atomic<unsigned> gen_{0};
};

class RePsnRendezvous : public IRendezvous {
public:
    struct Config {
        bool host = true;
        uint64_t join_room = 0;         // client: 0 = the oldest matching room, else that room (it must still match)
        uint32_t build = 0;
        uint64_t pair = 0;              // ENHANCEMENT (native-room pairing): the game's own room id both players are in (0 = none: the plain record); see make_record
        bool allow_relay = false;       // the player's choice
        uint8_t local_count = 1;
        int punch_ms = 4000;            // direct/punch window before relay is considered
        bool force_relay = false;       // TEST ONLY (JSP_ROLLBACK_FORCE_RELAY=1 with debug allowed): skip the direct try and use the relay at once (when allowed and offered),
                                        // so the relay path can be tested from one PC, where the direct path always works
        int give_up_ms = kRendezvousWindowMs;   // finding the other player (connect, room list / room, until its endpoint is known); the punch has its own windows
        int lookup_every_ms = kLookupEveryMs;   // joiner: the pause between two room lists without a matching room
        int progress_every_ms = kProgressEveryMs;
        std::string release;            // this game's release name (release.txt; "" = a local build): the version mismatch message
    };
    RePsnRendezvous(IPsnrTransport& t, UdpMux& mux, MessageHub& hub, const Config& c) : t_(t), mux_(mux), hub_(hub), c_(c) {}
    // when the path changes: Relay means the mux must send through the server from now on (psnr_relay_wrap / psnr_relay_unwrap_from); the glue sets that up
    std::function<void(Path, uint32_t peer_user)> on_path;
    std::function<uint64_t()> now_ms = [] { return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    // While waiting for the other player (before the punch), every progress_every_ms and once at the start: one line for the log (`line`) and the
    // two lines of the on-screen notice. Runs inside poll().
    std::function<void(const std::string& line, const std::string& notice1, const std::string& notice2)> on_progress;

    int poll() override
    {
        if (state_ == kDone) return 1;
        if (state_ == kFailed) return -1;
        const uint64_t t = now_ms();
        if (!t0_) t0_ = t;
        if (mm_build_) {   // the joiner joined the other version's room once, only so that its host learns it: fail when the server answered (or did not in time)
            PsnrEvent ev; bool answered = false;
            while (t_.poll_event(ev)) if (ev.kind == PsnrEvent::RoomJoined || ev.kind == PsnrEvent::Error) answered = true;
            if (answered || t >= mm_until_ || t_.failure()) return fail_version(mm_build_);
            return 0;
        }
        // The window covers finding the other player only: once its endpoint is known the punch (and the relay, if allowed) has its own bound (punch_ms).
        if (state_ < kPunch && t - t0_ > (uint64_t)c_.give_up_ms) return give_up();
        if (const char* f = t_.failure()) return fail(f);   // the connection failed for good, or a lookup was refused: end now, not at the give-up
        if (state_ == kConnect && t_.ready()) state_ = kLookup;
        if (state_ < kPunch) progress(t);   // after the connect step: a game that is already connected never reports "connecting"
        if (state_ == kConnect) return 0;
        if (state_ == kLookup) {
            if (c_.host) { t_.create_room(kRoomFlagRollback, make_record(c_.build, c_.pair), make_record(c_.build, c_.pair)); state_ = kRoom; }   // listed until the host leaves (give-up, failure or the end)
            else {
                if (next_lookup_ && t < next_lookup_) return 0;   // the last list had no room for us: wait before asking again
                std::vector<PsnrRoom> all;
                if (!t_.search(all)) return 0;                     // one request in flight; the answer comes on a later poll
                ++lookups;
                note_other_builds(all);
                // FIX (two PCs, another version): with a pair key the host's rollback room is known exactly (same native room). When it is there with
                // another build, waiting 2 minutes (then 3 retries) cannot help and the fight meanwhile starts on the game's own netcode with no word on
                // screen: say so at once, with both versions. ENHANCEMENT (version mismatch message): first join that room once with our own record, so a
                // host of this build or later fails at once too (its member check below); an older host refuses the member and still gives up at its window.
                // The room's record format is unchanged: older builds compare it exactly.
                if (c_.pair && !c_.join_room)
                    for (const PsnrRoom& x : all) {
                        uint32_t b = 0; uint64_t k = 0;
                        if ((x.flags & kRoomFlagRollback) && record_build(x.external, &b, &k) && k == c_.pair && b != c_.build) {
                            t_.join_room(x.id, make_record(c_.build, c_.pair)); room_ = x.id; state_ = kRoom;
                            mm_build_ = b; mm_until_ = t + (uint64_t)kMismatchJoinMs;
                            return 0;
                        }
                    }
                const std::vector<PsnrRoom> ok = filter_rooms(all, c_.build, c_.pair);
                uint64_t pick = 0;
                if (c_.join_room) { for (const auto& r : ok) if (r.id == c_.join_room) pick = r.id;
                    if (!pick) return fail("re-psn: that room is gone, full, or from another build"); }
                else if (!ok.empty()) pick = ok.front().id;
                else { next_lookup_ = t + (uint64_t)c_.lookup_every_ms; return 0; }   // no host yet (it may start later): keep looking until the window ends
                t_.join_room(pick, make_record(c_.build, c_.pair)); room_ = pick; state_ = kRoom;
            }
        }
        PsnrEvent ev;
        while (t_.poll_event(ev)) {
            if (ev.kind == PsnrEvent::Error) return fail("re-psn: the server refused (error " + std::to_string(ev.err) + ")");
            if (ev.kind == PsnrEvent::RoomJoined) { room_ = ev.room_id; owner_ = ev.owner; members_ = ev.members; }
            else if (ev.kind == PsnrEvent::MemberJoined) {
                for (auto& m : ev.members) {
                    uint32_t b = 0; if (other_version(m, &b)) return fail_version(b);   // checked here too: the joiner leaves again at once (MemberLeft may come in this same poll)
                    members_.push_back(m);
                }
            }
            else if (ev.kind == PsnrEvent::MemberLeft) {
                members_.erase(std::remove_if(members_.begin(), members_.end(), [&](const PsnrMember& m) { return m.user == ev.user; }), members_.end());
                if (ev.user == peer_user_ && state_ >= kPunch) return fail("re-psn: the other player left the room");
                if (ev.user == owner_ && !c_.host) return fail("re-psn: the host left the room");   // a session cannot move its host
            }
        }
        if (state_ == kRoom && room_) {
            // every OTHER member's record is checked; a mismatching member is refused (never played with). The host waits for the first match, the client looks at the owner.
            for (const PsnrMember& m : members_) {
                if (m.user == t_.self_user()) continue;
                { uint32_t b = 0; if (other_version(m, &b)) return fail_version(b); }   // ENHANCEMENT (version mismatch message): a player of our native room on another version
                if (!record_matches(m.data, c_.build, c_.pair)) {
                    if (!c_.host) return fail("re-psn: the host's build or settings record does not match (refused)");
                    if (!rejected_.count(m.user)) { rejected_.insert(m.user); ++rejected_members; t_.kick(m.user); }   // never played with, and removed from the room so it cannot hold the slot
                    continue;
                }
                if (!c_.host && m.user != owner_) continue;
                if (m.ep.port == 0) continue;   // no endpoint yet (the member's HELLO had no P2P port)
                peer_ = m; peer_user_ = m.user; break;
            }
            if (peer_user_) {
                local_index_ = player_index(members_, t_.self_user());
                Path_(Path::Punch);
                if (c_.force_relay && c_.allow_relay && t_.relay_offered()) { relay_ = true; Path_(Path::Relay); }   // TEST ONLY: see Config::force_relay
                start_inner(); punch_start_ = t; last_punch_ = 0; state_ = kPunch;
            }
        }
        if (state_ == kPunch) {
            if (!last_punch_ || t - last_punch_ >= 200) { t_.send_punch(peer_.ep); last_punch_ = t; }
            const int r = inner_->poll();
            if (r == 1) { plan_ = inner_->plan(); ch_ = inner_->peer_channel(); Path_(relay_ ? Path::Relay : Path::Direct); state_ = kDone; return 1; }
            if (r < 0) return fail(inner_->error());
            if (t - punch_start_ > (uint64_t)c_.punch_ms) {
                if (!relay_ && c_.allow_relay && t_.relay_offered()) {   // relay only when the player allowed it AND the server offers it
                    relay_ = true; Path_(Path::Relay); start_inner(); punch_start_ = t;
                } else
                    return fail(relay_ ? "re-psn: the other player cannot be reached, even through the relay"
                              : !c_.allow_relay ? "re-psn: not reachable directly (relay is not allowed in the settings)"
                              : "re-psn: not reachable directly and this server offers no relay");
            }
        }
        return 0;
    }
    const SessionPlan& plan() const override { return plan_; }
    const std::string& error() const override { return err_; }
    MessageChannel* peer_channel() override { return ch_; }
    Path path() const { return path_; }
    int local_index() const { return local_index_; }   // by member-id order (the owner joined first: 0)
    uint64_t room_id() const { return room_; }
    uint64_t rejected_members = 0;                    // members whose record did not match (the host side)
    uint64_t lookups = 0;                             // room lists read (the joiner)
    // The failure as the two lines of the on-screen notice (what happened / what to do); line 2 is empty when error() says it all.
    const std::string& notice1() const { return n1_; }
    const std::string& notice2() const { return n2_; }
    bool version_mismatch() const { return mismatch_; }   // the failure was the other player's version: notice1() is version_mismatch_text, no retry can help
private:
    enum { kConnect = 0, kLookup, kRoom, kPunch, kDone, kFailed };
    int fail(const std::string& why) { err_ = why; n1_ = "Rollback could not start: " + why; n2_.clear(); state_ = kFailed; t_.leave_room(); return -1; }
    // A member of our native room (the same pair key) whose record carries another build word.
    bool other_version(const PsnrMember& m, uint32_t* b) const
    {
        uint64_t k = 0;
        return c_.pair && m.user != t_.self_user() && record_build(m.data, b, &k) && k == c_.pair && *b != c_.build;
    }
    int fail_version(uint32_t other)
    {
        fail("re-psn: the other player runs another version (build " + build_hex(other) + ", yours " + build_hex(c_.build) + ")");
        n1_ = version_mismatch_text(other, c_.build, c_.release);
        n2_ = "Rollback is off in this room. Both need the same download and the same gameplay mods (or none); the main menu corner shows the version.";
        mismatch_ = true; return -1;
    }
    // The window ended before the other player was found. One line for the log and the screen, with the build word so the players can compare.
    int give_up()
    {
        std::string what;
        if (state_ == kConnect) what = "the online server did not answer within " + window_words(c_.give_up_ms);
        else if (!c_.host && state_ == kLookup) what = "no host with this version found within " + window_words(c_.give_up_ms) + (other_builds_.empty() ? std::string() : " (" + other_builds_text() + ")");
        // ENHANCEMENT (version mismatch message): a paired host knows the other player is in its native room; a joiner of a build before 0.7.0-test4 that
        // runs another version never joins (it only says so on its own screen), so that is the likely reason
        else if (c_.host && c_.pair) what = "the other player probably runs a different version (the main menu shows it), or their online connection failed";
        else if (c_.host) what = "no player with this version joined within " + window_words(c_.give_up_ms) + (rejected_members ? " (" + std::to_string(rejected_members) + " with another version refused)" : "");
        else what = "the host's room did not answer within " + window_words(c_.give_up_ms);
        const std::string advice = meet_advice(c_.build, c_.give_up_ms);
        fail("re-psn: " + what + ": " + advice);
        n1_ = "Rollback could not start: " + what + ".";
        n2_ = advice + "."; if (n2_[0] >= 'a' && n2_[0] <= 'z') n2_[0] = (char)(n2_[0] - 'a' + 'A');
        return -1;
    }
    void note_other_builds(const std::vector<PsnrRoom>& all)
    {
        for (const PsnrRoom& x : all) { uint32_t b = 0; if ((x.flags & kRoomFlagRollback) && record_build(x.external, &b) && b != c_.build) other_builds_.insert(b); }
    }
    std::string other_builds_text() const
    {
        if (other_builds_.empty()) return std::string();
        std::string s = "seen: a rollback room with build";
        size_t i = 0; for (uint32_t b : other_builds_) { if (i == 3) { s += ", ..."; break; } s += (i ? ", " : " ") + build_hex(b); ++i; }
        return s;
    }
    void progress(uint64_t t)
    {
        if (last_progress_ && t - last_progress_ < (uint64_t)c_.progress_every_ms) return;
        last_progress_ = t;
        if (!on_progress) return;
        const std::string w = std::to_string((t - t0_) / 1000) + " s of " + std::to_string(c_.give_up_ms / 1000) + " s";
        const std::string b = "build " + build_hex(c_.build);
        std::string line, n1, n2;
        if (state_ == kConnect) { line = "re-psn: connecting to the server (" + w + ", " + b + ")"; n1 = "Rollback: connecting to the online server (" + w + ")."; }
        else if (c_.host) {
            line = "re-psn: room open, waiting for a player with " + b + " (" + w + (rejected_members ? ", " + std::to_string(rejected_members) + " with another version refused" : "") + ")";
            n1 = "Rollback: room open, waiting for the other player (" + w + ").";
        } else if (state_ == kLookup) {
            line = "re-psn: looking for a host with " + b + " (" + w + ", " + std::to_string(lookups) + " room lists read" + (other_builds_.empty() ? std::string() : "; " + other_builds_text()) + ")";
            n1 = "Rollback: looking for the host's room (" + w + ").";
        } else { line = "re-psn: joined room " + std::to_string((unsigned long long)room_) + ", waiting for the host (" + w + ", " + b + ")"; n1 = "Rollback: joined the host's room, waiting for it (" + w + ")."; }
        n2 = "Both need the same version (build " + build_hex(c_.build) + ") and the same gameplay mods (or none).";
        on_progress(line, n1, n2);
    }
    void Path_(Path p) { path_ = p; if (on_path) on_path(p, peer_user_); }
    void start_inner()
    {
        if (c_.host) {
            inner_.reset(new DirectRendezvous(mux_, hub_, c_.local_count, room_));   // the room id is the session id
            inner_->set_only_peer(peer_.ep);   // a HELO from any other endpoint is dropped (relayed datagrams are rewritten to this endpoint by the transport's rx hook)
        }
        else inner_.reset(new DirectRendezvous(mux_, hub_, c_.local_count, peer_.ep));
    }
    IPsnrTransport& t_; UdpMux& mux_; MessageHub& hub_; Config c_;
    int state_ = kConnect; uint64_t t0_ = 0, punch_start_ = 0, last_punch_ = 0, next_lookup_ = 0, last_progress_ = 0;
    std::set<uint32_t> other_builds_; std::string n1_, n2_;
    uint32_t mm_build_ = 0; uint64_t mm_until_ = 0; bool mismatch_ = false;   // the joiner's one join of the other version's room; the failure was a version mismatch
    uint64_t room_ = 0; uint32_t owner_ = 0, peer_user_ = 0; int local_index_ = -1; bool relay_ = false;
    std::vector<PsnrMember> members_; PsnrMember peer_; std::set<uint32_t> rejected_;
    std::unique_ptr<DirectRendezvous> inner_; SessionPlan plan_; std::string err_; MessageChannel* ch_ = nullptr; Path path_ = Path::None;
};

}  // namespace jspnet

#endif
