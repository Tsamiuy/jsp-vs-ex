// ENHANCEMENT: canonical match manifest and control messages for host-controlled session settings.
// Fixed field order, little-endian, bounded (kMaxMessage), versioned, strict decode (exact lengths, every field once, in order).
// The digest covers the canonical bytes only (synchronized fields, never local ones). Transport-agnostic: bytes in, bytes out.
#ifndef JSP_SESSION_MANIFEST_H
#define JSP_SESSION_MANIFEST_H

#include "jsp_session_registry.h"
#include "jsp_session_digest.h"
#include <cstring>
#include <vector>

namespace jpsm {

inline constexpr size_t kMaxMessage = 512;
inline constexpr size_t kMaxRoster = 4;

enum class Reason : uint8_t {
    None, Malformed, SchemaMismatch, BuildMismatch, RollbackUnsupported, WrongSession, StaleRevision, NotInRoster,
    WrongSender, OutOfRange, Unsupported, CheatActive, RestartRequired, NoStartupEvidence, ApplyFailed,
    DigestMismatch, Timeout, HostCancelled, PeerLeft, RosterChanged, LocalLeave, PeerRejected, NotReady, DebugSwitch, ReaderError, LayerLost, RestoreFailed, HostLost, PeerLost,
    SessionRefused,  // the rollback session layer refused to start on one side (its own text goes to the log)
    SessionFailed,   // the rollback session failed on one side during the match (snapshot or state error; its own text goes to the log)
    ContentUnavailable, ContentMismatch, ContentChanged
};
inline constexpr uint8_t kLastReason = static_cast<uint8_t>(Reason::ContentChanged);
inline bool valid_reason(uint8_t r) { return r >= 1 && r <= kLastReason; }

inline const char* explain(Reason r)
{
    switch (r) {
    case Reason::None: return "no error";
    case Reason::Malformed: return "the message is damaged or too large";
    case Reason::SchemaMismatch: return "the other player has a different settings version (update the game)";
    case Reason::ContentUnavailable: return "could not verify the game and enabled mods. Check the mods list and restart";
    case Reason::ContentMismatch: return "game content differs. Use the same game version and enabled mods in the same order";
    case Reason::ContentChanged: return "a player could no longer apply the agreed content. Check the mods and restart";
    case Reason::BuildMismatch: return "the other player has a different game build or content";
    case Reason::RollbackUnsupported: return "rollback netcode is requested but this build cannot provide it (update the game or ask the host to turn rollback off)";
    case Reason::WrongSession: return "the message belongs to another session";
    case Reason::StaleRevision: return "an out-of-date settings revision";
    case Reason::NotInRoster: return "this player is not in the match roster";
    case Reason::WrongSender: return "the message did not come from the expected player";
    case Reason::OutOfRange: return "a setting value is outside its allowed range";
    case Reason::Unsupported: return "a setting value is not supported by this player";
    case Reason::CheatActive: return "a cheat or training option is on; online matches need it off";
    case Reason::RestartRequired: return "a startup-only option differs; restart the game with the host's value";
    case Reason::NoStartupEvidence: return "a startup-only option cannot be confirmed on this build";
    case Reason::ApplyFailed: return "the settings could not be applied and read back";
    case Reason::DigestMismatch: return "the confirmed settings differ from the proposed ones";
    case Reason::Timeout: return "a player did not answer in time";
    case Reason::HostCancelled: return "the host cancelled the match";
    case Reason::PeerLeft: return "a player left";
    case Reason::RosterChanged: return "the roster changed; settings must be agreed again";
    case Reason::LocalLeave: return "this player left";
    case Reason::PeerRejected: return "another player rejected the settings";
    case Reason::NotReady: return "a player has not announced itself yet";
    case Reason::DebugSwitch: return "a debug-only switch is set (fight seed, pad script, timing or vsync override); online matches need it unset";
    case Reason::LayerLost: return "the session settings on this player were replaced by another session";
    case Reason::RestoreFailed: return "the local settings could not be confirmed after leaving the session; restart the game";
    case Reason::HostLost: return "the host stopped responding (no keep-alive for about 10 seconds)";
    case Reason::PeerLost: return "a player stopped responding (no keep-alive for about 10 seconds) and was removed";
    case Reason::SessionFailed: return "the rollback session failed on another player (snapshot or state error, see its log); the match ended";
    case Reason::SessionRefused: return "the rollback session could not start on one player (see its log); the match was cancelled";
    case Reason::ReaderError: return "the game could not apply the settings (reader error)";
    }
    return "unknown";
}

struct Manifest {
    uint64_t session = 0;
    uint32_t revision = 0;
    uint8_t host = 0;      // slot of the room owner
    uint8_t roster = 0;    // bit per slot, includes host
    uint64_t seed = 0;
    Values values;
    Digest content{};
    bool operator==(const Manifest&) const = default;
};

inline bool manifest_shape_ok(const Manifest& m)
{
    return m.session && m.revision && m.host < kMaxRoster && m.roster && !(m.roster & ~0x0f) && (m.roster & (1u << m.host));
}

namespace wire {
inline void put(std::vector<uint8_t>& b, uint64_t v, int n) { for (int i = 0; i < n; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i))); }
struct Cur {
    const uint8_t* p; size_t n; size_t i = 0; bool bad = false;
    uint64_t get(int k) { if (i + k > n) { bad = true; return 0; } uint64_t v = 0; for (int j = 0; j < k; ++j) v |= uint64_t(p[i + j]) << (8 * j); i += k; return v; }
    bool done() const { return !bad && i == n; }
};
}

