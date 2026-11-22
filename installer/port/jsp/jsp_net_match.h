// jsp_net_match.h -- ENHANCEMENT (same-link rematch): several matches over ONE rollback link (socket, re-psn room, rendezvous plan). Each match runs its own
// settings handshake (new jpsm::Host / Client objects) under its own session id, made from the link's plan session id and a match counter both sides keep:
//   match 0 = the plan's session id itself (the first match on a link is byte for byte what it was before), match k > 0 = a mix of the two (never 0, never the
//   plan's id, different for every k).
// The handshake objects drop every message whose header session differs (Host / Client on_message), so a straggler of an older match (a repeated Start, a
// keep-alive, a Cancel) never reaches the newer match. Agreement: both sides count matches the same way (jsp_net_game.cpp arms k + 1 after a match ends); when
// they differ, the newer one wins and nobody goes back: a handshake datagram of a newer match (at most kMatchAhead ahead) makes the receiving side re-arm at that
// match (match_peek), and a host that is waiting for its client's Hello repeats a header-only beacon of its match (match_beacon), so a client that is behind learns
// it (a client that is ahead is seen by its Hello and keep-alives).
// Header only, no game addresses, no clock; host-tested in installer/tools/net_m2_link_test.cpp (three matches over one link, stragglers, a side that is behind).
#pragma once
#include "jsp_session_manifest.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace jspnet {

constexpr uint32_t kMatchAhead = 16;   // a datagram of a match further ahead than this is not taken as a newer match (a stranger, or garbage)

inline uint64_t match_session_id(uint64_t base, uint32_t k)
{
    if (!k) return base;
    uint64_t x = base + 0x9e3779b97f4a7c15ull * (uint64_t)k;   // splitmix64 of the base stepped k times
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    x ^= x >> 31;
    if (!x || x == base) x = ~base ^ (uint64_t)k;               // practically never; keeps the two promises
    return x ? x : 1;
}

enum class MatchMsg { Current, Beacon, Newer, Older, Foreign };
// Classifies one received handshake datagram against the local match `cur` of the link whose plan session id is `base`:
//   Current  a message of the current match: for the handshake objects
//   Beacon   a header-only beacon of the current match: NOT for the handshake objects (they would only count it as dropped)
//   Newer    a message (or beacon) of a later match, *to = that match (at most kMatchAhead ahead): re-arm there, then hand it to the new objects
//   Older    a message of an earlier match of this link: drop it
//   Foreign  not a handshake header of this link (malformed, another schema, an unknown session): for the objects, which drop or report it as before
inline MatchMsg match_peek(uint64_t base, uint32_t cur, const uint8_t* d, size_t n, uint32_t* to)
{
    jpsm::Header h;
    if (jpsm::decode_header(d, n, &h) != jpsm::Reason::None) return MatchMsg::Foreign;
    if (h.session == match_session_id(base, cur)) return n == jpsm::kHeaderSize ? MatchMsg::Beacon : MatchMsg::Current;
    for (uint32_t k = cur + 1; k <= cur + kMatchAhead; k++) if (h.session == match_session_id(base, k)) { if (to) *to = k; return MatchMsg::Newer; }
    for (uint32_t k = 0; k < cur; k++) if (h.session == match_session_id(base, k)) return MatchMsg::Older;
    return MatchMsg::Foreign;
}
// The host's beacon of match k: a KeepAlive header with no payload (a real KeepAlive has 5 payload bytes, so no handshake object ever takes it for one).
inline std::vector<uint8_t> match_beacon(uint64_t base, uint32_t k)
{
    std::vector<uint8_t> b; jpsm::put_header(b, jpsm::Msg::KeepAlive, match_session_id(base, k), 0); return b;
}

}  // namespace jspnet
