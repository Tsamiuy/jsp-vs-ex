// jsp_net_native_fight.cpp -- ENHANCEMENT (EXPERIMENTAL, off unless armed): the rollback session takes over the game's OWN online fight.
// API: jsp_net_native_fight.h. Nothing changes until jsp_native_fight_arm(1): every hook below runs the original function unchanged.
//
// What the game does per tick in an online fight (static analysis, the native-fm3 two-copy trace; US 1.00 addresses, TOC = 0x008D3888):
//   - online mode object (vtable 0x00878A70) slot 4 = 0x0F76C4 -> tail call 0x0F7234: for each battle player with a net slot, pop one 12-byte
//     sample from the share pad's member queue into mode + 0x14C + slot*20, then the previous / newly-pressed words. Returns 1 ok, 2 share pad
//     inactive (+0x6D8 == 0: loading), 3 queue base not ready, 4 no share pad, 5 battle paused, 6 queue empty (never seen in the trace).
//   - manager update 0x45222C (indirect, 30/s) calls the scheduler 0x4812EC and the share pad update 0x484690 DIRECTLY; 0x484690 tail-calls the
//     send 0x4844DC (builds the local sample from the pad object at +0x6CC and pushes it into the queues and the network).
//   - mode slot 2 = 0x0F7704 calls 0x0F73E8 DIRECTLY: share pad +0x6D4 (0x48407C) == 2 is the NORMAL state (the trace: 2 the whole fight);
//     any other value pauses the battle (0x148368, mode+0x1C |= 0xB0) and after 1 s opens the wait window; 2 again resumes. 0x0F73E8 returns at
//     once when +0x6D8 == 0 (0x484070), without pausing or resuming.
// The takeover: every PC reads every human's pad from ITS OWN cellPad port = net slot (the rollback serves table slot s on port s), so the
// game's queue is not needed: arm writes share pad +0x6D8 = 0 and +0x6DC = 0 (guest memory, so it is in the session's first save):
//   0x484690 then returns at once (handshake done, +0x6D8 == 0, +0x6DC == 0: 0x48477C -> 0x4847C8 -> return), so 0x4844DC never runs;
//   0x0F73E8 returns at once (no stall, no pause); and the pad take (hooked at 0x0F76C4) is replaced by take_armed() below.
//
// HOOK REACH (checked in ps3recomp runtime/ppu/ppu_loader.cpp): ppu_register_function / jsp_hook_chain only replace the entry of the
// INDIRECT-call table. A direct call in the lifted code (bl -> func_XXXXXXXX(ctx)) and a tail call through g_trampoline_fn = func_XXXXXXXX never
// look at it. So:
//   0x0F76C4  reached through the mode vtable (indirect): this hook is what replaces the pad take.
//   0x0F7234  only tail-called by 0x0F76C4 (direct): registered too, catches nothing today.
//   0x0F73E8, 0x484690, 0x4844DC: direct only; neutralised by the +0x6D8 / +0x6DC writes, the hooks catch nothing today.
//   0x4812EC, 0x454EE8, 0x466794, 0x466ADC, 0x4862B0: direct only (0x454EE8 has ~64 call sites). Registered (in case an indirect path exists), but
//     the stubs only take effect with the guarded instruction patches jnf::kPatches in lift_insn_patches.json (guard g_jsp_native_fight_armed,
//     which this file defines) and a RE-LIFT. Without them the scheduler and the error paths keep running while armed.
//   JSON lines to add (orig words read from the EBOOT; the lifter refuses a mismatch):
//     {"addr":"0x0045238C","orig":"0x48032305","new":"0x60000000","guard":"g_jsp_native_fight_armed"}   bl 0x484690 -> nop
//     {"addr":"0x00452294","orig":"0x4802F059","new":"0x60000000","guard":"g_jsp_native_fight_armed"}   bl 0x4812EC -> nop
//     {"addr":"0x00454F50","orig":"0x80039078","new":"0x38000000","guard":"g_jsp_native_fight_armed"}   0x454EE8 reports nothing
//     {"addr":"0x004516E4" / "0x004666E8" / "0x00466830" / "0x004673B8","orig":"0x38800001","new":"0x38800000",...}  0x466794 stores +0xFC = 0
//     {"addr":"0x00468178","orig":"0x4BFFE965","new":"0x38600001",...}, {"addr":"0x004682D4","orig":"0x4BFFE809","new":"0x38600001",...}  0x466ADC -> 1
//     {"addr":"0x0047DFD8","orig":"0x480082D9","new":"0x38600000",...}   0x4862B0 -> 0
// Not touched: 0x45222C (the manager update itself), 0x47BFE8 and 0x47AC98 (inits, not per tick).
//
// Rollback: the hooks keep no host state that changes what the game does except g_armed, which is constant for a whole session (armed before the
// first save, disarmed after the last tick). Everything the replacement writes (records, +0x6D8/+0x6DC) is guest memory and rolls back.
#include "jsp_net_native_fight.h"
#include "jsp_sharepad_wd.h"   // FIX (rematch black screen): the share pad watchdog rule

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>   // GetCurrentThreadId (the watchdog's one-shot thread lines)

#include "ps3emu/ps3types.h"
#include "ppu_recomp.h"
extern "C" void ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
#include "jsp_hook_chain.h"
extern "C" const char* cellGame_get_title_id(void);                     // libs/system/cellGame.c: PARAM.SFO's TITLE_ID (read by main.cpp before the table)
extern "C" void jsp_setting_set_live(const char* key, const char* value);   // jsp_settings.cpp: an in-process setting, never written to the file
extern "C" int jsp_setting_get(const char* key, char* out, int n);           // jsp_settings.cpp: the session layer, the live value, else the environment

extern "C" int g_jsp_native_fight_armed = 0;   // also the guard of jnf::kPatches (read by the lifted code)

