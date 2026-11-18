// ENHANCEMENT: the one effective-value access path with a temporary session layer on top.
// Order: session value (when a session is installed) > local preference (launcher value, in-game change) > registry default.
// The session layer lives in memory only. Installing, reading back and removing it never calls the persist callback, never
// touches the environment, the settings file, network identities or saves. Single-threaded: the caller serializes access
// (the game-side wrapper takes the same mutex as jsp_setting_get).
#ifndef JSP_SESSION_EFFECTIVE_H
#define JSP_SESSION_EFFECTIVE_H

#include "jsp_session_registry.h"
#include "jsp_session_digest.h"
#include <functional>

namespace jpsm {

enum class Src : uint8_t { Default, Local, Session };
struct Eff { uint32_t value = 0; Src src = Src::Default; bool controlled = false; };

enum class Res : uint8_t {
    Ok, OutOfRange, NoStartupEvidence, RestartRequired, ReadbackMismatch, NotInstalled, ReaderError
};

// The game side. Supplied by the integration; tests supply a fake reader.
class Reader {
public:
    virtual ~Reader() = default;
    // Actual startup value already applied for a Startup-boundary field. false = unknown (never assumed from configuration).
    virtual bool startup_value(Id id, uint32_t* out) = 0;
    // Called after the overlay is in place so static caches can be refreshed (Live readers re-read by themselves).
    virtual void applied(const Values&) {}
    // Value the real reader now uses for the field (read-back, not configuration). false = cannot read.
    virtual bool read_back(Id id, uint32_t* out) = 0;
    virtual void restored() {}
    // Name of the first debug-only switch set in this process (kDebugSwitches), or nullptr.
    virtual const char* debug_switch() { return nullptr; }
};

inline Digest values_digest(uint64_t session, uint32_t revision, const Values& v)
{
    uint8_t b[4 + 8 + 4 + 4 * kCount];
    size_t n = 0;
    b[n++] = 'J'; b[n++] = 'S'; b[n++] = 'A'; b[n++] = '1';
    for (int i = 0; i < 8; ++i) b[n++] = static_cast<uint8_t>(session >> (8 * i));
    for (int i = 0; i < 4; ++i) b[n++] = static_cast<uint8_t>(revision >> (8 * i));
    for (size_t k = 0; k < kCount; ++k)
        for (int i = 0; i < 4; ++i) b[n++] = static_cast<uint8_t>(v.v[k] >> (8 * i));
    return sha256(b, n);
}

class EffectiveValues {
public:
    using Persist = std::function<void(Id, uint32_t)>;
    explicit EffectiveValues(Reader* reader = nullptr) : reader_(reader) { local_ = defaults(); has_local_.fill(false); }
    void set_reader(Reader* r) { reader_ = r; }
    // Called only from set_local, i.e. for the player's own changes. Never from session operations.
    void set_persist(Persist p) { persist_ = std::move(p); }

    // Launcher-derived values and in-game changes enter here. While a session is active the value is stored for later
    // but the effective value does not change (returns false = stored, not effective).
    bool set_local(Id id, uint32_t v)
    {
        if (!in_range(id, v)) return false;
        const size_t i = static_cast<size_t>(id);
        local_.v[i] = v; has_local_[i] = true;
        if (persist_) persist_(id, v);
        return !(active_ && session_controls(id));
    }
    Eff get(Id id) const
    {
        const size_t i = static_cast<size_t>(id);
        if (active_) return { session_.v[i], Src::Session, true };
        return { local_.v[i], has_local_[i] ? Src::Local : Src::Default, false };
    }
    Values effective() const { Values x; for (size_t i = 0; i < kCount; ++i) x.v[i] = get(static_cast<Id>(i)).value; return x; }
    Values local_values() const { return local_; }

    // Pages ask this. Every synchronized field is locked while a session layer is installed.
    bool controlled_by_host(Id id) const { return active_ && session_controls(id); }
    bool controlled_by_host(const char* key) const { Id id; return find_id(key, &id) && controlled_by_host(id); }
    static const char* controlled_label() { return "Controlled by host"; }
    bool active() const { return active_; }
    uint32_t generation() const { return gen_; }

