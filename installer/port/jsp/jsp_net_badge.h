// jsp_net_badge.h -- ENHANCEMENT (display, opt-in, default off): the rollback status badge. One short line in the top-left corner while a
// rollback session runs: "Rollback | <path> | delay <N>f | last <M>f", and the "[net] status:" log line with the same values that the
// support bundle reads (installer/tools/support_bundle.py). Pure text, value and layout choice, no game or overlay calls, so
// installer/tools/net_badge_test.cpp runs it on the host. The drawing is in jsp_net_game.cpp (the same overlay layer as its messages).
//
// Switch: JSP_ROLLBACK_BADGE=1 in play_settings.txt (the launcher passes every line on as an environment variable), read once when the
// rollback hooks are installed. Any non-zero value is on (not empty, first character not '0'), as for JSP_ROLLBACK.
// Values, all already known to the session:
//   path   "direct" for a direct-IP session; over re-psn the rendezvous' path (jsp_net_repsn.h path_name: "direct" or "relay")
//   delay  the input delay of the match in ticks (the manifest's InputDelay: the host's value, the same on both PCs)
//   last   the length of the last rollback in ticks (the ticks the game went back and ran again: current tick minus the rollback target)
//   max    the largest rollback of the session, count the number of rollbacks (log line only)
// "f" is a game tick (30 per second); the game font has printable ASCII only, so the separator is '|', not a middle dot.
#pragma once
#include <cstdint>
#include <string>

