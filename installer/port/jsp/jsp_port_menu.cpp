// jsp_port_menu.cpp -- in-game "Port Settings" page.
//
// ENHANCEMENT: the whole file. On by default (JSP_PORT_MENU=0 turns it off), and only for the US 1.00 build (the hooks sit at fixed addresses of that code; the
// check below refuses any other build). Needs toolkit patches 2516-2518 (overlay API, label mask: jsp_overlay_api.h) and 6501 (pad hooks
// g_cellpad_filter, g_cellpad_rstick_filter), and a lift made with installer/port/jsp/lift_insn_patches.json (the Options row count, below).
// Adapted from the maintainers' development tree; the Online page text comes from jsp_online_describe (jsp_online.cpp).
//
// What it does:
//   1. The Options panel (menu id 8) gets a 6th selectable row. @0x001E0830 builds the main-menu object and its cursor
//      structs; the Options one is at obj+0xF4 (not +0xA4, that is Online, menu id 4: the jump table at 0x1DD638 sends
//      id 8 to 0x1DD834 which reads +0xF4, id 4 to 0x1DD980 which reads +0xA4). Struct {total, cursor, visible, top, last}
//      from func_001C07F0(cur, top 0, cursor, 5, 5): after it returns the wrapper makes it {6, cursor, 6, 0, 5}.
//      The label binder @0x001E6C68(obj, 0x29, 0x45, 5) is asked for 6 labels (it is reached only by two chained
//      tail calls, 0x001E7238 -> 0x001E6E98 -> 0x001E6C68 through g_trampoline_fn, never through the function table, so
//      the wrapper sits on 0x001E7238 and finishes the middle link itself), so the sixth frame binds texture 0x2E/0x4A
//      (menu2_{off,on}_tx_ranking_000), which the label mask (jsp_menu_label_set bit 1, patch 2518) replaces by "Port Settings"
//      while the Options panel is the open one.
//   2. Both hooks are reached through the function table (indirect calls); the choice of the row is not: @0x001DD5DC is
//      called directly by several lifted functions, so it cannot be wrapped. Instead the pad filter sees the composed
//      report of port 0: Cross, newly pressed, while the Options panel is open with the cursor on row 5, opens the page
//      and the game never sees that press (so it never runs its "row > 4" tail @0x001DD700).
//   3. While the page is open the filter hands the game an empty report (buttons 0, sticks centred; pressure words are NOT touched: see below) and
//      the page reads the raw one: Up/Down select, Cross opens, Circle goes back / closes. After Circle closes the page
//      the report stays empty until every button has been released, so the held Circle is no press on the panel.
//   4. The page is drawn with jsp_overlay_* (1280x720 layout space). Sub-page "Online": the settings jsp_online.cpp
//      read at start; editable (Method, Server, Online name, Community preset, Save, Discard) through the backend of jsp_online.cpp. JSP_PORT_MENU_EDIT=0 keeps it read only.
//
// The game's own row count: @0x001E6E98 sets the count r25 from the menu id (id 8: `li r25,5` at 0x001E71C0) and then hides the parts of every
// row >= count at each update (loop 0x001E6FC4) and passes the count on to the label binder. The lifter's --insn-patches option (firstrun.py
// passes lift_insn_patches.json for the US build) emits at 0x001E71C0 `if (g_port_menu_six_rows) li r25,6 else li r25,5`, so the game's loop
// shows row 5 and the binder gets 6 labels itself. The switch is set below (unless JSP_PORT_MENU=0) before the guest runs. The lift
// carries the patch for every US build whatever JSP_PORT_MENU says (firstrun.py does not read it), so the switch alone decides at start.
//
// Pressure: patch 6501's filter changes buttons and sticks only, and runs before cellPadGetData fills the pressure words (only when
// the game turned PRESS_ON on). Those words then come from the host pad's own pressure values and are not blanked while the page is
// open. ASSUMPTION: the game's menus read the digital bits (or the game does not enable PRESS_ON), so this is harmless; a run
// must check it (does the menu move behind the page with a pressure-capable pad).
//
// ASSUMPTIONS (to confirm with a run; every transition is logged as [portmenu]):
//   - 001E0830's r3 is the main-menu object (cursor structs at +0x40.., Options at +0xF4); 001E7238's / 001E6C68's r3 is the panel
//     object (id at +0x44, state at +0x38, state 3 = the Options list is active).
//   - the confirm button is Cross (US enter-button assignment).
//   - fl_sele_6 / fl_ok_6 and the sixth label part exist and show for total 6 (one panel reused, frames 1..6).
//   - the idle timers of the main menu are untouched by the pad filter because the swallowed report is "no input"
//     exactly like an idle player; if the demo starts behind the page it needs the activity counter (not done).
//   - the port's "Exit Game" option (jsp_game.cpp, exit_option_on_entry) shares the Options panel (it takes over Cross-Save, row 0, drawn at
//     the bottom; Port Settings is row 5, drawn at the top). It opens its box on Cross/Start only when in_menu_scene() holds and two words
//     of the game read as they did on the five-row list: the Options cursor counted from the bottom (0x008E63FC == 0) and the menu state
//     (0x0095654C == 5, the Options list; 9 = the Cross-Save submenu). ASSUMPTION: with six rows 0x008E63FC is still 0 only on Cross-Save
//     (never on the Port Settings row) and 0x0095654C still reads 5 on the six-row list and while this page is open over it. Exit Game's
//     filter runs before this one on the raw report (g_prev_filter) and handles ports 0 and 1; this page empties port 0's report only
//     (pad_filter returns early for any other port), so while the page is open the game still sees port 1's input. Not checked: needs a
//     run with both on (both are on by default now).

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "jsp_version_line.h"   // the version line (text and value choice, host-tested: installer/tools/version_line_test.cpp)
#include "jsp_online_status.h"
#include "jsp_overlay_owner.h"
#include "jsp_net_online.h"      // ENHANCEMENT (in-game netcode option): the one choice Off / re-psn (standard) / re-psn (rollback) / Community (host-tested: installer/tools/net_online_test.cpp)
#include "jsp_overlay_api.h"
#include "jsp_settings.h"        // the launcher's settings as data and the settings file model (host-tested: installer/tools/settings_test.cpp)

#ifndef JSP_PORT_MENU_TEST
#include <mutex>
#ifdef _WIN32
extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameA(void*, char*, unsigned long);   // no <windows.h>: its macros clash with the recomp headers
#endif
#include "ps3emu/ps3types.h"
#include "ppu_recomp.h"
#include "jsp_overlay_owner.h"
#include "jsp_overlay_api.h"
extern "C" uint32_t cellGcm_flip_request_count(void);
extern "C" void ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));
// Toolkit patch 7908 (ENHANCEMENT, player order): ask for the join; the toolkit calls the notifier (outside its pad lock) when join starts, ends or the next player changes.
extern "C" void pad_join_request(void);
extern "C" void (*g_pad_join_notify)(int joining, int next_player);
// Toolkit patch 6501: hooks in cellPadGetData, NULL by default (jsp_game.cpp sets them first; this file chains behind them).
extern "C" void (*g_cellpad_filter)(uint32_t port_no, uint16_t* buttons, uint16_t* lx, uint16_t* ly);
extern "C" void (*g_cellpad_rstick_filter)(uint32_t port_no, uint16_t* rx, uint16_t* ry);
extern "C" int jsp_online_describe(int line, char* out, int n);                  // jsp_online.cpp
extern "C" int jsp_online_edit_begin(char* err, int n);                            // jsp_online.cpp: the page's editing API
extern "C" int jsp_online_edit_row(int field, char* out, int n);
extern "C" int jsp_online_edit_get(int field, char* out, int n);
extern "C" int jsp_online_edit_set(int field, const char* v, char* hint, int n);
extern "C" void jsp_online_edit_cycle(int field, int dir);
extern "C" int jsp_online_edit_save(char* msg, int n);
extern "C" int jsp_online_running_method(void);
extern "C" int jsp_online_edit_method(void);                      // ENHANCEMENT (in-game netcode option): 0 off, 1 re-psn, 2 community (the working copy)
extern "C" void jsp_online_edit_set_method(int m);
extern "C" int jsp_online_edit_check(char* why, int n);            // 0 the working copy can be saved, 1 not (reason in why)
extern "C" void jsp_setting_set_live(const char* key, const char* value);        // jsp_settings.cpp
extern "C" int jsp_net_notice(char* line1, int n1, char* line2, int n2);          // jsp_net_game.cpp: the refused-rollback notice, else the re-psn rendezvous notice (1 = there is one)
extern "C" unsigned jsp_net_notice_gen(void);                                     // jsp_net_game.cpp: changes when the rendezvous notice text changes
#include <chrono>
namespace jps { std::string io_path(); bool io_load(std::string& text, std::string& note); bool io_save(const std::string& text, std::string& err); }
#endif

