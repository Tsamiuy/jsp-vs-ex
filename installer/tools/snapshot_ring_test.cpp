// Host test of installer/port/jsp/jsp_snapshot_ring.h, the dirty-page undo ring of the incremental rollback snapshot.
// Part 1 (correctness): a small memory with gaps, random page writes per tick (a hot set plus random pages, partial-page writes),
// a simulated write watch (the set of pages written since the last commit), a commit per tick, rollbacks of random depth with and
// without writes after the last commit, overflow and eviction. Every rollback is checked byte for byte against a full copy of the
// target tick (reference), the blob against the reference blob, the shadow against memory, and memory outside the set is untouched.
// Part 2 (timing): a 35,278-page set (the snap-3 size, 144,498,688 bytes), commit per tick with 27, 500 and 1634 written pages,
// rollback 8 ticks back, also checked against a full copy. Run: installer/tools/snapshot_ring_test.sh (builds with -O2).
// every part runs in both ring modes (Copy2 = two copies per committed page, PagePtr = one copy and a page-index swap); the random test
// also checks the arena accounting (free + entry pages + shadow pages == arena, no page owned twice) after every operation.
#include "../port/jsp/jsp_snapshot_ring.h"
#include "../port/jsp/jsp_snapshot_sync.h"   // the gate precheck (test_gate_precheck)
#include <chrono>
#include <cstdio>
#include <deque>
#include <map>
using namespace jss;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

struct Rng { uint64_t s; uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; } uint32_t below(uint32_t n) { return (uint32_t)(next() % n); } };

// Copy of the saved set (flat) + blob: the reference state of one tick.
struct Ref { uint32_t tick; std::vector<uint8_t> b; uint64_t blob; };
static Ref take(const PageMap& m, const uint8_t* mem, uint32_t t, uint64_t blob)
{
    Ref r{t, std::vector<uint8_t>((size_t)m.pages() * kPage), blob};
    for (uint32_t s = 0; s < m.pages(); s++) std::memcpy(r.b.data() + (size_t)s * kPage, mem + m.addr_of(s), kPage);
    return r;
}
static bool same(const PageMap& m, const uint8_t* mem, const Ref& r)
{
    for (uint32_t s = 0; s < m.pages(); s++) if (std::memcmp(r.b.data() + (size_t)s * kPage, mem + m.addr_of(s), kPage)) return false;
    return true;
}

// Writes into n pages of the set (some from a hot set), records the written slots (the "write watch"); occasionally writes outside.
static void tick_writes(Rng& g, const PageMap& m, uint8_t* mem, size_t mem_n, uint32_t n, std::vector<uint32_t>* ww, std::vector<uint8_t>* outside_copy)
{
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t s = (g.next() & 1) ? g.below(std::min<uint32_t>(16, m.pages())) : g.below(m.pages());
        const uint32_t a = m.addr_of(s) + g.below(kPage - 8);
        const uint32_t len = 1 + g.below(8);
        for (uint32_t j = 0; j < len; j++) mem[a + j] = (uint8_t)g.next();
        ww->push_back(s);
        if (g.below(4) == 0) ww->push_back(s);   // the watch may list a page twice across batches: must be harmless
    }
    if (outside_copy && g.below(3) == 0) {     // a write outside the saved set (an excluded range): rollback must leave it alone
        for (;;) {
            const uint32_t a = g.below((uint32_t)mem_n);
            if (m.slot_of(a) == kNoSlot) { mem[a] ^= 0x5A; (*outside_copy)[a] = mem[a]; break; }
        }
    }
}

static void test_pagemap()
{
    PageMap m; m.reset({{0x3000, 0x2000}, {0x1000, 0x1000}, {0x8000, 0x1000}});
    CHECK(m.pages() == 4);
    CHECK(m.addr_of(0) == 0x1000 && m.addr_of(1) == 0x3000 && m.addr_of(2) == 0x4000 && m.addr_of(3) == 0x8000);
    CHECK(m.slot_of(0x1000) == 0 && m.slot_of(0x1FFF) == 0 && m.slot_of(0x2000) == kNoSlot && m.slot_of(0x0) == kNoSlot);
    CHECK(m.slot_of(0x4ABC) == 2 && m.slot_of(0x8FFF) == 3 && m.slot_of(0x9000) == kNoSlot && m.slot_of(0xFFFFFFFFu) == kNoSlot);
}

static const char* mname(RingMode m) { return m == RingMode::PagePtr ? "ptr" : "copy2"; }

