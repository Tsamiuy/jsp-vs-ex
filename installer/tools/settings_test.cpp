// Host test for the in-game settings pages: installer/port/jsp/jsp_settings.h (key table, settings file model, key and button
// rules), the pages' state machine in jsp_port_menu.cpp (with a fake SettingsIO), and jsp_settings.cpp (file, live values).
// It also reads the launcher sources and the readers' sources and checks that the key table matches them, because the launcher's
// C# cannot be built here: every key the launcher writes is in the table (or is named launcher-only), the value lists are the
// same, the defaults are the same, and every table key is read by something.
//   bash installer/tools/settings_test.sh          (g++ only; the repo root is found from the script)
#define JSP_PORT_MENU_TEST 1
#include "../port/jsp/jsp_port_menu.cpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <set>
#include <unistd.h>

namespace jps { std::string io_path(); bool io_load(std::string& text, std::string& note); bool io_save(const std::string& text, std::string& err); }
extern "C" int jsp_setting_gen(void);
extern "C" int jsp_setting_get(const char* key, char* out, int n);
extern "C" void jsp_setting_set_live(const char* key, const char* value);

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
using namespace jpm;

static std::string slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

// ---- the launcher sources ----
static std::vector<std::string> cs_array(const std::string& src, const char* name)
{
    std::vector<std::string> v;
    const size_t a = src.find(std::string("string[] ") + name + " = {");
    if (a == std::string::npos) return v;
    const size_t b = src.find('}', a);
    const std::string body = src.substr(a, b - a);
    std::regex q("\"([^\"]*)\"");
    for (auto it = std::sregex_iterator(body.begin(), body.end(), q); it != std::sregex_iterator(); ++it) v.push_back((*it)[1]);
    return v;
}
static bool values_are(const char* key, const std::vector<std::string>& want)
{
    const jps::Item* it = jps::find_item(key);
    if (!it || (int)want.size() != it->n) return false;
    for (int i = 0; i < it->n; i++) if (want[(size_t)i] != it->values[i]) return false;
    return true;
}
static int cs_key(const std::string& k)
{
    static const std::map<std::string, int> m = { {"Up",0x26},{"Down",0x28},{"Left",0x25},{"Right",0x27},{"Return",0x0D},{"Tab",0x09},
        {"D1",'1'},{"D2",'2'},{"D3",'3'},{"D4",'4'} };
    auto it = m.find(k);
    if (it != m.end()) return it->second;
    return k.size() == 1 ? k[0] : -1;
}