namespace jpm {

// Digital buttons as one mask: (DIGITAL2 << 8) | DIGITAL1 (cellPad.h CELL_PAD_CTRL_*; the same masks PAD_SCRIPT uses).
enum : uint16_t { BTN_START = 0x0008, BTN_UP = 0x0010, BTN_RIGHT = 0x0020, BTN_DOWN = 0x0040, BTN_LEFT = 0x0080,
                  BTN_L1 = 0x0400, BTN_R1 = 0x0800, BTN_CIRCLE = 0x2000, BTN_CROSS = 0x4000, BTN_SQUARE = 0x8000 };
enum Page { CLOSED = 0, MAIN = 1, ONLINE = 2, DISPLAY = 3, GRAPHICS = 4, CONTROLS = 5, KEYS = 6, PADBTNS = 7, LAUNCHER = 8 };
constexpr int kSettingsVisible = 8;   // rows shown at once on a settings page
// The settings pages' rows (jsp_settings.h has the data): a choice item, a key, a controller button, a link to a sub-page, "Reset".
struct SRow { enum Kind { ITEM, KEY, PADBTN, OPEN, RESET, JOIN } kind; int idx; Page target; };   // JOIN: "Player order" (starts the pad join, toolkit patch 7908)
// the Online page's rows: 0 Method, 1 Server, 2 Online name, 3 Community preset, 4 Save, 5 Discard changes.
enum : int { ROW_METHOD = 0, ROW_SERVER = 1, ROW_NAME = 2, ROW_PRESET = 3, ROW_SAVE = 4, ROW_DISCARD = 5, kEditRows = 6 };

// What the Online page edits (jsp_online.cpp's working copy in the game; a fake in the host test). Fields: 0 method, 1 server, 2 name, 3 preset.
struct Backend {
    virtual ~Backend() {}
    virtual bool begin(std::string& note) = 0;                         // (re)read the files; false: note says why
    virtual std::string row_text(int field) = 0;                       // display line of rows 0..3
    virtual std::string get_text(int field) = 0;                       // raw text of field 1 / 2
    virtual std::string set_text(int field, const std::string& v) = 0; // store; "" valid, else the hint
    virtual void cycle(int field, int dir) = 0;                        // method / preset, wraps
    virtual bool save(std::string& msg) = 0;                           // writes the files; msg: confirmation or reason
    virtual const char* charset(int field) = 0;                        // the letters the picker offers, in order
    virtual size_t max_len(int field) = 0;
    // ENHANCEMENT (in-game netcode option): the one choice drives the method directly (defaults: a backend without them has no Netcode row use)
    virtual bool connection_status(JspOnlineStatus&) { return false; }
    virtual int running_method() { return 1; }                          // immutable startup backend
    virtual int method() { return 0; }                                   // 0 off, 1 re-psn, 2 community
    virtual void set_method(int) {}
    virtual std::string check() { return ""; }                           // "" when the working copy can be saved, else the reason
};

// The OS side of the settings pages (jsp_settings.cpp in the game; a fake in the host test).
struct SettingsIO {
    virtual ~SettingsIO() {}
    virtual bool load(std::string& text, std::string& note) = 0;       // the settings file; a missing file is fine (empty text)
    virtual bool save(const std::string& text, std::string& err) = 0;  // atomic write
    virtual const char* active(const char* key) = 0;                    // the value the running game started with (nullptr: unset)
    virtual void live(const char* key, const char* value) = 0;          // a live setting changed ("" = unset): the readers pick it up
    virtual int poll_vk(bool mouse) = 0;                                // a key held right now (Windows virtual-key code), 0 = none
    virtual std::string path() = 0;
    virtual void pad_join() {}                                          // start the player-order join (toolkit patch 7908); the page closes after it
};

// The Options panel's cursor struct (func_001C07F0: [0] total, [4] cursor, [8] visible, [0xC] top, [0x10] last).
// Returns true when it held the stock 5-row layout and was changed to 6 rows. The cursor is kept.
inline bool six_rows(int32_t* total, int32_t* visible, int32_t* last)
{
    if (*total != 5 || *visible != 5 || *last != 4) return false;
    *total = 6; *visible = 6; *last = 5;
    return true;
}
constexpr int kPortRow = 5;
inline int row_of(int32_t cursor, int32_t top) { return cursor - top; }   // func_001C0C88
// JSP_PORT_MENU: unset or empty = on (the default); a value starting with '0' = off; anything else = on.
inline bool port_menu_on(const char* v) { return !(v && v[0] == '0'); }

// The page's state machine; no guest memory, no drawing: everything the pad does.
struct Menu {
    Page page = CLOSED;
    int sel = 0;
    uint16_t prev = 0;
    bool wait_release = false;
    bool dirty = false;
    // the editable Online page. be == nullptr keeps the page read only.
    Backend* be = nullptr;
    int osel = 0;                 // selected row of the Online page
    bool editing = false;         // the character picker is open on field ef
    int ef = 0;
    std::string buf;              // picker text
    size_t cpos = 0;              // picker position; == buf.size() is the empty slot after the last letter
    std::string msg;              // line under the rows
    bool msg_err = false, edited = false;
    bool connection_have = false, connection_sampled = false;
    uint64_t connection_sample_ms = 0;
    JspOnlineStatus connection_observed = jpon::empty();
    std::vector<std::string> events;   // for the log, drained by the glue
    // the settings pages (Display, Graphics, Controls, Launcher only). io == nullptr: no such pages (JSP_PORT_MENU_EDIT=0).
    SettingsIO* io = nullptr;
    jps::Model model;                  // the settings file as read when the page opened, with the changes since
    std::vector<SRow> srows;           // rows of the open settings page
    int ssel = 0, stop = 0;            // selected row, first visible row
    bool capturing = false, cap_armed = false;   // waiting for a key on a KEYS row (armed = all keys were up once)
    bool smsg_err = false;
    std::string smsg;                  // the line under the settings rows
    // ENHANCEMENT (in-game netcode option): the Netcode choice of the Online page (jsp_net_online.h). net_on: the page has the Netcode rows (needs be and io: the Rollback
    // keys live in play_settings.txt). Without them the page is the older one, row for row.
    bool net_on = false;
    int nc = 0, nc_saved = 0, nc_running = 0;   // working choice, saved choice, what this process started with (jpno::Netcode)
    bool nc_mods_off = false;                   // a gameplay mod set turned the game's own online off for this session (jpno::kKeyModsOff)
    jpno::Rollback rb;
    std::string run_sig, saved_sig;     // rollback configuration the process started with / as saved (jpno::rb_signature): a role-only change needs a restart too
    int run_method = 1;                 // immutable startup backend, independent of the edited choice
    int saved_method = 1;               // the backend's method as saved (for the status line)
    bool saved_direct = false;          // the saved rollback is a direct IP one
    bool direct_pref = false;           // the saved rollback is a direct IP one (or the Connection row was set to it): choosing re-psn (rollback) again keeps it
    std::string net_note;

    bool active() const { return page != CLOSED || wait_release; }

    // One composed report. len = the report length (0: a drain read, nothing new, state does not advance).
    // in_port_row: the Options panel is open with the cursor on its sixth row. Returns true when the game must see
    // an empty report.
    bool feed(uint16_t raw, int len, bool in_port_row)
    {
        if (len == 0) return active();
        const uint16_t edge = (uint16_t)(raw & ~prev);
        prev = raw;
        if (wait_release) {
            if (raw != 0) return true;
            wait_release = false;
            return false;
        }
        if (page == CLOSED) {
            if (in_port_row && (edge & BTN_CROSS)) { page = MAIN; sel = 0; dirty = true; return true; }
            return false;
        }
        if (page == ONLINE && be) { feed_online(edge); return true; }
        if (page >= DISPLAY && page <= PADBTNS) { feed_settings(edge); return true; }
        if (page == LAUNCHER) { if (edge & (BTN_CIRCLE | BTN_CROSS)) back_to_main(LAUNCHER); return true; }
        const std::vector<Page> mr = main_rows();
        const int rows = page == MAIN ? (int)mr.size() : 0;
        if (rows && (edge & BTN_DOWN)) { sel = (sel + 1) % rows; dirty = true; }
        if (rows && (edge & BTN_UP)) { sel = (sel + rows - 1) % rows; dirty = true; }
        if (edge & BTN_CROSS) {
            if (page == MAIN) {
                const Page t = mr[(size_t)sel];
                if (t == ONLINE) enter_online();
                else if (t == CLOSED) close();
                else enter_settings(t);
            }
        }
        if (edge & BTN_CIRCLE) {
            if (page == ONLINE) back_to_main(ONLINE);
            else close();
        }
        return true;
    }
    // The main page's rows, top to bottom; CLOSED stands for "Close". The settings pages exist only with an io.
    std::vector<Page> main_rows() const
    {
        std::vector<Page> r;
        if (io) { r.push_back(DISPLAY); r.push_back(GRAPHICS); r.push_back(CONTROLS); }
        r.push_back(ONLINE);
        if (io) r.push_back(LAUNCHER);
        r.push_back(CLOSED);
        return r;
    }
    void back_to_main(Page from)
    {
        page = MAIN; dirty = true; sel = 0;
        const std::vector<Page> mr = main_rows();
        for (size_t i = 0; i < mr.size(); i++) if (mr[i] == from) sel = (int)i;
    }
    void close() { page = CLOSED; sel = 0; editing = false; capturing = false; wait_release = true; dirty = true; }
    bool take_dirty() { const bool d = dirty; dirty = false; return d; }
    bool poll_connection_status(uint64_t now_ms)
    {
        if (page != ONLINE || !be) return false;
        if (connection_sampled && now_ms >= connection_sample_ms && now_ms - connection_sample_ms < 250) return false;
        connection_sample_ms = now_ms;
        connection_sampled = true;
        JspOnlineStatus next = jpon::empty();
        const bool have = be->connection_status(next);
        const auto before = jpon::describe(connection_observed), after = jpon::describe(next);
        const bool changed = have != connection_have || (have && (before.title != after.title || before.detail != after.detail || before.tone != after.tone));
        connection_have = have;
        connection_observed = next;
        if (changed) dirty = true;
        return changed;
    }


