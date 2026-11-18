// net_m2_link_test.cpp -- the REAL settings handshake (jpsm::Host / Client) over the rollback session's datagram adapter and real localhost UDP with 30% loss each way.
// Checks: single retransmit layer (the handshake's Resender only) reaches Started under loss; settings applied on the client; the datagram path survives a blocked main
// thread (pump thread keeps draining and ticking: the main thread sleeps 1.5 s like a scene load); channel/peer silence reaches the handshake as PeerLost/HostLost;
// session id u64 and roster mapping (2+2 players) agree with the SessionPlan. Build and run: installer/tools/net_session_test.sh.
#include "jsp_net_m2_link.h"
#include "jsp_session_handshake.h"

#include <chrono>
#include <cstdio>
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
    std::printf("%s (%d checks, %d failed)\n", g_fail ? "FAIL" : "ok", g_n, g_fail);
    return g_fail ? 1 : 0;
}