int main(int argc, char** argv)
{
    const std::string root = argc > 1 ? argv[1] : "..";

    // ================= the key table against the launcher and the readers =================
    {
        const std::string l19 = slurp(root + "/launcher/JStarsLauncher19.cs"), l = slurp(root + "/launcher/JStarsLauncher.cs");
        CHECK(!l19.empty() && !l.empty());
        std::set<std::string> written;
        std::regex w("lines\\.Add\\(\"([A-Z_]+)=");
        for (const std::string* src : { &l19, &l })
            for (auto it = std::sregex_iterator(src->begin(), src->end(), w); it != std::sregex_iterator(); ++it) written.insert((*it)[1]);
        // every key the launchers write: a table item, the key pages (PAD_KEY_/KB_KEY_/KBM_KEY_/PAD_BTN_ are written with a prefix, not matched here), or launcher only
        for (const auto& k : written) {
            bool known = jps::find_item(k.c_str()) != nullptr || k == "BOOST_OFF_ON_EXIT" || k == "MOUSE_SENS";
            for (int i = 0; i < jps::k_launcher_only_count; i++) if (k == jps::k_launcher_only[i].key) known = true;
            if (!known) std::printf("launcher writes %s: not in the table\n", k.c_str());
            CHECK(known);
        }
        for (int i = 0; i < jps::k_item_count; i++) {
            if (!written.count(jps::k_items[i].key)) std::printf("table key %s is written by no launcher\n", jps::k_items[i].key);
            CHECK(written.count(jps::k_items[i].key));
        }
        CHECK(l19.find("\"JSP_SETTINGS_FILE\"") != std::string::npos && l.find("\"JSP_SETTINGS_FILE\"") != std::string::npos);
        CHECK(slurp(root + "/installer/launcher/launcher.c").find("SetEnvironmentVariableA(\"JSP_SETTINGS_FILE\", f)") != std::string::npos);
        // value lists
        CHECK(values_are("RES_SCALE", cs_array(l19, "ResScales")));
        CHECK(values_are("AA_MODE", cs_array(l19, "AaIds")) && values_are("AA_MODE", cs_array(l, "AaIds")));
        CHECK(values_are("MODEL_DETAIL", cs_array(l19, "ModelDetailIds")));
        CHECK(values_are("OBJECT_DISTANCE", cs_array(l19, "ObjectDistIds")));
        CHECK(values_are("INPUT_MODE", cs_array(l19, "ModeIds")) && values_are("INPUT_MODE", cs_array(l, "ModeIds")));
        { const auto x = cs_array(l19, "XButtons"); CHECK((int)x.size() == jps::k_xbutton_count); for (size_t i = 0; i < x.size() && (int)i < jps::k_xbutton_count; i++) CHECK(x[i] == jps::k_xbuttons[i]); }
        // the launcher's own numbering: shadow 1..4, range 1|2, HUD size 10..20 over 20 as 0.00, mouse 1..20 over 4 as 0.00, resolution 1..4 in the installer's window
        CHECK(l19.find("\"SHADOW_SCALE=\" + (shadowBox.SelectedIndex + 1)") != std::string::npos && jps::find_item("SHADOW_SCALE")->n == 4);
        CHECK(l19.find("shadowRangeBox.SelectedIndex == 1 ? \"2\" : \"1\"") != std::string::npos);
        CHECK(l19.find("Minimum = 10, Maximum = 20") != std::string::npos && l19.find("(hudBar.Value / 20.0).ToString(\"0.00\"") != std::string::npos);
        { const jps::Item* ui = jps::find_item("UI_SCALE"); CHECK(ui && ui->n == 11 && !std::strcmp(ui->values[0], "0.50") && !std::strcmp(ui->values[10], "1.00")); }
        CHECK(l19.find("sensBar.Value / 4.0") != std::string::npos && l19.find("(1, Math.Min(20,") != std::string::npos);
        { const jps::Item* ms = jps::find_item("MOUSE_SENS"); CHECK(ms && ms->n == 20 && !std::strcmp(ms->values[0], "0.25") && !std::strcmp(ms->values[19], "5.00") && !std::strcmp(ms->values[ms->def], "1.00")); }
        CHECK(l.find("\"RES_SCALE=\" + (resBox.SelectedIndex + 1)") != std::string::npos);   // the installer's window: 1..4 are in the table's list
        for (const char* v : { "1", "2", "3", "4" }) CHECK(jps::item_index(*jps::find_item("RES_SCALE"), v) >= 0);
        // defaults: the launcher's boxes
        CHECK(l19.find("shadowBox.SelectedIndex = 1;") != std::string::npos && jps::find_item("SHADOW_SCALE")->def == 1);
        CHECK(l19.find("aaBox.SelectedIndex = 1;") != std::string::npos && jps::find_item("AA_MODE")->def == 1);
        CHECK(l19.find("fxFullBox = new CheckBox") != std::string::npos && l19.find("Checked = true") != std::string::npos && jps::find_item("FX_FULL")->def == 1);
        CHECK(l19.find("objectDistBox.SelectedIndex = 1;") != std::string::npos && jps::find_item("OBJECT_DISTANCE")->def == 1);
        // key actions and controller buttons: same ids, same order, same defaults
        { std::regex a("new PadAction\\(\"([A-Z0-9_]+)\", \"[^\"]*\", Keys\\.([A-Za-z0-9]+)\\)"); int i = 0;
          for (auto it = std::sregex_iterator(l19.begin(), l19.end(), a); it != std::sregex_iterator(); ++it, ++i) {
              CHECK(i < jps::k_action_count && (*it)[1] == jps::k_actions[i].id && cs_key((*it)[2]) == jps::k_actions[i].def_vk);
          }
          CHECK(i == jps::k_action_count); }
        { std::regex b("new PadButton\\(\"([A-Z0-9_]+)\", \"[^\"]*\", \"([A-Z]+)\"\\)"); int i = 0;
          for (auto it = std::sregex_iterator(l19.begin(), l19.end(), b); it != std::sregex_iterator(); ++it, ++i)
              CHECK(i < jps::k_padbtn_count && (*it)[1] == jps::k_padbtns[i].id && (*it)[2] == jps::k_padbtns[i].def);
          CHECK(i == jps::k_padbtn_count); }
        // readers: a LIVE key is read through jsp_setting_get / setting_float in jsp_game.cpp; the others by getenv in the port or a toolkit patch
        const std::string game = slurp(root + "/installer/port/jsp/jsp_game.cpp");
        std::string patches;
        for (const char* dir : { "/installer/port/patches/", "/installer/patches/" }) {
            std::string cmd = "ls " + root + dir + "*.patch 2>/dev/null";
            FILE* p = popen(cmd.c_str(), "r"); char ln[1024];
            while (p && std::fgets(ln, sizeof ln, p)) { std::string f = ln; while (!f.empty() && (f.back() == '\n')) f.pop_back(); patches += slurp(f); }
            if (p) pclose(p);
        }
        CHECK(!patches.empty());
        for (int i = 0; i < jps::k_item_count; i++) {
            const jps::Item& it = jps::k_items[i];
            const std::string q = std::string("\"") + it.key + "\"";
            const bool live_read = game.find("setting_float(" + q) != std::string::npos || game.find("jsp_setting_get(" + q) != std::string::npos;
            const bool env_read = patches.find("getenv(" + q) != std::string::npos || game.find("getenv(" + q) != std::string::npos;
            if (it.apply == jps::LIVE) { if (!live_read) std::printf("%s: LIVE but not read through jsp_setting_get\n", it.key); CHECK(live_read); }
            else { if (!env_read) std::printf("%s: no reader found\n", it.key); CHECK(env_read || live_read); }
        }
        // every launcher-only entry has a reason; the unlock key is not in the table (never from the file)
        for (int i = 0; i < jps::k_launcher_only_count; i++) CHECK(jps::k_launcher_only[i].why[0] && jps::k_launcher_only[i].what[0]);
        CHECK(jps::find_item("JSP_UNLOCK_ALL") == nullptr);
        CHECK(l19.find("EnvironmentVariables.Remove(\"JSP_UNLOCK_ALL\")") != std::string::npos);
    }

    // ================= the file model =================
    {
        const std::string text = "# Written by the J-Stars v19 launcher.\r\nRES_SCALE=2\r\nFOO=bar\r\n; note\r\nSHADOW_SCALE = 3 \r\nRES_SCALE=4\r\n";
        jps::File f = jps::File::parse(text);
        CHECK(f.crlf && f.lines.size() == 6 && f.text() == text);          // exact round trip
        std::string v;
        CHECK(f.get("RES_SCALE", v) && v == "4");                           // the later line wins, as the environment conversion does
        CHECK(f.get("SHADOW_SCALE", v) && v == "3" && !f.get("; note", v) && !f.get("FOO2", v));
        f.set("RES_SCALE", "2.5");                                          // first line changes, the duplicate goes
        CHECK(f.text() == "# Written by the J-Stars v19 launcher.\r\nRES_SCALE=2.5\r\nFOO=bar\r\n; note\r\nSHADOW_SCALE = 3 \r\n");
        f.set("NEW_KEY", "1"); f.erase("FOO");
        CHECK(f.text().find("FOO") == std::string::npos && f.text().find("NEW_KEY=1\r\n") != std::string::npos && f.text().find("; note") != std::string::npos);
        jps::File lf = jps::File::parse("A=1\nB=2");                         // LF file without a final newline stays LF
        CHECK(!lf.crlf && lf.lines.size() == 2); lf.set("B", "3"); CHECK(lf.text() == "A=1\nB=3\n");
        jps::File e = jps::File::parse(""); CHECK(e.crlf && e.lines.empty() && e.text().empty());
        e.set("X", "1"); CHECK(e.text() == "X=1\r\n");
        int vk = 0;
        CHECK(jps::parse_vk("0x5A", &vk) && vk == 0x5A && !jps::parse_vk("0", &vk) && !jps::parse_vk("300", &vk) && !jps::parse_vk("zz", &vk));
        CHECK(jps::parse_vk("90", &vk) && vk == 90 && jps::vk_text(0x0D) == "0x0D");
        CHECK(jps::key_name('Z') == "Z" && jps::key_name(0x25) == "Left arrow" && jps::key_name(0x70) == "F1" && jps::key_name(0x0D) == "Enter" && jps::key_name(0x01) == "Left mouse");
    }

    // ================= the model: choices =================
    {
        jps::Model m;
        const jps::Item& res = *jps::find_item("RES_SCALE");
        CHECK(m.saved_index(res) == 0 && m.saved_text(res) == "720p (the PS3's own)");   // no file: what the game does
        m.cycle(res, 1); CHECK(m.saved_index(res) == 1);
        std::string v; CHECK(m.f.get("RES_SCALE", v) && v == "1.25");
        m.cycle(res, -1); m.cycle(res, -1); CHECK(m.saved_index(res) == 6);             // wraps
        m.f.set("RES_SCALE", "2.0"); CHECK(m.saved_index(res) == 3);                    // equal as a number
        m.f.set("RES_SCALE", "7"); CHECK(m.saved_index(res) == -2 && m.saved_text(res) == "7 (not in the list)");
        m.cycle(res, 1); CHECK(m.saved_index(res) == 0);                                // a foreign value: first step lands on an end
        const jps::Item& aa = *jps::find_item("AA_MODE");
        jps::Model n; CHECK(n.saved_index(aa) == 0);                                    // unset = off in the game
        n.f.set("AA_MODE", "SMAA"); CHECK(n.saved_index(aa) == 3);                      // case-insensitive, as the launcher
        const jps::Item& ws = *jps::find_item("WINDOW_SIZE");
        n.set_index(ws, 3); CHECK(n.f.get("WINDOW_SIZE", v) && v == "1920x1080");
        n.set_index(ws, 0); CHECK(!n.f.get("WINDOW_SIZE", v));                          // "Same as render size" removes the key
        // pending: a NEXT_START item whose saved value is not what the game started with
        const char* env_res = "1";
        auto act = [&](const char* k) -> const char* { return !std::strcmp(k, "RES_SCALE") ? env_res : nullptr; };
        jps::Model p; p.set_index(res, 3);
        CHECK(p.pending(res, act));
        env_res = "2"; CHECK(!p.pending(res, act));
        env_res = "2.00"; CHECK(!p.pending(res, act));                                  // equal as a number
        jps::Model q; q.set_index(*jps::find_item("UI_SCALE"), 3); CHECK(!q.pending(*jps::find_item("UI_SCALE"), act));   // LIVE: never pending
        jps::Model r; CHECK(!r.pending(res, [](const char*) -> const char* { return nullptr; }));   // no file and no env: same
        // group reset writes the launcher's defaults (INPUT_MODE untouched)
        jps::Model g; g.set_index(*jps::find_item("INPUT_MODE"), 2); g.set_index(*jps::find_item("MOUSE_SENS"), 9);
        g.reset_group(jps::G_CONTROLS);
        CHECK(g.mode() == 2 && g.f.get("MOUSE_SENS", v) && v == "1.00");
        jps::Model gg; gg.reset_group(jps::G_GRAPHICS);
        CHECK(gg.f.get("AA_MODE", v) && v == "fxaa" && gg.f.get("FX_FULL", v) && v == "1" && gg.f.get("UI_SCALE", v) && v == "1.00" && gg.f.get("SHADOW_SCALE", v) && v == "2");
    }

    // ================= the model: keys and buttons =================
    {
        jps::Model m;
        std::string v;
        CHECK(m.mode() == 0 && m.key_vk(8) == 'Z' && m.key_vk(0) == 0x26);
        m.set_key(8, 'C');                                                              // Cross -> C
        CHECK(m.key_vk(8) == 'C' && m.f.get("PAD_KEY_CROSS", v) && v == "0x43" && m.f.get("KB_KEY_CROSS", v) && v == "0x43");
        CHECK(m.f.get("KBM_KEY_CROSS", v) && v == "0x43");                              // no mouse page yet: becomes a copy
        m.set_key(9, 'C');                                                              // Circle takes C: Cross gets Circle's old key (swap)
        CHECK(m.key_vk(9) == 'C' && m.key_vk(8) == 'X');
        // the other modes: kbm has its own page; controller mode shows the keyboard page
        m.f.set("INPUT_MODE", "kbm"); CHECK(m.mode() == 1 && m.key_vk(9) == 'X' && m.key_vk(8) == 'C');   // the mouse page is its own copy
        m.set_key(9, 0x01); CHECK(m.key_vk(9) == 0x01 && m.f.get("KBM_KEY_CIRCLE", v) && v == "0x01" && m.f.get("KB_KEY_CIRCLE", v) && v == "0x43");
        CHECK(m.f.get("PAD_KEY_CIRCLE", v) && v == "0x01");                             // the game reads the page of the mode
        const jps::Item& mode = *jps::find_item("INPUT_MODE");
        m.set_index(mode, 0);                                                           // back to keyboard: PAD_KEY_* follow that page
        CHECK(m.mode() == 0 && m.f.get("PAD_KEY_CIRCLE", v) && v == "0x43");
        // a file from before the input types: PAD_KEY_* only
        jps::Model old; old.f.set("PAD_KEY_CROSS", "0x20"); old.f.set("INPUT_MODE", "keyboard");
        CHECK(old.key_vk(8) == 0x20 && old.key_vk(9) == 'X');
        old.set_key(10, 'V'); CHECK(old.key_vk(8) == 0x20 && old.key_vk(10) == 'V' && old.f.get("KB_KEY_CROSS", v) && v == "0x20");
        // editing the mouse page first keeps the keyboard page as it was (read from PAD_KEY_)
        jps::Model mp; mp.f.set("PAD_KEY_CROSS", "0x20"); mp.f.set("INPUT_MODE", "kbm");
        mp.set_key(9, 0x02); CHECK(mp.f.get("KB_KEY_CROSS", v) && v == "0x20" && mp.f.get("KBM_KEY_CROSS", v) && v == "0x20" && mp.f.get("KBM_KEY_CIRCLE", v) && v == "0x02");
        // unknown lines survive
        jps::Model u; u.f = jps::File::parse("# mine\nCPU_BOOST=1\nSOMETHING=else\n"); u.set_key(8, 'C'); u.cycle(*jps::find_item("FULLSCREEN"), 1);
        CHECK(u.f.text().find("# mine") == 0 && u.f.get("CPU_BOOST", v) && v == "1" && u.f.get("SOMETHING", v) && v == "else" && u.f.get("FULLSCREEN", v) && v == "1");
        // reset
        u.reset_keys(); CHECK(u.key_vk(8) == 'Z' && u.f.get("PAD_KEY_CROSS", v) && v == "0x5A");
        // controller buttons: default, change with a swap, reset
        jps::Model b;
        CHECK(b.pad_btn(8) == 0 && !std::strcmp(jps::k_xbuttons[b.pad_btn(9)], "B"));
        b.set_pad_btn(8, 2);                                                            // Cross -> X: Square (had X) gets A
        CHECK(!std::strcmp(jps::k_xbuttons[b.pad_btn(8)], "X") && !std::strcmp(jps::k_xbuttons[b.pad_btn(10)], "A"));
        CHECK(b.f.get("PAD_BTN_CROSS", v) && v == "X" && b.f.get("PAD_BTN_SQUARE", v) && v == "A" && b.f.get("PAD_BTN_SELECT", v) && v == "BACK");
        b.f.set("PAD_BTN_L1", "lb"); CHECK(!std::strcmp(jps::k_xbuttons[b.pad_btn(4)], "LB"));   // case-insensitive
        b.reset_pad_btns(); CHECK(b.pad_btn(8) == 0 && b.pad_btn(10) == 2);
    }

    // ================= the pages (fake io) =================
    {
        struct IO : SettingsIO {
            std::string file; bool load_ok = true, save_ok = true; int saves = 0;
            std::map<std::string, std::string> env, live_set; std::vector<int> keys;   // keys: queue of poll_vk answers
            bool mouse_asked = false;
            bool load(std::string& t, std::string& n) override { t = file; if (!load_ok) { n = "unreadable"; return false; } return true; }
            bool save(const std::string& t, std::string& e) override { if (!save_ok) { e = "read-only folder"; return false; } file = t; saves++; return true; }
            const char* active(const char* k) override { auto i = env.find(k); return i == env.end() ? nullptr : i->second.c_str(); }
            void live(const char* k, const char* v) override { live_set[k] = v; }
            int poll_vk(bool mouse) override { mouse_asked = mouse; if (keys.empty()) return 0; int k = keys.front(); keys.erase(keys.begin()); return k; }
            std::string path() override { return "C:\\x\\play_settings.txt"; }
        } io;
        Menu m; m.io = &io;
        auto press = [&](uint16_t b) { m.feed(b, 1, true); m.feed(0, 1, true); };
        press(BTN_CROSS);                                                               // closed -> main
        CHECK(m.page == MAIN && m.main_rows().size() == 6 && m.main_rows()[0] == DISPLAY && m.main_rows()[3] == ONLINE && m.main_rows()[5] == CLOSED);
        // Display: RES_SCALE changes with Right, saved at once, applies on the next start
        press(BTN_CROSS); CHECK(m.page == DISPLAY && m.srows.size() == 5 && m.srows.back().kind == SRow::RESET);
        press(BTN_RIGHT);
        CHECK(io.saves == 1 && io.file == "RES_SCALE=1.25\r\n" && m.smsg == "Saved. Applies on the next start." && !m.smsg_err);
        CHECK(m.row_pending(m.srows[0]) && m.row_value(m.srows[0]) == "900p");           // the running game has no RES_SCALE: still on 1
        io.env["RES_SCALE"] = "1.25"; CHECK(!m.row_pending(m.srows[0]));
        press(BTN_DOWN); press(BTN_CROSS); CHECK(io.file.find("FULLSCREEN=1") != std::string::npos && m.row_value(m.srows[1]) == "Fullscreen");
        // a failed save is shown, not hidden
        io.save_ok = false; press(BTN_CROSS); CHECK(m.smsg_err && m.smsg == "read-only folder" && io.file.find("FULLSCREEN=0") == std::string::npos); io.save_ok = true;
        // Circle: back to the main page on the Display row
        press(BTN_CIRCLE); CHECK(m.page == MAIN && m.sel == 0);
        // Graphics: a live item reaches its reader
        press(BTN_DOWN); press(BTN_CROSS); CHECK(m.page == GRAPHICS);
        int ui = -1; for (size_t i = 0; i < m.srows.size(); i++) if (m.srows[i].kind == SRow::ITEM && !std::strcmp(jps::k_items[m.srows[i].idx].key, "UI_SCALE")) ui = (int)i;
        CHECK(ui >= 0);
        for (int i = 0; i < ui; i++) press(BTN_DOWN);
        press(BTN_LEFT);
        CHECK(io.live_set["UI_SCALE"] == "0.95" && m.smsg == "Saved. Applied now." && !m.row_pending(m.srows[(size_t)ui]) && m.row_value(m.srows[(size_t)ui]) == "95%");
        CHECK(m.stop >= 0 && m.ssel == ui);
        // reset the page: writes the launcher's defaults, live ones are handed over
        for (size_t i = m.ssel; i + 1 < m.srows.size(); i++) press(BTN_DOWN);
        CHECK(m.srows[(size_t)m.ssel].kind == SRow::RESET); press(BTN_CROSS);
        CHECK(io.live_set["UI_SCALE"] == "1.00" && io.live_set["AA_MODE"] == "fxaa" && io.file.find("AA_MODE=fxaa") != std::string::npos && m.smsg.find("Defaults saved") == 0);
        // wrap and scroll: Up from the first row lands on the last, the window follows
        press(BTN_CIRCLE); press(BTN_DOWN); press(BTN_CROSS);                            // Controls
        CHECK(m.page == CONTROLS && m.srows.size() == 3 && m.srows[2].kind == SRow::OPEN && m.srows[2].target == KEYS);
        press(BTN_CROSS); CHECK(m.ssel == 0);                                           // INPUT_MODE: keyboard -> kbm
        CHECK(m.model.mode() == 1 && io.file.find("INPUT_MODE=kbm") != std::string::npos);
        press(BTN_DOWN); press(BTN_RIGHT); press(BTN_DOWN);                              // sensitivity 1.25x, then the Keys link
        CHECK(io.file.find("MOUSE_SENS=1.25") != std::string::npos && m.srows[(size_t)m.ssel].kind == SRow::OPEN);
        press(BTN_CROSS); CHECK(m.page == KEYS && m.srows.size() == 21);
        press(BTN_UP); CHECK(m.ssel == 20 && m.stop == 13 && m.srows[20].kind == SRow::RESET);   // wrapped to Reset, window scrolled
        press(BTN_DOWN); CHECK(m.ssel == 0 && m.stop == 0);
        // rebind Cross (row 8): wait for all keys up, then take the first key; a held key from the press is no binding
        for (int i = 0; i < 8; i++) press(BTN_DOWN);
        CHECK(m.srows[8].kind == SRow::KEY && m.row_value(m.srows[8]) == "Z");
        io.keys = { 'Z', 'Z' };                                                         // the key that started it is still held for two feeds
        press(BTN_CROSS); CHECK(m.capturing && !m.cap_armed);                            // feed 1 starts the capture, feed 2 sees Z held
        press(0); CHECK(m.capturing && m.cap_armed);                                     // feed 1 sees Z, feed 2 sees nothing: armed
        CHECK(io.mouse_asked);                                                          // kbm: mouse buttons are offered
        io.keys = { 'C' }; press(0);
        CHECK(!m.capturing && m.row_value(m.srows[8]) == "C" && io.file.find("KBM_KEY_CROSS=0x43") != std::string::npos && io.file.find("PAD_KEY_CROSS=0x43") != std::string::npos);
        CHECK(m.smsg == "Saved. Applies on the next start.");
        // Esc cancels, F11 is refused, a key already used swaps
        press(BTN_CROSS); CHECK(m.capturing && m.cap_armed); io.keys = { 0x1B }; press(0);
        CHECK(!m.capturing && m.smsg == "Cancelled." && m.row_value(m.srows[8]) == "C");
        press(BTN_CROSS); io.keys = { 0x7A }; press(0); CHECK(m.capturing && m.smsg_err && m.smsg.find("F11") == 0);
        io.keys = { 'X' }; press(0);                                                     // Circle holds X: Cross takes X, Circle gets C
        CHECK(!m.capturing && m.row_value(m.srows[8]) == "X" && m.row_value(m.srows[9]) == "C");
        // a controller's Circle cancels a capture
        press(BTN_CROSS); CHECK(m.capturing && m.cap_armed);
        press(BTN_CIRCLE); CHECK(!m.capturing && m.smsg == "Cancelled.");
        // Circle from Keys goes back to Controls on the link row; keyboard mode asks for no mouse buttons
        press(BTN_CIRCLE); CHECK(m.page == CONTROLS && m.srows[(size_t)m.ssel].kind == SRow::OPEN);
        // controller mode: the link is the buttons page, 16 rows and Reset, Left/Right change a button with a swap
        m.ssel = 0; press(BTN_LEFT); press(BTN_LEFT); CHECK(m.model.mode() == 2 && m.srows[2].target == PADBTNS);   // kbm -> keyboard -> controller
        m.ssel = 2; press(BTN_CROSS); CHECK(m.page == PADBTNS && m.srows.size() == 17);
        for (int i = 0; i < 8; i++) press(BTN_DOWN);
        CHECK(m.row_value(m.srows[8]) == "A"); press(BTN_RIGHT);
        CHECK(m.row_value(m.srows[8]) == "B" && m.row_value(m.srows[9]) == "A" && io.file.find("PAD_BTN_CROSS=B") != std::string::npos);
        press(BTN_CIRCLE); CHECK(m.page == CONTROLS);
        press(BTN_CIRCLE); CHECK(m.page == MAIN && m.sel == 2);
        // Launcher only: Cross or Circle goes back
        press(BTN_DOWN); press(BTN_CROSS); CHECK(m.page == ONLINE);   // row 3 is Online (no backend here: read only)
        press(BTN_CIRCLE); CHECK(m.page == MAIN && m.sel == 3);
        press(BTN_DOWN); press(BTN_CROSS); CHECK(m.page == LAUNCHER); press(BTN_CROSS); CHECK(m.page == MAIN && m.sel == 4);
        press(BTN_DOWN); press(BTN_CROSS); CHECK(m.page == CLOSED);
        // an unreadable settings file keeps the page on the main page with the reason
        Menu bad; bad.io = &io; io.load_ok = false;
        bad.feed(BTN_CROSS, 1, true); bad.feed(0, 1, true); bad.feed(BTN_CROSS, 1, true); bad.feed(0, 1, true);
        CHECK(bad.page == MAIN && bad.smsg == "unreadable" && bad.smsg_err);
        bad.feed(BTN_DOWN, 1, true); bad.feed(0, 1, true); bad.feed(BTN_DOWN, 1, true); bad.feed(0, 1, true); bad.feed(BTN_DOWN, 1, true); bad.feed(0, 1, true);   // Online
        bad.feed(BTN_DOWN, 1, true); bad.feed(0, 1, true); CHECK(bad.sel == 4);
        // without an io the old two-row main page is unchanged
        Menu old; CHECK(old.main_rows().size() == 2 && old.main_rows()[0] == ONLINE && old.main_rows()[1] == CLOSED);
    }

    // ================= jsp_settings.cpp: live values, the file =================
    {
        char tmpl[] = "/tmp/jsp_settings_test_XXXXXX";
        const char* dir = mkdtemp(tmpl);
        CHECK(dir != nullptr);
        const std::string path = std::string(dir) + "/play_settings.txt";
        setenv("JSP_SETTINGS_FILE", path.c_str(), 1);
        CHECK(jps::io_path() == path);
        std::string text, note;
        CHECK(jps::io_load(text, note) && text.empty());                                // a missing file is fine
        CHECK(jps::io_save("RES_SCALE=2\r\nX=1\r\n", note) && slurp(path) == "RES_SCALE=2\r\nX=1\r\n");
        CHECK(jps::io_load(text, note) && text == "RES_SCALE=2\r\nX=1\r\n");
        CHECK(jps::io_save("RES_SCALE=3\r\n", note) && slurp(path) == "RES_SCALE=3\r\n");   // replaces the file
        { std::string big(70000, 'a'); CHECK(!jps::io_save(big, note) && slurp(path) == "RES_SCALE=3\r\n"); }   // too large: untouched
        unsetenv("UI_SCALE");
        char b[32];
        const int g0 = jsp_setting_gen();
        CHECK(jsp_setting_get("UI_SCALE", b, sizeof b) == 0 && b[0] == 0);
        setenv("UI_SCALE", "0.8", 1);
        CHECK(jsp_setting_get("UI_SCALE", b, sizeof b) == 1 && !std::strcmp(b, "0.8"));   // the environment until an in-game change
        jsp_setting_set_live("UI_SCALE", "0.60");
        CHECK(jsp_setting_gen() == g0 + 1 && jsp_setting_get("UI_SCALE", b, sizeof b) == 1 && !std::strcmp(b, "0.60"));
        jsp_setting_set_live("UI_SCALE", "");                                           // back to unset: not the old environment value
        CHECK(jsp_setting_gen() == g0 + 2 && jsp_setting_get("UI_SCALE", b, sizeof b) == 0 && getenv("UI_SCALE") == nullptr);
        std::remove(path.c_str()); rmdir(dir);
    }

    if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
    std::printf("settings: all checks passed\n");
    return 0;
}
