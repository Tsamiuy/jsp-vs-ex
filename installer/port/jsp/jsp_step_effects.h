// jsp_step_effects.h -- host side effects during a replay: which are suppressed, which are deduplicated, which wait for confirmation.
// Pure (no runtime): the wiring that calls gate().request() from the audio / file / save / network layers is private glue; the policy and the
// ledger are tested on the host (installer/tools/stepper_test.cpp).
//
// The guest memory goes back on a rollback; the host does not. Whatever left the process during the speculative ticks (a sound that played, a
// packet that was sent, a file that was written) cannot be taken back, and the replay will ask for it again. Three answers per category:
//   Speculative  the effect is cheap and its repeat is the problem (sound effects): performed once in the live timeline, recorded in a ledger
//                keyed by (tick, category, key); the replay's identical request is SKIPPED (already heard), a new one is performed (late), and
//                the ledger entries the replay did not reproduce are reported as ORPHANS when the replay ends (the caller may fade them out).
//   ConfirmOnly  the effect is irreversible (save data, results, achievements, file writes): never performed while a tick is unconfirmed; it
//                is queued per tick, dropped when a rollback goes behind its tick (the replay asks again), and released exactly once when the
//                tick is confirmed (confirm(t)).
//   Suppress     never performed in a replay, always in the live timeline (network sends, rumble, window title, per-frame logging).
//   Pass         idempotent reads (file reads, device polls that do not change what the game sees): performed in both.
#pragma once
#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

namespace jse {

enum class Cat : uint8_t { SoundEffect, Music, FileRead, FileWrite, SaveData, Result, Net, Rumble, Log, Count };
enum class Policy : uint8_t { Pass, Speculative, ConfirmOnly, Suppress };
enum class Action : uint8_t { Perform, Skip, Defer };

inline Policy default_policy(Cat c)
{
    switch (c) {
    case Cat::SoundEffect: return Policy::Speculative;
    case Cat::Music:       return Policy::Speculative;    // a music start that was already heard is never started again (see late_new)
    case Cat::FileRead:    return Policy::Pass;
    case Cat::FileWrite:   return Policy::ConfirmOnly;
    case Cat::SaveData:    return Policy::ConfirmOnly;
    case Cat::Result:      return Policy::ConfirmOnly;
    case Cat::Net:         return Policy::Suppress;
    case Cat::Rumble:      return Policy::Suppress;
    case Cat::Log:         return Policy::Pass;
    default:               return Policy::Pass;
    }
}

struct Deferred { uint32_t tick; Cat cat; uint64_t key; uint64_t id; };
struct Orphan { uint32_t tick; Cat cat; uint64_t key; uint32_t count; };

class Gate {
public:
    void set_replay(bool on) { replay_ = on; }
    bool replay() const { return replay_; }
    // A replay may perform a speculative request the live timeline never made (a hit that only exists in the corrected timeline). Music never.
    void set_late_new_sound(bool on) { late_sfx_ = on; }

    // The one question the audio / file / save / net layers ask before doing something. `tick` = the simulation tick the request belongs to
    // (done + 1 while a tick is running), `key` = a stable hash of what is asked (cue id + player id, path + offset ...), `id` = an opaque
    // handle to find the queued request again (ConfirmOnly: returned by confirm()).
    Action request(Cat c, uint32_t tick, uint64_t key, uint64_t id = 0)
    {
        stats_.requests++;
        switch (default_policy(c)) {
        case Policy::Pass: return Action::Perform;
        case Policy::Suppress: if (replay_) { stats_.suppressed++; return Action::Skip; } return Action::Perform;
        case Policy::ConfirmOnly:
            deferred_.push_back({tick, c, key, id}); stats_.deferred++; return Action::Defer;
        case Policy::Speculative: break;
        }
        auto k = std::make_tuple(tick, c, key);
        if (!replay_) { ledger_[k].played++; stats_.performed++; return Action::Perform; }
        auto it = ledger_.find(k);
        if (it != ledger_.end() && it->second.matched < it->second.played) { it->second.matched++; stats_.deduped++; return Action::Skip; }
        // a request the live timeline did not make (or made fewer times)
        if (c == Cat::Music || !late_sfx_) { stats_.dropped_new++; return Action::Skip; }
        ledger_[k].played++; ledger_[k].matched++; stats_.performed++; stats_.late++;
        return Action::Perform;
    }

    // A rollback to `done` completed ticks: queued irreversible requests beyond it are dropped (the replay requests them again); the ledger
    // keeps its entries beyond it but starts matching from zero.
    void rollback(uint32_t done)
    {
        for (size_t i = 0; i < deferred_.size();) { if (deferred_[i].tick > done) { deferred_.erase(deferred_.begin() + (long)i); stats_.dropped_deferred++; } else i++; }
        for (auto& e : ledger_) if (std::get<0>(e.first) > done) e.second.matched = 0;
    }
    // The replay finished: ledger entries beyond `done_at_rollback` that the replay did not reproduce (heard once, not asked again).
    std::vector<Orphan> end_replay(uint32_t done_at_rollback)
    {
        std::vector<Orphan> o;
        for (auto& e : ledger_) {
            if (std::get<0>(e.first) <= done_at_rollback) continue;
            if (e.second.matched < e.second.played) o.push_back({std::get<0>(e.first), std::get<1>(e.first), std::get<2>(e.first), e.second.played - e.second.matched});
        }
        stats_.orphans += o.size();
        replay_ = false;
        return o;
    }
    // Ticks up to and including `t` are confirmed (no input can change them): release their irreversible requests once.
    std::vector<Deferred> confirm(uint32_t t)
    {
        std::vector<Deferred> out;
        for (size_t i = 0; i < deferred_.size();) { if (deferred_[i].tick <= t) { out.push_back(deferred_[i]); deferred_.erase(deferred_.begin() + (long)i); } else i++; }
        stats_.released += out.size();
        return out;
    }
    void forget_before(uint32_t t) { for (auto it = ledger_.begin(); it != ledger_.end();) { if (std::get<0>(it->first) < t) it = ledger_.erase(it); else ++it; } }
    size_t pending() const { return deferred_.size(); }

    struct Stats { uint64_t requests = 0, performed = 0, deduped = 0, late = 0, dropped_new = 0, suppressed = 0, deferred = 0, released = 0, dropped_deferred = 0, orphans = 0; };
    const Stats& stats() const { return stats_; }

private:
    struct L { uint32_t played = 0, matched = 0; };
    std::map<std::tuple<uint32_t, Cat, uint64_t>, L> ledger_;
    std::vector<Deferred> deferred_;
    bool replay_ = false, late_sfx_ = true;
    Stats stats_;
};

}  // namespace jse
