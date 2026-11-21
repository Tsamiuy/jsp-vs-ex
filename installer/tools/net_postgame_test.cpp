// Host test of the post-game agreement (installer/port/jsp/jsp_net_postgame.h): wire format, the rules, timeouts, the native-menu gate,
// and that both sides reach the same outcome whatever the order, loss, duplication or staleness of the messages. No game, no GPU.
#include "../port/jsp/jsp_net_postgame.h"
#include <algorithm>
#include <cstdio>
#include <random>
#include <vector>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
using namespace jspg;

static Msg msg(uint16_t seq, uint16_t rev, Choice p, bool saw) { Msg m; m.seq = seq; m.rev = rev; m.pick = p; m.saw = saw; return m; }

// ---- exhaustive interleavings: each side has a script of picks; every order of picks and deliveries is tried ----
struct Net {
    Agreement a, b;
    std::vector<Msg> to_a, to_b;   // in flight
    size_t ia = 0, ib = 0;         // next scripted pick
    uint16_t ra = 1, rb = 1;       // rev last sent by a, b (the first, empty state is not needed)
};
static const std::vector<Choice>* g_sa; static const std::vector<Choice>* g_sb;
static long g_leaves = 0, g_checked = 0, g_mismatch = 0;

// A message goes out when the sender's state changed (its rev moved past the last one sent); the resends are the final flush in settle().
static void emit(Agreement& from, uint16_t& last, std::vector<Msg>& to)
{
    const Msg m = from.current();
    if (m.rev <= last) return;
    last = m.rev;
    to.push_back(m);
}

// Resend until both decided (the final flush: models the periodic resend over a link that delivers eventually).
static void settle(Net n)
{
    for (int i = 0; i < 20 && (n.a.outcome() == PENDING || n.b.outcome() == PENDING); i++) {
        n.b.receive(n.a.current(), 0);
        n.a.receive(n.b.current(), 0);
    }
    g_checked++;
    if (n.a.outcome() == PENDING || n.b.outcome() == PENDING || n.a.outcome() != n.b.outcome()) {
        if (g_mismatch++ < 5) std::printf("FAIL interleaving: outcomes %d / %d (picks a %d b %d)\n", n.a.outcome(), n.b.outcome(), n.a.mine(), n.b.mine());
        fails++;
    }
    if (n.a.outcome() == O_LEAVE) g_leaves++;
}

static void explore(Net n, int depth)
{
    bool any = false;
    if (n.ia < g_sa->size()) { Net m = n; any = true; m.a.pick((*g_sa)[m.ia++], 0); emit(m.a, m.ra, m.to_b); explore(m, depth + 1); }
    if (n.ib < g_sb->size()) { Net m = n; any = true; m.b.pick((*g_sb)[m.ib++], 0); emit(m.b, m.rb, m.to_a); explore(m, depth + 1); }
    for (size_t i = 0; i < n.to_a.size(); i++) {   // deliver any in-flight message (out of order), or lose it
        { Net m = n; any = true; const Msg x = m.to_a[i]; m.to_a.erase(m.to_a.begin() + (long)i); m.a.receive(x, 0); emit(m.a, m.ra, m.to_b); explore(m, depth + 1); }
        if (depth < 3) { Net m = n; m.to_a.erase(m.to_a.begin() + (long)i); explore(m, depth + 1); }
    }
    for (size_t i = 0; i < n.to_b.size(); i++) {
        { Net m = n; any = true; const Msg x = m.to_b[i]; m.to_b.erase(m.to_b.begin() + (long)i); m.b.receive(x, 0); emit(m.b, m.rb, m.to_a); explore(m, depth + 1); }
        if (depth < 3) { Net m = n; m.to_b.erase(m.to_b.begin() + (long)i); explore(m, depth + 1); }
    }
    if (!any) settle(n);
}

