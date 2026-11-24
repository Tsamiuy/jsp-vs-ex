// jsp_train_readout.h -- ENHANCEMENT (display only, on by default inside Training, absent everywhere else): the Training readout.
// Two short lines in the top-right corner of a Training fight: the damage of the last hit, the combo's hit count and total, and both
// fighters' health and Stamina as numbers. Pure state, text and layout, no game or overlay calls, so installer/tools/train_readout_test.cpp
// runs it on the host. The glue (jsp_game.cpp) feeds it from addresses already used by Training and draws it with the port overlay.
//
// Sources (all already read by Training on main, see docs/notes/TRAINING_MODE.md):
//   damage   the apply-damage virtual call @0x00085B84: r3 the fighter's health object, r4 damage to +0x90, r5 damage to +0x94.
//            The readout takes r4 as the game asked for it, before Training's leave-1-health cap rewrites it, so a move's real damage
//            shows even when the dummy is near the cap. r4 <= 0 is a heal and is not a hit.
//   combo    the gauge over the characters, class 0x00879DA0: +0x3EC is non-zero while the combo number is up, +0x3F0 the hit count.
//            The up-to-down edge closes a combo; the next hit opens a new one with its own damage.
//   vitals   health object +0x90 current health, +0x98 max (both in hundredths); +0x9C Stamina, +0xA0 Stamina limit (whole units:
//            1000 at the start, the limit 1000..2000, docs/balance/Shared.md).
//
// Switch: TRAINING_READOUT (play_settings.txt or the environment, read live through jsp_setting_get). Unset means on; empty or a value
// starting with '0' means off (the same rule as the other port keys, jpnb::key_on), so only "0" switches it off in practice.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace jptr {

inline constexpr const char* kKey = "TRAINING_READOUT";

// Default on: nullptr (the key is not set at all) is on; a set key is on unless it is empty or starts with '0'.
inline bool key_on(const char* v) { return !v || (*v && v[0] != '0'); }

// Layout in the 1280x720 overlay space, right-aligned. ASSUMPTION (not seen on screen yet): the top-right corner above the fight HUD's
// top bar, which starts at about y 50 (measured for the rollback badge, jsp_net_badge.h kHudTop); the badge sits top left (and only in
// online fights), the FPS counter bottom left. Text ink at scale s spans baseline - 24 s .. baseline + 9 s, baseline = y + 21 s.
inline constexpr float kScale = 0.45f;
inline constexpr float kY1 = 8.0f, kY2 = 24.0f;          // the tops of the two lines' capitals
inline constexpr int kRight = 1274;                      // the box's right edge (6 px margin, as the badge's left one)
inline constexpr int kBoxY = 3, kBoxH = 38, kBoxPad = 6; // box = [kRight - text width - 2 * kBoxPad, kRight) x [kBoxY, kBoxY + kBoxH)
inline constexpr float kHudTop = 50.0f;
inline constexpr float kGlyphMax = 18.8125f;             // the widest glyph advance of menu_font.jmf at scale 1 (as net_badge_test.cpp)
inline constexpr int kSides = 2;                         // P1 and P2; Training locks the partner slots, so a fight has two fighters

// Health and damage are stored in hundredths. ASSUMPTION: health shown in whole points (the HUD bar has no numbers to compare with),
// damage to one decimal, rounded to the nearest tenth.
inline std::string tenths(int32_t hundredths)
{
    const bool neg = hundredths < 0;
    const int64_t a = neg ? -(int64_t)hundredths : (int64_t)hundredths;
    const int64_t t = (a + 5) / 10;
    char b[32];
    std::snprintf(b, sizeof b, "%s%lld.%lld", neg ? "-" : "", (long long)(t / 10), (long long)(t % 10));
    return b;
}
inline std::string whole(int32_t hundredths) { return std::to_string(hundredths / 100); }

