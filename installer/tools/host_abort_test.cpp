// The frame clock's host abort request (jsp_host_abort.h) against a real NetSession keepalive worker: no guest boundary, VM or game teardown.
// Checks: a handle only for a Game session with a keepalive worker; stop() retires it; a retired (earlier session's) handle cannot end a later
// session; a posted request ends the session through the worker alone, with or without fresh polls (the freshness check does not hide a request);
// exactly one Failed event; the locked accessors (failed / failure / running / pace_sleep_ms) race the worker cleanly; a post racing stop/start
// never ends the next session.
#include "jsp_net_session.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
using namespace jspnet;
using namespace std::chrono_literals;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
static SessionPlan plan() { SessionPlan p; p.host = true; p.total_players = 2; p.local_count = 1; p.local_first = 0; Endpoint e; CHECK(parse_endpoint("127.0.0.1:9", e)); p.remotes = { { e, 1, 1 } }; return p; }
static bool wait_failed(NetSession& s, bool polling)
{
    const auto until = std::chrono::steady_clock::now() + 2s;
    while (!s.failed() && std::chrono::steady_clock::now() < until) { if (polling) s.poll(); std::this_thread::sleep_for(1ms); }
    return s.failed();
}
int main()
{
    UdpMux mux; std::string err; CHECK(mux.open(0, &err)); NetSession s; SessionConfig c; c.keepalive_stale_ms = 25;
    CHECK(!s.host_abort_handle());
    SessionPlan bad; CHECK(!s.start(mux, bad, c)); CHECK(!s.host_abort_handle());
    for (int round = 0; round < 12; round++) {
        CHECK(s.start(mux, plan(), c)); auto old = s.host_abort_handle(); CHECK(old);
        s.stop(); CHECK(old->retired.load()); CHECK(!old->post()); CHECK(!s.host_abort_handle());
        CHECK(s.start(mux, plan(), c)); auto fresh = s.host_abort_handle(); CHECK(fresh && fresh != old);
        old->post(); std::this_thread::sleep_for(12ms); CHECK(!s.failed());   // the ended match's handle (a pass that began before the re-arm) ends nothing
        std::atomic<bool> read{ true };
        std::thread observer([&] { while (read.load()) { (void)s.failed(); const std::string v = s.failure(); CHECK(v.empty() || v == "frame clock stalled"); (void)s.running(); (void)s.pace_sleep_ms(33); } });
        CHECK(fresh->post()); fresh->post();
        CHECK(wait_failed(s, round % 2));   // no boundary; half the rounds keep the polls fresh
        CHECK(s.failure() == "frame clock stalled"); CHECK(!s.running()); CHECK(!s.host_abort_handle()); CHECK(!fresh->post());
        int failures = 0; for (const auto& e : s.take_events()) failures += e.kind == SessionEventKind::Failed;
        CHECK(failures == 1); std::this_thread::sleep_for(12ms); CHECK(s.take_events().empty());
        s.stop(); CHECK(s.start(mux, plan(), c)); auto racing = s.host_abort_handle(); CHECK(racing);
        std::atomic<bool> post{ true }; std::thread late([&] { while (post.load()) racing->post(); });
        s.stop(); CHECK(s.start(mux, plan(), c)); post.store(false); late.join();
        std::this_thread::sleep_for(12ms); CHECK(!s.failed()); read.store(false); observer.join(); s.stop();
    }
    c.keepalive_stale_ms = 0; CHECK(s.start(mux, plan(), c)); CHECK(!s.host_abort_handle()); s.stop();
    CHECK(s.start_stress(1, 1, c)); CHECK(!s.host_abort_handle()); s.stop();
    CHECK(!s.start_replay({}, c)); CHECK(!s.host_abort_handle());
    std::printf("host abort: real worker, retirement, no boundary, freshness, one failure, locked accessors, no handle without a worker: passed\n");
    return 0;
}