// ---- random traffic: delays, reordering, duplicates, loss, stale messages of the previous match, picks at random times ----
static bool random_run(uint32_t seed)
{
    std::mt19937 r(seed);
    Config c; c.idle_ms = 180000; c.silence_ms = 8000;
    Agreement a, b;
    const uint16_t seq = (uint16_t)(5 + r() % 3);
    struct Fl { uint64_t at; Msg m; bool to_a; };
    std::vector<Fl> fl;
    // a peer that reached its post-game first: b starts later, a's early messages wait aside in b
    const uint64_t b_start = r() % 3000;
    a.start(seq, 0, c);
    std::vector<std::pair<uint64_t, Choice>> pa, pb;
    auto rc = [&]() { const uint32_t v = r() % 6; return v < 3 ? REMATCH : v < 5 ? CHARSELECT : LEAVE; };   // Leave 1 in 6
    for (int i = 0, k = (int)(r() % 3) + 1; i < k; i++) pa.push_back({ (uint64_t)(r() % 6000), rc() });
    for (int i = 0, k = (int)(r() % 3) + 1; i < k; i++) pb.push_back({ b_start + r() % 6000, rc() });
    for (uint64_t t = 0; t < 40000; t += 16) {
        if (!b.started() && t >= b_start) b.start(seq, t, c);
        for (auto& p : pa) if (p.first >= t && p.first < t + 16) a.pick(p.second, t);
        for (auto& p : pb) if (p.first >= t && p.first < t + 16 && b.started()) b.pick(p.second, t);
        a.tick(t); if (b.started()) b.tick(t);
        Msg m;
        auto send = [&](Agreement& s, bool to_a) {
            if (!s.take(t, &m)) return;
            if (r() % 5 == 0) return;                                            // lost
            const int copies = r() % 4 == 0 ? 2 : 1;                             // duplicated
            for (int k = 0; k < copies; k++) fl.push_back({ t + 10 + r() % 400, m, to_a });   // delayed, so reordered
            if (r() % 10 == 0) { Msg old = m; old.seq = (uint16_t)(seq - 1); old.pick = (Choice)(1 + r() % 3); old.saw = true; old.rev = (uint16_t)(m.rev + 50); fl.push_back({ t + r() % 300, old, to_a }); }   // stale match
        };
        send(a, false); if (b.started()) send(b, true);
        std::stable_sort(fl.begin(), fl.end(), [](const Fl& x, const Fl& y) { return x.at < y.at; });
        while (!fl.empty() && fl.front().at <= t) {
            const Fl f = fl.front(); fl.erase(fl.begin());
            uint8_t w[kMsgLen]; encode(f.m, w);
            if (f.to_a) a.receive_bytes(w, kMsgLen, t); else b.receive_bytes(w, kMsgLen, t);
        }
        if (a.outcome() != PENDING && b.started() && b.outcome() != PENDING && t > 2000 + b_start) break;
    }
    if (a.outcome() == PENDING || b.outcome() == PENDING) { std::printf("FAIL random seed %u: undecided %d / %d\n", seed, a.outcome(), b.outcome()); return false; }
    if (a.outcome() != b.outcome()) { std::printf("FAIL random seed %u: outcomes %d / %d (reasons %d / %d)\n", seed, a.outcome(), b.outcome(), a.reason(), b.reason()); return false; }
    if (a.outcome() != O_LEAVE) {   // agreement: the outcome is combine() of the final picks
        if (combine(a.mine(), a.peer()) != a.outcome() || b.mine() != a.peer() || a.mine() != b.peer()) { std::printf("FAIL random seed %u: picks\n", seed); return false; }
    }
    return true;
}

