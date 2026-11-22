// net_perf_test.cpp -- host test of the match perf summary (installer/port/jsp/jsp_net_perf.h): press -> shown and press -> remote ack, rollback
// depths, hitches, the first frame shown after a replay, and the summary lines. A fake clock; no game, no GekkoNet. Run by net_perf_test.sh.
#include "jsp_net_perf.h"

#include <algorithm>
#include <cstdio>
#include <string>

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static int count(const std::string& s, const std::string& w) { int n = 0; for (size_t i = s.find(w); i != std::string::npos; i = s.find(w, i + 1)) n++; return n; }

int main()
{
    // Dist: percentiles are 2 ms bucket tops, never above the max
    {
        jpperf::Dist d;
        for (int i = 1; i <= 100; i++) d.add(i);
        CHECK(d.n == 100); CHECK(d.max == 100);
        CHECK(d.pct(50) == 50 || d.pct(50) == 52);
        CHECK(d.pct(90) >= 90 && d.pct(90) <= 92);
        CHECK(d.pct(100) == 100);
        jpperf::Dist o; o.add(5000);
        CHECK(o.pct(50) == 5000);
        jpperf::Dist e; CHECK(e.pct(90) == 0);
    }
    // hitches: only gaps above 50 ms between presented frames; replay iterations are no frame
    {
        jpperf::Match m; double t = 0;
        for (int i = 0; i < 30; i++) { jpperf::on_frame(m, t, true, 2); t += 33.3; }
        CHECK(m.hitch.n == 0); CHECK(m.frames == 30);
        t += 100;                                       // one 133 ms gap
        jpperf::on_frame(m, t, true, 2);
        CHECK(m.hitch.n == 1); CHECK(m.hitch.max > 133 && m.hitch.max < 134);
        t += 10; jpperf::on_frame(m, t, false, 2); t += 10; jpperf::on_frame(m, t, false, 2);   // two replay ticks, not shown
        t += 40; jpperf::on_frame(m, t, true, 2);       // 60 ms since the last shown frame
        CHECK(m.hitch.n == 2); CHECK(m.frames == 32);
        CHECK(m.at_delay[2] > 1000 && m.at_delay[3] == 0);
    }
    // rollbacks: depth counts, 8+ bucket, and the first shown frame after the replay (back-to-back rollbacks: from the first)
    {
        jpperf::Match m; double t = 1000;
        jpperf::on_frame(m, t, true);
        jpperf::on_rollback(m, t + 5, 3);
        jpperf::on_frame(m, t + 20, false);             // replay tick
        jpperf::on_rollback(m, t + 25, 1);              // another before anything shown
        jpperf::on_frame(m, t + 40, false);
        jpperf::on_frame(m, t + 75, true);              // first shown: 70 ms after the first rollback
        CHECK(m.shown.n == 1); CHECK(m.shown.max == 70);
        jpperf::on_rollback(m, t + 100, 12); jpperf::on_rollback(m, t + 200, 0);
        CHECK(m.rollbacks == 3); CHECK(m.depth[2] == 1 && m.depth[0] == 1 && m.depth[7] == 1); CHECK(m.deepest == 12);
    }
    // input: press -> shown is the press until the frame after the first live Advance holding the press; press -> ack from the ack history
    {
        jpperf::Match m; double t = 0;
        jpperf::on_frame(m, t, true, 3);
        jpperf::on_local(m, t + 1, 0x0000);
        jpperf::on_local(m, t + 2, 0x0040);             // press X at 2 ms
        CHECK(m.waiting && m.presses == 1);
        jpperf::on_local(m, t + 35, 0x0041);            // another button while one waits: not sampled
        CHECK(m.presses == 1);
        CHECK(jpperf::wants_ack(m));
        jpperf::on_ack(m, t + 33, 100);                 // the other PC acks up to frame 100 (before the press's frame)
        jpperf::on_advance(m, 101, false, 0x0000);      // live frames without the press (input delay)
        jpperf::on_advance(m, 102, true, 0x0040);       // a replay holding it does not count
        CHECK(m.press_frame == -1);
        jpperf::on_ack(m, t + 66, 104);                 // the ack of frame 104 arrives at 66 ms: before this PC runs it
        jpperf::on_advance(m, 104, false, 0x0040);      // frame 104 is the press's frame
        CHECK(m.press_frame == 104);
        CHECK(m.acked); CHECK(m.confirm.n == 1 && m.confirm.max == 64);   // 66 - 2: the ack seen before the frame was known still counts
        jpperf::on_frame(m, t + 110, true, 3);
        CHECK(m.applied); CHECK(m.apply.n == 1 && m.apply.max == 108);
        CHECK(!m.waiting);
        // a later ack-after-shown press
        jpperf::on_local(m, t + 200, 0x0001);           // 0x0041 -> 0x0001: nothing new
        CHECK(m.presses == 1);
        jpperf::on_local(m, t + 233, 0x0000);
        jpperf::on_local(m, t + 266, 0x0002);           // press at 266
        jpperf::on_advance(m, 110, false, 0x0002);
        jpperf::on_frame(m, t + 300, true, 3);
        CHECK(m.apply.n == 2 && m.apply.max == 108);
        jpperf::on_ack(m, t + 320, 109); CHECK(m.confirm.n == 1);
        jpperf::on_ack(m, t + 320, 109);                // the same ack again: one history entry
        CHECK(m.ack_n == 1);
        jpperf::on_ack(m, t + 400, 111); CHECK(m.confirm.n == 2 && m.confirm.max == 134);
        CHECK(!jpperf::wants_ack(m));
        // a press that never reaches a frame times out after 2 s and frees the sampler
        jpperf::on_local(m, t + 500, 0x0000); jpperf::on_local(m, t + 533, 0x0100);
        CHECK(m.waiting);
        jpperf::on_frame(m, t + 2600, true, 3);
        CHECK(!m.waiting && m.timeouts == 1);
        jpperf::on_local(m, t + 2633, 0x0000); jpperf::on_local(m, t + 2666, 0x0200);
        CHECK(m.presses == 4 && m.waiting);
    }
    // the summary: five [perf] lines, the connection named, no line when nothing was measured missing its fields
    {
        jpperf::Match m; double t = 0;
        for (int i = 0; i < 100; i++) { jpperf::on_frame(m, t, true, 2); t += 33.3; }
        jpperf::on_rollback(m, t, 4);
        const std::string s = jpperf::lines(m, "relay", "re-psn", "host", 3, 182.4);
        std::printf("%s", s.c_str());
        CHECK(count(s, "[perf] ") == 5); CHECK(count(s, "\n") == 5);
        CHECK(s.find("[perf] match 3: path relay, via re-psn, host, 182.4 s, 100 frames shown") != std::string::npos);
        CHECK(s.find("depth 1:0 2:0 3:0 4:1 5:0") != std::string::npos);
        CHECK(s.find("delay 2:3s") != std::string::npos);
        CHECK(s.find("[perf] hitches: 0 gaps > 50 ms") != std::string::npos);
        const std::string u = jpperf::lines(jpperf::Match(), "", "direct IP", "join", 1, 0);
        CHECK(u.find("path unknown, via direct IP, join") != std::string::npos);
        CHECK(u.find("| delay -") != std::string::npos);
        size_t longest = 0, a = 0; for (size_t i = s.find('\n'); i != std::string::npos; a = i + 1, i = s.find('\n', a)) longest = std::max(longest, i - a);
        CHECK(longest < 200);
    }
    std::printf("net perf: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
