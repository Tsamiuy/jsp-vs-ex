// jsp_port_menu.cpp -- in-game "Port Settings" page.
//
// ENHANCEMENT: the whole file. Off unless JSP_PORT_MENU=1, and only for the US 1.00 build (the hooks sit at fixed addresses of that code; the
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
//      read at start (read only; changing them is not part of this page yet).
//
// The game's own row count: @0x001E6E98 sets the count r25 from the menu id (id 8: `li r25,5` at 0x001E71C0) and then hides the parts of every
// row >= count at each update (loop 0x001E6FC4) and passes the count on to the label binder. The lifter's --insn-patches option (firstrun.py
// passes lift_insn_patches.json for the US build) emits at 0x001E71C0 `if (g_port_menu_six_rows) li r25,6 else li r25,5`, so the game's loop
// shows row 5 and the binder gets 6 labels itself. The switch is set below (JSP_PORT_MENU=1) before the guest runs.
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
//   - the port's "Exit Game" option (jsp_game.cpp) shares the Options panel; its test reads the Options cursor counted from the bottom
//     (0x008E63FC == 0), which may now be the Port Settings row. Not checked: needs a run with both on.

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#ifndef JSP_PORT_MENU_TEST
#include <mutex>
#include "ps3emu/ps3types.h"
#include "ppu_recomp.h"
#include "jsp_overlay_api.h"
extern "C" uint32_t cellGcm_flip_request_count(void);
extern "C" void ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));
// Toolkit patch 6501: hooks in cellPadGetData, NULL by default (jsp_game.cpp sets them first; this file chains behind them).
extern "C" void (*g_cellpad_filter)(uint32_t port_no, uint16_t* buttons, uint16_t* lx, uint16_t* ly);
extern "C" void (*g_cellpad_rstick_filter)(uint32_t port_no, uint16_t* rx, uint16_t* ry);
extern "C" int jsp_online_describe(int line, char* out, int n);                  // jsp_online.cpp
#endif

namespace jpm {

// Digital buttons as one mask: (DIGITAL2 << 8) | DIGITAL1 (cellPad.h CELL_PAD_CTRL_*; the same masks PAD_SCRIPT uses).
enum : uint16_t { BTN_START = 0x0008, BTN_UP = 0x0010, BTN_RIGHT = 0x0020, BTN_DOWN = 0x0040, BTN_LEFT = 0x0080,
                  BTN_CIRCLE = 0x2000, BTN_CROSS = 0x4000 };
enum Page { CLOSED = 0, MAIN = 1, ONLINE = 2 };
constexpr int kMainRows = 2;   // 0 "Online", 1 "Close"

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

// The page's state machine; no guest memory, no drawing: everything the pad does.
struct Menu {
    Page page = CLOSED;
    int sel = 0;
    uint16_t prev = 0;
    bool wait_release = false;
    bool dirty = false;

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
        const int rows = page == MAIN ? kMainRows : 0;
        if (rows && (edge & BTN_DOWN)) { sel = (sel + 1) % rows; dirty = true; }
        if (rows && (edge & BTN_UP)) { sel = (sel + rows - 1) % rows; dirty = true; }
        if (edge & BTN_CROSS) {
            if (page == MAIN) {
                if (sel == 0) { page = ONLINE; dirty = true; }
                else close();
            }
        }
        if (edge & BTN_CIRCLE) {
            if (page == ONLINE) { page = MAIN; sel = 0; dirty = true; }
            else close();
        }
        return true;
    }
    void close() { page = CLOSED; sel = 0; wait_release = true; dirty = true; }
    bool take_dirty() { const bool d = dirty; dirty = false; return d; }
};

}  // namespace jpm

#ifndef JSP_PORT_MENU_TEST
// The lifter's --insn-patches guard (lift_insn_patches.json): the lifted code reads this switch at 0x001E71C0 (see the top of the file).
// Defined always, so a lift made with the patch links even when JSP_PORT_MENU is off.
extern "C" { int g_port_menu_six_rows = 0; }

namespace {

typedef void (*fn_t)(ppu_context*);

bool port_menu_env()
{
    static const bool on = [] { const char* e = std::getenv("JSP_PORT_MENU"); return e && *e && e[0] != '0'; }();
    return on;
}

std::mutex g_mu;
jpm::Menu g_menu;
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
void draw_page()
{
    using namespace jpm;
    if (g_menu.page == CLOSED) {
        if (g_help) {
            // ENHANCEMENT: the game's help line for the Options list has no text for a sixth row (the text comes from a message
            // table keyed per row, not found), so the port draws one in the game's help bar (left x 210, y 540 in 1280x720).
            if (!jsp_overlay_begin(1280, 720)) return;
            jsp_overlay_text_style(210, 528, "Port settings: online options and more.", 0.9f, JSP_RGBA(255, 255, 255, 255), 0);
            jsp_overlay_end();
            g_drawn = true;
            return;
        }
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
        static const char* const rows[kMainRows] = {"Online", "Close"};
        for (int i = 0; i < kMainRows; i++) {
            const int y = 250 + i * 70;
            const bool on = i == g_menu.sel;
            if (on) jsp_overlay_fill_rect(250, y - 16, 780, 56, JSP_RGBA(255, 255, 255, 235));
            jsp_overlay_text_style(290, (float)y, rows[i], 1.2f, white, on ? 1 : 0);
        }
        jsp_overlay_text_style(260, 560, "Up/Down: select    Cross: open    Circle: close", 0.8f, white, 0);
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
    int panel, open, row;
    poll_panel(&panel, &open, &row);
    const bool in_row = g_obj && open && panel == 8 && row == jpm::kPortRow;
    const jpm::Page before = g_menu.page;
    const bool swallow = g_menu.feed(*buttons, 1, in_row);
    if (g_menu.page != before)
        std::fprintf(stderr, "[portmenu] page %d -> %d (flip %u)\n", (int)before, (int)g_menu.page, (unsigned)cellGcm_flip_request_count());
    if (swallow) { *buttons = 0; *lx = 0x80; *ly = 0x80; }
    const bool help = in_row && g_menu.page == jpm::CLOSED;
    const bool help_changed = help != g_help;
    g_help = help;
    if (g_menu.take_dirty() || help_changed) draw_page();
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
    if (!port_menu_env()) return;
    if (vm_read32(k_row_count_insn) != 0x3B200005u) {   // the hooks' addresses are those of the US 1.00 code only
        std::fprintf(stderr, "[portmenu] JSP_PORT_MENU is set, but this is not the US 1.00 build (0x%08X holds 0x%08X): the page stays off\n", k_row_count_insn, vm_read32(k_row_count_insn));
        return;
    }
    const bool a = install(k_build_options, on_build_options, &g_orig_build);
    const bool b = install(k_panel_update, on_panel_update, &g_orig_update);
    if (!a || !b) return;
    // Label mask: bit 0 (Training on the Free Battle panel) as before, from the environment; bit 1 Port Settings.
    int mask = 2;
    const char* m = std::getenv("JSP_MENU_LABEL_MASK"), *t = std::getenv("JSP_MENU_TRAINING_LABEL");
    if ((m && (std::atoi(m) & 1)) || (!m && t && std::atoi(t))) mask |= 1;
    jsp_menu_label_set(mask);
    g_port_menu_six_rows = 1;
    install_pad_filter();
    std::fprintf(stderr, "[portmenu] JSP_PORT_MENU set: Options gets a sixth row (hooks 0x%08X, 0x%08X), pad filter on, label mask %d\n",
                 k_build_options, k_panel_update, mask);
}
#endif