int main()
{
    // ---- wire format ----
    {
        uint8_t w[kMsgLen]; Msg m = msg(0x1234, 0xBEEF, CHARSELECT, true), d;
        encode(m, w);
        CHECK(w[0] == kMagic && w[1] == kVersion && w[2] == 0x34 && w[3] == 0x12 && w[4] == 0xEF && w[5] == 0xBE && w[6] == (2 | 4));
        CHECK(decode(w, kMsgLen, &d) && d.seq == 0x1234 && d.rev == 0xBEEF && d.pick == CHARSELECT && d.saw);
        CHECK(!decode(w, kMsgLen - 1, &d) && !decode(nullptr, kMsgLen, &d));
        for (size_t i = 0; i < kMsgLen; i++) { uint8_t x[kMsgLen]; std::copy(w, w + kMsgLen, x); x[i] ^= 0x01; CHECK(!decode(x, kMsgLen, &d)); }   // any flipped bit: refused
        { uint8_t x[kMsgLen]; std::copy(w, w + kMsgLen, x); x[1] = 2; x[7] ^= 1 ^ 2; CHECK(!decode(x, kMsgLen, &d)); }                      // other version
        { uint8_t x[kMsgLen]; Msg z = msg(1, 1, NONE, false); encode(z, x); x[6] |= 0x08; x[7] ^= 0x08; CHECK(!decode(x, kMsgLen, &d)); }  // reserved bit
    }
    // ---- idle setting ----
    CHECK(kIdleMs == 180000 && idle_ms_from(nullptr) == 180000 && idle_ms_from("") == 180000 && idle_ms_from("x") == 180000);
    CHECK(idle_ms_from("300") == 300000 && idle_ms_from("9") == 180000 && idle_ms_from("3601") == 180000 && idle_ms_from("10") == 10000);
    CHECK(combine(REMATCH, REMATCH) == O_REMATCH && combine(REMATCH, CHARSELECT) == O_CHARSELECT && combine(CHARSELECT, LEAVE) == O_LEAVE);

    // ---- the rules, one side ----
    {
        Agreement a; a.start(1, 0);
        Msg m; CHECK(a.take(0, &m) && m.seq == 1 && m.pick == NONE && !m.saw);                 // first state goes out at once
        CHECK(!a.take(100, &m) && a.take(250, &m));                                            // then every resend_ms
        CHECK(a.pick(REMATCH, 300) && a.pick(CHARSELECT, 400) && a.mine() == CHARSELECT);       // changeable before the peer picks
        CHECK(a.view(0, 400).status == "Waiting for opponent...");
        CHECK(a.receive(msg(1, 2, REMATCH, false), 500) && a.locked() && a.outcome() == PENDING);
        CHECK(a.view(0, 500).peer_line == "Opponent wants a rematch" && a.view(0, 500).sel == 1);
        CHECK(!a.pick(REMATCH, 600) && a.mine() == CHARSELECT);                                // locked
        CHECK(a.take(600, &m) && m.saw && m.pick == CHARSELECT);                               // the lock goes out at once
        CHECK(a.receive(msg(1, 2, REMATCH, true), 700) && a.outcome() == PENDING);             // duplicate rev: nothing
        CHECK(a.receive(msg(1, 1, CHARSELECT, false), 700) && a.peer() == REMATCH);             // older rev: nothing
        CHECK(!a.receive(msg(7, 9, LEAVE, true), 700) && a.outcome() == PENDING);              // another match: kept aside, not taken
        CHECK(a.receive(msg(1, 3, REMATCH, true), 800) && a.outcome() == O_CHARSELECT);
        CHECK(a.view(0, 800).status == "Back to character select");
        CHECK(a.take(900, &m) && m.saw && !a.take(3000, &m));                                  // lingers, then quiet
    }
    {   // both Rematch
        Agreement a, b; a.start(2, 0); b.start(2, 0);
        a.pick(REMATCH, 10); b.pick(REMATCH, 10);
        for (int i = 0; i < 3; i++) { b.receive(a.current(), 20); a.receive(b.current(), 20); }
        CHECK(a.outcome() == O_REMATCH && b.outcome() == O_REMATCH);
    }
    {   // Leave by either: at once, the other sees "Opponent left"
        Agreement a, b; a.start(3, 0); b.start(3, 0);
        b.pick(REMATCH, 5);
        CHECK(a.pick(LEAVE, 10) && a.outcome() == O_LEAVE && a.reason() == R_SELF);
        b.receive(a.current(), 20);
        CHECK(b.outcome() == O_LEAVE && b.reason() == R_PEER_LEFT && b.view(0, 20).status == "Opponent left");
    }
    {   // peer silent -> link lost (short); an unlocked pick becomes LEAVE so a late peer leaves too
        Config c; c.silence_ms = 8000;
        Agreement a; a.start(4, 0, c); a.pick(REMATCH, 100);
        a.tick(7999); CHECK(a.outcome() == PENDING);
        a.link_alive(7000); a.tick(14999); CHECK(a.outcome() == PENDING);                     // other traffic keeps it alive
        a.tick(15000); CHECK(a.outcome() == O_LEAVE && a.reason() == R_LINK_LOST && a.mine() == LEAVE);
    }
    {   // idle: 180 s by default; countdown only in the last 30 s; the peer is told (Leave)
        Agreement a, b; a.start(5, 0); b.start(5, 0);
        uint64_t t = 0;
        for (; t < 180000; t += 1000) { a.link_alive(t); b.link_alive(t); a.tick(t); b.tick(t); if (t == 149000) CHECK(a.view(0, t).countdown.empty()); }
        CHECK(a.view(0, 150000).countdown == "Returning to lobby in 30 s" && a.view(0, 150000).seconds_left == 30);
        CHECK(a.view(0, 179001).countdown == "Returning to lobby in 1 s");
        a.tick(180000);
        CHECK(a.outcome() == O_LEAVE && a.reason() == R_IDLE && a.mine() == LEAVE);
        b.receive(a.current(), 180001);
        CHECK(b.outcome() == O_LEAVE && b.reason() == R_PEER_LEFT);
        Agreement s; Config c; c.idle_ms = idle_ms_from("60"); s.start(9, 0, c); s.link_alive(59999); s.tick(59999); CHECK(s.outcome() == PENDING);
        s.tick(60000); CHECK(s.outcome() == O_LEAVE && s.reason() == R_IDLE);
    }
    {   // locked, no countdown; a locked side that never gets the peer's lock leaves after idle + silence
        Agreement a; a.start(6, 0); a.pick(REMATCH, 1); a.receive(msg(6, 2, REMATCH, false), 2);
        for (uint64_t t = 0; t < 188000; t += 1000) { a.link_alive(t); a.tick(t); }
        CHECK(a.outcome() == PENDING && a.view(0, 170000).countdown.empty());
        a.link_alive(188000); a.tick(188000); CHECK(a.outcome() == O_LEAVE && a.reason() == R_LINK_LOST && a.mine() == REMATCH);
    }
    {   // early messages of this match (peer reached its post-game first) are taken at start; stale ones of the last match are dropped
        Agreement a; a.start(10, 0); a.pick(REMATCH, 1); a.receive(msg(10, 2, REMATCH, true), 2);
        CHECK(a.outcome() == O_REMATCH);
        a.receive(msg(11, 2, CHARSELECT, false), 5);       // next match, early
        a.receive(msg(10, 9, LEAVE, true), 5);              // last match, late
        a.start(11, 10);
        CHECK(a.peer() == CHARSELECT && a.outcome() == PENDING);
        a.pick(REMATCH, 11);
        CHECK(a.locked() && a.outcome() == PENDING);
    }
    // ---- native menu gate ----
    {
        CHECK(choice_of_row(ROW_FIGHT_AGAIN) == REMATCH && choice_of_row(ROW_RESELECT) == CHARSELECT && choice_of_row(ROW_MAIN_MENU) == LEAVE && choice_of_row(3) == NONE);
        NativeGate g, h; g.start(20, 0); h.start(20, 0);
        CHECK(!NativeGate().on_confirm(0, 0) && !NativeGate().on_cancel());   // not running: the game's own menu as is
        CHECK(g.on_cancel());
        CHECK(g.on_confirm(ROW_FIGHT_AGAIN, 10) && g.run_row(10) == -1);       // held, waiting
        CHECK(g.on_confirm(ROW_RESELECT, 20) && g.ag.mine() == CHARSELECT);    // changed before the peer picked
        CHECK(h.on_confirm(ROW_FIGHT_AGAIN, 20));
        for (int i = 0; i < 3; i++) { h.ag.receive(g.ag.current(), 30); g.ag.receive(h.ag.current(), 30); }
        CHECK(g.run_row(40) == ROW_RESELECT && h.run_row(40) == ROW_RESELECT); // both run Re-select
        CHECK(h.on_confirm(ROW_FIGHT_AGAIN, 41));                               // the wrong row stays held
        CHECK(!h.on_confirm(ROW_RESELECT, 42) && h.run_row(42) == -1);          // the outcome row passes once
        g.done(); CHECK(g.run_row(43) == -1);
        NativeGate l; l.start(21, 0);
        CHECK(!l.on_confirm(ROW_MAIN_MENU, 5) && l.ag.outcome() == O_LEAVE);   // Return to main menu: no wait
    }

    // ---- every interleaving of small scripts (picks, out-of-order delivery, loss) ----
    {
        const Choice all[3] = { REMATCH, CHARSELECT, LEAVE };
        std::vector<std::vector<Choice>> scripts;
        for (Choice x : all) scripts.push_back({ x });
        for (Choice x : all) for (Choice y : all) if (x != y) scripts.push_back({ x, y });
        for (auto& sa : scripts) for (auto& sb : scripts) {
            g_sa = &sa; g_sb = &sb;
            Net n; n.a.start(1, 0); n.b.start(1, 0);
            explore(n, 0);
        }
        std::printf("net postgame: %ld interleavings checked (%ld ended in Leave), %ld mismatches\n", g_checked, g_leaves, g_mismatch);
    }
    // ---- random traffic ----
    {
        int bad = 0;
        for (uint32_t s = 1; s <= 3000; s++) if (!random_run(s)) bad++;
        if (bad) fails++;
        std::printf("net postgame: 3000 random runs (loss, delay, duplicates, stale match), %d disagreements\n", bad);
    }

    if (fails) { std::printf("net postgame test: %d FAILED\n", fails); return 1; }
    std::printf("net postgame test: PASS\n");
    return 0;
}
