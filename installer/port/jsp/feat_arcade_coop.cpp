/* Arcade co-op: a 1 Player / 2 Players window for Arcade, with both players on the same team. On by default (ARC_COOP=0 off).
 *
 * Choosing Arcade (the game's row 2, on the Battle Modes page or the old menu) opens the port's two-button window
 * "1 Player" / "2 Players". With 2 Players, Player 2 plays Player 1's partner on the same team in every fight of that
 * Arcade run, in split screen; back on the main menu it is forgotten. Player 1 still picks both fighters.
 * ARC_COOP_ANSWER=1|2 (scripted runs, the auto pilot): no window, that answer is taken.
 *
 * How (from scripted runs): the setup table's slot (+0x14 "who drives") does NOT reach
 * Arcade's fights. In the battle world (*(TOC - 0x7A90)) the partner, unit 2 (+0x60 + 2 * 0x94), gets +0x20 = 1
 * (player; 3 CPU) and +0x1C = 1 (pad slot = Player 2's port, as Player 2's unit in Impossible 2 Players), and the world
 * word +0x18 = 1 (the 2-player word: 1 in Impossible 2 Players, 0 in Arcade); written while Arcade's battle list is up
 * (the units are filled there, unit state 0x11) and kept. Result: split screen, both on Red Team, Player 2's partner
 * idle without Player 2's input.
 * Probe switches kept: ARC_COOP_DUMP=1 (units and world head every 12 s). */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif
#include "ppu_recomp.h"

extern "C" int jsp_ac_main_menu_open(void);   // jsp_game.cpp
extern "C" int jsp_ac_on_charsel(void);       // jsp_game.cpp: Character Select is up
extern "C" int jsp_ac_on_stagesel(void);      // jsp_game.cpp: Stage Select is up
extern "C" int jsp_ac_charsel_slot(void);     // jsp_game.cpp: Character Select's slot cursor, -1 when the screen is not up
extern "C" void jsp_set_two_players(int on);  // jsp_game.cpp: Player 2's pad counts (as after Free Battle > 2 Players)
extern "C" int jsp_ac_main_list_open(void);   // jsp_game.cpp: the main list is up (no submenu)
extern "C" int jsp_ac_menu_page(void);        // jsp_game.cpp: 1 = the game's rows (Battle Modes page or the old menu)
extern "C" int cellOskDialog_overlay_active(void);
extern "C" int cellOskDialog_overlay_key(unsigned vk);
extern "C" int cellOskDialog_confirm_poll(void);
extern "C" int cellOskDialog_choice_open(const wchar_t* prompt, const wchar_t* left, const wchar_t* right);

namespace {
bool enabled() { static const bool on = [] { const char* e = std::getenv("ARC_COOP"); return !(e && e[0] == '0'); }(); return on; }
bool g_coop = false;   /* 2 Players chosen for this Arcade run */
}

/* ARC_COOP_DUMP=1 (any mode): the four fighter units of the battle world, 0x94 bytes each, once a fight's units are
 * built (unit +0x14 == 8 fighting) and again 20 s later; to compare a Player 2 unit (Free Battle 2 Players) with
 * Arcade's CPU partner. World: *(TOC - 0x7A90), units at +0x60 + i * 0x94. */
static void unit_dump()
{
    static const bool on = std::getenv("ARC_COOP_DUMP") != nullptr; if (!on) return;
    static uint32_t last_world = 0; static unsigned long long t_first = 0; static int dumps = 0;
    const uint32_t w0 = vm_read32(0x008D3888u - 0x7A90u);
    const uint32_t world = (w0 >= 0x00010000u && w0 < 0x30000000u) ? vm_read32(w0) : 0u;
    { static unsigned long long tt = 0; static int nt = 0; const unsigned long long n2 = GetTickCount64();
      if (n2 - tt >= 3000 && nt < 120) { tt = n2; nt++; std::printf("[arcdump] tick w0 %08X world %08X u0 state %u scene %08X\n", w0, world,
          (world >= 0x10000000u && world < 0x20000000u) ? vm_read32(world + 0x60u + 0x14u) : 0xFFFFFFFFu, vm_read32(0x0094DEC4u)); std::fflush(stdout); } }
    if (world < 0x10000000u || world >= 0x20000000u) { last_world = 0; return; }
    /* every 12 s, at most 25 times (the scene word is not one number for every fight: 0x12 / 0x14 / 0x0E seen) */
    const unsigned long long now = GetTickCount64();
    (void)last_world;
    if (dumps >= 25 || (t_first && now - t_first < 12000)) return; t_first = now;
    dumps++;
    std::printf("[arcdump] world %08X scene %X head:", world, vm_read32(0x0094DEC4u));   /* the 0x60 bytes before the units (team lists at +0x34) */
    for (uint32_t o = 0; o < 0x60u; o += 4) std::printf(" %08X", vm_read32(world + o));
    std::printf("\n");
    for (uint32_t k = 0; k < 4; k++) {
        const uint32_t u = world + 0x60u + 0x94u * k;
        std::printf("[arcdump] world %08X unit %u:", world, k);
        for (uint32_t o = 0; o < 0x94u; o += 4) std::printf("%s%08X", (o % 0x20u) ? " " : (o ? " | " : " "), vm_read32(u + o));
        std::printf("\n");
    }
    std::fflush(stdout);
}

