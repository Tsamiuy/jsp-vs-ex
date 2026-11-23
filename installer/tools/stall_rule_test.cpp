// Host test of installer/port/jsp/jsp_stall_rule.h (the lost-flip write-off rule of the rollback frame clock). g++ only.
#include "jsp_stall_rule.h"
#include <cstdio>
#include <cstdlib>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static jsr::Sample lost(uint64_t now, uint64_t last_flip, uint32_t q = 6849)
{
    jsr::Sample s; s.now_ms = now; s.last_flip_ms = last_flip; s.status = 1; s.queued = q; s.reached = q - 1; s.completed = q - 1; s.main_in_step1 = true;
    return s;
}

int main()
{
    using jsr::Act;
    {   // the room-51 shape: written off once after kWriteOffMs, never twice for the same flip
        jsr::State st;
        CHECK(jsr::step(st, lost(1000, 900)) == Act::None);
        CHECK(jsr::step(st, lost(1999, 900)) == Act::None);
        CHECK(jsr::step(st, lost(2000, 900)) == Act::WriteOff);
        CHECK(jsr::step(st, lost(2500, 900)) == Act::None);   // refused (walker not idle): not asked again within kWriteOffMs
        CHECK(jsr::step(st, lost(2999, 900)) == Act::None);
        CHECK(jsr::step(st, lost(3000, 900)) == Act::WriteOff);   // asked again once per kWriteOffMs while the same flip stays lost
        CHECK(jsr::step(st, lost(3500, 900)) == Act::None);
    }
    {   // a normal frame: the flip is reached quickly (queued == reached), or the main thread is not in step 1: never written off
        jsr::State st;
        jsr::Sample s = lost(5000, 0); s.reached = s.queued; s.completed = s.queued - 1;
        CHECK(jsr::step(st, s) == Act::None);
        jsr::Sample t = lost(5000, 0); t.main_in_step1 = false;
        for (uint64_t ms = 5000; ms < 9000; ms += 10) { t.now_ms = ms; CHECK(jsr::step(st, t) == Act::None); }
        jsr::Sample u = lost(9000, 0); u.direct = 1;          // a direct flip is outstanding: it brings its own completion
        CHECK(jsr::step(st, u) == Act::None);
        jsr::Sample v = lost(9000, 0); v.status = 0;          // DONE: nothing outstanding
        CHECK(jsr::step(st, v) == Act::None);
        jsr::Sample w = lost(9000, 0); w.queued += 1;         // two flips ahead of the walker: a backlog, not a lost flip
        CHECK(jsr::step(st, w) == Act::None);
    }
    {   // the shape must hold continuously: a break restarts the timer
        jsr::State st;
        CHECK(jsr::step(st, lost(100, 50)) == Act::None);
        jsr::Sample b = lost(900, 50); b.main_in_step1 = false;
        CHECK(jsr::step(st, b) == Act::None);
        CHECK(jsr::step(st, lost(1050, 50)) == Act::None);    // 150 ms since the restart, not 950
        CHECK(jsr::step(st, lost(2049, 50)) == Act::None);
        CHECK(jsr::step(st, lost(2050, 50)) == Act::WriteOff);
    }
    {   // a later flip that is lost again is written off again
        jsr::State st;
        CHECK(jsr::step(st, lost(0 + 1, 0, 100)) == Act::None);
        CHECK(jsr::step(st, lost(1001, 0, 100)) == Act::WriteOff);
        jsr::flip_taken(st);
        CHECK(jsr::step(st, lost(5000, 4990, 101)) == Act::None);
        CHECK(jsr::step(st, lost(6000, 4990, 101)) == Act::WriteOff);
    }
    {   // anything else without a flip for kFailMs: Fail once
        jsr::State st;
        jsr::Sample s; s.status = 1; s.queued = s.reached = s.completed = 10; s.last_flip_ms = 1000;
        s.now_ms = 1000 + jsr::kFailMs - 1; CHECK(jsr::step(st, s) == Act::None);
        s.now_ms = 1000 + jsr::kFailMs;     CHECK(jsr::step(st, s) == Act::Fail);
        s.now_ms += 5000;                   CHECK(jsr::step(st, s) == Act::None);
    }
    // FIX (review of PR 248, the freeze timeout). (1) The deadline starts with the lockstep clock: a guest that never flipped in this match fails too.
    {   // the old gap: no start, last flip 0 -> never failed
        jsr::State st;
        jsr::Sample s; s.status = 1; s.queued = s.reached = s.completed = 0; s.last_flip_ms = 0;
        for (uint64_t ms = 1000; ms <= 1000 + 3 * jsr::kFailMs; ms += 500) { s.now_ms = ms; CHECK(jsr::step(st, s) == Act::None); }
    }
    {   // with start(): Fail exactly kFailMs after the start, once
        jsr::State st; jsr::start(st, 20000);
        jsr::Sample s; s.status = 1; s.queued = s.reached = s.completed = 0; s.last_flip_ms = 0;
        s.now_ms = 20000 + jsr::kFailMs - 1; CHECK(jsr::step(st, s) == Act::None);
        s.now_ms = 20000 + jsr::kFailMs;     CHECK(jsr::step(st, s) == Act::Fail);
        s.now_ms += 1000;                    CHECK(jsr::step(st, s) == Act::None);
    }
    {   // a flip after the start moves the deadline to that flip; a last flip from before the start (another match's clock) does not count
        jsr::State st; jsr::start(st, 20000);
        jsr::Sample s; s.status = 1; s.queued = s.reached = s.completed = 5; s.last_flip_ms = 50000;
        s.now_ms = 20000 + jsr::kFailMs + 10; CHECK(jsr::step(st, s) == Act::None);   // 30 s after the flip
        s.now_ms = 50000 + jsr::kFailMs;      CHECK(jsr::step(st, s) == Act::Fail);
        jsr::State st2; jsr::start(st2, 20000);
        s.last_flip_ms = 1000;   // older than the start
        s.now_ms = 1000 + jsr::kFailMs;       CHECK(jsr::step(st2, s) == Act::None);
        s.now_ms = 20000 + jsr::kFailMs;      CHECK(jsr::step(st2, s) == Act::Fail);
    }
    {   // a sample older than the start never underflows into a failure
        jsr::State st; jsr::start(st, 90000);
        jsr::Sample s; s.status = 1; s.now_ms = 80000; CHECK(jsr::step(st, s) == Act::None);
    }
    // (2) The owner that stays responsive: after a failure with no flip, release_due turns true kReleaseMs later; a flip or a new start clears it.
    {
        jsr::State st; jsr::start(st, 1000);
        jsr::Sample s; s.status = 1; s.last_flip_ms = 0;
        CHECK(!jsr::release_due(st, 1000 + jsr::kFailMs + jsr::kReleaseMs));   // not failed yet
        s.now_ms = 1000 + jsr::kFailMs; CHECK(jsr::step(st, s) == Act::Fail);
        CHECK(!jsr::release_due(st, s.now_ms));
        CHECK(!jsr::release_due(st, s.now_ms + jsr::kReleaseMs - 1));
        CHECK(jsr::release_due(st, s.now_ms + jsr::kReleaseMs));
        jsr::flip_taken(st);   // the guest flipped after all: nothing to let go of
        CHECK(!jsr::release_due(st, s.now_ms + 10 * jsr::kReleaseMs));
        jsr::State st2; jsr::start(st2, 1000); s.now_ms = 1000 + jsr::kFailMs; CHECK(jsr::step(st2, s) == Act::Fail);
        jsr::start(st2, s.now_ms + 1);   // the next match
        CHECK(!st2.failed && !jsr::release_due(st2, s.now_ms + 10 * jsr::kReleaseMs));
    }
    {   // the vblank-wait failure (kAckFailMs) uses the same timer; a second note does not restart it
        jsr::State st; jsr::start(st, 1000);
        jsr::note_fail(st, 70000);
        jsr::note_fail(st, 72000);
        CHECK(!jsr::release_due(st, 70000 + jsr::kReleaseMs - 1));
        CHECK(jsr::release_due(st, 70000 + jsr::kReleaseMs));
        jsr::Sample s; s.status = 1; s.now_ms = 1000 + jsr::kFailMs; CHECK(jsr::step(st, s) == Act::None);   // already failed: no second Fail
    }
    {   // the frame clock thread alone, a guest stalled for good from the clock's start (no flip, no boundary ever again): the pass runs every 20 ms and
        // asks the rule only after 500 ms without a flip (lockstep_pass); Fail comes once at kFailMs, the release kReleaseMs after it, and nothing else
        // has to run for either (the pump thread's end of the session reads the same failure flag the Fail sets)
        jsr::State st; const uint64_t t0 = 5000; jsr::start(st, t0);
        uint64_t fail_at = 0, release_at = 0; int fails = 0;
        for (uint64_t ms = t0; ms <= t0 + jsr::kFailMs + 2 * jsr::kReleaseMs; ms += 20) {
            if (!release_at && jsr::release_due(st, ms)) release_at = ms;
            if (ms - t0 <= 500) continue;
            jsr::Sample s; s.now_ms = ms; s.last_flip_ms = 0; s.status = 1; s.queued = s.reached = s.completed = 0;
            if (jsr::step(st, s) == Act::Fail) { fails++; fail_at = ms; }
        }
        CHECK(fails == 1 && fail_at == t0 + jsr::kFailMs);
        CHECK(release_at == fail_at + jsr::kReleaseMs);
    }
    {   // the lost-flip shape from the start: written off after kWriteOffMs as before, and if the write-off keeps being refused, Fail at kFailMs
        jsr::State st; jsr::start(st, 1000);
        CHECK(jsr::step(st, lost(1000, 0, 7)) == Act::None);
        CHECK(jsr::step(st, lost(2000, 0, 7)) == Act::WriteOff);
        CHECK(jsr::step(st, lost(1000 + jsr::kFailMs, 0, 7)) == Act::WriteOff);   // the write-off is asked again first
        CHECK(jsr::step(st, lost(1000 + jsr::kFailMs + 1, 0, 7)) == Act::Fail);
    }
    if (g_fail) { std::printf("stall_rule_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("stall_rule_test: all checks passed\n");
    return 0;
}
