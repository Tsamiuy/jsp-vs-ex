// jsp_net_delay_rule.h -- ENHANCEMENT (adaptive input delay, rule 2): the decision of jsp_net_game.cpp adapt_delay(), pure (no game, no GekkoNet, no
// Windows), so the host simulator (net_four_sim.cpp) runs the very same code. The caller reads the switches once (jsp_setting_get), feeds rollbacks
// (note_rollback) and calls step() once per live tick with a reader for the session's network stats; step() says when to set a new delay.
//
// Each peer sets only its OWN input delay. GekkoNet stamps every input with its frame and both PCs simulate the same stamped inputs, so a delay change only
// moves which frame a local input lands on; the peers need not agree and the determinism rule is not touched (see jsp_net_game.cpp adapt_delay).
//
// Rule 1 (JSP_INPUT_DELAY_RULE=1, the 2026-11-21 rule, kept for A/B): every 2 s from 10 s after the sync, ticks = ceil(((ping - min(ping/2, 33)) / 2 +
// EMA(jitter) + margin) / 33.3); a raise of one needs two readings in a row, a lower five. Its problems in the testers' logs (support bundle 1, ping 99-129 ms):
// it reacted late (a match ran at 2 ticks while it needed 3), the EMA hid the spikes, and nothing looked at the rollbacks that actually happened.
// Rule 2 (default): the same trip formula on a window of GekkoNet's single round-trip samples (one every 500 ms, the last 8 s) instead of its 5 s average
// and the EMA. FIX (RTT sampling, review of PR 248: repeated polls timestamped the same cached round trip as fresh history): a sample enters the window only
// when GekkoNet's round-trip count (Reading::rtt_seq) moved since the last one taken, so a poll with no new ping answer adds nothing; the polls run every
// 250 ms (twice the ping rate) so that no answer is missed. The window is 16 MEASURED round trips, never one answer counted twice.
//   one way = (median RTT - min(median / 2, 33.3)) / 2           (the answer wait taken off, as rule 1)
//   spread  = RTT at the JSP_INPUT_DELAY_AUTO_PCT percentile (90) - median RTT   (all of it, as if the whole excursion were one way)
//   margin  = JSP_INPUT_DELAY_AUTO_MARGIN_MS (8), or JSP_INPUT_DELAY_AUTO_HIGH_MARGIN_MS (16) once the median RTT is >= JSP_INPUT_DELAY_AUTO_HIGH_PING_MS (80)
//   ticks   = ceil((one way + spread + margin) / 33.3), clamped to MIN..MAX (1..4)
// First reading JSP_INPUT_DELAY_AUTO_FIRST_MS (4000) after the sync, then every 2 s; samples are kept from 2 s after the sync. Fight-start guard (the
// fight's first seconds build pipelines and answer pings late: native-rb1/rb2 read 243-416 ms on a 60 ms link): before 10 s the spread is mirrored from
// the low side (median - 10th percentile: a stall only makes answers late) and no decision goes above the start delay + 1; from 10 s on, samples taken in
// the first 5 s are no longer used. A rematch on the same link may start at settled() of the match before (jsp_net_game.cpp, JSP_INPUT_DELAY_AUTO_CARRY).
// One reading raises (JSP_INPUT_DELAY_AUTO_UP_VOTES, 1); lowering is one tick at a time after five calm readings (10 s).
// Rollback depth: rollbacks deeper than JSP_INPUT_DELAY_AUTO_DEEP_TICKS (2) are events (rollbacks within 300 ms of the last one are the same event: the testers'
// deep rollbacks came in pairs on consecutive ticks). JSP_INPUT_DELAY_AUTO_DEEP_COUNT (2) events within JSP_INPUT_DELAY_AUTO_DEEP_WINDOW_MS (10000) raise the delay
// by one at once, and no reading lowers it for JSP_INPUT_DELAY_AUTO_HOLD_MS (20000). Rollbacks in the first FIRST_MS after the sync are not counted (the stall).
// What this peer sees is the other peer's inputs arriving late, which the OTHER peer's delay covers; on a two-PC link both directions see much the same, so
// both peers raise. JSP_INPUT_DELAY_AUTO_DEEP_COUNT=0 turns the depth trigger off.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace jpdr {

