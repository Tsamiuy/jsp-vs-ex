// feat_3v3.cpp -- 3v3 Free Battle (the maintainer 2026-11-16..20). It is on by default together with the two-page menu
// (MENU_PAGES=0 or SIX=0 turns it off). What the game file must call, and the hooks it needs, are in jsp_game.cpp (the six_*
// weak functions and the jsp_* C API) and in lift_game_hooks.json.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#endif
#include "ppu_recomp.h"
// from the rest of the port (to become the game file's small C API)
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
extern "C" void ps3_indirect_call_to(ppu_context* ctx, uint32_t target);
static const bool kExp = false;   // probes that need an experiments build stay off
static inline void jsp_map_leave(ppu_context*) {}
extern "C" int64_t sys_memory_allocate(ppu_context* ctx);
extern "C" int  cellOskDialog_overlay_active(void);
extern "C" int  cellOskDialog_overlay_key(unsigned vk);
extern "C" int  cellOskDialog_confirm_poll(void);
extern "C" int  cellOskDialog_choice_open(const wchar_t* prompt, const wchar_t* left, const wchar_t* right);
// the game file's screen tracking and state (jsp_game.cpp, C API)
struct SeenScreen { volatile uint32_t obj; volatile unsigned long long ms; };
extern "C" SeenScreen* jsp_seen_charsel(void);
extern "C" SeenScreen* jsp_seen_main_screen(void);
extern "C" int jsp_on_charsel(void);
extern "C" int jsp_on_stagesel(void);
extern "C" int jsp_training_active(void);
extern "C" int jsp_main_list_open(void);
extern "C" int jsp_in_menu_scene(void);
extern "C" int jsp_training_fight_scene(void);
static inline bool training_fight_scene() { return jsp_training_fight_scene() != 0; }
#define g_seen_charsel (*jsp_seen_charsel())
#define g_seen_main (*jsp_seen_main_screen())
static inline bool on_charsel() { return jsp_on_charsel() != 0; }
static inline bool on_stagesel() { return jsp_on_stagesel() != 0; }
static inline bool training_active() { return jsp_training_active() != 0; }
static inline bool main_list_open() { return jsp_main_list_open() != 0; }
static inline bool in_menu_scene() { return jsp_in_menu_scene() != 0; }
static inline bool screen_up(const SeenScreen& sc, uint32_t vtable) { const uint32_t o = sc.obj; return o && GetTickCount64() - sc.ms < 300 && vm_read32(o) == vtable; }
#undef scr2   // windows dlgs.h
static inline uint32_t setup_field(int slot, uint32_t off) { return 0x009540E4u + 0x2Cu * (uint32_t)slot + off; }
extern volatile int g_mp_entry;   // feat_menu_pages.cpp

/* One setting (2026-11-21): 3v3 was first turned on with a list of SIX_* switches. Now the two-page menu (on by
 * default, MENU_PAGES=0 off) brings it with it: the team size window on the Free Battle list and the switches 3v3
 * needs. SIX=0 keeps 3v3 off; a switch already set wins. Defined first in the file, so it runs before the file's own switch readers (some are read while the program starts). */
static const bool g_six_defaults = [] {
    const char* mp = std::getenv("MENU_PAGES"); const char* off = std::getenv("SIX");
    if ((mp && mp[0] == '0') || (off && off[0] == '0')) return false;   /* on by default since 2026-11-21 */
    static const char* const kv[][2] = { { "SIX_ASK", "1" }, { "SIX_CSEL", "1" }, { "SIX_CSCLONE", "1" }, { "SIX_CSLAY", "2" },
        { "SIX_3V3", "1" }, { "SIX_LOADER", "1" }, { "SIX_HOLD_ROLES", "2" }, { "SIX_FIX_RULES", "1" }, { "SIX_UNIT_TABLE", "1" },
        { "SIX_SLOT45", "1" }, { "SIX_FULL45", "all" }, { "SIX_SEQPLACE", "1" }, { "SIX_HEAPMOVE", "1" }, { "SIX_MGR6", "1" },
        { "SIX_LISTFIX", "1" } };
    for (const auto& p : kv) if (!std::getenv(p[0])) _putenv_s(p[0], p[1]);
    return true;
}();

void vsn_arena_fix();   /* the VS loader's two arenas made larger (defined with the jsp_vsn_* code) */

/* 3v3 at run time (the maintainer 2026-11-19: "a 2v2 and 3v3 choice at the 1 Player and 2 Players menu on Free Battle ... all the
 * other battle modes 2v2 only"). The SIX_* switches say what the build CAN do; six_live() says whether the fight being
 * set up is a 3v3. Without SIX_ASK it is always on (the older launchers); with SIX_ASK=1 it is off until the team size
 * window on Free Battle's 1 Player / 2 Players list says 3 vs 3, and off again on the main menu. */
static std::atomic<int> g_six_live{-1};
/* FIX (online is 2 vs 2): never 3v3 while an online rollback match runs (g_ko_track, set by jsp_net_game.cpp from the session's start to its
 * end). Online Battle is reached from the main menu, where the answer is already forgotten (six_ask_pad), and the rollback arms only in the
 * game's own online mode; this keeps every 3v3 hook below (each gated on six_live) from writing guest memory during that fight whatever
 * happened before, since the other PC would not write it (the fight hash would catch the difference). */
extern "C" volatile int g_ko_track;   /* jsp_game.cpp */
static inline bool six_live() { if (g_ko_track) return false; int v = g_six_live.load(); if (v < 0) { v = (kExp && std::getenv("SIX_ASK")) ? 0 : 1; g_six_live.store(v); } return v != 0; }

/* parked name boxes (see jsp_six_boxpos_enter): park centre in 1280x720 px, and per box the shift kx, ky (px) and a
 * "valid" flag, written by the game thread, read by the draw hook */
#define g_six_park_on (g_six_park_env && six_live())
static const bool g_six_park_env = [] { const char* c = std::getenv("SIX_CSCLONE"); const char* l = std::getenv("SIX_CSLAY"); const char* n = std::getenv("SIX_CSNOPARK");
    return c && l && std::atoi(l) == 2 && !n; }();
/* 2 Players Character Select (2026-11-18 late, test runs): the team panel sits in the MIDDLE (x 413..872 of
 * 1280), Player 1's grid left (128..386, page row above it), Player 2's grid right (893..1150), the title centred on
 * top. The copies at their 1 Player offset land on Player 1's grid, and the draw hook cannot tell them from it by
 * place. So on that screen the copies are kept 800 px further DOWN (below the screen: draws off screen still reach
 * the hook), and the hook puts each group in its place: grid 1 far left, the three boxes
 * per team in the middle, grid 2 far right, all shrunk to 0.93. g_six_2p is set by the per-frame hook (a slot whose
 * controller is Player 2). SIX_CS2P=0: the screen is treated like 1 Player. */
static std::atomic<int> g_six_2p{0};
static std::atomic<uint32_t> g_six_draw_dump{0};   /* probe SIX_CSDRAWLOG: R3 asks for the next draws of the screen to be listed */
static const float g_six_2p_down = [] { const char* e = std::getenv("SIX_2P_DOWN"); return e ? (float)std::atof(e) : 800.0f; }();
static const float g_six_park_x1 = [] { const char* e = std::getenv("SIX_PARK_X"); return e ? (float)std::atof(e) : 745.0f; }();
static const float g_six_park_x2 = [] { const char* e = std::getenv("SIX_PARK_X2"); return e ? (float)std::atof(e) : 250.0f; }();   /* 2 Players: left of the title, above grid 1's page row */
#define g_six_park_x (g_six_2p.load() ? g_six_park_x2 : g_six_park_x1)   /* 900 caught the settings window's top edge (the maintainer 11-18): the band is now left of the window (x 870+) */
static const float g_six_park_y[2] = { [] { const char* e = std::getenv("SIX_PARK_Y4"); return e ? (float)std::atof(e) : 40.0f; }(),
                                       [] { const char* e = std::getenv("SIX_PARK_Y5"); return e ? (float)std::atof(e) : 72.0f; }() };
static std::atomic<float> g_six_park_k[4][3];   /* 0, 1: name boxes 4 / 5; 2, 3: the "CPU" / "Player" labels of the new boxes */
static const float g_six_park_lx1 = [] { const char* e = std::getenv("SIX_PARK_LX"); return e ? (float)std::atof(e) : 560.0f; }();   /* label band centre x */
static const float g_six_park_lx2 = [] { const char* e = std::getenv("SIX_PARK_LX2"); return e ? (float)std::atof(e) : 1000.0f; }();   /* 2 Players: right of the title */
#define g_six_park_lx (g_six_2p.load() ? g_six_park_lx2 : g_six_park_lx1)
/* SIX_CSLAY=1 (probe, 3v3 Character Select layout): log the draws the backend offers this hook while Character Select
 * is up (render target, screen box in NDC), to see which target the screen uses and how its pieces sit. */
static float six_cslay(uint32_t rt_off, const float* box, float* ax, float* ay)
{
    (void)ax; (void)ay;
    static const int mode = [] { const char* e = std::getenv("SIX_CSLAY"); return e ? std::atoi(e) : 0; }();
    if (!mode || !on_charsel() || (mode >= 2 && !six_live())) return 1.0f;
    static int n = 0;
    if (mode == 1 && n < 1500) { n++; std::printf("[cslay] rt %08X box %.3f %.3f .. %.3f %.3fQQn", rt_off, box[0], box[1], box[2], box[3]); if ((n & 63) == 0) std::fflush(stdout); }
    if (mode < 2) return 1.0f;
    /* SIX_CSLAY=2: the maintainer's layout (11-17 mock-up): the team panels shrink and move right, the grid moves right; the
     * title (top left) and the button bar (bottom) keep their place. Pieces go by screen box (NDC, y up), from the
     * a test run: grid and page row start at x >= -0.07, team pieces left of that; red team
     * above y -0.02, blue below. The backend shrinks each draw toward the anchor (scale < 1 only). Tunables:
     * SIX_CS_TS (team scale), SIX_CS_TR (team right edge after), SIX_CS_GS (grid scale), SIX_CS_GL (grid left edge). */
    const float w = box[2] - box[0], h = box[3] - box[1];
    { static const bool dl = std::getenv("SIX_CSDRAWLOG") != nullptr; static uint32_t seen = 0; static int left = 0, no = 0;   /* probe: the draw order of the screen, in px */
      if (dl) { const uint32_t r = g_six_draw_dump.load(); if (r != seen) { seen = r; left = 900; no = 0; std::printf("[csdraw] list %u\n", r); }
          if (left > 0) { left--; std::printf("[csdraw] %3d rt %08X  x %6.0f .. %6.0f  y %6.0f .. %6.0f  (%4.0f x %4.0f)\n", no++, rt_off, (box[0] + 1.0f) * 640.0f, (box[2] + 1.0f) * 640.0f, (1.0f - box[3]) * 360.0f, (1.0f - box[1]) * 360.0f, w * 640.0f, h * 360.0f);
              if (!left) std::fflush(stdout); } } }
    /* A message window over the screen ("Return to the previous screen?", 2 Players) is drawn after everything else:
     * first a quad of exactly the screen (the dimmer), then its pieces. Those pieces must stay where they are (the maintainer
     * 2026-11-18: the window was torn apart). Draw order of a frame (probe SIX_CSDRAWLOG, a test run): about
     * 19 big draws in a row (other targets, backgrounds, comic tiles, stripes), the layouts' small pieces, one
     * 1420 x 799 quad, the button bar, and with a window open: the 1280 x 720 dimmer, the window. So: two big draws in
     * a row = a new frame; an exact full-screen quad after small pieces = a window starts, and the rest of the frame is
     * left alone. SIX_CSNOOVERLAY=1 turns it off. */
    { static bool prev_big = false, small_seen = false, overlay = false; static const bool noov = std::getenv("SIX_CSNOOVERLAY") != nullptr;
      const bool big = w >= 0.95f || h >= 1.5f, full = std::fabs(w - 2.0f) < 0.02f && std::fabs(h - 2.0f) < 0.02f;
      if (big) { if (prev_big) { overlay = false; small_seen = false; } else if (full && small_seen && !noov) overlay = true;
          prev_big = true; return 1.0f; }   /* backgrounds (full screen, halves, scrolling comic tiles) */
      prev_big = false; small_seen = true;
      if (overlay) return 1.0f; }
    auto envf = [](const char* k, float d) { const char* e = std::getenv(k); return e ? (float)std::atof(e) : d; };
    if (g_six_2p.load()) {
        /* px maps x' = s*x + c (y the same): anchor A = c / (1 - s). Team: 140..872 (with the copies at their 1 Player
         * offset) -> 300..981; grid 1: 128..386 -> 34..274; grid 2: 893..1150 -> 1007..1246; vertical centres kept. */
        static const float s2 = envf("SIX_2P_S", 0.93f), ct = envf("SIX_2P_CT", 169.8f), c1 = envf("SIX_2P_C1", -85.0f), c2 = envf("SIX_2P_C2", 176.5f);
        static const float yt = envf("SIX_2P_YT", 359.0f), yg = envf("SIX_2P_YG", 347.0f);   /* vertical centres (px) of the team panel and of a grid with its page row */
        auto nx = [](float px) { return px / 640.0f - 1.0f; }; auto ny = [](float px) { return 1.0f - px / 360.0f; };
        const float cxp = (0.5f * (box[0] + box[2]) + 1.0f) * 640.0f, cyp = (1.0f - 0.5f * (box[1] + box[3])) * 360.0f;
        const float D = g_six_2p_down;
        if (cyp > 740.0f) {   /* the copies, kept D px below their place */
            *ax = nx(ct / (1.0f - s2)); *ay = ny(yt - s2 * D / (1.0f - s2)); return s2; }
        if (box[3] < -0.70f) return 1.0f;             /* button bar */
        if (g_six_park_on) {   /* parked name / label boxes of the new boxes: the park shift and the D px are undone inside the team map */
            for (int q = 0; q < 4; q++) { const int t = q & 1; if (g_six_park_k[q][2].load() == 0.0f) continue;
                if (q < 2 ? (cxp - g_six_park_x2 < -60.0f || cxp - g_six_park_x2 > 110.0f) : std::fabs(cxp - g_six_park_lx2) > 85.0f) continue;
                if (std::fabs(cyp - g_six_park_y[t]) > 14.0f) continue;
                if (w * 640.0f > 130.0f || h * 360.0f > 45.0f) continue;
                const float kx = g_six_park_k[q][0].load(), ky = g_six_park_k[q][1].load() + D;   /* px; drawn = place + k */
                *ax = nx(ct / (1.0f - s2) - s2 * kx / (1.0f - s2)); *ay = ny(yt - s2 * ky / (1.0f - s2));
                static int said = 0; if (said++ < 4) { std::printf("[cslay] 2 Players: parked text %d draw at %.0f %.0f\n", q, cxp, cyp); std::fflush(stdout); }
                return s2; } }
        if (cyp < 96.0f && cxp > 440.0f && cxp < 900.0f) return 1.0f;   /* title */
        if (cxp < 400.0f) { *ax = nx(c1 / (1.0f - s2)); *ay = ny(yg); return s2; }   /* Player 1's grid and page row */
        if (cxp > 882.0f) { *ax = nx(c2 / (1.0f - s2)); *ay = ny(yg); return s2; }   /* Player 2's grid and page row */
        *ax = nx(ct / (1.0f - s2)); *ay = ny(yt); return s2; }
    if (box[3] < -0.70f) return 1.0f;                 /* button bar (its pieces end above y -0.74) */
    /* defaults measured from the maintainer's second mock-up (2026-11-18: bigger boxes, teams closer, a test run);
     * the first layout was TS 0.75, TR 0.14, GS 0.90, GL 0.17, RT 0.70, BB -0.70 */
    static const float ts = envf("SIX_CS_TS", 0.928f), tr = envf("SIX_CS_TR", 0.2736f), gs = envf("SIX_CS_GS", 0.99f), gl = envf("SIX_CS_GL", 0.127f);
    static const float rt = envf("SIX_CS_RT", 0.639f), bb = envf("SIX_CS_BB", -0.899f);   /* red top, blue bottom after */
    const float xr = 0.084f, at = (tr - ts * xr) / (1.0f - ts);
    const float ay_red = (rt - ts * 0.73f) / (1.0f - ts), ay_blue = (bb - ts * (-0.92f)) / (1.0f - ts);
    if (g_six_park_on) {   /* parked name boxes 4/5: undo the park shift inside the team shrink */
        const float cxp = (0.5f * (box[0] + box[2]) + 1.0f) * 640.0f, cyp = (1.0f - 0.5f * (box[1] + box[3])) * 360.0f;
        { static int n = 0; static const bool lg = std::getenv("SIX_CSLOG") != nullptr; if (lg && n < 60 && cxp > 440.0f && cxp < 740.0f && cyp > 15.0f && cyp < 140.0f && w < 0.3f) { n++; std::printf("[cslay] draw in the label area: centre %.0f %.0f size %.0f x %.0f\n", cxp, cyp, w * 640.0f, h * 360.0f); } }
        for (int q = 0; q < 4; q++) { const int t = q & 1; if (g_six_park_k[q][2].load() == 0.0f) continue;
            if (q < 2 ? (cxp - g_six_park_x < -60.0f || cxp - g_six_park_x > 110.0f) :   /* draws sit 33 left (CPU slot) to 105 right (player slot) of the park x; the settings window's top piece starts near x 870 */ std::fabs(cxp - g_six_park_lx) > 85.0f) continue;
            if (std::fabs(cyp - g_six_park_y[t]) > 14.0f) continue;
            /* only name-bar sized pieces (a test run: name 83x20 and 85x34, label 43x19; the band also
             * caught 250x129 .. 256x220 pieces of other screens on the way to Stage Select and back) */
            if (w * 640.0f > 130.0f || h * 360.0f > 45.0f) continue;
            const float kxn = g_six_park_k[q][0].load() / 640.0f, kyn = -g_six_park_k[q][1].load() / 360.0f;   /* NDC, y up */
            /* x' = A + s(x - A) with x = P + K must equal at + s(P - at): A = at - s*K/(1-s) */
            *ax = at - ts * kxn / (1.0f - ts); *ay = (t == 0 ? ay_red : ay_blue) - ts * kyn / (1.0f - ts);
            { static const bool pl = std::getenv("SIX_CSPARKLOG") != nullptr; static int seen[4][64], ns[4]; static int tot = 0;
              if (pl && tot < 60) { const int key = (int)(w * 640.0f) * 1000 + (int)(h * 360.0f); bool k = false; for (int z = 0; z < ns[q]; z++) k |= seen[q][z] == key;
                  if (!k && ns[q] < 64) { seen[q][ns[q]++] = key; tot++; std::printf("[cslay] band %d: draw centre %.0f %.0f size %.0f x %.0f\n", q, cxp, cyp, w * 640.0f, h * 360.0f); std::fflush(stdout); } } }
            static int said = 0; if (said++ < 4) { std::printf("[cslay] parked name %d draw at %.0f %.0f\n", 4 + t, cxp, cyp); std::fflush(stdout); }
            return ts; } }
    if (box[1] >= 0.735f && box[2] < 0.0f) return 1.0f;   /* title (after the parked bands: the label band is left of x 640) */
    if (box[0] > -0.07f) {   /* grid and its page row: left edge -0.04 -> gl, vertical centre kept */
        const float x0 = -0.04f; *ax = (gl - gs * x0) / (1.0f - gs); *ay = -0.10f; return gs;
    }
    /* team panels: right edge 0.084 -> tr; red top 0.73 stays, blue bottom -0.75 stays */
    *ax = at;
    const float cy = 0.5f * (box[1] + box[3]);
    *ay = cy >= -0.02f ? ay_red : ay_blue;
    return ts;
}

volatile unsigned long long g_vs_screen_ms = 0; volatile uint32_t g_vs_obj = 0;   /* the VS loading screen after Stage Select (classes 0x00882220.. 0x00882460, seen 2026-11-15): last call */

volatile bool g_six_loading = false;   /* SIX_ICALL: from leaving the character select to the fight */
volatile bool g_six_in_loader = false;   /* SIX_LOADER_ROLES: the loader runs (jsp_six_loader_enter) */
volatile bool g_six_builder_seen = false;   /* SIX_HOLD_ROLES: the battle builder has polled this load (the units have their load places) */
volatile uint32_t g_six_mgr_obj = 0;   /* SIX_RULE_LOG: the battle manager object (func_001404B0), +0x34 the timer */
volatile bool g_six_in_battle = false;   /* 3v3 overrides only from the battle loader to the battle teardown */
void (*g_six_end_battle)(const char* who) = nullptr;   /* set below to six_heap_restore: the end of a 3v3 battle */
volatile unsigned long long g_six_fight_t = 0;   /* SIX_BITS: tick of the fight's first HUD health read */
volatile uint32_t g_six_toc = 0;    /* SIX_BITS: the game's r2 (TOC) as seen in a hook */
volatile uint32_t g_six_mode = 0;   /* SIX_BITS: the mode object that runs the scene list (+0x130 scene, +0x134 the task id it waits for) */
volatile uint32_t g_six_ai[8] = {};   /* SIX_PADS: the AI unit of each battle unit */

/* SIX_WATCH=<hex guest address> (test only): a hardware data breakpoint (debug register 0, 4 bytes, read or
 * write) on that guest word in every thread of the game, set from a helper thread once the match is being picked.
 * Each distinct host instruction address that touches it is kept with a count and printed as an offset into
 * JStarsRecomp.exe ("[sixwatch] +0x..."), to be named with llvm-symbolizer and the build-pdb symbols: that names
 * the recompiled game function (func_XXXXXXXX) that reads the slot. */
uint32_t g_six_role_ea[2] = { 0, 0 };
struct SixHit { uint64_t rip; unsigned n; uint64_t callers[8]; int ncallers; };
SixHit g_six_hits[64]; volatile int g_six_nhits = 0;
LONG CALLBACK six_veh(EXCEPTION_POINTERS* e)
{
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    /* SIX_3V3=2: debug registers 1 and 2 sit on the two Support units' role words; right after the game writes
     * one, a Support (3) becomes a partner (2), before anything reads it */
    if (e->ContextRecord->Dr6 & 6) {
        extern uint32_t g_six_role_ea[2];
        for (int k = 0; k < 2; k++)
            if ((e->ContextRecord->Dr6 & (2ull << k)) && g_six_role_ea[k] && vm_read32(g_six_role_ea[k]) == 3u) vm_write32(g_six_role_ea[k], 2u);
        e->ContextRecord->Dr6 = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (!(e->ContextRecord->Dr6 & 1)) return EXCEPTION_CONTINUE_SEARCH;
    const uint64_t rip = e->ContextRecord->Rip;
    int i = 0;
    while (i < g_six_nhits && g_six_hits[i].rip != rip) i++;
    if (i == g_six_nhits && g_six_nhits < 64) {
        g_six_hits[i].rip = rip; g_six_hits[i].n = 0; g_six_hits[i].ncallers = 0;
        /* who called it: host frames unwound from the faulting context (named the same way, func_XXXXXXXX) */
        CONTEXT c = *e->ContextRecord;
        for (int f = 0; f < 8; f++) {
            DWORD64 img = 0;
            PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &img, nullptr);
            if (!fn) break;
            void* hd = nullptr; DWORD64 est = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, img, c.Rip, fn, &c, &hd, &est, nullptr);
            if (!c.Rip) break;
            g_six_hits[i].callers[g_six_hits[i].ncallers++] = c.Rip;
        }
        g_six_nhits = i + 1;
    }
    if (i < 64) g_six_hits[i].n++;
    e->ContextRecord->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}
DWORD WINAPI six_arm(LPVOID p)
{
    const uint64_t host = (uint64_t)(vm_base + (uint32_t)(uintptr_t)p);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te; te.dwSize = sizeof te;
    int armed = 0;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId()) continue;
        HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!th) continue;
        if (SuspendThread(th) != (DWORD)-1) {
            CONTEXT c = {}; c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(th, &c)) {
                if (host != (uint64_t)vm_base) {
                    c.Dr0 = host;
                    c.Dr7 = (c.Dr7 & ~0xF0003ull) | 1ull | (3ull << 16) | (3ull << 18);   /* L0, read/write, 4 bytes */
                }
                if (g_six_role_ea[0]) {   /* L1, L2: write only (01), 4 bytes */
                    c.Dr1 = (uint64_t)(vm_base + g_six_role_ea[0]); c.Dr2 = (uint64_t)(vm_base + g_six_role_ea[1]);
                    c.Dr7 = (c.Dr7 & ~(0xFFull << 20) & ~0x3Cull) | (1ull << 2) | (1ull << 4) | (1ull << 20) | (3ull << 22) | (1ull << 24) | (3ull << 26);
                }
                if (SetThreadContext(th, &c)) armed++;
            }
            ResumeThread(th);
        }
        CloseHandle(th);
    }
    CloseHandle(snap);
    std::printf("[sixwatch] guest %08X (host %llX) armed in %d threads, exe base %p\n", (uint32_t)(uintptr_t)p,
                (unsigned long long)host, armed, (void*)GetModuleHandleA(nullptr));
    std::fflush(stdout);
    return 0;
}
/* SIX_STACKS=<seconds> (test only): that long after the character select is left, from a helper thread, every
 * other thread of the game is paused and its host call stack (12 frames) printed as exe offsets, to name with
 * llvm-symbolizer on build-pdb: where a stuck "Now loading" waits. Once. */
DWORD WINAPI six_stacks(LPVOID)
{
    const uint64_t base = (uint64_t)GetModuleHandleA(nullptr);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te; te.dwSize = sizeof te;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId()) continue;
        HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!th) continue;
        if (SuspendThread(th) != (DWORD)-1) {
            CONTEXT c = {}; c.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(th, &c)) {
                std::printf("[sixstack] thread %lu:", te.th32ThreadID);
                for (int f = 0; f < 12 && c.Rip; f++) {
                    const uint64_t rip = c.Rip;
                    if (rip >= base && rip < base + 0x10000000ull) std::printf(" +0x%llX", (unsigned long long)(rip - base));
                    else std::printf(" (%llX)", (unsigned long long)rip);
                    DWORD64 img = 0;
                    PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &img, nullptr);
                    if (!fn) break;
                    void* hd = nullptr; DWORD64 est = 0;
                    RtlVirtualUnwind(UNW_FLAG_NHANDLER, img, c.Rip, fn, &c, &hd, &est, nullptr);
                }
                std::printf("\n");
            }
            ResumeThread(th);
        }
        CloseHandle(th);
    }
    CloseHandle(snap);
    std::fflush(stdout);
    return 0;
}
void six_stacks_tick(bool picking)
{
    if (!kExp) return;   /* experiment */
    static const char* e = std::getenv("SIX_STACKS");
    static unsigned long long left = 0; static bool done = false, was = false;
    if (!e || done) return;
    const unsigned long long now = GetTickCount64();
    if (was && !picking && !left) left = now;
    was = picking;
    if (left && now - left >= (unsigned long long)std::atoi(e) * 1000ull) {
        done = true;
        HANDLE t = CreateThread(nullptr, 0, six_stacks, nullptr, 0, nullptr);
        if (t) { WaitForSingleObject(t, 10000); CloseHandle(t); }
    }
}
void six_watch_tick(bool picking)
{
    if (!kExp) return;   /* experiment */
    six_stacks_tick(picking);
    static const char* e0 = std::getenv("SIX_WATCH");
    static const bool role = std::getenv("SIX_3V3") && std::getenv("SIX_3V3")[0] == '2';
    static const char* e = e0 ? e0 : (role ? "0" : nullptr);
    static bool armed = false; static int printed = 0;
    if (!e) return;
    if (role && !g_six_role_ea[0]) {
        const uint32_t w = vm_read32(vm_read32(0x008D3888u - 0x7A90u));
        if (w > 0x10000u && w < 0xF0000000u) {
            g_six_role_ea[0] = w + 0x60 + 4 * 0x94 + 8; g_six_role_ea[1] = w + 0x60 + 5 * 0x94 + 8;
            std::printf("[six] role watch on %08X %08X\n", g_six_role_ea[0], g_six_role_ea[1]); std::fflush(stdout);
        }
    }
    /* armed again every half second until 20 s after the character select is left: the match is loaded by threads
     * made after the first arming (sixwatch: no game hit after the select) */
    static unsigned long long last = 0, left = 0;
    const unsigned long long now = GetTickCount64();
    if (armed && !picking && !left) left = now;
    if (picking) left = 0;
    const bool again = armed && now - last > 500 && (picking || (left && now - left < 20000));
    if ((!armed && picking) || again) {
        if (!armed) AddVectoredExceptionHandler(1, six_veh);
        armed = true; last = now;
        HANDLE t = CreateThread(nullptr, 0, six_arm, (LPVOID)(uintptr_t)std::strtoul(e, nullptr, 16), 0, nullptr);
        if (t) { WaitForSingleObject(t, 5000); CloseHandle(t); }
    }
    const uint64_t base = (uint64_t)GetModuleHandleA(nullptr);
    while (printed < g_six_nhits) {
        const SixHit& h = g_six_hits[printed++];
        std::printf("[sixwatch] new hit +0x%llX at %llu callers", (unsigned long long)(h.rip - base), GetTickCount64());
        for (int f = 0; f < h.ncallers; f++) std::printf(" +0x%llX", (unsigned long long)(h.callers[f] - base));
        std::printf("\n");
        std::fflush(stdout);
    }
}

LONG CALLBACK six_so_veh(EXCEPTION_POINTERS* e)
{
    if (e->ExceptionRecord->ExceptionCode != 0xC00000FDu) return EXCEPTION_CONTINUE_SEARCH;
    static volatile LONG once = 0; if (InterlockedExchange(&once, 1)) return EXCEPTION_CONTINUE_SEARCH;
    void* fr[48]; const USHORT n = RtlCaptureStackBackTrace(0, 48, fr, nullptr);
    const uintptr_t mb = (uintptr_t)GetModuleHandleA(nullptr);
    char buf[48 * 12 + 64]; int len = std::snprintf(buf, sizeof buf, "[sixso] stack overflow, frames:");
    for (USHORT i = 0; i < n; i++) len += std::snprintf(buf + len, sizeof buf - len, " +%llX", (unsigned long long)((uintptr_t)fr[i] - mb));
    std::fprintf(stdout, "%s\n", buf); std::fflush(stdout);
    return EXCEPTION_CONTINUE_SEARCH;
}

/* SIX_CSEL slot messages (func_0018C40C(ctl, kind, arg), ctl = the Character Select controller, its cursor record at
 * ctl+0x58): it builds two messages on the stack, A at sp+0x70 for the fighter windows and B at sp+0x90 for the Support
 * windows, and hands A to the receiver at ctl+0xA8, B to ctl+0xAC. For a cell whose slot is > 3 the roles swap
 * (0x0018C570): A gets {2, arg, 0} and B gets {kind, arg, slot-4}, so slot 4/5 opened the Support "Skill" window
 * (a test run: window vt 0x0087C640 open on slot 4). With SIX_CSEL the 3rd-fighter slots get the
 * fighter-slot messages instead: A {kind, arg, slot}, B {2, arg, 0}, rewritten at the first receiver call. */
struct SixMsg { bool active, done; int slot; uint32_t kind; };
static thread_local SixMsg t_six_msg = { false, false, -1, 0 };
extern "C" void jsp_six_slotmsg_enter(ppu_context* ctx)
{
    static const bool on_env = std::getenv("SIX_CSEL") != nullptr && std::getenv("SIX_CSNOMSG") == nullptr; const bool on = on_env && six_live();
    t_six_msg = { false, false, -1, 0 };
    if (!on) return;
    const uint32_t rec = (uint32_t)ctx->gpr[3] + 0x58u, tab = vm_read32(rec + 4), ci = vm_read32(rec + 8);
    if (tab < 0x10000u || tab >= 0x30000000u || ci > 64u) return;
    const int sl = (int8_t)vm_base[tab + ci * 5u + 4u];
    { static int n = 0; if (std::getenv("SIX_CSLOG") && n++ < 40) { std::printf("[sixmsg] enter ctl %08X tab %08X kind %u arg %d cell %u slot %d\n", (uint32_t)ctx->gpr[3], tab, (uint32_t)ctx->gpr[4], (int32_t)ctx->gpr[5], ci, sl); std::fflush(stdout); } }
    if (sl == 4 || sl == 5) t_six_msg = { true, false, sl, (uint32_t)ctx->gpr[4] };
}
extern "C" void jsp_six_slotmsg_leave(ppu_context* ctx) { (void)ctx; t_six_msg.active = false; }
static void six_msg_fix(uint64_t* gpr)
{
    if (!t_six_msg.active || t_six_msg.done) return;
    t_six_msg.done = true;
    const uint32_t a = (uint32_t)gpr[4], b = a + 0x20u;
    if (a < 0x10000u || a >= 0x30000000u) return;
    const uint32_t a0 = vm_read32(a), a8 = vm_read32(a + 8), b0 = vm_read32(b), b8 = vm_read32(b + 8);
    { static int n = 0; if (std::getenv("SIX_CSLOG") && n++ < 40) { std::printf("[sixmsg] first call r4 %08X: A {%u,%u,%u} B {%u,%u,%u}\n", a, a0, vm_read32(a + 4), a8, b0, vm_read32(b + 4), b8); std::fflush(stdout); } }
    if (b8 != (uint32_t)(t_six_msg.slot - 4)) return;   /* not the layout built for a Support cell */
    const uint32_t k = t_six_msg.kind;
    if (k == 5u || k == 6u) { if (a0 != 2u || b0 != k) return; vm_write32(a, k); vm_write32(b, 2u); }
    vm_write32(a + 8, (uint32_t)t_six_msg.slot); vm_write32(b + 8, 0u);
    static int said = 0;
    if (said++ < 8) { std::printf("[sixmsg] slot %d kind %u: A {%u,_,%u} B {%u,_,%u} -> fighter-slot messages\n", t_six_msg.slot, k, a0, a8, b0, b8); std::fflush(stdout); }
}

bool six_cs_locked() { static const bool on = std::getenv("SIX_CSEL") != nullptr && std::getenv("SIX_CSUNLOCK") == nullptr; return on && six_live(); }
static void six_winmsg_enter_body(ppu_context* ctx);

extern "C" void jsp_six_winmsg_enter(ppu_context* ctx) { six_winmsg_enter_body(ctx); }
extern "C" void jsp_six_winmsg_leave(ppu_context* ctx) { jsp_map_leave(ctx); }
static void six_winmsg_enter_body(ppu_context* ctx)
{
    static const bool on_env = std::getenv("SIX_CSEL") != nullptr && std::getenv("SIX_CSNOWIN") == nullptr; const bool on = on_env && six_live();
    static const bool log = std::getenv("SIX_CSLOG") != nullptr;
    const uint32_t w = (uint32_t)ctx->gpr[3], m = (uint32_t)ctx->gpr[4];
    if (w < 0x10000u || w >= 0x30000000u) return;
    const uint32_t vt = vm_read32(w);
    if (log) { static int n = 0; if (n++ < 40 && m >= 0x10000u && m < 0x30000000u) {
        { /* the senders: saved LRs up the guest stack (frame back chain at [sp], LR save at [caller sp + 0x10]) */
          uint32_t sp = (uint32_t)ctx->gpr[1]; std::printf("[sixwmsg] callers:");
          for (int d = 0; d < 6 && sp >= 0x10000u; d++) { const uint32_t up = vm_read32(sp + 4); if (up <= sp || up - sp > 0x10000u) break;
              std::printf(" %08X", vm_read32(up + 0x14)); sp = up; }
          std::printf("\n"); }
        std::printf("[sixwmsg] window %08X vt %08X lr %08X msg", w, vt, (uint32_t)ctx->lr);
        for (uint32_t q = 0; q < 8; q++) std::printf(" %08X", vm_read32(m + q * 4)); std::printf("\n"); std::fflush(stdout); } }
    const uint32_t scr = g_seen_charsel.obj;
    /* 3v3 lock: the leader window (screen+0x460, the one slots 0 and 2 open) has no Team row. With the lock on, the
     * partner slots' window (+0x570) and slot 4 / 5's go to it as well. SIX_CSWIN=partner keeps the Team row (then the
     * pad filter holds Left / Right on it). */
    static const char* wsel0 = std::getenv("SIX_CSWIN");
    /* not static: with SIX_ASK the 3v3 choice comes later, and a window opened before it froze the lock off for the
     * whole session (the maintainer 2026-11-20: the Team row was back with 2 Players, started through the launcher) */
    const bool to_leader = wsel0 ? !std::strcmp(wsel0, "leader") : six_cs_locked();
    { static int said = 0; if ((vt == 0x0087C570u || vt == 0x0087C640u) && said++ < 4) { std::printf("[sixwmsg] window %08X vt %08X; screen object %08X (window - screen = %X)\n", w, vt, scr, w - scr); std::fflush(stdout); } }
    /* the three windows sit 0x110 apart (leader, partner, Skill): the others are found from the window itself. The test
     * "window == screen+0x570" did not hold on the 2 Players screen (a test run: no redirect,
     * the Team row was back), so the screen object is no longer used here. */
    if (on && to_leader && vt == 0x0087C570u && vm_read32(w - 0x110u) == 0x0087C4A0u) {
        ctx->gpr[3] = w - 0x110u;
        static const char* me = std::getenv("SIX_CSWINMODE"); const int mode = me ? std::atoi(me) : 0;
        if (mode >= 0 && m >= 0x10000u && m < 0x30000000u) vm_write32(m, (uint32_t)mode);
        static int said = 0; if (said++ < 4) { std::printf("[sixwmsg] partner window message -> leader window (cmd %u, mode %d)\n", m ? vm_read32(m + 4) : 0u, mode); std::fflush(stdout); }
        return; }
    if (!on || vt != 0x0087C640u) return;
    if (vm_read32(w - 0x110u) != 0x0087C570u || vm_read32(w - 0x220u) != 0x0087C4A0u) return;
    /* the partner window (screen+0x570, vt 0x0087C570) has the Team row (a test run: slot 1 and
     * slot 3 open it with msg {1, cmd, 0, char, 0/1, 2, 2, 0}); the leader window (+0x460) has no Team row.
     * SIX_CSWIN=leader uses the leader window; SIX_CSWINMODE=<n> writes msg+0 (window mode) */
    const bool leader = to_leader;
    const uint32_t fw = w - (leader ? 0x220u : 0x110u);
    if (vm_read32(fw) != (leader ? 0x0087C4A0u : 0x0087C570u)) return;
    ctx->gpr[3] = fw;
    /* window mode msg+0: 1 = partner (a test run: mode 2 left the Support's mode and the name box
     * drifted; mode 1 shows Team / Player / COM / Equip / Type / Health for slot 4, and BLUE turned the new box blue) */
    { static const char* me = std::getenv("SIX_CSWINMODE"); const int mode = me ? std::atoi(me) : (leader ? 0 : 1);
      if (mode >= 0 && m >= 0x10000u && m < 0x30000000u) vm_write32(m, (uint32_t)mode); }
    static int said = 0;
    if (said++ < 6) { std::printf("[sixwmsg] Support window message -> settings window %08X (cmd %u, mode %u)\n", fw, m ? vm_read32(m + 4) : 0u, m ? vm_read32(m) : 0u); std::fflush(stdout); }
}
/* func_00188A58(obj) runs each player's Character Select state (state word at obj+0x60+0x14*player, 16 states). After
 * a pick the grid state func_00188660 sets state 9 for slots 0..3 and state 0xD for slots > 3 (0x00188810: cmplwi
 * slot,3 / bgt). State 0xD is the Support window: Left/Right and confirm only. State 9 leads to func_0018673C, the
 * fighter window state, which also sends Up (cmd 3) and Down (cmd 4) (a test run: slot 1 got
 * cmd 4 on Down, slot 4 got nothing). With SIX_CSEL a fresh 0xD on slot 4/5 becomes 9. SIX_CSNOSTATE=1 turns it off. */
static thread_local uint32_t t_six_state_obj = 0;
static std::atomic<uint32_t> g_six_state_obj{0};   /* for the pad filter (3v3 lock) */
static std::atomic<uint32_t> g_six_cs_dump_req{0};
extern "C" void jsp_six_state_enter(ppu_context* ctx) { t_six_state_obj = (uint32_t)ctx->gpr[3]; g_six_state_obj.store(t_six_state_obj); }
extern "C" void jsp_six_state_leave(ppu_context* ctx)
{
    (void)ctx;
    static const bool on_env = std::getenv("SIX_CSEL") != nullptr && std::getenv("SIX_CSNOSTATE") == nullptr; const bool on = on_env && six_live();
    const uint32_t obj = t_six_state_obj, scr = g_seen_charsel.obj;
    if (!on || obj < 0x10000u || obj >= 0x30000000u || scr < 0x10000u || scr >= 0x30000000u) return;
    static uint32_t last[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu };
    for (uint32_t pl = 0; pl < 2; pl++) {
        const uint32_t rec = obj + 0x60u + 0x14u * pl, st = vm_read32(rec);
        if (st != last[pl]) { static int n = 0; if (std::getenv("SIX_CSLOG") && n++ < 80) { std::printf("[sixstate] player %u state %u -> %u\n", pl, last[pl], st); std::fflush(stdout); } }
        if (st == 0xDu && last[pl] != 0xDu) {
            const uint32_t cr = scr + 0x3C8u + 0x1Cu * pl, tab = vm_read32(cr + 4), ci = vm_read32(cr + 8);
            const int sl = (tab >= 0x10000u && tab < 0x30000000u && ci < 64u) ? (int8_t)vm_base[tab + ci * 5u + 4u] : -1;
            if (sl == 4 || sl == 5) { vm_write32(rec, 9u);
                static int said = 0; if (said++ < 6) { std::printf("[sixstate] player %u slot %d: Support window state 0xD -> fighter window state 9\n", pl, sl); std::fflush(stdout); }
                last[pl] = 9u; continue; } }
        last[pl] = st; }
}

bool g_six_ask_up = false;           /* SIX_ASK's team size window is up (the auto pilot answers it) */

/* SIX_ASK=1 (the maintainer 2026-11-19): the team size is chosen on Free Battle's 1 Player / 2 Players list (screen word
 * 0x0095654C == 2 with the main menu's row 3). Cross / Start there opens the port's two-button window "2 vs 2" /
 * "3 vs 3"; the answer sets six_live() and the game then gets its Cross. Training and Impossible (the Battle Modes
 * page's rows, g_mp_entry) and every other mode stay 2 vs 2; back on the main menu the answer is forgotten. The
 * auto pilot is asked too since 2026-11-20 and answers like a player: 3 vs 3 with SIX_ASK_AUTO=3, else 2 vs 2. */
static unsigned long long g_six_ask_at = 0;   /* when the team size was last answered */
static bool g_six_to_fight = false;           /* Stage Select was left towards the fight (see fb_list) */
void six_ask_pad(uint32_t port, uint16_t* b, uint16_t* lx, uint16_t* ly)
{
    static const bool ask = std::getenv("SIX_ASK") != nullptr;
    if (!ask || port > 1) return;
    const uint16_t CROSS = 0x4000, CIRCLE = 0x2000, LEFT = 0x0080, RIGHT = 0x0020, START = 0x0008;
    static uint16_t prev_b[2] = { 0, 0 }, prev_dir = 0; static int state = 0, pulse = 0; static uint32_t owner = 0;
    const uint16_t cur = *b, pressed = (uint16_t)(cur & ~prev_b[port]); prev_b[port] = cur;
    g_six_ask_up = state == 1;
    if (state == 1) {   /* the window is up */
        if (!cellOskDialog_overlay_active()) {
            const int r = cellOskDialog_confirm_poll();   /* 1 the left button, 0 the right, else backed out */
            if (r == 1 || r == 0) { g_six_live.store(r == 0 ? 1 : 0); state = 2; pulse = 0;
                std::printf("[six] team size: %s\n", r == 0 ? "3 vs 3" : "2 vs 2"); std::fflush(stdout); }
            /* the maintainer 2026-11-19: the window is left only by picking one of the two. A backing out (the keyboard's Esc
             * reaches the window directly) opens it again at once. */
            else if (cellOskDialog_choice_open(L"Choose the team size", L"2 vs 2", L"3 vs 3")) {
                prev_dir = 0; std::printf("[six] team size window: backing out is not an answer, asked again\n"); std::fflush(stdout); }
            else state = 3;
        } else {
            const uint16_t dir = (uint16_t)(((cur & LEFT) || *lx < 64) ? LEFT : ((cur & RIGHT) || *lx > 192) ? RIGHT : 0);
            if (dir && dir != prev_dir) cellOskDialog_overlay_key(dir == LEFT ? 0x25u : 0x27u);
            prev_dir = dir;
            if (pressed & CROSS) cellOskDialog_overlay_key(0x0Du);
            /* Circle does nothing here: the window is left only by picking (the maintainer 2026-11-19) */
        }
        *b = 0; *lx = 128; *ly = 128; return;
    }
    if ((state == 2 || state == 3) && port != owner) { *b = 0; *lx = 128; *ly = 128; return; }
    if (state == 2) {   /* the game goes on from a fresh Cross */
        if (pulse == 0 && cur != 0) { *b = 0; return; }
        pulse++; *b = (uint16_t)((pulse >= 2 && pulse <= 5) ? CROSS : 0);
        if (pulse > 6) { state = 0; g_six_ask_at = GetTickCount64(); }
        return;
    }
    if (state == 3) { if (cur == 0) state = 0; else { *b = 0; return; } }
    if (main_list_open()) {   /* the main menu: nothing chosen */
        g_six_to_fight = false;
        static const bool auto3 = [] { const char* e = std::getenv("SIX_ASK_AUTO"); return e && e[0] == '3'; }();
        const int want = (false && auto3) ? 1 : 0;
        if (g_six_live.load() != want) { g_six_live.store(want); std::printf("[six] main menu: team size %s\n", want ? "3 vs 3 (auto pilot)" : "2 vs 2 until chosen"); std::fflush(stdout); }
        return;
    }
    /* Only on the 1 Player / 2 Players list: the screen word stays 2 on Character Select and Stage Select, so the window
     * came up for every Cross there (the maintainer 2026-11-19). Not while one of those screens is up, and not for 2.5 s after an
     * answer (the list is still closing then). */
    /* Stage Select left forward (not back to Character Select): the VS screen and the loading keep the same screen word,
     * and a Cross there opened the window and answered 2 vs 2 in the middle of a 3 vs 3 start (a test run
     * 3v3-walk, the auto pilot pressing Cross on the VS screen). Shut until Character Select or the main menu is up again. */
    static bool was_stagesel = false, was_charsel = false; bool& to_fight = g_six_to_fight;
    /* Character Select just closed: it may be going on to Stage Select (Start, Finish) or back to the list. A Cross /
     * Start in that moment opened the window as a fresh choice (the maintainer 2026-11-20: Player 2's second Start popped it up
     * and the auto pilot's Cross on Stage Select answered 2 vs 2). For 2 s, until Stage Select shows, such a press is
     * dropped: neither the window nor the game gets it (on the list a player just presses again). */
    static unsigned long long cs_off_t = 0;
    { const bool cs = on_charsel(), ss = on_stagesel();
      if (cs) to_fight = false;
      else if (was_stagesel && !ss) to_fight = true;
      if (was_charsel && !cs) cs_off_t = GetTickCount64();
      if (cs || ss) cs_off_t = 0;
      was_stagesel = ss; was_charsel = cs; }
    const bool listish = in_menu_scene() && vm_read32(0x0095654Cu) == 2u && vm_read32(0x00956550u) == 3u && g_mp_entry == 0 && !training_active()
                         && !on_charsel() && !on_stagesel() && !to_fight;
    if (listish && cs_off_t && GetTickCount64() - cs_off_t < 2000 && (cur & (CROSS | START))) {
        if (pressed & (CROSS | START)) { static int said = 0; if (said++ < 6) { std::printf("[six] Cross / Start right after Character Select closed (player %u): dropped\n", port); std::fflush(stdout); } }
        *b = (uint16_t)(cur & ~(CROSS | START)); return;
    }
    /* the list itself must be up: the main menu object ticks while the 1 Player / 2 Players list shows (two_player_watch
     * uses the same test). the maintainer 2026-11-20: when Player 2 joined on Character Select the screen was not seen for a
     * moment, and Player 2's Start opened the window there. */
    const bool list_up = screen_up(g_seen_main, 0x0087D600u);
    if (listish && (pressed & (CROSS | START))) { static int said = 0; if (said++ < 12) { std::printf("[six] team size press (player %u): list %s, %llu ms since the last answer\n", port, list_up ? "up" : "NOT up: no window", GetTickCount64() - g_six_ask_at); std::fflush(stdout); } }
    const bool fb_list = listish && list_up && GetTickCount64() - g_six_ask_at > 2500;
    if (fb_list && (pressed & (CROSS | START)) && cellOskDialog_choice_open(L"Choose the team size", L"2 vs 2", L"3 vs 3")) {
        state = 1; owner = port; prev_dir = 0; *b = (uint16_t)(cur & ~(CROSS | START)); g_six_ask_up = true;
    }
}

/* Game map probes through function-entry hooks (2026-11-15, experiments only: a local test hook list
 * and firstrun's lift step pass --fn-hooks to the lifter, toolkit 6014). The symbols are defined in every build; only a lift
 * with that file calls them. Output only with MAP_PROBE=1 ([map] lines).
 *   0x00012A00 AI think step (P-H5-2 / P-H5-4): calls per r3 (the AI unit), reported every 10 s
 *   0x000115A8 AI reflex press (P-H5-3): r3 pad, r4-r7, first 300 calls
 *   0x001DF598 main menu row dispatch (P-H4-1): r3-r5, [r3+0x14], [0x00956550] */
/* SIX_NEWTEST=2: after the battle-leave fade starts, test new(0x40)/free at every hooked function's return and print
 * each new return address once, until the first failure (which hooked call broke the game's allocator) */
static volatile uint32_t g_six_leave_seen = 0;
static volatile bool g_six_in_ee48 = false;   /* inside func_0002EE48 (SIX_FREE48) */

/* 3v3 research (the maintainer 2026-11-16: continue looking at 6 fighters in a match; FINDINGS "Where the battle builds its
 * fighters"). SIX_LOADER=1 (test only): the match loader func_000CEE0C (and its twin func_000CE3D4) on entry with the six
 * battle-world units (character, role 1 leader / 2 partner / 3 Support, team), on leave with its place bits (loader +0x68:
 * an 8-byte bit set, func_0043D1D8 sets a bit, func_0043D204 tests one; places 0x10-0x13 fighters, 0x14-0x15 Supports),
 * and every func_00423F14(pool, id) call (pool +0x14 = id, then the pool's virtual +0x08) with its caller. */
static bool six_on() { static const bool on = std::getenv("SIX_LOADER") != nullptr; return on && six_live(); }
static thread_local uint32_t t_six_loader = 0;
static void six_place_fix(ppu_context* ctx, const char* where);
static void six_units(const char* tag)
{
    const uint32_t w0 = vm_read32(0x008D3888u - 0x7A90u), world = (w0 >= 0x00010000u && w0 < 0x30000000u) ? vm_read32(w0) : 0u;
    std::printf("[six] %s world %08X", tag, world);
    if (world >= 0x10000000u && world < 0x20000000u) {
        for (uint32_t k = 0; k < 6; k++) { const uint32_t u = world + 0x60u + 0x94u * k;
            std::printf(" | u%u ch %u role %u team %u", k, vm_read32(u) >> 16, vm_read32(u + 0x08), vm_read32(u + 0x0C)); }
        for (int t = 0; t < 2; t++) { const uint32_t l = world + 0x34u + 0x18u * (uint32_t)t;
            std::printf(" | team%d [%d %d %d %d] +10 %d", t, (int32_t)vm_read32(l + 4), (int32_t)vm_read32(l + 8), (int32_t)vm_read32(l + 12), (int32_t)vm_read32(l + 16), (int32_t)vm_read32(l + 0x10)); }
    }
    std::printf("\n"); std::fflush(stdout);
}
extern "C" void jsp_six_loader_enter(ppu_context* ctx)
{
    g_six_in_battle = true;   /* the battle loader (func_000CEE0C) runs before the pools and places are set up */
    if (!six_on()) return;
    t_six_loader = (uint32_t)ctx->gpr[3];
    /* SIX_LOADER_ROLES=1 (test): units 4 and 5 count as Supports while the loader sizes its places (so all six places
     * are prepared), and as partners again right after */
    static const bool roles = std::getenv("SIX_LOADER_ROLES") != nullptr;
    if (roles) { g_six_in_loader = true;
        const uint32_t w0 = vm_read32(0x008D3888u - 0x7A90u), world = (w0 >= 0x00010000u && w0 < 0x30000000u) ? vm_read32(w0) : 0u;
        if (world >= 0x10000000u && world < 0x20000000u) for (uint32_t k = 4; k < 6; k++) { const uint32_t u = world + 0x60u + 0x94u * k; if (vm_read32(u + 8) == 2u) vm_write32(u + 8, 3u); } }
    std::printf("[six] loader enter r3 %08X lr %08X\n", t_six_loader, (uint32_t)ctx->lr);
    six_units("units");
}
extern "C" void jsp_six_loader_leave(ppu_context* ctx)
{
    (void)ctx;
    if (!six_on()) return;
    static const bool roles = std::getenv("SIX_LOADER_ROLES") != nullptr;
    if (roles) { const uint32_t w0 = vm_read32(0x008D3888u - 0x7A90u), world = (w0 >= 0x00010000u && w0 < 0x30000000u) ? vm_read32(w0) : 0u;
        if (world >= 0x10000000u && world < 0x20000000u) for (uint32_t k = 4; k < 6; k++) { const uint32_t u = world + 0x60u + 0x94u * k; if (vm_read32(u + 8) == 3u) vm_write32(u + 8, 2u); }
        g_six_in_loader = false; }
    six_place_fix(ctx, "loader");
    const uint32_t l = t_six_loader, bits = l ? vm_read32(l + 0x68) : 0u;
    std::printf("[six] loader leave: place bits %08X %08X\n", bits ? vm_read32(bits) : 0u, bits ? vm_read32(bits + 4) : 0u);
    std::fflush(stdout);
}
/* SIX_LOADER: the battle builder and its per-unit calls (func_000ED344 the builder, func_0005AEE0 a fighter,
 * func_0005AB70 a Support, func_0005A9B0 both, func_0008A540 a health object), counted and the first ones logged */
static void six_trace(const char* tag, ppu_context* ctx)
{
    if (!six_on()) return;
    static int n = 0, nb = 0;
    if (tag[0] == 'b') { if (nb++ >= 3) return; } else if (n++ >= 300) return;   /* the builder is polled every frame */
    std::printf("[six] %s r3 %08X r4 %08X r5 %08X r6 %08X lr %08X t=%llu\n", tag, (uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[4], (uint32_t)ctx->gpr[5],
                (uint32_t)ctx->gpr[6], (uint32_t)ctx->lr, (unsigned long long)GetTickCount64());
    std::fflush(stdout);
}
/* the builder waits until three objects of the battle manager (func_0014047C's object -> func_00140D74 +0x18,
 * func_00140520 +0x1C, func_00140B8C +0x20) are ready (func_0002FC54: +0x08 set and virtual +0x5C non-zero): their
 * words every second while it waits */
/* The battle manager's +0x1C object (vt 0x00876760) has one child per unit (vt 0x00876750, +0x24 the unit's load place
 * 0-5 = places 0x10-0x15, +0x28 its character). In a normal 2v2 the fighters get 0-3 and the Supports 4-5; with six
 * fighters the 5th and 6th get 0 and 1 again (run six-3v3-wait3), two fighters in one place. SIX_PLACE_FIX=1 (test):
 * a place used twice moves to the first free one. */
static void six_place_fix(ppu_context* ctx, const char* where)
{
    static const bool on = std::getenv("SIX_PLACE_FIX") != nullptr;
    if (!on) return;
    static ppu_context c; c = *ctx; c.lr = 0;
    func_0014047C(&c); const uint32_t mgr = (uint32_t)c.gpr[3];
    if (mgr < 0x00010000u || mgr >= 0x30000000u) return;
    c = *ctx; c.lr = 0; c.gpr[3] = mgr; func_001404B0(&c); const uint32_t o = (uint32_t)c.gpr[3];
    const uint32_t x = (o >= 0x10000000u && o < 0x20000000u) ? vm_read32(o + 0x1C) : 0u;
    if (x < 0x10000000u || x >= 0x20000000u || vm_read32(x) != 0x00876760u) return;
    bool used[6] = {}; uint32_t ch[6];
    for (uint32_t k = 0; k < 6; k++) { ch[k] = vm_read32(x + 0x14 + 4 * k); if (ch[k] < 0x10000000u || ch[k] >= 0x20000000u || vm_read32(ch[k]) != 0x00876750u) return; }
    for (uint32_t k = 0; k < 6; k++) {
        const uint32_t pl = vm_read32(ch[k] + 0x24);
        if (pl >= 6) continue;   /* not given out yet */
        if (!used[pl]) { used[pl] = true; continue; }
        int f = 0; while (f < 6 && used[f]) f++;
        if (f == 6) continue;
        vm_write32(ch[k] + 0x24, (uint32_t)f); used[f] = true;
        std::printf("[six] place fix (%s): child %u (character %u) place %u -> %d\n", where, k, vm_read32(ch[k] + 0x28), pl, f); std::fflush(stdout);
    }
}
extern "C" void jsp_six_builder_enter(ppu_context* ctx)
{
    six_place_fix(ctx, "builder");
    if (!g_six_builder_seen) { g_six_builder_seen = true; if (six_on()) { std::printf("[six] builder polled: Support roles may switch now\n"); std::fflush(stdout); } }
    six_trace("builder", ctx);
    if (!six_on()) return;
    static unsigned long long last = 0; static int lines = 0;
    const unsigned long long now = GetTickCount64();
    if (now - last < 1000 || lines >= 60) return;
    last = now; lines++;
    static ppu_context c; c = *ctx; c.lr = 0;
    func_0014047C(&c); const uint32_t mgr = (uint32_t)c.gpr[3];
    std::printf("[six] wait t=%llu mgr %08X", now, mgr);
    if (mgr >= 0x00010000u && mgr < 0x30000000u) {
        const uint32_t d = vm_read32(mgr);   /* func_001404B0 is the object the getters read */
        (void)d;
        c = *ctx; c.lr = 0; c.gpr[3] = mgr; func_001404B0(&c); const uint32_t o = (uint32_t)c.gpr[3];
        std::printf(" obj %08X", o); g_six_mgr_obj = o;
        for (uint32_t k = 0x18; k <= 0x20; k += 4) { const uint32_t x = vm_read32(o + k);
            std::printf(" | +%02X %08X", k, x);
            if (x >= 0x00010000u && x < 0x30000000u) { std::printf(" vt %08X +08 %08X:", vm_read32(x), vm_read32(x + 8));
                for (uint32_t q = 0x0C; q < 0x40; q += 4) std::printf(" %08X", vm_read32(x + q)); } }
    }
    std::printf("\n");
    if (mgr >= 0x00010000u && mgr < 0x30000000u) {   /* the +0x1C object (vt 0x00876760) holds 6 children at +0x14..+0x28: one per unit? */
        c = *ctx; c.lr = 0; c.gpr[3] = mgr; func_001404B0(&c); const uint32_t o = (uint32_t)c.gpr[3], x = vm_read32(o + 0x1C);
        if (x >= 0x10000000u && x < 0x20000000u)
            for (uint32_t k = 0; k < 6; k++) { const uint32_t ch = vm_read32(x + 0x14 + 4 * k);
                std::printf("[six] wait   child %u %08X:", k, ch);
                if (ch >= 0x10000000u && ch < 0x20000000u) for (uint32_t q = 0; q < 0x60; q += 4) std::printf(" %08X", vm_read32(ch + q));
                std::printf("\n"); }
    }
    std::fflush(stdout);
}
extern "C" void jsp_six_fighter_enter(ppu_context* ctx) { six_trace("fighter", ctx); }
extern "C" void jsp_six_support_enter(ppu_context* ctx) { six_trace("support", ctx); }
extern "C" void jsp_six_unit_enter(ppu_context* ctx) { six_trace("unit", ctx); }
extern "C" void jsp_six_unit_leave(ppu_context* ctx) { six_trace("unit done", ctx); }
/* func_000C7150 is the tail of the team object's set-up (func_000C74D8: +0x08.. fighters' unit numbers, +0x18 its
 * Support, +0x1C members, +0x20 fighters): SIX_FIX_RULES=1 moves a Support that SIX_3V3 makes a fighter (units in
 * SIX_3V3_MASK, default 4 and 5, with a fighter's character) into the fighters before the fight starts */
extern "C" void jsp_six_team_enter(ppu_context* ctx)
{
    static const bool fr_env = std::getenv("SIX_FIX_RULES") != nullptr; const bool fr = fr_env && six_live();
    if (!fr) return;
    static const int mask = [] { const char* e = std::getenv("SIX_3V3_MASK"); return e ? (int)std::strtol(e, nullptr, 16) : 0x30; }();
    const uint32_t a = (uint32_t)ctx->gpr[3];
    if (a < 0x10000000u || a >= 0x20000000u || vm_read32(a) != 0x00876558u) return;
    const int32_t sup = (int32_t)vm_read32(a + 0x18), nf = (int32_t)vm_read32(a + 0x20);
    const uint32_t w0 = vm_read32(0x008D3888u - 0x7A90u), world = (w0 >= 0x00010000u && w0 < 0x30000000u) ? vm_read32(w0) : 0u;
    if (sup < 0 || sup >= 6 || !((mask >> sup) & 1) || nf < 0 || nf >= 4 || world < 0x10000000u || world >= 0x20000000u) return;
    const uint32_t ch = vm_read32(world + 0x60u + 0x94u * (uint32_t)sup) >> 16;
    if (ch >= 200) return;   /* a Support-only character stays a Support */
    vm_write32(a + 0x08 + 4u * (uint32_t)nf, (uint32_t)sup); vm_write32(a + 0x20, (uint32_t)(nf + 1)); vm_write32(a + 0x18, 0xFFFFFFFFu);
    std::printf("[six] team object %08X set up: unit %d (character %u) moved from Support to fighter %d\n", a, sup, ch, nf); std::fflush(stdout);
}
/* func_000D2F5C(child, place, unit record) gives a unit's loading child its place (+0x24 = r4) and character (+0x28 =
 * the unit's halfword); called from func_000D4C18 <- func_000EBBC8 (SIX_WATCH run six-watch-place2). Fighters get
 * places 0,1,2,3 and then 0,1 again. SIX_PLACE_ENTRY=1 (3v3 test): a fighter in unit 4 or 5 gets place 4 or 5. */
static unsigned long long g_six_place_last = 0; static int g_six_place_used = 0;
   /* places given in the current load */
/* SIX_SEQPLACE: the team setup (func_000EBBC8) first calls func_000D4A30(CM, n, record) to write resource entry n's
 * record (+0x10..), then func_000D4C18 -> func_000D2F5C with the same n. Both must get the same new n (the next place
 * in call order), or the entry record and the loaded packs end up in different entries (battle_end.md section 13). */
extern "C" void jsp_six_d4a30_enter(ppu_context* ctx)
{
    static const bool seq_env = std::getenv("SIX_SEQPLACE") != nullptr; const bool seq = seq_env && six_live();
    if (!seq) return;
    const unsigned long long now = GetTickCount64();
    int used = (now - g_six_place_last > 2000) ? 0 : g_six_place_used;
    int c = 0; for (int b = 0; b < 6; b++) c += (used >> b) & 1;
    if (c < 6) { static int n = 0; if (n++ < 12) { std::printf("[six] entry record n %d -> %d\n", (int32_t)ctx->gpr[4], c); std::fflush(stdout); } ctx->gpr[4] = (uint32_t)c; }
}
extern "C" void jsp_six_place_enter(ppu_context* ctx)
{
    static const bool on = std::getenv("SIX_PLACE_ENTRY") != nullptr;
    if (!on && !six_on()) return;
    const uint32_t child = (uint32_t)ctx->gpr[3], place = (uint32_t)ctx->gpr[4], rec = (uint32_t)ctx->gpr[5];
    g_six_in_battle = true;
    const uint32_t w0 = vm_read32(0x008D3888u - 0x7A90u), world = (w0 >= 0x00010000u && w0 < 0x30000000u) ? vm_read32(w0) : 0u;
    int unit = -1;
    if (world >= 0x10000000u && rec >= world + 0x60u && rec < world + 0x60u + 6u * 0x94u && (rec - world - 0x60u) % 0x94u == 0) unit = (int)((rec - world - 0x60u) / 0x94u);
    const uint32_t role = unit >= 0 ? vm_read32(rec + 8) : 0u;
    uint32_t want = place;
    /* places of this load so far (a new load: more than 2 s since the last call); the game's own count runs on with
     * 0,1,2,3,0,1, so with more than 4 fighters each fighter takes the lowest free place instead */
    unsigned long long& last = g_six_place_last; int& used = g_six_place_used;
    const unsigned long long now = GetTickCount64();
    if (now - last > 2000) used = 0;
    last = now;
    if (on && (role == 1u || role == 2u) && (((used >> place) & 1) || (place < 4 && unit >= 4))) {
        int f = 0; while (f < 6 && ((used >> f) & 1)) f++;
        if (f < 6) want = (uint32_t)f;
    }
    /* SIX_SEQPLACE=1 (with SIX_FULL45=all): places in call order 0..5, the order the pools are taken from the fighter
     * free list, so each fighter's pack lands in its own place (a test run had them one off) */
    { static const bool seq_env = std::getenv("SIX_SEQPLACE") != nullptr; const bool seq = seq_env && six_live();
      if (seq) { int c = 0; for (int b = 0; b < 6; b++) c += (used >> b) & 1; want = (uint32_t)c; } }
    if (want < 6) used |= 1 << want;
    /* SIX_HOLD_ROLES=2: units 4/5 stay Supports only until their own places are given (here, on the game's thread),
     * then become partners at once, before the rest of the load reads the roles */
    { static const bool early_env = [] { const char* e = std::getenv("SIX_HOLD_ROLES"); return e && e[0] == '2'; }(); const bool early = early_env && six_live();
      static int done45 = 0; if (want < 6 && used == (1 << want)) done45 = 0;   /* the first place of a new load */
      if (early && unit >= 4 && role == 3u) {
          done45 |= 1 << (unit - 4);
          if (done45 == 3) { g_six_builder_seen = true;   /* lets the per-poll switch run too */
              for (uint32_t k = 4; k < 6; k++) { const uint32_t u = world + 0x60u + 0x94u * k;
                  if (vm_read32(u + 8) == 3u && (vm_read32(u) >> 16) < 200u) { vm_write32(u + 8, 2u);
                      /* SIX_SLOT45=1 (test): +0x1C is 0..3 for units 0..3 and 3 for both Supports; give units 4/5 their
                       * own number (fighters 5/6 take no damage and loop a stamina-charge start while sharing unit 3's) */
                      static const bool slot45_env = std::getenv("SIX_SLOT45") != nullptr; const bool slot45 = slot45_env && six_live();
                      if (slot45) vm_write32(u + 0x1C, k); } }
              /* SIX_ROLE0_MASK=<hex> (test): units in the mask get role 0 here (a 2v2 through units 4/5 whose units 2/3 are
               * out, so that the battle manager's 4-slot unit list, func_0003FFB8, holds 0, 1, 4, 5) */
              { static const uint32_t m0 = [] { const char* e = std::getenv("SIX_ROLE0_MASK"); return e ? (uint32_t)std::strtoul(e, nullptr, 16) : 0u; }();
                for (uint32_t k = 0; k < 6; k++) if (m0 & (1u << k)) vm_write32(world + 0x60u + 0x94u * k + 8, 0u); }
              std::printf("[six] places given: units 4 and 5 are partners now\n"); std::fflush(stdout); } } }
    if (want != place) ctx->gpr[4] = want;
    if (six_on()) { std::printf("[six] place: child %08X unit %d role %u character %u place %u%s\n", child, unit, role, rec ? (vm_read32(rec) >> 16) : 0u, want,
                                want != place ? " (moved)" : ""); std::fflush(stdout); }
}
/* func_000C6610(factory, component type, unit, record) makes one battle component of a unit; it asks func_000C5750 for
 * the type's free list, and skips the component when that list's head is 0 (none left). SIX_COMP=1 (3v3 test): every
 * component a unit could not get, by type and unit (a pool sized for 4 fighters would run dry at the 5th). */
static thread_local uint32_t t_comp_type = 0, t_comp_unit = 0;
extern "C" void jsp_six_comp_enter(ppu_context* ctx)
{
    t_comp_type = (uint32_t)ctx->gpr[4]; t_comp_unit = (uint32_t)ctx->gpr[5];
}
extern "C" void jsp_six_freelist_leave(ppu_context* ctx)
{
    static const bool on = std::getenv("SIX_COMP") != nullptr;
    if (!on) return;
    const uint32_t head = (uint32_t)ctx->gpr[3];
    const uint32_t first = (head >= 0x00010000u && head < 0x30000000u) ? vm_read32(head) : 0xFFFFFFFFu;
    static int n = 0, ok = 0;
    if (first == 0) { if (n++ < 60) { std::printf("[sixc] no free component: type 0x%X unit %u (list %08X)\n", t_comp_type, t_comp_unit, head); std::fflush(stdout); } }
    else if (ok++ < 3) { std::printf("[sixc] component type 0x%X unit %u ok\n", t_comp_type, t_comp_unit); std::fflush(stdout); }
}
/* func_0041F164: a HUD message object's update; message 0x17 plays "fl_timeup". SIX_MSG=1 (3v3 test): the object and
 * its words +0x10 and +0x4A8 whenever they change, to find which word picks the message (then SIX_WATCH it) */
/* SIX_BITS=1 (test, 3v3 "Time's Up" hunt, after the combat map's battle_end.md): the battle phase bits on the
 * manager's +0x04 object. Entry hooks on the set/clear functions log the object, the bit (r4), LR and two back-chain
 * return addresses, time since the fight's first HUD health read; the finish scene func_000FB8E0 and the team-out
 * func_000881A4 too. Each (function, bit, LR) is logged up to 4 times. */
static void six_bits_log(const char* what, ppu_context* ctx)
{
    static const bool on = std::getenv("SIX_BITS") != nullptr;
    if (!on) return;
    struct K { const char* w; uint32_t b, lr; int n; };
    static K seen[512]; static int ns = 0; static int total = 0;
    const uint32_t lr = (uint32_t)ctx->lr, bit = (uint32_t)ctx->gpr[4];
    int i = 0; while (i < ns && !(seen[i].w == what && seen[i].b == bit && seen[i].lr == lr)) i++;
    if (i == ns) { if (ns >= 512) return; seen[ns] = { what, bit, lr, 0 }; ns++; }
    if (seen[i].n++ >= 4 || total++ > 600) return;
    const uint32_t sp = (uint32_t)ctx->gpr[1];
    const uint32_t s1 = (uint32_t)vm_read64(sp), l1 = s1 ? (uint32_t)vm_read64(s1 + 0x10) : 0u;
    const uint32_t s2 = s1 ? (uint32_t)vm_read64(s1) : 0u, l2 = s2 ? (uint32_t)vm_read64(s2 + 0x10) : 0u;
    const uint32_t s3 = s2 ? (uint32_t)vm_read64(s2) : 0u, l3 = s3 ? (uint32_t)vm_read64(s3 + 0x10) : 0u;
    const uint32_t s4 = s3 ? (uint32_t)vm_read64(s3) : 0u, l4 = s4 ? (uint32_t)vm_read64(s4 + 0x10) : 0u;
    const unsigned long long now = GetTickCount64();
    const long long dt = g_six_fight_t ? (long long)(now - g_six_fight_t) : -1;
    const uint32_t o = (uint32_t)ctx->gpr[3];
    const bool ok = o >= 0x10000000u && o < 0x20000000u && !(o & 3);
    std::printf("[sixb] %s r3 %08X r4 %08X lr %08X <- %08X <- %08X <- %08X <- %08X fight+%lld ms now %08X ever %08X\n", what, o, bit, lr, l1, l2, l3, l4, dt,
                ok ? vm_read32(o + 0x14) : 0u, ok ? vm_read32(o + 0x18) : 0u);
    std::fflush(stdout);
}
/* SIX_UNIT_TABLE=1 (3v3 fix, 2026-11-16): the battle world keeps a per-unit move table, world + 0x7B8 + unit *
 * 0x11DF0 (func_00030AAC; 0x23BD entries of 8 bytes, written by func_000BDA7C), sized for 4 fighters only. A 5th or
 * 6th fighter (units 4/5) writes past the world into the next heap objects - the battle event flags among them, whose
 * "time over" bit then ends the fight ("Time's Up", a test run). Units 4 and 5 get their
 * own tables in guest memory the game does not use (0x7E000000, committed here), cleared when the world changes. */
static const uint32_t kSixUnitTable = 0x7E000000u, kSixUnitStride = 0x12000u;
static thread_local int t_six_unit = -1;
static bool six_unit_table_on()
{
    static const bool on = [] { const char* e = std::getenv("SIX_UNIT_TABLE"); return e && *e == '1'; }();
    return on && six_live();
}
extern "C" void jsp_six_unittab_enter(ppu_context* ctx) { t_six_unit = (int32_t)ctx->gpr[4]; }
extern "C" void jsp_six_unittab_leave(ppu_context* ctx)
{
    const int u = t_six_unit; t_six_unit = -1;
    if (u < 4 || u > 5 || !six_unit_table_on() || !vm_base) return;
    static bool ready = false; static uint32_t world = 0; static std::mutex mu;
    const uint32_t w = vm_read32(vm_read32(0x008D3888u - 0x7A90u));
    {
        std::lock_guard<std::mutex> lk(mu);
        if (!ready) {
            ready = VirtualAlloc(vm_base + kSixUnitTable, 2 * kSixUnitStride, MEM_COMMIT, PAGE_READWRITE) != nullptr;
            std::printf("[six] own move tables for units 4/5 at %08X: %s\n", kSixUnitTable, ready ? "ok" : "commit FAILED"); std::fflush(stdout);
            if (!ready) return;
        }
        if (w != world) { world = w; std::memset(vm_base + kSixUnitTable, 0, 2 * kSixUnitStride);
            std::printf("[six] world %08X: units 4/5 move tables cleared\n", w); std::fflush(stdout); }
    }
    ctx->gpr[3] = kSixUnitTable + (uint32_t)(u - 4) * kSixUnitStride;
}
/* SIX_PADS=1 (test, fighters 5/6 stand still): each fighter's input block (func_000BED94(block): +0x0C unit index,
 * +0x10 driver 1 controller / 2 world / 3 AI / 4 other) whenever a block's unit or driver changes, and the unit
 * indexes the AI driver func_000BDD48(out, unit) is asked for, with counts every 5 s. */
extern "C" void jsp_six_padblk_enter(ppu_context* ctx)
{
    static const bool on = std::getenv("SIX_PADS") != nullptr;
    if (!on) return;
    const uint32_t b = (uint32_t)ctx->gpr[3];
    if (b < 0x10000000u || b >= 0x20000000u) return;
    static uint32_t blk[16], key[16]; static unsigned cnt[16]; static int n = 0; static unsigned long long last = 0;
    const uint32_t k = (vm_read32(b + 0x0C) << 8) | (vm_read32(b + 0x10) & 0xFF);
    int i = 0; while (i < n && blk[i] != b) i++;
    if (i == n && n < 16) { blk[n] = b; key[n] = ~k; cnt[n] = 0; n++; }
    if (i < 16) { cnt[i]++;
        if (key[i] != k) { key[i] = k; std::printf("[sixp] block %08X unit %d driver %u lr %08X\n", b, (int32_t)vm_read32(b + 0x0C), vm_read32(b + 0x10), (uint32_t)ctx->lr); std::fflush(stdout); } }
    const unsigned long long now = GetTickCount64();
    { static unsigned long long ls = 0; static int lines = 0;   /* each fighter's state number (func_0001CAB4(AI unit)) every second */
      if (g_six_ai[0] && now - ls >= 500 && lines < 120) { ls = now; lines++;
          std::printf("[sixs] states:");
          for (int u = 0; u < 6; u++) { const uint32_t ai = g_six_ai[u]; if (!ai) { std::printf(" u%d -", u); continue; }
              static ppu_context c; c = *ctx; c.lr = 0; c.gpr[3] = ai; func_0001CAB4(&c);
              std::printf(" u%d %d", u, (int32_t)c.gpr[3]); }
          std::printf("\n"); std::fflush(stdout); } }
    if (now - last >= 5000) { last = now; std::printf("[sixp] calls:"); for (int j = 0; j < n; j++) std::printf(" %08X=%u", blk[j], cnt[j]); std::printf("\n"); std::fflush(stdout); }
}
extern "C" void jsp_six_aipad_enter(ppu_context* ctx)
{
    static const bool on = std::getenv("SIX_PADS") != nullptr;
    if (!on) return;
    static unsigned c[8]; static unsigned long long last = 0;
    const uint32_t u = (uint32_t)ctx->gpr[4]; if (u < 8) c[u]++;
    const unsigned long long now = GetTickCount64();
    if (now - last >= 5000) { last = now; std::printf("[sixp] AI driver asked for units: %u %u %u %u %u %u\n", c[0], c[1], c[2], c[3], c[4], c[5]); std::fflush(stdout); }
}
/* SIX_LIST=1 (test, F1 of battle_end.md): the battle manager's +0x0C object keeps a 4-slot unit list at +0x18..+0x24
 * (0x28-byte heap block), filled by func_0003FFB8 with every unit whose role is 1 or 2; a 5th/6th would land at
 * +0x28/+0x2C, past the block. Logged on leave. */
/* SIX_LISTFIX=1 (3v3 fix, 2026-11-16): with 6 fighters the 5th and 6th land at +0x28/+0x2C, which is the next heap
 * block's descriptor (its heap word read 5); the game's free of that block at the battle teardown then broke the
 * general allocator, so no menu could be made after a 3v3 (black screen; a test run,
 * _2338-six-3v3-obj2watch). The two words are saved on entry and put back on return: the list keeps its first 4
 * fighters, as the game sized it. */
static thread_local uint32_t t_six_list_obj = 0, t_six_list_w28 = 0, t_six_list_w2c = 0;
extern "C" void jsp_six_list_enter(ppu_context* ctx)
{
    t_six_list_obj = (uint32_t)ctx->gpr[3];
    if (t_six_list_obj >= 0x10000000u && t_six_list_obj < 0x20000000u) { t_six_list_w28 = vm_read32(t_six_list_obj + 0x28); t_six_list_w2c = vm_read32(t_six_list_obj + 0x2C); }
}
extern "C" void jsp_six_list_leave(ppu_context* ctx)
{
    (void)ctx;
    static const bool on = std::getenv("SIX_LIST") != nullptr, fix_env = std::getenv("SIX_LISTFIX") != nullptr; const bool fix = fix_env && six_live();
    const uint32_t o = t_six_list_obj; t_six_list_obj = 0;
    if (o < 0x10000000u || o >= 0x20000000u) return;
    if (fix && (vm_read32(o + 0x28) != t_six_list_w28 || vm_read32(o + 0x2C) != t_six_list_w2c)) {
        std::printf("[six] 4-fighter unit list %08X: entries 5/6 (%d %d) went past it; the next block's words put back (%08X %08X)\n", o,
                    (int32_t)vm_read32(o + 0x28), (int32_t)vm_read32(o + 0x2C), t_six_list_w28, t_six_list_w2c); std::fflush(stdout);
        vm_write32(o + 0x28, t_six_list_w28); vm_write32(o + 0x2C, t_six_list_w2c);
    }
    if (!on) return;
    std::printf("[sixl] unit list %08X:", o);
    for (uint32_t q = 0x18; q <= 0x2C; q += 4) std::printf(" %d", (int32_t)vm_read32(o + q));
    std::printf("\n"); std::fflush(stdout);
}
/* SIX_FULL45=1 (3v3 test, 2026-11-16): func_00412A8C(loader, place) answers 1 for load places 4 and 5 ("assist":
 * the Support's own pack, character/battle_character_assist/NNN, chosen in func_0041386C), 0 for the fighter places.
 * 14 loader and builder functions ask it. Fighters 5/6 sit in places 4/5 in a 3v3 and got the assist pack, so their
 * normal moves never play to the end (frozen in a special / dash). With the switch every place is a fighter place. */
static thread_local int t_six_place = -1, t_six_big = -1;
extern "C" void jsp_six_assist_enter(ppu_context* ctx) { t_six_place = (int32_t)ctx->gpr[4]; }
/* SIX_BIGMEM (with SIX_FULL45 including 413EF8): a fighter place's first kind-0 block is 0x5A9000 bytes, the heap only
 * has room for 4 of them. When places 4/5 are made fighter places there, their block comes from guest memory the game
 * does not use (0x7E100000 + 0x600000 * (place - 4)), committed here; func_0043DD88 (heap alloc) leave hook. */
static const uint32_t kSixBigBase = 0x7E100000u, kSixBigStride = 0x600000u;
static thread_local uint32_t t_six_alloc_size = 0;

struct SixMovedHeap { uint32_t heap, base, end, ours; };
static SixMovedHeap g_six_moved[8]; static int g_six_nmoved = 0; static std::mutex g_six_moved_mu;   /* SIX_HEAPMOVE: heaps moved this battle */
static thread_local uint32_t t_six_alloc_heap = 0;
/* SIX_HEAPMOVE=1 (3v3, 2026-11-16): each load place has its own kind-0 heap whose base is fixed in advance with the
 * Support size for places 4/5 (0x34F020 apart, a test run), and the heap has no end check
 * (battle_end.md section 11), so a fighter pack at place 4 runs into place 5's. Before a forced fighter place's first
 * kind-0 block (0x5A9000) the heap's base (+0x0C) and end (+0x10) move to a block of our own (sys_memory_allocate,
 * 0x700000 per place), where its three kind-0 blocks (0x5A9000 + 0xF800 + 0xFC000) fit. */
extern "C" void jsp_six_heapalloc_enter(ppu_context* ctx)
{
    t_six_alloc_size = (uint32_t)ctx->gpr[4]; t_six_alloc_heap = (uint32_t)ctx->gpr[3];
    static const bool mv_env = std::getenv("SIX_HEAPMOVE") != nullptr; const bool mv = mv_env && six_live();
    const int big = t_six_big;
    if (!mv || (big != 4 && big != 5) || t_six_alloc_size != 0x5A9000u || !vm_base) return;
    t_six_big = -1;
    /* SIX_HEAPMOVE=1: the new area comes from the game's own allocator (func_00717C2C, 0x700000 bytes) and the heap's
     * old area is given back to it (func_00717C4C), so the game's own free at the end of the battle (func_0043D748
     * frees heap +0x0C) stays valid. SIX_HEAPMOVE=sys keeps the earlier sys_memory_allocate block (never freed). */
    const uint32_t h = t_six_alloc_heap, old = vm_read32(h + 0x0C);
    /* default (=1): sys_memory_allocate block (works: a test run); =alloc: the game's
     * allocator with the old area freed (breaks the fight: no HUD, nobody moves) */
    static const bool sys = [] { const char* e = std::getenv("SIX_HEAPMOVE"); return !(e && !std::strcmp(e, "alloc")); }();
    uint32_t nb = 0;
    if (!sys) {
        static ppu_context c; c = *ctx; c.lr = 0; c.gpr[3] = 0x700000u;
        func_00717C2C(&c); while (g_trampoline_fn) { auto f = g_trampoline_fn; g_trampoline_fn = 0; f(&c); }
        nb = (uint32_t)c.gpr[3];
        if (nb && old) { c = *ctx; c.lr = 0; c.gpr[3] = old; func_00717C4C(&c); while (g_trampoline_fn) { auto f = g_trampoline_fn; g_trampoline_fn = 0; f(&c); } }
    } else {
        static uint32_t base = 0; static bool tried = false;
        if (!tried) { tried = true;
            if (VirtualAlloc(vm_base + 0x7DFF0000u, 0x1000, MEM_COMMIT, PAGE_READWRITE)) {
                static ppu_context c; c = *ctx; c.gpr[3] = 0xE00000; c.gpr[4] = 0x400; c.gpr[5] = 0x7DFF0004u;
                const int64_t rc = sys_memory_allocate(&c); base = rc == 0 ? vm_read32(0x7DFF0004u) : 0u; } }
        if (base) nb = base + (uint32_t)(big - 4) * 0x700000u;
    }
    std::printf("[six] place %d kind-0 heap %08X: base %08X end %08X -> %08X (%s)\n", big, h, old, vm_read32(h + 0x10), nb, sys ? "sys_memory" : "game allocator, old area freed"); std::fflush(stdout);
    if (!nb) return;
    { std::lock_guard<std::mutex> lk(g_six_moved_mu);
      int i = 0; while (i < g_six_nmoved && g_six_moved[i].heap != h) i++;
      if (i == g_six_nmoved && i < 8) g_six_nmoved++;
      if (i < 8) g_six_moved[i] = { h, old, vm_read32(h + 0x10), nb }; }
    vm_write32(h + 0x0C, nb); vm_write32(h + 0x10, nb + 0x700000u);
}
/* The battle's heaps are torn down by func_0043D748, which frees heap +0x0C with the game's free (func_00717C4C); our
 * moved base is not the game's memory, so freeing it broke the next screen (black Character Select, "bctr to NULL"
 * from func_003C320C: the maintainer's hand test 2026-11-16 18:54). Before any teardown path (func_0043DDFC, func_0043DF54,
 * func_0043E018, func_0043E644 all call func_0043D748) the moved heaps get their own base and end back. */
static void six_mgr_restore(const char* who);
static volatile bool g_six_teardown_seen = false;
static void six_heap_restore(const char* who)
{
    g_six_teardown_seen = true;
    six_mgr_restore(who);
    if (!std::strcmp(who, "character select") && g_six_in_battle) { g_six_in_battle = false; std::printf("[six] Character Select opened: 3v3 battle over\n"); std::fflush(stdout);
        /* the Support slots held fighters for this battle: empty them, so the next Character Select is a normal one */
        if (std::getenv("FORCE_SLOTS")) for (int sl = 4; sl <= 5; sl++) { vm_write32(setup_field(sl, 0x04), 1); vm_write32(setup_field(sl, 0x08), 2); vm_write32(setup_field(sl, 0x0C), 0); } }
    /* units 4/5 were turned from Supports (role 3) into partners (role 2) for the battle; the battle world outlives the
     * battle, and Character Select then broke on "Re-select characters" (black screen, "bctr to NULL" from
     * func_003C320C; with SIX_HOLD_ROLES but not without, a test run). Put them back to Supports. */
    { static const bool hold_env = std::getenv("SIX_HOLD_ROLES") != nullptr; const bool hold = hold_env && six_live();
      const uint32_t w0 = vm_read32(0x008D3888u - 0x7A90u), world = (w0 >= 0x00010000u && w0 < 0x30000000u) ? vm_read32(w0) : 0u;
      if (hold && world >= 0x10000000u && world < 0x20000000u)
          for (uint32_t k = 4; k < 6; k++) { const uint32_t u = world + 0x60u + 0x94u * k;
              if (vm_read32(u + 8) == 2u) { vm_write32(u + 8, 3u); vm_write32(u + 0x1C, 3u);
                  std::printf("[six] unit %u back to Support before %s\n", k, who); std::fflush(stdout); } } }
    std::lock_guard<std::mutex> lk(g_six_moved_mu);
    for (int i = 0; i < g_six_nmoved; i++) {
        SixMovedHeap& m = g_six_moved[i];
        if (!m.heap || vm_read32(m.heap + 0x0C) != m.ours) continue;
        vm_write32(m.heap + 0x0C, m.base); vm_write32(m.heap + 0x10, m.end);
        std::printf("[six] heap %08X base back to %08X before %s\n", m.heap, m.base, who); std::fflush(stdout);
        m.heap = 0;
        /* the end of a battle that moved heaps (func_0043E644 also runs at the start of a load, before any move). The 3v3
         * overrides stay on: "Fight again" loads the same 3v3 without the battle loader (a test run
         * twomatches: the rematch came back as a 2v2 with Supports). They go off when Character Select opens. */
        if (g_six_in_battle) { std::printf("[six] battle teardown: heaps back, 3v3 overrides stay on until Character Select\n"); std::fflush(stdout); }
    }
}
static const bool s_six_end_battle_set = (g_six_end_battle = six_heap_restore, true);
/* SIX_NEWTEST=1: new(0x1000)/free on the game's allocator around each teardown entry (does the arena still work?) */
static void six_newtest(ppu_context* ctx, const char* tag)
{
    static const bool nt = std::getenv("SIX_NEWTEST") != nullptr; static int n = 0;
    if (!nt || n++ > 60) return;
    static ppu_context c; c = *ctx; c.lr = 0; c.gpr[3] = 0x1000u;
    func_00717C2C(&c); while (g_trampoline_fn) { auto f = g_trampoline_fn; g_trampoline_fn = 0; f(&c); }
    const uint32_t b = (uint32_t)c.gpr[3];
    const uint32_t hh = (uint32_t)ctx->gpr[3], b0 = (hh >= 0x10000u && hh < 0x30000000u) ? vm_read32(hh + 0xC) : 0u;
    const uint32_t dd = (b0 >= 0x10004u && b0 < 0x30000000u) ? vm_read32(b0 - 4) : 0u;
    std::printf("[sixnew] %s: new(0x1000) -> %08X r3 in %08X (+C %08X +10 %08X, desc %08X: %08X %08X %08X) lr %08X\n", tag, b, hh, b0, (hh >= 0x10000u && hh < 0x30000000u) ? vm_read32(hh + 0x10) : 0u,
                dd, (dd >= 0x10000u && dd < 0x30000000u) ? vm_read32(dd) : 0u, (dd >= 0x10000u && dd < 0x30000000u) ? vm_read32(dd + 4) : 0u, (dd >= 0x10000u && dd < 0x30000000u) ? vm_read32(dd + 8) : 0u, (uint32_t)ctx->lr); std::fflush(stdout);
    if (b) { c = *ctx; c.lr = 0; c.gpr[3] = b; func_00717C4C(&c); while (g_trampoline_fn) { auto f = g_trampoline_fn; g_trampoline_fn = 0; f(&c); } }
}
extern "C" void jsp_six_teardown1_enter(ppu_context* ctx) { six_newtest(ctx, "before func_0043DDFC"); six_heap_restore("func_0043DDFC"); six_newtest(ctx, "after restore"); }
extern "C" void jsp_six_teardown2_enter(ppu_context* ctx) { six_newtest(ctx, "before func_0043DF54"); six_heap_restore("func_0043DF54"); six_newtest(ctx, "after restore"); }
extern "C" void jsp_six_teardown3_enter(ppu_context* ctx) { six_newtest(ctx, "before func_0043E018"); six_heap_restore("func_0043E018"); six_newtest(ctx, "after restore"); }
extern "C" void jsp_six_teardown4_enter(ppu_context* ctx) { six_newtest(ctx, "before func_0043E644"); six_heap_restore("func_0043E644"); six_newtest(ctx, "after restore"); }
extern "C" void jsp_six_heapalloc_leave(ppu_context* ctx)
{
    const int big = t_six_big; const uint32_t sz = t_six_alloc_size; t_six_alloc_size = 0;
    { static const bool hl = std::getenv("SIX_HEAPLOG") != nullptr; static int n = 0;   /* G3: every heap block of 32 KB or more */
      if (hl && sz >= 0x8000u && n++ < 300) { { const uint32_t dd = (uint32_t)ctx->gpr[3]; const bool ok = dd >= 0x10000u && dd < 0x30000000u;
          std::printf("[sixh] heap %08X size %08X -> %08X (addr %08X) lr %08X place %d t=%llu\n", t_six_alloc_heap, sz, dd, ok ? vm_read32(dd + 4) : 0u, (uint32_t)ctx->lr, t_six_place, (unsigned long long)GetTickCount64()); } std::fflush(stdout); } }
    static const bool bigmem = std::getenv("SIX_BIGMEM") != nullptr;   /* off by default: func_0043DD88 returns a block descriptor, not the memory (a test run) */
    if (!bigmem || big < 4 || big > 5 || sz != 0x5A9000u || !vm_base) return;
    t_six_big = -1;
    /* the block comes from the toolkit's sys_memory_allocate (the game uses 185 of its 213 MB), so it sits with the
     * game's own memory (0x10000000-0x20000000) that every unit of the port accepts; once per run, kept */
    static bool ready = false; static uint32_t base = 0;
    if (!ready) {
        if (VirtualAlloc(vm_base + 0x7DFF0000u, 0x1000, MEM_COMMIT, PAGE_READWRITE)) {
            static ppu_context c; c = *ctx; c.gpr[3] = 2 * kSixBigStride; c.gpr[4] = 0x400; c.gpr[5] = 0x7DFF0000u;
            const int64_t rc = sys_memory_allocate(&c);
            base = rc == 0 ? vm_read32(0x7DFF0000u) : 0u;
            std::printf("[six] sys_memory_allocate(0x%X) -> rc %lld, block %08X\n", 2 * kSixBigStride, (long long)rc, base); std::fflush(stdout);
        }
        ready = true;
    }
    if (!base) return;
    const uint32_t mine = base + (uint32_t)(big - 4) * kSixBigStride;
    std::printf("[six] place %d fighter block 0x%X: heap gave %08X, using %08X%s\n", big, sz, (uint32_t)ctx->gpr[3], mine, ready ? "" : " (commit FAILED)");
    std::fflush(stdout);
    if (ready) ctx->gpr[3] = mine;
}
extern "C" void jsp_six_assist_leave(ppu_context* ctx)
{
    /* SIX_FULL45=1: only the pack choice (func_000D9570, return 0x000D9600); SIX_FULL45=all: every asker (that hangs the
     * load: a test run); SIX_FULL45=<hex lr>,<hex lr>,..: those callers */
    static uint32_t lrs[16]; static int nl = -1; static bool all = false;
    if (nl < 0) { nl = 0; const char* e = std::getenv("SIX_FULL45");
        if (e && !std::strcmp(e, "all")) all = true;
        else if (e && !std::strcmp(e, "1")) lrs[nl++] = 0x000D9600u;
        else if (e) { const char* q = e; while (*q && nl < 16) { char* end; const unsigned long v = std::strtoul(q, &end, 16); if (end == q) break; lrs[nl++] = (uint32_t)v; q = (*end == ',') ? end + 1 : end; } } }
    if ((uint32_t)ctx->gpr[3] == 0u || !six_live()) return;
    { static const bool gate = std::getenv("SIX_NOGATE") == nullptr; if (gate && !g_six_in_battle) return; }   /* menus keep the game's answer */
    bool hit = all; for (int i = 0; i < nl && !hit; i++) hit = lrs[i] == (uint32_t)ctx->lr;
    { static const bool fl = std::getenv("SIX_FILES") != nullptr; static int n = 0;   /* M1: every forced answer with its place */
      if (fl && n++ < 80) { std::printf("[sixa8c] place %d lr %08X forced (hit %d) t=%llu\n", t_six_place, (uint32_t)ctx->lr, (int)hit, (unsigned long long)GetTickCount64()); std::fflush(stdout); } }
    if (!hit) return;
    if ((uint32_t)ctx->lr == 0x00413EF8u && (t_six_place == 4 || t_six_place == 5)) t_six_big = t_six_place;   /* the next 0x5A9000 block */
    static int said = 0;
    if (said++ < 4) { std::printf("[six] place asked as assist -> fighter pack (lr %08X)\n", (uint32_t)ctx->lr); std::fflush(stdout); }
    ctx->gpr[3] = 0;
}
/* SIX_SEQ=1 (test, G4 of battle_end.md): the scene sequencer func_0044522C(mode): whenever a mode's list
 * (+0x128 count, +0x12C index, +0x130 current scene, +0x134 wait id, +0x28.. 8-byte entries) changes, print it with LR */
/* Scene 6 of the battle leave list (vtable 0x00885A30, battle_end.md section 15) waits until the fader F = *(G2+0x58)
 * (G2 = *(0x00953584)) reaches level 1.0 at F+4; the fader only steps while G2 flag bit 1 (pause, in **(G2+0)) is clear.
 * SIX_SEQ2 logs F and the bit; SIX_FADEFIX=1 clears a pause bit left set once the fade has waited ~3 s (test). */
static void six_fade_watch(uint32_t sc, uint32_t tick)
{
    static const bool logon = std::getenv("SIX_SEQ2") != nullptr, fix = std::getenv("SIX_FADEFIX") != nullptr;
    if (!logon && !fix) return;
    const uint32_t g2 = vm_read32(0x00953584u);
    if (g2 < 0x10000u || g2 >= 0x30000000u) return;
    const uint32_t a1 = vm_read32(g2), words = (a1 >= 0x10000u && a1 < 0x30000000u) ? vm_read32(a1) : 0u;
    if (words < 0x10000u || words >= 0x30000000u) return;
    const uint32_t w0 = vm_read32(words), f = vm_read32(g2 + 0x58);
    static int lines = 0;
    if (logon && (tick % 60u == 1u) && lines < 30) {
        lines++;
        float fl[6] = {}; if (f >= 0x10000u && f < 0x30000000u) for (int j = 0; j < 6; j++) { const uint32_t v = vm_read32(f + j * 4); std::memcpy(&fl[j], &v, 4); }
        const uint32_t w = vm_read32(0x0095BC1Cu);
        std::printf("[sixfade] tick %u scene %08X +3C %08X | G2 %08X bits0 %08X (pause %u) | F %08X +0 %08X +4 %.3f +8 %.3f +C %.3f +10 %.3f +14 %.3f | W %08X +2C %d\n",
                    tick, sc, vm_read32(sc + 0x3C), g2, w0, (w0 >> 1) & 1u, f, f ? vm_read32(f) : 0u, fl[1], fl[2], fl[3], fl[4], fl[5], w,
                    (w >= 0x10000u && w < 0x30000000u) ? (int32_t)vm_read32(w + 0x2C) : -1);
        std::fflush(stdout);
    }
    if (fix && tick > 180u && ((w0 >> 1) & 1u)) {
        vm_write32(words, w0 & ~2u);
        static int said = 0; if (said++ < 4) { std::printf("[sixfade] pause bit was still set after the battle; cleared it (tick %u)\n", tick); std::fflush(stdout); }
    }
}
/* SIX_SEQ=1: func_00444EC8(mode, id, arg) adds an entry to a sequencer list; print it with 4 callers */
extern "C" void jsp_six_seqadd_enter(ppu_context* ctx)
{
    static const bool on = std::getenv("SIX_SEQ") != nullptr; static int n = 0;
    if (!on || n++ > 400) return;
    const uint32_t sp = (uint32_t)ctx->gpr[1];
    const uint32_t s1 = (uint32_t)vm_read64(sp), l1 = s1 ? (uint32_t)vm_read64(s1 + 0x10) : 0u;
    const uint32_t s2 = s1 ? (uint32_t)vm_read64(s1) : 0u, l2 = s2 ? (uint32_t)vm_read64(s2 + 0x10) : 0u;
    const uint32_t s3 = s2 ? (uint32_t)vm_read64(s2) : 0u, l3 = s3 ? (uint32_t)vm_read64(s3 + 0x10) : 0u;
    std::printf("[sixqa] mode %08X add %d/%d lr %08X <- %08X <- %08X <- %08X \n", (uint32_t)ctx->gpr[3], (int32_t)ctx->gpr[4], (int32_t)ctx->gpr[5],
                (uint32_t)ctx->lr, l1, l2, l3);
    { const uint32_t lr = (uint32_t)ctx->lr, o = (uint32_t)ctx->gpr[31];   /* the leave-list builder func_001531F4 reads its mode at r31+0x54 */
      if (lr >= 0x001531F4u && lr < 0x00154000u && o >= 0x10000u && o < 0x30000000u) {
          std::printf("[sixqa]  builder obj r31 %08X vt %08X +40..+5C:", o, vm_read32(o)); for (int j = 0; j < 8; j++) std::printf(" %08X", vm_read32(o + 0x40 + j * 4)); std::printf("\n"); } }
    std::fflush(stdout);
}
/* SIX_SEQ=1: func_00156A4C(new 0x148 block, flow obj, mode) builds the next game-flow mode after a battle
 * (func_00154220); in 3v3 the block was 0 (a test run). Print it and the allocator's current
 * heap (func_005EF73C: *(*(0x008DDAA0) + 4)) with its first words, and the moved place heaps */
extern "C" void jsp_six_newmode_enter(ppu_context* ctx)
{
    static const bool on = std::getenv("SIX_SEQ") != nullptr; static int n = 0;
    if (!on || n++ > 20) return;
    const uint32_t x = vm_read32(0x008DDAA0u), hp = (x >= 0x10000u && x < 0x30000000u) ? vm_read32(x + 4) : 0u;
    std::printf("[sixnm] new mode block %08X flow %08X r5 %08X lr %08X | alloc ctl %08X heap %08X:", (uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[4], (uint32_t)ctx->gpr[5], (uint32_t)ctx->lr, x, hp);
    if (hp >= 0x10000u && hp < 0x30000000u) for (int j = 0; j < 12; j++) std::printf(" %08X", vm_read32(hp + j * 4));
    { std::lock_guard<std::mutex> lk(g_six_moved_mu); for (int i = 0; i < g_six_nmoved; i++) std::printf(" | moved %08X", g_six_moved[i].heap); }
    std::printf("\n");
    static const bool all = std::getenv("SIX_NMDUMP") != nullptr;
    if (((uint32_t)ctx->gpr[3] == 0u || all) && hp >= 0x10000u && hp < 0x30000000u) {   /* the new() failed: is the arena full or broken? */
        std::printf("[sixnm]  heap +70..+8C:"); for (int j = 0; j < 8; j++) std::printf(" %08X", vm_read32(hp + 0x70 + j * 4)); std::printf("\n");
        uint32_t node = vm_read32(hp + 0x80);
        for (int j = 0; j < 12 && node >= 0x10000u && node < 0x30000000u; j++) {
            std::printf("[sixnm]  free node %08X: %08X %08X %08X %08X %08X %08X\n", node, vm_read32(node), vm_read32(node + 4), vm_read32(node + 8), vm_read32(node + 12), vm_read32(node + 16), vm_read32(node + 20));
            const uint32_t nx = vm_read32(node + 8); if (nx == node || nx == vm_read32(hp + 0x80)) break; node = nx; }
        static const uint32_t sizes[4] = { 0x10u, 0x148u, 0x1000u, 0x40000u };
        for (int j = 0; j < 4 && (uint32_t)ctx->gpr[3] == 0u; j++) { static ppu_context c; c = *ctx; c.lr = 0; c.gpr[3] = sizes[j];
            func_00717C2C(&c); while (g_trampoline_fn) { auto f = g_trampoline_fn; g_trampoline_fn = 0; f(&c); }
            std::printf("[sixnm]  test new(0x%X) -> %08X\n", sizes[j], (uint32_t)c.gpr[3]); }
    }
    std::fflush(stdout);
}
/* func_0007823C(mgr): a battle manager (battle object +0x58) with a list of per-fighter records (+4 pointer array,
 * +8 count = 4) fills record[k] for the k-th fighter it finds; fighters 5 and 6 go past the list into the allocator
 * heap header at 0x0096CCE8 (hardware watch, a test run). SIX_MGRLOG=1 dumps the manager. */
/* SIX_MGR6=1 (3v3 fix): before func_0007823C fills the list, give the manager a 6-entry list: its 4 records plus 2
 * more (copies of record 3's 0x34 bytes with their own 3-entry opponent storage, rec+0x18 / +0x1C = 0x80000003 like the
 * game's). Everything comes from the game's own new (func_00717C2C), because the manager's teardown frees the list,
 * each record and each record's storage with the game's free (a test run: our first version
 * in our own page broke the allocator that way). The old 4-entry list is freed with the game's free. */
static void six_mgr_restore(const char* who) { (void)who; }
static uint32_t six_game_new(ppu_context* ctx, uint32_t size)
{
    ppu_context c = *ctx; c.lr = 0; c.gpr[3] = size;
    func_00717C2C(&c); while (g_trampoline_fn) { auto f = g_trampoline_fn; g_trampoline_fn = 0; f(&c); }
    return (uint32_t)c.gpr[3];
}
static void six_game_free(ppu_context* ctx, uint32_t p)
{
    ppu_context c = *ctx; c.lr = 0; c.gpr[3] = p;
    func_00717C4C(&c); while (g_trampoline_fn) { auto f = g_trampoline_fn; g_trampoline_fn = 0; f(&c); }
}
static void six_mgr_grow(ppu_context* ctx, uint32_t m)
{
    static const bool on_env = std::getenv("SIX_MGR6") != nullptr; const bool on = on_env && six_live();
    if (!on || !vm_base) return;
    { /* only when the battle world really has more than 4 fighters (role 1 or 2): "Fight again" sets a 3v3 up again
       * without the battle loader, so g_six_in_battle is no guide here (a test run) */
      const uint32_t w0 = vm_read32(0x008D3888u - 0x7A90u), world = (w0 >= 0x00010000u && w0 < 0x30000000u) ? vm_read32(w0) : 0u;
      if (world < 0x10000000u || world >= 0x20000000u) return;
      int f = 0; for (uint32_t k = 0; k < 6; k++) { const uint32_t r = vm_read32(world + 0x60u + 0x94u * k + 8u); if (r == 1u || r == 2u) f++; }
      if (f <= 4) return; }
    const uint32_t arr = vm_read32(m + 4), cnt = vm_read32(m + 8);
    if ((cnt & 0x7FFFFFFFu) != 4u || arr < 0x10000u || arr >= 0x30000000u) return;
    const uint32_t r3 = vm_read32(arr + 12);
    if (r3 < 0x10000u || r3 >= 0x30000000u) return;
    const uint32_t na = six_game_new(ctx, 0x20u);
    uint32_t rec[2] = { six_game_new(ctx, 0x40u), six_game_new(ctx, 0x40u) }, sub[2] = { six_game_new(ctx, 0x10u), six_game_new(ctx, 0x10u) };
    if (!na || !rec[0] || !rec[1] || !sub[0] || !sub[1]) { std::printf("[six] fighter list: the game's new failed, list left at 4\n"); std::fflush(stdout); return; }
    for (uint32_t k = 0; k < 4; k++) vm_write32(na + k * 4, vm_read32(arr + k * 4));
    for (uint32_t k = 0; k < 2; k++) {
        for (uint32_t j = 0; j < 0x34u; j += 4) vm_write32(rec[k] + j, vm_read32(r3 + j));
        for (uint32_t j = 0; j < 0x10u; j += 4) vm_write32(sub[k] + j, 0u);
        vm_write32(rec[k] + 0x18, sub[k]); vm_write32(rec[k] + 0x1C, 0x80000003u);
        vm_write32(na + (4 + k) * 4, rec[k]);
    }
    vm_write32(m + 4, na); vm_write32(m + 8, (cnt & 0x80000000u) | 6u);
    six_game_free(ctx, arr);
    std::printf("[six] fighter list of manager %08X: 4 -> 6 entries (list %08X, records %08X %08X)\n", m, na, rec[0], rec[1]); std::fflush(stdout);
}
extern "C" void jsp_six_mgr_enter(ppu_context* ctx)
{
    const uint32_t m = (uint32_t)ctx->gpr[3];
    if (m < 0x10000u || m >= 0x30000000u) return;
    six_mgr_grow(ctx, m);
    static const bool lg = std::getenv("SIX_MGRLOG") != nullptr; static int n = 0;
    if (!lg || n++ > 6) return;
    const uint32_t arr = vm_read32(m + 4), cnt = vm_read32(m + 8);
    std::printf("[sixmgr] mgr %08X lr %08X +0..+3C:", m, (uint32_t)ctx->lr); for (int j = 0; j < 16; j++) std::printf(" %08X", vm_read32(m + j * 4)); std::printf("\n");
    std::printf("[sixmgr]  array %08X count %u, words past it:", arr, cnt);
    if (arr >= 0x10000u && arr < 0x30000000u) for (uint32_t j = 0; j < 8; j++) std::printf(" %08X", vm_read32(arr + j * 4));
    std::printf("\n");
    if (arr >= 0x10000u && arr < 0x30000000u) for (uint32_t k = 0; k < cnt && k < 8; k++) { const uint32_t r = vm_read32(arr + k * 4);
        if (r < 0x10000u || r >= 0x30000000u) continue;
        std::printf("[sixmgr]  rec %u %08X:", k, r); for (int j = 0; j < 16; j++) std::printf(" %08X", vm_read32(r + j * 4));
        const uint32_t sa = vm_read32(r + 0x18);
        if (sa >= 0x10000u && sa < 0x30000000u) { std::printf(" | sub:"); for (int j = 0; j < 6; j++) std::printf(" %08X", vm_read32(sa + j * 4)); }
        std::printf("\n"); }
    std::fflush(stdout);
}
/* SIX_FREELOG=1: func_005ED6C8(heap, p) runs inside the game's free (func_00717C4C -> 005EF034 -> 005EEFD8). Print
 * frees of blocks outside the general arena 0x19E00000-0x1C800000 that name the arena's heap 0x0096CCE8, and any
 * free of our own memory (0x7E000000 up, 0x10100000-0x10F00000), with 4 callers */
extern "C" void jsp_six_free_enter(ppu_context* ctx)
{
    static const bool on = std::getenv("SIX_FREELOG") != nullptr; static int n = 0;
    if (!on || n > 260) return;
    const uint32_t h = (uint32_t)ctx->gpr[3], p = (uint32_t)ctx->gpr[4];
    const bool ours = (p >= 0x7E000000u && p < 0x80000000u) || (p >= 0x10100000u && p < 0x10F00000u);
    const bool odd = h == 0x0096CCE8u && (p < 0x19E00000u || p >= 0x1C800000u);
    static const bool all = [] { const char* e = std::getenv("SIX_FREELOG"); return e && e[0] == '2'; }();
    static int nall = 0;
    const bool late = all && g_six_teardown_seen && g_six_leave_seen && nall < 150;
    if (late) nall++;
    { /* SIX_FREELOG=d: after the leave fade, report a block freed twice (or a free whose descriptor is outside the
       * general arena while naming its heap) */
      static const bool dbl = [] { const char* e = std::getenv("SIX_FREELOG"); return e && e[0] == 'd'; }();
      static std::unordered_set<uint32_t> freed; static int nd = 0;
      if (dbl && g_six_leave_seen && nd < 40) {
          const uint32_t d = vm_read32(p - 4);
          const bool twice = !freed.insert(p).second;
          const bool baddesc = h == 0x0096CCE8u && (d < 0x19E00000u || d >= 0x1C800000u);
          if (twice || baddesc) { nd++;
              const uint32_t sp = (uint32_t)ctx->gpr[1];
              const uint32_t s1 = (uint32_t)vm_read64(sp), l1 = s1 ? (uint32_t)vm_read64(s1 + 0x10) : 0u;
              const uint32_t s2 = s1 ? (uint32_t)vm_read64(s1) : 0u, l2 = s2 ? (uint32_t)vm_read64(s2 + 0x10) : 0u;
              const uint32_t s3 = s2 ? (uint32_t)vm_read64(s2) : 0u, l3 = s3 ? (uint32_t)vm_read64(s3 + 0x10) : 0u;
              const uint32_t s4 = s3 ? (uint32_t)vm_read64(s3) : 0u, l4 = s4 ? (uint32_t)vm_read64(s4 + 0x10) : 0u;
              const uint32_t s5 = s4 ? (uint32_t)vm_read64(s4) : 0u, l5 = s5 ? (uint32_t)vm_read64(s5 + 0x10) : 0u;
              std::printf("[sixfree] %s heap %08X block %08X desc %08X lr %08X <- %08X <- %08X <- %08X <- %08X <- %08X\n", twice ? "DOUBLE FREE" : "BAD DESCRIPTOR", h, p, d,
                          (uint32_t)ctx->lr, l1, l2, l3, l4, l5); std::fflush(stdout); }
      } }
    { static const bool ee = std::getenv("SIX_FREE48") != nullptr; static int ne = 0;
      if (ee && g_six_in_ee48 && ne < 400) { ne++;
          const uint32_t d = vm_read32(p - 4);
          const uint32_t sp = (uint32_t)ctx->gpr[1];
          const uint32_t s1 = (uint32_t)vm_read64(sp), l1 = s1 ? (uint32_t)vm_read64(s1 + 0x10) : 0u;
          const uint32_t s2 = s1 ? (uint32_t)vm_read64(s1) : 0u, l2 = s2 ? (uint32_t)vm_read64(s2 + 0x10) : 0u;
          const uint32_t s3 = s2 ? (uint32_t)vm_read64(s2) : 0u, l3 = s3 ? (uint32_t)vm_read64(s3 + 0x10) : 0u;
          std::printf("[sixf48] %d heap %08X block %08X (p-desc %X) desc %08X %08X %08X %08X lr %08X <- %08X <- %08X\n", ne, h, p, p - d, vm_read32(d), vm_read32(d + 4), vm_read32(d + 8), vm_read32(d + 12), l1, l2, l3);
          std::fflush(stdout); } }
    if (!ours && !odd && !late) return;
    n++;
    const uint32_t sp = (uint32_t)ctx->gpr[1];
    const uint32_t s1 = (uint32_t)vm_read64(sp), l1 = s1 ? (uint32_t)vm_read64(s1 + 0x10) : 0u;
    const uint32_t s2 = s1 ? (uint32_t)vm_read64(s1) : 0u, l2 = s2 ? (uint32_t)vm_read64(s2 + 0x10) : 0u;
    const uint32_t s3 = s2 ? (uint32_t)vm_read64(s2) : 0u, l3 = s3 ? (uint32_t)vm_read64(s3 + 0x10) : 0u;
    const uint32_t s4 = s3 ? (uint32_t)vm_read64(s3) : 0u, l4 = s4 ? (uint32_t)vm_read64(s4 + 0x10) : 0u;
    { const uint32_t d = vm_read32(p - 4); std::printf("[sixfree]  desc %08X: %08X %08X %08X %08X%s", d, vm_read32(d), vm_read32(d + 4), vm_read32(d + 8), vm_read32(d + 12), "   "); }
    std::printf("[sixfree] heap %08X frees %08X%s%s lr %08X <- %08X <- %08X <- %08X <- %08X leave %u\n", h, p, ours ? " (OUR memory)" : "", odd ? " (outside the arena)" : "",
                (uint32_t)ctx->lr, l1, l2, l3, l4, (unsigned)g_six_leave_seen);
    std::fflush(stdout);
}
/* SIX_WORLDDIFF=1: func_0002EE48 (battle scene dtor, after the managers) breaks the general allocator in a 3v3 (run
 * 2026-11-16_2257-six-3v3-dtor). Snapshot the 0x24000 bytes after the battle world's 4-unit move tables (world +
 * 0x7B8 + 4 * 0x11DF0, where a 5th/6th unit's table would go) at its entry and print what changed there on return. */
static std::vector<uint8_t> g_six_wsnap; static uint32_t g_six_wsnap_at = 0;
extern "C" void jsp_six_ee48_enter(ppu_context* ctx)
{
    (void)ctx; static const bool on = std::getenv("SIX_WORLDDIFF") != nullptr;
    g_six_in_ee48 = true;
    if (!on || !vm_base) return;
    const uint32_t w0 = vm_read32(0x008D3888u - 0x7A90u), world = (w0 >= 0x00010000u && w0 < 0x30000000u) ? vm_read32(w0) : 0u;
    if (world < 0x10000000u || world >= 0x20000000u) return;
    g_six_wsnap_at = world + 0x7B8u + 4u * 0x11DF0u;
    g_six_wsnap.assign(vm_base + g_six_wsnap_at, vm_base + g_six_wsnap_at + 0x24000u);
    std::printf("[sixwd] world %08X: snapshot of %08X..%08X at func_0002EE48 entry\n", world, g_six_wsnap_at, g_six_wsnap_at + 0x24000u); std::fflush(stdout);
}
extern "C" void jsp_six_ee48_leave(ppu_context* ctx)
{
    (void)ctx; g_six_in_ee48 = false;
    if (g_six_wsnap.empty() || !vm_base) return;
    int shown = 0, changed = 0;
    for (uint32_t o = 0; o + 4 <= 0x24000u; o += 4) {
        uint32_t a, b; std::memcpy(&a, &g_six_wsnap[o], 4); std::memcpy(&b, vm_base + g_six_wsnap_at + o, 4);
        if (a != b) { changed++; if (shown++ < 40) std::printf("[sixwd]  %08X: %08X -> %08X\n", g_six_wsnap_at + o, _byteswap_ulong(a), _byteswap_ulong(b)); }
    }
    std::printf("[sixwd] %d words changed in that range during func_0002EE48\n", changed); std::fflush(stdout);
    g_six_wsnap.clear();
}

extern "C" void jsp_six_loop30_enter(ppu_context* ctx)
{
    static const bool on = std::getenv("SIX_LOOP30") != nullptr;
    if (!on || !g_six_leave_seen) return;
    const uint32_t t = (uint32_t)ctx->gpr[3];
    if (t < 0x10000u || t >= 0x30000000u) return;
    for (uint32_t i = 0x11; i >= 1; i--) {
        const uint32_t slot = t + 4u + i * 4u, obj = vm_read32(slot);
        if (!obj) continue;
        const uint32_t vt = vm_read32(obj), d = vm_read32(vt + 4), fn = vm_read32(d), toc = vm_read32(d + 4);
        ppu_context c = *ctx; c.gpr[3] = obj; c.gpr[2] = toc; c.lr = 0; c.ctr = fn;
        g_six_in_ee48 = (i == 2u);   /* SIX_FREE48 logs the frees of object 2 (the one that breaks the arena in a 3v3) */
        ps3_indirect_call_to(&c, fn); while (g_trampoline_fn) { auto f = g_trampoline_fn; g_trampoline_fn = 0; f(&c); }
        g_six_in_ee48 = false;
        vm_write32(slot, 0u);
        ppu_context c2 = *ctx; c2.lr = 0; c2.gpr[3] = 0x40u;
        func_00717C2C(&c2); while (g_trampoline_fn) { auto f = g_trampoline_fn; g_trampoline_fn = 0; f(&c2); }
        const uint32_t b = (uint32_t)c2.gpr[3];
        if (b) { c2 = *ctx; c2.lr = 0; c2.gpr[3] = b; func_00717C4C(&c2); while (g_trampoline_fn) { auto f = g_trampoline_fn; g_trampoline_fn = 0; f(&c2); } }
        std::printf("[sixloop] object %u %08X vt %08X dtor %08X -> new(0x40) %s\n", i, obj, vt, fn, b ? "ok" : "FAILED"); std::fflush(stdout);
    }
}
/* SIX_CSEL=1 (3v3 Character Select, 2026-11-17; game map notes charsel.md): the Support slots 4/5 become 3rd-fighter slots.
 * func_00184E80(slot) is the game's only slot-kind test (slot <= 3 = fighter); answering 1 for 4/5 makes the pick check
 * (func_00188438) reject Support ids 200..213 there, random (func_001882DC) skip them, and the panel code take the
 * fighter path. The grid builder func_001857D0 puts the 14 Supports after the 39 fighters in the one 16x4 grid at
 * 0x009541F4 (row count 0x009541F0); with the switch they are taken out, so the cursor cannot reach them. */
static thread_local uint32_t t_six_kind_slot = 0;
extern "C" void jsp_six_kind_enter(ppu_context* ctx) { t_six_kind_slot = (uint32_t)ctx->gpr[3]; }
extern "C" void jsp_six_kind_leave(ppu_context* ctx)
{
    static const bool on_env = std::getenv("SIX_CSEL") != nullptr; const bool on = on_env && six_live();
    if (on && (t_six_kind_slot == 4u || t_six_kind_slot == 5u)) ctx->gpr[3] = 1;
}
extern "C" void jsp_six_grid_leave(ppu_context* ctx)
{
    (void)ctx; static const bool on_env = std::getenv("SIX_CSEL") != nullptr; const bool on = on_env && six_live();
    if (!on) return;
    const uint32_t base = 0x009541F4u, nrow_at = 0x009541F0u;
    const uint32_t rows = vm_read32(nrow_at);
    { static int dbg = 0; if (dbg++ < 3) { std::printf("[six] grid built: rows %u, first ids %d %d %d %d\n", rows, (int32_t)vm_read32(base), (int32_t)vm_read32(base + 4), (int32_t)vm_read32(base + 8), (int32_t)vm_read32(base + 12)); std::fflush(stdout); } }
    if (rows == 0u || rows > 16u) return;
    uint32_t keep[64]; uint32_t n = 0, dropped = 0;
    for (uint32_t i = 0; i < rows * 4u; i++) { const uint32_t id = vm_read32(base + i * 4u);
        if (id >= 200u && id <= 213u) { dropped++; continue; }
        if (id == 0xFFFFFFFFu) continue;
        keep[n++] = id; }
    const uint32_t nr = (n + 3u) / 4u;
    for (uint32_t i = 0; i < 64u; i++) vm_write32(base + i * 4u, i < n ? keep[i] : 0xFFFFFFFFu);
    vm_write32(nrow_at, nr);
    static int said = 0;
    if (said++ < 3) { std::printf("[six] Character Select grid: %u Supports taken out, %u rows -> %u (3rd-fighter slots)\n", dropped, rows, nr); std::fflush(stdout); }
}
static uint32_t g_six_leave_fade = 0;
/* func_00444088 (fade scene update, hooked in experiments): log its state (+0x3C), +0x2C, the fader level and the pause
 * bit every 30 calls (SIX_SEQ2); SIX_FADEFIX=1 clears a pause bit still set after ~3 s in state 3 */
extern "C" void jsp_six_fadeupd_enter(ppu_context* ctx)
{
    static const bool logon = std::getenv("SIX_SEQ2") != nullptr, fix = std::getenv("SIX_FADEFIX") != nullptr;
    if (!logon && !fix) return;
    const uint32_t o = (uint32_t)ctx->gpr[3];
    if (o < 0x10000u || o >= 0x30000000u || vm_read32(o) != 0x00885A30u) return;
    static uint32_t last = 0, calls = 0, in3 = 0; static int lines = 0;
    if (o != last) { last = o; calls = 0; in3 = 0; }
    calls++;
    const uint32_t g2 = vm_read32(0x00953584u);
    const uint32_t a1 = (g2 >= 0x10000u && g2 < 0x30000000u) ? vm_read32(g2) : 0u;
    const uint32_t words = (a1 >= 0x10000u && a1 < 0x30000000u) ? vm_read32(a1) : 0u;
    const uint32_t w0 = (words >= 0x10000u && words < 0x30000000u) ? vm_read32(words) : 0u;
    const uint32_t f = (g2 >= 0x10000u && g2 < 0x30000000u) ? vm_read32(g2 + 0x58) : 0u;
    float lv = -1.0f, tg = -1.0f; if (f >= 0x10000u && f < 0x30000000u) { uint32_t v = vm_read32(f + 4); std::memcpy(&lv, &v, 4); v = vm_read32(f + 8); std::memcpy(&tg, &v, 4); }
    const int32_t st = (int32_t)vm_read32(o + 0x3C);
    if (st == 3) in3++;
    { /* SIX_FADEROLES=1 (test): at the first fade step of a 3v3 battle, turn units 4/5 back into Supports (role 3) */
      static const bool fr = std::getenv("SIX_FADEROLES") != nullptr;
      static uint32_t done_obj = 0;
      const bool leave = o == g_six_leave_fade;   /* set by the sequencer hook: the fade of the leave list 26, 28, 6 */
      if (fr && leave && done_obj != o && g_six_in_battle) { done_obj = o;
          const uint32_t w0p = vm_read32(0x008D3888u - 0x7A90u), world = (w0p >= 0x00010000u && w0p < 0x30000000u) ? vm_read32(w0p) : 0u;
          if (world >= 0x10000000u && world < 0x20000000u)
              for (uint32_t k = 4; k < 6; k++) { const uint32_t u = world + 0x60u + 0x94u * k;
                  if (vm_read32(u + 8) == 2u) { vm_write32(u + 8, 3u); vm_write32(u + 0x1C, 3u);
                      std::printf("[sixfade2] unit %u back to Support at the fade \n", k); std::fflush(stdout); } } } }
    if (logon && (calls <= 6 || calls % 30u == 0u) && lines < 40) {
        lines++;
        std::printf("[sixfade2] call %u obj %08X state %d +2C %08X | pause %u (bits %08X) | F %08X level %.3f target %.3f lr %08X%s\n",
                    calls, o, st, vm_read32(o + 0x2C), (w0 >> 1) & 1u, w0, f, lv, tg, (uint32_t)ctx->lr, "");
        std::fflush(stdout);
    }
    if (fix && in3 > 90u && ((w0 >> 1) & 1u) && words) {
        vm_write32(words, w0 & ~2u);
        static int said = 0; if (said++ < 4) { std::printf("[sixfade2] pause bit still set in the leave fade; cleared it (call %u)\n", calls); std::fflush(stdout); }
    }
}
extern "C" void jsp_six_seq_enter(ppu_context* ctx)
{
    static const bool on = std::getenv("SIX_SEQ") != nullptr;
    if (!on) return;
    const uint32_t m = (uint32_t)ctx->gpr[3];
    if (m < 0x00800000u || m >= 0x20000000u) return;
    static uint32_t mo[16]; static uint32_t key[16]; static int n = 0, lines = 0;
    const uint32_t cnt = vm_read32(m + 0x128), idx = vm_read32(m + 0x12C);
    uint32_t k = cnt * 131u + idx * 7u + vm_read32(m + 0x130);
    for (uint32_t i = 0; i < cnt && i < 16; i++) k = k * 31u + vm_read32(m + 0x28 + i * 8) + vm_read32(m + 0x2C + i * 8) * 3u;
    int i = 0; while (i < n && mo[i] != m) i++;
    if (i == n) { if (n >= 16) return; mo[n] = m; key[n] = ~k; n++; }
    { /* SIX_W54=<hex address>: print every change of that word seen between sequencer calls (who cleared the builder's mode) */
      static const uint32_t wa = [] { const char* e = std::getenv("SIX_W54"); return e ? (uint32_t)std::strtoul(e, nullptr, 16) : 0u; }();
      static uint32_t wv = 0xFFFFFFFFu; static int wl = 0;
      if (wa >= 0x10000u && wa < 0x30000000u) { const uint32_t v = vm_read32(wa);
          if (v != wv && wl++ < 40) { std::printf("[sixw54] %08X: %08X -> %08X (seq mode %08X lr %08X) t=%llu ko %d \n", wa, wv, v, m, (uint32_t)ctx->lr, (unsigned long long)GetTickCount64(), (int)g_six_in_battle); std::fflush(stdout); }
          wv = v; } }
    { /* SIX_NEWTEST=1: every ~10 s of sequencer calls, try new(0x1000) on the game's allocator and free it again,
       * to see when the general arena runs out in a 3v3 */
      static const bool nt = std::getenv("SIX_NEWTEST") != nullptr; static unsigned long long last = 0; static int lines2 = 0;
      const unsigned long long now = GetTickCount64();
      if (nt && now - last > 10000 && lines2 < 60) { last = now; lines2++;
          static ppu_context c; c = *ctx; c.lr = 0; c.gpr[3] = 0x1000u;
          func_00717C2C(&c); while (g_trampoline_fn) { auto f = g_trampoline_fn; g_trampoline_fn = 0; f(&c); }
          const uint32_t b = (uint32_t)c.gpr[3];
          std::printf("[sixnew] t=%llu new(0x1000) -> %08X (in battle %d)\n", now, b, (int)g_six_in_battle); std::fflush(stdout);
          if (b) { c = *ctx; c.lr = 0; c.gpr[3] = b; func_00717C4C(&c); while (g_trampoline_fn) { auto f = g_trampoline_fn; g_trampoline_fn = 0; f(&c); } } } }
    { const uint32_t sc0 = vm_read32(m + 0x130);   /* the battle-leave list (26, 28, 6) is on its fade scene */
      if (cnt == 3u && vm_read32(m + 0x28) == 26u && sc0 >= 0x00800000u && sc0 < 0x20000000u && vm_read32(sc0) == 0x00885A30u) { g_six_leave_fade = sc0; g_six_leave_seen = 1; } }
    { /* SIX_SEQ2=1: a mode that stays on one scene for long: dump the scene (vtable, its first entries, +0..+0x4C) and
       * the system bit words (func_001483D0 reads *(*(*(*(TOC-0x3E28)))) + bit/32*4) every ~600 calls, 16 dumps */
      static const bool deep = std::getenv("SIX_SEQ2") != nullptr; static uint32_t tick[16]; static int dumps = 0;
      const uint32_t sc = vm_read32(m + 0x130);
      const bool fade = sc >= 0x00800000u && sc < 0x20000000u && vm_read32(sc) == 0x00885A30u;   /* scene 6: the fade to black */
      if (fade) six_fade_watch(sc, ++tick[i]);
      if (deep && key[i] == k && fade && tick[i] % 120u == 0u && dumps < 16) {
          dumps++;
          const uint32_t vt = vm_read32(sc);
          std::printf("[sixq2] mode %08X scene %08X vt %08X vt[0..7]:", m, sc, vt);
          if (vt >= 0x00010000u && vt < 0x20000000u) for (int j = 0; j < 8; j++) std::printf(" %08X", vm_read32(vt + j * 4));
          std::printf("\n[sixq2]  scene +0..+4C:");
          for (int j = 0; j < 20; j++) std::printf(" %08X", vm_read32(sc + j * 4));
          const uint32_t r2 = 0x008D3888u, p1 = vm_read32(r2 - 0x3E28u);
          const uint32_t sys = (p1 >= 0x10000u && p1 < 0x30000000u) ? vm_read32(p1) : 0u;
          const uint32_t a1 = (sys >= 0x10000u && sys < 0x30000000u) ? vm_read32(sys) : 0u;
          const uint32_t bits = (a1 >= 0x10000u && a1 < 0x30000000u) ? vm_read32(a1) : 0u;
          std::printf("\n[sixq2]  sys %08X words@%08X:", sys, bits);
          if (bits >= 0x10000u && bits < 0x30000000u) for (int j = 0; j < 8; j++) std::printf(" %08X", vm_read32(bits + j * 4));
          std::printf("\n"); std::fflush(stdout);
      } }
    if (key[i] == k || lines >= 200) return;
    key[i] = k; lines++;
    std::printf("[sixq] mode %08X count %u index %u scene %08X wait %d lr %08X list:", m, cnt, idx, vm_read32(m + 0x130), (int32_t)vm_read32(m + 0x134), (uint32_t)ctx->lr);
    for (uint32_t j = 0; j < cnt && j < 16; j++) std::printf(" %d/%d", (int32_t)vm_read32(m + 0x28 + j * 8), (int32_t)vm_read32(m + 0x2C + j * 8));
    std::printf("\n"); std::fflush(stdout);
}
extern "C" void jsp_six_subseq_enter(ppu_context* ctx) { if ((uint32_t)ctx->gpr[3] < 0x01000000u) six_bits_log("subseq-static", ctx); }   /* func_000E42D0 on a static object (SIX_BITS) */
/* SIX_STPK=1 (test, H1/H4 of battle_end.md): func_0051D124(element, data) entry from the STPK parser func_000D578C:
 * when the element's vtable is not 0x00887300, print the caller's node (r26 out-slot, *r26, **r26), the pass (r29),
 * r30 (i*0x54), the pack (r27); also the first 6 good calls of each node for comparison */
extern "C" void jsp_six_stpk_enter(ppu_context* ctx)
{
    static const bool on = std::getenv("SIX_STPK") != nullptr;
    if (!on || (uint32_t)ctx->lr != 0x000D592Cu) return;
    const uint32_t el = (uint32_t)ctx->gpr[3], vt = (el >= 0x10000u && el < 0x30000000u) ? vm_read32(el) : 0u;
    const uint32_t r26 = (uint32_t)ctx->gpr[26], n1 = (r26 >= 0x10000u && r26 < 0x30000000u) ? vm_read32(r26) : 0u;
    const uint32_t n2 = (n1 >= 0x10000u && n1 < 0x30000000u) ? vm_read32(n1) : 0u, n3 = (n1 >= 0x10000u && n1 < 0x30000000u) ? vm_read32(n1 + 4) : 0u;
    static int good = 0, bad = 0;
    const bool ok = vt == 0x00887300u;
    if ((ok && good++ < 12) || (!ok && bad++ < 6)) {
        std::printf("[sixk2] %s el %08X vt %08X data %08X | r26 %08X *r26 %08X node {%08X, %d} pass %d r30 %X pack %08X\n", ok ? "ok " : "BAD", el, vt, (uint32_t)ctx->gpr[4],
                    r26, n1, n2, (int32_t)n3, (int32_t)ctx->gpr[29], (uint32_t)ctx->gpr[30], (uint32_t)ctx->gpr[27]);
        std::fflush(stdout); }
}
/* SIX_FILES=1 (test): func_0052AB88(name) is the file check the pack loader runs on each built pack name; print the
 * name and the answer (to see which part of a fighter pack at place 4/5 is missing) */
static thread_local uint32_t t_six_fname = 0;
extern "C" void jsp_six_fcheck_enter(ppu_context* ctx) { t_six_fname = (uint32_t)ctx->gpr[3]; }
extern "C" void jsp_six_fcheck_leave(ppu_context* ctx)
{
    static const bool on = std::getenv("SIX_FILES") != nullptr;
    const uint32_t a = t_six_fname; t_six_fname = 0;
    if (!on || !a || !vm_base) return;
    static int n = 0; if (n++ > 400) return;
    char name[160]; int i = 0; for (; i < 159; i++) { const char c = (char)vm_base[a + i]; if (!c) break; name[i] = c; } name[i] = 0;
    std::printf("[sixf] %s -> %d (lr %08X)\n", name, (int32_t)ctx->gpr[3], (uint32_t)ctx->lr); std::fflush(stdout);
}
/* SIX_FILES: func_00424618(loader, slot, name) records a pack name for a region slot; print the character ones */
extern "C" void jsp_six_setname_enter(ppu_context* ctx)
{
    static const bool on = std::getenv("SIX_FILES") != nullptr;
    const uint32_t a = (uint32_t)ctx->gpr[5];
    if (!on || !vm_base || a < 0x10000u || a >= 0xF0000000u) return;
    static int n = 0; if (n++ > 600) return;
    char name[160]; int i = 0; for (; i < 159; i++) { const char c = (char)vm_base[a + i]; if (!c) break; name[i] = c; } name[i] = 0;
    if (!std::strstr(name, "character")) return;
    std::printf("[sixf] slot %d name %s (lr %08X)\n", (int32_t)ctx->gpr[4], name, (uint32_t)ctx->lr); std::fflush(stdout);
}
/* SIX_POOL45=1 (with SIX_FULL45): a fighter pack asked for place 4/5 must still take place 4/5's own pool (0x14/0x15,
 * the assist free list +0x100), not the next free fighter pool (+0xF4): otherwise Goku's pack lands in place 2's slots
 * and every later fighter is one place off (a test run). func_000D9570(loader, place, record)
 * sets the place; the pool pick func_00412BA0 and the loaded-pack search func_00413534 then get kind 1 (assist list),
 * while the pack name (func_0041386C's cr3) stays the fighter's. */
static thread_local int t_six_d9570_place = -1;
extern "C" void jsp_six_d9570_enter(ppu_context* ctx) { t_six_d9570_place = (int32_t)ctx->gpr[4]; }
extern "C" void jsp_six_d9570_leave(ppu_context* ctx) { (void)ctx; t_six_d9570_place = -1; }
static bool six_pool45() { static const bool on = std::getenv("SIX_POOL45") != nullptr; return on; }
extern "C" void jsp_six_poolpick_enter(ppu_context* ctx)
{
    if (!six_pool45() || (t_six_d9570_place != 4 && t_six_d9570_place != 5) || (uint32_t)ctx->gpr[5] != 0u) return;
    static int n = 0; if (n++ < 4) { std::printf("[six] place %d: pool from the place's own list (kind 1), fighter pack name\n", t_six_d9570_place); std::fflush(stdout); }
    ctx->gpr[5] = 1;
}
extern "C" void jsp_six_poolnew_enter(ppu_context* ctx)   /* func_00412A2C(loader, kind): takes a free pool of that kind */
{
    if (!six_pool45() || (t_six_d9570_place != 4 && t_six_d9570_place != 5) || (uint32_t)ctx->gpr[4] != 0u) return;
    ctx->gpr[4] = 1;
}
extern "C" void jsp_six_packfind_enter(ppu_context* ctx)
{
    if (!six_pool45() || (t_six_d9570_place != 4 && t_six_d9570_place != 5) || (uint32_t)ctx->gpr[5] != 0u) return;
    ctx->gpr[5] = 1;
}
/* SIX_PACKS=1 (test, K1 of battle_end.md): func_000D6A1C(loader, entry n, part k, 0) gives the pack pointer for a
 * resource entry (loader + 0x3F0 + n*0x30: +0x10 character, +0x14 place); a pack pointer below 0x01000000 means the
 * file's buffer base was 0 */
static thread_local uint32_t t_d6a_obj = 0, t_d6a_n = 0, t_d6a_k = 0;
extern "C" void jsp_six_d6a1c_enter(ppu_context* ctx) { t_d6a_obj = (uint32_t)ctx->gpr[3]; t_d6a_n = (uint32_t)ctx->gpr[4]; t_d6a_k = (uint32_t)ctx->gpr[5]; }
extern "C" void jsp_six_d6a1c_leave(ppu_context* ctx)
{
    static const bool on = std::getenv("SIX_PACKS") != nullptr;
    if (!on || !t_d6a_obj) return;
    static int n = 0; if (n++ > 120) return;
    const uint32_t e = t_d6a_obj + 0x3F0 + t_d6a_n * 0x30;
    std::printf("[sixpk] entry %u part %u: character %d place %d -> pack %08X\n", t_d6a_n, t_d6a_k, (int32_t)vm_read32(e + 0x10), (int32_t)vm_read32(e + 0x14), (uint32_t)ctx->gpr[3]);
    std::fflush(stdout);
}
/* SIX_FILES (L1/L3 of battle_end.md): func_0040EC14(handle, path) reads a pack into its reserved block: descriptor =
 * handle + 0x0C, capacity at descriptor + 0, address at descriptor + 4 */
extern "C" void jsp_six_read_enter(ppu_context* ctx)
{
    static const bool on = std::getenv("SIX_FILES") != nullptr;
    const uint32_t h = (uint32_t)ctx->gpr[3], a = (uint32_t)ctx->gpr[4];
    if (!on || !vm_base || a < 0x10000u || a >= 0xF0000000u || h < 0x10000u || h >= 0x30000000u) return;
    char name[160]; int i = 0; for (; i < 159; i++) { const char c = (char)vm_base[a + i]; if (!c) break; name[i] = c; } name[i] = 0;
    if (!std::strstr(name, "battle_character")) return;
    const uint32_t d = vm_read32(h + 0x0C);
    std::printf("[sixr] read %s: descriptor %08X capacity %08X address %08X\n", name, d, d ? vm_read32(d) : 0u, d ? vm_read32(d + 4) : 0u); std::fflush(stdout);
}
/* SIX_READY=1 (test): which load place / slot never reports "loaded". func_00412CAC(loader, place) answers 1 when
 * every reserved slot 7p+k is loaded; func_00424230(loader, slot) is the per-slot "loaded" test. Every 3 s: the last
 * answer per place and per slot 0..41. */
static thread_local int t_rdy_p = -1, t_rdy_s = -1; static thread_local uint32_t t_rdy_l = 0;
static int g_rdy_place[8] = { -9, -9, -9, -9, -9, -9, -9, -9 }, g_rdy_slot[2][48]; static uint32_t g_rdy_loader[2];
static void six_ready_report()
{
    static unsigned long long last = 0; static int lines = 0;
    const unsigned long long now = GetTickCount64();
    if (now - last < 3000 || lines >= 40) return;
    last = now; lines++;
    std::printf("[sixy] places:"); for (int i = 0; i < 6; i++) std::printf(" %d", g_rdy_place[i]);
    for (int l = 0; l < 2; l++) { std::printf(" | loader %08X slots:", g_rdy_loader[l]); for (int i = 0; i < 42; i++) std::printf("%s%d", (i % 7) ? "" : " ", g_rdy_slot[l][i] < -1 ? 8 : g_rdy_slot[l][i]); }
    std::printf("\n"); std::fflush(stdout);
}
extern "C" void jsp_six_rdyp_enter(ppu_context* ctx) { t_rdy_p = (int32_t)ctx->gpr[4]; }
extern "C" void jsp_six_rdyp_leave(ppu_context* ctx)
{
    static const bool on = std::getenv("SIX_READY") != nullptr;
    if (!on) return;
    if (t_rdy_p >= 0 && t_rdy_p < 8) g_rdy_place[t_rdy_p] = (int32_t)ctx->gpr[3];
    six_ready_report();
}
extern "C" void jsp_six_rdys_enter(ppu_context* ctx) { t_rdy_s = (int32_t)ctx->gpr[4]; t_rdy_l = (uint32_t)ctx->gpr[3]; }
extern "C" void jsp_six_rdys_leave(ppu_context* ctx)
{
    static const bool on = std::getenv("SIX_READY") != nullptr;
    if (!on) return;
    static bool init = false; if (!init) { init = true; for (int l = 0; l < 2; l++) for (int i = 0; i < 48; i++) g_rdy_slot[l][i] = -9; }
    int l = 0; while (l < 2 && g_rdy_loader[l] && g_rdy_loader[l] != t_rdy_l) l++;
    if (l == 2) return; g_rdy_loader[l] = t_rdy_l;
    if (t_rdy_s >= 0 && t_rdy_s < 48) g_rdy_slot[l][t_rdy_s] = (int32_t)ctx->gpr[3];
}
extern "C" void jsp_six_bset_enter(ppu_context* ctx) { six_bits_log("set", ctx); }
extern "C" void jsp_six_bever_enter(ppu_context* ctx) { six_bits_log("ever", ctx); }
extern "C" void jsp_six_bclr_enter(ppu_context* ctx) { six_bits_log("clr", ctx); }
extern "C" void jsp_six_bclr2_enter(ppu_context* ctx) { six_bits_log("clr2", ctx); }
/* func_000E375C(judge, reason r4): the fight scene's judge (func_000E6640) records why the fight is over; the battle
 * event flags it read are *(*(TOC-0x536C)) +0 / +4 (func_000E1A34) */
extern "C" void jsp_six_reason_enter(ppu_context* ctx)
{
    six_bits_log("reason", ctx);
    static const bool on = std::getenv("SIX_BITS") != nullptr;
    if (!on) return;
    g_six_toc = (uint32_t)ctx->gpr[2];
    const uint32_t f = vm_read32(vm_read32(g_six_toc - 0x536Cu));
    std::printf("[sixb] reason %d r2 %08X flags@%08X\n", (int32_t)ctx->gpr[4], g_six_toc, f);
    if (f >= 0x10000 && f < 0xF0000000u) { std::printf("[sixb] reason %d flags %08X: +0 %08X +4 %08X +8 %08X\n", (int32_t)ctx->gpr[4], f, vm_read32(f), vm_read32(f + 4), vm_read32(f + 8)); std::fflush(stdout); }
}
extern "C" void jsp_six_scenemake_enter(ppu_context* ctx) { g_six_mode = (uint32_t)ctx->gpr[3]; g_six_toc = (uint32_t)ctx->gpr[2]; six_bits_log("make", ctx); }   /* func_000FD780(mode, scene index r4): makes the next scene; 23 = the end scene */
extern "C" void jsp_six_finish_enter(ppu_context* ctx) { six_bits_log("finish", ctx); }
extern "C" void jsp_six_scene_enter(ppu_context* ctx) { six_bits_log("scene", ctx); }    /* func_00104854(r3, scene id r4, r5): scene request; 0x17 = the end scene */
extern "C" void jsp_six_scene2_enter(ppu_context* ctx) { if ((uint32_t)ctx->gpr[4] == 0x17u) six_bits_log("scene2", ctx); }
extern "C" void jsp_six_teamout_enter(ppu_context* ctx)
{
    six_bits_log("teamout", ctx);
    static const bool on = std::getenv("SIX_BITS") != nullptr;
    if (on) { const uint32_t e = vm_read32(0x00951FFCu); if (e >= 0x10000000u && e < 0x20000000u) { std::printf("[sixb] end record %08X +0C %d\n", e, (int32_t)vm_read32(e + 0x0C)); std::fflush(stdout); } }
}
extern "C" void jsp_six_msg_enter(ppu_context* ctx)
{
    static const bool on = std::getenv("SIX_MSG") != nullptr;
    if (!on) return;
    const uint32_t o = (uint32_t)ctx->gpr[3];
    if (o < 0x10000000u || o >= 0x20000000u) return;
    static uint32_t lo = 0, l10 = 0xFFFFFFFFu, l4a8 = 0xFFFFFFFFu; static int n = 0;
    const uint32_t a = vm_read32(o + 0x10), b = vm_read32(o + 0x4A8);
    if ((o != lo || a != l10 || b != l4a8) && n < 80) { n++; lo = o; l10 = a; l4a8 = b;
        std::printf("[sixm] t=%llu msg obj %08X vt %08X +10 %d +4A8 %d lr %08X\n", (unsigned long long)GetTickCount64(), o, vm_read32(o), (int32_t)a, (int32_t)b, (uint32_t)ctx->lr); std::fflush(stdout); }
}
extern "C" void jsp_six_pool_enter(ppu_context* ctx)
{
    if (!six_on()) return;
    static int n = 0; if (n++ >= 200) return;
    const uint32_t p = (uint32_t)ctx->gpr[3];
    std::printf("[six] pool %08X vt %08X id 0x%X lr %08X t=%llu\n", p, (p >= 0x00010000u && p < 0x30000000u) ? vm_read32(p) : 0u,
                (uint32_t)ctx->gpr[4], (uint32_t)ctx->lr, (unsigned long long)GetTickCount64());
    std::fflush(stdout);
}

static bool pa_heap(uint32_t a) { return a >= 0x10000000u && a < 0x20000000u && !(a & 3); }

/* P-KO-8 (2026-11-15): the game's own pane lookups for the KO bars: func_0032B230(layout, hash) enter/leave, logged when the
 * hash is one of the bar's names (a local probe output), with the pane it returns. MAP_PROBE only. */
/* SIX_CSCLONE=1 (3v3 Character Select, the maintainer's 11-17 mock-up): each team gets a copy of its partner box (slot 1 for red,
 * slot 3 for blue) left of the leader box, for its 3rd fighter (slot 4 red, slot 5 blue). When the team panel layout is
 * built, the partner's panes (and their children) are copied with the game's own new (pane block, its shape +0xE8 and
 * the shape's +0x08 block too, sizes read from the heap descriptors), renamed (name + "x4" / "x5"), moved by
 * SIX_CS_DX/DY in the file position (+0x98/+0xA4; copies are not animated, so nothing overwrites it) and appended to
 * the layout's flat pane list (a bigger list from new, the old one freed). The slot code finds panes by name hash
 * every frame; while it draws slot 4/5 (the slot-kind hook saw 4/5 last), the hashes it asks for (its 6-entry arrays,
 * and the 4-entry arrays that overflow into their neighbours) are sent to the copies instead (jsp_find_enter). */
static uint32_t six_name_hash(const char* n) { uint32_t h = 1; for (; *n; n++) h = (h + (uint8_t)*n) * 0x89u; return h; }
static void six_read_name(uint32_t a, char* out, int max) { int q = 0; if (a >= 0x10000u && a < 0x30000000u) for (; q < max - 1; q++) { const char c = (char)vm_base[a + q]; if (!c) break; out[q] = c; } out[q] = 0; }
static uint32_t six_block_size(uint32_t p) { const uint32_t d = (p >= 0x10004u && p < 0x30000000u) ? vm_read32(p - 4) : 0u; return (d >= 0x10000u && d < 0x30000000u) ? vm_read32(d + 8) : 0u; }
static uint32_t six_clone_block(ppu_context* ctx, uint32_t src, uint32_t fallback)
{
    uint32_t sz = six_block_size(src); if (sz < 0x10u || sz > 0x1000u) sz = fallback;
    const uint32_t dst = six_game_new(ctx, sz); if (!dst) return 0;
    for (uint32_t o = 0; o < sz; o += 4) vm_write32(dst + o, vm_read32(src + o));
    return dst;
}
struct SixCsRemap { uint32_t from, to; };
struct SixCsClone { uint32_t layout; int n4, n5; SixCsRemap r4[24], r5[24]; int nm[2]; SixCsRemap map[2][64]; };
static SixCsClone g_six_cs = {};
/* per call site (lr) the hash slot 1 (red partner) and slot 3 (blue partner) asked last: slot 4/5 lookups at the same
 * call site go to the copy of that pane (the fixed name table above is a fallback) */
struct SixCsSite { uint32_t lr, h; };
static SixCsSite g_six_cs_site[2][48]; static int g_six_cs_nsite[2] = { 0, 0 };
static const char* const k_six_cs_roles[] = { "_ca_", "p_waku_mask_", "p_rgb8_256_chara_ico_", "p_waku_", "p_waku_light_", "p_com_star_",
    "p_waku_defolt_bg_q_", "_f_c_cl00_000_", "_f_c_cl00_001_", "p_player_ico_tx_", "p_locked_", "p_don_", "p_effect_window_", "p_waku_effect_" };
/* Drawing walks a tree (charsel.md 7.10): each pane's +0xE8 object (vt 0x00882E60) is its child table: +0x04 owner,
 * +0x08 array of 12-byte records {text child, picture child, u16}, +0x0C count (u16). A copy is a deep copy: the pane,
 * its own child table and record array, and every child (text and picture) the same way, each child's +0x04 set to its
 * copied parent. Named copies get the name + tag and a new hash. The top copies are added as new records to the root
 * pane's table (after the old records, so drawn on top). */
static uint32_t g_six_cs_added[400]; static int g_six_cs_nadded = 0;
static uint32_t g_six_cs_pair[600][2]; static int g_six_cs_npair = 0;   /* every copied pane and its source */
static bool g_six_cs_own[600]; static bool g_six_cs_cur_own = false;
/* groups whose own timeline the game drives through slot 4/5's label plays (cursor glow p_waku_light, streak
 * p_waku_effect): the copy takes the Support's name, so the game's slot-4/5 lookups find it; below its root nothing is
 * mirrored, the copy's own timeline animates it */
static bool g_six_cs_anim[600]; static uint8_t g_six_cs_dep[600]; static bool g_six_cs_cur_anim = false;
static uint8_t g_six_cs_glow[600];   /* root of a copied group: 1 = glow (p_waku_light_), 2 = streak (p_waku_effect_) */
static uint32_t g_six_cs_sup_glow[4] = { 0, 0, 0, 0 };   /* the Support boxes' own glow and streak panes (hidden: locked) */
static bool g_six_cs_nopair = false;   /* a late copy (CPU stars): not put in the pair list */
static uint32_t g_six_cs_sup_pulse[2] = { 0, 0 };        /* _a_support_light_waku under each Support glow root */
static uint32_t g_six_cs_sup_kids[2][8]; static int g_six_cs_sup_nkids[2] = { 0, 0 };   /* the Support glow roots' children */
static const char* six_cs_anim_name(const char* nm, int team)
{
    struct { const char* role; const char* sup[2]; } static const k[] = {
        { "p_waku_light_", { "p_support_light_000", "p_support_light_001" } },
        { "p_waku_effect_", { "p_support_waku_effect_000", "p_support_waku_effect_001" } } };
    /* opt-in (SIX_CSGLOW=1): the copies' own timelines do not answer the game's label plays, so with the rename the
     * glow showed on neither box and both Support boxes stayed lit (a test run) */
    static const bool on = std::getenv("SIX_CSGLOW") != nullptr;
    if (!on) return nullptr;
    for (const auto& e : k) { const size_t L = std::strlen(e.role); if (!std::strncmp(nm, e.role, L) && std::strlen(nm) == L + 3) return e.sup[team & 1]; }
    return nullptr;
}   /* in a copy group slot 4/5's own code drives (face, icon, lock, don) */
static uint32_t g_six_cs_boxes = 0;
/* the root child table the copies were appended to and its new record array; when the game sets the table up again
 * (back from Stage Select it keeps the layout object but puts its own record array back: a test run,
 * no copies and the Support boxes as before), the copies are made again. g_six_cs_gen counts the copies made. */
static uint32_t g_six_cs_root_tb = 0, g_six_cs_root_arr = 0, g_six_cs_gen = 0, g_six_cs_root_base = 0;
static bool six_frozen[2] = { false, false }, g_six_cs_leaving = false;
/* Pick animations (the maintainer 2026-11-18: "the slide animation should happen only on that slot that chooses a character").
 * The game's timelines move a slot's panes when it gets a fighter. Two side effects with the 3v3 boxes:
 *  - the copies mirror slot 1 / 3's transforms, so the new box slid with its neighbour;
 *  - for slot 4 / 5 the game plays it on the Support plate's panes.
 * So (a) a copy keeps the transforms it last had while a cursor is on its source slot, and for a while after the
 * source slot's data changed; (b) the Support plates' panes get their rest values written back at the start of every
 * layout calc. Both rest on g_six_hold_frames (frames since the copies were made: nothing is held during the first 60,
 * the slide in), on g_six_cs_leaving (Start) and on g_six_cs_transition (both leader frames away from their rest
 * place = the screen is sliding). SIX_CSNOHOLD=1 turns both off. */
static uint32_t g_six_hold_gen = 0; static int g_six_hold_frames = 0;
static struct SixRest { uint32_t pane, name; uint32_t w[17]; } g_six_sup_rest[64];
static int g_six_sup_nrest = 0; static bool g_six_rest_ok = false, g_six_cs_transition = false;
static uint32_t g_six_ref_pane[2] = { 0, 0 }, g_six_ref_pos[2][2];
static bool six_hold[2] = { false, false };
/* The pick effect on a new box (the maintainer 2026-11-18: slots 5 and 6 have no slide / select effect). The game's own effect
 * runs in the slot's face clip, which a copy cannot play (jsp_six_label_enter), so it is replayed by hand with the
 * numbers measured on slot 3 (a test run, frames counted from the frame the slot's data changes):
 *   face (p_chara_000, file matrix x +0x98, colour multiply alpha +0xC0): hidden for frames 0..5, then
 *     x +90 -> 83.5 -> 64 -> 31.5 -> -14 (alpha 0, 1/16, 4/16, 9/16, 1), stays at -14 one frame, +10, 0;
 *   "?" node (child of p_waku_defolt_bg_q): frames 7..10 x -22.5, -45, -67.5, -90 with alpha 0.75, 0.5, 0.25, 0.
 * six_pick_t[t] = frame of the effect on new box 4+t (-1: none), six_pick_q[t] = the slot was on Random before (the
 * "?" slides out). SIX_CSNOPICKFX=1 turns it off. */
static int six_pick_t[2] = { -1, -1 }; static bool six_pick_q[2] = { false, false };
/* The "don" flash (the maintainer 2026-11-18: each character has its own pick effect, and for slot 6 it shows on the Support
 * slot). One pane per slot, p_don_000..005, root records 206..216 (not in the flat pane list; their names are not set
 * in the first frames). Its own clip plays frames 2..0x15 when the slot gets a fighter (a test run);
 * the picture is don_tx_%03d.png set by func_00197E68. Rest matrices: p_don_001 at 232,26 and p_don_003 at 232,287 with
 * scale 1; p_don_004 at 11,191 and p_don_005 at 11,455 with scale 0.6 (the Support plates). For 3v3 the two are given
 * the partner's scale and place plus the copy offset, and their records are moved behind the copies' records so they
 * draw over the new boxes. SIX_CSNODON=1 leaves them on the Support plates. */
static uint32_t g_six_don[2] = { 0, 0 }, g_six_don_src[2] = { 0, 0 };
/* The white flash when a fighter is chosen and the yellow burst when it is locked in (the maintainer 2026-11-19: both missing on
 * the two new boxes). Helper map 2026-11-19 (charsel.md 7.29): one root pane per slot, played by the game itself: the
 * flash p_waku_hilight_* (label fl_right / fl_left at 0x00199D34, slot command 0xB), the burst p_waku_effect_00N (fl_ok
 * at 0x0019A268, command 0xD); for slots 4 / 5 the game plays them on the Support plates' own panes
 * p_support_waku_hilight_00N / p_support_waku_effect_00N. Those four are treated like the don: their labels are let
 * through, their root records go behind the copies, and each frame they take the place of the partner box's pane
 * (p_waku_hilight_right_00N / p_waku_effect_001 / _003) plus the new box's offset. SIX_CSNOFX=1 keeps the old way. */
static uint32_t g_six_fx[4] = { 0, 0, 0, 0 }, g_six_fx_src[4] = { 0, 0, 0, 0 };   /* hilight team 0 / 1, effect team 0 / 1 */
static bool six_fx_on() { static const bool off = std::getenv("SIX_CSNOFX") != nullptr; return !off; }
/* The selector's pose. The copied glow tree took its transforms from slot 1 / 3's glow every frame. Once that glow
 * had been lit and switched off again its pieces rest in a "switched off" pose (frame drawn off to the side, a second
 * smaller outline), and the new box's selector showed exactly that (the maintainer's screenshots 2026-11-18 22:02; run
 * 2026-11-18_2211-six-repro a2, a7, a12). Now the copies keep one good pose per generation: the source tree's words
 * +0x18..+0xD3 taken once, either before the source was ever lit (frame 60 on) or after it has been lit for 30 frames.
 * six_glow_cap[t] is set on the frame the pose is taken. SIX_CSGLOWLIVE=1 = mirrored every frame as before. */
static bool six_glow_cap[2] = { false, false }, six_glow_have[2] = { false, false };
static bool g_six_pair_valid[600];   /* per frame: the pair's source is still the piece it was copied from (six_cs_frame) */
static uint32_t g_six_cs_list = 0, g_six_cs_list_arr = 0, g_six_cs_list_orig_arr = 0, g_six_cs_list_orig_cnt = 0;   /* the flat pane list we grew */
static uint32_t g_six_cs_sup_face[2] = { 0, 0 };   /* the Support plates' own face panes: hidden (after a return from Stage Select the game put slot 4/5's face on them) */
static uint32_t g_six_cs_sup_ico[2] = { 0, 0 };   /* the Support plates' own "1P/2P" icons (no longer driven: hidden) */
static uint32_t g_six_cs_top[200]; static int g_six_cs_ntop = 0; static float g_six_cs_dx = 0.0f, g_six_cs_dy = 0.0f;   /* top copies (moved) */
static uint32_t six_cs_copy_tree(ppu_context* ctx, uint32_t src, uint32_t parent, const char* tag, int team, int depth)
{
    if (src < 0x10000u || src >= 0x30000000u || depth > 6) return 0;
    const uint32_t c = six_clone_block(ctx, src, 0x1E0u); if (!c) return 0;
    vm_write32(c + 0x04, parent);
    if (depth == 0) { char n0[48]; six_read_name(vm_read32(src + 0x104), n0, 40); g_six_cs_cur_own = false;
        for (const char* r : { "p_rgb8_256_chara_ico_", "p_player_ico_tx_", "p_locked_", "p_don_", "p_effect_window_" }) if (!std::strncmp(n0, r, std::strlen(r))) g_six_cs_cur_own = true;
        g_six_cs_cur_anim = six_cs_anim_name(n0, team) != nullptr; if (g_six_cs_cur_anim) g_six_cs_cur_own = true; }
    if (!g_six_cs_nopair && g_six_cs_npair < 600) { g_six_cs_pair[g_six_cs_npair][0] = src; g_six_cs_pair[g_six_cs_npair][1] = c; g_six_cs_own[g_six_cs_npair] = g_six_cs_cur_own;
        g_six_cs_anim[g_six_cs_npair] = g_six_cs_cur_anim; g_six_cs_dep[g_six_cs_npair] = (uint8_t)depth;
        { uint8_t gl = 0; if (depth == 0) { char gn[48]; six_read_name(vm_read32(src + 0x104), gn, 40);
              gl = !std::strncmp(gn, "p_waku_light_", 13) ? 1 : !std::strncmp(gn, "p_waku_effect_", 14) ? 2 : 0; }
          g_six_cs_glow[g_six_cs_npair] = gl; }
        g_six_cs_npair++; }
    { /* SIX_CSE4=1 (test): the copy also gets its own copy of the +0xE4 object (per-pane draw data?) */
      static const bool e4 = std::getenv("SIX_CSE4") != nullptr;
      const uint32_t o = vm_read32(src + 0xE4);
      if (e4 && o >= 0x10000u && o < 0x30000000u) { const uint32_t sz = six_block_size(o);
          static int said = 0; if (said++ < 6) { std::printf("[sixcs] +E4 %08X block size %X words %08X %08X %08X %08X\n", o, sz, vm_read32(o), vm_read32(o + 4), vm_read32(o + 8), vm_read32(o + 12)); std::fflush(stdout); }
          if (sz >= 0x10u && sz <= 0x2000u) { const uint32_t o2 = six_clone_block(ctx, o, sz); if (o2) vm_write32(c + 0xE4, o2); } } }
    const uint32_t nmp = vm_read32(src + 0x104);
    if (nmp >= 0x10000u && nmp < 0x30000000u) {
        char nm[48]; six_read_name(nmp, nm, 40);
        static const char* const six_roles[] = { "p_rgb8_256_chara_ico_", "p_player_ico_tx_", "p_locked_", "p_don_", "p_effect_window_" };
        bool own = false;
        if (depth == 0) for (const char* r : six_roles) { const size_t L = std::strlen(r);
            if (!std::strncmp(nm, r, L) && std::strlen(nm) == L + 3) { std::snprintf(nm + L, 8, "%s", team == 0 ? "004" : "005"); own = true; break; } }
        if (depth == 0 && !own) { const char* sn = six_cs_anim_name(nm, team); if (sn) { std::snprintf(nm, sizeof nm, "%s", sn); own = true; } }
        if (depth > 0) { /* children keep their own names (found under their parent) */ }
        else if (!own) std::strncat(nm, tag, 8);
        const uint32_t len = (uint32_t)std::strlen(nm) + 1, str = six_game_new(ctx, (len + 15u) & ~15u);
        if (str) { for (uint32_t q = 0; q < len; q++) vm_base[str + q] = (uint8_t)nm[q]; vm_write32(c + 0x104, str); }
        vm_write32(c + 0x108, six_name_hash(nm));
        if (g_six_cs.nm[team] < 64) g_six_cs.map[team][g_six_cs.nm[team]++] = { vm_read32(src + 0x108), six_name_hash(nm) };
        if (g_six_cs_nadded < 400) g_six_cs_added[g_six_cs_nadded++] = c;
    }
    const uint32_t tb = vm_read32(src + 0xE8);
    if (tb >= 0x10000u && tb < 0x30000000u && vm_read32(tb) == 0x00882E60u) {
        const uint32_t tb2 = six_clone_block(ctx, tb, 0xE0u);
        if (tb2) {
            vm_write32(tb2 + 0x04, c);
            const uint32_t arr = vm_read32(tb + 0x08); const uint32_t n = vm_read32(tb + 0x0C) >> 16;   /* u16 count at +0x0C */
            uint32_t arr2 = 0;
            if (n && n < 64 && arr >= 0x10000u && arr < 0x30000000u) {
                arr2 = six_game_new(ctx, n * 12u + 4u);
                if (arr2) for (uint32_t r = 0; r < n; r++) {
                    const uint32_t a = vm_read32(arr + r * 12), b = vm_read32(arr + r * 12 + 4), w = vm_read32(arr + r * 12 + 8);
                    vm_write32(arr2 + r * 12, a ? six_cs_copy_tree(ctx, a, c, tag, team, depth + 1) : 0u);
                    vm_write32(arr2 + r * 12 + 4, b ? six_cs_copy_tree(ctx, b, c, tag, team, depth + 1) : 0u);
                    vm_write32(arr2 + r * 12 + 8, w);
                }
            }
            vm_write32(tb2 + 0x08, arr2);
            vm_write32(c + 0xE8, tb2);
        }
    }
    return c;
}
static void six_cs_clone(ppu_context* ctx, uint32_t layout)
{
    static const bool on_env = std::getenv("SIX_CSCLONE") != nullptr; const bool on = on_env && six_live();
    if (!on || g_six_cs.layout == layout || !vm_base) return;
    const uint32_t list = vm_read32(layout + 0x14);
    if (list < 0x10000u || list >= 0x30000000u) return;
    const uint32_t arr = vm_read32(list + 0x08), cnt = vm_read32(list + 0x0C);
    if (cnt < 200u || cnt > 400u) return;   /* the team panel layout (249 panes) */
    static uint32_t pane[400]; static char names[400][40];
    for (uint32_t i = 0; i < cnt; i++) { pane[i] = vm_read32(arr + i * 4); six_read_name(vm_read32(pane[i] + 0x104), names[i], 40); }
    int have = 0; for (uint32_t i = 0; i < cnt; i++) if (!std::strcmp(names[i], "p_waku_001")) have = 1;
    if (!have) return;   /* not built yet */
    { static const bool t = std::getenv("SIX_CSTEST") != nullptr;   /* test: does a file-position change of an original pane show? */
      if (t) for (uint32_t i = 0; i < cnt; i++) if (!std::strcmp(names[i], "p_waku_001") || !std::strcmp(names[i], "p_rgb8_256_chara_ico_001")) {
          union { uint32_t w; float f; } y; y.w = vm_read32(pane[i] + 0xA4); y.f += 150.0f; vm_write32(pane[i] + 0xA4, y.w); } }
    g_six_cs.layout = layout; g_six_cs.n4 = g_six_cs.n5 = 0; g_six_cs.nm[0] = g_six_cs.nm[1] = 0; g_six_cs_nsite[0] = g_six_cs_nsite[1] = 0; g_six_cs_nadded = 0; g_six_cs_npair = 0; g_six_cs_ntop = 0;
    auto envf = [](const char* k, float d) { const char* e = std::getenv(k); return e ? (float)std::atof(e) : d; };
    static const float dx = envf("SIX_CS_DX", -464.5f), dy = envf("SIX_CS_DY", 0.0f);
    /* The root's child table is indexed by node; a record's u16 (+8), when set, is the index of the last record of a
     * mask group that starts there (p_waku_mask / _ca_ masks for the face). So the partner box is copied as the whole
     * run of root records from its first to its last part (text records included), in order, and the u16 of each copy
     * is moved by the same offset as the records. */
    { static const char* const six_roles[] = { "p_rgb8_256_chara_ico_", "p_player_ico_tx_", "p_locked_", "p_don_", "p_effect_window_" };
      for (uint32_t i = 0; i < cnt; i++) for (const char* r : six_roles) for (const char* idx : { "004", "005" }) {
          char want[48]; std::snprintf(want, sizeof want, "%s%s", r, idx);
          if (!std::strcmp(names[i], want)) vm_write32(pane[i] + 0x108, six_name_hash(want) + 1u); }
      /* the Support's own glow and streak give their names to the copies (six_cs_anim_name) */
      g_six_cs_sup_ico[0] = g_six_cs_sup_ico[1] = 0; g_six_cs_sup_face[0] = g_six_cs_sup_face[1] = 0;
      for (uint32_t& g : g_six_cs_sup_glow) g = 0;
      for (uint32_t i = 0; i < cnt; i++) { static const char* const gn[4] = { "p_support_light_000", "p_support_light_001", "p_support_waku_effect_000", "p_support_waku_effect_001" };
          for (int k = 0; k < 4; k++) if (!std::strcmp(names[i], gn[k])) g_six_cs_sup_glow[k] = pane[i]; }
      for (int t = 0; t < 2; t++) { g_six_cs_sup_pulse[t] = 0; g_six_cs_sup_nkids[t] = 0; const uint32_t sr = g_six_cs_sup_glow[t]; if (!sr) continue;
          const uint32_t tb = vm_read32(sr + 0xE8); if (tb < 0x10000u || tb >= 0x30000000u || vm_read32(tb) != 0x00882E60u) continue;
          const uint32_t ra = vm_read32(tb + 0x08), rn = vm_read32(tb + 0x0C) >> 16;
          for (uint32_t r = 0; r < rn && r < 32; r++) for (uint32_t k = 0; k < 2; k++) { const uint32_t c = vm_read32(ra + r * 12 + k * 4); if (c < 0x10000u || c >= 0x30000000u) continue;
              if (g_six_cs_sup_nkids[t] < 8) g_six_cs_sup_kids[t][g_six_cs_sup_nkids[t]++] = c;
              char cn[48]; cn[0] = 0; const uint32_t np = vm_read32(c + 0x104); if (np >= 0x10000u && np < 0x30000000u) six_read_name(np, cn, 40);
              if (!std::strcmp(cn, "_a_support_light_waku")) g_six_cs_sup_pulse[t] = c; }
          std::printf("[sixcs] Support glow %d: root %08X, %d children, pulse %08X\n", t, sr, g_six_cs_sup_nkids[t], g_six_cs_sup_pulse[t]); std::fflush(stdout); }
      for (uint32_t i = 0; i < cnt; i++) { if (!std::strcmp(names[i], "p_player_ico_tx_004")) g_six_cs_sup_ico[0] = pane[i]; if (!std::strcmp(names[i], "p_player_ico_tx_005")) g_six_cs_sup_ico[1] = pane[i];
          if (!std::strcmp(names[i], "p_rgb8_256_chara_ico_004")) g_six_cs_sup_face[0] = pane[i]; if (!std::strcmp(names[i], "p_rgb8_256_chara_ico_005")) g_six_cs_sup_face[1] = pane[i]; }
      for (uint32_t i = 0; i < cnt; i++) for (int t = 0; t < 2; t++) for (const char* r : { "p_waku_light_001", "p_waku_effect_001" }) {
          const char* sn = six_cs_anim_name(r, t); if (sn && !std::strcmp(names[i], sn)) vm_write32(pane[i] + 0x108, six_name_hash(sn) + 1u); } }
    uint32_t root = 0; static uint32_t newrec[160][3]; static int oldidx[160], teamof[160]; int nnew = 0;
    for (uint32_t i = 0; i < cnt && !root; i++) if (!std::strcmp(names[i], "p_waku_001")) root = vm_read32(pane[i] + 4);
    const uint32_t rtb = (root >= 0x10000u && root < 0x30000000u) ? vm_read32(root + 0xE8) : 0u;
    if (!rtb || vm_read32(rtb) != 0x00882E60u) return;
    const uint32_t rra = vm_read32(rtb + 0x08), rrn = vm_read32(rtb + 0x0C) >> 16;
    for (int team = 0; team < 2; team++) {
        const char* sfx = team == 0 ? "001" : "003";
        const char* tag = team == 0 ? "x4" : "x5";
        int lo = 100000, hi = -1;
        for (uint32_t q = 0; q < rrn && q < 600; q++) { const uint32_t b = vm_read32(rra + q * 12 + 4); if (!b) continue;
            char nm[48]; six_read_name(vm_read32(b + 0x104), nm, 40);
            for (const char* r : k_six_cs_roles) { char want[48]; std::snprintf(want, sizeof want, "%s%s", r, sfx); if (!std::strcmp(nm, want)) { if ((int)q < lo) lo = (int)q; if ((int)q > hi) hi = (int)q; break; } } }
        if (hi < 0) continue;
        const int base = nnew;
        for (int q = lo; q <= hi && nnew < 160; q++) {
            const uint32_t a = vm_read32(rra + (uint32_t)q * 12), b = vm_read32(rra + (uint32_t)q * 12 + 4), w = vm_read32(rra + (uint32_t)q * 12 + 8);
            const uint32_t ca = a ? six_cs_copy_tree(ctx, a, root, tag, team, 0) : 0u, cb = b ? six_cs_copy_tree(ctx, b, root, tag, team, 0) : 0u;
            for (uint32_t c : { ca, cb }) if (c && g_six_cs_ntop < 200) g_six_cs_top[g_six_cs_ntop++] = c;
            g_six_cs_dx = dx; g_six_cs_dy = dy;
            for (uint32_t c : { ca, cb }) if (c) { union { uint32_t w; float f; } x, y; x.w = vm_read32(c + 0x98); y.w = vm_read32(c + 0xA4); x.f += dx; y.f += dy; vm_write32(c + 0x98, x.w); vm_write32(c + 0xA4, y.w); }
            newrec[nnew][0] = ca; newrec[nnew][1] = cb; newrec[nnew][2] = w; oldidx[nnew] = q; teamof[nnew] = team; nnew++;
        }
        /* fallback name table for slot 4/5 lookups (the call-site remap is the main one) */
        SixCsRemap* rm = team == 0 ? g_six_cs.r4 : g_six_cs.r5; int& nr = team == 0 ? g_six_cs.n4 : g_six_cs.n5;
        auto clone_of = [&](const char* role) -> uint32_t { char want[48]; std::snprintf(want, sizeof want, "%s%s%s", role, sfx, tag); return six_name_hash(want); };
        auto add = [&](const char* from, uint32_t to) { if (nr < 24) rm[nr++] = { six_name_hash(from), to }; };
        const char* own = team == 0 ? "004" : "005";
        char f[48];
        std::snprintf(f, sizeof f, "p_rgb8_256_chara_ico_%s", own); add(f, clone_of("p_rgb8_256_chara_ico_"));
        std::snprintf(f, sizeof f, "p_player_ico_tx_%s", own); add(f, clone_of("p_player_ico_tx_"));
        std::snprintf(f, sizeof f, "p_locked_%s", own); add(f, clone_of("p_locked_"));
        /* p_don_004 / _005 are NOT remapped: they are root records outside the copied run, so there is no copy, and the
         * remap made the lookup fail (no "don" flash at all for slot 4 / 5). The originals are moved onto the new boxes
         * instead (six_cs_frame, "don"). */
        std::snprintf(f, sizeof f, "p_effect_window_%s", own); add(f, clone_of("p_effect_window_"));
        for (const char* r : { "p_waku_light_001", "p_waku_effect_001" }) { const char* sn = six_cs_anim_name(r, team); if (sn) add(sn, six_name_hash(sn)); }
        std::printf("[sixcs] team %d: root records %d..%d copied (%d), %d named copies\n", team, lo, hi, nnew - base, g_six_cs.nm[team]); std::fflush(stdout);
    }
    { /* SIX_CSSWAP=1 (test): put the copy of p_waku_001 into the ORIGINAL record of p_waku_001 instead of a new one,
       * to see whether a copied pane draws at all when it sits in the table's old range */
      static const bool sw = std::getenv("SIX_CSSWAP") != nullptr;
      if (sw) for (uint32_t q = 0; q < rrn; q++) { const uint32_t b = vm_read32(rra + q * 12 + 4); if (!b) continue;
          char nm[48]; six_read_name(vm_read32(b + 0x104), nm, 40);
          if (std::strcmp(nm, "p_waku_001") && std::strcmp(nm, "p_rgb8_256_chara_ico_001")) continue;
          for (int k = 0; k < g_six_cs_npair; k++) if (g_six_cs_pair[k][0] == b) { vm_write32(rra + q * 12 + 4, g_six_cs_pair[k][1]);
              std::printf("[sixcs] test: record %u %s -> its copy %08X\n", q, nm, g_six_cs_pair[k][1]); std::fflush(stdout); break; } } }
    { /* SIX_CSMOVE=<index> (test): move the ORIGINAL p_waku_001 record to that empty record index; does it still show? */
      const char* e = std::getenv("SIX_CSMOVE");
      if (e) { const uint32_t to = (uint32_t)std::atoi(e);
          for (uint32_t q = 0; q < rrn && to < rrn; q++) { const uint32_t b = vm_read32(rra + q * 12 + 4); if (!b) continue;
              char nm[48]; six_read_name(vm_read32(b + 0x104), nm, 40); if (std::strcmp(nm, "p_waku_001")) continue;
              for (int w = 0; w < 3; w++) { vm_write32(rra + to * 12 + (uint32_t)w * 4, vm_read32(rra + q * 12 + (uint32_t)w * 4)); vm_write32(rra + q * 12 + (uint32_t)w * 4, 0u); }
              std::printf("[sixcs] test: p_waku_001 record %u moved to %u\n", q, to); std::fflush(stdout); break; } } }
    /* SIX_CSHOST=<pane name> (test, default p_waku_000): only the root's own record slots draw (moving the original
     * p_waku_001 record to an empty index hides it, a test run), so the copies become CHILDREN of
     * a visible root pane instead: appended to that pane's child table (a bigger record array), parent = that pane,
     * file position made relative to it. */
    { const char* hn = std::getenv("SIX_CSHOST"); const char* host_name = hn ? hn : "p_waku_000";
      uint32_t host = 0; for (uint32_t i = 0; i < cnt; i++) if (!std::strcmp(names[i], host_name)) { host = pane[i]; break; }
      const uint32_t ht = host ? vm_read32(host + 0xE8) : 0u;
      if (host && ht >= 0x10000u && ht < 0x30000000u && vm_read32(ht) == 0x00882E60u && nnew) {
          union { uint32_t w; float f; } hx, hy; hx.w = vm_read32(host + 0x98); hy.w = vm_read32(host + 0xA4);
          const uint32_t ha = vm_read32(ht + 0x08), hcw = vm_read32(ht + 0x0C), hn0 = hcw >> 16;
          const uint32_t ha2 = six_game_new(ctx, (hn0 + (uint32_t)nnew) * 12u + 4u);
          if (ha2) {
              for (uint32_t q = 0; q < hn0 * 3u; q++) vm_write32(ha2 + q * 4, vm_read32(ha + q * 4));
              for (int k = 0; k < nnew; k++) {
                  for (int w = 0; w < 2; w++) { const uint32_t c = newrec[k][w]; if (!c) continue;
                      vm_write32(c + 0x04, host);
                      union { uint32_t w; float f; } x, y; x.w = vm_read32(c + 0x98); y.w = vm_read32(c + 0xA4); x.f -= hx.f; y.f -= hy.f; vm_write32(c + 0x98, x.w); vm_write32(c + 0xA4, y.w); }
                  const uint32_t at = hn0 + (uint32_t)k; uint32_t wd = newrec[k][2], key = wd >> 16;
                  if (key) { int last = -1; for (int q = 0; q < nnew; q++) if (teamof[q] == teamof[k] && oldidx[q] <= (int)key) last = q; key = last >= 0 ? hn0 + (uint32_t)last : 0u; wd = (key << 16) | (wd & 0xFFFFu); }
                  vm_write32(ha2 + at * 12, newrec[k][0]); vm_write32(ha2 + at * 12 + 4, newrec[k][1]); vm_write32(ha2 + at * 12 + 8, wd);
              }
              vm_write32(ht + 0x08, ha2); vm_write32(ht + 0x0C, ((hn0 + (uint32_t)nnew) << 16) | (hcw & 0xFFFFu));
              std::printf("[sixcs] copies hung under %s %08X: its child table %u -> %u records\n", host_name, host, hn0, hn0 + (uint32_t)nnew); std::fflush(stdout);
              nnew = 0;   /* nothing left for the root placement below */
          } } }
    /* draw: each team's copied run (empty records kept, so face, mask and frame keep their distances) is appended
     * after the table's records in a bigger array; a clip depth N becomes N - lo + the run's new start. Copies are
     * mirrored from their sources every frame (charsel.md 7.14), so their depth no longer matters (7.16). */
    if (nnew) {
        const uint32_t ra2 = six_game_new(ctx, (rrn + (uint32_t)nnew) * 12u + 4u);
        if (ra2) {
            for (uint32_t q = 0; q < rrn * 3u; q++) vm_write32(ra2 + q * 4, vm_read32(rra + q * 4));
            int t0 = 0;
            while (t0 < nnew) {
                int t1 = t0; while (t1 < nnew && teamof[t1] == teamof[t0]) t1++;
                const uint32_t start = rrn + (uint32_t)t0; const int lo = oldidx[t0];
                for (int k = t0; k < t1; k++) {
                    const uint32_t at = rrn + (uint32_t)k; uint32_t w = newrec[k][2], key = w >> 16;
                    if (key) { key = start + (key - (uint32_t)lo); w = (key << 16) | (w & 0xFFFFu); }
                    vm_write32(ra2 + at * 12, newrec[k][0]); vm_write32(ra2 + at * 12 + 4, newrec[k][1]); vm_write32(ra2 + at * 12 + 8, w);
                }
                std::printf("[sixcs] team %d: records %d..%d copied to %u..%u\n", teamof[t0], lo, oldidx[t1 - 1], start, start + (uint32_t)(t1 - t0) - 1u); std::fflush(stdout);
                t0 = t1;
            }
            const uint32_t cw = vm_read32(rtb + 0x0C);
            vm_write32(rtb + 0x08, ra2); vm_write32(rtb + 0x0C, ((rrn + (uint32_t)nnew) << 16) | (cw & 0xFFFFu));
            g_six_cs_root_tb = rtb; g_six_cs_root_arr = ra2; g_six_cs_root_base = rrn; g_six_cs_gen++; g_six_cs_leaving = false;
        }
    }
    const uint32_t rt = rtb;
    if (std::getenv("SIX_CSLOG")) { /* diagnostics: the root table records, the vtables the draw calls */
      std::printf("[sixcsd] layout +0x10 %08X root vt %08X table vt %08X | table:", vm_read32(layout + 0x10), vm_read32(root), rt ? vm_read32(rt) : 0u);
      if (rt) for (int q = 0; q < 8; q++) std::printf(" %08X", vm_read32(rt + q * 4));
      std::printf("\n");
      if (rt) { const uint32_t ra = vm_read32(rt + 0x08), rn = vm_read32(rt + 0x0C) >> 16;
        for (uint32_t q = 0; q < rn; q++) if (vm_read32(ra + q * 12) | vm_read32(ra + q * 12 + 4)) { char n1[48], n2[48]; six_read_name(vm_read32(vm_read32(ra + q * 12) + 0x104), n1, 40); six_read_name(vm_read32(vm_read32(ra + q * 12 + 4) + 0x104), n2, 40); std::printf("[sixcsd] rec %u: %08X %s | %08X %s | key %u flags %08X\n", q, vm_read32(ra + q * 12), vm_read32(ra + q * 12) ? n1 : "-", vm_read32(ra + q * 12 + 4), vm_read32(ra + q * 12 + 4) ? n2 : "-", vm_read32(ra + q * 12 + 8) >> 16, vm_read32(ra + q * 12 + 4) ? vm_read32(vm_read32(ra + q * 12 + 4) + 0x10) : 0u); } }
      const uint32_t pv = 0x00882EF0u; std::printf("[sixcsd] pane vt:"); for (int q = 0; q < 12; q++) { const uint32_t d = vm_read32(pv + q * 4); std::printf(" %08X", (d >= 0x10000u && d < 0x01000000u) ? vm_read32(d) : d); } std::printf("\n");
      const uint32_t tv = 0x00882E60u; std::printf("[sixcsd] table vt:"); for (int q = 0; q < 8; q++) { const uint32_t d = vm_read32(tv + q * 4); std::printf(" %08X", (d >= 0x10000u && d < 0x01000000u) ? vm_read32(d) : d); } std::printf("\n"); std::fflush(stdout); }
    /* find: the named copies at the end of the flat list */
    if (g_six_cs_nadded) {
        const uint32_t na2 = six_game_new(ctx, (cnt + (uint32_t)g_six_cs_nadded) * 4u + 16u);
        if (na2) { for (uint32_t i = 0; i < cnt; i++) vm_write32(na2 + i * 4, pane[i]); for (int k = 0; k < g_six_cs_nadded; k++) vm_write32(na2 + (cnt + (uint32_t)k) * 4, g_six_cs_added[k]);
            g_six_cs_list = list; g_six_cs_list_arr = na2; g_six_cs_list_orig_arr = vm_read32(list + 0x08); g_six_cs_list_orig_cnt = cnt;
            vm_write32(list + 0x08, na2); vm_write32(list + 0x0C, cnt + (uint32_t)g_six_cs_nadded);
            std::printf("[sixcs] layout %08X: pane list %u -> %u (%08X)\n", layout, cnt, cnt + (uint32_t)g_six_cs_nadded, na2); std::fflush(stdout); } }
}
/* SIX_CSLOG: func_00329328 (picture pane draw, vt 0x00882EF0 +0x10) for copies and their sources: is it reached, and
 * with what visibility (+0x100), alpha (+0x3C), flags (+0x10), state bits (+0xEC/+0xF4) (probe C9) */
extern "C" void jsp_six_panedraw_enter(ppu_context* ctx)
{
    static const bool lg = std::getenv("SIX_CSLOG") != nullptr; static int n = 0;
    if (!lg || n > 80) return;
    const uint32_t pn = (uint32_t)ctx->gpr[3];
    for (int i = 0; i < g_six_cs_npair; i++) for (int w = 0; w < 2; w++) if (g_six_cs_pair[i][w] == pn) {
        n++; union { uint32_t u; float f; } a; a.u = vm_read32(pn + 0x3C);
        char nm[48]; six_read_name(vm_read32(pn + 0x104), nm, 40);
        std::printf("[sixcsdraw] %s %08X %s vis %08X alpha %.2f flags %08X EC %08X F4 %08X 110 %08X 1D0 %08X 1D4 %08X\n", w ? "COPY" : "src ", pn, nm, vm_read32(pn + 0x100), a.f, vm_read32(pn + 0x10),
                    vm_read32(pn + 0xEC), vm_read32(pn + 0xF4), vm_read32(pn + 0x110), vm_read32(pn + 0x1D0), vm_read32(pn + 0x1D4));
        std::fflush(stdout); return; }
}
/* SIX_CSPKT=1 (probe C10b): func_00526374 adds one draw packet to the renderer; count them per 2 s while Character
 * Select is up, to compare runs with and without the copied boxes */
extern "C" void jsp_six_pkt_enter(ppu_context* ctx)
{
    (void)ctx; static const bool on = std::getenv("SIX_CSPKT") != nullptr;
    if (!on) return;
    static std::atomic<unsigned> n{0}; static unsigned long long t0 = 0; static int lines = 0;
    n++;
    const unsigned long long now = GetTickCount64();
    if (!t0) t0 = now;
    if (now - t0 >= 2000 && lines < 30) { lines++; std::printf("[sixpkt] %u packets in %.1f s (charsel %d)\n", n.load(), (now - t0) / 1000.0, (int)on_charsel()); std::fflush(stdout); n = 0; t0 = now; }
}
static thread_local uint32_t t_find_layout = 0, t_find_hash = 0;
/* Once a frame, right after the team panel layout's calc (func_0032B2B0 leave hook, so after the fl_* timelines):
 * mirror the sources onto the copies, names, frame state (charsel.md 7.14-7.17) */
static uint32_t g_six_cs_cells = 0;   /* the 3v3 slot cursor table (guest), see six_cs_frame */
static const int g_six_cs_cell_of_slot[6] = { 1, 2, 4, 5, 0, 3 };
/* func_0018CA60(r3, r4, dir) moves a slot cursor: record = r3 + 0x58 + 0x2C*r4 (+4 table, +8 cell). The records at
 * screen+0x3C8/+0x3E4 are not the ones it reads (a test run: Left from slot 0 went to slot 2,
 * the old table's path), so the record it is given is switched to the 3v3 table here. */
extern "C" void jsp_six_cmove_enter(ppu_context* ctx)
{
    static const bool on_env = std::getenv("SIX_CSCLONE") != nullptr && std::getenv("SIX_CSNOCURSOR") == nullptr; const bool on = on_env && six_live();
    if (!on || !g_six_cs_cells || !g_six_cs.layout) return;
    /* hooked at func_0018CAB8, the body func_0018CA60 tail-jumps to: r29 = the record, r24 = player, r27 = dir */
    const uint32_t rec = (uint32_t)ctx->gpr[29];
    const uint32_t tp = vm_read32(rec + 4), ci = vm_read32(rec + 8);
    static int said = 0;
    if (said++ < 6) { std::printf("[sixcs] cursor move r3 %08X r4 %u dir %u: record %08X table %08X cell %u\n", (uint32_t)ctx->gpr[25], (uint32_t)ctx->gpr[24], (uint32_t)ctx->gpr[27], rec, tp, ci); std::fflush(stdout); }
    if (tp == 0x00758370u) { const int sl = ci < 8 ? (int8_t)vm_base[0x00758370u + ci * 5 + 4] : -1;
        vm_write32(rec + 4, g_six_cs_cells); vm_write32(rec + 8, (uint32_t)(sl >= 0 && sl < 6 ? g_six_cs_cell_of_slot[sl] : 1)); }
}
static void six_cs_wdump(uint32_t pn, int depth, const char* who)
{
    if (pn < 0x10000u || pn >= 0x30000000u || depth > 5) return;
    char nm[48]; nm[0] = 0; const uint32_t np = vm_read32(pn + 0x104); if (np >= 0x10000u && np < 0x30000000u) six_read_name(np, nm, 40);
    std::printf("[wdump] %s %*s%08X '%s' fl %08X mat %08X vis %08X ec %08X f8 %08X ct", who, depth * 2, "", pn, nm, vm_read32(pn + 0x10), vm_read32(pn + 0x14), vm_read32(pn + 0x100), vm_read32(pn + 0xEC), vm_read32(pn + 0xF8));
    for (uint32_t o = 0xB4; o < 0xD4; o += 4) std::printf(" %08X", vm_read32(pn + o));
    std::printf(" rt"); for (uint32_t o = 0x18; o < 0x40; o += 4) std::printf(" %08X", vm_read32(pn + o));
    std::printf("\n");
    if (depth > 0) { std::printf("[wdump] %s %*s words", who, depth * 2, ""); for (uint32_t o = 0; o < 0x1E0; o += 4) std::printf(" %08X", vm_read32(pn + o)); std::printf("\n"); }
    const uint32_t tb = vm_read32(pn + 0xE8);
    if (tb < 0x10000u || tb >= 0x30000000u || vm_read32(tb) != 0x00882E60u) return;
    const uint32_t arr = vm_read32(tb + 0x08), n = vm_read32(tb + 0x0C) >> 16;
    if (n == 0 || n >= 64 || arr < 0x10000u || arr >= 0x30000000u) return;
    for (uint32_t r = 0; r < n; r++) { const uint32_t a = vm_read32(arr + r * 12), b = vm_read32(arr + r * 12 + 4), w = vm_read32(arr + r * 12 + 8);
        std::printf("[wdump] %s %*s rec %u A %08X B %08X w %08X\n", who, depth * 2, "", r, a, b, w);
        six_cs_wdump(a, depth + 1, who); six_cs_wdump(b, depth + 1, who); }
}
static uint32_t six_cs_ctl(uint32_t scr, int sl);
/* hide a pane and everything under it for this frame: visible bit off and runtime alpha (+0x3C) 0 */
static void six_cs_hide_tree(uint32_t pn, int depth)
{
    if (pn < 0x10000u || pn >= 0x30000000u || depth > 4) return;
    vm_write32(pn + 0x100, vm_read32(pn + 0x100) & ~1u); vm_write32(pn + 0x3C, 0u);
    if (vm_read32(pn) == 0x00882EC0u) { union { float f; uint32_t w; } far_; far_.f = 100000.0f; vm_write32(pn + 0x18, far_.w); vm_write32(pn + 0x20, 0u); vm_write32(pn + 0x24, 0u); }   /* placement nodes ignore the two above (the Support's yellow frame stayed, a test run): moved away, scale 0 */
    const uint32_t tb = vm_read32(pn + 0xE8);
    if (tb < 0x10000u || tb >= 0x30000000u || vm_read32(tb) != 0x00882E60u) return;
    const uint32_t ra = vm_read32(tb + 0x08), rn = vm_read32(tb + 0x0C) >> 16;
    if (ra < 0x10000u || ra >= 0x30000000u) return;
    for (uint32_t r = 0; r < rn && r < 32; r++) { six_cs_hide_tree(vm_read32(ra + r * 12), depth + 1); six_cs_hide_tree(vm_read32(ra + r * 12 + 4), depth + 1); }
}
/* true when the game set the layout's root record array up again: the copies are gone and are made again at the next
 * lookup. Called at every Character Select pane lookup as well as per frame, so the Support panes get their renamed
 * hashes back BEFORE the game's slot code looks slot 4/5's panes up (else it put their faces on the Support plates). */
/* When the screen starts to close (Start, or "Return to the previous screen"), the game takes EVERY child off the
 * layout's root and sets its own panes up again from the file, in place (a test run: parent
 * word, name hash, flags, picture, visible bit of every object cleared; the root records from the copies' first
 * index on emptied; the file's panes back one frame later). The copies are not in the file, so they stayed removed:
 * the new boxes vanished for the slide out, and the name / label text boxes that sit on them drew as flat green bars
 * (the maintainer 2026-11-18 22:36). So a shadow of everything that hangs under the copied root records (objects, child
 * tables, record arrays, the root records themselves) is kept from the last frame before that, and written back
 * when the game has done it. SIX_CSNOSHADOW=1 turns it off. */
static uint32_t g_six_ph[8], g_six_ph_gen = 0; static int g_six_nph = 0;   /* text panes whose box sits on a copy (six_text_pane_hide) */
struct SixShadow { uint32_t addr, size; size_t off; };
static std::vector<SixShadow> g_six_sh; static std::vector<uint8_t> g_six_sh_bytes; static uint32_t g_six_sh_gen = 0, g_six_sh_first = 0; static bool g_six_sh_ok = false;
static void six_shadow_take()
{
    static const bool off = std::getenv("SIX_CSNOSHADOW") != nullptr;
    g_six_sh_ok = false; if (off || !g_six_cs_root_tb || !g_six_cs_root_base || !vm_base) return;
    const uint32_t arr = vm_read32(g_six_cs_root_tb + 0x08), cnt = vm_read32(g_six_cs_root_tb + 0x0C) >> 16, base = g_six_cs_root_base;
    if (arr != g_six_cs_root_arr || cnt <= base || cnt > 700u) return;
    auto okp = [](uint32_t q) { return q >= 0x10000u && q < 0x30000000u; };
    g_six_sh.clear(); g_six_sh_bytes.clear();
    auto add = [&](uint32_t a, uint32_t sz) { g_six_sh.push_back({ a, sz, g_six_sh_bytes.size() }); g_six_sh_bytes.insert(g_six_sh_bytes.end(), vm_base + a, vm_base + a + sz); };
    add(arr + base * 12u, (cnt - base) * 12u);
    static std::vector<uint32_t> st; st.clear(); g_six_sh_first = 0;
    for (uint32_t r = base; r < cnt; r++) for (uint32_t k = 0; k < 2; k++) { const uint32_t c = vm_read32(arr + r * 12u + k * 4u); if (okp(c)) { st.push_back(c); if (!g_six_sh_first) g_six_sh_first = c; } }
    for (size_t qi = 0; qi < st.size() && st.size() < 1200; qi++) { const uint32_t q = st[qi]; const uint32_t vt = vm_read32(q);
        if (vt != 0x00882EF0u && vt != 0x00882EC0u) continue;
        bool dup = false; for (size_t z = 0; z < qi && !dup; z++) dup = st[z] == q; if (dup) continue;
        uint32_t sz = six_block_size(q); if (sz < 0x10u || sz > 0x1000u) sz = 0x1E0u;
        add(q, sz);
        const uint32_t tb = vm_read32(q + 0xE8); if (vt != 0x00882EF0u || !okp(tb) || vm_read32(tb) != 0x00882E60u) continue;
        uint32_t tsz = six_block_size(tb); if (tsz < 0x10u || tsz > 0x1000u) tsz = 0xE0u;
        add(tb, tsz);
        const uint32_t ra = vm_read32(tb + 0x08), rn = vm_read32(tb + 0x0C) >> 16; if (!okp(ra) || !rn || rn >= 64u) continue;
        add(ra, rn * 12u);
        for (uint32_t r = 0; r < rn; r++) for (uint32_t k = 0; k < 2; k++) { const uint32_t c = vm_read32(ra + r * 12u + k * 4u); if (okp(c)) st.push_back(c); } }
    g_six_sh_gen = g_six_cs_gen; g_six_sh_ok = g_six_sh_first != 0;
}
/* true when the game has taken the copies off the root (first copied record empty, or the first copy has no parent) */
static bool six_shadow_lost()
{
    if (!g_six_sh_ok || g_six_sh_gen != g_six_cs_gen || !g_six_cs_root_tb || vm_read32(g_six_cs_root_tb + 0x08) != g_six_cs_root_arr) return false;
    return vm_read32(g_six_sh_first + 0x04) == 0u || vm_read32(g_six_sh_first) == 0u;
}
/* a pane the copies' records hold (the "don" panes moved to the end) comes back in its file record too when the game
 * sets the panes up again (a frame after it took them off): one place only */
static int six_shadow_dedupe()
{
    if (!g_six_cs_root_tb || !g_six_cs_root_base || vm_read32(g_six_cs_root_tb + 0x08) != g_six_cs_root_arr) return 0;
    const uint32_t arr = g_six_cs_root_arr, cnt = vm_read32(g_six_cs_root_tb + 0x0C) >> 16, base = g_six_cs_root_base; int dedup = 0;
    for (uint32_t r = base; r < cnt && r < 700u; r++) for (uint32_t k = 0; k < 2; k++) { const uint32_t c = vm_read32(arr + r * 12u + k * 4u); if (!c) continue;
        for (uint32_t q = 0; q < base; q++) for (uint32_t k2 = 0; k2 < 2; k2++) if (vm_read32(arr + q * 12u + k2 * 4u) == c) { vm_write32(arr + q * 12u + k2 * 4u, 0u); dedup++; } }
    if (dedup) { static int said = 0; if (said++ < 6) { std::printf("[sixcs] %d root records held a pane the copies' records hold too: cleared\n", dedup); std::fflush(stdout); } }
    return dedup;
}
static void six_shadow_restore(const char* when)
{
    if (!g_six_sh_ok || g_six_sh_gen != g_six_cs_gen || !g_six_cs_root_tb || vm_read32(g_six_cs_root_tb + 0x08) != g_six_cs_root_arr) return;
    for (const SixShadow& e : g_six_sh) std::memcpy(vm_base + e.addr, g_six_sh_bytes.data() + e.off, e.size);
    const int dedup = six_shadow_dedupe();
    static int said = 0; if (said++ < 12) { std::printf("[sixcs] the game took the copies off the screen (%s): %d blocks put back from the last frame, %d double records cleared\n", when, (int)g_six_sh.size(), dedup); std::fflush(stdout); }
}
static bool six_cs_rebuilt()
{
    /* three signs, whichever shows first: the root record array, the flat pane list, or a Support face pane that has
     * its own name hash back */
    static const uint32_t fh = six_name_hash("p_rgb8_256_chara_ico_004") + 1u;
    const int why = !g_six_cs.layout ? 0 : (g_six_cs_root_tb && vm_read32(g_six_cs_root_tb + 0x08) != g_six_cs_root_arr) ? 1
                  : (g_six_cs_list && vm_read32(g_six_cs_list + 0x08) != g_six_cs_list_arr) ? 2
                  : (g_six_cs_sup_face[0] && vm_read32(g_six_cs_sup_face[0] + 0x108) != fh) ? 3 : 0;
    /* sign 3 alone (Start pressed, on the way to Stage Select): the panes keep their place and the copies are still in
     * the root records; only the name hashes went back to the file's. The renames are put back and nothing is copied
     * again (a second set of copies during the slide is the likely cause of the green "CPU" blocks the maintainer saw there).
     * SIX_CSSIGN3=1 gives the old behaviour (copies made again). */
    static const bool s3full = std::getenv("SIX_CSSIGN3") != nullptr;
    if (why == 3 && !s3full) {
        const uint32_t list = vm_read32(g_six_cs.layout + 0x14); int nren = 0;
        if (list >= 0x10000u && list < 0x30000000u) { const uint32_t arr = vm_read32(list + 0x08), cnt = vm_read32(list + 0x0C);
            static const char* const six_roles[] = { "p_rgb8_256_chara_ico_", "p_player_ico_tx_", "p_locked_", "p_don_", "p_effect_window_" };
            for (uint32_t i = 0; i < cnt && i < 400u; i++) { const uint32_t pn = vm_read32(arr + i * 4); if (pn < 0x10000u || pn >= 0x30000000u) continue;
                bool own = false; for (int q = 0; q < g_six_cs_npair && !own; q++) own = g_six_cs_pair[q][1] == pn;   /* the copies keep their names */
                if (own) continue;
                char nm[40]; nm[0] = 0; const uint32_t np = vm_read32(pn + 0x104); if (np >= 0x10000u && np < 0x30000000u) six_read_name(np, nm, 40);
                if (!nm[0]) continue;
                bool ren = false;
                for (const char* r : six_roles) for (const char* idx : { "004", "005" }) { char want[48]; std::snprintf(want, sizeof want, "%s%s", r, idx); if (!std::strcmp(nm, want)) ren = true; }
                for (int t = 0; t < 2; t++) for (const char* r : { "p_waku_light_001", "p_waku_effect_001" }) { const char* sn = six_cs_anim_name(r, t); if (sn && !std::strcmp(nm, sn)) ren = true; }
                if (ren && vm_read32(pn + 0x108) == six_name_hash(nm)) { vm_write32(pn + 0x108, six_name_hash(nm) + 1u); nren++; } } }
        std::printf("[sixcs] layout %08X: name hashes were set back (sign 3): %d renames put back, copies kept\n", g_six_cs.layout, nren); std::fflush(stdout);
        g_six_cs_leaving = true;
        if (six_shadow_lost()) six_shadow_restore("screen closing");
        return false; }
    if (why) {
        std::printf("[sixcs] layout %08X: set up again (sign %d; root array now %08X, copies were in %08X): copies made again\n",
                    g_six_cs.layout, why, g_six_cs_root_tb ? vm_read32(g_six_cs_root_tb + 0x08) : 0u, g_six_cs_root_arr); std::fflush(stdout);
        /* the flat pane list still holds the old copies at its end: put the game's own list back first, so the
         * copies made again are the only ones found by name */
        if (g_six_cs_list && vm_read32(g_six_cs_list + 0x08) == g_six_cs_list_arr) { vm_write32(g_six_cs_list + 0x08, g_six_cs_list_orig_arr); vm_write32(g_six_cs_list + 0x0C, g_six_cs_list_orig_cnt); }
        g_six_cs_list = 0; g_six_cs_root_tb = 0; g_six_cs.layout = 0; return true; }
    return false;
}
static void six_cs_frame(ppu_context* ctx)
{
    if (six_cs_rebuilt()) return;
    { /* Which screen: player 2's state word is 1 on BOTH screens (a test run took 1 Player for 2
       * Players), so it is told by the slots' controller word (screen slot +0x10: 0 Player 1, 1 Player 2, 2 CPU): a
       * slot on Player 2 = the 2 Players screen. Kept for the life of these copies once seen. */
      static const bool no2p = [] { const char* e = std::getenv("SIX_CS2P"); return e && std::atoi(e) == 0; }(); static int last2 = -1; static float dy0 = 0.0f; static uint32_t dy_gen = 0xFFFFFFFFu; static bool seen2 = false;
      static const bool lay2_env = [] { const char* l = std::getenv("SIX_CSLAY"); return l && std::atoi(l) == 2; }(); const bool lay2 = lay2_env && six_live();
      const uint32_t scq = g_seen_charsel.obj; const bool sq = scq >= 0x10000u && scq < 0x30000000u;
      if (dy_gen != g_six_cs_gen) { dy_gen = g_six_cs_gen; dy0 = g_six_cs_dy; seen2 = false; }   /* the clone sets the offset again */
      uint32_t ctl[6] = { 9, 9, 9, 9, 9, 9 }; if (sq) for (int k = 0; k < 6; k++) { ctl[k] = six_cs_ctl(scq, k); if (ctl[k] == 1u) seen2 = true; }
      const int two = (!no2p && lay2 && seen2) ? 1 : 0;
      g_six_cs_dy = dy0 + (two ? g_six_2p_down : 0.0f);
      /* also when the flag was cleared behind our back (the copies let go at the screen's end store 0): a 2 Players screen
       * after a 2 Players match kept last2 = 1 and never set the flag again, so the copies sat at their 1 Player
       * place (the maintainer 2026-11-20; a 1 Player screen in between hid it) */
      if (two != last2 || g_six_2p.load() != two) { last2 = two; g_six_2p.store(two); for (int q = 0; q < 4; q++) g_six_park_k[q][2].store(0.0f);
          std::printf("[sixcs] this is the %s screen (slot controllers %u %u %u %u %u %u): copies kept %.0f px down\n", two ? "2 Players" : "1 Player", ctl[0], ctl[1], ctl[2], ctl[3], ctl[4], ctl[5], two ? g_six_2p_down : 0.0f); std::fflush(stdout); } }
    if (!g_six_cs_leaving) six_shadow_take(); else { if (six_shadow_lost()) six_shadow_restore("again while closing"); else if (g_six_sh_ok && g_six_sh_gen == g_six_cs_gen) six_shadow_dedupe(); }
    /* the text panes whose boxes sit on copies stay hidden: the game makes them visible again when it sets the panes
     * up as the screen closes, and the copy of such a pane would show its green plate for a frame */
    if (g_six_ph_gen == g_six_cs_gen) for (int k = 0; k < g_six_nph; k++) { const uint32_t pn = g_six_ph[k];
        if (vm_read32(pn) == 0x00882EF0u && (vm_read32(pn + 0x100) & 1u)) vm_write32(pn + 0x100, vm_read32(pn + 0x100) & ~1u); }
    { static const bool lr = std::getenv("SIX_CSLEAVEREC") != nullptr;   /* probe: what the game changes on the layout's objects when the screen starts to close */
      if (lr && g_six_cs_root_tb) { struct Obj { uint32_t p, w[8]; }; static Obj snap[2400]; static int nsnap = 0, lf = 0; static uint32_t rt[2], lgen = 0;
          auto grab = [](uint32_t q, uint32_t* w) { w[0] = vm_read32(q); w[1] = vm_read32(q + 4); w[2] = vm_read32(q + 0x108); w[3] = vm_read32(q + 0x100); w[4] = vm_read32(q + 0x10); w[5] = vm_read32(q + 0x14); w[6] = vm_read32(q + 0xE4); w[7] = vm_read32(q + 0xE8); };
          auto okp = [](uint32_t q) { return q >= 0x10000u && q < 0x30000000u; };
          if (lgen != g_six_cs_gen) { lgen = g_six_cs_gen; lf = 0; }
          if (!g_six_cs_leaving) { lf = 0; nsnap = 0; rt[0] = vm_read32(g_six_cs_root_tb + 0x08); rt[1] = vm_read32(g_six_cs_root_tb + 0x0C);
              static uint32_t st[2400]; int ns = 0;
              const uint32_t cnt = rt[1] >> 16;
              for (uint32_t r = 0; r < cnt && r < 600u; r++) for (uint32_t k = 0; k < 2; k++) { const uint32_t c = vm_read32(rt[0] + r * 12 + k * 4); if (okp(c) && ns < 2400) st[ns++] = c; }
              for (int qi = 0; qi < ns; qi++) { const uint32_t q = st[qi]; const uint32_t vt = vm_read32(q); if (vt != 0x00882EF0u && vt != 0x00882EC0u) continue;
                  if (nsnap < 2400) { snap[nsnap].p = q; grab(q, snap[nsnap].w); nsnap++; }
                  if (vt != 0x00882EF0u) continue;
                  const uint32_t tb = vm_read32(q + 0xE8); if (!okp(tb) || vm_read32(tb) != 0x00882E60u) continue;
                  const uint32_t ra = vm_read32(tb + 0x08), rn = vm_read32(tb + 0x0C) >> 16;
                  for (uint32_t r = 0; r < rn && r < 32; r++) for (uint32_t k = 0; k < 2; k++) { const uint32_t c = vm_read32(ra + r * 12 + k * 4); if (okp(c) && ns < 2400) st[ns++] = c; } } }
          else { if (lf == 0 || lf == 1 || lf == 2 || lf == 4 || lf == 8 || lf == 16) {
                  const uint32_t a0 = vm_read32(g_six_cs_root_tb + 0x08), a1 = vm_read32(g_six_cs_root_tb + 0x0C); int nch = 0, npr = 0;
                  std::printf("[leaverec] f %d: root table %08X array %08X -> %08X, count word %08X -> %08X, %d objects known\n", lf, g_six_cs_root_tb, rt[0], a0, rt[1], a1, nsnap);
                  for (int i = 0; i < nsnap; i++) { uint32_t w[8]; grab(snap[i].p, w); if (!std::memcmp(w, snap[i].w, sizeof w)) continue; nch++;
                      if (npr >= 400) continue; npr++;
                      int cp = -1, sr = -1; for (int q = 0; q < g_six_cs_npair; q++) { if (g_six_cs_pair[q][1] == snap[i].p) cp = q; if (g_six_cs_pair[q][0] == snap[i].p) sr = q; }
                      char nm[40]; nm[0] = 0; const uint32_t np = vm_read32(snap[i].p + 0x104); if (okp(np)) six_read_name(np, nm, 40);
                      char pn[40]; pn[0] = 0; if (okp(snap[i].w[1])) { const uint32_t pp = vm_read32(snap[i].w[1] + 0x104); if (okp(pp) && vm_read32(snap[i].w[1]) == 0x00882EF0u) six_read_name(pp, pn, 40); }
                      std::printf("[leaverec] f %d %08X '%s' (under '%s'; copy of pair %d, source of pair %d): vt %04X>%04X par %08X>%08X hash %08X>%08X vis %X>%X fl %08X>%08X tex %08X>%08X E4 %08X>%08X E8 %08X>%08X\n", lf, snap[i].p, nm, pn, cp, sr,
                                  snap[i].w[0] & 0xFFFFu, w[0] & 0xFFFFu, snap[i].w[1], w[1], snap[i].w[2], w[2], snap[i].w[3] & 0xFFu, w[3] & 0xFFu, snap[i].w[4], w[4], snap[i].w[5], w[5], snap[i].w[6], w[6], snap[i].w[7], w[7]);
                      std::memcpy(snap[i].w, w, sizeof w); }
                  { int gone = 0; const uint32_t cnt = a1 >> 16; for (uint32_t r = 285; r < cnt && r < 600u; r++) if (!vm_read32(a0 + r * 12) && !vm_read32(a0 + r * 12 + 4)) gone++;
                    std::printf("[leaverec] f %d: %d objects changed; root records from 285 on that hold nothing: %d of %u; slot number now %u\n", lf, nch, gone, cnt > 285u ? cnt - 285u : 0u, t_six_kind_slot); }
                  std::fflush(stdout); }
              lf++; } } }
    { static const bool nohold = std::getenv("SIX_CSNOHOLD") != nullptr;
      if (g_six_hold_gen != g_six_cs_gen) { g_six_hold_gen = g_six_cs_gen; g_six_hold_frames = 0; g_six_rest_ok = false; g_six_sup_nrest = 0; g_six_ref_pane[0] = g_six_ref_pane[1] = 0; g_six_don[0] = g_six_don[1] = 0; }
      g_six_hold_frames++;
      if (!g_six_rest_ok && g_six_hold_frames == 60 && !g_six_cs_leaving) {   /* rest values, once the slide in is over */
          const uint32_t list = vm_read32(g_six_cs.layout + 0x14);
          if (list >= 0x10000u && list < 0x30000000u) { const uint32_t arr = vm_read32(list + 0x08), cnt = vm_read32(list + 0x0C);
              for (uint32_t i = 0; i < cnt && i < 400u; i++) { const uint32_t pn = vm_read32(arr + i * 4); if (pn < 0x10000u || pn >= 0x30000000u || vm_read32(pn) != 0x00882EF0u) continue;
                  char nm[40]; nm[0] = 0; const uint32_t np = vm_read32(pn + 0x104); if (np >= 0x10000u && np < 0x30000000u) six_read_name(np, nm, 40);
                  const size_t L = std::strlen(nm); if (L < 4) continue;
                  if (!std::strcmp(nm, "p_com_star_000")) {   /* the leaders' star rows: the references for "the screen is sliding" (there is no p_waku_000 / _002 in this layout, a test run) */ g_six_ref_pane[0] = pn; g_six_ref_pos[0][0] = vm_read32(pn + 0x98); g_six_ref_pos[0][1] = vm_read32(pn + 0xA4); }
                  if (!std::strcmp(nm, "p_com_star_002")) { g_six_ref_pane[1] = pn; g_six_ref_pos[1][0] = vm_read32(pn + 0x98); g_six_ref_pos[1][1] = vm_read32(pn + 0xA4); }
                  const bool sup = std::strstr(nm, "support") != nullptr || !std::strcmp(nm + L - 4, "_004") || !std::strcmp(nm + L - 4, "_005");
                  if (six_fx_on() && (!std::strncmp(nm, "p_support_waku_hilight_", 23) || !std::strncmp(nm, "p_support_waku_effect_", 22))) continue;   /* they play, on the new boxes (g_six_fx) */
                  if (!sup || g_six_sup_nrest >= 64 || !std::strncmp(nm, "p_don_", 6)) continue;   /* not the don panes: they play, on the new boxes (a test run: p_don_005's label was held back here) */
                  SixRest& r = g_six_sup_rest[g_six_sup_nrest++]; r.pane = pn; r.name = np; for (uint32_t o = 0x90; o < 0xD4; o += 4) r.w[(o - 0x90) / 4] = vm_read32(pn + o); } }
          g_six_rest_ok = g_six_ref_pane[0] && g_six_ref_pane[1];
          { static const bool nodon = std::getenv("SIX_CSNODON") != nullptr; g_six_don[0] = g_six_don[1] = 0;
            const uint32_t rtb = g_six_cs_root_tb;
            if (!nodon && rtb && vm_read32(rtb + 0x08) == g_six_cs_root_arr) { const uint32_t ra = g_six_cs_root_arr, cw = vm_read32(rtb + 0x0C), n = cw >> 16;
                uint32_t don[6] = { 0, 0, 0, 0, 0, 0 }; int rec[6] = { -1, -1, -1, -1, -1, -1 };
                for (uint32_t r = 0; r < n && r < 600u; r++) for (uint32_t k = 0; k < 2; k++) { const uint32_t c = vm_read32(ra + r * 12 + k * 4); if (c < 0x10000u || c >= 0x30000000u) continue;
                    char nm[40]; nm[0] = 0; const uint32_t np = vm_read32(c + 0x104); if (np >= 0x10000u && np < 0x30000000u) six_read_name(np, nm, 40);
                    if (!std::strncmp(nm, "p_don_00", 8) && nm[8] >= '0' && nm[8] <= '5' && !nm[9]) { don[nm[8] - '0'] = c; rec[nm[8] - '0'] = (int)r; } }
                const bool plain = rec[4] >= 0 && rec[5] >= 0 && (vm_read32(ra + (uint32_t)rec[4] * 12 + 8) >> 16) == 0 && (vm_read32(ra + (uint32_t)rec[5] * 12 + 8) >> 16) == 0;
                if (don[1] && don[3] && don[4] && don[5] && plain) {
                    const uint32_t ra3 = six_game_new(ctx, (n + 2u) * 12u + 4u);
                    if (ra3) { for (uint32_t q = 0; q < n * 3u; q++) vm_write32(ra3 + q * 4, vm_read32(ra + q * 4));
                        for (uint32_t j = 0; j < 2; j++) { const uint32_t from = (uint32_t)rec[4 + j], to = n + j;
                            for (uint32_t q = 0; q < 3; q++) vm_write32(ra3 + to * 12 + q * 4, vm_read32(ra + from * 12 + q * 4));
                            vm_write32(ra3 + from * 12, 0u); vm_write32(ra3 + from * 12 + 4, 0u); }
                        vm_write32(rtb + 0x08, ra3); vm_write32(rtb + 0x0C, ((n + 2u) << 16) | (cw & 0xFFFFu)); g_six_cs_root_arr = ra3;
                        g_six_don[0] = don[4]; g_six_don[1] = don[5]; g_six_don_src[0] = don[1]; g_six_don_src[1] = don[3];
                        std::printf("[sixcs] don: p_don_004 / _005 (records %d, %d) moved to records %u, %u, drawn over the new boxes\n", rec[4], rec[5], n, n + 1u); } }
                else std::printf("[sixcs] don: panes not found as expected (%08X %08X %08X %08X, records %d %d): left on the Support plates\n", don[1], don[3], don[4], don[5], rec[4], rec[5]); } }
          for (int q = 0; q < 4; q++) g_six_fx[q] = g_six_fx_src[q] = 0;
          { const uint32_t rtb = g_six_cs_root_tb;
            if (six_fx_on() && rtb && vm_read32(rtb + 0x08) == g_six_cs_root_arr) { const uint32_t ra = g_six_cs_root_arr, cw = vm_read32(rtb + 0x0C), n = cw >> 16;
                static const char* const want[4] = { "p_support_waku_hilight_000", "p_support_waku_hilight_001", "p_support_waku_effect_000", "p_support_waku_effect_001" };
                static const char* const from[4] = { "p_waku_hilight_right_000", "p_waku_hilight_right_001", "p_waku_effect_001", "p_waku_effect_003" };
                uint32_t fx[4] = { 0, 0, 0, 0 }, src[4] = { 0, 0, 0, 0 }; int rec[4] = { -1, -1, -1, -1 };
                for (uint32_t r = 0; r < n && r < 600u; r++) for (uint32_t k = 0; k < 2; k++) { const uint32_t c = vm_read32(ra + r * 12 + k * 4); if (c < 0x10000u || c >= 0x30000000u) continue;
                    char nm[40]; nm[0] = 0; const uint32_t np = vm_read32(c + 0x104); if (np >= 0x10000u && np < 0x30000000u) six_read_name(np, nm, 40);
                    for (int q = 0; q < 4; q++) { if (!std::strcmp(nm, want[q]) && (vm_read32(ra + r * 12 + 8) >> 16) == 0) { fx[q] = c; rec[q] = (int)r; } if (!std::strcmp(nm, from[q])) src[q] = c; } }
                int nmove = 0; for (int q = 0; q < 4; q++) if (fx[q] && src[q]) nmove++;
                const uint32_t ra3 = nmove ? six_game_new(ctx, (n + (uint32_t)nmove) * 12u + 4u) : 0u;
                if (ra3) { for (uint32_t q = 0; q < n * 3u; q++) vm_write32(ra3 + q * 4, vm_read32(ra + q * 4));
                    uint32_t to = n;
                    for (int q = 0; q < 4; q++) { if (!fx[q] || !src[q]) continue;
                        const uint32_t fr = (uint32_t)rec[q];
                        for (uint32_t w = 0; w < 3; w++) vm_write32(ra3 + to * 12 + w * 4, vm_read32(ra + fr * 12 + w * 4));
                        vm_write32(ra3 + fr * 12, 0u); vm_write32(ra3 + fr * 12 + 4, 0u); to++;
                        g_six_fx[q] = fx[q]; g_six_fx_src[q] = src[q]; }
                    vm_write32(rtb + 0x08, ra3); vm_write32(rtb + 0x0C, (to << 16) | (cw & 0xFFFFu)); g_six_cs_root_arr = ra3; }
                std::printf("[sixcs] pick effects: flash %08X %08X, burst %08X %08X (sources %08X %08X %08X %08X), %d moved behind the copies\n", fx[0], fx[1], fx[2], fx[3], src[0], src[1], src[2], src[3], ra3 ? nmove : 0); } }
          std::printf("[sixcs] rest values kept: %d Support plate panes, leader star rows %08X / %08X\n", g_six_sup_nrest, g_six_ref_pane[0], g_six_ref_pane[1]); std::fflush(stdout); }
      { static uint32_t ggen = 0; static bool ever[2]; static int litf[2];
        const uint32_t scz = g_seen_charsel.obj; const bool sz = scz >= 0x10000u && scz < 0x30000000u;
        if (ggen != g_six_cs_gen) { ggen = g_six_cs_gen; ever[0] = ever[1] = false; litf[0] = litf[1] = 0; six_glow_have[0] = six_glow_have[1] = false; }
        for (int t = 0; t < 2; t++) { const int ssl = t == 0 ? 1 : 3; bool on = false; six_glow_cap[t] = false;
            if (sz) for (uint32_t pl = 0; pl < 2; pl++) { const uint32_t cr = scz + 0x3C8u + 0x1Cu * pl, tab = vm_read32(cr + 4), ci = vm_read32(cr + 8);
                if (tab >= 0x10000u && tab < 0x30000000u && ci < 64u && (int8_t)vm_base[tab + ci * 5u + 4u] == ssl) on = true; }
            if (on) { ever[t] = true; litf[t]++; } else litf[t] = 0;
            if (!six_glow_have[t] && sz && g_six_rest_ok && !g_six_cs_leaving && !g_six_cs_transition && (!ever[t] || litf[t] >= 30)) { six_glow_cap[t] = true; six_glow_have[t] = true;
                std::printf("[sixcs] new box %d: selector pose taken (%s)\n", 4 + t, ever[t] ? "neighbour lit for 30 frames" : "neighbour never lit yet"); std::fflush(stdout); } } }
      for (int t = 0; t < 2; t++) { const uint32_t d = g_six_don[t], sdn = g_six_don_src[t];   /* the don of slot 4 / 5 sits on the new box */
          if (!d || !sdn || !g_six_rest_ok || vm_read32(d) != 0x00882EF0u || vm_read32(sdn) != 0x00882EF0u) continue;
          for (uint32_t o = 0x90; o < 0xB4; o += 4) vm_write32(d + o, vm_read32(sdn + o));
          union { uint32_t w; float f; } x, y; x.w = vm_read32(d + 0x98); y.w = vm_read32(d + 0xA4); x.f += g_six_cs_dx; y.f += g_six_cs_dy; vm_write32(d + 0x98, x.w); vm_write32(d + 0xA4, y.w); }
      for (int q = 0; q < 4; q++) { const uint32_t d = g_six_fx[q], sdn = g_six_fx_src[q];   /* the flash / burst of slot 4 / 5 sits on the new box */
          if (!d || !sdn || !g_six_rest_ok || vm_read32(d) != 0x00882EF0u || vm_read32(sdn) != 0x00882EF0u) continue;
          for (uint32_t o = 0x90; o < 0xB4; o += 4) vm_write32(d + o, vm_read32(sdn + o));
          union { uint32_t w; float f; } x, y; x.w = vm_read32(d + 0x98); y.w = vm_read32(d + 0xA4); x.f += g_six_cs_dx; y.f += g_six_cs_dy;
          if (q >= 2) {   /* the burst is the Support plate's picture, drawn for a plate 0.6 of a box: made larger and set from the
                           * flash pane's place (the plate's burst sits 20,15 up-left of the plate's flash). SIX_CSFX="scalex scaley dx dy" to tune. */
              static float sc = 1.75f, scy = 2.35f, ox = -30.0f, oy = -30.0f; static bool rd = false;   /* a test run: 1.67 both ways covered only the top of the box */
              if (!rd) { rd = true; if (const char* e = std::getenv("SIX_CSFX")) std::sscanf(e, "%f %f %f %f", &sc, &scy, &ox, &oy); }
              const uint32_t hs = g_six_fx_src[q - 2];
              if (hs && vm_read32(hs) == 0x00882EF0u) { union { uint32_t w; float f; } hx, hy; hx.w = vm_read32(hs + 0x98); hy.w = vm_read32(hs + 0xA4); x.f = hx.f + g_six_cs_dx + ox; y.f = hy.f + g_six_cs_dy + oy; }
              union { uint32_t w; float f; } sx, sy; sx.w = vm_read32(d + 0x90); sy.w = vm_read32(d + 0xA0); sx.f *= sc; sy.f *= scy; vm_write32(d + 0x90, sx.w); vm_write32(d + 0xA0, sy.w); }
          vm_write32(d + 0x98, x.w); vm_write32(d + 0xA4, y.w); }
      /* (a) per team: is the copy to keep its transforms this frame? */
      { static uint32_t lastw[2][2] = { { 0xFFFFFFFFu, 0xFFFFFFFFu }, { 0xFFFFFFFFu, 0xFFFFFFFFu } }; static int since[2] = { 1000, 1000 };
        const uint32_t scz = g_seen_charsel.obj; const bool sz = scz >= 0x10000u && scz < 0x30000000u;
        for (int t = 0; t < 2; t++) { const int ssl = t == 0 ? 1 : 3; bool h = false;
            if (sz) { const uint32_t st = scz + 0x3Cu + 0x2Cu * (uint32_t)ssl; const uint32_t w0 = vm_read32(st + 4), w1 = vm_read32(st + 8);
                if (w0 != lastw[t][0] || w1 != lastw[t][1]) { if (lastw[t][0] != 0xFFFFFFFFu) since[t] = 0; lastw[t][0] = w0; lastw[t][1] = w1; }
                if (since[t] < 1000) since[t]++;
                if (since[t] < 45) h = true;
                for (uint32_t pl = 0; pl < 2; pl++) { const uint32_t cr = scz + 0x3C8u + 0x1Cu * pl, tab = vm_read32(cr + 4), ci = vm_read32(cr + 8);
                    if (tab >= 0x10000u && tab < 0x30000000u && ci < 64u && (int8_t)vm_base[tab + ci * 5u + 4u] == ssl) h = true; } }
            h = h && !nohold && g_six_rest_ok && !g_six_cs_leaving && !g_six_cs_transition;
            if (h != six_hold[t]) { static int n = 0; if (n++ < 16) { std::printf("[sixcs] new box %d: transforms %s\n", 4 + t, h ? "kept (cursor on its neighbour, or the neighbour just changed)" : "follow the neighbour"); std::fflush(stdout); } }
            six_hold[t] = h; } }
      { static const bool nofx = std::getenv("SIX_CSNOPICKFX") != nullptr; static uint32_t lastp[2][2] = { { 0xFFFFFFFFu, 0xFFFFFFFFu }, { 0xFFFFFFFFu, 0xFFFFFFFFu } }; static uint32_t fxgen = 0;
        const uint32_t scz = g_seen_charsel.obj; const bool sz = scz >= 0x10000u && scz < 0x30000000u;
        if (fxgen != g_six_cs_gen) { fxgen = g_six_cs_gen; six_pick_t[0] = six_pick_t[1] = -1; lastp[0][0] = lastp[1][0] = 0xFFFFFFFFu; }
        for (int t = 0; t < 2 && sz; t++) { const uint32_t st = scz + 0x3Cu + 0x2Cu * (uint32_t)(4 + t); const uint32_t w0 = vm_read32(st + 4), w1 = vm_read32(st + 8);
            if (six_pick_t[t] >= 0 && ++six_pick_t[t] > 13) six_pick_t[t] = -1;
            if (lastp[t][0] != 0xFFFFFFFFu && (w0 != lastp[t][0] || w1 != lastp[t][1]) && w0 == 0u && !nofx && g_six_rest_ok && !g_six_cs_leaving && !g_six_cs_transition) {
                six_pick_t[t] = 0; six_pick_q[t] = lastp[t][0] == 1u;
                static int said = 0; if (said++ < 6) { std::printf("[sixcs] new box %d: fighter %u picked (was mode %u): pick effect\n", 4 + t, w1, lastp[t][0]); std::fflush(stdout); } }
            lastp[t][0] = w0; lastp[t][1] = w1; } } }
    {   /* animated values onto the copies */
        uint32_t& boxes = g_six_cs_boxes; boxes = 0;
        { const uint32_t scr = g_seen_charsel.obj; const uint32_t vt = (scr >= 0x10000u && scr < 0x30000000u) ? vm_read32(scr) : 0u;
          const uint32_t d = (vt >= 0x10000u && vt < 0x30000000u) ? vm_read32(vt + 0x54) : 0u;
          if (d >= 0x10000u && d < 0x01000000u) { ppu_context c2 = *ctx; c2.gpr[3] = scr; c2.lr = 0; c2.gpr[2] = vm_read32(d + 4);
              ps3_indirect_call_to(&c2, vm_read32(d)); while (g_trampoline_fn) { auto f = g_trampoline_fn; g_trampoline_fn = 0; f(&c2); }
              const uint32_t ob = (uint32_t)c2.gpr[3]; if (ob >= 0x10000u && ob < 0x30000000u) boxes = vm_read32(ob + 0x18);
              { static int said = 0; if (said++ < 2) { std::printf("[sixcsbox] screen %08X vt+54 -> %08X, boxes %08X\n", scr, ob, (ob >= 0x10000u && ob < 0x30000000u) ? vm_read32(ob + 0x18) : 0u); std::fflush(stdout); } }
              if (boxes < 0x10000u || boxes >= 0x30000000u) boxes = 0; } }
        { static int built = -1; static uint32_t built_gen = 0; static std::unordered_map<uint32_t, uint32_t> copy_of;
          static const bool nosync = std::getenv("SIX_CSNOSYNC") != nullptr;
          /* per generation, not per count: copies made again have the same count, and the old map then gave the old
           * copies (white name bar on the blue new box after Stage Select, a test run) */
          /* Is a pair still what it was? The game takes pieces of a box away and makes them again when a slot gets a
           * fighter or its star row changes, and the pool hands old addresses to other pieces. A pair's source address
           * can then be a different object: after a pick on slot 3 the blue copy's star row received the copy of the
           * FACE node (its source address had become a star node), and a big face picture with the file's own image
           * was drawn under the new box (the maintainer 2026-11-18 22:18; a test run w2). So each pair
           * knows its parent pair, and a child pair counts only while its source's parent word (+0x04) is still the
           * parent pair's source and source and copy are the same class. A pair that fails mirrors nothing (it is fed
           * from itself) and is not handed out by the record sync. SIX_CSNOIDENT=1 turns the check off. */
          static int par[600]; static bool enforce[600]; static std::unordered_map<uint32_t, int> idx_of;
          static const bool noident = std::getenv("SIX_CSNOIDENT") != nullptr;
          if (built != g_six_cs_npair || built_gen != g_six_cs_gen) { built = g_six_cs_npair; built_gen = g_six_cs_gen; copy_of.clear(); idx_of.clear();
              int stk[16]; for (int& x : stk) x = -1;
              for (int i = 0; i < g_six_cs_npair && i < 600; i++) { copy_of[g_six_cs_pair[i][0]] = g_six_cs_pair[i][1]; idx_of[g_six_cs_pair[i][0]] = i;
                  const int d = g_six_cs_dep[i] < 15 ? g_six_cs_dep[i] : 15; par[i] = d > 0 ? stk[d - 1] : -1; stk[d] = i;
                  enforce[i] = par[i] >= 0 && vm_read32(g_six_cs_pair[i][0] + 4) == g_six_cs_pair[par[i]][0]; } }
          for (int i = 0; i < g_six_cs_npair && i < 600; i++) { const uint32_t a = g_six_cs_pair[i][0], b = g_six_cs_pair[i][1];
              bool v = noident || vm_read32(a) == vm_read32(b);
              if (v && !noident && par[i] >= 0) v = g_six_pair_valid[par[i]] && (!enforce[i] || vm_read32(a + 4) == g_six_cs_pair[par[i]][0]);
              if (v != g_six_pair_valid[i]) { static int n = 0; if (!v && n++ < 16) { std::printf("[sixcs] pair %d (depth %u): its source %08X is no longer the piece it was copied from: left alone\n", i, g_six_cs_dep[i], a); std::fflush(stdout); } }
              g_six_pair_valid[i] = v; }
          static int nsw = 0, nmiss = 0;
          /* six_frozen[t]: the source box of new box 4+t (slot 1 / 3) is not on Random. Its "?" node is then faded out
           * by the timeline (colour transform alpha 0, a test run pair 25) and the copy must not
           * follow. Not after Start (the slide out). SIX_CSNOFREEZE=1 turns it off.
           * A general hold of the copies' look was tried and dropped the same day: it also held the selector's own
           * pieces and broke the copies made after Stage Select (test runs). */
          { static const bool nofz = std::getenv("SIX_CSNOFREEZE") != nullptr;
            const uint32_t scz = g_seen_charsel.obj; const bool sz = scz >= 0x10000u && scz < 0x30000000u;
            for (int t = 0; t < 2; t++) six_frozen[t] = sz && !nofz && !g_six_cs_leaving && vm_read32(scz + 0x3Cu + 0x2Cu * (uint32_t)(t == 0 ? 1 : 3) + 4u) != 1u; }
          if (!nosync) for (int i = 0; i < g_six_cs_npair; i++) {
            if (g_six_cs_own[i]) continue;
            if (i < 600 && !g_six_pair_valid[i]) continue;
            const uint32_t a = g_six_cs_pair[i][0], b = g_six_cs_pair[i][1];
            const uint32_t ta = vm_read32(a + 0xE8), tb = vm_read32(b + 0xE8);
            if (ta < 0x10000u || ta >= 0x30000000u || tb < 0x10000u || tb >= 0x30000000u || ta == tb) continue;
            if (vm_read32(ta) != 0x00882E60u || vm_read32(tb) != 0x00882E60u) continue;
            const uint32_t n = vm_read32(ta + 0x0C) >> 16; if (n == 0 || n >= 64 || (vm_read32(tb + 0x0C) >> 16) != n) continue;
            const uint32_t ra = vm_read32(ta + 0x08), rb = vm_read32(tb + 0x08); if (ra < 0x10000u || rb < 0x10000u || ra == rb) continue;
            for (uint32_t r = 0; r < n; r++) for (uint32_t k = 0; k < 2; k++) {
                const uint32_t sa = vm_read32(ra + r * 12 + k * 4), cur = vm_read32(rb + r * 12 + k * 4);
                uint32_t want = 0;
                if (sa) { auto it = copy_of.find(sa); if (it == copy_of.end()) { if (nmiss++ < 8) { char nm[48]; nm[0] = 0; const uint32_t np = vm_read32(sa + 0x104); if (np >= 0x10000u && np < 0x30000000u) six_read_name(np, nm, 40);
                        std::printf("[sixcs] sync: record %u.%u of %08X holds %08X '%s' with no copy\n", r, k, a, sa, nm); std::fflush(stdout); } continue; }
                    want = it->second;
                    { auto ix = idx_of.find(sa); if (!noident && (ix == idx_of.end() || par[ix->second] != i || !g_six_pair_valid[ix->second])) continue; } }   /* a copy from another place: not handed out */
                if (want != cur) { vm_write32(rb + r * 12 + k * 4, want); if (nsw++ < 16) { std::printf("[sixcs] sync: copy %08X record %u.%u %08X -> %08X\n", b, r, k, cur, want); std::fflush(stdout); } } }
            vm_write32(tb + 0x0C, vm_read32(ta + 0x0C)); } }
        for (int i = 0; i < g_six_cs_npair; i++) { const uint32_t b = g_six_cs_pair[i][1], a = (i < 600 && !g_six_pair_valid[i]) ? b : g_six_cs_pair[i][0];   /* a pair that is no longer itself is fed from itself */
            /* the fl_* timelines rewrite panes by DEPTH every frame (file matrix +0x90..+0xB3, colour transform
             * +0xB4..+0xD3; charsel.md 7.14), copies sit at other depths and would keep stale values (a pre-fade
             * alpha keeps them invisible): mirror them from the source, the top copies moved by the layout offset */
            /* everything the timelines and the slot code drive: flags, material, runtime transform and colour, file
             * matrix and colour transform, visibility. This runs before slot 4/5's own code in the frame (slot 0's
             * first lookup), so what slot 4/5 sets on its copies (face, lock, stars) is written after it and wins. */
            if (g_six_cs_anim[i] && g_six_cs_dep[i] > 0) continue;   /* the copy's own timeline animates it */
            if (!g_six_cs_own[i]) {   /* groups slot 4/5's own code drives keep their material, flags and visibility */
                vm_write32(b + 0x10, vm_read32(a + 0x10));
                vm_write32(b + 0x14, vm_read32(a + 0x14));
                vm_write32(b + 0x100, vm_read32(a + 0x100)); }
            { static uint32_t tc[600][47], tcgen[600]; static bool in_glow3 = false;   /* 47 words: +0x18..+0xD3 as last taken from the source */
              if (g_six_cs_dep[i] == 0) in_glow3 = g_six_cs_glow[i] != 0;
              static uint32_t gp[600][47], gpgen[600]; static const bool glive = std::getenv("SIX_CSGLOWLIVE") != nullptr;
              const int tg = i < g_six_cs_npair / 2 ? 0 : 1;
              if (in_glow3 && i < 600 && !glive) {   /* the selector keeps one good pose */
                  if (six_glow_cap[tg]) { for (uint32_t o = 0x18; o < 0xD4; o += 4) gp[i][(o - 0x18) / 4] = vm_read32(a + o); gpgen[i] = g_six_cs_gen; }
                  if (gpgen[i] == g_six_cs_gen && !g_six_cs_leaving && !g_six_cs_transition) { for (uint32_t o = 0x18; o < 0xD4; o += 4) vm_write32(b + o, gp[i][(o - 0x18) / 4]); }
                  else { for (uint32_t o = 0x18; o < 0xD4; o += 4) vm_write32(b + o, vm_read32(a + o)); } }
              else if (i < 600 && six_hold[i < g_six_cs_npair / 2 ? 0 : 1] && !in_glow3 && tcgen[i] == g_six_cs_gen) { for (uint32_t o = 0x18; o < 0xD4; o += 4) vm_write32(b + o, tc[i][(o - 0x18) / 4]); }
              else { for (uint32_t o = 0x18; o < 0xD4; o += 4) { const uint32_t v = vm_read32(a + o); vm_write32(b + o, v); if (i < 600) tc[i][(o - 0x18) / 4] = v; }
                  if (i < 600) tcgen[i] = g_six_cs_gen; } }
            /* Two trees keep the words they had while the source slot was on Random ("plain"), +0x18..+0xD3:
             *  - the "?" plate (p_waku_defolt_bg_q and its node): on a source that has a fighter the timeline fades the "?"
             *    node out and slides it left (colour transform alpha 0; position moves too);
             *  - the face (p_rgb8_256_chara_ico_00N, p_chara_000 and its node): on a source with a fighter the node gets
             *    another shape and matrix (source E4 0x1D -> 0x25 and its record moves). The copy's
             *    node keeps shape 0x1D, and with the source's new matrix a picked new box showed a white, empty face
             *    (seen with both the partner and the new box picked).
             * While the source is on Random the words are remembered each frame; while it has a fighter they are written
             * back. If the copies were made with a fighter already there: the other team's partner box when that one is
             * on Random (deeper panes: matrix and colour, top pane: colour only), else for the "?" plate a plain colour
             * (multiply 1, add 0). Whether the "?" shows is slot 4 / 5's own Random state (the visible bit, further down). */
            { static int kind = -1, bk = 0; static uint32_t bq[2][2][8][47], bqgen = 0; static bool bqv[2][2] = { { false, false }, { false, false } };
              if (bqgen != g_six_cs_gen) { bqgen = g_six_cs_gen; bqv[0][0] = bqv[0][1] = bqv[1][0] = bqv[1][1] = false; }
              if (g_six_cs_dep[i] == 0) { static const uint32_t hq0 = six_name_hash("p_waku_defolt_bg_q_001x4"), hq1 = six_name_hash("p_waku_defolt_bg_q_003x5"),
                                                               hf0 = six_name_hash("p_rgb8_256_chara_ico_004"), hf1 = six_name_hash("p_rgb8_256_chara_ico_005");
                  const uint32_t hb = vm_read32(b + 0x108); kind = (hb == hq0 || hb == hq1) ? 0 : (hb == hf0 || hb == hf1) ? 1 : -1; bk = 0; }
              if (kind >= 0 && bk < 8) { const int half3 = g_six_cs_npair / 2, t3 = i < half3 ? 0 : 1;
                  if (!six_frozen[t3]) { if (!g_six_cs_leaving) { for (uint32_t o = 0x18; o < 0xD4; o += 4) bq[t3][kind][bk][(o - 0x18) / 4] = vm_read32(b + o); if (g_six_cs_dep[i] == 0) bqv[t3][kind] = true; } }
                  else if (bqv[t3][kind]) { for (uint32_t o = 0x18; o < 0xD4; o += 4) vm_write32(b + o, bq[t3][kind][bk][(o - 0x18) / 4]); }
                  else { const uint32_t oa = (g_six_cs_npair & 1) == 0 ? g_six_cs_pair[t3 == 0 ? i + half3 : i - half3][0] : 0u;
                      if (!six_frozen[1 - t3] && oa >= 0x10000u && oa < 0x30000000u && vm_read32(oa) == vm_read32(a)) { for (uint32_t o = g_six_cs_dep[i] > 0 ? 0x90u : 0xB4u; o < 0xD4; o += 4) vm_write32(b + o, vm_read32(oa + o)); }
                      else if (kind == 0) { for (uint32_t o = 0xB4; o < 0xC4; o += 4) vm_write32(b + o, 0x3F800000u); for (uint32_t o = 0xC4; o < 0xD4; o += 4) vm_write32(b + o, 0u); } }
                  { const int at = six_pick_t[t3];
                    if (at >= 0 && bk == 1) { union { uint32_t w; float f; } x, al; x.w = vm_read32(b + 0x98);
                        if (kind == 1) {   /* the face slides in */
                            static const float fx[14] = { 90.0f, 90.0f, 90.0f, 90.0f, 90.0f, 90.0f, 90.0f, 83.5f, 64.0f, 31.5f, -14.0f, -14.0f, 10.0f, 0.0f };
                            static const float fa[14] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0625f, 0.25f, 0.5625f, 1.0f, 1.0f, 1.0f, 1.0f };
                            x.f += fx[at]; al.f = fa[at]; vm_write32(b + 0x98, x.w); vm_write32(b + 0xC0, al.w); }
                        else if (kind == 0 && six_pick_q[t3]) {   /* the "?" slides out */
                            const int q = at < 7 ? 0 : at > 10 ? 4 : at - 6;
                            x.f += -22.5f * (float)q; al.f = 1.0f - 0.25f * (float)q; vm_write32(b + 0x98, x.w); vm_write32(b + 0xC0, al.w); } } }
                  bk++; } }
            /* team colour: the timeline's placement nodes (vt 0x00882EC0, the unnamed children of p_waku and of the
             * waku sides) carry the shape id at +0xE4 (red and blue frames are different shapes: slot 1 0x13/0x15/0x16,
             * slot 3 0x1D/0x17/0x18, a test run). The copy takes the id from the partner run whose
             * team matches slot 4's / slot 5's team: its own source, or the same node in the other partner's run. */
            if (!g_six_cs_own[i] && vm_read32(a) == 0x00882EC0u && (g_six_cs_npair & 1) == 0) {
                const int half = g_six_cs_npair / 2, t = i < half ? 0 : 1;
                const uint32_t scr = g_seen_charsel.obj; const bool so = scr >= 0x10000u && scr < 0x30000000u;
                auto team_of = [&](int sl) { return so ? vm_read32(scr + 0x3Cu + 0x2Cu * (uint32_t)sl + 0x0Cu) : vm_read32(setup_field(sl, 0x10)); };
                const bool same = (team_of(4 + t) != 0) == (team_of(t == 0 ? 1 : 3) != 0);
                const uint32_t from = same ? a : g_six_cs_pair[t == 0 ? i + half : i - half][0];
                if (vm_read32(from) == 0x00882EC0u) vm_write32(b + 0xE4, vm_read32(from + 0xE4)); }
            /* names: text boxes (screen vt+0x54 object +0x18 array, charsel.md 7.17): the copy of slot 1's / slot 3's
             * name box shows slot 4's / slot 5's name (+0x58 string, +0x5C type .. +0x6C) */
            if (boxes) { for (int bx = 0; bx < 2; bx++) if (a == vm_read32(boxes + (bx == 0 ? 1u : 3u) * 4u)) {
                const uint32_t from = vm_read32(boxes + (bx == 0 ? 4u : 5u) * 4u);
                if (from >= 0x10000u && from < 0x30000000u) for (uint32_t o = 0x58; o < 0x70; o += 4) vm_write32(b + o, vm_read32(from + o)); } }
            /* The selector (a test run). A lit glow tree: root visible (the game writes that
             * bit after this hook, every frame), child p_rgb8_waku_right visible, pulse child _a_waku_light_000_001
             * with flag 0x400 and an animated alpha (colour transform +0xC0). The game lights the SUPPORT's glow for
             * slot 4 / 5 (p_support_light_00t, root visible, pulse child _a_support_light_waku the same way). So the
             * copied glow is lit while the Support's glow root is visible, and takes the pulse from the Support's
             * pulse child; otherwise it is hidden (its source would light it with slot 1 / 3). The streak copy stays
             * hidden. SIX_CSGLOW=1 / SIX_CSGLOWMIRROR=1 keep the older behaviours. */
            { static const bool keep = std::getenv("SIX_CSGLOW") != nullptr || std::getenv("SIX_CSGLOWMIRROR") != nullptr;
              static bool grp_lit = false, grp_in = false; static int grp_t = 0;
              if (g_six_cs_dep[i] == 0) { grp_lit = false; grp_in = g_six_cs_glow[i] == 1; }
              if (!keep && g_six_cs_glow[i]) {
                  bool lit = false; const int t = i < g_six_cs_npair / 2 ? 0 : 1;
                  if (g_six_cs_glow[i] == 1) { const uint32_t sr = g_six_cs_sup_glow[t];
                      lit = sr && (vm_read32(sr + 0x100) & 1u);
                      /* The cursor decides (since 2026-11-18 evening): lit while a player's slot cursor is on slot 4 / 5.
                       * The Support root's bit is not steady: after a pick on the partner slot it read 0 with the cursor
                       * on the new box (copied root off, only the pulse piece showing).
                       * SIX_CSGLOWSUP=1 asks for the Support root's bit as well, as before. */
                      { const uint32_t scr2 = g_seen_charsel.obj; bool on_slot = false;
                        if (scr2 >= 0x10000u && scr2 < 0x30000000u) for (uint32_t pl = 0; pl < 2; pl++) { const uint32_t cr = scr2 + 0x3C8u + 0x1Cu * pl, tab = vm_read32(cr + 4), ci = vm_read32(cr + 8);
                            if (tab >= 0x10000u && tab < 0x30000000u && ci < 64u && (int8_t)vm_base[tab + ci * 5u + 4u] == 4 + t) on_slot = true; }
                        { static const bool gl = std::getenv("SIX_CSGLOWLOG") != nullptr; static uint32_t last[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu }; static int n = 0;   /* probe: what decides the copied selector, on change */
                          if (gl && n < 200 && scr2 >= 0x10000u && scr2 < 0x30000000u) { uint32_t cs[2];
                              for (uint32_t pl = 0; pl < 2; pl++) { const uint32_t cr = scr2 + 0x3C8u + 0x1Cu * pl, tab = vm_read32(cr + 4), ci = vm_read32(cr + 8);
                                  cs[pl] = (tab >= 0x10000u && tab < 0x30000000u && ci < 64u) ? (uint32_t)(uint8_t)vm_base[tab + ci * 5u + 4u] : 0xEEu; }
                              const uint32_t key = (cs[0] << 24) | (cs[1] << 16) | ((uint32_t)lit << 8) | (on_slot ? 2u : 0u) | (six_frozen[t] ? 1u : 0u);
                              if (key != last[t]) { last[t] = key; n++; std::printf("[sixglow] box %d: cursor slots %u / %u, Support root lit %d, cursor on it %d, held %d, raw cursor +0x38 %u\n", 4 + t, cs[0], cs[1], (int)lit, (int)on_slot, (int)six_frozen[t], vm_read32(scr2 + 0x38)); std::fflush(stdout); } } }
                        static const bool sup = std::getenv("SIX_CSGLOWSUP") != nullptr;
                        lit = on_slot && (lit || !sup); }
                      static int lastlit[2] = { -1, -1 };
                      if (lastlit[t] != (int)lit && std::getenv("SIX_CSLOG")) { lastlit[t] = lit; std::printf("[sixglow] new box %d selector %s\n", 4 + t, lit ? "on" : "off"); std::fflush(stdout); }
                      grp_lit = lit; grp_t = t; }
                  vm_write32(b + 0x100, (vm_read32(b + 0x100) & ~1u) | (lit ? 1u : 0u)); }
              else if (!keep && grp_in && !grp_lit && g_six_cs_dep[i] == 1) {
                  /* not lit: every piece under the root off. The root's own bit does not hide them, and they copy the
                   * source's, which shows them while the cursor is on slot 1 / 3 (the frame, and the pulse piece that
                   * blinks after X and Back: the maintainer's second selector) */
                  vm_write32(b + 0x100, vm_read32(b + 0x100) & ~1u); }
              else if (!keep && grp_lit && g_six_cs_dep[i] == 1) {
                  static const uint32_t h_right = six_name_hash("p_rgb8_waku_right"), h_pulse = six_name_hash("_a_waku_light_000_001");
                  const uint32_t h = vm_read32(a + 0x108);
                  static const uint32_t h_left = six_name_hash("p_rgb8_waku_left");
                  if (h == h_right) vm_write32(b + 0x100, vm_read32(b + 0x100) | 1u);
                  else if (h == h_left) vm_write32(b + 0x100, vm_read32(b + 0x100) & ~1u);   /* the source's loop shows it for a frame about once a second (the maintainer 11-18: the selector blinked) */
                  else if (h == h_pulse) { const uint32_t sp = g_six_cs_sup_pulse[grp_t];
                      static const bool pulse = std::getenv("SIX_CSPULSE") != nullptr;   /* drew as a grey rectangle (a test run): off */
                      if (pulse && sp) { vm_write32(b + 0x10, vm_read32(sp + 0x10)); for (uint32_t o = 0xB4; o < 0xD4; o += 4) vm_write32(b + o, vm_read32(sp + o)); }
                      vm_write32(b + 0x100, (vm_read32(b + 0x100) & ~1u) | (pulse ? 1u : 0u)); } } }
            bool top = false; for (int t = 0; t < g_six_cs_ntop && !top; t++) top = g_six_cs_top[t] == b;
            if (top) { union { uint32_t w; float f; } x, y; x.w = vm_read32(b + 0x98); y.w = vm_read32(b + 0xA4); x.f += g_six_cs_dx; y.f += g_six_cs_dy; vm_write32(b + 0x98, x.w); vm_write32(b + 0xA4, y.w); } } }
    {
        /* team colour of the copied frames: the game plays fl_red_on / fl_blue_on (labels in 0x009545F8 +0x54 / +0x58)
         * only on p_waku_00s for s <= 3; for the copies it is played here when slot 4/5's team changes */
        /* func_00335F00(pane, hash) = label index from func_00336CD8([pane+0xE4], hash) (table: [+4]+0xA u16 count, [+0xC]
         * entries of 0x14 bytes, +4 nonzero, +8 hash), then pane+0xF8 = index and pane+0xEC |= 4 (go-to request) */
        static uint32_t last_lab[2] = { 0, 0 }, last_copy[2] = { 0, 0 };
        const uint32_t scr = g_seen_charsel.obj; const bool scr_ok = scr >= 0x10000u && scr < 0x30000000u;
        for (int t = 0; t < 2; t++) { const int sl = 4 + t;
            /* live slot state while the screen is up: screen+0x3C+0x2C*s (+0x4 mode, +0xC team; charsel.md 7.17) */
            const uint32_t st = scr_ok ? scr + 0x3Cu + 0x2Cu * (uint32_t)sl : 0u;
            const int team = (int)(st ? vm_read32(st + 0x0C) : vm_read32(setup_field(sl, 0x10)));
            const int mode = (int)(st ? vm_read32(st + 0x04) : vm_read32(setup_field(sl, 0x08)));
            const uint32_t lab = vm_read32(0x009545F8u + (mode == 2 ? (team == 0 ? 0x64u : 0x68u) : (team == 0 ? 0x54u : 0x58u)));
            const uint32_t hw = six_name_hash(t == 0 ? "p_waku_001x4" : "p_waku_003x5");
            for (int q = 0; q < g_six_cs_npair; q++) { const uint32_t c = g_six_cs_pair[q][1]; if (vm_read32(c + 0x108) != hw) continue;
                if (lab == last_lab[t] && c == last_copy[t]) break;
                const uint32_t tab = vm_read32(c + 0xE4); int idx = -1;
                if (tab >= 0x10000u && tab < 0x30000000u) { const uint32_t hd = vm_read32(tab + 4); uint32_t e = vm_read32(tab + 0xC);
                    const int n = (hd >= 0x10000u && hd < 0x30000000u) ? (int)vm_read16(hd + 0xA) : 0;
                    for (int k = 0; k < n && k < 256; k++, e += 0x14) if (vm_read32(e + 4) != 0 && vm_read32(e + 8) == lab) { idx = k; break; } }
                { static int said = 0; if (said++ < 12) { std::printf("[sixcs] slot %d frame: team %d mode %d (setup %d/%d) label %08X -> index %d on copy %08X\n",
                      sl, team, mode, (int)vm_read32(setup_field(sl, 0x10)), (int)vm_read32(setup_field(sl, 0x08)), lab, idx, c); std::fflush(stdout); } }
                static const bool golab = std::getenv("SIX_CSLABEL") != nullptr;
                if (golab && idx >= 0) { vm_write32(c + 0xF8, (uint32_t)idx & 0xFFFFu); vm_write32(c + 0xEC, vm_read32(c + 0xEC) | 4u); }
                last_lab[t] = lab; last_copy[t] = c;
                break; } }
        /* slot cursor (charsel.md 7.5): each player's cursor record holds a pointer to the cell table (0x00758358+0x18,
         * cells of 5 bytes {left, right, up, down, slot}: a test run, Left took byte 0, Down
         * byte 3; charsel.md 7.5 had the pairs swapped) and the cell index after it. The 3v3 layout gets its own
         * table, per team row [new box][leader][partner]; the Support boxes are left out (locked):
         *   cell 0 slot 4 | cell 1 slot 0 | cell 2 slot 1
         *   cell 3 slot 5 | cell 4 slot 2 | cell 5 slot 3      (Left/Right wrap in the row, Up/Down swap rows) */
        { uint32_t& cells = g_six_cs_cells; static uint32_t cells_layout = 0; static int said = 0;
          static const bool off = std::getenv("SIX_CSNOCURSOR") != nullptr;
          const uint32_t scr = g_seen_charsel.obj;
          if (!off && scr >= 0x10000u && scr < 0x30000000u) {
              if (cells_layout != g_six_cs.layout) { cells_layout = g_six_cs.layout; cells = six_game_new(ctx, 0x20u);
                  static const uint8_t t[6][5] = { {2,1,3,3,4}, {0,2,4,4,0}, {1,0,5,5,1}, {5,4,0,0,5}, {3,5,1,1,2}, {4,3,2,2,3} };
                  if (cells) for (int k = 0; k < 30; k++) vm_base[cells + (uint32_t)k] = t[k / 5][k % 5]; }
              static const int cell_of_slot[6] = { 1, 2, 4, 5, 0, 3 };
              if (cells) for (uint32_t o = 0; o < 0x800u; o += 4) if (vm_read32(scr + o) == 0x00758370u) {
                  const uint32_t ci = vm_read32(scr + o + 4); const int sl = ci < 8 ? (int8_t)vm_base[0x00758370u + ci * 5 + 4] : -1;
                  const int nc = sl >= 0 && sl < 6 ? cell_of_slot[sl] : 1;
                  vm_write32(scr + o, cells); vm_write32(scr + o + 4, (uint32_t)nc);
                  if (said++ < 4) { std::printf("[sixcs] cursor record at screen+%X: cell %u (slot %d) -> new table %08X cell %d\n", o - 4, ci, sl, cells, nc); std::fflush(stdout); } }
              /* probe: watch the cursor records and the slot cursor (screen+0x38) change */
              if (std::getenv("SIX_CSLOG")) { static uint32_t last[5] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu }; static int nlog = 0;
                const uint32_t v[5] = { vm_read32(scr + 0x3CC), vm_read32(scr + 0x3D0), vm_read32(scr + 0x3E8), vm_read32(scr + 0x3EC), vm_read32(scr + 0x38) };
                if (std::memcmp(v, last, sizeof v) && nlog++ < 60) { std::memcpy(last, v, sizeof v);
                    std::printf("[sixcur] P1 table %08X cell %u | P2 table %08X cell %u | slot cursor %u | rec3C8:", v[0], v[1], v[2], v[3], v[4]);
                    for (uint32_t w = 0; w < 7; w++) std::printf(" %08X", vm_read32(scr + 0x3C8 + w * 4)); std::printf("\n"); std::fflush(stdout); } } } }
        if (std::getenv("SIX_CSLOG")) { /* the four settings windows (vt 0x0087C4A0 / C570 / C640 / C6A8): where the screen keeps
             * them, and which one is open (+0x2C) */
            const uint32_t sc = g_seen_charsel.obj; static bool once = false; static uint32_t lastopen = 0xFFFFFFFFu;
            if (sc >= 0x10000u && sc < 0x30000000u) {
                uint32_t open = 0; int k = 0;
                for (uint32_t o = 0; o < 0x900u; o += 4) { const uint32_t w = vm_read32(sc + o);
                    if (w < 0x10000u || w >= 0x30000000u) continue; const uint32_t vt = vm_read32(w);
                    if (vt == 0x0087C4A0u || vt == 0x0087C570u || vt == 0x0087C640u || vt == 0x0087C6A8u) {
                        if (!once) std::printf("[sixwin] screen+%X -> window %08X vt %08X\n", o, w, vt);
                        if (vm_read32(w + 0x2C)) open |= 1u << k; k++; } }
                once = true;
                if (open != lastopen) { lastopen = open; std::printf("[sixwin] open mask %X (slot cursor %u)\n", open, vm_read32(sc + 0x38)); }
                std::fflush(stdout); } }
        { static bool done = false; if (!done && std::getenv("SIX_CSLOG")) { done = true; const uint32_t r2 = (uint32_t)ctx->gpr[2];
              for (int32_t o = -0x8000; o < 0x8000; o += 4) { const uint32_t v = vm_read32(r2 + (uint32_t)o);
                  if (v >= 0x0087C400u && v < 0x0087C700u) std::printf("[sixwin] TOC r2%+X = %08X\n", o, v); }
              for (uint32_t vt : { 0x0087C438u, 0x0087C4A0u, 0x0087C508u, 0x0087C570u, 0x0087C5D8u, 0x0087C640u, 0x0087C6A8u }) {
                  std::printf("[sixwin] vt %08X:", vt);
                  for (uint32_t q = 0; q < 26; q++) { const uint32_t d = vm_read32(vt + q * 4); std::printf(" %08X", (d >= 0x00800000u && d < 0x00A00000u) ? vm_read32(d) : d); }
                  std::printf("\n"); }
              std::fflush(stdout); } }
        /* SIX_CSGLOWDUMP=1 (probe): 30 frames after the slot cursor (screen+0x38) changes, dump the glow / streak
         * trees of slot 1 and of the red Support, to see what differs between lit and unlit */
        { static const bool gd = std::getenv("SIX_CSGLOWDUMP") != nullptr; static uint32_t lastc = 0xFFFFFFFFu; static int wait = -1, ndump = 0;
          const uint32_t sc = g_seen_charsel.obj;
          if (gd && sc >= 0x10000u && sc < 0x30000000u && g_six_cs.layout) { const uint32_t cur = vm_read32(sc + 0x38);
              if (cur != lastc) { lastc = cur; wait = 30; }
              if (wait >= 0 && wait-- == 0 && ndump++ < 8) {
                  const uint32_t list = vm_read32(g_six_cs.layout + 0x14), arr = vm_read32(list + 0x08), cnt = vm_read32(list + 0x0C);
                  std::printf("[wdump] ---- slot cursor %u ----\n", cur);
                  for (const char* nm : { "p_waku_light_001", "p_support_light_000", "p_waku_effect_001", "p_support_waku_effect_000" }) { const uint32_t h = six_name_hash(nm);
                      for (uint32_t i = 0; i < cnt && i < 600; i++) { const uint32_t pn = vm_read32(arr + i * 4); if (pn >= 0x10000u && pn < 0x30000000u && vm_read32(pn + 0x108) == h) { six_cs_wdump(pn, 0, nm); break; } } }
                  std::fflush(stdout); } } }
        /* SIX_CSSUPDUMP=1 (probe): 25 frames after the slot cursor changes, one line per pane whose name has "support" */
        { static const bool sd = std::getenv("SIX_CSSUPDUMP") != nullptr; static uint32_t lastc = 0xFFFFFFFFu, at = 0, fr = 0; fr++;
          const uint32_t sc = g_seen_charsel.obj;
          if (sd && sc >= 0x10000u && sc < 0x30000000u && g_six_cs.layout) { const uint32_t cur = vm_read32(sc + 0x38);
              if (cur != lastc) { lastc = cur; at = fr + 25; }
              if (at && fr == at) { at = 0; const uint32_t list = vm_read32(g_six_cs.layout + 0x14), arr = vm_read32(list + 0x08), cnt = vm_read32(list + 0x0C);
                  for (uint32_t i = 0; i < cnt && i < 700; i++) { const uint32_t pn = vm_read32(arr + i * 4); if (pn < 0x10000u || pn >= 0x30000000u) continue;
                      char nm[48]; nm[0] = 0; const uint32_t np = vm_read32(pn + 0x104); if (np >= 0x10000u && np < 0x30000000u) six_read_name(np, nm, 40);
                      if (!std::strstr(nm, "support")) continue;
                      std::printf("[supdump] cur %u %-34s vis %u fl %08X mat %08X ec %X f8 %X a %08X kids", cur, nm, vm_read32(pn + 0x100) & 1u, vm_read32(pn + 0x10), vm_read32(pn + 0x14), vm_read32(pn + 0xEC), vm_read32(pn + 0xF8), vm_read32(pn + 0xC0));
                      const uint32_t tb = vm_read32(pn + 0xE8);
                      if (tb >= 0x10000u && tb < 0x30000000u && vm_read32(tb) == 0x00882E60u) { const uint32_t ra = vm_read32(tb + 0x08), rn = vm_read32(tb + 0x0C) >> 16;
                          for (uint32_t r = 0; r < rn && r < 16; r++) for (uint32_t k = 0; k < 2; k++) { const uint32_t c = vm_read32(ra + r * 12 + k * 4); if (c >= 0x10000u && c < 0x30000000u) std::printf(" %u.%u:%s%X", r, k, vm_read32(c) == 0x00882EC0u ? "n" : "p", vm_read32(c) == 0x00882EC0u ? vm_read32(c + 0xE4) >> 16 : vm_read32(c + 0x100) & 1u); } }
                      std::printf("\n"); }
                  std::fflush(stdout); } } }
        /* SIX_CSTREE=<name,name,...> (probe): dump those panes' trees once, 300 frames after the screen is up */
        { static const char* tn = std::getenv("SIX_CSTREE"); static int fr = 0;
          if (tn && g_six_cs.layout && ++fr == 300) { const uint32_t list = vm_read32(g_six_cs.layout + 0x14), arr = vm_read32(list + 0x08), cnt = vm_read32(list + 0x0C);
              std::string all(tn); size_t p0 = 0;
              while (p0 <= all.size()) { const size_t p1 = all.find(',', p0); const std::string nm = all.substr(p0, p1 == std::string::npos ? std::string::npos : p1 - p0);
                  if (!nm.empty()) { const uint32_t h = six_name_hash(nm.c_str());
                      for (uint32_t i = 0; i < cnt && i < 700; i++) { const uint32_t pn = vm_read32(arr + i * 4); if (pn >= 0x10000u && pn < 0x30000000u && vm_read32(pn + 0x108) == h) { six_cs_wdump(pn, 0, nm.c_str()); break; } } }
                  if (p1 == std::string::npos) break; p0 = p1 + 1; }
              std::fflush(stdout); } }
        { static int fr = 0; static const bool wd = std::getenv("SIX_CSWDUMP") != nullptr;
          if (wd && ++fr == 240) { const uint32_t hw = six_name_hash("p_waku_003x5"), h1 = six_name_hash("p_waku_001x4");
              for (int q = 0; q < g_six_cs_npair; q++) { const uint32_t c = g_six_cs_pair[q][1]; if (vm_read32(c + 0x108) != hw && vm_read32(c + 0x108) != h1) continue;
                  six_cs_wdump(g_six_cs_pair[q][0], 0, "src"); six_cs_wdump(c, 0, "cpy"); }
              /* slot 1's run (top copies) against slot 3's run (bottom copies): what differs between the two sources */
              const int half = g_six_cs_npair / 2;
              std::printf("[wdiff] pairs %d\n", g_six_cs_npair);
              for (int i = 0; i < half; i++) { const uint32_t a = g_six_cs_pair[i][0], b = g_six_cs_pair[i + half][0];
                  char na[48], nb[48]; na[0] = nb[0] = 0; uint32_t np = vm_read32(a + 0x104); if (np >= 0x10000u && np < 0x30000000u) six_read_name(np, na, 40);
                  np = vm_read32(b + 0x104); if (np >= 0x10000u && np < 0x30000000u) six_read_name(np, nb, 40);
                  std::printf("[wdiff] %d '%s' / '%s' vt %08X/%08X:", i, na, nb, vm_read32(a), vm_read32(b));
                  for (uint32_t o = 0x08; o < 0x1E0; o += 4) { if (o >= 0x18 && o < 0x30) continue; if (o >= 0x90 && o < 0xB4) continue; if (o == 0x104 || o == 0x108 || o == 0x04) continue;
                      const uint32_t va = vm_read32(a + o), vb = vm_read32(b + o); if (va != vb) std::printf(" +%X %08X/%08X", o, va, vb); }
                  std::printf("\n"); }
              std::fflush(stdout); } }
        /* CPU stars of the new boxes. p_com_star_00s shows N stars by holding the star panes i_com_star_000_00k in
         * its records 5, 7, 9, 11, 13 (a test run: 3 stars = records 5/7/9). The copy was made with
         * one star and follows slot 1 / 3; here its records take slot 4's / 5's own COM level (screen state +0x14,
         * 0..4 = 1..5 stars; controller at state -4, 2 = CPU). A star pane the copy lacks is copied once from any
         * slot's star row that has it. SIX_CSNOSTARS=1 turns this off. */
        { static const bool off = std::getenv("SIX_CSNOSTARS") != nullptr;
          static uint32_t star[2][5], star_layout = 0; static int lastlv[2] = { -9, -9 };
          const uint32_t sc = g_seen_charsel.obj;
          if (!off && sc >= 0x10000u && sc < 0x30000000u && g_six_cs.layout) {
              if (star_layout != g_six_cs_gen) { star_layout = g_six_cs_gen; for (auto& r : star) for (uint32_t& x : r) x = 0; }
              const uint32_t list = vm_read32(g_six_cs.layout + 0x14), arr = vm_read32(list + 0x08), cnt = vm_read32(list + 0x0C);
              auto find = [&](const char* nm) -> uint32_t { const uint32_t h = six_name_hash(nm);
                  for (uint32_t i = 0; i < cnt && i < 700; i++) { const uint32_t pn = vm_read32(arr + i * 4); if (pn >= 0x10000u && pn < 0x30000000u && vm_read32(pn + 0x108) == h) return pn; } return 0u; };
              auto recs = [](uint32_t pn, uint32_t* n) -> uint32_t { const uint32_t tb = pn ? vm_read32(pn + 0xE8) : 0u;
                  if (tb < 0x10000u || tb >= 0x30000000u || vm_read32(tb) != 0x00882E60u) return 0u; *n = vm_read32(tb + 0x0C) >> 16; return vm_read32(tb + 0x08); };
              for (int t = 0; t < 2; t++) {
                  const uint32_t C = find(t == 0 ? "p_com_star_001x4" : "p_com_star_003x5"); uint32_t cn = 0; const uint32_t cr = recs(C, &cn);
                  if (!C || !cr || cn < 14) continue;
                  const uint32_t st = sc + 0x3Cu + 0x2Cu * (uint32_t)(4 + t);
                  const uint32_t rec = sc + 0x190u + 0x2Cu * (uint32_t)(4 + t);   /* the slot's setup record on the screen: +0x00 controller (2 = CPU), +0x18 COM level */
                  (void)rec; const int ctl = (int)six_cs_ctl(sc, 4 + t), lv = (int)vm_read32(st + 0x14);
                  if (lv != lastlv[t]) { lastlv[t] = lv; if (std::getenv("SIX_CSLOG")) { std::printf("[sixstar] slot %d: controller %d, COM level %d\n", 4 + t, ctl, lv); std::fflush(stdout); } }
                  const int stars = lv < 0 ? 0 : lv > 4 ? 5 : lv + 1;
                  vm_write32(C + 0x100, (vm_read32(C + 0x100) & ~1u) | (ctl == 2 ? 1u : 0u));   /* the star row shows for a CPU slot only */
                  for (int k = 0; k < 5; k++) { const uint32_t ri = 5u + 2u * (uint32_t)k;
                      if (!star[t][k]) {   /* a copy of star k: the one the copy row already holds, or a new copy from a source row */
                          if (k == 0 && vm_read32(cr + ri * 12 + 4)) star[t][k] = vm_read32(cr + ri * 12 + 4);
                          else if (k < stars) for (const char* sn : { "p_com_star_001", "p_com_star_003", "p_com_star_000", "p_com_star_002" }) {
                              uint32_t n2 = 0; const uint32_t S = find(sn), sr = recs(S, &n2); const uint32_t X = (sr && ri < n2) ? vm_read32(sr + ri * 12 + 4) : 0u;
                              if (X < 0x10000u || X >= 0x30000000u) continue;
                              g_six_cs_nopair = true; star[t][k] = six_cs_copy_tree(ctx, X, C, t == 0 ? "x4" : "x5", t, 1); g_six_cs_nopair = false;
                              if (star[t][k]) { std::printf("[sixstar] slot %d: star %d copied from %s (%08X -> %08X)\n", 4 + t, k + 1, sn, X, star[t][k]); std::fflush(stdout); }
                              break; }
                          /* no row shows star k yet (all at fewer stars): copy star 2's pane and step it along the
                           * row by the spacing of stars 1 and 2 (file position +0x98 / +0xA4) */
                          if (!star[t][k] && k >= 2 && k < stars && star[t][0] && star[t][1]) {
                              g_six_cs_nopair = true; star[t][k] = six_cs_copy_tree(ctx, star[t][1], C, t == 0 ? "x4" : "x5", t, 1); g_six_cs_nopair = false;
                              if (star[t][k]) { union { uint32_t w; float f; } x0, x1, y0, y1, x, y;
                                  x0.w = vm_read32(star[t][0] + 0x98); x1.w = vm_read32(star[t][1] + 0x98); y0.w = vm_read32(star[t][0] + 0xA4); y1.w = vm_read32(star[t][1] + 0xA4);
                                  x.f = x0.f + (float)k * (x1.f - x0.f); y.f = y0.f + (float)k * (y1.f - y0.f);
                                  vm_write32(star[t][k] + 0x98, x.w); vm_write32(star[t][k] + 0xA4, y.w);
                                  std::printf("[sixstar] slot %d: star %d made from star 2 at %.1f %.1f (step %.1f)\n", 4 + t, k + 1, x.f, y.f, x1.f - x0.f); std::fflush(stdout); } } }
                      vm_write32(cr + ri * 12 + 4, (k < stars && star[t][k]) ? star[t][k] : 0u); } } } }
        { static const bool bl = std::getenv("SIX_CSBOXLOG") != nullptr; static uint32_t lastw[2][28]; static int n = 0;
          if (bl && g_six_cs_boxes && n < 40) for (int t = 0; t < 2; t++) { const uint32_t bx = vm_read32(g_six_cs_boxes + (uint32_t)(4 + t) * 4u); if (bx < 0x10000u || bx >= 0x30000000u) continue;
              uint32_t w[28]; for (uint32_t o = 0; o < 28; o++) w[o] = vm_read32(bx + o * 4u); w[6] = w[7] = 0;
              if (std::memcmp(w, lastw[t], sizeof w)) { std::memcpy(lastw[t], w, sizeof w); n++; std::printf("[sixbox] gen %u box %d %08X:", g_six_cs_gen, 4 + t, bx); for (uint32_t o = 0; o < 28; o++) std::printf(" %08X", w[o]); std::printf("\n"); std::fflush(stdout); } } }
        { static const bool rd = std::getenv("SIX_CSRUNDUMP") != nullptr; static uint32_t lg = 0, at = 0, fr = 0; fr++;
          if (rd) { if (lg != g_six_cs_gen) { lg = g_six_cs_gen; at = fr + 200; }
              if (at && fr == at) { at = 0; auto fl = [](uint32_t a) { union { uint32_t w; float f; } u; u.w = vm_read32(a); return u.f; };
                  for (int i = g_six_cs_npair / 2; i < g_six_cs_npair; i++) { if (g_six_cs_dep[i] > 1) continue; const uint32_t a2 = g_six_cs_pair[i][0], b2 = g_six_cs_pair[i][1];
                      char nm[48]; nm[0] = 0; const uint32_t np = vm_read32(b2 + 0x104); if (np >= 0x10000u && np < 0x30000000u) six_read_name(np, nm, 40);
                      std::printf("[rundump] gen %u pair %d dep %u %-28s vt %08X vis %u/%u rt-alpha %.2f/%.2f ct-a %.2f/%.2f pos %.0f %.0f E4 %08X/%08X ec %X f8 %X\n", g_six_cs_gen, i, g_six_cs_dep[i], nm, vm_read32(b2),
                                  vm_read32(b2 + 0x100) & 1u, vm_read32(a2 + 0x100) & 1u, fl(b2 + 0x3C), fl(a2 + 0x3C), fl(b2 + 0xC0), fl(a2 + 0xC0), fl(b2 + 0x18), fl(b2 + 0x1C), vm_read32(b2 + 0xE4), vm_read32(a2 + 0xE4), vm_read32(a2 + 0xEC), vm_read32(a2 + 0xF8)); }
                  std::fflush(stdout); } } }
        /* SIX_CSVISLOG=1 (probe): named copies and the Support panes whose visible bit or material changes */
        { static const bool vl = std::getenv("SIX_CSVISLOG") != nullptr; static uint32_t lastv[460], lastm[460], lastg = 0xFFFFFFFFu; static int n = 0;
          if (vl && n < 300) { if (lastg != g_six_cs_gen) { lastg = g_six_cs_gen; std::memset(lastv, 0xFF, sizeof lastv); std::memset(lastm, 0xFF, sizeof lastm); }
              const uint32_t list = vm_read32(g_six_cs.layout + 0x14), arr = vm_read32(list + 0x08), cnt = vm_read32(list + 0x0C);
              for (uint32_t i = 0; i < cnt && i < 460; i++) { const uint32_t pn = vm_read32(arr + i * 4); if (pn < 0x10000u || pn >= 0x30000000u) continue;
                  char nm[48]; nm[0] = 0; const uint32_t np = vm_read32(pn + 0x104); if (np >= 0x10000u && np < 0x30000000u) six_read_name(np, nm, 40);
                  const size_t L = std::strlen(nm);
                  const bool want = std::strstr(nm, "support") || (L > 2 && (!std::strcmp(nm + L - 2, "x4") || !std::strcmp(nm + L - 2, "x5") || !std::strcmp(nm + L - 3, "004") || !std::strcmp(nm + L - 3, "005")));
                  if (!want) continue;
                  const uint32_t v = vm_read32(pn + 0x100) & 1u, m = vm_read32(pn + 0x14);
                  if (v != lastv[i] || m != lastm[i]) { lastv[i] = v; lastm[i] = m; if (n++ < 300) std::printf("[sixvis] gen %u slotcur %u %-32s %08X vis %u mat %08X\n", g_six_cs_gen, vm_read32(g_seen_charsel.obj + 0x38), nm, pn, v, m); } }
              std::fflush(stdout); } }
        /* locked Support boxes: their "1P/2P" icon is no longer driven (slot 4/5's icon is on the copies), so it is
         * hidden; the padlock stays (SIX_CSSUPICO=1 keeps the icon) */
        { static const bool keep = std::getenv("SIX_CSSUPICO") != nullptr;
          if (!keep) for (uint32_t ic : g_six_cs_sup_ico) if (ic) vm_write32(ic + 0x100, vm_read32(ic + 0x100) & ~1u);
          static const bool hideface = std::getenv("SIX_CSSUPFACEHIDE") != nullptr;   /* off: hiding it also removed the plate's no-character picture */
          if (hideface) for (uint32_t fc : g_six_cs_sup_face) if (fc) vm_write32(fc + 0x100, vm_read32(fc + 0x100) & ~1u);
          { static const bool dr = std::getenv("SIX_CSDONREC") != nullptr;   /* probe: the "don" panes (p_don_00N and what hangs under them), frame by frame after slot 3 or slot 5 changes */
            static uint32_t dp[24], dprev[24][22], dlw[2][2] = { { 0xFFFFFFFFu, 0xFFFFFFFFu }, { 0xFFFFFFFFu, 0xFFFFFFFFu } }, dgen = 0; static int dn = 0, dfr = 1000, dcount = 0; static char dnm[24][40];
            const uint32_t scq = g_seen_charsel.obj;
            if (dr && scq >= 0x10000u && scq < 0x30000000u && dcount < 6) {
                if (dgen != g_six_cs_gen) { dgen = g_six_cs_gen; dn = 0; }
                if (dn == 0 && g_six_hold_frames >= 60 && (g_six_hold_frames % 30) == 0) {   /* collect (the names are not there in the first frames) */
                    /* the don panes are root records (206..216 in a test run), not in the flat pane list */
                    const uint32_t arr = g_six_cs_root_tb ? vm_read32(g_six_cs_root_tb + 0x08) : 0u, cnt = g_six_cs_root_tb ? (vm_read32(g_six_cs_root_tb + 0x0C) >> 16) * 3u : 0u;
                    for (uint32_t i = 0; i < cnt && i < 1200u && dn < 24; i++) { if (i % 3u == 2u) continue; const uint32_t pn = vm_read32(arr + i * 4); if (pn < 0x10000u || pn >= 0x30000000u) continue;
                        char nm[40]; nm[0] = 0; const uint32_t np = vm_read32(pn + 0x104); if (np >= 0x10000u && np < 0x30000000u) six_read_name(np, nm, 40);
                        if (std::strncmp(nm, "p_don_", 6)) continue;
                        uint32_t st[8]; int ns = 0, qi = 0; st[ns++] = pn;
                        for (; qi < ns && dn < 24; qi++) { const uint32_t q = st[qi]; dp[dn] = q; std::snprintf(dnm[dn], 40, "%s%s", qi == 0 ? "" : "  under ", nm); dn++;
                            const uint32_t tb = vm_read32(q + 0xE8); if (tb < 0x10000u || tb >= 0x30000000u || vm_read32(tb) != 0x00882E60u) continue;
                            const uint32_t ra = vm_read32(tb + 0x08), rn = vm_read32(tb + 0x0C) >> 16;
                            for (uint32_t r = 0; r < rn && r < 8; r++) for (uint32_t k = 0; k < 2; k++) { const uint32_t c = vm_read32(ra + r * 12 + k * 4); if (c >= 0x10000u && c < 0x30000000u && ns < 8) st[ns++] = c; } } }
                    std::printf("[donrec] frame %d: %d don panes and children tracked (root table %08X, %u records)\n", g_six_hold_frames, dn, g_six_cs_root_tb, cnt / 3u); }
                auto grab = [&](int i, uint32_t* o) { const uint32_t q = dp[i]; for (int k = 0; k < 17; k++) o[k] = vm_read32(q + 0x90u + 4u * (uint32_t)k); o[17] = vm_read32(q + 0x100) & 1u; o[18] = vm_read32(q + 0xE4); o[19] = vm_read32(q + 0x10); o[20] = vm_read32(q + 0xF8); o[21] = vm_read32(q + 0x14); };
                for (int w = 0; w < 2; w++) { const uint32_t st = scq + 0x3Cu + 0x2Cu * (w == 0 ? 3u : 5u); const uint32_t w0 = vm_read32(st + 4), w1 = vm_read32(st + 8);
                    if (w0 != dlw[w][0] || w1 != dlw[w][1]) { if (dlw[w][0] != 0xFFFFFFFFu) { dfr = 0; dcount++; std::printf("[donrec] slot %d mode %u -> %u, character %u -> %u\n", w == 0 ? 3 : 5, dlw[w][0], w0, dlw[w][1], w1); } dlw[w][0] = w0; dlw[w][1] = w1; } }
                if (dfr == 1000) { for (int i = 0; i < dn; i++) grab(i, dprev[i]); }
                else if (dfr < 50) { auto ff = [](uint32_t w) { float f; std::memcpy(&f, &w, 4); return f; };
                    for (int i = 0; i < dn; i++) { uint32_t cur[22]; grab(i, cur);
                        if (dfr == 0 || std::memcmp(cur, dprev[i], sizeof cur)) {
                            std::printf("[donrec] f %2d %-34s %08X vt %04X: m %.3f %.3f %.2f | %.3f %.3f %.2f  mul %.2f %.2f %.2f %.2f  vis %u E4 %08X fl %08X f8 %X tex %08X\n", dfr, dnm[i], dp[i], vm_read32(dp[i]) & 0xFFFFu,
                                        ff(cur[0]), ff(cur[1]), ff(cur[2]), ff(cur[3]), ff(cur[4]), ff(cur[5]), ff(cur[9]), ff(cur[10]), ff(cur[11]), ff(cur[12]), cur[17], cur[18], cur[19], cur[20], cur[21]);
                            std::memcpy(dprev[i], cur, sizeof cur); } }
                    dfr++; if (dfr == 50) { dfr = 1000; std::fflush(stdout); } } } }
          { static const bool pr = std::getenv("SIX_CSPICKREC") != nullptr;   /* probe: what the game's pick effect does to slot 3's box, frame by frame (source side of the blue pairs) */
            static uint32_t lw[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu }, prev[80][20]; static int fr = 1000, nrec = 0;
            const uint32_t scq = g_seen_charsel.obj;
            if (pr && scq >= 0x10000u && scq < 0x30000000u && nrec < 3) { const uint32_t st = scq + 0x3Cu + 0x2Cu * 3u; const uint32_t w0 = vm_read32(st + 4), w1 = vm_read32(st + 8);
                const int half = g_six_cs_npair / 2;
                auto grab = [&](int i, uint32_t* o) { const uint32_t a2 = g_six_cs_pair[i][0]; for (int k = 0; k < 17; k++) o[k] = vm_read32(a2 + 0x90u + 4u * (uint32_t)k); o[17] = vm_read32(a2 + 0x100) & 1u; o[18] = vm_read32(a2 + 0xE4); o[19] = vm_read32(a2 + 0x10); };
                if (w0 != lw[0] || w1 != lw[1]) { if (lw[0] != 0xFFFFFFFFu) { fr = 0; nrec++; std::printf("[pickrec] slot 3 mode %u -> %u, character %u -> %u\n", lw[0], w0, lw[1], w1); } lw[0] = w0; lw[1] = w1; }
                if (fr == 1000) { for (int i = half; i < g_six_cs_npair && i - half < 80; i++) grab(i, prev[i - half]); }   /* idle: keep the last rest values */
                else if (fr < 40) { auto ff = [](uint32_t w) { float f; std::memcpy(&f, &w, 4); return f; };
                    for (int i = half; i < g_six_cs_npair && i - half < 80; i++) { uint32_t cur[20]; grab(i, cur);
                        if (fr == 0 || std::memcmp(cur, prev[i - half], sizeof cur)) { char nm[40]; nm[0] = 0; const uint32_t np = vm_read32(g_six_cs_pair[i][1] + 0x104); if (np >= 0x10000u && np < 0x30000000u) six_read_name(np, nm, 40);
                            std::printf("[pickrec] f %2d pair %2d dep %u '%s' vt %04X: m %.3f %.3f %.2f | %.3f %.3f %.2f  mul %.2f %.2f %.2f %.2f add %.2f %.2f %.2f %.2f  vis %u E4 %08X fl %08X\n", fr, i - half, g_six_cs_dep[i], nm, vm_read32(g_six_cs_pair[i][0]) & 0xFFFFu,
                                        ff(cur[0]), ff(cur[1]), ff(cur[2]), ff(cur[3]), ff(cur[4]), ff(cur[5]), ff(cur[9]), ff(cur[10]), ff(cur[11]), ff(cur[12]), ff(cur[13]), ff(cur[14]), ff(cur[15]), ff(cur[16]), cur[17], cur[18], cur[19]);
                            std::memcpy(prev[i - half], cur, sizeof cur); } }
                    fr++; if (fr == 40) { fr = 1000; std::fflush(stdout); } } } }
          { static const bool fl8 = std::getenv("SIX_CSF8LOG") != nullptr; static uint32_t last[2][6]; static int n = 0;   /* probe: the copied face clips' timeline words, on change */
            if (fl8 && n < 80) { static const uint32_t hf[2] = { six_name_hash("p_rgb8_256_chara_ico_004"), six_name_hash("p_rgb8_256_chara_ico_005") };
                const uint32_t scq = g_seen_charsel.obj; const bool sq = scq >= 0x10000u && scq < 0x30000000u;
                for (int i = 0; i < g_six_cs_npair; i++) { if (g_six_cs_dep[i] != 0) continue; const uint32_t a2 = g_six_cs_pair[i][0], b2 = g_six_cs_pair[i][1]; const uint32_t hb = vm_read32(b2 + 0x108);
                    const int t = hb == hf[0] ? 0 : hb == hf[1] ? 1 : -1; if (t < 0) continue;
                    const uint32_t tb = vm_read32(b2 + 0xE8); uint32_t rec0 = 0, rec2 = 0; if (tb >= 0x10000u && tb < 0x30000000u) { const uint32_t ra = vm_read32(tb + 0x08); rec0 = vm_read32(ra + 4); rec2 = vm_read32(ra + 28); }
                    const uint32_t v[6] = { vm_read32(b2 + 0xEC), vm_read32(b2 + 0xF8), vm_read32(a2 + 0xEC), vm_read32(a2 + 0xF8), rec0, rec2 };
                    if (std::memcmp(v, last[t], sizeof v)) { std::memcpy(last[t], v, sizeof v); n++;
                        std::printf("[sixf8] gen %u frame %d team %d: copy ec %X f8 %X rec0 %08X rec2 %08X | source ec %X f8 %X | modes src %u new %u, cursor +0x38 %u, slot number now %u\n", g_six_cs_gen, g_six_hold_frames, t,
                                    v[0], v[1], rec0, rec2, v[2], v[3], sq ? vm_read32(scq + 0x3Cu + 0x2Cu * (uint32_t)(t == 0 ? 1 : 3) + 4u) : 9u, sq ? vm_read32(scq + 0x3Cu + 0x2Cu * (uint32_t)(4 + t) + 4u) : 9u,
                                    sq ? vm_read32(scq + 0x38) : 9u, t_six_kind_slot); std::fflush(stdout); } } } }
          { static const bool td = std::getenv("SIX_CSTEXDUMP") != nullptr; static uint32_t seen_req = 0, nd = 0;   /* probe: blue copies and their sources, on R3 */
            if (td && seen_req != g_six_cs_dump_req.load()) { seen_req = g_six_cs_dump_req.load(); nd++;
                for (int i = 0; i < g_six_cs_npair; i++) { const uint32_t a2 = g_six_cs_pair[i][0], b2 = g_six_cs_pair[i][1];
                    char nm[40]; nm[0] = 0; const uint32_t np = vm_read32(b2 + 0x104); if (np >= 0x10000u && np < 0x30000000u) six_read_name(np, nm, 40);
                    if (g_six_cs_own[i]) { for (int side = 0; side < 2; side++) { const uint32_t pp = side == 0 ? a2 : b2;   /* own panes: every word */
                        std::printf("[textr] dump %u team %d pair %d %s:", nd, i < g_six_cs_npair / 2 ? 0 : 1, i % (g_six_cs_npair / 2), side == 0 ? "src" : "cpy");
                        for (uint32_t o = 0; o < 0x110; o += 4) std::printf(" %08X", vm_read32(pp + o)); std::printf("\n"); } }
                    std::printf("[textd] dump %u pair %d dep %u own %u glow %u '%s' vt %08X |src vis %u fl %08X tex %08X E4 %08X col", nd, i,
                                g_six_cs_dep[i], (unsigned)g_six_cs_own[i], (unsigned)g_six_cs_glow[i], nm, vm_read32(b2), vm_read32(a2 + 0x100) & 1u, vm_read32(a2 + 0x10), vm_read32(a2 + 0x14), vm_read32(a2 + 0xE4));
                    for (uint32_t o = 0xB4; o < 0xD4; o += 4) std::printf(" %08X", vm_read32(a2 + o));
                    std::printf(" |cpy vis %u fl %08X tex %08X E4 %08X col", vm_read32(b2 + 0x100) & 1u, vm_read32(b2 + 0x10), vm_read32(b2 + 0x14), vm_read32(b2 + 0xE4));
                    for (uint32_t o = 0xB4; o < 0xD4; o += 4) std::printf(" %08X", vm_read32(b2 + o));
                    { const uint32_t ta = vm_read32(a2 + 0xE8), tb2 = vm_read32(b2 + 0xE8);   /* which records hold something */
                      for (int side = 0; side < 2; side++) { const uint32_t tt = side == 0 ? ta : tb2; std::printf(side == 0 ? " |srec" : " |crec");
                          if (tt >= 0x10000u && tt < 0x30000000u && vm_read32(tt) == 0x00882E60u) { const uint32_t ra = vm_read32(tt + 0x08), rn = vm_read32(tt + 0x0C) >> 16;
                              for (uint32_t r = 0; r < rn && r < 16; r++) std::printf(" %c%c", vm_read32(ra + r * 12) ? 'A' : '.', vm_read32(ra + r * 12 + 4) ? 'B' : '.'); } } }
                    std::printf("\n"); }
                std::fflush(stdout); } }
          { static const bool sw = std::getenv("SIX_CSSUPWATCH") != nullptr; static int n = 0;   /* probe: the Support face pane and its tree, on change */
            if (sw && n < 400) { static uint32_t last[2][24][5];
                for (int t = 0; t < 2; t++) { uint32_t st[24]; int ns = 0, qi = 0; if (g_six_cs_sup_face[t]) st[ns++] = g_six_cs_sup_face[t];
                    for (; qi < ns; qi++) { const uint32_t pn = st[qi];
                        const uint32_t v[5] = { pn, vm_read32(pn + 0x100), vm_read32(pn + 0xE4), vm_read32(pn + 0x14), vm_read32(pn + 0xE0) };
                        if (std::memcmp(v, last[t][qi], sizeof v)) { std::memcpy(last[t][qi], v, sizeof v); n++;
                            char nm[40]; nm[0] = 0; const uint32_t np = vm_read32(pn + 0x104); if (np >= 0x10000u && np < 0x30000000u) six_read_name(np, nm, 40);
                            std::printf("[supwatch] gen %u plate %d node %d %08X '%s' vt %08X vis %08X +E4 %08X +14 %08X +E0 %08X\n", g_six_cs_gen, t, qi, pn, nm, vm_read32(pn), v[1], v[2], v[3], v[4]); }
                        { static uint32_t dg[2][24]; if (t == 0 && qi < 3 && dg[t][qi] != g_six_cs_gen) { dg[t][qi] = g_six_cs_gen;   /* whole node once per generation */
                              std::printf("[supdump] gen %u node %d %08X:", g_six_cs_gen, qi, pn); for (uint32_t o = 0; o < 0x120; o += 4) std::printf(" %08X", vm_read32(pn + o)); std::printf("\n"); } }
                        const uint32_t tb = vm_read32(pn + 0xE8); if (tb < 0x10000u || tb >= 0x30000000u || vm_read32(tb) != 0x00882E60u) continue;
                        const uint32_t ra = vm_read32(tb + 0x08), rn = vm_read32(tb + 0x0C) >> 16;
                        for (uint32_t r = 0; r < rn && r < 16; r++) for (uint32_t c2 = 0; c2 < 2; c2++) { const uint32_t ch = vm_read32(ra + r * 12 + c2 * 4);
                            if (ch >= 0x10000u && ch < 0x30000000u && ns < 24) st[ns++] = ch; } } }
                std::fflush(stdout); } }
          /* The Support plates get the no-character picture set by hand. The game no longer sets their face (slot 4/5's
           * setter func_001980C8 finds the copies), and after Stage Select the picture behind the plate's own shape was
           * a fighter face although the pane tree was word for word the same (a test run,
           * _1708-six-supdump). Same steps as func_001980C8: object = func_0032B204(layout, pane hash, p_chara_000),
           * name from the format at [r2-0x2C88] with id 302 (red) / 303 (blue), the team silhouettes,
           * texture = func_0051C0D8([screen+0x3F8], name), then texture at object+0x14 and bit 0 of +0x10
           * (func_00333FCC). SIX_CSSUPTEX=0 turns this off, SIX_CSSUPTEX=<n> uses picture id n. */
          { static int pics[2] = { 302, 303 };   /* red, blue silhouettes (a test run; 300 is black, 304/306 dark, 301/305 other art); "SIX_CSSUPTEX=a,b" */
            static const int pic = [] { const char* e = std::getenv("SIX_CSSUPTEX"); if (!e) return 1; pics[0] = pics[1] = std::atoi(e);
                                        const char* c = std::strchr(e, ','); if (c) pics[1] = std::atoi(c + 1); return pics[0]; }();
            static uint32_t forscr = 0, texs[2] = { 0, 0 }, buf = 0; static int said = 0;
            const uint32_t scr = g_seen_charsel.obj;
            if (pic > 0 && scr >= 0x10000u && scr < 0x30000000u && vm_read32(scr) == 0x0087C3D0u) {
                static uint32_t forgen = 0;   /* the pictures are loaded again after Stage Select: an old texture draws black (a test run) */
                const bool fetch = forscr != scr || !texs[0] || forgen != g_six_cs_gen; forgen = g_six_cs_gen;
                if (fetch) for (int t = 0; t < 2; t++) {
                    if (!buf) buf = six_game_new(ctx, 64);
                    char fmt[48]; fmt[0] = 0; const uint32_t fp = vm_read32((uint32_t)ctx->gpr[2] - 0x2C88u);
                    if (fp >= 0x10000u && fp < 0x30000000u) six_read_name(fp, fmt, 48);
                    if (std::strncmp(fmt, "_rgb8_256_chara_ico_", 20)) std::snprintf(fmt, sizeof fmt, "_rgb8_256_chara_ico_%%03d.png");
                    char nm[64]; std::snprintf(nm, sizeof nm, fmt, pics[t]);
                    if (buf) { for (int i = 0; i < 63 && (i == 0 || nm[i - 1]); i++) vm_base[buf + i] = (uint8_t)nm[i]; vm_base[buf + 63] = 0;
                        ppu_context c = *ctx; c.lr = 0; c.gpr[3] = vm_read32(scr + 0x3F8); c.gpr[4] = buf;
                        func_0051C0D8(&c); while (g_trampoline_fn) { auto f = g_trampoline_fn; g_trampoline_fn = 0; f(&c); }
                        texs[t] = (uint32_t)c.gpr[3]; forscr = scr;
                        std::printf("[sixcs] Support plate %d: picture '%s' -> texture %08X\n", t, nm, texs[t]); std::fflush(stdout); } }
                for (int t = 0; t < 2; t++) { const uint32_t tex = texs[t]; if (tex < 0x10000u || tex >= 0x30000000u) continue;
                    ppu_context c = *ctx; c.lr = 0; c.gpr[3] = g_six_cs.layout;
                    c.gpr[4] = six_name_hash(t == 0 ? "p_rgb8_256_chara_ico_004" : "p_rgb8_256_chara_ico_005") + 1u; c.gpr[5] = six_name_hash("p_chara_000");
                    func_0032B204(&c); while (g_trampoline_fn) { auto f = g_trampoline_fn; g_trampoline_fn = 0; f(&c); }
                    const uint32_t ob = (uint32_t)c.gpr[3]; if (ob < 0x10000u || ob >= 0x30000000u) continue;
                    if (vm_read32(ob + 0x14) != tex) { vm_write32(ob + 0x14, tex); vm_write32(ob + 0x10, vm_read32(ob + 0x10) | 1u);
                        if (said < 6) { said++; std::printf("[sixcs] Support plate %d: picture set on %08X\n", t, ob); std::fflush(stdout); } } } } }
          static const bool keepglow = std::getenv("SIX_CSSUPGLOW") != nullptr;   /* the locked Support boxes never light up */
          /* the game rewrites the roots' visible bit after this hook, so the roots' CHILDREN are hidden instead
           * (the root bit stays readable as "slot 4 / 5 is selected") */
          if (!keepglow) { for (int t = 0; t < 2; t++) for (int k = 0; k < g_six_cs_sup_nkids[t]; k++) six_cs_hide_tree(g_six_cs_sup_kids[t][k], 0);
              /* and the children the roots have NOW: pressing X on a new box brings in one that was not there when the
               * copies were made (_a_support_light_000_001, a test run; the maintainer: the Support box
               * flashes for about 0.1 s) */
              for (int t = 0; t < 2; t++) { const uint32_t sr = g_six_cs_sup_glow[t]; const uint32_t tb = sr ? vm_read32(sr + 0xE8) : 0u;
                  if (tb < 0x10000u || tb >= 0x30000000u || vm_read32(tb) != 0x00882E60u) continue;
                  const uint32_t ra = vm_read32(tb + 0x08), rn = vm_read32(tb + 0x0C) >> 16;
                  for (uint32_t r = 0; r < rn && r < 32; r++) for (uint32_t k = 0; k < 2; k++) { const uint32_t c = vm_read32(ra + r * 12 + k * 4);
                      if (c >= 0x10000u && c < 0x30000000u && vm_read32(c) == 0x00882EF0u) six_cs_hide_tree(c, 0); } }
              if (!six_fx_on()) for (int k = 2; k < 4; k++) if (g_six_cs_sup_glow[k]) six_cs_hide_tree(g_six_cs_sup_glow[k], 0);   /* the burst panes play on the new boxes now (g_six_fx) */
              for (int k = 0; k < 2; k++) if (g_six_cs_sup_glow[k]) vm_write32(g_six_cs_sup_glow[k] + 0x3C, 0u);
              /* the yellow frame itself is the placement node in record 0 of the glow root (shape 0x25, run
               * 2026-11-18_1421-six-supdump); hiding or moving it did nothing, so it is taken out of the record */
              for (int k = 0; k < 2; k++) { const uint32_t sr = g_six_cs_sup_glow[k]; const uint32_t tb = sr ? vm_read32(sr + 0xE8) : 0u;
                  if (tb < 0x10000u || tb >= 0x30000000u || vm_read32(tb) != 0x00882E60u) continue;
                  const uint32_t ra = vm_read32(tb + 0x08), rn = vm_read32(tb + 0x0C) >> 16;
                  for (uint32_t r = 0; r < rn && r < 32; r++) { const uint32_t c = vm_read32(ra + r * 12); if (c >= 0x10000u && c < 0x30000000u && vm_read32(c) == 0x00882EC0u) vm_write32(ra + r * 12, 0u); } } } }   /* roots: alpha only, the visible bit stays the game's */
        /* the "?" background (p_waku_defolt_bg_q) is set by the game only for slots 0-3: on the copies it shows when
         * slot 4/5 is on Random (setup record +0x08 state 1) and hides when a character is picked */
        for (int t = 0; t < 2; t++) {
            const uint32_t hq = six_name_hash(t == 0 ? "p_waku_defolt_bg_q_001x4" : "p_waku_defolt_bg_q_003x5");
            const uint32_t scq = g_seen_charsel.obj;   /* the live slot state (+0x4 mode 1 = random); the setup table lags after a return from Stage Select (a test run: white bubble on a picked slot) */
            const bool rnd = (scq >= 0x10000u && scq < 0x30000000u) ? vm_read32(scq + 0x3Cu + 0x2Cu * (uint32_t)(4 + t) + 4u) == 1u : vm_read32(setup_field(4 + t, 0x08)) == 1u;
            for (int q = 0; q < g_six_cs_npair; q++) { const uint32_t c = g_six_cs_pair[q][1]; if (vm_read32(c + 0x108) != hq) continue;
                const bool out = six_pick_t[t] >= 0 && six_pick_t[t] <= 10 && six_pick_q[t];   /* still sliding out (pick effect) */
                vm_write32(c + 0x100, (vm_read32(c + 0x100) & ~1u) | ((rnd || out) ? 1u : 0u)); break; } }
        /* names: the text boxes (vt 0x008832A8) sit at fixed screen positions (+0x18 x, +0x1C y in 1280x720, +0x50
         * width), not on panes (a test run). Slot 4/5's name boxes (4/5) go where the copied
         * plates are: slot 1's / slot 3's box position plus the copy offset, with their width. */
        /* the boxes are positioned every frame by the text binder (func_0033C5FC -> func_0033A864) from the layout's text
         * node that points at them (hardware watch, a test run). The copied text node of
         * slot 1's / slot 3's name still points at box 1 / 3: once, it is pointed at box 4 / 5 instead; it is drawn
         * after the Support's own node (higher depth), so the name lands on the copied plate. */
        { static uint32_t done_layout = 0;
          if (g_six_cs_boxes && done_layout != g_six_cs_gen) { done_layout = g_six_cs_gen; int nfix = 0;
              for (int t = 0; t < 2; t++) {
                  const uint32_t src = vm_read32(g_six_cs_boxes + (t == 0 ? 1u : 3u) * 4u), dst = vm_read32(g_six_cs_boxes + (t == 0 ? 4u : 5u) * 4u);
                  if (src < 0x10000u || src >= 0x30000000u || dst < 0x10000u || dst >= 0x30000000u) continue;
                  for (int q = 0; q < g_six_cs_npair; q++) { const uint32_t c = g_six_cs_pair[q][1]; const uint32_t sz = six_block_size(c);
                      for (uint32_t o = 0; o + 4 <= (sz ? sz : 0x1E0u) && o < 0x400u; o += 4) if (vm_read32(c + o) == src) { vm_write32(c + o, dst); nfix++; } } }
              std::printf("[sixcs] name boxes: %d pointers in the copies moved to box 4/5\n", nfix); std::fflush(stdout); } }
        static int dbg = 0; static const bool log = std::getenv("SIX_CSLOG") != nullptr;
        if (log && g_six_cs_boxes && dbg++ == 60) { for (uint32_t bx = 0; bx < 14; bx++) { const uint32_t o = vm_read32(g_six_cs_boxes + bx * 4); char str[40]; str[0] = 0;
                if (o >= 0x10000u && o < 0x30000000u) six_read_name(vm_read32(o + 0x58), str, 40);
                bool paired = false; for (int q = 0; q < g_six_cs_npair; q++) if (g_six_cs_pair[q][0] == o) paired = true;
                std::printf("[sixcsbox] box %u %08X type %08X '%s' copied %d\n", bx, o, o ? vm_read32(o + 0x5C) : 0u, str, (int)paired);
                if (o >= 0x10000u && o < 0x30000000u && (bx == 1 || bx == 4)) { std::printf("[sixcsbox]  box %u words:", bx); for (int w = 0; w < 40; w++) std::printf(" %08X", vm_read32(o + (uint32_t)w * 4)); std::printf("\n"); } } std::fflush(stdout); } }
}
/* func_0033A864(box, x, y) sets a text box's screen position (the binder calls it every frame). Slot 1's / slot 3's name
 * box position is remembered; slot 4's / slot 5's name box is put at that position plus the copy offset, so the
 * name shows on the copied plate (and no longer on the Support plate). */
/* A slot's controller on the Character Select screen (2 = CPU). The slot state is screen+0x3C+0x2C*s (+0x4 mode,
 * +0x8 character, +0xC team, +0x14 COM level); the controller word's place is SIX_CS_CTLOFF (default -4, a guess that
 * read 2 for CPU slots 4/5; screen+0x190 records read 0: a test run). SIX_CSLOG dumps the six states. */
static uint32_t six_cs_ctl(uint32_t scr, int sl)
{
    static const int off = [] { const char* e = std::getenv("SIX_CS_CTLOFF"); return e ? (int)std::strtol(e, nullptr, 0) : 0x10; }();   /* +0x10: slot 0 reads 0 (player), CPU slots 2; slot 4 went 2 -> 0 when set to Player 1 (a test run) */
    static uint32_t last[6][11]; static int n = 0;
    if (std::getenv("SIX_CSLOG") && n < 40) { uint32_t cur[11]; for (uint32_t o = 0; o < 11; o++) cur[o] = vm_read32(scr + 0x3Cu + 0x2Cu * (uint32_t)sl + o * 4u);
        if (std::memcmp(cur, last[sl], sizeof cur)) { std::memcpy(last[sl], cur, sizeof cur); n++; std::printf("[sixstate6] slot %d:", sl); for (uint32_t o = 0; o < 11; o++) std::printf(" %08X", cur[o]); std::printf("\n"); std::fflush(stdout); } }
    return vm_read32(scr + 0x3Cu + 0x2Cu * (uint32_t)sl + (uint32_t)off);
}
static thread_local int t_six_label_pass = -1;   /* 0 / 1 while the label of new box 4 / 5 is drawn a second time */
extern "C" void jsp_six_boxpos_enter(ppu_context* ctx)
{
    static const bool on_env = std::getenv("SIX_CSCLONE") != nullptr; const bool on = on_env && six_live();
    if (!on || !g_six_cs_boxes || !g_six_cs.layout) return;
    static float px[2] = { 0, 0 }, py[2] = { 0, 0 }; static bool have[2] = { false, false };
    const uint32_t b = (uint32_t)ctx->gpr[3];
    { static int n = 0; static const bool lg = std::getenv("SIX_CSLOG") != nullptr;
      if (lg && n < 40) { int bi = -1; for (int k = 0; k < 14; k++) if (vm_read32(g_six_cs_boxes + (uint32_t)k * 4u) == b) bi = k;
          if (bi >= 0) { n++; std::printf("[sixcsbp] box %d pos %.1f %.1f lr %08X\n", bi, (float)ctx->fpr[1], (float)ctx->fpr[2], (uint32_t)ctx->lr); std::fflush(stdout); } } }
    if (t_six_label_pass >= 0 && g_six_park_on) {   /* second pass of a label box for a new box (jsp_six_binder_leave) */
        const int t = t_six_label_pass; const float x = (float)ctx->fpr[1], y = (float)ctx->fpr[2];
        const float kx = g_six_park_lx - x, ky = g_six_park_y[t] - 14.0f - y;   /* the text draws 14 px below the box position (measured in a test run) */
        g_six_park_k[2 + t][0].store(kx); g_six_park_k[2 + t][1].store(ky); g_six_park_k[2 + t][2].store(1.0f);
        { static int n = 0; if (n++ < 6 && std::getenv("SIX_CSLOG")) { std::printf("[sixlab] label %d box pos %.1f %.1f -> %.1f %.1f (lr %08X)\n", 4 + t, x, y, x + kx, y + ky, (uint32_t)ctx->lr); std::fflush(stdout); } }
        ctx->fpr[1] = x + kx; ctx->fpr[2] = y + ky; return; }
    for (int t = 0; t < 2; t++) {
        if (b == vm_read32(g_six_cs_boxes + (t == 0 ? 1u : 3u) * 4u)) { px[t] = (float)ctx->fpr[1]; py[t] = (float)ctx->fpr[2]; have[t] = true; return; }
        if (b == vm_read32(g_six_cs_boxes + (t == 0 ? 4u : 5u) * 4u) && g_six_park_on) {
            const float x = (float)ctx->fpr[1], y = (float)ctx->fpr[2];
            const float kx = g_six_park_x - x, ky = g_six_park_y[t] - 15.0f - y;   /* the text draws 13 px (CPU slot) to 18 px (player slot) below: centred in the band */
            g_six_park_k[t][0].store(kx); g_six_park_k[t][1].store(ky); g_six_park_k[t][2].store(1.0f);
            ctx->fpr[1] = x + kx; ctx->fpr[2] = y + ky;
            { static int said = 0; if (said++ < 2) { std::printf("[sixcs] name box %d at %.1f %.1f parked at %.1f %.1f\n", 4 + t, x, y, x + kx, y + ky); std::fflush(stdout); } }
            return; } }
    (void)have;
}
static thread_local ppu_context t_six_bind_ctx; static thread_local bool t_six_bind_saved = false; static thread_local int t_six_bind_t = 0;
static uint32_t g_six_label_text[4][6]; static bool g_six_label_have[4] = { false, false, false, false };
/* func_0033C5FC(box, pane, ?, offset*, flags) places (and draws) a text box at a pane's world position + offset. The
 * Support name boxes 4/5 follow the Support plates; they are sent to the COPY of the pane slot 1's / slot 3's name
 * box follows (with that box's offset), so slot 4/5's name shows on the copied plate. */
/* A text box's pane in the layout (_f_c_cl00_...) is a placeholder: a flat green plate, visible in the file. The pane a
 * box is placed on each frame is hidden (visible bit off); a box sent to a copy leaves the pane it was meant for
 * alone, so that one stays as the file has it. Seen when the game set the panes up again as the screen closed (run
 * 2026-11-18_2301-six-leaverec: _f_c_cl00_003_000 / _001 and _f_c_cl00_001_001 / _003 visible from then on = the
 * green bars next to the Support plates and over the partner's star row, the maintainer 22:36), and now and then on entering
 * the screen (the green bars that stayed). The pane the box was meant for is hidden here. SIX_CSNOPHIDE=1: not. */
static void six_text_pane_hide(uint32_t pane)
{
    static const bool off = std::getenv("SIX_CSNOPHIDE") != nullptr;
    if (off || pane < 0x10000u || pane >= 0x30000000u || vm_read32(pane) != 0x00882EF0u) return;
    if (g_six_ph_gen != g_six_cs_gen) { g_six_ph_gen = g_six_cs_gen; g_six_nph = 0; }
    { bool have = false; for (int k = 0; k < g_six_nph; k++) have |= g_six_ph[k] == pane; if (!have && g_six_nph < 8) g_six_ph[g_six_nph++] = pane; }
    const uint32_t v = vm_read32(pane + 0x100);
    if (v & 1u) { vm_write32(pane + 0x100, v & ~1u);
        static int said = 0; if (said++ < 12) { char nm[40]; nm[0] = 0; const uint32_t np = vm_read32(pane + 0x104); if (np >= 0x10000u && np < 0x30000000u) six_read_name(np, nm, 40);
            std::printf("[sixcs] text pane '%s' (%08X) was visible while its box sits on a copy: hidden\n", nm, pane); std::fflush(stdout); } }
}

extern "C" void jsp_six_binder_enter(ppu_context* ctx)
{



    static const bool on_env = std::getenv("SIX_CSCLONE") != nullptr; const bool on = on_env && six_live();
    if (!on || !g_six_cs_boxes || !g_six_cs.layout) return;
    static uint32_t pane_of[2] = { 0, 0 }, off_of[2] = { 0, 0 };
    const uint32_t b = (uint32_t)ctx->gpr[3];
    /* Labels of the new boxes. Each slot s <= 3 has two label boxes: 6+s ("Player N", empty for a CPU slot) and 10+s
     * ("CPU", empty for a player slot) (box dump 2026-11-17: 6 'P...', 7-9 empty, 10-13 'C...'). The text draw is
     * deferred, so a box cannot be drawn twice (a test run); the partner slot's EMPTY label box is
     * borrowed instead: it gets the text for slot 4's / 5's controller and is placed on the copy of the pane that
     * kind of label sits on, parked like the names. SIX_CSNOLABEL=1 turns it off. */
    t_six_label_pass = -1;
    { static const bool lab = std::getenv("SIX_CSNOLABEL") == nullptr; const uint32_t scr = g_seen_charsel.obj;
      static uint32_t lab_pane[2][4], lab_off[2][4];
      if (lab && scr >= 0x10000u && scr < 0x30000000u) for (int k = 0; k < 2; k++) for (int sl = 0; sl < 4; sl++) {
          if (b != vm_read32(g_six_cs_boxes + (uint32_t)((k ? 10 : 6) + sl) * 4u)) continue;
          lab_pane[k][sl] = (uint32_t)ctx->gpr[4]; lab_off[k][sl] = (uint32_t)ctx->gpr[6];
          { static bool have_style[2][4]; static uint32_t style[2][4][14];   /* the box's own style: kept, and put back whenever the box shows its own label */
            if (!have_style[k][sl]) { have_style[k][sl] = true; for (uint32_t o = 0; o < 14; o++) style[k][sl][o] = vm_read32(b + 0x20u + o * 4u); }
            const uint32_t c0 = six_cs_ctl(scr, sl);
            if ((k == 1) == (c0 == 2u)) for (uint32_t o = 0; o < 14; o++) vm_write32(b + 0x20u + o * 4u, style[k][sl][o]); }
          auto ctl_of = [&](int s2) { return six_cs_ctl(scr, s2); };
          const uint32_t ctl = ctl_of(sl); const bool cpu = ctl == 2u;
          if (ctl < 4u && (k == 1) == cpu) { for (uint32_t o = 0; o < 6; o++) g_six_label_text[ctl][o] = vm_read32(b + 0x58u + o * 4u); g_six_label_have[ctl] = true; }
          if ((sl == 1 || sl == 3) && (k == 1) != cpu) {   /* this is the partner's empty label box */
              const int t = sl == 1 ? 0 : 1; const uint32_t c4 = ctl_of(4 + t);
              const int want = c4 == 2u ? 1 : 0; const uint32_t sp = lab_pane[want][sl];
              if (c4 < 4u && g_six_label_have[c4] && sp) {
                  uint32_t copy = 0; for (int q = 0; q < g_six_cs_npair; q++) if (g_six_cs_pair[q][0] == sp && (q < g_six_cs_npair / 2) == (t == 0)) { copy = g_six_cs_pair[q][1]; break; }
                  if (copy) { for (uint32_t o = 0; o < 6; o++) vm_write32(b + 0x58u + o * 4u, g_six_label_text[c4][o]);
                      const uint32_t wb = vm_read32(g_six_cs_boxes + (uint32_t)((want ? 10 : 6) + sl) * 4u); if (wb >= 0x10000u && wb < 0x30000000u) for (uint32_t o = 0x20; o < 0x58; o += 4) vm_write32(b + o, vm_read32(wb + o));   /* size, alignment, colour, width of that kind of label */
                      six_text_pane_hide((uint32_t)ctx->gpr[4]);
                      ctx->gpr[4] = copy; if (lab_off[want][sl]) ctx->gpr[6] = lab_off[want][sl];
                      t_six_label_pass = t;
                      static int said = 0; if (said++ < 2) { std::printf("[sixcs] label of new box %d: box %d borrowed, on copy %08X (controller %u)\n", 4 + t, (k ? 10 : 6) + sl, copy, c4); std::fflush(stdout); } } } }
          return; } }
    for (int t = 0; t < 2; t++) {
        if (b == vm_read32(g_six_cs_boxes + (t == 0 ? 1u : 3u) * 4u)) { pane_of[t] = (uint32_t)ctx->gpr[4]; off_of[t] = (uint32_t)ctx->gpr[6]; return; }
        if (b == vm_read32(g_six_cs_boxes + (t == 0 ? 4u : 5u) * 4u) && pane_of[t]) {
            for (int q = 0; q < g_six_cs_npair; q++) if (g_six_cs_pair[q][0] == pane_of[t]) {
                six_text_pane_hide((uint32_t)ctx->gpr[4]);
                ctx->gpr[4] = g_six_cs_pair[q][1]; if (off_of[t]) ctx->gpr[6] = off_of[t];
                const uint32_t src = vm_read32(g_six_cs_boxes + (t == 0 ? 1u : 3u) * 4u); vm_write32(b + 0x50, vm_read32(src + 0x50));
                vm_write32(b + 0x6C, vm_read32(src + 0x6C)); vm_write32(b + 0x4C, vm_read32(src + 0x4C));   /* also +0x6C (a pointer on the second visit, 0 on a fighter box); box +0x4C: 1 on a picked slot 4/5 after a return from Stage Select drew a white bar behind the name (a test run); a fighter slot's name box has 0 */
                static int said = 0; if (said++ < 2) { std::printf("[sixcs] name box %d follows copy %08X (of %08X)\n", 4 + t, g_six_cs_pair[q][1], pane_of[t]); std::fflush(stdout); }
                return; }
            return; } }
}

extern "C" void jsp_six_binder_leave(ppu_context* ctx)
{
    jsp_map_leave(ctx);
    t_six_label_pass = -1;
}
static thread_local uint32_t t_six_calc_layout = 0;

extern "C" void jsp_six_calc_enter(ppu_context* ctx)
{
    t_six_calc_layout = (uint32_t)ctx->gpr[3];

    if (!six_live()) return;
    if (!g_six_cs.layout || t_six_calc_layout != g_six_cs.layout || !g_six_rest_ok || g_six_hold_gen != g_six_cs_gen) return;
    static const bool nohold = std::getenv("SIX_CSNOHOLD") != nullptr;
    int moved = 0;
    for (int k = 0; k < 2; k++) { const uint32_t pn = g_six_ref_pane[k]; if (vm_read32(pn) != 0x00882EF0u) return;
        if (vm_read32(pn + 0x98) != g_six_ref_pos[k][0] || vm_read32(pn + 0xA4) != g_six_ref_pos[k][1]) moved++; }
    const bool tr = moved == 2;
    if (tr != g_six_cs_transition) { static int n = 0; if (n++ < 12) { std::printf("[sixcs] screen %s\n", tr ? "is sliding: nothing held" : "is at rest again"); std::fflush(stdout); } }
    g_six_cs_transition = tr;
    if (nohold || tr || g_six_cs_leaving) return;
    static int said = 0;
    for (int q = 0; q < g_six_sup_nrest; q++) { const SixRest& r = g_six_sup_rest[q];
        if (vm_read32(r.pane) != 0x00882EF0u || vm_read32(r.pane + 0x104) != r.name) continue;
        bool diff = false; for (uint32_t o = 0x90; o < 0xD4; o += 4) if (vm_read32(r.pane + o) != r.w[(o - 0x90) / 4]) { diff = true; vm_write32(r.pane + o, r.w[(o - 0x90) / 4]); }
        if (diff && said < 6) { said++; char nm[40]; nm[0] = 0; six_read_name(r.name, nm, 40); std::printf("[sixcs] Support plate pane '%s' moved by a timeline: put back\n", nm); std::fflush(stdout); } }
}

/* VSDUMP=1 (probe, the maintainer 2026-11-19: the VS screen for 3 vs 3; game map notes vs_screen.md): the layouts of the VS screen, found
 * by a pane named p_player_rd_000 (names part) or p_vs_chara_fr_000_000 (portraits part), each dumped once after it has
 * been calculated 40 times and again 150 calculations later (the rows have slid in by then): every pane's name, place,
 * scale, alpha, visible bit, its +0x04 / +0x08 words (parent / kind), clip and frame. */
static void vs_dump(uint32_t lay)
{
    static const bool on = std::getenv("VSDUMP") != nullptr;
    if (!on || lay < 0x10000u || lay >= 0x30000000u) return;
    struct L { uint32_t lay, calls; int kind; }; static L ls[8]; static int nl2 = 0;
    int li = 0; while (li < nl2 && ls[li].lay != lay) li++;
    if (li == nl2) {
        const uint32_t list0 = vm_read32(lay + 0x14); if (list0 < 0x10000u || list0 >= 0x30000000u) return;
        const uint32_t arr0 = vm_read32(list0 + 0x08), cnt0 = vm_read32(list0 + 0x0C); if (!cnt0 || cnt0 > 400u || arr0 < 0x10000u || arr0 >= 0x30000000u) return;
        static const uint32_t h1 = six_name_hash("p_player_rd_000"), h2 = six_name_hash("p_vs_chara_fr_000_000");
        int kind = 0;
        for (uint32_t i = 0; i < cnt0 && !kind; i++) { const uint32_t pn = vm_read32(arr0 + i * 4); if (pn < 0x10000u || pn >= 0x30000000u) continue;
            const uint32_t h = vm_read32(pn + 0x108); if (h == h1) kind = 1; else if (h == h2) kind = 2; }
        if (!kind || nl2 >= 8) return;
        ls[nl2].lay = lay; ls[nl2].calls = 0; ls[nl2].kind = kind; nl2++;
    }
    L& l = ls[li]; l.calls++;
    if (l.calls != 40u && l.calls != 190u) return;
    const uint32_t list = vm_read32(lay + 0x14), arr = vm_read32(list + 0x08), cnt = vm_read32(list + 0x0C);
    auto fl = [](uint32_t a2) { union { uint32_t w; float f; } u; u.w = vm_read32(a2); return u.f; };
    std::printf("[vsdump] layout %08X (%s) %u panes, calc %u\n", lay, l.kind == 1 ? "names" : "portraits", cnt, l.calls);
    for (uint32_t i = 0; i < cnt && i < 400u; i++) { const uint32_t pn = vm_read32(arr + i * 4); if (pn < 0x10000u || pn >= 0x30000000u) continue;
        char nm[40]; nm[0] = 0; const uint32_t np = vm_read32(pn + 0x104); if (np >= 0x10000u && np < 0x30000000u) six_read_name(np, nm, 40);
        std::printf("[vsdump] %3u %08X %-30s x %7.1f y %7.1f sc %.2f %.2f a %.2f vis %u w04 %08X w08 %08X tex %08X clip %08X fr %u\n", i, pn, nm, fl(pn + 0x98), fl(pn + 0xA4), fl(pn + 0x90), fl(pn + 0xA0),
                    fl(pn + 0xC0), vm_read32(pn + 0x100) & 1u, vm_read32(pn + 0x04), vm_read32(pn + 0x08), vm_read32(pn + 0x14), vm_read32(pn + 0xE4), vm_read32(pn + 0xF8)); }
    std::fflush(stdout);
}
/* VS screen for 3 vs 3 (the maintainer 2026-11-19; game map notes vs_screen.md). The names part has three rows per team; the third is
 * for position 2, and the scene puts units 4 / 5 at position 3 (Support, no row). When the part gets its table
 * (func_002F2E70 -> func_002F2DA8: r3 part, the table at part +0x30, team*0xA0 + pos*0x28 = { character, who drives, COM level }), position 3
 * is moved to position 2 for each team that has none there: the game then shows the third row, its 1P / COM picture and
 * its stars, and plays the three-row animation set. The name pictures: the game has four name packs (slots 0..3) at
 * part +0x178 + 4*i and takes them in filled-row order, which is wrong with six rows (and past the fourth it reads a
 * word that is no pointer). The lookup call (func_0051C0D8 from 0x002F2808: r3 pack, r4 file name with the character
 * number) gets the pack of the slot that has this character; a third fighter's name has no pack (the first pack is
 * given: the lookup finds nothing there). SIX_VSROWS=0 turns it off. */
static uint32_t g_vs_part = 0; static int32_t g_vs_slot_chr[4] = { -1, -1, -1, -1 };
static void vsn_ask(ppu_context* ctx, int t, int32_t chr);   /* below: the third fighters' name packs */
extern "C" void jsp_vs_names_enter(ppu_context* ctx)
{
    static const bool off = [] { const char* e = std::getenv("SIX_VSROWS"); return e && e[0] == '0'; }();
    g_vs_part = 0;
    if (off || !six_live()) return;
    const uint32_t part = (uint32_t)ctx->gpr[3], prm = part + 0x30u;   /* func_002F2E70 copied the table to part +0x30 and tail-calls func_002F2DA8 (hooked: 2E70 cannot carry a hook) */
    if (part < 0x10000u || part >= 0x30000000u || vm_read32(part) != 0x00882460u) return;
    /* the slots' characters as the game filled them: red position 0, 1 = slots 0, 1; blue position 0, 1 = slots 2, 3 */
    for (int t = 0; t < 2; t++) for (int ps = 0; ps < 2; ps++) g_vs_slot_chr[t * 2 + ps] = (int32_t)vm_read32(prm + (uint32_t)t * 0xA0u + (uint32_t)ps * 0x28u);
    int moved = 0;
    for (uint32_t t = 0; t < 2; t++) { const uint32_t p2 = prm + t * 0xA0u + 2u * 0x28u, p3 = prm + t * 0xA0u + 3u * 0x28u;
        if ((int32_t)vm_read32(p2) != -1 || (int32_t)vm_read32(p3) == -1) continue;
        vsn_ask(ctx, (int)t, (int32_t)vm_read32(p3));
        for (uint32_t o = 0; o < 0x28u; o += 4) vm_write32(p2 + o, vm_read32(p3 + o));
        vm_write32(p3, 0xFFFFFFFFu); moved++; }
    if (moved) { g_vs_part = part;
        std::printf("[sixvs] names part %08X: %d third fighters moved to the third rows (slots' characters %d %d %d %d)\n", part, moved, g_vs_slot_chr[0], g_vs_slot_chr[1], g_vs_slot_chr[2], g_vs_slot_chr[3]); std::fflush(stdout); }
}
/* The third fighters' NAME PICTURES on the VS screen (the maintainer 2026-11-19; helper map in game map notes vs_screen.md "Name
 * pictures"). The VS loader (mgr = [func_0042D018()+0x64]) has 16 resource ids, all in use, and name packs for setup
 * slots 0..3 only. For 3 vs 3:
 *  - func_00433D94 (the loader reserves its blocks, mode 1): at enter the id table (mgr+4: 8 bytes per id = resource
 *    pointer, block; mgr+8 = count | 0x80000000) is grown from 0x10 to 0x14 ids with four new resources (0x18 bytes,
 *    ctor func_0040ED34); at leave four blocks are reserved the way the game does for a name pack
 *    (func_004247CC(mgr, kind, id, size, align): _m 0x800 kind 0 align 0x10, _v 0x2800 kind 1 align 0x80).
 *  - jsp_vs_names_enter (the names part gets its table): the two third fighters' packs are asked for on ids
 *    0x10 / 0x11 (red) and 0x12 / 0x13 (blue) with func_00424618(mgr, id, path); the path is the game's own format
 *    (TOC+0x55E4 / +0x55E8) with the language of func_0043F448(func_0043F41C()).
 *  - jsp_vs_fill_leave (per frame): once both packs of a row are loaded (func_00424230), an archive object is made as
 *    the game makes its four (func_0051B718, func_0051CF8C(obj, m data, 0, v data, func_0051B4F8())) and the picture
 *    "battle_name_tx_%03d.png" is looked up (func_0051C0D8); vs_layout_frame puts it on the row's name pane.
 *  - func_0043442C (the loader closes): the port's two archive objects are destroyed (vtable+4) and the four ids
 *    released (func_00424654) before the game releases its own.
 * SIX_VSNAMES=0 turns all of it off (the third rows' names stay hidden then). */
static uint32_t g_vsn_mgr = 0; static bool g_vsn_grown = false; static uint32_t g_vsn_old_arr = 0;   /* the loader's own 16-id table, put back when it closes */
static int32_t g_vsn_chr[2] = { -1, -1 }; static uint32_t g_vsn_obj[2] = { 0, 0 }, g_vsn_tex[2] = { 0, 0 }; static int g_vsn_asked[2] = { 0, 0 };
static uint32_t g_vsn_buf = 0;
/* SIX_VSN_STAGE=1..4 (probe, to find which step upsets the fight loader): 1 only the larger table, 2 + the four blocks,
 * 3 + the packs are asked for, 4 (default) + the archives and pictures */
static int vsn_stage() { static const int st = [] { const char* e = std::getenv("SIX_VSN_STAGE"); return e ? std::atoi(e) : 4; }(); return st; }
/* On unless SIX_VSNAMES=0. History (2026-11-19): at first, with the two packs loaded, the fight did not load afterwards
 * (test runs, stage switches SIX_VSN_STAGE): the extra blocks lay past the end of the loader's arenas, see
 * vsn_arena_fix. With the arenas made larger the fight loads and runs (test runs). */
static bool vsn_on() { static const bool off = [] { const char* e = std::getenv("SIX_VSNAMES"); return e && e[0] == '0'; }(); return !off; }
static bool g_vsn_arena_ok = false;
void vsn_arena_fix()
{
    static bool done = false; if (done || !vsn_on() || !vm_base) return;
    const uint32_t T = vm_read32(0x008D3888u + 0x5868u); if (T < 0x10000u || T >= 0x30000000u) return;
    const uint32_t a0 = vm_read32(T + 0x9FE4u), a1 = vm_read32(T + 0xA034u);
    if (a0 == 0u && a1 == 0u) return;   /* the table is not filled in yet */
    done = true;
    if (a0 == 0x11000u && a1 == 0x4B5000u) { vm_write32(T + 0x9FE4u, 0x12000u); vm_write32(T + 0xA034u, 0x4BA000u); g_vsn_arena_ok = true; }
    std::printf("[sixvsn] heap group 0x2B arenas were %X / %X: %s\n", a0, a1, g_vsn_arena_ok ? "raised to 12000 / 4BA000" : "NOT as expected, left alone (no extra name packs)"); std::fflush(stdout);
}
static uint32_t vs_call(void (*fn)(ppu_context*), ppu_context* ctx, uint64_t a = 0, uint64_t b = 0, uint64_t c2 = 0, uint64_t d = 0, uint64_t e = 0)
{
    ppu_context c = *ctx; c.lr = 0; c.gpr[3] = a; c.gpr[4] = b; c.gpr[5] = c2; c.gpr[6] = d; c.gpr[7] = e;
    fn(&c); while (g_trampoline_fn) { auto f = g_trampoline_fn; g_trampoline_fn = 0; f(&c); }
    return (uint32_t)c.gpr[3];
}
static void vs_put_str(uint32_t at, const char* t) { size_t i = 0; for (; t[i]; i++) vm_base[at + i] = (uint8_t)t[i]; vm_base[at + i] = 0; }
static thread_local uint32_t t_vsn_res_mgr = 0;
extern "C" void jsp_vsn_reserve_enter(ppu_context* ctx)
{
    t_vsn_res_mgr = 0;
    const uint32_t mgr = (uint32_t)ctx->gpr[3];
    if (!vsn_on() || mgr < 0x10000u || mgr >= 0x30000000u || vm_read32(mgr + 0x20) != 1u) return;
    if ((vm_read32(mgr + 8) & 0x7FFFFFFFu) == 0x10u) t_vsn_res_mgr = mgr;
}
/* The table is grown AFTER the game's own reserve pass: that pass walks every id of the table with sizes it has for 16
 * only (a test run..1736: with 20 ids at its entry it asked for a 0x64A000-byte block from a null heap, stored
 * to the null page, and the fight never loaded). */
extern "C" void jsp_vsn_reserve_leave(ppu_context* ctx)
{
    jsp_map_leave(ctx);
    const uint32_t mgr = t_vsn_res_mgr; if (!mgr) return;
    t_vsn_res_mgr = 0;
    const uint32_t cw = vm_read32(mgr + 8), n = cw & 0x7FFFFFFFu, arr = vm_read32(mgr + 4);
    if (!g_vsn_arena_ok || n != 0x10u || arr < 0x10000u || arr >= 0x30000000u || vm_read32(mgr + 0x14) != 0x2Bu) { std::printf("[sixvsn] loader %08X after its reserve: %u ids, heap group %X: left alone\n", mgr, n, vm_read32(mgr + 0x14)); std::fflush(stdout); return; }
    const uint32_t na = six_game_new(ctx, 0x14u * 8u + 8u); if (!na) return;
    for (uint32_t o = 0; o < 0x80u; o += 4) vm_write32(na + o, vm_read32(arr + o));
    static uint32_t s_res[4] = { 0, 0, 0, 0 };   /* the four resources are made once and kept: each has a reader that stays linked in the game's reader list */
    for (uint32_t i = 0x10; i < 0x14u; i++) { uint32_t res = s_res[i - 0x10u];
        if (!res) { res = six_game_new(ctx, 0x18u); if (!res) return;
            for (uint32_t o = 0; o < 0x18u; o += 4) vm_write32(res + o, 0u);
            vs_call(func_0040ED34, ctx, res); s_res[i - 0x10u] = res; }
        vm_write32(na + i * 8u, res); vm_write32(na + i * 8u + 4u, 0u); }
    vm_write32(mgr + 4, na); vm_write32(mgr + 8, (cw & 0x80000000u) | 0x14u); g_vsn_grown = true; g_vsn_old_arr = arr; g_vsn_mgr = mgr;
    if (vsn_stage() >= 2) for (uint32_t t = 0; t < 2; t++) { vs_call(func_004247CC, ctx, mgr, 0, 0x10u + 2u * t, 0x800u, 0x10u); vs_call(func_004247CC, ctx, mgr, 1, 0x11u + 2u * t, 0x2800u, 0x80u); }
    std::printf("[sixvsn] loader %08X: table grown to 20 ids after its reserve (table %08X, was %08X); blocks for ids 0x10..0x13: %08X %08X %08X %08X\n", mgr, na, arr,
                vm_read32(na + 0x10 * 8 + 4), vm_read32(na + 0x11 * 8 + 4), vm_read32(na + 0x12 * 8 + 4), vm_read32(na + 0x13 * 8 + 4)); std::fflush(stdout);
    for (int t = 0; t < 2; t++) { g_vsn_chr[t] = -1; g_vsn_obj[t] = 0; g_vsn_tex[t] = 0; g_vsn_asked[t] = 0; }
}
/* ask for the two packs of team t's third fighter (character chr) */
static void vsn_ask(ppu_context* ctx, int t, int32_t chr)
{
    const uint32_t mgr = g_vsn_mgr;
    if (!vsn_on() || vsn_stage() < 3 || !mgr || chr < 0 || (vm_read32(mgr + 8) & 0x7FFFFFFFu) != 0x14u || g_vsn_asked[t]) return;
    const uint32_t arr = vm_read32(mgr + 4);
    if (!vm_read32(arr + (0x10u + 2u * (uint32_t)t) * 8u + 4u) || !vm_read32(arr + (0x11u + 2u * (uint32_t)t) * 8u + 4u)) { std::printf("[sixvsn] team %d: no block was reserved, no name pack\n", t); std::fflush(stdout); g_vsn_asked[t] = -1; return; }
    if (!g_vsn_buf) g_vsn_buf = six_game_new(ctx, 0x240u);
    if (!g_vsn_buf) return;
    const uint32_t lp = vs_call(func_0043F448, ctx, vs_call(func_0043F41C, ctx));
    char lang[24]; lang[0] = 0; if (lp >= 0x10000u && lp < 0x30000000u) six_read_name(lp, lang, 24);
    const uint32_t toc = (uint32_t)ctx->gpr[2];
    for (int k = 0; k < 2; k++) {
        char fmt[96]; fmt[0] = 0; const uint32_t fp = vm_read32(toc + (k == 0 ? 0x55E4u : 0x55E8u)); if (fp >= 0x10000u && fp < 0x30000000u) { int i = 0; for (; i < 95; i++) { fmt[i] = (char)vm_base[fp + (uint32_t)i]; if (!fmt[i]) break; } fmt[i] = 0; }
        if (std::strncmp(fmt, "progress/", 9) || !lang[0]) { std::printf("[sixvsn] path format or language not found ('%s', '%s')\n", fmt, lang); std::fflush(stdout); g_vsn_asked[t] = -1; return; }
        char path[200]; std::snprintf(path, sizeof path, fmt, lang, (int)chr, lang);
        vs_put_str(g_vsn_buf, path);
        const uint32_t id = 0x10u + 2u * (uint32_t)t + (uint32_t)k;
        vs_call(func_00424618, ctx, mgr, id, g_vsn_buf);
        std::printf("[sixvsn] team %d: asked for '%s' on id 0x%X\n", t, path, id); std::fflush(stdout);
    }
    g_vsn_chr[t] = chr; g_vsn_asked[t] = 1;
}
/* per frame: when a row's packs are in, make its archive and look the picture up */
static void vsn_poll(ppu_context* ctx)
{
    const uint32_t mgr = g_vsn_mgr; if (!mgr || !vsn_on() || vsn_stage() < 4) return;
    for (int t = 0; t < 2; t++) { if (g_vsn_asked[t] != 1 || g_vsn_tex[t]) continue;
        const uint32_t idm = 0x10u + 2u * (uint32_t)t, idv = idm + 1u;
        if (!g_vsn_obj[t]) {
            if (vs_call(func_00424230, ctx, mgr, idm) != 1u || vs_call(func_00424230, ctx, mgr, idv) != 1u) continue;
            const uint32_t obj = six_game_new(ctx, 0x34u); if (!obj) continue;
            vs_call(func_0051B718, ctx, obj);
            const uint32_t dm = vs_call(func_004337CC, ctx, mgr, idm, 0), dv = vs_call(func_004337CC, ctx, mgr, idv, 0), tk = vs_call(func_0051B4F8, ctx);
            vs_call(func_0051CF8C, ctx, obj, dm, 0, dv, tk);
            g_vsn_obj[t] = obj;
            std::printf("[sixvsn] team %d: packs loaded (data %08X %08X), archive %08X\n", t, dm, dv, obj); std::fflush(stdout);
        }
        if (!g_vsn_buf) continue;
        char nm[48]; std::snprintf(nm, sizeof nm, "battle_name_tx_%03d.png", (int)g_vsn_chr[t]); vs_put_str(g_vsn_buf, nm);
        const uint32_t tex = vs_call(func_0051C0D8, ctx, g_vsn_obj[t], g_vsn_buf);
        static int said = 0; if (said++ < 8) { std::printf("[sixvsn] team %d: picture '%s' -> %08X\n", t, nm, tex); std::fflush(stdout); }
        if (tex >= 0x10000u && tex < 0x30000000u) g_vsn_tex[t] = tex; else g_vsn_asked[t] = -1;   /* not in the pack: give up, the name stays hidden */
    }
}
/* The names part is destroyed (its three destructors func_002F2EB0 / 2FB0 / 30A4 destroy the game's four archives through
 * their vtable+4): the port's two go the same way, at the same time. Destroying them only when the loader closed (runs
 * 2026-11-19_1727..1746) left the fight loader without a heap for its first block: by then the screen's own memory
 * was gone already. */
extern "C" void jsp_vsn_part_dtor_enter(ppu_context* ctx)
{
    const uint32_t part = (uint32_t)ctx->gpr[3];
    if (!g_vs_part || part != g_vs_part) return;
    int n = 0;
    for (int t = 0; t < 2; t++) { const uint32_t obj = g_vsn_obj[t];
        if (obj && vm_read32(obj) >= 0x10000u) { const uint32_t opd = vm_read32(vm_read32(obj) + 4u);
            if (opd >= 0x10000u && opd < 0x30000000u) { ppu_context c = *ctx; c.lr = 0; c.gpr[3] = obj; ps3_indirect_call_to(&c, vm_read32(opd)); while (g_trampoline_fn) { auto f = g_trampoline_fn; g_trampoline_fn = 0; f(&c); } n++; } }
        g_vsn_obj[t] = 0; g_vsn_tex[t] = 0; if (g_vsn_asked[t] == 1) g_vsn_asked[t] = -1; }
    g_vs_part = 0;
    std::printf("[sixvsn] names part %08X destroyed (from %08X): %d port archives destroyed with it\n", part, (uint32_t)ctx->lr, n); std::fflush(stdout);
}
/* The port's ids are let go AFTER the game's own close pass (leave), not before it: stage a test run..1758
 * showed the larger table and the blocks alone are harmless, and that with packs loaded on the ids, letting them go
 * before the game's pass (func_00424B00, then its own sixteen) left the fight loader without a heap. */
static thread_local uint32_t t_vsn_close_mgr = 0;
extern "C" void jsp_vsn_close_enter(ppu_context* ctx)
{
    t_vsn_close_mgr = 0;
    const uint32_t mgr = (uint32_t)ctx->gpr[3];
    if (!g_vsn_mgr || mgr != g_vsn_mgr || vm_read32(mgr + 0x20) != 1u || (vm_read32(mgr + 8) & 0x7FFFFFFFu) != 0x14u) return;
    t_vsn_close_mgr = mgr;
}
extern "C" void jsp_vsn_close_leave(ppu_context* ctx)
{
    jsp_map_leave(ctx);
    const uint32_t mgr = t_vsn_close_mgr; if (!mgr) return;
    t_vsn_close_mgr = 0;
    if ((vm_read32(mgr + 8) & 0x7FFFFFFFu) != 0x14u) return;
    for (int t = 0; t < 2; t++) {   /* the archives were destroyed with the names part (jsp_vsn_part_dtor_enter); one still here is only forgotten */
        if (g_vsn_obj[t]) { std::printf("[sixvsn] team %d: the archive was still there when the loader closed: forgotten, not destroyed\n", t); std::fflush(stdout); }
        g_vsn_obj[t] = 0; g_vsn_tex[t] = 0; g_vsn_asked[t] = 0; g_vsn_chr[t] = -1; }
    /* func_00424654 finds the blocks' heap by the group at mgr+0x14 (0x2B while the loader reserves): a test run
     * crashed after this point with stores to the null page from the release path, so the group is checked and set
     * for these four calls */
    const uint32_t grp = vm_read32(mgr + 0x14);
    if (grp != 0x2Bu) vm_write32(mgr + 0x14, 0x2Bu);
    if (vsn_stage() >= 2) for (uint32_t id = 0x10; id < 0x14u; id++) vs_call(func_00424654, ctx, mgr, id);
    if (grp != 0x2Bu) vm_write32(mgr + 0x14, grp);
    std::printf("[sixvsn] loader close: heap group word was %X\n", grp);
    g_vs_part = 0;
    /* The loader gets its own 16-id table back before it closes (a test run: with the 20-id table left
     * in place the game stored to the null page from its release path after this point and the fight never started).
     * Its 16 entries are copied back first: the blocks were written into the larger table. The larger table and the four
     * extra resources are freed. */
    { const uint32_t na = vm_read32(mgr + 4), old = g_vsn_old_arr, cw = vm_read32(mgr + 8);
      if (old && na != old) {
          for (uint32_t o = 0; o < 0x80u; o += 4) vm_write32(old + o, vm_read32(na + o));
          vm_write32(mgr + 4, old); vm_write32(mgr + 8, (cw & 0x80000000u) | 0x10u);
          six_game_free(ctx, na); }   /* the four resources are kept for the next time */
      g_vsn_old_arr = 0; g_vsn_grown = false; }
    /* nothing of the port's may stay in the game's heap past this point: a test run still failed, and the first
     * fault was the fight loader's 0x64A000-byte block (func_000CEE0C -> func_00423F14 -> func_004157A0 -> func_004247CC)
     * getting no heap: the text buffer for the paths was still allocated in the middle of that space */
    if (g_vsn_buf) { six_game_free(ctx, g_vsn_buf); g_vsn_buf = 0; }
    std::printf("[sixvsn] loader closes: the port's two archives and four ids let go, the 16-id table is back\n"); std::fflush(stdout);
}
/* func_002F25F4 (per frame: each filled row's name picture and 1P / COM picture). It takes the name packs in filled-row
 * order from part +0x178 + 4*i, and there are four; with six rows the fifth would read a word that is no pointer. The
 * lookup itself (func_0051C0D8) cannot carry a hook (tail call). So for this function only, the third rows are empty
 * again: it fills rows 0 / 1 of each team as in a 2 vs 2, and the row code (func_002F32A0) still shows the third rows
 * and their stars. The third rows' name and 1P / COM pictures are a later step. */
static thread_local uint32_t t_vs_fill_part = 0; static thread_local uint32_t t_vs_fill_chr[2] = { 0, 0 };
extern "C" void jsp_vs_fill_enter(ppu_context* ctx)
{
    t_vs_fill_part = 0;
    const uint32_t part = (uint32_t)ctx->gpr[3];
    if (!g_vs_part || part != g_vs_part || vm_read32(part) != 0x00882460u) return;
    t_vs_fill_part = part;
    for (uint32_t t = 0; t < 2; t++) { const uint32_t p2 = part + 0x30u + t * 0xA0u + 2u * 0x28u; t_vs_fill_chr[t] = vm_read32(p2); vm_write32(p2, 0xFFFFFFFFu); }
}
extern "C" void jsp_vs_fill_leave(ppu_context* ctx)
{
    jsp_map_leave(ctx);
    const uint32_t part = t_vs_fill_part; if (!part) return;
    for (uint32_t t = 0; t < 2; t++) vm_write32(part + 0x30u + t * 0xA0u + 2u * 0x28u, t_vs_fill_chr[t]);
    t_vs_fill_part = 0;
    vsn_poll(ctx);
}
/* VS screen for 3 vs 3, the pictures (the maintainer 2026-11-19; pane places from a test run, vs_screen.md).
 * Portraits layout: four fighter boxes (panes *_00b, b 0..3, all at 149 + 226 b, 112; the portrait 22,-52 from there) and
 * two small Support boxes (panes *_s_*_00n, origin 86,276 and 952,276; portrait at 0.7) that hold units 4 / 5. For 3 vs 3
 * all six stand in one row at 0.8: red slot 0, slot 1, third; blue third, then the game's boxes 2, 3. The Support title
 * and its lines are hidden. Names layout: the third rows' 1P / COM picture is taken from a CPU row (the fill function
 * does not reach the third rows, jsp_vs_fill_enter), and their name picture is hidden (no name pack for them yet).
 * A pane is only re-placed when the game has written it again (its timeline does while the screen slides in).
 * SIX_VSBOX="scale x0 y ks ksx" tunes; SIX_VSBOX=0 leaves the boxes alone. */
/* VS screen, 3 vs 3: the two middle boxes as REAL fighter boxes (the maintainer 2026-11-19 18:35Z: "copy any of the other fighter
 * boxes and use those instead like we did with character select"). Design in game map notes vs_screen.md "Design for the real
 * copy". The seven panes of fighter box 1 (red) and box 2 (blue) are copied (pane block, child table, records) and their
 * root records appended to the portraits layout's root table; the flat pane list is grown by the same panes. Every frame
 * a copy takes its source's matrix / colour / frame (+0x90..+0xD3, +0xF8), moved one column inward; the copied portrait
 * takes the picture of p_rgb8_256_chara_ico_004 / 005 (units 4 / 5); the Support box panes are hidden. When the
 * portraits part is destroyed (func_002F1148 / func_002F107C) the original tables are put back and the copies freed.
 * SIX_VSCOPY=0 = the stretched Support frames instead. */
struct VsCopy { uint32_t lay = 0, root = 0, tb = 0, arr_old = 0, cw_old = 0, list = 0, larr_old = 0, lcnt_old = 0, arr_new = 0, larr_new = 0;
                uint32_t src[40] = {}, cp[40] = {}; uint8_t team[40] = {}; bool pic[40] = {}; int n = 0; uint32_t pic_src[2] = { 0, 0 }; uint32_t extra[64] = {}; int nextra = 0; uint32_t ksrc[48] = {}, kcp[48] = {}; int nk = 0; uint32_t host = 0; float colw = 0.0f; uint32_t early_tex[2] = { 0, 0 }; bool early_tried = false; bool on = false; };
static VsCopy g_vsc;
static bool vsc_enabled() { static const bool off = [] { const char* e = std::getenv("SIX_VSCOPY"); return e && e[0] == '0'; }(); return !off; }
static uint32_t vsc_clone(ppu_context* ctx, uint32_t src, uint32_t parent, int depth)
{
    if (src < 0x10000u || src >= 0x30000000u || depth > 4) return 0;
    const uint32_t c = six_clone_block(ctx, src, 0x1E0u); if (!c) return 0;
    if (g_vsc.nextra < 64) g_vsc.extra[g_vsc.nextra++] = c;
    vm_write32(c + 0x04, parent);
    vm_write32(c + 0x108, vm_read32(src + 0x108) + 1u);   /* the game's name lookups must not find the copy */
    if (depth > 0 && g_vsc.nk < 48) { g_vsc.ksrc[g_vsc.nk] = src; g_vsc.kcp[g_vsc.nk] = c; g_vsc.nk++; }   /* children are mirrored too (no offset) */
    const uint32_t tb = vm_read32(src + 0xE8);
    if (tb >= 0x10000u && tb < 0x30000000u && vm_read32(tb) == 0x00882E60u) {
        const uint32_t tb2 = six_clone_block(ctx, tb, 0xE0u);
        if (tb2) { if (g_vsc.nextra < 64) g_vsc.extra[g_vsc.nextra++] = tb2;
            vm_write32(tb2 + 0x04, c);
            const uint32_t arr = vm_read32(tb + 0x08), n = vm_read32(tb + 0x0C) >> 16; uint32_t arr2 = 0;
            if (n && n < 64 && arr >= 0x10000u && arr < 0x30000000u) { arr2 = six_game_new(ctx, n * 12u + 4u);
                if (arr2) { if (g_vsc.nextra < 64) g_vsc.extra[g_vsc.nextra++] = arr2;
                    for (uint32_t r = 0; r < n; r++) { const uint32_t a = vm_read32(arr + r * 12), b = vm_read32(arr + r * 12 + 4);
                        vm_write32(arr2 + r * 12, a ? vsc_clone(ctx, a, c, depth + 1) : 0u); vm_write32(arr2 + r * 12 + 4, b ? vsc_clone(ctx, b, c, depth + 1) : 0u);
                        vm_write32(arr2 + r * 12 + 8, vm_read32(arr + r * 12 + 8)); } } }
            vm_write32(tb2 + 0x08, arr2); vm_write32(c + 0xE8, tb2); } }
    return c;
}
/* set up once per portraits layout (from the layout calc hook, which has a guest context) */
static void vsc_setup(ppu_context* ctx, uint32_t lay)
{
    if (g_vsc.lay == lay || !vsc_enabled()) return;
    /* tried on each calc until it works, at most 240 times per layout (a test run: on the first calc the two
     * portrait panes of units 4 / 5 were not found yet) */
    static uint32_t tries_lay = 0; static int tries = 0;
    if (tries_lay != lay) { tries_lay = lay; tries = 0; }
    if (++tries > 240) return;
    const bool quiet = tries > 1 && tries != 240;
    g_vsc = VsCopy();
    const uint32_t list = vm_read32(lay + 0x14); if (list < 0x10000u || list >= 0x30000000u) return;
    const uint32_t larr = vm_read32(list + 0x08), lcnt = vm_read32(list + 0x0C); if (!lcnt || lcnt > 200u || larr < 0x10000u || larr >= 0x30000000u) return;
    static const char* const want[7] = { "p_vs_chara_fr_000_", "i_fr_deco_l_", "p_vs_chara_fr_001_", "_ca_00", "p_vs_chara_fr_mask_", "p_rgb8_256_chara_ico_", "p_vs_chara_fr_002_" };
    uint32_t root = 0;
    for (uint32_t i = 0; i < lcnt; i++) { const uint32_t pn = vm_read32(larr + i * 4); if (pn < 0x10000u || pn >= 0x30000000u) continue;
        char nm[40]; nm[0] = 0; six_read_name(vm_read32(pn + 0x104), nm, 40);
        if (!std::strcmp(nm, "p_rgb8_256_chara_ico_004")) g_vsc.pic_src[0] = pn; if (!std::strcmp(nm, "p_rgb8_256_chara_ico_005")) g_vsc.pic_src[1] = pn;
        if (!std::strcmp(nm, "p_vs_chara_fr_000_001")) root = vm_read32(pn + 0x04); }
    const uint32_t tb = root >= 0x10000u && root < 0x30000000u ? vm_read32(root + 0xE8) : 0u;
    /* the unit 4 / 5 portrait panes come a few calcs later (a test run); waiting for them made the copies
     * pop in after the boxes had slid in (the maintainer). So the copies are made on the first calc and the two
     * pictures are looked up each frame until found (vsc_frame), the copied portrait hidden until then. */
    if (!tb || tb >= 0x30000000u || vm_read32(tb) != 0x00882E60u) { if (quiet) return; std::printf("[sixvsc] root %08X table %08X pictures %08X %08X: not as expected, no copy\n", root, tb, g_vsc.pic_src[0], g_vsc.pic_src[1]); std::fflush(stdout); return; }
    const uint32_t arr = vm_read32(tb + 0x08), cw = vm_read32(tb + 0x0C), n = cw >> 16;
    if (!n || n > 200u || arr < 0x10000u || arr >= 0x30000000u) return;
    /* The root table holds the panes in field b of its 12-byte records, with empty records between them (run
     * 2026-11-19_1947-vs-copy3: box 0 = records 31..45, its mask p_vs_chara_fr_mask_000 at 39 with clip depth 0x2C).
     * So each box is copied as its whole record range, empty records included, and a mask's clip depth is moved with
     * the range. */
    int first[2] = { -1, -1 }, last[2] = { -1, -1 };
    for (uint32_t r = 0; r < n; r++) { const uint32_t bp = vm_read32(arr + r * 12 + 4); if (bp < 0x10000u || bp >= 0x30000000u) continue;
        char nm[40]; nm[0] = 0; six_read_name(vm_read32(bp + 0x104), nm, 40); const size_t L2 = std::strlen(nm); if (L2 < 4) continue;
        for (int b = 1; b <= 2; b++) { bool hit = false;
            for (int k = 0; k < 7 && !hit; k++) { const size_t wl = std::strlen(want[k]); if (std::strncmp(nm, want[k], wl)) continue;
                if (k == 3) hit = L2 == wl + 1 && nm[wl] == (char)('0' + b + 1); else hit = L2 == wl + 3 && nm[L2 - 1] == (char)('0' + b) && nm[L2 - 2] == '0' && nm[L2 - 3] == '0'; }
            if (hit) { if (first[b - 1] < 0) first[b - 1] = (int)r; last[b - 1] = (int)r; } } }
    if (first[0] < 0 || first[1] < 0 || last[0] - first[0] > 20 || last[1] - first[1] > 20) { std::printf("[sixvsc] box record ranges %d..%d / %d..%d: no copy\n", first[0], last[0], first[1], last[1]); std::fflush(stdout); return; }
    /* Copies appended to the root table are not drawn, even when mirrored every frame (a test run; the same
     * as on Character Select, charsel.md 7.14 / SIX_CSHOST). So, like there, they hang under a visible pane that is
     * drawn the whole time: appended to its child table, parent = that pane, position made relative to it each frame.
     * SIX_VSHOST=<pane name> picks it (default p_vs_chara_fr_000_000, box 0's frame); SIX_VSHOST=root = the old way. */
    uint32_t T = tb, tarr = arr, tcw = cw, tn = n, hostp = root;
    { const char* hn = std::getenv("SIX_VSHOST"); const char* hname = hn ? hn : "root";   /* root: a test run (the copies draw and slide in with the boxes); under a box they did not draw */
      if (std::strcmp(hname, "root")) for (uint32_t i = 0; i < lcnt; i++) { const uint32_t pn = vm_read32(larr + i * 4); if (pn < 0x10000u || pn >= 0x30000000u) continue;
          char nm[40]; nm[0] = 0; six_read_name(vm_read32(pn + 0x104), nm, 40); if (std::strcmp(nm, hname)) continue;
          const uint32_t ht = vm_read32(pn + 0xE8);
          if (ht >= 0x10000u && ht < 0x30000000u && vm_read32(ht) == 0x00882E60u) { const uint32_t ha = vm_read32(ht + 0x08), hc = vm_read32(ht + 0x0C);
              if ((hc >> 16) < 64u && (!(hc >> 16) || (ha >= 0x10000u && ha < 0x30000000u))) { hostp = pn; T = ht; tarr = ha; tcw = hc; tn = hc >> 16; } }
          std::printf("[sixvsc] host %s %08X table %08X (%u records) matrix %08X %08X %08X %08X %08X %08X%s\n", hname, pn, ht, vm_read32(ht + 0x0C) >> 16,
              vm_read32(pn + 0x90), vm_read32(pn + 0x94), vm_read32(pn + 0x98), vm_read32(pn + 0x9C), vm_read32(pn + 0xA0), vm_read32(pn + 0xA4), hostp == pn ? "" : ": not usable, root instead");
          std::fflush(stdout); break; } }
    const uint32_t add = (uint32_t)(last[0] - first[0] + 1 + last[1] - first[1] + 1);
    const uint32_t na = six_game_new(ctx, (tn + add) * 12u + 4u), nla = six_game_new(ctx, (lcnt + add) * 4u + 4u);
    if (!na || !nla) return;
    for (uint32_t q = 0; q < tn * 3u; q++) vm_write32(na + q * 4, vm_read32(tarr + q * 4));
    for (uint32_t q = 0; q < lcnt; q++) vm_write32(nla + q * 4, vm_read32(larr + q * 4));
    uint32_t to = tn, lc = lcnt; int k2 = 0;
    for (int t = 0; t < 2; t++) for (int r = first[t]; r <= last[t]; r++) {
        const uint32_t fa = vm_read32(arr + (uint32_t)r * 12), fb = vm_read32(arr + (uint32_t)r * 12 + 4), w = vm_read32(arr + (uint32_t)r * 12 + 8);
        const uint32_t ca = fa ? vsc_clone(ctx, fa, hostp, 0) : 0u, cb = fb ? vsc_clone(ctx, fb, hostp, 0) : 0u;
        uint32_t w2 = w; if (w >> 16) w2 = ((((w >> 16) - (uint32_t)first[t]) + (to - (uint32_t)(r - first[t]))) << 16) | (w & 0xFFFFu);   /* clip depth moved with the range */
        vm_write32(na + to * 12, ca); vm_write32(na + to * 12 + 4, cb); vm_write32(na + to * 12 + 8, w2);
        for (uint32_t c2 : { ca, cb }) if (c2) { vm_write32(nla + lc * 4, c2); lc++; }
        for (int q = 0; q < 2; q++) { const uint32_t sp = q ? fb : fa, cp2 = q ? cb : ca; if (sp && cp2 && k2 < 40) { g_vsc.src[k2] = sp; g_vsc.cp[k2] = cp2; g_vsc.team[k2] = (uint8_t)t;
            char nm[40]; nm[0] = 0; six_read_name(vm_read32(sp + 0x104), nm, 40); g_vsc.pic[k2] = !std::strncmp(nm, "p_rgb8_256_chara_ico_", 21); k2++; } }
        to++; }
    g_vsc.n = k2; g_vsc.root = root; g_vsc.tb = T; g_vsc.arr_old = tarr; g_vsc.cw_old = tcw; g_vsc.arr_new = na; g_vsc.host = hostp != root ? hostp : 0u;
    g_vsc.list = list; g_vsc.larr_old = larr; g_vsc.lcnt_old = lcnt; g_vsc.larr_new = nla;
    vm_write32(T + 0x08, na); vm_write32(T + 0x0C, (to << 16) | (tcw & 0xFFFFu));
    vm_write32(list + 0x08, nla); vm_write32(list + 0x0C, lc);
    std::printf("[sixvsc] layout %08X: box 1 records %d..%d and box 2 records %d..%d copied (%d panes), %s table %u -> %u records, pane list %u -> %u\n", lay, first[0], last[0], first[1], last[1], k2, g_vsc.host ? "host" : "root", tn, to, lcnt, lc); std::fflush(stdout);
    g_vsc.on = true; g_vsc.lay = lay;
}
/* per frame, after the layout code placed the boxes: copies follow their sources one column inward */
static void vsc_frame(ppu_context* ctx, float colw)
{
    if (!g_vsc.on) return;
    g_vsc.colw = colw;
    auto rd = [](uint32_t a2) { union { uint32_t w; float f; } u; u.w = vm_read32(a2); return u.f; };
    auto wr = [](uint32_t a2, float f) { union { uint32_t w; float f; } u; u.f = f; vm_write32(a2, u.w); };
    for (int i = 0; i < g_vsc.n; i++) { const uint32_t a = g_vsc.src[i], c = g_vsc.cp[i]; if (vm_read32(a) != 0x00882EF0u || vm_read32(c) != 0x00882EF0u) continue;
        /* the whole live state, like Character Select's copies (charsel.md 7.14 / 7.15): flags, picture, runtime
         * transform and colour (+0x18..+0x3C: the boxes fade in, and a copy taken on the first calc kept alpha 0, run
         * 2026-11-19_1953), file matrix and colour transform, frame, visible */
        vm_write32(c + 0x10, vm_read32(a + 0x10)); if (!g_vsc.pic[i]) vm_write32(c + 0x14, vm_read32(a + 0x14));   /* a portrait copy keeps its own picture (no second picture within a frame) */
        for (uint32_t o = 0x18; o < 0xD4; o += 4) vm_write32(c + o, vm_read32(a + o));
        vm_write32(c + 0xF8, vm_read32(a + 0xF8)); vm_write32(c + 0x100, vm_read32(a + 0x100));
        wr(c + 0x98, rd(a + 0x98) + (g_vsc.team[i] == 0 ? colw : -colw));
        if (g_vsc.host) {   /* in the host's space: its matrix is a 0.78 scale plus a move (a test run) */
            float hsx = rd(g_vsc.host + 0x90), hsy = rd(g_vsc.host + 0xA0); if (hsx < 0.05f && hsx > -0.05f) hsx = 1.0f; if (hsy < 0.05f && hsy > -0.05f) hsy = 1.0f;
            wr(c + 0x98, (rd(c + 0x98) - rd(g_vsc.host + 0x98)) / hsx); wr(c + 0xA4, (rd(a + 0xA4) - rd(g_vsc.host + 0xA4)) / hsy);
            wr(c + 0x90, rd(a + 0x90) / hsx); wr(c + 0x94, rd(a + 0x94) / hsx); wr(c + 0x9C, rd(a + 0x9C) / hsy); wr(c + 0xA0, rd(a + 0xA0) / hsy); }
        if (g_vsc.pic[i] && !g_vsc.pic_src[g_vsc.team[i]]) {   /* not there yet: look for it, hide the copied portrait */
            const uint32_t list = vm_read32(g_vsc.lay + 0x14);
            if (list >= 0x10000u && list < 0x30000000u) { const uint32_t la = vm_read32(list + 0x08), lc = vm_read32(list + 0x0C);
                static const uint32_t h4 = six_name_hash("p_rgb8_256_chara_ico_004"), h5 = six_name_hash("p_rgb8_256_chara_ico_005");
                if (la >= 0x10000u && la < 0x30000000u && lc < 400u) for (uint32_t q = 0; q < lc; q++) { const uint32_t pn = vm_read32(la + q * 4); if (pn < 0x10000u || pn >= 0x30000000u) continue;
                    const uint32_t h = vm_read32(pn + 0x108); if (h == h4) g_vsc.pic_src[0] = pn; else if (h == h5) g_vsc.pic_src[1] = pn; } }
            if (!g_vsc.pic_src[g_vsc.team[i]]) {
                const uint32_t et = g_vsc.early_tex[g_vsc.team[i]];
                if (et) { vm_write32(c + 0x14, et); vm_write32(c + 0x10, vm_read32(c + 0x10) | 1u); continue; }
                vm_write32(c + 0x100, vm_read32(c + 0x100) & ~1u); } }
        if (g_vsc.pic[i]) { const uint32_t ps = g_vsc.pic_src[g_vsc.team[i]]; if (ps && vm_read32(ps) == 0x00882EF0u) { vm_write32(c + 0x14, vm_read32(ps + 0x14)); vm_write32(c + 0x10, vm_read32(c + 0x10) | (vm_read32(ps + 0x10) & 1u)); } } }
    for (int i = 0; i < g_vsc.nk; i++) { const uint32_t a = g_vsc.ksrc[i], c = g_vsc.kcp[i]; if (vm_read32(a) != 0x00882EF0u || vm_read32(c) != 0x00882EF0u) continue;
        vm_write32(c + 0x10, vm_read32(a + 0x10)); vm_write32(c + 0x14, vm_read32(a + 0x14));
        for (uint32_t o = 0x18; o < 0xD4; o += 4) vm_write32(c + o, vm_read32(a + o));
        vm_write32(c + 0xF8, vm_read32(a + 0xF8)); vm_write32(c + 0x100, vm_read32(a + 0x100)); }
}
/* The VS screen's outro (the X flash) goes to a timeline frame that does not hold the appended depths, and the game
 * removes them (func_0032D288(table, index): both panes of the record destroyed, clip word cleared; run
 * 2026-11-19_2033: records 140..169 emptied at frame 70, and the copies vanished there). For our records the
 * remove is made a no-op: the record is emptied for the call and put back after it. */
static thread_local uint32_t t_vsc_rm_rec = 0; static thread_local uint32_t t_vsc_rm_w[3];
extern "C" void jsp_vsc_remove_enter(ppu_context* ctx)
{
    t_vsc_rm_rec = 0;
    if (!g_vsc.on || (uint32_t)ctx->gpr[3] != g_vsc.tb || vm_read32(g_vsc.tb + 0x08) != g_vsc.arr_new) return;
    const uint32_t idx = (uint32_t)ctx->gpr[4], old_n = g_vsc.cw_old >> 16, n = vm_read32(g_vsc.tb + 0x0C) >> 16;
    if (idx < old_n || idx >= n) return;
    const uint32_t rec = g_vsc.arr_new + idx * 12u;
    for (int w = 0; w < 3; w++) { t_vsc_rm_w[w] = vm_read32(rec + (uint32_t)w * 4); vm_write32(rec + (uint32_t)w * 4, 0u); }
    t_vsc_rm_rec = rec;
    static int said = 0; if (said++ < 2) { std::printf("[sixvsc] the game removes our record %u (lr %08X): kept\n", idx, (uint32_t)ctx->lr); std::fflush(stdout); }
}
extern "C" void jsp_vsc_remove_leave(ppu_context* ctx)
{
    jsp_map_leave(ctx);
    const uint32_t rec = t_vsc_rm_rec; if (!rec) return;
    t_vsc_rm_rec = 0;
    for (int w = 0; w < 3; w++) vm_write32(rec + (uint32_t)w * 4, t_vsc_rm_w[w]);
}
/* func_00333FCC (set a pane's picture) enter: the portraits part's per-frame setter (func_002F0E84, lr 0x002F0FD4, r28 = part) */
extern "C" void jsp_vsc_setpic_enter(ppu_context* ctx)
{
    if (!g_vsc.on || !vm_base) return;
    /* A picture object draws white for its first ~40 frames after its first use (a test run: even a copy of box 1's
     * object did), and the game asks for the unit 4 / 5 pictures only when their panes appear (~frame 19), so their
     * faces came ~1.5 s after the boxes. The copies ask for them at once instead: the part's icon archive (+0x178), the
     * characters at +0x17C + 0x10 * box (boxes 4 / 5; -1 = 300). */
    if ((uint32_t)ctx->lr == 0x002F0FD4u && !g_vsc.early_tried) { g_vsc.early_tried = true;
        const uint32_t part = (uint32_t)ctx->gpr[28];
        if (part >= 0x10000u && part < 0x30000000u) { const uint32_t arc = vm_read32(part + 0x178);
            const uint32_t buf = six_game_new(ctx, 0x40u);
            if (buf && arc >= 0x10000u && arc < 0x30000000u) for (int t = 0; t < 2; t++) {
                int32_t id = (int32_t)vm_read32(part + 0x17C + (uint32_t)(4 + t) * 0x10u); if (id < 0) id = 300;
                char nm[48]; std::snprintf(nm, sizeof nm, "_rgb8_256_chara_ico_%03d.png", (int)id); vs_put_str(buf, nm);
                const uint32_t tex = vs_call(func_0051C0D8, ctx, arc, buf);
                if (tex >= 0x10000u && tex < 0x30000000u) g_vsc.early_tex[t] = tex;
                std::printf("[sixvsc] unit %d picture '%s' asked for early: %08X\n", 4 + t, nm, tex); std::fflush(stdout); }
            if (buf) six_game_free(ctx, buf); } }
}
extern "C" void jsp_vsc_part_dtor_enter(ppu_context* ctx)
{
    if (!g_vsc.on) return;
    if (vm_read32(g_vsc.tb) == 0x00882E60u && vm_read32(g_vsc.tb + 0x08) == g_vsc.arr_new) { vm_write32(g_vsc.tb + 0x08, g_vsc.arr_old); vm_write32(g_vsc.tb + 0x0C, g_vsc.cw_old); }
    if (vm_read32(g_vsc.list + 0x08) == g_vsc.larr_new) { vm_write32(g_vsc.list + 0x08, g_vsc.larr_old); vm_write32(g_vsc.list + 0x0C, g_vsc.lcnt_old); }
    for (int i = 0; i < g_vsc.nextra; i++) six_game_free(ctx, g_vsc.extra[i]);
    six_game_free(ctx, g_vsc.arr_new); six_game_free(ctx, g_vsc.larr_new);
    std::printf("[sixvsc] portraits part destroyed (from %08X): tables put back, %d copied blocks freed\n", (uint32_t)ctx->lr, g_vsc.nextra); std::fflush(stdout);
    g_vsc = VsCopy();
}
static void vs_layout_frame(ppu_context* ctx, uint32_t lay)
{
    if (!six_live() || !g_vs_part || lay < 0x10000u || lay >= 0x30000000u) return;
    const uint32_t list = vm_read32(lay + 0x14); if (list < 0x10000u || list >= 0x30000000u) return;
    const uint32_t arr = vm_read32(list + 0x08), cnt = vm_read32(list + 0x0C); if (!cnt || cnt > 400u || arr < 0x10000u || arr >= 0x30000000u) return;
    static const uint32_t h_names = six_name_hash("p_player_rd_000"), h_box = six_name_hash("p_vs_chara_fr_000_000");
    int kind = 0;
    for (uint32_t i = 0; i < cnt && !kind; i++) { const uint32_t pn = vm_read32(arr + i * 4); if (pn < 0x10000u || pn >= 0x30000000u) continue;
        const uint32_t h = vm_read32(pn + 0x108); if (h == h_names) kind = 1; else if (h == h_box) kind = 2; }
    if (!kind) return;
    auto rd = [](uint32_t a2) { union { uint32_t w; float f; } u; u.w = vm_read32(a2); return u.f; };
    auto wr = [](uint32_t a2, float f) { union { uint32_t w; float f; } u; u.f = f; vm_write32(a2, u.w); };
    if (kind == 1) {
        static const uint32_t h_r2 = six_name_hash("p_player_rd_002"), h_b2 = six_name_hash("p_player_bl_002"), h_ptx = six_name_hash("p_vs_player_tx"), h_ntx = six_name_hash("p_battle_name_tx");
        uint32_t row3[2] = { 0, 0 }, comtex = 0;
        for (uint32_t i = 0; i < cnt; i++) { const uint32_t pn = vm_read32(arr + i * 4); if (pn < 0x10000u || pn >= 0x30000000u) continue;
            const uint32_t h = vm_read32(pn + 0x108); if (h == h_r2) row3[0] = pn; else if (h == h_b2) row3[1] = pn; }
        if (!row3[0] && !row3[1]) return;
        /* a CPU row's picture: the 1P / COM pane of a row whose stars are shown has the COM picture; the third rows' own
         * panes are told apart by their parent */
        static const uint32_t h_star = six_name_hash("p_vs_comlv_001_000");
        for (uint32_t i = 0; i < cnt && !comtex; i++) { const uint32_t pn = vm_read32(arr + i * 4); if (pn < 0x10000u || pn >= 0x30000000u) continue;
            if (vm_read32(pn + 0x108) != h_star || !(vm_read32(pn + 0x100) & 1u)) continue;
            const uint32_t par = vm_read32(pn + 0x04); if (par == row3[0] || par == row3[1]) continue;
            for (uint32_t k = 0; k < cnt; k++) { const uint32_t q = vm_read32(arr + k * 4); if (q < 0x10000u || q >= 0x30000000u) continue;
                if (vm_read32(q + 0x108) == h_ptx && vm_read32(q + 0x04) == par && vm_read32(q + 0x14)) { comtex = vm_read32(q + 0x14); break; } } }
        for (uint32_t i = 0; i < cnt; i++) { const uint32_t pn = vm_read32(arr + i * 4); if (pn < 0x10000u || pn >= 0x30000000u) continue;
            const uint32_t par = vm_read32(pn + 0x04); if (par != row3[0] && par != row3[1]) continue;
            const uint32_t h = vm_read32(pn + 0x108);
            if (h == h_ptx && comtex && vm_read32(pn + 0x14) != comtex) { vm_write32(pn + 0x14, comtex); vm_write32(pn + 0x10, vm_read32(pn + 0x10) | 1u); }   /* as func_00333FCC(pane, picture): +0x14 and bit 0 of +0x10 */
            if (h == h_ntx) { const uint32_t tex = g_vsn_tex[par == row3[0] ? 0 : 1];
                if (tex) { if (vm_read32(pn + 0x14) != tex) { vm_write32(pn + 0x14, tex); vm_write32(pn + 0x10, vm_read32(pn + 0x10) | 1u); } if (!(vm_read32(pn + 0x100) & 1u)) vm_write32(pn + 0x100, vm_read32(pn + 0x100) | 1u); }
                else if (vm_read32(pn + 0x100) & 1u) vm_write32(pn + 0x100, vm_read32(pn + 0x100) & ~1u); } }
        return;
    }
    /* a test run: at one scale for all, the two former Support boxes came out lower than the others (their
     * frame is a wider, lower shape). They are scaled up evenly until they are as high as the fighter boxes (factor ks of
     * the fighter boxes' scale), which makes them wider: the columns have two widths. */
    /* measured in two test runs (0.8 / 1.143 and 0.72 / 1.53): a fighter box is about 144 high per unit of scale, a
     * Support box about 83 high and 166 wide; both groups' origin is the box's top left corner (to a few pixels). */
    /* the maintainer 2026-11-19: the two middle boxes should be the same boxes as the other four. The Support frame is stretched to
     * the fighter box's shape (x by ksx, y by ks: a fighter box is about 250 x 144 per unit, a Support box 166 x 83), and
     * its portrait is not stretched: it gets the fighter boxes' own size and place in the box (scale 1, at 22,-52). All six
     * columns are one width then. */
    static float sc = 0.78f, x0 = 95.0f, y0 = 112.0f, ks = 1.70f, ksx = 1.27f; static bool rdenv = false, boxoff = false;   /* a test run: 1.506 was about a tenth too wide */
    if (!rdenv) { rdenv = true; if (const char* e = std::getenv("SIX_VSBOX")) { if (e[0] == '0' && !e[1]) boxoff = true; else std::sscanf(e, "%f %f %f %f %f", &sc, &x0, &y0, &ks, &ksx); } }
    if (boxoff) return;
    vsc_setup(ctx, lay);
    struct W { uint32_t pn; uint32_t x, y, sx, sy; }; static W wrote[80]; static int nw = 0; static uint32_t wlay = 0;
    if (wlay != lay) { wlay = lay; nw = 0; }
    static const int col_of_box[4] = { 0, 1, 4, 5 };
    /* the maintainer 2026-11-19: the two middle boxes should BE fighter boxes, not stretched Support boxes. A pane's picture and
     * shape come from its clip (+0xE4), and panes of one symbol share it (dump of a test run: all four
     * p_vs_chara_fr_000_00b have one clip pointer). So each Support box pane is given the clip of the fighter box pane
     * that does the same job (frame parts, deco, mask, portrait), and is then placed and scaled exactly like a fighter
     * box. The frame's colour is its clip frame (+0xF8: 0 red, 1 blue). Tried and NOT working (see below): off unless SIX_VSCLIP=1; without it the Support frames are stretched to the fighter boxes' size. */
    static const bool clipswap = [] { const char* e = std::getenv("SIX_VSCLIP"); return e && e[0] == '1'; }();   /* OFF: a test run showed the Support frames unchanged (the picture does not follow +0xE4 alone) and a solid red plate; SIX_VSCLIP=1 to try again */
    uint32_t bclip[6] = { 0, 0, 0, 0, 0, 0 };   /* fr_000, deco_l, fr_001, mask, fr_002, portrait (of box 1) */
    if (clipswap) for (uint32_t i = 0; i < cnt; i++) { const uint32_t pn = vm_read32(arr + i * 4); if (pn < 0x10000u || pn >= 0x30000000u || vm_read32(pn) != 0x00882EF0u) continue;
        static const uint32_t hb[6] = { six_name_hash("p_vs_chara_fr_000_001"), six_name_hash("i_fr_deco_l_001"), six_name_hash("p_vs_chara_fr_001_001"), six_name_hash("p_vs_chara_fr_mask_001"),
                                        six_name_hash("p_vs_chara_fr_002_001"), six_name_hash("p_rgb8_256_chara_ico_001") };
        const uint32_t h = vm_read32(pn + 0x108); for (int q = 0; q < 6; q++) if (h == hb[q]) bclip[q] = vm_read32(pn + 0xE4); }
    const bool swap_ok = clipswap && bclip[0] && bclip[1] && bclip[2] && bclip[3] && bclip[4] && bclip[5];
    for (uint32_t i = 0; i < cnt; i++) { const uint32_t pn = vm_read32(arr + i * 4); if (pn < 0x10000u || pn >= 0x30000000u || vm_read32(pn) != 0x00882EF0u) continue;
        char nm[40]; nm[0] = 0; const uint32_t np = vm_read32(pn + 0x104); if (np >= 0x10000u && np < 0x30000000u) six_read_name(np, nm, 40);
        const size_t L = std::strlen(nm); if (L < 6) continue;
        if (!std::strcmp(nm, "p_vs_chara_tx_001") || !std::strncmp(nm, "i_vs_sp_deco_", 13)) { if (vm_read32(pn + 0x100) & 1u) vm_write32(pn + 0x100, vm_read32(pn + 0x100) & ~1u); continue; }
        int big = -1, small = -1;   /* which group */
        const int last = nm[L - 1] - '0';
        if (!std::strncmp(nm, "p_vs_chara_fr_s_", 16) || !std::strncmp(nm, "i_fr_deco_s_", 12) || !std::strncmp(nm, "p_mask_s_", 9)) small = last;
        else if (!std::strcmp(nm, "p_rgb8_256_chara_ico_004") || !std::strcmp(nm, "_ca_005")) small = 0;
        else if (!std::strcmp(nm, "p_rgb8_256_chara_ico_005") || !std::strcmp(nm, "_ca_006")) small = 1;
        else if (!std::strncmp(nm, "p_vs_chara_fr_", 14) || !std::strncmp(nm, "i_fr_deco_l_", 12) || !std::strncmp(nm, "p_rgb8_256_chara_ico_", 21)) big = last;
        else if (!std::strncmp(nm, "_ca_00", 6) && last >= 1 && last <= 4) big = last - 1;
        if ((big < 0 || big > 3) && (small < 0 || small > 1)) continue;
        if (small >= 0 && g_vsc.on) { if (vm_read32(pn + 0x100) & 1u) vm_write32(pn + 0x100, vm_read32(pn + 0x100) & ~1u); continue; }   /* the copies stand in for the Support boxes */
        if (small >= 0 && swap_ok) {
            int job = -1;
            if (!std::strncmp(nm, "p_vs_chara_fr_s_000_", 20)) job = 0; else if (!std::strncmp(nm, "i_fr_deco_s_", 12)) job = 1; else if (!std::strncmp(nm, "p_vs_chara_fr_s_001_", 20)) job = 2;
            else if (!std::strncmp(nm, "p_mask_s_", 9)) job = 3; else if (!std::strncmp(nm, "p_vs_chara_fr_s_002_", 20)) job = 4; else if (!std::strncmp(nm, "p_rgb8_256_chara_ico_", 21)) job = 5;
            if (job >= 0 && vm_read32(pn + 0xE4) != bclip[job]) vm_write32(pn + 0xE4, bclip[job]);
            if (job == 0 && vm_read32(pn + 0xF8) != (uint32_t)small) vm_write32(pn + 0xF8, (uint32_t)small);   /* red / blue */
            /* placed outright as a fighter box's pane: the box origin, the portrait 22,-52 from it, the clip area -4,-76 at 0.23 x 0.55 */
            const float bx = x0 + 226.0f * sc * (float)(2 + small), by = y0;
            float px = bx, py = by, sx = sc, sy = sc;
            if (job == 5) { px = bx + 22.0f * sc; py = by - 52.0f * sc; }
            else if (job < 0) { px = bx - 4.0f * sc; py = by - 76.0f * sc; sx = 0.23f * sc; sy = 0.55f * sc; }   /* _ca_005 / _ca_006 */
            wr(pn + 0x98, px); wr(pn + 0xA4, py); wr(pn + 0x90, sx); wr(pn + 0xA0, sy);
            continue; }
        int wi = 0; while (wi < nw && wrote[wi].pn != pn) wi++;
        if (wi < nw && wrote[wi].x == vm_read32(pn + 0x98) && wrote[wi].y == vm_read32(pn + 0xA4) && wrote[wi].sx == vm_read32(pn + 0x90) && wrote[wi].sy == vm_read32(pn + 0xA0)) continue;   /* ours still stands */
        float ox, oy, kx, ky, nx, ny;
        const float wb = 226.0f * sc;
        if (big >= 0 && big <= 3) { ox = 149.0f + 226.0f * (float)big; oy = 112.0f; kx = ky = sc; nx = x0 + wb * (float)col_of_box[big]; ny = y0; }
        else { ox = small == 0 ? 86.0f : 952.0f; oy = 276.0f; kx = sc * ksx; ky = sc * ks; nx = x0 + wb * (float)(2 + small); ny = y0 + 3.0f; }   /* a test run: 1.366 still covered the next box's edge, and the box sat 3 to 4 px high */
        if (small >= 0 && !std::strncmp(nm, "p_rgb8_256_chara_ico_", 21)) {   /* the portrait: as in a fighter box */
            /* a test run: at the fighter boxes' own portrait scale it came out too small (this portrait pane is a smaller
             * shape); it keeps its place in the frame and grows evenly by the frame's height factor */
            wr(pn + 0x98, nx + (rd(pn + 0x98) - ox) * kx); wr(pn + 0xA4, ny + (rd(pn + 0xA4) - oy) * ky);
            wr(pn + 0x90, rd(pn + 0x90) * ky); wr(pn + 0xA0, rd(pn + 0xA0) * ky); }
        else {
            wr(pn + 0x98, nx + (rd(pn + 0x98) - ox) * kx); wr(pn + 0xA4, ny + (rd(pn + 0xA4) - oy) * ky);
            wr(pn + 0x90, rd(pn + 0x90) * kx); wr(pn + 0xA0, rd(pn + 0xA0) * ky); }
        if (wi == nw && nw < 80) nw++;
        if (wi < 80) { wrote[wi].pn = pn; wrote[wi].x = vm_read32(pn + 0x98); wrote[wi].y = vm_read32(pn + 0xA4); wrote[wi].sx = vm_read32(pn + 0x90); wrote[wi].sy = vm_read32(pn + 0xA0); } }
    vsc_frame(ctx, 226.0f * sc);
}
extern "C" void jsp_six_calc_leave(ppu_context* ctx)
{
    vs_dump(t_six_calc_layout);
    vs_layout_frame(ctx, t_six_calc_layout);
    /* the names part goes before the portraits part (the X flash): vs_layout_frame stops there, but the copied boxes
     * must keep following their sources until the portraits part goes, or they vanish first (a test run c20) */
    if (!g_vs_part && g_vsc.on && t_six_calc_layout == g_vsc.lay) vsc_frame(ctx, g_vsc.colw);


    if (!six_live()) {   /* a 2v2 is being set up: the copies of an earlier 3v3 are let go */
        if (g_six_cs.layout) {
            if (g_six_cs_list && vm_read32(g_six_cs_list + 0x08) == g_six_cs_list_arr) { vm_write32(g_six_cs_list + 0x08, g_six_cs_list_orig_arr); vm_write32(g_six_cs_list + 0x0C, g_six_cs_list_orig_cnt); }
            g_six_cs_list = 0; g_six_cs_root_tb = 0; g_six_cs.layout = 0; g_six_2p.store(0);
            std::printf("[sixcs] 2v2: the 3v3 copies are let go\n"); std::fflush(stdout); }
        return; }
    if (g_six_cs.layout && t_six_calc_layout == g_six_cs.layout) six_cs_frame(ctx);
}
static void six_layout_dump(uint32_t layout, uint32_t lr, uint32_t at);
extern "C" void jsp_find_enter(ppu_context* ctx)
{
    t_find_layout = (uint32_t)ctx->gpr[3]; t_find_hash = (uint32_t)ctx->gpr[4];
    static const bool cl_env = std::getenv("SIX_CSCLONE") != nullptr; const bool cl = cl_env && six_live();
    if (!cl) return;
    const uint32_t lr = (uint32_t)ctx->lr;
    /* Stage Select's team bar (layout 1A037BF0 in a test run) has EIGHT face panes
     * p_rgb8_256_chara_ico_000..007, looked up from 0x00274C8C in the order 000 001 002 006 | 003 004 005 007: three
     * fighter boxes and one Support box per team. The game puts slot 4 / 5 into 006 / 007 (Support) and leaves 002 /
     * 005 empty; for 3v3 the two are swapped at the lookup, in both pane families seen (0x89738xxx faces, 0xA557Bxxx).
     * SIX_SSBOX=0 turns it off. */
    { static const bool sb = [] { const char* e = std::getenv("SIX_SSBOX"); return !(e && e[0] == '0'); }();
      if (sb && lr >= 0x00274000u && lr < 0x00276000u) {
          static const uint32_t sw[8][2] = { { 0x89738A10u, 0x89738C34u }, { 0x89738C34u, 0x89738A10u }, { 0x89738BABu, 0x89738CBDu }, { 0x89738CBDu, 0x89738BABu },
                                             { 0xA557BD03u, 0xA557BF27u }, { 0xA557BF27u, 0xA557BD03u }, { 0xA557BE9Eu, 0xA557BFB0u }, { 0xA557BFB0u, 0xA557BE9Eu } };
          for (const auto& w : sw) if (t_find_hash == w[0]) { ctx->gpr[4] = w[1]; t_find_hash = w[1]; break; } } }
    { static const bool sd = std::getenv("SIX_SSDUMP") != nullptr;   /* probe: Stage Select's team bar uses func_0032B230 (callers 0x00272000..0x00278000) */
      if (sd && lr >= 0x00272000u && lr < 0x00278000u && t_find_layout >= 0x10000u && t_find_layout < 0x30000000u) {
          static uint32_t seen[160][2]; static int ns = 0;
          bool k = false; for (int i = 0; i < ns; i++) k |= seen[i][0] == lr && seen[i][1] == t_find_hash;
          if (!k && ns < 160) { seen[ns][0] = lr; seen[ns][1] = t_find_hash; ns++; std::printf("[ssfind] lr %08X layout %08X hash %08X slot %u\n", lr, t_find_layout, t_find_hash, t_six_kind_slot); std::fflush(stdout); }
          six_layout_dump(t_find_layout, lr, 3000u); } }
    { static const bool fl = std::getenv("SIX_CSFACELOG") != nullptr; static int n = 0;   /* probe: who asks for the Support face panes, and when */
      if (fl && n < 300 && g_six_cs.layout && t_find_layout == g_six_cs.layout) { static const uint32_t h4 = six_name_hash("p_rgb8_256_chara_ico_004"), h5 = six_name_hash("p_rgb8_256_chara_ico_005");
          const uint32_t h = t_find_hash; if (h == h4 || h == h5 || h == h4 + 1u || h == h5 + 1u) { n++;
              std::printf("[sixface] find lr %08X slot %u hash %s%s layout %08X (ours %08X) face panes %08X/%08X hash now %08X\n", lr, t_six_kind_slot,
                          (h == h4 || h == h4 + 1u) ? "004" : "005", (h == h4 || h == h5) ? "" : "+1", t_find_layout, g_six_cs.layout,
                          g_six_cs_sup_face[0], g_six_cs_sup_face[1], g_six_cs_sup_face[0] ? vm_read32(g_six_cs_sup_face[0] + 0x108) : 0u); std::fflush(stdout); } } }
    if (lr < 0x00185000u || lr >= 0x001A2000u) return;
    if (g_six_cs.layout == t_find_layout) six_cs_rebuilt();
    six_cs_clone(ctx, t_find_layout);
    if (g_six_cs.layout != t_find_layout) return;
    const uint32_t sl = t_six_kind_slot;
    { static int lg = 0; static const bool log = std::getenv("SIX_CSLOG") != nullptr; if (log && lg < 600) { lg++; std::printf("[sixcsf] lr %08X slot %u hash %08X\n", lr, sl, t_find_hash); } }
    /* The slot number here is the last one given to func_00184E80 on this thread, and it goes stale: the per-frame slot
     * loop ends on slot 5, so a lookup made afterwards for another slot (the label play when slot 3 gets its fighter,
     * for one) still read "slot 5" and was sent to the blue copy. Its face clip then ran the "has a fighter" frame
     * (+0xF8 0x0E), dropped its child, and a fighter picked on the new box later showed a white, empty face (runs
     * 2026-11-18_2039/2041 face dumps; also why slot 6 "reacted" when slot 4 picked, the maintainer). So per call site the
     * hashes the four original slots ask are kept, and a lookup with one of those is never sent to a copy; and what
     * slot 4 and slot 5 themselves ask at a site decides the team, not the stale number. SIX_CSNOSTALE=1 = as before. */
    static struct SixSite2 { uint32_t lr, legit[4], g[2]; int nl; } site2[128]; static int nsite2 = 0;
    static const bool nostale = std::getenv("SIX_CSNOSTALE") != nullptr;
    SixSite2* s2 = nullptr;
    if (!nostale) { for (int i = 0; i < nsite2 && !s2; i++) if (site2[i].lr == lr) s2 = &site2[i];
        if (!s2 && nsite2 < 128) { s2 = &site2[nsite2++]; s2->lr = lr; s2->nl = 0; s2->g[0] = s2->g[1] = 0; }
        if (s2) { bool is_legit = false; for (int k = 0; k < s2->nl; k++) is_legit |= s2->legit[k] == t_find_hash;
            if (sl <= 3u) { if (!is_legit && s2->nl < 4 && t_find_hash != s2->g[0] && t_find_hash != s2->g[1]) s2->legit[s2->nl++] = t_find_hash; }
            else if (sl == 4u || sl == 5u) {
                if (is_legit) { static int n = 0; if (n++ < 8) { std::printf("[sixcs] lookup at %08X asks an original slot's pane (%08X) with the slot number still at %u: left alone\n", lr, t_find_hash, sl); std::fflush(stdout); } return; }
                if (!s2->g[sl - 4u] && t_find_hash != s2->g[5u - sl]) s2->g[sl - 4u] = t_find_hash; } } }
    uint32_t sl_use = sl;
    if (s2 && (sl == 4u || sl == 5u)) { if (t_find_hash == s2->g[0] && s2->g[0] != s2->g[1]) sl_use = 4u; else if (t_find_hash == s2->g[1] && s2->g[0] != s2->g[1]) sl_use = 5u; }
    if (sl == 1u || sl == 3u) {   /* remember what the partner slots ask, per call site */
        const int t = sl == 1u ? 0 : 1; int i = 0; while (i < g_six_cs_nsite[t] && g_six_cs_site[t][i].lr != lr) i++;
        if (i == g_six_cs_nsite[t] && i < 48) g_six_cs_nsite[t]++;
        if (i < 48) g_six_cs_site[t][i] = { lr, t_find_hash };
        return; }
    if (sl != 4u && sl != 5u) return;
    const int t = sl_use == 4u ? 0 : 1;
    for (int i = 0; i < g_six_cs_nsite[t]; i++) if (g_six_cs_site[t][i].lr == lr) {
        const uint32_t h = g_six_cs_site[t][i].h;
        for (int k = 0; k < g_six_cs.nm[t]; k++) if (g_six_cs.map[t][k].from == h) { ctx->gpr[4] = g_six_cs.map[t][k].to; t_find_hash = g_six_cs.map[t][k].to; return; }
        break; }
    const SixCsRemap* rm = sl_use == 4u ? g_six_cs.r4 : g_six_cs.r5; const int nr = sl_use == 4u ? g_six_cs.n4 : g_six_cs.n5;
    for (int i = 0; i < nr; i++) if (rm[i].from == t_find_hash) { ctx->gpr[4] = rm[i].to; t_find_hash = rm[i].to; break; }
}
/* func_00335F00(pane, labelHash): finds the label in [pane+0xE4] (func_00336CD8) and, when found, sets pane+0xF8 and
 * pane+0xEC |= 4 (go to that frame and play). The slot code calls it on the panes it looked up by hash, for slot 4 / 5
 * that is a COPY (func_001997C0 at 0x00199904 on the face clip, at 0x001999B4 on the frame). A copy cannot run its
 * clip: with the partner slot already holding a fighter, the copied face clip played frames 2..0x0E, took its child
 * out of record 0 and never put it into record 2, and the new box's picture stayed white (a test run,
 * _2041 face dump: child reset, parent 0, hash 0). With the partner on Random the same call found no label and did
 * nothing (seen in a test run). So for a copy the label is made unfindable: copies are driven by mirroring only.
 * SIX_CSLABELOK=1 lets the calls through as before. */
extern "C" void jsp_six_label_enter(ppu_context* ctx)
{
    static const bool on_env = std::getenv("SIX_CSCLONE") != nullptr && std::getenv("SIX_CSLABELOK") == nullptr; const bool on = on_env && six_live();
    if (!on || !g_six_cs.layout) return;
    const uint32_t pn = (uint32_t)ctx->gpr[3];
    for (int i = 0; i < g_six_cs_npair; i++) if (g_six_cs_pair[i][1] == pn) {
        static int said = 0; if (said++ < 12) { char nm[40]; nm[0] = 0; const uint32_t np = vm_read32(pn + 0x104); if (np >= 0x10000u && np < 0x30000000u) six_read_name(np, nm, 40);
            std::printf("[sixcs] label %08X asked on the copy '%s' (%08X) from %08X: not played\n", (uint32_t)ctx->gpr[4], nm, pn, (uint32_t)ctx->lr); std::fflush(stdout); }
        ctx->gpr[4] = 0xDEAD0001u;
        return; }
    /* The Support plates' own panes: with the 3v3 lock they never change, but the game still plays slot 4 / 5's pick
     * effect on the plate's face clip (its child goes away for some frames: the plate flashed white while the new box
     * played its effect, a test run k6). Not let through either, once the screen is at rest. */
    if (!six_cs_locked() || !g_six_rest_ok || g_six_cs_leaving || g_six_cs_transition || g_six_hold_gen != g_six_cs_gen) return;
    for (int q = 0; q < g_six_sup_nrest; q++) if (g_six_sup_rest[q].pane == pn) {
        static int said = 0; if (said++ < 16) { char nm[40]; nm[0] = 0; six_read_name(g_six_sup_rest[q].name, nm, 40);
            std::printf("[sixcs] label %08X asked on the Support plate pane '%s' (%08X) from %08X: not played\n", (uint32_t)ctx->gpr[4], nm, pn, (uint32_t)ctx->lr); std::fflush(stdout); }
        ctx->gpr[4] = 0xDEAD0001u;
        return; }
}
extern "C" void jsp_six_label_leave(ppu_context*) {}
/* func_0032B204(layout, hash, childHash): the face setters (func_001980C8, func_00197E68) find their pane with this one,
 * not with func_0032B230. After a return from Stage Select they ran before the copies were made again and put slot
 * 4/5's face on the Support plates (a test run: no func_0032B230 lookup of the face at all). */
static void six_layout_dump(uint32_t layout, uint32_t lr, uint32_t at);
extern "C" void jsp_find2_enter(ppu_context* ctx)
{
    static const bool cl_env = std::getenv("SIX_CSCLONE") != nullptr; const bool cl = cl_env && six_live();
    if (!cl) return;
    const uint32_t lay = (uint32_t)ctx->gpr[3], lr = (uint32_t)ctx->lr;
    { static const bool sd = std::getenv("SIX_SSDUMP") != nullptr;   /* probe: Stage Select's layout (callers 0x00272000..0x00278000): pane list on its 200th lookup, and each caller / hash pair once */
      if (sd && lr >= 0x00272000u && lr < 0x00278000u && lay >= 0x10000u && lay < 0x30000000u) {
          static uint32_t seen[96][3]; static int ns = 0; const uint32_t h = (uint32_t)ctx->gpr[4], h2 = (uint32_t)ctx->gpr[5];
          bool k = false; for (int i = 0; i < ns; i++) k |= seen[i][0] == lr && seen[i][1] == h && seen[i][2] == h2;
          if (!k && ns < 96) { seen[ns][0] = lr; seen[ns][1] = h; seen[ns][2] = h2; ns++; std::printf("[ssfind2] lr %08X layout %08X hash %08X child %08X\n", lr, lay, h, h2); std::fflush(stdout); }
          six_layout_dump(lay, lr, 200u); } }
    if (lr < 0x00185000u || lr >= 0x001A2000u) return;
    if (g_six_cs.layout == lay) six_cs_rebuilt();
    six_cs_clone(ctx, lay);
    { static const bool fl = std::getenv("SIX_CSFACELOG") != nullptr; static int n = 0;
      if (fl && n < 200) { static const uint32_t h4 = six_name_hash("p_rgb8_256_chara_ico_004"), h5 = six_name_hash("p_rgb8_256_chara_ico_005");
          const uint32_t h = (uint32_t)ctx->gpr[4]; if (h == h4 || h == h5) { n++;
              std::printf("[sixface] find2 lr %08X hash %s layout %08X (ours %08X) Support face hash now %08X\n", lr, h == h4 ? "004" : "005", lay, g_six_cs.layout,
                          g_six_cs_sup_face[0] ? vm_read32(g_six_cs_sup_face[0] + 0x108) : 0u); std::fflush(stdout); } } }
}
extern "C" void jsp_find2_leave(ppu_context*) {}
/* SIX_LAYDUMP=1 (probe C5, game map notes charsel.md 7.9): the first time a layout is searched from the Character Select
 * slot drawing (callers 0x00198000-0x0019B000), dump its flat pane list [layout+0x14]+0x08 (count +0x0C): index, pane,
 * name, parent, file position (+0x98/+0xA4), file scale (+0x90/+0xA0), shape (+0xE8), visible (+0x100). */
static void six_layout_dump(uint32_t layout, uint32_t lr, uint32_t at)
{
    static uint32_t seen[8], calls[8]; static int ns = 0;   /* dump on a layout's 3000th lookup: it is built by then */
    int si = 0; while (si < ns && seen[si] != layout) si++;
    if (si == ns) { if (ns >= 8) return; seen[ns] = layout; calls[ns] = 0; ns++; }
    if (++calls[si] != at) return;
    const uint32_t list = vm_read32(layout + 0x14);
    if (list < 0x10000u || list >= 0x30000000u) return;
    const uint32_t arr = vm_read32(list + 0x08), cnt = vm_read32(list + 0x0C);
    std::printf("[sixlay] layout %08X (lr %08X) list %08X panes %u at %08X\n", layout, lr, list, cnt, arr);
    (void)0;
    auto fl = [](uint32_t a) { union { uint32_t w; float f; } u; u.w = vm_read32(a); return u.f; };
    for (uint32_t i = 0; i < cnt && i < 600; i++) {
        const uint32_t pn = vm_read32(arr + i * 4);
        if (pn < 0x10000u || pn >= 0x30000000u) continue;
        const uint32_t par = vm_read32(pn + 0x04);
        int pi = -1; for (uint32_t k = 0; k < cnt && k < 600; k++) if (vm_read32(arr + k * 4) == par) { pi = (int)k; break; }
        char nm[40]; { const uint32_t a = vm_read32(pn + 0x104); int q = 0; if (a >= 0x10000u && a < 0x30000000u) for (; q < 39; q++) { const char c = (char)vm_base[a + q]; if (!c) break; nm[q] = (c >= 32 && c < 127) ? c : '?'; } nm[q] = 0; }
        std::printf("[sixlay] %3u %08X %-30s hash %08X parent %3d flags %08X vis %u vt %08X m3", i, pn, nm, vm_read32(pn + 0x108), pi, vm_read32(pn + 0x10), vm_read32(pn + 0x100) & 1u, vm_read32(pn));
        for (int k = 0; k < 9; k++) std::printf(" %.2f", fl(pn + 0x90 + k * 4));
        std::printf(" tr %.1f %.1f sc %.2f %.2f shape %08X\n", fl(pn + 0x18), fl(pn + 0x1C), fl(pn + 0x20), fl(pn + 0x24), vm_read32(pn + 0xE8));
        if (!std::strcmp(nm, "p_waku_001") || !std::strcmp(nm, "p_rgb8_256_chara_ico_001") || !std::strcmp(nm, "p_com_star_001")) {   /* raw block for the clone plan */
            for (uint32_t o = 0; o < 0x300u; o += 0x20u) { std::printf("[sixraw] %s +%03X:", nm, o); for (uint32_t q = 0; q < 0x20u; q += 4) std::printf(" %08X", vm_read32(pn + o + q)); std::printf("\n"); } }
    }
    std::fflush(stdout);
}
extern "C" void jsp_find_leave(ppu_context* ctx)
{
    { static const bool ld = std::getenv("SIX_LAYDUMP") != nullptr; const uint32_t lr = (uint32_t)ctx->lr;
      if (ld && lr >= 0x00185000u && lr < 0x001A2000u && t_find_layout >= 0x10000u && t_find_layout < 0x30000000u) six_layout_dump(t_find_layout, lr, 3000u); }
    static const bool mp = std::getenv("MAP_PROBE") != nullptr;
    if (!mp) return;
    const uint32_t h = t_find_hash;
    if (h != 0x9F7C3307u && h != 0x3057B94Cu && h != 0x2C436821u && h != 0x9E96AF57u && h != 0xD859C045u) return;
    static int n = 0; if (n >= 24) return; n++;
    const uint32_t p = (uint32_t)ctx->gpr[3];
    union { uint32_t w; float f; } x, y; x.w = pa_heap(p) ? vm_read32(p + 0x18) : 0; y.w = pa_heap(p) ? vm_read32(p + 0x1C) : 0;
    std::printf("[ko8] find layout %08X hash %08X -> pane %08X +104 %08X +108 %08X x %.2f y %.2f +100 %08X +10 %08X\n", t_find_layout, h, p,
                pa_heap(p) ? vm_read32(p + 0x104) : 0u, pa_heap(p) ? vm_read32(p + 0x108) : 0u, x.f, y.f, pa_heap(p) ? vm_read32(p + 0x100) : 0u, pa_heap(p) ? vm_read32(p + 0x10) : 0u);
    std::fflush(stdout);
}

/* ---- called from the pad filter (jsp_game.cpp) ---- */
void six_pad_lock(uint32_t port, uint16_t* b, uint16_t* lx, uint16_t* ly)
{
    /* 3v3 lock: Triangle on a slot is "Don't participate" (it took slots out and the 3v3 boxes fell apart, the maintainer
     * 2026-11-18). Dropped while Character Select is up. SIX_CSTRI=<state> drops it only in that player state. */
    { static const bool td = std::getenv("SIX_CSTEXDUMP") != nullptr; static bool was = false;   /* probe: R3 asks for a dump of the blue copies */
      if (td && port == 0) { const bool now = (*b & 0x0004u) != 0; if (now && !was) g_six_cs_dump_req.fetch_add(1); was = now; *b &= (uint16_t)~0x0004u; } }
    { static const bool dl = std::getenv("SIX_CSDRAWLOG") != nullptr; static bool was = false;   /* probe: R3 asks for a list of the screen's draws */
      if (dl && port == 0) { const bool now = (*b & 0x0004u) != 0; if (now && !was) g_six_draw_dump.fetch_add(1); was = now; *b &= (uint16_t)~0x0004u; } }
    if (port < 2 && six_cs_locked() && on_charsel()) {
        const uint32_t scr = g_seen_charsel.obj, w = scr + 0x570u;
        if (scr >= 0x10000u && scr < 0x30000000u && vm_read32(w) == 0x0087C570u) {
            const uint32_t open = vm_read32(w + 0x2Cu), row = vm_read32(w + 0x9Cu);
            { static uint32_t lo = 0xFFFFFFFFu, lr2 = 0xFFFFFFFFu; static int n = 0;   /* the two words, on change (first 40) */
              if ((open != lo || row != lr2) && n < 40) { n++; lo = open; lr2 = row; std::printf("[sixlock] partner window +0x2C %u, row +0x9C %u\n", open, row); std::fflush(stdout); } }
            static const int trow = [] { const char* e = std::getenv("SIX_CSTEAMROW"); return e ? std::atoi(e) : 0; }();
            if (open == 2u && row == (uint32_t)trow) {
                if (*b & 0x00A0u) { static int said = 0; if (said++ < 4) { std::printf("[sixlock] Left / Right dropped on the Team row (player %u)\n", port); std::fflush(stdout); } }
                *b &= (uint16_t)~0x00A0u; *lx = 128; } } }
    if (port < 2 && (*b & 0x1000u) && six_cs_locked() && on_charsel()) {
        static const int only = [] { const char* e = std::getenv("SIX_CSTRI"); return e ? std::atoi(e) : -1; }();
        const uint32_t so = g_six_state_obj.load();
        const uint32_t st = (so >= 0x10000u && so < 0x30000000u) ? vm_read32(so + 0x60u + 0x14u * port) : 0xFFFFFFFFu;
        if (only < 0 || st == (uint32_t)only) { *b &= (uint16_t)~0x1000u;
            static int said = 0; if (said++ < 4) { std::printf("[sixlock] Triangle dropped on Character Select (player %u state %u)\n", port, st); std::fflush(stdout); } } }
}
void six_pad_watch(uint32_t port, uint16_t* b)
{
    (void)port; (void)b;
        /* SIX_PROBE=1 (test only, the maintainer 11-05: can a match hold 6 fighters?): the battle setup table (0x009540E4,
         * 0x2C a slot) dumped, 12 slots, whenever the character select is left or a fight starts. FORCE_SLOTS=
         * "slot:id[:ctl[:stars]],..." writes those slots while the character select is up (+0x08 = 1 takes part,
         * +0x0C the character, +0x00 the controller, +0x18 the stars). */
        { static const bool sp = std::getenv("SIX_PROBE") != nullptr;
          static bool was_cs = false, was_fight = false;
          const bool cs = on_charsel(), fight = training_fight_scene() || vm_read32(0x0094DEC4u) == 0x14;
          const bool left_cs = was_cs && !cs;   /* FORCE_SLOTS: the Support slots (4, 5) are written only as the screen is left */
          /* "Re-select character(s)" goes from the battle straight to Character Select without the teardown paths
           * hooked below (the restore only ran at quit: a test run, black screen): opening
           * Character Select ends the 3v3 battle too (heaps back, units 4/5 Supports again, overrides off) */
          if (cs && g_six_in_battle && g_six_end_battle) g_six_end_battle("character select");
          if (sp && ((was_cs && !cs) || (fight && !was_fight) || (cs && !was_cs))) {
              std::printf("[six] setup at %s:\n", fight && !was_fight ? "fight start" : (cs && !was_cs) ? "entering character select" : "leaving character select");
              for (int k = 0; k < 12; k++) {
                  std::printf("[six]  slot %2d:", k);
                  for (uint32_t o = 0; o < 0x2C; o += 4) std::printf(" %08X", vm_read32(setup_field(k, o)));
                  std::printf("\n");
              }
              std::fflush(stdout);
              /* The battle world (func_00030A14: *(TOC-0x7A90)): per team a list of unit indices at +0x34 + team*0x18
               * (-1 = none; func_00031968 walks 4 of them), and unit records of 0x94 bytes at +0x60 + index*0x94. */
              { const uint32_t w = vm_read32(vm_read32(0x008D3888u - 0x7A90u));
                std::printf("[six] battle world %08X\n", w);
                if (w > 0x10000u && w < 0xF0000000u) {
                    for (int t = 0; t < 2; t++) {
                        std::printf("[six]  team %d units:", t);
                        for (int k = 0; k < 6; k++) std::printf(" %d", (int)(int32_t)vm_read32(w + 0x34 + t * 0x18 + k * 4));
                        std::printf("\n");
                    }
                    for (int u = 0; u < 8; u++) {
                        std::printf("[six]  unit %d:", u);
                        for (uint32_t o = 0; o < 0x40; o += 4) std::printf(" %08X", vm_read32(w + 0x60 + u * 0x94 + o));
                        std::printf("\n");
                    }
                }
                std::fflush(stdout); }
              /* Where else are the team's characters? The VS screen has three boxes a team (the maintainer's screenshot): look
               * for P1's character followed by the partner's at a fixed step, in the game's data and its heap, and
               * show the word one more step on (a third member's place?). */
              const uint32_t c0 = vm_read32(setup_field(0, 0x0C)), c1 = vm_read32(setup_field(1, 0x0C));
              const uint32_t c2 = vm_read32(setup_field(2, 0x0C)), c3 = vm_read32(setup_field(3, 0x0C));
              static const uint32_t ranges[][2] = { { 0x00800000u, 0x00A00000u }, { 0x10000000u, 0x10200000u }, { 0x40000000u, 0x4CA30000u } };
              int found = 0;
              /* the battle's own copy: all four fighters in slot order at one step, then what follows (Supports?) */
              if (fight && !was_fight) {
                  int nb = 0;
                  for (const auto& rg : ranges)
                      for (uint32_t ea = rg[0]; ea < rg[1] && nb < 30; ea += 4) {
                          if (vm_read32(ea) != c0) continue;
                          for (uint32_t st = 4; st <= 0x1000 && nb < 30; st += 4)
                              if (vm_read32(ea + st) == c1 && vm_read32(ea + 2 * st) == c2 && vm_read32(ea + 3 * st) == c3) {
                                  nb++;
                                  std::printf("[six] copy? %08X step %u: then %08X %08X | words before: %08X %08X %08X\n", ea, st,
                                              vm_read32(ea + 4 * st), vm_read32(ea + 5 * st), vm_read32(ea - 4), vm_read32(ea - 8), vm_read32(ea - 12));
                              }
                      }
                  std::fflush(stdout);
              }
              for (const auto& rg : ranges)
                  for (uint32_t ea = rg[0]; ea < rg[1] && found < 40; ea += 4) {
                      if (vm_read32(ea) != c0) continue;
                      for (uint32_t st = 4; st <= 0x200; st += 4) {
                          if (vm_read32(ea + st) != c1) continue;
                          /* the other team should sit nearby with the same step */
                          bool other = false; uint32_t ost = 0;
                          for (uint32_t o = 4; o <= 0x800 && !other; o += 4)
                              if (vm_read32(ea + o) == c2 && vm_read32(ea + o + st) == c3) { other = true; ost = o; }
                          if (!other) continue;   /* both teams with the same step: a team list, not chance */
                          std::printf("[six] %08X: %u then %u %+d later, next %08X | team 2 at %+d: %u %u, its next %08X\n", ea, c0, c1,
                                      (int)st, vm_read32(ea + 2 * st), (int)ost, c2, c3, vm_read32(ea + ost + 2 * st));
                          if (++found >= 40) break;
                      }
                  }
              std::fflush(stdout);
          }
          /* SIX_POKE="addr[,addr...]" (test only): from leaving the character select until the fight, write character
           * 34 at those addresses every poll, to see whether one of them is the VS screen's 3rd box. */
          { static bool between = false;
            if (was_cs && !cs) between = true;
            if (fight) between = false;
            g_six_loading = between;
            if (between) if (const char* e = std::getenv("SIX_POKE")) {
                const char* q = e;
                while (*q) { char* end; const uint32_t a = (uint32_t)std::strtoul(q, &end, 16); if (end == q) break;
                             if (a) vm_write32(a, 34u); q = (*end == ',') ? end + 1 : end; if (*end != ',') break; }
            } }
          /* Which TOC entries point at or near the setup table, so the game code that reads it can be found in the
           * recompiled sources by its r2 offset. Once. */
          { static bool toc_done = false;
            if (sp && !toc_done) { toc_done = true;
                const uint32_t toc = 0x008D3888u;
                for (int32_t off = -0x8000; off < 0x8000; off += 4) {
                    const uint32_t v = vm_read32(toc + (uint32_t)off);
                    if (v >= 0x00953F00u && v < 0x00954300u)
                        std::printf("[six] TOC %+d (r2 %s0x%X) = %08X (setup %+d)\n", off, off < 0 ? "-" : "+", off < 0 ? -off : off,
                                    v, (int)(v - 0x009540E4u));
                }
                std::fflush(stdout); } }
          six_watch_tick(cs);
          /* SIX_LOADDUMP=1 (test only): the 6 battle-world unit records in full, every 3 s while the match loads,
           * to compare a normal load with a stuck 3v3 one */
          { static const bool ld = std::getenv("SIX_LOADDUMP") != nullptr; static unsigned long long last = 0;
            const unsigned long long now = GetTickCount64();
            if (ld && g_six_loading && now - last > 3000) { last = now;
                const uint32_t w = vm_read32(vm_read32(0x008D3888u - 0x7A90u));
                if (w > 0x10000u && w < 0xF0000000u) {
                    std::printf("[sixload] t=%llu world %08X head", now, w);
                    for (uint32_t o = 0; o < 0x60; o += 4) std::printf(" %08X", vm_read32(w + o));
                    std::printf("\n");
                    for (int u = 0; u < 6; u++) {
                        std::printf("[sixload]  unit %d:", u);
                        for (uint32_t o = 0; o < 0x94; o += 4) std::printf(" %08X", vm_read32(w + 0x60 + (uint32_t)u * 0x94u + o));
                        std::printf("\n");
                    }
                    std::fflush(stdout);
                } } }
          /* SIX_3V3=1 (test only): while the match loads, every battle-world unit marked as a Support (+0x08 = 3) is
           * marked as a partner (2), so the battle builder (func_000ED344) makes it a fighter: 3 against 3. */
          { static const bool on3_env = std::getenv("SIX_3V3") && std::getenv("SIX_3V3")[0] == '1'; const bool on3 = on3_env && six_live();   /* mode 2: the role watch in six_veh */
            static const bool hold_env = std::getenv("SIX_HOLD_ROLES") != nullptr; const bool hold = hold_env && six_live();   /* keep the Supports until the load places are given out */
            if (!g_six_loading) g_six_builder_seen = false;
            if (on3 && g_six_loading && !g_six_in_loader && (!hold || g_six_builder_seen)) {
                const uint32_t w = vm_read32(vm_read32(0x008D3888u - 0x7A90u));
                if (w > 0x10000u && w < 0xF0000000u)
                    for (int u = 0; u < 6; u++) {
                        const uint32_t rec = w + 0x60 + (uint32_t)u * 0x94u;
                        static const int mask = [] { const char* e = std::getenv("SIX_3V3_MASK"); return e ? (int)std::strtol(e, nullptr, 16) : 0x30; }();   /* units to make fighters (hex) */
                        if (vm_read32(rec + 8) == 3u && ((mask >> u) & 1)) {
                            vm_write32(rec + 8, 2u);
                            static const bool kind = std::getenv("SIX_3V3_KIND") != nullptr;   /* +0x14 7 Support -> 8 fighter, +0x1C its own index */
                            if (kind) { vm_write32(rec + 0x14, 8u); vm_write32(rec + 0x1C, (uint32_t)u); }
                            static int n = 0;
                            if (n++ < 12) { std::printf("[six] unit %d: Support -> partner (char %u, team %u)\n", u, vm_read32(rec) >> 16, vm_read32(rec + 0xC)); std::fflush(stdout); }
                        }
                    }
            } }
          was_cs = cs; was_fight = fight;
          if (cs) if (const char* e = std::getenv("MAP_COM")) {   /* MAP_COM=<0-4> (test only, probe P-H5-1): COM level (+0x18) of slots 0-3 */
              const int lvl = std::atoi(e);
              for (int s = 0; s < 4; s++) vm_write32(setup_field(s, 0x18), (uint32_t)lvl);
          }
          if (cs) { static const bool csel_env = std::getenv("SIX_CSEL") != nullptr; const bool csel = csel_env && six_live();   /* 3rd-fighter slots must not keep a Support id */
              if (csel) for (int sl = 4; sl <= 5; sl++) { const uint32_t id = vm_read32(setup_field(sl, 0x0C));   /* +0x0C character, +0x08 state (0 picked, 1 random) */
                  if (id >= 200u && id <= 213u && vm_read32(setup_field(sl, 0x08)) == 0u) { vm_write32(setup_field(sl, 0x08), 1u);
                      static int said = 0; if (said++ < 4) { std::printf("[six] slot %d held Support %u: set to random fighter\n", sl, id); std::fflush(stdout); } } } }
          if (cs || left_cs) if (const char* e = std::getenv("FORCE_SLOTS")) {
              const char* q = e;
              while (*q) {
                  int s = -1, id = -1, ctl = -1, st = -1, used = 0;
                  if (std::sscanf(q, "%d:%d%n", &s, &id, &used) < 2) break;
                  q += used;
                  if (*q == ':') { int u2 = 0; if (std::sscanf(q, ":%d%n", &ctl, &u2) == 1) q += u2; }
                  if (*q == ':') { int u3 = 0; if (std::sscanf(q, ":%d%n", &st, &u3) == 1) q += u3; }
                  int team = -1;
                  if (*q == ':') { int u4 = 0; if (std::sscanf(q, ":%d%n", &team, &u4) == 1) q += u4; }
                  /* written on every Character Select poll (writing slots 4/5 only as the screen is left came too late:
                   * a test run loaded a real Support); the screen is safe as long as it does
                   * not OPEN with fighters in the Support slots, see the battle teardown */
                  if (cs && s >= 0 && s < 12) {
                      if (id < 0) { vm_write32(setup_field(s, 0x04), 1); vm_write32(setup_field(s, 0x08), 2); vm_write32(setup_field(s, 0x0C), 0); }   /* id -1: not taking part */
                      else { vm_write32(setup_field(s, 0x08), 0); vm_write32(setup_field(s, 0x0C), (uint32_t)id); }   /* +0x08: 0 picked (1 = Random Select: 2026-11-16 runs got random fighters with 1), 2 not taking part */
                      if (ctl >= 0) vm_write32(setup_field(s, 0x00), (uint32_t)ctl);
                      if (st >= 0) vm_write32(setup_field(s, 0x18), (uint32_t)st);
                      if (team >= 0) vm_write32(setup_field(s, 0x10), (uint32_t)team);   /* +0x10: the team, 0 or 1 */
                  }
                  if (*q == ',') q++; else break;
              }
          } }
}
/* called from the indirect-call hook (training_icall): the message fix and the fight start stamp */
void six_icall(uint32_t target, uint64_t* gpr)
{
    six_msg_fix(gpr);
    if (!g_six_fight_t && target == 0x00085CECu) g_six_fight_t = GetTickCount64();
}


/* the HUD-scale draw hook (jsp_game.cpp ui_scale) asks this first: the 3v3 Character Select layout */
float six_ui_scale(uint32_t rt_off, const float* box, float* ax, float* ay) { return six_cslay(rt_off, box, ax, ay); }

extern "C" void jsp_six_noop(ppu_context*) {}   /* the other side of hooks that only need one */

