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
#include <chrono>
#include <cstdio>
#include <functional>
#include <set>

namespace jspnet {

// ASSUMPTION: bit 30 of the room flags is "this room holds a rollback session" (the convention says "a reserved bit"; the server stores flags and does not read them).
constexpr uint32_t kRoomFlagRollback = 0x40000000u;

// The record in the external blob and in member data: "rb=1;ver=<8 hex digits of the build id>". Exactly this shape; anything else does not match.
inline std::string make_record(uint32_t build) { char b[40]; std::snprintf(b, sizeof b, "rb=1;ver=%08x", (unsigned)build); return b; }
inline bool record_matches(const std::string& blob, uint32_t build) { return blob == make_record(build); }

// Field widths follow re-psn docs/api.md "Rooms" (v0.8.0): room ids are u64, member ids u16, user ids u32. ROOM_LIST names the owner by online id (no user id).
struct PsnrRoom { uint64_t id = 0; uint32_t flags = 0; std::string external; uint32_t owner = 0 /* user id when known, 0 from ROOM_LIST */; uint8_t members = 0, max_members = 0; std::string owner_name; };
struct PsnrMember { uint32_t user = 0; uint16_t member_id = 0; Endpoint ep; std::string data; };

// ROOM_LIST entries a rollback client may join: the flag bit, the same build record, and not full. Sorted by room id (the first is the oldest).
inline std::vector<PsnrRoom> filter_rooms(const std::vector<PsnrRoom>& all, uint32_t build)
{
    std::vector<PsnrRoom> r;
    for (const PsnrRoom& x : all)
        if ((x.flags & kRoomFlagRollback) && record_matches(x.external, build) && x.members < x.max_members) r.push_back(x);
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

class RePsnRendezvous : public IRendezvous {
public:
    struct Config {
        bool host = true;
        uint64_t join_room = 0;         // client: 0 = the oldest matching room, else that room (it must still match)
        uint32_t build = 0;
        bool allow_relay = false;       // the player's choice
        uint8_t local_count = 1;
        int punch_ms = 4000;            // direct/punch window before relay is considered
        int give_up_ms = 30000;         // the whole rendezvous
    };
    RePsnRendezvous(IPsnrTransport& t, UdpMux& mux, MessageHub& hub, const Config& c) : t_(t), mux_(mux), hub_(hub), c_(c) {}
    // when the path changes: Relay means the mux must send through the server from now on (psnr_relay_wrap / psnr_relay_unwrap_from); the glue sets that up
    std::function<void(Path, uint32_t peer_user)> on_path;
    std::function<uint64_t()> now_ms = [] { return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); };

    int poll() override
    {
        if (state_ == kDone) return 1;
        if (state_ == kFailed) return -1;
        const uint64_t t = now_ms();
        if (!t0_) t0_ = t;
        if (t - t0_ > (uint64_t)c_.give_up_ms) return fail("re-psn: timed out waiting for the other player");
        if (const char* f = t_.failure()) return fail(f);   // the connection failed for good, or a lookup was refused: end now, not at the give-up
        if (state_ == kConnect) { if (!t_.ready()) return 0; state_ = kLookup; }
        if (state_ == kLookup) {
            if (c_.host) { t_.create_room(kRoomFlagRollback, make_record(c_.build), make_record(c_.build)); state_ = kRoom; }
            else {
                std::vector<PsnrRoom> all;
                if (!t_.search(all)) return 0;
                const std::vector<PsnrRoom> ok = filter_rooms(all, c_.build);
                uint64_t pick = 0;
                if (c_.join_room) { for (const auto& r : ok) if (r.id == c_.join_room) pick = r.id;
                    if (!pick) return fail("re-psn: that room is gone, full, or from another build"); }
                else if (!ok.empty()) pick = ok.front().id;
                else return fail("re-psn: no rollback room for this build");
                t_.join_room(pick, make_record(c_.build)); room_ = pick; state_ = kRoom;
            }
        }
        PsnrEvent ev;
        while (t_.poll_event(ev)) {
            if (ev.kind == PsnrEvent::Error) return fail("re-psn: the server refused (error " + std::to_string(ev.err) + ")");
            if (ev.kind == PsnrEvent::RoomJoined) { room_ = ev.room_id; owner_ = ev.owner; members_ = ev.members; }
            else if (ev.kind == PsnrEvent::MemberJoined) { for (auto& m : ev.members) members_.push_back(m); }
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
                if (!record_matches(m.data, c_.build)) {
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
private:
    enum { kConnect = 0, kLookup, kRoom, kPunch, kDone, kFailed };
    int fail(const std::string& why) { err_ = why; state_ = kFailed; t_.leave_room(); return -1; }
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
    int state_ = kConnect; uint64_t t0_ = 0, punch_start_ = 0, last_punch_ = 0;
    uint64_t room_ = 0; uint32_t owner_ = 0, peer_user_ = 0; int local_index_ = -1; bool relay_ = false;
    std::vector<PsnrMember> members_; PsnrMember peer_; std::set<uint32_t> rejected_;
    std::unique_ptr<DirectRendezvous> inner_; SessionPlan plan_; std::string err_; MessageChannel* ch_ = nullptr; Path path_ = Path::None;
};

}  // namespace jspnet

#endif