// ENHANCEMENT (parallel page copy): a copier that checks the batch contract (no op writes a page another op reads or writes; an op's second copy may read its
// own first destination) and then runs the ops in REVERSE order, so a ring whose result depended on the serial order would fail the byte checks below.
static uint64_t g_batches = 0, g_batch_ops = 0, g_batch_bad = 0;
static void reverse_copier(void*, const PageCopy* ops, size_t n)
{
    std::map<const uint8_t*, size_t> dst;
    for (size_t i = 0; i < n; i++) {
        if (!dst.emplace(ops[i].d1, i).second) g_batch_bad++;
        if (ops[i].d2 && (ops[i].d2 == ops[i].d1 || !dst.emplace(ops[i].d2, i).second)) g_batch_bad++;
    }
    for (size_t i = 0; i < n; i++) {
        auto a = dst.find(ops[i].s1); if (a != dst.end() && a->second != i) g_batch_bad++;
        if (ops[i].d2) { auto b = dst.find(ops[i].s2); if (b != dst.end() && b->second != i) g_batch_bad++; }
    }
    for (size_t i = n; i-- > 0;) { std::memcpy(ops[i].d1, ops[i].s1, kPage); if (ops[i].d2) std::memcpy(ops[i].d2, ops[i].s2, kPage); }
    g_batches++; g_batch_ops += n;
}

static void test_random(uint64_t seed, uint32_t depth, size_t pool, uint32_t max_n, int ticks, RingMode mode, bool batched = false)
{
    const size_t mem_n = 0x200000;   // 2 MB "guest" space, saved set with gaps
    std::vector<uint8_t> mem(mem_n), outside(mem_n);
    Rng g{seed};
    for (auto& b : mem) b = (uint8_t)g.next();
    outside = mem;
    const std::vector<Range> set = {{0x10000, 64 * kPage}, {0x80000, 200 * kPage}, {0x190000, 50 * kPage}};
    DeltaRing R; R.reset(set, depth, pool, sizeof(uint64_t), mode);
    if (batched) R.set_copier(reverse_copier, nullptr);
    const PageMap& m = R.map();
    std::deque<Ref> refs;
    int audits = 0;
    uint32_t t = 100; uint64_t blob = 0xB0B0000000000000ull | t;
    R.baseline(t, mem.data(), &blob);
    CHECK(R.audit()); audits++;
    refs.push_back(take(m, mem.data(), t, blob));
    int rollbacks = 0, deep = 0, overflow_seen = 0, refused_ok = 0;
    for (int i = 0; i < ticks; i++) {
        std::vector<uint32_t> ww;
        const uint32_t n = g.below(8) == 0 ? max_n : g.below(max_n / 4 + 1);   // bursts
        tick_writes(g, m, mem.data(), mem_n, n, &ww, &outside);
        const int what = (int)g.below(10);
        if (what == 0) {   // rollback now; ww = pending writes since the last commit
            const uint32_t d = g.below(depth + 3);   // sometimes too deep
            const uint32_t to = R.newest() >= d ? R.newest() - d : 0;
            const std::vector<uint8_t> before = mem;
            uint64_t ob = 0;
            const bool ok = R.rollback(to, mem.data(), ww.data(), ww.size(), &ob);
            CHECK(R.audit()); audits++;
            if (to < R.oldest()) { CHECK(!ok); CHECK(mem == before); refused_ok++;
                // memory still has the pending writes; commit them as the next tick to keep going
                t = R.newest() + 1; blob = 0xB0B0000000000000ull | t;
                R.commit(t, mem.data(), ww.data(), ww.size(), &blob);
                CHECK(R.audit()); audits++;
                refs.push_back(take(m, mem.data(), t, blob));
                continue;
            }
            CHECK(ok);
            while (!refs.empty() && refs.back().tick > to) refs.pop_back();
            CHECK(!refs.empty() && refs.back().tick == to);
            CHECK(same(m, mem.data(), refs.back()));
            CHECK(ob == refs.back().blob);
            CHECK(R.verify(mem.data()) == 0);
            for (size_t a = 0; a < mem_n; a++) if (m.slot_of((uint32_t)a) == kNoSlot && mem[a] != outside[a]) { CHECK(!"outside byte changed"); break; }
            CHECK(R.newest() == to);
            t = to; rollbacks++; deep += d >= depth / 2;
            continue;
        }
        t = R.newest() + 1; blob = 0xB0B0000000000000ull | t;
        const bool ok = R.commit(t, mem.data(), ww.data(), ww.size(), &blob);
        CHECK(R.audit()); audits++;
        CHECK(R.free_pages() == pool - R.pool_used());
        if (!ok) { overflow_seen++; CHECK(R.depth_now() == 0); }
        CHECK(R.newest() == t);
        refs.push_back(take(m, mem.data(), t, blob));
        while (refs.size() > depth + 4) refs.pop_front();
        if (i % 97 == 0) CHECK(R.verify(mem.data()) == 0);
        CHECK(R.depth_now() <= depth);
        CHECK(R.pool_used() <= pool);
    }
    std::printf("random %s%s seed=%llx depth=%u pool=%zu max_n=%u ticks=%d: rollbacks=%d (deep %d) refused=%d overflows=%d evicted=%llu final depth=%u audits=%d\n",
                mname(mode), batched ? " batched-reverse" : "", (unsigned long long)seed, depth, pool, max_n, ticks, rollbacks, deep, refused_ok, overflow_seen,
                (unsigned long long)R.stats().evicted_entries, R.depth_now(), audits);
    CHECK(rollbacks > 0);
}