    void enter_online()
    {
        connection_have = connection_sampled = false;
        page = ONLINE; dirty = true; osel = 0; editing = false; edited = false; msg.clear(); msg_err = false;
        net_on = false; net_note.clear();
        if (be) { std::string note; const bool ok = be->begin(note); msg = note; msg_err = !ok; if (ok && io) { net_load(); if (!net_note.empty()) { msg = net_note; msg_err = true; } } }
    }
    // ENHANCEMENT (in-game netcode option): reads the saved Netcode (play_settings.txt + the backend's method) and what this process started with. A saved pair that is
    // both on (hand edit) shows as Off: the page never picks for the player, the game refuses it at start.
    std::string online_status(bool* restart) const
    {
        return jpno::status_line(nc_running, nc_saved, restart, nc_mods_off, run_method, saved_method,
                                 nc_saved == jpno::NC_ROLLBACK && saved_direct, run_sig, saved_sig);
    }
    void net_load()
    {
        std::string text, note;
        if (!io->load(text, note)) { net_note = note; return; }
        const jps::File f = jps::File::parse(text);
        rb = jpno::Rollback(); rb.load(f);
        nc_saved = jpno::saved_netcode(f, be->method());
        direct_pref = saved_direct = nc_saved == jpno::NC_ROLLBACK && !rb.repsn;
        saved_method = be->method() ? be->method() : 1;
        run_method = be->running_method();
        nc_running = jpno::running_netcode([&](const char* k) { return io->active(k); });
        nc_mods_off = jpno::flag_on(io->active(jpno::kKeyModsOff));
        run_sig = jpno::running_rb_signature([&](const char* k) { return io->active(k); }); saved_sig = rb.signature();
        nc = nc_saved == jpno::NC_CONFLICT ? jpno::NC_OFF : nc_saved;
        if (nc_saved == jpno::NC_CONFLICT) {
            be->set_method(0);
            net_note = std::string("Rollback and ") + jpno::name_of(jpno::NC_DELAY, saved_method)
                     + " were both on in the saved files; shown as Off. Pick one and save.";
        }
        else if (rb.both_set) net_note = "Both a Rollback host port and a join address are saved; saving keeps only the one for the chosen role.";
        net_on = true; osel = 0;
    }
    // the rows of the Online page, top to bottom (row ids of jpno): the older page without net_on
    std::vector<int> orows() const
    {
        if (net_on) return jpno::rows_for(nc, rb.host, rb.repsn, be->method(), rb.role_auto);
        return std::vector<int>{ ROW_METHOD, ROW_SERVER, ROW_NAME, ROW_PRESET, ROW_SAVE, ROW_DISCARD };
    }
    int orow() const { const std::vector<int> r = orows(); return r[(size_t)(osel < (int)r.size() ? osel : 0)]; }
    bool field_is_text_row(int row) const { return field_is_text(row) || (net_on && (row == jpno::R_PORT || row == jpno::R_JOIN || row == jpno::R_CODE)); }
    const char* field_charset(int f) const
    {
        if (net_on && f == jpno::R_PORT) return "0123456789";
        if (net_on && f == jpno::R_JOIN) return "0123456789.:";
        if (net_on && f == jpno::R_CODE) return "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";   // the match code: 4 to 8 letters or digits, upper case
        return be->charset(f);
    }
    size_t field_max_len(int f) const
    {
        if (net_on && f == jpno::R_PORT) return 5;
        if (net_on && f == jpno::R_JOIN) return 21;   // 255.255.255.255:65535
        if (net_on && f == jpno::R_CODE) return 8;
        return be->max_len(f);
    }
    // The one choice: Off / re-psn (standard) / re-psn (rollback) / Community (jpno::Mode). Only one is on: the backend's method follows (re-psn (standard) = method 1,
    // Community = method 2; Off and Rollback switch it off).
    void net_choose(int mode)
    {
        const int before = nc;
        nc = mode == jpno::M_NATIVE ? jpno::NC_NATIVE : mode == jpno::M_REPSN_RB ? jpno::NC_ROLLBACK : (mode == jpno::M_REPSN || mode == jpno::M_COMMUNITY) ? jpno::NC_DELAY : jpno::NC_OFF;
        be->set_method((mode == jpno::M_REPSN || mode == jpno::M_NATIVE) ? 1 : mode == jpno::M_COMMUNITY ? 2 : 0);
        if (nc == jpno::NC_ROLLBACK && before != jpno::NC_ROLLBACK) rb.repsn = !direct_pref;   // re-psn unless the direct IP connection was chosen before
        osel = 0; edited = true; msg = "Not saved yet."; msg_err = false; dirty = true;
        events.push_back(std::string("online edit: choice ") + jpno::name_of(nc, be->method(), !rb.repsn));
    }
    int mode() const { return jpno::mode_of(nc, be->method()); }
    // Save: the method that goes OFF is written first, so a failed or interrupted save never leaves both on.
    bool net_save(std::string& m)
    {
        const bool rbon = nc == jpno::NC_ROLLBACK, native = nc == jpno::NC_NATIVE;
        std::string why = rbon ? rb.check() : be->check();
        if (why.empty() && rbon && rb.repsn && (be->get_text(ROW_SERVER).empty() || be->get_text(ROW_NAME).empty()))
            why = "re-psn (rollback) needs the Server and Online name above (3 to 16 of A-Z a-z 0-9 _ -, starting with a letter).";
        if (!why.empty()) { m = why; return false; }
        auto write_file = [&](std::string& err) {
            std::string text, note;
            if (!io->load(text, note)) { err = note; return false; }
            jps::File f = jps::File::parse(text);
            // off writes JSP_ROLLBACK=0 when the line exists or the environment holds one (a file line wins over it). After a refused start resolve_env has set the process variable to 0 and
            // JSP_NET_REFUSED=1, so the original value is unknown: write the 0 then too, or the next start would inherit the old non-zero value and be refused again.
            if (native) rb.store_native(f);   // NC_NATIVE: JSP_ROLLBACK=1 without a role key
            else rb.store(f, rbon, jpno::flag_on(io->active(jpno::kKeyOn)) || jpno::flag_on(io->active(jpno::kKeyRefused)));
            return io->save(f.text(), err);
        };
        std::string err;
        if (native) {   // both on: the online settings first, then the rollback line
            if (!be->save(m)) return false;
            if (!write_file(err)) { m = "The online settings were saved, but the rollback line could not be: " + err; return false; }
        } else if (rbon) {
            if (!be->save(m)) return false;
            if (!write_file(err)) { if (nc_saved == jpno::NC_DELAY) nc_saved = jpno::NC_OFF; m = "The game's own online was switched off, but Rollback could not be saved: " + err; return false; }   // edited stays set: the choice is not saved
        } else {
            if (!write_file(err)) { m = err; return false; }
            if (!be->save(m)) { if (nc_saved == jpno::NC_ROLLBACK) nc_saved = jpno::NC_OFF; m = "Rollback was switched off, but the online settings could not be saved: " + m; return false; }
        }
        nc_saved = nc;
        direct_pref = saved_direct = nc == jpno::NC_ROLLBACK && !rb.repsn;
        saved_method = be->method() ? be->method() : 1;
        saved_sig = rb.signature();
        m = native ? "Saved. On the next start, choose Online Battle > Friendly Match for rollback."
          : nc == jpno::NC_ROLLBACK ? (rb.repsn ? std::string("Saved. re-psn (rollback)") + (rb.role_auto ? "" : rb.host ? ", Host" : ", Join") + " applies on the next start."
                                                : std::string("Saved. Rollback (direct IP, ") + (rb.host ? "host, port " + rb.port : "join " + rb.join) + ") applies on the next start.")
          : nc == jpno::NC_DELAY ? std::string("Saved. ") + jpno::name_of(nc, be->method()) + " applies on the next start."
          : "Saved. Online play is off from the next start.";
        return true;
    }
    static bool field_is_text(int row) { return row == ROW_SERVER || row == ROW_NAME; }

    // The Online page with a backend. Circle leaves without saving (the working copy is read again on the next visit: nothing changed on disk).
    void feed_online(uint16_t edge)
    {
        if (editing) { feed_picker(edge); return; }
        const int n = (int)orows().size();
        if (osel >= n) osel = n - 1;
        if (edge & BTN_DOWN) { osel = (osel + 1) % n; dirty = true; }
        if (edge & BTN_UP) { osel = (osel + n - 1) % n; dirty = true; }
        const int row = orow();   // ENHANCEMENT (in-game netcode option): the older page is the identity (row == osel)
        if ((edge & (BTN_LEFT | BTN_RIGHT)) && net_on && row == jpno::R_NETCODE) net_choose((mode() + ((edge & BTN_LEFT) ? jpno::kModes - 1 : 1)) % jpno::kModes);
        else if ((edge & (BTN_LEFT | BTN_RIGHT)) && net_on && (row == jpno::R_CONN || row == jpno::R_RELAY)) {   // ENHANCEMENT (rollback over re-psn)
            if (row == jpno::R_CONN) { rb.repsn = !rb.repsn; direct_pref = !rb.repsn; if (osel >= (int)orows().size()) osel = (int)orows().size() - 1; }
            else rb.relay = !rb.relay;
            edited = true; msg = row == jpno::R_RELAY ? "Relay: used only if a direct path fails and the server offers it." : "Not saved yet."; msg_err = false; dirty = true;
            events.push_back(std::string("online edit: rollback ") + (row == jpno::R_CONN ? (rb.repsn ? "connection re-psn" : "connection direct IP (advanced)") : (rb.relay ? "relay allowed" : "relay not allowed")));
        }
        else if ((edge & (BTN_LEFT | BTN_RIGHT)) && net_on && row == jpno::R_ROLE) {
            // over re-psn the row only shows an advanced override: Auto -> Host -> Join -> Auto; direct IP: Host / Join
            if (rb.repsn) { if (rb.role_auto) { rb.role_auto = false; rb.host = true; } else if (rb.host) rb.host = false; else { rb.role_auto = true; rb.host = true; } }
            else rb.host = !rb.host;
            if (osel >= (int)orows().size()) osel = (int)orows().size() - 1;
            edited = true; msg = "Not saved yet."; msg_err = false; dirty = true;
            events.push_back(std::string("online edit: rollback role ") + (rb.repsn && rb.role_auto ? "auto" : rb.host ? "host" : "join"));
        }
        else if ((edge & (BTN_LEFT | BTN_RIGHT)) && (row == ROW_METHOD || row == ROW_PRESET)) {
            be->cycle(row, (edge & BTN_LEFT) ? -1 : 1);   // (the method is the one choice's row: with net_on only the preset row reaches this)
            edited = true; msg = "Not saved yet."; msg_err = false; dirty = true;
            events.push_back("online edit: " + be->row_text(row));
        }
        if (edge & BTN_CROSS) {
            if (field_is_text_row(row)) {
                editing = true; ef = row; cpos = 0; dirty = true;
                buf = net_on && row == jpno::R_PORT ? rb.port : net_on && row == jpno::R_JOIN ? rb.join : net_on && row == jpno::R_CODE ? rb.code : be->get_text(row);
                cpos = buf.size();
                events.push_back(std::string("online edit: picker opened on ") + field_name(row));
            } else if (row == ROW_SAVE) {
                std::string m; const bool ok = net_on ? net_save(m) : be->save(m);
                msg = m; msg_err = !ok; if (ok) edited = false; dirty = true;
                events.push_back(std::string("online edit: save ") + (ok ? "ok: " : "refused: ") + m);
            } else if (row == ROW_DISCARD) {
                std::string note; const bool ok = be->begin(note);
                net_on = false; net_note.clear(); if (ok && io) net_load();
                edited = false; msg = ok ? "Changes discarded." : note; msg_err = !ok;
                if (!net_note.empty()) { msg = net_note; msg_err = true; }
                dirty = true;
                events.push_back("online edit: discarded");
            }
        }
        if (edge & BTN_CIRCLE) {
            events.push_back(std::string("online edit: back") + (edited ? " (unsaved changes dropped)" : ""));
            back_to_main(ONLINE);
        }
    }
    static const char* field_name(int row)
    {
        return row == ROW_SERVER ? "server" : row == jpno::R_PORT ? "rollback port" : row == jpno::R_JOIN ? "rollback host address" : row == jpno::R_CODE ? "match code" : "name";
    }

