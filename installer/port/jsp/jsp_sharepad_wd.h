// jsp_sharepad_wd.h -- FIX (rematch black screen: the share pad left starved after a rollback match). Pure and host-tested
// (installer/tools/sharepad_wd_test.sh); the game glue is jsp_net_native_fight.cpp (hooks on the online mode's slot 2 0x0F7704, its free 0x0F77DC,
// and the post-fight object's deleting destructor 0x0E2048). Analysis: PR #248, Update 3 (the rematch black screen).
//
// After a rollback match the game's own share pad (a pure input lockstep, no catch-up) runs Results and the exit. When the object of class 0x876A60 is
// deleted (0x0E2048) the online mode needs one more mode tick to reach its exit poll (vtable slot 18, 0x0F777C, twice) and its free (slot 1, 0x0F77DC).
// A copy whose peer has input delay 0 holds no spare peer sample for that tick: when the peer leaves in the same frame, the member stays without
// samples, sync +0x6D4 = 3, the stall check keeps the mode paused, the stall counter +0x6E8 climbs to 779 (26 s) and the game leaves the room.
//
// The watchdog: once this copy ended a rollback match NORMALLY (an agreed KO / time-up hand-back), the 0x876A60 object was deleted, the fight HUD is no
// longer drawn, the mode is paused (+0x1C & 0xB0) and a peer member has been starved with sync 3 for kStartTicks consecutive mode ticks, it pushes one
// NEUTRAL 12-byte sample into that member's received-sample queue, the same list operation the receive path 0x4824CC does (0x473704(q + 4, 1, s, 12)).
// Once started (latched) for a member, it feeds one sample in every later tick in which that member is starved again. A tick in which a guard fails only
// skips the feed; the latch ends only with the episode (the mode's free, the next arm), the member leaving (state 4), a failed push or kMaxFeed samples.
// Nothing is ever sent to the peer: the list is the RECEIVED queue, which only the local pad take reads.
//
// A peer that is late but alive: its real samples still come after the neutral ones, so every neutral sample fed is one extra queued sample. As soon as a
// REAL sample from a member arrives after a feed (View::real, from the manager update hook: that member's count rose without our push), the member is
// stopped for the episode and never fed again, so the extra samples stay at what was fed before that arrival (State::surplus). They end with the mode:
// the next online mode's build resets every member queue (jsp_net_native_fight.cpp, free_wrap). A real sample before any feed only restarts the count.
//
// "Starved" uses the real counts, not only the state word: in 0x482E64 the register holding "own queue count" is overwritten with each member's
// count, so after one member with count 0 every later member reads state 1 whatever its queue holds. A member is starved when the own queue is not
// empty, its own count is 0 and its state is 3 or 1 (1 = the quirk). So in 2v2 all starved members start and are fed in the same tick.
#pragma once
#include <cctype>
#include <cstddef>
#include <cstdint>

