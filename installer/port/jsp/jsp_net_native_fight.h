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
// FIX (rematch black screen): called right before jsp_native_fight_arm(0) when the session ends NORMALLY (an agreed KO / time-up hand-back, "Match over").
// Only such a disarm lets the share pad watchdog act on the exit that follows; any other disarm (desync, disconnect, Cancel) leaves it off.
void jsp_native_fight_end_normal(void);
// FIX: returns 1 armed, 0 refused (the mode vtable is not 0x00878A70, or no share pad; g_jsp_native_fight_armed stays 0) and 0 for a disarm,
// which writes share pad +0x6D8 / +0x6DC back while the same share pad object still exists (the .cpp says why).
// The guard of the guarded instruction patches (jnf::kPatches, for lift_insn_patches.json): 1 while armed. The lifted code reads it.
extern int g_jsp_native_fight_armed;
// FIX (swapped controls, testers on 0.7.0-test1): which pad drives which fighter. The battle builder 0x002EEE90 numbers the humans' net slots
// (player +0x70) in PLACEMENT order, team-0 records first, then team-1, each in record order (0x002EF350 stw r20,0x70; r20++), while a PC's rollback slot
// is its room RECORD index (share pad +0x6E0, 0x0F7880 by member id; checked == plan.local_first). They differ when a lower record is on team 1 (the
// versus default put the joiner, record 1, on team 0: net slot 0 = the joiner's fighter, fed from GekkoNet slot 0 = the host's pad, and back).
// jsp_native_fight_map reads the fight's battle players (net slot, member id) and the room records (member id) at the session start and fixes, for the
// whole session, net slot s -> the rollback slot (record index) of the human whose fighter has it; the armed pad take reads that port. It also checks
// this PC's own fighter (the builder's "me", G+0x48158, by member id) gets this PC's rollback slot. 1 = ok, 0 = refused (*msg says why; the map is off).
// FIX (review of PR 248): `seats` = the session plan's rollback seats (bit r = record r, (1 << total_players) - 1); the room's human records and the mapped
// fighters must cover exactly those (jnf::net_ports: no missing, extra or stale member).
// msg always gets the "[net] ownership" line's text. Main thread, before arm; jsp_native_fight_arm(0) clears it.
int  jsp_native_fight_map(int local_slot, unsigned seats, char* msg, int n);
// The ownership check of a running session: the battle players the LOCAL pad's non-neutral samples went to (counted in the armed pad take) against this
// PC's own fighter. 1 = only my fighter got them, 0 = another fighter got them (SWAPPED), -1 = no local input seen yet. *msg = the line's text.
int  jsp_native_fight_owner_check(char* msg, int n);
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

// FIX (swapped controls): the builder's net slot of room record `rec` (0x002EEE90: team-0 records, then team-1, each in record order; a COM gets none).
// team[i] = record +0x16 (0 / 1; anything else is not placed), com[i] = record +0x1A != 0. -1: rec is a COM or not placed.
inline int placement_net_slot(const uint8_t team[4], const uint8_t com[4], int rec)
{
    if (rec < 0 || rec > 3 || com[rec] || team[rec] > 1) return -1;
    int n = 0;
    for (int t = 0; t <= 1; t++)
        for (int i = 0; i < 4; i++) {
            if (team[i] != t || com[i]) continue;
            if (i == rec) return n;
            n++;
        }
    return -1;
}
// FIX (swapped controls): port[s] = the room record (= rollback slot) of the human whose battle player has net slot s, matched by member id
// (player +0x78 == record +0x08, human records only); -1 = no player has s.
// FIX (review of PR 248: the mapper accepted incomplete tables): the room's human records and the mapped fighters must cover EXACTLY the planned
// rollback seats (`seats`, bit r = record / rollback slot r is played by a human of this session: (1 << plan.total_players) - 1). Refused:
//   MAP_SLOT       a net slot outside 0..3 or used by two fighters;
//   MAP_MEMBER     a fighter whose member id is in no human record (a stale fighter: its player left the room), or in two records;
//   MAP_RECORD     two fighters on one record;
//   MAP_EXTRA      a human record outside the seats (an extra or stale room member), or a fighter on one;
//   MAP_MISSING    a planned seat with no human record, or whose record no fighter is mapped to (a missing member or fighter).
// A record with member id 0 is empty, a COM record (+0x1A != 0) is never a human.
enum MapErr { MAP_OK = 0, MAP_SLOT, MAP_MEMBER, MAP_RECORD, MAP_EXTRA, MAP_MISSING };
inline const char* map_err_text(MapErr e)
{
    switch (e) {
    case MAP_OK: return "ok";
    case MAP_SLOT: return "a fighter net slot is out of range or used twice";
    case MAP_MEMBER: return "a fighter's member is in no human room record (stale) or in two";
    case MAP_RECORD: return "two fighters belong to one room record";
    case MAP_EXTRA: return "a human room record or fighter is outside the planned rollback seats (extra or stale member)";
    case MAP_MISSING: return "a planned rollback seat has no human room record or no fighter (missing member)";
    }
    return "?";
}
inline MapErr net_ports(const int32_t net[kPlayers], const uint16_t pmember[kPlayers], const uint16_t rmember[4], const uint8_t rcom[4], uint32_t seats,
                        int8_t port[kRecCount])
{
    for (uint32_t s = 0; s < kRecCount; s++) port[s] = -1;
    seats &= 0xFu;
    uint32_t used = 0, humans = 0;
    for (int r = 0; r < 4; r++) if (!rcom[r] && rmember[r]) humans |= 1u << r;
    if (humans & ~seats) return MAP_EXTRA;
    for (uint32_t i = 0; i < kPlayers; i++) {
        const int32_t s = net[i];
        if (s < 0) continue;
        if (s >= (int32_t)kRecCount || port[s] >= 0) return MAP_SLOT;
        int rec = -1;
        for (int r = 0; r < 4; r++) {
            if (!(humans & (1u << r)) || rmember[r] != pmember[i]) continue;
            if (rec >= 0) return MAP_MEMBER;
            rec = r;
        }
        if (rec < 0) return MAP_MEMBER;
        if (used & (1u << rec)) return MAP_RECORD;
        used |= 1u << rec;
        port[s] = (int8_t)rec;
    }
    if (used & ~seats) return MAP_EXTRA;
    if ((humans & seats) != seats || (used & seats) != seats) return MAP_MISSING;
    return MAP_OK;
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
