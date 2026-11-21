// jsp_snapshot_ring.h -- DIAGNOSTIC (rollback, route A incremental): pure, host-testable dirty-page undo ring.
// No guest or runtime headers. A "memory" is a host byte pointer (vm_base); the saved set is the page-aligned range list of
// jsp_snapshot_core.h (plan_save()).
//
//   shadow   = one copy of the saved set at the newest committed tick (same flat layout as jss::Store: slot s = page s of the set)
//   entry t  = the pages written during tick t (reported by the write watch), each with its PRE-image (state at tick t-1),
//              plus a fixed-size host blob for tick t (main-thread ppu_context, tick counters)
//   commit t : per written page, shadow -> undo pool (old), mem -> shadow (new)          cost 2 x 4 KB memcpy per written page
//   rollback : pages of entries to+1..newest, oldest pre-image first, each page once    cost 2 x 4 KB memcpy per distinct page
//              (mem and shadow), then the pages written since the last commit that no entry covers: shadow -> mem (1 x 4 KB)
// (RingMode::PagePtr, JSP_SNAPSHOT_RING=ptr): the shadow and the undo pool share one arena of
// pages and the shadow is a table of page indices. commit: mem -> a free arena page that becomes the shadow page, the old shadow page becomes the entry's
// pre-image (1 x 4 KB per written page, a pointer swap instead of the second copy). rollback: pre-image -> mem and the pre-image page becomes the shadow page
// again (1 x 4 KB per distinct page), the replaced page goes back to the free list. The free list is sized at reset() (pool pages), so a commit never finds it
// empty: the pool accounting (evict, overflow) is the same as RingMode::Copy2 and keeps free + entry pages + shadow pages == arena (audit()).
// The pool is a ring of pages shared by all entries; the oldest entries are evicted when a commit needs room, so a burst lowers the
// reachable depth instead of being dropped. A single tick larger than the whole pool resets the ring (depth 0, shadow kept exact).
#pragma once
#include "jsp_snapshot_core.h"