inline constexpr double kTickMs = 1000.0 / 30.0;
inline constexpr int kMaxSamples = 32;
inline constexpr int kMaxDeep = 8;

struct Params {
    int rule = 2;                 // JSP_INPUT_DELAY_RULE: 1 = the 2026-11-21 rule, 2 = percentile + rollback depth
    int lo = 1, hi = 4;           // JSP_INPUT_DELAY_AUTO_MIN / _MAX
    int margin_ms = 8;            // JSP_INPUT_DELAY_AUTO_MARGIN_MS
    // rule 2
    int first_ms = 4000;          // JSP_INPUT_DELAY_AUTO_FIRST_MS: the first reading after the sync (rule 1: 10000)
    int every_ms = 2000;          // a reading every 2 s
    int sample_from_ms = 2000;    // RTT samples are kept from this long after the sync
    int sample_ms = 250;          // the stats poll: GekkoNet pings every 500 ms (NetStats::NET_CHECK_DELAY); a poll keeps only a new round trip (rtt_seq)
    int samples = 16;             // JSP_INPUT_DELAY_AUTO_SAMPLES: the window (16 x 500 ms = 8 s)
    int pct = 90;                 // JSP_INPUT_DELAY_AUTO_PCT
    int high_ping_ms = 80;        // JSP_INPUT_DELAY_AUTO_HIGH_PING_MS
    int high_margin_ms = 16;      // JSP_INPUT_DELAY_AUTO_HIGH_MARGIN_MS
    int guard_ms = 10000;         // fight-start guard: before this, no decision goes above the start delay + guard_raise, and readings use the median only
    int guard_raise = 1;
    int early_ms = 5000;          // samples taken before this are used only by the guarded readings (the stall answers pings late)
    int guard_spread = 1;         // guarded readings: 1 = the spread is mirrored from the low side (median - the (100 - PCT) percentile: a stall only
                                  // makes answers late, never early), 0 = no spread
    int up_votes = 1;             // JSP_INPUT_DELAY_AUTO_UP_VOTES
    int down_votes = 5;           // JSP_INPUT_DELAY_AUTO_DOWN_VOTES
    int deep_ticks = 2;           // JSP_INPUT_DELAY_AUTO_DEEP_TICKS
    int deep_count = 2;           // JSP_INPUT_DELAY_AUTO_DEEP_COUNT (0 = off)
    int deep_window_ms = 10000;   // JSP_INPUT_DELAY_AUTO_DEEP_WINDOW_MS
    int deep_merge_ms = 300;
    int hold_ms = 20000;          // JSP_INPUT_DELAY_AUTO_HOLD_MS

    void clamp()
    {
        if (rule != 1) rule = 2;
        lo = std::max(lo, 0); hi = std::min(hi, 8); hi = std::max(hi, lo);
        margin_ms = std::max(margin_ms, 0); high_margin_ms = std::max(high_margin_ms, 0);
        first_ms = std::max(first_ms, 1000);
        samples = std::min(std::max(samples, 3), kMaxSamples);
        pct = std::min(std::max(pct, 50), 100);
        up_votes = std::max(up_votes, 1); down_votes = std::max(down_votes, 1);
        deep_ticks = std::max(deep_ticks, 1); deep_count = std::min(std::max(deep_count, 0), kMaxDeep);
        deep_window_ms = std::max(deep_window_ms, 1000); hold_ms = std::max(hold_ms, 0);
    }
};

// What step() reads from the session (only when it needs it): GekkoNet's 5 s average round trip and jitter, its last single round trip, how many round
// trips it measured so far (rtt_seq: last_ping is new only when this moved), and this peer's delay.
struct Reading { double ping_avg = -1, jitter = 0, last_ping = -1; uint32_t rtt_seq = 0; int cur = 0; };

struct Decision {
    int want = -1;                // >= 0: set this delay now (differs from Reading::cur)
    int cur = 0;
    const char* why = "";         // "trip" (a reading) or "rollbacks" (the depth trigger)
    double ping = 0, jitter = 0;  // rule 1: GekkoNet's average and jitter; rule 2: median RTT and spread (percentile - median)
    double need_ms = 0;
    double ping_avg = -1, jitter_avg = 0;   // GekkoNet's average and jitter at this reading (the log line)
};

