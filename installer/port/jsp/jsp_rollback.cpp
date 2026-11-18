// jsp_rollback.cpp -- ENHANCEMENT (EXPERIMENTAL rollback netcode): the three entry points main.cpp and stubs.cpp call. OPT-IN: every one returns at
// once unless JSP_ROLLBACK=1, so with the switch unset the guest reservation, the lifted function table and the HLE registrations are exactly as
// before. Player-facing description, limits and what has run: docs/ROLLBACK.md.
//   jsp_rollback_reserve_guest()       main.cpp alloc_guest_vm(): the guest reservation with MEM_WRITE_WATCH, so the snapshot ring (jsp_snapshot.cpp)
//                                      can ask GetWriteWatch which pages a tick wrote. The flag can only be given at reservation time.
//   jsp_rollback_after_table()         main.cpp, after ppu_recomp_register() and ps3_load_prx_modules() (the table registration overwrites earlier
//                                      hooks), before the first guest call: the replay clock / canonical pad (jsp_stepper.cpp) and the session's
//                                      frame hooks (jsp_net_game.cpp).
//   jsp_rollback_register_overrides()  stubs.cpp, after the other registrations: the fight tick counter and per-tick hash (jsp_fight_hash.cpp),
//                                      which the session uses as its tick number and checksum.
// jsp_net_game.cpp sets the switches these modules read (JSP_SNAPSHOT_WW, JSP_STEP_TURBO, JSP_FIGHT_HASH, ...) before main() when JSP_ROLLBACK=1.
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <windows.h>

extern "C" { int jsp_vm_write_watch = 0; }   /* 1 = the guest reservation carries MEM_WRITE_WATCH (read by jsp_snapshot.cpp) */

namespace {
bool rollback_on()
{
    const char* e = std::getenv("JSP_ROLLBACK");
    return e && *e && e[0] != '0';
}
}  // namespace

extern "C" uint8_t* jsp_rollback_reserve_guest(size_t size)
{
    if (!rollback_on()) return nullptr;
    const char* ww = std::getenv("JSP_SNAPSHOT_WW");
    if (!ww || !*ww || ww[0] == '0') return nullptr;
    uint8_t* base = (uint8_t*)VirtualAlloc(NULL, size, MEM_RESERVE | MEM_WRITE_WATCH, PAGE_READWRITE);
    jsp_vm_write_watch = base != nullptr;
    std::fprintf(stderr, "[snap] guest reservation with MEM_WRITE_WATCH: %s (err %lu)\n", base ? "ok" : "REFUSED, plain reservation",
                 base ? 0ul : (unsigned long)GetLastError());
    return base;
}

extern "C" void jsp_stepper_install(void);   /* jsp_stepper.cpp: JSP_STEP_TURBO / JSP_STEP_CANON */
extern "C" void jsp_net_game_install(void);  /* jsp_net_game.cpp: the session, its frame hooks and overlays */
extern "C" void jsp_rollback_after_table(void)
{
    if (!rollback_on()) return;
    jsp_stepper_install();
    jsp_net_game_install();
}

extern "C" void jsp_register_fight_hash_overrides(void);   /* jsp_fight_hash.cpp: JSP_FIGHT_HASH */
extern "C" void jsp_rollback_register_overrides(void)
{
    if (!rollback_on()) return;
    jsp_register_fight_hash_overrides();
}
