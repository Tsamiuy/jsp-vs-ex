// jsp_net_ko_rule.h -- the online match's KO / time-up end rules (pure: no game, no session). jsp_net_game.cpp keeps one Ring and one Watermark per
// match; the host test installer/tools/net_session_test.cpp drives the same classes through a real two-process GekkoNet session (--ko-from, --mute-at).
//
// FIX (a missed KO): the winner of EVERY saved tick (commit and hash_only, predicted or not) is recorded, keyed by tick; a re-save after a rollback
// overwrites its entry and a rollback drops the entries after its target. The boundary ends the match on the earliest recorded winner whose tick is
// confirmed (its save frame is below GekkoNet's final frame). Both PCs confirm the same ticks with the same inputs, so they end on the same tick.
//
// FIX (review of PR 248, a predicted result became a completed match on a disconnect): the Disconnected path used to end the match as "Match over" on ANY
// recorded winner, predicted or not. A predicted KO is the guess of this PC (the other player's last input repeated), and once GekkoNet marks a peer
// disconnected it fills that peer's slots with neutral input and confirms them, so its final frame then covers ticks no peer ever played. Now a
// result survives a disconnect only when its tick is covered by the LIVE confirmation watermark: the highest final frame read at a boundary whose
// session events showed every peer still connected (so it was confirmed by real inputs of every peer). It is read before the Disconnected event of
// the same boundary is handled and never moves after it. Anything above it ends as a disconnect.
#pragma once
#include <cstdint>
#include <string>

namespace jkr {

struct Rec { uint32_t tick = 0; int w = 0; };   // w: 1 red wins, 2 blue wins, 3 time up; 0 = no winner on that tick
constexpr int kRing = 128;                      // far more than the rollback window plus the confirmation lag

class Ring {
public:
    void clear() { for (Rec& r : r_) r = Rec(); }
    // the save of `tick` (absolute tick number) holds winner w (<= 0: none; a re-save clears an older entry of the same tick)
    void record(uint32_t tick, int w) { Rec& r = r_[tick % kRing]; r.tick = tick; r.w = w > 0 ? w : 0; }
    // a rollback to `tick`: the ticks after it run again, their re-saves record them anew
    void drop_after(uint32_t tick) { for (Rec& r : r_) if (r.tick > tick) r = Rec(); }
    // The earliest recorded winner whose save frame (tick - base - 1) is below `ff`, i.e. a frame GekkoNet treats as final. 0 = none; *at = its tick.
    int earliest_covered(uint32_t base, int ff, uint32_t* at) const
    {
        uint32_t best = 0; int bw = 0;
        for (const Rec& r : r_)
            if (r.w > 0 && (int)(r.tick - base) - 1 < ff && (!bw || r.tick < best)) { best = r.tick; bw = r.w; }
        if (at) *at = best;
        return bw;
    }
    // The earliest recorded winner of any tick, predicted or not: the OLD disconnect rule. Diagnostics and the host test only, never an end.
    int earliest_any(uint32_t* at) const
    {
        uint32_t best = 0; int bw = 0;
        for (const Rec& r : r_) if (r.w > 0 && (!bw || r.tick < best)) { best = r.tick; bw = r.w; }
        if (at) *at = best;
        return bw;
    }
    // recorded winners NOT covered by final frame ff (predicted results): logged when a disconnect ignores them
    int uncovered(uint32_t base, int ff) const
    {
        int n = 0;
        for (const Rec& r : r_) if (r.w > 0 && !((int)(r.tick - base) - 1 < ff)) n++;
        return n;
    }
private:
    Rec r_[kRing];
};

// The live confirmation watermark (see the header). live(): a boundary whose events had no Disconnected (call it after the events were taken);
// lost(): a peer was reported disconnected, the watermark is frozen from then on. -1 = nothing confirmed live yet.
struct Watermark {
    int ff = -1;
    bool frozen = false;
    void live(int final_frame) { if (!frozen && final_frame > ff) ff = final_frame; }
    void lost() { frozen = true; }
    void reset() { ff = -1; frozen = false; }
};

// The result a disconnect may keep: the earliest recorded winner covered by the live watermark (0 = none: the match ends as a disconnect).
inline int disconnect_result(const Ring& r, uint32_t base, const Watermark& wm, uint32_t* at)
{
    if (at) *at = 0;
    return wm.ff < 0 ? 0 : r.earliest_covered(base, wm.ff, at);
}

// The end texts of the boundary (end_with). A result ends as "Match over: ..."; a disconnect that keeps no result as "Opponent disconnected".
inline std::string match_over_text(int w) { return w == 3 ? std::string("Match over: time up") : std::string("Match over: ") + (w == 1 ? "red" : "blue") + " wins"; }
inline std::string disconnect_end_text(int kept) { return kept > 0 ? match_over_text(kept) : std::string("Opponent disconnected"); }
// A normal match end: only a "Match over" text. The share pad watchdog (rematch black screen, jsp_native_fight_end_normal) may act only after one,
// so a disconnect that ignored a predicted result never arms it, and a confirmed KO kept across a disconnect still does.
inline bool normal_end(const std::string& text) { return text.rfind("Match over", 0) == 0; }

}  // namespace jkr
