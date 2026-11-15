// Pure settings agreement contract. ENHANCEMENT: foundation only, not connected to a game reader, lobby or transport.
// Configured/effective lookup is not proof of application. Only a later game integration can supply AppliedReceipt
// from actual reader read-back; settings_agreed() does not establish scene readiness or authorize a match start.
#ifndef JSP_SESSION_SETTINGS_H
#define JSP_SESSION_SETTINGS_H

#include "jsp_settings.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace jpss {

enum class Key : uint8_t { ModelDetail, ObjectDistance, ShadowRange, Pipe, Render60, Count };
enum class Policy { SessionLive, StartupMustMatch };
enum class Source { ReaderDefault, BaseLookup, SessionOverride };
enum class Result {
    Ok, UnknownKey, DuplicateKey, MissingKey, TooManyEntries, InvalidValue, UnsupportedProfile,
    InvalidToken, Incompatible, RestartRequired, Stale, NotStaged, WrongPeer, AppliedMismatch
};
inline constexpr size_t kCount = static_cast<size_t>(Key::Count);
inline constexpr uint16_t kSchema = 1;
inline constexpr uint8_t kPeerMask = 0x0f;

struct Rule {
    const char* name;
    Policy policy;
    const char* const* values;
    uint8_t count, reader_default;
};
inline constexpr const char* const kDetailValues[] = { "1", "2", "4" };
inline constexpr const char* const kDistanceValues[] = { "0.75", "1", "2", "0" };
inline constexpr const char* const kRangeValues[] = { "1", "2" };
inline constexpr const char* const kPipeValues[] = { "0", "1" };
inline constexpr const char* const kRenderValues[] = { "0", "probe", "a", "b" };
// Choice indices are the canonical typed representation for schema 1, never native struct bytes.
// Keep these schema-owned arrays independent of the UI header's internal-linkage arrays; tests detect UI drift.
inline constexpr std::array<Rule, kCount> kRules{{
    { "MODEL_DETAIL", Policy::SessionLive, kDetailValues, 3, 0 },
    { "OBJECT_DISTANCE", Policy::SessionLive, kDistanceValues, 4, 1 },
    { "SHADOW_RANGE", Policy::SessionLive, kRangeValues, 2, 0 },
    { "PIPE", Policy::StartupMustMatch, kPipeValues, 2, 0 },
    { "JSP_RENDER60", Policy::StartupMustMatch, kRenderValues, 4, 0 },
}};

inline const Rule* rule(Key key)
{
    const size_t i = static_cast<size_t>(key);
    return i < kCount ? &kRules[i] : nullptr;
}
inline bool find_key(const std::string& name, Key* out)
{
    for (size_t i = 0; i < kCount; ++i)
        if (name == kRules[i].name) { *out = static_cast<Key>(i); return true; }
    return false;
}

struct Profile {
    std::array<uint8_t, kCount> choices{};
    bool operator==(const Profile&) const = default;
};
inline Profile conservative() { return Profile{{0, 1, 0, 0, 0}}; }
inline bool valid(const Profile& p)
{
    for (size_t i = 0; i < kCount; ++i) if (p.choices[i] >= kRules[i].count) return false;
    return true;
}
inline Result required_profile(const Profile& p)
{
    if (!valid(p)) return Result::InvalidValue;
    // The first profile is intentionally fixed: normal LOD/culling/shadow range, PIPE/render60 off.
    return p == conservative() ? Result::Ok : Result::UnsupportedProfile;
}

struct Entry { std::string name, value; };
inline Result choice(const Rule& r, const std::string& text, uint8_t* out)
{
    if (text.empty() || text.size() > 64 || text.find('\0') != std::string::npos) return Result::InvalidValue;
    if (r.policy == Policy::StartupMustMatch) {
        // These readers use literal/first-character rules, not numeric conversion. Never turn an underflowed
        // numeric alias such as PIPE=1e-9999 into "off" while the actual reader enables it.
        for (uint8_t i = 0; i < r.count; ++i)
            if (text == r.values[i]) { *out = i; return Result::Ok; }
        return Result::InvalidValue;
    }
    const jps::Item it{r.name, jps::G_GRAPHICS, "", r.values, r.values, r.count, r.reader_default,
                       r.reader_default, jps::LIVE, "contract input, not an applied-reader report"};
    const int i = jps::item_index(it, text);
    if (i < 0) return Result::InvalidValue;
    *out = static_cast<uint8_t>(i);
    return Result::Ok;
}

// All required keys exactly once. A failed parse leaves the caller's previous profile untouched.
inline Result parse_required(const std::vector<Entry>& entries, Profile* out)
{
    if (entries.size() > kCount) return Result::TooManyEntries;
    Profile p;
    std::array<bool, kCount> seen{};
    for (const Entry& e : entries) {
        Key key;
        if (!find_key(e.name, &key)) return Result::UnknownKey;
        const size_t i = static_cast<size_t>(key);
        if (seen[i]) return Result::DuplicateKey;
        const Result r = choice(kRules[i], e.value, &p.choices[i]);
        if (r != Result::Ok) return r;
        seen[i] = true;
    }
    for (bool s : seen) if (!s) return Result::MissingKey;
    const Result r = required_profile(p);
    if (r == Result::Ok) *out = p;
    return r;
}

