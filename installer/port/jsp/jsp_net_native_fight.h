// jsp_net_native_fight.h -- ENHANCEMENT (EXPERIMENTAL, off unless armed): the rollback session takes over the game's OWN online fight
// (Friendly Match, mode vtable 0x00878A70). The C API is for jsp_net_game.cpp (main thread); the pure part (namespace jnf) is for the host test
// installer/tools/native_fight_test.cpp. Implementation and the guest analysis: jsp_net_native_fight.cpp.
#pragma once
#include <cstdint>
#include <cstring>

#ifdef __cplusplus
extern "C" {
#endif
void jsp_native_fight_install(void);   // once at start: registers the hooks, all pass-through until armed (zero behaviour change)
// FIX (region): 1 when this game is the US 1.00 disc the takeover was traced on (jnf::support), else 0. Decided by jsp_native_fight_install (0 before it).
// 0: no hook is installed, jsp_native_fight_online() is 0, arm refuses, and install turns native-room pairing off for this run (JSP_ROLLBACK_NATIVE=0
// as a live, unsaved setting), so Online Battle stays on the game's own netcode.
int  jsp_native_fight_supported(void);
int  jsp_native_fight_online(void);    // 1 when the current battle mode object is the ONLINE one (vtable 0x00878A70), else 0
int  jsp_native_fight_slot(void);      // my battle slot (share pad +0x6E0; host 0, join 1 in the traced Friendly Match), -1 unknown
int  jsp_native_fight_arm(int on);     // 1 at the rollback session's first tick (before its first save), 0 when the session ended
// FIX: returns 1 armed, 0 refused (the mode vtable is not 0x00878A70, or no share pad; g_jsp_native_fight_armed stays 0) and 0 for a disarm,
// which writes share pad +0x6D8 / +0x6DC back while the same share pad object still exists (the .cpp says why).
// The guard of the guarded instruction patches (jnf::kPatches, for lift_insn_patches.json): 1 while armed. The lifted code reads it.
extern int g_jsp_native_fight_armed;
#ifdef __cplusplus
}
#endif

namespace jnf {

// One battle player's sample record in the online mode object: mode + 0x14C + slot * 20 (4 records; the ctor 0x0F7880 clears 4 x 20 bytes).
// +0x00 u32 buttons, +0x04 four stick bytes, +0x08 u32 flags (the 12 bytes the queue pop 0x48495C wrote), +0x0C previous buttons, +0x10 newly pressed.
constexpr uint32_t kRecBase = 0x14C, kRecStride = 20, kRecCount = 4, kOffPrev = 0x0C, kOffPressed = 0x10;
// Battle player table: global G = *(*(TOC - 0x7A90)) (0x00030A14), player i at G + 0x60 + i * 0x94 (i = 0..5), +0x70 net slot (s32, < 0 = not a
// net player: COM), +0x78 room member id (u16).
constexpr uint32_t kPlayers = 6, kPlayerBase = 0x60, kPlayerStride = 0x94, kOffNetSlot = 0x70;

// 0x0F7234 at 0x0F738C..0x0F7398, after a successful pop: [rec+0xC] = cur; [rec+0x10] = cur & ~old [rec+0xC].
struct Edges { uint32_t prev, pressed; };
inline Edges edges(uint32_t cur, uint32_t old_prev) { return Edges{cur, cur & ~old_prev}; }

// 0x0F7234 0x0F7330..0x0F7368: a player gets a sample when its net slot is >= 0 and (member count + 1) > slot (count = 0x484A1C(sp):
// sp+0x10 if sp+0 != 0, else 0). Added here: the slot must name one of the 4 records, and one sample per slot per call (`*seen`, a local of
// one call: two players naming one slot would otherwise get pressed = 0 on the second build; the original popped two queue entries then).
inline bool take_slot(int32_t slot, int32_t members, uint32_t* seen)
{
    if (slot < 0) return false;
    if (!(members + 1 > slot)) return false;
    if (slot >= (int32_t)kRecCount) return false;
    if (*seen & (1u << slot)) return false;
    *seen |= 1u << slot;
    return true;
}

// Target of a PowerPC `bl` word at `site` (I-form, LK=1, AA=0), 0 when the word is not one.
inline uint32_t bl_target(uint32_t site, uint32_t word)
{
    if ((word & 0xFC000003u) != 0x48000001u) return 0;
    int32_t li = (int32_t)(word & 0x03FFFFFCu);
    if (li & 0x02000000) li -= 0x04000000;
    return site + (uint32_t)li;
}

// Guarded instruction patches (guard g_jsp_native_fight_armed) for the DIRECT calls a runtime hook cannot reach (see the .cpp header).
// orig words read from the US 1.00 EBOOT.elf; the lifter refuses a patch whose orig does not match.
struct Patch { uint32_t addr, orig, repl, target; const char* note; };
constexpr Patch kPatches[] = {
    {0x0045238Cu, 0x48032305u, 0x60000000u, 0x00484690u, "manager update 0x45222C: bl share pad update 0x484690 -> nop"},
    {0x00452294u, 0x4802F059u, 0x60000000u, 0x004812ECu, "manager update 0x45222C: bl scheduler 0x4812EC -> nop"},
    {0x00454F50u, 0x80039078u, 0x38000000u, 0, "net error 0x454EE8: lwz r0,-0x6F88(r3) -> li r0,0 (every report path ends there: no error is raised)"},
    {0x004516E4u, 0x38800001u, 0x38800000u, 0, "li r4,1 -> li r4,0 before bl 0x466794 @0x4516E8: it stores +0xFC = 0, no error"},
    {0x004666E8u, 0x38800001u, 0x38800000u, 0, "li r4,1 -> li r4,0 before bl 0x466794 @0x4666EC"},
    {0x00466830u, 0x38800001u, 0x38800000u, 0, "li r4,1 -> li r4,0 for bl 0x466794 @0x46685C (r4 is used by that call only)"},
    {0x004673B8u, 0x38800001u, 0x38800000u, 0, "li r4,1 -> li r4,0 before bl 0x466794 @0x4673BC"},
    {0x00468178u, 0x4BFFE965u, 0x38600001u, 0x00466ADCu, "bl 0x466ADC -> li r3,1"},
    {0x004682D4u, 0x4BFFE809u, 0x38600001u, 0x00466ADCu, "bl 0x466ADC -> li r3,1"},
    {0x0047DFD8u, 0x480082D9u, 0x38600000u, 0x004862B0u, "bl 0x4862B0 -> li r3,0"},
};

// FIX (region): every address above and in the .cpp is the US 1.00 EBOOT's (BLUS31519); firstrun.py lifts lift_insn_patches.json for US only. On another disc
// (EU BLES02136) the hooks would sit on unrelated code, so the takeover runs only when the disc's PARAM.SFO title id is the US one (an empty or unknown id is
// not refused for that alone) AND every kPatches orig word reads back from the loaded EBOOT (rd(addr) = the guest word), the same proof the lifter asks for.
constexpr const char* kTitleUs = "BLUS31519";
enum Support { SUP_OK = 0, SUP_TITLE = 1, SUP_CODE = 2 };
template <class Read> Support support(const char* title_id, Read rd)
{
    if (title_id && *title_id && std::strcmp(title_id, kTitleUs) != 0) return SUP_TITLE;
    for (const Patch& p : kPatches) if (rd(p.addr) != p.orig) return SUP_CODE;
    return SUP_OK;
}

}  // namespace jnf
