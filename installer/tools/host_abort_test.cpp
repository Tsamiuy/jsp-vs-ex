// Real NetSession keepalive ownership; no guest boundary, VM, or game teardown.
#include "jsp_net_session.h"
#include "jsp_stall_rule.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
using namespace jspnet;
using namespace std::chrono_literals;
static SessionPlan plan() { SessionPlan p; p.host=true; p.total_players=2; p.local_count=1; p.local_first=0; Endpoint e; assert(parse_endpoint("127.0.0.1:9",e)); p.remotes={{e,1,1}}; return p; }
static bool wait_failed(NetSession& s, bool polling) {
    const auto until=std::chrono::steady_clock::now()+2s;
    while (!s.failed() && std::chrono::steady_clock::now()<until) { if(polling) s.poll(); std::this_thread::sleep_for(1ms); }
    return s.failed();
}
int main() {
    UdpMux mux; std::string err; assert(mux.open(0,&err)); NetSession s; SessionConfig c; c.keepalive_stale_ms=25;
    assert(!s.host_abort_handle());
    SessionPlan bad; assert(!s.start(mux,bad,c)); assert(!s.host_abort_handle());
    for (int round=0;round<12;round++) {
        assert(s.start(mux,plan(),c)); auto old=s.host_abort_handle(); assert(old);
        jsr::Activations a; a.set(true,0,old); auto pinned=a.current();
        s.stop(); assert(old->retired.load()); assert(!old->post()); assert(!s.host_abort_handle());
        assert(s.start(mux,plan(),c)); auto fresh=s.host_abort_handle(); assert(fresh && fresh!=old);
        a.set(true,0,fresh); // same timestamp rearm; the clock's old pass is retained
        pinned->report_failure(); std::this_thread::sleep_for(12ms); assert(!s.failed());
        std::atomic<bool> read{true}; std::thread observer([&] { while(read.load()) { (void)s.failed(); const auto v=s.failure(); assert(v.empty() || v=="frame clock stalled"); (void)s.running(); (void)s.pace_sleep_ms(33); } });
        a.current()->report_failure(); a.current()->report_failure();
        assert(wait_failed(s,round%2)); // no boundary; half keep guest polls fresh
        assert(s.failure()=="frame clock stalled"); assert(!s.running()); assert(!s.host_abort_handle()); assert(!fresh->post());
        int failures=0; for(const auto& e:s.take_events()) failures+=e.kind==SessionEventKind::Failed;
        assert(failures==1); std::this_thread::sleep_for(12ms); assert(s.take_events().empty());
        // Publish while stop retires and joins its consumer; neither token can affect the next start.
        s.stop(); assert(s.start(mux,plan(),c)); auto racing=s.host_abort_handle(); assert(racing);
        std::atomic<bool> post{true}; std::thread late([&] { while(post.load()) racing->post(); });
        s.stop(); assert(s.start(mux,plan(),c)); post.store(false); late.join();
        std::this_thread::sleep_for(12ms); assert(!s.failed()); read.store(false); observer.join(); s.stop();
    }
    c.keepalive_stale_ms=0; assert(s.start(mux,plan(),c)); assert(!s.host_abort_handle()); s.stop();
    assert(s.start_stress(1,1,c)); assert(!s.host_abort_handle()); s.stop();
    assert(!s.start_replay({},c)); assert(!s.host_abort_handle());
    std::cout<<"host abort: real worker, activation rearm, no boundary, freshness, retirement, snapshots, unavailable controls passed\n";
}