struct State {
    uint64_t start = 0, next = 0, next_sample = 0;
    double jit_ema = -1; int up = 0, down = 0;
    float rtt[kMaxSamples] = {}; uint64_t rtt_t[kMaxSamples] = {}; int n = 0, head = 0;
    uint32_t rtt_seq = 0;         // the round-trip count of the newest sample taken (Reading::rtt_seq)
    uint32_t rtt_taken = 0, rtt_stale = 0;   // polls that took a new round trip / found none new (diagnostics)
    int start_delay = -1;         // the delay at the first reading (the guard's base)
    uint64_t deep_t[kMaxDeep] = {}; int deep_n = 0; uint64_t last_deep = 0;
    uint64_t hold_until = 0;
    uint32_t deep_raises = 0;
    uint64_t at_ms[9] = {}; uint64_t last_t = 0;   // time spent at each delay (note_time), for settled()
};

// Time at the current delay: call once per live tick with the delay in effect (cheap, no stats).
inline void note_time(State& s, uint64_t now, int cur)
{
    if (s.last_t && now > s.last_t && cur >= 0 && cur <= 8) s.at_ms[cur] += now - s.last_t;
    s.last_t = now;
}

// The delay this match spent the most time at (ties: the higher), or -1 before 20 s. A rematch on the same link starts there (JSP_INPUT_DELAY_AUTO_CARRY):
// the start value of a match otherwise comes from the manifest (2), and the first seconds at a delay the link does not cover are where the testers' first
// rollbacks were (support bundle 1: 5 ticks at 2.4 s).
inline int settled(const State& s)
{
    uint64_t total = 0; int best = -1;
    for (int d = 0; d <= 8; d++) { total += s.at_ms[d]; if (s.at_ms[d] && (best < 0 || s.at_ms[d] >= s.at_ms[best])) best = d; }
    return total >= 20000 ? best : -1;
}

// A rollback of `depth` ticks seen at `now` (ms). Main thread, as step().
inline void note_rollback(State& s, const Params& p, uint64_t now, int64_t depth)
{
    if (p.rule != 2 || p.deep_count == 0 || !s.start || now < s.start + (uint64_t)p.first_ms || depth <= p.deep_ticks) return;
    if (s.last_deep && now - s.last_deep < (uint64_t)p.deep_merge_ms) { s.last_deep = now; return; }   // the same event
    s.last_deep = now;
    if (s.deep_n == kMaxDeep) { for (int i = 1; i < kMaxDeep; i++) s.deep_t[i - 1] = s.deep_t[i]; s.deep_n--; }
    s.deep_t[s.deep_n++] = now;
}

inline double percentile(float* v, int n, int pct)   // sorts v
{
    std::sort(v, v + n);
    int k = (int)std::ceil(pct / 100.0 * n) - 1; if (k < 0) k = 0; if (k >= n) k = n - 1;
    return v[k];
}

inline int ticks_for(double need_ms, const Params& p)
{
    int want = (int)std::ceil(need_ms / kTickMs);
    return want < p.lo ? p.lo : want > p.hi ? p.hi : want;
}

