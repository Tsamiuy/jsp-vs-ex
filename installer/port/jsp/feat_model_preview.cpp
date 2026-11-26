/* Model preview on Character Select: the Gallery's model viewer stands on the right, where the character grid was, on the
 * 1 Player screens. Off unless MODEL_PREVIEW=1 (Experiments builds: on unless MODEL_PREVIEW=0)
 * (1 Player Character Select of Victory Road, Arcade, Free Battle, Training and Impossible; not 2 Players, 3 vs 3 or
 * Online).
 *
 * Step 1: room for a fighter on the menus.
 *   The game keeps its packs in fixed heaps, one list ("layout") per kind of screen: table at [TOC + 0x5868] + 0x6C8,
 *   0x3168 bytes a layout, 0xF8 bytes a group (+0 group id, 0 ends the list; a 0x50-byte record per memory kind at
 *   +0x08: +0 in use, +4 size). The menus and Character Select use layout 3 (groups 26, 28, 27, 29, 2A, 2B); the
 *   Gallery's Character Info uses layout 4, which has the fighter groups 0x10 and 0x14 (battle_character_basic) and
 *   0x24 (ultimate_blast) in place of 2A and 2B. Here layout 4's three fighter records are copied to the end of
 *   layout 3's list, once, as soon as the game has filled the table, and one more record of ours, group 0x2F (a copy of
 *   scene1's, no layout uses that id), for the Gallery's own picture packs. A layout switch keeps the groups two
 *   layouts start with and rebuilds the rest (func_0043E644), so the Gallery and the fight go on as before.
 *   Tested in scripted runs: menus, Character Select, Stage Select and a fight show the same 6 fault lines as without
 *   the heaps, and the Gallery's model the same fault lines as without.
 *
 * ASSUMPTION for everything below: the addresses, class layouts and heap group numbers are the US disc's (the build the
 * port is lifted from), found by reading the lifted code and by test runs, as the notes say at each place.
 *
 * Step 2: the Gallery's viewer object on Character Select.
 *   The viewer (class at 0x001B2318..0x001B67FF) is made by func_001B54BC(viewer, parent node), told a character with
 *   func_001B26A8(viewer, id) and run once a frame with func_001B65D8(viewer). Its constructor
 *     - unloads and gives back the loaders [registry + 0x60] and [+ 0x64] (groups 2A, 2B: Character Select's own packs),
 *     - switches the heap layout to 4 (func_0043E5E8 -> func_0043E644),
 *     - reads the Gallery's layout packs from the loader [registry + 0x5C], ids 0, 1, 2.
 *   So while a viewer function runs, the registry pointer [0x0095BD9C] (func_0042D018) points at a copy of the registry
 *   whose +0x5C is a loader of ours holding progress_scene_gallery_zukan_ALL_m / _v / _i (base loader class
 *   func_00424880, blocks from group 0x2F with func_004247CC, files asked with func_00424618, "loaded" func_00424230)
 *   and whose +0x60 / +0x64 are an empty loader with do-nothing virtuals; and a layout switch asked for meanwhile is
 *   turned into "the layout already in use", which returns at once (model_preview_layout_enter).
 *   The parent node is the Progress scene (vtable 0x0087AC78; its per-frame call is indirect-call target 0x00153040);
 *   the driver is Character Select's own per-frame call (target 0x0019AA74, vtable 0x0087C3D0).
 * MODEL_PREVIEW_CHAR=<id> the fighter shown (default 28). MODEL_PREVIEW_STOP=<n>: stop after phase n (1 the packs asked,
 * 2 the packs loaded, 3 the viewer made, 4 updates, 5 a character asked: default). MODEL_PREVIEW_LOG=1 logs more. */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstddef>
#include <cstdarg>
#include <atomic>
#ifdef _WIN32
#include <windows.h>
#endif
#include "ppu_recomp.h"
#include "jsp_region.h"
// US only for now (jsp_region.h): the Model Viewer on Character Select sits on hooks, guest addresses and lifted functions of the US 1.00 code, so an EU build
// compiles none of this file. Its callers (jsp_game.cpp, the other feat_*.cpp) reach it through weak symbols, which an EU build
// leaves null: the feature is simply absent; its lift hooks are in the US lift only (firstrun.py, lift_game_hooks.json).
#if JSP_US_CODE
#include "jsp_overlay_api.h"
#include <vector>

extern "C" uint8_t* vm_base;
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
extern "C" void ps3_indirect_call_to(ppu_context* ctx, uint32_t target);
extern "C" int jsp_imp_mode(void);            // jsp_game.cpp: 0 Free Battle, 1 Impossible 2 vs 1, 2 and up another special fight
extern "C" int jsp_tr_active(void);           // jsp_game.cpp: Training Mode is on
// jsp_game.cpp, where the build has other colours for a character (absent: everyone has only its own colour)
extern "C" __attribute__((weak)) int jsp_alt_count(int chr);                 // how many colours the character has, its own included
extern "C" __attribute__((weak)) const char* jsp_alt_name(int chr, int alt); // a colour's name for the label; 0 is its own
extern "C" int jsp_cs_is_3v3(void);           // jsp_game.cpp: the 3 vs 3 Character Select (three boxes a team) is the one in use
extern "C" uint32_t jsp_cs_state_seq(void);   // jsp_game.cpp: how often func_00188A58 has run (the object below is this visit's once it has moved)
extern "C" uint32_t jsp_cs_state_obj(void);   // jsp_game.cpp: Character Select's player state object (player state at +0x60 + 0x14 * player)
extern "C" int jsp_session_on(void);          // jsp_game.cpp: a rollback run, an online session or a re-run tick: nothing of the viewer runs
// The other users of the one overlay layer (weak: a build without them has none). While one holds the layer the viewer's texts are
// not drawn and the layer is not closed from here.
extern "C" __attribute__((weak)) int jsp_port_menu_overlay_held(void);
extern "C" __attribute__((weak)) int jsp_net_overlay_held(void);

#ifndef JSP_EXPERIMENTS
#define JSP_EXPERIMENTS 0
#endif

