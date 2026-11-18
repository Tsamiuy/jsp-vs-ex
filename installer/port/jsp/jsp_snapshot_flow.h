// jsp_snapshot_flow.h -- the per-tick decision flow of the incremental (write-watch) snapshot mode, as a pure template over an environment.
// jsp_snapshot.cpp instantiates it with the Windows glue (ring, threads, logging); a host test can instantiate the SAME code with a fake
// environment, so the round bookkeeping (abort, divergence, the end of the run) is tested as wired, not as a model.
//
// Rules:
//   * a layout change (committed set differs from the saved layout) seen in PASS 1 re-baselines and the round is ABORTED (no verdict);
//   * a layout change seen in PASS 2 (the replay), at the rollback request, or on the tick that ends the last round (after the verdict) is a replay
//     divergence: that round is a MISMATCH (layout_diverged), never ABORTED and never EQUAL, and its verdict is withdrawn;
//   * when the driver finishes (including when the last round was just aborted or diverged) the run is reported and cleaned up exactly once.
// Env interface (all return false on a fatal failure that already ran capture_failed):
//   bool on();  bool ring_valid();  bool layout_changed(k)   (logs the diff)   bool baseline(k)  bool rebaseline(k)
//   bool commit(k)  bool rollback(k, to)  void saved(k)  (a round starts at k: save bookkeeping)  void restored()  void flush_rounds()  void finish()
//   void note(const char*, k)  void rng(const char* what, k)
#pragma once
#include <cstdint>
#include "jsp_snapshot_core.h"

namespace jsf {

template<class Env>
void ww_pass_end(Env& e, jss::Driver& d, uint32_t k, const jss::Rec& rec)
{
    if (!e.on()) return;
    if (d.finished()) { e.finish(); return; }
    const bool was_pass2 = d.in_pass2();
    const bool was_gap = d.in_gap();   // soak: between two rounds (this tick may be the next round's save tick)
    const size_t round_before = d.round();
    const jss::Act a = d.on_tick(k, rec);
    const bool closed = d.round() > round_before;   // this tick ended a round (with a soak gap the answer is None, not Save: the round count is what says so)
    const bool ended = d.finished();
    if (a == jss::Act::Save && !e.ring_valid() && !ended) { if (e.baseline(k)) e.saved(k); e.flush_rounds(); return; }
    if (!e.ring_valid()) { e.flush_rounds(); if (ended) e.finish(); return; }
    // the layout check runs on every tick, including the one that ends the last round
    const bool changed = e.layout_changed(k);
    if (!e.on()) return;                               // discovery failed: capture_failed ran
    if (changed) {
        jss::Act next = jss::Act::None;
        if (was_gap) {                                   // soak gap (or the save tick that ends it): no round is in progress, nothing to abort
            e.note("layout changed between soak rounds: re-baselined", k);
            next = a;
        } else if (a == jss::Act::Restore) {                    // found at the rollback request: pass 1 ran across the change (no replay ran yet): growth, not divergence
            e.note("layout changed at the rollback request: re-baselined, round aborted, nothing rolled back", k);
            next = d.abort_round(k);
        } else if (was_pass2 && closed) {   // the tick that ended a round: the verdict was computed, now withdrawn
            e.note("layout changed on the tick that ended the replay: verdict withdrawn (divergence)", k);
            d.downgrade_previous(k);
            next = a;                                    // a new round (if any) starts here from the new baseline
        } else if (was_pass2) {
            e.note("layout changed during the replay: divergence", k);
            next = d.diverge_round(k);
        } else {
            e.note("layout changed inside the window (pass 1): re-baselined, round aborted", k);
            next = d.abort_round(k);
        }
        if (!d.finished()) {
            if (!e.rebaseline(k)) { e.flush_rounds(); return; }
            if (next == jss::Act::Save) e.saved(k);
        }
        e.flush_rounds();
        if (d.finished()) e.finish();
        return;
    }
    if (a == jss::Act::Restore) { if (e.rollback(k, d.rewind_to())) e.restored(); e.flush_rounds(); return; }
    if (ended) { e.flush_rounds(); e.finish(); return; }
    if (a == jss::Act::Save) e.saved(k);               // the next round starts here; the ring already holds this tick
    if (!e.commit(k)) { e.flush_rounds(); return; }
    e.rng(a == jss::Act::Save ? "save" : "tick", k);
    e.flush_rounds();
}

}  // namespace jsf
