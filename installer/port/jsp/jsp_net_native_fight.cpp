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

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "ps3emu/ps3types.h"
#include "ppu_recomp.h"
extern "C" void ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
#include "jsp_hook_chain.h"
extern "C" const char* cellGame_get_title_id(void);                     // libs/system/cellGame.c: PARAM.SFO's TITLE_ID (read by main.cpp before the table)
extern "C" void jsp_setting_set_live(const char* key, const char* value);   // jsp_settings.cpp: an in-process setting, never written to the file

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
}

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
extern "C" int jsp_native_fight_map(int local_slot, unsigned planned_players, char* msg, int n)
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
    const bool ok = jnf::net_ports(net, pm, rm, rc, planned_players, port);
    const int32_t me_net = me >= 0 && me < (int32_t)jnf::kPlayers ? net[me] : -1;
    const int me_port = me_net >= 0 && me_net < (int32_t)jnf::kRecCount ? port[me_net] : -1;
    int p = std::snprintf(msg, (size_t)n, "my fighter = battle player %d (net slot %d, member %u) -> rollback slot %d, mine %d; net slot -> rollback slot:", me, me_net,
                          me >= 0 && me < (int32_t)jnf::kPlayers ? (unsigned)pm[me] : 0u, me_port, local_slot);
    for (uint32_t s = 0; s < jnf::kRecCount && p > 0 && p < n; s++) p += std::snprintf(msg + p, (size_t)(n - p), " %u->%d", s, port[s]);
    for (uint32_t i = 0; i < jnf::kPlayers && p > 0 && p < n; i++)
        if (net[i] >= 0) p += std::snprintf(msg + p, (size_t)(n - p), "; player %u net %d member %u", i, net[i], (unsigned)pm[i]);
    for (uint32_t r = 0; r < 4 && p > 0 && p < n; r++) p += std::snprintf(msg + p, (size_t)(n - p), "; record %u member %u team %u%s", r, (unsigned)rm[r], rt[r], rc[r] ? " COM" : "");
    if (!ok || me_port != local_slot) {
        if (p > 0 && p < n) std::snprintf(msg + p, (size_t)(n - p), " -> MISMATCH (%s)", !ok ? "the battle fighters and human room records do not cover the planned player slots" : "my fighter is not on my rollback slot");
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
        g_arm_sp = sp; g_arm_vt = vm_read32(sp); g_arm_mgr = vm_read32(vm_read32(kToc + 0x59F8u));
        g_arm_6d8 = vm_read32(sp + 0x6D8u); g_arm_6dc = vm_read32(sp + 0x6DCu);
        vm_write32(sp + 0x6D8u, 0);   // share pad inactive: 0x4844DC / 0x0F73E8 return at once
        vm_write32(sp + 0x6DCu, 0);   // no start request: 0x484690 does not set +0x6D8 again
        g_jsp_native_fight_armed = 1;
        return 1;
    }
    const bool was = g_jsp_native_fight_armed != 0;
    g_jsp_native_fight_armed = 0;
    g_port_on = false;   // FIX (swapped controls): the next session maps its own fight (jsp_native_fight_map); the counts stay for a last owner check
    if (was) {
        const uint32_t sp = share_pad();
        const bool same = sp && sp == g_arm_sp && vm_read32(sp) == g_arm_vt && vm_read32(vm_read32(kToc + 0x59F8u)) == g_arm_mgr;
        if (same) {
            if (vm_read32(sp + 0x6D8u) == 0) vm_write32(sp + 0x6D8u, g_arm_6d8);
            if (vm_read32(sp + 0x6DCu) == 0) vm_write32(sp + 0x6DCu, g_arm_6dc);
        }
        std::fprintf(stderr, "[native-fight] disarmed (share pad +6D8=%u +6DC=%u: %s)\n", g_arm_6d8, g_arm_6dc,
                     same ? "restored" : "not restored, the share pad object is gone or another one");
    }
    g_arm_sp = g_arm_vt = g_arm_mgr = 0;
    return 0;
}
