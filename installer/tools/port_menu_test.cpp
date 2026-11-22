// Host test for the page state (pad edges, release, swallow flag) of installer/port/jsp/jsp_port_menu.cpp. No game, no guest memory.
#define JSP_PORT_MENU_TEST 1
#include "../port/jsp/jsp_port_menu.cpp"
#include <cstdio>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
using namespace jpm;

struct LayoutBackend : Backend {
    std::string server = "relay.example.test:3658";
    std::string name = "Long_Online_Name";
    int selected_method = 1;
    bool expose_status = false;
    int status_reads = 0;
    JspOnlineStatus observed = jpon::empty();
    bool connection_status(JspOnlineStatus& out) override { ++status_reads; out = observed; return expose_status; }

    bool begin(std::string&) override { return true; }
    std::string row_text(int f) override
    {
        switch (f) {
        case ROW_SERVER: return "Server: " + server;
        case ROW_NAME: return "Online name: " + name;
        case ROW_PRESET: return "Community preset: " + std::string(220, 'W') + "-selected";
        default: return "Method: re-psn";
        }
    }
    std::string get_text(int f) override { return f == ROW_SERVER ? server : name; }
    std::string set_text(int, const std::string&) override { return {}; }
    void cycle(int, int) override {}
    bool save(std::string&) override { return true; }
    const char* charset(int) override { return "abc"; }
    size_t max_len(int f) override { return f == ROW_SERVER ? 260 : 16; }
    int method() override { return selected_method; }
};

struct LayoutIO : SettingsIO {
    bool load(std::string& text, std::string&) override { text.clear(); return true; }
    bool save(const std::string&, std::string&) override { return true; }
    const char* active(const char*) override { return nullptr; }
    void live(const char*, const char*) override {}
    int poll_vk(bool) override { return 0; }
    std::string path() override { return "C:\\Game\\" + std::string(218, 'W') + "\\play_settings.txt"; }
};

struct MenuLayoutCase {
    std::string name;
    Menu menu;
    std::vector<std::string> startup;
};

static std::vector<MenuLayoutCase> layout_fixtures(LayoutIO& io, LayoutBackend& be)
{
    Menu m;
    m.io = &io;
    m.be = &be;
    m.feed(BTN_CROSS, 1, true);
    m.feed(0, 1, true);
    std::vector<MenuLayoutCase> cases{{"hub", m, {}}};
    m.sel = 4;
    cases.push_back({"hub-mods", m, {}});
    m.page = LAUNCHER;
    cases.push_back({"mods-launcher", m, {}});
    m.open_settings(DISPLAY, 0);
    m.model.f.set("RES_SCALE", "3");
    m.smsg = "Saved. Applies on the next start.";
    cases.push_back({"display", m, {}});
    m.open_settings(GRAPHICS, jps::k_item_count);
    m.smsg.clear();
    cases.push_back({"graphics-scrolled", m, {}});
    m.open_settings(CONTROLS, 0);
    cases.push_back({"controls", m, {}});
    m.open_settings(PADBTNS, 9);
    cases.push_back({"controller-buttons", m, {}});
    m.open_settings(KEYS, 1);
    m.capturing = true;
    m.smsg = "F11 is kept for fullscreen.";
    m.smsg_err = true;
    cases.push_back({"keyboard-capture", m, {}});
    m.enter_online();
    m.nc = jpno::NC_ROLLBACK;
    m.rb.repsn = true;
    m.rb.role_auto = false;
    m.osel = (int)m.orows().size() - 1;
    cases.push_back({"online-dense-last", m, {}});
    m.msg = "Could not save to C:\\Example\\" + std::string(210, 'W') + "\\settings.ini. Check the folder permissions and try Save again.";
    m.msg_err = true;
    cases.push_back({"online-long-error", m, {}});
    m.msg.clear();
    m.msg_err = false;
    const auto ids = m.orows();
    m.osel = (int)(std::find(ids.begin(), ids.end(), ROW_SERVER) - ids.begin());
    m.editing = true;
    m.ef = ROW_SERVER;
    m.buf = std::string(250, 'W') + "example";
    m.cpos = 232;
    cases.push_back({"online-edit", m, {}});
    m.be = nullptr;
    m.editing = false;
    m.net_on = false;
    cases.push_back({"online-read-only", m, {"Method: re-psn", "Server: " + std::string(176, 'W') + "-last", "Online name: Guest", "Settings files: 2 read, 0 rejected"}});
    cases.push_back({"online-no-startup", m, {}});
    // A failed reload after Discard can leave the old selection beyond the shorter row list.
    m.be = &be;
    m.net_on = false;
    m.osel = 8;
    cases.push_back({"online-stale-selection", m, {}});
    m.net_on = true;
    m.rb.repsn = true; m.rb.role_auto = true;
    m.nc = m.nc_saved = m.nc_running = jpno::NC_ROLLBACK;
    m.run_method = 0;
    m.run_sig = m.saved_sig = jpno::rb_signature(m.rb.repsn, m.rb.role_auto, m.rb.host, m.rb.relay, m.rb.code, m.rb.port, m.rb.join);
    m.osel = 0;
    m.connection_have = true;
    m.connection_observed = jpon::empty();
    m.connection_observed.phase = static_cast<uint32_t>(jpon::Phase::Verifying);
    cases.push_back({"online-checking-mods", m, {}});
    m.connection_observed.phase = static_cast<uint32_t>(jpon::Phase::Playing);
    m.connection_observed.content_agreed = 1;
    m.connection_observed.path = 2;
    m.connection_observed.input_delay = 3;
    m.connection_observed.last_rollback = 2;
    cases.push_back({"online-live-match", m, {}});
    return cases;
}

