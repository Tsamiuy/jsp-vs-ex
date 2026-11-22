// jsp_net_repsn.h -- ENHANCEMENT (rollback over re-psn, direct or hole-punched): the second IRendezvous source.
// Header only; talks to re-psn through IPsnrTransport (a fake in installer/tools/net_repsn_test.cpp; the real one over the psnr client is jsp_net_repsn_client.h).
// Convention: re-psn docs/api.md, section "Real-time titles such as rollback" (re-psn v0.8.0, merge 9398a557cb57):
//   - OUR UdpMux socket is THE P2P socket: its port goes in HELLO, PROBE and PUNCH go out from it (the transport's job), peers' ip/p2p_port come from the member entries.
//   - a reserved room-flag bit plus the port's versioned JRB2 release/content/code record in the room's external blob and the same record in every member's data; any mismatch is refused
//     (ROOM_LIST shows flags + external before joining; the member entries at join). No fallback to the game's own online.
//   - the owner hosts; room_id is the session id; the player index follows member-id order (ids count up in join order, never reused in a room).
//   - direct when reachable (PUNCH first), punched otherwise; relay only if the player allowed it AND the server offers it (HELLO_ACK flag bit 0); the path is shown.
//   - psnr_is_control datagrams belong to the psnr client, not to us (our tags G/M/S/K never start with "PSNR"): the transport's job.
//   - peer lag comes from GekkoNet; psnr_rtt_ms is only a server indicator.
// After the peer's endpoint is known the existing DirectRendezvous (HELO / PLAN over the 'M' channel) runs against it: the host is the room owner, session_id = room_id.
#ifndef JSP_NET_REPSN_H
#define JSP_NET_REPSN_H

#include "jsp_net_session.h"
#include "jsp_session_digest.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <functional>
#include <set>

