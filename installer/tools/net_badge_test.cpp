// Host test of installer/port/jsp/jsp_net_badge.h: the rollback status badge's text, key, values, refresh gate and layout, and the ping counter under the fight timer. No game, no GPU.
// With a file argument it writes the status lines it built there, one per line, for net_badge_test.sh to match against support_bundle.py.
#include "../port/jsp/jsp_net_badge.h"
#include <cstdio>
#include <limits>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
using namespace jpnb;
int main(int argc, char** argv)
{
    // the key: off unless set to a non-zero value (as JSP_ROLLBACK)
    CHECK(!key_on(nullptr) && !key_on("") && !key_on("0") && !key_on("00"));
    CHECK(key_on("1") && key_on("yes") && key_on("2"));
    CHECK(std::string(kKey) == "JSP_ROLLBACK_BADGE");

    // the text: nothing known yet, then a direct session with a delay and rollbacks
    Values v;
    CHECK(text(v) == "Rollback | - | delay - | last -");
    v.path = "direct"; v.delay = 2;
    CHECK(text(v) == "Rollback | direct | delay 2f | last -");
    note_rollback(v, 3);
    CHECK(text(v) == "Rollback | direct | delay 2f | last 3f" && v.max == 3 && v.count == 1);
    note_rollback(v, 7); note_rollback(v, 1);
    CHECK(text(v) == "Rollback | direct | delay 2f | last 1f" && v.max == 7 && v.count == 3);
    note_rollback(v, -4);                                          // not a rollback: ignored
    CHECK(v.last == 1 && v.count == 3);
    note_rollback(v, 0);                                           // a zero-length rollback counts (it happened) and shows 0f
    CHECK(text(v) == "Rollback | direct | delay 2f | last 0f" && v.count == 4 && v.max == 7);
    v.path = "relay"; v.delay = 0;
    CHECK(text(v) == "Rollback | relay | delay 0f | last 0f");
    v.path = "punching";                                           // anything but direct / relay is not a path yet
    CHECK(text(v) == "Rollback | - | delay 0f | last 0f");
    v.path = "198.51.100.4:7000";                                  // never an address
    CHECK(text(v).find("198") == std::string::npos && log_line(v).find("198") == std::string::npos);
    for (char c : text(v)) CHECK(c >= 0x20 && c <= 0x7E);          // the overlay font prints only ASCII

    // the log line: the words support_bundle.py matches
    Values w; w.path = "relay"; w.delay = 2; note_rollback(w, 3); note_rollback(w, 6); note_rollback(w, 2);
    CHECK(log_line(w) == "status: path relay, delay 2f, last rollback 2f, largest 6f, rollbacks 3");
    CHECK(log_line(Values{}) == "status: path -, delay -, last rollback -, largest -, rollbacks 0");

    // the refresh gate: the first call, then once per period; a clock jump backwards does not stop it
    Every e;
    CHECK(e.due(1000, 250) && !e.due(1100, 250) && !e.due(1249, 250) && e.due(1250, 250) && !e.due(1300, 250) && e.due(1600, 250));
    CHECK(e.due(10, 250));                                         // went back by more than a period: due again, not stuck until 1850
    CHECK(kRefreshMs >= 250 && kLogMs >= 10000);                   // at most four times a second; the log line is sparse

    // layout: the ink of the text (rim 24 above and 9 below the baseline at scale 1; baseline = y + 21 * scale) is inside the box,
    // and the box ends above the fight HUD's top bar with a margin
    const float base = kY + 21.0f * kScale, top = base - 24.0f * kScale, bottom = base + 9.0f * kScale;
    CHECK(top >= kBoxY + 1 && bottom <= kBoxY + kBoxH - 1);
    CHECK(kBoxY + kBoxH + 10 <= kHudTop);
    CHECK(kX - 3.0f * kScale >= kBoxX + 1 && kBoxPad >= kX - kBoxX);
    // the widest line ("Rollback | direct | delay 99f | last 99f", 40 characters) at the widest glyph advance (18.8125 at scale 1, menu_font.jmf)
    // stays in the left half: clear of the top bar's centre ornament and the timer side
    const float widest = 40 * 18.8125f * kScale;
    CHECK(kBoxX + widest + 2 * kBoxPad < 640.0f);

    // the ping counter: on by default, hidden only by a value starting with '0'
    CHECK(std::string(kPingKey) == "JSP_ROLLBACK_PING");
    CHECK(ping_key_on(nullptr) && ping_key_on("") && ping_key_on("1") && ping_key_on("yes"));
    CHECK(!ping_key_on("0") && !ping_key_on("00"));
    // its text: nothing before the first ping, rounded to whole ms, clamped above 999
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    CHECK(ping_text(-1.0f).empty() && ping_text(-0.01f).empty() && ping_text(nan).empty());
    CHECK(ping_text(62.0f) == "62 ms" && ping_text(61.5f) == "62 ms" && ping_text(62.49f) == "62 ms");
    CHECK(ping_text(0.0f) == "0 ms" && ping_text(0.4f) == "0 ms" && ping_text(7.6f) == "8 ms");
    CHECK(ping_text(999.0f) == "999 ms" && ping_text(999.49f) == "999 ms");
    CHECK(ping_text(999.5f) == "999+ ms" && ping_text(1000.0f) == "999+ ms" && ping_text(1e9f) == "999+ ms" && ping_text(inf) == "999+ ms");
    for (float p : {0.0f, 62.0f, 999.0f, 5000.0f}) for (char c : ping_text(p)) CHECK(c >= 0x20 && c <= 0x7E);
    // the tint follows the shown (rounded) value: green below 100, yellow below 180, red above
    CHECK(ping_quality(30.0f) == PingQuality::Good && ping_quality(99.49f) == PingQuality::Good && ping_quality(99.5f) == PingQuality::Fair);
    CHECK(ping_quality(179.0f) == PingQuality::Fair && ping_quality(179.5f) == PingQuality::Poor && ping_quality(2000.0f) == PingQuality::Poor);
    CHECK(ping_rgba(30.0f) == 0xFF78FF78u && ping_rgba(150.0f) == 0xFF5AE6FFu && ping_rgba(400.0f) == 0xFF646EFFu);
    // its refresh: at most twice a second, through the same gate
    CHECK(kPingRefreshMs >= 250 && kPingRefreshMs <= 1000);
    Every pe;
    CHECK(pe.due(5000, kPingRefreshMs) && !pe.due(5400, kPingRefreshMs) && pe.due(5500, kPingRefreshMs));
    // its layout: the ink inside the box, the box below the timer bar's rule and above the screen's bottom, right-aligned under the digits
    {
        const float pbase = kPingY + 21.0f * kPingScale, ptop = pbase - 24.0f * kPingScale, pbottom = pbase + 9.0f * kPingScale;
        CHECK(ptop >= kPingBoxY + 1 && pbottom <= kPingBoxY + kPingBoxH - 1);
        CHECK(kPingBoxY >= kHudRuleBottom + 3 && kPingBoxY + kPingBoxH <= 720 - 8);
        const std::string t = ping_text(62.0f);
        CHECK(ping_x(t, 60.0f) == kPingRight - 60.0f);
        CHECK(ping_box_x(t, 60.0f) + ping_box_w(t, 60.0f) >= (int)kPingRight + kPingBoxPad && ping_box_x(t, 60.0f) + ping_box_w(t, 60.0f) <= 1280);
        // the widest text ("999+ ms") with no atlas width: the estimate, still under the timer bar (x >= 912), far right of the player panel
        const std::string wt = ping_text(5000.0f);
        CHECK(ping_width(wt, 0.0f) == 7 * kGlyphMax * kPingScale && ping_box_x(wt, 0.0f) >= 912);
        CHECK(kPingRight + kPingBoxPad <= 1210);
        CHECK(kPingY > kY + 600);   // never meets the badge (top left)
    }

    if (argc > 1) {   // the status lines for the bundle's pattern
        if (FILE* f = std::fopen(argv[1], "w")) {
            Values a; std::fprintf(f, "%s\n", log_line(a).c_str());
            a.path = "direct"; a.delay = 8; note_rollback(a, 12); std::fprintf(f, "%s\n", log_line(a).c_str());
            a.path = "relay"; note_rollback(a, 0); std::fprintf(f, "%s\n", log_line(a).c_str());
            a.path = "punching"; std::fprintf(f, "%s\n", log_line(a).c_str());
            std::fclose(f);
        } else { std::printf("FAIL: cannot write %s\n", argv[1]); fails++; }
    }
    if (!fails) std::printf("net badge: all checks pass\n");
    return fails ? 1 : 0;
}