namespace jspw {

constexpr uint32_t kMaxMembers = 4;    // share pad queue entries / member blocks (+0x1C + 0x28*k, +0xBC + 0xB8*i)
constexpr uint32_t kStartTicks = 10;   // consecutive starved mode ticks before the first feed (0.33 s at 30 Hz; normal transients last 1-2)
constexpr uint32_t kMaxFeed = 300;     // neutral samples per member per episode (the exit needs a handful)
constexpr uint32_t kStateOwnEmpty = 1, kStateNormal = 2, kStateStarved = 3, kStateGone = 4;   // member states of 0x482E64

// One mode tick's view (the slot 2 hook reads it from guest memory before the stall check runs).
struct View {
    bool guards = false;          // normal match end, 0x876A60 object deleted since, the same online mode object, share pad present, not armed
    bool hud_drawn = false;       // the fight HUD (0x00085CEC) was drawn since the previous mode tick
    bool paused = false;          // the mode's pause bits (mode + 0x1C & 0xB0, set by the stall check through 0x148368)
    uint32_t active = 0;          // share pad +0x6D8
    uint32_t sync = 0;            // share pad +0x6D4 (aggregate member state)
    uint32_t members = 0;         // share pad +0x10
    uint32_t own = 0;             // this PC's own queue count (its input delay)
    uint32_t st[kMaxMembers] = {};      // member block +0x00
    uint32_t count[kMaxMembers] = {};   // *(member block + 8) + 8: that member's received-sample count
    bool queue_ok[kMaxMembers] = {};    // *(member block + 8) is a plausible queue entry with a peer at +0
    uint32_t real = 0;                  // bit i: a REAL sample from member i arrived since the previous tick (its count rose in a manager update)
};

struct State {
    uint32_t run[kMaxMembers] = {};      // consecutive ticks the start condition held
    bool latched[kMaxMembers] = {};      // feeding this member
    uint32_t fed[kMaxMembers] = {};      // neutral samples fed this episode
    uint32_t ticks[kMaxMembers] = {};    // ticks since the latch (for the summary line)
    bool stopped[kMaxMembers] = {};      // a real sample came after a feed: never fed again this episode
    uint32_t surplus[kMaxMembers] = {};  // neutral samples fed before that real sample (extra queued samples, until the mode's queues are reset)
};

inline void reset(State& s) { s = State{}; }

inline bool starved(const View& v, uint32_t i)
{
    return v.queue_ok[i] && v.own > 0 && v.count[i] == 0 && (v.st[i] == kStateStarved || v.st[i] == kStateOwnEmpty);
}

// Once per mode tick. Returns the bit mask of members to feed ONE neutral sample now. The caller pushes, and calls fed_failed() for a push that did
// not land or could not be made (the member is then left alone for this episode).
inline uint32_t step(State& s, const View& v)
{
    const bool live = v.guards && v.active == 1;
    const uint32_t n = v.members < kMaxMembers ? v.members : kMaxMembers;
    const bool start_ok = live && v.sync == kStateStarved && v.paused && !v.hud_drawn;
    uint32_t mask = 0;
    for (uint32_t i = 0; i < kMaxMembers; i++) {
        if (s.stopped[i]) continue;
        if (v.real & (1u << i)) {   // a real sample from this peer: it is alive
            if (s.fed[i] > 0) { s.stopped[i] = true; s.surplus[i] = s.fed[i]; s.latched[i] = false; }
            s.run[i] = 0;
            continue;
        }
        if (s.latched[i]) {
            s.ticks[i]++;
            if (i < n && (v.st[i] == kStateGone || s.fed[i] >= kMaxFeed)) { s.latched[i] = false; s.run[i] = 0; continue; }
            if (live && i < n && starved(v, i)) { mask |= 1u << i; s.fed[i]++; }   // a failed guard skips this tick only; the latch stays
            continue;
        }
        if (i >= n || s.fed[i] >= kMaxFeed) { s.run[i] = 0; continue; }
        if (start_ok && starved(v, i)) s.run[i]++;
        else s.run[i] = 0;
        if (s.run[i] >= kStartTicks) {
            s.latched[i] = true; s.run[i] = 0; s.ticks[i] = 0;
            mask |= 1u << i; s.fed[i]++;
        }
    }
    return mask;
}

// The push for member i did not land, or was refused before the call: stop feeding it for this episode.
inline void fed_failed(State& s, uint32_t i)
{
    if (i >= kMaxMembers) return;
    s.latched[i] = false; s.run[i] = 0; s.fed[i] = kMaxFeed;
}

// JSP_SHAREPAD_WATCHDOG: the watchdog is on unless the value is 0, off, false or no (any case, surrounding blanks ignored). Unset, empty and
// anything else leave it on.
inline bool setting_off(const char* v)
{
    if (!v) return false;
    while (*v == ' ' || *v == '\t') v++;
    std::size_t n = 0;
    while (v[n]) n++;
    while (n && (v[n - 1] == ' ' || v[n - 1] == '\t' || v[n - 1] == '\r' || v[n - 1] == '\n')) n--;
    static const char* const kOff[] = { "0", "off", "false", "no" };
    for (const char* w : kOff) {
        std::size_t i = 0;
        while (i < n && w[i] && std::tolower((unsigned char)v[i]) == w[i]) i++;
        if (i == n && !w[i]) return true;
    }
    return false;
}

}  // namespace jspw
