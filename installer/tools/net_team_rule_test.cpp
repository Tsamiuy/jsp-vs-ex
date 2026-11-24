// The rollback start's team-size rule (installer/port/jsp/jsp_net_team_rule.h) on a fake guest memory.
#include "jsp_net_team_rule.h"
#include <cstdio>
#include <map>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
using namespace jptr;
struct Mem {
    std::map<uint32_t, uint32_t> m;
    uint32_t operator()(uint32_t ea) const { auto it = m.find(ea); return it == m.end() ? 0u : it->second; }
};
static Mem world(const uint32_t (&roles)[6], uint32_t w = 0x10234000u)
{
    Mem mm; mm.m[kWorldPtr] = 0x00ABC000u; mm.m[0x00ABC000u] = w;
    for (int k = 0; k < 6; k++) mm.m[w + 0x60u + 0x94u * (uint32_t)k + 8u] = roles[k];
    return mm;
}
int main()
{
    CHECK(kWorldPtr == 0x008CBDF8u);
    { const uint32_t r[6] = { 1, 2, 1, 2, 3, 3 }; CHECK(classify(world(r)) == Teams::TwoVsTwo); }
    { const uint32_t r[6] = { 1, 2, 1, 2, 2, 2 }; CHECK(classify(world(r)) == Teams::ThreeVsThree); }   // units 4 / 5 as fighters (feat_3v3.cpp)
    { const uint32_t r[6] = { 1, 2, 1, 2, 1, 2 }; CHECK(classify(world(r)) == Teams::ThreeVsThree); }
    { const uint32_t r[6] = { 1, 2, 1, 2, 3, 2 }; CHECK(classify(world(r)) == Teams::Unknown); }        // 5 fighters: neither shape
    { const uint32_t r[6] = { 1, 2, 1, 2, 0, 3 }; CHECK(classify(world(r)) == Teams::Unknown); }        // an empty unit
    { const uint32_t r[6] = { 1, 2, 1, 2, 3, 7 }; CHECK(classify(world(r)) == Teams::Unknown); }
    { Mem none; CHECK(classify(none) == Teams::Unknown); }                                               // no battle world
    { const uint32_t r[6] = { 1, 2, 1, 2, 2, 2 }; Mem mm = world(r); mm.m[kWorldPtr] = 0x30000000u; CHECK(classify(mm) == Teams::Unknown); }
    { const uint32_t r[6] = { 1, 2, 1, 2, 2, 2 }; Mem mm = world(r); mm.m[0x00ABC000u] = 0xF0000000u; CHECK(classify(mm) == Teams::Unknown); }
    if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
    std::printf("net team rule: all checks passed\n");
    return 0;
}