namespace {

typedef void (*fn_t)(ppu_context*);
constexpr uint32_t kToc = 0x008D3888u;
constexpr uint32_t kOnlineVt = 0x00878A70u, kOfflineVt = 0x008789D0u;

bool armed() { return g_jsp_native_fight_armed != 0; }
// FIX (swapped controls): jsp_native_fight_map's net slot -> port (rollback slot) table, constant for a whole session like g_armed; g_local_port = this PC's
// rollback slot; g_me = this PC's battle player; g_drove[i] = pad takes in which this PC's port gave battle player i a non-neutral sample (diagnostic only).
bool g_port_on = false;
int8_t g_port[jnf::kRecCount] = { -1, -1, -1, -1 };
int32_t g_local_port = -1, g_me = -1;
std::atomic<uint32_t> g_drove[jnf::kPlayers];
bool plausible(uint32_t p) { return p >= 0x10000u && p < 0xF0000000u && !(p & 3u); }
void drain(ppu_context* c) { while (g_trampoline_fn) { void (*f)(void*) = g_trampoline_fn; g_trampoline_fn = nullptr; f((void*)c); } }
// a call of a lifted function from here: lr as the lifted code sets it before a bl, then the tail-call drain (DRAIN_TRAMPOLINE)
template <class F> void call(ppu_context* c, F f, uint32_t ret) { c->lr = ret; f(c); drain(c); }

// share pad = *(manager + 0x40000 - 0x7020) (0x451114), manager = *(*(TOC + 0x59F8)) (0x451108)
uint32_t share_pad()
{
    const uint32_t p = vm_read32(kToc + 0x59F8u);
    if (!plausible(p)) return 0;
    const uint32_t mgr = vm_read32(p);
    if (!plausible(mgr)) return 0;
    const uint32_t sp = vm_read32(mgr + 0x38FE0u);
    return plausible(sp) ? sp : 0;
}
// battle mode object, the chain of the fighter input 0x0BED94 @0x0BF08C: S = *(*(TOC - 0x3F9C)) (0x14047C), X = *S (0x1404A8),
// Y = *(X + 0x58) (0x1404FC), mode = *(Y + 0x28) (0x0E4A74)
uint32_t mode_obj()
{
    uint32_t p = vm_read32(kToc - 0x3F9Cu);
    if (!plausible(p)) return 0;
    const uint32_t s = vm_read32(p);
    if (!plausible(s)) return 0;
    const uint32_t x = vm_read32(s);
    if (!plausible(x)) return 0;
    const uint32_t y = vm_read32(x + 0x58u);
    if (!plausible(y)) return 0;
    const uint32_t m = vm_read32(y + 0x28u);
    return plausible(m) ? m : 0;
}

// ENHANCEMENT: 0x0F7234 with the queue pop replaced by a local pad read. r3 = the online mode object. Returns the original's code.
uint32_t take_armed(ppu_context* c, uint32_t mode)
{
    const uint64_t lr0 = c->lr, cr0 = c->cr, r1 = c->gpr[1];
    // own frame (0x0F7234 makes a 0xA0 one): stdu r1,-0xA0(r1)
    c->gpr[1] = r1 - 0xA0u; vm_write64(c->gpr[1], r1);
    uint32_t ret = 1;
    // 0x0F7254..0x0F7298: B = 0x140568(0x14047C()); paused when *(*(*(B + 0x14))) & 1 -> 5 (the trace: about 1/s). Kept from the original.
    call(c, func_0014047C, 0x000F7260u);
    c->gpr[3] = (uint32_t)c->gpr[3];
    call(c, func_00140568, 0x000F726Cu);
    const uint32_t b = (uint32_t)c->gpr[3];
    const uint32_t sp = share_pad();   // 0x451114(0x451108())
    if (vm_read32(vm_read32(vm_read32(b + 0x14u))) & 1u) ret = 5;
    else if (!sp) ret = 4;   // 0x0F72A4: no share pad
    // 0x0F72AC..0x0F72DC (2: +0x6D8 == 0, 3: 0x4841D8 base not ready) are the QUEUE's state: arm cleared +0x6D8 on purpose. The loading case (2)
    // is before the fight: armed only from the session's first fight tick, and pass-through (the original, which returns 2 then) until then.
    else {
        uint32_t seen = 0;
        for (uint32_t i = 0; i < jnf::kPlayers; i++) {   // 0x0F7310: r27 = 0..5, r28 = 0x60 + i*0x94
            call(c, func_00030A14, 0x000F731Cu);         // G = *(*(TOC - 0x7A90))
            const uint32_t pl = (uint32_t)c->gpr[3] + jnf::kPlayerBase + i * jnf::kPlayerStride;
            const int32_t slot = (int32_t)vm_read32(pl + jnf::kOffNetSlot);   // < 0: COM / no net player: no pad read, record untouched (as the original)
            c->gpr[3] = sp;
            call(c, func_00484A1C, 0x000F7340u);         // member count: sp+0x10 if sp+0 != 0, else 0
            // FIX (swapped controls): the pad of net slot `slot` is the rollback slot (room record) of the human whose fighter has it (jsp_native_fight_map;
            // the builder numbers net slots in team placement order, not record order). Without a map (never in a session: map refuses) port = slot as before.
            const int32_t port = g_port_on && slot >= 0 && slot < (int32_t)jnf::kRecCount ? g_port[slot] : slot;
            if (port < 0) continue;
            if (!jnf::take_slot(slot, (int32_t)c->gpr[3], &seen)) continue;
            const uint32_t rec = mode + jnf::kRecBase + (uint32_t)slot * jnf::kRecStride;   // 0x0F7344..0x0F7364: slot*16 + slot*4 + mode + 0x14C
            // The 12-byte sample, built as the send 0x4844DC builds the LOCAL one (0x48452C..0x484588) but into the record, from port `slot`:
            vm_write32(rec + 0x0u, 0);                   // buttons = 0 (0x0BE3D4 only ORs bits in)
            for (int k = 0; k < 2; k++) {                // sticks = 0x56610(f[TOC + 0x61D8]) twice, each result in two bytes (the neutral stick)
                const uint32_t fw = vm_read32(kToc + 0x61D8u); float f; std::memcpy(&f, &fw, 4); c->fpr[1] = f;
                call(c, func_00056610, k ? 0x00484558u : 0x00484540u);
                vm_write8(rec + 4u + 2u * k, (uint8_t)c->gpr[3]); vm_write8(rec + 5u + 2u * k, (uint8_t)c->gpr[3]);
            }
            vm_write32(rec + 0x8u, 0);                   // flags = 0
            // pad object of port `slot` (the ctor used 0x3F485C(0x3F4624(), 0) = user 0's port -> 0x43F430): *(padmgr + 0xC20 + port*4 + 0xC),
            // padmgr = *(*(TOC + 0x58AC)) (0x43F41C). share pad +0x6CC holds this object pointer for the local port (the trace's 0x19E2C1C0).
            call(c, func_0043F41C, 0x000F7AD0u);
            c->gpr[4] = (uint32_t)port;
            call(c, func_0043F430, 0x003F4898u);
            const uint32_t pad = (uint32_t)c->gpr[3];
            const uint32_t neutral_sticks = vm_read32(rec + 4u);
            // 0x484480(sp, rec, 12, pad, sp+0x6C8): 0x0BE3D4(rec, pad, *(TOC+0x61CC) + 0x10 = the DEFAULT button table, ..) then
            // 0x0F6E10(0x0F6B28(), rec) (flags bits 0/1). pad 0 -> 0x0BE3D4 returns at once (neutral sample).
            c->gpr[3] = sp; c->gpr[4] = rec; c->gpr[5] = 12; c->gpr[6] = pad; c->gpr[7] = (uint64_t)(int64_t)(int32_t)vm_read32(sp + 0x6C8u);
            call(c, func_00484480, 0x00484588u);
            // FIX (swapped controls), the ownership check: which fighter this PC's own pad moves (host state only, never read by the game)
            if (port == g_local_port && (vm_read32(rec + 0x0u) || vm_read32(rec + 4u) != neutral_sticks)) g_drove[i]++;
            // 0x0F738C..0x0F7398: previous / newly pressed
            const jnf::Edges e = jnf::edges(vm_read32(rec + 0x0u), vm_read32(rec + jnf::kOffPrev));
            vm_write32(rec + jnf::kOffPrev, e.prev);
            vm_write32(rec + jnf::kOffPressed, e.pressed);
        }
    }
    c->gpr[1] = r1; c->lr = lr0; c->cr = cr0;
    return ret;   // never 6: nothing waits for a queue
}

fn_t g_orig_slot4 = nullptr, g_orig_take = nullptr;
// 0x0F76C4 (mode slot 4): 0x14047C / 0x140568 (pure getters, results unused) then tail call 0x0F7234(this). Armed and online: the replacement.
void slot4_wrap(ppu_context* c)
{
    const uint32_t mode = (uint32_t)c->gpr[3];
    if (!armed() || !plausible(mode) || vm_read32(mode) != kOnlineVt) { g_orig_slot4(c); return; }
    c->gpr[3] = take_armed(c, mode);
}
void take_wrap(ppu_context* c)   // 0x0F7234 itself (only reachable indirectly, which the lift does not do today)
{
    const uint32_t mode = (uint32_t)c->gpr[3];
    if (!armed() || !plausible(mode) || vm_read32(mode) != kOnlineVt) { g_orig_take(c); return; }
    c->gpr[3] = take_armed(c, mode);
}

// Stubs while armed: 0 void, 1 return 0, 2 return 1, 3 store r3+0xFC = 0 (0x466794 with r4 = 0 does exactly that and nothing else).
template <uint32_t A, int K> struct Stub {
    static fn_t orig;
    static void run(ppu_context* c)
    {
        if (!armed()) { orig(c); return; }
        if (K == 1) c->gpr[3] = 0;
        else if (K == 2) c->gpr[3] = 1;
        else if (K == 3) vm_write32((uint32_t)c->gpr[3] + 0xFCu, 0);
    }
    static bool install() { return jsp_hook_chain::install<fn_t>(A, &run, &orig); }
};
template <uint32_t A, int K> fn_t Stub<A, K>::orig = nullptr;

// ---- FIX (rematch black screen): the share pad watchdog (PR #248, Update 3; the rule is jspw::step in jsp_sharepad_wd.h) ----
// Verified in the lifted code (US 1.00, build\recompiled\ppu_recomp_039.cpp / 008 / 007):
//   - a share pad queue entry q = *(member block + 8) (= sp + 0x1C + 0x28*k) is { +0 peer, +4 list L, ... }; L+4 (= q+8) is the sample count,
//     L+0x10 the used list head, L+0x18 (= q+0x1C) the free list head; nodes are { +0 next, +4 prev, +8 tag, +0xC length, +0x10 data }.
//   - 0x473704(L, tag, src, len) takes the free list's head (0x473270); with none it takes the oldest USED node and does L+4 -= 1, and with both lists
//     empty it writes through node 0. It writes tag and min(capacity, len), copies the bytes (0x717CCC, memcpy), appends the node to the used list
//     (0x473230) and does L+4 += 1. No other field is touched. The watchdog only feeds a queue with count 0 (used list empty), so it checks the free
//     list head before the call and never calls with it empty.
//   - the receive path 0x4824CC fills a member's queue with exactly that call (0x00482688: 0x473704(q + 4, 1, sample, len)), and nothing else: the
//     member's +4 word is written before from the packet header, the +0xB4 / +0x94 words belong to the SEND side (0x4834E4 drains member + 0x94).
//   - the readers only use counts: member states 0x482E64, base 0x481FD8 (min of the counts), stall 0x4844DC. Nothing in the received queue reaches
//     the network (only 0x4834E4 sends, from the member's own +0x94 send list).
//   - the pop (0x48495C / 0x4826F0) runs in the mode's pad take (slot 4), unlocked, as does the receive push in the manager update 0x45222C: the game
//     only ever touches these lists from one thread (inferred; the one-shot "thread" lines below check it). This hook runs in slot 2 of the same mode.
//   - neutral sample = what a released pad gives (the send 0x4844DC / take_armed): buttons 0, the four stick bytes 0x56610(f[TOC+0x61D8]) (centred,
//     not 0), flags 0.
//   - markers: 0x0E2048 is the DELETING destructor of the post-fight object of class 0x876A60 (base vtable, dtor 0x0E38D0, free); 0x0F777C is the online
//     mode's exit poll (slot 18: mode+0x1A0 += 1, 1 on the second call); 0x0F77DC is the mode's deleting destructor (slot 1). 0x0094DEC4 is NOT a scene
//     word (an audio playback count); it is not used. The share pad object persists across matches.
extern "C" volatile uint32_t g_hud_calls;  // jsp_game.cpp: calls of the fight HUD draw 0x00085CEC (counted on every draw)
constexpr uint32_t kPostFightVt = 0x00876A60u;
std::atomic<bool> g_wd_on{false};          // JSP_SHAREPAD_WATCHDOG (default on), decided at install
std::atomic<int> g_end_normal{0};          // jsp_native_fight_end_normal(): the session ending now ended on an agreed KO / time-up
std::atomic<int> g_wd_after{0};            // 1 from a NORMAL match-end disarm to the mode's free 0x0F77DC or the next arm
std::atomic<int> g_wd_f2{0};               // the 0x876A60 object was deleted (0x0E2048) since that disarm
std::atomic<uint32_t> g_wd_mode{0};        // the online mode object the watchdog acts on: the first slot 2 caller after 0x0E2048
std::atomic<uint32_t> g_wd_epoch{0};       // bumped by arm / disarm / the mode's free: the game-thread state below starts a new episode
std::atomic<uint32_t> g_wd_match{0};       // normal match-end disarms (for the log lines)
std::atomic<uint32_t> g_wd_logs{0};        // per-match lines (0x0E2048, the free summary), capped per process
std::atomic<uint32_t> g_wd_feed_logs{0};   // feed lines, their own cap
std::atomic<unsigned long> g_tid_slot2{0}, g_tid_mgr{0};   // host thread ids of slot 2 and of the manager update 0x45222C (one-shot logs)
jspw::State g_wd;                          // game thread only (slot 2 / free hooks)
uint32_t g_wd_seen_epoch = 0xFFFFFFFFu;    // game thread only
uint32_t g_wd_fed_total = 0;               // game thread only: samples fed this episode
uint32_t g_wd_hud_last = 0;                // game thread only: g_hud_calls at the previous slot 2 tick
fn_t g_orig_slot2 = nullptr, g_orig_f2 = nullptr, g_orig_free = nullptr, g_orig_mgr = nullptr;
#if defined(JSP_TEST_KNOBS) && JSP_TEST_KNOBS
#define JSP_WD_KNOB 1
#else
#define JSP_WD_KNOB 0
#endif
#if JSP_WD_KNOB
// Debug repro knob JSP_SHAREPAD_SKIP_PUSH=N, COMPILED ONLY into JSP_TEST_KNOBS builds (cmake -DJSP_TEST_KNOBS=ON); a release build has neither the code
// nor the name, so play_settings.txt cannot reach it. It reproduces the shape of the black screen seen in a two-copy rematch test: THIS copy's own input delay (own queue count after the pop) is 0 at the
// post-fight deletion 0x0E2048, so its PEER holds no spare sample from it and starves at the exit.
// How: from a NORMAL match-end disarm (never another disarm, so never in a live fight) to the online mode's free 0x0F77DC (or the next arm), every
// manager update 0x45222C at which this copy's own queue still holds a sample (own ctr >= 1) runs with share pad +0x6D8 = 0 for that one call only:
// 0x484690 then skips its tail 0x4844DC (no own push, no send, no state / stall update; the receive path and the handshake timers still run), and +0x6D8
// is 1 again before the mode's slot 2 / slot 4 run, so this copy still pops one own and one peer sample that tick. Each dropped push lowers this copy's
// delay by one and makes the peer wait one tick (its delay goes up by one): the same thing a restart pause does, in the other direction. So the own
// delay is pinned at 0 through Results until the exit, and a later pause of this copy (a hitch) is taken back at the next update.
// Only when the last computed states are all normal (+0x6D0 started 1, +0x6D4 sync 2, every member state 2), +0x6DC is 0, the own queue holds a sample,
// and at most N pushes per window. The member queues are NOT required to hold a sample at the update's entry (an earlier version did, and never fired in
// a two-copy test): on a local link the peer's sample for this tick usually arrives inside the same update (its receive 0x451448 runs before the push
// 0x484690), so the member count is 0 at the entry. They are checked AFTER the update instead: a member still without a sample gets the states 0x483168 /
// 0x483260 would have written (member state 3, +0x6D0 = 0, +0x6D4 = 3), so the stall check pauses this one tick and nothing is popped (the game's own path
// for a late sample). The next update recomputes the states. Nothing is held across a mode tick.
uint32_t g_skip_n = 0;                     // JSP_SHAREPAD_SKIP_PUSH: own pushes dropped at most per window
std::atomic<int> g_pin_on{0};              // the window: a normal match-end disarm .. the mode's free 0x0F77DC or the next arm
std::atomic<uint32_t> g_pin_left{0}, g_pin_done{0}, g_pin_logs{0};
#endif
// FIX (late but alive): members whose received-sample count rose inside a manager update 0x45222C since the previous watchdog tick. Only the receive
// path 0x4824CC pushes into a member's queue there (the pop is in slot 4, the watchdog's own push in slot 2), so a rise is a REAL sample from that peer.
std::atomic<uint32_t> g_wd_real{0};

constexpr uint32_t kWdLogCap = 400, kWdFeedLogCap = 400;
bool wd_log_ok() { return g_wd_logs.fetch_add(1) < kWdLogCap; }
bool wd_feed_log_ok() { return g_wd_feed_logs.fetch_add(1) < kWdFeedLogCap; }

uint32_t own_count(uint32_t sp)
{
    const uint32_t me = vm_read32(sp + 0x18u);
    for (uint32_t k = 0; k < jspw::kMaxMembers; k++) if (vm_read32(sp + 0x1Cu + 0x28u * k) == me) return vm_read32(sp + 0x1Cu + 0x28u * k + 8u);
    return 0;
}
// "sync S active A stall N own ctr C; m0 st S ctr C; ..." (guest reads only)
void sp_counts(uint32_t sp, char* out, size_t n)
{
    int p = 0;
    if (!sp) { std::snprintf(out, n, "no share pad"); return; }
    p += std::snprintf(out + p, n - (size_t)p, "sync %u active %u stall %u own ctr %u", vm_read32(sp + 0x6D4u), vm_read32(sp + 0x6D8u), vm_read32(sp + 0x6E8u), own_count(sp));
    const uint32_t m = vm_read32(sp + 0x10u);
    for (uint32_t i = 0; i < m && i < jspw::kMaxMembers && p > 0 && (size_t)p < n; i++) {
        const uint32_t blk = sp + 0xBCu + 0xB8u * i, q = vm_read32(blk + 8u);
        p += std::snprintf(out + p, n - (size_t)p, "; m%u st %u ctr %d", i, vm_read32(blk), plausible(q) ? (int)vm_read32(q + 8u) : -1);
    }
}

// The 12-byte neutral sample at guest `addr` (as take_armed builds it): buttons 0, sticks 0x56610(f[TOC + 0x61D8]) twice, two bytes each, flags 0.
void write_neutral(ppu_context* c, uint32_t addr)
{
    vm_write32(addr + 0x0u, 0);
    for (int k = 0; k < 2; k++) {
        const uint32_t fw = vm_read32(kToc + 0x61D8u); float f; std::memcpy(&f, &fw, 4); c->fpr[1] = f;
        call(c, func_00056610, k ? 0x00484558u : 0x00484540u);
        vm_write8(addr + 4u + 2u * k, (uint8_t)c->gpr[3]); vm_write8(addr + 5u + 2u * k, (uint8_t)c->gpr[3]);
    }
    vm_write32(addr + 0x8u, 0);
}

// Push one neutral sample into queue entry q (0x473704(q + 4, 1, sample, 12), as the receive path 0x4824CC does). 1 = landed (12 bytes copied and
// the count went up by one), 0 = called but did not land, -1 = refused before the call (no free node: 0x473704 would recycle a used node, and with
// the count at 0 there is none, so it would write through node 0). Volatile registers are clobbered as by any call; the caller restores slot 2's.
int feed_neutral(ppu_context* c, uint32_t q)
{
    const uint32_t before = vm_read32(q + 8u);
    if (!plausible(vm_read32(q + 4u + 0x18u))) return -1;   // free list empty: never call (the used-list fallback is empty too at count 0)
    if (before != 0 && !plausible(vm_read32(q + 4u + 0x10u))) return -1;   // defensive: a count with no used node is a broken list
    const uint64_t r1 = c->gpr[1];
    c->gpr[1] = r1 - 0x90u; vm_write64(c->gpr[1], r1);   // own frame: ABI header (0x70) + the sample
    const uint32_t buf = (uint32_t)c->gpr[1] + 0x70u;
    write_neutral(c, buf);
    c->gpr[3] = q + 4u; c->gpr[4] = 1; c->gpr[5] = buf; c->gpr[6] = 12;
    call(c, func_00473704, 0x00482688u);
    const uint32_t len = (uint32_t)c->gpr[3];
    c->gpr[1] = r1;
    return len == 12 && vm_read32(q + 8u) == before + 1u ? 1 : 0;
}

#if JSP_WD_KNOB
// Debug knob: should this manager update drop this copy's own push? (game thread, at the manager update's entry, before the receive and the push)
bool pin_skip(uint32_t* sp_out)
{
    if (!g_pin_on.load() || armed() || !g_pin_left.load()) return false;
    const uint32_t sp = share_pad();
    if (!sp || vm_read32(sp + 0x6D8u) != 1 || vm_read32(sp + 0x6DCu) != 0 || vm_read32(sp + 0x6D0u) != 1 || vm_read32(sp + 0x6D4u) != 2) return false;
    if (own_count(sp) < 1) return false;   // own delay already 0: push as usual
    const uint32_t m = vm_read32(sp + 0x10u);
    if (m < 1 || m > jspw::kMaxMembers) return false;
    for (uint32_t i = 0; i < m; i++) {   // the peer's sample may still arrive in this update's receive: checked after it (pin_fix_states)
        const uint32_t q = vm_read32(sp + 0xBCu + 0xB8u * i + 8u);
        if (vm_read32(sp + 0xBCu + 0xB8u * i) != 2 || !plausible(q) || !vm_read32(q)) return false;
    }
    *sp_out = sp;
    return true;
}
// Debug knob, after a dropped push: the states 0x4844DC did not compute this update. A member with no sample now would be state 3 (own queue not empty,
// that member empty), started 0 and sync 3 (3 outranks 1 and 2 in the priority table [2, 0, 1, 3, 4]); write that, so the stall check pauses this tick
// and the pad take pops nothing. Returns the number of such members.
uint32_t pin_fix_states(uint32_t sp)
{
    const uint32_t m = vm_read32(sp + 0x10u);
    uint32_t empty = 0;
    for (uint32_t i = 0; i < m && i < jspw::kMaxMembers; i++) {
        const uint32_t blk = sp + 0xBCu + 0xB8u * i, q = vm_read32(blk + 8u);
        if (plausible(q) && vm_read32(q + 8u) == 0) { vm_write32(blk, 3); empty++; }
    }
    if (empty) { vm_write32(sp + 0x6D0u, 0); vm_write32(sp + 0x6D4u, 3); }
    return empty;
}
// Debug knob: runs this manager update with +0x6D8 = 0 when pin_skip says so. Returns false (nothing called) otherwise.
bool pin_call(ppu_context* c)
{
    uint32_t sp = 0;
    if (!pin_skip(&sp)) return false;
    const uint32_t own0 = own_count(sp);
    vm_write32(sp + 0x6D8u, 0);   // this call only: 0x484690 skips 0x4844DC (no own push / send)
    g_orig_mgr(c);
    drain(c);                     // any tail call of the update runs before the word is written back
    if (vm_read32(sp + 0x6D8u) == 0) vm_write32(sp + 0x6D8u, 1);
    const uint32_t late = pin_fix_states(sp);
    g_pin_left--;
    const uint32_t n = ++g_pin_done;
    if (g_pin_logs.fetch_add(1) < 60) {
        char b[160]; sp_counts(sp, b, sizeof b);
        std::fprintf(stderr, "[sharepad-wd] DEBUG skip-push: own push %u dropped (own ctr was %u%s; %s)\n", n, own0,
                     late ? "; a peer sample is late: this tick pauses" : "", b);
    }
    return true;
}
#endif

// FIX (late but alive): each member's received-sample count (UINT32_MAX for a member without a plausible queue); returns the member count (<= 4).
uint32_t member_counts(uint32_t sp, uint32_t* out)
{
    if (!sp) return 0;
    const uint32_t m = vm_read32(sp + 0x10u), n = m < jspw::kMaxMembers ? m : jspw::kMaxMembers;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t q = vm_read32(sp + 0xBCu + 0xB8u * i + 8u);
        out[i] = plausible(q) && vm_read32(q) != 0 ? vm_read32(q + 8u) : 0xFFFFFFFFu;
    }
    return n;
}