struct RecordingLayout {
    struct DrawText { float x; int y; std::string value; float scale; uint32_t color; int style; };
    struct DrawRect { int x, y, w, h; uint32_t color; };
    std::vector<DrawText> texts;
    std::vector<DrawRect> rects;

    // Synthetic proportional advances. The production sink measures the actual atlas.
    float width(const std::string& value, float scale) const
    {
        int pixels = 0;
        for (const char c : value)
            pixels += c == 'W' || c == 'M' ? 28 : c == 'i' || c == 'l' || c == ' ' ? 8 : 16;
        return pixels * scale;
    }
    void text(float x, int y, const std::string& value, float scale, uint32_t color, int style)
    { texts.push_back({x, y, value, scale, color, style}); }
    void rect(int x, int y, int w, int h, uint32_t color) { rects.push_back({x, y, w, h, color}); }
    bool has(const std::string& s) const
    {
        return std::any_of(texts.begin(), texts.end(), [&](const DrawText& t) { return t.value.find(s) != std::string::npos; });
    }
    void check_bounds() const
    {
        for (const DrawRect& r : rects) {
            CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= 1280 && r.y + r.h <= 720);
            if (r.w == 856 && r.h == 32) CHECK(r.y >= 165 && r.y + r.h < 446);
        }
        for (size_t i = 0; i < texts.size(); ++i) {
            const DrawText& t = texts[i];
            const float bottom = t.y + 28 * t.scale;
            CHECK(t.x >= 212 && t.x + width(t.value, t.scale) <= 1068);
            CHECK((t.y > 71 && bottom < 152) || (t.y > 152 && bottom < 446)
                  || (t.y > 446 && bottom < 606) || (t.y > 606 && bottom < 666));
            for (size_t j = i + 1; j < texts.size(); ++j) {
                const DrawText& u = texts[j];
                CHECK(t.y >= u.y + 28 * u.scale || u.y >= bottom
                      || t.x >= u.x + width(u.value, u.scale) || u.x >= t.x + width(t.value, t.scale));
            }
        }
    }
};

