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


    // ---- the editable Online page (fake backend) ----
    struct Fake : Backend {
        int method = 0; std::string server = "", name = "", preset = "";
        int begins = 0, saves = 0; bool refuse = false;
        bool begin(std::string&) override { begins++; method = 0; server.clear(); name.clear(); return true; }
        std::string row_text(int f) override { return f == 0 ? (method ? "re" : "off") : f == 1 ? server : f == 2 ? name : preset; }
        std::string get_text(int f) override { return f == 1 ? server : name; }
        std::string set_text(int f, const std::string& v) override { (f == 1 ? server : name) = v; return v.size() < 3 ? "short" : ""; }
        void cycle(int, int d) override { method = ((method + d) % 3 + 3) % 3; }
        bool save(std::string& m) override { saves++; m = refuse ? "refused" : "Saved. Applies on the next start."; return !refuse; }
        const char* charset(int) override { return "abc.-9"; }
        size_t max_len(int f) override { return f == 1 ? 8 : 4; }
    } fk;
    Menu e; e.be = &fk;
    auto press = [&](uint16_t b) { e.feed(b, 1, true); e.feed(0, 1, true); };
    press(BTN_CROSS); press(BTN_CROSS);                         // closed -> main -> online
    CHECK(e.page == ONLINE && fk.begins == 1 && e.osel == 0 && !e.edited);
    press(BTN_RIGHT); CHECK(fk.method == 1 && e.edited && e.msg == "Not saved yet.");
    press(BTN_LEFT); press(BTN_LEFT); CHECK(fk.method == 2);    // wraps
    press(BTN_RIGHT); CHECK(fk.method == 0);
    press(BTN_UP); CHECK(e.osel == kEditRows - 1);               // wraps to Discard
    press(BTN_DOWN); CHECK(e.osel == 0);
    press(BTN_RIGHT); CHECK(fk.method == 1);
    // picker on Server: Down on the empty slot appends the first letter, Down again moves along the set, Right adds a slot
    press(BTN_DOWN);
    CHECK(e.osel == ROW_SERVER);
    press(BTN_CROSS); CHECK(e.editing && e.ef == ROW_SERVER && e.buf.empty());
    press(BTN_DOWN); CHECK(e.buf == "a");
    press(BTN_DOWN); CHECK(e.buf == "b");
    press(BTN_UP); press(BTN_UP); CHECK(e.buf == "9");           // wraps backwards over the set
    press(BTN_RIGHT); CHECK(e.cpos == 1);
    press(BTN_DOWN); CHECK(e.buf == "9a");
    press(BTN_R1); CHECK(e.buf == "9" + std::string(1, "abc.-9"[(0 + 10) % 6]));   // ten steps wrap
    press(BTN_LEFT); press(BTN_SQUARE); CHECK(e.buf == std::string(1, "abc.-9"[4]));  // delete at the position
    press(BTN_DOWN); press(BTN_DOWN); press(BTN_DOWN); CHECK(e.buf == "b");       // '-' + 3 steps = 'b'
    for (int i = 0; i < 12; i++) { press(BTN_RIGHT); press(BTN_DOWN); }
    CHECK(e.buf.size() <= 8);                                    // max length 8
    press(BTN_CIRCLE); CHECK(!e.editing && fk.server.empty() && e.page == ONLINE);   // cancel changes nothing
    press(BTN_CROSS); press(BTN_DOWN); press(BTN_RIGHT); press(BTN_DOWN); press(BTN_DOWN); press(BTN_CROSS);   // reopen, type 'ab', accept
    CHECK(!e.editing && fk.server == "ab" && e.msg_err && e.msg == "short");          // accepted but not valid yet: hint, stored
    press(BTN_DOWN); press(BTN_CROSS); press(BTN_DOWN); press(BTN_RIGHT); press(BTN_DOWN); press(BTN_RIGHT); press(BTN_DOWN); press(BTN_CROSS);
    CHECK(fk.name == "aaa" && e.msg == "Not saved yet." && !e.msg_err);
    // Save row, then Circle leaves
    press(BTN_DOWN); press(BTN_DOWN); CHECK(e.osel == ROW_SAVE);
    press(BTN_CROSS); CHECK(fk.saves == 1 && e.msg == "Saved. Applies on the next start." && !e.edited && !e.msg_err);
    e.osel = ROW_METHOD; press(BTN_RIGHT); fk.refuse = true;     // dirty again
    e.osel = ROW_SAVE; press(BTN_CROSS); CHECK(fk.saves == 2 && e.msg_err && e.msg == "refused" && e.edited);
    e.osel = ROW_DISCARD; press(BTN_CROSS); CHECK(fk.begins == 2 && !e.edited && e.msg == "Changes discarded." && e.page == ONLINE);
    e.osel = ROW_METHOD; press(BTN_RIGHT); CHECK(e.edited);
    press(BTN_CIRCLE); CHECK(e.page == MAIN && fk.saves == 2);   // Circle: back, no save
    bool logged_back = false; for (auto& ev : e.events) if (ev.find("unsaved changes dropped") != std::string::npos) logged_back = true;
    CHECK(logged_back);
    press(BTN_DOWN); press(BTN_CROSS); CHECK(e.page == CLOSED);    // Close
    // without a backend the page stays read only (covered above)

    if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
    std::printf("port menu: all checks passed\n");
    return 0;
}
