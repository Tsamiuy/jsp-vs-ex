// Host test of the pure part of installer/port/jsp/jsp_net_native_fight.h: the pad take's previous / newly-pressed words, which players get a
// sample, and that every guarded instruction patch replaces the call it names. No game: build with any C++20 compiler and run.
//   bash installer/tools/native_fight_test.sh
#include "../port/jsp/jsp_net_native_fight.h"
#include <cstdio>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main()
{
    using namespace jnf;
    // edges as 0x0F738C..0x0F7398: prev = cur, pressed = cur & ~old prev
    { Edges e = edges(0x0000000Fu, 0x00000003u); CHECK(e.prev == 0xFu && e.pressed == 0xCu); }
    { Edges e = edges(0, 0xFFFFFFFFu); CHECK(e.prev == 0 && e.pressed == 0); }
    { Edges e = edges(0x00100001u, 0); CHECK(e.prev == 0x00100001u && e.pressed == 0x00100001u); }
    { uint32_t prev = 0, held = 0; for (int t = 0; t < 3; t++) { Edges e = edges(0x40u, prev); prev = e.prev; held += e.pressed ? 1 : 0; } CHECK(held == 1); }
    // which players: COM (< 0) never, the original's (members + 1) > slot, 4 records, one sample per slot per call
    { uint32_t s = 0; CHECK(!take_slot(-1, 2, &s) && s == 0); }
    { uint32_t s = 0; CHECK(take_slot(0, 2, &s) && take_slot(1, 2, &s) && s == 3u); CHECK(!take_slot(1, 2, &s)); }
    { uint32_t s = 0; CHECK(take_slot(2, 2, &s)); CHECK(!take_slot(3, 2, &s)); }   // the original's bound is count + 1
    { uint32_t s = 0; CHECK(!take_slot(4, 9, &s)); CHECK(!take_slot(0, -1, &s)); }
    // FIX (swapped controls): the builder's net slots are in placement order (team 0 first); the pad take maps them to the owner's record by member id.
    {   // the testers' fight: records 0 host (member 1), 1 joiner (member 2), 2-3 COM; teams 1,0,1,0 after the old versus swap
        const uint8_t team[4] = {1, 0, 1, 0}, com[4] = {0, 0, 1, 1};
        CHECK(placement_net_slot(team, com, 1) == 0 && placement_net_slot(team, com, 0) == 1 && placement_net_slot(team, com, 2) == -1);
        const int32_t net[6] = {0, -1, 1, -1, -1, -1};   // battle players as the two-copy repro read them: player 0 net 0 member 2, player 2 net 1 member 1
        const uint16_t pm[6] = {2, 0, 1, 0, 0, 0}, rm[4] = {1, 2, 0, 0};
        int8_t port[4];
        CHECK(net_ports(net, pm, rm, com, 0x3u, port) == MAP_OK && port[0] == 1 && port[1] == 0 && port[2] == -1 && port[3] == -1);
    }
    {   // the fixed versus (teams 0,1,1,0): net slot = record, the identity map
        const uint8_t team[4] = {0, 1, 1, 0}, com[4] = {0, 0, 1, 1};
        CHECK(placement_net_slot(team, com, 0) == 0 && placement_net_slot(team, com, 1) == 1);
        const int32_t net[6] = {0, -1, 1, -1, -1, -1}; const uint16_t pm[6] = {1, 0, 2, 0, 0, 0}, rm[4] = {1, 2, 0, 0};
        int8_t port[4]; CHECK(net_ports(net, pm, rm, com, 0x3u, port) == MAP_OK && port[0] == 0 && port[1] == 1);
    }
    {   // 4 humans 1 + 3 vs 2 + 4 (teams 0,1,0,1): net slots 0,2,1,3 for records 0,1,2,3
        const uint8_t team[4] = {0, 1, 0, 1}, com[4] = {0, 0, 0, 0};
        CHECK(placement_net_slot(team, com, 0) == 0 && placement_net_slot(team, com, 2) == 1 && placement_net_slot(team, com, 1) == 2 && placement_net_slot(team, com, 3) == 3);
        const int32_t net[6] = {0, 1, 2, 3, -1, -1}; const uint16_t pm[6] = {11, 13, 12, 14, 0, 0}, rm[4] = {11, 12, 13, 14};
        int8_t port[4]; CHECK(net_ports(net, pm, rm, com, 0xFu, port) == MAP_OK && port[0] == 0 && port[1] == 2 && port[2] == 1 && port[3] == 3);
    }
    {   // refusals: a member in no human record, one member twice, a net slot used twice, a net slot out of range
        const uint8_t com[4] = {0, 0, 1, 1}; const uint16_t rm[4] = {1, 2, 0, 0}; int8_t port[4];
        { const int32_t net[6] = {0, 1, -1, -1, -1, -1}; const uint16_t pm[6] = {1, 9, 0, 0, 0, 0}; CHECK(net_ports(net, pm, rm, com, 0x3u, port) == MAP_MEMBER); }
        { const int32_t net[6] = {0, 1, -1, -1, -1, -1}; const uint16_t pm[6] = {1, 1, 0, 0, 0, 0}; CHECK(net_ports(net, pm, rm, com, 0x3u, port) == MAP_RECORD); }
        { const int32_t net[6] = {0, 0, -1, -1, -1, -1}; const uint16_t pm[6] = {1, 2, 0, 0, 0, 0}; CHECK(net_ports(net, pm, rm, com, 0x3u, port) == MAP_SLOT); }
        { const int32_t net[6] = {4, -1, -1, -1, -1, -1}; const uint16_t pm[6] = {1, 0, 0, 0, 0, 0}; CHECK(net_ports(net, pm, rm, com, 0x3u, port) == MAP_SLOT); }
        { const uint16_t rm2[4] = {1, 1, 0, 0}; const int32_t net[6] = {0, -1, -1, -1, -1, -1}; const uint16_t pm[6] = {1, 0, 0, 0, 0, 0}; CHECK(net_ports(net, pm, rm2, com, 0x3u, port) == MAP_MEMBER); }
    }
    // FIX (review of PR 248): the humans and the fighters must cover EXACTLY the planned rollback seats; missing, extra and stale members are refused
    {   // 2 seats (records 0, 1), records 2-3 COM
        const uint8_t com[4] = {0, 0, 1, 1}; int8_t port[4];
        const int32_t net2[6] = {0, -1, 1, -1, -1, -1};
        // missing: the joiner's fighter is not in the battle (one fighter only), so seat 1 has no fighter
        { const uint16_t rm[4] = {1, 2, 0, 0}; const int32_t net[6] = {0, -1, -1, -1, -1, -1}; const uint16_t pm[6] = {1, 0, 0, 0, 0, 0};
          CHECK(net_ports(net, pm, rm, com, 0x3u, port) == MAP_MISSING); }
        // missing: seat 1's room record is empty (member id 0: the joiner is gone from the room) while the plan still has 2 seats
        { const uint16_t rm[4] = {1, 0, 0, 0}; const uint16_t pm[6] = {1, 0, 2, 0, 0, 0};
          CHECK(net_ports(net2, pm, rm, com, 0x3u, port) == MAP_MEMBER); }
        { const uint16_t rm[4] = {1, 0, 0, 0}; const int32_t net[6] = {0, -1, -1, -1, -1, -1}; const uint16_t pm[6] = {1, 0, 0, 0, 0, 0};
          CHECK(net_ports(net, pm, rm, com, 0x3u, port) == MAP_MISSING); }
        // missing: a COM sits on a planned seat (record 1 COM), so the plan's second human has no record
        { const uint8_t com1[4] = {0, 1, 1, 1}; const uint16_t rm[4] = {1, 2, 0, 0}; const int32_t net[6] = {0, -1, -1, -1, -1, -1}; const uint16_t pm[6] = {1, 0, 0, 0, 0, 0};
          CHECK(net_ports(net, pm, rm, com1, 0x3u, port) == MAP_MISSING); }
        // extra: a third human record (a player who joined the room late) outside the plan's 2 seats, with or without a fighter
        { const uint8_t com3[4] = {0, 0, 0, 1}; const uint16_t rm[4] = {1, 2, 3, 0}; const uint16_t pm[6] = {1, 0, 2, 0, 0, 0};
          CHECK(net_ports(net2, pm, rm, com3, 0x3u, port) == MAP_EXTRA); }
        { const uint8_t com3[4] = {0, 0, 0, 1}; const uint16_t rm[4] = {1, 2, 3, 0}; const int32_t net[6] = {0, -1, 1, 2, -1, -1}; const uint16_t pm[6] = {1, 0, 2, 3, 0, 0};
          CHECK(net_ports(net, pm, rm, com3, 0x3u, port) == MAP_EXTRA); }
        // extra: a lone human on record 2 while the plan's seats are records 0 and 1 (a stale room table from another match)
        { const uint8_t comx[4] = {0, 1, 0, 1}; const uint16_t rm[4] = {1, 0, 2, 0}; const uint16_t pm[6] = {1, 0, 2, 0, 0, 0};
          CHECK(net_ports(net2, pm, rm, comx, 0x3u, port) == MAP_EXTRA); }
        // stale: a fighter left over with a member id no record holds any more (the room changed after the fight was built)
        { const uint16_t rm[4] = {1, 2, 0, 0}; const uint16_t pm[6] = {1, 0, 7, 0, 0, 0};
          CHECK(net_ports(net2, pm, rm, com, 0x3u, port) == MAP_MEMBER); }
        // stale: a third fighter of a member who is not in the room at all
        { const uint16_t rm[4] = {1, 2, 0, 0}; const int32_t net[6] = {0, -1, 1, 2, -1, -1}; const uint16_t pm[6] = {1, 0, 2, 9, 0, 0};
          CHECK(net_ports(net, pm, rm, com, 0x3u, port) == MAP_MEMBER); }
        // the plan has more seats than the room has humans: a 4-seat plan over a 2-human room
        { const uint16_t rm[4] = {1, 2, 0, 0}; const uint16_t pm[6] = {1, 0, 2, 0, 0, 0};
          CHECK(net_ports(net2, pm, rm, com, 0xFu, port) == MAP_MISSING); }
        // exactly covered: the same tables pass with the plan's 2 seats
        { const uint16_t rm[4] = {1, 2, 0, 0}; const uint16_t pm[6] = {1, 0, 2, 0, 0, 0};
          CHECK(net_ports(net2, pm, rm, com, 0x3u, port) == MAP_OK && port[0] == 0 && port[1] == 1); }
        for (int e = MAP_OK; e <= MAP_MISSING; e++) CHECK(map_err_text((MapErr)e)[0] != '?');
    }
    {   // 2v2 over 4 seats: one human record of the four is a COM now (a player dropped out and the game put a COM in): missing
        const uint8_t com[4] = {0, 0, 0, 1}; const uint16_t rm[4] = {11, 12, 13, 14}; int8_t port[4];
        const int32_t net[6] = {0, 1, 2, -1, -1, -1}; const uint16_t pm[6] = {11, 13, 12, 0, 0, 0};
        CHECK(net_ports(net, pm, rm, com, 0xFu, port) == MAP_MISSING);
    }
    // patches: a replaced bl names its target; the replacement is never a branch (the lifter refuses those)
    for (const Patch& p : kPatches) {
        if (p.target) CHECK(bl_target(p.addr, p.orig) == p.target);
        else CHECK(bl_target(p.addr, p.orig) == 0);
        const uint32_t op = p.repl >> 26;
        CHECK(op != 16 && op != 17 && op != 18 && op != 19);
        CHECK(!(p.addr & 3u));
    }
    CHECK(bl_target(0x0045238Cu, 0x48032305u) == 0x00484690u);
    CHECK(bl_target(0x00468178u, 0x4BFFE965u) == 0x00466ADCu);   // backward branch
    std::printf(fails ? "native fight: %d FAILED\n" : "native fight: all checks passed\n", fails);
    return fails ? 1 : 0;
}
