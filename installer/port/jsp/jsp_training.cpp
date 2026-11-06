// jsp_training.cpp -- Training mode for J-Stars (Free Battle with Training's setup).
//
// After the v0.15 port's Training Mode (jstars/port/jstars_game.cpp, Tsamiuy, MIT), ported to ps3recomp v0.18.0 piece
// by piece. Notes: docs/notes/TRAINING_MODE.md, docs/notes/FINDINGS.md "Training mode step 1-3".
//
// ENHANCEMENT: the whole file. Off unless the environment variable TRAINING is set (not 0): then Free Battle runs as
// Training (the menu row is a later round).
//
// How it hooks the game: v0.15 used a toolkit indirect-call hook (g_ppu_icall_hook). v0.18.0 dispatches every indirect
// call through its function table, so wrapping a lifted function there (ppu_register_function, as the tracer in
// jsp_hle.cpp does) sees the same calls; no toolkit patch is needed. The functions below have no direct callers.
//
// Time: the v0.15 code used GetTickCount64. Here the clock is the flip count (cellGcm_flip_request_count), the same
// deterministic clock as PAD_CLOCK=flips; ASSUMPTION: the game flips 30 times per game second (Free Battle's 300 s timer runs out after about 8000 flips), so 1.5 s = 45 flips.
//
//   Step 1  no KO and health refill (v0.15 FINDINGS "Training mode step 1"): fighter health is kept in hundredths in
//           the fighter object: +0x90 current (regenerates up to +0x94), +0x94 the recoverable part, +0x98 max.
//           Damage is the virtual call @0x00085B84(obj, damage to +0x90, damage to +0x94), which tests for the KO
//           right after subtracting, so the wrapper shrinks the damage to leave 1 health (100). The HUD reads every
//           fighter's health through @0x00085CEC each frame (never on menus): that marks "a fight is up" and is where
//           the refill runs, 45 flips after the fighter's last hit.
//   Step 2  the dummy (FINDINGS "step 2"): each fighter has a pad object (vtable 0x00875858) whose getters
//           @0x9D5AC/0x9D680/0x9D6A8/0x9D6D0/0x9D6D8/0x9D728/0x9D750 read bits of +0x204/+0x20C/+0x210/+0x214; +0x14 is
//           the fighter number, +0x18 the driver (1 human pad, 3 CPU). A dummy's pad is handed to a human driver
//           (+0x18 and +0x1FC 3 -> 1; with the CPU driver the AI still attacks up close) and its getters answer 0.

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <utility>

#include "ps3emu/ps3types.h"
#include "ppu_recomp.h"

#ifdef _WIN32
#include <windows.h>
#endif

extern "C" uint32_t cellGcm_flip_request_count(void);
extern "C" void ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));