/* 2 Players: unit 2 (the partner) as Player 2's unit and the world's 2-player word (see the top). ARC_COOP_PADSLOT=<n>
 * (test, default 1). */
static void unit_takeover()
{
    static const uint32_t slot = [] { const char* e = std::getenv("ARC_COOP_PADSLOT"); return e ? (uint32_t)std::atoi(e) : 1u; }();   /* 1 = port 1: Player 2 (Impossible 2 Players: P2's unit +0x1C is 1) */
    /* any scene while Arcade runs (written in the fight scene only, the first write came at unit state 0x11, after the
     * fighters were made, and nothing changed); off on Character Select / Stage Select */
    if (jsp_ac_on_charsel() || jsp_ac_on_stagesel()) return;
    const uint32_t w0 = vm_read32(0x008D3888u - 0x7A90u);
    const uint32_t world = (w0 >= 0x00010000u && w0 < 0x30000000u) ? vm_read32(w0) : 0u;
    if (world < 0x10000000u || world >= 0x20000000u) return;
    const uint32_t u = world + 0x60u + 0x94u * 2u;
    if (vm_read32(u + 0x0C) != 0u) return;   /* not on Player 1's team */
    if (vm_read32(u + 0x08) != 2u) return;   /* not the partner (role 2) */
    { static unsigned n18 = 0; if (vm_read32(world + 0x18) != 1u) { if (n18++ < 20) { std::printf("[arccoop] world +0x18 %u -> 1\n", vm_read32(world + 0x18)); std::fflush(stdout); } vm_write32(world + 0x18, 1u); } }
    const uint32_t c20 = vm_read32(u + 0x20), c1c = vm_read32(u + 0x1C);
    if (c20 != 1u || c1c != slot) {
        vm_write32(u + 0x20, 1u); vm_write32(u + 0x1C, slot);
        static unsigned n = 0;
        if (n++ < 30) { std::printf("[arccoop] fight unit 2 (char %u, state %u, scene %X): +20 %u -> 1, +1C %u -> %u\n", vm_read32(u) >> 16, vm_read32(u + 0x14), vm_read32(0x0094DEC4u), c20, c1c, slot); std::fflush(stdout); }
    }
}

/* The window, on Cross / Start with Arcade's row on the main list. Input of both pads is kept from the game while it is
 * up; the answer passes one fresh Cross on (as the team size window, feat 3v3). Backing out is not an answer. */
