// jsp_settings.h -- the launcher's settings as data: the key table, the settings file (play_settings.txt) as an editable line list,
// and the rules the launcher applies when it reads and writes the keys (input modes, key pages, swaps). Header only, no OS calls:
// installer/tools/settings_test.cpp runs it on the host, and compares the table with launcher/JStarsLauncher*.cs.
//
// ENHANCEMENT: part of the in-game settings pages (jsp_port_menu.cpp, wishlist 39). One source of truth for the file format: the
// launcher (launcher/JStarsLauncher19.cs, JStarsLauncher.cs) writes the same keys in the same format; every unknown line (comments,
// keys the launcher keeps for itself, the other tools' keys) is kept as it is when this file edits the list.
//
// Where each key is read (all of them are plain getenv in the game process; the launcher turns every line of the file into an
// environment variable before it starts the game, installer/launcher/launcher.c, launcher/JStarsLauncher*.cs):
//   LIVE        the reader in jsp_game.cpp asks jsp_setting_get(), which an in-game change updates (jsp_settings.cpp)
//   NEXT_START  read once by the toolkit or the port (static cache after the first call): a saved change applies on the next start
#ifndef JSP_SETTINGS_H
#define JSP_SETTINGS_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace jps {

enum Apply { LIVE = 0, NEXT_START = 1 };
enum Group { G_DISPLAY = 0, G_GRAPHICS = 1, G_CONTROLS = 2 };

// One choice setting. values[i] is the text written to the file ("" = the key is removed), names[i] the line the page shows.
struct Item {
    const char* key;
    Group group;
    const char* label;
    const char* const* values;
    const char* const* names;
    int n;
    int def;         // the launcher's own default (what its box shows, and what it writes)
    int reader_def;  // what the game does when the key is absent from the file
    Apply apply;
    const char* reader;   // where it is read (for the docs and the test)
};

