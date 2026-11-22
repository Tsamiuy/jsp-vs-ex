// jsp_net_perf.h -- ENHANCEMENT (match perf summary, review of PR 248: "treat the performance claims as awaiting measurements"): what a rollback match
// felt like on THIS PC, as a few compact log lines at the end of every match, so the logs of two PCs (direct and relay) can be compared line by line.
// Pure (no game, no GekkoNet, no Windows): jsp_net_game.cpp feeds it, installer/tools/net_perf_test.cpp tests it. Main thread only.
//
// What it measures (all wall time on this PC, milliseconds):
//   input    press -> shown: a button newly pressed on the local pad (local_pad) until the frame that runs the tick with that press is presented (the
//            first live Advance whose local input holds the press, then the next presented frame). It includes the input delay.
//            press -> remote ack: until the other PC acknowledged the GekkoNet frame that carries the press (its last_acked_frame, read at each frame
//            while a press waits: boundary resolution, about 33 ms). One press is followed at a time; presses while one waits are not sampled, a press
//            not seen within 2 s is dropped (counted as timed out).
//   rollbacks the depth of every rollback (ticks gone back), as counts per depth 1..7 and 8+, and the deepest.
//   hitches  the gap between two presented frames above 50 ms (replay ticks are not presented, so a replay's catch-up is inside the gap).
//   replay shown  from a rollback until the first presented frame after its replay (the first frame that shows the corrected state).
// Cost: a few adds per frame; one stats read per frame only while a press waits for its ack. Histograms of 2 ms buckets (percentiles are bucket tops).
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace jpperf {

inline constexpr double kHitchMs = 50.0;
inline constexpr double kPressTimeoutMs = 2000.0;

// A distribution in 2 ms buckets up to 1 s, the overflow in the last bucket (its percentile is the max).
struct Dist {
    static constexpr int kBuckets = 512; static constexpr double kStep = 2.0;
    uint32_t b[kBuckets] = {}; uint32_t n = 0; double sum = 0, max = 0;
    void add(double ms)
    {
        if (ms < 0) ms = 0;
        int i = (int)(ms / kStep); if (i >= kBuckets) i = kBuckets - 1;
        b[i]++; n++; sum += ms; if (ms > max) max = ms;
    }
    double pct(int p) const   // the top of the bucket that holds the p-th percentile (the max for the overflow bucket or when smaller)
    {
        if (!n) return 0;
        uint64_t want = ((uint64_t)n * (uint64_t)p + 99) / 100; if (want < 1) want = 1;
        uint64_t c = 0;
        for (int i = 0; i < kBuckets; i++) { c += b[i]; if (c >= want) { const double top = (i + 1) * kStep; return i == kBuckets - 1 || top > max ? max : top; } }
        return max;
    }
};

struct Match {
    // frames and hitches
    double last_frame = -1; uint64_t frames = 0; Dist hitch; double hitch_total = 0;
    double at_delay[9] = {};   // ms between presented frames spent at each input delay (the delay passed to on_frame)
    // rollbacks and the first frame shown after one
    uint32_t depth[8] = {}; uint32_t rollbacks = 0, deepest = 0;
    double replay_t = -1; Dist shown;
    // input
    uint16_t prev_buttons = 0;
    bool waiting = false; double press_t = 0; uint16_t press_bits = 0; int press_frame = -1; bool applied = false, acked = false;
    struct Ack { int frame; double t; } ack[32] = {}; int ack_n = 0; int ack_last = -2;
    uint32_t presses = 0, timeouts = 0; Dist apply, confirm;
};

// The press is done when both its times are in (or it timed out).
inline void press_check(Match& m, double now)
{
    if (!m.waiting) return;
    if (m.applied && m.acked) { m.waiting = false; return; }
    if (now - m.press_t > kPressTimeoutMs) { m.waiting = false; m.timeouts++; }
}

// A presented frame at `now` (ms), with this PC's input delay `delay` in ticks. presented = false for an iteration that ran a replay tick (nothing shown):
// it neither ends a gap nor shows a replay.
inline void on_frame(Match& m, double now, bool presented, int delay = -1)
{
    if (!presented) return;
    if (m.last_frame >= 0) {
        const double gap = now - m.last_frame; if (gap > kHitchMs) { m.hitch.add(gap); m.hitch_total += gap; }
        if (delay >= 0 && delay <= 8) m.at_delay[delay] += gap;
    }
    m.last_frame = now; m.frames++;
    if (m.replay_t >= 0) { m.shown.add(now - m.replay_t); m.replay_t = -1; }
    if (m.waiting && m.press_frame >= 0 && !m.applied) { m.applied = true; m.apply.add(now - m.press_t); }
    press_check(m, now);
}

// A rollback of `depth` ticks at `now`. Back-to-back rollbacks before anything is shown count once for "replay shown" (from the first).
inline void on_rollback(Match& m, double now, int64_t depth)
{
    if (depth <= 0) return;
    m.rollbacks++; m.depth[depth >= 8 ? 7 : depth - 1]++; if ((uint64_t)depth > m.deepest) m.deepest = (uint32_t)depth;
    if (m.replay_t < 0) m.replay_t = now;
}

// The local pad's buttons sampled for an update (local player 0). A newly pressed button starts a sample when none waits.
inline void on_local(Match& m, double now, uint16_t buttons)
{
    const uint16_t fresh = (uint16_t)(buttons & ~m.prev_buttons);
    m.prev_buttons = buttons;
    press_check(m, now);
    if (!fresh || m.waiting) return;
    m.waiting = true; m.press_t = now; m.press_bits = fresh; m.press_frame = -1; m.applied = m.acked = false; m.ack_n = 0; m.ack_last = -2; m.presses++;
}

inline void ack_resolve(Match& m)
{
    if (m.press_frame < 0 || m.acked) return;
    for (int i = 0; i < m.ack_n; i++) if (m.ack[i].frame >= m.press_frame) { m.acked = true; m.confirm.add(m.ack[i].t - m.press_t); return; }
}

// An Advance event for GekkoNet frame `frame` whose input of this PC's local player 0 is `local_buttons`. The first live one holding the press is its frame.
inline void on_advance(Match& m, int frame, bool rolling_back, uint16_t local_buttons)
{
    if (!m.waiting || m.press_frame >= 0 || rolling_back || (local_buttons & m.press_bits) != m.press_bits) return;
    m.press_frame = frame;
    ack_resolve(m);
}

inline bool wants_ack(const Match& m) { return m.waiting && !m.acked; }

// The other PC's ack of this PC's input (GekkoNet frame, -1 none) read at `now`; call it each frame while wants_ack().
inline void on_ack(Match& m, double now, int acked_frame)
{
    if (!wants_ack(m) || acked_frame == m.ack_last) return;
    m.ack_last = acked_frame;
    if (m.ack_n == 32) { for (int i = 1; i < 32; i++) m.ack[i - 1] = m.ack[i]; m.ack_n--; }
    m.ack[m.ack_n++] = {acked_frame, now};
    ack_resolve(m);
    press_check(m, now);
}

// The summary: five lines, each starting "[perf] ", with the same fields in the same order on every PC. `path`: "direct", "relay" or "" (unknown);
// `via`: "re-psn" or "direct IP". The input line ends with the time spent at each input delay.
inline std::string lines(const Match& m, const char* path, const char* via, const char* role, uint32_t match_k, double secs)
{
    std::string s; char b[512];
    auto d3 = [](const Dist& d, char* o, size_t n) { std::snprintf(o, n, "p50 %.0f p90 %.0f max %.0f", d.pct(50), d.pct(90), d.max); };
    std::snprintf(b, sizeof b, "[perf] match %u: path %s, via %s, %s, %.1f s, %llu frames shown\n", (unsigned)match_k, path && path[0] ? path : "unknown", via, role, secs,
                  (unsigned long long)m.frames);
    s += b;
    char a[96], c[96]; d3(m.apply, a, sizeof a); d3(m.confirm, c, sizeof c);
    std::string dl;
    for (int d = 0; d <= 8; d++) if (m.at_delay[d] >= 500) { std::snprintf(b, sizeof b, " %d:%.0fs", d, m.at_delay[d] / 1000.0); dl += b; }
    std::snprintf(b, sizeof b, "[perf] input: %u presses sampled, %u timed out | press->shown ms (%u) %s | press->remote ack ms (%u) %s | delay%s\n", m.presses, m.timeouts,
                  m.apply.n, a, m.confirm.n, c, dl.empty() ? " -" : dl.c_str());
    s += b;
    std::snprintf(b, sizeof b, "[perf] rollbacks: %u | depth 1:%u 2:%u 3:%u 4:%u 5:%u 6:%u 7:%u 8+:%u | deepest %u\n", m.rollbacks, m.depth[0], m.depth[1], m.depth[2], m.depth[3],
                  m.depth[4], m.depth[5], m.depth[6], m.depth[7], m.deepest);
    s += b;
    d3(m.hitch, a, sizeof a);
    std::snprintf(b, sizeof b, "[perf] hitches: %u gaps > %.0f ms | %.2f s in them | ms %s\n", m.hitch.n, kHitchMs, m.hitch_total / 1000.0, a);
    s += b;
    d3(m.shown, a, sizeof a);
    std::snprintf(b, sizeof b, "[perf] replay shown: %u | rollback->first shown frame ms %s\n", m.shown.n, a);
    s += b;
    return s;
}

}  // namespace jpperf