// The watchdog's tick (slot 2, before the stall check). `mode` = slot 2's this.
void wd_tick(ppu_context* c, uint32_t mode)
{
    const uint32_t ep = g_wd_epoch.load();
    if (ep != g_wd_seen_epoch) { g_wd_seen_epoch = ep; jspw::reset(g_wd); g_wd_fed_total = 0; g_wd_hud_last = g_hud_calls; g_wd_real = 0; }
    const uint32_t hud = g_hud_calls;
    const bool hud_drawn = hud != g_wd_hud_last;
    g_wd_hud_last = hud;
    if (!g_wd_after.load() || !g_wd_f2.load()) return;   // the cheap gate: nothing is read in a fight or before the 0x876A60 object is deleted
    const uint32_t sp = share_pad();
    jspw::View v;
    bool g = sp && plausible(mode) && vm_read32(mode) == kOnlineVt && !armed();
    if (g) {
        uint32_t m0 = g_wd_mode.load();
        if (!m0 && g_wd_mode.compare_exchange_strong(m0, mode)) m0 = mode;
        g = m0 == mode;
    }
    v.guards = g;
    v.hud_drawn = hud_drawn;
    v.paused = plausible(mode) && (vm_read32(mode + 0x1Cu) & 0xB0u) == 0xB0u;
    if (sp) {
        v.active = vm_read32(sp + 0x6D8u); v.sync = vm_read32(sp + 0x6D4u); v.members = vm_read32(sp + 0x10u); v.own = own_count(sp);
        for (uint32_t i = 0; i < jspw::kMaxMembers && i < v.members; i++) {
            const uint32_t blk = sp + 0xBCu + 0xB8u * i, q = vm_read32(blk + 8u);
            v.st[i] = vm_read32(blk);
            v.queue_ok[i] = plausible(q) && vm_read32(q) != 0;
            v.count[i] = v.queue_ok[i] ? vm_read32(q + 8u) : 0;
        }
    }
    v.real = g_wd_real.exchange(0);
    bool stopped0[jspw::kMaxMembers];
    for (uint32_t i = 0; i < jspw::kMaxMembers; i++) stopped0[i] = g_wd.stopped[i];
    const uint32_t mask = jspw::step(g_wd, v);
    for (uint32_t i = 0; i < jspw::kMaxMembers; i++)
        if (g_wd.stopped[i] && !stopped0[i] && wd_feed_log_ok()) {
            char b[160]; sp_counts(sp, b, sizeof b);
            std::fprintf(stderr, "[sharepad-wd] match %u: member %u sent a real sample after %u neutral one(s): no more feeding for this match (the %u stay queued "
                         "until the next online mode resets the queues; %s)\n", g_wd_match.load(), i, g_wd.surplus[i], g_wd.surplus[i], b);
        }
    if (!mask) return;
    const uint64_t r2 = c->gpr[2], r3 = c->gpr[3], lr = c->lr, cr = c->cr, ctr = c->ctr;
    for (uint32_t i = 0; i < jspw::kMaxMembers; i++) {   // every starved member in this same tick (2v2)
        if (!(mask & (1u << i))) continue;
        const uint32_t q = vm_read32(sp + 0xBCu + 0xB8u * i + 8u);
        const int r = feed_neutral(c, q);
        c->gpr[2] = r2;
        if (r == 1) g_wd_fed_total++; else jspw::fed_failed(g_wd, i);
        if ((r != 1 || g_wd.fed[i] <= 8) && wd_feed_log_ok()) {
            char b[160]; sp_counts(sp, b, sizeof b);
            const unsigned long ts = g_tid_slot2.load(), tm = g_tid_mgr.load();
            std::fprintf(stderr, "[sharepad-wd] match %u: %s neutral sample %u to member %u (%s; %s; threads slot2 %lu mgr %lu %s)\n", g_wd_match.load(),
                         r == 1 ? "fed" : r == 0 ? "FAILED to feed" : "REFUSED (no free node) to feed", r == 1 ? g_wd.fed[i] : 0u, i,
                         g_wd.ticks[i] == 0 ? "starved 10 ticks, paused, no fight HUD" : "starved again", b, ts, tm,
                         !tm ? "(mgr not seen)" : ts == tm ? "same" : "DIFFERENT");
        }
    }
    c->gpr[2] = r2; c->gpr[3] = r3; c->lr = lr; c->cr = cr; c->ctr = ctr;
}

