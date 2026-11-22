// net_m2_link_test.cpp -- the REAL settings handshake (jpsm::Host / Client) over the rollback session's datagram adapter and real localhost UDP with 30% loss each way.
// Checks: single retransmit layer (the handshake's Resender only) reaches Started under loss; settings applied on the client; the datagram path survives a blocked main
// thread (pump thread keeps draining and ticking: the main thread sleeps 1.5 s like a scene load); channel/peer silence reaches the handshake as PeerLost/HostLost;
// session id u64 and roster mapping (2+2 players) agree with the SessionPlan. ENHANCEMENT (same-link rematch, jsp_net_match.h): three and more matches over ONE link,
// each with new handshake objects under its match's session id: stragglers of an earlier match are ignored, a side that is behind follows (host beacon), a side
// that is ahead is followed (its Hello), never backwards. Build and run: installer/tools/net_session_test.sh.
#include "jsp_net_m2_link.h"
#include "jsp_session_handshake.h"
#include "jsp_net_match.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <set>
#include <thread>

using namespace jspnet;
using namespace jpsm;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { g_n++; if (!(c)) { g_fail++; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

struct FakeReader : Reader {
    std::array<uint32_t, kCount> startup{};
    Values live{};
    EffectiveValues* ev = nullptr;
    FakeReader() { for (size_t i = 0; i < kCount; ++i) startup[i] = kRules[i].def; startup[(size_t)Id::KoTarget] = 3; }
    const char* debug_switch() override { return nullptr; }
    bool startup_value(Id id, uint32_t* o) override { *o = startup[(size_t)id]; return true; }
    void applied(const Values& v) override { live = v; }
    bool read_back(Id id, uint32_t* o) override { *o = live.v[(size_t)id]; return true; }
    void restored() override { if (ev) live = ev->local_values(); }
};
using Link = M2LinkT<Transport>;

static Values rb_values() { Values v = defaults(); v.v[(size_t)Id::Rollback] = 1; v.v[(size_t)Id::InputDelay] = 3; v.v[(size_t)Id::KoTarget] = 3; v.v[(size_t)Id::StageId] = 7; return v; }
template <class F> static bool wait_for(int ms, F f) { for (int i = 0; i < ms / 5; i++) { if (f()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(5)); } return f(); }

// ENHANCEMENT (same-link rematch): one side of a link as jsp_net_game.cpp drives it: the pump classifies every datagram by match (match_peek), a host waiting
// for its client's Hello sends the beacon, the main thread re-arms at a newer match it saw (follow = match_poll / the native re-arm).
struct MatchSide {
    bool is_host; UdpMux& mux; Link link; FakeReader r; EffectiveValues ev{&r}; Caps caps; Config cfg; M2Pump pump;
    std::unique_ptr<Host> h; std::unique_ptr<Client> c; uint64_t base; uint32_t k = 0; std::atomic<uint32_t> jump{0};
    uint64_t beacon_next = 0, older = 0, beacons = 0, newer = 0, follows = 0; bool proposed = false;
    MatchSide(bool host, UdpMux& m, const Endpoint& peer, uint64_t b, const Config& cf) : is_host(host), mux(m), link(m), cfg(cf), base(b)
    { r.ev = &ev; caps.build = 0xB001; link.set_peer(host ? 1 : 0, peer); }
    void start() { pump.start(mux, link, [this](uint8_t f, const uint8_t* d, size_t n, uint64_t t) { on(f, d, n, t); }, [this](uint64_t t) { tick(t); }, 100); }
    void on(uint8_t from, const uint8_t* d, size_t n, uint64_t t)
    {
        uint32_t to = 0;
        switch (match_peek(base, k, d, n, &to)) {
        case MatchMsg::Beacon: beacons++; return;
        case MatchMsg::Older: older++; return;
        case MatchMsg::Newer: { newer++; uint32_t cur = jump.load(); while (to > cur && !jump.compare_exchange_weak(cur, to)) {} return; }
        default: break;
        }
        if (h) h->on_message(from, d, n, t); else if (c) c->on_message(from, d, n, t);
    }
    void tick(uint64_t t)
    {
        if (c) { c->tick(t); return; }
        if (!h) return;
        h->tick(t);
        const State st = h->state();
        if (st == State::Open && (h->hellos() & 2) && !proposed) proposed = h->propose(rb_values(), t) == Reason::None;
        if (st == State::Ready) h->start(0);
        if (k > 0 && st == State::Open && !(h->hellos() & 2) && t >= beacon_next) { beacon_next = t + 1000; const auto b = match_beacon(base, k); link.send(1, b.data(), b.size()); }
    }
    void arm(uint32_t kk)
    {
        std::lock_guard<std::mutex> lk(pump.mutex());
        h.reset(); c.reset(); k = kk; beacon_next = 0; proposed = false;
        const uint64_t sid = match_session_id(base, kk);
        if (is_host) { h.reset(new Host(link, ev, caps, 0, cfg)); h->begin(sid, 0x3, sid ^ 0x5eedull, now_ms()); }
        else { c.reset(new Client(link, ev, caps, 1, cfg)); c->join(sid, 0, now_ms()); }
    }
    bool follow() { const uint32_t j = jump.load(); if (j > k) { arm(j); follows++; return true; } return false; }
    State state() { std::lock_guard<std::mutex> lk(pump.mutex()); return h ? h->state() : c ? c->state() : State::Idle; }
    void end() { std::lock_guard<std::mutex> lk(pump.mutex()); if (h) h->end_match(); else if (c) c->match_ended(); }
};

int main()
{
    UdpMux mh, mc; CHECK(mh.open(0) && mc.open(0));
    Endpoint eh{}, ec{}; parse_endpoint("127.0.0.1:" + std::to_string(mh.local_port()), eh); parse_endpoint("127.0.0.1:" + std::to_string(mc.local_port()), ec);
    LinkModel lm; lm.loss_permille = 300; lm.jitter_ms = 15; lm.delay_ms = 5; lm.seed = 11; mh.set_link_model(lm); lm.seed = 12; mc.set_link_model(lm);

    SessionPlan plan; plan.session_id = 0xABCDEF0123456789ull; plan.host = true; plan.total_players = 4; plan.local_first = 0; plan.local_count = 2; plan.remotes = {{ec, 2, 2}};
    const auto mask = plan.peer_slot_mask();           // {0b0011, 0b1100}: host = peer 0, client = peer 1
    CHECK(mask[0] == 3 && mask[1] == 12);
    const uint8_t roster = 0x3;                        // peers 0 and 1

    FakeReader rh, rc; EffectiveValues evh(&rh), evc(&rc); rh.ev = &evh; rc.ev = &evc;
    Caps caps; caps.build = 0xB001;
    Link lh(mh), lc(mc); lh.set_peer(1, ec); lc.set_peer(0, eh);
    Config cfg; cfg.retry_ms = 150; cfg.retries = 40; cfg.keepalive_ms = 300; cfg.liveness_ms = 2000;   // short timers for the test
    Host host(lh, evh, caps, 0, cfg);
    Client client(lc, evc, caps, 1, cfg);
    M2Pump ph, pc;
    ph.start(mh, lh, [&](uint8_t from, const uint8_t* d, size_t n, uint64_t t) { host.on_message(from, d, n, t); }, [&](uint64_t t) { host.tick(t); }, 100);
    pc.start(mc, lc, [&](uint8_t from, const uint8_t* d, size_t n, uint64_t t) { client.on_message(from, d, n, t); }, [&](uint64_t t) { client.tick(t); }, 100);

    { std::lock_guard<std::mutex> a(ph.mutex()); CHECK(host.begin(plan.session_id, roster, 0x1234, now_ms()) == Reason::None); }
    { std::lock_guard<std::mutex> a(pc.mutex()); CHECK(client.join(plan.session_id, 0, now_ms()) == Reason::None); }
    CHECK(wait_for(8000, [&] { std::lock_guard<std::mutex> a(ph.mutex()); return host.hellos() & 2; }));
    { std::lock_guard<std::mutex> a(ph.mutex()); CHECK(host.propose(rb_values(), now_ms()) == Reason::None); }
    CHECK(wait_for(8000, [&] { std::lock_guard<std::mutex> a(ph.mutex()); return host.state() == State::Ready; }));
    // The main thread is blocked (a scene load) while the datagram path keeps ticking: no loss of liveness over 1.5 s (liveness_ms is 2 s)
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    { std::lock_guard<std::mutex> a(ph.mutex()); CHECK(host.state() == State::Ready); CHECK(host.start(7) == Reason::None); }
    CHECK(wait_for(8000, [&] { std::lock_guard<std::mutex> a(pc.mutex()); return client.state() == State::Started; }));
    { std::lock_guard<std::mutex> a(pc.mutex()); CHECK(evc.active() && rc.live.v[(size_t)Id::StageId] == 7 && rc.live.v[(size_t)Id::InputDelay] == 3); }   // applied on the client
    CHECK(mh.dropped > 0 && mc.dropped > 0);            // the run really was lossy
    // handover: the rollback session owns liveness; handshake keep-alives keep flowing
    { std::lock_guard<std::mutex> a(ph.mutex()); CHECK(host.handover()); }
    { std::lock_guard<std::mutex> a(pc.mutex()); CHECK(client.handover()); }
    // Silence reaches the handshake as a lost peer BEFORE handover only; here (after handover) the handshake no longer ends the match: the rollback session does. Both sides stay Started.
    ph.stop();                                          // the host's pump dies: no keep-alives to the client
    std::this_thread::sleep_for(std::chrono::milliseconds(2600));
    { std::lock_guard<std::mutex> a(pc.mutex()); CHECK(client.state() == State::Started); }
    pc.stop();
    // A second pair, not handed over: the client's host goes silent -> HostLost reaches the client's state machine
    UdpMux m2h, m2c; CHECK(m2h.open(0) && m2c.open(0));
    Endpoint eh2{}, ec2{}; parse_endpoint("127.0.0.1:" + std::to_string(m2h.local_port()), eh2); parse_endpoint("127.0.0.1:" + std::to_string(m2c.local_port()), ec2);
    FakeReader rh2, rc2; EffectiveValues evh2(&rh2), evc2(&rc2); rh2.ev = &evh2; rc2.ev = &evc2;
    Link lh2(m2h), lc2(m2c); lh2.set_peer(1, ec2); lc2.set_peer(0, eh2);
    Host host2(lh2, evh2, caps, 0, cfg); Client client2(lc2, evc2, caps, 1, cfg);
    M2Pump ph2, pc2;
    ph2.start(m2h, lh2, [&](uint8_t f, const uint8_t* d, size_t n, uint64_t t) { host2.on_message(f, d, n, t); }, [&](uint64_t t) { host2.tick(t); }, 100);
    pc2.start(m2c, lc2, [&](uint8_t f, const uint8_t* d, size_t n, uint64_t t) { client2.on_message(f, d, n, t); }, [&](uint64_t t) { client2.tick(t); }, 100);
    { std::lock_guard<std::mutex> a(ph2.mutex()); host2.begin(5, roster, 1, now_ms()); }
    { std::lock_guard<std::mutex> a(pc2.mutex()); client2.join(5, 0, now_ms()); }
    CHECK(wait_for(4000, [&] { std::lock_guard<std::mutex> a(ph2.mutex()); return host2.hellos() & 2; }));
    ph2.stop();                                         // host gone (crash / channel dead)
    CHECK(wait_for(6000, [&] { std::lock_guard<std::mutex> a(pc2.mutex()); return client2.state() == State::Failed && client2.reason() == Reason::HostLost; }));
    pc2.stop();
    // A third pair, host side: the client goes silent after its hello -> PeerLost reaches the host (lost peer, reason PeerLost)
    UdpMux m3h, m3c; CHECK(m3h.open(0) && m3c.open(0));
    Endpoint eh3{}, ec3{}; parse_endpoint("127.0.0.1:" + std::to_string(m3h.local_port()), eh3); parse_endpoint("127.0.0.1:" + std::to_string(m3c.local_port()), ec3);
    FakeReader rh3, rc3; EffectiveValues evh3(&rh3), evc3(&rc3); rh3.ev = &evh3; rc3.ev = &evc3;
    Link lh3(m3h), lc3(m3c); lh3.set_peer(1, ec3); lc3.set_peer(0, eh3);
    Host host3(lh3, evh3, caps, 0, cfg); Client client3(lc3, evc3, caps, 1, cfg);
    M2Pump ph3, pc3;
    ph3.start(m3h, lh3, [&](uint8_t f, const uint8_t* d, size_t n, uint64_t t) { host3.on_message(f, d, n, t); }, [&](uint64_t t) { host3.tick(t); }, 100);
    pc3.start(m3c, lc3, [&](uint8_t f, const uint8_t* d, size_t n, uint64_t t) { client3.on_message(f, d, n, t); }, [&](uint64_t t) { client3.tick(t); }, 100);
    { std::lock_guard<std::mutex> a(ph3.mutex()); host3.begin(6, roster, 1, now_ms()); }
    { std::lock_guard<std::mutex> a(pc3.mutex()); client3.join(6, 0, now_ms()); }
    CHECK(wait_for(4000, [&] { std::lock_guard<std::mutex> a(ph3.mutex()); return host3.hellos() & 2; }));
    pc3.stop();                                         // client gone
    CHECK(wait_for(6000, [&] { std::lock_guard<std::mutex> a(ph3.mutex()); return (host3.lost_peers() & 2) && host3.lost_reason() == Reason::PeerLost; }));
    ph3.stop();

    // ---- ENHANCEMENT (same-link rematch): the match session ids and the classification ----
    {
        const uint64_t base = 0x0123456789ABCDEFull;
        CHECK(match_session_id(base, 0) == base);
        std::set<uint64_t> ids; bool nonzero = true;
        for (uint32_t k = 0; k < 1000; k++) { const uint64_t s = match_session_id(base, k); ids.insert(s); if (!s) nonzero = false; }
        CHECK(ids.size() == 1000 && nonzero);
        CHECK(match_session_id(1, 1) != match_session_id(2, 1));   // another link: other ids
        std::vector<uint8_t> b; uint32_t to = 99;
        put_header(b, Msg::KeepAlive, match_session_id(base, 2), 0); b.resize(b.size() + 5, 0);
        CHECK(match_peek(base, 2, b.data(), b.size(), &to) == MatchMsg::Current);
        CHECK(match_peek(base, 1, b.data(), b.size(), &to) == MatchMsg::Newer && to == 2);
        CHECK(match_peek(base, 3, b.data(), b.size(), &to) == MatchMsg::Older);
        CHECK(match_peek(base, 2 + 0, nullptr, 0, &to) == MatchMsg::Foreign);
        const auto beacon = match_beacon(base, 2);
        CHECK(beacon.size() == kHeaderSize && match_peek(base, 2, beacon.data(), beacon.size(), &to) == MatchMsg::Beacon);
        to = 0; CHECK(match_peek(base, 0, beacon.data(), beacon.size(), &to) == MatchMsg::Newer && to == 2);
        CHECK(match_peek(base, 1000, b.data(), b.size(), &to) == MatchMsg::Older);
        std::vector<uint8_t> far; put_header(far, Msg::Hello, match_session_id(base, 2 + kMatchAhead + 1), 0);
        CHECK(match_peek(base, 2, far.data(), far.size(), &to) == MatchMsg::Foreign);   // too far ahead: not taken as a newer match
        std::vector<uint8_t> other; put_header(other, Msg::Hello, 0x5555, 0);
        CHECK(match_peek(base, 0, other.data(), other.size(), &to) == MatchMsg::Foreign);
    }
    // ---- three (four) matches over ONE link, 10% loss each way: new objects per match, stragglers ignored, a side behind follows, a side ahead is followed ----
    {
        UdpMux ma, mb; CHECK(ma.open(0) && mb.open(0));
        Endpoint ea{}, eb{}; parse_endpoint("127.0.0.1:" + std::to_string(ma.local_port()), ea); parse_endpoint("127.0.0.1:" + std::to_string(mb.local_port()), eb);
        LinkModel lm; lm.loss_permille = 100; lm.jitter_ms = 10; lm.delay_ms = 3; lm.seed = 21; ma.set_link_model(lm); lm.seed = 22; mb.set_link_model(lm);
        Config mcfg; mcfg.retry_ms = 150; mcfg.retries = 40; mcfg.keepalive_ms = 300; mcfg.liveness_ms = 4000;
        const uint64_t base = 0x7777000011112222ull;
        MatchSide H(true, ma, eb, base, mcfg), C(false, mb, ea, base, mcfg);
        H.start(); C.start();
        auto both_started = [&] { H.follow(); C.follow(); return H.state() == State::Started && C.state() == State::Started; };
        // match 0: the plan's own session id (the first match on a link is what it always was)
        H.arm(0); C.arm(0);
        CHECK(wait_for(10000, both_started));
        CHECK(H.k == 0 && C.k == 0 && C.ev.active());
        uint32_t old_epoch = 0; { std::lock_guard<std::mutex> a(C.pump.mutex()); old_epoch = C.c->epoch(); }
        H.end(); C.end();
        CHECK(H.state() == State::Ended && C.state() == State::Ended && !C.ev.active());   // the local settings came back
        // match 1: both arm at once; stragglers of match 0 (a Start, a Cancel to the old join, a keep-alive) arrive at the new client: ignored, it reaches Started
        H.arm(1); C.arm(1);
        {
            std::vector<uint8_t> st; put_header(st, Msg::Start, base, 1); st.resize(st.size() + 36, 0);
            const auto cn = detail::cancel_bytes(base, 1, Reason::HostCancelled, old_epoch);
            std::vector<uint8_t> ka; put_header(ka, Msg::KeepAlive, base, 1); ka.resize(ka.size() + 5, 0);
            for (int i = 0; i < 20; i++) { H.link.send(1, st.data(), st.size()); H.link.send(1, cn.data(), cn.size()); H.link.send(1, ka.data(), ka.size()); }
        }
        CHECK(wait_for(10000, both_started));
        CHECK(H.k == 1 && C.k == 1 && C.older > 0 && H.follows == 0 && C.follows == 0);
        { std::lock_guard<std::mutex> a(C.pump.mutex()); CHECK(C.c->reason() == Reason::None); }
        H.end(); C.end();
        // match 2: the host re-arms first, the client is still behind (its match ended, it has not armed): the host's beacon makes it follow
        H.arm(2);
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));   // the client does nothing on its own for 2.5 s
        CHECK(C.k == 1 && C.jump.load() == 2);                          // it saw the beacon of match 2
        CHECK(wait_for(10000, both_started));
        CHECK(H.k == 2 && C.k == 2 && C.follows == 1 && H.follows == 0);
        H.end(); C.end();
        // match 3: now the client is ahead (it re-armed, the host has not): the client's Hello makes the host follow; nobody goes back
        C.arm(3);
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        CHECK(H.k == 2 && H.jump.load() == 3);
        CHECK(wait_for(10000, both_started));
        CHECK(H.k == 3 && C.k == 3 && H.follows == 1);
        { std::lock_guard<std::mutex> a(C.pump.mutex()); CHECK(C.ev.active() && C.r.live.v[(size_t)Id::StageId] == 7); }   // settings applied for match 3
        CHECK(ma.dropped > 0 && mb.dropped > 0);   // the link really was lossy
        H.pump.stop(); C.pump.stop();
    }
    std::printf("%s (%d checks, %d failed)\n", g_fail ? "FAIL" : "ok", g_n, g_fail);
    return g_fail ? 1 : 0;
}
