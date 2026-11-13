// Host test of installer/port/jsp/jsp_version_line.h: text and value choice. No game, no GPU.
#include "../port/jsp/jsp_version_line.h"
#include <cstdio>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
using namespace jpv;
int main()
{
    CHECK(parse_version_txt("0.1.0\n") == "0.1.0" && parse_version_txt("0.1.0\r\nrest\n") == "0.1.0");
    CHECK(parse_version_txt("\xEF\xBB\xBF" "0.1.0-rc1\n") == "0.1.0-rc1");
    CHECK(parse_version_txt("").empty() && parse_version_txt("\n0.1\n").empty() && parse_version_txt("a b\n").empty());
    CHECK(parse_version_txt("<html>\n").empty() && parse_version_txt(std::string(30, '1')).empty());

    Inputs in;
    CHECK(!choose(in).any());                                   // dev build: nothing
    in.switch_env = "dev";
    CHECK(choose(in).line1 == "J-Stars Victory VS-EX dev");
    in.switch_env = "0"; in.port_version = "0.1.0";
    CHECK(!choose(in).any());                                   // off beats everything
    in.switch_env = nullptr;
    CHECK(choose(in).line1 == "J-Stars Victory VS-EX 0.1.0");
    in.port_version = nullptr; in.file_version = "0.1.0";
    CHECK(choose(in).line1 == "J-Stars Victory VS-EX 0.1.0");   // from release.txt
    in.port_version = "0.3.0";
    CHECK(choose(in).line1 == "J-Stars Victory VS-EX 0.3.0");   // env beats the file
    in.port_version = "no good";
    CHECK(choose(in).line1 == "J-Stars Victory VS-EX 0.1.0");   // invalid env ignored
    in.port_version = nullptr;

    // args (obj_alive, panel_seen, flips_since_update, panel_state, flow_state, menu_mode); flow_state -1 = no flow object
    CHECK(top_menu(true, true, 0, 0, 4, 0) && top_menu(true, true, 2, 0, 4, 0));
    CHECK(!top_menu(false, true, 0, 0, 4, 0) && !top_menu(true, false, 0, 0, 4, 0) && !top_menu(true, true, 3, 0, 4, 0));   // scene gone / stale
    for (int s = 1; s <= 5; s++) CHECK(!top_menu(true, true, 0, s, 4, 0));                                 // a sub-menu panel is up
    CHECK(!top_menu(true, true, 0, 0, -1, 0));                                                            // no flow object
    for (int st = 0; st <= 9; st++) if (st != 4) CHECK(!top_menu(true, true, 0, 0, st, 0));              // request / leave / next scene
    for (int m = 1; m <= 9; m++) CHECK(!top_menu(true, true, 0, 0, 4, m));
    CHECK(!top_menu(true, true, 0, -1, 2, 0) && !top_menu(true, true, 0, 0, 2, 0));                       // st=2 title -> menu transition
    CHECK(!top_menu(true, true, 0, 2, 6, 5) && !top_menu(true, true, 0, 3, 7, 5) && !top_menu(true, true, 0, 0, 7, 5)); // leaving into Options

    // placement in the legend bar (geometry measured on a main-menu screenshot, see jsp_version_line.h). Advance widths at scale 1 from
    // menu_font.jmf: "J-Stars Victory VS-EX 0.1.0" 375.9375, "... 10.10.10" 424.875, "... 0.1.0-rc1" 432.125; a 24-char version of widest
    // glyphs (18.8125) after the 296 px name is the bound used for the longest case.
    const float widths1[] = { 375.9375f, 424.875f, 432.125f, 296.0f + 24 * 18.8125f };
    for (float w1 : widths1) {
        const InkBox b = ink_box(w1 * kScale1);
        CHECK(b.y0 >= kBarTop + 4 && b.y1 <= kBarBottom - 1);            // inside the bar interior, clear of its borders
        CHECK(b.x1 <= kBarEdgeMinX - kMargin + kInkSide * kScale1);       // left of the slanted end by the margin
        CHECK(b.x1 + 8 <= kBarEdgeMinX);                                  // no glyph touches the edge
        CHECK(b.x0 >= kBackRight + kMargin);                              // clear of the game's "Back" label
    }
    {   // vertical centring on the bar: the capitals' rim box (above the baseline by 24, below by 2) is centred within 1 px
        const InkBox b = ink_box(0);
        const float capmid = (b.y0 + (b.y0 + (kInkAbove + 2) * kScale1)) / 2, barmid = (kBarTop + kBarBottom) / 2 - 0.5f;
        CHECK(capmid > barmid - 1 && capmid < barmid + 1);
        CHECK(kScale1 <= 0.7f && kScale1 >= 0.5f);                        // a little smaller than the game's labels (19 px ink)
    }
    if (!fails) std::printf("version line: all checks pass\n");
    return fails ? 1 : 0;
}
