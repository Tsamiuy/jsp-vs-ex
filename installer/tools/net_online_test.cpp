// ENHANCEMENT (in-game netcode option): host test of the Online page's Netcode choice (Off / Delay / Rollback): jsp_net_online.h and the page state in
// jsp_port_menu.cpp (see installer/tools/net_online_test.sh). Fake backend and fake settings file: no game.
// Covers: rows per choice, the saved state read back (incl. a hand-edited conflict), mutual exclusion in the working copy, the save order (the method that goes
// off is written first), persistence in play_settings.txt (other lines kept), validation refusals, env / file precedence helpers, the status line (restart needed).
#define JSP_PORT_MENU_TEST 1
#include "jsp_port_menu.cpp"   // the page and the helpers (-I installer/port/jsp)
#include <cstdio>
#include <map>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
using namespace jpm;
using jpno::NC_OFF; using jpno::NC_DELAY; using jpno::NC_ROLLBACK; using jpno::NC_CONFLICT; using jpno::NC_NATIVE;

static std::vector<std::string> g_log;   // order of the writes: "be.save" / "file.save"

struct Fake : Backend {
    int m = 0, saved_m = 0; std::string server = "example.invalid", name = "tester", preset = "";
    bool refuse = false; std::string check_fail;
    bool begin(std::string&) override { m = saved_m; return true; }
    std::string row_text(int f) override { return f == 0 ? (m == 1 ? "Method: re-psn" : m == 2 ? "Method: Community server" : "Method: Off") : f == 1 ? "Server: " + server : f == 2 ? "Online name: " + name : "Community preset: " + preset; }
    std::string get_text(int f) override { return f == 1 ? server : name; }
    std::string set_text(int f, const std::string& v) override { (f == 1 ? server : name) = v; return ""; }
    void cycle(int, int d) override { preset = d > 0 ? "b" : "a"; }
    bool save(std::string& msg) override { g_log.push_back("be.save"); if (refuse) { msg = "refused"; return false; } saved_m = m; msg = "Saved. Applies on the next start."; return true; }
    const char* charset(int) override { return "abc"; }
    size_t max_len(int) override { return 16; }
    int method() override { return m; }
    void set_method(int v) override { m = v; }
    std::string check() override { return check_fail; }
};
struct FakeIO : SettingsIO {
    std::string text; bool fail_save = false; std::map<std::string, std::string> env;
    bool load(std::string& t, std::string&) override { t = text; return true; }
    bool save(const std::string& t, std::string& err) override { g_log.push_back("file.save"); if (fail_save) { err = "disk full"; return false; } text = t; return true; }
    const char* active(const char* k) override { auto it = env.find(k); return it == env.end() ? nullptr : it->second.c_str(); }
    void live(const char*, const char*) override {}
    int poll_vk(bool) override { return 0; }
    std::string path() override { return "fake"; }
};

static void press(Menu& e, uint16_t b) { e.feed(b, 1, true); e.feed(0, 1, true); }
static std::string val(const std::string& t, const char* k) { std::string v; jps::File::parse(t).get(k, v); return v; }
static bool present(const std::string& t, const char* k) { std::string v; return jps::File::parse(t).get(k, v); }
static std::vector<int> V(std::initializer_list<int> l) { return std::vector<int>(l); }