namespace jss {

constexpr uint32_t kNoSlot = 0xFFFFFFFFu;

// Page slots of a saved set: slot s = page s of the normalised, page-aligned ranges in address order (jss::Store's flat layout / kPage).
class PageMap {
public:
    void reset(const std::vector<Range>& ranges)
    {
        r_ = normalise(ranges); first_.clear(); addr_.clear();
        for (const Range& x : r_) {
            first_.push_back((uint32_t)addr_.size());
            for (uint32_t p = 0; p < x.n; p += kPage) addr_.push_back(x.a + p);
        }
        n_sorted_ = r_.size();
    }
    // Append ranges that are not in the set (demand-committed memory appeared). Existing slot numbers do not change; the new slots follow the old ones, the new ranges
    // are appended (ranges() is then address-ordered only within the first reset set and within the appended set). The caller guarantees no overlap with the set.
    void extend(const std::vector<Range>& add)
    {
        for (const Range& x : normalise(add)) {
            r_.push_back(x); first_.push_back((uint32_t)addr_.size());
            for (uint32_t p = 0; p < x.n; p += kPage) addr_.push_back(x.a + p);
        }
    }
    uint32_t pages() const { return (uint32_t)addr_.size(); }
    const std::vector<Range>& ranges() const { return r_; }
    uint32_t first_slot(size_t range_index) const { return first_[range_index]; }
    uint32_t addr_of(uint32_t slot) const { return addr_[slot]; }
    // Slot of the guest page holding `addr`, kNoSlot outside the set.
    uint32_t slot_of(uint32_t addr) const
    {
        size_t lo = 0, hi = n_sorted_;
        while (lo < hi) { const size_t m = (lo + hi) / 2; if (r_[m].a <= addr) lo = m + 1; else hi = m; }
        if (lo && (uint64_t)addr < end_of(r_[lo - 1])) return first_[lo - 1] + (addr - r_[lo - 1].a) / kPage;
        for (size_t i = n_sorted_; i < r_.size(); i++)
            if (addr >= r_[i].a && (uint64_t)addr < end_of(r_[i])) return first_[i] + (addr - r_[i].a) / kPage;
        return kNoSlot;
    }

private:
    std::vector<Range> r_;
    std::vector<uint32_t> first_, addr_;
    size_t n_sorted_ = 0;   // the first n_sorted_ ranges are the reset set (sorted); the rest were appended by extend()
};

struct RingStats {
    uint64_t commits = 0, committed_pages = 0, evicted_entries = 0, overflows = 0, rollbacks = 0, refused = 0,
             pool_evicted = 0;             // entries evicted because the PAGE POOL was short while the ring still had a free entry slot (the reachable depth shrank: the rollback session checks it is 0)
    uint32_t last_commit_pages = 0;      // distinct pages of the last commit
    uint32_t last_rollback_pages = 0;    // distinct pages written into mem by the last rollback (undo + pending)
    uint32_t max_commit_pages = 0;
};

enum class RingMode : uint8_t { Copy2 = 0, PagePtr = 1 };

// ENHANCEMENT (rollback latency, Copy2 only): one page's copies of a commit or a rollback, d1 <- s1 then d2 <- s2 (d2 null: none). Every op of one batch writes
// pages no other op of the batch reads or writes (distinct slots; pool pages of distinct positions), so a batch may run in any order or in parallel and leaves
// the same bytes as the serial loop. The ring lists the ops, then hands the whole batch to the copier (set_copier; none = the serial loop, as before).
struct PageCopy { uint8_t* d1; const uint8_t* s1; uint8_t* d2; const uint8_t* s2; };
using CopyFn = void (*)(void* ctx, const PageCopy* ops, size_t n);

class DeltaRing {
public:
    // ranges: the saved set (page-aligned); depth: the furthest rollback in ticks; pool_pages: undo capacity of all entries together;
    // blob_bytes: host state stored per tick. Allocates everything (arena = shadow + pool pages); call before any thread is suspended.
    void reset(const std::vector<Range>& ranges, uint32_t depth, size_t pool_pages, size_t blob_bytes, RingMode mode = RingMode::Copy2)
    {
        map_.reset(ranges); mode_ = mode;
        cap_ = depth + 1; pool_n_ = pool_pages ? pool_pages : 1; blob_n_ = blob_bytes;
        const size_t np = map_.pages(); np0_ = np;
        arena_.clear(); arena_.reserve((np + pool_n_ + kGrowPages) * kPage);   // spare capacity: extend() never reallocates (a 500 MB copy) up to kGrowPages pages
        arena_.assign((np + pool_n_) * kPage, 0);
        sp_.resize(np); for (size_t s = 0; s < np; s++) sp_[s] = (uint32_t)s;                 // Copy2: fixed; PagePtr: moves
        pre_.resize(pool_n_); for (size_t k = 0; k < pool_n_; k++) pre_[k] = (uint32_t)(np + k); // Copy2: fixed; PagePtr: set per commit
        free_.clear();
        if (mode_ == RingMode::PagePtr) { free_.reserve(pool_n_); for (size_t k = pool_n_; k-- > 0;) free_.push_back((uint32_t)(np + k)); }
        pool_slot_.assign(pool_n_, 0);
        stamp_.assign(map_.pages(), 0);
        ent_.assign(cap_, Entry());
        blob_.assign(cap_ * (blob_n_ ? blob_n_ : 1), 0);
        ops_.clear(); ops_.reserve(np + pool_n_ + kGrowPages);   // a commit lists at most pool_n_ ops, a rollback at most one per page: never reallocated while frozen
        gen_ = 0; count_ = 0; first_ = 0; head_ = 0; used_ = 0; st_ = RingStats(); valid_ = false;
    }
    // The saved set grew by `add` (memory that was demand-committed after the baseline: it was zero-filled and unmapped at every tick before now). Copy2 only. The new
    // pages get zero shadow pages, so a commit that lists them as written stores a ZERO pre-image: a rollback to any older tick writes zeros into them, exactly the content they had
    // then. Old slot numbers, entries and pool pages are untouched. The caller lists the new slots in the next commit or rollback. false = not possible (PagePtr, no ring, or past the spare capacity).
    bool extend(const std::vector<Range>& add)
    {
        if (!valid_ || mode_ != RingMode::Copy2) return false;
        size_t more = 0; for (const Range& x : add) more += x.n / kPage;
        if (!more || arena_.size() / kPage + more > np0_ + pool_n_ + kGrowPages) return false;
        const size_t old_np = map_.pages(), base = arena_.size() / kPage;
        map_.extend(add);
        const size_t np = map_.pages();
        arena_.resize(arena_.size() + (np - old_np) * kPage, 0);
        sp_.resize(np); for (size_t s = old_np; s < np; s++) sp_[s] = (uint32_t)(base + (s - old_np));
        stamp_.resize(np, 0);
        return true;
    }
    // ENHANCEMENT (rollback latency): the copies of a Copy2 commit / rollback go to fn as one batch (it must run every op before it returns). nullptr = the serial loop.
    void set_copier(CopyFn fn, void* ctx) { copier_ = fn; copier_ctx_ = ctx; }
    const PageMap& map() const { return map_; }
    RingMode mode() const { return mode_; }
    size_t bytes() const { return arena_.size(); }
    bool valid() const { return valid_; }
    // FIX (same-link rematch): the session ended. The next session's first commit takes a full baseline again (its ticks restart at 1, and commit()
    // refuses t <= newest(): a second session in one process failed its first commit). The storage is kept; baseline() / reset() refill it.
    void invalidate() { valid_ = false; }
    uint32_t newest() const { return ent(count_ - 1).tick; }
    uint32_t oldest() const { return ent(0).tick; }
    uint32_t depth_now() const { return valid_ ? newest() - oldest() : 0; }
    // True when tick t has an entry (a rollback target). Every tick in oldest()..newest() when every tick was committed.
    bool has(uint32_t t) const { return index_of(t) != kNone; }
    size_t pool_used() const { return used_; }
    const RingStats& stats() const { return st_; }