namespace {

typedef void (*fn_t)(ppu_context*);

bool training_env()
{
    static const bool on = [] { const char* e = std::getenv("TRAINING"); return e && *e && e[0] != '0'; }();
    return on;
}

// Free Battle's own screens (1 Player / 2 Players, character select, stage select) all show 2 in 0x0095654C and the
// main list 0 (v0.15 FINDINGS step 3). Seeing 2 starts Training; Training ends when the main list shows again.
const uint32_t k_menu_state = 0x0095654Cu;
volatile bool g_free_battle = false;   // latch: Free Battle's screens were entered with TRAINING set

const uint32_t k_flip_fight_window = 30;   // the HUD stamps this often in a fight; older = no fight
volatile uint32_t g_hud_flip = 0;          // flip count at the last HUD health read
volatile bool g_hud_seen = false;

uint32_t now_flips() { return cellGcm_flip_request_count(); }
bool fight_up() { return g_hud_seen && now_flips() - g_hud_flip < k_flip_fight_window; }
bool training_active() { return training_env() && g_free_battle; }

uint32_t g_linked[8]; int g_nlinked = 0;
uint32_t g_pads[8]; int g_npads = 0;   // pad objects seen in this fight (step 2 below)

// ---- step 1: health -------------------------------------------------------------------------------------------
const uint32_t k_refill_flips = 45;   // 1.5 s (ASSUMPTION: 30 flips per game second, see header)
struct TrainFighter { uint32_t obj; uint32_t last_hit; };
TrainFighter g_train[8];
int g_train_count = 0;

int train_find(uint32_t obj)
{
    for (int i = 0; i < g_train_count; i++) if (g_train[i].obj == obj) return i;
    return -1;
}

// TRAINING_TEST_LOW=N (test only): every fighter starts the fight at N/100 health, so a few hits would KO it
// without the cap.
int test_low()
{
    static const int low = [] { const char* e = std::getenv("TRAINING_TEST_LOW"); return e ? std::atoi(e) : 0; }();
    return low;
}
uint32_t g_low_done[8];
int g_low_n = 0;
uint32_t g_low_started[8];
int g_low_started_n = 0;

void on_health_read(uint32_t obj)   // @0x00085CEC, r3 = fighter object, once per fighter per frame
{
    const uint32_t now = now_flips();
    if (!g_hud_seen || now - g_hud_flip >= k_flip_fight_window) {   // a new fight: forget the last one
        g_train_count = 0; g_low_n = 0; g_low_started_n = 0; g_nlinked = 0;
        for (int k = 0; k < 8; k++) g_train[k].obj = 0;
    }
    g_hud_flip = now; g_hud_seen = true;
    if (!training_active()) return;
    const int32_t max = (int32_t)vm_read32(obj + 0x98);
    if (max <= 0 || max > 1000000) return;
    int j = 0;
    for (; j < g_low_n; j++) if (g_low_done[j] == obj) break;
    if (j == g_low_n && g_low_n < 8) {   // first sight of this fighter in this fight
        g_low_done[g_low_n++] = obj;
        std::fprintf(stderr, "[training] fighter 0x%08X max %d words", obj, max / 100);
        for (uint32_t o = 0; o < 0x40; o += 4) std::fprintf(stderr, " %08X", vm_read32(obj + o));
        std::fprintf(stderr, "\n");
    }
    const int i = train_find(obj);
    if (i < 0 || g_train[i].last_hit == 0 || now - g_train[i].last_hit < k_refill_flips) return;
    if (vm_read32(obj + 0x90) < (uint32_t)max || vm_read32(obj + 0x94) < (uint32_t)max) {
        vm_write32(obj + 0x94, (uint32_t)max);
        vm_write32(obj + 0x90, (uint32_t)max);
        std::fprintf(stderr, "[training] refill 0x%08X to %d at flip %u\n", obj, max / 100, now);
    }
}

// Diagnostic: which pad belongs to which fighter object (pointer to each other), logged once per fighter.
void link_scan(uint32_t obj)
{
    for (int i = 0; i < g_nlinked; i++) if (g_linked[i] == obj) return;
    if (g_nlinked >= 8) return;
    g_linked[g_nlinked++] = obj;
    for (int k = 0; k < g_npads; k++)
        for (uint32_t o = 0; o < 0x800; o += 4) {
            if (vm_read32(obj + o) == g_pads[k]) std::fprintf(stderr, "[training] fighter 0x%08X +0x%X holds pad 0x%08X (slot %u driver %u)\n", obj, o, g_pads[k], vm_read32(g_pads[k] + 0x14), vm_read32(g_pads[k] + 0x18));
            if (vm_read32(g_pads[k] + o) == obj) std::fprintf(stderr, "[training] pad 0x%08X +0x%X holds fighter 0x%08X (slot %u driver %u)\n", g_pads[k], o, obj, vm_read32(g_pads[k] + 0x14), vm_read32(g_pads[k] + 0x18));
        }
    std::fprintf(stderr, "[training] fighter 0x%08X scanned against %d pads\n", obj, g_npads);
}

void on_damage(ppu_context* c)   // @0x00085B84(obj, damage90, damage94)
{
    if (!training_active()) return;
    const uint32_t obj = (uint32_t)c->gpr[3];
    const int32_t max = (int32_t)vm_read32(obj + 0x98);
    if (max <= 0 || max > 1000000) return;
    if (test_low() > 0) {   // test only: the fight sets health after the HUD's first read, so start low at the first hit
        int j = 0;
        for (; j < g_low_started_n; j++) if (g_low_started[j] == obj) break;
        if (j == g_low_started_n && g_low_started_n < 8) {
            g_low_started[g_low_started_n++] = obj;
            vm_write32(obj + 0x90, (uint32_t)test_low()); vm_write32(obj + 0x94, (uint32_t)test_low());
            std::fprintf(stderr, "[training] fighter 0x%08X test start at %d\n", obj, test_low());
        }
    }
    link_scan(obj);
    const int32_t cur = (int32_t)vm_read32(obj + 0x90), rec = (int32_t)vm_read32(obj + 0x94);
    const int32_t d90 = (int32_t)c->gpr[4], d94 = (int32_t)c->gpr[5];
    if (d90 <= 0 && d94 <= 0) return;   // a heal
    if (d90 > 0 && cur - d90 < 100) c->gpr[4] = (uint64_t)(int64_t)(cur > 100 ? cur - 100 : 0);
    if (d94 > 0 && rec - d94 < 100) c->gpr[5] = (uint64_t)(int64_t)(rec > 100 ? rec - 100 : 0);
    std::fprintf(stderr, "[training] hit 0x%08X health %d (%d) damage %d (%d) -> %d (%d) flip %u\n", obj, cur, rec, d90, d94,
                 (int32_t)c->gpr[4], (int32_t)c->gpr[5], now_flips());
    const uint32_t now = now_flips() ? now_flips() : 1u;
    int i = train_find(obj);
    if (i < 0 && g_train_count < 8) {
        i = g_train_count++;
        g_train[i].obj = obj;
        std::fprintf(stderr, "[training] fighter 0x%08X max %d\n", obj, max / 100);
    }
    if (i >= 0) g_train[i].last_hit = now;
}

// ---- step 2: the dummy ----------------------------------------------------------------------------------------
const uint32_t k_pad_vt = 0x00875858u;
uint32_t g_dummy_pads[4]; int g_ndummy = 0;

// The battle setup character select edits: one 0x2C record per slot at 0x009540E4 (0 P1, 1 P1's partner, 2 the
// opponent, 3 the opponent's partner), +0x00 driver (0 Player 1, 2 CPU), +0x08 state (2 = not taking part).
uint32_t setup_field(int slot, uint32_t off) { return 0x009540E4u + 0x2Cu * (uint32_t)slot + off; }

// Fighter numbering in a fight (pad +0x14, fighter object +0x18; seen in test runs): 0 Player 1, 1 the opponent, 2 Player 1's
// partner, 3 the opponent's partner (v0.15 read it as "0 P1, 1 partner, 2-3 the other team", which held only in 1 on 1, the
// only case Training allows once the CPU partner slots are locked). The dummies are the CPU fighters of the other team
// (numbers 1 and 3). ASSUMPTION for 3: a 4-fighter check (partners not locked) is still open.
int dummy_mask() { return (1 << 1) | (1 << 3); }

bool on_pad_getter(ppu_context* c)   // true = skip the call, the answer is 0 ("nothing pressed")
{
    if (!training_active() || !fight_up()) return false;
    const uint32_t o = (uint32_t)c->gpr[3];
    if (vm_read32(o) != k_pad_vt) return false;
    int i = 0;
    for (; i < g_npads; i++) if (g_pads[i] == o) break;
    if (i == g_npads && g_npads < 8) {
        g_pads[g_npads++] = o;
        const uint32_t slot = vm_read32(o + 0x14);
        static const bool nodummy = std::getenv("TRAINING_NODUMMY") != nullptr;   // test only: control run, opponents stay CPUs
        if (!nodummy && vm_read32(o + 0x18) == 3 && slot < 4 && ((dummy_mask() >> slot) & 1) && g_ndummy < 4) {
            g_dummy_pads[g_ndummy++] = o;
            vm_write32(o + 0x18, 1);
            vm_write32(o + 0x1FC, 1);
            std::fprintf(stderr, "[training] pad 0x%08X (fighter %u) is the training dummy\n", o, slot);
        }
    }
    for (int k = 0; k < g_ndummy; k++)
        if (g_dummy_pads[k] == o) { c->gpr[3] = 0; return true; }
    return false;
}

// ---- step 3: Training's setup on character select and stage select ---------------------------------------------
// Per-frame functions of the two screens, found in v0.15 (screens_note); all are only called through vtables:
//   @0x0019AA74 character select (vtable 0x0087C3D0), @0x0026EF94 stage select (vtable 0x0087FCF8),
//   @0x00279F0C its time limit box (vtable 0x00880018).
// The screens are found by the r3 vtable, not by fixed heap addresses (they move between builds).
const uint32_t k_charsel_vt = 0x0087C3D0u, k_stage_vt = 0x0087FCF8u, k_timebox_vt = 0x00880018u;
bool g_locked[4] = {false, false, false, false};

// Slot locks: the CPU partners (setup slots 1 and 3) do not take part and show the game's own padlock, as the
// opponent's slots do in Arcade: setup record +0x04 flags bit 0, +0x08 state 2, +0x0C character 0, +0x18 level 1,
// +0x20 0 (v0.15 FINDINGS 10-30 "padlocks"), also in the screen's two copies of the records (+0x38 and +0x168). A human
// Player 2 in a slot (driver not CPU) stays free.
void lock_slots(uint32_t screen)
{
    static const bool nolock = std::getenv("TRAINING_NOLOCK") != nullptr;   // test only: 4-fighter Training for the dummy check
    if (nolock) return;
    const uint32_t bases[3] = {0x009540E4u, screen + 0x38, screen + 0x168};
    for (int k = 1; k < 4; k += 2) {
        if (vm_read32(setup_field(k, 0x00)) != 2) continue;
        if (!g_locked[k]) { g_locked[k] = true; std::fprintf(stderr, "[training] slot %d locked (screen 0x%08X)\n", k, screen); }
        for (uint32_t base : bases) {
            const uint32_t r = base + 0x2Cu * (uint32_t)k;
            vm_write32(r + 0x04, 1); vm_write32(r + 0x08, 2); vm_write32(r + 0x0C, 0);
            vm_write32(r + 0x18, 1); vm_write32(r + 0x20, 0);
        }
    }
}

// Time Unlimited: the stage select screen holds the seconds at +0x78 (0 = Unlimited), Finish and Back copy them to the
// battle settings (@0x0026FAA0, field +0x18); the time limit box shows its own choice at +0x40. v0.15 wrote both on every
// pad read and hid Cross/Left/Right on the box and stamped a padlock over its arrow. Here the values are written on every
// frame of the screens; the padlock picture and the blocked presses are not ported yet (ENHANCEMENT, later), so the
// box can be changed but is written back at once.
bool g_time_logged = false;

bool on_screen_call(uint32_t addr, ppu_context* c)
{
    if (!training_active()) return false;
    const uint32_t o = (uint32_t)c->gpr[3];
    if (addr == 0x0019AA74u && vm_read32(o) == k_charsel_vt) lock_slots(o);
    else if (addr == 0x0026EF94u && vm_read32(o) == k_stage_vt) {
        vm_write32(o + 0x78, 0);
        if (!g_time_logged) { g_time_logged = true; std::fprintf(stderr, "[training] stage select 0x%08X: time Unlimited\n", o); }
    } else if (addr == 0x00279F0Cu && vm_read32(o) == k_timebox_vt) vm_write32(o + 0x40, 0);
    return false;
}

// ---- hook table -----------------------------------------------------------------------------------------------
constexpr uint32_t k_addrs[] = {0x00085B84u, 0x00085CECu, 0x0009D5ACu, 0x0009D680u, 0x0009D6A8u,
                                0x0009D6D0u, 0x0009D6D8u, 0x0009D728u, 0x0009D750u, 0x0019AA74u, 0x0026EF94u, 0x00279F0Cu};

template <int I> struct Hook {
    static fn_t orig;
    static void run(ppu_context* c)
    {
        const uint32_t a = k_addrs[I];
        if (a == 0x00085B84u) on_damage(c);
        else if (a == 0x00085CECu) on_health_read((uint32_t)c->gpr[3]);
        else if (a == 0x0019AA74u || a == 0x0026EF94u || a == 0x00279F0Cu) on_screen_call(a, c);
        else if (on_pad_getter(c)) return;
        orig(c);
    }
    static void install()
    {
        for (uint64_t i = 0; i < function_table_count; i++)
            if (function_table[i].addr == k_addrs[I]) {
                orig = function_table[i].func;
                ppu_register_function(k_addrs[I], run);
                return;
            }
        std::fprintf(stderr, "[training] no lifted function at %08X\n", k_addrs[I]);
    }
};
template <int I> fn_t Hook<I>::orig;

template <int... Is> void install_all(std::integer_sequence<int, Is...>) { (Hook<Is>::install(), ...); }

#ifdef _WIN32
// Watches the menu state (reads only) and keeps the Training latch; one look per flip.
DWORD WINAPI menu_watch(LPVOID)
{
    uint32_t last = ~0u, seen_main = 0;
    for (;;) {
        Sleep(10);
        const uint32_t f = now_flips();
        if (f == last || f == 0) continue;
        last = f;
        const uint32_t s = vm_read32(k_menu_state);
        if (s == 2 && !g_free_battle) {
            g_free_battle = true; seen_main = 0;
            std::fprintf(stderr, "[training] Free Battle entered: Training on (flip %u)\n", f);
        } else if (g_free_battle && s == 0 && !fight_up()) {
            if (++seen_main == 60) {   // the main list has shown for 60 flips
                g_free_battle = false;
                g_npads = 0; g_ndummy = 0; g_time_logged = false;
                for (int k = 0; k < 4; k++) g_locked[k] = false;
                std::fprintf(stderr, "[training] main list again: Training off (flip %u)\n", f);
            }
        } else if (s != 0) {
            seen_main = 0;
        }
        if (!fight_up() && g_npads) { g_npads = 0; g_ndummy = 0; }
    }
    return 0;
}
#endif

}  // namespace

extern "C" void jsp_register_training_overrides(void)
{
    if (!training_env()) return;
    install_all(std::make_integer_sequence<int, sizeof k_addrs / sizeof k_addrs[0]>());
#ifdef _WIN32
    CreateThread(nullptr, 0, menu_watch, nullptr, 0, nullptr);
#endif
    std::fprintf(stderr, "[training] TRAINING set: Free Battle runs as Training\n");
}
