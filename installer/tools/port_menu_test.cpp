// Host test for the page state (pad edges, release, swallow flag) of installer/port/jsp/jsp_port_menu.cpp. No game, no guest memory.
#define JSP_PORT_MENU_TEST 1
#include "../port/jsp/jsp_port_menu.cpp"
#include <cstdio>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
using namespace jpm;

int main()
{
    // cursor struct
    int32_t t = 5, v = 5, l = 4;
    CHECK(six_rows(&t, &v, &l) && t == 6 && v == 6 && l == 5);
    CHECK(!six_rows(&t, &v, &l) && t == 6);          // already six: unchanged
    int32_t t2 = 4, v2 = 4, l2 = 3;
    CHECK(!six_rows(&t2, &v2, &l2) && t2 == 4);      // another layout: untouched
    CHECK(row_of(5, 0) == kPortRow && row_of(7, 2) == 5);

    // closed: Cross outside the sixth row is the game's
    Menu m;
    CHECK(!m.feed(BTN_CROSS, 1, false) && m.page == CLOSED);
    CHECK(!m.feed(0, 1, false));
    // Cross on the row opens, swallowed; the held Cross is no press inside the page
    CHECK(m.feed(BTN_CROSS, 1, true) && m.page == MAIN && m.sel == 0 && m.take_dirty() && !m.take_dirty());
    CHECK(m.feed(BTN_CROSS, 1, true) && m.page == MAIN);       // still held: no edge
    CHECK(m.feed(0, 1, true));
    // a drain read changes nothing but still hides input
    CHECK(m.feed(BTN_DOWN, 0, true) && m.sel == 0);
    // Down / Up wrap over the 2 rows, hold gives one step
    CHECK(m.feed(BTN_DOWN, 1, true) && m.sel == 1);
    CHECK(m.feed(BTN_DOWN, 1, true) && m.sel == 1);
    CHECK(m.feed(0, 1, true));
    CHECK(m.feed(BTN_DOWN, 1, true) && m.sel == 0);            // wrap down
    CHECK(m.feed(0, 1, true));
    CHECK(m.feed(BTN_UP, 1, true) && m.sel == 1);              // wrap up
    CHECK(m.feed(0, 1, true));
    CHECK(m.feed(BTN_UP, 1, true) && m.sel == 0);
    CHECK(m.feed(0, 1, true));
    // Cross on "Online" opens the sub-page; Up/Down do nothing there; Circle goes back
    CHECK(m.feed(BTN_CROSS, 1, true) && m.page == ONLINE);
    CHECK(m.feed(0, 1, true));
    CHECK(m.feed(BTN_DOWN, 1, true) && m.sel == 0 && m.page == ONLINE);
    CHECK(m.feed(0, 1, true));
    CHECK(m.feed(BTN_CROSS, 1, true) && m.page == ONLINE);     // read only: Cross does nothing
    CHECK(m.feed(0, 1, true));
    CHECK(m.feed(BTN_CIRCLE, 1, true) && m.page == MAIN && m.sel == 0);
    CHECK(m.feed(0, 1, true));
    // Circle closes; the game sees nothing until all buttons are up (a held Circle is no press on the panel)
    CHECK(m.feed(BTN_CIRCLE, 1, true) && m.page == CLOSED && m.wait_release && m.active());
    CHECK(m.feed(BTN_CIRCLE, 1, true));
    CHECK(m.feed(BTN_CIRCLE, 0, false));                       // drain read: still hidden
    CHECK(m.feed(BTN_CIRCLE | BTN_UP, 1, false));
    CHECK(!m.feed(0, 1, false) && !m.wait_release && !m.active());
    CHECK(!m.feed(BTN_CIRCLE, 1, false));                      // now the game gets it again
    CHECK(m.feed(0, 1, false) == false);

    // Cross held while waiting for release must not reopen the page
    Menu n;
    CHECK(n.feed(BTN_CROSS, 1, true) && n.page == MAIN);
    CHECK(n.feed(0, 1, true));
    CHECK(n.feed(BTN_DOWN, 1, true) && n.sel == 1);
    CHECK(n.feed(0, 1, true));
    CHECK(n.feed(BTN_CROSS, 1, true) && n.page == CLOSED && n.wait_release);   // "Close"
    CHECK(n.feed(BTN_CROSS, 1, true) && n.page == CLOSED);                     // held, in the row: no reopen
    CHECK(!n.feed(0, 1, true) && n.page == CLOSED);
    CHECK(n.feed(BTN_CROSS, 1, true) && n.page == MAIN);                       // a new press reopens

    // Cross pressed while the button was already held when the cursor arrives on the row: edge only
    Menu o;
    CHECK(!o.feed(BTN_CROSS, 1, false));
    CHECK(!o.feed(BTN_CROSS, 1, true) && o.page == CLOSED);

    if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
    std::printf("port menu: all checks passed\n");
    return 0;
}