struct Side {
    bool known = false;
    int32_t hp = 0, hp_max = 0;     // hundredths
    int32_t st = 0, st_max = 0;     // whole Stamina units
};

class Readout {
public:
    // One damage call in Training. d90 is the game's requested damage to +0x90 (before the cap). Returns true when it counted as a hit.
    bool on_hit(uint32_t defender, int32_t d90, uint64_t tick)
    {
        if (d90 <= 0) return false;                      // a heal (or nothing): not a hit
        if (!combo_open_ || defender != defender_) {     // the first hit after the gauge went down, or a hit on the other fighter
            combo_open_ = true;
            combo_total_ = 0;
            own_hits_ = 0;
            game_hits_ = 0;
            defender_ = defender;
        }
        combo_total_ += d90;
        own_hits_++;
        last_dmg_ = d90;
        have_hit_ = true;
        last_tick_ = tick;
        return true;
    }

    // Once per tick: the combo gauge's state (any gauge up) and the largest hit count among the gauges.
    void on_gauge(bool up, uint32_t hits)
    {
        if (up && hits > game_hits_) game_hits_ = hits;
        if (gauge_up_ && !up) combo_open_ = false;       // the combo number went away: the combo is over, its numbers stay on screen
        gauge_up_ = up;
    }

    void set_vitals(int slot, int32_t hp, int32_t hp_max, int32_t st, int32_t st_max)
    {
        if (slot < 0 || slot >= kSides) return;
        Side& s = side_[slot];
        s.known = true; s.hp = hp; s.hp_max = hp_max; s.st = st; s.st_max = st_max;
    }

    // The game's own count when its gauge has shown one, else the hits this readout saw in the combo.
    uint32_t combo_hits() const { return game_hits_ ? game_hits_ : own_hits_; }
    int32_t combo_total() const { return combo_total_; }
    int32_t last_damage() const { return have_hit_ ? last_dmg_ : -1; }
    bool combo_open() const { return combo_open_; }
    uint64_t last_tick() const { return last_tick_; }

    // Line 1: the last hit and the combo. ASCII only (the overlay font prints anything else as '?').
    std::string line1() const
    {
        if (!have_hit_) return "Damage - | Combo -";
        const uint32_t n = combo_hits();
        return "Damage " + tenths(last_dmg_) + " | Combo " + std::to_string(n) + (n == 1 ? " hit " : " hits ") + tenths(combo_total_);
    }

    // Line 2: both fighters' vitals. ASSUMPTION: slot 0 is Player 1 (the first fighter the HUD's health getter noted), slot 1 the dummy.
    std::string line2() const
    {
        std::string out;
        for (int i = 0; i < kSides; i++) {
            if (i) out += " | ";
            const Side& s = side_[i];
            out += "P" + std::to_string(i + 1);
            if (!s.known) { out += " -"; continue; }
            out += " HP " + whole(s.hp) + "/" + whole(s.hp_max) + " ST " + std::to_string(s.st) + "/" + std::to_string(s.st_max);
        }
        return out;
    }

    std::string text() const { return line1() + "\n" + line2(); }

    // The redraw gate: true when the text differs from the one last drawn (and records it as drawn).
    bool changed()
    {
        const std::string t = text();
        if (drawn_ && t == shown_) return false;
        shown_ = t;
        drawn_ = true;
        return true;
    }
    // The overlay was closed or taken by someone else: the next changed() draws again.
    void invalidate() { drawn_ = false; }

    // A new fight (or leaving Training): everything goes.
    void reset() { *this = Readout(); }

private:
    Side side_[kSides];
    uint32_t defender_ = 0;
    int32_t last_dmg_ = 0;
    int32_t combo_total_ = 0;
    uint32_t own_hits_ = 0, game_hits_ = 0;
    uint64_t last_tick_ = 0;
    bool have_hit_ = false, combo_open_ = false, gauge_up_ = false;
    std::string shown_;
    bool drawn_ = false;
};

}  // namespace jptr