    // ---- the settings pages ----
    void enter_settings(Page t)
    {
        std::string text, note;
        if (!io->load(text, note)) { smsg = note; smsg_err = true; dirty = true; return; }   // stays on the main page
        model.f = jps::File::parse(text);
        smsg.clear(); smsg_err = false;
        open_settings(t, 0);
        events.push_back(std::string("settings: page ") + page_name(t) + " (" + io->path() + ")");
    }
    static const char* page_name(Page p)
    {
        return p == DISPLAY ? "Display" : p == GRAPHICS ? "Graphics" : p == CONTROLS ? "Controls" : p == KEYS ? "Keys" : p == PADBTNS ? "Controller buttons" : "";
    }
    void open_settings(Page t, int sel_row)
    {
        page = t; ssel = sel_row; stop = 0; capturing = false; dirty = true;
        rebuild_rows();
    }
    // the rows of the open page (rebuilt after every change: the Controls page shows the key or the controller list by the input mode)
    void rebuild_rows()
    {
        srows.clear();
        auto items = [&](jps::Group g) { for (int i = 0; i < jps::k_item_count; i++) if (jps::k_items[i].group == g) srows.push_back({ SRow::ITEM, i, CLOSED }); };
        if (page == DISPLAY) { items(jps::G_DISPLAY); srows.push_back({ SRow::RESET, 0, CLOSED }); }
        else if (page == GRAPHICS) { items(jps::G_GRAPHICS); srows.push_back({ SRow::RESET, 0, CLOSED }); }
        else if (page == CONTROLS) {
            items(jps::G_CONTROLS);
            if (model.mode() == 2) srows.push_back({ SRow::OPEN, 0, PADBTNS }); else srows.push_back({ SRow::OPEN, 0, KEYS });
            srows.push_back({ SRow::JOIN, 0, CLOSED });   // ENHANCEMENT: choose the player order by pressing a button on each pad
        }
        else if (page == KEYS) { for (int a = 0; a < jps::k_action_count; a++) srows.push_back({ SRow::KEY, a, CLOSED }); srows.push_back({ SRow::RESET, 0, CLOSED }); }
        else if (page == PADBTNS) { for (int b = 0; b < jps::k_padbtn_count; b++) srows.push_back({ SRow::PADBTN, b, CLOSED }); srows.push_back({ SRow::RESET, 0, CLOSED }); }
        const int n = (int)srows.size();
        if (ssel >= n) ssel = n - 1;
        if (ssel < 0) ssel = 0;
        if (ssel < stop) stop = ssel;
        if (ssel >= stop + kSettingsVisible) stop = ssel - kSettingsVisible + 1;
    }
    // the text of the right-hand column and whether the running game still has the old value
    std::string row_label(const SRow& r) const
    {
        switch (r.kind) {
        case SRow::ITEM: return jps::k_items[r.idx].label;
        case SRow::KEY: return jps::k_actions[r.idx].label;
        case SRow::PADBTN: return jps::k_padbtns[r.idx].label;
        case SRow::OPEN: return r.target == KEYS ? "Keys" : "Controller buttons";
        case SRow::JOIN: return "Player order";
        default: return "Reset this page to the defaults";
        }
    }
    std::string row_value(const SRow& r) const
    {
        switch (r.kind) {
        case SRow::ITEM: return model.saved_text(jps::k_items[r.idx]);
        case SRow::KEY: return capturing && r.idx == srows[(size_t)ssel].idx ? (model.mode() == 1 ? "key or mouse button..." : "press a key...") : jps::key_name(model.key_vk(r.idx));
        case SRow::PADBTN: return jps::k_xbutton_names[model.pad_btn(r.idx)];
        case SRow::OPEN: return "Cross: open";
        case SRow::JOIN: return "Cross: start";
        default: return "";
        }
    }
    bool row_pending(const SRow& r) const
    {
        if (r.kind != SRow::ITEM) return false;
        return model.pending(jps::k_items[r.idx], [&](const char* k) { return io->active(k); });
    }
    // Saves the file after a change; a live item is also handed to its reader. what: for the log.
    void commit(const std::string& what, const jps::Item* live_item)
    {
        std::string err;
        if (!io->save(model.f.text(), err)) { smsg = err; smsg_err = true; events.push_back("settings: NOT saved: " + err); dirty = true; return; }
        smsg_err = false; dirty = true;
        bool live = false;
        if (live_item && live_item->apply == jps::LIVE) {
            std::string v; model.f.get(live_item->key, v);
            io->live(live_item->key, v.c_str());
            live = true;
        }
        if (live) smsg = "Saved. Applied now.";
        else if (live_item || what.compare(0, 4, "key ") == 0 || what.compare(0, 7, "button ") == 0) smsg = "Saved. Applies on the next start.";
        else smsg = "Saved.";
        events.push_back("settings: " + what + (live ? " (saved, applied now)" : " (saved)"));
    }
    void feed_settings(uint16_t edge)
    {
        if (capturing) { feed_capture(edge); return; }
        const int n = (int)srows.size();
        if (edge & BTN_DOWN) { ssel = (ssel + 1) % n; dirty = true; }
        if (edge & BTN_UP) { ssel = (ssel + n - 1) % n; dirty = true; }
        if (edge & (BTN_DOWN | BTN_UP)) {
            if (ssel < stop) stop = ssel;
            if (ssel >= stop + kSettingsVisible) stop = ssel - kSettingsVisible + 1;
            if (ssel == 0) stop = 0;
        }
        const SRow r = srows[(size_t)ssel];
        const int dir = (edge & BTN_LEFT) ? -1 : (edge & BTN_RIGHT) ? 1 : (edge & BTN_CROSS) ? 1 : 0;
        if (dir && r.kind == SRow::ITEM) {
            const jps::Item& it = jps::k_items[r.idx];
            model.cycle(it, dir);
            rebuild_rows();
            commit(std::string(it.key) + "=" + it.values[model.saved_index(it) < 0 ? 0 : model.saved_index(it)], &it);
        } else if (dir && r.kind == SRow::PADBTN) {
            const int cur = model.pad_btn(r.idx);
            model.set_pad_btn(r.idx, ((cur + dir) % jps::k_xbutton_count + jps::k_xbutton_count) % jps::k_xbutton_count);
            commit(std::string("button ") + jps::k_padbtns[r.idx].id + " = " + jps::k_xbuttons[model.pad_btn(r.idx)], nullptr);
        } else if ((edge & BTN_CROSS) && r.kind == SRow::KEY) {
            capturing = true; cap_armed = false; smsg.clear(); smsg_err = false; dirty = true;
        } else if ((edge & BTN_CROSS) && r.kind == SRow::OPEN) {
            open_settings(r.target, 0);
        } else if ((edge & BTN_CROSS) && r.kind == SRow::JOIN) {
            // ENHANCEMENT: every device's first press takes the next player; the page closes, the join text shows on the game screen
            io->pad_join();
            events.push_back("settings: player order join started");
            close();
            return;
        } else if ((edge & BTN_CROSS) && r.kind == SRow::RESET) {
            if (page == DISPLAY) model.reset_group(jps::G_DISPLAY);
            else if (page == GRAPHICS) model.reset_group(jps::G_GRAPHICS);
            else if (page == KEYS) model.reset_keys();
            else if (page == PADBTNS) model.reset_pad_btns();
            // live items go back to the defaults at once
            if (page == GRAPHICS) {
                for (int i = 0; i < jps::k_item_count; i++)
                    if (jps::k_items[i].group == jps::G_GRAPHICS && jps::k_items[i].apply == jps::LIVE) {
                        std::string v; model.f.get(jps::k_items[i].key, v); io->live(jps::k_items[i].key, v.c_str());
                    }
            }
            commit(std::string("reset ") + page_name(page), nullptr);
            if (!smsg_err) smsg = page == GRAPHICS ? "Defaults saved. Some apply on the next start." : "Defaults saved. Applies on the next start.";
        }
        if (edge & BTN_CIRCLE) {
            if (page == KEYS || page == PADBTNS) { open_settings(CONTROLS, 0); for (int i = 0; i < (int)srows.size(); i++) if (srows[(size_t)i].kind == SRow::OPEN) { ssel = i; } rebuild_rows(); }
            else back_to_main(page);
            events.push_back("settings: back");
        }
    }
    // Waiting for a key on a KEYS row. Esc, or Circle on a controller, cancels; F11 stays the fullscreen key.
    void feed_capture(uint16_t edge)
    {
        const int vk = io->poll_vk(model.mode() == 1);
        if (!cap_armed) { if (vk == 0) cap_armed = true; return; }   // a key still held from the press that started the capture is not a binding
        if (vk == 0) {
            if (edge & BTN_CIRCLE) { capturing = false; smsg = "Cancelled."; smsg_err = false; dirty = true; }
            return;
        }
        if (vk == 0x1B) { capturing = false; smsg = "Cancelled."; smsg_err = false; dirty = true; return; }
        if (vk == 0x7A) { smsg = "F11 is kept for fullscreen."; smsg_err = true; dirty = true; return; }
#ifdef JSP_ROLLBACK_BUILD
        { const char* rb = std::getenv("JSP_ROLLBACK"); if (vk == 0x78 && rb && *rb && rb[0] != '0') { smsg = "F9 is kept for the rollback cancel key."; smsg_err = true; dirty = true; return; } }
#endif
        const SRow r = srows[(size_t)ssel];
        model.set_key(r.idx, vk);
        capturing = false;
        commit(std::string("key ") + jps::k_actions[r.idx].id + " = " + jps::vk_text(vk), nullptr);
    }

    // The character picker: Up/Down the letter at the position (L1/R1 by ten), Left/Right the position, Square deletes,
    // Cross accepts, Circle cancels. The empty slot after the last letter takes the first letter on Up/Down: that appends.
    void feed_picker(uint16_t edge)
    {
        const char* cs = field_charset(ef); const int n = (int)std::strlen(cs);
        const size_t mx = field_max_len(ef);
        int step = 0;
        if (edge & BTN_DOWN) step += 1;
        if (edge & BTN_UP) step -= 1;
        if (edge & BTN_R1) step += 10;
        if (edge & BTN_L1) step -= 10;
        if (step && n > 0) {
            if (cpos >= buf.size()) {
                if (buf.size() < mx) { buf.push_back(cs[step > 0 ? 0 : n - 1]); }
            } else {
                const char* at = std::strchr(cs, buf[cpos]);
                int i = at ? (int)(at - cs) : 0;
                i = ((i + step) % n + n) % n;
                buf[cpos] = cs[i];
            }
            dirty = true;
        }
        if ((edge & BTN_LEFT) && cpos > 0) { cpos--; dirty = true; }
        if ((edge & BTN_RIGHT) && cpos < buf.size()) { cpos++; dirty = true; }
        if ((edge & BTN_SQUARE) && !buf.empty()) {
            if (cpos >= buf.size()) { buf.pop_back(); cpos = buf.size(); }
            else buf.erase(cpos, 1);
            dirty = true;
        }
        if (edge & BTN_CROSS) {
            std::string hint;
            if (net_on && ef == jpno::R_PORT) { rb.port = buf; if (!buf.empty() && !jpno::valid_port(buf)) hint = "Not a valid port: a number from 1 to 65535."; }
            else if (net_on && ef == jpno::R_CODE) { rb.code = buf; std::string c; if (!buf.empty() && !jpno::normalize_code(buf, &c)) hint = "Match code: 4 to 8 letters or digits, or empty for none."; }
            else if (net_on && ef == jpno::R_JOIN) { rb.join = buf; if (!buf.empty() && !jpno::valid_join(buf)) hint = "Not a valid address: IP:port, for example 192.0.2.10:36200."; }
            else hint = be->set_text(ef, buf);
            editing = false; edited = true; dirty = true;
            msg = hint.empty() ? "Not saved yet." : hint; msg_err = !hint.empty();
            if (net_on && ef == jpno::R_CODE) events.push_back(std::string("online edit: match code ") + (buf.empty() ? "cleared" : "set") + (hint.empty() ? " (valid)" : " (not valid yet)"));   // the code is never logged
            else events.push_back(std::string("online edit: ") + field_name(ef) + " set to '" + buf + "'" + (hint.empty() ? " (valid)" : " (not valid yet)"));
        } else if (edge & BTN_CIRCLE) {
            editing = false; dirty = true;
            events.push_back("online edit: picker cancelled");
        }
    }
};

// The in-game pages and the synthetic host preview use the same layout. The sink
// measures and draws the overlay font in the game's 1280x720 coordinate space.
namespace menu_view {
constexpr uint32_t white = 0xFFFFFFFF, gold = JSP_RGBA(255, 202, 76, 255);
constexpr uint32_t muted = JSP_RGBA(190, 199, 217, 255), error = JSP_RGBA(255, 158, 130, 255);
constexpr uint32_t rule = JSP_RGBA(64, 74, 96, 255);
constexpr uint32_t caret_ink = JSP_RGBA(12, 16, 24, 255);
constexpr int left = 212, right = 1068, text_x = 228, width = 824;
constexpr int rows_y = 165, pitch = 34, detail_y = 454, footer_y = 619;

const char* title(Page p)
{
    switch (p) {
    case MAIN: return "Port Settings";
    case ONLINE: return "Online";
    case LAUNCHER: return "Mods & Launcher";
    case CLOSED: return "Close";
    default: return Menu::page_name(p);
    }
}

const char* hub_detail(Page p)
{
    switch (p) {
    case DISPLAY: return "Choose your render resolution, window size and display mode.";
    case GRAPHICS: return "Adjust shadows, draw distance, edge smoothing and the fight HUD.";
    case CONTROLS: return "Choose an input mode, change bindings or set the order of your controllers.";
    case ONLINE: return "Choose how you connect and see which settings will apply on the next start.";
    case LAUNCHER: return "Find Mods and settings that the launcher applies before the game starts.";
    default: return "Return to the game's Options menu.";
    }
}

const char* online_detail(int row)
{
    switch (row) {
    case ROW_METHOD: case jpno::R_NETCODE: return "Choose the online method for the next game session.";
    case ROW_SERVER: return "Set the server address. A port can follow the hostname.";
    case ROW_NAME: return "Set the name other players will see.";
    case ROW_PRESET: return "Choose a saved Community server preset.";
    case jpno::R_CONN: return "Choose matchmaking via re-psn or a direct IP connection.";
    case jpno::R_ROLE: return "Choose whether to host or join the match.";
    case jpno::R_CODE: return "Use the same match code as your opponent to find each other.";
    case jpno::R_RELAY: return "Allow a relay when a direct peer connection is unavailable.";
    case jpno::R_PORT: return "Choose the port your opponent will connect to.";
    case jpno::R_JOIN: return "Enter the host's IP address and port.";
    case ROW_SAVE: return "Save your choices for the next game session.";
    default: return "Reload the saved choices.";
    }
}

struct Note { std::string text; uint32_t color; };

template<class Sink>
struct View {
    Sink& out;