static const char* const k_res_v[] = { "1", "1.25", "1.5", "2", "2.5", "3", "4" };
static const char* const k_res_n[] = { "720p (the PS3's own)", "900p", "1080p", "1440p", "1800p", "4K", "5K (heavy)" };
static const char* const k_off_on_v[] = { "0", "1" };
static const char* const k_win_fs_n[] = { "Window", "Fullscreen" };
static const char* const k_winsize_v[] = { "", "1280x720", "1600x900", "1920x1080", "2560x1440", "3200x1800", "3840x2160" };
static const char* const k_winsize_n[] = { "Same as render size", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3200 x 1800", "3840 x 2160" };
static const char* const k_off_n[] = { "Off", "On" };
static const char* const k_shadow_v[] = { "1", "2", "3", "4" };
static const char* const k_shadow_n[] = { "Low (the game's own)", "Medium (2x)", "High (3x)", "Ultra (4x)" };
static const char* const k_srange_v[] = { "1", "2" };
static const char* const k_srange_n[] = { "Normal (the game's own)", "Far (2x)" };
static const char* const k_mdetail_v[] = { "1", "2", "4" };
static const char* const k_mdetail_n[] = { "Normal (the game's own)", "High (2x further)", "Ultra (4x further)" };
static const char* const k_odist_v[] = { "0.75", "1", "2", "0" };
static const char* const k_odist_n[] = { "Near", "Normal (the game's own)", "Far (2x)", "Max (never disappear)" };
static const char* const k_ui_v[] = { "0.50", "0.55", "0.60", "0.65", "0.70", "0.75", "0.80", "0.85", "0.90", "0.95", "1.00" };
static const char* const k_ui_n[] = { "50%", "55%", "60%", "65%", "70%", "75%", "80%", "85%", "90%", "95%", "100% (the original)" };
static const char* const k_aa_v[] = { "off", "fxaa", "fxaa2", "smaa" };
static const char* const k_aa_n[] = { "Off", "FXAA", "FXAA, stronger", "SMAA" };
static const char* const k_mode_v[] = { "keyboard", "kbm", "controller" };
static const char* const k_mode_n[] = { "Keyboard", "Keyboard and mouse", "Controller" };
static const char* const k_sens_v[] = { "0.25", "0.50", "0.75", "1.00", "1.25", "1.50", "1.75", "2.00", "2.25", "2.50",
                                        "2.75", "3.00", "3.25", "3.50", "3.75", "4.00", "4.25", "4.50", "4.75", "5.00" };
static const char* const k_sens_n[] = { "0.25x", "0.50x", "0.75x", "1.00x", "1.25x", "1.50x", "1.75x", "2.00x", "2.25x", "2.50x",
                                        "2.75x", "3.00x", "3.25x", "3.50x", "3.75x", "4.00x", "4.25x", "4.50x", "4.75x", "5.00x" };

#define JPS_N(a) ((int)(sizeof(a) / sizeof((a)[0])))
// key, group, label, values, names, n, launcher default, reader default, apply, where read
static const Item k_items[] = {
    { "RES_SCALE",       G_DISPLAY,  "Render resolution", k_res_v, k_res_n, JPS_N(k_res_v), 0, 0, NEXT_START, "toolkit 2503/2504, once at the first present" },
    { "FULLSCREEN",      G_DISPLAY,  "Display mode", k_off_on_v, k_win_fs_n, 2, 0, 0, NEXT_START, "toolkit 2500, at window creation (F11 switches in the game)" },
    { "WINDOW_SIZE",     G_DISPLAY,  "Window size", k_winsize_v, k_winsize_n, JPS_N(k_winsize_v), 0, 0, NEXT_START, "toolkit 2500, at window creation" },
    { "TITLE_STATS",     G_DISPLAY,  "FPS in the window title", k_off_on_v, k_off_n, 2, 0, 0, NEXT_START, "toolkit 2500/0007 (the launchers set 0 when unset)" },
    { "SHADOW_SCALE",    G_GRAPHICS, "Shadows", k_shadow_v, k_shadow_n, 4, 1, 1, NEXT_START, "toolkit 2502, static cache" },
    { "SHADOW_RANGE",    G_GRAPHICS, "Shadow distance", k_srange_v, k_srange_n, 2, 0, 0, LIVE, "jsp_game.cpp shadow_range_tick" },
    { "MODEL_DETAIL",    G_GRAPHICS, "Model detail", k_mdetail_v, k_mdetail_n, 3, 0, 0, LIVE, "jsp_game.cpp lod_select_hook" },
    { "OBJECT_DISTANCE", G_GRAPHICS, "Object draw distance", k_odist_v, k_odist_n, 4, 1, 1, LIVE, "jsp_game.cpp lod_select_hook" },
    { "UI_SCALE",        G_GRAPHICS, "Fight HUD size", k_ui_v, k_ui_n, 11, 10, 10, LIVE, "jsp_game.cpp ui_scale" },
    { "AA_MODE",         G_GRAPHICS, "Edge smoothing", k_aa_v, k_aa_n, 4, 1, 0, LIVE, "jsp_game.cpp aa_mode (unset = off; the launcher writes fxaa)" },
    { "FX_FULL",         G_GRAPHICS, "Full-size smoke and effects", k_off_on_v, k_off_n, 2, 1, 0, NEXT_START, "toolkit 2512, static cache (unset = off; the launcher writes 1)" },
    { "FPS_COUNTER",     G_GRAPHICS, "FPS counter (bottom left)", k_off_on_v, k_off_n, 2, 0, 0, LIVE, "jsp_game.cpp, g_rsx_stamp2 hook" },
    { "INPUT_MODE",      G_CONTROLS, "Input", k_mode_v, k_mode_n, 3, 0, 0, NEXT_START, "toolkit 6500 pad_input_mode, static cache (unset = a keyboard fills in when no pad is on port 0)" },
    { "MOUSE_SENS",      G_CONTROLS, "Mouse sensitivity", k_sens_v, k_sens_n, 20, 3, 3, NEXT_START, "toolkit 6500, static cache" },
};
static const int k_item_count = JPS_N(k_items);

inline const Item* find_item(const char* key)
{
    for (int i = 0; i < k_item_count; i++) if (!std::strcmp(k_items[i].key, key)) return &k_items[i];
    return nullptr;
}

// Keys the launcher owns and the in-game pages do not offer, with the reason (shown on the "Launcher only" page and checked by the test).
struct LauncherOnly { const char* key; const char* what; const char* why; };
static const LauncherOnly k_launcher_only[] = {
    { "JSP_UNLOCK_ALL", "Unlock everything (cheat)", "the save is backed up before the game starts; the game loads its save first, so it cannot back itself up" },
    { "BOOST_OFF_ON_EXIT", "CPU boost off again at exit", "the launcher sets and restores the power plan around the game" },
    { "CPU_BOOST", "CPU boost while playing", "the launcher changes the power plan before the game starts" },
    { "(disc, EBOOT)", "Game disc folder, EBOOT, rebuild", "chosen at install; changing them needs a build of the port" },
    { "(Online tab)", "Preset list, Find on LAN", "list editing and a network scan; the Online page here edits the active choice" },
};
static const int k_launcher_only_count = JPS_N(k_launcher_only);

// ---- keys and controller buttons (the Controls tab) ----
struct Action { const char* id; const char* label; int def_vk; };
static const Action k_actions[] = {
    { "LSTICK_UP", "Left stick up", 0x26 }, { "LSTICK_DOWN", "Left stick down", 0x28 }, { "LSTICK_LEFT", "Left stick left", 0x25 },
    { "LSTICK_RIGHT", "Left stick right", 0x27 }, { "DPAD_UP", "D-pad up", 'I' }, { "DPAD_DOWN", "D-pad down", 'K' },
    { "DPAD_LEFT", "D-pad left", 'J' }, { "DPAD_RIGHT", "D-pad right", 'L' }, { "CROSS", "Cross", 'Z' }, { "CIRCLE", "Circle", 'X' },
    { "SQUARE", "Square", 'A' }, { "TRIANGLE", "Triangle", 'S' }, { "L1", "L1", 'Q' }, { "R1", "R1", 'W' }, { "L2", "L2", '1' },
    { "R2", "R2", '2' }, { "L3", "L3", '3' }, { "R3", "R3", '4' }, { "START", "Start", 0x0D }, { "SELECT", "Select", 0x09 },
};
static const int k_action_count = JPS_N(k_actions);

struct PadBtn { const char* id; const char* label; const char* def; };
static const char* const k_xbuttons[] = { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "START", "BACK", "DUP", "DDOWN", "DLEFT", "DRIGHT" };
static const int k_xbutton_count = JPS_N(k_xbuttons);
static const char* const k_xbutton_names[] = { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS (stick click)", "RS (stick click)", "Start", "Back",
                                               "D-pad up", "D-pad down", "D-pad left", "D-pad right" };
static const PadBtn k_padbtns[] = {
    { "DPAD_UP", "D-pad up", "DUP" }, { "DPAD_DOWN", "D-pad down", "DDOWN" }, { "DPAD_LEFT", "D-pad left", "DLEFT" },
    { "DPAD_RIGHT", "D-pad right", "DRIGHT" }, { "L1", "L1", "LB" }, { "R1", "R1", "RB" }, { "L2", "L2", "LT" }, { "R2", "R2", "RT" },
    { "CROSS", "Cross", "A" }, { "CIRCLE", "Circle", "B" }, { "SQUARE", "Square", "X" }, { "TRIANGLE", "Triangle", "Y" },
    { "L3", "L3", "LS" }, { "R3", "R3", "RS" }, { "START", "Start", "START" }, { "SELECT", "Select", "BACK" },
};
static const int k_padbtn_count = JPS_N(k_padbtns);

// The key's name as a player reads it. Mouse buttons are VK 1, 2, 4, 5, 6.
inline std::string key_name(int vk)
{
    char b[24];
    if (vk >= 'A' && vk <= 'Z') { b[0] = (char)vk; b[1] = 0; return b; }
    if (vk >= '0' && vk <= '9') { b[0] = (char)vk; b[1] = 0; return b; }
    if (vk >= 0x70 && vk <= 0x87) { std::snprintf(b, sizeof b, "F%d", vk - 0x6F); return b; }
    if (vk >= 0x60 && vk <= 0x69) { std::snprintf(b, sizeof b, "Num %d", vk - 0x60); return b; }
    switch (vk) {
    case 0x01: return "Left mouse"; case 0x02: return "Right mouse"; case 0x04: return "Middle mouse";
    case 0x05: return "Mouse side (back)"; case 0x06: return "Mouse side (forward)";
    case 0x08: return "Backspace"; case 0x09: return "Tab"; case 0x0D: return "Enter"; case 0x10: return "Shift"; case 0x11: return "Ctrl";
    case 0x12: return "Alt"; case 0x14: return "Caps Lock"; case 0x1B: return "Esc"; case 0x20: return "Space"; case 0x21: return "Page Up";
    case 0x22: return "Page Down"; case 0x23: return "End"; case 0x24: return "Home"; case 0x25: return "Left arrow"; case 0x26: return "Up arrow";
    case 0x27: return "Right arrow"; case 0x28: return "Down arrow"; case 0x2D: return "Insert"; case 0x2E: return "Delete";
    case 0x6A: return "Num *"; case 0x6B: return "Num +"; case 0x6D: return "Num -"; case 0x6E: return "Num ."; case 0x6F: return "Num /";
    case 0xBA: return ";"; case 0xBB: return "="; case 0xBC: return ","; case 0xBD: return "-"; case 0xBE: return "."; case 0xBF: return "/";
    case 0xC0: return "`"; case 0xDB: return "["; case 0xDC: return "\\"; case 0xDD: return "]"; case 0xDE: return "'";
    }
    std::snprintf(b, sizeof b, "Key 0x%02X", vk);
    return b;
}

// ---- the settings file as a list of lines ----
struct File {
    std::vector<std::string> lines;
    bool crlf = true;      // a new file and a file that already uses CRLF are written with CRLF (the launcher's WriteAllLines)

    static bool split(const std::string& line, std::string& k, std::string& v)
    {
        size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos || line[a] == '#' || line[a] == ';') return false;
        const size_t eq = line.find('=', a);
        if (eq == std::string::npos || eq == a) return false;
        size_t e = eq; while (e > a && (line[e - 1] == ' ' || line[e - 1] == '\t')) e--;
        k = line.substr(a, e - a);
        size_t b = eq + 1; while (b < line.size() && (line[b] == ' ' || line[b] == '\t')) b++;
        size_t f = line.size(); while (f > b && (line[f - 1] == ' ' || line[f - 1] == '\t' || line[f - 1] == '\r')) f--;
        v = line.substr(b, f - b);
        return true;
    }
    static File parse(const std::string& text)
    {
        File f;
        f.crlf = text.empty() || text.find("\r\n") != std::string::npos;
        size_t p = 0;
        while (p < text.size()) {
            size_t q = text.find('\n', p);
            std::string l = text.substr(p, q == std::string::npos ? std::string::npos : q - p);
            if (!l.empty() && l.back() == '\r') l.pop_back();
            f.lines.push_back(l);
            if (q == std::string::npos) break;
            p = q + 1;
        }
        return f;
    }
    std::string text() const
    {
        std::string t;
        for (const auto& l : lines) { t += l; t += crlf ? "\r\n" : "\n"; }
        return t;
    }
    // the value of the last line with this key (the launcher's environment conversion lets a later line win)
    bool get(const std::string& key, std::string& out) const
    {
        bool found = false;
        for (const auto& l : lines) { std::string k, v; if (split(l, k, v) && k == key) { out = v; found = true; } }
        return found;
    }
    bool has_prefix(const char* prefix) const
    {
        const size_t n = std::strlen(prefix);
        for (const auto& l : lines) { std::string k, v; if (split(l, k, v) && k.compare(0, n, prefix) == 0) return true; }
        return false;
    }
    void erase(const std::string& key)
    {
        for (size_t i = lines.size(); i-- > 0;) { std::string k, v; if (split(lines[i], k, v) && k == key) lines.erase(lines.begin() + (long)i); }
    }
    // changes the first line with the key (later duplicates go), or appends one; the line's own spelling of the key is kept
    void set(const std::string& key, const std::string& val)
    {
        bool done = false;
        for (size_t i = 0; i < lines.size();) {
            std::string k, v;
            if (split(lines[i], k, v) && k == key) {
                if (done) { lines.erase(lines.begin() + (long)i); continue; }
                lines[i] = key + "=" + val; done = true;
            }
            i++;
        }
        if (!done) lines.push_back(key + "=" + val);
    }
};