static void check_layout()
{
    LayoutIO io;
    LayoutBackend backend;
    Menu observer;
    observer.page = ONLINE; observer.be = &backend;
    backend.expose_status = true;
    backend.observed.phase = static_cast<uint32_t>(jpon::Phase::Verifying);
    CHECK(observer.poll_connection_status(100) && observer.take_dirty());
    const int reads = backend.status_reads;
    backend.observed.phase = static_cast<uint32_t>(jpon::Phase::Ended);
    std::snprintf(backend.observed.reason, sizeof backend.observed.reason, "%s", "The mod content differs from the other player.");
    CHECK(!observer.poll_connection_status(200) && backend.status_reads == reads);
    CHECK(observer.poll_connection_status(350) && observer.take_dirty());
    RecordingLayout ended;
    menu_view::draw(ended, observer);
    CHECK(ended.has("Online session ended") && ended.has("mod content differs"));
    backend.expose_status = false;
    CHECK(observer.poll_connection_status(600) && !observer.connection_have);
    const int unavailable_reads = backend.status_reads;
    CHECK(!observer.poll_connection_status(610) && backend.status_reads == unavailable_reads);
    const auto fixtures = layout_fixtures(io, backend);
    for (const auto& fixture : fixtures) {
        RecordingLayout d;
        menu_view::draw(d, fixture.menu, fixture.startup);
        d.check_bounds();
        CHECK(d.has(menu_view::title(fixture.menu.page)));
        if (fixture.name == "hub") CHECK(!d.has("Settings file:") && d.has("Choose your render resolution"));
        if (fixture.name == "hub-mods") CHECK(d.has("Find Mods"));
        if (fixture.name == "mods-launcher") CHECK(d.has("Mods tab") && d.has("Find on LAN") && d.has("Cross or Circle"));
        if (fixture.name == "display") CHECK(d.has("4K *") && d.has("Saved. Applies"));
        if (fixture.name == "keyboard-capture") CHECK(d.has("Press a key") && d.has("F11 stays") && d.has("Esc or Circle"));
        if (fixture.name == "online-long-error") {
            std::string message;
            for (const auto& text : d.texts)
                if (text.color == menu_view::error) message += text.value + ' ';
            CHECK(message.starts_with("Could not save") && message.find("try Save again.") != std::string::npos);
            CHECK(d.has("This session:"));
            CHECK(!d.has("shortened"));
            CHECK(std::any_of(d.texts.begin(), d.texts.end(), [](const auto& t) { return t.value.starts_with("Could not save") && t.color == menu_view::error; }));
        }
        if (fixture.name == "online-edit") {
            CHECK(d.has("Server: ...") && d.has("Square  Delete") && d.has("Circle  Cancel"));
            CHECK(std::any_of(d.rects.begin(), d.rects.end(), [](const auto& r) { return r.color == menu_view::caret_ink && r.h == 3 && r.x + r.w < 1068; }));
        }
        if (fixture.name == "online-read-only") CHECK(d.has("-last") && d.has("Read only") && d.has("Circle  Back"));
        if (fixture.name == "online-no-startup") CHECK(d.has("No online settings were read"));
        if (fixture.name == "online-stale-selection") {
            CHECK(d.has("Discard changes") && d.has("Cross  Discard"));
            CHECK(std::count_if(d.texts.begin(), d.texts.end(), [](const auto& t) { return t.style == 1; }) == 1);
        }
        if (fixture.name != "online-dense-last") continue;
        Menu m = fixture.menu;
        const auto ids = m.orows();
        CHECK(ids.size() > 8 && d.has("More above") && d.has("Discard changes"));
        m.osel = 0;
        RecordingLayout choice;
        menu_view::draw(choice, m);
        CHECK(choice.has("Left / Right  Change") && !choice.has("or Cross  Change"));
        for (int i = 0; i < (int)ids.size(); ++i) {
            RecordingLayout scrolled;
            m.osel = i;
            menu_view::draw(scrolled, m);
            scrolled.check_bounds();
            const auto is_selected = [](const auto& t) { return t.style == 1; };
            const auto selected = std::find_if(scrolled.texts.begin(), scrolled.texts.end(), is_selected);
            CHECK(selected != scrolled.texts.end() && std::count_if(scrolled.texts.begin(), scrolled.texts.end(), is_selected) == 1);
            const int row = ids[(size_t)i];
            const std::string expected = row <= ROW_PRESET ? backend.row_text(row) : jpno::row_text(row, m.nc, m.rb, backend.method());
            CHECK(selected != scrolled.texts.end() && selected->value == expected && selected->y < 446);
            CHECK(scrolled.has("More") && scrolled.has(i == 0 ? "More below" : "Rows "));
        }
        m.nc = jpno::NC_DELAY;
        m.osel = 1;
        backend.selected_method = 2;
        RecordingLayout preset;
        menu_view::draw(preset, m);
        preset.check_bounds();
        CHECK(preset.has("Community preset:") && preset.has("-selected") && !preset.has("shortened"));
    }
}

int main()
{
    check_layout();
    // cursor struct
    int32_t t = 5, v = 5, l = 4;
    CHECK(six_rows(&t, &v, &l) && t == 6 && v == 6 && l == 5);
    CHECK(!six_rows(&t, &v, &l) && t == 6);          // already six: unchanged
    int32_t t2 = 4, v2 = 4, l2 = 3;
    CHECK(!six_rows(&t2, &v2, &l2) && t2 == 4);      // another layout: untouched
    CHECK(row_of(5, 0) == kPortRow && row_of(7, 2) == 5);
    // JSP_PORT_MENU: on unless it starts with '0'
    CHECK(port_menu_on(nullptr) && port_menu_on("") && port_menu_on("1") && port_menu_on("yes"));
    CHECK(!port_menu_on("0") && !port_menu_on("00"));

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
