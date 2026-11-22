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

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "jsp_version_line.h"   // the version line (text and value choice, host-tested: installer/tools/version_line_test.cpp)
#include "jsp_net_online.h"      // ENHANCEMENT (in-game netcode option): the Netcode choice Off / Delay / Rollback (host-tested: installer/tools/net_online_test.cpp)
#include "jsp_settings.h"        // the launcher's settings as data and the settings file model (host-tested: installer/tools/settings_test.cpp)

#ifndef JSP_PORT_MENU_TEST
#include <mutex>
#ifdef _WIN32
extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameA(void*, char*, unsigned long);   // no <windows.h>: its macros clash with the recomp headers
#endif
#include "ps3emu/ps3types.h"
#include "ppu_recomp.h"
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
    // ENHANCEMENT (in-game netcode option): the Netcode choice drives the method directly (defaults: a backend without them has no Netcode row use)
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
    int delay_method = 1;               // the last Delay method (1 re-psn, 2 community): leaving Delay and coming back keeps it
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

    void enter_online()
    {
        page = ONLINE; dirty = true; osel = 0; editing = false; edited = false; msg.clear(); msg_err = false;
        net_on = false; net_note.clear();
        if (be) { std::string note; const bool ok = be->begin(note); msg = note; msg_err = !ok; if (ok && io) { net_load(); if (!net_note.empty()) { msg = net_note; msg_err = true; } } }
    }
    // ENHANCEMENT (in-game netcode option): reads the saved Netcode (play_settings.txt + the backend's method) and what this process started with. A saved pair that is
    // both on (hand edit) shows as Off: the page never picks for the player, the game refuses it at start.
    void net_load()
    {
        std::string text, note;
        if (!io->load(text, note)) { net_note = note; return; }
        const jps::File f = jps::File::parse(text);
        rb = jpno::Rollback(); rb.load(f);
        nc_saved = jpno::saved_netcode(f, be->method());
        delay_method = be->method() ? be->method() : 1;
        nc_running = jpno::running_netcode([&](const char* k) { return io->active(k); });
        nc_mods_off = jpno::flag_on(io->active(jpno::kKeyModsOff));
        nc = nc_saved == jpno::NC_CONFLICT ? jpno::NC_OFF : nc_saved;
        if (nc_saved == jpno::NC_CONFLICT) { be->set_method(0); net_note = "Rollback and the game's own online are both on in the saved files; the game refuses both. Pick one and save."; }
        else if (rb.both_set) net_note = "Both a Rollback host port and a join address are saved; saving keeps only the one for the chosen role.";
        net_on = true; osel = 0;
    }
    // the rows of the Online page, top to bottom (row ids of jpno): the older page without net_on
    std::vector<int> orows() const
    {
        if (net_on) return jpno::rows_for(nc, rb.host, rb.repsn);
        return std::vector<int>{ ROW_METHOD, ROW_SERVER, ROW_NAME, ROW_PRESET, ROW_SAVE, ROW_DISCARD };
    }
    int orow() const { const std::vector<int> r = orows(); return r[(size_t)(osel < (int)r.size() ? osel : 0)]; }
    bool field_is_text_row(int row) const { return field_is_text(row) || (net_on && (row == jpno::R_PORT || row == jpno::R_JOIN)); }
    const char* field_charset(int f) const
    {
        if (net_on && f == jpno::R_PORT) return "0123456789";
        if (net_on && f == jpno::R_JOIN) return "0123456789.:";
        return be->charset(f);
    }
    size_t field_max_len(int f) const
    {
        if (net_on && f == jpno::R_PORT) return 5;
        if (net_on && f == jpno::R_JOIN) return 21;   // 255.255.255.255:65535
        return be->max_len(f);
    }
    // Netcode: Off / Delay / Rollback. Only one is on: the backend's method follows (Delay needs re-psn or a community server; Off and Rollback switch it off).
    void net_choose(int v)
    {
        if (nc == jpno::NC_DELAY && be->method()) delay_method = be->method();   // remember the Delay method before leaving it
        nc = v;
        be->set_method(v == jpno::NC_DELAY ? delay_method : v == jpno::NC_NATIVE ? 1 : 0);   // NC_NATIVE: the game's own online over re-psn, with rollback
        osel = 0; edited = true; msg = "Not saved yet."; msg_err = false; dirty = true;
        events.push_back(std::string("online edit: netcode ") + jpno::name_of(nc));
    }
    // Save: the method that goes OFF is written first, so a failed or interrupted save never leaves both on.
    bool net_save(std::string& m)
    {
        const bool rbon = nc == jpno::NC_ROLLBACK, native = nc == jpno::NC_NATIVE;
        std::string why = rbon ? rb.check() : be->check();
        if (why.empty() && rbon && rb.repsn && (be->get_text(ROW_SERVER).empty() || be->get_text(ROW_NAME).empty()))
            why = "Rollback over re-psn needs the server and online name of repsn.ini: set them under Delay first (they are kept).";
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
        m = native ? "Saved. Online Battle + rollback applies on the next start (Online Battle > Friendly Match)."
          : nc == jpno::NC_ROLLBACK ? std::string("Saved. Rollback (") + (rb.host ? "host, port " + rb.port : "join " + rb.join) + ") applies on the next start."
          : nc == jpno::NC_DELAY ? "Saved. Delay (the game's own online) applies on the next start."
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
        if ((edge & (BTN_LEFT | BTN_RIGHT)) && net_on && row == jpno::R_NETCODE) net_choose((nc + ((edge & BTN_LEFT) ? jpno::kNetcodeChoices - 1 : 1)) % jpno::kNetcodeChoices);
        else if ((edge & (BTN_LEFT | BTN_RIGHT)) && net_on && (row == jpno::R_CONN || row == jpno::R_RELAY)) {   // ENHANCEMENT (rollback over re-psn)
            if (row == jpno::R_CONN) { rb.repsn = !rb.repsn; if (osel >= (int)orows().size()) osel = (int)orows().size() - 1; }
            else rb.relay = !rb.relay;
            edited = true; msg = row == jpno::R_RELAY ? "Relay: used only if a direct path fails and the server offers it." : "Not saved yet."; msg_err = false; dirty = true;
            events.push_back(std::string("online edit: rollback ") + (row == jpno::R_CONN ? (rb.repsn ? "connection re-psn" : "connection direct IP") : (rb.relay ? "relay allowed" : "relay not allowed")));
        }
        else if ((edge & (BTN_LEFT | BTN_RIGHT)) && net_on && row == jpno::R_ROLE) {
            rb.host = !rb.host; edited = true; msg = "Not saved yet."; msg_err = false; dirty = true;
            events.push_back(std::string("online edit: rollback role ") + (rb.host ? "host" : "join"));
        }
        else if ((edge & (BTN_LEFT | BTN_RIGHT)) && (row == ROW_METHOD || row == ROW_PRESET)) {
            if (net_on && row == ROW_METHOD) be->set_method(be->method() == 1 ? 2 : 1);   // Delay: re-psn or a community server (Off is the Netcode row)
            else be->cycle(row, (edge & BTN_LEFT) ? -1 : 1);
            edited = true; msg = "Not saved yet."; msg_err = false; dirty = true;
            events.push_back("online edit: " + be->row_text(row));
        }
        if (edge & BTN_CROSS) {
            if (field_is_text_row(row)) {
                editing = true; ef = row; cpos = 0; dirty = true;
                buf = net_on && row == jpno::R_PORT ? rb.port : net_on && row == jpno::R_JOIN ? rb.join : be->get_text(row);
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
        return row == ROW_SERVER ? "server" : row == jpno::R_PORT ? "rollback port" : row == jpno::R_JOIN ? "rollback host address" : "name";
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
            else if (net_on && ef == jpno::R_JOIN) { rb.join = buf; if (!buf.empty() && !jpno::valid_join(buf)) hint = "Not a valid address: IP:port, for example 192.0.2.10:36200."; }
            else hint = be->set_text(ef, buf);
            editing = false; edited = true; dirty = true;
            msg = hint.empty() ? "Not saved yet." : hint; msg_err = !hint.empty();
            events.push_back(std::string("online edit: ") + field_name(ef) + " set to '" + buf + "'" + (hint.empty() ? " (valid)" : " (not valid yet)"));
        } else if (edge & BTN_CIRCLE) {
            editing = false; dirty = true;
            events.push_back("online edit: picker cancelled");
        }
    }
};

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

