// Host test of installer/port/jsp/jsp_train_readout.h: the Training readout's combo state, text, key, redraw gate and layout. No game, no GPU.
#include "../port/jsp/jsp_train_readout.h"
#include <cstdio>
#include <algorithm>
#include <cstring>
static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
using namespace jptr;

static bool ascii(const std::string& s)
{
    for (char c : s) if (c < 0x20 || c > 0x7E) return false;
    return true;
}

int main()
{
    // the key: on unless set to empty or a value starting with '0'
    CHECK(key_on(nullptr));                                          // unset: on by default
    CHECK(!key_on("") && !key_on("0") && !key_on("00"));
    CHECK(key_on("1") && key_on("yes"));
    CHECK(key_on("off"));                                            // the shared rule: only a leading '0' (or empty) switches it off
    CHECK(std::string(kKey) == "TRAINING_READOUT");

    // units: hundredths to one decimal (rounded), whole health points
    CHECK(tenths(1250) == "12.5" && tenths(1234) == "12.3" && tenths(1235) == "12.4" && tenths(5) == "0.1" && tenths(0) == "0.0");
    CHECK(tenths(100000) == "1000.0" && tenths(-1250) == "-12.5");
    CHECK(whole(81234) == "812" && whole(100000) == "1000" && whole(99) == "0");

    // nothing yet
    Readout r;
    CHECK(r.line1() == "Damage - | Combo -" && r.last_damage() == -1);
    CHECK(r.line2() == "P1 - | P2 -");

    // a single hit, then the gauge goes up and down: the combo closes
    CHECK(r.on_hit(0x100, 1250, 10));
    CHECK(r.line1() == "Damage 12.5 | Combo 1 hit 12.5");
    r.on_gauge(true, 1);
    r.on_gauge(false, 0);
    CHECK(!r.combo_open());
    CHECK(r.line1() == "Damage 12.5 | Combo 1 hit 12.5");          // the closed combo's numbers stay on screen

    // a multi-hit combo is summed; the hit count is the game's own counter (+0x3F0) once the gauge shows one
    CHECK(r.on_hit(0x100, 1000, 20));                                // the first hit after the edge opens a new combo at its own damage
    CHECK(r.line1() == "Damage 10.0 | Combo 1 hit 10.0");
    r.on_gauge(true, 1);
    CHECK(r.on_hit(0x100, 800, 21));
    CHECK(r.on_hit(0x100, 1200, 22));
    r.on_gauge(true, 3);
    CHECK(r.on_hit(0x100, 800, 23));
    r.on_gauge(true, 4);
    CHECK(r.combo_total() == 3800 && r.combo_hits() == 4);
    CHECK(r.line1() == "Damage 8.0 | Combo 4 hits 38.0");
    r.on_gauge(true, 4);                                             // the same count on the next tick: still the same combo
    CHECK(r.combo_hits() == 4 && r.combo_total() == 3800 && r.combo_open());

    // a heal is not a hit
    CHECK(!r.on_hit(0x100, -500, 24) && !r.on_hit(0x100, 0, 24));
    CHECK(r.combo_total() == 3800 && r.last_damage() == 800);

    // the gauge edge closes the combo; the next hit starts at its own damage, not on top of 38.0
    r.on_gauge(false, 0);
    CHECK(r.on_hit(0x100, 450, 40));
    CHECK(r.line1() == "Damage 4.5 | Combo 1 hit 4.5");
    CHECK(r.last_tick() == 40);

    // a hit on the other fighter while a combo is open starts that fighter's own combo
    r.on_gauge(true, 1);
    CHECK(r.on_hit(0x200, 300, 41));
    CHECK(r.combo_total() == 300 && r.combo_hits() == 1);

    // a new combo while the old number still shows: the game's count drops (5 -> 2), so the newest two hits since the last tick are
    // the new combo and the older one stays with the old combo
    Readout n;
    for (int i = 0; i < 5; i++) { n.on_hit(0x100, 1000, 50 + i); n.on_gauge(true, (uint32_t)i + 1); }
    CHECK(n.combo_hits() == 5 && n.combo_total() == 5000);
    n.on_hit(0x100, 700, 60);                                        // the last hit of the old combo, before the tick that shows 6
    n.on_gauge(true, 6);
    CHECK(n.combo_hits() == 6 && n.combo_total() == 5700);
    n.on_hit(0x100, 300, 70);                                        // a new combo starts while 6 still shows
    n.on_hit(0x100, 200, 71);
    n.on_gauge(true, 2);
    CHECK(n.combo_open() && n.combo_hits() == 2 && n.combo_total() == 500);
    CHECK(n.line1() == "Damage 2.0 | Combo 2 hits 5.0");
    n.on_hit(0x100, 100, 72);
    n.on_gauge(true, 3);
    CHECK(n.combo_hits() == 3 && n.combo_total() == 600);
    // the game counted the new combo's first hit before the readout saw it: the old combo closes and that hit opens the new one
    n.on_gauge(true, 1);
    CHECK(!n.combo_open() && n.combo_total() == 600);
    n.on_hit(0x100, 450, 73);
    n.on_gauge(true, 1);
    CHECK(n.combo_open() && n.combo_hits() == 1 && n.combo_total() == 450);
    // a count that keeps rising while the number shows is one combo (the game chained the hits): no restart
    n.on_hit(0x100, 50, 74);
    n.on_gauge(true, 2);
    CHECK(n.combo_hits() == 2 && n.combo_total() == 500);

    // requested damage, not the capped one: the glue passes r4 as the game asked (here 25.0 although the cap left only 1.0 to take)
    Readout c;
    const int32_t requested = 2500, cur = 200;
    const int32_t capped = cur - requested < 100 ? (cur > 100 ? cur - 100 : 0) : requested;   // Training's cap in jsp_game.cpp
    CHECK(capped == 100);
    c.on_hit(0x100, requested, 1);
    CHECK(c.last_damage() == 2500 && c.line1() == "Damage 25.0 | Combo 1 hit 25.0");

    // vitals: health in whole points, Stamina as stored
    r.set_vitals(0, 81234, 100000, 1200, 2000);
    r.set_vitals(1, 100000, 100000, 1000, 1000);
    r.set_vitals(2, 1, 1, 1, 1);                                     // out of range: ignored
    r.set_vitals(-1, 1, 1, 1, 1);
    CHECK(r.line2() == "P1 HP 812/1000 ST 1200/2000 | P2 HP 1000/1000 ST 1000/1000");

    // the redraw gate: the first tick draws, unchanged values do not, a change does, invalidate() draws again
    Readout g;
    auto draw = [&g]() { const bool c = g.changed(); if (c) g.drawn(g.text()); return c; };   // the glue, with the overlay taking it
    CHECK(draw() && !draw() && !draw());
    g.set_vitals(0, 100000, 100000, 1000, 1000);
    CHECK(draw() && !draw());
    g.set_vitals(0, 100000, 100000, 1000, 1000);                     // the same values written again
    CHECK(!draw());
    g.set_vitals(0, 100000, 100000, 999, 1000);
    CHECK(draw());
    g.set_vitals(0, 100099, 100000, 999, 1000);                      // a change below one shown point does not redraw
    CHECK(!draw());
    g.invalidate();
    CHECK(draw() && !draw());
    // the overlay refused the drawing (jsp_overlay_begin failed): nothing is recorded, so the next tick tries again
    g.set_vitals(1, 100000, 100000, 1000, 1000);
    CHECK(g.changed() && g.changed());                               // asked twice, not drawn: still due
    CHECK(draw() && !draw());

    // every line is printable ASCII (the overlay font is 0x20-0x7E) and the widest lines fit the box
    Readout w;
    w.on_hit(0x100, 999999, 1);
    for (int i = 0; i < 98; i++) w.on_hit(0x100, 999999, 2);
    w.on_gauge(true, 999);
    w.set_vitals(0, 1000000, 1000000, 2000, 2000);
    w.set_vitals(1, 1000000, 1000000, 2000, 2000);
    CHECK(ascii(w.line1()) && ascii(w.line2()) && ascii(r.line1()) && ascii(r.line2()) && ascii(Readout().line1()) && ascii(Readout().line2()));
    const size_t widest = std::max(w.line1().size(), w.line2().size());
    const float box_w = widest * kGlyphMax * kScale + 2 * kBoxPad;
    CHECK(box_w < 640.0f);                                           // within the right half of the 1280 px layout space
    CHECK(kRight - box_w > 640.0f && kRight <= 1280 - 6);

    // layout: both lines' ink inside the box, the box above the fight HUD's top bar with a margin, lines not overlapping
    const float b1 = kY1 + 21.0f * kScale, b2 = kY2 + 21.0f * kScale;
    CHECK(b1 - 24.0f * kScale >= kBoxY + 1 && b2 + 9.0f * kScale <= kBoxY + kBoxH - 1);
    CHECK(b1 + 9.0f * kScale <= b2 - 24.0f * kScale);
    CHECK(kBoxY + kBoxH + 8 <= kHudTop);

    // rollback: the core rolls back 3 ticks and runs them again (rerun = true, jsp_stepper_in_replay). The damage calls of the re-run ticks
    // repeat hits already counted and the restored memory shows the older combo count: neither changes the readout.
    Readout rb;
    CHECK(rb.on_hit(0x100, 1000, 1));  rb.on_gauge(true, 1);
    CHECK(rb.on_hit(0x100, 800, 2));   rb.on_gauge(true, 2);
    CHECK(rb.on_hit(0x100, 1200, 3));  rb.on_gauge(true, 3);
    rb.set_vitals(1, 70000, 100000, 1000, 1000);
    const std::string before = rb.text();
    CHECK(rb.combo_hits() == 3 && rb.combo_total() == 3000);
    // a rollback to the end of tick 1 and a re-run of ticks 2..3: the restored memory shows the count 1 (a drop from 3 while the number
    // is up, which on a first-run tick means a new combo), then the same two damage calls; the restored health is older
    rb.on_gauge(true, 1, true);
    CHECK(rb.combo_hits() == 3 && rb.combo_total() == 3000 && rb.combo_open());   // not read as a new combo
    CHECK(!rb.on_hit(0x100, 800, 2, true));  rb.on_gauge(true, 2, true);
    CHECK(!rb.on_hit(0x100, 1200, 3, true)); rb.on_gauge(true, 3, true);
    rb.set_vitals(1, 90000, 100000, 1000, 1000, true);
    CHECK(rb.combo_hits() == 3 && rb.combo_total() == 3000 && rb.combo_open());   // counted once
    CHECK(rb.text() == before);
    // a deeper one, to before the first hit (no number up), re-running all three: still counted once
    rb.on_gauge(false, 0, true);
    CHECK(!rb.on_hit(0x100, 1000, 1, true)); rb.on_gauge(true, 1, true);
    CHECK(!rb.on_hit(0x100, 800, 2, true));  rb.on_gauge(true, 2, true);
    CHECK(!rb.on_hit(0x100, 1200, 3, true)); rb.on_gauge(true, 3, true);
    CHECK(rb.combo_hits() == 3 && rb.combo_total() == 3000 && rb.combo_open());
    CHECK(rb.text() == before);
    // the first tick after the replay counts again
    rb.on_gauge(true, 3);
    CHECK(rb.on_hit(0x100, 500, 4)); rb.on_gauge(true, 4);
    CHECK(rb.combo_hits() == 4 && rb.combo_total() == 3500 && rb.last_damage() == 500);
    // the same sequence fed without the flag shows what it prevents: the hits count twice
    Readout dbl;
    for (int pass = 0; pass < 2; pass++) { dbl.on_hit(0x100, 1000, 1); dbl.on_hit(0x100, 800, 2); dbl.on_hit(0x100, 1200, 3); }
    CHECK(dbl.combo_total() == 6000);

    // the shared overlay layer (jptr::layer_step): another user (the Port Settings page or the rollback glue's line / badge) always wins
    CHECK(layer_step(true, false, true) == Layer::Yield && layer_step(true, true, true) == Layer::Yield);
    CHECK(layer_step(false, true, true) == Layer::Yield);           // its drawing is already gone: closing would wipe the other user's
    CHECK(layer_step(false, false, true) == Layer::Yield);
    CHECK(layer_step(false, true, false) == Layer::Close);          // the readout closes only its own drawing
    CHECK(layer_step(false, false, false) == Layer::Idle);
    CHECK(layer_step(true, false, false) == Layer::Draw && layer_step(true, true, false) == Layer::Draw);
    {   // the glue's sequence: drawn, the rollback message line comes up, goes away: the readout yields, then draws once again
        Readout y; bool mine = false; int draws = 0, closes = 0;
        auto tick = [&](bool show, bool others) {
            switch (layer_step(show, mine, others)) {
            case Layer::Yield: mine = false; y.invalidate(); return;
            case Layer::Close: closes++; mine = false; y.invalidate(); return;
            case Layer::Idle: y.invalidate(); return;
            case Layer::Draw: break;
            }
            if (!y.changed()) return;
            draws++; mine = true; y.drawn(y.text());
        };
        tick(true, false); tick(true, false);
        CHECK(draws == 1 && mine);
        tick(true, true); tick(true, true);                          // the other user holds the layer: no draw, no close
        CHECK(draws == 1 && closes == 0 && !mine);
        tick(true, false);                                           // it let go: the readout draws again, once
        tick(true, false);
        CHECK(draws == 2 && mine);
        tick(false, true);                                           // pause menu up while the other user holds the layer: not closed
        CHECK(closes == 0);
        tick(true, false); tick(false, false);                       // drawn again, then hidden: its own drawing is closed
        CHECK(draws == 3 && closes == 1 && !mine);
    }

    // a new fight clears everything
    r.reset();
    CHECK(r.line1() == "Damage - | Combo -" && r.line2() == "P1 - | P2 -" && !r.combo_open() && r.combo_total() == 0);
    CHECK(r.changed());                                              // and is due to be drawn

    if (fails) { std::printf("train readout: %d of %d checks failed\n", fails, checks); return 1; }
    std::printf("train readout: all %d checks pass\n", checks);
    return 0;
}
