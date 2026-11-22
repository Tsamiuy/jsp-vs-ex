// Host test of installer/port/jsp/jsp_sharepad_wd.h (the share pad watchdog's decision rule, rematch black screen). g++ only.
// The member states are built as the game builds them (0x482E64, with its register quirk) and sync as 0x483260 picks it (priority table
// [2, 0, 1, 3, 4] at *(TOC+0x61C4): the last match wins, so 4 > 3 > 1 > 0 > 2).
#include "jsp_sharepad_wd.h"
#include "jsp_net_ko_rule.h"   // the disconnect result rule (fix/rv-net) and the normal-end text that arms the watchdog
#include <string>
#include <cstdio>
#include <cstdlib>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

// 0x482E64 for live members (queue and peer present, in the room): r24 = own count; per member: r24 == 0 -> state 1 (r24 kept), else r24 = that
// member's count and state 3 if it is 0, else 2. So after one member with count 0, every later member reads state 1.
static void game_states(uint32_t own, const uint32_t* count, uint32_t n, uint32_t* st)
{
    uint32_t r24 = own;
    for (uint32_t i = 0; i < n; i++) {
        if (r24 == 0) { st[i] = 1; continue; }
        r24 = count[i];
        st[i] = r24 == 0 ? 3u : 2u;
    }
}
static uint32_t game_sync(const uint32_t* st, uint32_t n)
{
    static const uint32_t prio[5] = { 2, 0, 1, 3, 4 };   // index = priority rank, value = state; the later one wins
    int best = -1; uint32_t s = 2;
    for (uint32_t i = 0; i < n; i++)
        for (int r = 0; r < 5; r++) if (prio[r] == st[i] && r > best) { best = r; s = st[i]; }
    return s;
}
static jspw::View game_view(bool guards, uint32_t own, const uint32_t* count, uint32_t n, bool paused = true, bool hud = false)
{
    jspw::View v; v.guards = guards; v.active = 1; v.members = n; v.own = own; v.paused = paused; v.hud_drawn = hud;
    for (uint32_t i = 0; i < n; i++) { v.count[i] = count[i]; v.queue_ok[i] = true; }
    game_states(own, count, n, v.st);
    v.sync = game_sync(v.st, n);
    return v;
}
// 2 players: one member (the peer)
static jspw::View view1(uint32_t own, uint32_t peer, bool guards = true, bool paused = true, bool hud = false)
{
    const uint32_t c[1] = { peer };
    return game_view(guards, own, c, 1, paused, hud);
}
static jspw::View starved_view() { return view1(4, 0); }

// The stuck copy, frame by frame, with `n` peers that have all left (no more peer samples). Manager update (own push capped at delay + 1, the game's
// states and sync, the stall counter), then the mode: slot 2 (the watchdog, then the stall check: paused while sync != 2), slot 4 (the pad take pops
// one own and one sample per member unless paused). The mode needs `need` more ticks to reach its exit. -1 frames: the game's own error at 779.
struct Sim { int frames = -1; uint32_t fed = 0; uint32_t max_stall = 0; uint32_t first_feed_frame = 0; uint32_t feeds_in_first_frame = 0; };
static Sim simulate(bool watchdog, uint32_t need, uint32_t own0, uint32_t n, const uint32_t* peer0, uint32_t delay)
{
    Sim r; jspw::State s;
    uint32_t own = own0, peer[4] = {}, ticks = 0, stall = 0;
    for (uint32_t i = 0; i < n; i++) peer[i] = peer0[i];
    bool paused = false;
    for (int f = 1; f < 3000; f++) {
        if (own <= delay + 1) own++;
        uint32_t st[4]; game_states(own, peer, n, st);
        const uint32_t sync = game_sync(st, n);
        uint32_t base = own; for (uint32_t i = 0; i < n; i++) if (peer[i] < base) base = peer[i];
        stall = base == 0 ? stall + 1 : 0;
        if (stall > r.max_stall) r.max_stall = stall;
        if (stall >= 779) return r;
        if (watchdog) {
            jspw::View v; v.guards = true; v.active = 1; v.members = n; v.own = own; v.paused = paused; v.hud_drawn = false; v.sync = sync;
            for (uint32_t i = 0; i < n; i++) { v.st[i] = st[i]; v.count[i] = peer[i]; v.queue_ok[i] = true; }
            const uint32_t m = jspw::step(s, v);
            uint32_t k = 0;
            for (uint32_t i = 0; i < n; i++) if (m & (1u << i)) { peer[i]++; r.fed++; k++; }
            if (k && !r.first_feed_frame) { r.first_feed_frame = (uint32_t)f; r.feeds_in_first_frame = k; }
        }
        paused = sync != 2;   // the stall check (0x0F73E8) reads the sync computed by the manager update this frame
        bool all = own > 0; for (uint32_t i = 0; i < n; i++) all = all && peer[i] > 0;
        if (!paused && all) { own--; for (uint32_t i = 0; i < n; i++) peer[i]--; ticks++; }
        if (ticks >= need) { r.frames = f; return r; }
    }
    return r;
}