// ---- drawing: 1280x720 layout space, the game's own look where cheap (Fira Sans Heavy Italic art; white-grey with
// black rim = the menu's "off" label, black with white glow = "on" on the highlight bar) ----

// the editable Online page (same panel, font and colours as before). Rows at y 190 + 48 i; the picker underlines the letter at its position.
void draw_online_edit(uint32_t white, uint32_t gold)
{
    using namespace jpm;
    jsp_overlay_text_style(260, 120, "Port Settings - Online", 1.4f, white, 0);
    // ENHANCEMENT (in-game netcode option): with the Netcode rows the list is shorter or longer by the choice (up to 7 rows), so the pitch is 40 instead of 48
    const std::vector<int> orows = g_menu.orows();
    const int pitch = g_menu.net_on ? 40 : 48;
    for (int k = 0; k < (int)orows.size(); k++) {
        const int i = orows[(size_t)k];
        const int y = 188 + k * pitch;
        const bool on = k == g_menu.osel;
        if (on) jsp_overlay_fill_rect(250, y - 12, 780, pitch - 2, JSP_RGBA(255, 255, 255, 235));
        std::string text;
        if (i <= ROW_PRESET) text = g_backend.row_text(i);
        else if (i >= jpno::R_NETCODE || g_menu.net_on) text = jpno::row_text(i, g_menu.nc, g_menu.rb);
        else text = i == ROW_SAVE ? "Save" : "Discard changes";
        const bool pick = g_menu.editing && g_menu.ef == i;
        std::string label;
        if (pick) {
            label = i == ROW_SERVER ? "Server: " : i == jpno::R_PORT ? "Port: " : i == jpno::R_JOIN ? "Host address: " : "Online name: ";
            text = label + g_menu.buf;
        }
        jsp_overlay_text_style(290, (float)y, text.c_str(), 1.0f, white, on ? 1 : 0);
        if (pick) {
            const float x0 = 290 + jsp_overlay_text_width(label.c_str(), 1.0f);
            const float cx = x0 + jsp_overlay_text_width(g_menu.buf.substr(0, g_menu.cpos).c_str(), 1.0f);
            const bool slot = g_menu.cpos >= g_menu.buf.size();
            const float cw = slot ? 22.0f : jsp_overlay_text_width(g_menu.buf.substr(g_menu.cpos, 1).c_str(), 1.0f);
            jsp_overlay_fill_rect((int)cx, y + 28, (int)cw, 4, gold);
        }
    }
    char line[200];
    if (g_menu.net_on) {
        // the exclusion rule and what this session runs against what is saved (a restart applies a changed choice)
        bool restart = false;
        const std::string st = jpno::status_line(g_menu.nc_running, g_menu.nc_saved, &restart, g_menu.nc_mods_off);
        jsp_overlay_text_style(260, 476, jpno::exclusion_line(g_menu.nc), 0.6f, white, 0);
        jsp_overlay_text_style(260, 502, st.c_str(), 0.6f, restart ? gold : white, 0);
    } else
    if (jsp_online_describe(5, line, sizeof line) >= 0) jsp_overlay_text_style(290, 480, line, 0.7f, white, 0);
    // the message under the rows, wrapped at 60 characters (the launcher's refusal texts run to ~80) so it stays inside the panel
    const std::string m = !g_menu.msg.empty() ? g_menu.msg : g_menu.edited ? std::string("Not saved yet.") : std::string();
    const uint32_t mc = g_menu.msg_err ? JSP_RGBA(255, 150, 110, 255) : white;
    size_t from = 0; int ly = g_menu.net_on ? 530 : 510;
    while (from < m.size() && ly < 574) {
        size_t to = from + 60;
        if (to >= m.size()) to = m.size();
        else { size_t sp = m.rfind(' ', to); if (sp != std::string::npos && sp > from) to = sp; }
        jsp_overlay_text_style(260, (float)ly, m.substr(from, to - from).c_str(), 0.7f, mc, 0);
        from = to; while (from < m.size() && m[from] == ' ') from++;
        ly += 28;
    }
    jsp_overlay_text_style(260, 578, g_menu.editing ? "Up/Down: letter  Left/Right: place  Square: delete  Cross: ok  Circle: cancel"
                                                    : "Left/Right: change  Cross: edit or save  Circle: back (unsaved changes are dropped)", 0.5f, white, 0);
    jsp_overlay_text_style(260, 598, g_menu.editing ? "L1 / R1: ten letters at a time" : g_menu.net_on ? "Netcode settings cannot change in a session: saved settings apply on the next start of the game." : "Online play cannot change in a session: saved settings apply on the next start.", 0.5f, white, 0);
}