inline std::vector<uint8_t> encode_manifest(const Manifest& m)
{
    std::vector<uint8_t> b;
    b.push_back('J'); b.push_back('S'); b.push_back('M'); b.push_back('1');
    wire::put(b, kSchema, 2); wire::put(b, m.session, 8); wire::put(b, m.revision, 4);
    wire::put(b, m.host, 1); wire::put(b, m.roster, 1); wire::put(b, m.seed, 8); wire::put(b, kCount, 1);
    for (size_t i = 0; i < kCount; ++i) { wire::put(b, i, 1); wire::put(b, m.values.v[i], 4); }
    b.insert(b.end(), m.content.begin(), m.content.end());
    return b;
}
inline Digest manifest_digest(const Manifest& m) { const auto b = encode_manifest(m); return sha256(b.data(), b.size()); }

// Strict decode. Reason::None on success. An unknown or missing field, a changed order or any trailing byte is rejected.
inline Reason decode_manifest(const uint8_t* d, size_t n, Manifest* out)
{
    if (n > kMaxMessage || n < 29) return Reason::Malformed;
    if (std::memcmp(d, "JSM1", 4)) return Reason::Malformed;
    wire::Cur c{ d + 4, n - 4 };
    Manifest m;
    const uint16_t schema = static_cast<uint16_t>(c.get(2));
    if (c.bad) return Reason::Malformed;
    if (schema != kSchema) return Reason::SchemaMismatch;
    m.session = c.get(8); m.revision = static_cast<uint32_t>(c.get(4)); m.host = static_cast<uint8_t>(c.get(1));
    m.roster = static_cast<uint8_t>(c.get(1)); m.seed = c.get(8);
    const uint64_t count = c.get(1);
    if (c.bad || count != kCount) return Reason::Malformed;
    for (size_t i = 0; i < kCount; ++i) {
        const uint64_t id = c.get(1);
        const uint64_t v = c.get(4);
        if (c.bad || id != i) return Reason::Malformed;
        m.values.v[i] = static_cast<uint32_t>(v);
    }
    for (auto& byte : m.content) byte = static_cast<uint8_t>(c.get(1));
    if (!c.done() || !manifest_shape_ok(m)) return Reason::Malformed;
    if (m.content == Digest{}) return Reason::ContentUnavailable;
    *out = m;
    return Reason::None;
}

enum class Msg : uint8_t { Hello = 1, Propose = 2, Ack = 3, Reject = 4, Start = 5, Cancel = 6, KeepAlive = 7 };

struct Header { Msg type; uint64_t session; uint32_t revision; };
inline void put_header(std::vector<uint8_t>& b, Msg t, uint64_t session, uint32_t rev)
{
    b.push_back('J'); b.push_back('S'); b.push_back('H'); b.push_back('1');
    wire::put(b, static_cast<uint8_t>(t), 1); wire::put(b, kSchema, 2); wire::put(b, session, 8); wire::put(b, rev, 4);
}
inline constexpr size_t kHeaderSize = 19;
// Header only; the payload follows at d + kHeaderSize. Schema mismatch is reported so that an old peer gets a clear message.
inline Reason decode_header(const uint8_t* d, size_t n, Header* h)
{
    if (n < kHeaderSize || n > kMaxMessage || std::memcmp(d, "JSH1", 4)) return Reason::Malformed;
    wire::Cur c{ d + 4, n - 4 };
    const uint8_t t = static_cast<uint8_t>(c.get(1));
    const uint16_t schema = static_cast<uint16_t>(c.get(2));
    h->session = c.get(8); h->revision = static_cast<uint32_t>(c.get(4));
    if (t < 1 || t > 7) return Reason::Malformed;
    h->type = static_cast<Msg>(t);
    return schema == kSchema ? Reason::None : Reason::SchemaMismatch;
}

inline std::vector<uint8_t> encode_hello(uint64_t session, uint8_t slot, const Caps& c)
{
    std::vector<uint8_t> b; put_header(b, Msg::Hello, session, 0);
    wire::put(b, slot, 1); wire::put(b, c.epoch, 4); wire::put(b, c.build, 4); wire::put(b, (c.rollback ? 1 : 0) | (c.debug_active ? 2 : 0), 1);
    for (size_t i = 0; i < kCount; ++i) { wire::put(b, c.lo[i], 4); wire::put(b, c.hi[i], 4); wire::put(b, c.mask[i], 4); }
    b.insert(b.end(), c.content.begin(), c.content.end());
    return b;
}
inline Reason decode_hello(const uint8_t* d, size_t n, uint8_t* slot, Caps* c)
{
    if (n < kHeaderSize || n > kMaxMessage) return Reason::Malformed;
    wire::Cur cur{ d + kHeaderSize, n - kHeaderSize };
    *slot = static_cast<uint8_t>(cur.get(1)); c->epoch = static_cast<uint32_t>(cur.get(4)); c->build = static_cast<uint32_t>(cur.get(4));
    const uint64_t rb = cur.get(1);
    for (size_t i = 0; i < kCount; ++i) { c->lo[i] = static_cast<uint32_t>(cur.get(4)); c->hi[i] = static_cast<uint32_t>(cur.get(4)); c->mask[i] = static_cast<uint32_t>(cur.get(4)); }
    for (auto& byte : c->content) byte = static_cast<uint8_t>(cur.get(1));
    if (!cur.done() || rb > 3 || *slot >= kMaxRoster) return Reason::Malformed;
    c->rollback = rb & 1; c->debug_active = (rb & 2) != 0; c->schema = kSchema;
    return Reason::None;
}

} // namespace jpsm
#endif