// A peer that is late but alive: after the match end it stalls now and then (hitch k lasts 20 + 15k frames, one every 150 frames) and sends nothing
// while stalled, then one sample per frame again: every sample it sends still arrives. The manager update reports a real arrival (View::real) on each
// frame a sample came. `report_real` false is the old rule (no real-sample input). Returns the samples fed and the frames of the last feed / the first
// real arrival after a feed.
struct Late { uint32_t fed = 0; int last_feed = -1; int first_real_after_feed = -1; };
static Late simulate_late(bool report_real, int frames)
{
    Late r; jspw::State s;
    uint32_t own = 4, peer = 2;
    bool paused = false;
    for (int f = 1; f <= frames; f++) {
        if (own <= 5) own++;
        const int k = f / 150, in = f % 150;
        const bool hitch = in >= 50 && in < 50 + 20 + 15 * (k % 6);
        const uint32_t real = hitch ? 0u : 1u;
        peer += real;
        uint32_t st[1]; const uint32_t c[1] = { peer }; game_states(own, c, 1, st);
        const uint32_t sync = game_sync(st, 1);
        jspw::View v; v.guards = true; v.active = 1; v.members = 1; v.own = own; v.paused = paused; v.hud_drawn = false; v.sync = sync;
        v.st[0] = st[0]; v.count[0] = peer; v.queue_ok[0] = true; v.real = report_real ? real : 0;
        if (real && r.fed && r.first_real_after_feed < 0) r.first_real_after_feed = f;
        if (jspw::step(s, v) & 1u) { peer++; r.fed++; r.last_feed = f; }
        paused = sync != 2;
        if (!paused && own > 0 && peer > 0) { own--; peer--; }
    }
    return r;
}

