/* More Training options: no cooldown for Support call-ins, infinite Victory Burst, new CPU options, and placement resets.
 *
 * Rows added to the pause menu's Training settings (jsp_game.cpp asks train_opt_count / _line / _press):
 *   CPU: Stand / Guard / Jump / Attack / Fights back
 *       What the 0-star training dummy does. Stand is the dummy as before (its pad handed to a human driver, nothing
 *       pressed). For the others the pad goes back to the CPU driver (pad object +0x18 and +0x1FC = 3) and the dummy's
 *       AI unit is driven by hand: at the think step func_0001F758 the think
 *       flag (unit +0x14 bit 2) is cleared and the virtual pad's word (unit +0x5C -> +0x20) written every frame:
 *       Guard = R1 held (bit 8), Jump = Cross (bit 7) once every 1.2 s, Attack = Square (bits 4 | 13) every 0.4 s.
 *       Fights back = the AI thinks again, at the stars chosen on Character Select (0 stars = the weakest level).
 *   Reset to start positions   (also Select + Cross in the fight)
 *       Every fighter goes back to where it stood when the fight began.
 *   Reset close together       (also Select + Circle in the fight)
 *       Player 1 on its start spot, the other team TRAIN_CLOSE10 / 10 (default 2.5) in front of it, on the line toward
 *       the other team's start spot and at Player 1's height (the spot the held Training dummy used: flat ground on
 *       every stage tried); team mates stand 1.2 apart, side by side.
 *   Support cooldown: On / Off
 *       Off = a Support can be called again as soon as it has left. The cooldown is a timer object (class 0x00886128:
 *       +0x08 value counting up, +0x14 its end) at +0x1C of the object func_0007E95C works on; that function reads the
 *       Support's own time from a table and multiplies card effect 5 in, then starts the timer
 *       through func_00491F98. With the row at Off every such timer seen in the fight has its value put at its end.
 *       TRAIN_NOCOOL=1 (test): Off from the start. (The object at the global 0x00952198 is the Victory Burst manager,
 *       not this: tried first)
 *   Infinite Victory Burst: Off / On
 *       On = both teams' Victory gauges are held full, so R3 starts a Victory Burst at any time. The gauges are in the
 *       Victory Burst manager, the object the global at 0x00952198 (TOC - 0x7638) points to: +0x128 team 0, +0x12C team
 *       1, full at 7500 (the HUD's voltage arc, class 0x0087A1E0, copies them to its +0x2C / +0x30 and holds 7500 at
 *       +0x34 / +0x38). The manager's own timers (class
 *       0x00886128 at +0x18 and +0x30) are the burst's 30 s and the 7 s the R3 prompt stays up; both are held at their
 *       start while they run (TRAIN_BURST_HOLD=0: not held), so the prompt stays up and a burst lasts until its
 *       Ultimate Attack. Logged with TRAIN_OPT_LOG=1. TRAIN_BURST=1 (test): On from the start.
 * While Select is held in a Training fight, the fighters get no buttons from that pad (Start still pauses).
 * The game itself does not read Select in a fight (it is not in the game's button word, jsp_game.cpp tb_raw).
 * No fighter intros in Training (Training Mode only): once the loading is
 * over (scene word 0xB or more) Cross + Start are pressed for Player 1, 120 ms in every 600, which is the game's own
 * intro skip, until Player 1 enters its first state (func_0004296C with +4 = 0: it can
 * move) or 20 s have passed. TRAIN_INTROS=1 keeps the intros.
 * Position keys: bring the CPU close at any place on the map (Select + Triangle), and a save state (Select + R1) that
 * saves the positions of both the player and the CPU and puts them back later, without locking the CPU in place:
 *   Select + Triangle   the other team comes to Player 1: onto the spot Player 1 walked over TRAIN_CLOSE10 / 10 (2.5)
 *       ago (ground that is known to be free, at its own height); see ask_call for the case without a trail.
 *   Select + R1         remembers where every fighter stands now (until the fight is left); "Positions saved" shows for
 *       a moment on the overlay layer.
 *   Select + Square     everyone back to the saved positions ("No saved positions" shows when there are none). (Select + Circle
 *       is close together, always.)
 *   Positions are only written for a few pad reads, so nobody is held in place afterwards.
 * TRAIN_OPTS=0 turns all of this off. TRAIN_CPU=<0..4> (test): the CPU row's value from the start. TRAIN_OPT_LOG=1. */
#include <cstddef>
#include <cstdint>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#endif
#include "ppu_recomp.h"
#include "jsp_overlay_api.h"

extern "C" int jsp_tr_active(void);     // jsp_game.cpp: plain Training (not Turn-based)
extern "C" int jsp_tr_fight(void);      // a fight's health bars are up
extern "C" int jsp_tr_paused(void);     // the pause menu is open
extern "C" int jsp_tr_fighters(uint32_t* health, uint32_t* place, int* slot, int* team, int max);
extern "C" int jsp_tr_dummy_pads(uint32_t* pads, int max);
extern "C" int jsp_tr_unit_of_slot(int slot);
extern "C" int jsp_tr_cpu_stars(void);   // jsp_game.cpp: the first CPU's stars from Character Select (0 = the dummy)
__attribute__((weak)) int train_menu_on(void);   // feat_training_menu.cpp: the two-window Training menu is in use
// jsp_game.cpp where the build has the Training readout (damage, combo, health; top right): the readout's box drawn inside this file's
// drawing, s = picture height / 720. The overlay is one layer and every drawing replaces the last, so whoever has something on it for
// the whole fight (the key line here) draws the other's part too. Absent: nothing.
extern "C" __attribute__((weak)) int jsp_tr_readout_draw(float s);