namespace {
int envi(const char* k, int d) { const char* e = std::getenv(k); return e && *e ? std::atoi(e) : d; }
/* a probe's switch: read in experiments builds only (elsewhere the probe is off whatever is set) */
#if JSP_EXPERIMENTS
int probe_env(const char* k, int d) { return envi(k, d); }
#else
int probe_env(const char*, int d) { return d; }
#endif
/* MODEL_PREVIEW is the switch for the viewer. In a rollback run it is off whatever the setting says: a rollback match
 * is set up on Free Battle's 1 Player Character Select, and the viewer changes the menus' heap layout, the grid's table and what is
 * loaded, which the two sides must not differ in. That answer is taken once, at the first question (the run's mode does not change),
 * so nothing of the file runs in such a run, the heap change at the start included. */
/* The viewer is off unless MODEL_PREVIEW=1 (the settings window's "Model viewer" box); Experiments builds keep it on unless
 * MODEL_PREVIEW=0. Always off in a rollback run. */
bool enabled() { static const bool v = envi("MODEL_PREVIEW", JSP_EXPERIMENTS ? 1 : 0) == 1 && !jsp_session_on(); return v; }
bool layer_free() { return !(jsp_port_menu_overlay_held && jsp_port_menu_overlay_held()) && !(jsp_net_overlay_held && jsp_net_overlay_held()); }
bool logon() { static const bool v = envi("MODEL_PREVIEW_LOG", 0) == 1; return v; }
bool ok(uint32_t a) { return a >= 0x10000u && a < 0x30000000u; }
void say(const char* fmt, ...) { va_list ap; va_start(ap, fmt); std::printf("[modelpv] "); std::vprintf(fmt, ap); std::printf("\n"); std::fflush(stdout); va_end(ap); }

/* ---- step 1: the heap layout ---- */
const uint32_t kLayoutBytes = 0x3168u, kGroupBytes = 0xF8u, kMaxGroups = 0x32u, kOurGroup = 0x2Fu;
uint32_t layout_table() { const uint32_t t = vm_read32(0x008D3888u + 0x5868u); return ok(t) ? t + 0x6C8u : 0u; }
int group_count(uint32_t layout) { int n = 0; while (n < (int)kMaxGroups && vm_read32(layout + (uint32_t)n * kGroupBytes)) n++; return n; }
int group_at(uint32_t layout, uint32_t id) { const int n = group_count(layout); for (int i = 0; i < n; i++) if (vm_read32(layout + (uint32_t)i * kGroupBytes) == id) return i; return -1; }

/* MODEL_PREVIEW_NAMEPIC (default 1; 0 = the port's text as before; MODEL_PREVIEW_NAMEPIC_SCALE percent, default 120): the fighter's name as the game's own name picture of the VS screen
 * (the names the VS screen shows). The packs are
 * progress/coomon_chara_name/<lang>/common_chara_name_<id>_<lang>_m.pak / _v.pak, blocks of 0x800 (main) and
 * 0x2800 (video) bytes, picture battle_name_tx_<id>.png. They go on ids 8 / 9 of our own loader. */
bool namepic() { static const bool v = envi("MODEL_PREVIEW_NAMEPIC", 1) != 0; return v; }
char g_lang[16];   /* the game's language string ("US"), kept when the Gallery packs are asked for */
bool g_room_done = false, g_room_ok = false;
void make_room()
{
    if (g_room_done || !vm_base) return;
    const uint32_t t = layout_table(); if (!t) return;
    const uint32_t l3 = t + 3u * kLayoutBytes, l4 = t + 4u * kLayoutBytes;
    static const uint32_t want[3] = { 0x10u, 0x14u, 0x24u };
    /* the table is filled at boot (func_0043EBE8): wait until both lists are there and every size is set */
    const int scene1 = group_at(l3, 0x29u);
    if (group_at(l3, 0x2Bu) < 0 || scene1 < 0) return;
    int from[3];
    for (int i = 0; i < 3; i++) { from[i] = group_at(l4, want[i]); if (from[i] < 0 || !vm_read32(l4 + (uint32_t)from[i] * kGroupBytes + 0x0Cu)) return; }
    int n = group_count(l3);
    g_room_done = true;
    if (n + 4 > (int)kMaxGroups - 1) { say("no room in layout 3's list (%d groups)", n); return; }
    for (int i = 0; i < 3; i++) {
        if (group_at(l3, want[i]) >= 0) continue;
        std::memcpy(vm_base + l3 + (uint32_t)n * kGroupBytes, vm_base + l4 + (uint32_t)from[i] * kGroupBytes, kGroupBytes);
        n++;
    }
    /* our own group for the Gallery's layout packs: scene1's record under a free id, its video arena cut to what the
     * packs need (ALL_m 0xCB800 of main memory, ALL_v 0x19B800 of video memory, ALL_i 0x2000: read from the Gallery's
     * blocks) */
    const uint32_t ours = l3 + (uint32_t)n * kGroupBytes;
    std::memcpy(vm_base + ours, vm_base + l3 + (uint32_t)scene1 * kGroupBytes, kGroupBytes);
    vm_write32(ours, kOurGroup);   /* arenas as scene1's: 0xD4800 / 0x116F800 / 0x2000, exactly what the Gallery's seven packs take */
    if (namepic()) {   /* room for one name pack: 0x800 + 0x2800 and what the allocator puts around a block (the VS screen's own names needed 0x1000 / 0x5000 for two, jsp_game.cpp vsn_arena_fix) */
        vm_write32(ours + 8u + 4u, vm_read32(ours + 8u + 4u) + 0x1000u); vm_write32(ours + 8u + 0x50u + 4u, vm_read32(ours + 8u + 0x50u + 4u) + 0x5000u); }
    n++;
    vm_write32(l3 + (uint32_t)n * kGroupBytes, 0u);   /* the list's end */
    g_room_ok = vm_read32(ours + 8u + 4u) >= 0xCB800u && vm_read32(ours + 8u + 0xA0u + 4u) >= 0x2000u;
    say("layout 3 has the fighter groups 10, 14, 24 and our group %X now (%d groups; ours: %X / %X / %X)", kOurGroup, n,
        vm_read32(ours + 8u + 4u), vm_read32(ours + 8u + 0x50u + 4u), vm_read32(ours + 8u + 0xA0u + 4u));
}

/* ---- step 2: the viewer ---- */
uint32_t call(void (*fn)(ppu_context*), const ppu_context* ctx, uint64_t a = 0, uint64_t b = 0, uint64_t c2 = 0, uint64_t d = 0, uint64_t e = 0)
{
    ppu_context c = *ctx; c.lr = 0; c.gpr[3] = a; c.gpr[4] = b; c.gpr[5] = c2; c.gpr[6] = d; c.gpr[7] = e;
    fn(&c); while (g_trampoline_fn) { auto f = g_trampoline_fn; g_trampoline_fn = 0; f(&c); }
    return (uint32_t)c.gpr[3];
}
uint32_t gnew(const ppu_context* ctx, uint32_t size)
{
    const uint32_t p = call(func_00717C2C, ctx, size);
    if (!ok(p)) return 0u;
    std::memset(vm_base + p, 0, size);
    return p;
}
/* a block of ours that is kept for the whole run: made at the first use, cleared at each later one */
uint32_t keep(const ppu_context* ctx, uint32_t& slot, uint32_t size)
{
    if (slot) std::memset(vm_base + slot, 0, size); else slot = gnew(ctx, size);
    return slot;
}

/* a virtual of a game object: the descriptor at vtable + slot */
uint32_t vcall(const ppu_context* ctx, uint32_t obj, uint32_t slot, uint64_t b = 0)
{
    const uint32_t vt = ok(obj) ? vm_read32(obj) : 0u, d = ok(vt) ? vm_read32(vt + slot) : 0u;
    if (!ok(d)) return 0u;
    ppu_context c = *ctx; c.lr = 0; c.gpr[3] = obj; c.gpr[4] = b; c.ctr = vm_read32(d); c.gpr[2] = vm_read32(d + 4u);
    ps3_indirect_call_to(&c, vm_read32(d)); while (g_trampoline_fn) { auto f = g_trampoline_fn; g_trampoline_fn = 0; f(&c); }
    return (uint32_t)c.gpr[3];
}
/* the nodes under a node (library node: +0x08 next, +0x0C first child, +0x10 parent, +0x14 name) */
int children(uint32_t node, uint32_t* out, int max)
{
    int n = 0; if (!ok(node)) return 0;
    for (uint32_t c = vm_read32(node + 0x0Cu); ok(c) && n < max; c = vm_read32(c + 0x08u)) out[n++] = c;
    return n;
}
void say_children(const char* when, uint32_t node)
{
    uint32_t c[24]; const int n = children(node, c, 24);
    std::printf("[modelpv] %s: node %08X has %d children:", when, node, n);
    for (int i = 0; i < n; i++) std::printf(" %08X/%08X", c[i], vm_read32(c[i]));
    std::printf("\n"); std::fflush(stdout);
}

const uint32_t kRegPtr = 0x0095BD9Cu, kRegBytes = 0xBE0u;
PPU_THREAD_LOCAL bool t_in_viewer = false;
volatile uint32_t g_scene = 0;          /* the Progress scene (seen at its per-frame call) */
uint32_t g_cs = 0; unsigned g_cs_ticks = 0; unsigned long long g_cs_ms = 0;
int g_phase = 0; unsigned g_phase_ticks = 0;
uint32_t g_loader = 0, g_fake_reg = 0, g_dummy = 0, g_viewer = 0, g_real_reg = 0;
volatile uint32_t g_scene_vt = 0;       /* our copy of the scene's vtable, once the scene draws through it */
/* The Gallery scene's own draw function, func_001BD9F4(scene, pass), does everything the viewer needs each pass (the 3D
 * passes, and at pass 7 the composite onto the screen, which is inline there). It reads from its scene only +0x58 (the
 * UI controller: vtable +0x40 (7) gives the window part, whose +0x38 is the picture behind the model), +0x60 (a state,
 * not 0 and not 7) and the viewer at +0xD0, and writes +0x324 / +0x328. So the viewer lives at +0xD0 of a stand-in
 * block, with a stand-in controller whose virtuals do nothing (the call then "returns" the controller itself, and its
 * +0x38 is where we put the background texture). */
uint32_t g_fscene = 0, g_ctrl = 0;
/* The Gallery scene's draw leaves the finished picture (the model on its background) at scene +0x328 (and +0x324); the
 * scene hands those four words at +0x320 to its window part every frame (func_001C0F8C(controller, 7, scene + 0x320) ->
 * part +0x68..0x77), and the part puts +0x70 on its pane p_chara_illust with func_00333FCC(pane, picture). With us the
 * picture goes on a pane of Character Select instead. The panes are learned from the game's own func_00333FCC calls
 * made from Character Select's code (return address 0x00185000..0x001A2000): pane, its name (pane +0x104), caller. */
/* The Gallery's window part itself (class vtable 0x0087CE40, 0x78 bytes: ctor func_001C1A78(part, parent node), init
 * vtable +0x08 func_001C1930(part, { scene, ALL_m, ALL_v, language _m, language _v }), update +0x10 func_001C2100,
 * animation step +0x14 func_002F94E0, the four picture words +0x1C func_001C1654(part, words), command +0x20
 * func_002FA0B8(part, 0 = come in), character func_001C1624(part, id); it loads the character's background by itself
 * through [registry]+0x5C ids 5 / 6 and keeps the texture at part +0x38). MODEL_PREVIEW_WINDOW=0: not made. */
uint32_t g_part = 0, g_part_args = 0;
bool window_on() { static const bool v = envi("MODEL_PREVIEW_WINDOW", 1) != 0; return v; }
struct SeenPane { uint32_t pane, lr, tex; char name[40]; };
SeenPane g_panes[64]; int g_npanes = 0;
uint32_t g_show_pane = 0, g_show_old = 0;
/* The grid of faces is two layouts of 94 panes, not parts with a visible flag. A layout draws from
 * its root pane ([layout + 0x10]; every top pane's +0x04 points at it); the root's children are the records of the
 * table at root +0xE8 (vtable 0x00882E60: +0x08 array of 12-byte records { pane A, pane B, option }, +0x0C count in the
 * high half), and a pane with +0x100 bit 0 clear is not drawn (the root itself has a pointer at +0x100). The roots are found from the grid's face panes (func_00333FCC calls from
 * 0x0018D9CC) by walking +0x04 up. Hiding = bit 0 off on every child of the roots, each tick; what was visible when
 * the hiding began gets the bit back when the window goes. MODEL_PREVIEW_HIDEGRID=0: the grid stays. */
uint32_t g_grid_root[4]; int g_ngrid = 0;
uint32_t g_grid_was[512]; int g_ngrid_was = 0; bool g_grid_hidden = false;
/* The Support badge of each chip (p_support_000 .. 003, the yellow ring) is on whenever the chip's cell holds a
 * Support: with the list on a Support slot every cell does, and eight badges stood over the window. They are top panes of the grid's two layouts and the tick does hide them,
 * but the game shows them again every frame AFTER the tick (all eight
 * were visible at the next tick), so they are hidden once more in the draw phase: at every part draw of Character Select
 * (the func_002F94E0 hook). They are taken from the layouts' flat pane lists ([layout +0x14]: +0x08 an array of pane
 * pointers, +0x0C the count); the grid's layouts are learned from its own pane lookups (func_0032B230
 * called from 0x0018B000 .. 0x0018F000, model_preview_find). */
uint32_t g_grid_layout[4]; int g_ngrid_layout = 0;
void grid_hide_named()
{
    for (int i = 0; i < g_ngrid_layout; i++) {
        const uint32_t list = vm_read32(g_grid_layout[i] + 0x14u); if (!ok(list)) continue;
        const uint32_t arr = vm_read32(list + 0x08u), n = vm_read32(list + 0x0Cu); if (!ok(arr)) continue;
        for (uint32_t q = 0; q < n && q < 600u; q++) {
            const uint32_t pn = vm_read32(arr + q * 4u); if (!ok(pn)) continue;
            const uint32_t np = vm_read32(pn + 0x104u), v = vm_read32(pn + 0x100u);
            if (!(v & 1u) || !ok(np) || (std::memcmp(vm_base + np, "p_support_", 10) && std::memcmp(vm_base + np, "_a_support_base", 15))) continue;   /* the badge pane, its ring (_a_support_base_000) and its picture (p_support_tx): with p_support_00N alone hidden the badges stayed, with all three they are gone */
            bool have = false; for (int k = 0; k < g_ngrid_was && !have; k++) have = g_grid_was[k] == pn;
            if (!have && g_ngrid_was < 512) { g_grid_was[g_ngrid_was++] = pn; if (logon()) say("a Support badge pane %08X '%.20s' (layout %08X) is hidden", pn, (const char*)(vm_base + np), g_grid_layout[i]); }
            vm_write32(pn + 0x100u, v & ~1u);
        }
    }
}
/* The roots when no face picture named them: Character Select can be built again without the face cells getting new
 * pictures (Re-select Characters from the pause menu, the screen after a 3v3 battle; a player's log: "the grid is
 * hidden (0 roots, 4 panes ...)" and the grid stayed under the viewer). The grid's layouts are known by then from its
 * pane lookups, and a layout draws from its root pane [layout + 0x10] (the comment above). A root is taken only when it
 * has the child table the hiding walks (vtable 0x00882E60 at +0xE8). Called on every tick, also when face pictures
 * named some roots already: a player's log (Arcade 2 Players, a battle quit, back on Character Select) had
 * "the grid is hidden (1 roots, 7 panes ...)": only one layer's faces got new pictures, the other layer stayed under
 * the viewer showing the list's cells. The roots named by pictures were each layout's [layout + 0x10] in every visit
 * of that log (layout + 0x11FB0 both times), so a layout's root is the same pane. Returns how many roots there are. */
int grid_roots_from_layouts()
{
    for (int i = 0; i < g_ngrid_layout && g_ngrid < 4; i++) {
        const uint32_t r = vm_read32(g_grid_layout[i] + 0x10u); if (!ok(r)) continue;
        const uint32_t tb = vm_read32(r + 0xE8u); if (!ok(tb) || vm_read32(tb) != 0x00882E60u) continue;
        bool have = false; for (int k = 0; k < g_ngrid; k++) have |= g_grid_root[k] == r;
        if (!have) { g_grid_root[g_ngrid++] = r; say("the grid's root pane %d: %08X, from layout %08X (no face picture named it)", g_ngrid, r, g_grid_layout[i]); }
    }
    return g_ngrid;
}
void grid_show(bool on)
{
    if (on) {
        if (!g_grid_hidden) return;
        for (int i = 0; i < g_ngrid_was; i++) { const uint32_t pn = g_grid_was[i]; if (ok(pn)) vm_write32(pn + 0x100u, vm_read32(pn + 0x100u) | 1u); }
        g_ngrid_was = 0; g_grid_hidden = false; return;
    }
    for (int i = 0; i < g_ngrid; i++) {
        const uint32_t r = g_grid_root[i]; if (!ok(r)) continue;
        const uint32_t tb = vm_read32(r + 0xE8u); if (!ok(tb) || vm_read32(tb) != 0x00882E60u) continue;
        const uint32_t arr = vm_read32(tb + 0x08u), n = vm_read32(tb + 0x0Cu) >> 16; if (!ok(arr)) continue;
        for (uint32_t q = 0; q < n && q < 512u; q++) for (uint32_t c = 0; c < 2; c++) {
            const uint32_t pn = vm_read32(arr + q * 12u + c * 4u); if (!ok(pn)) continue;
            const uint32_t v = vm_read32(pn + 0x100u);
            if (v & 1u) { if (!g_grid_hidden || true) { bool have = false; for (int k = 0; k < g_ngrid_was && !have; k++) have = g_grid_was[k] == pn; if (!have && g_ngrid_was < 512) g_grid_was[g_ngrid_was++] = pn; }
                vm_write32(pn + 0x100u, v & ~1u); }
        }
    }
    grid_hide_named();
    static int said_roots = -1;
    if (!g_grid_hidden || said_roots != g_ngrid) { g_grid_hidden = true; said_roots = g_ngrid; say("the grid is hidden (%d roots, %d panes were visible)", g_ngrid, g_ngrid_was); }
}

/* run a viewer function with our registry copy in place */
uint32_t viewer_call(void (*fn)(ppu_context*), const ppu_context* ctx, uint64_t a, uint64_t b = 0)
{
    const uint32_t real = vm_read32(kRegPtr);
    /* the copy follows the real registry except for the three loaders */
    std::memcpy(vm_base + g_fake_reg, vm_base + real, kRegBytes);
    vm_write32(g_fake_reg + 0x5Cu, g_loader); vm_write32(g_fake_reg + 0x60u, g_dummy); vm_write32(g_fake_reg + 0x64u, g_dummy);
    vm_write32(kRegPtr, g_fake_reg);
    t_in_viewer = true;
    const uint32_t r = call(fn, ctx, a, b);
    t_in_viewer = false;
    vm_write32(kRegPtr, real);
    /* nothing is copied back: the registry's own words are pointers the viewer only reads (a change would show here) */
    if (logon()) { static unsigned said = 0; uint32_t diff = 0; for (uint32_t o = 0; o < kRegBytes; o += 4) if (o != 0x5Cu && o != 0x60u && o != 0x64u && vm_read32(real + o) != vm_read32(g_fake_reg + o)) diff++;
        if (diff && said++ < 10) say("the registry copy differs from the registry in %u words after a viewer call", diff); }
    return r;
}

void fail(const char* why) { g_phase = -1; say("stopped: %s", why); }
bool late() { static const bool v = envi("MODEL_PREVIEW_LATE", 1) != 0; return v; }

/* Pass 7 with a model shown: the offscreen picture is put on the screen (the Gallery scene has that code inline in its
 * +0x38, func_001BD9F4; func_001B683C is the viewer's own copy: bit 8 of MODEL_PREVIEW_DRAW), then the viewer's passes
 * 3 and 5 are drawn. viewer +0xAC is the picture behind the model: the Gallery scene sets it every frame from its
 * window part ([part + 0x38] = texture 0 of the character's background archive, func_001C1B34). Test
 * (MODEL_PREVIEW_BG=<n>, default 0): texture n-1 of the viewer's own archive (+0x13C, or +0x140 with
 * MODEL_PREVIEW_BGARC=1). */
void composite(const ppu_context* c, int draw)
{
    if (vm_read32(g_viewer) != 2u) return;
    static const int bg = envi("MODEL_PREVIEW_BG", 1), bgarc = envi("MODEL_PREVIEW_BGARC", 0);
    if (bg > 0 && !g_part && !vm_read32(g_viewer + 0xACu)) {
        const uint32_t arc = vm_read32(g_viewer + (bgarc ? 0x140u : 0x13Cu));
        const uint32_t tex = ok(arc) ? call(func_0051B660, c, arc, (uint64_t)(bg - 1)) : 0u;
        static unsigned said = 0; if (said++ < 4) say("background test: archive %08X texture %d -> %08X", arc, bg - 1, tex);
        if (ok(tex)) vm_write32(g_viewer + 0xACu, tex);
    }
    if (g_ctrl && !g_part && !vm_read32(g_ctrl + 0x38u)) vm_write32(g_ctrl + 0x38u, vm_read32(g_viewer + 0xACu));
    if (draw & 8) call(func_001B683C, c, g_viewer);
    if (draw & 1) { call(func_001B99A4, c, g_viewer, 3); call(func_001B99A4, c, g_viewer, 5); }
}

/* the scene gets a vtable copy whose +0x18 / +0x38 / +0x3C are three do-nothing functions of the game (func_0071C030 /
 * func_0071C040 / func_0071C044), each through a descriptor of ours; model_preview_icall sees them called */
void scene_draw_on(const ppu_context* ctx)
{
    const uint32_t scene = g_scene, vt = vm_read32(scene);
    if (vt != 0x0087AC78u) { say("the scene's vtable is %08X, not 0087AC78: no draw calls", vt); return; }
    static uint32_t s_blk = 0, s_desc = 0;
    const uint32_t blk = keep(ctx, s_blk, 0x140u), desc = keep(ctx, s_desc, 0x20u);
    if (!blk || !desc) return;
    std::memcpy(vm_base + blk, vm_base + vt - 0x10u, 0x130u);
    const uint32_t nvt = blk + 0x10u, toc = (uint32_t)ctx->gpr[2];
    static const uint32_t fn[3] = { 0x0071C030u, 0x0071C040u, 0x0071C044u }, slot[3] = { 0x18u, 0x38u, 0x3Cu };
    for (uint32_t i = 0; i < 3; i++) { vm_write32(desc + i * 8u, fn[i]); vm_write32(desc + i * 8u + 4u, toc); vm_write32(nvt + slot[i], desc + i * 8u); }
    g_scene_vt = nvt;
    vm_write32(scene, nvt);
    const uint32_t fl = vm_read32(scene + 0x20u);
    vm_write32(scene + 0x20u, fl & ~0x3010u);
    say("the scene %08X draws through the vtable copy %08X now (node flags %08X -> %08X)", scene, nvt, fl, fl & ~0x3010u);
}

/* MODEL_PREVIEW_PANE=<name>[:n] (default p_rgb8_256_chara_ico_000): the n-th pane of that name seen (from 0) shows the
 * model's picture while a model is shown; its own picture comes back when the model goes */
void show_picture()
{
    static char want[40]; static int nth = -1;
    if (nth < 0) { const char* e = std::getenv("MODEL_PREVIEW_PANE"); std::snprintf(want, sizeof want, "%s", e && *e ? e : "p_rgb8_256_chara_ico_000");
        nth = 0; if (char* c = std::strchr(want, ':')) { *c = 0; nth = std::atoi(c + 1); } }
    if (!g_fscene) return;
    const uint32_t pic = vm_read32(g_fscene + 0x328u), shown = vm_read32(g_viewer) == 2u && ok(pic);
    { static uint32_t last[2] = { 1, 1 }; const uint32_t now[2] = { vm_read32(g_fscene + 0x324u), pic };
      if (now[0] != last[0] || now[1] != last[1]) { last[0] = now[0]; last[1] = now[1]; say("the stand-in scene's picture words: +0x324 %08X, +0x328 %08X (viewer state %d)", now[0], now[1], (int32_t)vm_read32(g_viewer)); } }
    if (!g_show_pane) { int k = 0; for (int i = 0; i < g_npanes; i++) if (!std::strcmp(g_panes[i].name, want) && k++ == nth) { g_show_pane = g_panes[i].pane; break; }
        if (g_show_pane) say("the model's picture goes on pane %08X '%s' (number %d of that name)", g_show_pane, want, nth); }
    if (!g_show_pane || !ok(g_show_pane)) return;
    if (shown) {
        if (vm_read32(g_show_pane + 0x14u) != pic) { if (!g_show_old) g_show_old = vm_read32(g_show_pane + 0x14u);
            vm_write32(g_show_pane + 0x14u, pic); vm_write32(g_show_pane + 0x10u, vm_read32(g_show_pane + 0x10u) | 1u); }
    } else if (g_show_old && vm_read32(g_show_pane + 0x14u) != g_show_old) { vm_write32(g_show_pane + 0x14u, g_show_old); g_show_old = 0; }
}

/* the carousel's state (car_tick, below) */
volatile bool g_support_grid = false;   /* Player 1 is on the grid for a Support slot: the game's own grid is shown, our window is not */
bool g_out = false;                    /* the window part was told to go (its "out" animation), with Character Select's own parts */
bool g_title_gone = false;             /* ... and the title part has finished going: the window is not drawn any more */
uint32_t g_cs_parts[48]; int g_ncs_parts = 0;   /* Character Select's parts as they draw (func_002F94E0), relearned at each visit */
uint32_t g_cs_part_state[48];
int g_pose_char = -1, g_pose_idx = 0;  /* the fighter whose pose number is held, and the number (0..3) */
unsigned g_load_ticks = 0;             /* ticks with the viewer loading or unloading (state 1 or 3), for the load time line */
bool g_no_preview = false;             /* this visit's Character Select is one the preview is not made on (the grid stays) */
unsigned g_close_ticks = 0;            /* frames spent unloading the model before the take-down */
uint32_t g_seq0 = 0;                   /* jsp_cs_state_seq() when the last visit was taken down (0 at the start) */
unsigned g_cs_miss = 0;                /* frames of the Progress scene since Character Select's last own frame */
volatile bool g_logo = true;            /* the window shows its loading logo (no fighter to show, or not loaded yet) */
bool g_random_text = false;             /* "Random" is on the overlay layer */
void random_text(bool on);

/* The Gallery's packs on our own loader (ids 0..4; 5 and 6 are the character's background, which the window part
 * asks for itself). They have their own state: asked for as soon as a menu runs on heap layout
 * 3 (the Progress scene's own frame, MODEL_PREVIEW_PRELOAD=1, default), so that the window can be made in Character
 * Select's first frame and come in with the screen (so that the Model Viewer slides in with the character
 * select menu); given back when the heap layout is about to change (model_preview_layout_enter), because the heaps they
 * sit in are rebuilt then. With MODEL_PREVIEW_PRELOAD=0 they are asked for when Character Select opens and given back
 * with the take-down, as before. */
int g_packs = 0;   /* 0 not asked for, 1 asked for, 2 loaded, -1 loaded without data */
const char* g_soft_why = "";   /* why packs_ask said "not now" (another heap layout, heaps not built) */
int g_np_chr = -1, g_np_state = 0;   /* the name pack: whose, and 0 none, 1 loading, 2 loaded, 3 did not come */
unsigned g_packs_ticks = 0;
bool preload() { static const bool v = envi("MODEL_PREVIEW_PRELOAD", 1) != 0; return v; }
void fail(const char* why);
bool packs_ask(const ppu_context* ctx, bool strict)
{
    auto bad = [&](const char* why) { g_soft_why = why; if (strict) fail(why); return false; };
    {
        const uint32_t hm = vm_read32(0x0095BE70u);
        if (!ok(hm) || vm_read32(hm + 0x264u) != 3u) return bad("the heap layout in use is not 3");
        if (!vm_read32(hm + (3u * kOurGroup + 0u) * 4u) || !vm_read32(hm + (3u * 0x10u + 0u) * 4u)) return bad("the heaps of our groups are not built");
        if (!g_loader) {   /* made once; its blocks are given back when the preview is taken down */
            /* 0x200 bytes: the game's scene1 loader, in whose place this one stands, has its own words after the
             * base loader's (its table starts at +0x1D0), and its functions write them: func_00435378 keeps the
             * background's character at +0xA4, which in a 0x60-byte object was our table's entry 4 (the 15.7 MB
             * language pack's block word read 0x1C = fighter 28) */
            g_loader = gnew(ctx, 0x200u); if (!g_loader) { fail("no memory for the loader"); return false; }
            call(func_00424880, ctx, g_loader, 16);
        }
        for (uint32_t o = 0x18u; o < 0x1D0u; o += 4) vm_write32(g_loader + o, 0xFFFFFFFFu);   /* as the game's own reads on the menus */
        vm_write32(g_loader + 0x14u, kOurGroup);
        /* the Gallery's own seven blocks on its scene1 loader: ids 0 ALL_m, 1 ALL_v,
         * 2 ALL_i, 3 language _m, 4 language _v, 5 / 6 the character's background _m / _v */
        static const uint32_t size[7] = { 0xCB800u, 0x19B800u, 0x2000u, 0x8800u, 0xFAB000u, 0x800u, 0x29000u };
        static const uint32_t kind[7] = { 0, 1, 2, 0, 1, 0, 1 };
        static const uint32_t path[3] = { 0x007D6138u, 0x007D6178u, 0x007D61B8u };   /* progress/gallery_zukan/progress_scene_gallery_zukan_ALL_m / _v / _i .pak */
        const uint32_t tab = vm_read32(g_loader + 4u);
        for (uint32_t id = 0; id < 7; id++) {
            call(func_004247CC, ctx, g_loader, kind[id], id, size[id], kind[id] ? 0x80u : 0x10u);
            if (!ok(tab) || !vm_read32(tab + id * 8u + 4u)) { fail("a block could not be reserved"); return false; }
        }
        if (namepic()) {   /* ids 8 / 9: one name pack */
            call(func_004247CC, ctx, g_loader, 0, 8, 0x800u, 0x10u); call(func_004247CC, ctx, g_loader, 1, 9, 0x2800u, 0x80u);
            say("name pack blocks: %08X %08X", vm_read32(tab + 8u * 8u + 4u), vm_read32(tab + 9u * 8u + 4u)); }
        if (logon()) { std::printf("[modelpv] blocks after the reserve:"); for (uint32_t id = 0; id < 7; id++) std::printf(" %08X", vm_read32(tab + id * 8u + 4u)); std::printf("\n"); }
        for (uint32_t id = 0; id < 3; id++) call(func_00424618, ctx, g_loader, id, path[id]);
        {   /* the two language packs: the game's formats 0x007D61F8 / 0x007D6238 ("..._%s_m.pak" / "..._%s_v.pak") with its language string */
            static uint32_t s_buf = 0; const uint32_t lp = call(func_0043F448, ctx, call(func_0043F41C, ctx)), buf = keep(ctx, s_buf, 0x200u);
            char lang[16]; lang[0] = 0; if (ok(lp)) { int k = 0; for (; k < 15; k++) { const char c = (char)vm_base[lp + (uint32_t)k]; if (c < 33 || c > 126) break; lang[k] = c; } lang[k] = 0; }
            if (!lang[0] || !buf) { fail("the language string was not found"); return false; }
            std::snprintf(g_lang, sizeof g_lang, "%s", lang);
            for (uint32_t k = 0; k < 2; k++) {
                char fmt[96], path2[160]; int q = 0; const uint32_t fp = k ? 0x007D6238u : 0x007D61F8u;
                for (; q < 95; q++) { fmt[q] = (char)vm_base[fp + (uint32_t)q]; if (!fmt[q]) break; } fmt[q] = 0;
                std::snprintf(path2, sizeof path2, fmt, lang);
                const uint32_t at = buf + k * 0x100u; for (q = 0; path2[q] && q < 0xFF; q++) vm_base[at + (uint32_t)q] = (uint8_t)path2[q]; vm_base[at + (uint32_t)q] = 0;
                call(func_00424618, ctx, g_loader, 3u + k, at);
                if (k == 0) say("language '%s': %s", lang, path2);
                if (logon()) say("after asking for id %u: its block word %08X", 3u + k, vm_read32(tab + (3u + k) * 8u + 4u));
            }
        }
        say("loader %08X (vtable %08X, table %08X, %u ids): blocks %08X %08X %08X, the three Gallery packs asked for", g_loader, vm_read32(g_loader), tab,
            vm_read32(g_loader + 8u) & 0x7FFFFFFFu, vm_read32(tab + 4u), vm_read32(tab + 12u), vm_read32(tab + 20u));
    }
    g_packs = 1; g_packs_ticks = 0;
    return true;
}
bool packs_check(const ppu_context* ctx)
{
    if (g_packs == 2) return true;
    if (g_packs != 1 || !g_loader) return false;
    g_packs_ticks++;
    bool all = true; for (uint32_t id = 0; id < 5; id++) all &= call(func_00424230, ctx, g_loader, id) == 1u;
    if (!all) return false;
    const uint32_t m = call(func_00435A44, ctx, g_loader, 0), v = call(func_00435A44, ctx, g_loader, 1), i = call(func_00435A44, ctx, g_loader, 2);
    say("the packs are loaded after %u ticks: data %08X %08X %08X (first words %08X %08X %08X)", g_packs_ticks, m, v, i,
        ok(m) ? vm_read32(m) : 0u, v >= 0x10000u ? vm_read32(v) : 0u, ok(i) ? vm_read32(i) : 0u);
    if (logon()) { const uint32_t tab = vm_read32(g_loader + 4u); std::printf("[modelpv] blocks when loaded:"); for (uint32_t id = 0; id < 7; id++) std::printf(" %08X", vm_read32(tab + id * 8u + 4u)); std::printf("\n"); }
    g_packs = ok(m) ? 2 : -1;
    return g_packs == 2;
}
void packs_release(const ppu_context* ctx)
{
    g_np_chr = -1; g_np_state = 0;
    if (g_loader) for (uint32_t id = 0; id < 10; id++) {
        const uint32_t tab = vm_read32(g_loader + 4u);
        if (!ok(tab)) break;
        const uint32_t blk = vm_read32(tab + id * 8u + 4u), res = vm_read32(tab + id * 8u);
        if (logon()) say("  id %u: resource %08X block %08X (block: %08X %08X %08X heap %08X)", id, res, blk, ok(blk) ? vm_read32(blk) : 0u, ok(blk) ? vm_read32(blk + 4u) : 0u, ok(blk) ? vm_read32(blk + 8u) : 0u, ok(blk) ? vm_read32(blk + 0xCu) : 0u);
        if (!ok(blk)) continue;
        call(func_004241F8, ctx, g_loader, id);
        if (ok(vm_read32(blk + 0xCu))) call(func_00424654, ctx, g_loader, id); else vm_write32(tab + id * 8u + 4u, 0u);
    }
    if (g_packs) say("the packs are given back");
    g_packs = 0;
}

const uint32_t kGrid = 0x009541F4u, kGridRows = 0x009541F0u;
volatile bool g_car_on = false;
std::atomic<int> g_car_step{0};
int g_car_list[72]; int g_car_n = 0, g_car_at = 0, g_car_asked = -2, g_car_rows = 0;
uint32_t g_car_saved[64];
unsigned long long g_car_changed = 0;

/* Character Select has gone (its per-frame call stopped): everything of the preview is taken down, from the Progress
 * scene's own per-frame call. The part goes through its deleting destructor (vtable +0x04, func_001C16DC), the viewer
 * through func_001B4FEC (the Gallery scene's destructors call it on their member), both with the registry copy in
 * place; the scene gets its own vtable back; our loader's seven packs are unloaded (func_004241F8) and their blocks
 * given back (func_00424654), because the heaps they sit in are rebuilt whenever the heap layout changes (a fight).
 * Character Select's panes and parts are not touched: they went with the screen. */
void teardown(const ppu_context* ctx)
{
    say("Character Select is gone: the preview is taken down (phase %d)", g_phase);
    if (g_car_on) { for (int i = 0; i < g_car_rows * 4 && i < 64; i++) vm_write32(kGrid + (uint32_t)i * 4u, g_car_saved[i]); g_car_on = false; }
    g_car_asked = -2; g_support_grid = false; g_logo = true; random_text(false);
    const uint32_t scene = g_scene;
    if (logon()) say_children("before the take-down", scene);
    if (g_part) {
        /* The part's own node (part +0x44, a child of the scene) and what hangs under it, the background's archive
         * node (vtable 0x0087CEA8: its destructor frees the archive and clears the "a background is there" word at
         * 0x00955D5C), belong to the node tree: the Gallery leaves them to its scene's end. Here the scene lives on,
         * so they are deleted by hand (their +0x04 virtual), children first; then the part (func_001C16DC). */
        const uint32_t node = vm_read32(g_part + 0x44u);
        uint32_t c[8]; const int n = children(node, c, 8);
        if (t_in_viewer == false) { const uint32_t real = vm_read32(kRegPtr); std::memcpy(vm_base + g_fake_reg, vm_base + real, kRegBytes);
            vm_write32(g_fake_reg + 0x5Cu, g_loader); vm_write32(g_fake_reg + 0x60u, g_dummy); vm_write32(g_fake_reg + 0x64u, g_dummy);
            vm_write32(kRegPtr, g_fake_reg); t_in_viewer = true;
            for (int i = 0; i < n; i++) vcall(ctx, c[i], 4);
            if (ok(node)) vcall(ctx, node, 4);
            t_in_viewer = false; vm_write32(kRegPtr, real); }
        say("the part's node %08X and its %d children are deleted (background word now %u)", node, n, vm_read32(0x00955D5Cu));
        viewer_call(func_001C16DC, ctx, g_part); g_part = 0;
    }
    if (g_viewer && g_phase >= 3) {
        /* the viewer's layout root (viewer +0x148, vtable 0x008832B8) is a node under the scene too, and the viewer's
         * destructor leaves it there (it was still a child afterwards): deleted by hand */
        const uint32_t lroot = vm_read32(g_viewer + 0x148u);
        viewer_call(func_001B4FEC, ctx, g_viewer);
        uint32_t c[24]; const int n = children(scene, c, 24); bool there = false; for (int i = 0; i < n; i++) there |= c[i] == lroot;
        if (there && ok(lroot) && vm_read32(lroot) == 0x008832B8u) { vcall(ctx, lroot, 4); say("the viewer's layout root %08X is deleted", lroot); }
    }
    g_viewer = 0;
    if (logon()) say_children("after the take-down", scene);
    if (g_scene_vt && ok(scene) && vm_read32(scene) == g_scene_vt) vm_write32(scene, 0x0087AC78u);
    g_scene_vt = 0;
    if (!preload()) packs_release(ctx);   /* preloaded packs stay until the heap layout changes */
    g_out = false; g_title_gone = false; g_ncs_parts = 0; g_cs_miss = 0; g_pose_char = -1; g_seq0 = jsp_cs_state_seq(); g_close_ticks = 0;
    g_npanes = 0; g_show_pane = 0; g_show_old = 0; g_ngrid = 0; g_ngrid_was = 0; g_grid_hidden = false; g_ngrid_layout = 0;
    g_cs = 0; g_cs_ticks = 0; g_phase = 0; g_phase_ticks = 0;
    say("taken down");
}

/* The carousel (Left and Right change the shown model to the next character: it starts on Random, and going right shows the first
 * character in the list; choosing a character locks its picture into the character slot).
 * While Player 1 is on the grid for a fighter slot (player state 8, or 7 while the grid comes in; the slot from
 * func_00185BE0(obj, 0, 2, player)), the grid is hidden, the window shows the candidate, and Left / Right walk our own
 * list: Random first, then the fighters in the grid's order (the grid table at 0x009541F4, 4 ids a row, row count at
 * 0x009541F0; Supports 200..213, locked chips 0x1F5 and empty cells left out). The game's own cursor is not moved
 * (the D-pad, the stick, L1 and R1 are kept from the game meanwhile); instead EVERY cell of the grid table holds the
 * candidate while the carousel is on, so the game's own pick on Cross (func_00188438 reads the cell under its cursor)
 * takes the candidate, with all its own rules. The table is put back when the grid is left.
 * MODEL_PREVIEW_MODE=0: the first test instead (a fixed fighter, the window always up). */
bool car_mode() { static const bool v = envi("MODEL_PREVIEW_MODE", 1) != 0; return v; }
int car_candidate() { return g_car_on && g_car_n ? g_car_list[g_car_at] : -1; }
/* the window is drawn: in the first test always; in the carousel while a fighter (not Random) is the candidate */
bool window_up() { if (!g_part || g_phase < 3 || g_title_gone || g_close_ticks) return false; return car_mode() ? !g_support_grid : true; }

/* Colours (a button swaps a character's colours, so colour palettes can be added and players can
 * change between them at Character Select).
 * A character can have other colours besides its own: jsp_game.cpp says how many and their names (jsp_alt_count,
 * jsp_alt_name; a build without them has none), recolours the character's textures as the game first uses them, and
 * asks model_preview_alt(character) which colour is wanted.
 *  - On the list Triangle goes to the shown character's next colour (kept from the game meanwhile); the model is
 *    loaded again, because the recolour is done on fresh textures.
 *  - A pick remembers the colour with the slot (g_slot_alt); a slot's picked fighter shows in its colour.
 *  - For the fight: a character is recoloured when a slot holds it picked with another colour (g_fight_alt, worked out
 *    every tick of Character Select and left standing when the screen goes). The recolour cannot tell two of the same
 *    character apart: both get it.
 * MODEL_PREVIEW_ALT=0: no colours. */
bool alt_on() { static const bool v = envi("MODEL_PREVIEW_ALT", 1) != 0 && jsp_alt_count != nullptr; return v; }   /* a build without colour data: Triangle stays the game's */
int alt_count(int chr) { const int n = alt_on() && jsp_alt_count ? jsp_alt_count(chr) : 1; return n < 1 ? 1 : n > 8 ? 8 : n; }
const char* alt_name(int chr, int alt) { const char* n = jsp_alt_name ? jsp_alt_name(chr, alt) : nullptr; return n ? n : ""; }
std::atomic<int> g_tri{0};              /* Triangle presses seen by the pad hook */
volatile int g_alt[256];                /* the colour last shown for a character on the list */
volatile int g_slot_alt[6];             /* the colour a slot's fighter was picked in */
volatile int g_fight_alt[256];          /* the colour a character gets in the fight */
volatile int g_asked_alt_chr = -1, g_asked_alt = 0;   /* what the viewer's model was last asked in */
int g_car_slot = -1;                    /* the slot the list is up for */

void car_end()
{
    if (!g_car_on) return;
    for (int i = 0; i < g_car_rows * 4 && i < 64; i++) vm_write32(kGrid + (uint32_t)i * 4u, g_car_saved[i]);
    g_car_on = false;
    say("carousel off (the grid table is put back)");
}
/* What the port writes over the window (the overlay layer, toolkit 2517; one retained layer, so everything is drawn
 * in one go and only when it changes). Places are in the 1280x720 layout: the window's picture is x 680..1280,
 * y 130..553, its bottom black bar y 553..590.
 *  - "Random" under the logo (the loading state shows only this text)
 *  - the fighter's name on the bottom bar with an arrow on each side (so the name is easy to read
 *    and the player sees that Left and Right change the character); the arrows show while Left /
 *    Right work (the list is up)
 *  - "Pose 2 of 3" in the picture's top left corner while Square still changes the pose (the step in which each fighter's pose is picked;
 *    MODEL_PREVIEW_SQUARE=1) */
struct Ovl { int random, arrows, pose, poses, pic, alt, alts; char name[48]; char altname[16]; };   /* pic: character + 1 whose name picture (g_np_rgba) is drawn in place of the text */
Ovl g_ovl_now; bool g_ovl_up = false;
/* the decoded name picture (namepic_tick): straight RGBA, and the columns its letters take */
uint8_t g_np_rgba[512 * 64 * 4]; int g_np_w = 0, g_np_h = 0, g_np_x0 = 0, g_np_x1 = 0, g_np_y0 = 0, g_np_y1 = 0;
/* the picture, scaled by sc with two-way blending, its letters' middle at (cx, cy) */
void ovl_picture(float cx, float cy, float sc)
{
    static std::vector<uint8_t> buf;
    const int sw = g_np_x1 - g_np_x0, sh = g_np_y1 - g_np_y0; if (sw <= 0 || sh <= 0) return;
    const int dw = (int)((float)sw * sc + 0.5f), dh = (int)((float)sh * sc + 0.5f); if (dw <= 0 || dh <= 0 || dw > 4096 || dh > 1024) return;
    buf.resize((size_t)dw * (size_t)dh * 4u);
    for (int y = 0; y < dh; y++) { float fy = ((float)y + 0.5f) / sc - 0.5f; if (fy < 0) fy = 0; int y0 = (int)fy; if (y0 > sh - 1) y0 = sh - 1; const int y1 = y0 + 1 < sh ? y0 + 1 : y0; const float ty = fy - (float)y0;
        for (int x = 0; x < dw; x++) { float fx = ((float)x + 0.5f) / sc - 0.5f; if (fx < 0) fx = 0; int x0 = (int)fx; if (x0 > sw - 1) x0 = sw - 1; const int x1 = x0 + 1 < sw ? x0 + 1 : x0; const float tx = fx - (float)x0;
            const uint8_t* a = g_np_rgba + ((size_t)(g_np_y0 + y0) * (size_t)g_np_w + (size_t)(g_np_x0 + x0)) * 4u, *b = g_np_rgba + ((size_t)(g_np_y0 + y0) * (size_t)g_np_w + (size_t)(g_np_x0 + x1)) * 4u;
            const uint8_t* c = g_np_rgba + ((size_t)(g_np_y0 + y1) * (size_t)g_np_w + (size_t)(g_np_x0 + x0)) * 4u, *d = g_np_rgba + ((size_t)(g_np_y0 + y1) * (size_t)g_np_w + (size_t)(g_np_x0 + x1)) * 4u;
            /* blend with the colours weighted by their alpha, so that clear pixels do not grey the letters' edges */
            const float wa = (1 - tx) * (1 - ty) * a[3], wb = tx * (1 - ty) * b[3], wc = (1 - tx) * ty * c[3], wd = tx * ty * d[3], wsum = wa + wb + wc + wd;
            uint8_t* o = buf.data() + ((size_t)y * (size_t)dw + (size_t)x) * 4u;
            if (wsum <= 0.0f) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
            for (int k = 0; k < 3; k++) o[k] = (uint8_t)((wa * a[k] + wb * b[k] + wc * c[k] + wd * d[k]) / wsum + 0.5f);
            o[3] = (uint8_t)(wsum + 0.5f); } }
    jsp_overlay_blit_rgba((int)(cx - (float)dw * 0.5f), (int)(cy - (float)dh * 0.5f), dw, dh, buf.data(), dw * 4);
}
void ovl_text(float x, float y, const char* t, float sc, float k, uint32_t rgba)
{
    for (int dy = -2; dy <= 2; dy += 2) for (int dx = -2; dx <= 2; dx += 2) if (dx || dy) jsp_overlay_text_style(x + (float)dx * k, y + (float)dy * k, t, sc, JSP_RGBA(0, 0, 0, 255), 0);
    jsp_overlay_text_style(x, y, t, sc, rgba, 0);
}
/* a triangle pointing left (dir -1) or right (+1): tip at (x, y), h rows above and below, drawn as rows */
void ovl_arrow(float x, float y, int dir, float h, uint32_t rgba)
{
    const int n = (int)(h + 0.5f), full = (int)((float)n * 1.3f) + 1;   /* the middle row is the longest and ends at the tip */
    for (int i = -n; i <= n; i++) {
        const int w = (int)((float)(n - (i < 0 ? -i : i)) * 1.3f) + 1;
        const int x0 = dir < 0 ? (int)x + full - w : (int)x - full;
        jsp_overlay_fill_rect(x0, (int)y + i, w, 1, rgba);
    }
}
void overlay_show(const Ovl& w)
{
    const bool any = w.random || w.arrows || w.name[0] || w.poses > 1 || w.pic || w.alts > 1;
    /* another user has the layer: its begin() has replaced our drawing, so nothing of ours is up, nothing is drawn and the layer is
     * not ours to close; the texts come back with the first call after it is free */
    if (!layer_free()) { g_ovl_up = false; g_random_text = false; return; }
    if (!any) { if (g_ovl_up) { jsp_overlay_close(); g_ovl_up = false; } g_random_text = false; return; }
    if (g_ovl_up && !jsp_overlay_active()) g_ovl_up = false;   /* someone closed the layer meanwhile: draw again */
    if (g_ovl_up && !std::memcmp(&w, &g_ovl_now, sizeof w)) return;
    int ow = 0, oh = 0; jsp_overlay_output_size(&ow, &oh);
    if (ow <= 0 || oh <= 0 || !jsp_overlay_begin(0, 0)) { g_ovl_up = false; return; }
    const float k = (float)oh / 720.0f, ox = ((float)ow - 1280.0f * k) * 0.5f;
    if (w.random) { const float sc = 2.2f * k; const char* t = "Random"; ovl_text(ox + 968.0f * k - jsp_overlay_text_width(t, sc) * 0.5f, 452.0f * k, t, sc, k, JSP_RGBA(255, 255, 255, 255)); }
    if (w.pic) { static const float ps = (float)envi("MODEL_PREVIEW_NAMEPIC_SCALE", 120) / 100.0f; ovl_picture(ox + 968.0f * k, 572.0f * k, ps * k); }
    else if (w.name[0]) { const float sc = 1.0f * k; ovl_text(ox + 968.0f * k - jsp_overlay_text_width(w.name, sc) * 0.5f, 561.0f * k, w.name, sc, k, JSP_RGBA(255, 255, 255, 255)); }
    if (w.arrows) {
        for (int pass = 0; pass < 2; pass++) {   /* a black one a little larger behind a yellow one */
            const float h = (pass ? 11.0f : 14.0f) * k, in = (pass ? 0.0f : -3.0f) * k; const uint32_t c = pass ? JSP_RGBA(255, 214, 0, 255) : JSP_RGBA(0, 0, 0, 255);
            ovl_arrow(ox + 712.0f * k + in, 572.0f * k, -1, h, c);
            ovl_arrow(ox + 1226.0f * k - in, 572.0f * k, +1, h, c);
        }
    }
    if (w.alts > 1) { char t[64]; std::snprintf(t, sizeof t, "Triangle: colour %d of %d (%s)", w.alt, w.alts, w.altname); ovl_text(ox + 700.0f * k, 525.0f * k, t, 0.7f * k, k, JSP_RGBA(255, 255, 255, 255)); }
    if (w.poses > 1) { char t[48]; std::snprintf(t, sizeof t, "Pose %d of %d   (Square: next)", w.pose, w.poses); ovl_text(ox + 700.0f * k, 142.0f * k, t, 0.75f * k, k, JSP_RGBA(255, 255, 255, 255)); }
    jsp_overlay_end();
    g_ovl_now = w; g_ovl_up = true; g_random_text = w.random != 0;
}
void random_text(bool on) { if (!on) { Ovl e; std::memset(&e, 0, sizeof e); overlay_show(e); } }   /* the take-down's "all text off" */

/* The game's own name of a fighter: func_0033A1A0(func_00339E18(), id) (the
 * string the slot plates show). Measured: two bytes a letter, low byte first,
 * after the mark FF FE ("FF FE 4B 00" = K of Koro-sensei). The overlay font has ASCII 0x20..0x7E. */
const char* name_of(const ppu_context* ctx, int chr)
{
    static char names[256][48]; static bool have[256];
    if (chr < 0 || chr >= 256) return "";
    if (have[chr]) return names[chr];
    const uint32_t tab = call(func_00339E18, ctx), sp = ok(tab) ? call(func_0033A1A0, ctx, tab, (uint64_t)chr) : 0u;
    char* o = names[chr]; int n = 0;
    if (ok(sp)) {
        uint32_t q = sp; int step = 1, lo = 0;   /* plain bytes unless a mark or a zero byte says two bytes a letter */
        if (vm_base[sp] == 0xFF && vm_base[sp + 1u] == 0xFE) { q = sp + 2u; step = 2; lo = 0; }
        else if (vm_base[sp] == 0xFE && vm_base[sp + 1u] == 0xFF) { q = sp + 2u; step = 2; lo = 1; }
        else if (vm_base[sp] && !vm_base[sp + 1u]) { step = 2; lo = 0; }
        else if (!vm_base[sp] && vm_base[sp + 1u]) { step = 2; lo = 1; }
        for (; n < 46; q += (uint32_t)step) {
            const uint8_t c = vm_base[q + (uint32_t)lo], hi = step == 2 ? vm_base[q + (uint32_t)(1 - lo)] : 0;
            if (!c && !hi) break;
            /* letters with a mark fold to the plain letter (fighter 41 read "N?b?": the game writes
             * its u and e with a long mark, U+016B and U+0113) */
            const unsigned u = (unsigned)hi << 8 | c; char f = '?';
            if (u >= 0x20 && u <= 0x7E) f = (char)u;
            else if (u >= 0xC0 && u <= 0xFF) { static const char* k = "AAAAAA?CEEEEIIIID?OOOOO?OUUUUY??aaaaaa?ceeeeiiiid?ooooo?ouuuuy?y"; f = k[u - 0xC0]; if (u == 0xD1) f = 'N'; if (u == 0xF1) f = 'n'; }
            else if (u >= 0x100 && u <= 0x17F) { static const char* k =
                "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIi??JjKk?LlLlLlLlLlNnNnNn???OoOoOo??RrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZz?"; f = k[u - 0x100]; }
            else if (u == 0x2019 || u == 0x2018) f = '\'';
            o[n++] = f;
        }
    }
    o[n] = 0; have[chr] = true;
    say("the game's name of character %d: '%s' (string at %08X, first bytes %02X %02X %02X %02X)", chr, o, sp, ok(sp) ? vm_base[sp] : 0, ok(sp) ? vm_base[sp + 1u] : 0, ok(sp) ? vm_base[sp + 2u] : 0, ok(sp) ? vm_base[sp + 3u] : 0);
    return o;
}

/* Poses (the main pose is one of the poses a character can change to with Square; there are no Square buttons; the right
 * stick only turns the model, with no zoom).
 * Read in the viewer's input function func_001B29F0 (it runs only with viewer +0x150 not 0 and state 2):
 *   the pad object = func_003F485C(func_003F4624(), 0); its four stick values = func_0053A30C(pad, out) (left x, y,
 *   right x, y as floats); pad +0x20 the held bits (0xC0000 right stick left / right, 0x30000 right stick up / down,
 *   0x200000 and 0x100000 the two resets), pad +0x28 the bits just pressed (0x80 = Square).
 *   right stick x: viewer +0xA0 (the turn) += x * -0.0943;  right stick y: viewer +0xD0 (the zoom) += y * 0.1;
 *   left stick: viewer +0xD4 / +0xD8 (the model's place) += x, y * 0.1;
 *   Square: viewer +0x14 (the pose / model number, 0..3) goes to the next one the character has: its record at
 *   viewer +0xE8 (0x220 bytes a character, +0 the id) has the bits at +4: bit 16 = number 1, 17 = 2, 18 = 3; 0 is
 *   always there.
 * Here the viewer's own input is switched off (+0x150 = 0, written once by its constructor) and the port does the
 * part that is wanted: the turn; the pose number from a list (a fighter's pose, the choices in kPose). No zoom,
 * no moving, no Square. MODEL_PREVIEW_SQUARE=1 brings Square and the "Pose 2 of 4" label back, for picking poses.
 * Pose numbers for people are 1..n over the poses a fighter has ("Pose 2 of 3"). MODEL_PREVIEW_POSES="15:2,7:3"
 * (character:pose, ...) sets starting poses for a try; kPose below is the list that stays. */
std::atomic<int> g_sq{0};               /* Square presses seen by the pad hook */
bool square_on() { static const bool v = envi("MODEL_PREVIEW_SQUARE", 0) != 0; return v; }   /* 1 = Square goes through the poses again, with the label (for picking poses) */
int start_pose(int chr)   /* 1..4 */
{
    static int tab[256]; static bool init = false;
    if (!init) { init = true;
        /* The chosen poses (found by going through every fighter with Square and the "Pose n of 4" label, and the
         * Supports in the Gallery; one not listed stays on pose 1). */
        static const struct { int chr, pose; } kPose[] = {
            { 28, 2 } /* Gintoki */, { 33, 3 } /* Saiki */,   { 34, 2 } /* Momotaro */, { 41, 3 } /* Nube */,
            { 40, 2 } /* Joseph */,  { 13, 2 } /* Toriko */,  { 14, 3 } /* Zebra */,    {  9, 2 } /* Vegeta */,
            {  6, 3 } /* Madara */,  { 17, 2 } /* Gon */,     { 15, 2 } /* Ichigo */,   { 16, 2 } /* Aizen */,
            { 27, 2 } /* Oga */,     { 25, 2 } /* Kenshiro */, { 26, 3 } /* Raoh */,    { 35, 2 } /* Bo-bobo */,
            { 10, 2 } /* Yusuke */,  { 12, 3 } /* Hiei */,    { 11, 2 } /* Toguro */,   { 22, 3 } /* Kenshin */,
            { 23, 3 } /* Shishio */, {  1, 3 } /* Ace */,     {  2, 2 } /* Hancock */,  {  3, 2 } /* Akainu */,
            /* Supports: Neuro 1, Kumagawa (Misogi) 4, Rukia 1, Chitoge 4, Hisoka 2, Edajima (Heihachi) 2, Kagura 2,
             * Jaguar 2, Kuroko 2, SKET-dan 1, Lala 1, Allen 2, Hinata 1 */
            { 201, 4 }, { 203, 4 }, { 204, 2 }, { 205, 2 }, { 206, 2 }, { 207, 2 }, { 209, 2 }, { 212, 2 },
        };
        for (const auto& e : kPose) if (e.chr >= 0 && e.chr < 256) tab[e.chr] = e.pose;
        if (const char* e = std::getenv("MODEL_PREVIEW_POSES")) for (const char* q = e; *q;) { char* end = nullptr; const long c = std::strtol(q, &end, 10); if (end == q || *end != ':') break;
            q = end + 1; const long v = std::strtol(q, &end, 10); if (end == q) break; if (c >= 0 && c < 256) tab[c] = (int)v; q = *end ? end + 1 : end; } }
    return chr >= 0 && chr < 256 && tab[chr] >= 1 && tab[chr] <= 4 ? tab[chr] : 1;
}
/* once a tick with a model shown: the pose number is ours; out: the pose's place among the fighter's poses and their count */
/* the viewer has a record for this character (its list at +0xE4, +0xE0 records of 0x220 bytes, +0 the id) */
bool viewer_has(int chr)
{
    const uint32_t n = vm_read32(g_viewer + 0xE0u), list = vm_read32(g_viewer + 0xE4u);
    if (!ok(list) || n > 512u) return false;
    for (uint32_t i = 0; i < n; i++) if ((int32_t)vm_read32(list + i * 0x220u) == chr) return true;
    return false;
}
void pose_tick(int chr, int* rank, int* count)
{
    const uint32_t rec = vm_read32(g_viewer + 0xE8u), flags = ok(rec) ? vm_read32(rec + 4u) : 0u;
    bool have[4] = { true, (flags >> 16 & 1u) != 0, (flags >> 17 & 1u) != 0, (flags >> 18 & 1u) != 0 };
    int n = 0; for (int i = 0; i < 4; i++) n += have[i];
    if (chr != g_pose_char) {   /* a new fighter: its starting pose */
        g_pose_char = chr; g_pose_idx = 0; int want = start_pose(chr), r = 0;
        for (int i = 0; i < 4; i++) if (have[i] && ++r == want) g_pose_idx = i;
        g_sq.store(0);
        if (logon() || n > 1) say("character %d has %d poses (bits %08X), starting pose %d", chr, n, flags, want <= n ? want : 1);
    }
    int presses = g_sq.exchange(0); if (!square_on()) presses = 0;
    for (; presses > 0 && n > 1; presses--) { do g_pose_idx = (g_pose_idx + 1) % 4; while (!have[g_pose_idx]); }
    if (!have[g_pose_idx]) g_pose_idx = 0;
    if (vm_read32(g_viewer + 0x14u) != (uint32_t)g_pose_idx) vm_write32(g_viewer + 0x14u, (uint32_t)g_pose_idx);
    int r = 0; for (int i = 0; i <= g_pose_idx; i++) r += have[i];
    *rank = r; *count = n;
}
/* once a tick: the right stick turns the model (left / right only) */
void turn_tick(const ppu_context* ctx)
{
    static uint32_t s_buf = 0;
    if (vm_read32(g_viewer + 0x150u)) vm_write32(g_viewer + 0x150u, 0u);   /* the viewer's own input stays off */
    if (vm_read32(g_viewer) != 2u) return;
    if (!s_buf) { s_buf = gnew(ctx, 0x20u); if (!s_buf) return; }
    const uint32_t pad = call(func_003F485C, ctx, call(func_003F4624, ctx), 0);
    if (!ok(pad) || !(vm_read32(pad + 0x20u) & 0x000C0000u)) return;
    call(func_0053A30C, ctx, pad, s_buf);
    uint32_t u = vm_read32(s_buf + 8u); float x; std::memcpy(&x, &u, 4);
    u = vm_read32(g_viewer + 0xA0u); float turn; std::memcpy(&turn, &u, 4);
    if (!(x >= -1.5f && x <= 1.5f)) return;
    turn += x * -0.0943f; std::memcpy(&u, &turn, 4); vm_write32(g_viewer + 0xA0u, u);
}

void hexdump(const char* what, uint32_t a, uint32_t n)
{
    for (uint32_t o = 0; o < n; o += 32) { std::printf("[modelpv] %s +%03X:", what, o);
        for (uint32_t k = 0; k < 32 && o + k < n; k++) std::printf("%s%02X", (k % 4) ? "" : " ", vm_base[a + o + k]);
        std::printf("  "); for (uint32_t k = 0; k < 32 && o + k < n; k++) { const uint8_t c = vm_base[a + o + k]; std::printf("%c", c >= 32 && c < 127 ? c : '.'); }
        std::printf("\n"); }
    std::fflush(stdout);
}
void namepic_tick(const ppu_context* ctx, int want)
{
    if (!namepic() || g_packs != 2 || !g_loader || !g_lang[0]) return;
    static uint32_t s_buf = 0; static unsigned ticks = 0;
    const uint32_t tab = vm_read32(g_loader + 4u); if (!ok(tab) || !ok(vm_read32(tab + 8u * 8u + 4u)) || !ok(vm_read32(tab + 9u * 8u + 4u))) return;
    if (want >= 0 && want != g_np_chr && g_np_state != 1) {
        if (!keep(ctx, s_buf, 0x200u)) return;
        if (g_np_state == 2 || g_np_state == 3) { call(func_004241F8, ctx, g_loader, 8); call(func_004241F8, ctx, g_loader, 9); }
        for (uint32_t k = 0; k < 2; k++) { char path[200]; std::snprintf(path, sizeof path, "progress/coomon_chara_name/%s/common_chara_name_%03d_%s_%s.pak", g_lang, want, g_lang, k ? "v" : "m");
            const uint32_t at = s_buf + k * 0x100u; std::memcpy(vm_base + at, path, std::strlen(path) + 1); call(func_00424618, ctx, g_loader, 8u + k, at);
            if (k == 0) say("name pack asked for: %s", path); }
        g_np_chr = want; g_np_state = 1; ticks = 0;
    }
    if (g_np_state != 1) return;
    ticks++;
    const bool in = call(func_00424230, ctx, g_loader, 8) == 1u && call(func_00424230, ctx, g_loader, 9) == 1u;
    if (!in) { if (ticks > 180u) { g_np_state = 3; say("the name pack of character %d did not come in 3 s (states %u %u)", g_np_chr, call(func_00424230, ctx, g_loader, 8), call(func_00424230, ctx, g_loader, 9)); } return; }
    const uint32_t m = call(func_00435A44, ctx, g_loader, 8), v = call(func_00435A44, ctx, g_loader, 9);
    if (logon()) { say("the name pack of character %d is in after %u ticks: data %08X %08X", g_np_chr, ticks, m, v); if (ok(m)) hexdump("name _m", m, 0x120u); if (v >= 0x10000u) hexdump("name _v", v, 0xA0u); }
    /* The packs: "STPK", one file; +0x10 the
     * file's place in the pack, +0x14 its size. In _m the file is a chunk list ($CFH, $RSF, $TXR, $RSI ...): the $TXR
     * chunk's data, 0x10 after its tag, is u32 1, u16 0x0200, u16 width (256), u16 height (32), u16 bytes a row of
     * blocks (0x400), u8 format (0x11 = DXT5). In _v the file is the pixels (0x2000 bytes at +0x80). */
    g_np_state = 3;
    if (!ok(m) || v < 0x10000u) return;
    uint32_t t = 0; for (uint32_t o = 0x20u; o < 0x700u; o += 4) if (vm_read32(m + o) == 0x24545852u) { t = m + o; break; }   /* "$TXR" */
    if (!t) { say("no picture record in the name pack of character %d", g_np_chr); return; }
    const uint32_t w = vm_read16(t + 0x16u), h = vm_read16(t + 0x18u), fmt = vm_base[t + 0x1Cu], px = v + vm_read32(v + 0x10u), size = vm_read32(v + 0x14u);
    if (fmt != 0x11u || w < 4u || h < 4u || w > 512u || h > 64u || (w & 3u) || (h & 3u) || size < w * h) { say("name picture of character %d: %u x %u format %02X size %X is not one this code reads", g_np_chr, w, h, fmt, size); return; }
    for (uint32_t by = 0; by < h / 4u; by++) for (uint32_t bx = 0; bx < w / 4u; bx++) {   /* DXT5: 16 bytes a block of 4 x 4 */
        const uint8_t* q = vm_base + px + (by * (w / 4u) + bx) * 16u;
        uint8_t al[8]; al[0] = q[0]; al[1] = q[1];
        if (al[0] > al[1]) for (int i = 1; i < 7; i++) al[1 + i] = (uint8_t)(((7 - i) * al[0] + i * al[1]) / 7);
        else { for (int i = 1; i < 5; i++) al[1 + i] = (uint8_t)(((5 - i) * al[0] + i * al[1]) / 5); al[6] = 0; al[7] = 255; }
        uint64_t ai = 0; for (int i = 0; i < 6; i++) ai |= (uint64_t)q[2 + i] << (8 * i);
        const uint32_t c0 = q[8] | (uint32_t)q[9] << 8, c1 = q[10] | (uint32_t)q[11] << 8, ci = q[12] | (uint32_t)q[13] << 8 | (uint32_t)q[14] << 16 | (uint32_t)q[15] << 24;
        uint8_t col[4][3];
        col[0][0] = (uint8_t)((c0 >> 11) * 255 / 31); col[0][1] = (uint8_t)(((c0 >> 5) & 63) * 255 / 63); col[0][2] = (uint8_t)((c0 & 31) * 255 / 31);
        col[1][0] = (uint8_t)((c1 >> 11) * 255 / 31); col[1][1] = (uint8_t)(((c1 >> 5) & 63) * 255 / 63); col[1][2] = (uint8_t)((c1 & 31) * 255 / 31);
        for (int k = 0; k < 3; k++) { col[2][k] = (uint8_t)((2 * col[0][k] + col[1][k]) / 3); col[3][k] = (uint8_t)((col[0][k] + 2 * col[1][k]) / 3); }
        for (uint32_t i = 0; i < 16; i++) { uint8_t* o = g_np_rgba + ((by * 4u + i / 4u) * w + bx * 4u + i % 4u) * 4u; const uint8_t* c = col[(ci >> (2 * i)) & 3u];
            o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = al[(ai >> (3 * i)) & 7u]; }
    }
    int x0 = (int)w, x1 = 0, y0 = (int)h, y1 = 0;   /* where the letters are */
    for (int y = 0; y < (int)h; y++) for (int x = 0; x < (int)w; x++) if (g_np_rgba[((size_t)y * w + (size_t)x) * 4u + 3u] > 16) { if (x < x0) x0 = x; if (x + 1 > x1) x1 = x + 1; if (y < y0) y0 = y; if (y + 1 > y1) y1 = y + 1; }
    if (x1 <= x0 || y1 <= y0) { say("the name picture of character %d is empty", g_np_chr); return; }
    g_np_w = (int)w; g_np_h = (int)h; g_np_x0 = x0; g_np_x1 = x1; g_np_y0 = y0; g_np_y1 = y1; g_np_state = 2;
    { static int said = 0; if (said++ < 6 || logon()) say("name picture of character %d: %u x %u, letters at x %d..%d y %d..%d, in after %u ticks", g_np_chr, w, h, x0, x1, y0, y1, ticks); }
}

void car_tick(const ppu_context* ctx)
{
    /* Player states seen: 6 on the slots, Cross -> 7 (the grid comes in) -> 8 on
     * the grid; a pick -> 9 the settings window (Circle there: 10, 11, 12, 7, 8); Circle on the grid -> 5 -> 6 */
    /* The state object is the one of THIS visit only once func_00188A58 has run since the visit began. Before
     * that it is the last visit's (after a fight: freed memory), and func_00185BE0 on it read through null pointers
     * in func_001855B4 (11 lines; none without the
     * preview). Since the window is made in the screen's first frame, this function runs that early. */
    const uint32_t obj = jsp_cs_state_seq() != g_seq0 ? jsp_cs_state_obj() : 0u, st = ok(obj) ? vm_read32(obj + 0x60u) : 0xFFFFFFFFu;
    const uint32_t slot = ok(obj) ? call(func_00185BE0, ctx, obj, 0, 2, 0, 0) : 0xFFu;
    { static uint32_t last_st = 0xFFFFFFFEu, last_slot = 0xFFu; if (st != last_st || slot != last_slot) { last_st = st; last_slot = slot; if (logon()) say("player 1 state %u, slot %u, candidate %d (at %llu)", st, slot, g_car_on && g_car_n ? g_car_list[g_car_at] : -1, GetTickCount64() % 1000000ull); } }
    /* Support slots (4 and 5; they get the poses too): the
     * same list, made of the grid table's Supports (ids 200 .. 213). MODEL_PREVIEW_SUPPORT=0: the game's own grid
     * for them, as before. */
    static const bool sup = envi("MODEL_PREVIEW_SUPPORT", 1) != 0;
    const bool on_grid = st == 8u || st == 7u, sup_slot = slot == 4u || slot == 5u, want_car = on_grid && (slot <= 3u || (sup && sup_slot));
    g_support_grid = on_grid && sup_slot && !sup;
    const unsigned long long now = GetTickCount64();
    if (want_car && !g_car_on) {
        const int rows = (int)vm_read32(kGridRows);
        if (rows < 1 || rows > 16) return;
        g_car_rows = rows; g_car_n = 0; g_car_list[g_car_n++] = 0x1F4;   /* Random first */
        /* a fighter slot: the fighters. A Support slot: the Supports first, then the fighters, because the game lets a
         * fighter stand in as a Support;
         * the game's own pick decides whether the shown one is allowed */
        for (int i = 0; i < rows * 4; i++) g_car_saved[i] = vm_read32(kGrid + (uint32_t)i * 4u);
        if (sup_slot) for (int i = 0; i < rows * 4; i++) { const uint32_t id = g_car_saved[i]; if (id >= 200u && id < 0x1F4u && g_car_n < 72) g_car_list[g_car_n++] = (int)id; }
        for (int i = 0; i < rows * 4; i++) { const uint32_t id = g_car_saved[i]; if (id < 200u && g_car_n < 72) g_car_list[g_car_n++] = (int)id; }
        g_car_at = 0; g_car_step = 0; g_car_on = true;
        { static const int start = envi("MODEL_PREVIEW_START", -1);   /* test: the list opens on this character instead of Random (never 19, 20, 21, 208 in a test) */
          for (int i = 0; start >= 0 && i < g_car_n; i++) if (g_car_list[i] == start) g_car_at = i; }
        say("carousel on for slot %u: %d rows, %d choices (Random, then %d, %d, %d ...)", slot, rows, g_car_n, g_car_n > 1 ? g_car_list[1] : -1, g_car_n > 2 ? g_car_list[2] : -1, g_car_n > 3 ? g_car_list[3] : -1);
        g_car_slot = (int)slot; g_tri.store(0);
    } else if (!want_car && g_car_on) {
        /* the list closes: by a pick (states 9 / 13) the slot takes the shown character's colour */
        const int cand = g_car_n ? g_car_list[g_car_at] : -1;
        if ((st == 9u || st == 13u) && g_car_slot >= 0 && g_car_slot < 6) g_slot_alt[g_car_slot] = cand >= 0 && cand < 256 ? (int)g_alt[cand] : 0;
        car_end();
    }

    /* what the window should show: on the grid the candidate; on the slots the fighter the slot holds (slot record at
     * 0x009540E8 + 0x2C * slot: +4 mode, 0 = picked, +8 the character); -1 = Random or nothing: the logo */
    int want = -1, want_alt = 0; bool random = false;
    if (g_car_on) {
        const int step = g_car_step.exchange(0);
        if (step) { g_car_at = ((g_car_at + step) % g_car_n + g_car_n) % g_car_n; if (logon()) say("candidate %d", g_car_list[g_car_at]); }
        const int cand = g_car_list[g_car_at];
        for (int i = 0; i < g_car_rows * 4; i++) vm_write32(kGrid + (uint32_t)i * 4u, (uint32_t)cand);
        if (cand == 0x1F4) random = true; else want = cand;
        int tri = g_tri.exchange(0);   /* Triangle: the shown character's next colour */
        if (tri && cand >= 0 && cand < 256 && alt_count(cand) > 1) { g_alt[cand] = (g_alt[cand] + tri) % alt_count(cand); say("character %d: colour %d (%s)", cand, (int)g_alt[cand] + 1, alt_name(cand, g_alt[cand])); }
        if (cand >= 0 && cand < 256) want_alt = g_alt[cand];
    } else if (slot <= 3u || (sup && sup_slot)) {
        const uint32_t rec = 0x009540E8u + 0x2Cu * slot, mode = vm_read32(rec + 4u), chr = vm_read32(rec + 8u);
        if (mode == 0u && chr < 0x1F4u) { want = (int)chr; want_alt = slot < 6u ? (int)g_slot_alt[slot] : 0; } else random = mode == 1u;
    }
    if (want < 0 || want >= 256 || want_alt >= alt_count(want)) want_alt = 0;
    /* the fight's colours: every slot that holds a fighter picked in another colour */
    { int f[256]; std::memset(f, 0, sizeof f);
      for (uint32_t q = 0; q < 6u; q++) { const uint32_t rec = 0x009540E8u + 0x2Cu * q, chr = vm_read32(rec + 8u); if (vm_read32(rec + 4u) == 0u && chr < 256u && g_slot_alt[q] > 0 && g_slot_alt[q] < alt_count((int)chr)) f[chr] = g_slot_alt[q]; }
      for (int q = 0; q < 256; q++) if (g_fight_alt[q] != f[q]) { g_fight_alt[q] = f[q]; say("the fight will have character %d in colour %d (%s)", q, f[q] + 1, alt_name(q, f[q])); } }
    /* a character the viewer has no record for is not asked for (its name still shows) */
    const bool known = want < 0 || viewer_has(want);
    if (!known) { static int said = -1; if (said != want) { said = want; say("the viewer has no record for character %d: the logo stays", want); } }
    { static int last_want = -2, last_alt = 0; if (want != last_want || want_alt != last_alt) { last_want = want; last_alt = want_alt; g_car_changed = now; } }
    /* MODEL_PREVIEW_DUMP=1 (probe): the grid layouts' panes, 1.5 s after a Support is first shown on the list */
    { static const bool dump = probe_env("MODEL_PREVIEW_DUMP", 0) != 0; static unsigned n = 0;
      if (dump && g_car_on && sup_slot && want >= 200 && ++n == 90u) for (int i = 0; i < g_ngrid_layout; i++) {
          const uint32_t list = vm_read32(g_grid_layout[i] + 0x14u), arr = ok(list) ? vm_read32(list + 0x08u) : 0u, cnt = ok(list) ? vm_read32(list + 0x0Cu) : 0u;
          say("layout %08X: %u panes", g_grid_layout[i], cnt);
          for (uint32_t q = 0; ok(arr) && q < cnt && q < 600u; q++) { const uint32_t pn = vm_read32(arr + q * 4u); if (!ok(pn)) continue;
              const uint32_t np = vm_read32(pn + 0x104u), par = vm_read32(pn + 4u); char nm[40]; int k = 0; if (ok(np)) for (; k < 39; k++) { const char c = (char)vm_base[np + (uint32_t)k]; if (c < 32 || c > 126) break; nm[k] = c; } nm[k] = 0;
              std::printf("[modelpv]   %3u %08X %-32s vis %u word100 %08X flags %08X parent %08X picture %08X\n", q, pn, nm, vm_read32(pn + 0x100u) & 1u, vm_read32(pn + 0x100u), vm_read32(pn + 0x10u), par, vm_read32(pn + 0x14u)); }
          std::fflush(stdout); } }
    { static const bool hide = envi("MODEL_PREVIEW_HIDEGRID", 1) != 0; if (hide) grid_show(g_support_grid); }
    /* The model: asked for once the choice has rested (MODEL_PREVIEW_REST ms, default 250), and the viewer is not
     * unloading (state 3). A request while a load is still running (state 1) is the viewer's own business: its request
     * handler func_001B28A8 takes states 1 and 2 alike (it stops the load with func_0039C49C, goes to 3, then 0, then
     * loads the new id). Before, a started load was waited for, so a walk through three fighters took
     * over 4 s for the last one. MODEL_PREVIEW_CANCEL=0: wait for the load, as before. */
    static const bool cancel = envi("MODEL_PREVIEW_CANCEL", 1) != 0; static const unsigned rest = (unsigned)envi("MODEL_PREVIEW_REST", 250);
    static unsigned long long asked_at = 0, asked_read = 0, asked_ops = 0; static int shown = -2;
    auto io_read = [](unsigned long long* ops) { IO_COUNTERS io; std::memset(&io, 0, sizeof io); GetProcessIoCounters(GetCurrentProcess(), &io); if (ops) *ops = io.ReadOperationCount; return (unsigned long long)io.ReadTransferCount; };
    const uint32_t vs = vm_read32(g_viewer);
    if (want >= 0 && known && (want != g_car_asked || want_alt != g_asked_alt) && now - g_car_changed >= rest && vs != 3u && (cancel || vs != 1u) && !g_out) {
        g_asked_alt_chr = want; g_asked_alt = want_alt;   /* before the request: the recolour asks while the new textures come in */
        call(func_001B26A8, ctx, g_viewer, (uint64_t)(int64_t)want);
        if (g_part) call(func_001C1624, ctx, g_part, (uint64_t)(int64_t)want);
        g_car_asked = want; asked_at = now; asked_read = io_read(&asked_ops); shown = -2; g_load_ticks = 0; say("asked for character %d%s (viewer state %u)", want, want_alt ? " in another colour" : "", vs);
        vm_write32(g_viewer + 0xA0u, 0u);   /* the next fighter faces the front again */
    }
    if (want >= 0 && g_car_asked == want && g_asked_alt == want_alt && shown != want && vm_read32(g_viewer) == 2u && vm_read32(g_viewer + 0x0Cu) == 0u && (int32_t)vm_read32(g_viewer + 0x1Cu) == want) {
        unsigned long long ops = 0; const unsigned long long rd = io_read(&ops) - asked_read;   /* what the whole process read meanwhile: the model, its background, the name pack */
        shown = want; say("character %d is shown %llu ms after it was asked for (%u ticks of loading, %.1f MB read in %llu reads meanwhile)", want, now - asked_at, g_load_ticks, (double)rd / 1048576.0, ops - asked_ops); }
    g_logo = !(want >= 0 && g_car_asked == want && g_asked_alt == want_alt && vm_read32(g_viewer) == 2u && vm_read32(g_viewer + 0x0Cu) == 0u && (int32_t)vm_read32(g_viewer + 0x1Cu) == want);
    if (logon()) { static bool once = false; if (!once) { once = true; for (int c = 0; c < 42; c++) name_of(ctx, c); for (int c = 200; c < 214; c++) name_of(ctx, c);   /* every name into the log */
        const uint32_t n = vm_read32(g_viewer + 0xE0u), list = vm_read32(g_viewer + 0xE4u); std::printf("[modelpv] the viewer's list has %u characters:", n);
        for (uint32_t i = 0; ok(list) && i < n && i < 80u; i++) std::printf(" %d", (int32_t)vm_read32(list + i * 0x220u)); std::printf("\n"); std::fflush(stdout); } }
    namepic_tick(ctx, want);
    Ovl w; std::memset(&w, 0, sizeof w);
    /* The overlay is drawn over everything, also over the game's own windows. So the texts show only while the player
     * is on the slots or on the list (states 5 .. 8), not while the settings window of a pick is up (9 .. 12): the name
     * of the Model Viewer would be shown over that window. */
    const bool plain = st >= 5u && st <= 8u;
    if (g_part && vm_read32(g_part + 0x2Cu) == 3u && !g_support_grid && !g_out && plain) {   /* the window is up and nothing of the game's is over it */
        w.random = random ? 1 : 0;
        w.arrows = g_car_on ? 1 : 0;
        /* the name: the game's picture when it is in; the port's text when there is none (or the probe is off);
         * nothing for the few frames the picture's pack takes */
        if (want >= 0) {
            if (namepic() && g_np_chr == want && g_np_state == 2) w.pic = want + 1;
            else if (!namepic() || (g_np_chr == want && g_np_state == 3)) std::snprintf(w.name, sizeof w.name, "%s", name_of(ctx, want));
        }
        if (g_car_on && want >= 0 && alt_count(want) > 1) { w.alt = want_alt + 1; w.alts = alt_count(want); std::snprintf(w.altname, sizeof w.altname, "%s", alt_name(want, want_alt)); }   /* only where Triangle works */
        if (!g_logo) { int r = 1, n = 1; pose_tick(want, &r, &n); if (square_on() && n > 1) { w.pose = r; w.poses = n; } }
    }
    overlay_show(w);
}

void tick(const ppu_context* ctx)
{
    static const int stop = envi("MODEL_PREVIEW_STOP", 5), chr = envi("MODEL_PREVIEW_CHAR", 28);
    g_phase_ticks++;
    switch (g_phase) {
    case 0:   /* Character Select is up: the packs, if a menu has not loaded them already */
        if (g_cs_ticks < (unsigned)envi("MODEL_PREVIEW_WAIT", 0)) return;   /* at once, so the window comes in with the screen */
        if (!g_room_ok) { fail("the heap layout was not extended"); return; }
        if (!ok(g_scene)) { if (g_cs_ticks > 240) fail("the Progress scene was not seen (target 0x00153040)"); return; }
        if (g_packs < 0) { fail("no data for the layout pack"); return; }
        /* a Character Select on another heap layout than the menus' (or with our heaps not built): no preview on this
         * visit, the grid comes back; before, this stopped the feature for the rest of the run */
        if (g_packs == 0 && !packs_ask(ctx, false)) {
            if (g_phase >= 0) { g_no_preview = true; if (g_grid_hidden) grid_show(true); say("Character Select: no preview here (%s), the grid stays", g_soft_why); }
            return;
        }
        g_phase = 1; g_phase_ticks = 0;
        [[fallthrough]];
    case 1:   /* the files are in */
        if (stop <= 1) return;
        if (!packs_check(ctx)) { if (g_packs < 0) fail("no data for the layout pack"); else if (g_phase_ticks > 900) fail("the Gallery packs did not load in 15 s"); return; }
        if (logon()) say("Character Select tick %u: the packs are there", g_cs_ticks);
        g_phase = 2; g_phase_ticks = 0;
        [[fallthrough]];
    case 2:   /* the registry copy, the empty loader, the viewer */
        if (stop <= 2) return;
        {
            g_real_reg = vm_read32(kRegPtr);
            if (!ok(g_real_reg)) { fail("no registry"); return; }
            static uint32_t s_vt = 0;
            keep(ctx, g_fake_reg, kRegBytes + 0x20u); keep(ctx, g_dummy, 0x60u);
            const uint32_t vt = keep(ctx, s_vt, 0x80u), nop = vm_read32(0x0087CBB8u + 0x08u);   /* a virtual that only returns (func_0071C02C) */
            keep(ctx, g_fscene, 0x380u); keep(ctx, g_ctrl, 0x60u);
            if (!g_fake_reg || !g_dummy || !vt || !g_fscene || !g_ctrl || !ok(nop) || vm_read32(nop) != 0x0071C02Cu) { fail("no memory, or the do-nothing virtual is not where it was read"); return; }
            g_viewer = g_fscene + 0xD0u;
            for (uint32_t o = 0; o < 0x80u; o += 4) vm_write32(vt + o, nop);
            vm_write32(g_ctrl, vt); vm_write32(g_fscene + 0x58u, g_ctrl); vm_write32(g_fscene + 0x60u, 1u);
            vm_write32(g_dummy, vt); vm_write32(g_dummy + 4u, 0u); vm_write32(g_dummy + 8u, 0x80000000u);
            say("making the viewer %08X under the scene %08X (registry %08X, copy %08X)", g_viewer, (uint32_t)g_scene, g_real_reg, g_fake_reg);
            if (logon()) say_children("before the viewer", g_scene);
            viewer_call(func_001B54BC, ctx, g_viewer, g_scene);
            say("the viewer is made: state %d, world %08X, 3D scene %08X, layout root %08X, pass %08X", (int32_t)vm_read32(g_viewer), vm_read32(g_viewer + 8u),
                vm_read32(g_viewer + 0x144u), vm_read32(g_viewer + 0x148u), vm_read32(g_viewer + 0xB0u));
            if (envi("MODEL_PREVIEW_DRAW", 54)) scene_draw_on(ctx);
            if (window_on()) {
                g_part = gnew(ctx, 0x90u); keep(ctx, g_part_args, 0x20u);   /* the part is the game's to delete (its +0x04 virtual) */
                if (g_part && g_part_args) {
                    call(func_001C1A78, ctx, g_part, g_scene);
                    vm_write32(g_part_args, g_scene);
                    static const uint32_t ids[4] = { 0, 1, 3, 4 };
                    for (uint32_t k = 0; k < 4; k++) vm_write32(g_part_args + 4u + k * 4u, call(func_00435A44, ctx, g_loader, ids[k]));
                    viewer_call(func_001C1930, ctx, g_part, g_part_args);
                    call(func_002FA0B8, ctx, g_part, 0);
                    say("the window part %08X is made (vtable %08X, node %08X, layout %08X, archive %08X)", g_part, vm_read32(g_part), vm_read32(g_part + 0x44u), vm_read32(g_part + 0x28u), vm_read32(g_part + 0x50u));
                }
            }
        }
        g_phase = 3; g_phase_ticks = 0;
        return;
    case 3:   /* once a frame; after half a second, the character */
    case 4:
        if (stop <= 3) return;
        if (car_mode()) turn_tick(ctx);
        viewer_call(func_001B65D8, ctx, g_viewer);
        /* MODEL_PREVIEW_SPEED=n (default 4; 1 = as the game does it): part of a model's load is counted in the viewer's
         * own frames, 30 a second on this screen. Measured, three models each:
         * n = 1: 3.0 s (88 .. 90 ticks), n = 2: 2.1 s (62 .. 63), n = 4: 1.6 .. 1.8 s (49 .. 53); the same 8 .. 11 MB
         * are read each time, so what is left is the game's reading pace. So
         * while a model loads or unloads (state 1 or 3) the viewer's frame is
         * run n times a tick, to see whether the load is counted in frames */
        { static const int speed = envi("MODEL_PREVIEW_SPEED", 4); g_load_ticks += (vm_read32(g_viewer) == 1u || vm_read32(g_viewer) == 3u) ? 1u : 0u;
          for (int i = 1; i < speed && (vm_read32(g_viewer) == 1u || vm_read32(g_viewer) == 3u); i++) viewer_call(func_001B65D8, ctx, g_viewer); }
        if (g_part) {   /* as func_001BBC0C: the picture words to the part, then the part's own frame */
            vm_write32(g_fscene + 0x32Cu, (car_mode() ? (bool)g_logo : vm_read32(g_viewer) != 2u) ? 1u : 0u);   /* the fourth word: 1 = nothing to show yet, the part keeps its loading logo up */
            call(func_001C1654, ctx, g_part, g_fscene + 0x320u);
            /* with the logo wanted (Random, an empty slot) the part's "picture is up" word (+0x48) is cleared: its
             * frame then takes its own logo path (loc_001C1E30 in func_001C1B34: logo pane shown, the three picture
             * panes hidden) and, with the fourth word 1, does not set the word again. A model that is still loaded
             * stayed on screen under "Random" otherwise */
            if (car_mode() && g_logo) vm_write32(g_part + 0x48u, 0u);
            viewer_call(func_001C2100, ctx, g_part);
            if (envi("MODEL_PREVIEW_PARTCALC", 0)) call(func_002F951C, ctx, g_part);   /* part +0x28: the layout's own frame step (func_00329E58), if the update does not do it */
            const uint32_t bgtex = vm_read32(g_part + 0x38u);
            if (bgtex != vm_read32(g_ctrl + 0x38u)) { vm_write32(g_ctrl + 0x38u, bgtex); say("the part's background texture: %08X", bgtex); }
        }
        /* Character Select's own parts: their state word (+0x2C) is watched. Measured
         * for the title part (vtable 0x0087C368, MODEL_PREVIEW_OUTVT): 1 at the start, 2 from about 0.15 s on (up), 3 at
         * the Circle that leaves the screen (going), 0 about 0.14 s later (gone), then the screen's frames stop. These
         * are the screen parts' own numbers, not the base part's (1 / 2 coming in, 3 up, 4 / 5 going, 0 gone), which
         * our window part uses. When the title goes, our window is told to go too (command 1 = its "out" animation);
         * when the title is gone, the window is not drawn any more; when the title comes up again, so does ours. */
        if (g_part) {
            static const uint32_t outvt = (uint32_t)std::strtoul(std::getenv("MODEL_PREVIEW_OUTVT") ? std::getenv("MODEL_PREVIEW_OUTVT") : "0087C368", nullptr, 16);
            for (int i = 0; i < g_ncs_parts; i++) {
                const uint32_t part = g_cs_parts[i]; if (!ok(part) || part == g_part) continue;
                const uint32_t vt = vm_read32(part), st = vm_read32(part + 0x2Cu), was = g_cs_part_state[i];
                if (st == was) continue;
                if (logon()) say("part %08X (vtable %08X): state %u -> %u (at %llu)", part, vt, was, st, GetTickCount64() % 1000000ull);
                g_cs_part_state[i] = st;
                if (vt != outvt || was == 0xFFFFFFFFu) continue;
                const bool go = st == 3u || st == 0u;
                if (go != g_out) { g_out = go; call(func_002FA0B8, ctx, g_part, go ? 1u : 0u); say("the window is told to %s with Character Select's title", go ? "go" : "come in"); }
                g_title_gone = st == 0u;
            }
        }
        /* MODEL_PREVIEW_DUMP=2 (probe): the window part's own layout, every pane, 2 s after it was made and again with a model shown */
        if (g_part) { static const bool dump = probe_env("MODEL_PREVIEW_DUMP", 0) == 2; static int done = 0;
            const bool now2 = done == 1 && vm_read32(g_viewer) == 2u && !g_logo;
            if (dump && ((done == 0 && g_phase_ticks == 120u) || now2)) { done++;
                const uint32_t lobj = vcall(ctx, g_part, 0x54), lay = ok(lobj) ? vm_read32(lobj + 4u) : 0u /* as func_001C1B34 gets it: the +0x54 virtual, then +4 */, list = ok(lay) ? vm_read32(lay + 0x14u) : 0u, arr = ok(list) ? vm_read32(list + 0x08u) : 0u, cnt = ok(list) ? vm_read32(list + 0x0Cu) : 0u;
                say("the window part's layout %08X: %u panes (%s)", lay, cnt, done == 1 ? "logo up" : "model shown");
                auto fl = [](uint32_t a) { const uint32_t w = vm_read32(a); float f; std::memcpy(&f, &w, 4); return f; };
                for (uint32_t q = 0; ok(arr) && q < cnt && q < 300u; q++) { const uint32_t pn = vm_read32(arr + q * 4u); if (!ok(pn)) continue;
                    const uint32_t np = vm_read32(pn + 0x104u), par = vm_read32(pn + 4u); char nm[40]; int k = 0; if (ok(np)) for (; k < 39; k++) { const char c = (char)vm_base[np + (uint32_t)k]; if (c < 32 || c > 126) break; nm[k] = c; } nm[k] = 0;
                    int pi = -1; for (uint32_t z = 0; z < cnt && z < 300u; z++) if (vm_read32(arr + z * 4u) == par) { pi = (int)z; break; }
                    std::printf("[modelpv]   %3u %08X %-30s vt %08X vis %u parent %3d picture %08X tr %.1f %.1f sc %.2f %.2f | +28: %.1f %.1f %.1f %.1f %.1f %.1f\n", q, pn, nm, vm_read32(pn), vm_read32(pn + 0x100u) & 1u, pi,
                                vm_read32(pn + 0x14u), fl(pn + 0x18u), fl(pn + 0x1Cu), fl(pn + 0x20u), fl(pn + 0x24u), fl(pn + 0x28u), fl(pn + 0x2Cu), fl(pn + 0x30u), fl(pn + 0x34u), fl(pn + 0x38u), fl(pn + 0x3Cu)); }
                std::fflush(stdout); } }
        if (!g_part || envi("MODEL_PREVIEW_PANE_ALSO", 0)) show_picture();
        if (car_mode() && g_part) { if (stop >= 5) car_tick(ctx); }
        else if (g_part) { static const bool hide = envi("MODEL_PREVIEW_HIDEGRID", 1) != 0; if (hide) { grid_roots_from_layouts(); grid_show(false); } }
        if (!car_mode() && g_phase == 3 && stop >= 5 && g_phase_ticks >= 30) {
            call(func_001B26A8, ctx, g_viewer, (uint64_t)(int64_t)chr);
            if (g_part) call(func_001C1624, ctx, g_part, (uint64_t)(int64_t)chr);
            say("asked for character %d", chr);
            g_phase = 4; g_phase_ticks = 0;
        }
        if (logon() || g_phase_ticks % 60 == 1) {
            static uint32_t last = 0xFFFFFFFFu; const uint32_t st = vm_read32(g_viewer);
            if (logon() || st != last) { last = st; say("viewer state %d (was %d), request %u, character %d, +DC %u, tick %u", (int32_t)st, (int32_t)vm_read32(g_viewer + 4u),
                                                        vm_read32(g_viewer + 0x0Cu), (int32_t)vm_read32(g_viewer + 0x10u), vm_read32(g_viewer + 0xDCu), g_phase_ticks); }
        }
        return;
    default: return;
    }
}
}