static bool ask_pad(uint32_t port, uint16_t* b)
{
    const uint16_t CROSS = 0x4000, CIRCLE = 0x2000, LEFT = 0x0080, RIGHT = 0x0020, START = 0x0008;
    static uint16_t prev_b[2] = { 0, 0 }, prev_dir = 0; static int state = 0, pulse = 0; static uint32_t owner = 0;
    if (port > 1) return false;
    const uint16_t cur = *b, pressed = (uint16_t)(cur & ~prev_b[port]); prev_b[port] = cur;
    if (state == 1) {
        if (port == owner) {
            if (!cellOskDialog_overlay_active()) {
                const int r = cellOskDialog_confirm_poll();   /* 1 the left button, 0 the right, else backed out */
                if (r == 1 || r == 0) { g_coop = r == 0; state = 2; pulse = 0;
                    std::printf("[arccoop] Arcade: %s\n", g_coop ? "2 Players (Player 2 plays the partner)" : "1 Player"); std::fflush(stdout); }
                else cellOskDialog_choice_open(L"Arcade", L"1 Player", L"2 Players");
            } else {
                const uint16_t dir = (uint16_t)((cur & LEFT) ? LEFT : (cur & RIGHT) ? RIGHT : 0);
                if (dir && dir != prev_dir) cellOskDialog_overlay_key(dir == LEFT ? 0x25u : 0x27u);
                prev_dir = dir;
                if (pressed & CROSS) cellOskDialog_overlay_key(0x0Du);
            }
        }
        *b = 0; return true;
    }
    if (state == 2) {
        if (port != owner) { *b = 0; return true; }
        if (pulse == 0 && cur != 0) { *b = 0; return true; }   /* wait for the answer's button to be let go */
        pulse++; *b = (uint16_t)((pulse >= 2 && pulse <= 5) ? CROSS : 0);
        if (pulse > 6) state = 3;
        return true;
    }
    if (state == 3) { if (!jsp_ac_main_list_open()) state = 0; else if (cur == 0 && port == owner) state = 0; }   /* the game took it */
    if (!jsp_ac_main_list_open() || jsp_ac_menu_page() != 1 || vm_read32(0x00956550u) != 2u) return false;
    if (state != 0 || !(pressed & (CROSS | START)) || (pressed & CIRCLE)) return false;
    static const int answer = [] { const char* e = std::getenv("ARC_COOP_ANSWER"); return e ? std::atoi(e) : 0; }();
    if (answer == 1 || answer == 2) { g_coop = answer == 2;   /* scripted: the Cross goes on to the game */
        std::printf("[arccoop] Arcade: %s (ARC_COOP_ANSWER)\n", g_coop ? "2 Players (Player 2 plays the partner)" : "1 Player"); std::fflush(stdout); return false; }
    if (!cellOskDialog_choice_open(L"Arcade", L"1 Player", L"2 Players")) return false;
    state = 1; owner = port; prev_dir = 0;
    std::printf("[arccoop] window: 1 Player / 2 Players\n"); std::fflush(stdout);
    *b = 0; return true;
}

/* ARC_TEST_DMG=<n> (test only, any mode): a scripted run wins its battles quickly, to reach the screens after them.
 *   - every hit on a fighter of the other team (battle world unit +0x0C not 0) does n times its damage;
 *   - a hit on Player 1's team lands on a fighter of the other team instead, n times as strong (the scripted players
 *     hardly ever reach the CPUs, but the CPUs come and hit them: a scripted run was lost that way).
 * The damage call is func_00085B84(health object, damage, recoverable); a health object's character is its record's
 * +0xE8, looked up among the world's units for its team. Health objects are noted at
 * func_00085CEC, which the game calls on each of them all the time. */
static int arc_team_of(uint32_t obj)
{
    if (obj < 0x10000u || obj >= 0xF0000000u || (obj & 3u) || vm_read32(obj) != 0x00875198u) return -1;
    const uint32_t rec = vm_read32(obj + 0x08);
    if (rec < 0x10000u || rec >= 0xF0000000u || (rec & 3u)) return -1;
    const uint32_t chr = vm_read32(rec + 0xE8);
    const uint32_t w0 = vm_read32(0x008D3888u - 0x7A90u);
    const uint32_t world = (w0 >= 0x00010000u && w0 < 0x30000000u) ? vm_read32(w0) : 0u;
    if (world < 0x10000000u || world >= 0x20000000u) return -1;
    for (uint32_t k = 0; k < 4; k++) {
        const uint32_t u = world + 0x60u + 0x94u * k;
        if ((vm_read32(u) >> 16) == chr) return (int)vm_read32(u + 0x0C);
    }
    return -1;
}

