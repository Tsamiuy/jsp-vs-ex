// net_repsn_test.cpp -- host test of the re-psn rendezvous (jsp_net_repsn.h) with a FAKE psnr transport and real loopback UDP (two UdpMux): room list filtering, version
// refusal before joining and at join, roles and player index, punch then the HELO/PLAN start, relay only when allowed and offered; the rendezvous window (a joiner that starts
// before the host keeps reading the room list at the poll interval and joins when the room appears; either side gives up at the window with the text that says what to do;
// the host's room stays listed while it waits; progress every 10 s). Build and run: installer/tools/net_repsn_test.sh.
#include "jsp_net_repsn.h"
#include <cstdio>
#include <functional>
#include <thread>

using namespace jspnet;
// Embedded NULs exercise byte-counted external and member records.
static const jpsm::Digest content = [] { jpsm::Digest d{}; d.back() = 0xff; return d; }();

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { g_n++; if (!(c)) { g_fail++; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

struct FakeServer {
    struct Room { PsnrRoom r; std::vector<PsnrMember> members; };
    std::vector<Room> rooms; uint64_t next_room = 100; uint32_t next_member = 1; bool relay = false;
};
struct FakeClient : IPsnrTransport {
    FakeServer& sv; uint32_t user; Endpoint ep; bool is_ready = true;
    std::vector<PsnrEvent> ev; int punches = 0, joins = 0, leaves = 0; FakeClient* peers[2] = { nullptr, nullptr };
    bool fail_next_join = false;   // the next JOIN_ROOM is answered with an error (the room filled up or vanished between the list and the join)
    int searches = 0; std::vector<uint64_t> search_at; std::function<uint64_t()> clock;   // every room-list request, at the fake time
    FakeClient(FakeServer& s, uint32_t u, const Endpoint& e) : sv(s), user(u), ep(e) {}
    bool ready() override { return is_ready; }
    uint32_t self_user() override { return user; }
    bool relay_offered() override { return sv.relay; }
    int latency = 0, pending = -1; std::vector<PsnrRoom> snap;   // latency: a room list is taken at the first call and answered that many polls later (two games asking at the same time see the same, empty, list)
    void reset_search() override { pending = -1; }
    bool search(uint16_t offset, std::vector<PsnrRoom>& out, uint16_t& total) override {
        if (pending < 0) { searches++; if (clock) search_at.push_back(clock()); snap.clear(); for (auto& r : sv.rooms) snap.push_back(r.r); pending = latency; }
        if (pending > 0) { pending--; return false; }
        pending = -1; total = (uint16_t)snap.size();
        size_t start = std::min<size_t>(offset, snap.size());
        out.assign(snap.begin() + start, snap.begin() + std::min(start + 64, snap.size())); return true;
    }
    void create_room(uint32_t flags, const std::string& external, const std::string& data) override {
        FakeServer::Room r; r.r.id = sv.next_room++; r.r.flags = flags; r.r.external = external; r.r.owner = user; r.r.members = 1; r.r.max_members = 2;
        r.members.push_back(PsnrMember{ user, sv.next_member++, ep, data });
        PsnrEvent e; e.kind = PsnrEvent::RoomJoined; e.room_id = r.r.id; e.owner = user; e.members = r.members; ev.push_back(e);
        sv.rooms.push_back(r);
    }
    void join_room(uint64_t id, const std::string& data) override {
        joins++;
        if (fail_next_join) { fail_next_join = false; PsnrEvent e; e.kind = PsnrEvent::Error; e.err = 3; ev.push_back(e); return; }
        { bool found = false; for (auto& r : sv.rooms) if (r.r.id == id && r.r.members < r.r.max_members) found = true; if (!found) { PsnrEvent e; e.kind = PsnrEvent::Error; e.err = 3; ev.push_back(e); return; } }
        for (auto& r : sv.rooms) if (r.r.id == id) {
            PsnrMember me{ user, (uint16_t)sv.next_member++, ep, data }; r.members.push_back(me); r.r.members++;
            PsnrEvent e; e.kind = PsnrEvent::RoomJoined; e.room_id = id; e.owner = r.r.owner; e.members = r.members; ev.push_back(e);
            for (FakeClient* p : peers) if (p && p->user == r.r.owner) { PsnrEvent j; j.kind = PsnrEvent::MemberJoined; j.room_id = id; j.members = { me }; p->ev.push_back(j); }
        }
    }
    bool poll_event(PsnrEvent& e) override { if (ev.empty()) return false; e = ev.front(); ev.erase(ev.begin()); return true; }
    void send_punch(const Endpoint&) override { punches++; }
    // like the server: the member goes, the others are told (MEMBER_LEFT with the owner after the leave), an empty room closes
    void leave_room() override {
        leaves++;
        for (size_t i = 0; i < sv.rooms.size(); i++) {
            auto& r = sv.rooms[i]; bool mine = false;
            for (size_t k = 0; k < r.members.size(); k++) if (r.members[k].user == user) { r.members.erase(r.members.begin() + k); mine = true; break; }
            if (!mine) continue;
            r.r.members--;
            if (r.members.empty()) { sv.rooms.erase(sv.rooms.begin() + i); break; }
            if (r.r.owner == user) r.r.owner = r.members[0].user;
            for (FakeClient* p : peers) if (p && p->user != user) for (auto& m : r.members) if (m.user == p->user) { PsnrEvent e; e.kind = PsnrEvent::MemberLeft; e.user = user; e.owner = r.r.owner; p->ev.push_back(e); }
            break;
        }
    }
};

struct Side {
    UdpMux mux; std::unique_ptr<MessageHub> hub; std::unique_ptr<FakeClient> cl; std::unique_ptr<RePsnRendezvous> rz; uint64_t clock = 1000; Path last = Path::None; int relay_calls = 0;
    std::vector<std::string> progress; std::string n1, n2;   // on_progress lines and the last notice
    void init(FakeServer& sv, uint32_t user, RePsnRendezvous::Config c) {
        std::string err; CHECK(mux.open(0, &err));
        Endpoint ep; char b[32]; std::snprintf(b, sizeof b, "127.0.0.1:%u", (unsigned)mux.local_port()); CHECK(parse_endpoint(b, ep));
        hub.reset(new MessageHub(mux)); cl.reset(new FakeClient(sv, user, ep));
        rz.reset(new RePsnRendezvous(*cl, mux, *hub, c));
        rz->now_ms = [this] { return clock; };
        rz->on_path = [this](Path p, uint32_t) { last = p; };
        rz->on_progress = [this](const std::string& l, const std::string& a, const std::string& b) { progress.push_back(l); n1 = a; n2 = b; };
        cl->clock = [this] { return clock; };
    }
    // polls this side alone for `ms` of fake time in `step` ms steps; the result of the last poll (stops at a decision)
    int run_alone(uint64_t ms, uint64_t step = 50) { int r = 0; for (uint64_t e = 0; e < ms && r == 0; e += step) { clock += step; r = rz->poll(); } return r; }
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
static RePsnRendezvous::Config acfg(uint32_t build, const std::string& code = std::string(), int give_up = 20000)
{ RePsnRendezvous::Config c; c.auto_role = true; c.build = build; c.content = content; c.code = code; c.give_up_ms = give_up; c.lookup_every_ms = 500; return c; }
static RePsnRendezvous::Config cfg(bool host, uint32_t build, bool relay = false) { RePsnRendezvous::Config c; c.host = host; c.build = build; c.content = content; c.allow_relay = relay; return c; }

int main()
{
    const uint32_t B = 0xA1B2C3D4u;
    // ---- record and room list ----
    const std::string golden = std::string("JRB2\xA1\xB2\xC3\xD4", 8) + std::string(31, '\0') + char(0xff);
    CHECK(make_record(B, content) == golden && record_matches(golden, B, content));
    CHECK(!record_matches("rb=1;ver=a1b2c3d4", B, content) && !record_matches("", B, {}) && !record_matches(golden + "x", B, content));
    { std::vector<PsnrRoom> all(5);
      all[0] = { 7, kRoomFlagRollback, make_record(B, content), 1, 1, 2 };            // ok
      all[1] = { 3, kRoomFlagRollback, make_record(B, content), 1, 1, 2 };            // ok, older
      all[2] = { 4, 0, make_record(B, content), 1, 1, 2 };                            // no flag (a game room)
      all[3] = { 5, kRoomFlagRollback, make_record(B + 1, content), 1, 1, 2 };        // other build
      all[4] = { 6, kRoomFlagRollback, make_record(B, content), 1, 2, 2 };            // full
      const auto f = filter_rooms(all, B, content); CHECK(f.size() == 2 && f[0].id == 3 && f[1].id == 7); }
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
      CHECK(sv.rooms.size() == 1 && (sv.rooms[0].r.flags & kRoomFlagRollback) && sv.rooms[0].r.external == make_record(B, content) && sv.rooms[0].members[0].data == make_record(B, content)); }

    // ---- version mismatch: never joined (room list); the joiner keeps looking, and its give-up names the build it saw ----
    { FakeServer sv; Side h, c; h.init(sv, 11, cfg(true, B + 1)); c.init(sv, 22, cfg(false, B)); h.cl->peers[0] = c.cl.get();
      int rc = 0; for (int i = 0; i < 4; i++) { h.rz->poll(); rc = c.rz->poll(); }
      CHECK(rc == 0 && c.cl->joins == 0);
      rc = c.run_alone(130000);
      CHECK(rc == -1 && c.cl->joins == 0 && c.cl->leaves == 1);
      CHECK(!c.progress.empty() && c.progress.back().find(" room lists read; seen: a rollback room with build a1b2c3d5)") != std::string::npos);   // one level of parentheses
      CHECK(c.rz->error() == "re-psn (rollback): no host with this version found within 2 minutes (seen: a rollback room with build a1b2c3d5) (is the other player set to Host?): start both games at about the same time "
                             "(the host first is best; each waits 2 minutes after its own start-up); both need the same version (build a1b2c3d4) and the same enabled mods in the same order"); }

    // ---- the rendezvous window ----
    CHECK(kRendezvousWindowMs == 120000 && RePsnRendezvous::Config().give_up_ms == kRendezvousWindowMs && RePsnRendezvous::Config().lookup_every_ms == 2000);
    CHECK(window_words(120000) == "2 minutes" && window_words(60000) == "1 minute" && window_words(20000) == "20 seconds" && window_words(1000) == "1 second" && window_words(90000) == "90 seconds");
    { uint32_t b = 0; CHECK(record_build(golden, &b) && b == B && !record_build("rb=1;ver=0a1b2c3d", &b) && !record_build("", &b)); }
    // the joiner starts first: it reads the room list every 2 s (never faster), and joins when the host's room appears 60 s later; they meet
    { FakeServer sv; Side h, c; c.init(sv, 22, cfg(false, B)); h.init(sv, 11, cfg(true, B)); h.cl->peers[0] = c.cl.get(); c.cl->peers[0] = h.cl.get();
      CHECK(c.run_alone(60100) == 0 && c.cl->joins == 0 && sv.rooms.empty());
      CHECK(c.cl->searches >= 29 && c.cl->searches <= 31 && c.rz->lookups == (uint64_t)c.cl->searches);
      bool spaced = true; for (size_t i = 1; i < c.cl->search_at.size(); i++) if (c.cl->search_at[i] - c.cl->search_at[i - 1] < 2000) spaced = false;
      CHECK(spaced);
      CHECK(c.progress[0].find("re-psn (rollback): looking for a host with build a1b2c3d4 (0 s of 120 s, ") == 0);   // already connected: never "connecting"
      CHECK(c.progress.size() == 7 && c.progress[6].find("re-psn (rollback): looking for a host with build a1b2c3d4 (60 s of 120 s, ") == 0 && c.progress[6].find("room lists read") != std::string::npos);
      CHECK(c.n1 == "re-psn (rollback), Join: looking for the host's room (60 s of 120 s)." && c.n2 == "Start the match from Free Battle, not the Online menu. Same version (build a1b2c3d4) and mods needed.");
      int rh, rc; drive(h, c, 400, &rh, &rc);
      CHECK(rh == 1 && rc == 1 && c.cl->joins == 1 && h.rz->path() == Path::Direct && c.rz->plan().session_id == h.rz->room_id()); }
    // the host starts first and waits 100 s with its room listed the whole time; a joiner that starts then still meets it
    { FakeServer sv; Side h, c; h.init(sv, 11, cfg(true, B)); c.init(sv, 22, cfg(false, B)); h.cl->peers[0] = c.cl.get(); c.cl->peers[0] = h.cl.get();
      CHECK(h.run_alone(100100) == 0 && sv.rooms.size() == 1 && h.cl->leaves == 0);
      CHECK(h.progress[0] == "re-psn (rollback): room open, waiting for a player with build a1b2c3d4 (0 s of 120 s)");
      CHECK(h.progress.size() == 11 && h.progress[10] == "re-psn (rollback): room open, waiting for a player with build a1b2c3d4 (100 s of 120 s)");
      CHECK(h.n1 == "re-psn (rollback), Host: room open, waiting for the other player (100 s of 120 s).");
      int rh, rc; drive(h, c, 400, &rh, &rc);
      CHECK(rh == 1 && rc == 1 && c.cl->searches == 1); }
    // the host gives up at the window (not before), leaves its room, and says what to do
    { FakeServer sv; Side h; h.init(sv, 11, cfg(true, B));
      const uint64_t t0 = h.clock;
      CHECK(h.run_alone(119900) == 0 && h.cl->leaves == 0);
      const int r = h.run_alone(1000);
      CHECK(r == -1 && h.clock - t0 > 120000 && h.clock - t0 <= 120100 && h.cl->leaves == 1);
      CHECK(h.rz->error() == "re-psn (rollback): no player with this version joined within 2 minutes: start both games at about the same time (the host first is best; each waits 2 minutes after its own start-up); "
                             "both need the same version (build a1b2c3d4) and the same enabled mods in the same order");
      CHECK(h.rz->notice1() == "Rollback could not start: no player with this version joined within 2 minutes.");
      CHECK(h.rz->notice2() == "Start both games at about the same time (the host first is best; each waits 2 minutes after its own start-up); both need the same version (build a1b2c3d4) and the same enabled mods in the same order.");
      CHECK(h.progress.size() == 13); }
    // the joiner gives up at the window when no host appears
    { FakeServer sv; Side c; c.init(sv, 22, cfg(false, B));
      const uint64_t t0 = c.clock;
      const int r = c.run_alone(125000);
      CHECK(r == -1 && c.clock - t0 > 120000 && c.clock - t0 <= 120100 && c.cl->joins == 0 && c.cl->searches >= 60 && c.cl->searches <= 61);
      CHECK(c.rz->notice1() == "Rollback could not start: no host with this version found within 2 minutes (is the other player set to Host?)." && c.rz->error().find("(build a1b2c3d4)") != std::string::npos); }
    // a server that never answers HELLO: the window names that instead
    { FakeServer sv; Side c; c.init(sv, 22, cfg(false, B)); c.cl->is_ready = false;
      CHECK(c.run_alone(1000) == 0 && c.progress.size() == 1 && c.progress[0] == "re-psn (rollback): connecting to the server (0 s of 120 s, build a1b2c3d4)");
      CHECK(c.run_alone(125000) == -1 && c.rz->error().find("re-psn (rollback): the re-psn server did not answer within 2 minutes: ") == 0 && c.cl->searches == 0); }
    // a shorter window from the config is used in the texts
    { FakeServer sv; Side c; auto cc = cfg(false, B); cc.give_up_ms = 20000; c.init(sv, 22, cc);
      CHECK(c.run_alone(30000) == -1 && c.rz->error().find("found within 20 seconds (is the other player set to Host?): start both games at about the same time (the host first is best; each waits 20 seconds after its own start-up)") != std::string::npos); }
    // a named room that is gone / another build: refused too
    { FakeServer sv; Side h, c; h.init(sv, 11, cfg(true, B)); auto cc = cfg(false, B); cc.join_room = 999; c.init(sv, 22, cc);
      h.rz->poll(); int rc = c.rz->poll(); CHECK(rc == -1 && c.cl->joins == 0); }
    // ---- mismatch at join: the owner's member data differs (a client refuses the host) ----
    { FakeServer sv; Side h, c; h.init(sv, 11, cfg(true, B)); c.init(sv, 22, cfg(false, B)); h.cl->peers[0] = c.cl.get();
      h.rz->poll(); sv.rooms[0].members[0].data[38] ^= 1;     // the room list still says B, the member entry does not
      int rc = 0; for (int i = 0; i < 4; i++) rc = c.rz->poll();
      CHECK(rc == -1 && c.rz->error().find("does not match") != std::string::npos); }
    // the host refuses a joiner whose data differs and keeps waiting (counted), the session never starts
    { FakeServer sv; Side h, c; h.init(sv, 11, cfg(true, B)); c.init(sv, 22, cfg(false, B)); h.cl->peers[0] = c.cl.get(); c.cl->peers[0] = h.cl.get();
      h.rz->poll(); c.rz->poll();
      for (auto& m : sv.rooms[0].members) if (m.user == 22) m.data[38] ^= 1;
      for (auto& e : h.cl->ev) for (auto& m : e.members) if (m.user == 22) m.data[38] ^= 1;
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
            const std::string want = variant == 0 || variant == 3 ? "relay is not allowed" : "offers no relay";   // the side that failed first leaves its room, which the other one may then report as "left"
            CHECK(h.rz->error().find(want) != std::string::npos || c.rz->error().find(want) != std::string::npos);
        }
    }
    // a direct path never asks for the relay even when allowed and offered
    { FakeServer sv; sv.relay = true; Side h, c; h.init(sv, 11, cfg(true, B, true)); c.init(sv, 22, cfg(false, B, true)); h.cl->peers[0] = c.cl.get(); c.cl->peers[0] = h.cl.get();
      int rh, rc; drive(h, c, 400, &rh, &rc); CHECK(rh == 1 && rc == 1 && h.rz->path() == Path::Direct && c.rz->path() == Path::Direct); }
    // TEST ONLY force_relay: the relay at once, though the direct path works (one-PC relay tests); never without allow_relay
    for (int allow = 0; allow < 2; allow++) { FakeServer sv; sv.relay = true; auto ch = cfg(true, B, allow), cc = cfg(false, B, allow); ch.force_relay = cc.force_relay = true;
      Side h, c; h.init(sv, 11, ch); c.init(sv, 22, cc); h.cl->peers[0] = c.cl.get(); c.cl->peers[0] = h.cl.get();
      h.rz->on_path = [&](Path p, uint32_t) { if (p == Path::Relay) h.relay_calls++; }; c.rz->on_path = [&](Path p, uint32_t) { if (p == Path::Relay) c.relay_calls++; };
      int rh, rc; drive(h, c, 400, &rh, &rc); CHECK(rh == 1 && rc == 1);
      if (allow) CHECK(h.rz->path() == Path::Relay && c.rz->path() == Path::Relay && h.relay_calls >= 1 && c.relay_calls >= 1);
      else CHECK(h.rz->path() == Path::Direct && c.rz->path() == Path::Direct && h.relay_calls == 0); }
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

    // ---- ENHANCEMENT (native-room pairing): the pair key (the game's own room id) in the record and the room filter ----
    { uint32_t b = 0; const uint64_t P = 0x00000001deadbeefull, Q = 0x00000001deadbef0ull;
      CHECK(native_room_tag(P) != native_room_tag(Q) && native_room_tag(P) != code_tag("AB12"));
      CHECK(native_room_tag(0).empty() && native_room_tag(P).size() == 64);
      CHECK(record_matches(make_record(B, content, native_room_tag(P)), B, content, native_room_tag(P)));
      CHECK(!record_matches(make_record(B, content, native_room_tag(P)), B, content, code_tag("AB12")));
      CHECK(!record_build("rb=1;ver=a1b2c3d4;pair=00000001DEADBEEF", &b) && !record_build("rb=1;ver=a1b2c3d4;pair=0000000000000000", &b) && !record_build("rb=1;ver=a1b2c3d4;pairs=0000001deadbeef", &b));
      std::vector<PsnrRoom> all(5);
      all[0] = { 9, kRoomFlagRollback, make_record(B, content, native_room_tag(P)), 1, 1, 2 };      // our native room's rollback room
      all[1] = { 4, kRoomFlagRollback, make_record(B, content, native_room_tag(Q)), 1, 1, 2 };      // another native room's (older)
      all[2] = { 5, kRoomFlagRollback, make_record(B, content), 1, 1, 2 };         // a plain (lobby / start-up) rollback room
      all[3] = { 6, kRoomFlagRollback, make_record(B + 1, content, native_room_tag(P)), 1, 1, 2 };  // our key, another build: refused
      all[4] = { 7, kRoomFlagRollback, make_record(B, content, native_room_tag(P)), 1, 2, 2 };      // our key, full
      auto f = filter_rooms(all, B, content, native_room_tag(P)); CHECK(f.size() == 1 && f[0].id == 9);
      f = filter_rooms(all, B, content, native_room_tag(Q)); CHECK(f.size() == 1 && f[0].id == 4);
      f = filter_rooms(all, B, content); CHECK(f.size() == 1 && f[0].id == 5);   // a plain joiner never takes a paired room
      CHECK(rollback_name("Player1") == "Player1-rb" && rollback_name("abcdefghijklm") == "abcdefghijklm-rb");
      const std::string l1 = rollback_name("abcdefghijklmnop"), l2 = rollback_name("abcdefghijklmnoq");
      CHECK(l1.size() == 16 && l2.size() == 16 && l1 != l2 && l1.compare(0, 12, "abcdefghijk-") == 0);
      CHECK(rollback_comm_id("NPWR08138_00") == "NPWR08138_RB"); }
    // two native rooms, each with its own pair key: every joiner meets the host of ITS room (the older room of the other key is skipped); the owner hosts (slot 0)
    { FakeServer sv; const uint64_t P = 0x1111, Q = 0x2222;
      auto pc = [&](bool host, uint64_t pair) { auto c = cfg(host, B); c.pair = pair; return c; };
      Side hq, hp, cp, cq; hq.init(sv, 11, pc(true, Q)); hp.init(sv, 12, pc(true, P)); cp.init(sv, 21, pc(false, P)); cq.init(sv, 22, pc(false, Q));
      cp.cl->peers[0] = hp.cl.get(); cp.cl->peers[1] = hq.cl.get(); cq.cl->peers[0] = hq.cl.get(); cq.cl->peers[1] = hp.cl.get();
      hq.rz->poll(); hp.rz->poll();   // Q's room is created first (the oldest): a joiner without the key filter would take it
      CHECK(sv.rooms.size() == 2 && sv.rooms[0].r.external == make_record(B, content, native_room_tag(Q)) && sv.rooms[1].r.external == make_record(B, content, native_room_tag(P)) && sv.rooms[1].members[0].data == make_record(B, content, native_room_tag(P)));
      int rh, rc; drive(hp, cp, 400, &rh, &rc);
      CHECK(rh == 1 && rc == 1 && cp.rz->room_id() == hp.rz->room_id() && cp.rz->plan().session_id == hp.rz->room_id() && cp.cl->joins == 1);
      CHECK(hp.rz->local_index() == 0 && cp.rz->local_index() == 1 && hp.rz->plan().host && !cp.rz->plan().host);
      drive(hq, cq, 400, &rh, &rc);
      CHECK(rh == 1 && rc == 1 && cq.rz->room_id() == hq.rz->room_id() && cq.rz->room_id() != hp.rz->room_id() && cq.cl->joins == 1); }
    // Same release and native room ID, but content differs only in its last byte.
    { FakeServer sv; Side h, c; auto ch = cfg(true, B); ch.pair = 0x5555;
      auto cc = ch; cc.host = false; cc.content[31] ^= 1; h.init(sv, 11, ch); c.init(sv, 22, cc);
      h.rz->poll(); CHECK(c.run_alone(3000) == -1 && c.cl->joins == 0 && c.rz->notice1().find("different game content") != std::string::npos); }
    // Matching bytes do not make a custom, uncoded room eligible for native play.
    { FakeServer sv; Side h, c; auto ch = cfg(true, B); auto cc = cfg(false, B); cc.pair = 0x5555;
      h.init(sv, 11, ch); c.init(sv, 22, cc); h.rz->poll();
      CHECK(c.run_alone(3000) == 0 && c.cl->joins == 0); }
    // a joiner whose native room has no rollback room yet keeps looking (a room of another key is never taken) and gives up at the window
    { FakeServer sv; Side h, c; auto ch = cfg(true, B); ch.pair = 0x3333; auto cc = cfg(false, B); cc.pair = 0x4444; h.init(sv, 11, ch); c.init(sv, 22, cc); h.cl->peers[0] = c.cl.get();
      h.rz->poll();
      CHECK(c.run_alone(10000) == 0 && c.cl->joins == 0 && c.cl->searches >= 4);
      CHECK(c.run_alone(125000) == -1 && c.cl->joins == 0 && c.rz->error().find("no host with this version found") != std::string::npos); }
    // wrong pair key at join (the room list said ours, the owner's member data has another key): the joiner refuses; a host refuses a member with another key
    { FakeServer sv; Side h, c; auto ch = cfg(true, B); ch.pair = 0x5555; auto cc = ch; cc.host = false; h.init(sv, 11, ch); c.init(sv, 22, cc); h.cl->peers[0] = c.cl.get();
      h.rz->poll(); sv.rooms[0].members[0].data = make_record(B, content, native_room_tag(0x6666));
      int rc = 0; for (int i = 0; i < 4; i++) rc = c.rz->poll();
      CHECK(rc == -1 && c.rz->error().find("does not match") != std::string::npos); }
    { FakeServer sv; Side h, c; auto ch = cfg(true, B); ch.pair = 0x5555; auto cc = ch; cc.host = false; h.init(sv, 11, ch); c.init(sv, 22, cc);
      h.cl->peers[0] = c.cl.get(); c.cl->peers[0] = h.cl.get();
      h.rz->poll(); c.rz->poll();
      for (auto& e : h.cl->ev) for (auto& m : e.members) if (m.user == 22) m.data = make_record(B, content, native_room_tag(0x6666));
      int r = 0; for (int i = 0; i < 5; i++) { h.clock += 50; r = h.rz->poll(); }
      CHECK(r == 0 && h.rz->rejected_members == 1 && h.rz->path() == Path::None); }

    // ---- ENHANCEMENT (version mismatch message): the release table, the text, both sides fail at once (paired), an older host's give-up text ----
    { CHECK(release_name(0xe9be7f43u) == "0.7.0-test1" && release_name(0xf1e06d4du) == "0.7.0-test2" && release_name(0x8a47e98au) == "0.7.0-test3");
      CHECK(release_name(0x0000abcdu) == "build 0000abcd" && release_name(B) == "build a1b2c3d4");
      CHECK(version_mismatch_text(0xf1e06d4du, 0x8a47e98au, "") == "The other player is on 0.7.0-test2; you're on 0.7.0-test3. Both players need the same version for rollback.");
      CHECK(version_mismatch_text(0x8a47e98au, B, "0.7.0-test4") == "The other player is on 0.7.0-test3; you're on 0.7.0-test4. Both players need the same version for rollback.");
      CHECK(version_mismatch_text(0x12345678u, B, "0.7.0-test4") == "The other player is on build 12345678; you're on 0.7.0-test4. Both players need the same version for rollback.");
      // the same release name on both sides (other gameplay mods): the build words
      CHECK(version_mismatch_text(0x8a47e98au, B, "0.7.0-test3") == "The other player is on build 8a47e98a; you're on build a1b2c3d4. Both players need the same version for rollback."); }
    // A build mismatch in the same native room identifies the release locally, without joining it.
    { FakeServer sv; auto ch = cfg(true, 0x8a47e98au); ch.pair = 0x7777;
      auto cc = cfg(false, B); cc.pair = ch.pair; cc.release = "0.7.0-test4";
      Side h, c; h.init(sv, 11, ch); c.init(sv, 22, cc); h.rz->poll();
      CHECK(c.run_alone(3000) == -1 && c.cl->joins == 0 && c.rz->version_mismatch());
      CHECK(c.rz->notice1() == version_mismatch_text(0x8a47e98au, B, cc.release));
      CHECK(h.rz->poll() == 0 && !h.rz->version_mismatch()); }

    // ---- the notice store (jsp_net_game.cpp): every end of the wait leaves it right ----
    { RendezvousNotice n; std::string a, b; const unsigned g0 = n.gen();
      CHECK(!n.read(a, b));
      n.waiting("re-psn (rollback): room open, waiting for the other player (40 s of 120 s).", "Both need ...");
      CHECK(n.read(a, b) && a.find("waiting") != std::string::npos && n.gen() == g0 + 1);
      n.waiting("re-psn (rollback): room open, waiting for the other player (40 s of 120 s).", "Both need ..."); CHECK(n.gen() == g0 + 1);   // the same text: no redraw
      n.ended();   // another part ended the session before the games met (a mods refusal during the wait): the waiting band must go
      CHECK(!n.read(a, b) && n.gen() == g0 + 2);
      n.waiting("w", "x"); n.failed("Rollback could not start: x.", "Start ...");
      CHECK(n.read(a, b) && a == "Rollback could not start: x." && b == "Start ..." && n.gen() == g0 + 4);
      n.waiting("w", "x"); n.met(); CHECK(!n.read(a, b) && n.gen() == g0 + 6); }

    // ================= ENHANCEMENT (auto role): nobody picks Host or Join =================
    auto link = [](Side& a, Side& b) { a.cl->peers[0] = b.cl.get(); b.cl->peers[0] = a.cl.get(); };
    // both arrive at the same moment: both searched an empty list, both created a room (ids 100 and 101); the higher id yields: exactly one side leaves and joins the other
    { FakeServer sv; Side a, b; a.init(sv, 11, acfg(B)); b.init(sv, 22, acfg(B)); link(a, b); a.cl->latency = 1; b.cl->latency = 1;
      int ra, rb; drive(a, b, 600, &ra, &rb);
      CHECK(ra == 1 && rb == 1 && sv.next_room == 102);                                       // both really created a room
      CHECK(a.rz->hosting() && !b.rz->hosting() && a.rz->yields == 0 && b.rz->yields == 1);   // the lower room id (a's, 100) stays; b yields once
      CHECK(a.rz->plan().host && !b.rz->plan().host && a.rz->plan().session_id == 100 && b.rz->plan().session_id == 100);
      CHECK(sv.rooms.size() == 1 && sv.rooms[0].members.size() == 2 && b.cl->leaves >= 1 && a.cl->leaves == 0);
      CHECK(a.rz->local_index() == 0 && b.rz->local_index() == 1); }
    // the same race with the other side's room first: whoever created the lower id stays, whatever its position in the call order
    { FakeServer sv; Side a, b; a.init(sv, 11, acfg(B)); b.init(sv, 22, acfg(B)); link(a, b); a.cl->latency = 2; b.cl->latency = 1;
      int ra, rb; drive(a, b, 600, &ra, &rb);
      CHECK(ra == 1 && rb == 1 && b.rz->hosting() && !a.rz->hosting() && a.rz->yields == 1 && b.rz->yields == 0 && a.rz->plan().session_id == 100); }
    // either order of arrival: the first opens the room, the second finds it and joins; nobody yields
    for (int first = 0; first < 2; first++) {
        FakeServer sv; Side a, b; a.init(sv, 11, acfg(B)); b.init(sv, 22, acfg(B)); link(a, b);
        Side& f = first ? b : a; Side& l = first ? a : b;
        f.run_alone(3000); CHECK(sv.rooms.size() == 1 && f.rz->hosting());                   // alone: a room is open and listed while it waits
        int rf, rl; if (first) drive(l, f, 600, &rl, &rf); else drive(f, l, 600, &rf, &rl);
        CHECK(rf == 1 && rl == 1 && f.rz->hosting() && !l.rz->hosting() && f.rz->yields == 0 && l.rz->yields == 0 && sv.next_room == 101);
        CHECK(f.rz->plan().host && !l.rz->plan().host);
    }
    // the room filled up (or vanished) between the list and the join: back to searching inside the window (no failure)
    { FakeServer sv; Side a, b; a.init(sv, 11, acfg(B)); b.init(sv, 22, acfg(B)); link(a, b);
      a.run_alone(3000); b.cl->fail_next_join = true;
      int ra, rb; drive(a, b, 600, &ra, &rb);
      CHECK(ra == 1 && rb == 1 && b.rz->restarts == 1 && b.cl->joins == 2 && !b.rz->hosting() && a.rz->hosting()); }
    // the host leaves while the joiner has not got its endpoint yet: the joiner goes back to searching, opens its own room, and a third game finds it
    { FakeServer sv; Side a, b, c; a.init(sv, 11, acfg(B)); b.init(sv, 22, acfg(B)); link(a, b);
      a.run_alone(3000); sv.rooms[0].members[0].ep.port = 0;                                  // a's member entry has no endpoint yet: the joiner cannot start the punch
      b.run_alone(300); CHECK(!b.rz->hosting() && b.rz->room_id() == 100 && b.rz->restarts == 0 && sv.rooms[0].members.size() == 2);
      a.cl->leave_room();                                                                      // the owner is gone (the room is closed or passes on: the joiner must not wait for it)
      b.run_alone(300); CHECK(b.rz->restarts == 1);
      b.run_alone(3000); CHECK(b.rz->hosting() && sv.rooms.size() == 1);                       // its own room is open now
      c.init(sv, 33, acfg(B)); b.cl->peers[1] = c.cl.get(); c.cl->peers[0] = b.cl.get();
      int rb, rc; drive(b, c, 600, &rb, &rc);
      CHECK(rb == 1 && rc == 1 && b.rz->hosting() && !c.rz->hosting()); }
    // after the punch started, the owner leaving is still the old failure (a session cannot move its host)
    { FakeServer sv; Side a, b; a.init(sv, 11, acfg(B)); b.init(sv, 22, acfg(B)); link(a, b); a.run_alone(3000); b.run_alone(100);
      a.cl->leave_room(); int r = b.run_alone(300); CHECK(r == -1 && b.rz->error().find("left the room") != std::string::npos); }
    // progress text: no Host or Join in auto mode, no "set to Host" hint at the end
    { FakeServer sv; Side a; a.init(sv, 11, acfg(B, "", 30000)); a.run_alone(11000);
      CHECK(!a.progress.empty() && a.progress.back().find("room open") != std::string::npos && a.n1.find("re-psn (rollback): room open, waiting for the other player") == 0 && a.n1.find("Host") == std::string::npos);
      int r = a.run_alone(25000); CHECK(r == -1 && a.rz->error().find("no other player with this version found within 30 seconds") != std::string::npos && a.rz->error().find("set to Host") == std::string::npos
                                       && a.rz->error().find("the host first is best") == std::string::npos);
      Side l; FakeServer sv2; l.init(sv2, 12, acfg(B, "", 6000)); l.progress.clear(); l.run_alone(100); }
    // ---- match code: only the same code meets; no code meets only no code; the code never appears in the records, logs or messages ----
    CHECK(make_record(B, content, "") == golden && make_record(B, content, code_tag("AB12CD")).size() == 104 && make_record(B, content, code_tag("AB12CD")).find("AB12CD") == std::string::npos);
    CHECK(code_tag("") == "" && code_tag("AB12CD") == code_tag("AB12CD") && code_tag("AB12CD") != code_tag("AB12CE"));
    { uint32_t bb = 0; std::string tg; CHECK(record_build(make_record(B, content, code_tag("AB12CD")), &bb, &tg) && bb == B && tg == code_tag("AB12CD") && record_build(make_record(B, content), &bb, &tg) && tg.empty());
      CHECK(!record_build("rb=1;ver=a1b2c3d4;code=zzzzzzzz", &bb, &tg) && !record_matches(make_record(B, content, code_tag("A")), B, content) && record_matches(make_record(B, content, code_tag("A")), B, content, code_tag("A"))); }
    { FakeServer sv; Side a, b; a.init(sv, 11, acfg(B, "AB12CD")); b.init(sv, 22, acfg(B, "AB12CD")); link(a, b);
      int ra, rb; drive(a, b, 600, &ra, &rb);
      CHECK(ra == 1 && rb == 1 && sv.rooms.size() == 1 && sv.rooms[0].r.external == make_record(B, content, code_tag("AB12CD")) && sv.rooms[0].members[0].data == sv.rooms[0].r.external);
      for (const auto& l : a.progress) CHECK(l.find("AB12CD") == std::string::npos); }
    for (int v = 0; v < 3; v++) {   // 0: two different codes, 1: a code and none, 2: none and a code
        const std::string ca = v == 2 ? "" : "AB12CD", cb = v == 0 ? "WX34YZ" : v == 1 ? "" : "AB12CD";
        FakeServer sv; Side a, b; a.init(sv, 11, acfg(B, ca, 6000)); b.init(sv, 22, acfg(B, cb, 6000)); link(a, b);
        int ra, rb; drive(a, b, 600, &ra, &rb);
        CHECK(ra == -1 && rb == -1 && a.cl->joins == 0 && b.cl->joins == 0);
        const std::string all = a.rz->error() + b.rz->error() + a.rz->notice1() + a.rz->notice2() + b.rz->notice1() + b.rz->notice2();
        CHECK(all.find("AB12CD") == std::string::npos && all.find("WX34YZ") == std::string::npos && all.find("another match code") != std::string::npos);
        for (const auto& l : a.progress) CHECK(l.find("AB12CD") == std::string::npos && l.find("WX34YZ") == std::string::npos);
    }
    { FakeServer sv; Side a, b; a.init(sv, 11, acfg(B, "", 6000)); b.init(sv, 22, acfg(B, "", 6000)); link(a, b);   // none and none meet (the 0.6.x record)
      int ra, rb; drive(a, b, 600, &ra, &rb); CHECK(ra == 1 && rb == 1 && sv.rooms[0].r.external == golden); }
    {   // Same release and code. An older room differs only at byte 31 of the content SHA.
        FakeServer sv; Side wrong, right, seeker;
        auto incompatible = cfg(true, B); incompatible.content[31] ^= 1;
        wrong.init(sv, 11, incompatible); wrong.rz->poll();
        right.init(sv, 22, cfg(true, B)); right.rz->poll();
        seeker.init(sv, 33, cfg(false, B)); seeker.rz->poll();
        CHECK(sv.rooms.size() == 2 && sv.rooms[0].members.size() == 1 && sv.rooms[1].members.size() == 2);
    }
    { FakeServer sv; Side a, b; auto x = acfg(B, "AB12CD", 2000); auto y = x; y.content[30] = 1;
      a.init(sv, 11, x); b.init(sv, 22, y); a.cl->latency = b.cl->latency = 1;
      int ra, rb; drive(a, b, 400, &ra, &rb);
      CHECK(ra < 0 && rb < 0 && a.cl->joins == 0 && b.cl->joins == 0);
      CHECK((a.rz->error() + b.rz->error()).find("different game content or mods") != std::string::npos); }
    { FakeServer sv; Side a; auto missing = cfg(true, B); missing.content = {};
      a.init(sv, 11, missing); CHECK(a.rz->poll() < 0 && sv.rooms.empty()); }
    { FakeServer sv; Side h, c; h.init(sv, 11, cfg(true, B)); h.rz->poll();
      auto join = cfg(false, B); join.give_up_ms = 300;
      sv.rooms.front().r.external = "rb=1;ver=a1b2c3d4";
      c.init(sv, 22, join); c.rz->poll(); c.clock += 500;
      CHECK(c.rz->poll() < 0 && c.cl->joins == 0 && c.rz->notice1().find("update both games") != std::string::npos); }
    CHECK(make_record(B, content, code_tag("T60CDATX")) == golden + "ce973beee4b76122b831913719bb34a0953dc4f256133feac337e369141ad15a");
    // the code is not part of the build id: the records of two different codes carry the same build word
    { uint32_t b1 = 0, b2 = 0; CHECK(record_build(make_record(B, content, code_tag("AAAA")), &b1) && record_build(make_record(B, content, code_tag("BBBB")), &b2) && b1 == b2 && b1 == B); }

    // Independent fixture: the draft's 32-bit FNV tags both codes as 20d8f929.
    // Keep both exact SHA-256 known answers and actual rendezvous separation.
    CHECK(code_tag("T60CDATX") == "ce973beee4b76122b831913719bb34a0953dc4f256133feac337e369141ad15a");
    CHECK(code_tag("39M5ANTS") == "65293859e45a3ef6119fff01fc23da4b1c6b297e1e33c57c4a1ddeb143158292");
    { FakeServer sv; Side a, b; a.init(sv, 11, acfg(B, "T60CDATX", 6000)); b.init(sv, 22, acfg(B, "39M5ANTS", 6000)); link(a, b);
      int ra, rb; drive(a, b, 600, &ra, &rb);
      CHECK(ra == -1 && rb == -1 && a.cl->joins == 0 && b.cl->joins == 0); }
    { uint32_t bb = 0; std::string tg;
      CHECK(!record_build("rb=1;ver=a1b2c3d4;code=20d8f929", &bb, &tg));
      CHECK(!record_build(make_record(B, content, std::string(64, 'G')), &bb, &tg));
      CHECK(!record_build(make_record(B, content, std::string(63, 'a')), &bb, &tg));
      CHECK(!record_build(make_record(B, content, std::string(65, 'a')), &bb, &tg)); }

    if (g_fail) { std::printf("%d of %d checks FAILED\n", g_fail, g_n); return 1; }
    std::printf("net repsn: all %d checks passed\n", g_n);
    return 0;
}
