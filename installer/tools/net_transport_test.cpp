// net_transport_test.cpp -- host test of the transport pieces of jsp_net_session: endpoint wire form, pad wire encoding, message channel
// epochs (restarted peer, stale packets), rendezvous validation (stray datagrams), bounded queues, heartbeat thread, roster masks.
// In-process, real localhost UDP. Build and run: installer/tools/net_session_test.sh. Prints one line per check, exit 1 on any failure.
#include "jsp_net_session.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace jspnet;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { g_n++; if (!(c)) { g_fail++; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static Endpoint local_ep(const UdpMux& m) { Endpoint e{}; parse_endpoint("127.0.0.1:" + std::to_string(m.local_port()), e); return e; }
static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
// Pump both hubs for up to `ms` until `done()`.
template <class F> static bool until(int ms, UdpMux* a, UdpMux* b, F done) { (void)a; (void)b; for (int i = 0; i < ms / 2; i++) { if (done()) return true; sleep_ms(2); } return done(); }

int main()
{
    // ---- Endpoint: 6-byte wire form, independent of padding ----
    {
        alignas(Endpoint) unsigned char raw[sizeof(Endpoint)];
        std::memset(raw, 0xA5, sizeof raw);
        Endpoint* e = reinterpret_cast<Endpoint*>(raw);
        e->ip_be = 0x0100007F; e->port = 4242;
        Endpoint clean{}; clean.ip_be = 0x0100007F; clean.port = 4242;
        CHECK(e->wire() == clean.wire());
        CHECK(clean.wire().size() == 6);
        Endpoint back{}; CHECK(Endpoint::from_wire(clean.wire().data(), 6, &back) && back == clean);
        CHECK(!Endpoint::from_wire(clean.wire().data(), 8, &back));
    }
    // ---- pad wire form: all-zero = neutral, involution ----
    {
        const uint8_t zero[8] = {0};
        uint8_t canon[8]; pad_wire_to_pack(zero, canon);
        const uint8_t neutral[8] = {0, 0, 128, 128, 128, 128, 0, 0};
        CHECK(std::memcmp(canon, neutral, 8) == 0);
        uint8_t w[8], c2[8]; bool ok = true;
        for (int i = 0; i < 256; i++) { uint8_t c[8] = {uint8_t(i), uint8_t(i * 3), uint8_t(i * 5), uint8_t(i * 7), uint8_t(255 - i), uint8_t(i ^ 0x5a), uint8_t(i & 1), 0}; pad_pack_to_wire(c, w); pad_wire_to_pack(w, c2); ok = ok && !std::memcmp(c, c2, 8); }
        CHECK(ok);
    }
    // ---- roster masks ----
    {
        SessionPlan p; p.host = true; p.total_players = 4; p.local_first = 0; p.local_count = 2; p.remotes = {{Endpoint{}, 2, 2}};
        auto m = p.peer_slot_mask(); CHECK(m[0] == 0x3 && m[1] == 0xC && m[2] == 0 && m[3] == 0);
        SessionPlan q; q.total_players = 4; q.local_first = 1; q.local_count = 3; q.remotes = {{Endpoint{}, 0, 1}};
        m = q.peer_slot_mask(); CHECK(m[0] == 0x1 && m[1] == 0xE);
    }
    // ---- message channel: epochs, restart, stale ----
    {
        UdpMux ma, mb; CHECK(ma.open(0) && mb.open(0));
        const Endpoint ea = local_ep(ma), eb = local_ep(mb);
        MessageHub ha(ma, 100), hb(mb, 200);
        MessageChannel* ca = ha.open(eb); (void)hb;
        const uint8_t m1[3] = {'a', 'b', 'c'};
        CHECK(ca->send(m1, 3));
        std::vector<uint8_t> in;
        bool got = until(2000, &ma, &mb, [&] { ha.pump(); hb.pump(); MessageChannel* cb = hb.find(ea); return cb && cb->recv(in); });
        CHECK(got && in.size() == 3);
        CHECK(until(2000, &ma, &mb, [&] { ha.pump(); hb.pump(); return ca->idle(); }));   // acked
        // B restarts on the same port with a HIGHER epoch: its first message (seq 0) must not be dropped as a duplicate of the earlier run
        const uint16_t bport = mb.local_port();
        mb.close();
        UdpMux mb2; std::string err; CHECK(mb2.open(bport, &err));
        MessageHub hb2(mb2, 300);
        MessageChannel* cb2 = hb2.open(ea);
        const uint8_t m2[2] = {'h', 'i'};
        CHECK(cb2->send(m2, 2));
        bool got2 = until(2000, &ma, &mb2, [&] { ha.pump(); hb2.pump(); return ca->recv(in) && in.size() == 2; });
        CHECK(got2);
        CHECK(ca->resets == 1 && ca->peer_epoch() == 300);
        // A's later message reaches the new run
        const uint8_t m3[2] = {'y', 'o'};
        CHECK(ca->send(m3, 2));
        bool got3 = until(2000, &ma, &mb2, [&] { ha.pump(); hb2.pump(); return cb2->recv(in) && in.size() == 2 && in[0] == 'y'; });
        CHECK(got3);
        // A stale packet of the earlier B run (epoch 200) is refused by A
        std::vector<uint8_t> stale(21 + 1, 0);   // type 0, src epoch 200, dst 100, seq 0
        stale[1] = 200; stale[9] = 100; stale[21] = 'Z';
        const uint64_t before = ca->refused;
        CHECK(!ca->on_packet(stale) && ca->refused == before + 1);
        // A packet addressed to an earlier run of the receiver (dst != its epoch) is refused too
        std::vector<uint8_t> wrong(22, 0); wrong[1] = 0x2C; wrong[2] = 0x01; wrong[9] = 55; wrong[21] = 'Q';   // src epoch 300, dst 55
        CHECK(!cb2->on_packet(wrong));
    }
    // ---- rendezvous: stray datagrams must not hijack or fail the host ----
    {
        UdpMux mh, mc, mx; CHECK(mh.open(0) && mc.open(0) && mx.open(0));
        const Endpoint eh = local_ep(mh);
        MessageHub hh(mh), hc(mc);
        DirectRendezvous host(mh, hh, 2, 0x1122334455667788ull);
        // garbage and a well-formed non-HELO first message from a stranger
        const uint8_t junk[7] = {1, 2, 3, 4, 5, 6, 7};
        for (int i = 0; i < 5; i++) mx.send(eh, kTagMessage, junk, 7);
        std::vector<uint8_t> pk(21, 0); pk[0] = 0; pk[1] = 9; pk.push_back('X'); pk.push_back('X'); pk.push_back('X'); pk.push_back('X'); pk.push_back('X');   // data, seq 0, not HELO
        mx.send(eh, kTagMessage, pk.data(), pk.size());
        for (int i = 0; i < 100; i++) { host.poll(); sleep_ms(2); }
        CHECK(host.poll() == 0 && host.rejected_hello >= 1 && hh.ignored >= 5);
        // a HELO with an impossible count is rejected as well (2 + 3 > 4)
        {
            UdpMux my; CHECK(my.open(0)); MessageHub hy(my); MessageChannel* c = hy.open(eh);
            const uint8_t bad[5] = {'H', 'E', 'L', 'O', 3};
            c->send(bad, 5);
            for (int i = 0; i < 100; i++) { hy.pump(); host.poll(); sleep_ms(2); }
            CHECK(host.poll() == 0);
        }
        // the real client still gets in
        DirectRendezvous client(mc, hc, 2, eh);
        bool ready = until(3000, &mh, &mc, [&] { hh.pump(); const int hp = host.poll(), cp = client.poll(); return hp == 1 && cp == 1; });
        if (!ready) std::printf("  rendezvous: host=%d (%s) client=%d (%s) rejected=%llu\n", host.poll(), host.error().c_str(), client.poll(), client.error().c_str(), (unsigned long long)host.rejected_hello);
        CHECK(ready);
        if (ready) {
            CHECK(client.plan().session_id == 0x1122334455667788ull);
            CHECK(host.plan().total_players == 4 && client.plan().local_first == 2 && client.plan().total_players == 4);
            const auto hm = host.plan().peer_slot_mask(), cm = client.plan().peer_slot_mask();
            CHECK(hm == cm && hm[0] == 3 && hm[1] == 12);
        }
        // a later HELO from another endpoint after the client was chosen does not disturb the plan
        UdpMux m2; CHECK(m2.open(0)); MessageHub h2(m2); MessageChannel* c2 = h2.open(eh);
        const uint8_t late[5] = {'H', 'E', 'L', 'O', 2};
        c2->send(late, 5);
        for (int i = 0; i < 50; i++) { h2.pump(); host.poll(); sleep_ms(2); }
        CHECK(host.poll() == 1 && host.plan().remotes.size() == 1 && host.plan().remotes[0].ep == local_ep(mc));
    }
    // ---- bounded queues, oversize ----
    {
        UdpMux a, b; CHECK(a.open(0) && b.open(0));
        const Endpoint eb = local_ep(b);
        uint8_t d[16] = {0};
        for (int round = 0; round < 4; round++) {            // 4 x 250 datagrams, pumped but never popped: the queue must stay bounded
            for (int i = 0; i < 250; i++) { d[0] = (uint8_t)i; a.send(eb, kTagGekko, d, 16); }
            sleep_ms(30); b.pump();
        }
        size_t n = 0; Endpoint f{}; std::vector<uint8_t> v;
        while (b.pop(kTagGekko, f, v)) n++;
        if (n != UdpMux::kMaxQueued || b.overflow == 0) std::printf("  queue: n=%zu overflow=%llu received=%llu\n", n, (unsigned long long)b.overflow, (unsigned long long)b.received);
        CHECK(n == UdpMux::kMaxQueued && b.overflow > 0 && b.received >= 600);
        std::vector<uint8_t> big(2000, 1);
        a.send(eb, kTagGekko, big.data(), big.size());
        CHECK(a.oversize == 1);
    }
    // ---- heartbeat thread: traffic while the main thread does nothing ----
    {
        UdpMux a, b; CHECK(a.open(0) && b.open(0));
        PumpThread pt, ptb; pt.start(a, 77, 0, {local_ep(b)}, 200); ptb.start(b, 0, 1, {}, 1000000);   // B's thread stamps arrivals
        sleep_ms(1100);                                     // the main thread of A is "blocked in a scene load"
        pt.stop(); ptb.stop();
        Heartbeat hb; int n = 0; uint64_t first = 0, last = 0; bool ok = true;
        while (heartbeat_pop(b, &hb)) { if (!n) first = hb.arrived_ms; last = hb.arrived_ms; n++; ok = ok && hb.session_id == 77 && hb.peer == 0; }
        if (!(n >= 4 && ok && last - first >= 600)) std::printf("  heartbeat: n=%d ok=%d span=%llu\n", n, (int)ok, (unsigned long long)(last - first));
        CHECK(n >= 4 && ok && last - first >= 600);
    }
    std::printf("%s (%d checks, %d failed)\n", g_fail ? "FAIL" : "ok", g_n, g_fail);
    return g_fail ? 1 : 0;
}
