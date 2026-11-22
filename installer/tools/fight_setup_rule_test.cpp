// Host test of installer/port/jsp/jsp_fight_setup_rule.h (review of PR 248, finding 3: the battle setup table in the fight hash). g++ only.
//   bash installer/tools/fight_setup_rule_test.sh
// Two PCs' guest memory: the host played an offline Free Battle first (its picks are in the table), the joiner is fresh (all zero). Everything else equal.
#include "jsp_fight_setup_rule.h"
#include <cstdio>
#include <cstring>
#include <map>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

struct Fnv { uint64_t h = 0xCBF29CE484222325ull; void b(uint8_t v) { h ^= v; h *= 0x100000001B3ull; } };   // as jsp_fight_hash.cpp

struct Mem {   // big-endian guest words, sparse
    std::map<uint32_t, uint8_t> m;
    uint8_t rd8(uint32_t a) const { auto it = m.find(a); return it == m.end() ? 0 : it->second; }
    uint32_t rd32(uint32_t a) const { return (uint32_t)rd8(a) << 24 | (uint32_t)rd8(a + 1) << 16 | (uint32_t)rd8(a + 2) << 8 | rd8(a + 3); }
    void wr32(uint32_t a, uint32_t v) { for (int i = 0; i < 4; i++) m[a + i] = (uint8_t)(v >> (24 - 8 * i)); }
};

// the per-tick checksum's shape: some gameplay bytes (here 16 bytes at 0x00800000), then the setup table, then more gameplay bytes
static uint64_t all_of(const Mem& mem, bool native, bool keep, uint64_t* part)
{
    Fnv all, setup, game;
    for (uint32_t i = 0; i < 16; i++) { const uint8_t v = mem.rd8(0x00800000u + i); game.b(v); all.b(v); }
    jfs::hash([&](uint32_t a) { return mem.rd8(a); }, setup, all, jfs::in_checksum(native, keep));
    for (uint32_t i = 16; i < 32; i++) { const uint8_t v = mem.rd8(0x00800000u + i); game.b(v); all.b(v); }
    if (part) *part = setup.h;
    return all.h;
}

int main()
{
    Mem host, join;
    for (uint32_t i = 0; i < 32; i++) { host.m[0x00800000u + i] = (uint8_t)(i * 7 + 1); join.m[0x00800000u + i] = (uint8_t)(i * 7 + 1); }
    // the host's offline Free Battle picks: slot 0 player, character 4; slot 2 CPU, character 7, level 2; slots 1 and 3 not taking part
    const uint32_t r[4] = {jfs::kAddr, jfs::kAddr + 0x2C, jfs::kAddr + 0x58, jfs::kAddr + 0x84};
    host.wr32(r[0] + 0x08, 0); host.wr32(r[0] + 0x0C, 4);
    host.wr32(r[1] + 0x08, 2); host.wr32(r[3] + 0x08, 2);
    host.wr32(r[2] + 0x00, 2); host.wr32(r[2] + 0x0C, 7); host.wr32(r[2] + 0x18, 2);

    // native online: the parts differ (logged), the checksum does not
    { uint64_t ph = 0, pj = 0;
      CHECK(all_of(host, true, false, &ph) == all_of(join, true, false, &pj));
      CHECK(ph != pj); }
    // offline (the bench, the non-native path): the fight is built from the table, so a difference is a real difference
    CHECK(all_of(host, false, false, nullptr) != all_of(join, false, false, nullptr));
    // A/B JSP_FIGHT_HASH_SETUP=1: in every mode
    CHECK(all_of(host, true, true, nullptr) != all_of(join, true, true, nullptr));
    CHECK(jfs::in_checksum(false, false) && !jfs::in_checksum(true, false) && jfs::in_checksum(true, true) && jfs::in_checksum(false, true));
    // equal tables: equal in both modes, and the mode only changes `all`, never the part hash
    { uint64_t a = 0, b = 0;
      CHECK(all_of(join, true, false, &a) != all_of(join, false, false, &b));
      CHECK(a == b); }
    // a later change of the table (a byte in the last record) still changes the offline checksum and still not the native one
    { Mem j2 = join; j2.m[jfs::kAddr + jfs::kLen - 1] = 1;
      CHECK(all_of(j2, false, false, nullptr) != all_of(join, false, false, nullptr));
      CHECK(all_of(j2, true, false, nullptr) == all_of(join, true, false, nullptr));
      Mem j3 = join; j3.m[jfs::kAddr + jfs::kLen] = 1;   // the byte after the table is not the table's
      uint64_t p3 = 0, p0 = 0; (void)all_of(j3, false, false, &p3); (void)all_of(join, false, false, &p0); CHECK(p3 == p0); }
    // the teardown / open report counts and never writes: the host's picks are still there after it
    { const Mem before = host;
      CHECK(jfs::nonzero_words([&](uint32_t a) { return host.rd32(a); }) == 6u);
      CHECK(jfs::nonzero_words([&](uint32_t a) { return join.rd32(a); }) == 0u);
      CHECK(host.m == before.m); }
    CHECK(jfs::kAddr == 0x009540E4u && jfs::kLen == 0xB0u && jfs::kLen == 4u * 0x2Cu);
    if (g_fail) { std::printf("fight_setup_rule_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("fight_setup_rule_test: all checks passed\n");
    return 0;
}