// Exact cases: depth limits, eviction under a small pool, overflow, rollback 0 ticks (pending only), rollback then roll back again.
static void test_edges(RingMode mode)
{
    std::vector<uint8_t> mem(16 * kPage, 0);
    DeltaRing R; R.reset({{0, 16 * kPage}}, 3, 4, 4, mode);   // depth 3, pool of 4 pages
    uint32_t b = 10; R.baseline(10, mem.data(), &b);
    auto w = [&](uint32_t s, uint8_t v) { mem[s * kPage + 7] = v; };
    uint32_t s1[] = {1}; w(1, 11); b = 11; CHECK(R.commit(11, mem.data(), s1, 1, &b));
    uint32_t s2[] = {2, 3}; w(2, 12); w(3, 12); b = 12; CHECK(R.commit(12, mem.data(), s2, 2, &b));
    CHECK(R.oldest() == 10 && R.depth_now() == 2 && R.pool_used() == 3);
    CHECK(!R.commit(12, mem.data(), s1, 1, &b) && !R.commit(11, mem.data(), s1, 1, &b));   // not after newest (a later tick with a gap is allowed: the gap test)
    uint32_t s3[] = {4, 5}; w(4, 13); w(5, 13); b = 13; CHECK(R.commit(13, mem.data(), s3, 2, &b));   // needs 2, pool has 1 free: evicts 10
    CHECK(R.oldest() == 11 && R.pool_used() == 4);       // 10 evicted; 11's page (the pre-image of 10) freed with it: 2 + 2 pages
    uint32_t ob = 0;
    CHECK(!R.rollback(10, mem.data(), nullptr, 0, &ob));  // evicted
    // pending write then rollback 0 ticks
    w(6, 99); uint32_t p[] = {6};
    CHECK(R.rollback(13, mem.data(), p, 1, &ob) && ob == 13 && mem[6 * kPage + 7] == 0);
    CHECK(R.rollback(11, mem.data(), nullptr, 0, &ob) && ob == 11);
    CHECK(mem[1 * kPage + 7] == 11 && mem[2 * kPage + 7] == 0 && mem[3 * kPage + 7] == 0 && mem[4 * kPage + 7] == 0 && mem[5 * kPage + 7] == 0);
    CHECK(R.newest() == 11 && R.verify(mem.data()) == 0);
    // continue after a rollback, then roll back across the old and new entries
    uint32_t s4[] = {1, 7}; w(1, 21); w(7, 21); b = 12; CHECK(R.commit(12, mem.data(), s4, 2, &b));
    CHECK(R.rollback(11, mem.data(), nullptr, 0, &ob) && ob == 11 && mem[1 * kPage + 7] == 11 && mem[7 * kPage + 7] == 0);
    // overflow: 5 pages > pool 4 -> false, ring reset to that tick, shadow exact, still usable
    uint32_t s5[] = {8, 9, 10, 11, 12}; for (uint32_t s : s5) w(s, 30); b = 12;
    CHECK(!R.commit(12, mem.data(), s5, 5, &b));
    CHECK(R.depth_now() == 0 && R.oldest() == 12 && R.verify(mem.data()) == 0 && R.stats().overflows == 1);
    w(8, 31); uint32_t p2[] = {8};
    CHECK(R.rollback(12, mem.data(), p2, 1, &ob) && mem[8 * kPage + 7] == 30 && ob == 12);
    CHECK(R.audit());
    // depth cap without pool pressure: depth 3 -> 4 entries
    DeltaRing Q; Q.reset({{0, 16 * kPage}}, 3, 100, 0, mode);
    Q.baseline(0, mem.data(), nullptr);
    for (uint32_t t = 1; t <= 10; t++) { uint32_t s[] = {t % 16}; w(t % 16, (uint8_t)t); CHECK(Q.commit(t, mem.data(), s, 1, nullptr)); }
    CHECK(Q.oldest() == 7 && Q.newest() == 10 && Q.depth_now() == 3 && Q.audit());
}

using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a) { return std::chrono::duration<double, std::milli>(Clock::now() - a).count(); }