    std::string clip(const std::string& text, float scale, float max, const char* suffix = "...") const
    {
        if (out.width(text, scale) <= max) return text;
        size_t end = text.size();
        while (end && out.width(text.substr(0, end) + suffix, scale) > max) --end;
        return text.substr(0, end) + suffix;
    }

    std::vector<std::string> wrap(const std::string& text, float scale, float max) const
    {
        std::vector<std::string> lines;
        size_t start = text.find_first_not_of(" \r\n\t");
        while (start != std::string::npos) {
            const size_t newline = text.find_first_of("\r\n", start);
            const size_t limit = newline == std::string::npos ? text.size() : newline;
            size_t end = start + 1;
            while (end < limit && out.width(text.substr(start, end + 1 - start), scale) <= max) ++end;
            if (end < limit) {
                const size_t space = text.rfind(' ', end);
                if (space != std::string::npos && space > start + (end - start) / 2) end = space;
            }
            lines.push_back(text.substr(start, end - start));
            start = text.find_first_not_of(" \r\n\t", end);
        }
        return lines;
    }

    void frame(Page page)
    {
        out.rect(0, 0, 1280, 720, JSP_RGBA(0, 0, 0, 143));
        out.rect(184, 66, 912, 602, JSP_RGBA(18, 23, 37, 248));
        out.rect(184, 66, 912, 5, gold);
        out.rect(184, 666, 912, 2, rule);
        const char* trail = page == MAIN ? "Options" : page == KEYS || page == PADBTNS ? "Port Settings  >  Controls" : "Port Settings";
        out.text(text_x, 85, trail, 0.71f, gold, 0);
        out.text(text_x, 111, title(page), 1.46f, white, 0);
        for (const int y : { 152, 446, 606 }) out.rect(left, y, right - left, 1, rule);
    }

    void row(int slot, bool selected)
    {
        const int y = rows_y + slot * pitch;
        out.rect(left, y, right - left, pitch - 2, selected ? JSP_RGBA(246, 244, 232, 255) : JSP_RGBA(28, 35, 52, 255));
        if (selected) out.rect(left, y, 5, pitch - 2, gold);
    }

    void row_text(int slot, int x, int max, const std::string& text, bool selected, float scale = 0.9f)
    {
        out.text(x, rows_y + slot * pitch + 6, clip(text, scale, max), scale, white, selected ? 1 : 0);
    }

    void scroll(int top, int count, int total)
    {
        if (total <= count) return;
        const std::string range = "Rows " + std::to_string(top + 1) + "-" + std::to_string(top + count) + " of " + std::to_string(total);
        auto right_text = [&](int y, const std::string& s, uint32_t color) {
            out.text(right - 15 - out.width(s, 0.69f), y, s, 0.69f, color, 0);
        };
        right_text(102, range, muted);
        right_text(123, top == 0 ? "More below" : top + count == total ? "More above" : "More above and below", gold);
    }

    void context(const std::vector<Note>& notes)
    {
        // Reserve the entire block before drawing. Messages are the first notes so
        // even unexpectedly large input cannot push them out behind the controls.
        std::vector<Note> lines;
        float scale = 0.74f;
        int step = 21, capacity = 7;
        for (const int size : { 74, 64, 57 }) {
            scale = size / 100.0f;
            step = size == 74 ? 21 : size == 64 ? 18 : 16;
            capacity = (606 - detail_y) / step;
            lines.clear();
            for (const Note& note : notes)
                for (const std::string& line : wrap(note.text, scale, width)) lines.push_back({ line, note.color });
            if ((int)lines.size() <= capacity) break;
        }
        if ((int)lines.size() > capacity) {
            lines.resize((size_t)capacity);
            lines.back() = { "More text was shortened to fit this panel.", muted };
        }
        for (size_t i = 0; i < lines.size(); ++i)
            out.text(text_x, detail_y + (int)i * step, lines[i].text, scale, lines[i].color, 0);
    }

    void footer(const char* controls, const char* hint = "")
    {
        out.text(text_x, footer_y, clip(controls, 0.71f, width), 0.71f, white, 0);
        if (*hint) out.text(text_x, footer_y + 22, clip(hint, 0.65f, width), 0.65f, muted, 0);
    }

    void hub(const Menu& m)
    {
        const auto pages = m.main_rows();
        for (int i = 0; i < (int)pages.size(); ++i) {
            const bool on = i == m.sel;
            row(i, on);
            row_text(i, text_x, width, title(pages[(size_t)i]), on, 0.98f);
        }
        const Page selected = pages[(size_t)m.sel];
        std::vector<Note> notes{ { m.smsg, m.smsg_err ? error : gold }, { hub_detail(selected), white } };
        notes.push_back({ "Choose a category. Changes marked * apply after restarting the game.", muted });
        context(notes);
        footer(selected == CLOSED ? "Up / Down  Select     Cross  Close     Circle  Close"
                                  : "Up / Down  Select     Cross  Open     Circle  Close");
    }

    void settings(const Menu& m)
    {
        const int total = (int)m.srows.size();
        const int count = std::min(kSettingsVisible, total);
        const int top = std::min(m.stop, total - count);
        for (int slot = 0; slot < count; ++slot) {
            const SRow& r = m.srows[(size_t)(top + slot)];
            const bool on = top + slot == m.ssel;
            row(slot, on);
            row_text(slot, text_x, 435, m.row_label(r), on);
            std::string value = m.row_value(r);
            if (m.row_pending(r)) value += " *";
            if (!value.empty()) row_text(slot, 688, 364, value, on, 0.84f);
        }
        scroll(top, count, total);
        const SRow& selected = m.srows[(size_t)m.ssel];
        const char* action = "Left / Right  Change";
        const char* detail = "Changes are saved immediately. * applies on the next start.";
        switch (selected.kind) {
        case SRow::ITEM: detail = jps::k_items[selected.idx].apply == jps::LIVE
                                  ? "Changes take effect as soon as they are saved." : "Changes take effect the next time you start the game."; break;
        case SRow::KEY: action = "Cross  Change binding"; detail = "Assign a key for the selected action."; break;
        case SRow::PADBTN: detail = "Choose a controller button for the selected action."; break;
        case SRow::OPEN: action = "Cross  Open"; detail = "Open the bindings for the saved input mode."; break;
        case SRow::RESET: action = "Cross  Reset"; detail = "Restore the defaults on this page."; break;
        case SRow::JOIN: action = "Cross  Start"; detail = "Press a button on each controller in order, or hold Select + L3 + R3 for 2 seconds to start."; break;
        }
        if (m.capturing) {
            context({ { m.smsg, m.smsg_err ? error : gold },
                      { m.model.mode() == 1 ? "Press a key or mouse button." : "Press a key.", white } });
            footer("Esc or Circle  Cancel", "F11 stays the fullscreen key.");
        } else {
            context({ { m.smsg, m.smsg_err ? error : gold }, { detail, white } });
            const std::string controls = std::string("Up / Down  Select     ") + action + "     Circle  Back";
            footer(controls.c_str(), "* Saved for next start");
        }
    }

    void picker(const Menu& m, int slot, const std::string& label)
    {
        constexpr float scale = 0.9f;
        const size_t cursor = std::min(m.cpos, m.buf.size());
        size_t start = 0;
        while (start < cursor && out.width(label + "..." + m.buf.substr(start, cursor - start), scale) > width * 0.72f) ++start;
        const std::string lead = label + (start ? "..." : "");
        const std::string before = lead + m.buf.substr(start, cursor - start);
        const bool empty = cursor == m.buf.size();
        const std::string text = lead + m.buf.substr(start) + (empty ? " " : "");
        row_text(slot, text_x, width, text, true, scale);
        const float caret = empty ? 13.0f : out.width(m.buf.substr(cursor, 1), scale);
        out.rect(text_x + (int)out.width(before, scale), rows_y + slot * pitch + 27, std::max(3, (int)caret), 3, caret_ink);
    }

    void online(const Menu& m, const std::vector<std::string>& startup)
    {
        const auto ids = m.orows();
        const int total = (int)ids.size(), count = std::min(kSettingsVisible, total);
        const int cursor = std::clamp(m.osel, 0, total - 1);
        const int top = std::clamp(cursor - count / 2, 0, total - count);
        std::string long_value;
        for (int slot = 0; slot < count; ++slot) {
            const int i = top + slot, id = ids[(size_t)i];
            const bool on = i == cursor;
            row(slot, on);
            std::string text = id <= ROW_PRESET ? m.be->row_text(id)
                              : m.net_on ? jpno::row_text(id, m.nc, m.rb, m.be->method())
                              : id == ROW_SAVE ? "Save" : "Discard changes";
            if (on && m.editing && id == m.ef) {
                const size_t sep = text.find(": ");
                picker(m, slot, sep == std::string::npos ? std::string() : text.substr(0, sep + 2));
            } else {
                row_text(slot, text_x, width, text, on);
                if (on && out.width(text, 0.9f) > width) long_value = text;
            }
        }
        scroll(top, count, total);
        const int selected = ids[(size_t)cursor];
        std::vector<Note> notes{ { !m.msg.empty() ? m.msg : m.edited ? "Not saved yet." : "", m.msg_err ? error : gold },
                                 { online_detail(selected), white } };
        if (m.net_on) {
            bool restart = false;
            const std::string status = m.online_status(&restart);
            notes.push_back({ status, restart ? gold : muted });
            notes.push_back({ jpno::exclusion_line(m.nc), muted });
        } else if (!startup.empty()) {
            notes.push_back({ startup.front(), muted });
        }
        if (m.connection_have) {
            const auto connection = jpon::describe(m.connection_observed);
            const uint32_t color = connection.tone == jpon::Tone::Warning ? error : connection.tone == jpon::Tone::Good ? gold : white;
            notes.insert(notes.begin() + 1, { connection.title, color });
            notes.push_back({ connection.detail, muted });
        }
        notes.push_back({ long_value, white });
        context(notes);
        if (m.editing) {
            footer("Up / Down  Letter     Left / Right  Position     L1 / R1  Ten letters",
                   "Square  Delete     Cross  Accept     Circle  Cancel");
        } else {
            const char* action = m.field_is_text_row(selected) ? "Cross  Edit" : selected == ROW_SAVE ? "Cross  Save"
                               : selected == ROW_DISCARD ? "Cross  Discard" : "Left / Right  Change";
            const std::string controls = std::string("Up / Down  Select     ") + action + "     Circle  Back";
            footer(controls.c_str(), "Saved changes apply on the next start. Going back drops unsaved changes.");
        }
    }

