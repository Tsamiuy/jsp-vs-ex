// net_delay_rule_test.cpp -- host test of the adaptive input delay decision (installer/port/jsp/jsp_net_delay_rule.h): rule 1 is the
// 2026-11-21 formula unchanged, rule 2's percentile trip, high-ping margin, fight-start guard, depth trigger, hold and one-tick lowering.
// A fake clock and fake stats; no GekkoNet. Run by net_delay_rule_test.sh.
#include "jsp_net_delay_rule.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

struct Sim {
    jpdr::Params p; jpdr::State s; int cur = 2; uint64_t t = 100000;
    std::function<double(uint64_t)> rtt = [](uint64_t) { return 100.0; };   // single round trip at time t (ms since start)
    double avg = 100, jit = 2;
    uint64_t t0 = 0;
    int changes = 0; const char* last_why = "";
    // runs ms of live ticks (one per 33 ms); returns the delay
    int run(uint64_t ms)
    {
        const uint64_t end = t + ms;
        for (; t < end; t += 33) {
            if (!t0) t0 = t;
            const jpdr::Decision d = jpdr::step(s, p, t, [&] { jpdr::Reading r; r.ping_avg = avg; r.jitter = jit; r.last_ping = rtt(t - t0); r.cur = cur; return r; });
            if (d.want >= 0 && d.want != cur) { cur = d.want; changes++; last_why = d.why; }
        }
        return cur;
    }
};

