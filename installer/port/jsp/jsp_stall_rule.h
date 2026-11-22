// jsp_stall_rule.h -- FIX (rollback freeze: a flip the FIFO walker never reaches). Pure and host-tested (installer/tools/stall_rule_test.sh);
// the Windows glue is jsp_stepper.cpp (lockstep_pass and the replay clock), the write-off is toolkit patch 7927 (cellGcm_flip_write_off_lost).
//
// Two live freezes on one tester's PC (0.7.0-test1 room 37, 0.7.0-test3 room 51, both hosting) ended the same way: flip status WAITING,
// flips queued = reached + 1 (= completed + 1), the main thread in the frame pacing step 1 (func_006507DC polls cellGcmGetFlipStatus with a
// 200 us sleep: lr 0x00650820 / 0x00650854), no flip for good. The lockstep clock gives a vblank only with a flip or to a guest in step 3, so
// nothing could move the guest again; the main thread never reached the tick boundary, the session stopped sending, and the peer saw
// "Opponent disconnected" ~6 s later. The joiner's census at the same time had every other guest thread in the same waits.
//
// The rule: when that exact shape (one flip queued and not reached, nothing else outstanding, a guest thread provably in step 1) has held
// for kWriteOffMs, the frame clock asks the toolkit to write the flip off (it does so only with the walker idle at put: nothing left that
// could still reach it), once per queued flip. The guest is blocked on exactly that flip, so completing it is what the walker reaching the
// marker would have done at the same guest position: the other PC's guest saw the same flip complete, with the same 2 vblanks (the lockstep
// clock takes it as usual). Only WHEN differs, and the picture of that frame (whatever was not drained is not drawn).
// Anything else with no flip keeps waiting (and is logged); after kFailMs without a flip the match is declared failed (lockstep_failed),
// the same end as a vblank the guest never takes.
// FIX (review of PR 248, the freeze timeout): (1) the no-flip deadline starts when the lockstep clock starts (start()): before the first live flip the
// last-flip time is 0, and a guest that never flipped at all was never failed. (2) The failure must reach an owner that stays responsive: the flag was
// consumed only at the tick boundary on the game thread, which a guest stalled for good never reaches. Now the rollback layer's pump thread ends the
// session as soon as the flag is set (jsp_net_game.cpp pump_tick: the peer is told at once, the keepalive stops answering), and if no flip came for
// kReleaseMs after the failure the frame clock itself hands the guest back to the normal frame clock (release_due), so the game thread can reach its
// boundary and end the match locally. Both owners act on the frame clock's own state; nothing waits on the stalled thread.
#pragma once
#include <cstdint>

namespace jsr {

constexpr uint64_t kWriteOffMs = 1000;   // the lost-flip shape held this long (a normal frame flips within ~35 ms; a cold drain keeps get != put)
constexpr uint64_t kFailMs = 60000;      // no flip at all for this long, whatever the shape: the match must end
constexpr uint64_t kReleaseMs = 5000;    // a failure no flip followed for this long: the lockstep clock lets go of the guest (the session is already ended)

// One sample of the frame clock (no flip outstanding that the drain reached, i.e. the "no flip" branch).
struct Sample {
    uint64_t now_ms = 0;
    uint64_t last_flip_ms = 0;   // when the last flip was taken
    uint32_t status = 0;         // flip status (1 = WAITING)
    uint32_t queued = 0, reached = 0, completed = 0, direct = 0;
    bool main_in_step1 = false;  // a guest thread in an HLE wait at lr 0x00650820 or 0x00650854 (func_006507DC step 1)
};

enum class Act { None, WriteOff, Fail };

struct State {
    uint64_t shape_since = 0;    // 0 = the shape does not hold
    uint32_t shape_queued = 0;   // the queued count it holds for
    uint32_t tried_queued = 0;   // the queued count a write-off was last asked for
    uint64_t tried_at = 0;       // when it was last asked (a refused write-off is asked again every kWriteOffMs)
    bool tried = false;
    bool failed = false;
    uint64_t started_ms = 0;     // FIX (review of PR 248): when the lockstep clock started (start()); the no-flip deadline counts from here until a flip
    uint64_t failed_ms = 0;      // when this stall failed (0 = not failed): release_due counts from here
};

// FIX (review of PR 248): the lockstep clock starts (a match): a fresh state whose no-flip deadline runs from now.
inline void start(State& st, uint64_t now_ms) { st = State{}; st.started_ms = now_ms ? now_ms : 1; }

// A failure found elsewhere (the vblank acknowledgement wait, kAckFailMs): the same release timer.
inline void note_fail(State& st, uint64_t now_ms) { if (!st.failed) { st.failed = true; st.failed_ms = now_ms ? now_ms : 1; } }

// FIX (review of PR 248): the stall failed and still no flip came kReleaseMs later: the frame clock lets go of the guest.
inline bool release_due(const State& st, uint64_t now_ms) { return st.failed && st.failed_ms && now_ms - st.failed_ms >= kReleaseMs; }

inline bool lost_flip_shape(const Sample& s)
{
    return s.status == 1 && s.direct == 0 && s.queued == s.reached + 1u && s.reached == s.completed && s.main_in_step1;
}

// Call on every "no flip" pass. WriteOff: ask the toolkit now (it may refuse: the walker not idle); Fail: once, the match must end.
inline Act step(State& st, const Sample& s)
{
    if (lost_flip_shape(s)) {
        if (!st.shape_since || st.shape_queued != s.queued) { st.shape_since = s.now_ms ? s.now_ms : 1; st.shape_queued = s.queued; }
        if (s.now_ms - st.shape_since >= kWriteOffMs && !(st.tried && st.tried_queued == s.queued && s.now_ms - st.tried_at < kWriteOffMs)) {
            st.tried = true; st.tried_queued = s.queued; st.tried_at = s.now_ms;
            return Act::WriteOff;
        }
    } else {
        st.shape_since = 0;
    }
    // FIX (review of PR 248): from the later of the last flip and the clock's start (a guest that has not flipped since the start fails too)
    const uint64_t since = s.last_flip_ms > st.started_ms ? s.last_flip_ms : st.started_ms;
    if (!st.failed && since && s.now_ms >= since && s.now_ms - since >= kFailMs) { st.failed = true; st.failed_ms = s.now_ms ? s.now_ms : 1; return Act::Fail; }
    return Act::None;
}

// A flip was taken: the next stall starts from scratch (a write-off that was refused may be asked again for a later flip).
inline void flip_taken(State& st) { st.shape_since = 0; st.failed = false; st.failed_ms = 0; }

}  // namespace jsr
