// Host test of two rollback snapshot helpers (review of the online rollback PR):
//  1. jsp_thread_bit.h: guest thread ids 1..64 -> mask bit t - 1 (the helper-thread register allowlist / save / restore of jsp_net_game.cpp, the
//     park roles of jsp_snapshot_sync.h, ppu_alive_mask of jsp_snapshot.cpp). Ids 1, 63 and 64, every id once, and ids outside 1..64.
//  2. jsp_audit_start.h: the background thread-registry audit. A worker start that fails runs that scheduled audit synchronously and is tried again
//     at the next scheduled audit; no scheduled audit is ever skipped (the old code skipped every audit after one failed CreateThread).
// Run: installer/tools/thread_audit_test.sh (builds with -fsanitize=undefined where available: a shift by 64 fails the run).
#include "../port/jsp/jsp_thread_bit.h"
#include "../port/jsp/jsp_audit_start.h"
#include <cstdio>
#include <vector>
static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

// The register save / restore of jsp_net_game.cpp (fs_find_threads, regs_save, regs_restore), reduced to its masks.
struct Regs { uint64_t allow = 0, mask = 0; };
static void allow_threads(Regs& r, const std::vector<uint32_t>& cri) { r.allow = 0; for (uint32_t t : cri) r.allow |= jtb::thread_bit(t); }
static void save(Regs& r, const bool alive[65]) { r.mask = 0; for (uint32_t t = 1; t < 65; t++) { if (!jtb::has_thread(r.allow, t) || !alive[t]) continue; r.mask |= jtb::thread_bit(t); } }
static std::vector<uint32_t> restore(const Regs& r) { std::vector<uint32_t> out; for (uint32_t t = 1; t < 65; t++) if (jtb::has_thread(r.mask, t)) out.push_back(t); return out; }

static void test_thread_bit()
{
    CHECK(jtb::thread_bit(1) == 0x1ull);
    CHECK(jtb::thread_bit(2) == 0x2ull);
    CHECK(jtb::thread_bit(63) == 0x4000000000000000ull);
    CHECK(jtb::thread_bit(64) == 0x8000000000000000ull);
    for (uint32_t bad : {0u, 65u, 66u, 128u, 0x7FFFFFFFu, 0xFFFFFFFFu, (uint32_t)-1, (uint32_t)-64}) {
        CHECK(jtb::thread_bit(bad) == 0); CHECK(!jtb::valid_tid(bad)); CHECK(!jtb::has_thread(~0ull, bad));
    }
    uint64_t all = 0;
    for (uint32_t t = 1; t <= 64; t++) { const uint64_t b = jtb::thread_bit(t); CHECK(b != 0); CHECK((b & (b - 1)) == 0); CHECK((all & b) == 0); all |= b; CHECK(jtb::has_thread(all, t)); }
    CHECK(all == ~0ull);
    for (uint32_t t = 1; t <= 64; t++) CHECK(jtb::has_thread(jtb::thread_bit(t), t) && !jtb::has_thread(~jtb::thread_bit(t), t));

    // allowlist -> save -> restore with the edge ids; out-of-range ids in the allowlist change nothing
    Regs r; bool alive[65] = {};
    for (uint32_t t = 1; t < 65; t++) alive[t] = true;
    allow_threads(r, {1, 17, 63, 64, 0, 65, 0xFFFFFFFFu});
    CHECK(r.allow == (0x1ull | (1ull << 16) | 0x4000000000000000ull | 0x8000000000000000ull));
    save(r, alive);
    CHECK(r.mask == r.allow);
    CHECK((restore(r) == std::vector<uint32_t>{1, 17, 63, 64}));
    alive[64] = false; alive[1] = false;                 // threads 1 and 64 ended: not saved, not written back
    save(r, alive);
    CHECK((restore(r) == std::vector<uint32_t>{17, 63}));
    allow_threads(r, {64}); for (uint32_t t = 1; t < 65; t++) alive[t] = true;
    save(r, alive);
    CHECK((restore(r) == std::vector<uint32_t>{64}));     // the old 1ull << t: thread 64 never restored (undefined shift), thread 63 took bit 63
}

// Scheduled audits with a worker start that fails `fail_first` times (or always): every audit is covered, by the worker or synchronously.
static void run_audits(int audits, int fail_first, bool always_fail)
{
    jau::Starter s; int start_calls = 0, sync = 0, background = 0, worker_runs = 0;
    auto start = [&] { start_calls++; if (always_fail || start_calls <= fail_first) return false; worker_runs++; return true; };
    for (int i = 0; i < audits; i++) {
        const jau::Path p = s.at_audit(start);
        if (p == jau::Path::Sync) sync++; else background++;
    }
    const int expect_fail = always_fail ? audits : (fail_first < audits ? fail_first : audits);
    CHECK(sync + background == audits);                  // no scheduled audit skipped
    CHECK(sync == expect_fail);                          // each failed start: that audit ran on the game thread
    CHECK((int)s.failures == expect_fail && (int)s.sync_runs == expect_fail);
    CHECK(start_calls == (always_fail ? audits : (fail_first < audits ? fail_first + 1 : audits)));   // tried again at every audit until it ran, then never
    CHECK(worker_runs == (always_fail || fail_first >= audits ? 0 : 1));   // one worker at most
    CHECK(s.running == (worker_runs == 1));
}

static void test_audit_start()
{
    run_audits(10, 0, false);    // starts at once
    run_audits(10, 1, false);    // the reviewed case: one CreateThread failure
    run_audits(10, 3, false);
    run_audits(10, 9, false);
    run_audits(10, 10, false);
    run_audits(50, 0, true);     // never starts: every audit synchronous
    // first audit after one failure: the sync path, then the worker
    jau::Starter s; int n = 0;
    CHECK(s.at_audit([&] { return ++n > 1; }) == jau::Path::Sync);
    CHECK(s.at_audit([&] { return ++n > 1; }) == jau::Path::Background);
    CHECK(s.at_audit([&] { n += 100; return false; }) == jau::Path::Background && n == 2);   // running: start not called again
}

int main()
{
    test_thread_bit();
    test_audit_start();
    std::printf("thread_audit_test: %d checks, %d failed\n", checks, fails);
    std::printf("%s\n", fails ? "RESULT FAIL" : "RESULT ok");
    return fails ? 1 : 0;
}