namespace jspnet {

// ASSUMPTION: bit 30 of the room flags is "this room holds a rollback session" (the convention says "a reserved bit"; the server stores flags and does not read them).
constexpr uint32_t kRoomFlagRollback = 0x40000000u;

// External room and member records are opaque u16-length blobs. JRB2 binds the release, full applied-content
// SHA-256 and optional lowercase SHA-256 match-code tag. Codes select a public group and are never sent directly.
inline std::string code_tag(const std::string& normalized)
{
    if (normalized.empty()) return std::string();
    const std::string domain = "jsp-rb-code:" + normalized;
    const jpsm::Digest digest = jpsm::sha256(reinterpret_cast<const uint8_t*>(domain.data()), domain.size());
    static const char hex[] = "0123456789abcdef";
    std::string tag; tag.reserve(64);
    for (uint8_t byte : digest) { tag.push_back(hex[byte >> 4]); tag.push_back(hex[byte & 15]); }
    return tag;
}
inline bool valid_code_tag(const std::string& tag)
{
    return tag.empty() || (tag.size() == 64 && tag.find_first_not_of("0123456789abcdef") == std::string::npos);
}
inline std::string make_record(uint32_t build, const jpsm::Digest& content, const std::string& tag = {})
{
    if (content == jpsm::Digest{} || !valid_code_tag(tag)) return {};
    std::string record = "JRB2";
    for (int shift = 24; shift >= 0; shift -= 8) record.push_back(static_cast<char>(build >> shift));
    record.append(reinterpret_cast<const char*>(content.data()), content.size());
    return record + tag;
}
inline bool record_matches(const std::string& blob, uint32_t build, const jpsm::Digest& content, const std::string& tag = {})
{
    return !blob.empty() && blob == make_record(build, content, tag);
}
inline bool record_build(const std::string& blob, uint32_t* build, std::string* tag = nullptr, jpsm::Digest* content = nullptr)
{
    if ((blob.size() != 40 && blob.size() != 104) || blob.compare(0, 4, "JRB2") != 0) return false;
    jpsm::Digest digest;
    std::memcpy(digest.data(), blob.data() + 8, digest.size());
    const std::string code = blob.substr(40);
    if (digest == jpsm::Digest{} || !valid_code_tag(code)) return false;
    uint32_t value = 0;
    for (size_t i = 4; i < 8; ++i) value = (value << 8) | static_cast<uint8_t>(blob[i]);
    *build = value;
    if (content) *content = digest;
    if (tag) *tag = code;
    return true;
}
// Valid user codes contain only 4–8 ASCII letters/digits. The reserved prefix and full
// room ID therefore name a separate group even on a server sharing one communication ID.
inline std::string native_room_tag(uint64_t room)
{
    if (!room) return {};
    char group[40];
    std::snprintf(group, sizeof group, "native-room:%016llx", (unsigned long long)room);
    return code_tag(group);
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
// release the other player runs. A word not listed (a newer release, a local build) is named "build xxxxxxxx". Add each release when it is published.
struct KnownBuild { uint32_t build; const char* release; };
inline constexpr KnownBuild kKnownBuilds[] = { { 0xe9be7f43u, "0.7.0-test1" }, { 0xf1e06d4du, "0.7.0-test2" }, { 0x8a47e98au, "0.7.0-test3" } };
inline std::string release_name(uint32_t build)
{
    for (const KnownBuild& k : kKnownBuilds) if (k.build == build) return k.release;
    return "build " + build_hex(build);
}
// The message of a version mismatch (the message line and the first line of the menu notice). own_release: this game's release.txt ("" = by its build word).
// Two different words that name the same release label (for example a local build) are both shown as build words.
inline std::string version_mismatch_text(uint32_t other, uint32_t own, const std::string& own_release)
{
    std::string theirs = release_name(other), mine = own_release.empty() ? release_name(own) : own_release;
    if (theirs == mine) { theirs = "build " + build_hex(other); mine = "build " + build_hex(own); }
    return "The other player is on " + theirs + "; you're on " + mine + ". Both players need the same version for rollback.";
}
// What to check when the two games did not meet (the second line of the on-screen notice; the tail of the one-line text).
// auto_role: both players just pick re-psn (rollback) (nobody is the host first), code: a match code is set (the same one is needed).
inline std::string meet_advice(uint32_t build, int window_ms, bool auto_role = false, bool code = false)
{
    return std::string("start both games at about the same time (") + (auto_role ? "" : "the host first is best; ") + "each waits " + window_words(window_ms) + " after its own start-up); both need the same version (build " + build_hex(build) +
           ")" + (code ? ", the same match code" : "") + " and the same enabled mods in the same order";
}

// Field widths follow re-psn docs/api.md "Rooms" (v0.8.0): room ids are u64, member ids u16, user ids u32. ROOM_LIST names the owner by online id (no user id).
struct PsnrRoom { uint64_t id = 0; uint32_t flags = 0; std::string external; uint32_t owner = 0 /* user id when known, 0 from ROOM_LIST */; uint8_t members = 0, max_members = 0; std::string owner_name; };
struct PsnrMember { uint32_t user = 0; uint16_t member_id = 0; Endpoint ep; std::string data; };

// ROOM_LIST entries a rollback client may join: the flag bit, the same build record, and not full. Sorted by room id (the first is the oldest).
// exclude_id: a room that is ours (the search lists it too).
inline std::vector<PsnrRoom> filter_rooms(const std::vector<PsnrRoom>& all, uint32_t build, const jpsm::Digest& content, const std::string& tag = std::string(), uint64_t exclude_id = 0)
{
    std::vector<PsnrRoom> r;
    for (const PsnrRoom& x : all)
        if ((x.flags & kRoomFlagRollback) && record_matches(x.external, build, content, tag) && x.members < x.max_members && x.id != exclude_id) r.push_back(x);
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
    virtual void reset_search() = 0;
    virtual bool search(uint16_t offset, std::vector<PsnrRoom>& out, uint16_t& total) = 0;   // true when a ROOM_LIST answer is available (rooms searched with the rollback flag)
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
        // ENHANCEMENT (auto role): nobody picks Host or Join. SEARCH first: a matching room exists -> JOIN the oldest, else CREATE one. While a room is open the search
        // goes on every lookup interval; when another matching room with a LOWER id exists the room with the higher id yields (LEAVE, then JOIN the lower). The server
        // (re-psn server/rooms.go createRoom) gives every room the next value of one counter, so room ids are unique and ordered by creation: exactly one side yields.
        // host is ignored when auto_role is set; the role is known when the rendezvous is done (hosting()).
        bool auto_role = false;
        std::string code;               // the NORMALIZED match code (upper case), "" = none; never logged
        uint64_t join_room = 0;         // client: 0 = the oldest matching room, else that room (it must still match)
        uint32_t build = 0;
        uint64_t pair = 0;              // ENHANCEMENT (native-room pairing): the game's own room id both players are in (0 = none: the plain record); see make_record
        jpsm::Digest content{};
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
    RePsnRendezvous(IPsnrTransport& t, UdpMux& mux, MessageHub& hub, const Config& c) : t_(t), mux_(mux), hub_(hub), c_(c), tag_(c.pair ? native_room_tag(c.pair) : code_tag(c.code)), hosting_(c.auto_role ? false : c.host) {}
    // The role of this side once the games have met (auto role: whoever created the room hosts). Also the role while waiting (auto: false until a room is created).
    bool hosting() const { return hosting_; }
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
        if (c_.content == jpsm::Digest{}) return fail("game and mod content could not be verified");
        const uint64_t t = now_ms();
        if (!t0_) t0_ = t;
        // The window covers finding the other player only: once its endpoint is known the punch (and the relay, if allowed) has its own bound (punch_ms).
        if (state_ < kPunch && t - t0_ > (uint64_t)c_.give_up_ms) return give_up();
        if (const char* f = t_.failure()) return fail(f);   // the connection failed for good, or a lookup was refused: end now, not at the give-up
        if (state_ == kConnect && t_.ready()) state_ = kLookup;
        if (state_ < kPunch) progress(t);   // after the connect step: a game that is already connected never reports "connecting"
        if (state_ == kConnect) return 0;
        if (state_ == kLookup) {
            if (c_.host && !c_.auto_role) { t_.create_room(kRoomFlagRollback, make_record(c_.build, c_.content, tag_), make_record(c_.build, c_.content, tag_)); state_ = kRoom; }   // listed until the host leaves (give-up, failure or the end)
            else {
                if (next_lookup_ && t < next_lookup_) return 0;   // the last list had no room for us: wait before asking again
                uint64_t pick = 0;
                if (!search_page(0, &pick)) return 0;
                if (!pick && c_.pair && native_mismatch_) {
                    if (native_version_seen_) return fail_version(native_other_build_);
                    fail("the other player runs another version or different game content");
                    n1_ = "Rollback could not start: the other player has different game content.";
                    n2_ = "Use the same release and the same enabled mods.";
                    return -1;
                }
                if (c_.join_room && !pick) return fail("re-psn (rollback): that room is gone, full, or uses a different build, content or match code");
                if (!pick && c_.auto_role) {   // nobody there yet: open a room and keep looking while it is open
                    t_.create_room(kRoomFlagRollback, make_record(c_.build, c_.content, tag_), make_record(c_.build, c_.content, tag_));
                    hosting_ = true; join_pending_ = false; room_ = 0; state_ = kRoom; next_lookup_ = t + (uint64_t)c_.lookup_every_ms;
                    return 0;
                }
                else if (!pick) { next_lookup_ = t + (uint64_t)c_.lookup_every_ms; return 0; }   // no host yet (it may start later): keep looking until the window ends
                t_.join_room(pick, make_record(c_.build, c_.content, tag_)); room_ = pick; hosting_ = false; join_pending_ = true; state_ = kRoom;
            }
        }
        PsnrEvent ev;
        while (t_.poll_event(ev)) {
            if (ev.kind == PsnrEvent::Error) {
                // a join that failed (the room filled up or vanished between the list and the join): look again within the window, unless a fixed room was asked for
                if (join_pending_ && !hosting_ && !c_.join_room && state_ == kRoom) { restart_lookup(t, false); continue; }
                return fail("re-psn (rollback): the server refused (error " + std::to_string(ev.err) + ")");
            }
            if (ev.kind == PsnrEvent::RoomJoined) { room_ = ev.room_id; owner_ = ev.owner; members_ = ev.members; join_pending_ = false; }
            else if (ev.kind == PsnrEvent::MemberJoined) { for (auto& m : ev.members) members_.push_back(m); }
            else if (ev.kind == PsnrEvent::MemberLeft) {
                members_.erase(std::remove_if(members_.begin(), members_.end(), [&](const PsnrMember& m) { return m.user == ev.user; }), members_.end());
                if (ev.user == peer_user_ && state_ >= kPunch) return fail("re-psn (rollback): the other player left the room");
                if (ev.user == owner_ && !hosting_) {
                    // before the punch the room's owner leaving only means it is gone: look again (a session cannot move its host, but nothing was started yet)
                    if (state_ < kPunch && !c_.join_room) { restart_lookup(t, true); continue; }
                    return fail("re-psn (rollback): the host left the room");
                }
            }
        }
        // auto role, room open and nobody in it: another matching room with a lower id means this side yields (see Config::auto_role)
        if (c_.auto_role && hosting_ && state_ == kRoom && room_ && !join_pending_ && !peer_user_ && members_.size() <= 1) {
            if (!next_lookup_ || t >= next_lookup_) {
                uint64_t target = 0;
                if (search_page(room_, &target)) {
                    if (target && target < room_) {
                        t_.leave_room(); members_.clear(); owner_ = 0;
                        t_.join_room(target, make_record(c_.build, c_.content, tag_)); room_ = target; hosting_ = false; join_pending_ = true;
                        ++yields;
                    } else next_lookup_ = t + (uint64_t)c_.lookup_every_ms;
                }
            }
        }
        if (state_ == kRoom && room_) {
            // every OTHER member's record is checked; a mismatching member is refused (never played with). The host waits for the first match, the client looks at the owner.
            for (const PsnrMember& m : members_) {
                if (m.user == t_.self_user()) continue;
                if (!record_matches(m.data, c_.build, c_.content, tag_)) {
                    if (!hosting_) return fail("re-psn (rollback): the host's build or settings record does not match (refused)");
                    if (!rejected_.count(m.user)) { rejected_.insert(m.user); ++rejected_members; t_.kick(m.user); }   // never played with, and removed from the room so it cannot hold the slot
                    continue;
                }
                if (!hosting_ && m.user != owner_) continue;
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
                    return fail(relay_ ? "re-psn (rollback): the other player cannot be reached, even through the relay"
                              : !c_.allow_relay ? "re-psn (rollback): not reachable directly (relay is not allowed in the settings)"
                              : "re-psn (rollback): not reachable directly and this server offers no relay");
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
    uint64_t yields = 0, restarts = 0;                // auto role: times this side left its own room for an older one / went back to searching (counters for the tests and the log)
    // The failure as the two lines of the on-screen notice (what happened / what to do); line 2 is empty when error() says it all.
    const std::string& notice1() const { return n1_; }
    const std::string& notice2() const { return n2_; }
    bool version_mismatch() const { return mismatch_; }   // the failure was the other player's version: notice1() is version_mismatch_text, no retry can help
private:
    enum { kConnect = 0, kLookup, kRoom, kPunch, kDone, kFailed };
    int fail(const std::string& why) { err_ = why; n1_ = "Rollback could not start: " + why; n2_.clear(); state_ = kFailed; t_.leave_room(); return -1; }
    int fail_version(uint32_t other)
    {
        fail("re-psn: the other player runs another version (build " + build_hex(other) + ", yours " + build_hex(c_.build) + ")");
        n1_ = version_mismatch_text(other, c_.build, c_.release);
        n2_ = "Rollback is off in this room. Both need the same release and enabled mods; the main menu corner shows the version.";
        mismatch_ = true; return -1;
    }
    // The window ended before the other player was found. One line for the log and the screen, with the build word so the players can compare.
    int give_up()
    {
        std::string what;
        if (state_ == kConnect) what = "the re-psn server did not answer within " + window_words(c_.give_up_ms);
        else if (c_.auto_role && (state_ == kLookup || hosting_)) what = "no other player with this version" + std::string(c_.code.empty() ? "" : " and match code") + " found within " + window_words(c_.give_up_ms) + (!saw_other_rooms() ? std::string() : " (" + other_builds_text() + ")");
        else if (!hosting_ && state_ == kLookup) what = "no host with this version found within " + window_words(c_.give_up_ms) + (!saw_other_rooms() ? std::string() : " (" + other_builds_text() + ")") + " (is the other player set to Host?)";
        else if (hosting_ && c_.pair) what = "no compatible peer joined this Friendly Match within " + window_words(c_.give_up_ms);
        else if (hosting_) what = "no player with this version joined within " + window_words(c_.give_up_ms) + (rejected_members ? " (" + std::to_string(rejected_members) + " with another version refused)" : "");
        else what = "the host's room did not answer within " + window_words(c_.give_up_ms);
        const std::string advice = meet_advice(c_.build, c_.give_up_ms, c_.auto_role, !c_.code.empty());
        fail("re-psn (rollback): " + what + ": " + advice);
        n1_ = "Rollback could not start: " + what + ".";
        n2_ = advice + "."; if (n2_[0] >= 'a' && n2_[0] <= 'z') n2_[0] = (char)(n2_[0] - 'a' + 'A');
        return -1;
    }
    // At most one reply per poll. Retain only the smallest eligible ID across pages.
    bool search_page(uint64_t exclude, uint64_t* match)
    {
        std::vector<PsnrRoom> page;
        uint16_t total = 0;
        if (!t_.search(search_offset_, page, total)) return false;
        ++lookups;
        if (!search_offset_) { native_mismatch_ = false; native_version_seen_ = false; }
        note_other_builds(page);
        if (!search_offset_) scan_end_ = total;
        for (const auto& room : filter_rooms(page, c_.build, c_.content, tag_, exclude))
            if ((!c_.join_room || room.id == c_.join_room) && (!best_room_ || room.id < best_room_)) best_room_ = room.id;
        const size_t next = search_offset_ + page.size();
        if (!page.empty() && next < std::min(total, scan_end_)) {
            search_offset_ = static_cast<uint16_t>(next);
            return false;
        }
        *match = best_room_;
        search_offset_ = 0;
        best_room_ = 0;
        return true;
    }
    void note_other_builds(const std::vector<PsnrRoom>& all)
    {
        for (const PsnrRoom& x : all) {
            uint32_t b = 0; std::string tg;
            if (!(x.flags & kRoomFlagRollback)) continue;
            jpsm::Digest content;
            if (!record_build(x.external, &b, &tg, &content)) { saw_unverified_ = true; continue; }
            if (c_.pair && tg == tag_ && (b != c_.build || content != c_.content)) {
                native_mismatch_ = true;
                if (b != c_.build) { native_version_seen_ = true; native_other_build_ = b; }
            }
            if (b != c_.build) { if (other_builds_.size() < 4) other_builds_.insert(b); }
            else if (tg != tag_) other_codes_ = true;   // the same build with another match code (or none): counted, the code is never shown
            else if (content != c_.content) saw_other_content_ = true;
        }
    }
    bool saw_other_rooms() const { return !other_builds_.empty() || other_codes_ || saw_other_content_ || saw_unverified_; }
    std::string other_builds_text() const
    {
        std::string s;
        if (!other_builds_.empty()) {
            s = "seen: a rollback room with build";
            size_t i = 0; for (uint32_t b : other_builds_) { if (i == 3) { s += ", ..."; break; } s += (i ? ", " : " ") + build_hex(b); ++i; }
        }
        if (saw_other_content_) s += std::string(s.empty() ? "seen: " : "; ") + "a room with different game content or mods";
        if (saw_unverified_) s += std::string(s.empty() ? "seen: " : "; ") + "a room without a compatible content record (update both games)";
        if (other_codes_) s += std::string(s.empty() ? "seen: " : "; ") + "a rollback room with another match code (or none)";
        return s;
    }
    // Back to searching (auto role, or a joiner whose room filled up or vanished). leave: this side is in a room (it may have become the owner of the abandoned one).
    void restart_lookup(uint64_t, bool leave)
    {
        t_.reset_search(); search_offset_ = 0; best_room_ = 0;
        if (leave) t_.leave_room();
        members_.clear(); room_ = 0; owner_ = 0; peer_user_ = 0; hosting_ = false; join_pending_ = false; state_ = kLookup; next_lookup_ = 0;
        ++restarts;
    }
    void progress(uint64_t t)
    {
        if (last_progress_ && t - last_progress_ < (uint64_t)c_.progress_every_ms) return;
        last_progress_ = t;
        if (!on_progress) return;
        const std::string w = std::to_string((t - t0_) / 1000) + " s of " + std::to_string(c_.give_up_ms / 1000) + " s";
        const std::string b = "build " + build_hex(c_.build);
        std::string line, n1, n2;
        const std::string rl = c_.auto_role ? "re-psn (rollback): " : c_.host ? "re-psn (rollback), Host: " : "re-psn (rollback), Join: ";   // the role, plainly (explicit roles: two Joins never meet)
        if (state_ == kConnect) { line = "re-psn (rollback): connecting to the server (" + w + ", " + b + ")"; n1 = rl + "connecting to the server (" + w + ")."; }
        else if (hosting_) {
            line = "re-psn (rollback): room open, waiting for a player with " + b + " (" + w + (rejected_members ? ", " + std::to_string(rejected_members) + " with another version refused" : "") + ")";
            n1 = rl + "room open, waiting for the other player (" + w + ").";
        } else if (c_.auto_role && (state_ == kLookup || join_pending_)) {
            line = std::string("re-psn (rollback): ") + (state_ == kLookup ? "looking for the other player with " : "joining the other player's room, with ") + b + " (" + w + ", " + std::to_string(lookups) + " room lists read" + (!saw_other_rooms() ? std::string() : "; " + other_builds_text()) + ")";
            n1 = rl + (state_ == kLookup ? "looking for the other player (" : "found the other player's room, joining (") + w + ").";
        } else if (state_ == kLookup) {
            line = "re-psn (rollback): looking for a host with " + b + " (" + w + ", " + std::to_string(lookups) + " room lists read" + (!saw_other_rooms() ? std::string() : "; " + other_builds_text()) + ")";
            n1 = rl + "looking for the host's room (" + w + ").";
        } else { line = "re-psn (rollback): joined room " + std::to_string((unsigned long long)room_) + ", waiting for the host (" + w + ", " + b + ")"; n1 = rl + "joined the host's room, waiting for it (" + w + ")."; }
        n2 = "Start the match from Free Battle, not the Online menu. Same version (build " + build_hex(c_.build) + ") and mods needed.";
        if (search_offset_) n2 = "Checking more rooms for matching game content and mods.";
        on_progress(line, n1, n2);
    }
    void Path_(Path p) { path_ = p; if (on_path) on_path(p, peer_user_); }
    void start_inner()
    {
        if (hosting_) {
            inner_.reset(new DirectRendezvous(mux_, hub_, c_.local_count, room_));   // the room id is the session id
            inner_->set_only_peer(peer_.ep);   // a HELO from any other endpoint is dropped (relayed datagrams are rewritten to this endpoint by the transport's rx hook)
        }
        else inner_.reset(new DirectRendezvous(mux_, hub_, c_.local_count, peer_.ep));
    }
    IPsnrTransport& t_; UdpMux& mux_; MessageHub& hub_; Config c_;
    int state_ = kConnect; uint64_t t0_ = 0, punch_start_ = 0, last_punch_ = 0, next_lookup_ = 0, last_progress_ = 0;
    bool native_mismatch_ = false, native_version_seen_ = false, mismatch_ = false;
    uint32_t native_other_build_ = 0;
    uint16_t search_offset_ = 0, scan_end_ = 0;
    uint64_t best_room_ = 0;
    bool saw_other_content_ = false, saw_unverified_ = false;
    std::set<uint32_t> other_builds_; bool other_codes_ = false; std::string n1_, n2_;
    std::string tag_; bool hosting_ = false, join_pending_ = false;   // tag_: the match code's hash in the record; join_pending_: a JOIN_ROOM is out and unanswered
    uint64_t room_ = 0; uint32_t owner_ = 0, peer_user_ = 0; int local_index_ = -1; bool relay_ = false;
    std::vector<PsnrMember> members_; PsnrMember peer_; std::set<uint32_t> rejected_;
    std::unique_ptr<DirectRendezvous> inner_; SessionPlan plan_; std::string err_; MessageChannel* ch_ = nullptr; Path path_ = Path::None;
};

}  // namespace jspnet

#endif