// Text wrapped at `width` characters; returns the next y. Words are not split.
int draw_wrapped(float x, int y, const std::string& m, size_t width, float scale, uint32_t col, int line_h, int max_y)
{
    size_t from = 0;
    while (from < m.size() && y < max_y) {
        size_t to = from + width;
        if (to >= m.size()) to = m.size();
        else { size_t sp = m.rfind(' ', to); if (sp != std::string::npos && sp > from) to = sp; }
        jsp_overlay_text_style(x, (float)y, m.substr(from, to - from).c_str(), scale, col, 0);
        from = to; while (from < m.size() && m[from] == ' ') from++;
        y += line_h;
    }
    return y;
}

// A settings page: label left, value right, 8 rows at a time. "*" after a value: saved, the running game has not picked it up.
void draw_settings(uint32_t white, uint32_t gold)
{
    using namespace jpm;
    const Menu& m = g_menu;
    jsp_overlay_text_style(260, 112, (std::string("Port Settings - ") + Menu::page_name(m.page)).c_str(), 1.4f, white, 0);
    const int n = (int)m.srows.size();
    for (int i = m.stop; i < n && i < m.stop + kSettingsVisible; i++) {
        const SRow& r = m.srows[(size_t)i];
        const int y = 170 + (i - m.stop) * 44;
        const bool on = i == m.ssel;
        if (on) jsp_overlay_fill_rect(250, y - 10, 780, 42, JSP_RGBA(255, 255, 255, 235));
        jsp_overlay_text_style(290, (float)y, m.row_label(r).c_str(), 0.95f, white, on ? 1 : 0);
        const std::string v = m.row_value(r);
        if (!v.empty()) {
            const std::string shown = m.row_pending(r) ? v + " *" : v;
            jsp_overlay_text_style(640, (float)y, shown.c_str(), 0.85f, white, on ? 1 : 0);
        }
    }
    if (m.stop > 0) jsp_overlay_text_style(930, 112, "more above", 0.5f, white, 0);
    if (m.stop + kSettingsVisible < n) jsp_overlay_text_style(930, 536, "more below", 0.5f, white, 0);
    const uint32_t mc = m.smsg_err ? JSP_RGBA(255, 150, 110, 255) : white;
    if (!m.smsg.empty()) draw_wrapped(260, 536, m.smsg, 60, 0.7f, mc, 24, 572);
    const SRow& cur = m.srows[(size_t)m.ssel];
    if (m.capturing) {
        jsp_overlay_text_style(260, 578, m.model.mode() == 1 ? "Press a key or a mouse button.  Esc or Circle: cancel" : "Press a key.  Esc or Circle: cancel", 0.5f, white, 0);
        jsp_overlay_text_style(260, 598, "F11 stays the fullscreen key.", 0.5f, white, 0);
    } else {
        const char* h1 = cur.kind == SRow::KEY ? "Up/Down: select  Cross: change the key  Circle: back"
                       : cur.kind == SRow::OPEN || cur.kind == SRow::RESET ? "Up/Down: select  Cross: open or reset  Circle: back"
                       : cur.kind == SRow::JOIN ? "Up/Down: select  Cross: start  Circle: back.  Or hold Select + L3 + R3 for 2 s on a pad."
                       : "Up/Down: select  Left/Right or Cross: change  Circle: back";
        jsp_overlay_text_style(260, 578, h1, 0.5f, white, 0);
        jsp_overlay_text_style(260, 598, "Every change is saved at once. * = saved; applies on the next start.", 0.5f, white, 0);
    }
    (void)gold;
}