    // Full copy of the saved set into the shadow at tick t; the ring holds only t afterwards. Cost: one full save.
    void baseline(uint32_t t, const uint8_t* mem, const void* blob)
    {
        for (size_t s = 0; s < map_.pages(); s++) std::memcpy(page(sp_[s]), mem + map_.addr_of((uint32_t)s), kPage);
        release_all();
        count_ = 0; first_ = 0; head_ = 0; used_ = 0;
        push(t, 0, 0, blob);
        valid_ = true;
    }

    // Commit tick t > newest(): usually newest()+1. ENHANCEMENT (rollback latency): a later tick when the ticks in between took no snapshot (states the
    // rollback session can never load again, jsp_net_glue.h): the entry then holds the pages written since the previous ENTRY, with their pre-images at that
    // entry, and the skipped ticks are no rollback target. slots = the pages written since the previous commit (write watch output; duplicates and kNoSlot are
    // skipped). Returns false when this tick alone needs more than the whole pool: the shadow is still brought up to date, the ring is
    // reset to {t} (depth 0) and stats().overflows counts it.
    bool commit(uint32_t t, const uint8_t* mem, const uint32_t* slots, size_t n, const void* blob)
    {
        if (!valid_ || t <= newest()) return false;
        const uint32_t g = next_gen();
        uint32_t d = 0;
        for (size_t i = 0; i < n; i++) if (slots[i] < map_.pages() && stamp_[slots[i]] != g) { stamp_[slots[i]] = g; d++; }
        st_.commits++; st_.committed_pages += d; st_.last_commit_pages = d; st_.max_commit_pages = std::max(st_.max_commit_pages, d);
        if (d > pool_n_) {   // burst larger than the whole pool: no undo for this tick, the window restarts here
            const uint32_t g2 = next_gen();
            for (size_t i = 0; i < n; i++) {
                const uint32_t s = slots[i];
                if (s >= map_.pages() || stamp_[s] == g2) continue;
                stamp_[s] = g2;
                std::memcpy(page(sp_[s]), mem + map_.addr_of(s), kPage);
            }
            release_all();
            count_ = 0; first_ = 0; head_ = 0; used_ = 0;
            push(t, 0, 0, blob);
            st_.overflows++;
            return false;
        }
        while (count_ == cap_ || used_ + d > pool_n_) { if (count_ < cap_) st_.pool_evicted++; evict_oldest(); }
        const size_t start = head_;
        const uint32_t g2 = next_gen();
        size_t k = start;
        const bool batch = copier_ && mode_ == RingMode::Copy2;
        if (batch) ops_.clear();
        for (size_t i = 0; i < n; i++) {
            const uint32_t s = slots[i];
            if (s >= map_.pages() || stamp_[s] == g2) continue;
            stamp_[s] = g2;
            if (mode_ == RingMode::PagePtr) {                            // the free list holds >= d pages: pool_n_ - used_ >= d after the evictions above
                const uint32_t nw = free_.back(); free_.pop_back();
                std::memcpy(page(nw), mem + map_.addr_of(s), kPage);     // state at t
                pre_[k] = sp_[s]; sp_[s] = nw;                           // the old shadow page is the pre-image (state at t-1)
            } else if (batch) {
                uint8_t* sh = page(sp_[s]);
                ops_.push_back({page(pre_[k]), sh, sh, mem + map_.addr_of(s)});   // the same two copies, in the same order, run by the copier
            } else {
                uint8_t* sh = page(sp_[s]);
                std::memcpy(page(pre_[k]), sh, kPage);                   // pre-image (state at t-1)
                std::memcpy(sh, mem + map_.addr_of(s), kPage);           // state at t
            }
            pool_slot_[k] = s;
            k = k + 1 == pool_n_ ? 0 : k + 1;
        }
        if (batch && !ops_.empty()) copier_(copier_ctx_, ops_.data(), ops_.size());
        head_ = k; used_ += d;
        push(t, start, d, blob);
        return true;
    }