// snap-3 size: 144,498,688 bytes = 35,278 pages. 36 ranges in the game; here 36 equal-ish ranges with 1 MB gaps.
// hot = 0: pages uniform over the whole set (worst case, little overlap between ticks); hot = H: pages drawn from a fixed set of H pages
// (snap-3: 1,634 distinct pages changed over 60 ticks, so the per-tick sets overlap and an 8-tick rollback copies at most H pages).
static void timing(uint32_t per_tick, uint32_t ticks, uint32_t depth, size_t pool, uint32_t hot, RingMode mode)
{
    const uint32_t total_pages = 35278, nr = 36;
    std::vector<Range> set; uint32_t a = 0x00100000, left = total_pages;
    for (uint32_t i = 0; i < nr; i++) {
        const uint32_t n = i + 1 == nr ? left : total_pages / nr;
        set.push_back({a, n * kPage}); a += n * kPage + 0x100000; left -= n;
    }
    static std::vector<uint8_t> mem;
    mem.assign((size_t)a + 0x100000, 0);
    Rng g{0x1234567ull + per_tick};
    for (size_t i = 0; i < mem.size(); i += 64) mem[i] = (uint8_t)g.next();   // touch every page (no first-touch faults in the timing)
    DeltaRing R; R.reset(set, depth, pool, 64, mode);
    const PageMap& m = R.map();
    CHECK(m.pages() == total_pages && (uint64_t)m.pages() * kPage == 144498688ull);
    const auto tb = Clock::now();
    R.baseline(0, mem.data(), nullptr);
    const double base_ms = ms(tb);
    std::vector<uint32_t> hotset;
    for (uint32_t i = 0; i < hot; i++) hotset.push_back(g.below(m.pages()));
    auto pick = [&]() { return hot ? hotset[g.below(hot)] : g.below(m.pages()); };
    std::vector<uint32_t> ww; ww.reserve(per_tick);
    double commit_sum = 0, commit_max = 0, rb_sum = 0, rb_max = 0; int rbs = 0, rb_ok = 0;
    Ref target{0, {}, 0};
    uint32_t t = 0;
    for (uint32_t i = 0; i < ticks; i++) {
        ww.clear();
        for (uint32_t j = 0; j < per_tick; j++) { const uint32_t s = pick(); mem[m.addr_of(s) + g.below(kPage)] ^= (uint8_t)(1 + g.below(255)); ww.push_back(s); }
        t++;
        const auto t0 = Clock::now();
        R.commit(t, mem.data(), ww.data(), ww.size(), nullptr);
        const double c = ms(t0); commit_sum += c; commit_max = std::max(commit_max, c);
        if (t % 40 == 30) target = take(m, mem.data(), t, 0);   // a reference 8 ticks before the rollback at t % 40 == 38
        if (t % 40 == 38) {
            ww.clear();   // pending writes of the current tick, then roll back 8 ticks
            for (uint32_t j = 0; j < per_tick; j++) { const uint32_t s = pick(); mem[m.addr_of(s) + g.below(kPage)] ^= 0x33; ww.push_back(s); }
            const auto t1 = Clock::now();
            const bool ok = R.rollback(t - 8, mem.data(), ww.data(), ww.size(), nullptr);
            const double r = ms(t1); rb_sum += r; rb_max = std::max(rb_max, r); rbs++;
            rb_ok += ok && same(m, mem.data(), target);
            t -= 8;
        }
    }
    CHECK(rbs > 0 && rb_ok == rbs);
    CHECK(R.verify(mem.data()) == 0 && R.audit());
    std::printf("timing %-5s %s pages/tick=%u (distinct %u last) ticks=%u: baseline(full copy) %.2f ms | commit mean %.3f ms max %.3f ms | rollback 8 ticks (+%u pending) mean %.3f ms max %.3f ms, "
                "%u pages copied last | %d/%d rollbacks byte-equal to the full copy | ring %zu MB (shadow + pool %zu pages)\n",
                mname(mode), hot ? "hot-1634" : "uniform", per_tick, R.stats().last_commit_pages, ticks, base_ms, commit_sum / ticks, commit_max, per_tick, rb_sum / rbs, rb_max, R.stats().last_rollback_pages, rb_ok, rbs,
                R.bytes() >> 20, pool);
}

