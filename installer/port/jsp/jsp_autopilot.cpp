/* jsp_autopilot.cpp -- the test auto-pilot for the Fixes build (test only, off unless AUTO_FIGHT or AUTO_MENU is set).
 *
 * AUTO_FIGHT="<fighter> <opponent> <support> [stage]": the game walks itself from boot into a Free Battle with those
 * picks, pressing what a player would press, in about a minute (Test Fight v19.bat, bench). AUTO_MENU="<row>": the
 * same walk, stopping on that main menu row (0 J-Adventure .. 8 Options) and handing the pad back.
 *
 * The Features build has the same auto-pilot inside jsp_game.cpp (with Training Mode); there this file stays idle,
 * because jsp_game.cpp owns the two hooks. Here it uses the two toolkit hooks of patches 0500 (indirect calls: which
 * screen is up, when player 1 can move) and 6501 (the pad data the game reads), both unset in normal play.
 *
 * Screens are found the way jsp_game.cpp finds them: each screen calls its own virtual methods every frame, seen in
 * the indirect-call hook with the object in r3; a screen is up while its method was called in the last moment and
 * the object still has that vtable. Addresses are the US 1.00 EBOOT's (as the rest of the port). */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>        /* GetTickCount64 */

#include "ppu_recomp.h"     /* vm_read32, vm_write32 */

extern "C" int (*g_ppu_icall_hook)(uint32_t target, uint64_t* gpr);                                  /* 0500 */
extern "C" void (*g_cellpad_filter)(uint32_t port_no, uint16_t* buttons, uint16_t* lx, uint16_t* ly); /* 6501 */

