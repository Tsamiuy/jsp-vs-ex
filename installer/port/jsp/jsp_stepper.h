// jsp_stepper.h -- the tick stepper control state machine. Pure; the Windows glue is jsp_stepper.cpp, the host test installer/tools/stepper_test.cpp
// runs this SAME code against a fake game and a fake snapshot ring.
//
// Vocabulary. `done` = the number of simulation ticks completed (the fight hash's g_done; a tick is one counted scene pass, @0x0014EA68).
// GekkoNet's frame numbers (read in gekkonet/src/game_session.cpp and event.cpp at the pinned commit):
//   Advance(F)  simulates frame F: needs done == F, afterwards done == F+1 (tick F+1 of the fight hash). Order in one Update: [RunaheadLoad] [Load(L)
//               Advance(L+1,rb) Save(L+1) ... Advance(cur-1,rb) Save(cur-1)] Advance(cur) Save(cur) [RunaheadSave(cur+1) Advance(cur+1,ahead)..]
//   Save(F)     comes AFTER Advance(F) with the same F (state after advancing F, i.e. done == F+1). The first Save of a session is Save(-1) = done 0.
//   Load(L)     returns to the state saved by Save(L) = done L+1; the replay then starts with Advance(L+1).
//   RunaheadSave(F) is a Save event taken BEFORE the run-ahead advances, with F = the frame about to run (done == F); RunaheadLoad(F) returns to it.
// FRAME BASE: GekkoNet frames always start at 0, a session starts at whatever `done` the fight is at (the hash counts from the anchor): begin(base, depth)
// maps GekkoNet frame F to done base+F+1 (after advancing F); Advance(F) needs done == base+F; Save(-1) is done == base.
// The control rules below are the part that is independent of the library; installer/tools/stepper_test.cpp drives them in exactly this event order.
//
// The main thread calls boundary(done) once per main-loop iteration, at the boundary the snapshot is taken at, and
// executes the returned op; it repeats the call after Commit and Rollback until it gets Run or None:
//   Commit(d)    the state with d completed ticks is a valid rollback target (the ring commits every tick; this marks the token, evicts old ones)
//   Rollback(d)  restore the ring to d completed ticks, the glue then continues with the events that follow (the replay)
//   Run(t,r)     let the simulation run tick t (r = a replay or hidden run-ahead tick: fast clock, no presentation, effects gated); the iteration proceeds.
//                Returned AGAIN, unchanged, while the granted tick has not completed (never None: that would starve the tick)
//   None         no tick may run in this iteration: the glue must NOT let the simulation advance (skip phases 1 and 2 and the pad update, as the 60 Hz
//                gate does on a render-only iteration) and keeps presenting
//   Error(e)     a protocol violation (see Err); the session must end with that reason, nothing is repaired silently
#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <set>
#include "jsp_step_input.h"

namespace jsx {

enum class Kind : uint8_t { Save, Load, Advance, RunaheadSave, RunaheadLoad };
struct Event {
    Kind kind = Kind::Advance;
    int32_t frame = 0;                         // GekkoNet frame (signed: the first Save is -1)
    bool rolling_back = false;                 // Advance: a replay tick (the netcode is catching up), not the live one
    bool running_ahead = false;                // Advance: a hidden run-ahead tick (state is loaded back before the next update)
    jsi::Pad in[jsi::kSlots];                  // Advance: the inputs of tick frame+1, one per slot
    bool confirmed[jsi::kSlots] = {};          // Advance: slot's input is confirmed (not predicted)
};

enum class OpKind : uint8_t { None, Commit, Rollback, Run, Error };
enum class Err : uint8_t { None, NoSession, SaveFrame, StaleToken, AdvanceFrame, TickLost, DepthExceeded, NoRunaheadSave, RunaheadOff };
struct Op { OpKind kind = OpKind::None; uint32_t tick = 0; bool replay = false; Err err = Err::None;
            bool regrant = false;   // Run only: this tick was granted before and has not completed. The glue must NOT poll the pad again (skip @0x003F7E2C): see Stepper::boundary
};

inline const char* err_name(Err e)
{
    switch (e) {
    case Err::None: return "none"; case Err::NoSession: return "no-session"; case Err::SaveFrame: return "save-frame-is-not-the-current-tick";
    case Err::StaleToken: return "load-of-a-tick-that-is-not-in-the-ring"; case Err::AdvanceFrame: return "advance-frame-is-not-the-current-tick";
    case Err::TickLost: return "the-simulation-ticked-without-permission-or-skipped"; case Err::NoRunaheadSave: return "runahead-load-without-its-save"; case Err::RunaheadOff: return "runahead-event-but-runahead-is-not-allowed"; case Err::DepthExceeded: return "rollback-deeper-than-the-ring";
    }
    return "?";
}

class Stepper {
public:
    // `depth` = the ring depth (JSP_SNAPSHOT_WW_DEPTH, default 10): the oldest tick Load may name is current - depth.
    void begin(uint32_t first_done, uint32_t depth)
    {
        started_ = true; base_ = first_done; cur_ = first_done; depth_ = depth; running_ = false; replay_ = false; queue_.clear(); saved_.clear(); ra_valid_ = false; st_ = Stats();
        table_.rewind(first_done);
    }
    void push(const Event& e) { queue_.push_back(e); }
    jsi::Table& table() { return table_; }
    uint32_t current() const { return cur_; }
    // Run-ahead is OFF unless allowed (default). GekkoNet runs a hidden live advance plus hidden ahead ticks and presents only a speculative display tick; the next
    // update loads back and runs that same tick live again, so it would be presented twice and its effects performed twice. Supporting it needs the effect ledger
    // to cover display ticks; not done, so a session with runahead_frames > 0 is refused (Err::RunaheadOff). The host test model enables it to keep the state logic covered.
    void allow_runahead(bool on) { allow_ra_ = on; }
    bool replaying() const { return replay_; }           // the fast clock / no-present / effect gate is wanted now
    size_t queued() const { return queue_.size(); }
    // ENHANCEMENT (rollback latency): the Commit just returned for `tick` took no snapshot (its state can never be loaded: Glue, final frame). It is no
    // rollback target: a Load naming it fails loudly (StaleToken) instead of restoring a state nobody saved.
    void forget(uint32_t tick) { saved_.erase(tick); }