// The saved set grows mid-session (demand-committed memory). Memory is zero outside the set at the start; the new range is written only after the growth. Every tick's
// reference is taken over the whole current set; a rollback to a tick before the growth must give the old pages back and ZERO the new ones, and the shadow audit must hold.
static void test_extend(uint64_t seed, int ticks, int grow_at, bool batched = false)
{
    Rng g{seed};
    const size_t N = 64 * kPage;
    std::vector<uint8_t> mem(N, 0);
    std::vector<Range> base = {{0, 8 * kPage}, {16 * kPage, 4 * kPage}}, add1 = {{32 * kPage, 4 * kPage}}, add2 = {{40 * kPage, 2 * kPage}, {48 * kPage, kPage}};
    DeltaRing r; r.reset(base, 12, 4096, 8, RingMode::Copy2);
    if (batched) r.set_copier(reverse_copier, nullptr);
    CHECK(!r.extend(add1));   // no baseline yet
    for (size_t i = 0; i < N; i++) mem[i] = 0;
    for (uint32_t s = 0; s < r.map().pages(); s++) mem[r.map().addr_of(s)] = (uint8_t)(s + 1);
    r.baseline(1, mem.data(), nullptr);
    std::vector<Ref> refs; std::vector<uint32_t> pend;   // refs over the set AT THAT TICK; pages of later growth are zero then
    auto full_ref = [&](uint32_t t) { Ref x = take(r.map(), mem.data(), t, 0); return x; };
    refs.push_back(full_ref(1));
    std::vector<uint32_t> nt;   // new slots to list in the next commit
    uint32_t t = 1; bool grew1 = false, grew2 = false;
    for (int i = 0; i < ticks; i++) {
        std::vector<uint32_t> ww;
        if (i == grow_at && !grew1) { const uint32_t np0 = r.map().pages(); CHECK(r.extend(add1)); for (uint32_t s = np0; s < r.map().pages(); s++) { nt.push_back(s); mem[r.map().addr_of(s) + 3] = (uint8_t)(0x40 + s); } grew1 = true; }   // the block was written during this tick
        if (i == grow_at + 7 && !grew2) { const uint32_t np0 = r.map().pages(); CHECK(r.extend(add2)); for (uint32_t s = np0; s < r.map().pages(); s++) { nt.push_back(s); mem[r.map().addr_of(s) + 9] = (uint8_t)(0x80 + s); } grew2 = true; }
        tick_writes(g, r.map(), mem.data(), N, 1 + g.below(6), &ww, nullptr);
        ww.insert(ww.end(), nt.begin(), nt.end()); nt.clear();
        ++t; CHECK(r.commit(t, mem.data(), ww.data(), ww.size(), nullptr));
        refs.push_back(full_ref(t));
        CHECK(r.audit());
        if (g.below(5) == 0 && t > r.oldest() + 1) {   // roll back 1..depth ticks (not past the oldest), then continue
            const uint32_t to = r.oldest() + g.below(t - r.oldest());
            std::vector<uint32_t> none;
            CHECK(r.rollback(to, mem.data(), none.data(), 0, nullptr));
            // the reference of tick `to`, over the CURRENT set: pages that did not exist at `to` must read zero
            const Ref& ref = [&]() -> const Ref& { for (const Ref& x : refs) if (x.tick == to) return x; return refs.front(); }();
            bool ok = true;
            for (uint32_t s = 0; s < r.map().pages() && ok; s++) {
                const uint8_t* a = mem.data() + r.map().addr_of(s);
                if ((size_t)s * kPage < ref.b.size()) ok = !std::memcmp(a, ref.b.data() + (size_t)s * kPage, kPage);
                else for (uint32_t j = 0; j < kPage; j++) if (a[j]) { ok = false; break; }
            }
            CHECK(ok);
            while (!refs.empty() && refs.back().tick > to) refs.pop_back();
            t = to; CHECK(r.audit());
        }
    }
    CHECK(grew1 && grew2);
    CHECK(r.map().slot_of(32 * kPage) != kNoSlot && r.map().slot_of(41 * kPage) != kNoSlot && r.map().slot_of(48 * kPage + 5) != kNoSlot && r.map().slot_of(5 * kPage) != kNoSlot);
    CHECK(r.map().slot_of(36 * kPage) == kNoSlot && r.map().slot_of(20 * kPage) == kNoSlot);
    DeltaRing p; p.reset(base, 12, 4096, 8, RingMode::PagePtr); p.baseline(1, mem.data(), nullptr);
    CHECK(!p.extend(add1));   // PagePtr cannot extend: the caller re-baselines
}

