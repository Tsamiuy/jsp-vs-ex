/* The two-window Training pause menu (in the style of a fighting game's practice mode menu). On by default; TRAIN_MENU=0: the old pause menu with its Training settings row.
 * Does nothing in a rollback run (jsp_tr_session).
 *
 * Start in a Training fight opens the game's own pause menu as before (that is what pauses the fight), but it is not
 * drawn (jsp_game.cpp ui_scale asks train_menu_hide for every HUD draw: the ones inside the pause panel's place go off
 * screen) and the port draws two windows in the middle of the screen on the overlay layer (toolkit 2517), in the look
 * of the old pause panel:
 *   - the smaller left window, "Training Menu": the categories, then Skill Table, Re-select Characters, Return to Main
 *     Menu. Up / Down move, Cross enters a category or does the row, Circle (or Start) closes the menu.
 *   - the bigger right window: the options of the category the selector is on. They only show (darkened) until Cross
 *     enters the category; then Up / Down move, Left / Right change the value (Cross too), Circle goes back. The
 *     window scrolls when a category has more rows than fit.
 *   - the window that is not in use is darkened.
 * "Close menu" and "Restart" are not rows any more. The rows that belong to the game are done by the game: the
 * port walks the hidden pause menu's selector to that row (Down presses until jsp_tr_pause_info says it is there) and
 * presses Cross; the game's own Yes / No box and Skill table then show as always, and our windows come back if the
 * pause list does. Closing is Cross on the hidden "Close menu".
 * The values are jsp_game.cpp's Training settings (jsp_tr_flag) and feat_training_opts.cpp's (train_opt_value /
 * train_opt_reset). TRAIN_MENU_LOG=1 logs the steps. 
 * The addresses and the pause menu's layout are the US disc's (the build the port is lifted from).
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
#include "ppu_recomp.h"
#include "jsp_region.h"
// US only for now (jsp_region.h): the two-window Training menu sits on hooks, guest addresses and lifted functions of the US 1.00 code, so an EU build
// compiles none of this file. Its callers (jsp_game.cpp, the other feat_*.cpp) reach it through weak symbols, which an EU build
// leaves null: the feature is simply absent; its lift hooks are in the US lift only (firstrun.py, lift_game_hooks.json).
#if JSP_US_CODE
#include "jsp_overlay_api.h"

extern "C" int jsp_tr_active(void);                       // jsp_game.cpp: plain Training
extern "C" int jsp_tr_pause_info(int* depth, int* row);   // the game's pause menu: depth 0 = its list, row from 1
extern "C" void jsp_tr_pause_set_row(int row);            // puts the hidden list's selector on a row
extern "C" int jsp_tr_flag(int which, int set);           // 0 Unlimited Stamina, 1 Free Camera, 2 Hide HUD, 3 Group Camera
extern "C" int jsp_tr_has_groupcam(void);
extern "C" int train_opt_value(int what, int set);        // feat_training_opts.cpp: 0 CPU Level, 1 CPU Behavior, 2 Support cooldown off, 3 Infinite Victory Burst
extern "C" void train_opt_reset(int kind);                // 1 start positions, 2 close together, 3 the CPU to Player 1, 4 saved positions, 5 save now
extern "C" int train_opt_saved(void);                     // positions are saved in this fight
extern "C" int train_opt_on(void);                        // 0 with TRAIN_OPTS=0: the rows of that file are left out
extern "C" int jsp_tr_session(void);                      // jsp_game.cpp: an online session is on or its ticks are run again: this menu stays away
// The other users of the one overlay layer (weak: a build without them has none). While one holds the layer the windows are not drawn
// (they come back with the next pulse once it is free) and the layer is not closed from here.
extern "C" __attribute__((weak)) int jsp_port_menu_overlay_held(void);
extern "C" __attribute__((weak)) int jsp_net_overlay_held(void);

namespace {
int envi(const char* k, int d) { const char* e = std::getenv(k); return e && *e ? std::atoi(e) : d; }
bool enabled() { static const bool v = envi("TRAIN_MENU", 1) != 0; return v; }
bool logon() { static const bool v = envi("TRAIN_MENU_LOG", 0) == 1; return v; }
bool layer_free() { return !(jsp_port_menu_overlay_held && jsp_port_menu_overlay_held()) && !(jsp_net_overlay_held && jsp_net_overlay_held()); }

enum { B_SELECT = 0x0001, B_START = 0x0008, B_UP = 0x0010, B_RIGHT = 0x0020, B_DOWN = 0x0040, B_LEFT = 0x0080, B_CIRCLE = 0x2000, B_CROSS = 0x4000 };
enum { K_ONOFF, K_STARS, K_BEHAVIOR, K_ACTION };
enum { V_STARS, V_BEHAVIOR, V_STAMINA, V_COOLDOWN, V_BURST, V_FREECAM, V_GROUPCAM, V_HIDEHUD, V_RESET_START, V_RESET_CLOSE, V_CALL, V_SAVE, V_RESET_SAVED };
struct Opt { const char* label; int kind; int id; const char* help; };
struct Cat { const char* name; int game_row; const char* about; std::vector<Opt> opts; };   /* game_row 0: a category of ours */