inline bool parse_vk(const std::string& s, int* vk)
{
    if (s.empty()) return false;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? 16 : 10);
    if (!end || *end || v <= 0 || v >= 256) return false;
    *vk = (int)v;
    return true;
}
inline std::string vk_text(int vk) { char b[12]; std::snprintf(b, sizeof b, "0x%02X", vk & 0xFF); return b; }

inline bool ieq(const std::string& a, const char* b)
{
    size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; i++) { char x = a[i], y = b[i]; if (x >= 'A' && x <= 'Z') x = (char)(x + 32); if (y >= 'A' && y <= 'Z') y = (char)(y + 32); if (x != y) return false; }
    return true;
}

// the value text -> index in the item's list: exact, then case-insensitive, then equal as numbers; -1 = not one of ours
inline int item_index(const Item& it, const std::string& v)
{
    for (int i = 0; i < it.n; i++) if (v == it.values[i]) return i;
    for (int i = 0; i < it.n; i++) if (ieq(v, it.values[i])) return i;
    char* e1 = nullptr; const double a = std::strtod(v.c_str(), &e1);
    if (!v.empty() && e1 && !*e1)
        for (int i = 0; i < it.n; i++) {
            if (!it.values[i][0]) continue;
            char* e2 = nullptr; const double b = std::strtod(it.values[i], &e2);
            if (e2 && !*e2 && a == b) return i;
        }
    return -1;
}

