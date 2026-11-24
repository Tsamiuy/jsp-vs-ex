// jsp_thread_bit.h -- the one conversion from a guest thread id to its bit in a 64-bit thread mask (rollback snapshot census, park roles, the
// helper-thread register save / restore of jsp_net_game.cpp). Guest thread ids run from 1 through 64 (the runtime's table has 65 slots, slot 0
// unused), so thread t owns bit t - 1. `1ull << t` is wrong for every id (bit 0 never used) and undefined for t = 64. An id outside 1..64 has no
// bit: thread_bit returns 0, so a set leaves the mask unchanged and a test reads "not in the mask". Pure header, host-tested (thread_audit_test.sh).
#pragma once
#include <cstdint>

namespace jtb {
constexpr uint32_t kFirstTid = 1, kLastTid = 64;
constexpr bool valid_tid(uint32_t t) { return t >= kFirstTid && t <= kLastTid; }
constexpr uint64_t thread_bit(uint32_t t) { return valid_tid(t) ? (uint64_t)1 << (t - 1) : 0; }
constexpr bool has_thread(uint64_t mask, uint32_t t) { return (mask & thread_bit(t)) != 0; }
static_assert(thread_bit(1) == 1 && thread_bit(64) == 0x8000000000000000ull && thread_bit(0) == 0 && thread_bit(65) == 0, "thread_bit");
}  // namespace jtb