// ENHANCEMENT (rollback latency, jsy::Cfg::precheck_us): a fake world for jsy::gate(). `busy_unfrozen` = presamples that still see the mutex held, `busy_frozen`
// = frozen samples that see it held (a condition the unfrozen read misses). The frozen judge must still decide: a clean precheck never skips a refused freeze.
struct FakeGateOps {
    int busy_unfrozen = 0, busy_frozen = 0, suspends = 0, resumes = 0, spins = 0, presamples = 0; bool frozen = false, sampled_frozen_only = true;
    bool suspend() { suspends++; frozen = true; return true; }
    bool resume() { resumes++; frozen = false; return true; }
    void sample(jsy::View* v) { *v = jsy::View{}; if (!frozen) sampled_frozen_only = false; if (busy_frozen > 0) { busy_frozen--; v->mtx_held = 1; } }
    void yield() { if (busy_unfrozen > 0) busy_unfrozen--; }
    void presample(jsy::View* v) { *v = jsy::View{}; presamples++; if (frozen) sampled_frozen_only = false; if (busy_unfrozen > 0) v->mtx_held = 1; }
    void spin() { spins++; if (busy_unfrozen > 0) busy_unfrozen--; }
};
static void test_gate_precheck()
{
    jsy::Cfg off; jsy::Cfg on; on.precheck_us = 50000;
    {   // off: the earlier gate, one refused freeze per busy frozen sample, no presample
        FakeGateOps o; o.busy_frozen = 2;
        const jsy::GateResult g = jsy::gate(o, jsy::Mode::Wait, nullptr, 100, off);
        CHECK(g.ok && g.freezes == 3 && o.presamples == 0 && o.spins == 0 && g.first_reasons == (unsigned)jsy::MutexHeld);
    }
    {   // on: the precheck waits out the unfrozen condition, then ONE freeze passes
        FakeGateOps o; o.busy_unfrozen = 5; o.busy_frozen = 0;
        const jsy::GateResult g = jsy::gate(o, jsy::Mode::Wait, nullptr, 100, on);
        CHECK(g.ok && g.suspended && g.freezes == 1 && o.spins == 5 && g.prechecks == 6 && g.wakes == 5 && o.frozen && o.sampled_frozen_only);
    }
    {   // on: a condition only the frozen sample sees is still refused (the frozen judge decides), and the gate retries as before
        FakeGateOps o; o.busy_unfrozen = 0; o.busy_frozen = 2;
        const jsy::GateResult g = jsy::gate(o, jsy::Mode::Wait, nullptr, 100, on);
        CHECK(g.ok && g.freezes == 3 && g.first_reasons == (unsigned)jsy::MutexHeld && o.resumes == 2 && o.sampled_frozen_only);
    }
    {   // on, budget used up: a precheck that never clears stops waiting after precheck_us and the frozen loop goes on (here the frozen sample is clean)
        FakeGateOps o; o.busy_unfrozen = 1 << 30; jsy::Cfg b = on; b.precheck_us = 2000;
        const jsy::GateResult g = jsy::gate(o, jsy::Mode::Wait, nullptr, 100, b);
        CHECK(g.ok && g.freezes == 1 && o.spins > 0);
    }
    {   // Log mode: one freeze, no precheck
        FakeGateOps o; o.busy_unfrozen = 3;
        const jsy::GateResult g = jsy::gate(o, jsy::Mode::Log, nullptr, 100, on);
        CHECK(g.ok && g.freezes == 1 && o.presamples == 0);
    }
    std::printf("gate precheck: off/on/frozen-only/budget/log cases checked\n");
}

// FIX (same-link rematch): two rollback sessions in one process. The second session's ticks restart at 1: without invalidate() its first commit is
// refused (t <= newest()), which ended every rematch at once ("commit failed"). After invalidate() the next session baselines afresh and commits /
// rolls back exactly as the first one (checked against full copies).
static void test_two_sessions(RingMode mode)
{
    const size_t mem_n = 0x100000;
    std::vector<uint8_t> mem(mem_n);
    Rng g{0x2E5E5510ull};
    for (auto& b : mem) b = (uint8_t)g.next();
    DeltaRing R; R.reset({{0x10000, 32 * kPage}, {0x60000, 32 * kPage}}, 8, 1024, sizeof(uint64_t), mode);
    const PageMap& m = R.map();
    for (int session = 0; session < 2; session++) {
        uint64_t blob = 1;
        if (session == 1) {
            std::vector<uint32_t> none(1, 0);
            CHECK(!R.commit(1, mem.data(), none.data(), 0, &blob));   // the old bug: the first session's ring refuses tick 1
            R.invalidate();
            CHECK(!R.valid());
        }
        R.baseline(1, mem.data(), &blob);
        CHECK(R.valid() && R.newest() == 1);
        std::map<uint32_t, Ref> refs; refs.emplace(1, take(m, mem.data(), 1, blob));
        const uint32_t last = session == 0 ? 60 : 30;
        for (uint32_t t = 2; t <= last; t++) {
            std::vector<uint32_t> ww; tick_writes(g, m, mem.data(), mem_n, 1 + g.below(20), &ww, nullptr);
            blob = 0xA000 + t + 1000u * (uint32_t)session;
            CHECK(R.commit(t, mem.data(), ww.data(), ww.size(), &blob));
            refs.emplace(t, take(m, mem.data(), t, blob));
        }
        std::vector<uint32_t> ww; tick_writes(g, m, mem.data(), mem_n, 5, &ww, nullptr);   // written after the last commit
        uint64_t ob = 0;
        const uint32_t to = last - 5;
        CHECK(R.rollback(to, mem.data(), ww.data(), ww.size(), &ob));
        CHECK(same(m, mem.data(), refs.at(to)) && ob == refs.at(to).blob);
        CHECK(R.audit());
    }
    std::printf("two sessions (%s): second session baselined at tick 1 after invalidate, commits and rollback ok\n", mname(mode));
}