    // Roll mem back to the state of tick `to` (oldest() <= to <= newest()). pending = pages written since the last commit (write watch
    // output at the rollback point; they are restored from the shadow when no entry covers them). Entries newer than `to` are dropped,
    // so the next commit is to+1. blob_out (blob_bytes) receives tick `to`'s blob. False (nothing touched) when `to` is outside the window.
    bool rollback(uint32_t to, uint8_t* mem, const uint32_t* pending, size_t np, void* blob_out)
    {
        const size_t at = index_of(to);
        if (at == kNone) { st_.refused++; return false; }   // outside the window, or a tick that took no snapshot
        const uint32_t g = next_gen();
        uint32_t w = 0;
        const size_t keep = at + 1;                                 // entries 0..keep-1 stay
        const bool batch = copier_ && mode_ == RingMode::Copy2;    // every op below writes its own slot's mem and shadow page and reads a pool page or that shadow page
        if (batch) ops_.clear();
        for (size_t e = keep; e < count_; e++) {                    // oldest dropped entry first: its pre-image is the state at `to`
            const Entry& x = ent(e);
            for (uint32_t i = 0; i < x.n; i++) {
                const size_t k = (x.start + i) % pool_n_;
                const uint32_t s = pool_slot_[k];
                if (stamp_[s] == g) { if (mode_ == RingMode::PagePtr) free_.push_back(pre_[k]); continue; }   // a later pre-image of a page already restored
                stamp_[s] = g; w++;
                const uint8_t* old = page(pre_[k]);
                if (batch) { ops_.push_back({mem + map_.addr_of(s), old, page(sp_[s]), old}); continue; }
                std::memcpy(mem + map_.addr_of(s), old, kPage);
                if (mode_ == RingMode::PagePtr) { free_.push_back(sp_[s]); sp_[s] = pre_[k]; }   // the pre-image page is the shadow again
                else std::memcpy(page(sp_[s]), old, kPage);
            }
        }
        for (size_t i = 0; i < np; i++) {                           // written after the last commit, unchanged in to+1..newest
            const uint32_t s = pending[i];
            if (s >= map_.pages() || stamp_[s] == g) continue;
            stamp_[s] = g; w++;
            if (batch) { ops_.push_back({mem + map_.addr_of(s), page(sp_[s]), nullptr, nullptr}); continue; }
            std::memcpy(mem + map_.addr_of(s), page(sp_[s]), kPage);
        }
        if (batch && !ops_.empty()) copier_(copier_ctx_, ops_.data(), ops_.size());
        if (keep < count_) { head_ = ent(keep).start; for (size_t e = keep; e < count_; e++) used_ -= ent(e).n; }
        count_ = keep;
        if (blob_out && blob_n_) std::memcpy(blob_out, blob_ptr(keep - 1), blob_n_);
        st_.rollbacks++; st_.last_rollback_pages = w;
        return true;
    }