    // Install all-or-nothing. Startup-boundary fields need evidence equal to the required value. On any failure the previous
    // layer is left as it was, except a failed read-back or a throwing reader, which remove the layer. Then asks the reader to
    // read every value back; a mismatch fails and names the first differing field. `owner` tags the layer so that only the object
    // that installed it clears it (clear_owned); a new install takes the layer over.
    Res install(uint64_t session, uint32_t revision, const Values& required, Digest* applied_digest, const void* owner = nullptr)
    {
        fail_id_ = Id::Rollback;
        Id bad;
        if (!values_valid(required, &bad)) { fail_id_ = bad; return Res::OutOfRange; }
        try {
            for (size_t i = 0; i < kCount; ++i) {
                const Id id = static_cast<Id>(i);
                if (rule(id).boundary != Boundary::Startup) continue;
                uint32_t have = 0;
                fail_id_ = id;
                if (!reader_ || !reader_->startup_value(id, &have)) return Res::NoStartupEvidence;
                if (have != required.v[i]) return Res::RestartRequired;
            }
            fail_id_ = Id::Rollback;
            session_ = required; active_ = true; owner_ = owner; ++gen_;
            if (reader_) reader_->applied(required);
            Values back{};
            for (size_t i = 0; i < kCount; ++i) {
                fail_id_ = static_cast<Id>(i);
                if (!reader_ || !reader_->read_back(static_cast<Id>(i), &back.v[i]) || back.v[i] != required.v[i]) {
                    // fail closed: the layer is removed entirely, not rolled back to a previous revision the host has superseded
                    clear();
                    return Res::ReadbackMismatch;
                }
            }
            if (applied_digest) *applied_digest = values_digest(session, revision, back);
        } catch (...) {
            clear();
            return Res::ReaderError;
        }
        fail_id_ = Id::Rollback;
        session_id_ = session; revision_ = revision;
        return Res::Ok;
    }
    // Remove the layer unconditionally. Idempotent. The reader is told (restored()); then the live and match-start fields are read back
    // and compared with the local values: false = the real reader does not show the local values again (the layer is gone either way).
    // Startup-boundary fields are never changed by a session and are not compared. A throwing reader cannot keep the layer active.
    bool clear()
    {
        const bool was = active_;
        active_ = false; session_ = {}; session_id_ = 0; revision_ = 0; owner_ = nullptr;
        if (!was) return true;
        ++gen_;
        restore_ok_ = true;
        try {
            if (reader_) {
                reader_->restored();
                for (size_t i = 0; i < kCount; ++i) {
                    if (rule(static_cast<Id>(i)).boundary == Boundary::Startup) continue;
                    uint32_t v = 0;
                    if (!reader_->read_back(static_cast<Id>(i), &v) || v != local_.v[i]) restore_ok_ = false;
                }
            }
        } catch (...) { restore_ok_ = false; }
        return restore_ok_;
    }
    // Clears only a layer this owner installed; a newer owner's layer survives a delayed teardown of an older object.
    bool clear_owned(const void* owner) { if (!active_ || owner_ != owner) return true; return clear(); }
    bool owned_by(const void* owner) const { return active_ && owner_ == owner; }
    bool restore_ok() const { return restore_ok_; }
    const char* debug_switch() const { try { return reader_ ? reader_->debug_switch() : nullptr; } catch (...) { return nullptr; } }
    Id fail_id() const { return fail_id_; }   // the field that made the last install fail
    uint64_t session_id() const { return session_id_; }
    uint32_t revision() const { return revision_; }
private:
    static bool session_controls(Id) { return true; }
    Reader* reader_;
    Id fail_id_ = Id::Rollback;
    const void* owner_ = nullptr;
    bool restore_ok_ = true;
    Persist persist_;
    Values local_{}, session_{};
    std::array<bool, kCount> has_local_{};
    bool active_ = false;
    uint32_t gen_ = 0, revision_ = 0;
    uint64_t session_id_ = 0;
};

} // namespace jpsm
#endif