/* jsp_game.cpp's recolour (the texture patch hook): the colour this character is to have right now: on
 * Character Select what the viewer's model was asked in, elsewhere what the fight was set up with. -1 = no say. */
int model_preview_alt(int chr)
{
    if (!enabled() || !alt_on() || chr < 0 || chr >= 256) return -1;
    if (g_phase >= 3 && !g_close_ticks) return g_asked_alt_chr == chr ? (int)g_asked_alt : 0;
    return g_fight_alt[chr];
}

/* jsp_game.cpp game_pad_filter: every pad read, from the start of the game */
void model_preview_pad(uint32_t port, uint16_t* b, uint16_t* lx, uint16_t* ly)
{
    if (!enabled() || port != 0) return;
    make_room();
    if (logon()) { static uint16_t was = 0; const uint16_t edge = (uint16_t)(*b & ~was), up = (uint16_t)(was & ~*b); was = *b;   /* buttons as they go down and up */
        if (edge || up) say("pad: down %04X up %04X (at %llu, carousel %d)", edge, up, GetTickCount64() % 1000000ull, (int)g_car_on); }
    { static uint16_t sq_was = 0; if ((*b & 0x8000) && !(sq_was & 0x8000) && g_phase >= 3) g_sq++; sq_was = *b; }   /* Square: the next pose (pose_tick) */
    { static uint16_t tri_was = 0; if ((*b & 0x1000) && !(tri_was & 0x1000) && g_car_on) g_tri++; tri_was = *b; }   /* Triangle on the list: the next colour */
    if (!g_car_on) return;
    /* the carousel: Left / Right (D-pad or stick) are ours, with a repeat after 0.4 s every 0.13 s; the game gets no
     * direction, L1 or R1 meanwhile */
    static uint16_t prev = 0; static unsigned long long since = 0, last = 0;
    uint16_t cur = *b;
    if (*lx < 64) cur |= 0x0080; else if (*lx > 192) cur |= 0x0020;
    const uint16_t dir = (uint16_t)(cur & 0x00A0);
    const unsigned long long now = GetTickCount64();
    if (dir && dir != (prev & 0x00A0)) { g_car_step += (dir & 0x0020) ? 1 : -1; since = last = now; }
    else if (dir && now - since >= 400 && now - last >= 130) { g_car_step += (dir & 0x0020) ? 1 : -1; last = now; }
    prev = cur;
    *b = (uint16_t)(*b & ~(0x0010 | 0x0020 | 0x0040 | 0x0080 | 0x0400 | 0x0800 | (alt_on() ? 0x1000 : 0))); *lx = 128; *ly = 128;
}