int main()
{
    // rule 1: no reading before 10 s; ping 129, jitter 36 -> ceil(((129 - 33.3) / 2 + 36 + 8) / 33.3) = 3, after two readings
    {
        Sim m; m.p.rule = 1; m.p.clamp(); m.avg = 129; m.jit = 36; m.cur = 2;
        CHECK(m.run(9900) == 2);
        CHECK(m.run(2000) == 2);   // first reading at 10 s: one vote
        CHECK(m.run(2000) == 3);   // second reading
        CHECK(m.changes == 1);
        // a two-tick jump goes at once: ping 250, jitter 40 -> ceil((108.3 + 40 + 8) / 33.3) = 5 -> 4
        Sim j; j.p.rule = 1; j.p.clamp(); j.avg = 250; j.jit = 40;
        CHECK(j.run(10100) == 4);
        // lowering needs five readings
        Sim l; l.p.rule = 1; l.p.clamp(); l.avg = 20; l.jit = 1; l.cur = 3;
        CHECK(l.run(10100 + 3 * 2000) == 3);   // readings at 10, 12, 14, 16 s: four votes
        CHECK(l.run(2000) == 1);   // the earlier rule lowers to the target at once
    }
    // rule 2 defaults: 99 ms steady (+ 16 ms answer wait in the samples): one way (115 - 33.3) / 2 = 40.8, spread 0, margin 16 -> 2 ticks
    {
        Sim m; m.p.clamp(); m.rtt = [](uint64_t) { return 115.0; };
        CHECK(m.run(30000) == 2);
        CHECK(m.changes == 0);
    }
    // rule 2: +-36 jitter on 99 ms (RTT 115 +- 50): p90 well above the median -> 3 or 4 ticks, reached within the first 12 s
    {
        Sim m; m.p.clamp(); unsigned k = 1;
        m.rtt = [&k](uint64_t) { k = k * 1103515245u + 12345u; return 115.0 + ((double)((k >> 8) % 1001) / 1000.0 * 100.0 - 50.0); };
        m.run(12000);
        CHECK(m.cur >= 3);
        m.run(30000);
        CHECK(m.cur >= 3 && m.cur <= 4);
    }
    // the high-ping margin: median 75 (below 80): (75 - 33.3) / 2 + 8 = 28.9 -> 1; median 85: (85 - 33.3) / 2 + 16 = 41.9 -> 2
    {
        Sim a; a.p.clamp(); a.cur = 1; a.rtt = [](uint64_t) { return 75.0; };
        CHECK(a.run(30000) == 1);
        Sim b; b.p.clamp(); b.cur = 1; b.rtt = [](uint64_t) { return 85.0; };
        CHECK(b.run(30000) == 2);
    }
    // the fight-start guard: round trips of 400 ms in the first 4 s, 60 ms after: at most start + 1 before 10 s, back down later
    {
        Sim m; m.p.clamp(); m.rtt = [](uint64_t t) { return t < 4500 ? 400.0 : 60.0; };
        int hi = 0; for (int i = 0; i < 10; i++) { m.run(1000); hi = std::max(hi, m.cur); }
        CHECK(hi <= 3);
        m.run(40000);
        CHECK(m.cur == 1);   // (60 - 30) / 2 + 8 = 23 ms -> 1 tick, lowered one tick at a time
    }
    // a slow link seen early: 129 ms steady + wait -> 3 ticks at the first reading (4 s), not at 10 s
    {
        Sim m; m.p.clamp(); m.rtt = [](uint64_t) { return 145.0; };   // (145 - 33.3) / 2 + 16 = 71.8 -> 3
        m.run(4100);
        CHECK(m.cur == 3);
    }
    // the depth trigger: two deep rollbacks (as two events) within 10 s raise by one at once and hold the delay 20 s
    {
        Sim m; m.p.clamp(); m.rtt = [](uint64_t) { return 60.0; };   // the trip alone wants 1
        m.cur = 2;
        m.run(5000);
        CHECK(m.cur == 2);   // the guard: median of 60 -> 1, but down needs five readings
        jpdr::note_rollback(m.s, m.p, m.t, 6); jpdr::note_rollback(m.s, m.p, m.t + 33, 6);   // one event (consecutive ticks)
        m.run(1000);
        CHECK(m.cur == 2);
        m.t += 3000; jpdr::note_rollback(m.s, m.p, m.t, 5);
        m.run(100);
        CHECK(m.cur == 3); CHECK(std::string(m.last_why) == "rollbacks");
        m.run(19000);
        CHECK(m.cur == 3);   // held
        m.run(30000);
        CHECK(m.cur < 3);    // then down, one tick at a time
        // shallow rollbacks never trigger; DEEP_COUNT=0 turns it off
        Sim n; n.p.clamp(); n.rtt = [](uint64_t) { return 60.0; }; n.run(5000);
        for (int i = 0; i < 6; i++) { n.t += 1000; jpdr::note_rollback(n.s, n.p, n.t, 2); n.run(33); }
        CHECK(n.s.deep_n == 0 && n.s.deep_raises == 0);
        Sim o; o.p.deep_count = 0; o.p.clamp(); o.rtt = [](uint64_t) { return 60.0; }; o.run(5000);
        for (int i = 0; i < 4; i++) { o.t += 1000; jpdr::note_rollback(o.s, o.p, o.t, 8); o.run(33); }
        CHECK(o.s.deep_raises == 0);
        // rollbacks before the first reading time are the fight-start stall: not counted
        Sim q; q.p.clamp(); q.rtt = [](uint64_t) { return 60.0; }; q.run(100);
        jpdr::note_rollback(q.s, q.p, q.t, 8); q.t += 1000; jpdr::note_rollback(q.s, q.p, q.t, 8);
        CHECK(q.s.deep_n == 0);
    }
    // settled(): the delay held longest, only after 20 s; ties go to the higher
    {
        jpdr::State st; uint64_t t = 1000;
        for (int i = 0; i < 300; i++, t += 33) jpdr::note_time(st, t, 2);   // ~10 s at 2
        CHECK(jpdr::settled(st) == -1);
        for (int i = 0; i < 400; i++, t += 33) jpdr::note_time(st, t, 3);   // ~13 s at 3
        CHECK(jpdr::settled(st) == 3);
    }
    // the guard's mirrored spread: a +-36 ms link read at 4 s already wants 3 (median 115, 10th percentile ~75: (115 - 33.3) / 2 + 40 + 16 > 66.7)
    {
        Sim m; m.p.clamp(); unsigned k = 7;
        m.rtt = [&k](uint64_t) { k = k * 1103515245u + 12345u; return 115.0 + ((double)((k >> 8) % 1001) / 1000.0 * 100.0 - 50.0); };
        m.run(4100);
        CHECK(m.cur == 3);
        Sim n; n.p.guard_spread = 0; n.p.clamp(); unsigned k2 = 7;
        n.rtt = [&k2](uint64_t) { k2 = k2 * 1103515245u + 12345u; return 115.0 + ((double)((k2 >> 8) % 1001) / 1000.0 * 100.0 - 50.0); };
        n.run(4100);
        CHECK(n.cur == 2);   // the median alone: (115 - 33.3) / 2 + 16 = 56.9 -> 2
    }
    // the range: MAX caps every decision
    {
        Sim m; m.p.hi = 3; m.p.clamp(); m.rtt = [](uint64_t) { return 400.0; };
        CHECK(m.run(30000) == 3);
    }
    std::printf("net delay rule: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