    void launcher()
    {
        for (int i = 0; i <= jps::k_launcher_only_count; ++i) {
            const int y = rows_y + i * 38;
            out.rect(left, y, right - left, 36, JSP_RGBA(28, 35, 52, 255));
            const char* name = i == 0 ? "Mods" : jps::k_launcher_only[i - 1].what;
            const char* why = i == 0 ? "Manage and enable mod packages from the launcher's Mods tab." : jps::k_launcher_only[i - 1].why;
            out.text(text_x, y + 8, clip(name, 0.76f, 354), 0.76f, gold, 0);
            const auto lines = wrap(why, 0.60f, 451);
            for (int j = 0; j < (int)lines.size() && j < 2; ++j) {
                const std::string line = j == 1 && lines.size() > 2 ? clip(lines[1] + "...", 0.60f, 451) : lines[(size_t)j];
                out.text(601, y + 2 + j * 17, line, 0.60f, white, 0);
            }
        }
        context({ { "Open the launcher to manage these features before your next game session.", white } });
        footer("Cross or Circle  Back");
    }

    void readonly(const std::vector<std::string>& startup)
    {
        std::vector<std::string> lines;
        for (const auto& text : startup)
            for (auto& line : wrap(text, 0.82f, width)) lines.push_back(std::move(line));
        if (lines.empty()) lines.push_back("No online settings were read at start.");
        if (lines.size() > 11) { lines.resize(11); lines.back() = "Additional startup text was shortened."; }
        for (size_t i = 0; i < lines.size(); ++i)
            out.text(text_x, 172 + (int)i * 24, lines[i], 0.82f, white, 0);
        context({ { "Read only. Change the online settings in the launcher and start the game again.", white } });
        footer("Circle  Back");
    }
};

template<class Sink>
void draw(Sink& out, const Menu& m, const std::vector<std::string>& startup = {})
{
    if (m.page == CLOSED) return;
    View<Sink> view{out};
    view.frame(m.page);
    if (m.page == MAIN) view.hub(m);
    else if (m.page == LAUNCHER) view.launcher();
    else if (m.page != ONLINE) view.settings(m);
    else if (m.be) view.online(m, startup);
    else view.readonly(startup);
}

}  // namespace menu_view

}  // namespace jpm

#ifndef JSP_PORT_MENU_TEST
// The lifter's --insn-patches guard (lift_insn_patches.json): the lifted code reads this switch at 0x001E71C0 (see the top of the file).
// Defined always, so a lift made with the patch links even when the page is off (JSP_PORT_MENU=0).
extern "C" { int g_port_menu_six_rows = 0; }

#ifdef _WIN32
extern "C" {
__declspec(dllimport) short __stdcall GetAsyncKeyState(int vk);
__declspec(dllimport) void* __stdcall GetForegroundWindow(void);
__declspec(dllimport) unsigned long __stdcall GetWindowThreadProcessId(void* hwnd, unsigned long* pid);
__declspec(dllimport) unsigned long __stdcall GetCurrentProcessId(void);
}
#endif
namespace {

typedef void (*fn_t)(ppu_context*);

bool port_menu_env()
{
    static const bool on = jpm::port_menu_on(std::getenv("JSP_PORT_MENU"));
    return on;
}

std::mutex g_mu;
jpm::Menu g_menu;
// The page's backend: jsp_online.cpp's working copy (what the launcher's Online tab edits: repsn.ini, community.ini).
struct GameBackend : jpm::Backend {
    static std::string call(int (*f)(int, char*, int), int field)
    { char b[300] = ""; f(field, b, sizeof b); return b; }
    bool begin(std::string& note) override { char b[300] = ""; const int r = jsp_online_edit_begin(b, sizeof b); note = b; return r == 0; }
    std::string row_text(int f) override { return call(jsp_online_edit_row, f); }
    std::string get_text(int f) override { return call(jsp_online_edit_get, f); }
    std::string set_text(int f, const std::string& v) override { char b[300] = ""; return jsp_online_edit_set(f, v.c_str(), b, sizeof b) ? b : ""; }
    void cycle(int f, int d) override { jsp_online_edit_cycle(f, d); }
    bool save(std::string& m) override { char b[300] = ""; const int r = jsp_online_edit_save(b, sizeof b); m = b; return r == 1; }
    // letters first in lower case (names and hosts are mostly lower case), then capitals, digits, then the punctuation each field allows:
    // the reader's rules: host of A-Za-z0-9 . - _ and a :port; name of A-Za-z0-9 _ - (3..16, first a letter, checked at Save)
    const char* charset(int f) override { return f == jpm::ROW_SERVER ? "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_:"
                                                                      : "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-"; }
    size_t max_len(int f) override { return f == jpm::ROW_SERVER ? 260 : 16; }
    bool connection_status(JspOnlineStatus& status) override
    {
        status = jpon::empty();
        if (jsp_net_status_snapshot(&status, sizeof status)) return true;
        const int method = jsp_online_running_method();
        status.phase = static_cast<uint32_t>(method == 1 ? jpon::Phase::NativeEnabled : method == 0 ? jpon::Phase::Offline : jpon::Phase::Unavailable);
        return true;
    }
    int running_method() override { return jsp_online_running_method(); }
    int method() override { return jsp_online_edit_method(); }
    void set_method(int m) override { jsp_online_edit_set_method(m); }
    std::string check() override { char b[300] = ""; return jsp_online_edit_check(b, sizeof b) ? b : ""; }
};
GameBackend g_backend;

// The settings pages' OS side: jsp_settings.cpp (file, live values) and the keyboard (key capture).
struct GameSettingsIO : jpm::SettingsIO {
    void pad_join() override { pad_join_request(); }
    bool load(std::string& text, std::string& note) override { return jps::io_load(text, note); }
    bool save(const std::string& text, std::string& err) override { return jps::io_save(text, err); }
    const char* active(const char* key) override { return std::getenv(key); }
    void live(const char* key, const char* value) override { jsp_setting_set_live(key, value); }
    std::string path() override { return jps::io_path(); }
    // The lowest virtual-key code held down while the game window has the focus, 0 when none. Mouse buttons only when asked; the left and
    // right Shift, Ctrl and Alt are one key each (0x10-0x12, as the launcher binds them); Windows keys and the unassigned codes are skipped.
    int poll_vk(bool mouse) override
    {
#ifdef _WIN32
        unsigned long pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        if (pid != GetCurrentProcessId()) return 0;
        for (int vk = 1; vk < 255; vk++) {
            if (vk == 0x03 || vk == 0x07 || vk == 0x0A || vk == 0x0B || vk == 0x0E || vk == 0x0F) continue;
            if ((vk >= 0xA0 && vk <= 0xA5) || vk == 0x5B || vk == 0x5C || vk == 0x5D || vk == 0x5F || vk >= 0xE0) continue;
            if (vk <= 0x06 && !mouse) continue;
            if (GetAsyncKeyState(vk) & 0x8000) return vk;
        }
#else
        (void)mouse;
#endif
        return 0;
    }
};
GameSettingsIO g_settings_io;

uint32_t g_obj = 0;            // the main-menu object (r3 of @0x001E0830)
int g_last_panel = -1, g_last_open = -1, g_last_cursor = -1, g_last_top = -1;
bool g_drawn = false;
bool g_help = false;   // the help line for row 5 is up (overlay)

constexpr uint32_t k_build_options = 0x001E0830u;   // builds the Options panel (menu id 8), cursor struct at the main-menu object (cursor struct at +0xF4)
constexpr uint32_t k_panel_update = 0x001E7238u;    // main_menu2 panel per-frame update (vtable 0x87D910 +0x10); tail-calls 0x001E6E98 -> 0x001E6C68
constexpr uint32_t k_row_count_insn = 0x001E71C0u;  // `li r25,5` (0x3B200005) in the US 1.00 code; the EU build has other code here
constexpr uint32_t k_cursor = 0xF4u;                // Options cursor struct in the main-menu object (0xA4 is Online)
// Fields of the panel object (r3 of 0x001E7238 / 0x001E6C68, a different object from the main-menu object; re-notes-data-logo R5):
constexpr uint32_t k_panel_state = 0x38u, k_panel_id = 0x44u, k_panel_open = 0x4Cu;   // state 3 = active; id 8 = option; +0x4C mode != 0 = sub-menu mode

fn_t g_orig_build = nullptr, g_orig_update = nullptr;
uint32_t g_panel = 0;          // the panel object (r3 of 0x001E7238)
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);

// The guest objects can die under us (the confirm of an unhandled row leaves the main menu: the panel object then read id 0 / state 0,
// later whole garbage words). The first word of a live object is its vtable pointer; it
// is recorded when the object is first seen (on_build_options / on_panel_update) and a different value later means freed or reused.
// No fixed vtable address is assumed (0x87D910 / 0x87D600 are from the notes only).
uint32_t g_panel_vt = 0, g_obj_vt = 0;
bool g_dead_logged = false;

// Reads the open panel and the Options cursor; tells the label gate when the panel changes. Caller holds g_mu.
// "open" is state 3 (active) of the panel object whose id is 8 (the mode word at +0x4C reads 0 in every state, so it is not used).
void poll_panel(int* panel, int* open, int* row)
{
    *panel = 0; *open = 0; *row = -1;
    if (!g_obj || !g_panel) return;
    if (vm_read32(g_panel) != g_panel_vt || (g_obj_vt && vm_read32(g_obj) != g_obj_vt)) {
        if (!g_dead_logged) {
            std::fprintf(stderr, "[portmenu] panel/menu object gone (panel 0x%08X vtable 0x%08X was 0x%08X; menu 0x%08X vtable 0x%08X was 0x%08X) (flip %u)\n",
                         g_panel, vm_read32(g_panel), g_panel_vt, g_obj, vm_read32(g_obj), g_obj_vt, (unsigned)cellGcm_flip_request_count());
            g_dead_logged = true;
        }
        if (g_last_open) jsp_menu_label_panel(0);
        g_panel = 0; g_obj = 0;
        g_last_panel = -1; g_last_open = -1; g_last_cursor = -1; g_last_top = -1;
        return;
    }
    const int id = (int)vm_read32(g_panel + k_panel_id);
    const int st = (int)vm_read32(g_panel + k_panel_state);
    const int op = st == 3;   // ASSUMPTION: state 3 ("active", re-notes-data-logo R5) of the panel with id 8 = the Options list is up and takes input
    const int32_t cur = (int32_t)vm_read32(g_obj + k_cursor + 4), top = (int32_t)vm_read32(g_obj + k_cursor + 0xC);
    *panel = id; *open = op; *row = jpm::row_of(cur, top);
    if (id != g_last_panel || op != g_last_open || cur != g_last_cursor || top != g_last_top) {
        if (id != g_last_panel || op != g_last_open)
            jsp_menu_label_panel(op ? id : 0);   // label gate (patch 2518): the replaced label shows only on its own panel
        std::fprintf(stderr, "[portmenu] panel id=%d state=%d open=%d options cursor=%d top=%d total=%d (flip %u)\n", id, st, op, cur, top,
                     (int)vm_read32(g_obj + k_cursor), (unsigned)cellGcm_flip_request_count());
        g_last_panel = id; g_last_open = op; g_last_cursor = cur; g_last_top = top;
    }
}

// Production sink for the shared menu layout.
struct Overlay {
    void rect(int x, int y, int w, int h, uint32_t color) const
    { jsp_overlay_fill_rect(x, y, w, h, color); }
    void text(float x, int y, const std::string& text, float scale, uint32_t color, int style) const
    { jsp_overlay_text_style(x, (float)y, text.c_str(), scale, color, style); }
    float width(const std::string& text, float scale) const
    { return jsp_overlay_text_width(text.c_str(), scale); }
};