int main()
{
    using namespace jpno;
    // ---- validators (the page's rules are parse_endpoint's: a.b.c.d:port) ----
    CHECK(valid_port("36200") && valid_port("1") && valid_port("65535") && !valid_port("0") && !valid_port("65536") && !valid_port("") && !valid_port("12a") && !valid_port("123456"));
    CHECK(valid_join("192.0.2.10:36200") && valid_join("127.0.0.1:1") && !valid_join("192.0.2.10") && !valid_join("192.0.2:5") && !valid_join("192.0.2.256:5")
          && !valid_join("192.0.2.10:0") && !valid_join("host.example:5") && !valid_join(":5") && !valid_join("1.2.3.4.5:5") && !valid_join("1..3.4:5"));
    // ---- rows per choice ----
    CHECK(rows_for(NC_OFF, true) == V({ R_NETCODE, R_SAVE, R_DISCARD }));
    CHECK(rows_for(NC_DELAY, true) == V({ R_NETCODE, R_METHOD, R_SERVER, R_NAME, R_PRESET, R_SAVE, R_DISCARD }));
    CHECK(rows_for(NC_ROLLBACK, true) == V({ R_NETCODE, R_CONN, R_ROLE, R_PORT, R_SAVE, R_DISCARD }));
    CHECK(rows_for(NC_ROLLBACK, false) == V({ R_NETCODE, R_CONN, R_ROLE, R_JOIN, R_SAVE, R_DISCARD }));
    // ---- saved state and running state ----
    { jps::File f; CHECK(saved_netcode(f, 0) == NC_OFF && saved_netcode(f, 1) == NC_DELAY);
      f = jps::File::parse("JSP_ROLLBACK=1\r\n"); CHECK(saved_netcode(f, 0) == NC_ROLLBACK && saved_netcode(f, 2) == NC_CONFLICT);
      f = jps::File::parse("JSP_ROLLBACK=0\r\n"); CHECK(saved_netcode(f, 0) == NC_OFF && saved_netcode(f, 1) == NC_DELAY);
      // ENHANCEMENT (native-room pairing): rollback without a role key + re-psn is Online Battle + rollback; with a role key it stays the refused pair
      f = jps::File::parse("JSP_ROLLBACK=1\r\nJSP_ROLLBACK_RELAY=1\r\n"); CHECK(saved_netcode(f, 1) == NC_NATIVE && saved_netcode(f, 0) == NC_ROLLBACK && saved_netcode(f, 2) == NC_CONFLICT);
      for (const char* rk : { "JSP_ROLLBACK_LISTEN=36200", "JSP_ROLLBACK_JOIN=192.0.2.1:5", "JSP_ROLLBACK_VIA=repsn", "JSP_ROLLBACK_BENCH=2" }) {
          f = jps::File::parse(std::string("JSP_ROLLBACK=1\r\n") + rk + "\r\n"); CHECK(saved_netcode(f, 1) == NC_CONFLICT); } }
    { std::map<std::string, const char*> env;
      auto get = [&](const char* k) -> const char* { auto it = env.find(k); return it == env.end() ? nullptr : it->second; };
      CHECK(running_netcode(get) == NC_OFF);
      env["PS3_NET_ONLINE"] = "1"; CHECK(running_netcode(get) == NC_DELAY);
      env["JSP_ROLLBACK"] = "1"; CHECK(running_netcode(get) == NC_NATIVE);   // the lobby start next to the game's own online (native-room pairing)
      env["JSP_ROLLBACK_LISTEN"] = "36200"; CHECK(running_netcode(get) == NC_CONFLICT); env.erase("JSP_ROLLBACK_LISTEN");   // a role key with it: the refused pair
      // the runtime's own test (cellNetCtl.c / np_psnr.c): any value of PS3_NET_ONLINE, "0" too, is online
      env["PS3_NET_ONLINE"] = "0"; CHECK(running_netcode(get) == NC_NATIVE);
      env.erase("PS3_NET_ONLINE"); CHECK(running_netcode(get) == NC_ROLLBACK);
      env["JSP_ROLLBACK"] = "0"; CHECK(running_netcode(get) == NC_OFF);                 // "0" is off for JSP_ROLLBACK
      env["PS3_NET_ONLINE"] = "0"; env["JSP_ROLLBACK"] = ""; CHECK(running_netcode(get) == NC_DELAY);
      env.clear(); env["JSP_NET_REFUSED"] = "1"; CHECK(running_netcode(get) == NC_CONFLICT);   // a refused pair is reported as such although both were cleared
    }
    // ---- the start decision (EnvInit side): refusal sets NO rollback defaults, clears both, marks the refusal ----
    { std::map<std::string, std::string> env; std::vector<std::string> own; std::string why;
      auto get = [&](const char* k) -> const char* { auto it = env.find(k); return it == env.end() ? nullptr : it->second.c_str(); };
      auto set = [&](const char* k, const char* v) { if (v && *v) env[k] = v; else env.erase(k); };   // "" / null remove, as _putenv_s does
      CHECK(resolve_env(get, set, &own, &why) == Boot::NotRequested && env.empty() && own.empty());
      env["JSP_ROLLBACK"] = "0"; env["PS3_NET_ONLINE"] = "1"; CHECK(resolve_env(get, set, &own, &why) == Boot::NotRequested && env.size() == 2);   // JSP_ROLLBACK=0 is off, online untouched
      // JSP_ROLLBACK=0 with a join address left in the settings: still not requested, and nothing is written (no set() call)
      { int sets = 0; auto counted = [&](const char* k, const char* v) { sets++; set(k, v); };
        env.clear(); own.clear(); env["JSP_ROLLBACK"] = "0"; env["JSP_ROLLBACK_JOIN"] = "192.0.2.1:36200";
        CHECK(resolve_env(get, counted, &own, &why) == Boot::NotRequested && sets == 0 && env.size() == 2 && own.empty()); }
      // the pair, with PS3_NET_ONLINE = "1" and = "0" (the runtime counts any value)
      for (const char* pv : { "1", "0" }) {
          env.clear(); own.clear(); env["JSP_ROLLBACK"] = "1"; env["JSP_ROLLBACK_LISTEN"] = "36200"; env["PS3_NET_ONLINE"] = pv; env["PSNR_SERVER"] = "example.invalid";
          CHECK(resolve_env(get, set, &own, &why) == Boot::Refused && own.empty());
          CHECK(!get("JSP_SNAPSHOT") && !get("JSP_FIGHT_HASH") && !get("JSP_STEP_TURBO") && !get("JSP_SNAPSHOT_THREADS"));   // none of the rollback defaults
          CHECK(std::string(get("JSP_ROLLBACK")) == "0" && !get("PS3_NET_ONLINE") && std::string(get("JSP_NET_REFUSED")) == "1" && why.find("both on") != std::string::npos);
          // the page then reads the real state: nothing runs, reported as refused
          CHECK(running_netcode(get) == NC_CONFLICT);
          // and install (jsp_net_game_install) sees JSP_ROLLBACK off
          CHECK(!flag_on(get("JSP_ROLLBACK")));
      }
      // role errors: Rollback refused alone, the game's own online (not on here) and everything else untouched, no defaults
      const char* bad[][2] = { {"36200", "192.0.2.1:5"},{"0", ""}, {"", "host.example:5"}, {"", "192.0.2.1:0"} };
      for (auto& b : bad) {
          env.clear(); own.clear(); env["JSP_ROLLBACK"] = "1"; if (b[0][0]) env["JSP_ROLLBACK_LISTEN"] = b[0]; if (b[1][0]) env["JSP_ROLLBACK_JOIN"] = b[1];
          CHECK(resolve_env(get, set, &own, &why) == Boot::Refused && !get("JSP_SNAPSHOT") && std::string(get("JSP_ROLLBACK")) == "0" && std::string(get("JSP_NET_REFUSED")) == "1" && running_netcode(get) == NC_CONFLICT);   // a role-key refusal reports as refused too
      }
      // a valid start: the defaults are set, a value the player set stays, the names are recorded
      env.clear(); own.clear(); env["JSP_ROLLBACK"] = "1"; env["JSP_ROLLBACK_JOIN"] = "192.0.2.1:36200"; env["JSP_SNAPSHOT_QUIESCE"] = "1";
      CHECK(resolve_env(get, set, &own, &why) == Boot::Go && std::string(get("JSP_SNAPSHOT")) == "1" && std::string(get("JSP_STEP_TURBO")) == "1"
            && std::string(get("JSP_SNAPSHOT_QUIESCE")) == "1" && own.size() == sizeof kDefaults / sizeof kDefaults[0] - 1 && std::string(get("JSP_ROLLBACK")) == "1");
      // ENHANCEMENT (lobby): JSP_ROLLBACK=1 with NO role key is Go with the lobby flag: every default set (as for a match start), nothing refused, no session yet
      for (const char* via : { (const char*)nullptr, "direct", "" }) {
          env.clear(); own.clear(); env["JSP_ROLLBACK"] = "1"; if (via) env["JSP_ROLLBACK_VIA"] = via; bool lob = false; Refusal rc = Refusal::Count;
          CHECK(resolve_env(get, set, &own, &why, &rc, &lob) == Boot::Go && lob && rc == Refusal::None && own.size() == sizeof kDefaults / sizeof kDefaults[0]);
          for (const KV& kv : kDefaults) CHECK(get(kv.k) && std::string(get(kv.k)) == kv.v);
          CHECK(std::string(get("JSP_ROLLBACK")) == "1" && !get("JSP_NET_REFUSED") && running_netcode(get) == NC_ROLLBACK);
      }
      // the lobby flag is only for that case: every role start and the bench clear it; the invalid combinations stay refused (and are not lobbies)
      { struct Case { std::map<std::string, std::string> e; Boot b; Refusal r; };
        const Case cases[] = {
            { { { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_LISTEN", "36200" } }, Boot::Go, Refusal::None },
            { { { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_JOIN", "192.0.2.1:36200" } }, Boot::Go, Refusal::None },
            { { { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_VIA", "repsn" }, { "JSP_ROLLBACK_ROLE", "join" } }, Boot::Go, Refusal::None },
            { { { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_BENCH", "2" } }, Boot::Go, Refusal::None },
            { { { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_LISTEN", "36200" }, { "JSP_ROLLBACK_JOIN", "192.0.2.1:5" } }, Boot::Refused, Refusal::RoleKeys },
            { { { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_VIA", "repsn" } }, Boot::Refused, Refusal::RepsnNoRole },
            { { { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_LISTEN", "0" } }, Boot::Refused, Refusal::BadListen },
            { { { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_JOIN", "host.example:5" } }, Boot::Refused, Refusal::BadJoin },
            { { { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_BENCH", "2" }, { "JSP_ROLLBACK_VIA", "direct" } }, Boot::Refused, Refusal::RoleKeys },
        };
        for (const Case& c : cases) {
            env = c.e; own.clear(); bool lob = true; Refusal rc = Refusal::Count;
            CHECK(resolve_env(get, set, &own, &why, &rc, &lob) == c.b && rc == c.r && !lob);
            if (c.b == Boot::Refused) CHECK(!get("JSP_SNAPSHOT") && std::string(get("JSP_ROLLBACK")) == "0" && std::string(get("JSP_NET_REFUSED")) == "1");
        }
        env = { { "JSP_ROLLBACK", "0" } }; { bool lob = true; CHECK(resolve_env(get, set, &own, &why, nullptr, &lob) == Boot::NotRequested && !lob); }
        // ENHANCEMENT (native-room pairing): the lobby start WITH the game's own online on is Go (lobby), the online stays on; with any role key it is the refused pair
        for (const char* pv : { "1", "0" }) {
            env = { { "JSP_ROLLBACK", "1" }, { "PS3_NET_ONLINE", pv }, { "PSNR_SERVER", "example.invalid" } }; own.clear(); bool lob = false; Refusal rc = Refusal::Count;
            CHECK(resolve_env(get, set, &own, &why, &rc, &lob) == Boot::Go && lob && rc == Refusal::None && get("PS3_NET_ONLINE") && std::string(get("PS3_NET_ONLINE")) == pv);
            CHECK(std::string(get("JSP_ROLLBACK")) == "1" && !get("JSP_NET_REFUSED") && get("JSP_SNAPSHOT") && running_netcode(get) == NC_NATIVE);
        }
        for (const char* rk : { "JSP_ROLLBACK_LISTEN", "JSP_ROLLBACK_JOIN", "JSP_ROLLBACK_VIA", "JSP_ROLLBACK_BENCH" }) {
            env = { { "JSP_ROLLBACK", "1" }, { "PS3_NET_ONLINE", "1" }, { rk, "2" } }; own.clear(); bool lob = true; Refusal rc = Refusal::Count;
            CHECK(resolve_env(get, set, &own, &why, &rc, &lob) == Boot::Refused && rc == Refusal::BothOn && !lob && !get("PS3_NET_ONLINE") && !get("JSP_SNAPSHOT"));
        }
      }
    }
    // ---- the rendezvous: the decision is taken once, by whichever of the two static constructors arrives last, in either order ----
    for (int first = 1; first <= 2; first++) {
        Rendezvous rv; int calls = 0; auto d = [&] { calls++; };
        CHECK(!rv.arrive(first, d) && calls == 0);
        CHECK(!rv.arrive(first, d) && calls == 0);                  // the same part twice does not count
        CHECK(rv.arrive(3 - first, d) && calls == 1 && rv.order[0] == first && rv.order[1] == 3 - first);
        CHECK(!rv.arrive(first, d) && !rv.arrive(3 - first, d) && calls == 1);   // decided once
    }
    { bool r; std::string s = status_line(NC_OFF, NC_OFF, &r); CHECK(!r && s.find("Restart") == std::string::npos);
      s = status_line(NC_OFF, NC_ROLLBACK, &r); CHECK(r && s.find("Restart to apply") != std::string::npos && s.find("Rollback") != std::string::npos);
      s = status_line(NC_CONFLICT, NC_OFF, &r); CHECK(r && s.find("refused") != std::string::npos);
      // a gameplay mod set turned the game's own online off: the cause, no "Restart to apply" (a restart does not help while the mods are on)
      s = status_line(NC_OFF, NC_DELAY, &r, true); CHECK(!r && s == "This session: off (gameplay mods are on); turn the mods off to play online.");
      s = status_line(NC_OFF, NC_ROLLBACK, &r, true); CHECK(r && s.find("Restart to apply") != std::string::npos);   // another saved choice: as before
      s = status_line(NC_OFF, NC_DELAY, &r, false); CHECK(r && s.find("Restart to apply") != std::string::npos);
      for (char ch : status_line(NC_OFF, NC_DELAY, &r, true)) CHECK(ch >= 0x20 && ch <= 0x7E); }

    // ---- the page: no settings file access (io == nullptr): the older page, row for row ----
    { Fake fk; Menu e; e.be = &fk; e.enter_online();
      CHECK(!e.net_on && e.orows() == V({ 0, 1, 2, 3, 4, 5 }) && e.orow() == 0); press(e, BTN_UP); CHECK(e.osel == 5); }

    // ---- first visit with no file: Off, one row set, defaults ----
    Fake fk; FakeIO io; Menu e; e.be = &fk; e.io = &io;
    e.enter_online();
    CHECK(e.net_on && e.nc == NC_OFF && e.nc_saved == NC_OFF && e.orows() == V({ R_NETCODE, R_SAVE, R_DISCARD }) && e.rb.host && e.rb.port == kDefaultPort && e.net_note.empty());
    // Right: Off -> Delay (re-psn by default), Right: Rollback (method off), Left back
    press(e, BTN_RIGHT); CHECK(e.nc == NC_DELAY && fk.m == 1 && e.edited && e.osel == 0 && e.orows().size() == 7);
    press(e, BTN_RIGHT); CHECK(e.nc == NC_ROLLBACK && fk.m == 0 && e.orows() == V({ R_NETCODE, R_CONN, R_ROLE, R_PORT, R_SAVE, R_DISCARD }));   // exclusion: the game's own online is off in the working copy
    press(e, BTN_RIGHT); CHECK(e.nc == NC_NATIVE && fk.m == 1 && e.orows() == V({ R_NETCODE, R_SERVER, R_NAME, R_RELAY, R_SAVE, R_DISCARD }));   // Online Battle + rollback: re-psn on
    press(e, BTN_RIGHT); CHECK(e.nc == NC_OFF && fk.m == 0);
    press(e, BTN_LEFT); CHECK(e.nc == NC_NATIVE && fk.m == 1); press(e, BTN_LEFT); CHECK(e.nc == NC_ROLLBACK && fk.m == 0);
    // Delay: the Method row only toggles re-psn / Community (Off is the Netcode row)
    press(e, BTN_LEFT); CHECK(e.nc == NC_DELAY && fk.m == 1); press(e, BTN_DOWN); CHECK(e.orow() == R_METHOD);
    press(e, BTN_RIGHT); CHECK(fk.m == 2); press(e, BTN_RIGHT); CHECK(fk.m == 1);
    // ---- Rollback host: port by the picker ----
    e.osel = 0; press(e, BTN_RIGHT); CHECK(e.nc == NC_ROLLBACK);
    press(e, BTN_DOWN); CHECK(e.orow() == R_CONN); press(e, BTN_DOWN); CHECK(e.orow() == R_ROLE); press(e, BTN_DOWN); CHECK(e.orow() == R_PORT);
    press(e, BTN_CROSS); CHECK(e.editing && e.ef == R_PORT && e.buf == kDefaultPort);
    for (int i = 0; i < 6; i++) press(e, BTN_SQUARE);            // clear
    CHECK(e.buf.empty());
    press(e, BTN_DOWN); CHECK(e.buf == "0");                      // digits only: first letter of the set is '0'
    press(e, BTN_DOWN); press(e, BTN_DOWN); CHECK(e.buf == "2");
    for (int i = 0; i < 12; i++) { press(e, BTN_RIGHT); press(e, BTN_DOWN); }
    CHECK(e.buf.size() <= 5);                                     // port max 5 digits
    press(e, BTN_CROSS); CHECK(!e.editing && !e.msg.empty());     // accepted; a bad value gets a hint, a good one "Not saved yet."
    e.rb.port = "7777";
    // ---- Role: Join shows the address row, Host the port row ----
    press(e, BTN_UP); CHECK(e.orow() == R_ROLE); press(e, BTN_RIGHT); CHECK(!e.rb.host && e.orows()[3] == R_JOIN);
    press(e, BTN_DOWN); press(e, BTN_CROSS); CHECK(e.editing && e.ef == R_JOIN && e.buf.empty());
    press(e, BTN_DOWN); CHECK(e.buf == "0");
    press(e, BTN_CROSS); CHECK(!e.editing && e.rb.join == "0" && e.msg_err && e.msg.find("Not a valid address") != std::string::npos);   // accepted but not valid yet: stored with a hint
    press(e, BTN_CROSS); CHECK(e.editing && e.buf == "0"); press(e, BTN_CIRCLE); CHECK(!e.editing && e.rb.join == "0");               // Circle cancels
    e.rb.join = "192.0.2.10:7777";
    // ---- Save: Rollback join ----
    g_log.clear(); io.text = "PIPE=1\r\nINPUT_MODE=kbm\r\nJSP_ROLLBACK_LISTEN=9999\r\n";
    e.osel = (int)e.orows().size() - 2; CHECK(e.orow() == R_SAVE);
    press(e, BTN_CROSS);
    CHECK(!e.msg_err && e.nc_saved == NC_ROLLBACK && !e.edited && e.msg.find("applies on the next start") != std::string::npos);
    CHECK(g_log == (std::vector<std::string>{ "be.save", "file.save" }));          // the game's own online goes off first
    CHECK(val(io.text, "JSP_ROLLBACK") == "1" && val(io.text, "JSP_ROLLBACK_JOIN") == "192.0.2.10:7777" && !present(io.text, "JSP_ROLLBACK_LISTEN"));
    CHECK(val(io.text, "PIPE") == "1" && val(io.text, "INPUT_MODE") == "kbm");      // the other lines are kept
    CHECK(io.text.find("\r\n") != std::string::npos);
    CHECK(fk.saved_m == 0);
    // persistence: a new visit reads it back, and the restart note is there (this session started Off)
    e.enter_online();
    CHECK(e.nc == NC_ROLLBACK && !e.rb.host && e.rb.join == "192.0.2.10:7777" && e.nc_saved == NC_ROLLBACK && e.nc_running == NC_OFF && !e.rb.both_set);
    { bool r; status_line(e.nc_running, e.nc_saved, &r); CHECK(r); }
    // the running game started with the same choice: no restart note
    io.env["JSP_ROLLBACK"] = "1"; e.enter_online(); { bool r; status_line(e.nc_running, e.nc_saved, &r); CHECK(!r && e.nc_running == NC_ROLLBACK); }
    io.env.clear();
    // the page reads the mods key the game set at start (jsp_online.cpp refuse_for_mods); without it the flag is off
    CHECK(!e.nc_mods_off);
    io.env["JSP_MODS_ONLINE_OFF"] = "1"; e.enter_online(); CHECK(e.nc_mods_off && e.nc_running == NC_OFF);
    io.env.clear(); e.enter_online(); CHECK(!e.nc_mods_off);

    // ---- Save: Rollback host replaces the join key ----
    e.osel = 0; e.nc = NC_ROLLBACK; e.rb.host = true; e.rb.port = "36200"; e.edited = true;
    e.osel = (int)e.orows().size() - 2; g_log.clear(); press(e, BTN_CROSS);
    CHECK(!e.msg_err && val(io.text, "JSP_ROLLBACK_LISTEN") == "36200" && !present(io.text, "JSP_ROLLBACK_JOIN") && e.msg.find("host, port 36200") != std::string::npos);

    // ---- validation refusals write nothing ----
    g_log.clear(); e.rb.host = false; e.rb.join = "not-an-ip"; e.osel = (int)e.orows().size() - 2; press(e, BTN_CROSS);
    CHECK(e.msg_err && g_log.empty() && e.msg.find("IP:port") != std::string::npos && val(io.text, "JSP_ROLLBACK_LISTEN") == "36200");
    e.rb.host = true; e.rb.port = "0"; e.osel = (int)e.orows().size() - 2; press(e, BTN_CROSS); CHECK(e.msg_err && g_log.empty());

    // ---- Delay: the rollback switch goes off FIRST, then the online files; the remembered rollback values stay (inert) ----
    e.rb.port = "36200"; e.osel = 0; e.nc = NC_OFF; press(e, BTN_RIGHT); CHECK(e.nc == NC_DELAY && fk.m == 1);
    e.osel = (int)e.orows().size() - 2; g_log.clear(); press(e, BTN_CROSS);
    CHECK(!e.msg_err && g_log == (std::vector<std::string>{ "file.save", "be.save" }) && val(io.text, "JSP_ROLLBACK") == "0" && e.nc_saved == NC_DELAY && fk.saved_m == 1);   // the line existed: JSP_ROLLBACK=0, not removed
    CHECK(e.msg.find("Delay") != std::string::npos);
    // a backend refusal (bad server) stops before any write
    fk.check_fail = "re-psn: the server is host or host:port, port 1 to 65535."; io.text = "JSP_ROLLBACK=1\r\nJSP_ROLLBACK_LISTEN=36200\r\n"; g_log.clear();
    press(e, BTN_RIGHT);   // from the Save row Right does nothing; just keep the selection
    e.osel = (int)e.orows().size() - 2; press(e, BTN_CROSS);
    CHECK(e.msg_err && g_log.empty() && present(io.text, "JSP_ROLLBACK"));
    fk.check_fail.clear();
    // the file cannot be written: Delay stops before the online files; Rollback has already switched the online files off and says so
    io.fail_save = true; g_log.clear(); press(e, BTN_CROSS);
    CHECK(e.msg_err && g_log == (std::vector<std::string>{ "file.save" }) && e.msg.find("disk full") != std::string::npos);
    e.osel = 0; e.nc = NC_ROLLBACK; fk.m = 0; e.rb.host = true; e.osel = (int)e.orows().size() - 2; g_log.clear(); press(e, BTN_CROSS);
    CHECK(e.msg_err && g_log == (std::vector<std::string>{ "be.save", "file.save" }) && e.msg.find("switched off") != std::string::npos);
    io.fail_save = false;

    // ---- Off: both off ----
    e.nc = NC_OFF; fk.m = 0; e.osel = (int)e.orows().size() - 2; press(e, BTN_CROSS);
    CHECK(!e.msg_err && val(io.text, "JSP_ROLLBACK") == "0" && fk.saved_m == 0 && e.nc_saved == NC_OFF && e.msg.find("off") != std::string::npos);
    // a JSP_ROLLBACK=0 line counts as off
    io.text = "JSP_ROLLBACK=0\r\n"; e.enter_online(); CHECK(e.nc == NC_OFF);

    // Off / Delay with no JSP_ROLLBACK line and none inherited: the file stays free of it; an INHERITED JSP_ROLLBACK=1 gets a JSP_ROLLBACK=0 line (the file line wins at the launcher)
    io.text = "PIPE=1\r\n"; io.env.clear(); e.enter_online(); e.nc = NC_OFF; fk.m = 0; e.osel = (int)e.orows().size() - 2; press(e, BTN_CROSS);
    CHECK(!e.msg_err && !present(io.text, "JSP_ROLLBACK") && val(io.text, "PIPE") == "1");
    io.env["JSP_ROLLBACK"] = "1"; e.enter_online(); e.nc = NC_OFF; fk.m = 0; e.osel = (int)e.orows().size() - 2; press(e, BTN_CROSS);
    CHECK(!e.msg_err && val(io.text, "JSP_ROLLBACK") == "0"); io.env.clear();
    // after a refused start the process variable is 0 and JSP_NET_REFUSED=1 (resolve_env): the original non-zero value is gone, but the next start would inherit it again,
    // so Off and Delay must still write the JSP_ROLLBACK=0 line (the launcher's file line wins over an inherited variable)
    for (int target : { NC_OFF, NC_DELAY }) {
        io.text = "PIPE=1\r\n"; io.env.clear(); io.env["JSP_ROLLBACK"] = "0"; io.env["JSP_NET_REFUSED"] = "1";
        e.enter_online(); e.nc = target; fk.m = target == NC_DELAY ? 1 : 0; e.osel = (int)e.orows().size() - 2; g_log.clear(); press(e, BTN_CROSS);
        CHECK(!e.msg_err && val(io.text, "JSP_ROLLBACK") == "0" && val(io.text, "PIPE") == "1");
    }
    io.env.clear();
    // line endings of the file the page writes: a new (empty) file and a CRLF file get CRLF, an LF file stays LF
    io.text = ""; e.enter_online(); e.nc = NC_ROLLBACK; fk.m = 0; e.rb.host = true; e.rb.port = "36200"; e.osel = (int)e.orows().size() - 2; press(e, BTN_CROSS);
    CHECK(!e.msg_err && io.text == "JSP_ROLLBACK=1\r\nJSP_ROLLBACK_LISTEN=36200\r\n");
    io.text = "PIPE=1\n"; e.enter_online(); e.nc = NC_ROLLBACK; fk.m = 0; e.rb.host = true; e.rb.port = "36200"; e.osel = (int)e.orows().size() - 2; press(e, BTN_CROSS);
    CHECK(!e.msg_err && io.text.find('\r') == std::string::npos && val(io.text, "JSP_ROLLBACK") == "1");
    // Delay -> Rollback -> Delay keeps Community
    fk.saved_m = 2; io.text = ""; e.enter_online(); CHECK(e.nc == NC_DELAY && fk.m == 2);
    e.osel = 0; press(e, BTN_RIGHT); CHECK(e.nc == NC_ROLLBACK && fk.m == 0); press(e, BTN_LEFT); CHECK(e.nc == NC_DELAY && fk.m == 2);
    e.osel = 0; press(e, BTN_RIGHT); press(e, BTN_RIGHT); press(e, BTN_RIGHT); CHECK(e.nc == NC_OFF && fk.m == 0);
    press(e, BTN_LEFT); press(e, BTN_LEFT); press(e, BTN_LEFT); CHECK(e.nc == NC_DELAY && fk.m == 2);   // via Off and Online Battle + rollback too
    press(e, BTN_DOWN); press(e, BTN_RIGHT); CHECK(fk.m == 1);                                  // a changed Delay method is the one remembered
    e.osel = 0; press(e, BTN_RIGHT); press(e, BTN_LEFT); CHECK(e.nc == NC_DELAY && fk.m == 1);
    // partial failure of the Rollback save: the online files were saved off, the file write failed: nc_saved drops to Off, the choice stays unsaved
    fk.saved_m = 1; io.text = ""; e.enter_online(); CHECK(e.nc_saved == NC_DELAY);
    e.osel = 0; press(e, BTN_RIGHT); CHECK(e.nc == NC_ROLLBACK); io.fail_save = true;
    e.osel = (int)e.orows().size() - 2; g_log.clear(); press(e, BTN_CROSS);
    CHECK(e.msg_err && e.edited && e.nc_saved == NC_OFF && fk.saved_m == 0 && g_log == (std::vector<std::string>{ "be.save", "file.save" })); io.fail_save = false;

    // ---- a hand-edited conflict (JSP_ROLLBACK=1 and the game's online on): shown as Off with a message; the page never picks ----
    fk.saved_m = 1; fk.m = 1; io.text = "JSP_ROLLBACK=1\r\nJSP_ROLLBACK_LISTEN=36200\r\n"; e.enter_online();
    CHECK(e.nc_saved == NC_CONFLICT && e.nc == NC_OFF && fk.m == 0 && e.msg_err && e.msg.find("both on") != std::string::npos);
    // both role keys in the file: noted, and a save keeps only the chosen role
    fk.saved_m = 0; io.text = "JSP_ROLLBACK=1\r\nJSP_ROLLBACK_LISTEN=36200\r\nJSP_ROLLBACK_JOIN=192.0.2.1:5\r\n"; e.enter_online();
    CHECK(e.nc == NC_ROLLBACK && e.rb.both_set && e.rb.host && e.msg_err);
    e.osel = (int)e.orows().size() - 2; press(e, BTN_CROSS); CHECK(!e.msg_err && !present(io.text, "JSP_ROLLBACK_JOIN") && val(io.text, "JSP_ROLLBACK_LISTEN") == "36200");

    // ---- rollback over re-psn (ENHANCEMENT): Connection / Relay rows, keys, validation, start decision with the transport wired ----
    CHECK(rows_for(NC_ROLLBACK, true, true) == V({ R_NETCODE, R_CONN, R_ROLE, R_RELAY, R_SAVE, R_DISCARD }));
    { Rollback r; r.repsn = true; r.relay = false; CHECK(row_text(R_CONN, NC_ROLLBACK, r).find("re-psn") != std::string::npos);
      // every row text fits the page's row width (about 40 characters at scale 1.0; the Netcode row is the longest of the older ones at 38)
      for (bool repsn : { false, true }) for (bool host : { false, true }) for (bool relay : { false, true }) {
          Rollback q; q.repsn = repsn; q.host = host; q.relay = relay; q.join = "255.255.255.255:65535";
          for (int row : { R_NETCODE, R_ROLE, R_PORT, R_JOIN, R_CONN, R_RELAY }) for (int nc : { 0, 1, 2 }) CHECK(row_text(row, nc, q).size() <= 40);
      }
      jps::File f; r.host = false; r.store(f, true); CHECK(val(f.text(), "JSP_ROLLBACK_VIA") == "repsn" && val(f.text(), "JSP_ROLLBACK_ROLE") == "join" && val(f.text(), "JSP_ROLLBACK_RELAY") == "0" && !present(f.text(), "JSP_ROLLBACK_LISTEN"));
      Rollback b; b.load(f); CHECK(b.repsn && !b.host && !b.relay); r.relay = true; r.host = true; r.store(f, true); b.load(f); CHECK(b.repsn && b.host && b.relay);
      Rollback d; d.host = true; d.port = "36200"; d.store(f, true); CHECK(!present(f.text(), "JSP_ROLLBACK_VIA") && !present(f.text(), "JSP_ROLLBACK_ROLE") && val(f.text(), "JSP_ROLLBACK_LISTEN") == "36200");   // direct IP leaves no re-psn keys
      Rollback e2; e2.load(f); CHECK(!e2.repsn && e2.host); }
    { std::map<std::string, std::string> env; std::vector<std::string> own; std::string why;
      auto get = [&](const char* k) -> const char* { auto it = env.find(k); return it == env.end() ? nullptr : it->second.c_str(); };
      auto set = [&](const char* k, const char* v) { if (v && *v) env[k] = v; else env.erase(k); };
      env["JSP_ROLLBACK"] = "1"; env["JSP_ROLLBACK_VIA"] = "repsn"; env["JSP_ROLLBACK_ROLE"] = "host";
      CHECK(kRepsnWired && resolve_env(get, set, &own, &why) == Boot::Go && get("JSP_SNAPSHOT") && get("JSP_STEP_TURBO"));   // wired: a re-psn start with a role is a valid start and gets the rollback defaults
      env.clear(); env["JSP_ROLLBACK"] = "1"; env["JSP_ROLLBACK_VIA"] = "repsn"; env["JSP_ROLLBACK_ROLE"] = "x";
      CHECK(resolve_env(get, set, &own, &why) == Boot::Refused && why.find("ROLE") != std::string::npos);
      for (const char* lp : { "0", "65536", "abc" }) {   // the optional fixed port over re-psn is range-checked like the direct one
          env.clear(); own.clear(); env["JSP_ROLLBACK"] = "1"; env["JSP_ROLLBACK_VIA"] = "repsn"; env["JSP_ROLLBACK_ROLE"] = "host"; env["JSP_ROLLBACK_LISTEN"] = lp;
          CHECK(resolve_env(get, set, &own, &why) == Boot::Refused && why.find("LISTEN") != std::string::npos && !get("JSP_SNAPSHOT"));
      }
      env.clear(); own.clear(); env["JSP_ROLLBACK"] = "1"; env["JSP_ROLLBACK_VIA"] = "repsn"; env["JSP_ROLLBACK_ROLE"] = "host"; env["JSP_ROLLBACK_LISTEN"] = "36200";
      CHECK(resolve_env(get, set, &own, &why) == Boot::Go);
      env.clear(); env["JSP_ROLLBACK"] = "1"; env["JSP_ROLLBACK_VIA"] = "repsn"; env["JSP_ROLLBACK_ROLE"] = "join"; env["PS3_NET_ONLINE"] = "1";
      CHECK(resolve_env(get, set, &own, &why) == Boot::Refused && !get("PS3_NET_ONLINE") && why.find("both on") != std::string::npos);   // rollback over re-psn is not the game's own Delay: one method at a time still holds
    }
    // ENHANCEMENT (display only): the refused-rollback notice. resolve_env reports a code for every refusal; every code has a two-line text that starts the same way
    // and names the fix; ASCII only (the overlay font); the real case (repsn.ini enabled=1 with a re-psn rollback host) names repsn.ini and enabled=0.
    { std::map<std::string, std::string> env; std::vector<std::string> own; std::string why; Refusal code = Refusal::Count;
      auto get = [&](const char* k) -> const char* { auto it = env.find(k); return it == env.end() ? nullptr : it->second.c_str(); };
      auto set = [&](const char* k, const char* v) { if (v && *v) env[k] = v; else env.erase(k); };
      auto run = [&](std::map<std::string, std::string> e) { env = e; own.clear(); code = Refusal::Count; return resolve_env(get, set, &own, &why, &code); };
      CHECK(run({}) == Boot::NotRequested && code == Refusal::None);
      CHECK(run({ { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_LISTEN", "36200" } }) == Boot::Go && code == Refusal::None);
      CHECK(run({ { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_VIA", "repsn" }, { "JSP_ROLLBACK_ROLE", "host" }, { "PS3_NET_ONLINE", "1" } }) == Boot::Refused && code == Refusal::BothOn);
      CHECK(run({ { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_VIA", "repsn" } }) == Boot::Refused && code == Refusal::RepsnNoRole);
      CHECK(run({ { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_VIA", "repsn" }, { "JSP_ROLLBACK_ROLE", "join" }, { "JSP_ROLLBACK_LISTEN", "0" } }) == Boot::Refused && code == Refusal::BadListen);
      CHECK(run({ { "JSP_ROLLBACK", "1" } }) == Boot::Go && code == Refusal::None);   // ENHANCEMENT (lobby): no role key is the lobby start now, not RoleKeys
      CHECK(run({ { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_LISTEN", "1" }, { "JSP_ROLLBACK_JOIN", "192.0.2.10:36200" } }) == Boot::Refused && code == Refusal::RoleKeys);
      CHECK(run({ { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_LISTEN", "99999" } }) == Boot::Refused && code == Refusal::BadListen);
      CHECK(run({ { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_JOIN", "host:1" } }) == Boot::Refused && code == Refusal::BadJoin);
      // ENHANCEMENT (rollback bench): a depth 1..8 alone is a valid start with the rollback defaults; with a role key, or out of range, it is refused
      CHECK(run({ { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_BENCH", "4" } }) == Boot::Go && code == Refusal::None && get("JSP_SNAPSHOT") && get("JSP_STEP_TURBO"));
      CHECK(run({ { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_BENCH", "8" } }) == Boot::Go);
      CHECK(run({ { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_BENCH", "9" } }) == Boot::Refused && code == Refusal::RoleKeys && !get("JSP_SNAPSHOT"));
      CHECK(run({ { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_BENCH", "12" } }) == Boot::Refused && code == Refusal::RoleKeys);
      CHECK(run({ { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_BENCH", "2" }, { "JSP_ROLLBACK_LISTEN", "36200" } }) == Boot::Refused && code == Refusal::RoleKeys && why.find("BENCH") != std::string::npos);
      CHECK(run({ { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_BENCH", "0" }, { "JSP_ROLLBACK_LISTEN", "36200" } }) == Boot::Go);   // 0 = no bench: the usual host start
      CHECK(bench_depth("1") == 1 && bench_depth("8") == 8 && !bench_depth("0") && !bench_depth("9") && !bench_depth("") && !bench_depth(nullptr) && !bench_depth("3x"));
      env = { { "JSP_ROLLBACK", "1" }, { "JSP_ROLLBACK_LISTEN", "1" }, { "JSP_ROLLBACK_JOIN", "192.0.2.10:36200" } };
      CHECK(resolve_env(get, set, &own, &why) == Boot::Refused && std::string(get("JSP_ROLLBACK")) == "0");   // the code pointer stays optional
      CHECK(!notice_text(Refusal::None).any() && !notice_text(Refusal::Count).any() && !notice_text(static_cast<Refusal>(99)).any());
      for (int c = 1; c < static_cast<int>(Refusal::Count); ++c)
          for (int src : { SRC_OTHER, SRC_REPSN_INI, SRC_PSNR_FLAG, SRC_ENVIRONMENT }) {
              const Notice n = notice_text(static_cast<Refusal>(c), src);
              CHECK(n.any() && n.line1.rfind("Rollback is off: ", 0) == 0 && !n.line2.empty() && n.line1.size() <= 100 && n.line2.size() <= 100);
              for (char ch : n.line1 + n.line2) CHECK(ch >= 0x20 && ch <= 0x7E);
          }
      const Notice real = notice_text(Refusal::BothOn, SRC_REPSN_INI);
      CHECK(real.line1 == "Rollback is off: repsn.ini has enabled=1 (the game's own online) and a rollback role key is set." && real.line2.find("Online Battle + rollback") != std::string::npos
            && real.line2.find("set enabled=0 in repsn.ini") != std::string::npos);
      CHECK(notice_text(Refusal::BothOn, SRC_PSNR_FLAG).line1.find("--psnr") != std::string::npos);
      CHECK(notice_text(Refusal::BothOn, SRC_ENVIRONMENT).line1.find("PSNR_SERVER") != std::string::npos);
      CHECK(notice_text(Refusal::BothOn, SRC_OTHER).line1.find("PS3_NET_ONLINE") != std::string::npos);
      CHECK(notice_text(Refusal::RepsnNoIdentity).line1.find("server=") != std::string::npos && notice_text(Refusal::RepsnNoIdentity).line2.find("enabled=0") != std::string::npos);
      CHECK(notice_text(Refusal::RepsnLongName).line1.find("16") != std::string::npos);
      CHECK(notice_text(Refusal::NoSocket).line2.find("JSP_ROLLBACK_LISTEN") != std::string::npos);
      // the fix line names the file change first; the Port Settings page (off unless JSP_PORT_MENU=1 before it is on by default) only as "or Port Settings > Online"
      // or, where the line has room, the full path "or Options > Port Settings > Online"
      for (Refusal r : { Refusal::RepsnNoRole, Refusal::BadListen, Refusal::RoleKeys, Refusal::BadJoin, Refusal::RepsnNoIdentity }) {
          const std::string l2 = notice_text(r).line2;
          const char* file = r == Refusal::RepsnNoIdentity ? "repsn.ini" : "play_settings.txt";
          const std::string tail = l2.find(", or Options > Port Settings > Online.") != std::string::npos ? ", or Options > Port Settings > Online." : ", or Port Settings > Online.";
          const size_t f = l2.find(file), ps = l2.find(tail);
          CHECK(f != std::string::npos && ps != std::string::npos && f < ps && ps + tail.size() == l2.size());
      }
      CHECK(notice_text(Refusal::RepsnNoRole).line2.find("JSP_ROLLBACK_ROLE=host or join") != std::string::npos);
      CHECK(notice_text(Refusal::BadJoin).line2.find("JSP_ROLLBACK_JOIN=IP:PORT") != std::string::npos);
      for (int c = 1; c < static_cast<int>(Refusal::Count); ++c)
          for (int src : { SRC_OTHER, SRC_REPSN_INI, SRC_PSNR_FLAG, SRC_ENVIRONMENT }) {
              const Notice n = notice_text(static_cast<Refusal>(c), src);
              const std::string both = n.line1 + "|" + n.line2;
              const size_t at = both.find("Port Settings");
              CHECK(at == std::string::npos || both.find("or Port Settings > Online") == at - 3 || both.find("or Options > Port Settings > Online") == at - 13);
          }
      // the visibility rule: only while the top list is up, for kNoticeMs after it came up
      CHECK(notice_visible(true, true, 1000, 1000) && notice_visible(true, true, 1000, 1000 + kNoticeMs - 1));
      CHECK(!notice_visible(true, true, 1000, 1000 + kNoticeMs) && !notice_visible(true, false, 1000, 1001) && !notice_visible(false, true, 1000, 1001));
    }
    {   // FIX (an F9 tap cancelled rollback at fight start): the cancel key counts only once held kCancelHoldMs; a tap or a release resets it
      uint64_t since = 0;
      CHECK(!cancel_held(true, 5000, since) && since == 5000);                  // pressed
      CHECK(!cancel_held(true, 5000 + kCancelHoldMs, since));                   // not yet past the hold
      CHECK(!cancel_held(false, 5100 + kCancelHoldMs, since) && since == 0);    // released: a tap never cancels
      CHECK(!cancel_held(true, 9000, since) && !cancel_held(true, 9000 + kCancelHoldMs - 1, since) && cancel_held(true, 9001 + kCancelHoldMs, since));
    }
    // the page: Connection and Relay toggles, saved to the file; re-psn needs a server and a name
    { Fake fk3; FakeIO io3; Menu m; m.be = &fk3; m.io = &io3; m.enter_online(); press(m, BTN_RIGHT); press(m, BTN_RIGHT); CHECK(m.nc == NC_ROLLBACK);
      press(m, BTN_DOWN); CHECK(m.orow() == R_CONN); press(m, BTN_RIGHT); CHECK(m.rb.repsn && m.orows() == V({ R_NETCODE, R_CONN, R_ROLE, R_RELAY, R_SAVE, R_DISCARD }));
      press(m, BTN_DOWN); press(m, BTN_RIGHT); CHECK(!m.rb.host);                              // Role: Join
      press(m, BTN_DOWN); CHECK(m.orow() == R_RELAY); press(m, BTN_RIGHT); CHECK(m.rb.relay && m.msg.find("only if a direct path fails") != std::string::npos);
      press(m, BTN_RIGHT); CHECK(!m.rb.relay); press(m, BTN_RIGHT);
      fk3.server.clear(); m.osel = (int)m.orows().size() - 2; g_log.clear(); press(m, BTN_CROSS);
      CHECK(m.msg_err && g_log.empty() && m.msg.find("server and online name") != std::string::npos);   // nothing written
      fk3.server = "example.invalid"; g_log.clear(); press(m, BTN_CROSS);
      CHECK(!m.msg_err && val(io3.text, "JSP_ROLLBACK_VIA") == "repsn" && val(io3.text, "JSP_ROLLBACK_ROLE") == "join" && val(io3.text, "JSP_ROLLBACK_RELAY") == "1" && g_log == (std::vector<std::string>{ "be.save", "file.save" }));
      m.enter_online(); CHECK(m.nc == NC_ROLLBACK && m.rb.repsn && !m.rb.host && m.rb.relay);          // persistence
      press(m, BTN_DOWN); press(m, BTN_RIGHT); CHECK(!m.rb.repsn && m.orows().size() == 6 && m.orows()[3] == R_JOIN); }

    // ---- Discard re-reads both ----
    e.enter_online(); press(e, BTN_RIGHT); CHECK(e.nc == NC_NATIVE && e.edited);
    e.osel = (int)e.orows().size() - 1; CHECK(e.orow() == R_DISCARD); press(e, BTN_CROSS);
    CHECK(!e.edited && e.nc == NC_ROLLBACK && e.msg == "Changes discarded.");
    // Circle leaves without saving (nothing written)
    g_log.clear(); press(e, BTN_RIGHT); press(e, BTN_CIRCLE); CHECK(e.page == MAIN && g_log.empty());

    // ---- ENHANCEMENT (native-room pairing): Online Battle + rollback saves re-psn on AND JSP_ROLLBACK=1 without a role key; Delay from it writes JSP_ROLLBACK=0 ----
    { Fake fk4; FakeIO io4; Menu m; m.be = &fk4; m.io = &io4;
      io4.text = "PIPE=1\r\nJSP_ROLLBACK_LISTEN=36200\r\nJSP_ROLLBACK_VIA=repsn\r\nJSP_ROLLBACK_ROLE=host\r\n"; m.enter_online(); CHECK(m.nc == NC_OFF);
      m.osel = 0; press(m, BTN_LEFT); CHECK(m.nc == NC_NATIVE && fk4.m == 1);
      m.osel = 3; CHECK(m.orow() == R_RELAY); press(m, BTN_RIGHT); CHECK(m.rb.relay);
      m.osel = (int)m.orows().size() - 2; g_log.clear(); press(m, BTN_CROSS);
      CHECK(!m.msg_err && fk4.saved_m == 1 && val(io4.text, "JSP_ROLLBACK") == "1" && val(io4.text, "JSP_ROLLBACK_RELAY") == "1" && val(io4.text, "PIPE") == "1");
      CHECK(!present(io4.text, "JSP_ROLLBACK_LISTEN") && !present(io4.text, "JSP_ROLLBACK_VIA") && !present(io4.text, "JSP_ROLLBACK_ROLE") && m.nc_saved == NC_NATIVE);
      CHECK(g_log == (std::vector<std::string>{ "be.save", "file.save" }) && m.msg.find("Friendly Match") != std::string::npos);
      m.enter_online(); CHECK(m.nc == NC_NATIVE && m.rb.relay && m.net_note.empty());   // persistence
      fk4.check_fail = "re-psn: bad name"; m.osel = (int)m.orows().size() - 2; press(m, BTN_CROSS); CHECK(m.msg_err && m.msg == "re-psn: bad name"); fk4.check_fail.clear();
      m.osel = 0; press(m, BTN_LEFT); press(m, BTN_LEFT); CHECK(m.nc == NC_DELAY && fk4.m == 1);
      m.osel = (int)m.orows().size() - 2; press(m, BTN_CROSS);
      CHECK(!m.msg_err && val(io4.text, "JSP_ROLLBACK") == "0" && fk4.saved_m == 1 && m.nc_saved == NC_DELAY);
      CHECK(std::string(exclusion_line(NC_NATIVE)).find("Friendly Match") != std::string::npos && std::string(name_of(NC_NATIVE)) == "Online Battle + rollback");
      { bool r; CHECK(status_line(NC_NATIVE, NC_NATIVE, &r) == "This session: Online Battle + rollback.  Saved: Online Battle + rollback." && !r); } }

    if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
    std::printf("net online page: all checks passed\n");
    return 0;
}