/* jsp_game.cpp jsp_six_teardown4_enter: func_0043E644(heap manager, layout, mask) is entered. While a viewer function
 * runs, a switch to another layout becomes a switch to the one in use (the function returns at once then). */
void model_preview_layout_enter(ppu_context* ctx)
{
    const uint32_t hm = (uint32_t)ctx->gpr[3];
    if (!ok(hm)) return;
    const uint32_t cur = vm_read32(hm + 0x264u);
    if (!t_in_viewer) {   /* a real switch: what sits in our heaps goes first */
        if (enabled() && (uint32_t)ctx->gpr[4] != cur && (g_packs != 0 || g_phase >= 1)) {
            say("heap layout %u -> %d: the preview's packs go first (phase %d, packs %d)", cur, (int32_t)ctx->gpr[4], g_phase, g_packs);
            if (g_phase >= 1) teardown(ctx);
            packs_release(ctx);
        }
        return;
    }
    if ((uint32_t)ctx->gpr[4] != cur) { say("a switch to heap layout %d is held back (layout %u stays)", (int32_t)ctx->gpr[4], cur); ctx->gpr[4] = cur; }
}

/* jsp_game.cpp jsp_find_enter: func_0032B230(layout, hash) is entered (every pane lookup of the game: kept short) */
void model_preview_find(ppu_context* ctx)
{
    const uint32_t lr = (uint32_t)ctx->lr;
    if (lr < 0x0018B000u || lr >= 0x0018F000u || !enabled()) return;
    const uint32_t lay = (uint32_t)ctx->gpr[3]; if (!ok(lay)) return;
    for (int i = 0; i < g_ngrid_layout; i++) if (g_grid_layout[i] == lay) return;
    if (g_ngrid_layout < 4) { g_grid_layout[g_ngrid_layout++] = lay; say("a layout of the grid: %08X (pane lookup from %08X)", lay, lr - 4u); }
}