// ---- ENHANCEMENT (display, default on): the version line on the main menu's top list (jsp_version_line.h) ----
// "Top list up" = jpv::top_menu(): the main-menu object (r3 of 0x001E0830) is alive, and the panel's per-frame update
// (0x001E7238, vtable 0x87D910 +0x10) ran within the last two flips with panel state (+0x38) 0 = closed. ASSUMPTION: that update is
// called every frame also while the panel is closed (it is the panel's state machine, state 0 included; a scene that is gone stops
// calling it, which is what hides the line). The [version] log line prints every change with its inputs; a run decides.
jpv::Lines g_ver;                       // chosen once at start; empty = nothing is drawn
bool g_ver_vis = false;
bool g_ver_seen = false;                // the panel update has run at least once since the object was built
uint32_t g_ver_flip = 0;                // flip count at the last panel update
int g_ver_state = -1;                   // panel state at the last panel update

// ENHANCEMENT (display only, default on): the refused-rollback notice (jpno::notice_text, jsp_net_game.cpp jsp_net_notice) on the same top list as the version line.
// A refused start sets JSP_ROLLBACK=0 before main(), so the rollback module installs nothing and has no frame of its own to draw on; the top list is where the player
// picks Online Battle / Free Battle, and the overlay layer is this file's to share. Shown for jpno::kNoticeMs each time the top list comes up.
bool g_net_watch = false;               // set at install: JSP_ROLLBACK or JSP_NET_REFUSED is on, so a notice may exist (read lazily: jsp_net_game_install may decide later)
jpno::Notice g_note;                    // the text, read when the top list comes up
bool g_note_vis = false;
uint64_t g_note_since = 0;              // ms: the top list last came up, or the notice text last changed while it was up
unsigned g_note_gen = 0;                // jsp_net_notice_gen() at the last read
uint64_t mono_ms() { return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
bool top_items() { return g_ver.any() || g_net_watch; }

void draw_page();

// ENHANCEMENT (toolkit patch 7908): the player-order join. g_joining / g_join_next come from the toolkit's notifier (g_mu held).
bool g_joining = false;
int g_join_next = 0;   // the player (1-4) the next press takes

// Flow object S (@0x009535F0 -> +0x30 state) and the menu-mode global (0x0095653C +0x10 mode): the idle top list is state 4 (first time)
// or 7 (after a submenu) with mode 0 (jpv::flow_running).
constexpr uint32_t k_flow_ptr = 0x009535F0, k_mode_g = 0x0095653C;
int flow_state() { const uint32_t s = vm_read32(k_flow_ptr); return s ? (int)vm_read32(s + 0x30) : -1; }

// Caller holds g_mu. Re-evaluates visibility; redraws on a change (the page is closed whenever the top list is up).
void ver_refresh()
{
    if (!top_items()) return;
    const uint32_t now = (uint32_t)cellGcm_flip_request_count();
    const bool alive = g_obj && g_obj_vt && vm_read32(g_obj) == g_obj_vt;
    const int fs = flow_state(), mode = (int)vm_read32(k_mode_g + 0x10);
    const bool vis = jpv::top_menu(alive, g_ver_seen, now - g_ver_flip, g_ver_state, fs, mode) && g_menu.page == jpm::CLOSED;
    {   // every change of a rule input (not the flip count), so a run shows which one keeps the top list off
        static int l_alive = -2, l_state = -2, l_fs = -2, l_mode = -2, l_lines = 0;   // at most 1000 lines per run (log size only)
        if (((int)alive != l_alive || g_ver_state != l_state || fs != l_fs || mode != l_mode) && l_lines < 1000) {
            l_lines++;
            std::fprintf(stderr, "[version] inputs: alive %d, panel state %d, flow state %d, mode %d, page %d (flip %u)\n", (int)alive, g_ver_state, fs, mode, (int)g_menu.page, now);
            l_alive = (int)alive; l_state = g_ver_state; l_fs = fs; l_mode = mode;
        }
    }
    if (g_net_watch) {   // the notice: re-read and re-armed each time the top list comes up, dropped after jpno::kNoticeMs
        const uint64_t t = mono_ms();
        const unsigned gen = jsp_net_notice_gen();
        bool text_changed = false;
        if (vis && (!g_ver_vis || gen != g_note_gen)) {   // the top list came up, or the rendezvous text changed while it is up (re-armed for kNoticeMs)
            char l1[200] = "", l2[200] = "";
            const jpno::Notice n = jsp_net_notice(l1, sizeof l1, l2, sizeof l2) ? jpno::Notice{ l1, l2 } : jpno::Notice{};
            text_changed = n.line1 != g_note.line1 || n.line2 != g_note.line2;
            g_note = n; g_note_since = t; g_note_gen = gen;
        }
        const bool nv = jpno::notice_visible(g_note.any(), vis, g_note_since, t);
        if (nv && g_note_vis && text_changed && vis == g_ver_vis) { draw_page(); return; }   // only the text changed (the waiting time, or the failure)
        if (nv != g_note_vis) {
            g_note_vis = nv;
            if (nv) std::fprintf(stderr, "[net] notice on the top menu: %s / %s\n", g_note.line1.c_str(), g_note.line2.c_str());
            else std::fprintf(stderr, "[net] notice hidden after %llu ms (top list %s, flip %u)\n", (unsigned long long)(t - g_note_since), vis ? "up" : "gone", now);
            if (vis == g_ver_vis) { draw_page(); return; }   // only the notice changed
        }
    }
    if (vis == g_ver_vis) return;
    g_ver_vis = vis;
    std::fprintf(stderr, "[version] top menu %s: obj 0x%08X alive %d, panel state %d, flips since panel update %u, flow state %d, mode %d (flip %u)\n", vis ? "up, line drawn" : "gone, line cleared",
                 g_obj, (int)alive, g_ver_state, (unsigned)(now - g_ver_flip), fs, mode, now);
    draw_page();
}

void ver_draw()
{
    if (!g_ver.any() && !g_note_vis) { if (g_drawn) { jsp_overlay_close(); g_drawn = false; } return; }
    if (!jsp_overlay_begin(1280, 720)) return;
    const uint32_t white = JSP_RGBA(255, 255, 255, 255);
    const float w1 = g_ver.any() ? jsp_overlay_text_width(g_ver.line1.c_str(), jpv::kScale1) : 0;
    if (w1 > 0) jsp_overlay_text_style(jpv::kRight - w1, jpv::kY1, g_ver.line1.c_str(), jpv::kScale1, white, 0);
    if (g_note_vis) {   // a dark band at the top of the 1280x720 layout space, both lines scaled down to fit 1200 px; the game's colours (white text with black rim, gold rule)
        const float w = jsp_overlay_text_width(g_note.line1.c_str(), 1.0f), w2 = jsp_overlay_text_width(g_note.line2.c_str(), 1.0f);
        const float wm = w > w2 ? w : w2;
        const float sc = wm > 0 && wm * 0.75f > 1180.0f ? 1180.0f / wm : 0.75f;
        jsp_overlay_fill_rect(20, 20, 1240, 92, JSP_RGBA(16, 20, 40, 225));
        jsp_overlay_fill_rect(20, 108, 1240, 4, JSP_RGBA(240, 190, 30, 255));
        jsp_overlay_text_style(40, 34, g_note.line1.c_str(), sc, white, 0);
        jsp_overlay_text_style(40, 70, g_note.line2.c_str(), sc, white, 0);
    }
    jsp_overlay_end();
    g_drawn = true;
}

// "Press a button: player N", bottom centre of the 1280x720 layout space, in the menu's own text. The overlay layer is shared with the
// page and the version line: this is drawn from draw_page() and replaces them while it runs (the page cannot be open: its row closes it).
void join_draw()
{
    if (!jsp_overlay_begin(1280, 720)) return;
    char line[64];
    std::snprintf(line, sizeof line, "Press a button: player %d", g_join_next);
    const uint32_t white = JSP_RGBA(255, 255, 255, 255);
    const float w = jsp_overlay_text_width(line, 1.1f);
    const float x = w > 0 ? (1280.0f - w) / 2 : 400.0f;
    jsp_overlay_fill_rect((int)x - 30, 600, (int)(w > 0 ? w : 480) + 60, 80, JSP_RGBA(0, 0, 0, 170));
    jsp_overlay_text_style(x, 618, line, 1.1f, white, 0);
    const char* hint = "Player 1: Start when done";
    const float w2 = jsp_overlay_text_width(hint, 0.55f);
    jsp_overlay_text_style(w2 > 0 ? (1280.0f - w2) / 2 : 450.0f, 650, hint, 0.55f, white, 0);
    jsp_overlay_end();
    g_drawn = true;
}

void draw_page()
{
    using namespace jpm;
    jpo::MenuFrame surface(g_menu.page != CLOSED || (g_joining && g_join_next > 0) || g_help
                         || jpo::version_surface(g_ver_vis, g_ver.any(), g_note_vis));
    if (g_menu.page == CLOSED) {
        if (g_joining && g_join_next > 0) { join_draw(); return; }
        if (g_help) {
            // ENHANCEMENT: the game's help line for the Options list has no text for a sixth row (the text comes from a message
            // table keyed per row, not found), so the port draws one in the game's help bar (left x 210, y 540 in 1280x720).
            if (!jsp_overlay_begin(1280, 720)) return;
            jsp_overlay_text_style(210, 528, "Port settings: display, controls, online and launcher features.", 0.9f, JSP_RGBA(255, 255, 255, 255), 0);
            jsp_overlay_end();
            g_drawn = true;
            return;
        }
        if (g_ver_vis) { ver_draw(); return; }
        if (g_drawn) { jsp_overlay_close(); g_drawn = false; }
        return;
    }
    if (!jsp_overlay_begin(1280, 720)) return;
    std::vector<std::string> startup;
    if (g_menu.page == ONLINE && !g_menu.net_on) {
        for (int i = g_menu.be ? 5 : 0; i < 6; ++i) {
            char line[300];
            if (jsp_online_describe(i, line, sizeof line) < 0) break;
            startup.emplace_back(line);
        }
    }
    Overlay out;
    menu_view::draw(out, g_menu, startup);
    jsp_overlay_end();
    g_drawn = true;
}

// Patch 6501 hooks, called from cellPadGetData after every source is composed (host pad, keyboard, PAD_SCRIPT): buttons are
// (DIGITAL2 << 8) | DIGITAL1, lx/ly the left stick. The call comes on every read including drain reads (no length is passed);
// those carry the same state as the last fresh one, so no edge is lost or invented. Port 0 only.
void (*g_prev_filter)(uint32_t, uint16_t*, uint16_t*, uint16_t*) = nullptr;

void pad_filter(uint32_t port, uint16_t* buttons, uint16_t* lx, uint16_t* ly)
{
    if (g_prev_filter) g_prev_filter(port, buttons, lx, ly);
    if (port != 0) return;
    std::lock_guard<std::mutex> lk(g_mu);
    ver_refresh();   // also hides the line once the scene has stopped updating the panel
    if (!port_menu_env()) return;
    int panel, open, row;
    poll_panel(&panel, &open, &row);
    const bool in_row = g_obj && open && panel == 8 && row == jpm::kPortRow;
    const jpm::Page before = g_menu.page;
    const bool swallow = g_menu.feed(*buttons, 1, in_row);
    if (g_menu.page != before)
        std::fprintf(stderr, "[portmenu] page %d -> %d (flip %u)\n", (int)before, (int)g_menu.page, (unsigned)cellGcm_flip_request_count());
    for (auto& e : g_menu.events) std::fprintf(stderr, "[portmenu] %s\n", e.c_str());
    g_menu.events.clear();
    if (swallow) { *buttons = 0; *lx = 0x80; *ly = 0x80; }
    const bool help = in_row && g_menu.page == jpm::CLOSED;
    const bool help_changed = help != g_help;
    g_help = help;
    const auto status_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    g_menu.poll_connection_status(static_cast<uint64_t>(status_ms));
    if (g_menu.take_dirty() || help_changed) draw_page();
}

// Toolkit patch 7908 calls this when join starts, ends or the next player changes (not under its pad lock).
void jsp_pad_join_notify(int joining, int next_player)
{
    std::lock_guard<std::mutex> lk(g_mu);
    g_joining = joining != 0;
    g_join_next = joining ? next_player : 0;
    std::fprintf(stderr, "[padjoin] %s, next player %d (flip %u)\n", joining ? "join running" : "join ended", next_player, (unsigned)cellGcm_flip_request_count());
    draw_page();
}

// The right stick is filtered before the buttons on the same read, so it follows the state of the previous read: a one-read
// lag at the edges, harmless for a stick (the open press itself is a button).
void (*g_prev_rstick)(uint32_t, uint16_t*, uint16_t*) = nullptr;

void rstick_filter(uint32_t port, uint16_t* rx, uint16_t* ry)
{
    if (g_prev_rstick) g_prev_rstick(port, rx, ry);
    if (port != 0) return;
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_menu.active()) { *rx = 0x80; *ry = 0x80; }
}