    // Pages of the saved set where the shadow differs from mem (a full compare: verification only). With no write since the last
    // commit/rollback this must be 0; anything else is a writer the write watch did not report.
    // `ignore`: byte ranges that a restore deliberately leaves alone (the preserved excluded bytes inside saved pages); they are not compared.
    size_t verify(const uint8_t* mem, const std::vector<Range>* ignore = nullptr) const
    {
        size_t d = 0;
        for (uint32_t s = 0; s < map_.pages(); s++) {
            const uint8_t* sh = page(sp_[s]); const uint32_t a = map_.addr_of(s);
            bool part = false;
            if (ignore) for (const Range& r : *ignore) if (r.a < a + kPage && end_of(r) > a) { part = true; break; }
            if (!part) { d += std::memcmp(sh, mem + a, kPage) != 0; continue; }
            uint8_t tmp[kPage]; std::memcpy(tmp, mem + a, kPage);
            for (const Range& r : *ignore) {
                const uint64_t lo = std::max<uint64_t>(r.a, a), hi = std::min<uint64_t>(end_of(r), (uint64_t)a + kPage);
                if (lo < hi) std::memcpy(tmp + (lo - a), sh + (lo - a), (size_t)(hi - lo));
            }
            d += std::memcmp(sh, tmp, kPage) != 0;
        }
        return d;
    }