// mode slot 2, 0x0F7704 (indirect only: the mode vtable): this -> 0x0F73E8, the stall check. Runs every mode tick, paused or not.
void slot2_wrap(ppu_context* c)
{
    if (!g_tid_slot2.load()) {
        unsigned long z = 0; const unsigned long t = (unsigned long)GetCurrentThreadId();
        if (g_tid_slot2.compare_exchange_strong(z, t)) std::fprintf(stderr, "[sharepad-wd] thread: mode slot 2 0x0F7704 runs on host thread %lu\n", t);
    }
    if (!armed()) {
        if (g_wd_on.load()) wd_tick(c, (uint32_t)c->gpr[3]);
    }
    g_orig_slot2(c);
}
// the share pad manager update 0x45222C (indirect, 30/s): the receive push 0x4824CC runs inside it. One-shot thread id. FIX (late but alive): from the
// post-fight deletion to the mode's free (the only time the watchdog may feed), the member counts are read before and after the call, and a rise marks a
// real sample from that peer (g_wd_real): the watchdog then stops feeding that member. In JSP_TEST_KNOBS builds the debug knob may run one call with
// +0x6D8 = 0 (pin_call above).
void mgr_wrap(ppu_context* c)
{
    if (!g_tid_mgr.load()) {
        unsigned long z = 0; const unsigned long t = (unsigned long)GetCurrentThreadId();
        if (g_tid_mgr.compare_exchange_strong(z, t)) std::fprintf(stderr, "[sharepad-wd] thread: manager update 0x45222C (receive push) runs on host thread %lu\n", t);
    }
    const bool watch = g_wd_on.load() && !armed() && g_wd_after.load() && g_wd_f2.load();
    uint32_t sp = 0, n = 0, before[jspw::kMaxMembers] = {};
    if (watch) { sp = share_pad(); n = member_counts(sp, before); }
#if JSP_WD_KNOB
    if (!pin_call(c)) g_orig_mgr(c);
#else
    g_orig_mgr(c);
#endif
    if (!n || share_pad() != sp) return;
    uint32_t after[jspw::kMaxMembers] = {}, real = 0;
    const uint32_t n2 = member_counts(sp, after);
    for (uint32_t i = 0; i < n && i < n2; i++)
        if (before[i] != 0xFFFFFFFFu && after[i] != 0xFFFFFFFFu && after[i] > before[i]) real |= 1u << i;
    if (real) g_wd_real.fetch_or(real);
}
// 0x0E2048 (indirect only): the deleting destructor of the post-fight object of class 0x876A60; its online mode reaches its exit a mode tick later.
void f2_wrap(ppu_context* c)
{
    const uint32_t obj = (uint32_t)c->gpr[3];
    if (!armed() && g_wd_after.load() && plausible(obj) && vm_read32(obj) == kPostFightVt && !g_wd_f2.exchange(1) && wd_log_ok()) {
        char b[160]; sp_counts(share_pad(), b, sizeof b);
        std::fprintf(stderr, "[sharepad-wd] match %u: post-fight object (vt 876A60) deleted, 0x0E2048 (%s)\n", g_wd_match.load(), b);
    }
    g_orig_f2(c);
}
// the online mode's deleting destructor, 0x0F77DC (slot 1, indirect only; on the frame of the second exit poll 0x0F777C): the episode and the
// watchdog's scope end with the mode, so a later fight on the game's own netcode is never touched.
void free_wrap(ppu_context* c)
{
    const uint32_t mode = (uint32_t)c->gpr[3];
    if (!armed() && g_wd_after.load() && plausible(mode) && vm_read32(mode) == kOnlineVt) {
        if (g_wd_fed_total && wd_log_ok()) {
            char b[160]; sp_counts(share_pad(), b, sizeof b);
            // The counts in `b` are what the queues still hold. They do not carry over: the next online mode's build (0x0F7880 / 0x0F7D1C) calls the
            // share pad start 0x484270 and then 0x4848C4, which stops a started share pad (0x484224 -> 0x482AA4 -> 0x4828F4) and starts it again;
            // 0x4828F4 runs 0x473BD4 on every member queue (count 0, every node back on the free list), and 0x484270's own start does the same.
            std::fprintf(stderr, "[sharepad-wd] match %u: online mode freed (0x0F77DC) after %u neutral samples (fed m0..m3: %u %u %u %u; stopped on a real sample "
                         "m0..m3: %d %d %d %d; left queued, reset by the next online mode: %s)\n", g_wd_match.load(), g_wd_fed_total, g_wd.fed[0], g_wd.fed[1],
                         g_wd.fed[2], g_wd.fed[3], g_wd.stopped[0], g_wd.stopped[1], g_wd.stopped[2], g_wd.stopped[3], b);
        }
#if JSP_WD_KNOB
        if (g_pin_on.exchange(0)) {   // debug knob: the window ends with the mode
            char b[160]; sp_counts(share_pad(), b, sizeof b);
            std::fprintf(stderr, "[sharepad-wd] DEBUG skip-push: window over at the mode's free, %u own pushes dropped (%s)\n", g_pin_done.load(), b);
        }
#endif
        g_wd_after = 0; g_wd_f2 = 0; g_wd_mode = 0; g_wd_epoch++;
    }
    g_orig_free(c);
}