// What the pages show and change. The file is the truth the launcher reads; `active` (a getter for the game's own environment) is
// only used to tell which saved values the running game has not picked up.
struct Model {
    File f;

    // ---- choice items ----
    // the saved index (file, else what the game does without the key); -2 = the file holds something that is not one of ours
    int saved_index(const Item& it) const
    {
        std::string v;
        if (!f.get(it.key, v)) return it.reader_def;
        if (!v.size() && it.values[0][0]) return it.reader_def;
        const int i = item_index(it, v);
        return i < 0 ? -2 : i;
    }
    std::string saved_text(const Item& it) const
    {
        const int i = saved_index(it);
        if (i >= 0) return it.names[i];
        std::string v; f.get(it.key, v);
        return v + " (not in the list)";
    }
    // writes one choice; the launcher's list entry "" removes the key. The caller saves the file.
    void set_index(const Item& it, int idx)
    {
        if (idx < 0 || idx >= it.n) return;
        if (!it.values[idx][0]) f.erase(it.key); else f.set(it.key, it.values[idx]);
        if (!std::strcmp(it.key, "INPUT_MODE")) sync_pad_keys();   // the game reads PAD_KEY_* of the page of the chosen mode
    }
    void cycle(const Item& it, int dir)
    {
        int i = saved_index(it);
        if (i < 0) i = dir > 0 ? -1 : 0;   // a foreign value: the first step lands on an end of the list
        set_index(it, ((i + dir) % it.n + it.n) % it.n);
    }
    // true when the running game has not picked up the saved value (a NEXT_START item whose environment differs)
    template <class ActiveFn> bool pending(const Item& it, ActiveFn active) const
    {
        if (it.apply == LIVE) return false;
        const char* e = active(it.key);
        const int a = (e && *e) ? item_index(it, e) : it.reader_def;
        return a != saved_index(it);
    }