struct Observation { uint8_t choice = 0; Source source = Source::ReaderDefault; };
struct Captured {
    Profile profile;
    std::array<Source, kCount> sources{};
};
// Lookup returns nullptr for absence, otherwise the current value at the caller's chosen source.
// This resolves configuration; it does not manufacture an application receipt or assume getenv is a cached reader.
template<class Lookup> Result capture(Lookup lookup, Captured* out)
{
    Captured c;
    for (size_t i = 0; i < kCount; ++i) {
        const char* text = lookup(kRules[i].name);
        c.sources[i] = text ? Source::BaseLookup : Source::ReaderDefault;
        if (!text) c.profile.choices[i] = kRules[i].reader_default;
        else {
            const Result r = choice(kRules[i], text, &c.profile.choices[i]);
            if (r != Result::Ok) return r;
        }
    }
    *out = c;
    return Result::Ok;
}

// No environment, file or caller-owned preference mutations. The game-facing integration is still absent.
class SessionLayer {
public:
    Result install(const Profile& required, const Profile& applied_base)
    {
        const Result r = required_profile(required);
        if (r != Result::Ok) return r;
        if (!valid(applied_base)) return Result::InvalidValue;
        for (size_t i = 0; i < kCount; ++i)
            if (kRules[i].policy == Policy::StartupMustMatch && required.choices[i] != applied_base.choices[i])
                return Result::RestartRequired;
        required_ = required;
        active_ = true;
        return Result::Ok;
    }
    void clear() { active_ = false; }
    bool active() const { return active_; }
    bool locked(Key key) const { return active_ && rule(key); }
    Result lookup(Key key, const Profile& base, Observation* out) const
    {
        const Rule* r = rule(key);
        if (!r) return Result::UnknownKey;
        if (!valid(base)) return Result::InvalidValue;
        const size_t i = static_cast<size_t>(key);
        *out = active_ && r->policy == Policy::SessionLive
            ? Observation{required_.choices[i], Source::SessionOverride}
            : Observation{base.choices[i], Source::BaseLookup};
        return Result::Ok;
    }
private:
    Profile required_{};
    bool active_ = false;
};

struct Token {
    uint64_t session = 0;
    uint32_t revision = 0;
    uint8_t owner = 0, roster = 0;
    bool operator==(const Token&) const = default;
};
// Roster bits identify slots, not their occupants. The integration must advance revision/session when an occupant
// is replaced, even when the bitmask stays the same, and must use a fresh session identity after leaving.
inline bool valid(const Token& t)
{
    return t.session && t.revision && t.owner < 4 && t.roster && !(t.roster & ~kPeerMask)
        && (t.roster & (1u << t.owner));
}

struct Capabilities {
    uint16_t schema = kSchema;
    std::array<uint8_t, kCount> choices{{0x07, 0x0f, 0x03, 0x03, 0x0f}};
};
inline bool supports(const Capabilities& c, const Profile& p)
{
    if (c.schema != kSchema || !valid(p)) return false;
    for (size_t i = 0; i < kCount; ++i) if (!(c.choices[i] & (1u << p.choices[i]))) return false;
    return true;
}

// Supplied explicitly by the later integration after actual application/read-back. The pure contract cannot
// validate hardware or reader execution. Full canonical values are compared; no hash collision or padded bytes.
// The caller must bind receipt.peer to the actual message sender; this data structure is not sender verification.
struct AppliedReceipt { Token token; uint8_t peer = 0; Profile applied; };

class SettingsAgreement {
public:
    // A local join/create operation binds the session epoch; incoming proposals cannot choose it.
    Result begin_session(uint64_t session)
    {
        if (!session) return Result::InvalidToken;
        if (local_session_ && local_session_ != session) return Result::Stale;
        local_session_ = session;
        return Result::Ok;
    }
    Result stage(const Token& token, const Profile& required, const Capabilities& local)
    {
        if (!valid(token)) return Result::InvalidToken;
        if (!local_session_) return Result::NotStaged;
        if (token.session != local_session_) return Result::Stale;
        if (staged_) {
            if (token.session != token_.session || token.revision < token_.revision) return Result::Stale;
            if (token.revision == token_.revision && (token != token_ || required != required_)) return Result::Stale;
        }
        const bool duplicate = staged_ && token == token_;
        token_ = token;
        required_ = required;
        staged_ = true;
        if (!duplicate) receipts_ = 0;
        accepted_ = false;
        const Result r = required_profile(required);
        if (r != Result::Ok) { receipts_ = 0; return r; }
        if (!supports(local, required)) { receipts_ = 0; return Result::Incompatible; }
        accepted_ = true;
        return Result::Ok;
    }
    Result receive(const AppliedReceipt& receipt, const Capabilities& peer)
    {
        if (!staged_ || !accepted_) return Result::NotStaged;
        if (receipt.token != token_) return Result::Stale;
        if (receipt.peer >= 4 || !(token_.roster & (1u << receipt.peer))) return Result::WrongPeer;
        if (!supports(peer, required_)) {
            receipts_ &= static_cast<uint8_t>(~(1u << receipt.peer));
            return Result::Incompatible;
        }
        if (!valid(receipt.applied) || receipt.applied != required_) {
            receipts_ &= static_cast<uint8_t>(~(1u << receipt.peer));
            return Result::AppliedMismatch;
        }
        receipts_ |= static_cast<uint8_t>(1u << receipt.peer);
        return Result::Ok;
    }
    bool settings_agreed() const { return staged_ && accepted_ && receipts_ == token_.roster; }
    uint8_t receipts() const { return receipts_; }
    void reset() { local_session_ = 0; staged_ = false; accepted_ = false; receipts_ = 0; token_ = {}; required_ = {}; }
private:
    uint64_t local_session_ = 0;
    Token token_{};
    Profile required_{};
    uint8_t receipts_ = 0;
    bool staged_ = false, accepted_ = false;
};

} // namespace jpss
#endif
