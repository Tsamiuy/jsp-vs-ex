// Host test of the pure part of installer/port/jsp/jsp_net_native_fight.h: the pad take's previous / newly-pressed words, which players get a
// sample, and that every guarded instruction patch replaces the call it names. No game: build with any C++20 compiler and run.
//   bash installer/tools/native_fight_test.sh
#include "../port/jsp/jsp_net_native_fight.h"
#include <algorithm>
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
        CHECK(net_ports(net, pm, rm, com, 2, port) && port[0] == 1 && port[1] == 0 && port[2] == -1 && port[3] == -1);
    }
    {   // the fixed versus (teams 0,1,1,0): net slot = record, the identity map
        const uint8_t team[4] = {0, 1, 1, 0}, com[4] = {0, 0, 1, 1};
        CHECK(placement_net_slot(team, com, 0) == 0 && placement_net_slot(team, com, 1) == 1);
        const int32_t net[6] = {0, -1, 1, -1, -1, -1}; const uint16_t pm[6] = {1, 0, 2, 0, 0, 0}, rm[4] = {1, 2, 0, 0};
        int8_t port[4]; CHECK(net_ports(net, pm, rm, com, 2, port) && port[0] == 0 && port[1] == 1);
    }
    {   // 4 humans 1 + 3 vs 2 + 4 (teams 0,1,0,1): net slots 0,2,1,3 for records 0,1,2,3
        const uint8_t team[4] = {0, 1, 0, 1}, com[4] = {0, 0, 0, 0};
        CHECK(placement_net_slot(team, com, 0) == 0 && placement_net_slot(team, com, 2) == 1 && placement_net_slot(team, com, 1) == 2 && placement_net_slot(team, com, 3) == 3);
        const int32_t net[6] = {0, 1, 2, 3, -1, -1}; const uint16_t pm[6] = {11, 13, 12, 14, 0, 0}, rm[4] = {11, 12, 13, 14};
        int8_t port[4]; CHECK(net_ports(net, pm, rm, com, 4, port) && port[0] == 0 && port[1] == 2 && port[2] == 1 && port[3] == 3);
    }
    {   // refusals: a member in no human record, one member twice, a net slot used twice, a net slot out of range
        const uint8_t com[4] = {0, 0, 1, 1}; const uint16_t rm[4] = {1, 2, 0, 0}; int8_t port[4];
        { const int32_t net[6] = {0, 1, -1, -1, -1, -1}; const uint16_t pm[6] = {1, 9, 0, 0, 0, 0}; CHECK(!net_ports(net, pm, rm, com, 2, port)); }
        { const int32_t net[6] = {0, 1, -1, -1, -1, -1}; const uint16_t pm[6] = {1, 1, 0, 0, 0, 0}; CHECK(!net_ports(net, pm, rm, com, 2, port)); }
        { const int32_t net[6] = {0, 0, -1, -1, -1, -1}; const uint16_t pm[6] = {1, 2, 0, 0, 0, 0}; CHECK(!net_ports(net, pm, rm, com, 2, port)); }
        { const int32_t net[6] = {4, -1, -1, -1, -1, -1}; const uint16_t pm[6] = {1, 0, 0, 0, 0, 0}; CHECK(!net_ports(net, pm, rm, com, 2, port)); }
        { const uint16_t rm2[4] = {1, 1, 0, 0}; const int32_t net[6] = {0, -1, -1, -1, -1, -1}; const uint16_t pm[6] = {1, 0, 0, 0, 0, 0}; CHECK(!net_ports(net, pm, rm2, com, 2, port)); }
    }
    // Rollback handles are dense, but fighter order can be any permutation of the participants.
    for (unsigned count = 1; count <= 4; ++count) {
        int record[] = {0, 1, 2, 3};
        const uint16_t room[] = {31, 47, 59, 73};
        uint8_t cpu[4];
        for (unsigned i = 0; i < 4; ++i) cpu[i] = i >= count;
        do {
            int32_t net[6] = {-1, -1, -1, -1, -1, -1};
            uint16_t battle[6] = {};
            for (unsigned i = 0; i < count; ++i) {
                net[i + 1] = static_cast<int32_t>(i);
                battle[i + 1] = room[record[i]];
            }
            int8_t port[4];
            CHECK(net_ports(net, battle, room, cpu, count, port));
            for (unsigned i = 0; i < 4; ++i) CHECK(port[i] == (i < count ? record[i] : -1));
        } while (std::next_permutation(record, record + count));
    }
    {
        const uint16_t room[4] = {31, 47, 59, 0};
        const uint8_t cpu[4] = {0, 0, 1, 1}, three_humans[4] = {0, 0, 0, 1};
        int32_t net[6] = {0, -1, -1, -1, -1, -1};
        uint16_t battle[6] = {31, 47, 59};
        int8_t port[4];
        CHECK(!net_ports(net, battle, room, cpu, 2, port));  // host only
        net[0] = -1; net[1] = 1;
        CHECK(!net_ports(net, battle, room, cpu, 2, port));  // peer only
        net[0] = 0;
        CHECK(net_ports(net, battle, room, cpu, 2, port));
        CHECK(!net_ports(net, battle, room, three_humans, 2, port));  // unplanned occupied human record
        net[2] = 2;
        CHECK(!net_ports(net, battle, room, three_humans, 2, port));  // extra mapped fighter
        CHECK(!net_ports(net, battle, room, cpu, 2, port));  // CPU member cannot acquire a net slot
        net[2] = -1; net[1] = 2;
        CHECK(!net_ports(net, battle, room, cpu, 2, port));  // two participants, hole in net slots
        net[1] = 1;
        const uint16_t rematch[4] = {83, 97, 0, 0};
        CHECK(!net_ports(net, battle, rematch, cpu, 2, port));  // old fighters with new room members
        battle[0] = 97; battle[1] = 83;
        CHECK(net_ports(net, battle, rematch, cpu, 2, port) && port[0] == 1 && port[1] == 0);
        CHECK(!net_ports(net, battle, rematch, cpu, 0, port));
        CHECK(!net_ports(net, battle, rematch, cpu, 5, port));
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
