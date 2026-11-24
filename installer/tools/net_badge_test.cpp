// Host test of installer/port/jsp/jsp_net_badge.h: the rollback status badge's text, key, values, refresh gate and layout. No game, no GPU.
// (The round trip under the fight timer is not part of the netcode core: an optional UI module draws it, with its own host test; it is not in this build.)
// With a file argument it writes the status lines it built there, one per line, for net_badge_test.sh to match against support_bundle.py.
#include "../port/jsp/jsp_net_badge.h"
#include <cstdio>
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