/* jsp_game.cpp jsp_vsc_setpic_enter: func_00333FCC(pane, picture) is entered */
void model_preview_setpic(ppu_context* ctx)
{
    if (!enabled()) return;
    const uint32_t lr = (uint32_t)ctx->lr, pane = (uint32_t)ctx->gpr[3];
    if (lr < 0x00185000u || lr >= 0x001A2000u || !ok(pane)) return;
    for (int i = 0; i < g_npanes; i++) if (g_panes[i].pane == pane) { g_panes[i].tex = (uint32_t)ctx->gpr[4]; return; }
    if (g_npanes >= 64) return;
    SeenPane& q = g_panes[g_npanes++]; q.pane = pane; q.lr = lr; q.tex = (uint32_t)ctx->gpr[4]; q.name[0] = 0;
    const uint32_t np = vm_read32(pane + 0x104u);
    if (ok(np)) { int k = 0; for (; k < 39; k++) { const char c = (char)vm_base[np + (uint32_t)k]; if (c < 32 || c > 126) break; q.name[k] = c; } q.name[k] = 0; }
    if (logon()) say("pane %08X '%s' gets a picture %08X (caller %08X)", pane, q.name, q.tex, lr - 4u);
    if (lr == 0x0018D9D0u) {   /* a face of the grid: its layout's root pane */
        uint32_t r = pane; for (int k = 0; k < 16; k++) { const uint32_t up = vm_read32(r + 4u); if (!ok(up)) break; r = up; }
        bool have = r == pane; for (int i = 0; i < g_ngrid; i++) have |= g_grid_root[i] == r;
        if (!have && g_ngrid < 4) { g_grid_root[g_ngrid++] = r; say("the grid's root pane %d: %08X (visible word %08X)", g_ngrid, r, vm_read32(r + 0x100u)); }
    }
}