    // Arena accounting (host tests; allocates, never call with threads frozen). PagePtr: every arena page is exactly one of free, an entry's pre-image or a
    // shadow page, and free + entry pages + shadow pages == arena. Copy2: the fixed layout (shadow s = page s, pool k = page pages + k).
    bool audit() const
    {
        const size_t np = map_.pages(), total = np + pool_n_;
        if (mode_ == RingMode::Copy2) {   // reset layout: shadow s = page s, pool k = page np0 + k; pages added by extend() follow the pool, in slot order
            for (size_t s = 0; s < np0_; s++) if (sp_[s] != s) return false;
            for (size_t s = np0_; s < np; s++) if (sp_[s] != np0_ + pool_n_ + (s - np0_)) return false;
            for (size_t k = 0; k < pool_n_; k++) if (pre_[k] != np0_ + k) return false;
            return free_.empty() && arena_.size() == total * kPage;
        }
        std::vector<uint8_t> seen(total, 0);
        auto mark = [&](uint32_t i) { if (i >= total || seen[i]) return false; seen[i] = 1; return true; };
        for (uint32_t i : free_) if (!mark(i)) return false;
        for (size_t s = 0; s < np; s++) if (!mark(sp_[s])) return false;
        size_t in_entries = 0;
        for (size_t e = 0; e < count_; e++) {
            const Entry& x = ent(e);
            for (uint32_t i = 0; i < x.n; i++) if (!mark(pre_[(x.start + i) % pool_n_])) return false;
            in_entries += x.n;
        }
        return in_entries == used_ && free_.size() + in_entries + np == total;
    }
    size_t free_pages() const { return mode_ == RingMode::PagePtr ? free_.size() : pool_n_ - used_; }

private:
    static constexpr size_t kNone = ~(size_t)0;
    // Entry index of tick t (entries are in ascending tick order): the direct position when no tick was skipped, else a search (at most cap_ entries).
    size_t index_of(uint32_t t) const
    {
        if (!valid_ || !count_ || t < oldest() || t > newest()) return kNone;
        const size_t d = (size_t)(t - oldest());
        if (d < count_ && ent(d).tick == t) return d;
        for (size_t e = 0; e < count_; e++) if (ent(e).tick == t) return e;
        return kNone;
    }
    struct Entry { uint32_t tick = 0; size_t start = 0; uint32_t n = 0; };
    const Entry& ent(size_t i) const { return ent_[(first_ + i) % cap_]; }
    Entry& ent(size_t i) { return ent_[(first_ + i) % cap_]; }
    uint8_t* page(uint32_t idx) { return arena_.data() + (size_t)idx * kPage; }
    const uint8_t* page(uint32_t idx) const { return arena_.data() + (size_t)idx * kPage; }
    uint8_t* blob_ptr(size_t i) { return blob_.data() + ((first_ + i) % cap_) * blob_n_; }
    uint32_t next_gen()
    {
        if (++gen_ == 0) { std::fill(stamp_.begin(), stamp_.end(), 0u); gen_ = 1; }
        return gen_;
    }
    void push(uint32_t t, size_t start, uint32_t n, const void* blob)
    {
        Entry& e = ent(count_);
        e.tick = t; e.start = start; e.n = n;
        if (blob_n_) { if (blob) std::memcpy(blob_ptr(count_), blob, blob_n_); else std::memset(blob_ptr(count_), 0, blob_n_); }
        count_++;
    }
    // PagePtr: an entry's pre-image pages go back to the free list (within its reserved capacity: no allocation).
    void release(const Entry& x)
    {
        if (mode_ != RingMode::PagePtr) return;
        for (uint32_t i = 0; i < x.n; i++) free_.push_back(pre_[(x.start + i) % pool_n_]);
    }
    void release_all() { for (size_t e = 0; e < count_; e++) release(ent(e)); }
    // Drops the oldest tick. The new oldest entry's pages are pre-images of the dropped tick, so nothing can use them any more: they are
    // freed too (they sit at the tail of the pool ring). Invariant after the first eviction: the oldest entry holds no pages.
    void evict_oldest()
    {
        release(ent(0));
        used_ -= ent(0).n;
        first_ = (first_ + 1) % cap_; count_--;
        if (count_) { release(ent(0)); used_ -= ent(0).n; ent(0).n = 0; }
        st_.evicted_entries++;
    }

    PageMap map_;
    RingMode mode_ = RingMode::Copy2;
    std::vector<uint8_t> arena_, blob_;          // arena: shadow + pool pages (indices below)
    std::vector<uint32_t> sp_, pre_, free_;     // sp_[slot]: the shadow page; pre_[k]: pool position k's pre-image page; free_: PagePtr free pages
    std::vector<uint32_t> pool_slot_, stamp_;
    std::vector<Entry> ent_;
    std::vector<PageCopy> ops_;                  // the batch handed to copier_ (capacity reserved at reset())
    CopyFn copier_ = nullptr; void* copier_ctx_ = nullptr;
    static constexpr size_t kGrowPages = 4096;   // spare arena capacity for extend() (16 MB)
    size_t np0_ = 0;                             // pages at reset()
    size_t cap_ = 1, pool_n_ = 1, blob_n_ = 0, count_ = 0, first_ = 0, head_ = 0, used_ = 0;
    uint32_t gen_ = 0;
    RingStats st_;
    bool valid_ = false;
};

}  // namespace jss
