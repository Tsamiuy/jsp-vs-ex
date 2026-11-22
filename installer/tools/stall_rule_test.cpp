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
    if (g_fail) { std::printf("stall_rule_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("stall_rule_test: all checks passed\n");
    return 0;
}