// What the launcher keeps for itself, and why.
void draw_launcher_only(uint32_t white)
{
    jsp_overlay_text_style(260, 112, "Port Settings - Launcher only", 1.4f, white, 0);
    int y = 168;
    for (int i = 0; i < jps::k_launcher_only_count; i++) {
        jsp_overlay_text_style(290, (float)y, jps::k_launcher_only[i].what, 0.8f, JSP_RGBA(240, 190, 30, 255), 0);
        y = draw_wrapped(310, y + 30, jps::k_launcher_only[i].why, 78, 0.55f, white, 22, 580);
        y += 12;
    }
    jsp_overlay_text_style(260, 598, "Circle: back", 0.5f, white, 0);
}

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
    if (g_menu.page == CLOSED) {
        if (g_joining && g_join_next > 0) { join_draw(); return; }
        if (g_help) {
            // ENHANCEMENT: the game's help line for the Options list has no text for a sixth row (the text comes from a message
            // table keyed per row, not found), so the port draws one in the game's help bar (left x 210, y 540 in 1280x720).
            if (!jsp_overlay_begin(1280, 720)) return;
            jsp_overlay_text_style(210, 528, "Port settings: online options and more.", 0.9f, JSP_RGBA(255, 255, 255, 255), 0);
            jsp_overlay_end();
            g_drawn = true;
            return;
        }
        if (g_ver_vis) { ver_draw(); return; }
        if (g_drawn) { jsp_overlay_close(); g_drawn = false; }
        return;
    }
    if (!jsp_overlay_begin(1280, 720)) return;
    const uint32_t white = JSP_RGBA(255, 255, 255, 255);
    const uint32_t gold = JSP_RGBA(240, 190, 30, 255);   // ASSUMPTION: close to the menu's yellow
    jsp_overlay_fill_rect(0, 0, 1280, 720, JSP_RGBA(0, 0, 0, 150));
    jsp_overlay_fill_rect(220, 90, 840, 540, JSP_RGBA(16, 20, 40, 235));
    jsp_overlay_fill_rect(220, 90, 840, 6, gold);
    jsp_overlay_fill_rect(220, 624, 840, 6, gold);
    if (g_menu.page == MAIN) {
        jsp_overlay_text_style(260, 130, "Port Settings", 1.6f, white, 0);
        const std::vector<Page> mr = g_menu.main_rows();
        const int step = mr.size() > 2 ? 52 : 70, y0 = mr.size() > 2 ? 200 : 250;
        for (size_t i = 0; i < mr.size(); i++) {
            const int y = y0 + (int)i * step;
            const bool on = (int)i == g_menu.sel;
            const char* name = mr[i] == DISPLAY ? "Display" : mr[i] == GRAPHICS ? "Graphics" : mr[i] == CONTROLS ? "Controls" : mr[i] == ONLINE ? "Online" : mr[i] == LAUNCHER ? "Launcher only" : "Close";
            if (on) jsp_overlay_fill_rect(250, y - 14, 780, 48, JSP_RGBA(255, 255, 255, 235));
            jsp_overlay_text_style(290, (float)y, name, 1.2f, white, on ? 1 : 0);
        }
        if (g_menu.io) {
            if (!g_menu.smsg.empty()) jsp_overlay_text_style(260, 520, g_menu.smsg.c_str(), 0.7f, g_menu.smsg_err ? JSP_RGBA(255, 150, 110, 255) : white, 0);
            std::string pth = jps::display_path(g_menu.io->path());
            if (pth.size() > 78) pth = "..." + pth.substr(pth.size() - 75);
            jsp_overlay_text_style(260, 548, ("Settings file: " + pth).c_str(), 0.5f, white, 0);
        }
        jsp_overlay_text_style(260, 575, "Up/Down: select    Cross: open    Circle: close", 0.8f, white, 0);
    } else if (g_menu.page >= DISPLAY && g_menu.page <= PADBTNS) {
        draw_settings(white, gold);
    } else if (g_menu.page == LAUNCHER) {
        draw_launcher_only(white);
    } else if (g_menu.be) {
        draw_online_edit(white, gold);
    } else {
        jsp_overlay_text_style(260, 130, "Port Settings - Online", 1.4f, white, 0);
        char line[200];
        for (int i = 0; i < 6; i++) {
            if (jsp_online_describe(i, line, sizeof line) < 0) {
                if (i == 0) jsp_overlay_text_style(290, 230, "No settings were read at start.", 1.0f, white, 0);
                break;
            }
            jsp_overlay_text_style(290, (float)(220 + i * 50), line, 1.0f, white, 0);
        }
        jsp_overlay_text_style(260, 530, "Read only. Changes apply on the next start.", 0.8f, white, 0);
        jsp_overlay_text_style(260, 565, "Circle: back", 0.8f, white, 0);
    }
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