int main()
{
    test_two_sessions(RingMode::Copy2); test_two_sessions(RingMode::PagePtr);
    test_gate_precheck();
    test_pagemap();
    test_extend(0xE17E0001ull, 400, 20); test_extend(0xE17E0002ull, 400, 3); test_extend(0xE17E0003ull, 600, 150);
    test_extend(0xE17E0001ull, 400, 20, true); test_extend(0xE17E0003ull, 600, 150, true);   // the ring growth with batched (reverse-order) copies
    for (RingMode mode : {RingMode::Copy2, RingMode::PagePtr}) {
        test_edges(mode);
        test_random(0xC0FFEE01ull, 8, 4096, 120, 4000, mode);   // ample pool: depth-limited
        test_random(0x5EED0002ull, 10, 300, 120, 4000, mode);   // small pool: bursts evict
        test_random(0x5EED0003ull, 8, 100, 400, 3000, mode);    // bursts larger than the pool: overflows
    }
    // ENHANCEMENT (parallel page copy): the same random runs with the copies handed out as batches and run in reverse (Copy2; PagePtr ignores the copier).
    for (RingMode mode : {RingMode::Copy2, RingMode::PagePtr}) {
        test_random(0xC0FFEE01ull, 8, 4096, 120, 4000, mode, true);
        test_random(0x5EED0002ull, 10, 300, 120, 4000, mode, true);
        test_random(0x5EED0003ull, 8, 100, 400, 3000, mode, true);
    }
    CHECK(g_batches > 1000 && g_batch_bad == 0);
    std::printf("batched copier: %llu batches, %llu page ops, %llu contract violations\n", (unsigned long long)g_batches, (unsigned long long)g_batch_ops, (unsigned long long)g_batch_bad);
    // Rollback mode: the eviction signal. After a rollback of >= 2 ticks the SPAN is below the depth until the ring refills (the entry capacity is depth + 1 and a
    // rollback drops the newer entries): a span check gives false alarms. pool_evicted is the right signal: 0 for capacity eviction and rollbacks, > 0 when the pool is short.
    for (RingMode mode : {RingMode::Copy2, RingMode::PagePtr}) {
        std::vector<uint8_t> mem(1 << 20, 0);
        DeltaRing r; r.reset({{0, 16 * kPage}}, 12, 20480, 16, mode);
        uint8_t blob[16] = {}; uint32_t slot = 0;
        r.baseline(118, mem.data(), blob);
        for (uint32_t t = 119; t <= 130; t++) r.commit(t, mem.data(), &slot, 1, blob);
        CHECK(r.oldest() == 118 && r.newest() == 130);
        r.rollback(127, mem.data(), nullptr, 0, blob);
        r.commit(128, mem.data(), &slot, 1, blob);
        CHECK(r.newest() - r.oldest() == 10);          // the span the old check misread as an eviction
        CHECK(r.stats().pool_evicted == 0);            // the real signal stays 0
        for (uint32_t t = 129; t <= 150; t++) r.commit(t, mem.data(), &slot, 1, blob);
        CHECK(r.stats().pool_evicted == 0 && r.stats().evicted_entries > 0);   // capacity evictions happen and are not pool evictions
        DeltaRing q; q.reset({{0, 16 * kPage}}, 12, 3, 16, mode);          // a 3-page pool: two pages per commit cannot hold 13 entries
        uint32_t two[2] = {0, 1};
        q.baseline(10, mem.data(), blob);
        for (uint32_t t = 11; t <= 30; t++) q.commit(t, mem.data(), two, 2, blob);
        CHECK(q.stats().pool_evicted > 0);
    }
    // Rollback mode, ext mode: 40+ ticks with rollbacks of 1..8 (what GekkoNet asks within max_prediction 8, ring depth 12): every target stays inside [oldest, newest]
    // (the module's own check) and nothing is ever evicted by the pool. The oldest entry leaving by CAPACITY is normal.
    for (RingMode mode : {RingMode::Copy2, RingMode::PagePtr}) {
        std::vector<uint8_t> mem(1 << 20, 0);
        DeltaRing r; r.reset({{0, 16 * kPage}}, 12, 20480, 16, mode);
        uint8_t blob[16] = {}; uint32_t slot = 0, tick = 1; int bad = 0;
        r.baseline(tick, mem.data(), blob);
        for (int i = 0; i < 80; i++) {
            const uint32_t back = 1 + (uint32_t)(i % 8);
            for (uint32_t j = 0; j < back + 2; j++) r.commit(++tick, mem.data(), &slot, 1, blob);   // the replay re-commits `back` ticks, plus two new ones
            if (tick - back >= r.oldest() && tick - back <= r.newest()) { r.rollback(tick - back, mem.data(), nullptr, 0, blob); tick -= back; }
            else bad++;
        }
        CHECK(bad == 0 && r.stats().pool_evicted == 0);
    }
    // ENHANCEMENT (rollback latency: saves of final frames take no snapshot): commits skip ticks (0..3 between two snapshots, written pages accumulate in
    // between); a rollback to a saved tick restores it exactly (mem, blob, shadow); a skipped tick, or one outside the window, is refused and touches nothing.
    for (RingMode mode : {RingMode::Copy2, RingMode::PagePtr}) {
        Rng g{0x5EED5u + (uint64_t)mode};
        const size_t mem_n = 1 << 20; std::vector<uint8_t> mem(mem_n, 0), outside(mem_n, 0);
        DeltaRing r; r.reset({{0x10000, 64 * kPage}, {0x60000, 32 * kPage}}, 10, 4096, 8, mode);
        const PageMap& m = r.map();
        uint32_t t = 1; uint64_t blob = 1; std::vector<Ref> refs;
        r.baseline(t, mem.data(), &blob); refs.push_back(take(m, mem.data(), t, blob));
        int restored = 0, skipped_refused = 0;
        for (int i = 0; i < 600; i++) {
            std::vector<uint32_t> ww;
            const uint32_t gap = g.below(4);
            for (uint32_t j = 0; j <= gap; j++) tick_writes(g, m, mem.data(), mem_n, 1 + g.below(12), &ww, &outside);
            t += gap + 1; blob = t * 7919u;
            CHECK(r.commit(t, mem.data(), ww.data(), ww.size(), &blob));
            CHECK(!r.commit(t, mem.data(), ww.data(), 0, &blob));   // the same tick again: refused (t <= newest)
            refs.push_back(take(m, mem.data(), t, blob));
            while (!refs.empty() && refs.front().tick < r.oldest()) refs.erase(refs.begin());
            CHECK(!refs.empty() && refs.front().tick == r.oldest() && refs.back().tick == r.newest());
            if (g.below(3) == 0) {
                std::vector<uint32_t> pend; tick_writes(g, m, mem.data(), mem_n, 1 + g.below(6), &pend, &outside);   // the tick in progress
                if (refs.size() >= 2 && refs[refs.size() - 1].tick - refs[refs.size() - 2].tick > 1) {   // a skipped tick: refused, nothing touched
                    const std::vector<uint8_t> before = mem; uint64_t ob = 0;
                    CHECK(!r.has(refs.back().tick - 1));
                    CHECK(!r.rollback(refs.back().tick - 1, mem.data(), pend.data(), pend.size(), &ob) && mem == before);
                    skipped_refused++;
                }
                const size_t pick = refs.size() - 1 - g.below((uint32_t)std::min<size_t>(refs.size(), 4));
                uint64_t ob = 0;
                CHECK(r.has(refs[pick].tick));
                CHECK(r.rollback(refs[pick].tick, mem.data(), pend.data(), pend.size(), &ob));
                CHECK(same(m, mem.data(), refs[pick]) && ob == refs[pick].blob && r.verify(mem.data()) == 0 && r.newest() == refs[pick].tick);
                for (size_t a = 0; a < mem_n; a++) if (m.slot_of((uint32_t)a) == kNoSlot && mem[a] != outside[a]) { CHECK(!"outside byte changed"); break; }
                refs.resize(pick + 1); t = refs.back().tick; restored++;
            }
            CHECK(r.audit());
        }
        CHECK(restored > 100 && skipped_refused > 20 && r.stats().overflows == 0);
        std::printf("gaps %s: %d rollbacks to saved ticks, %d to skipped ticks refused\n", mname(mode), restored, skipped_refused);
    }
    for (RingMode mode : {RingMode::Copy2, RingMode::PagePtr}) {
        timing(27, 400, 10, 10 * 2048, 0, mode);
        timing(500, 400, 10, 10 * 2048, 0, mode);
        timing(1634, 200, 10, 10 * 2048, 0, mode);
        timing(500, 400, 10, 10 * 2048, 1634, mode);
        timing(1634, 200, 10, 10 * 2048, 1634, mode);
        timing(3500, 120, 10, 10 * 6144, 0, mode);   // the q4-io-1 rate (~3000-4300 pages per tick with the IO block saved), pool depth*6144
    }
    std::printf("%s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