namespace {

struct SeenScreen { volatile uint32_t obj; volatile unsigned long long ms; };
SeenScreen s_main, s_charsel, s_stage, s_stage_time;
volatile unsigned long long s_hud_ms = 0;

struct AutoState {
    bool on; int stage; int step; unsigned long long t0, t, press_until; uint16_t button; bool p1_moved;
    int me, opp, sup, menu_row;
};
AutoState g = { false, -1, 0, 0, 0, 0, 0, false, -1, -1, -1, -1 };

bool screen_up(const SeenScreen& sc, uint32_t vtable)
{
    const uint32_t obj = sc.obj;
    return obj && GetTickCount64() - sc.ms < 300 && vm_read32(obj) == vtable;
}

bool fight_hud_up() { return GetTickCount64() - s_hud_ms < 500; }
bool in_menu_scene() { return !fight_hud_up() && vm_read32(0x0094DEC4u) < 0xB; }
/* main menu list up, no submenu open (0x0095654C: 2 = Free Battle's 1 Player / 2 Players) */
bool main_list_open() { return in_menu_scene() && vm_read32(0x0095654Cu) == 0 && screen_up(s_main, 0x0087D600u); }
bool on_charsel() { return screen_up(s_charsel, 0x0087C3D0u); }
bool on_stagesel() { return screen_up(s_stage, 0x0087FCF8u) && screen_up(s_stage_time, 0x00880018u); }
uint32_t setup_field(int slot, uint32_t off) { return 0x009540E4u + 0x2Cu * (uint32_t)slot + off; }

void log_step(const char* what)
{
    std::printf("[auto] %s at %.1f s\n", what, (GetTickCount64() - g.t0) / 1000.0);
    std::fflush(stdout);
}

void press(uint16_t button, unsigned long long now, unsigned hold_ms, unsigned every_ms)
{
    if (now - g.t >= every_ms) { g.t = now; g.button = button; g.press_until = now + hold_ms; }
}

int icall(uint32_t target, uint64_t* gpr)
{
    const uint32_t r3 = (uint32_t)gpr[3];
    SeenScreen* sc = nullptr;
    switch (target) {
    case 0x001DE098u: sc = &s_main; break;        /* main menu list and 1P/2P, vtable 0x0087D600 */
    case 0x0019AA74u: sc = &s_charsel; break;     /* character select, vtable 0x0087C3D0 */
    case 0x0026EF94u: sc = &s_stage; break;       /* stage select, vtable 0x0087FCF8 */
    case 0x00279F0Cu: sc = &s_stage_time; break;  /* its time limit box, vtable 0x00880018 */
    case 0x00085CECu: s_hud_ms = GetTickCount64(); break;   /* the fight HUD reads a fighter's health */
    case 0x0004296Cu: if (g.on && vm_read32(r3 + 4) == 0) g.p1_moved = true; break;   /* P1 enters its first state */
    default: break;
    }
    if (sc) { sc->obj = r3; sc->ms = GetTickCount64(); }
    return 0;
}

void pad(uint32_t port, uint16_t* b, uint16_t* lx, uint16_t* ly)
{
    if (port != 0 || !g.on) return;
    const uint16_t CROSS = 0x4000, START = 0x0008, DOWN = 0x0040;
    const unsigned long long now = GetTickCount64();
    { static uint32_t last_scene = 0xFFFFFFFFu, last_screen = 0xFFFFFFFFu;
      const uint32_t scene = vm_read32(0x0094DEC4u), screen = vm_read32(0x0095654Cu);
      if (scene != last_scene || screen != last_screen) {
          last_scene = scene; last_screen = screen;
          std::printf("[auto] scene %X screen %X at %.1f s\n", scene, screen, (now - g.t0) / 1000.0);
          std::fflush(stdout);
      } }
    const bool menu = main_list_open();
    { static int last_step = -1; static unsigned long long step_t = 0, said_t = 0;   /* a step that does not move on shows in the log */
      if (g.step != last_step) { last_step = g.step; step_t = said_t = now; }
      else if (now - said_t >= 15000) { said_t = now;
          std::printf("[auto] still at step %d after %.0f s (character select %d, stage select %d, main menu %d)\n",
                      g.step, (now - step_t) / 1000.0, (int)on_charsel(), (int)on_stagesel(), (int)menu);
          std::fflush(stdout); } }
    switch (g.step) {
    case 0:   /* movie, title, save dialog: Left and Cross in turn until the main menu is up (with no save, Left then
               * Cross goes on without making one) */
        if (g.menu_row == -2 && vm_read32(0x0094DEC4u) == 3u) {   /* AUTO_MENU=title: the title screen is up */
            g.on = false; g.press_until = 0; g.button = 0;
            log_step("title screen reached");
            return;
        }
        if (g.menu_row == -2 && vm_read32(0x0094DEC4u) == 2u) {   /* the opening movie: one Cross skips it; no more presses, */
            static unsigned long long t_skip = 0;                 /* or one is still down when the title comes up */
            if (!t_skip || now - t_skip > 6000) { t_skip = now; g.t = now; g.button = CROSS; g.press_until = now + 120; }
            break;
        }
        if (menu) { g.step = 1; g.t = now; g.press_until = 0; log_step("main menu"); break; }
        if (now - g.t0 > 3000 && now - g.t >= 1000) {
            static bool left = false;
            press(left ? (uint16_t)0x0080 : CROSS, now, 120, 1000);
            left = !left;
        }
        break;
    case 1:   /* down to the row (Free Battle = 3), then in */
        if (now - g.t < 400 || !menu) break;
        if (g.menu_row >= 0) {
            if ((int)vm_read32(0x00956550u) != g.menu_row) { press(DOWN, now, 80, 300); break; }
            g.on = false; g.press_until = 0;
            log_step("menu row reached");
            return;
        }
        if (vm_read32(0x00956550u) != 3) { press(DOWN, now, 80, 300); break; }
        g.step = 2; g.t = now; g.button = CROSS; g.press_until = now + 120;
        log_step("Free Battle");
        break;
    case 2:   /* 1 Player, until character select is up */
        if (on_charsel()) { g.step = 3; g.t = now; g.press_until = 0; log_step("character select"); break; }
        press(CROSS, now, 120, 1500);
        break;
    case 3:   /* the picks go in on every poll (the game reads them when leaving the screen); Start until stage select */
        if (on_charsel()) {
            vm_write32(setup_field(0, 0x08), 0); vm_write32(setup_field(0, 0x0C), (uint32_t)g.me);
            vm_write32(setup_field(2, 0x08), 0); vm_write32(setup_field(2, 0x0C), (uint32_t)g.opp);
            vm_write32(setup_field(2, 0x18), 0);   /* opponent at 0 stars */
            if (g.sup >= 0) { vm_write32(setup_field(4, 0x08), 0); vm_write32(setup_field(4, 0x0C), (uint32_t)g.sup); }
        }
        if (on_stagesel()) { g.step = 4; g.t = now; g.press_until = 0; log_step("stage select"); break; }
        if (now - g.t >= 1500) press(START, now, 120, 1500);
        break;
    case 4: { /* no time limit (the screen's seconds +0x78 and the box's choice +0x40, as Training's lock), then the
               * stage: Cross opens the list, the value goes in at +0x2E4, Cross confirms; then Start and Cross in turn
               * (Start does nothing while the list is open) until the screen is gone */
        { static unsigned long long off_since = 0;   /* gone for 0.7 s, not for one poll */
          if (!on_stagesel()) {
              if (!off_since) off_since = now;
              if (now - off_since >= 700) { off_since = 0; g.step = 5; g.t = now; }
              break;
          }
          off_since = 0; }
        vm_write32(s_stage.obj + 0x78, 0);
        vm_write32(s_stage_time.obj + 0x40, 0);
        if (now - g.t < 500) break;
        static int phase = 0;
        static unsigned long long phase_t = 0;
        static bool cross_next = false;
        if (now - phase_t < 900) break;
        phase_t = now;
        if (g.stage >= 0 && phase < 3) {
            if (phase == 1) {
                std::printf("[auto] stage value was %u, now %d\n", vm_read32(s_stage.obj + 0x2E4u), g.stage);
                vm_write32(s_stage.obj + 0x2E4u, (uint32_t)g.stage);
            } else { g.button = CROSS; g.press_until = now + 200; }
            phase++;
            break;
        }
        g.button = (g.stage >= 0 && cross_next) ? CROSS : START; g.press_until = now + 200;
        cross_next = !cross_next;
        break; }
    case 5:   /* loading and intros (Cross + Start skips them) until player 1 can move */
        if (g.p1_moved) { g.on = false; g.press_until = 0; log_step("fight"); break; }
        { static unsigned long long back_since = 0;   /* stage select is up again: back to that step */
          if (on_stagesel()) {
              if (!back_since) back_since = now;
              if (now - back_since >= 1500) { back_since = 0; g.step = 4; g.t = now; g.press_until = 0; log_step("stage select again"); break; }
          } else back_since = 0; }
        press((uint16_t)(CROSS | START), now, 120, 1000);
        break;
    }
    *b = now < g.press_until ? g.button : 0;
    *lx = 128; *ly = 128;
}

}  // namespace