const std::vector<Cat>& cats()
{
    static const std::vector<Cat> c = [] {
        std::vector<Cat> v;
        const bool opts = train_opt_on() != 0;   /* TRAIN_OPTS=0: the rows that feat_training_opts.cpp does are not shown (they would do nothing) */
        if (opts) v.push_back({ "CPU Settings", 0, "", {
            { "CPU Level", K_STARS, V_STARS, "0 stars: a training dummy. 1 to 5 stars: the CPU fights at that level." },
            { "CPU Behavior", K_BEHAVIOR, V_BEHAVIOR, "What the dummy does at 0 stars." } } });
        Cat sys{ "System Settings", 0, "", {
            { "Unlimited Stamina", K_ONOFF, V_STAMINA, "Stamina is not used up." } } };
        if (opts) { sys.opts.push_back({ "Support Cooldown", K_ONOFF, V_COOLDOWN, "Off: a Support can be called again as soon as it has left." });
                    sys.opts.push_back({ "Infinite Victory Burst", K_ONOFF, V_BURST, "The Victory gauge stays full and a burst does not run out." }); }
        v.push_back(sys);
        if (opts) v.push_back({ "Position Reset", 0, "", {
            { "Reset to Start Positions", K_ACTION, V_RESET_START, "Everyone back to where the fight began. In the fight: Select + Cross." },
            { "Reset Close Together", K_ACTION, V_RESET_CLOSE, "The opponent right in front of Player 1. In the fight: Select + Circle." },
            { "Move CPU to Player", K_ACTION, V_CALL, "Brings the opponent to Player 1. In the fight: Select + Triangle." },
            { "Save Positions", K_ACTION, V_SAVE, "Remembers where everyone stands now. In the fight: Select + R1." },
            { "Reset to Saved Positions", K_ACTION, V_RESET_SAVED, "Everyone back to the saved positions. In the fight: Select + Square." } } });
        Cat cam{ "Camera / HUD", 0, "", {
            { "Free Camera", K_ONOFF, V_FREECAM, "Fly the camera with the sticks; the fighters stand still." } } };
        if (jsp_tr_has_groupcam()) cam.opts.push_back({ "Group Camera", K_ONOFF, V_GROUPCAM, "One camera that keeps every fighter in view." });
        cam.opts.push_back({ "Hide HUD", K_ONOFF, V_HIDEHUD, "No bars, portraits or map (for pictures)." });
        v.push_back(cam);
        v.push_back({ "Skill Table", 2, "The fighter's moves and how to do them.", {} });
        v.push_back({ "Re-select Characters", 4, "Back to Character Select.", {} });
        v.push_back({ "Return to Main Menu", 5, "Leave Training Mode.", {} });
        return v; }();
    return c;
}

int value_of(int id)
{
    switch (id) {
    case V_STARS: return train_opt_value(0, -1);
    case V_BEHAVIOR: return train_opt_value(1, -1);
    case V_STAMINA: return jsp_tr_flag(0, -1);
    case V_COOLDOWN: return train_opt_value(2, -1) ? 0 : 1;   /* shown as the cooldown being On / Off */
    case V_BURST: return train_opt_value(3, -1);
    case V_FREECAM: return jsp_tr_flag(1, -1);
    case V_HIDEHUD: return jsp_tr_flag(2, -1);
    case V_GROUPCAM: return jsp_tr_flag(3, -1);
    default: return 0;
    }
}
void change(const Opt& o, int dir)
{
    const int v = value_of(o.id);
    switch (o.id) {
    case V_STARS: train_opt_value(0, (v + (dir < 0 ? 5 : 1)) % 6); break;
    case V_BEHAVIOR: if (train_opt_value(0, -1) == 0) train_opt_value(1, (v + (dir < 0 ? 3 : 1)) % 4); break;
    case V_STAMINA: jsp_tr_flag(0, !v); break;
    case V_COOLDOWN: train_opt_value(2, v ? 1 : 0); break;   /* On -> the "off" switch set */
    case V_BURST: train_opt_value(3, !v); break;
    case V_FREECAM: jsp_tr_flag(1, !v); break;
    case V_HIDEHUD: jsp_tr_flag(2, !v); break;
    case V_GROUPCAM: jsp_tr_flag(3, !v); break;
    default: break;
    }
}