/* jsp_game.cpp training_icall: every indirect call of the game */
void model_preview_icall(uint32_t target, uint64_t* gpr)
{
    if (!enabled()) return;   /* off, or a rollback run: nothing below runs */
#if JSP_EXPERIMENTS
    /* MODEL_PREVIEW_WHO=<hex target> (probe): where an indirect call comes from (the return address and r3 .. r6), 12 times */
    { static const uint32_t who = (uint32_t)std::strtoul(std::getenv("MODEL_PREVIEW_WHO") ? std::getenv("MODEL_PREVIEW_WHO") : "0", nullptr, 16);
      if (who && target == who) { static unsigned n = 0; if (n++ < 12) { const ppu_context* c = reinterpret_cast<const ppu_context*>(reinterpret_cast<const char*>(gpr) - offsetof(ppu_context, gpr));
          say("target %08X called from %08X: r3 %08X r4 %08X r5 %08X r6 %08X", target, (uint32_t)c->lr - 4u, (uint32_t)gpr[3], (uint32_t)gpr[4], (uint32_t)gpr[5], (uint32_t)gpr[6]); } } }
#endif
    /* Drawing. In the Gallery the scene's own virtuals draw the viewer: +0x18 -> func_001B8958(viewer), +0x38 (once a
     * pass, 0..5 and 7) -> func_001B99A4(viewer, pass), +0x3C (once a pass) -> func_001B4B0C(viewer, pass). The Progress
     * scene has the library's empty virtuals there (func_0064D294 / func_0064D214 / func_0064D204), which mark the node
     * (+0x20 bits 0x10 / 0x1000 / 0x2000) so the tree walk never calls them again. scene_draw_on() gives the scene a
     * copy of its vtable with three do-nothing functions in those slots and clears the bits: the walk then calls
     * them at the scene's own place in the frame, and they are seen here with the scene in r3.
     * MODEL_PREVIEW_DRAW=<bits> (default 54): 32 the window part draws its layout, 16 the Gallery scene's own draw function on a stand-in scene (see
     * g_fscene), 2 func_001B4B0C, 4 func_001B8958; the first tries: 1 func_001B99A4 called by us, 8 func_001B683C. */
    if ((target == 0x0071C030u || target == 0x0071C040u || target == 0x0071C044u) && g_scene_vt && (uint32_t)gpr[3] == g_scene && g_viewer && !t_in_viewer) {
        static const int draw = envi("MODEL_PREVIEW_DRAW", 54);
        const ppu_context* c = reinterpret_cast<const ppu_context*>(reinterpret_cast<const char*>(gpr) - offsetof(ppu_context, gpr));
        if (logon()) { static unsigned n = 0; if (n++ < 24) say("draw call %08X pass %d (viewer state %d)", target, (int32_t)gpr[4], (int32_t)vm_read32(g_viewer)); }
        t_in_viewer = true;
        if (target == 0x0071C030u) {
            /* as the Gallery scene's +0x18 (func_001BBB88): its controller's +0x14, which is every part's +0x14 = the part
             * draws its layout (func_002F94E0 -> func_00329EDC(layout, alpha) -> func_0032BE98), then func_001B8958 */
            if (g_part && (draw & 32) && window_up()) call(func_002F94E0, c, g_part);
            if (draw & 4) call(func_001B8958, c, g_viewer);
        }
        else if (target == 0x0071C040u) {
            /* as the Gallery scene's +0x38 (func_001BD9F4): every pass but 3 and 5 draws the 3D side; at pass 7, with a
             * model shown, the offscreen picture is put on the screen (the scene has that code inline; func_001B683C
             * is the viewer's own copy of it: bit 8 of MODEL_PREVIEW_DRAW), then passes 3 and 5 are drawn */
            const uint32_t pass = (uint32_t)gpr[4];
            if (draw & 16) { if (!late()) { if (pass == 7u) composite(c, 0); call(func_001BD9F4, c, g_fscene, pass); } }   /* the Gallery scene's own draw, on the stand-in scene */
            else {
                if ((draw & 1) && pass != 3u && pass != 5u) call(func_001B99A4, c, g_viewer, pass);
                if (pass == 7u && !late()) composite(c, draw);
            }
        }
        else {   /* +0x3C: after the scene's children have drawn this pass. MODEL_PREVIEW_LATE=1 (default): the drawing
                  * goes here, over Character Select's own pictures, instead of before them as in the Gallery */
            const uint32_t pass = (uint32_t)gpr[4];
            if ((draw & 16) && late()) { if (pass == 7u) composite(c, 0); call(func_001BD9F4, c, g_fscene, pass); }
            if (draw & 2) call(func_001B4B0C, c, g_viewer, pass);
            if (!(draw & 16) && pass == 7u && late()) composite(c, draw);
        }
        t_in_viewer = false;
        return;
    }
    /* Character Select's own parts draw through the same func_002F94E0(part) (part +0x1C = visible, +0x28 its layout).
     * While the window is up, the parts listed in MODEL_PREVIEW_HIDE (vtables, hex, comma separated; default: the grid's)
     * get +0x1C = 0 just before they draw; they get 1 back when the window goes. Every part seen is logged once. */
    if (target == 0x002F94E0u && enabled() && GetTickCount64() - g_cs_ms < 300) {
        if (g_grid_hidden) grid_hide_named();   /* the Support badges, shown again by the game since the tick */
        const uint32_t part = (uint32_t)gpr[3];
        if (ok(part)) {
            const uint32_t vt = vm_read32(part);
            bool have = false; for (int i = 0; i < g_ncs_parts; i++) have |= g_cs_parts[i] == part;
            if (!have && g_ncs_parts < 48) { g_cs_part_state[g_ncs_parts] = 0xFFFFFFFFu; g_cs_parts[g_ncs_parts++] = part; const ppu_context* c = reinterpret_cast<const ppu_context*>(reinterpret_cast<const char*>(gpr) - offsetof(ppu_context, gpr));
                say("a part draws: %08X vtable %08X layout %08X visible %u (caller %08X)", part, vt, vm_read32(part + 0x28u), vm_read32(part + 0x1Cu), (uint32_t)c->lr - 4u); }
            static uint32_t hide[8]; static int nhide = -1;
            if (nhide < 0) { nhide = 0; const char* e = std::getenv("MODEL_PREVIEW_HIDE");   /* default: the grid's page line (L1 01/07 R1) and the dark panel behind the grid */
                for (const char* q = e ? e : "0087C0B8,0087C508"; *q && nhide < 8;) { char* end = nullptr; const uint32_t v = (uint32_t)std::strtoul(q, &end, 16); if (end == q) break; hide[nhide++] = v; q = *end ? end + 1 : end; } }
            bool mine = false; for (int i = 0; i < nhide; i++) mine |= hide[i] == vt;
            if (mine) {
                /* +0x1C is written to 0 while the window is up and back to 1 once, when it is not (any more): a part this file did
                 * not hide is left alone (the 2 Players, 3 vs 3 and Online screens draw these parts as the game wants them) */
                static uint32_t hid[8]; static int nhid = 0;
                const bool up = g_no_preview ? false : car_mode() ? (g_phase >= 0 && !g_support_grid) : (g_part && g_phase >= 3);
                int at = -1; for (int i = 0; i < nhid; i++) if (hid[i] == part) at = i;
                if (up) { if (at < 0 && nhid < 8) { hid[nhid++] = part; at = nhid - 1; } if (at >= 0) vm_write32(part + 0x1Cu, 0u); }
                else if (at >= 0) { vm_write32(part + 0x1Cu, 1u); hid[at] = hid[--nhid]; }
            }
        }
        return;
    }
    if (target != 0x0019AA74u && target != 0x00153040u) return;
    if (!enabled()) return;
    const uint32_t obj = (uint32_t)gpr[3];
    if (!ok(obj)) return;
    if (target == 0x00153040u) {
        const uint32_t vt = vm_read32(obj); if (vt == 0x0087AC78u || (g_scene_vt && vt == g_scene_vt)) g_scene = obj;
        /* the scene's own frame: is Character Select still there? */
        const ppu_context* c = reinterpret_cast<const ppu_context*>(reinterpret_cast<const char*>(gpr) - offsetof(ppu_context, gpr));
        /* Character Select is gone when the scene has had three frames without Character Select's own (it was 300 ms
         * of clock: the window stood alone for the first pictures of the way out
         * b01, b02; frames do not mistake a long frame for leaving) */
        static const unsigned gone = (unsigned)envi("MODEL_PREVIEW_GONE", 3);
        if (g_phase >= 1) g_cs_miss++;
        if (g_phase >= 1 && !t_in_viewer && g_cs_miss >= gone && envi("MODEL_PREVIEW_TEARDOWN", 1)) {
            /* The model goes first, the way the viewer itself lets one go (request word 2: state 3 "unloading", then
             * 0), with the viewer's frame run from here meanwhile; only then the take-down. The destructor on a
             * viewer with a model still loaded is the likely cause of the dropped null writes in func_005228AC right
             * after leaving (a shader table of an object among the viewer's own was
             * 0 while the 3D tree walk still drew it). MODEL_PREVIEW_UNLOAD=0: the take-down at once, as before. */
            static const bool unload = envi("MODEL_PREVIEW_UNLOAD", 1) != 0;
            const uint32_t vs = g_viewer && g_phase >= 3 ? vm_read32(g_viewer) : 0u;
            if (unload && vs != 0u && vs <= 3u && g_close_ticks < 240u) {
                if (g_close_ticks++ == 0) { random_text(false); say("leaving: the model is unloaded first (viewer state %u)", vs); }
                if ((vs == 1u || vs == 2u) && vm_read32(g_viewer + 0x0Cu) == 0u) vm_write32(g_viewer + 0x0Cu, 2u);
                viewer_call(func_001B65D8, c, g_viewer);
                return;
            }
            if (g_close_ticks) say("the model was unloaded in %u frames (viewer state now %u)", g_close_ticks, vs);
            teardown(c);
        }
        /* a menu is running and Character Select is not up: the packs are asked for ahead of time */
        else if (g_phase == 0 && preload() && g_room_ok && !t_in_viewer && GetTickCount64() - g_cs_ms > 300 && vm_read32(0x00956550u) != 4u) {   /* not with the cursor on Online Battle: its menus get nothing of ours */
            if (g_packs == 0) { if (packs_ask(c, false)) say("the packs are asked for ahead of time (a menu on heap layout 3)"); }
            else if (g_packs == 1) packs_check(c);
        }
        return;
    }
    if (vm_read32(obj) != 0x0087C3D0u) return;
    const unsigned long long now = GetTickCount64();
    if (obj != g_cs || now - g_cs_ms > 500) {   /* a new Character Select (or the first one) */
        if (g_phase > 0 && logon()) say("Character Select object %08X (was %08X, %llu ms ago)", obj, g_cs, now - g_cs_ms);
        if (g_phase == 0) { g_cs = obj; g_cs_ticks = 0; g_no_preview = false; }
    }
    if (obj != g_cs) return;
    g_cs_ms = now; g_cs_ticks++; g_cs_miss = 0;
    /* Where the preview is made: only on the
     * Character Select screens it was built and tested on, Free Battle and Training (the main menu's row word
     * 0x00956550 = 3) with one player and two fighters a team. Everywhere else the game's own grid stays:
     *  - 3 vs 3: the third box of each team stands where the window would be;
     *  - 2 Players: each player has his own grid there (a slot's controller word, record +0x10, is 1 for Player 2),
     *    one hidden grid would leave Player 2 blind;
     *  - the Impossible modes, Arcade, Victory Road, J-Adventure, Online: not tried yet.
     * The slot records are the last screen's for a moment, so the answer is taken in the third frame; until then the
     * grid is hidden on trust and comes back at once where the answer is no.
     * MODEL_PREVIEW_ANYWHERE=1 (test) drops the mode checks, not the 3 vs 3 and 2 Players ones. */
    if (g_no_preview) return;
    if (g_phase == 0 && g_cs_ticks == 3u) {
        static const bool anywhere = envi("MODEL_PREVIEW_ANYWHERE", 0) != 0;
        const char* why = nullptr;
        if (jsp_cs_is_3v3()) why = "3 vs 3";
        else { for (uint32_t q = 0; q < 6u && !why; q++) if (vm_read32(0x009540E8u + 0x2Cu * q + 0x10u) == 1u) why = "2 Players"; }
        /* The other modes get the viewer too, for 1 player only. The main
         * menu's row word: 0 J-Adventure (no Character Select), 1 Victory Road, 2 Arcade, 3 Free Battle (Training and
         * the Impossible modes come through it), 4 Online Battle. Online stays out: not testable here, and the other
         * side would not see what the hidden grid's rewritten table does. MODEL_PREVIEW_MODES=<bits> says which
         * rows get it. */
        /* 110 = rows 1, 2, 3 and 5, 6: on the two-page menu the word holds the Battle Modes page's row once the game
         * has come back from a Character Select, 5 for Training Mode and 6 for Impossible Mode (measured: "menu row word 5"). The game's own rows 5 and 6
         * (J-Customize, J-Point Shop) have no Character Select. */
        static const unsigned modes = (unsigned)envi("MODEL_PREVIEW_MODES", 110);
        const uint32_t row = vm_read32(0x00956550u);
        /* Training and the Impossible modes come through Free Battle's screens, and their row word is not always 3: on
         * the two-page menu it holds the mode's own row once the game has come back from Character Select (after 1 Player, 2 Players, 1 Player
         * the viewer was missing in those two modes: "this mode is not on the list"). So they are asked from jsp_game.cpp and go with Free Battle's bit. */
        const bool as_free_battle = jsp_tr_active() != 0 || jsp_imp_mode() != 0;
        if (!why && !anywhere && !(as_free_battle ? (modes >> 3 & 1u) != 0 : (row <= 31u && (modes >> row & 1u)))) why = row == 4u ? "Online Battle" : "this mode is not on the list";
        if (why) { g_no_preview = true; if (g_grid_hidden) grid_show(true);
            say("Character Select: no preview here (%s), the grid stays (menu row word %u, Training %d, Impossible %d, controller words %u %u %u %u %u %u)", why,
                row, jsp_tr_active(), jsp_imp_mode(),
                vm_read32(0x009540E8u + 0x10u), vm_read32(0x009540E8u + 0x2Cu + 0x10u), vm_read32(0x009540E8u + 0x58u + 0x10u), vm_read32(0x009540E8u + 0x84u + 0x10u), vm_read32(0x009540E8u + 0xB0u + 0x10u), vm_read32(0x009540E8u + 0xDCu + 0x10u));
            return; }
    }
    /* the grid is hidden from the first frame, unless the preview could not be made */
    if (car_mode() && window_on() && g_phase >= 0 && g_phase < 3 && grid_roots_from_layouts() && envi("MODEL_PREVIEW_HIDEGRID", 1)) grid_show(false);
    if (g_phase < 0 && g_grid_hidden) grid_show(true);
    if (t_in_viewer || g_phase < 0) return;
    if (g_phase == 0 && g_cs_ticks < 3u) return;   /* the answer to "is the preview made here" comes in the third frame (above) */
    tick(reinterpret_cast<const ppu_context*>(reinterpret_cast<const char*>(gpr) - offsetof(ppu_context, gpr)));
}
#endif  // JSP_US_CODE