    // ---- input mode and the key pages ----
    int mode() const
    {
        std::string v;
        if (!f.get("INPUT_MODE", v)) return 0;
        for (int i = 0; i < 3; i++) if (ieq(v, k_mode_v[i])) return i;
        return 0;
    }
    static const char* page_prefix(int mode) { return mode == 1 ? "KBM_KEY_" : "KB_KEY_"; }   // the launcher's CurrentKeys(): mouse page for kbm, else keyboard page

    static int kb_default(int a) { return k_actions[a].def_vk; }
    // the key an action has on the keyboard page (prefix "KB_KEY_", else PAD_KEY_ of before the input types) or the mouse page
    int page_vk(int mode, int a) const
    {
        const bool kbm = mode == 1;
        std::string v; int vk;
        std::string key = std::string(kbm ? "KBM_KEY_" : "KB_KEY_") + k_actions[a].id;
        if (kbm ? f.has_prefix("KBM_KEY_") : f.has_prefix("KB_KEY_")) {
            if (f.get(key, v) && parse_vk(v, &vk)) return vk;
            return kb_default(a);
        }
        if (kbm) return page_vk(0, a);   // no mouse page saved: a copy of the keyboard page (the launcher's rule)
        key = std::string("PAD_KEY_") + k_actions[a].id;
        if (f.get(key, v) && parse_vk(v, &vk)) return vk;
        return kb_default(a);
    }
    // Writes the page of `mode` and PAD_KEY_* (what the game reads). The other page is written too when the file has none yet, so
    // the launcher finds both (it reads KB_KEY_ else PAD_KEY_, and KBM_KEY_ else a copy of the keyboard page): the keyboard page
    // keeps what it was (read before PAD_KEY_ changes), a missing mouse page becomes a copy of the new keyboard page.
    void write_page(int mode, const int* vk)
    {
        const int other = mode == 1 ? 0 : 1;
        int ov[32];
        const bool other_missing = !f.has_prefix(page_prefix(other));
        if (other_missing) for (int a = 0; a < k_action_count; a++) ov[a] = mode == 1 ? page_vk(0, a) : vk[a];
        for (int a = 0; a < k_action_count; a++) f.set(std::string("PAD_KEY_") + k_actions[a].id, vk_text(vk[a]));
        for (int a = 0; a < k_action_count; a++) f.set(std::string(page_prefix(mode)) + k_actions[a].id, vk_text(vk[a]));
        if (other_missing) for (int a = 0; a < k_action_count; a++) f.set(std::string(page_prefix(other)) + k_actions[a].id, vk_text(ov[a]));
    }
    // PAD_KEY_* (what the game reads) follow the page of the current mode, as the launcher's SaveSettings writes them
    void sync_pad_keys()
    {
        const int m = mode();
        int vk[32];
        for (int a = 0; a < k_action_count; a++) vk[a] = page_vk(m, a);
        write_page(m, vk);
    }
    int key_vk(int a) const { return page_vk(mode(), a); }
    // binds one action; the action that had the key gets the old one (a swap, as the launcher does)
    void set_key(int a, int new_vk)
    {
        const int m = mode();
        int vk[32];
        for (int i = 0; i < k_action_count; i++) vk[i] = page_vk(m, i);
        const int old = vk[a];
        for (int i = 0; i < k_action_count; i++) if (i != a && vk[i] == new_vk) vk[i] = old;
        vk[a] = new_vk;
        write_page(m, vk);
    }
    void reset_keys()
    {
        const int m = mode();
        int vk[32];
        for (int i = 0; i < k_action_count; i++) vk[i] = kb_default(i);
        write_page(m, vk);
    }