int main()
{
    {   // the quirk model itself: after a member with count 0 every later member reads state 1; sync 3 wins over 1
        const uint32_t c[3] = { 0, 2, 0 }; uint32_t st[3];
        game_states(4, c, 3, st);
        CHECK(st[0] == 3 && st[1] == 1 && st[2] == 1);
        CHECK(game_sync(st, 3) == 3);
        const uint32_t d[3] = { 1, 1, 1 }; game_states(4, d, 3, st); CHECK(st[0] == 2 && st[1] == 2 && st[2] == 2 && game_sync(st, 3) == 2);
        game_states(0, d, 3, st); CHECK(st[0] == 1 && game_sync(st, 3) == 1);
        const uint32_t e[2] = { 0, 0 }; game_states(4, e, 2, st); CHECK(st[0] == 3 && st[1] == 1);
    }
    {   // never outside the guards (no normal match end, no 0x876A60 deletion, another mode, armed)
        jspw::State s;
        for (int i = 0; i < 100; i++) CHECK(jspw::step(s, view1(4, 0, false)) == 0);
        CHECK(s.fed[0] == 0);
    }
    {   // share pad inactive (+6D8 = 0, e.g. the debug hold or loading): nothing
        jspw::State s; jspw::View v = starved_view(); v.active = 0;
        for (int i = 0; i < 100; i++) CHECK(jspw::step(s, v) == 0);
    }
    {   // the start needs the mode paused and no fight HUD drawn
        jspw::State s;
        for (int i = 0; i < 100; i++) CHECK(jspw::step(s, view1(4, 0, true, false)) == 0);
        for (int i = 0; i < 100; i++) CHECK(jspw::step(s, view1(4, 0, true, true, true)) == 0);
        for (uint32_t i = 0; i + 1 < jspw::kStartTicks; i++) CHECK(jspw::step(s, starved_view()) == 0);
        CHECK(jspw::step(s, view1(4, 0, true, true, true)) == 0);   // a HUD draw restarts the count
        for (uint32_t i = 0; i + 1 < jspw::kStartTicks; i++) CHECK(jspw::step(s, starved_view()) == 0);
        CHECK(jspw::step(s, starved_view()) == 1u);
    }
    {   // the first feed exactly on the kStartTicks-th consecutive starved tick
        jspw::State s;
        for (uint32_t i = 1; i < jspw::kStartTicks; i++) CHECK(jspw::step(s, starved_view()) == 0);
        CHECK(jspw::step(s, starved_view()) == 1u);
        CHECK(s.fed[0] == 1 && s.latched[0]);
    }
    {   // transients (1..9 ticks) never feed
        jspw::State s;
        for (int round = 0; round < 50; round++) {
            for (uint32_t i = 0; i + 1 < jspw::kStartTicks; i++) CHECK(jspw::step(s, starved_view()) == 0);
            CHECK(jspw::step(s, view1(4, 1)) == 0);
        }
        CHECK(s.fed[0] == 0);
    }
    {   // not starved: a sample waiting, or the own queue empty (state 1 with own 0), or a bad queue pointer
        jspw::State s;
        for (int i = 0; i < 50; i++) CHECK(jspw::step(s, view1(4, 1)) == 0);
        for (int i = 0; i < 50; i++) CHECK(jspw::step(s, view1(0, 0)) == 0);
        jspw::View v = starved_view(); v.queue_ok[0] = false;
        for (int i = 0; i < 50; i++) CHECK(jspw::step(s, v) == 0);
    }
    {   // latched: one per tick only while starved again, no new 10-tick wait, and the start-only gates (paused, HUD, sync) no longer apply
        jspw::State s;
        for (uint32_t i = 0; i < jspw::kStartTicks; i++) jspw::step(s, starved_view());
        CHECK(s.fed[0] == 1);
        CHECK(jspw::step(s, view1(4, 1, true, true)) == 0);    // the fed sample waits: nothing
        CHECK(jspw::step(s, view1(4, 0, true, false)) == 1u);  // resumed last tick (not paused), starved again: feed at once
        CHECK(s.fed[0] == 2);
        CHECK(jspw::step(s, view1(4, 5)) == 0);                // the peer's real samples came back: nothing
    }
    {   // a transient guard failure while latched only skips that tick: the latch stays (no new 10-tick wait, no 26 s stall)
        jspw::State s;
        for (uint32_t i = 0; i < jspw::kStartTicks; i++) jspw::step(s, starved_view());
        CHECK(jspw::step(s, view1(4, 0, false)) == 0);
        CHECK(s.latched[0]);
        jspw::View v = starved_view(); v.active = 0;
        CHECK(jspw::step(s, v) == 0);
        CHECK(s.latched[0]);
        CHECK(jspw::step(s, starved_view()) == 1u);
    }
    {   // the member leaves (state 4) while latched: stop, and no new start on state 4
        jspw::State s;
        for (uint32_t i = 0; i < jspw::kStartTicks; i++) jspw::step(s, starved_view());
        jspw::View g = starved_view(); g.st[0] = 4; g.sync = 4;
        CHECK(jspw::step(s, g) == 0);
        CHECK(!s.latched[0]);
        for (int i = 0; i < 50; i++) CHECK(jspw::step(s, g) == 0);
    }
    {   // capped at kMaxFeed per member per episode; a reset (the next episode) re-arms it
        jspw::State s; uint32_t fed = 0;
        for (int i = 0; i < 5000; i++) fed += jspw::step(s, starved_view()) ? 1u : 0u;
        CHECK(fed == jspw::kMaxFeed && s.fed[0] == jspw::kMaxFeed);
        jspw::reset(s);
        for (uint32_t i = 0; i < jspw::kStartTicks; i++) jspw::step(s, starved_view());
        CHECK(s.fed[0] == 1);
    }
    {   // a push that did not land or was refused (no free node): that member is left alone for the episode
        jspw::State s;
        for (uint32_t i = 0; i < jspw::kStartTicks; i++) jspw::step(s, starved_view());
        jspw::fed_failed(s, 0);
        for (int i = 0; i < 100; i++) CHECK(jspw::step(s, starved_view()) == 0);
    }
    {   // 2v2, real game states: three peers all empty (st 3, 1, 1 by the quirk): all three start and are fed in the SAME tick
        jspw::State s; const uint32_t c[3] = { 0, 0, 0 };
        uint32_t m = 0;
        for (uint32_t i = 0; i < jspw::kStartTicks; i++) m = jspw::step(s, game_view(true, 4, c, 3));
        CHECK(m == 7u);
        // only m1 empty: it reads st 3 (m0 has a sample); m2 has samples but reads 1 after it: not fed
        jspw::State t; const uint32_t d[3] = { 2, 0, 3 };
        for (uint32_t i = 0; i < jspw::kStartTicks; i++) m = jspw::step(t, game_view(true, 4, d, 3));
        CHECK(m == 2u);
        // members beyond +0x10 are ignored
        jspw::State u; jspw::View v = game_view(true, 4, c, 3); v.members = 2;
        for (uint32_t i = 0; i < jspw::kStartTicks; i++) m = jspw::step(u, v);
        CHECK(m == 3u);
    }
    {   // late but alive: a real sample after a feed stops that member for the episode, whatever comes later
        jspw::State s;
        for (uint32_t i = 0; i < jspw::kStartTicks; i++) jspw::step(s, starved_view());
        CHECK(s.fed[0] == 1 && s.latched[0]);
        jspw::View r = view1(4, 2); r.real = 1u;              // the fed sample plus the peer's real one
        CHECK(jspw::step(s, r) == 0);
        CHECK(s.stopped[0] && !s.latched[0] && s.surplus[0] == 1);
        for (int i = 0; i < 500; i++) CHECK(jspw::step(s, starved_view()) == 0);   // starved again later: never fed again
        CHECK(s.fed[0] == 1);
        jspw::reset(s);                                         // the next episode starts clean
        CHECK(!s.stopped[0] && s.surplus[0] == 0);
        for (uint32_t i = 0; i < jspw::kStartTicks; i++) jspw::step(s, starved_view());
        CHECK(s.fed[0] == 1);
    }
    {   // a real sample before any feed only restarts the 10-tick count (it is not a stop)
        jspw::State s;
        for (uint32_t i = 0; i + 1 < jspw::kStartTicks; i++) CHECK(jspw::step(s, starved_view()) == 0);
        jspw::View r = starved_view(); r.real = 1u;             // arrived and was popped before this tick: still reads count 0
        CHECK(jspw::step(s, r) == 0);
        CHECK(!s.stopped[0]);
        for (uint32_t i = 0; i + 1 < jspw::kStartTicks; i++) CHECK(jspw::step(s, starved_view()) == 0);
        CHECK(jspw::step(s, starved_view()) == 1u);
    }
    {   // 2v2: only the member whose real sample came is stopped; the gone ones are still fed in the same tick
        jspw::State s; const uint32_t c[3] = { 0, 0, 0 };
        for (uint32_t i = 0; i < jspw::kStartTicks; i++) jspw::step(s, game_view(true, 4, c, 3));
        CHECK(s.fed[0] == 1 && s.fed[1] == 1 && s.fed[2] == 1);
        jspw::View v = game_view(true, 4, c, 3); v.real = 2u;
        CHECK(jspw::step(s, v) == 5u);
        CHECK(s.stopped[1] && !s.stopped[0] && !s.stopped[2]);
        CHECK(jspw::step(s, game_view(true, 4, c, 3)) == 5u);
    }
    {   // frame model: a peer that stalls now and then, longer each time (late but alive). The old rule fed again in every hitch longer than the extra
        // samples it had already queued; now the feeding ends at the first real arrival after a feed, so the extra samples stay at the first hitch's.
        const Late now = simulate_late(true, 900), old = simulate_late(false, 900);
        CHECK(now.fed >= 1 && now.fed <= 20 - jspw::kStartTicks + 1);
        CHECK(now.first_real_after_feed > 0 && now.last_feed < now.first_real_after_feed);
        CHECK(old.fed > now.fed && old.last_feed > now.first_real_after_feed);
        std::printf("late but alive (hitches of 20, 35, 50, ... frames): %u neutral sample(s), none after the first real one at frame %d; the old rule fed %u, "
                    "the last at frame %d\n", now.fed, now.first_real_after_feed, old.fed, old.last_feed);
    }
    {   // the stuck copy of the two-copy rematch test (2 players): peer gone, own delay 4, 0 peer samples
        const uint32_t p0[1] = { 0 };
        for (uint32_t need = 1; need <= 5; need++) {
            const Sim a = simulate(false, need, 4, 1, p0, 4);
            CHECK(a.frames < 0 && a.max_stall >= 779);
            const Sim b = simulate(true, need, 4, 1, p0, 4);
            CHECK(b.frames > 0 && b.frames <= (int)(jspw::kStartTicks + 2 * need + 3));
            CHECK(b.fed == need);
            CHECK(b.max_stall < 779 && b.max_stall <= jspw::kStartTicks + 2);
            std::printf("1v1 stuck copy, %u tick(s) to its exit: no watchdog -> game error after %u stalled ticks; watchdog -> exit at frame %d, %u fed, max stall %u\n",
                        need, a.max_stall, b.frames, b.fed, b.max_stall);
        }
        const uint32_t p1[1] = { 1 };
        const Sim c = simulate(true, 1, 1, 1, p1, 1);   // a spare peer sample: nothing fed
        CHECK(c.frames > 0 && c.fed == 0);
    }
    {   // 2v2: three peers gone together, this copy with no spares: all fed in the same frame, exit as fast as 1v1
        const uint32_t p0[3] = { 0, 0, 0 };
        for (uint32_t need = 1; need <= 3; need++) {
            const Sim a = simulate(false, need, 4, 3, p0, 4);
            CHECK(a.frames < 0);
            const Sim b = simulate(true, need, 4, 3, p0, 4);
            CHECK(b.frames > 0 && b.frames <= (int)(jspw::kStartTicks + 2 * need + 3));
            CHECK(b.feeds_in_first_frame == 3);
            CHECK(b.fed == 3 * need);
            std::printf("2v2 stuck copy, %u tick(s) to its exit: watchdog -> exit at frame %d, %u fed (%u in the first feeding frame)\n", need, b.frames, b.fed,
                        b.feeds_in_first_frame);
        }
        const uint32_t p2[3] = { 2, 0, 1 };   // one peer gone, the others with spares: it exits
        const Sim d = simulate(true, 1, 4, 3, p2, 4);
        CHECK(d.frames > 0);
    }
    {   // The watchdog's arming after a disconnect: end_with arms it only on a normal end (jkr::normal_end), and the Disconnected path ends with
        // jkr::disconnect_end_text(jkr::disconnect_result(...)). base 1000; the session's save frame of tick t is t - base - 1.
        const uint32_t base = 1000;
        jkr::Ring ring; ring.clear();
        jkr::Watermark wm; wm.reset();
        for (uint32_t t = base + 1; t <= base + 200; t++) ring.record(t, 0);
        wm.live(150);                              // confirmed by real inputs of both PCs up to frame 150
        ring.record(base + 206, 1);                // a KO predicted at tick 206 (save frame 205), above the live watermark
        wm.lost();                                 // the Disconnected event: the watermark freezes
        wm.live(400);                              // the zero-filled final frame after it must not count
        uint32_t at = 1;
        const int kept = jkr::disconnect_result(ring, base, wm, &at);
        CHECK(kept == 0 && at == 0);
        CHECK(jkr::disconnect_end_text(kept) == "Opponent disconnected");
        CHECK(!jkr::normal_end(jkr::disconnect_end_text(kept)));   // a predicted KO and a disconnect: the watchdog stays off
        jkr::Ring ring2; ring2.clear();
        jkr::Watermark wm2; wm2.reset();
        ring2.record(base + 150, 2);               // a KO at tick 150 (save frame 149)
        wm2.live(151);                             // confirmed before the disconnect
        wm2.lost();
        const int kept2 = jkr::disconnect_result(ring2, base, wm2, &at);
        CHECK(kept2 == 2 && at == base + 150);
        CHECK(jkr::disconnect_end_text(kept2) == "Match over: blue wins");
        CHECK(jkr::normal_end(jkr::disconnect_end_text(kept2)));   // a confirmed KO kept across a disconnect: the watchdog may act
        CHECK(jkr::normal_end(jkr::match_over_text(1)) && jkr::normal_end(jkr::match_over_text(3)));   // the boundary's own KO and time-up ends
        for (const char* t : { "Match ended", "Match ended: the game stopped taking vblanks", "Desync detected", "Opponent disconnected",
                               "Opponent did not reach the fight: match ended" })
            CHECK(!jkr::normal_end(t));
        std::printf("disconnect: a predicted KO ends as \"%s\" (watchdog off), a confirmed one as \"%s\" (watchdog on)\n",
                    jkr::disconnect_end_text(kept).c_str(), jkr::disconnect_end_text(kept2).c_str());
    }
    {   // JSP_SHAREPAD_WATCHDOG off values: 0/off/false/no in any case; anything else (unset, empty, 1, on, near misses) leaves it on
        const char* off[] = { "0", "off", "OFF", "Off", "false", "FALSE", "False", "no", "NO", " no ", "off\r\n", "\tfalse" };
        const char* on[] = { nullptr, "", "1", "on", "yes", "true", "00", "offf", "nope", "0x0", "of", "n", " " };
        for (const char* v : off) CHECK(jspw::setting_off(v));
        for (const char* v : on) CHECK(!jspw::setting_off(v));
        std::printf("JSP_SHAREPAD_WATCHDOG values: %zu off, %zu on checked\n", sizeof off / sizeof *off, sizeof on / sizeof *on);
    }
    if (g_fail) { std::printf("sharepad_wd_test: %d FAILED\n", g_fail); return 1; }
    std::printf("sharepad_wd_test: all passed\n");
    return 0;
}
