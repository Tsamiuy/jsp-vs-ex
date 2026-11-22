// jsp_fight_setup_rule.h -- FIX (review of PR 248): the battle setup table (0x009540E4, 0xB0 bytes: Free Battle's 4 slot records of 0x2C) in the per-tick
// fight hash. Pure; used by jsp_fight_hash.cpp (hash_tick, jsp_fight_hash_reset_fight) and host-tested by installer/tools/fight_setup_rule_test.sh.
//
// Free Battle's character select writes the table; a native online fight (the game's own Online Battle under the rollback session) neither writes nor
// reads it (jsp_fight_hash.cpp, before jsp_fight_hash_native_online, names the lifted code that was checked). A PC that played an offline Free Battle
// first held those picks there and a fresh PC zeroes, so the checksum split at tick 1 (0.7.0-test4). The rule:
//   - native online fight: the table is logged (its part hash) but NOT in `all`, the rollback checksum;
//   - any other fight (offline under the bench, the non-native path): in `all`, as before (there the fight is built from it);
//   - JSP_FIGHT_HASH_SETUP=1 keeps it in `all` in every mode (A/B; only with JSP_ROLLBACK_ALLOW_DEBUG=1, test tooling).
// Nothing in the port writes the table any more: the match teardown (desync, disconnect), a lobby open and a rematch only count its nonzero words.
#pragma once
#include <cstdint>

namespace jfs {

constexpr uint32_t kAddr = 0x009540E4u, kLen = 0xB0u;

inline bool in_checksum(bool native_online, bool keep_ab) { return keep_ab || !native_online; }

// The table into its own part hash always, into the running checksum `all` only when this mode's checksum has it. rd(a) = the guest byte at a.
template <class Read8, class Part, class All>
inline void hash(Read8 rd, Part& part, All& all, bool in_all)
{
    for (uint32_t i = 0; i < kLen; i++) {
        const uint8_t v = rd(kAddr + i);
        part.b(v);
        if (in_all) all.b(v);
    }
}

// The teardown / open report: the nonzero words. It is given a reader only, so it cannot write the table.
template <class Read32>
inline uint32_t nonzero_words(Read32 rd)
{
    uint32_t n = 0;
    for (uint32_t o = 0; o < kLen; o += 4) if (rd(kAddr + o)) n++;
    return n;
}

}  // namespace jfs
