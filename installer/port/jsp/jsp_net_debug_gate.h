// jsp_net_debug_gate.h -- FIX (review of PR 248): the rollback layer's test switches apply only with JSP_ROLLBACK_ALLOW_DEBUG=1 (test tooling).
// Each of these changes what one PC simulates or sends (the audio tick lock, the sound-server lock, the guest clock, the helper thread registers, GekkoNet's
// sparse saving, the session start point, a recorded fight in place of the local pad) or impairs its link. None is in the settings handshake, so a line set on ONE PC alone (a stray play_settings.txt
// line, a variable left in a shell) would desync the match without a word. DECISION: a gate, not the handshake manifest: a manifest entry changes the
// handshake protocol for both peers (an older peer would refuse), while these are test knobs no player needs; the gate keeps them for the two-instance
// tests that already set JSP_ROLLBACK_ALLOW_DEBUG=1 on both copies. Without it a set value is ignored and the log says so (jsp_net_game.cpp gated_get).
// Pure (no Windows, no game): installer/tools/net_debug_gate_test.sh tests the rule and that jsp_net_game.cpp (kGated) and jsp_fight_hash.cpp (kGatedHash)
// read these keys only through the gate.
#ifndef JSP_NET_DEBUG_GATE_H
#define JSP_NET_DEBUG_GATE_H
#include <cstring>

namespace jpdg {

constexpr const char* kAllowKey = "JSP_ROLLBACK_ALLOW_DEBUG";
constexpr const char* kGated[] = {
    "JSP_NET_LINK",                  // TEST: link impairment of this copy's sends (delay, jitter, loss)
    "JSP_AUDIO_TICKLOCK",            // =0: the audio tick lock off (patch 3005)
    "JSP_NET_CRI_LOCK",              // =0: the sound-server lock off
    "JSP_NET_GUEST_CLOCK",           // =0: the guest clock shift off (patch 3006)
    "JSP_NET_THREAD_REGS",           // =0: the helper thread registers are not restored (patch 7923)
    "JSP_ROLLBACK_LIMITED_SAVING",   // =1: GekkoNet sparse saving
    "JSP_ROLLBACK_START",            // =fight: the session starts at the first fight HUD draw instead of the camera anchor
    "JSP_ROLLBACK_INPUTS",           // =<csv>: a recorded fight drives the local player instead of the pad
    "JSP_ROLLBACK_INPUTS_SKIP",      // =N: the recording starts N rows in
};

// FIX (review of split PR A): the per-tick fight hash's A/B switches. Each one puts a part back into `all`, the rollback checksum, that is left out because it
// differs between PCs whose game state is equal; set on one PC alone it ends the match as a desync. jsp_fight_hash.cpp reads them only through hash_ab (getenv,
// like the rest of that file), which applies this gate.
constexpr const char* kGatedHash[] = {
    "JSP_FIGHT_HASH_RNG_PTRS",       // =1: the RNG object's heap pointers in the hash
    "JSP_FIGHT_HASH_SCENE",          // =1: the sound library's playback count in the hash
    "JSP_FIGHT_HASH_CAM_CACHE",      // =1: the cameras' matrix cache in the hash
    "JSP_FIGHT_HASH_FIGHTER",        // =1: the fighters' never-written bytes in the checksum
};

inline bool is_gated(const char* key)
{
    if (!key) return false;
    for (const char* k : kGated) if (!std::strcmp(k, key)) return true;
    for (const char* k : kGatedHash) if (!std::strcmp(k, key)) return true;
    return false;
}

// The value the rollback layer honours for a gated key: `raw` with the allow switch on, else nullptr (the built-in default applies).
// *ignored = a non-empty value was dropped (the caller logs it once). An ungated key passes through unchanged.
inline const char* honoured(const char* key, const char* raw, bool allow_debug, bool* ignored)
{
    if (ignored) *ignored = false;
    if (!raw || !*raw) return nullptr;
    if (allow_debug || !is_gated(key)) return raw;
    if (ignored) *ignored = true;
    return nullptr;
}

}  // namespace jpdg
#endif
