// ENHANCEMENT: temporary host-session inputs for the real player settings readers.
// No transport, title-start gate or application receipt is supplied by this API.
#ifndef JSP_SESSION_READER_H
#define JSP_SESSION_READER_H

#include "jsp_session_settings.h"

namespace jpss {

// Trusted LOCAL evidence from actual initialized startup readers, never a wire
// field or a fresh getenv lookup. Unavailable by default; no install can assume
// PIPE/render60 initialized successfully from configuration alone.
struct ReaderStartup {
    bool available = false;
    // Only Pipe/Render60 are startup evidence. Live entries are not read-back.
    Profile applied{};
};
// Point-in-time actual renderer observation plus this target's explicit build
// capability. Unavailable on other targets or unknown/invalidated PIPE state.
// Re-read at application/ACK boundaries and bind backend/title/session lifetime;
// caching this value does not pin the renderer or create AppliedReceipt.
ReaderStartup reader_startup();
struct ReaderState {
    uint64_t generation = 0;
    uint64_t bound_session = 0;
    Token token{};
    bool active = false;
    bool operator==(const ReaderState&) const = default;
};
// Match jsp_game.cpp's actual 48-byte setting_float input, including truncation
// of ordinary custom local values. This is read input, not an applied float.
struct ReaderValue { bool present = false; char text[48]{}; };
struct ReaderSnapshot {
    ReaderState state;
    // Key::ModelDetail, Key::ObjectDistance, Key::ShadowRange, in that order.
    std::array<ReaderValue, 3> live;
};
static_assert(kRules[0].policy == Policy::SessionLive && kRules[1].policy == Policy::SessionLive
    && kRules[2].policy == Policy::SessionLive && kRules[3].policy == Policy::StartupMustMatch
    && kRules[4].policy == Policy::StartupMustMatch && kCount == 5,
    "Update coherent reader coverage when the agreement schema changes");

// Only a trusted local lifecycle may bind/end a session. A fresh session ID is
// required after leaving; remote proposals must never call begin or end.
Result reader_begin(uint64_t session);
// Caller must bind token/sender/roster and provide actual startup evidence.
// Failure preserves the previous complete layer/binding. Duplicate install is
// idempotent; a new revision invalidates older snapshots even for equal values.
Result reader_install(const Token&, const Profile&, const ReaderStartup& = {});
// Exact-token clear is safe against delayed teardown of a replaced revision.
// It also closes the local binding; delayed proposals cannot reactivate it.
Result reader_clear(const Token&);
// Trusted local teardown of this whole session, including a pending install.
Result reader_end(uint64_t session);
ReaderSnapshot reader_snapshot();
// Verify after actual reader effects; this check alone is NOT AppliedReceipt.
// Old separate per-key gets can straddle install/clear and are not an atomic
// profile read. Consumers needing agreement must use the coherent snapshot.
bool reader_current(const ReaderState&);

} // namespace jpss
#endif