namespace jpnb {

inline constexpr const char* kKey = "JSP_ROLLBACK_BADGE";
inline constexpr uint32_t kRefreshMs = 250;        // the badge text is rebuilt at most four times a second
inline constexpr uint32_t kLogMs = 30000;          // one "[net] status:" line every 30 s of a running session, and one when it ends

// Layout in the 1280x720 overlay space. MEASURED on a fight screenshot (960x540, scaled by 4/3): the top health / team bar starts at about
// y 50 and x 120; the player panel is bottom left, the minimap and timer bottom right; the area above the bar is free across the width.
// At scale 0.5 the text's ink (rim 24 px above and 9 px below the baseline at scale 1, baseline = y + 21 * scale) spans y 8.5 .. 25;
// the dark box spans y 4 .. 28. The FPS counter (FPS_COUNTER) sits bottom left, so the two never meet.
inline constexpr float kX = 12.0f, kY = 10.0f, kScale = 0.5f;
inline constexpr int kBoxX = 6, kBoxY = 4, kBoxH = 24, kBoxPad = 6;   // box = [kBoxX, kBoxX + text width + 2 * kBoxPad) x [kBoxY, kBoxY + kBoxH)
inline constexpr float kHudTop = 50.0f;            // the top bar's upper edge (measured, see above)

inline bool key_on(const char* v) { return v && *v && v[0] != '0'; }

struct Values {
    std::string path;          // "direct", "relay", or anything else (shown as "-")
    int delay = -1;            // < 0: not known yet
    int last = -1;             // < 0: no rollback yet
    int max = -1;
    uint32_t count = 0;
};

inline std::string path_word(const std::string& p) { return p == "direct" || p == "relay" ? p : "-"; }
inline std::string frames(int v) { return v < 0 ? std::string("-") : std::to_string(v) + "f"; }

// The badge line. ASCII only (the overlay font prints anything else as '?').
inline std::string text(const Values& v)
{
    return "Rollback | " + path_word(v.path) + " | delay " + frames(v.delay) + " | last " + frames(v.last);
}

// The text after "[net] " of the status line (support_bundle.py matches it exactly: program words and numbers only, never an address or a name).
inline std::string log_line(const Values& v)
{
    return "status: path " + path_word(v.path) + ", delay " + frames(v.delay) + ", last rollback " + frames(v.last) + ", largest " + frames(v.max) +
           ", rollbacks " + std::to_string(v.count);
}

// One rollback of `len` ticks (negative lengths are not rollbacks and are ignored).
inline void note_rollback(Values& v, int64_t len)
{
    if (len < 0) return;
    const int l = len > 0x7fffffff ? 0x7fffffff : (int)len;
    v.last = l;
    if (l > v.max) v.max = l;
    v.count++;
}

// A periodic gate: true at the first call and then once per period (a clock that goes backwards does not stop it).
struct Every {
    uint64_t next = 0;
    bool due(uint64_t now, uint32_t period)
    {
        if (next && now < next && next - now <= period) return false;
        next = now + period;
        return true;
    }
};

// ---- ENHANCEMENT (ping counter, default ON): the round trip in ms, small, under the game's fight timer (bottom right) while an online rollback match runs,
// so players see what kind of connection they are in. JSP_ROLLBACK_PING=0 in play_settings.txt hides it (unset, empty or any other value: shown).
// The value is GekkoNet's averaged round trip to the other player (SessionStats::ping_ms, the one adapt_delay reads). Over a relay it is the round trip through
// the server. It carries up to one tick (33 ms) of answer wait (GekkoNet answers a ping at its next poll), so a 30 ms link reads about 60-95 ms.
inline constexpr const char* kPingKey = "JSP_ROLLBACK_PING";
inline constexpr uint32_t kPingRefreshMs = 500;    // the text is rebuilt at most twice a second (no flicker)

inline bool ping_key_on(const char* v) { return !(v && v[0] == '0'); }   // default on: only a value starting with '0' hides it

// "62 ms"; "999+ ms" above 999; "" (nothing drawn) while the round trip is not known yet (ping_ms < 0, or not a number).
inline std::string ping_text(float ms)
{
    if (!(ms >= 0.0f)) return std::string();
    if (ms >= 999.5f) return "999+ ms";
    return std::to_string((int)(ms + 0.5f)) + " ms";
}

// Quality tint (the overlay colour modulates the font atlas, 0xAABBGGRR; the black rim stays black). The edges follow adapt_delay with no jitter:
// below 100 ms the input delay settles on 1 tick (green), below 180 ms on 2 (yellow), above that on 3 or more (red).
enum class PingQuality { Good, Fair, Poor };
inline PingQuality ping_quality(float ms) { return ms < 99.5f ? PingQuality::Good : ms < 179.5f ? PingQuality::Fair : PingQuality::Poor; }
inline uint32_t ping_rgba(float ms)
{
    switch (ping_quality(ms)) {
    case PingQuality::Good: return 0xFF78FF78u;   // green   (120, 255, 120)
    case PingQuality::Fair: return 0xFF5AE6FFu;   // yellow  (255, 230,  90)
    default:                return 0xFF646EFFu;   // red     (255, 110, 100)
    }
}

// Layout in the 1280x720 overlay space, right-aligned under the timer. MEASURED on fight screenshots (640x360, scaled by 2: build/bench/runs native-rm5
// 0343/0401, native-rm1 0128): the timer digits span x 1082..1206, y 628..656; the timer bar's lower rule y 672..675, x 912..1208; the minimap sits above the
// bar (x ~1000..1180, y <= 640) and the player panel is bottom left (x < 600). Below the rule, y 676..720, nothing of the HUD is drawn.
// At scale 0.5 the ink (rim 24 above and 9 below the baseline at scale 1, baseline = y + 21 * scale) spans y 682.5 .. 699; the dark box spans y 680 .. 704.
// The text's right edge is the digits' right edge (x 1204); the box reaches 6 px either side.
inline constexpr float kPingRight = 1204.0f, kPingY = 684.0f, kPingScale = 0.5f;
inline constexpr int kPingBoxY = 680, kPingBoxH = 24, kPingBoxPad = 6;
inline constexpr float kHudRuleBottom = 675.0f;   // the timer bar's lower rule (measured, see above)
inline constexpr float kGlyphMax = 18.8125f;      // the widest glyph advance at scale 1 (menu_font.jmf): the width when the atlas reports none

// The text's left x for a measured width (jsp_overlay_text_width at kPingScale; 0 or less = unknown, the widest-glyph estimate is used).
inline float ping_width(const std::string& t, float measured) { return measured > 0 ? measured : (float)t.size() * kGlyphMax * kPingScale; }
inline float ping_x(const std::string& t, float measured) { return kPingRight - ping_width(t, measured); }
inline int ping_box_x(const std::string& t, float measured) { return (int)(ping_x(t, measured)) - kPingBoxPad; }
inline int ping_box_w(const std::string& t, float measured) { return (int)(ping_width(t, measured) + 0.999f) + 2 * kPingBoxPad; }

}  // namespace jpnb