/* ---- state ---- */
volatile bool g_shown = false;   /* our windows are up */
volatile bool g_cover = false;   /* the game's pause panel is not drawn (the render thread reads it in train_menu_hide): while our windows are
                                  * up and while the hidden selector is walked to a row */
int g_cat = 0, g_focus = 0, g_row = 0, g_scroll = 0;
uint16_t g_prev = 0xFFFF;
int g_hold_dir = 0, g_hold = 0;
/* a row of the game's own pause menu being done: walk to it, press Cross, then wait for the game to act */
int g_goal = 0, g_tries = 0; uint16_t g_btn = 0; unsigned long long g_until = 0, g_next = 0, g_sent = 0;
bool g_final = false;
int g_reset_after_close = 0;
/* 2 Players: the game's pause list answers only the pad that opened it. Whose Start came last is
 * noted; when the pause is Player 2's, the presses meant for the game go out on Player 2's pad too, and while one of
 * the game's own screens is up (its Yes / No box, the Skill Table) Player 1's presses are passed on to that pad. */
unsigned long long g_start_ms[2] = { 0, 0 };
uint16_t g_start_prev[2] = { 0, 0 };
int g_owner = 0;                     /* the pad that opened this pause (0 Player 1, 1 Player 2) */
bool g_owner_known = false;          /* taken when the pause came up, kept until it is over (the windows come and go meanwhile) */
bool g_skill_mirror = false;         /* the Skill Table was opened from a pause of Player 2's: until the list is back */
bool g_skill_away = false;           /* ... and the list has been away since (it stays up for some frames after the Cross) */
uint16_t g_p1_b = 0, g_p1_lx = 128, g_p1_ly = 128;   /* Player 1's pad as it was read this frame */
unsigned long long g_drawn_at = 0;   /* the last draw (the selector's pulse redraws about 14 times a second) */
unsigned long long g_left_at = 0;   /* when the game left its pause list (our windows and the cover go a moment later) */

/* ---- drawing ---- */
float g_s = 1.0f; int g_ox = 0, g_oy = 0;
int X(float v) { return g_ox + (int)std::lround(v * g_s); }
int Y(float v) { return g_oy + (int)std::lround(v * g_s); }
int S(float v) { const int r = (int)std::lround(v * g_s); return r < 1 ? 1 : r; }

struct Img { int w, h, kind; std::vector<uint8_t> px; };
std::vector<Img> g_imgs;
/* kind 0: the selected row's gold band (bright in the middle, fading to both ends, a light line above and below);
 * kind 1: the same, dim (the selector of the window that is not in use); kind 2 / 3: a lit / an empty star */
const Img& img(int w, int h, int kind)
{
    for (const Img& i : g_imgs) if (i.w == w && i.h == h && i.kind == kind) return i;
    Img im; im.w = w; im.h = h; im.kind = kind; im.px.assign((size_t)w * h * 4, 0);
    if (kind <= 1 || kind >= 10) {   /* 10 .. 15: the gold band at six strengths, for its pulse */
        const float k = kind >= 10 ? 0.80f + 0.04f * (float)(kind - 10) : 1.0f;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            const float u = std::fabs(2.0f * ((float)x + 0.5f) / (float)w - 1.0f);
            float a = 1.0f - std::pow(u, 2.4f); if (a < 0.0f) a = 0.0f;
            const bool line = y < 2 || y >= h - 2;
            uint8_t* p = &im.px[((size_t)y * w + x) * 4];
            if (kind != 1) { p[0] = line ? 255 : 248; p[1] = line ? 236 : 186; p[2] = line ? 160 : 36; p[3] = (uint8_t)((0.30f + 0.70f * a) * k * (line ? 255.0f : 235.0f)); }   /* solid enough to read on a bright stage */
            else { p[0] = 190; p[1] = 190; p[2] = 190; p[3] = (uint8_t)(a * (line ? 150.0f : 70.0f)); }
        }
    } else {
        const float cx = (float)w * 0.5f, cy = (float)h * 0.54f, R = (float)w * 0.5f, r = R * 0.42f;
        float vx[10], vy[10];
        for (int k = 0; k < 10; k++) { const float ang = -1.5707963f + (float)k * 0.6283185f, rad = (k & 1) ? r : R; vx[k] = cx + rad * std::cos(ang); vy[k] = cy + rad * std::sin(ang); }
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            int in = 0;   /* 2 x 2 samples, even-odd rule */
            for (int sy = 0; sy < 2; sy++) for (int sx = 0; sx < 2; sx++) {
                const float px = (float)x + 0.25f + 0.5f * (float)sx, py = (float)y + 0.25f + 0.5f * (float)sy; bool c = false;
                for (int i = 0, j = 9; i < 10; j = i++)
                    if (((vy[i] > py) != (vy[j] > py)) && (px < (vx[j] - vx[i]) * (py - vy[i]) / (vy[j] - vy[i]) + vx[i])) c = !c;
                in += c ? 1 : 0;
            }
            uint8_t* p = &im.px[((size_t)y * w + x) * 4];
            if (kind == 2) { p[0] = 255; p[1] = 206; p[2] = 48; } else { p[0] = 78; p[1] = 78; p[2] = 84; }
            p[3] = (uint8_t)(in * 255 / 4);
        }
    }
    g_imgs.push_back(std::move(im));
    return g_imgs.back();
}
void blit(int x, int y, int w, int h, int kind) { const Img& i = img(w, h, kind); jsp_overlay_blit_rgba(x, y, w, h, i.px.data(), w * 4); }