void arc_coop_icall(uint32_t target, uint64_t* gpr)
{
    static const int mult = [] { const char* e = std::getenv("ARC_TEST_DMG"); return e ? std::atoi(e) : 0; }();
    if (mult <= 1 || (target != 0x00085B84u && target != 0x00085CECu)) return;
    static uint32_t seen[8]; static int ns = 0;
    const uint32_t obj = (uint32_t)gpr[3];
    if (obj < 0x10000u || obj >= 0xF0000000u || (obj & 3u) || vm_read32(obj) != 0x00875198u) return;
    if (target == 0x00085CECu) {
        for (int i = 0; i < ns; i++) if (seen[i] == obj) return;
        seen[ns < 8 ? ns++ : (ns = 1, 0)] = obj;   /* a full list starts over (a new fight's objects) */
        return;
    }
    if ((int32_t)gpr[4] <= 0) return;
    int team = arc_team_of(obj);
    uint32_t to = obj;
    if (team == 0) {   /* onto a fighter of the other team that still has health (+0x90) */
        for (int i = 0; i < ns && to == obj; i++) if (arc_team_of(seen[i]) > 0 && (int32_t)vm_read32(seen[i] + 0x90) > 0) to = seen[i];
        if (to == obj) return;
    } else if (team < 0) return;
    const int64_t d = (int64_t)(int32_t)gpr[4] * mult;
    static unsigned n = 0;
    if (n++ < 16) { std::printf("[arccoop] ARC_TEST_DMG: hit on %08X (team %d)%s: %d -> %lld\n", obj, team, to != obj ? " moved to the other team" : "", (int32_t)gpr[4] / 100, (long long)(d / 100)); std::fflush(stdout); }
    gpr[3] = to;
    gpr[4] = (uint64_t)(d > 0x7FFFFFFF ? 0x7FFFFFFF : d);
}

void arc_coop_pad(uint32_t port, uint16_t* b, uint16_t* lx, uint16_t* ly)
{
    if (port == 0) unit_dump();
    if (!enabled()) return;
    if (ask_pad(port, b)) return;
    /* Player 2 picks the partner: Arcade's Character Select is a
     * 1 Player screen, so while its slot cursor is on the partner's box (setup slot 1) Player 1's port carries Player 2's
     * pad as well as Player 1's (the grid and the Tools / Character Type panel that follow keep the cursor there). On
     * every other box Player 1 alone has it as before. ARC_COOP_P2PICK=0: Player 1 picks both.
     * Seen in a scripted run: Player 2's Cross opened the grid, Right moved in it, Cross picked a fighter. */
    {
        static const bool p2pick = [] { const char* e = std::getenv("ARC_COOP_P2PICK"); return !(e && e[0] == '0'); }();
        static uint16_t p2b = 0, p2lx = 128, p2ly = 128; static bool was = false;
        if (port == 1) { p2b = *b; p2lx = *lx; p2ly = *ly; }
        if (port == 0 && p2pick && g_coop && vm_read32(0x00956550u) == 2u) {
            { static const bool cslog = std::getenv("ARC_COOP_CSLOG") != nullptr; static int last = -9; static uint16_t lb = 0xFFFF; static unsigned n = 0;   /* ARC_COOP_CSLOG=1 (test): the slot cursor and Player 1's buttons when either changes */
              const int sl = jsp_ac_charsel_slot();
              if (cslog && sl >= 0 && (sl != last || *b != lb) && n < 200) { n++; std::printf("[arccoop] cs slot %d, P1 buttons %04X, P2 buttons %04X at %llu\n", sl, *b, p2b, GetTickCount64()); std::fflush(stdout); }
              last = sl; lb = *b; }
            const bool on = jsp_ac_charsel_slot() == 1;
            if (on != was) { was = on; std::printf("[arccoop] Character Select: the partner's box is %s\n", on ? "open to Player 2's pad too" : "left; Player 1 alone again"); std::fflush(stdout); }
            if (on) { *b |= p2b; if (p2lx < 96 || p2lx > 160 || p2ly < 96 || p2ly > 160) { *lx = p2lx; *ly = p2ly; } }   /* both pads: Player 2 picks, Player 1 can still move on or press Start */
        }
    }
    if (port != 0) return;
    static bool left_menu = false, said = false;
    if (jsp_ac_main_menu_open()) {   /* back on the main list after an Arcade run: the answer is forgotten */
        if (left_menu && jsp_ac_main_list_open()) { left_menu = false; said = false;
            if (g_coop) { g_coop = false; std::printf("[arccoop] main menu: 2 Players off\n"); std::fflush(stdout); } }
        return;
    }
    left_menu = true;
    if (!g_coop || vm_read32(0x00956550u) != 2u) return;   /* not a 2 Players Arcade run */
    if (!said) { said = true; jsp_set_two_players(1); }
    unit_takeover();
}
