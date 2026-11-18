// jsp_snapshot_core.h -- DIAGNOSTIC (rollback prototype, route A): pure, host-testable parts of JSP_SNAPSHOT.
// No guest or runtime headers: a "memory" is a host byte pointer (vm_base) and guest ranges are [addr, addr+n).
//   * range set bookkeeping: normalise (sort + merge), subtract (exclusions), total
//   * Store: copy of a range set, restore, page diff against a store (dirty pages), diff-and-update (per-tick dirty count)
//   * Driver: the replay/compare state machine (save at tick S, run K ticks, restore, replay K ticks, compare per-tick hashes)
//
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstddef>
#include <vector>
#include <string>
#include <algorithm>

namespace jss {

constexpr uint32_t kPage = 0x1000;   // guest page size (vm.h VM_PAGE_SIZE)

struct Range { uint32_t a = 0, n = 0; };   // [a, a+n), n > 0; the 4 GB space ends at 0xFFFFFFFF so n is 32-bit (a + n <= 2^32 is kept in u64 below)

inline uint64_t end_of(const Range& r) { return (uint64_t)r.a + r.n; }

// Sorted, non-empty, merged (touching ranges join).
inline std::vector<Range> normalise(std::vector<Range> v)
{
    v.erase(std::remove_if(v.begin(), v.end(), [](const Range& r) { return r.n == 0; }), v.end());
    std::sort(v.begin(), v.end(), [](const Range& x, const Range& y) { return x.a < y.a; });
    std::vector<Range> o;
    for (const Range& r : v) {
        if (!o.empty() && r.a <= end_of(o.back())) {
            const uint64_t e = std::max(end_of(o.back()), end_of(r));
            o.back().n = (uint32_t)(e - o.back().a);
        } else o.push_back(r);
    }
    return o;
}

// in minus ex (both normalised internally).
inline std::vector<Range> subtract(const std::vector<Range>& in, const std::vector<Range>& ex)
{
    const std::vector<Range> I = normalise(in), E = normalise(ex);
    std::vector<Range> o;
    size_t j = 0;
    for (const Range& r : I) {
        uint64_t cur = r.a; const uint64_t e = end_of(r);
        while (j < E.size() && end_of(E[j]) <= cur) j++;
        size_t k = j;
        while (cur < e) {
            if (k >= E.size() || E[k].a >= e) { o.push_back({(uint32_t)cur, (uint32_t)(e - cur)}); break; }
            if (E[k].a > cur) o.push_back({(uint32_t)cur, (uint32_t)(E[k].a - cur)});
            cur = std::max(cur, end_of(E[k]));
            k++;
        }
    }
    return o;
}

// Clip to a window [lo, hi) (hi as u64 so 0x100000000 is expressible).
inline std::vector<Range> clip(const std::vector<Range>& in, uint32_t lo, uint64_t hi)
{
    std::vector<Range> o;
    for (const Range& r : normalise(in)) {
        const uint64_t a = std::max<uint64_t>(r.a, lo), e = std::min<uint64_t>(end_of(r), hi);
        if (a < e) o.push_back({(uint32_t)a, (uint32_t)(e - a)});
    }
    return o;
}

inline uint64_t total(const std::vector<Range>& v) { uint64_t t = 0; for (const Range& r : v) t += r.n; return t; }

// Round every range outward to whole guest pages (so page diffs and copies are page-exact), then normalise.
inline std::vector<Range> page_align(const std::vector<Range>& in)
{
    std::vector<Range> o;
    for (const Range& r : in) {
        const uint64_t a = r.a & ~(uint64_t)(kPage - 1), e = (end_of(r) + kPage - 1) & ~(uint64_t)(kPage - 1);
        o.push_back({(uint32_t)a, (uint32_t)(e - a)});
    }
    return normalise(o);
}

// The guest windows route A copies (their committed, writable pages; the thread/callback stacks are added by the caller because their
// end moves). Run snap-1 (2026-11-12) saved only 11 MB because "user-mem" was missing: ps3recomp's sys_memory_allocate hands out
// 0x10000000..0x20000000 (runtime/syscalls/sys_memory.c SYS_MEM_ALLOC_BASE/END), and J-Stars allocates its whole heap there at boot
// (0xB800000 at 0x11000000, 0x100000 at 0x10000000): the Random object, the fighters, the setup singleton, the job descriptors. The
// RSX-visible IO mapping that the game carves out of that heap (cellGcmInit ioAddr 0x16300000, size 0x3A00000) is excluded by the caller.
struct Window { uint32_t lo; uint64_t hi; const char* name; };
inline constexpr Window kSaveWindows[] = {
    {0x00010000u, 0x10000000ull, "main"},       // ELF image, .data/.bss, the guest TLS block, the primary thread's stack
    {0x10000000u, 0x20000000ull, "user-mem"},   // sys_memory_allocate (the game heap); the runtime TLS image 0x10F00000
};

// Pages lying ENTIRELY inside an exclusion (rounded inward). Rounding the exclusion outward dropped the whole page 0x0096E000 (title GCM
// context + a per-frame SPURS job) to keep a 4-byte slot out of the save; a partially excluded page is now saved and its excluded bytes are preserved
// across a restore (preserved_bytes()).
inline std::vector<Range> inner_pages(const std::vector<Range>& in)
{
    std::vector<Range> o;
    for (const Range& r : in) {
        const uint64_t a = ((uint64_t)r.a + kPage - 1) & ~(uint64_t)(kPage - 1), e = end_of(r) & ~(uint64_t)(kPage - 1);
        if (a < e) o.push_back({(uint32_t)a, (uint32_t)(e - a)});
    }
    return normalise(o);
}
// The saved set: committed pages minus the pages entirely inside an exclusion.
inline std::vector<Range> plan_save(const std::vector<Range>& committed, const std::vector<Range>& ex)
{
    return subtract(page_align(committed), inner_pages(ex));
}
// The exact bytes of the exclusions that lie inside saved pages: a restore must leave them as they are (the caller copies them out before and back after).
inline std::vector<Range> preserved_bytes(const std::vector<Range>& keep, const std::vector<Range>& ex)
{
    std::vector<Range> o;
    for (const Range& e : normalise(ex))
        for (const Range& k : keep) {
            const uint64_t a = std::max<uint64_t>(e.a, k.a), b = std::min<uint64_t>(end_of(e), end_of(k));
            if (a < b) o.push_back({(uint32_t)a, (uint32_t)(b - a)});
        }
    return normalise(o);
}

// True when [a, a+n) lies inside one range of the set (normalised internally; touching ranges merge, so "one range" is exact).
inline bool covers(const std::vector<Range>& set, uint32_t a, uint32_t n)
{
    const uint64_t e = (uint64_t)a + (n ? n : 1);
    for (const Range& r : normalise(set)) if (r.a <= a && e <= end_of(r)) return true;
    return false;
}

// A copy of a range set of one memory. The buffer is allocated once (reset() resizes; call it before any thread is suspended).
struct Store {
    std::vector<Range> r;
    std::vector<uint8_t> buf;
    bool valid = false;