    // ---- controller buttons ----
    static int xbutton_index(const std::string& s) { for (int i = 0; i < k_xbutton_count; i++) if (ieq(s, k_xbuttons[i])) return i; return -1; }
    int pad_btn(int b) const
    {
        std::string v;
        if (f.get(std::string("PAD_BTN_") + k_padbtns[b].id, v)) { const int i = xbutton_index(v); if (i >= 0) return i; }
        return xbutton_index(k_padbtns[b].def);
    }
    void set_pad_btn(int b, int x)
    {
        int cur[32];
        for (int i = 0; i < k_padbtn_count; i++) cur[i] = pad_btn(i);
        const int old = cur[b];
        for (int i = 0; i < k_padbtn_count; i++) if (i != b && cur[i] == x) cur[i] = old;   // swap with whoever had that button
        cur[b] = x;
        for (int i = 0; i < k_padbtn_count; i++) f.set(std::string("PAD_BTN_") + k_padbtns[i].id, k_xbuttons[cur[i]]);
    }
    void reset_pad_btns()
    {
        for (int i = 0; i < k_padbtn_count; i++) f.set(std::string("PAD_BTN_") + k_padbtns[i].id, k_padbtns[i].def);
    }
    // sets every item of a group to the launcher's defaults
    void reset_group(Group g)
    {
        for (int i = 0; i < k_item_count; i++)
            if (k_items[i].group == g) { if (std::strcmp(k_items[i].key, "INPUT_MODE")) set_index(k_items[i], k_items[i].def); }
    }
};

}  // namespace jps

#endif
