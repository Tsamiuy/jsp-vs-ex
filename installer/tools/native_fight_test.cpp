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