/* Called from stubs.cpp. Takes the hooks only when AUTO_FIGHT / AUTO_MENU is set and nothing else has them (in the
 * Features build jsp_game.cpp sets them and has its own auto-pilot). */
extern "C" void jsp_register_autopilot_overrides(void)
{
    if (const char* e = std::getenv("AUTO_FIGHT")) {
        int me = -1, opp = -1, sup = -1, stage = -1;
        if (std::sscanf(e, "%d %d %d %d", &me, &opp, &sup, &stage) >= 3) {
            g.me = me; g.opp = opp; g.sup = sup; g.stage = stage; g.on = true;
        }
    }
    /* AUTO_MENU=title (test only, demo-fight speed tests): stop when the title screen is up (scene 3; 2 is the opening
     * movie, which Cross skips), so the attract demo starts by itself. menu_row -2 marks it. */
    if (const char* e = std::getenv("AUTO_MENU")) { g.menu_row = (std::strcmp(e, "title") == 0) ? -2 : std::atoi(e); g.on = true; }
    if (!g.on) return;
    if (g_ppu_icall_hook || g_cellpad_filter) { g.on = false; return; }
    g.t0 = GetTickCount64();
    g_ppu_icall_hook = icall;
    g_cellpad_filter = pad;
    log_step("started");
}