    void reset(const std::vector<Range>& ranges)
    {
        r = normalise(ranges);
        buf.assign((size_t)total(r), 0);
        valid = false;
    }
    size_t bytes() const { return buf.size(); }
    void save(const uint8_t* mem)
    {
        size_t o = 0;
        for (const Range& x : r) { std::memcpy(buf.data() + o, mem + x.a, x.n); o += x.n; }
        valid = true;
    }
    void restore(uint8_t* mem) const
    {
        size_t o = 0;
        for (const Range& x : r) { std::memcpy(mem + x.a, buf.data() + o, x.n); o += x.n; }
    }
    // Pages (guest page size) of the store that differ from `mem`; every range must be page-aligned (page_align()).
    size_t dirty_pages(const uint8_t* mem) const
    {
        size_t o = 0, d = 0;
        for (const Range& x : r) {
            for (uint32_t p = 0; p < x.n; p += kPage) d += std::memcmp(buf.data() + o + p, mem + x.a + p, kPage) != 0;
            o += x.n;
        }
        return d;
    }
    // Per-tick dirty count: pages that differ from `mem` are counted, and copied into the store (the store follows `mem`).
    size_t diff_update(const uint8_t* mem)
    {
        size_t o = 0, d = 0;
        for (const Range& x : r) {
            for (uint32_t p = 0; p < x.n; p += kPage)
                if (std::memcmp(buf.data() + o + p, mem + x.a + p, kPage) != 0) { d++; std::memcpy(buf.data() + o + p, mem + x.a + p, kPage); }
            o += x.n;
        }
        valid = true;
        return d;
    }
};

// ---------------------------------------------------------------------------------------------------------------------------------
// Replay driver. Ticks are the hash ticks k of jsp_fight_hash.cpp (state after scene pass k). A record = the hash line of tick k.
// Rounds: each round = K ticks. Round i saves at tick S_i, records ticks S_i+1..S_i+K_i (pass 1), asks for a RESTORE at S_i+K_i, then
// the tick counter is rewound to S_i, ticks S_i+1..S_i+K_i are recorded again (pass 2) and compared with pass 1. After round i the
// next round saves at once (S_{i+1} = S_i + K_i, the state after pass 2) and continues.
// ---------------------------------------------------------------------------------------------------------------------------------

constexpr int kComps = 8;   // all, rng, glob, seed, setup, cam, fi, pad (jsp_fight_hash.cpp hash_tick order)
inline const char* const kCompNameImpl[kComps] = {"all", "rng", "glob", "seed", "setup", "cam", "fi", "pad"};

struct Rec { uint64_t v[kComps] = {}; bool have = false; };

enum class Act { None, Save, Restore };

struct RoundResult {
    uint32_t S = 0, K = 0;
    bool done = false, equal = false;
    bool layout_diverged = false;   // the layout changed during the replay (or at its verdict): a replay divergence, counted as MISMATCH
    bool aborted = false;     // the saved layout changed inside the window, the round has no verdict (never counted as equal)
    uint32_t aborted_at = 0;
    int first_bad_tick = 0;   // 0 = none (tick number k)
    int first_bad_comp = -1;  // index into kCompNameImpl of the first component that differs at first_bad_tick (-1 = none)
    uint32_t missing = 0;     // ticks without a record in either pass (hash tap not called)
    uint32_t bad_ticks = 0;   // ticks whose `all` differs
};

// soak mode. The round list is not fixed: each round's K is drawn from a counter-based generator (splitmix64 of seed and round index, so the draw depends
// on nothing but the seed: the same seed gives the same K sequence in every run and on every peer), uniform in 1..depth.
inline uint64_t splitmix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
inline uint32_t soak_k(uint64_t seed, uint64_t round, uint32_t depth) { return depth ? 1u + (uint32_t)(splitmix64(seed ^ splitmix64(round + 1)) % depth) : 1u; }

class Driver {
public:
    // start = S of the first round (first tick at which the state is saved), ks = K per round.
    void configure(uint32_t start, const std::vector<uint32_t>& ks)
    {
        start_ = start; ks_ = ks; round_ = 0; phase_ = Phase::Wait; res_.assign(ks.size(), RoundResult());
        for (size_t i = 0; i < ks.size(); i++) res_[i].K = ks[i];
        if (!ks.empty()) { cur_S_ = start; res_[0].S = start; }
    }
    // soak. Rounds are appended as the previous one closes: K = soak_k(seed, round, depth), the next save `gap` ticks after the replay of the previous
    // round ended (gap 0 = at once), until max_rounds (0 = no cap) or until a round's last tick would pass `last_tick` (the end of the hash window: a round needs a
    // hash record for every tick it compares). The run then finishes like a fixed list does.
    void configure_soak(uint32_t start, uint32_t depth, uint64_t seed, uint32_t gap, uint32_t max_rounds, uint64_t last_tick)
    {
        soak_ = true; sdepth_ = depth; sseed_ = seed; sgap_ = gap; smax_ = max_rounds; slast_ = last_tick;
        configure(start, std::vector<uint32_t>());
        start_ = start; cur_S_ = start;
        if (!soak_append((uint64_t)start)) { ks_.clear(); res_.clear(); }
    }
    bool soak() const { return soak_; }
    // true between two soak rounds (the gap): ticks commit, nothing is being compared, a layout change needs a re-baseline and no round is aborted
    bool in_gap() const { return soak_ && round_ > 0 && round_ < ks_.size() && phase_ == Phase::Wait; }
    // Called after tick k finished, with that tick's record (rec.have = false if no hash line was produced). Returns what to do now:
    // Save: save memory and host state now (this tick is S); Restore: restore it, rewind the tick counter to the returned rewind_to().
    Act on_tick(uint32_t k, const Rec& rec)
    {
        if (round_ >= ks_.size()) return Act::None;
        RoundResult& R = res_[round_];
        const uint32_t S = cur_S_, K = ks_[round_];
        switch (phase_) {
        case Phase::Wait:
            if (k == S) { phase_ = Phase::Pass1; p1_.assign(K, Rec()); return Act::Save; }
            return Act::None;
        case Phase::Pass1:
            if (k > S && k <= S + K) p1_[k - S - 1] = rec;
            if (k == S + K) { phase_ = Phase::Pass2; return Act::Restore; }
            return Act::None;
        case Phase::Pass2:
            if (k > S && k <= S + K) compare(R, k - S - 1, k, rec);
            if (k == S + K) {
                R.done = true; R.equal = R.first_bad_tick == 0 && R.missing == 0;
                round_++;
                if (soak_) soak_append((uint64_t)S + K + sgap_);
                if (round_ < ks_.size()) {
                    cur_S_ = S + K + (soak_ ? sgap_ : 0); res_[round_].S = cur_S_;
                    if (soak_ && sgap_) { phase_ = Phase::Wait; return Act::None; }   // the gap: normal ticks until the next save tick
                    phase_ = Phase::Pass1; p1_.assign(ks_[round_], Rec()); return Act::Save;
                }
                phase_ = Phase::Wait;   // all rounds done: nothing more (round_ == ks_.size())
            }
            return Act::None;
        }
        return Act::None;
    }
    // the memory layout of the saved set changed inside this round's window (new committed pages), so the round can neither be rolled back nor
    // verified. The round is closed as aborted (done, not equal) and the next round starts at tick k (the caller has just taken a new baseline there);
    // returns Act::Save for it, or None when it was the last round. Valid in any phase; ignored when finished.
    Act abort_round(uint32_t k) { return close_current(k, false); }
    // The layout changed in pass 2 (or at the rollback request): the replay cannot be compared, which is a divergence, never ABORTED and never EQUAL.
    Act diverge_round(uint32_t k) { return close_current(k, true); }
    // The layout change was found on the tick that ended the last round (after its verdict): the verdict is withdrawn.
    void downgrade_previous(uint32_t k)
    {
        if (round_ == 0 || round_ > ks_.size()) return;
        RoundResult& R = res_[round_ - 1];
        R.equal = false; R.layout_diverged = true; R.aborted = false; R.bad_ticks++; if (!R.first_bad_tick) R.first_bad_tick = (int)k;
    }
    bool in_pass2() const { return phase_ == Phase::Pass2; }
    Act close_current(uint32_t k, bool diverged)
    {
        if (round_ >= ks_.size()) return Act::None;
        RoundResult& R = res_[round_];
        R.done = true; R.equal = false;
        if (diverged) { R.layout_diverged = true; R.bad_ticks++; if (!R.first_bad_tick) R.first_bad_tick = (int)k; } else { R.aborted = true; R.aborted_at = k; }
        round_++;
        if (soak_) soak_append(k);
        if (round_ < ks_.size()) { cur_S_ = k; res_[round_].S = k; phase_ = Phase::Pass1; p1_.assign(ks_[round_], Rec()); return Act::Save; }
        phase_ = Phase::Wait;
        return Act::None;
    }
    uint32_t rewind_to() const { return cur_S_; }   // valid when the last on_tick returned Restore
    bool finished() const { return round_ >= ks_.size(); }
    size_t round() const { return round_; }
    const std::vector<RoundResult>& results() const { return res_; }
    bool all_equal() const { for (const RoundResult& r : res_) if (!r.done || !r.equal) return false; return !res_.empty(); }

private:
    enum class Phase { Wait, Pass1, Pass2 };
    void compare(RoundResult& R, uint32_t i, uint32_t k, const Rec& b)
    {
        const Rec& a = p1_[i];
        if (!a.have || !b.have) { R.missing++; return; }
        bool bad = false; int comp = -1;
        for (int c = 0; c < kComps; c++) if (a.v[c] != b.v[c]) { bad = true; if (comp < 0 && c > 0) comp = c; }   // component 0 (all) alone differing still counts as bad
        if (bad) {
            R.bad_ticks++;
            if (R.first_bad_tick == 0) { R.first_bad_tick = (int)k; R.first_bad_comp = comp; }
        }
    }
    // soak: append the round that would start at tick `at`; false = none (cap reached or the window cannot hold it)
    bool soak_append(uint64_t at)
    {
        const size_t i = ks_.size();
        if (smax_ && i >= smax_) return false;
        const uint32_t K = soak_k(sseed_, i, sdepth_);
        if (at + K > slast_ || at > 0xFFFFFFFFull) return false;   // also guards the 32-bit tick counter
        ks_.push_back(K); RoundResult r; r.K = K; r.S = (uint32_t)at; res_.push_back(r);
        return true;
    }
    bool soak_ = false; uint32_t sdepth_ = 0, sgap_ = 0, smax_ = 0; uint64_t sseed_ = 0, slast_ = 0;
    uint32_t start_ = 0, cur_S_ = 0;
    std::vector<uint32_t> ks_;
    size_t round_ = 0;
    Phase phase_ = Phase::Wait;
    std::vector<Rec> p1_;
    std::vector<RoundResult> res_;
};

// "60,300,1800" -> {60,300,1800}; returns false on a malformed or zero item (out cleared).
inline bool parse_ks(const char* s, std::vector<uint32_t>* out)
{
    out->clear();
    std::string cur;
    for (const char* c = s ? s : ""; ; c++) {
        if (*c == ',' || !*c) {
            if (cur.empty()) { if (!*c && !out->empty()) break; out->clear(); return false; }
            char* e = nullptr; unsigned long v = std::strtoul(cur.c_str(), &e, 10);
            if (*e || v == 0 || v > 1000000) { out->clear(); return false; }
            out->push_back((uint32_t)v); cur.clear();
            if (!*c) break;
        } else if (*c != ' ') cur += *c;
    }
    return !out->empty();
}

// JSP_SNAPSHOT_EXCLUDE="0x16100000+0x200000,0x1C600000+0x100000": extra guest ranges left out of the save (addr+size, hex or decimal),
// to bisect a hang or a mismatch by region without a rebuild. Empty/unset = none. False on a malformed item (out cleared).
inline bool parse_ranges(const char* s, std::vector<Range>* out)
{
    out->clear();
    if (!s) return true;
    std::string cur;
    for (const char* c = s; ; c++) {
        if (*c == ',' || !*c) {
            if (!cur.empty()) {
                const size_t plus = cur.find('+');
                if (plus == std::string::npos) { out->clear(); return false; }
                char* e1 = nullptr; char* e2 = nullptr;
                const std::string as = cur.substr(0, plus), ns = cur.substr(plus + 1);
                const unsigned long long a = std::strtoull(as.c_str(), &e1, 0), n = std::strtoull(ns.c_str(), &e2, 0);
                if (as.empty() || ns.empty() || *e1 || *e2 || n == 0 || a + n > 0x100000000ull) { out->clear(); return false; }
                out->push_back({(uint32_t)a, (uint32_t)n});
                cur.clear();
            }
            if (!*c) break;
        } else if (*c != ' ') cur += *c;
    }
    return true;
}

}  // namespace jss
