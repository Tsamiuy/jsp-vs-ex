// jsp_net_team_rule.h -- FIX (review of PR 248): a rollback match is 2 vs 2 only; a 3 vs 3 fight is refused at the session start with a clear message.
// Why: the 3 vs 3 feature (feat_3v3.cpp) keeps running during the fight (the battle manager's 6-unit growth, the role watch, the held roles) on host-side
// state and wall-clock timers that a rollback does not restore and that differ between the two PCs, so the two simulations would diverge. Making those
// hooks rollback-safe is later work; until then the rollback start reads the fight's team size and refuses 3 vs 3 (the other PC is told: SessionRefused).
// The team size: the battle world (the pointer at 0x008D3888 - 0x7A90, then its object) holds 6 units from +0x60, 0x94 apart; +0x08 is the role
// (1 leader, 2 partner, 3 Support). A 2 vs 2 fight has 4 fighters (roles 1 / 2) and 2 Supports; 3 vs 3 makes units 4 and 5 fighters (6 fighters).
// Pure (a read callback, no VM, no Windows): installer/tools/net_team_rule_test.sh.
#ifndef JSP_NET_TEAM_RULE_H
#define JSP_NET_TEAM_RULE_H
#include <cstdint>

namespace jptr {

constexpr uint32_t kWorldPtr = 0x008D3888u - 0x7A90u;
constexpr uint32_t kUnits = 0x60u, kUnitStride = 0x94u, kRoleOff = 0x08u;
constexpr int kUnitCount = 6;

enum class Teams { Unknown, TwoVsTwo, ThreeVsThree };

// read32(ea) -> the guest word at ea (big-endian already swapped). Unknown: no battle world, or roles that fit neither shape (the caller logs it and
// lets the start go on: the guard must not refuse a 2 vs 2 match it cannot read; the session's own checks still apply).
template <class Read32>
Teams classify(Read32 read32)
{
    const uint32_t w0 = read32(kWorldPtr);
    if (w0 < 0x00010000u || w0 >= 0x30000000u) return Teams::Unknown;
    const uint32_t w = read32(w0);
    if (w < 0x00010000u || w >= 0xF0000000u) return Teams::Unknown;
    int fighters = 0, supports = 0;
    for (int k = 0; k < kUnitCount; k++) {
        const uint32_t role = read32(w + kUnits + kUnitStride * (uint32_t)k + kRoleOff);
        if (role == 1u || role == 2u) fighters++;
        else if (role == 3u) supports++;
        else return Teams::Unknown;
    }
    if (fighters == 4 && supports == 2) return Teams::TwoVsTwo;
    if (fighters == 6) return Teams::ThreeVsThree;
    return Teams::Unknown;
}

constexpr const char* kRefusal = "3 vs 3 is not available with rollback yet (rollback matches are 2 vs 2 only)";

}  // namespace jptr
#endif