namespace {
int envi(const char* k, int d) { const char* e = std::getenv(k); return e && *e ? std::atoi(e) : d; }
bool enabled() { static const bool v = envi("TRAIN_OPTS", 1) != 0; return v; }
bool logon() { static const bool v = envi("TRAIN_OPT_LOG", 0) == 1; return v; }
bool ok(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u && !(a & 3u); }
float fget(uint32_t ea) { union { float f; uint32_t u; } c; c.u = vm_read32(ea); return c.f; }
void fput(uint32_t ea, float v) { union { float f; uint32_t u; } c; c.f = v; vm_write32(ea, c.u); }
uint32_t scene() { return vm_read32(0x0094DEC4u); }

enum { ROW_CPU, ROW_RESET_START, ROW_RESET_CLOSE, ROW_SUPPORT, ROW_BURST, ROW_COUNT };
enum { CPU_STAND, CPU_GUARD, CPU_JUMP, CPU_ATTACK, CPU_FIGHT, CPU_COUNT };
const char* const k_cpu[CPU_COUNT] = { "Stand", "Guard", "Jump", "Attack", "Fights back" };
int g_cpu = envi("TRAIN_CPU", 0) % CPU_COUNT;
/* With the two-window Training menu (feat_training_menu.cpp) the CPU has two settings: CPU Level, 0 to 5 stars, taken
 * from Character Select at the start of each fight and changed live in the menu, and CPU Behavior (Stand / Guard /
 * Jump / Attack) for 0 stars. 1 to 5 stars: the AI fights at the level the game gives those stars (AI unit +0x20 =
 * 0, 2, 7, 12, 16). Every CPU fighter of the fight follows them, and all of it goes through the
 * AI units (Stand = thinking off and nothing pressed), so a CPU can be switched between dummy and fighter in the
 * fight. -1: the old single CPU row. */
int g_stars = -1;
int eff_mode() { return g_stars < 0 ? g_cpu : g_stars > 0 ? CPU_FIGHT : (g_cpu > CPU_ATTACK ? CPU_STAND : g_cpu); }
bool g_nocool = envi("TRAIN_NOCOOL", 0) == 1;   /* Support cooldown: Off */
bool g_burst = envi("TRAIN_BURST", 0) == 1;     /* Infinite Victory Burst: On */

/* This fight's fighters: their places and where each stood when the fight began. Kept through the pause menu (the
 * health bars are gone there, and jsp_game.cpp forgets its own lists); forgotten when the scene word shows a menu or
 * a loading screen (below 0xB) or Training is left. */
struct Spot { uint32_t place; int slot, team; float x, y, z; };
Spot g_spot[4]; int g_nspot = 0;
uint32_t g_dpad[4]; int g_ndpad = 0;   /* the training dummies' pad objects, remembered the same way */
uint32_t g_held[4]; int g_nheld = 0;   /* AI units whose think flag this file cleared */
int g_reset = 0, g_reset_polls = 0;    /* 1 start spots, 2 close together, 3 the goals below; written on this many pad reads */
struct P3 { float x, y, z; };
P3 g_save[4]; bool g_saved = false;    /* Select + R1: where everyone stood */
P3 g_goal[4]; bool g_goal_on[4];       /* kind 3: where each fighter is put (the other team called to Player 1, or the saved positions) */
unsigned long long g_toast_until = 0;  /* a line of ours is on the overlay layer until then */
unsigned long long g_report_at = 0;    /* TRAIN_OPT_LOG: the places are logged then (a second after a placement) */
P3 g_trail[96]; int g_ntrail = 0, g_trail_at = 0; P3 g_trail_prev; bool g_trail_have = false;   /* Player 1's trail (trail_step) */
void trail_clear() { g_ntrail = 0; g_trail_at = 0; g_trail_have = false; }

bool g_p1_moved = false;               /* this fight: Player 1 has entered its first state (the intros are over) */
unsigned long long g_fight_scene_ms = 0;   /* when the scene word first showed the fight's scenes */
uint32_t g_sup[8]; int g_nsup = 0;     /* Support cooldown timers seen this fight (set through func_00491F98) */
void forget() { g_nspot = 0; g_ndpad = 0; g_nheld = 0; g_reset = 0; g_reset_polls = 0; g_nsup = 0; g_saved = false; g_report_at = 0; trail_clear(); }

void spots_take()
{
    uint32_t h[4], p[4]; int s[4], t[4];
    const int n = jsp_tr_fighters(h, p, s, t, 4);
    if (n < 2) return;
    g_nspot = n;
    for (int k = 0; k < n; k++) { g_spot[k].place = p[k]; g_spot[k].slot = s[k]; g_spot[k].team = t[k];
        g_spot[k].x = fget(p[k]); g_spot[k].y = fget(p[k] + 4); g_spot[k].z = fget(p[k] + 8); }
    std::printf("[trainopt] start spots:");
    for (int k = 0; k < n; k++) std::printf(" fighter %d (slot %d team %d) %.1f %.1f %.1f", k, s[k], t[k], g_spot[k].x, g_spot[k].y, g_spot[k].z);
    std::printf("\n"); std::fflush(stdout);
}

void put(const Spot& f, float x, float y, float z)
{
    if (!ok(f.place)) return;
    fput(f.place, x); fput(f.place + 4, y); fput(f.place + 8, z);
    fput(f.place + 0x10, x); fput(f.place + 0x14, y); fput(f.place + 0x18, z);
}

void reset_write(int kind)
{
    if (g_nspot < 2) return;
    if (kind == 1) { for (int k = 0; k < g_nspot; k++) put(g_spot[k], g_spot[k].x, g_spot[k].y, g_spot[k].z); return; }
    if (kind == 3) { for (int k = 0; k < g_nspot; k++) if (g_goal_on[k]) put(g_spot[k], g_goal[k].x, g_goal[k].y, g_goal[k].z); return; }
    /* close together: fighter 0 is Player 1; the line goes to the first fighter of the other team */
    static const float apart = (float)envi("TRAIN_CLOSE10", 25) / 10.0f, side = 1.2f;
    const Spot& a = g_spot[0];
    int b = -1; for (int k = 1; k < g_nspot && b < 0; k++) if (g_spot[k].team != a.team) b = k;
    if (b < 0) return;
    float dx = g_spot[b].x - a.x, dz = g_spot[b].z - a.z; const float len = std::sqrt(dx * dx + dz * dz);
    if (len < 0.01f) { dx = 1.0f; dz = 0.0f; } else { dx /= len; dz /= len; }
    int count[2] = { 0, 0 }, seen[2] = { 0, 0 };
    for (int k = 0; k < g_nspot; k++) count[g_spot[k].team == a.team ? 0 : 1]++;
    for (int k = 0; k < g_nspot; k++) {
        const int sd = g_spot[k].team == a.team ? 0 : 1;
        const float off = ((float)seen[sd]++ - (float)(count[sd] - 1) * 0.5f) * side;   /* team mates side by side, across the line */
        const float fwd = sd ? apart : 0.0f;
        /* Player 1's own team keeps Player 1 exactly on its start spot when it is alone */
        put(g_spot[k], a.x + dx * fwd - dz * off, a.y, a.z + dz * fwd + dx * off);
    }
}

/* Is this AI unit a training dummy's? A pad object's +0x14 is the fighter number it asks the AI manager for
 * (func_00024B00 -> func_000202DC), the same number as the AI unit's +0x18: 1 for the opponent of a
 * 1 vs 1 (matching through the setup slot found no unit). */
bool dummy_unit(uint32_t unit_index)
{
    for (int k = 0; k < g_ndpad; k++)
        if (ok(g_dpad[k]) && vm_read32(g_dpad[k]) == 0x00875858u && vm_read32(g_dpad[k] + 0x14) == unit_index) return true;
    return false;
}

void think_back()
{
    for (int i = 0; i < g_nheld; i++) if (ok(g_held[i])) vm_write32(g_held[i] + 0x14, vm_read32(g_held[i] + 0x14) | 2u);
    if (g_nheld && logon()) { std::printf("[trainopt] %d AI unit(s) thinking again\n", g_nheld); std::fflush(stdout); }
    g_nheld = 0;
}

void ask_reset(int kind, const char* how)
{
    g_reset = kind; g_reset_polls = 4;
    if (logon()) g_report_at = GetTickCount64() + 1000;
    std::printf("[trainopt] %s: %s\n", how, kind == 1 ? "Reset to start positions" : "Reset close together"); std::fflush(stdout);
}

P3 live(const Spot& f) { P3 p = { 0.0f, 0.0f, 0.0f }; if (ok(f.place)) { p.x = fget(f.place); p.y = fget(f.place + 4); p.z = fget(f.place + 8); } return p; }
void say_places(const char* what)
{
    std::printf("[trainopt] %s:", what);
    for (int k = 0; k < g_nspot; k++) { const P3 p = live(g_spot[k]); std::printf(" fighter %d (team %d) %.2f %.2f %.2f", k, g_spot[k].team, p.x, p.y, p.z); }
    std::printf("\n"); std::fflush(stdout);
}

/* The position keys along the bottom of the picture in a Training fight (so that a player who enters
 * Training Mode can see what the keys are; kept short). One line on the overlay layer, on a dark band;
 * TRAIN_HINTS=0: none. The layer is one picture, so the line is drawn again with every toast and after it. */
bool hints_on() { static const bool v = envi("TRAIN_HINTS", 1) != 0; return v; }
bool g_hint_up = false;   /* the line is on the layer (alone or with a toast) */

/* The game's own button pictures (the same pictures the Port Settings page uses). The menus' button bar gets them from the text system, which is not drawn in a fight, so the pictures
 * themselves are read: 35 pictures of 64 x 64, four bytes a pixel (A R G B) in Z order, 0x4000 bytes each, one after the
 * other in video memory: SELECT (two pictures side by side), START (two), Circle, Square, Triangle, Cross, L1, L2,
 * L stick, R1, R2, R stick, the sticks' directions, the d-pad, L3, R3 (Measured:
 * the first at 0xC00CF800; they are there from the start of the game, also in a fight). They are found by a
 * fingerprint of the Cross picture (numbers taken from that dump; no picture is carried in the port): first at the
 * place measured, else by a search of the first 4 MB of video memory in 0x800 steps. Not found: the line shows the
 * buttons' names instead. TRAIN_ICONS=0: names. */
const uint32_t kIconBytes = 0x4000u;
enum { IC_SELECT = 0, IC_CIRCLE = 4, IC_SQUARE = 5, IC_TRIANGLE = 6, IC_CROSS = 7, IC_R1 = 11 };
uint32_t g_icons = 0; bool g_icons_tried = false;   /* the first picture (SELECT's left half) */
uint64_t fnv64(const uint8_t* q, size_t n) { uint64_t h = 0xcbf29ce484222325ull; for (size_t i = 0; i < n; i++) h = (h ^ q[i]) * 0x100000001b3ull; return h; }
bool mem_in(uint32_t a, uint32_t n)
{
    for (uint32_t at = a & ~4095u; at < a + n; at += 4096u) { MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(vm_base + at, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false; }
    return true;
}
bool icon_is_cross(uint32_t a)
{
    if (!mem_in(a, kIconBytes) || fnv64(vm_base + a + 0x3000u, 0x100u) != 0x8B7C78FDA815F843ull) return false;   /* the middle of the picture first: cheap */
    return fnv64(vm_base + a, kIconBytes) == 0x14AD7BA39D6E37FAull;
}
uint32_t icons_base()
{
    static const bool on = envi("TRAIN_ICONS", 1) != 0;
    if (!on) return 0u;
    if (g_icons || g_icons_tried) return g_icons;
    g_icons_tried = true;
    const uint32_t first = 0xC00CF800u + IC_CROSS * kIconBytes;
    uint32_t cross = icon_is_cross(first) ? first : 0u;
    for (uint32_t a = 0xC0000000u; !cross && a < 0xC0400000u; a += 0x800u) if (icon_is_cross(a)) cross = a;
    if (cross && mem_in(cross - IC_CROSS * kIconBytes, 12u * kIconBytes) && fnv64(vm_base + cross - (IC_CROSS - IC_CIRCLE) * kIconBytes, kIconBytes) == 0x80A4F16B26196F40ull) g_icons = cross - IC_CROSS * kIconBytes;
    std::printf("[trainopt] button pictures: %s%08X\n", g_icons ? "found, the first at " : "not found (the names are shown), Cross at ", g_icons ? g_icons : cross); std::fflush(stdout);
    return g_icons;
}
/* one button's picture (n pictures side by side), scaled to the height h, onto the layer at x, y; returns its width */
int icon_blit(int first, int n, int x, int y, int h)
{
    static std::vector<uint8_t> src, dst;
    const int sw = 64 * n, sh = 64, dw = (int)((float)sw * (float)h / 64.0f + 0.5f); if (h < 4 || dw < 4 || h > 512) return 0;
    src.resize((size_t)sw * sh * 4u); dst.resize((size_t)dw * (size_t)h * 4u);
    for (int t = 0; t < n; t++) { const uint8_t* q = vm_base + g_icons + (uint32_t)(first + t) * kIconBytes;
        for (int yy = 0; yy < 64; yy++) for (int xx = 0; xx < 64; xx++) { uint32_t m = 0; for (int bit = 0; bit < 6; bit++) m |= (uint32_t)((xx >> bit) & 1) << (2 * bit) | (uint32_t)((yy >> bit) & 1) << (2 * bit + 1);
            uint8_t* o = src.data() + ((size_t)yy * sw + (size_t)(t * 64 + xx)) * 4u; o[0] = q[m * 4 + 1]; o[1] = q[m * 4 + 2]; o[2] = q[m * 4 + 3]; o[3] = q[m * 4]; } }
    const float sc = (float)h / 64.0f;   /* shrinking: every target pixel is the mean of the source pixels it covers, the colours weighted by their alpha */
    for (int yy = 0; yy < h; yy++) for (int xx = 0; xx < dw; xx++) {
        int x0 = (int)((float)xx / sc), x1 = (int)((float)(xx + 1) / sc), y0 = (int)((float)yy / sc), y1 = (int)((float)(yy + 1) / sc);
        if (x1 <= x0) x1 = x0 + 1; if (y1 <= y0) y1 = y0 + 1; if (x1 > sw) x1 = sw; if (y1 > sh) y1 = sh;
        float r = 0, g = 0, bl = 0, al = 0; int cnt = 0;
        for (int v = y0; v < y1; v++) for (int u = x0; u < x1; u++) { const uint8_t* q = src.data() + ((size_t)v * sw + (size_t)u) * 4u; r += q[0] * (float)q[3]; g += q[1] * (float)q[3]; bl += q[2] * (float)q[3]; al += q[3]; cnt++; }
        uint8_t* o = dst.data() + ((size_t)yy * dw + (size_t)xx) * 4u;
        if (al <= 0.0f || !cnt) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
        o[0] = (uint8_t)(r / al + 0.5f); o[1] = (uint8_t)(g / al + 0.5f); o[2] = (uint8_t)(bl / al + 0.5f); o[3] = (uint8_t)(al / (float)cnt + 0.5f); }
    jsp_overlay_blit_rgba(x, y, dw, h, dst.data(), dw * 4);
    return dw;
}
void hints_draw(float s, int ow, int oh)
{
    static const struct { const char* name; const char* what; int icon; } part[5] = { { "Cross", "Start spots", IC_CROSS }, { "Circle", "Close together", IC_CIRCLE },
        { "Triangle", "CPU to me", IC_TRIANGLE }, { "R1", "Save spot", IC_R1 }, { "Square", "Saved spot", IC_SQUARE } };
    const bool pics = icons_base() != 0;
    const float sc = 0.55f * s, gap = 24.0f * s, sp = 6.0f * s;
    const int ih = (int)(28.0f * s), iw = ih;   /* a button's picture is square; SELECT is two of them */
    const char* head = "SELECT +";
    float w = pics ? (float)(2 * iw) + sp + jsp_overlay_text_width("+", sc) + gap * 0.6f : jsp_overlay_text_width(head, sc) + gap * 0.6f;
    for (const auto& q : part) w += (pics ? (float)iw : jsp_overlay_text_width(q.name, sc)) + sp + jsp_overlay_text_width(q.what, sc) + gap;
    w -= gap;
    const int bh = (int)((pics ? 30.0f : 22.0f) * s), by = oh - bh - (int)(3.0f * s);
    float x = ((float)ow - w) * 0.5f; const float y = (float)by + ((float)bh - 12.0f * s) * 0.5f; const int iy = by + (bh - ih) / 2;
    jsp_overlay_fill_rect((int)(x - 16.0f * s), by, (int)(w + 32.0f * s), bh, JSP_RGBA(0, 0, 0, 150));
    if (pics) { x += (float)icon_blit(IC_SELECT, 2, (int)x, iy, ih) + sp; jsp_overlay_text_style(x, y, "+", sc, JSP_RGBA(255, 255, 255, 255), 0); x += jsp_overlay_text_width("+", sc) + gap * 0.6f; }
    else { jsp_overlay_text_style(x, y, head, sc, JSP_RGBA(255, 214, 0, 255), 0); x += jsp_overlay_text_width(head, sc) + gap * 0.6f; }
    for (const auto& q : part) {
        if (pics) x += (float)icon_blit(q.icon, 1, (int)x, iy, ih) + sp;
        else { jsp_overlay_text_style(x, y, q.name, sc, JSP_RGBA(255, 214, 0, 255), 0); x += jsp_overlay_text_width(q.name, sc) + sp; }
        jsp_overlay_text_style(x, y, q.what, sc, JSP_RGBA(255, 255, 255, 255), 0); x += jsp_overlay_text_width(q.what, sc) + gap;
    }
}
void hints_show()
{
    int ow = 0, oh = 0; jsp_overlay_output_size(&ow, &oh);
    if (ow <= 0 || oh <= 0 || !jsp_overlay_begin(0, 0)) return;
    hints_draw((float)oh / 720.0f, ow, oh);
    if (jsp_tr_readout_draw) jsp_tr_readout_draw((float)oh / 720.0f);
    jsp_overlay_end();
    g_hint_up = true;
}

/* a short line near the top of the picture, on the overlay layer (the game draws nothing for a saved position) */
char g_toast_text[64] = "";
bool toast_draw(const char* text)
{
    int ow = 0, oh = 0; jsp_overlay_output_size(&ow, &oh);
    if (ow <= 0 || oh <= 0 || !jsp_overlay_begin(0, 0)) return false;
    if (hints_on() && g_hint_up) hints_draw((float)oh / 720.0f, ow, oh);
    if (jsp_tr_readout_draw) jsp_tr_readout_draw((float)oh / 720.0f);
    const float s = (float)oh / 720.0f, tw = jsp_overlay_text_width(text, 0.9f * s);
    const int w = (int)(tw + 56.0f * s), h = (int)(40.0f * s), x = (ow - w) / 2, y = (int)(118.0f * s);
    jsp_overlay_fill_rect(x, y, w, h, JSP_RGBA(0, 0, 0, 255));
    jsp_overlay_fill_rect(x + (int)(3.0f * s), y + (int)(3.0f * s), w - 2 * (int)(3.0f * s), h - 2 * (int)(3.0f * s), JSP_RGBA(40, 40, 41, 255));
    jsp_overlay_text_style((float)x + 28.0f * s, (float)y + 8.0f * s, text, 0.9f * s, JSP_RGBA(255, 255, 255, 255), 0);
    jsp_overlay_end();
    return true;
}
void toast(const char* text)
{
    std::snprintf(g_toast_text, sizeof g_toast_text, "%s", text);
    if (toast_draw(g_toast_text)) g_toast_until = GetTickCount64() + 1400;
}

/* Select + R1: where everyone stands now */
void save_places(const char* how)
{
    if (g_nspot < 2) return;
    for (int k = 0; k < g_nspot; k++) g_save[k] = live(g_spot[k]);
    g_saved = true;
    std::printf("[trainopt] %s: positions saved\n", how); say_places("saved");
    toast("Positions saved");
}

/* Select + Square: everyone back to the saved positions */
void ask_saved(const char* how)
{
    if (!g_saved || g_nspot < 2) return;
    for (int k = 0; k < g_nspot; k++) { g_goal[k] = g_save[k]; g_goal_on[k] = true; }
    g_reset = 3; g_reset_polls = 4;
    if (logon()) g_report_at = GetTickCount64() + 1000;
    std::printf("[trainopt] %s: Reset to saved positions\n", how); std::fflush(stdout);
}

/* Player 1's trail: where it stood lately, a point every 0.25 it moved. The CPU is called onto a point of it, which is
 * ground Player 1 has just walked over at the right height. (The first try put the CPU 2.5 away on the line toward
 * where the CPU was: next to a cliff that spot was inside the rock and the game pushed the CPU up onto it, y 0.9 ->
 * 5.9 k3.) A jump of more than 3 between two reads is a placement: the trail starts again. */
void trail_step()
{
    if (g_nspot < 1) return;
    const P3 p = live(g_spot[0]);
    if (!g_trail_have) { g_trail_prev = p; g_trail_have = true; return; }
    const float jx = p.x - g_trail_prev.x, jy = p.y - g_trail_prev.y, jz = p.z - g_trail_prev.z;
    g_trail_prev = p;
    if (jx * jx + jz * jz > 9.0f) { g_ntrail = 0; g_trail_at = 0; return; }
    if (std::fabs(jy) > 0.12f) return;   /* rising or falling: not a place to stand */
    if (g_ntrail) { const P3& l = g_trail[(g_trail_at + 95) % 96]; const float dx = p.x - l.x, dz = p.z - l.z; if (dx * dx + dz * dz < 0.0625f) return; }
    g_trail[g_trail_at] = p; g_trail_at = (g_trail_at + 1) % 96; if (g_ntrail < 96) g_ntrail++;
}

/* Select + Triangle: the other team comes to Player 1 (fighter 0): onto the newest point of Player 1's trail that is
 * TRAIN_CLOSE10 / 10 (2.5) away, so right behind where Player 1 walked. With no such point (Player 1 has not walked
 * since the fight began or since a reset) it is put that far away on the line toward where it was, TRAIN_CALL_LIFT10 /
 * 10 (0.3) above Player 1's height. Player 1's own team stays where it is. */
void ask_call(const char* how)
{
    if (g_nspot < 2) return;
    static const float apart = (float)envi("TRAIN_CLOSE10", 25) / 10.0f, lift = (float)envi("TRAIN_CALL_LIFT10", 3) / 10.0f, side = 1.2f;
    const Spot& a = g_spot[0];
    int b = -1, others = 0, seen = 0;
    for (int k = 1; k < g_nspot; k++) if (g_spot[k].team != a.team) { if (b < 0) b = k; others++; }
    if (b < 0) return;
    const P3 pa = live(a), pb = live(g_spot[b]);
    P3 at = { 0.0f, 0.0f, 0.0f }; bool trail = false;
    for (int i = 1; i <= g_ntrail && !trail; i++) {
        const P3& t = g_trail[(g_trail_at + 96 - i) % 96];
        const float dx = t.x - pa.x, dz = t.z - pa.z;
        if (dx * dx + dz * dz >= apart * apart) { at = t; at.y += 0.05f; trail = true; }
    }
    float dx = (trail ? at.x : pb.x) - pa.x, dz = (trail ? at.z : pb.z) - pa.z; const float len = std::sqrt(dx * dx + dz * dz);
    if (len < 0.01f) { dx = 1.0f; dz = 0.0f; } else { dx /= len; dz /= len; }
    if (!trail) { at.x = pa.x + dx * apart; at.y = pa.y + lift; at.z = pa.z + dz * apart; }
    for (int k = 0; k < g_nspot; k++) {
        g_goal_on[k] = g_spot[k].team != a.team;
        if (!g_goal_on[k]) continue;
        const float off = ((float)seen++ - (float)(others - 1) * 0.5f) * side;
        g_goal[k].x = at.x - dz * off; g_goal[k].y = at.y; g_goal[k].z = at.z + dx * off;
    }
    g_reset = 3; g_reset_polls = 4;
    if (logon()) { say_places("before the call"); g_report_at = GetTickCount64() + 1000; }
    std::printf("[trainopt] %s: the CPU comes to Player 1 (%.2f %.2f %.2f), %s %.2f %.2f %.2f\n", how, pa.x, pa.y, pa.z,
                trail ? "onto Player 1's trail at" : "no trail, on the line toward it at", at.x, at.y, at.z); std::fflush(stdout);
}
}

/* feat_training_menu.cpp: the values behind its rows. what: 0 CPU Level (0-5 stars), 1 CPU Behavior (0 Stand, 1 Guard,
 * 2 Jump, 3 Attack), 2 Support cooldown off, 3 Infinite Victory Burst; set < 0 reads. train_opt_reset: 1 the start
 * positions, 2 close together. */
extern "C" int train_opt_value(int what, int set)
{
    int* v = what == 0 ? &g_stars : what == 1 ? &g_cpu : nullptr;
    bool* f = what == 2 ? &g_nocool : what == 3 ? &g_burst : nullptr;
    if (v) {
        if (set >= 0 && *v != set) { *v = set; std::printf("[trainopt] %s %d\n", what == 0 ? "CPU Level" : "CPU Behavior", set); std::fflush(stdout); }
        return what == 0 ? (*v < 0 ? 0 : *v) : (*v > CPU_ATTACK ? CPU_STAND : *v);
    }
    if (f) {
        if (set >= 0 && *f != (set != 0)) { *f = set != 0; std::printf("[trainopt] %s %s\n", what == 2 ? "Support cooldown" : "Infinite Victory Burst", what == 2 ? (*f ? "Off" : "On") : (*f ? "On" : "Off")); std::fflush(stdout); }
        return *f ? 1 : 0;
    }
    return 0;
}
/* 3 the CPU comes to Player 1, 4 the saved positions, 5 save the positions now; train_opt_saved: positions are saved */
extern "C" void train_opt_reset(int kind)
{
    if (kind == 1 || kind == 2) ask_reset(kind, "Training menu");
    else if (kind == 3) ask_call("Training menu");
    else if (kind == 4) ask_saved("Training menu");
    else if (kind == 5) save_places("Training menu");
}
extern "C" int train_opt_saved(void) { return g_saved ? 1 : 0; }

int train_opt_count(void) { return enabled() ? ROW_COUNT : 0; }

const char* train_opt_line(int i)
{
    static char line[64];
    switch (i) {
    case ROW_CPU: std::snprintf(line, sizeof line, "CPU: %s", k_cpu[g_cpu]); return line;
    case ROW_RESET_START: return "Reset to start positions";
    case ROW_RESET_CLOSE: return "Reset close together";
    case ROW_SUPPORT: std::snprintf(line, sizeof line, "Support cooldown: %s", g_nocool ? "Off" : "On"); return line;
    case ROW_BURST: std::snprintf(line, sizeof line, "Infinite Victory Burst: %s", g_burst ? "On" : "Off"); return line;
    default: return "";
    }
}

void train_opt_press(int i)
{
    switch (i) {
    case ROW_CPU: g_cpu = (g_cpu + 1) % CPU_COUNT; std::printf("[trainopt] %s\n", train_opt_line(i)); std::fflush(stdout); break;
    case ROW_RESET_START: ask_reset(1, "menu"); break;
    case ROW_RESET_CLOSE: ask_reset(2, "menu"); break;
    case ROW_SUPPORT: g_nocool = !g_nocool; std::printf("[trainopt] %s\n", train_opt_line(i)); std::fflush(stdout); break;
    case ROW_BURST: g_burst = !g_burst; std::printf("[trainopt] %s\n", train_opt_line(i)); std::fflush(stdout); break;
    default: break;
    }
}

/* jsp_game.cpp's Training readout: 1 = this file's key line or message is on the layer, and the readout's box is drawn with it
 * (jsp_tr_readout_draw), so the readout must not draw or close the layer itself. redraw: its text changed, or it is to go: drawn again now. */
int train_opt_layer(int redraw)
{
    if (!enabled() || !(g_hint_up || g_toast_until)) return 0;
    if (redraw) { if (g_toast_until) toast_draw(g_toast_text); else hints_show(); }
    return 1;
}

int train_opt_dummy_ai(void) { return enabled() && jsp_tr_active() && (g_stars >= 0 || g_cpu != CPU_STAND) ? 1 : 0; }

/* jsp_game.cpp training_icall: every indirect call of the game */
void train_opt_icall(uint32_t target, uint64_t* gpr)
{
    if (!enabled() || !jsp_tr_active()) return;
    if (target == 0x0004296Cu && !g_p1_moved && g_fight_scene_ms && ok((uint32_t)gpr[3]) && vm_read32((uint32_t)gpr[3] + 4) == 0) {
        g_p1_moved = true;
        std::printf("[trainopt] Player 1 can move %llu ms after the loading (intros %s)\n", GetTickCount64() - g_fight_scene_ms, envi("TRAIN_INTROS", 0) ? "kept" : "skipped"); std::fflush(stdout);
    }
    /* A timer (class 0x00886128: +0x04 state, +0x08 value counting up, +0x14 its end) is started through its vtable's
     * func_00491F98(timer, end). The Support cooldown is the timer at +0x1C of the object func_0007E95C works on (it
     * reads the Support's own time from a table and multiplies card effect 5 in). TRAIN_SUP_VT=<hex>
     * (default 00875098; 0 = any owner) is that owner's vtable; with TRAIN_OPT_LOG=1 every start is logged with its owner. */
    if (target == 0x00491F98u) {
        const uint32_t t = (uint32_t)gpr[3];
        if (ok(t) && t >= 0x10000000u && vm_read32(t) == 0x00886128u) {
            /* the Support's cooldown: owner class 0x00875098, 20.0 s for Support 205 (it is
             * started at each L2 call; with the row at Off three calls 14 s apart all brought the Support) */
            static const uint32_t want_vt = (uint32_t)std::strtoul(std::getenv("TRAIN_SUP_VT") ? std::getenv("TRAIN_SUP_VT") : "00875098", nullptr, 16);
            const uint32_t owner_vt = vm_read32(t - 0x1Cu);
            static unsigned said = 0;
            if (logon() && said++ < 60) { std::printf("[trainopt] timer %08X started (owner -0x1C: %08X vt %08X; -0x18: %08X) at %llu\n", t, t - 0x1Cu, owner_vt, vm_read32(t - 0x18u), GetTickCount64()); std::fflush(stdout); }
            if (owner_vt >= 0x00870000u && owner_vt < 0x00890000u && (!want_vt || owner_vt == want_vt)) {
                bool have = false; for (int q = 0; q < g_nsup; q++) have |= g_sup[q] == t;
                if (!have && g_nsup < 8) g_sup[g_nsup++] = t;
            }
        }
        return;
    }
    if (target != 0x0001F758u) return;
    const uint32_t u = (uint32_t)gpr[3];
    if (!ok(u) || (g_stars < 0 && !dummy_unit(vm_read32(u + 0x18)))) return;
    const int mode = eff_mode();
    if (mode == CPU_FIGHT || (g_stars < 0 && mode == CPU_STAND)) {   /* the AI thinks (old Stand: its pad is not read by the fighter) */
        for (int i = 0; i < g_nheld; i++) if (g_held[i] == u) { think_back(); break; }
        if (g_stars > 0) {   /* CPU Level: the AI level of these stars */
            static const uint32_t k_level[5] = { 0, 2, 7, 12, 16 };
            const uint32_t lv = k_level[g_stars > 5 ? 4 : g_stars - 1];
            if (vm_read32(u + 0x20) != lv) {
                if (logon()) { std::printf("[trainopt] AI unit %08X (unit %u): level %u -> %u (%d stars)\n", u, vm_read32(u + 0x18), vm_read32(u + 0x20), lv, g_stars); std::fflush(stdout); }
                vm_write32(u + 0x20, lv);
            }
        }
        return;
    }
    if (vm_read32(u + 0x14) & 2u) {
        bool have = false; for (int i = 0; i < g_nheld; i++) have |= g_held[i] == u;
        if (!have && g_nheld < 4) g_held[g_nheld++] = u;
        vm_write32(u + 0x14, vm_read32(u + 0x14) & ~2u);
        if (logon()) { std::printf("[trainopt] AI unit %08X (unit %u) thinking off: %s\n", u, vm_read32(u + 0x18), k_cpu[mode]); std::fflush(stdout); }
    }
    const uint32_t pad = vm_read32(u + 0x5C);
    if (!ok(pad)) return;
    const unsigned long long t = GetTickCount64();
    uint32_t w = 0;
    if (mode == CPU_GUARD) w = 1u << 8;
    else if (mode == CPU_JUMP) w = (t % 1200u) < 70u ? (1u << 7) : 0u;
    else if (mode == CPU_ATTACK) w = (t % 400u) < 70u ? ((1u << 13) | (1u << 4)) : 0u;
    vm_write32(pad + 0x20, w);
    for (uint32_t o = 0x30; o <= 0x3C; o += 4) vm_write32(pad + o, 0u);   /* both sticks still */
}

/* jsp_game.cpp game_pad_filter: every pad read */
void train_opt_pad(uint32_t port, uint16_t* b, uint16_t* lx, uint16_t* ly)
{
    (void)lx; (void)ly;
    if (!enabled() || port > 1) return;
    if (!jsp_tr_active() || scene() < 0xBu) {
        if (port == 0) { if (g_nspot || g_ndpad) forget(); g_p1_moved = false; g_fight_scene_ms = 0;
            if (g_hint_up) { g_hint_up = false; g_toast_until = 0; jsp_overlay_close(); }   /* the fight is over: the key line goes */
            g_icons = 0; g_icons_tried = false; }
        return;
    }
    if (port == 0) {   /* no intros: the game's own skip, pressed until Player 1 can move */
        static const bool keep = envi("TRAIN_INTROS", 0) == 1;
        const unsigned long long now = GetTickCount64();
        if (!g_fight_scene_ms) g_fight_scene_ms = now;
        if (!keep && !g_p1_moved && now - g_fight_scene_ms < 20000 && (now - g_fight_scene_ms) % 600u < 120u) { *b |= (uint16_t)(0x4000 | 0x0008); return; }
    }
    const bool fight = jsp_tr_fight() != 0, paused = jsp_tr_paused() != 0;
    if (port == 0) {
        if (fight && !g_nspot) {
            spots_take();
            if (g_nspot && train_menu_on && train_menu_on()) {   /* a new fight: the CPU Level Character Select gave */
                g_stars = jsp_tr_cpu_stars();
                std::printf("[trainopt] CPU Level %d stars from Character Select, CPU Behavior %s\n", g_stars, k_cpu[g_cpu > CPU_ATTACK ? CPU_STAND : g_cpu]); std::fflush(stdout);
            }
        }
        /* the dummies' pads, remembered for the fight */
        uint32_t pads[4]; const int n = jsp_tr_dummy_pads(pads, 4);
        for (int k = 0; k < n; k++) { bool have = false; for (int i = 0; i < g_ndpad; i++) have |= g_dpad[i] == pads[k];
            if (!have && g_ndpad < 4) g_dpad[g_ndpad++] = pads[k]; }
        /* human driver for Stand (as jsp_game.cpp made them), the CPU driver for the rest */
        const uint32_t want = (g_stars < 0 && g_cpu == CPU_STAND) ? 1u : 3u;
        for (int k = 0; k < g_ndpad; k++) {
            if (!ok(g_dpad[k]) || vm_read32(g_dpad[k]) != 0x00875858u) continue;
            if (vm_read32(g_dpad[k] + 0x18) != want) {
                vm_write32(g_dpad[k] + 0x18, want); vm_write32(g_dpad[k] + 0x1FC, want);
                if (logon()) { std::printf("[trainopt] pad %08X driver -> %u (%s)\n", g_dpad[k], want, k_cpu[eff_mode()]); std::fflush(stdout); }
            }
        }
        if (g_reset_polls > 0 && fight && !paused) { g_reset_polls--; reset_write(g_reset); }
        if (fight && !paused) trail_step();
        if (g_report_at && GetTickCount64() >= g_report_at) { g_report_at = 0; say_places("a second later"); }
        if (g_toast_until && GetTickCount64() >= g_toast_until) {   /* our line goes; the Training menu's windows are its own to close */
            g_toast_until = 0;
            if (!(paused && train_menu_on && train_menu_on())) { if (hints_on() && g_hint_up && fight && !paused) hints_show(); else { jsp_overlay_close(); g_hint_up = false; } }
        }
        /* the key line: up once Player 1 can move, off while the game is paused (the pause menu draws on the same
         * layer), and put back when something else has cleared the layer */
        if (hints_on()) {
            const bool want = fight && !paused && g_p1_moved;
            if (!want && g_hint_up) { g_hint_up = false; if (!g_toast_until && !(paused && train_menu_on && train_menu_on())) jsp_overlay_close(); }   /* paused with the Training menu: its windows are on the layer already */
            else if (want && !g_toast_until && (!g_hint_up || !jsp_overlay_active())) hints_show();
        }
        if ((g_burst || logon()) && fight) {   /* Infinite Victory Burst (with TRAIN_OPT_LOG=1 the manager is logged either way) */
            const uint32_t m = vm_read32(0x00952198u);
            if (ok(m) && m >= 0x10000000u && vm_read32(m + 0x18) == 0x00886128u) {
                static const int full = envi("TRAIN_BURST_FULL", 7500);
                if (logon()) {
                    static uint32_t last[8]; static unsigned said = 0; static unsigned long long t_last = 0;
                    const uint32_t now[8] = { vm_read32(m + 0x0C), vm_read32(m + 0x1C), vm_read32(m + 0x20), vm_read32(m + 0x2C), vm_read32(m + 0x34), vm_read32(m + 0x38), vm_read32(m + 0x128), vm_read32(m + 0x12C) };
                    bool ch = false; for (int q = 0; q < 8; q++) ch |= now[q] != last[q];
                    if (ch && said < 80 && GetTickCount64() - t_last >= 400) { said++; t_last = GetTickCount64();
                        for (int q = 0; q < 8; q++) last[q] = now[q];
                        std::printf("[trainopt] burst manager %08X: id %d | timer A state %u value %.1f end %.1f | timer B state %u value %.1f end %.1f | gauges %d %d\n", m, (int32_t)now[0],
                                    now[1], fget(m + 0x20), fget(m + 0x2C), now[4], fget(m + 0x38), fget(m + 0x44), (int32_t)now[6], (int32_t)now[7]); std::fflush(stdout); }
                }
                if (g_burst) {
                    for (uint32_t o = 0x128; o <= 0x12C; o += 4) if ((int32_t)vm_read32(m + o) < full) vm_write32(m + o, (uint32_t)full);
                    /* timer B (+0x30) is the 7 s the R3 prompt stays up once a gauge is full, timer A (+0x18) the burst
                     * itself, 30 s: both are kept at their start while they run, so
                     * the prompt stays and a burst only ends with its Ultimate Attack */
                    static const bool hold = envi("TRAIN_BURST_HOLD", 1) != 0;
                    if (hold && vm_read32(m + 0x34) == 1u && fget(m + 0x38) > 0.5f) fput(m + 0x38, 0.0f);
                    if (hold && vm_read32(m + 0x1C) == 1u && fget(m + 0x20) > 0.5f) fput(m + 0x20, 0.0f);
                }
            }
        }
        if (g_nocool && fight) {   /* Support cooldown: Off: every Support timer seen this fight is run to its end */
            for (int q = 0; q < g_nsup; q++) {
                const uint32_t t = g_sup[q];
                if (!ok(t) || vm_read32(t) != 0x00886128u) continue;
                const float val = fget(t + 0x08), end = fget(t + 0x14);
                if (end > 0.0f && val < end) { fput(t + 0x08, end);
                    static unsigned said = 0; if (said++ < 20) { std::printf("[trainopt] Support cooldown off: timer %08X %.1f -> %.1f\n", t, val, end); std::fflush(stdout); } }
            }
        }
    }
    /* Select held in the fight: Select + Cross / Circle reset the positions, Select + Triangle calls the CPU, Select + R1
     * saves the positions, and nothing else gets through but Start */
    static uint16_t prev[2] = { 0, 0 };
    const uint16_t SELECT = 0x0001, START = 0x0008, CROSS = 0x4000, CIRCLE = 0x2000, TRIANGLE = 0x1000, SQUARE = 0x8000, R1 = 0x0800, cur = *b, pressed = (uint16_t)(cur & ~prev[port]);
    prev[port] = cur;
    if (!fight || paused || !(cur & SELECT)) return;
    if (pressed & CROSS) ask_reset(1, "Select + Cross");
    else if (pressed & CIRCLE) ask_reset(2, "Select + Circle");
    else if (pressed & SQUARE) {   /* Square: the saved positions (Circle stays "close together") */
        if (g_saved) ask_saved("Select + Square"); else toast("No saved positions"); }
    else if (pressed & TRIANGLE) ask_call("Select + Triangle");
    else if (pressed & R1) save_places("Select + R1");
    *b = (uint16_t)(cur & START);
}