const uint32_t WHITE = JSP_RGBA(255, 255, 255, 255), GREY = JSP_RGBA(150, 150, 150, 255), DIMTXT = JSP_RGBA(120, 120, 120, 255);

/* A window in the look of the game's pause panel (see-through like the old pause menu, on the options only), made as one picture because the overlay only draws over what is there:
 * a solid black frame with rounded corners and a black title strip; every row is a see-through glass strip (the fight
 * shows through it, darkened) with the game's glossy line along its top. veil = the same shape in see-through black,
 * laid over the window that is not in use. */
struct Panel { int w, h, rh, inset; std::vector<int> rows; bool veil; std::vector<uint8_t> px; };
std::vector<Panel> g_panels;
const Panel& panel(int w, int h, const std::vector<int>& rows, int rh, int inset, bool veil)
{
    for (const Panel& q : g_panels) if (q.w == w && q.h == h && q.rh == rh && q.inset == inset && q.veil == veil && q.rows == rows) return q;
    Panel q; q.w = w; q.h = h; q.rh = rh; q.inset = inset; q.rows = rows; q.veil = veil; q.px.assign((size_t)w * h * 4, 0);
    const float rad = 11.0f * g_s;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        /* the rounded outline: how much of the pixel is inside */
        float cov = 1.0f;
        const float cx = (float)x + 0.5f < rad ? rad : (float)x + 0.5f > (float)w - rad ? (float)w - rad : -1.0f;
        const float cy = (float)y + 0.5f < rad ? rad : (float)y + 0.5f > (float)h - rad ? (float)h - rad : -1.0f;
        if (cx >= 0.0f && cy >= 0.0f) { const float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy; cov = rad + 0.5f - std::sqrt(dx * dx + dy * dy); cov = cov < 0.0f ? 0.0f : cov > 1.0f ? 1.0f : cov; }
        uint8_t* px = &q.px[((size_t)y * w + x) * 4];
        if (veil) { px[0] = px[1] = px[2] = 0; px[3] = (uint8_t)(cov * 125.0f); continue; }
        float r = 0.0f, g = 0.0f, b = 0.0f, a = 255.0f;   /* the frame */
        if (x >= inset && x < w - inset) for (int ry : rows) {
            if (y < ry || y >= ry + rh) continue;
            const float u = std::fabs(2.0f * ((float)(x - inset) + 0.5f) / (float)(w - 2 * inset) - 1.0f);
            float shine = 1.0f - u * u * u; if (shine < 0.0f) shine = 0.0f;
            const int ly = y - ry;
            /* the glass, measured on the game's own pause rows (the pause rows against the same
             * place with the menu closed: 225 -> 81, 140 -> 62, 163 -> 68, 147 -> 64): 22 % of the picture shows through a grey of 40 */
            r = 40.0f; g = 40.0f; b = 41.0f; a = 199.0f;
            if (ly == 0) { r = g = b = 235.0f; a = 199.0f + 50.0f * shine; }       /* its glossy top line */
            else if (ly == 1) { r = g = b = 120.0f; a = 199.0f + 20.0f * shine; }
            else if (ly >= rh - 1) { r = g = b = 0.0f; a = 200.0f; }
            break;
        }
        px[0] = (uint8_t)r; px[1] = (uint8_t)g; px[2] = (uint8_t)b; px[3] = (uint8_t)(a * cov);
    }
    g_panels.push_back(std::move(q));
    return g_panels.back();
}
void window(float x, float y, float w, float h, const char* title, const std::vector<float>& row_y, bool veil)
{
    std::vector<int> rows; for (float ry : row_y) rows.push_back(Y(ry) - Y(y));
    const Panel& q = panel(S(w), S(h), rows, S(36), S(12), veil);
    jsp_overlay_blit_rgba(X(x), Y(y), q.w, q.h, q.px.data(), q.w * 4);
    if (!veil) jsp_overlay_text_style((float)X(x + 20), (float)Y(y + 20), title, 1.3f * g_s, WHITE, 0);
}
void centred(float cx, float y, const char* t, float scale, uint32_t col)
{
    const float tw = jsp_overlay_text_width(t, scale * g_s);
    jsp_overlay_text_style((float)X(cx) - tw * 0.5f, (float)Y(y), t, scale * g_s, col, 0);
}