    Op boundary(uint32_t done)
    {
        if (!started_) return err(Err::NoSession);
        if (running_) {
            if (done == running_tick_) { cur_ = done; running_ = false; st_.ticks++; if (running_replay_) st_.replayed++; }
            else if (done == cur_) {
                // The granted tick has not completed (a loading iteration, a pause): the permission STAYS, as a Run again. Returning None here would make the glue
                // skip phases 1/2 and the pad poll, and the tick could never complete (livelock). The inputs are already in the table; serve() hands a slot its packet
                // once per tick, so a re-run reads the same value. PAD EDGES: a re-granted iteration must not call pad_update (@0x003F7E2C) again: the listener does
                // prev = cur, press = cur & ~prev (so a press is zeroed by the second call) and the hold / key-repeat counters count CALLS ,
                // so presses would be lost and hold/repeat would depend on how many times the iteration stalled (wall clock). Hence `regrant`: the glue skips the pad
                // poll (skip_pad) rather than latching edges, because skipping leaves the guest's pad state exactly as the first poll of the tick left it.
                st_.stalled++;
                Op r = op(OpKind::Run, running_tick_, running_replay_); r.regrant = true;   // the pad must not be polled again: see the comment above
                return r;
            } else return err(Err::TickLost);
        } else if (done != cur_) return err(Err::TickLost);
        if (queue_.empty()) { replay_ = false; return Op(); }
        const Event e = queue_.front();
        switch (e.kind) {
        case Kind::Save: {                                // state AFTER advancing frame F = done F+1
            const int64_t d = (int64_t)base_ + e.frame + 1;
            if (d != (int64_t)cur_) return err(Err::SaveFrame);
            queue_.pop_front();
            saved_.insert(cur_);
            while (!saved_.empty() && *saved_.begin() + depth_ < cur_) saved_.erase(saved_.begin());
            st_.saves++;
            return op(OpKind::Commit, cur_, false);
        }
        case Kind::Load: {                                // back to the state Save(L) took: done L+1
            const int64_t d = (int64_t)base_ + e.frame + 1;
            if (d < 0 || !saved_.count((uint32_t)d)) return err(d + (int64_t)depth_ < (int64_t)cur_ ? Err::DepthExceeded : Err::StaleToken);
            queue_.pop_front();
            return rollback_to((uint32_t)d, true);
        }
        case Kind::RunaheadSave:                          // state BEFORE frame F runs = done base+F
            if (!allow_ra_) return err(Err::RunaheadOff);
            if ((int64_t)base_ + e.frame != (int64_t)cur_) return err(Err::SaveFrame);
            queue_.pop_front();
            ra_valid_ = true; ra_done_ = cur_; saved_.insert(cur_);
            return op(OpKind::Commit, cur_, false);
        case Kind::RunaheadLoad:
            if (!allow_ra_) return err(Err::RunaheadOff);
            if (!ra_valid_ || (int64_t)base_ + e.frame != (int64_t)ra_done_) return err(Err::NoRunaheadSave);
            queue_.pop_front();
            ra_valid_ = false;
            return rollback_to(ra_done_, false);          // back to the live state: the hidden ticks are not a replay of anything
        case Kind::Advance:
            if ((int64_t)base_ + e.frame != (int64_t)cur_) return err(Err::AdvanceFrame);
            if (e.running_ahead && !allow_ra_) return err(Err::RunaheadOff);
            queue_.pop_front();
            for (int s = 0; s < jsi::kSlots; s++) table_.set(cur_ + 1, s, e.in[s], e.confirmed[s]);
            running_ = true; running_tick_ = cur_ + 1; running_replay_ = e.rolling_back || e.running_ahead; replay_ = running_replay_;
            return op(OpKind::Run, running_tick_, running_replay_);
        }
        return Op();
    }

    struct Stats { uint64_t ticks = 0, replayed = 0, saves = 0, rollbacks = 0, rollback_ticks = 0, stalled = 0; uint32_t max_rollback = 0; };
    const Stats& stats() const { return st_; }

private:
    Op rollback_to(uint32_t d, bool is_replay)
    {
        st_.rollbacks++; st_.rollback_ticks += cur_ - d; st_.max_rollback = std::max(st_.max_rollback, cur_ - d);
        saved_.erase(saved_.upper_bound(d), saved_.end());   // later states are gone: the replay re-saves them
        table_.rewind(d);
        cur_ = d; replay_ = is_replay;
        return op(OpKind::Rollback, d, is_replay);
    }
    static Op op(OpKind k, uint32_t t, bool r) { Op o; o.kind = k; o.tick = t; o.replay = r; return o; }
    Op err(Err e) { Op o; o.kind = OpKind::Error; o.err = e; return o; }
    bool allow_ra_ = false;
    uint32_t base_ = 0;
    bool started_ = false, running_ = false, running_replay_ = false, replay_ = false, ra_valid_ = false;
    uint32_t cur_ = 0, depth_ = 10, running_tick_ = 0, ra_done_ = 0;
    std::deque<Event> queue_;
    std::set<uint32_t> saved_;
    jsi::Table table_;
    Stats st_;
};

}  // namespace jsx