// The one place that knows where the pad hook lives (toolkit patch 6501); swap here if the hook point changes.
void install_pad_filter()
{
    g_prev_filter = g_cellpad_filter;   // chain behind the filter jsp_game.cpp installed earlier
    g_cellpad_filter = pad_filter;
    g_prev_rstick = g_cellpad_rstick_filter;
    g_cellpad_rstick_filter = rstick_filter;
}

// @0x001E0830(obj): the Options panel is built; make its cursor struct six rows.
void on_build_options(ppu_context* c)
{
    const uint32_t obj = (uint32_t)c->gpr[3];
    g_orig_build(c);
    int32_t total = (int32_t)vm_read32(obj + k_cursor), vis = (int32_t)vm_read32(obj + k_cursor + 8), last = (int32_t)vm_read32(obj + k_cursor + 0x10);
    const int32_t t0 = total, v0 = vis, l0 = last;
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_obj != obj) std::fprintf(stderr, "[portmenu] main-menu object 0x%08X (was 0x%08X)\n", obj, g_obj);
    g_obj = obj; g_obj_vt = vm_read32(obj); g_dead_logged = false;
    g_ver_seen = false; g_ver_vis = false;   // a new menu object: the panel must be seen again before the line shows
    g_note_vis = false;
    draw_page();   // reconcile retained pixels and ownership before ver_refresh can return on unchanged visibility
    if (top_items()) std::fprintf(stderr, "[version] menu object built 0x%08X (flip %u)\n", obj, (unsigned)cellGcm_flip_request_count());
    if (!port_menu_env()) return;            // the hook also runs for the version line alone
    if (jpm::six_rows(&total, &vis, &last)) {
        vm_write32(obj + k_cursor, (uint32_t)total); vm_write32(obj + k_cursor + 8, (uint32_t)vis); vm_write32(obj + k_cursor + 0x10, (uint32_t)last);
        std::fprintf(stderr, "[portmenu] Options panel built (+0xF4): cursor struct {total %d, vis %d, last %d} -> {6, 6, 5}, cursor %d\n", t0, v0, l0,
                     (int)vm_read32(obj + k_cursor + 4));
    } else {
        std::fprintf(stderr, "[portmenu] Options panel built (+0xF4): cursor struct {total %d, vis %d, last %d} left as is\n", t0, v0, l0);
    }
}



// @0x001E7238(panel): the panel's per-frame update. In state 3 it ends in a tail call to 0x001E6E98 (rows and label bases per
// menu id), which ends in a tail call to 0x001E6C68(obj, off base, on base, rows) -- both through g_trampoline_fn, so neither is
// seen by a function-table wrapper (a wrapper on 0x001E6E98 never ran). Run the middle link here, then the registers
// of the last link are final: the Options panel's call is (0x29, 0x45, 5); ask for one more, so the sixth frame gets
// base+5 = ranking_000, which the label mask replaces (patch 2518). The last link is left pending for the caller's drain.
void on_panel_update(ppu_context* c)
{
    const uint32_t obj = (uint32_t)c->gpr[3];
    g_orig_update(c);
    if (top_items()) {   // panel state and time of this update, for the version line and the net notice
        std::lock_guard<std::mutex> lk(g_mu);
        g_ver_seen = true; g_ver_flip = (uint32_t)cellGcm_flip_request_count(); g_ver_state = (int)vm_read32(obj + k_panel_state);
        ver_refresh();
    }
    if (!port_menu_env()) return;
    if (g_trampoline_fn != (void (*)(void*))func_001E6E98) return;
    g_trampoline_fn = nullptr;
    func_001E6E98(c);
    if (g_trampoline_fn != (void (*)(void*))func_001E6C68) return;
    const uint32_t a4 = (uint32_t)c->gpr[4], a5 = (uint32_t)c->gpr[5], a6 = (uint32_t)c->gpr[6];
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_panel != obj) std::fprintf(stderr, "[portmenu] panel object 0x%08X (was 0x%08X)\n", obj, g_panel);
    g_panel = obj; g_panel_vt = vm_read32(obj); g_dead_logged = false;
    static uint32_t s4, s5, s6, sid = ~0u;
    const uint32_t id = vm_read32(obj + k_panel_id);
    if (a4 != s4 || a5 != s5 || a6 != s6 || id != sid) {
        std::fprintf(stderr, "[portmenu] label bind r4=0x%X r5=0x%X r6=%u (panel id %u mode %u state %u)\n", a4, a5, a6, id,
                     (unsigned)vm_read32(obj + k_panel_open), (unsigned)vm_read32(obj + k_panel_state));
        s4 = a4; s5 = a5; s6 = a6; sid = id;
    }
}

// The inputs of the version line: env, then release.txt beside the exe or in its parent folder.
std::string read_text(const std::string& path)
{
    std::string t;
    if (FILE* f = std::fopen(path.c_str(), "rb")) { char b[4096]; size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0 && t.size() < 65536) t.append(b, n); std::fclose(f); }
    return t;
}

void load_version_line()
{
    jpv::Inputs in;
    in.switch_env = std::getenv("JSP_VERSION_LINE");
    in.port_version = std::getenv("JSP_PORT_VERSION");
#ifdef _WIN32
    char exe[1024] = "";
    const unsigned long n = GetModuleFileNameA(nullptr, exe, sizeof exe);
    std::string dir(exe, n < sizeof exe ? n : 0);
    const size_t sl = dir.find_last_of("\\/");
    dir = sl == std::string::npos ? "" : dir.substr(0, sl);
    const size_t sl2 = dir.find_last_of("\\/");
    const std::string dirs[2] = { dir, sl2 == std::string::npos ? "" : dir.substr(0, sl2) };
    for (const auto& d : dirs) {
        if (d.empty()) continue;
        if (in.file_version.empty()) in.file_version = jpv::parse_version_txt(read_text(d + "\\release.txt"));
    }
#endif
    g_ver = jpv::choose(in);
    if (!g_ver.any()) std::fprintf(stderr, "[version] no version line (no release.txt, no JSP_PORT_VERSION, JSP_VERSION_LINE=%s)\n", in.switch_env ? in.switch_env : "unset");
    // the net notice may exist: rollback was asked for (it can still stop in jsp_net_game_install, after this) or refused before main() (JSP_ROLLBACK is 0 then)
    g_net_watch = jpno::flag_on(std::getenv(jpno::kKeyOn)) || jpno::flag_on(std::getenv(jpno::kKeyRefused));
}

bool install(uint32_t addr, fn_t wrapper, fn_t* orig)
{
    for (uint64_t i = 0; i < function_table_count; i++)
        if (function_table[i].addr == addr) {
            *orig = function_table[i].func;
            ppu_register_function(addr, wrapper);
            return true;
        }
    std::fprintf(stderr, "[portmenu] no lifted function at %08X\n", addr);
    return false;
}

}  // namespace

extern "C" void jsp_register_port_menu_overrides(void)
{
    load_version_line();
    g_pad_join_notify = jsp_pad_join_notify;   // the join text does not depend on the Port Settings page or the US build
    if (!port_menu_env() && !top_items()) return;
    if (vm_read32(k_row_count_insn) != 0x3B200005u) {   // the hooks' addresses are those of the US 1.00 code only
        std::fprintf(stderr, "[portmenu] JSP_PORT_MENU, a version line or a rollback notice is set, but this is not the US 1.00 build (0x%08X holds 0x%08X): the page, the line and the notice stay off (the [net] log lines remain)\n", k_row_count_insn, vm_read32(k_row_count_insn));
        return;
    }
    const bool a = install(k_build_options, on_build_options, &g_orig_build);
    const bool b = install(k_panel_update, on_panel_update, &g_orig_update);
    if (!a || !b) return;
    if (!port_menu_env()) {   // the version line alone: only the pad hook (a once-per-read tick), no Port Settings behaviour
        install_pad_filter();
        std::fprintf(stderr, "[version] line \"%s\"%s (hooks 0x%08X, 0x%08X, pad tick)\n", g_ver.line1.c_str(), g_net_watch ? ", rollback notice watch" : "",
                     k_build_options, k_panel_update);
        return;
    }
    // Label mask: bit 0 (Training on the Free Battle panel) as before, from the environment; bit 1 Port Settings.
    int mask = 2;
    const char* m = std::getenv("JSP_MENU_LABEL_MASK"), *t = std::getenv("JSP_MENU_TRAINING_LABEL");
    if ((m && (std::atoi(m) & 1)) || (!m && t && std::atoi(t))) mask |= 1;
    jsp_menu_label_set(mask);
    g_port_menu_six_rows = 1;
    g_menu.be = &g_backend;   // the Online page is editable (JSP_PORT_MENU_EDIT=0 keeps it read only)
    g_menu.io = &g_settings_io;   // the Display, Graphics, Controls and Launcher-only pages
    if (const char* ed = std::getenv("JSP_PORT_MENU_EDIT")) if (ed[0] == '0') { g_menu.be = nullptr; g_menu.io = nullptr; }
    install_pad_filter();
    // The line names where the setting came from: "unset (default on)" or the value it was set to. It starts with "JSP_PORT_MENU setting"
    // (run checkers match the prefix "JSP_PORT_MENU set").
    const char* pm = std::getenv("JSP_PORT_MENU");
    std::fprintf(stderr, "[portmenu] JSP_PORT_MENU setting: %s%s%s: Options gets a sixth row (hooks 0x%08X, 0x%08X), pad filter on, label mask %d\n",
                 pm ? "set to \"" : "unset (default on)", pm ? pm : "", pm ? "\"" : "", k_build_options, k_panel_update, mask);
}
#endif