const float kLX = 150, kLW = 340, kRX = 502, kRW = 628, kWY = 104, kWH = 514, kRowH = 44, kRow0 = 80;
const int kRightRows = 7;

void draw()
{
    int ow = 0, oh = 0; jsp_overlay_output_size(&ow, &oh);
    if (ow <= 0 || oh <= 0) { ow = 1280; oh = 720; }
    if (!layer_free()) { g_drawn_at = GetTickCount64(); return; }   /* another user has the layer: not drawn now, tried again with the next pulse */
    if (!jsp_overlay_begin(0, 0)) return;
    g_s = (float)oh / 720.0f; g_ox = (int)(((float)ow - 1280.0f * g_s) * 0.5f); g_oy = 0;
    const std::vector<Cat>& cs = cats();
    const Cat& cat = cs[(size_t)g_cat];

    /* the selector pulses as the game's does: six strengths, up and down over 1.2 s */
    const float ph = (float)(GetTickCount64() % 1200u) / 1200.0f;
    const int pulse = 10 + (int)std::lround((0.5f + 0.5f * std::sin(ph * 6.2831853f)) * 5.0f);
    /* left: the categories */
    std::vector<float> lrows, rrows;
    for (int i = 0; i < (int)cs.size(); i++) lrows.push_back(kWY + kRow0 + (float)i * kRowH + (cs[(size_t)i].game_row ? 22.0f : 0.0f));   /* the game's own rows sit a little apart, below */
    for (int k = 0; k < kRightRows; k++) rrows.push_back(kWY + kRow0 + (float)k * kRowH);
    window(kLX, kWY, kLW, kWH, "Training Menu", lrows, false);
    for (int i = 0; i < (int)cs.size(); i++) {
        const float y = lrows[(size_t)i];
        if (i == g_cat) blit(X(kLX + 12), Y(y), S(kLW - 24), S(36), g_focus == 0 ? pulse : 1);
        centred(kLX + kLW * 0.5f, y + 6, cs[(size_t)i].name, 0.92f, WHITE);
    }
    centred(kLX + kLW * 0.5f, kWY + kWH - 30, g_focus == 0 ? "Cross: select     Circle: close" : "", 0.56f, GREY);

    /* right: the options of that category */
    window(kRX, kWY, kRW, kWH, cat.name, rrows, false);
    if (cat.opts.empty()) {
        centred(kRX + kRW * 0.5f, kWY + 170, cat.about, 0.78f, JSP_RGBA(225, 225, 225, 255));
        centred(kRX + kRW * 0.5f, kWY + 214, "Press Cross.", 0.66f, GREY);
    } else {
        const int n = (int)cat.opts.size();
        if (g_row >= n) g_row = n - 1;
        if (g_row < g_scroll) g_scroll = g_row;
        if (g_row >= g_scroll + kRightRows) g_scroll = g_row - kRightRows + 1;
        const int stars = train_opt_value(0, -1);
        for (int k = 0; k < kRightRows && g_scroll + k < n; k++) {
            const int i = g_scroll + k; const Opt& o = cat.opts[(size_t)i];
            const float y = kWY + kRow0 + (float)k * kRowH; const bool sel = g_focus == 1 && i == g_row;
            if (sel) blit(X(kRX + 12), Y(y), S(kRW - 24), S(36), pulse);
            jsp_overlay_text_style((float)X(kRX + 44), (float)Y(y + 6), o.label, 0.9f * g_s, WHITE, 0);
            const float cx = kRX + kRW - 150;   /* the middle of the value column */
            if (o.kind == K_ACTION) {
                const bool none = o.id == V_RESET_SAVED && !train_opt_saved();
                if (sel || none) centred(cx, y + 8, none ? "Nothing saved" : "Press Cross", 0.68f, none ? DIMTXT : JSP_RGBA(235, 235, 235, 255));
                continue; }
            const bool locked = o.kind == K_BEHAVIOR && stars > 0;   /* a fighting CPU has no dummy behaviour */
            if (o.kind == K_STARS) {
                const int v = value_of(o.id), sz = S(22);
                for (int q = 0; q < 5; q++) blit(X(cx - 62.0f + (float)q * 25.0f), Y(y + 6), sz, sz, q < v ? 2 : 3);
            } else {
                static const char* const beh[4] = { "Stand", "Guard", "Jump", "Attack" };
                const int v = value_of(o.id);
                const char* t = o.kind == K_BEHAVIOR ? (locked ? "Fights" : beh[v & 3]) : (v ? "On" : "Off");
                centred(cx, y + 6, t, 0.9f, locked ? DIMTXT : WHITE);
            }
            if (sel && !locked) {
                jsp_overlay_text_style((float)X(cx - 104), (float)Y(y + 6), "<", 0.9f * g_s, WHITE, 0);
                jsp_overlay_text_style((float)X(cx + 92), (float)Y(y + 6), ">", 0.9f * g_s, WHITE, 0);
            }
        }
        if (g_scroll > 0) centred(kRX + kRW - 26, kWY + kRow0 - 4, "^", 0.7f, GREY);
        if (g_scroll + kRightRows < n) centred(kRX + kRW - 26, kWY + kRow0 + (float)kRightRows * kRowH - 12, "v", 0.7f, GREY);
        /* the help line of the row the selector is on, and the buttons */
        if (g_focus == 1) {
            centred(kRX + kRW * 0.5f, kWY + kWH - 76, cat.opts[(size_t)g_row].help, 0.6f, JSP_RGBA(225, 225, 225, 255));
            centred(kRX + kRW * 0.5f, kWY + kWH - 30, cat.opts[(size_t)g_row].kind == K_ACTION ? "Cross: do it     Circle: back" : "Left / Right: change     Circle: back", 0.56f, GREY);
        }
    }
    /* the window that is not in use is darkened */
    if (g_focus == 0) window(kRX, kWY, kRW, kWH, "", rrows, true);
    else window(kLX, kWY, kLW, kWH, "", lrows, true);
    jsp_overlay_end();
    g_drawn_at = GetTickCount64();
}

