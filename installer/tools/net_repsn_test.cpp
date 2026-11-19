// net_repsn_test.cpp -- host test of the re-psn rendezvous (jsp_net_repsn.h) with a FAKE psnr transport and real loopback UDP (two UdpMux): room list filtering, version
// refusal before joining and at join, roles and player index, punch then the HELO/PLAN start, relay only when allowed and offered. Build and run: installer/tools/net_repsn_test.sh.
#include "jsp_net_repsn.h"
#include <cstdio>
#include <thread>

using namespace jspnet;
static int g_fail = 0, g_n = 0;
#define CHECK(c) do { g_n++; if (!(c)) { g_fail++; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

struct FakeServer {
    struct Room { PsnrRoom r; std::vector<PsnrMember> members; };
    std::vector<Room> rooms; uint64_t next_room = 100; uint32_t next_member = 1; bool relay = false;
};
struct FakeClient : IPsnrTransport {
    FakeServer& sv; uint32_t user; Endpoint ep; bool is_ready = true;
    std::vector<PsnrEvent> ev; int punches = 0, joins = 0, leaves = 0; FakeClient* peers[2] = { nullptr, nullptr };
    FakeClient(FakeServer& s, uint32_t u, const Endpoint& e) : sv(s), user(u), ep(e) {}
    bool ready() override { return is_ready; }
    uint32_t self_user() override { return user; }
    bool relay_offered() override { return sv.relay; }
    bool search(std::vector<PsnrRoom>& out) override { out.clear(); for (auto& r : sv.rooms) out.push_back(r.r); return true; }
    void create_room(uint32_t flags, const std::string& external, const std::string& data) override {
        FakeServer::Room r; r.r.id = sv.next_room++; r.r.flags = flags; r.r.external = external; r.r.owner = user; r.r.members = 1; r.r.max_members = 2;
        r.members.push_back(PsnrMember{ user, sv.next_member++, ep, data });
        PsnrEvent e; e.kind = PsnrEvent::RoomJoined; e.room_id = r.r.id; e.owner = user; e.members = r.members; ev.push_back(e);
        sv.rooms.push_back(r);
    }
    void join_room(uint64_t id, const std::string& data) override {
        joins++;
        for (auto& r : sv.rooms) if (r.r.id == id) {
            PsnrMember me{ user, (uint16_t)sv.next_member++, ep, data }; r.members.push_back(me); r.r.members++;
            PsnrEvent e; e.kind = PsnrEvent::RoomJoined; e.room_id = id; e.owner = r.r.owner; e.members = r.members; ev.push_back(e);
            for (FakeClient* p : peers) if (p && p->user == r.r.owner) { PsnrEvent j; j.kind = PsnrEvent::MemberJoined; j.room_id = id; j.members = { me }; p->ev.push_back(j); }
        }
    }
    bool poll_event(PsnrEvent& e) override { if (ev.empty()) return false; e = ev.front(); ev.erase(ev.begin()); return true; }
    void send_punch(const Endpoint&) override { punches++; }
    void leave_room() override { leaves++; }
};