bool g_installed = false;
int g_supported = 0;   // FIX (region): jsp_native_fight_supported(); set once by jsp_native_fight_install

}  // namespace

extern "C" void jsp_native_fight_install(void)
{
    if (g_installed) return;
    g_installed = true;
    // FIX (region): the addresses here are the US 1.00 EBOOT's. On any other disc (EU) nothing is hooked, nothing is ever armed, and native-room pairing
    // (jsp_net_game.cpp g_nat.on, read right after this call) is turned off for this run, so no rollback link attaches to the game's own Online Battle fight.
    const char* tid = cellGame_get_title_id();
    const jnf::Support sup = jnf::support(tid, [](uint32_t a) { return (uint32_t)vm_read32(a); });
    if (sup != jnf::SUP_OK) {
        jsp_setting_set_live("JSP_ROLLBACK_NATIVE", "0");
        std::fprintf(stderr, "[online] rollback takeover is US-disc only; Online Battle uses the game's own netcode on this disc (%s %s)\n",
                     tid && *tid ? tid : "(no title id)", sup == jnf::SUP_TITLE ? "is not BLUS31519" : "does not hold the US 1.00 code");
        return;
    }
    g_supported = 1;
    int ok = 0, n = 0;
    auto one = [&](bool b) { n++; ok += b ? 1 : 0; };
    one(jsp_hook_chain::install<fn_t>(0x000F76C4u, slot4_wrap, &g_orig_slot4));
    one(jsp_hook_chain::install<fn_t>(0x000F7234u, take_wrap, &g_orig_take));
    one(Stub<0x000F73E8u, 0>::install());   // stall check: no stall
    one(Stub<0x00484690u, 0>::install());   // share pad update
    one(Stub<0x004844DCu, 0>::install());   // share pad send
    one(Stub<0x004812ECu, 0>::install());   // scheduler
    one(Stub<0x00454EE8u, 0>::install());   // net error report (armed = in the fight only)
    one(Stub<0x00466794u, 3>::install());   // SetPacketError: +0xFC = 0
    one(Stub<0x00466ADCu, 2>::install());   // connection check: ok
    one(Stub<0x004862B0u, 1>::install());   // user error check: none
    std::fprintf(stderr, "[native-fight] hooks installed (%d of %d; pass-through until armed; direct calls need the guarded insn patches)\n", ok, n);
    // FIX (rematch black screen): the share pad watchdog, on unless JSP_SHAREPAD_WATCHDOG is 0/off/false/no. The debug knob (see JSP_WD_KNOB) exists only in
    // JSP_TEST_KNOBS builds (a release build never reads it).
    {
        char b[16] = {};
        const bool wd = !(jsp_setting_get("JSP_SHAREPAD_WATCHDOG", b, sizeof b) && jspw::setting_off(b));
#if defined(JSP_TEST_KNOBS) && JSP_TEST_KNOBS
        const char* sk = std::getenv("JSP_SHAREPAD_SKIP_PUSH");
        const long skn = sk && *sk ? std::strtol(sk, nullptr, 10) : 0;
        g_skip_n = skn > 0 ? (uint32_t)(skn < 300 ? skn : 300) : 0u;
#endif
#if JSP_WD_KNOB
        if (wd || g_skip_n) {
#else
        if (wd) {
#endif
            int k = 0;
            k += jsp_hook_chain::install<fn_t>(0x000F7704u, slot2_wrap, &g_orig_slot2) ? 1 : 0;   // mode slot 2
            k += jsp_hook_chain::install<fn_t>(0x000E2048u, f2_wrap, &g_orig_f2) ? 1 : 0;         // post-fight object's deleting destructor
            k += jsp_hook_chain::install<fn_t>(0x000F77DCu, free_wrap, &g_orig_free) ? 1 : 0;     // the online mode's deleting destructor
            const bool m = jsp_hook_chain::install<fn_t>(0x0045222Cu, mgr_wrap, &g_orig_mgr);    // thread id, real-sample detection
            k += m ? 1 : 0;
            g_wd_on = wd && k == 4;   // never half on: without the 0x0E2048 / free hooks the gates would be wrong, without 0x45222C a late peer's real samples
            std::fprintf(stderr, "[sharepad-wd] share pad watchdog %s (%d of 4 hooks; JSP_SHAREPAD_WATCHDOG=0 turns it off)\n", g_wd_on ? "on" : "OFF", k);
#if JSP_WD_KNOB
            if (k != 4) g_skip_n = 0;   // the debug knob needs the manager-update hook
            if (g_skip_n)
                std::fprintf(stderr, "[sharepad-wd] DEBUG JSP_SHAREPAD_SKIP_PUSH=%u (JSP_TEST_KNOBS build, TEST ONLY): after each normal match end this copy drops up to %u own "
                             "share pad pushes to keep its own input delay at 0 until the exit (its peer starves there)\n", g_skip_n, g_skip_n);
#endif
        } else std::fprintf(stderr, "[sharepad-wd] share pad watchdog off (JSP_SHAREPAD_WATCHDOG=0)\n");
    }
}

extern "C" void jsp_native_fight_end_normal(void) { g_end_normal = 1; }

extern "C" int jsp_native_fight_supported(void) { return g_supported; }

extern "C" int jsp_native_fight_online(void)
{
    if (!g_supported) return 0;   // FIX (region): the US chain below would read unrelated words on another disc
    const uint32_t m = mode_obj();
    return m && vm_read32(m) == kOnlineVt ? 1 : 0;
}

extern "C" int jsp_native_fight_slot(void)
{
    if (!g_supported) return -1;   // FIX (region)
    const uint32_t sp = share_pad();
    if (!sp) return -1;
    const int32_t s = (int32_t)vm_read32(sp + 0x6E0u);
    return s >= 0 && s < (int32_t)jnf::kRecCount ? s : -1;
}

// FIX (swapped controls): see the header. Guest reads only: G = *(*(TOC - 0x7A90)) (0x00030A14), players G+0x60+i*0x94 (+0x70 net slot, +0x78 member id),
// "me" = *(G + 0x48158) (the builder 0x002EF6B8 stores the battle index of the record whose member id is this PC's), room records R+0x68+0x24*r.
extern "C" int jsp_native_fight_map(int local_slot, unsigned seats, char* msg, int n)
{
    g_port_on = false; g_local_port = -1; g_me = -1;
    for (auto& d : g_drove) d = 0;
    for (int8_t& p : g_port) p = -1;
    auto say = [&](const char* fmt, auto... a) { if (msg && n > 0) std::snprintf(msg, (size_t)n, fmt, a...); };
    if (!g_supported) { say("%s", "not the US disc"); return 0; }
    const uint32_t gp = vm_read32(kToc - 0x7A90u);
    const uint32_t G = plausible(gp) ? vm_read32(gp) : 0;
    const uint32_t R = vm_read32(0x0095839Cu);
    if (!plausible(G) || !plausible(R)) { say("no battle player table (%08X) or room data (%08X)", G, R); return 0; }
    int32_t net[jnf::kPlayers]; uint16_t pm[jnf::kPlayers], rm[4]; uint8_t rc[4], rt[4];
    for (uint32_t i = 0; i < jnf::kPlayers; i++) {
        const uint32_t pl = G + jnf::kPlayerBase + i * jnf::kPlayerStride;
        net[i] = (int32_t)vm_read32(pl + jnf::kOffNetSlot); pm[i] = (uint16_t)vm_read16(pl + 0x78u);
    }
    for (uint32_t r = 0; r < 4; r++) {
        const uint32_t a = R + 0x68u + 0x24u * r;
        rm[r] = (uint16_t)vm_read16(a + 0x08u); rt[r] = (uint8_t)vm_read8(a + 0x16u); rc[r] = (uint8_t)vm_read8(a + 0x1Au);
    }
    const int32_t me = (int32_t)vm_read32(G + 0x48158u);
    int8_t port[jnf::kRecCount];
    const jnf::MapErr me_err = jnf::net_ports(net, pm, rm, rc, seats, port);   // FIX (review of PR 248): exactly the planned seats
    const bool ok = me_err == jnf::MAP_OK;
    const int32_t me_net = me >= 0 && me < (int32_t)jnf::kPlayers ? net[me] : -1;
    const int me_port = me_net >= 0 && me_net < (int32_t)jnf::kRecCount ? port[me_net] : -1;
    int p = std::snprintf(msg, (size_t)n, "my fighter = battle player %d (net slot %d, member %u) -> rollback slot %d, mine %d; net slot -> rollback slot:", me, me_net,
                          me >= 0 && me < (int32_t)jnf::kPlayers ? (unsigned)pm[me] : 0u, me_port, local_slot);
    for (uint32_t s = 0; s < jnf::kRecCount && p > 0 && p < n; s++) p += std::snprintf(msg + p, (size_t)(n - p), " %u->%d", s, port[s]);
    for (uint32_t i = 0; i < jnf::kPlayers && p > 0 && p < n; i++)
        if (net[i] >= 0) p += std::snprintf(msg + p, (size_t)(n - p), "; player %u net %d member %u", i, net[i], (unsigned)pm[i]);
    for (uint32_t r = 0; r < 4 && p > 0 && p < n; r++) p += std::snprintf(msg + p, (size_t)(n - p), "; record %u member %u team %u%s", r, (unsigned)rm[r], rt[r], rc[r] ? " COM" : "");
    if (!ok || me_port != local_slot) {
        if (p > 0 && p < n) std::snprintf(msg + p, (size_t)(n - p), " -> MISMATCH (%s; planned seats 0x%X)", !ok ? jnf::map_err_text(me_err) : "my fighter is not on my rollback slot", seats);
        return 0;
    }
    for (uint32_t s = 0; s < jnf::kRecCount; s++) g_port[s] = port[s];
    g_local_port = local_slot; g_me = me; g_port_on = true;
    return 1;
}

extern "C" int jsp_native_fight_owner_check(char* msg, int n)
{
    uint32_t total = 0, mine = 0; int top = -1; uint32_t topn = 0;
    for (uint32_t i = 0; i < jnf::kPlayers; i++) {
        const uint32_t d = g_drove[i].load();
        total += d;
        if ((int32_t)i == g_me) mine = d;
        if (d > topn) { topn = d; top = (int)i; }
    }
    const int r = !total ? -1 : mine == total ? 1 : 0;
    if (msg && n > 0)
        std::snprintf(msg, (size_t)n, "my pad (rollback slot %d) moved battle player %d in %u of %u takes; my fighter is battle player %d -> %s", g_local_port, top, topn, total, g_me,
                      r < 0 ? "no local input yet" : r ? "OK" : "SWAPPED");
    return r;
}

// FIX: arm returns 1 armed / 0 refused, and refuses unless the battle mode object is the ONLINE one (vtable 0x00878A70) and a share pad exists
// (g_jsp_native_fight_armed is set only when armed, as before). FIX: arm saves share pad +0x6D8 / +0x6DC and the object they came from; disarm
// writes them back when that same object is still the share pad (same pointer, same vtable, same manager).
// DECISION (unverified in a game run): a session that ends inside the fight (desync, disconnect, Cancel) used to leave +0x6D8 = 0, so the pad take
// returned 2 (inactive) for the rest of that fight and the game could not go on. Calling the game's own error report 0x454EE8 from here is not safe:
// it is lifted guest code that needs the game thread's PPU context, and disarm runs in the session's main-thread code. Restoring the words hands the
// fight back to the game's own netcode instead (the guarded insn patches are off once g_jsp_native_fight_armed is 0): with the other game still there
// the fight plays on (desynced at worst); with it gone the game's own stall, wait window and disconnect error take over and return to its menu.
// Words the game changed itself since arm (no longer 0) are left alone.
static uint32_t g_arm_sp = 0, g_arm_vt = 0, g_arm_mgr = 0, g_arm_6d8 = 0, g_arm_6dc = 0;
extern "C" int jsp_native_fight_arm(int on)
{
    if (on) {
        if (!g_supported) {   // FIX (region): never arm on a disc the takeover does not know (jsp_native_fight_install said why)
            std::fprintf(stderr, "[native-fight] arm: rollback takeover is US-disc only (not armed)\n");
            return 0;
        }
        const uint32_t sp = share_pad();
        if (!sp) { std::fprintf(stderr, "[native-fight] arm: no share pad object (not armed)\n"); return 0; }
        const uint32_t m = mode_obj();
        const uint32_t vt = m ? vm_read32(m) : 0u;
        if (vt != kOnlineVt) {
            std::fprintf(stderr, "[native-fight] arm: battle mode %08X vtable %08X (%s) is not the online one 0x00878A70 (not armed)\n", m, vt,
                         vt == kOfflineVt ? "OFFLINE" : "other");
            return 0;
        }
        std::fprintf(stderr, "[native-fight] armed: share pad %08X (+6D4=%u +6D8=%u +6DC=%u +6E0=%d), mode %08X vt %08X (online)\n", sp,
                     vm_read32(sp + 0x6D4u), vm_read32(sp + 0x6D8u), vm_read32(sp + 0x6DCu), (int32_t)vm_read32(sp + 0x6E0u), m, vt);
#if JSP_WD_KNOB
        g_pin_on = 0;   // debug knob: its window never reaches a fight
#endif
        g_arm_sp = sp; g_arm_vt = vm_read32(sp); g_arm_mgr = vm_read32(vm_read32(kToc + 0x59F8u));
        g_arm_6d8 = vm_read32(sp + 0x6D8u); g_arm_6dc = vm_read32(sp + 0x6DCu);
        vm_write32(sp + 0x6D8u, 0);   // share pad inactive: 0x4844DC / 0x0F73E8 return at once
        vm_write32(sp + 0x6DCu, 0);   // no start request: 0x484690 does not set +0x6D8 again
        g_jsp_native_fight_armed = 1;
        g_end_normal = 0; g_wd_after = 0; g_wd_f2 = 0; g_wd_mode = 0; g_wd_epoch++;   // FIX (rematch black screen): the watchdog never acts while armed
        return 1;
    }
    const bool was = g_jsp_native_fight_armed != 0;
    const bool normal = g_end_normal.exchange(0) != 0;   // FIX (rematch black screen): consumed by every disarm call, so it never leaks to a later one
    g_jsp_native_fight_armed = 0;
    g_port_on = false;   // FIX (swapped controls): the next session maps its own fight (jsp_native_fight_map); the counts stay for a last owner check
    if (was) {
        const uint32_t sp = share_pad();
        const bool same = sp && sp == g_arm_sp && vm_read32(sp) == g_arm_vt && vm_read32(vm_read32(kToc + 0x59F8u)) == g_arm_mgr;
        // FIX (rematch black screen): only a NORMAL match end (jsp_native_fight_end_normal: an agreed KO / time-up hand-back) opens the watchdog's scope.
        // After a mid-fight end (desync, disconnect, Cancel, F9) the game's own fight plays on and the watchdog stays off.
        // Debug knob (JSP_TEST_KNOBS builds only): a normal end opens the window in which this copy's own input delay is pinned at 0 (pin_skip).
#if JSP_WD_KNOB
        const bool hold = normal && same && g_skip_n && g_orig_mgr;
#endif
        if (same) {
            if (vm_read32(sp + 0x6D8u) == 0) vm_write32(sp + 0x6D8u, g_arm_6d8);
            if (vm_read32(sp + 0x6DCu) == 0) vm_write32(sp + 0x6DCu, g_arm_6dc);
        }
#if JSP_WD_KNOB
        if (hold) { g_pin_left = g_skip_n; g_pin_done = 0; g_pin_logs = 0; g_pin_on = 1; }
#endif
        g_wd_f2 = 0; g_wd_mode = 0; g_wd_epoch++;
        if (normal) g_wd_match++;
        g_wd_after = normal ? 1 : 0;
        char b[160]; sp_counts(sp, b, sizeof b);
        std::fprintf(stderr, "[sharepad-wd] %s disarm, share pad: %s; watchdog %s%s\n", normal ? "normal match-end" : "mid-fight (not a normal match end)", b,
                     normal && g_wd_on.load() ? "armed for the exit" : "off for this match",
#if JSP_WD_KNOB
                     hold ? "; DEBUG skip-push: own delay pinned at 0 until the exit" : "");
#else
                     "");
#endif
        std::fprintf(stderr, "[native-fight] disarmed (share pad +6D8=%u +6DC=%u: %s)\n", g_arm_6d8, g_arm_6dc,
                     !same ? "not restored, the share pad object is gone or another one" : "restored");
    }
    g_arm_sp = g_arm_vt = g_arm_mgr = 0;
    return 0;
}