void hide_windows(const char* why)
{
    if (!g_shown) return;
    g_shown = false; if (layer_free()) jsp_overlay_close();   /* another user's drawing is not ours to close (its begin() has replaced ours already) */
    if (logon()) { std::printf("[trainmenu] windows off (%s) at %llu\n", why, GetTickCount64()); std::fflush(stdout); }
}

/* put the game's hidden pause list on a row and press Cross there. The selector is written straight to the row
 * (walking there with Down presses took about a second); only if the game does not keep that, it is
 * walked with Down presses as before. */
void go(int game_row, const char* what)
{
    g_goal = game_row; g_final = false; g_until = 0; g_next = GetTickCount64() + 40; g_sent = 0; g_tries = 0;
    if (game_row == 2 && g_owner == 1) { g_skill_mirror = true; g_skill_away = false; }   /* the Skill Table of a pause Player 2 opened */
    jsp_tr_pause_set_row(game_row);
    std::printf("[trainmenu] %s (the game's pause row %d) at %llu\n", what, game_row, GetTickCount64()); std::fflush(stdout);
}
}

int train_menu_on(void) { return enabled() && !jsp_tr_session() ? 1 : 0; }   /* a rollback run: the game's pause menu keeps its own Training settings row */

/* render thread: is this HUD draw the game's pause panel? (720p: x 432..848, y 192..528; the box is in -1..1, y up)
 * ASSUMPTION: the panel is told from other HUD draws only by that box, measured on screenshots at 1280 x 720. */
int train_menu_hide(const float* box)
{
    if (!g_cover) return 0;
    return box[0] >= -0.36f && box[2] <= 0.36f && box[1] >= -0.52f && box[3] <= 0.52f ? 1 : 0;
}