struct Side {
    UdpMux mux; std::unique_ptr<MessageHub> hub; std::unique_ptr<FakeClient> cl; std::unique_ptr<RePsnRendezvous> rz; uint64_t clock = 1000; Path last = Path::None; int relay_calls = 0;
    void init(FakeServer& sv, uint32_t user, RePsnRendezvous::Config c) {
        std::string err; CHECK(mux.open(0, &err));
        Endpoint ep; char b[32]; std::snprintf(b, sizeof b, "127.0.0.1:%u", (unsigned)mux.local_port()); CHECK(parse_endpoint(b, ep));
        hub.reset(new MessageHub(mux)); cl.reset(new FakeClient(sv, user, ep));
        rz.reset(new RePsnRendezvous(*cl, mux, *hub, c));
        rz->now_ms = [this] { return clock; };
        rz->on_path = [this](Path p, uint32_t) { last = p; };
    }
};
// drives both sides until both are decided or `steps` run out; each step advances the fake clock 50 ms and sleeps 2 ms so the real loopback UDP can deliver
static void drive(Side& a, Side& b, int steps, int* ra, int* rb)
{
    *ra = *rb = 0;
    for (int i = 0; i < steps && (*ra == 0 || *rb == 0); i++) {
        a.clock += 50; b.clock += 50;
        a.mux.pump(); b.mux.pump(); a.hub->pump(); b.hub->pump();
        if (*ra == 0) *ra = a.rz->poll();
        if (*rb == 0) *rb = b.rz->poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
static RePsnRendezvous::Config cfg(bool host, uint32_t build, bool relay = false) { RePsnRendezvous::Config c; c.host = host; c.build = build; c.allow_relay = relay; return c; }

int main()
{
    const uint32_t B = 0xA1B2C3D4u;
    // ---- record and room list ----
    CHECK(make_record(B) == "rb=1;ver=a1b2c3d4" && record_matches("rb=1;ver=a1b2c3d4", B) && !record_matches("rb=1;ver=a1b2c3d5", B) && !record_matches("rb=1;ver=a1b2c3d4;x=1", B) && !record_matches("", B));
    { std::vector<PsnrRoom> all(5);
      all[0] = { 7, kRoomFlagRollback, make_record(B), 1, 1, 2 };            // ok
      all[1] = { 3, kRoomFlagRollback, make_record(B), 1, 1, 2 };            // ok, older
      all[2] = { 4, 0, make_record(B), 1, 1, 2 };                            // no flag (a game room)
      all[3] = { 5, kRoomFlagRollback, make_record(B + 1), 1, 1, 2 };        // other build
      all[4] = { 6, kRoomFlagRollback, make_record(B), 1, 2, 2 };            // full
      const auto f = filter_rooms(all, B); CHECK(f.size() == 2 && f[0].id == 3 && f[1].id == 7); }
    { std::vector<PsnrMember> m = { { 50, 9, {}, "" }, { 60, 2, {}, "" }, { 70, 5, {}, "" } };
      CHECK(player_index(m, 60) == 0 && player_index(m, 70) == 1 && player_index(m, 50) == 2 && player_index(m, 99) == -1); }

    // ---- punch, then the plan: roles, session id = room id, player index, path "direct" ----
    { FakeServer sv; Side h, c; h.init(sv, 11, cfg(true, B)); c.init(sv, 22, cfg(false, B)); h.cl->peers[0] = c.cl.get(); c.cl->peers[0] = h.cl.get();
      int rh, rc; drive(h, c, 400, &rh, &rc);
      CHECK(rh == 1 && rc == 1);
      CHECK(h.rz->path() == Path::Direct && c.rz->path() == Path::Direct && h.relay_calls == 0);
      CHECK(h.rz->plan().host && !c.rz->plan().host && h.rz->plan().session_id == h.rz->room_id() && c.rz->plan().session_id == h.rz->room_id());
      CHECK(h.rz->local_index() == 0 && c.rz->local_index() == 1);
      CHECK(h.cl->punches > 0 && c.cl->punches > 0);                         // PUNCH went out before the session started
      CHECK(sv.rooms.size() == 1 && (sv.rooms[0].r.flags & kRoomFlagRollback) && sv.rooms[0].r.external == make_record(B) && sv.rooms[0].members[0].data == make_record(B)); }

    // ---- version mismatch is refused BEFORE joining (room list) ----
    { FakeServer sv; Side h, c; h.init(sv, 11, cfg(true, B + 1)); c.init(sv, 22, cfg(false, B)); h.cl->peers[0] = c.cl.get();
      int rh = 0, rc = 0; for (int i = 0; i < 4; i++) { h.rz->poll(); rc = c.rz->poll(); }
      CHECK(rc == -1 && c.cl->joins == 0 && c.rz->error().find("no rollback room for this build") != std::string::npos && c.cl->leaves == 1); (void)rh; }
    // a named room that is gone / another build: refused too
    { FakeServer sv; Side h, c; h.init(sv, 11, cfg(true, B)); auto cc = cfg(false, B); cc.join_room = 999; c.init(sv, 22, cc);
      h.rz->poll(); int rc = c.rz->poll(); CHECK(rc == -1 && c.cl->joins == 0); }
    // ---- mismatch at join: the owner's member data differs (a client refuses the host) ----
    { FakeServer sv; Side h, c; h.init(sv, 11, cfg(true, B)); c.init(sv, 22, cfg(false, B)); h.cl->peers[0] = c.cl.get();
      h.rz->poll(); sv.rooms[0].members[0].data = "rb=1;ver=00000000";     // the room list still says B, the member entry does not
      int rc = 0; for (int i = 0; i < 4; i++) rc = c.rz->poll();
      CHECK(rc == -1 && c.rz->error().find("does not match") != std::string::npos); }
    // the host refuses a joiner whose data differs and keeps waiting (counted), the session never starts
    { FakeServer sv; Side h, c; h.init(sv, 11, cfg(true, B)); c.init(sv, 22, cfg(false, B)); h.cl->peers[0] = c.cl.get(); c.cl->peers[0] = h.cl.get();
      h.rz->poll(); c.rz->poll();
      for (auto& m : sv.rooms[0].members) if (m.user == 22) m.data = "rb=1;ver=deadbeef";
      for (auto& e : h.cl->ev) for (auto& m : e.members) if (m.user == 22) m.data = "rb=1;ver=deadbeef";
      int r = 0; for (int i = 0; i < 5; i++) { h.clock += 50; r = h.rz->poll(); }
      CHECK(r == 0 && h.rz->rejected_members == 1 && h.rz->path() == Path::None); }

    // ---- relay only when allowed AND offered: the direct path is dead (every datagram dropped), the relay is stood in for by lifting the drop ----
    auto dead = [](Side& s) { LinkModel lm; lm.loss_permille = 1000; s.mux.set_link_model(lm); };
    auto alive = [](Side& s) { s.mux.set_link_model(LinkModel()); };
    for (int variant = 0; variant < 4; variant++) {   // 0 not allowed (not offered), 1 allowed but the server offers none, 2 allowed and offered, 3 offered but NOT allowed
        const bool allow = variant == 1 || variant == 2;
        FakeServer sv; sv.relay = variant >= 2; Side h, c; h.init(sv, 11, cfg(true, B, allow)); c.init(sv, 22, cfg(false, B, allow));
        h.cl->peers[0] = c.cl.get(); c.cl->peers[0] = h.cl.get(); dead(h); dead(c);
        h.rz->on_path = [&](Path p, uint32_t) { h.last = p; if (p == Path::Relay) { h.relay_calls++; alive(h); } };
        c.rz->on_path = [&](Path p, uint32_t) { c.last = p; if (p == Path::Relay) { c.relay_calls++; alive(c); } };
        int rh, rc; drive(h, c, 600, &rh, &rc);
        if (variant == 2) { CHECK(rh == 1 && rc == 1 && h.rz->path() == Path::Relay && c.rz->path() == Path::Relay && h.relay_calls >= 1 && c.relay_calls >= 1); }
        else {
            CHECK((rh == -1 || rc == -1) && h.relay_calls == 0 && c.relay_calls == 0);
            const std::string e = rh == -1 ? h.rz->error() : c.rz->error();
            CHECK(e.find(variant == 0 || variant == 3 ? "relay is not allowed" : "offers no relay") != std::string::npos);
        }
    }
    // a direct path never asks for the relay even when allowed and offered
    { FakeServer sv; sv.relay = true; Side h, c; h.init(sv, 11, cfg(true, B, true)); c.init(sv, 22, cfg(false, B, true)); h.cl->peers[0] = c.cl.get(); c.cl->peers[0] = h.cl.get();
      int rh, rc; drive(h, c, 400, &rh, &rc); CHECK(rh == 1 && rc == 1 && h.rz->path() == Path::Direct && c.rz->path() == Path::Direct); }
    // a stranger that knows the host's UDP port sends a well-formed HELO first: the host's session is with the room's other player, never with the stranger
    { FakeServer sv; Side h, c, x; h.init(sv, 11, cfg(true, B)); c.init(sv, 22, cfg(false, B)); x.init(sv, 33, cfg(false, B)); h.cl->peers[0] = c.cl.get(); c.cl->peers[0] = h.cl.get();
      MessageChannel* ch = x.hub->open(h.cl->ep); const uint8_t hello[5] = { 'H', 'E', 'L', 'O', 1 };
      for (int i = 0; i < 40 && ch; i++) { ch->send(hello, 5); x.mux.pump(); x.hub->pump(); h.mux.pump(); h.hub->pump(); }   // the stranger first: its HELO is already queued at the host
      int rh, rc; drive(h, c, 600, &rh, &rc);
      CHECK(rh == 1 && rc == 1 && h.rz->plan().remotes.size() == 1 && h.rz->plan().remotes[0].ep == c.cl->ep); }
    // the other player leaving during the punch ends it
    { FakeServer sv; Side h, c; h.init(sv, 11, cfg(true, B)); c.init(sv, 22, cfg(false, B)); h.cl->peers[0] = c.cl.get(); c.cl->peers[0] = h.cl.get(); dead(h); dead(c);
      h.rz->poll(); c.rz->poll(); for (int i = 0; i < 6; i++) { h.clock += 50; h.rz->poll(); }
      PsnrEvent e; e.kind = PsnrEvent::MemberLeft; e.user = 22; h.cl->ev.push_back(e); int r = h.rz->poll(); CHECK(r == -1 && h.rz->error().find("left") != std::string::npos); }

    if (g_fail) { std::printf("%d of %d checks FAILED\n", g_fail, g_n); return 1; }
    std::printf("net repsn: all %d checks passed\n", g_n);
    return 0;
}