// Once per live tick while the session is synced. `read` returns a Reading; it is called only when the stats are needed (every 500 ms at most, or a trigger).
template <class Read>
Decision step(State& s, const Params& p, uint64_t now, Read&& read)
{
    Decision d;
    if (!s.start) { s.start = now; s.next = now + (uint64_t)(p.rule == 1 ? 10000 : p.first_ms); s.next_sample = now + (uint64_t)p.sample_from_ms; return d; }
    if (p.rule == 1) {
        if (now < s.next) return d;
        s.next = now + 2000;
        const Reading r = read(); d.cur = r.cur; d.ping_avg = r.ping_avg; d.jitter_avg = r.jitter;
        if (r.ping_avg < 0) return d;
        s.jit_ema = s.jit_ema < 0 ? r.jitter : s.jit_ema * 0.75 + r.jitter * 0.25;
        const double wait_ms = std::min(r.ping_avg * 0.5, kTickMs);
        d.need_ms = (r.ping_avg - wait_ms) / 2.0 + s.jit_ema + p.margin_ms; d.ping = r.ping_avg; d.jitter = r.jitter; d.why = "trip";
        const int want = ticks_for(d.need_ms, p);
        if (want > r.cur) { s.down = 0; if (want < r.cur + 2 && ++s.up < 2) return d; s.up = 0; }
        else if (want < r.cur) { s.up = 0; if (++s.down < 5) return d; s.down = 0; }
        else { s.down = 0; s.up = 0; return d; }
        d.want = want;
        return d;
    }
    // rule 2: the depth trigger first
    if (p.deep_count > 0 && s.deep_n >= p.deep_count) {
        int k = 0; for (int i = 0; i < s.deep_n; i++) if (now - s.deep_t[i] <= (uint64_t)p.deep_window_ms) s.deep_t[k++] = s.deep_t[i];
        s.deep_n = k;
        if (s.deep_n >= p.deep_count) {
            s.deep_n = 0;
            const Reading r = read(); d.cur = r.cur; d.why = "rollbacks"; d.ping_avg = r.ping_avg; d.jitter_avg = r.jitter;
            s.hold_until = now + (uint64_t)p.hold_ms; s.up = s.down = 0;
            if (s.start_delay < 0) s.start_delay = r.cur;
            const bool capped = now < s.start + (uint64_t)p.guard_ms && r.cur >= s.start_delay + p.guard_raise;
            if (r.cur < p.hi && !capped) { d.want = r.cur + 1; s.deep_raises++; s.next = now + (uint64_t)p.every_ms; }
            return d;
        }
    }
    Reading r; bool have = false;
    if (now >= s.next_sample) {
        s.next_sample = now + (uint64_t)p.sample_ms;
        r = read(); have = true;
        if (r.last_ping > 0 && r.rtt_seq != s.rtt_seq) {   // a round trip measured since the last sample: the only kind that enters the window
            s.rtt_seq = r.rtt_seq; s.rtt_taken++;
            s.rtt[s.head] = (float)r.last_ping; s.rtt_t[s.head] = now; s.head = (s.head + 1) % p.samples; if (s.n < p.samples) s.n++;
        } else s.rtt_stale++;
    }
    if (now < s.next) return d;
    s.next = now + (uint64_t)p.every_ms;
    const bool guarded = now < s.start + (uint64_t)p.guard_ms;
    float v[kMaxSamples]; int m = 0;
    for (int i = 0; i < s.n; i++) if (guarded || s.rtt_t[i] >= s.start + (uint64_t)p.early_ms) v[m++] = s.rtt[i];
    if (m < 3) return d;
    if (!have) r = read();
    if (s.start_delay < 0) s.start_delay = r.cur;
    d.cur = r.cur; d.why = "trip"; d.ping_avg = r.ping_avg; d.jitter_avg = r.jitter;
    const double med = percentile(v, m, 50);
    const double hi = !guarded ? percentile(v, m, p.pct) : p.guard_spread ? med + (med - percentile(v, m, 100 - p.pct)) : med;
    const double wait_ms = std::min(med * 0.5, kTickMs);
    const double margin = med >= p.high_ping_ms ? p.high_margin_ms : p.margin_ms;
    d.ping = med; d.jitter = hi - med;
    d.need_ms = (med - wait_ms) / 2.0 + (hi - med) + margin;
    int want = ticks_for(d.need_ms, p);
    if (guarded && want > s.start_delay + p.guard_raise) want = std::max(r.cur, s.start_delay + p.guard_raise);
    if (want > r.cur) { s.down = 0; if (++s.up < p.up_votes) return d; s.up = 0; }
    else if (want < r.cur) {
        s.up = 0;
        if (now < s.hold_until) { s.down = 0; return d; }
        if (++s.down < p.down_votes) return d;
        s.down = 0; want = r.cur - 1;   // one tick at a time
    } else { s.up = s.down = 0; return d; }
    d.want = want;
    return d;
}

}  // namespace jpdr