/* jsp_game.cpp game_pad_filter, before everything else of Training: 1 = the pad read is ours */
int train_menu_pad(uint32_t port, uint16_t* b, uint16_t* lx, uint16_t* ly)
{
    if (!enabled()) return 0;
    /* an online session or a re-run tick: the menu does nothing (it presses buttons for the game from the host's clock);
     * windows that are up go, the pad is the game's */
    if (jsp_tr_session()) { if (port == 0 && (g_shown || g_cover)) { hide_windows("an online session"); g_cover = false; g_goal = 0; g_sent = 0; g_owner = 0; g_owner_known = false; g_skill_mirror = false; } return 0; }
    int depth = 0, row = 0;
    const bool list_up = jsp_tr_active() && jsp_tr_pause_info(&depth, &row) && depth == 0;
    const unsigned long long now = GetTickCount64();
    if (port < 2) {   /* whose Start came last, and Player 1's pad for the passing on */
        if ((*b & B_START) && !(g_start_prev[port] & B_START)) g_start_ms[port] = now;
        g_start_prev[port] = *b;
        if (port == 0) { g_p1_b = *b; g_p1_lx = *lx; g_p1_ly = *ly; }
    }
    if (g_skill_mirror && port == 0) {   /* over when the list is back after having been away, or after two minutes */
        static unsigned long long since = 0;
        if (!g_skill_away) since = now;
        if (!list_up) g_skill_away = true;
        else if (g_skill_away) { g_skill_mirror = false; g_skill_away = false; }
        if (g_skill_mirror && now - since > 120000) { g_skill_mirror = false; g_skill_away = false; }
    }
    if (!list_up) {
        if (port == 1 && g_owner == 1 && jsp_tr_active()) {
            /* one of the game's own screens of a pause that Player 2 opened: Player 1 answers it too */
            int d4 = 0, r4 = 0;
            if (g_skill_mirror || (jsp_tr_pause_info(&d4, &r4) && d4 >= 1)) {
                *b |= g_p1_b;
                if (g_p1_lx < 64 || g_p1_lx > 192) *lx = g_p1_lx;
                if (g_p1_ly < 64 || g_p1_ly > 192) *ly = g_p1_ly;
            }
        }
        if (port == 0) {
            /* The game plays its list out before its Yes / No box or the Skill table is up: for a few frames the old pause
             * list was drawn again. Our windows
             * and the cover stay for TRAIN_MENU_LEAVE_MS (default 280) after the list is left. */
            static const unsigned long long leave_ms = (unsigned long long)envi("TRAIN_MENU_LEAVE_MS", 280);
            if (g_shown || g_cover) {
                if (!g_left_at) { g_left_at = now;
                    if (logon()) { int d2 = -1, r2 = -1; const int up2 = jsp_tr_pause_info(&d2, &r2); std::printf("[trainmenu] the list is left (pause %d depth %d) at %llu\n", up2, d2, now); std::fflush(stdout); } }
                int d3 = 0, r3 = 0;
                const bool pause_gone = !jsp_tr_pause_info(&d3, &r3);   /* the Skill table (or the fight again): nothing of the list is drawn any more */
                if (pause_gone || now - g_left_at >= leave_ms) { hide_windows("the pause list is not up"); g_cover = false; g_left_at = 0; }
            }
            g_goal = 0; g_sent = 0; g_prev = 0xFFFF;
            { int d5 = 0, r5 = 0; if (!g_skill_mirror && !jsp_tr_pause_info(&d5, &r5)) { g_owner = 0; g_owner_known = false; } }   /* the fight goes on: no pause, nobody's */
            if (g_reset_after_close && !jsp_tr_pause_info(&depth, &row)) { train_opt_reset(g_reset_after_close); g_reset_after_close = 0; }
        }
        return 0;
    }
    g_left_at = 0;
    if (port != 0) {   /* the main pad drives the menu; what it sends to the game also goes out on Player 2's pad when the pause is Player 2's */
        *b = (port == 1 && g_owner == 1 && now < g_until) ? g_btn : (uint16_t)0; *lx = 128; *ly = 128; return 1; }
    uint16_t cur = *b;
    if (*ly < 64) cur |= B_UP; else if (*ly > 192) cur |= B_DOWN;
    if (*lx < 64) cur |= B_LEFT; else if (*lx > 192) cur |= B_RIGHT;
    *b = 0; *lx = 128; *ly = 128;

    if (g_sent) {   /* the Cross went to the game: its screen comes (or the menu closes); after 1.5 s without either, ours again */
        if (now - g_sent < 1500) return 1;
        g_sent = 0;
    }
    if (g_goal) {   /* walking the hidden selector: 70 ms presses, 190 ms apart */
        if (now < g_until) { *b = g_btn; return 1; }
        /* after the Cross our windows and the cover stay as they are until the game has left its list (its Yes / No box
         * or the Skill table is up, or the menu has closed): the old pause menu showed for a moment in between */
        if (g_final) { g_goal = 0; g_final = false; g_sent = now; return 1; }
        if (now >= g_next) {
            if (row != g_goal && g_tries < 3) { g_tries++; jsp_tr_pause_set_row(g_goal); g_next = now + 40; return 1; }
            g_btn = row == g_goal ? (uint16_t)B_CROSS : (uint16_t)B_DOWN;
            g_final = row == g_goal;
            g_until = now + 70; g_next = now + 190; *b = g_btn;
        }
        return 1;
    }
    if (!g_shown) { g_shown = true; g_cover = true; g_focus = 0; g_prev = 0xFFFF; draw();
        if (!g_owner_known) { g_owner_known = true; g_owner = (g_start_ms[1] > g_start_ms[0] && now - g_start_ms[1] < 3000) ? 1 : 0; }
        if (g_owner == 1) { std::printf("[trainmenu] this pause was opened by Player 2's pad: the game's rows are answered on it\n"); std::fflush(stdout); }
        if (logon()) { std::printf("[trainmenu] windows on (pause row %d) at %llu\n", row, now); std::fflush(stdout); } }
    if (g_prev == 0xFFFF) { if (cur & (B_START | B_CROSS | B_CIRCLE)) return 1; g_prev = 0; }   /* the press that opened the pause menu is let go first */
    uint16_t pressed = (uint16_t)(cur & ~g_prev); g_prev = cur;
    const int dir = (cur & B_UP) ? 1 : (cur & B_DOWN) ? 2 : (cur & B_LEFT) ? 3 : (cur & B_RIGHT) ? 4 : 0;   /* a held direction repeats */
    if (dir && dir == g_hold_dir) { if (++g_hold > 24 && (g_hold % 8) == 0) pressed |= dir == 1 ? B_UP : dir == 2 ? B_DOWN : dir == 3 ? B_LEFT : B_RIGHT; }
    else { g_hold_dir = dir; g_hold = 0; }
    if (!pressed) { if (now - g_drawn_at >= 70) draw(); return 1; }   /* the pulse */
    const std::vector<Cat>& cs = cats();
    const Cat& cat = cs[(size_t)g_cat];
    bool redraw = false;
    if (g_focus == 0) {
        if (pressed & (B_CIRCLE | B_START)) { go(1, "close"); hide_windows("closing"); return 1; }
        if (pressed & B_UP) { g_cat = (g_cat + (int)cs.size() - 1) % (int)cs.size(); g_row = 0; g_scroll = 0; redraw = true; }
        else if (pressed & B_DOWN) { g_cat = (g_cat + 1) % (int)cs.size(); g_row = 0; g_scroll = 0; redraw = true; }
        else if (pressed & (B_CROSS | B_RIGHT)) {
            if (cat.game_row) { if (pressed & B_CROSS) { go(cat.game_row, cat.name); return 1; } }
            else { g_focus = 1; g_row = 0; g_scroll = 0; redraw = true; }
        }
    } else {
        const int n = (int)cat.opts.size();
        const Opt& o = cat.opts[(size_t)g_row];
        if (pressed & B_START) { go(1, "close"); hide_windows("closing"); return 1; }
        if (pressed & B_CIRCLE) { g_focus = 0; redraw = true; }
        else if (pressed & B_UP) { g_row = (g_row + n - 1) % n; redraw = true; }
        else if (pressed & B_DOWN) { g_row = (g_row + 1) % n; redraw = true; }
        else if (o.kind == K_ACTION) {
            if (pressed & B_CROSS) {   /* a position reset: the menu closes and the fighters are placed as the fight goes on */
                if (o.id == V_RESET_SAVED && !train_opt_saved()) return 1;   /* nothing saved yet: the row says so */
                g_reset_after_close = o.id == V_RESET_START ? 1 : o.id == V_RESET_CLOSE ? 2 : o.id == V_CALL ? 3 : o.id == V_RESET_SAVED ? 4 : 5;
                go(1, o.label); hide_windows("closing"); return 1;
            }
        }
        else if (pressed & (B_LEFT | B_RIGHT | B_CROSS)) { change(o, (pressed & B_LEFT) ? -1 : 1); redraw = true; }
    }
    if (redraw) draw();
    return 1;
}
#endif  // JSP_US_CODE
