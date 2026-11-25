// ENHANCEMENT: settings registry for host-controlled temporary session settings.
// Classifies every setting as synchronized (must match on all peers) or local-only, and defines the canonical typed
// value of each synchronized one. Header only, no OS calls, no game code: installer/tools/session_manifest_test.cpp runs it on the host.
//
// Values are canonical unsigned 32-bit numbers, never native struct bytes or float text: an enum is its choice index
// in the table the launcher/game readers use, a number is itself, a flag is 0 or 1. The wire order of the synchronized fields is
// the Id order below and never changes within a schema version; a new field needs a new schema.
//
// ASSUMPTION: the ranges marked (A) below are bounds chosen for this contract, not values read from the game.
#ifndef JSP_SESSION_REGISTRY_H
#define JSP_SESSION_REGISTRY_H

#include "jsp_session_settings.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace jpsm {

inline constexpr uint16_t kSchema = 2;   // 2: GraceSeconds appended

// Wire order. Count is the number of synchronized fields every manifest carries exactly once.
enum class Id : uint8_t {
    Rollback,       // rollback netcode on/off (the opt-in; also for room-server play)
    SimRateHz,      // simulation rate, 30 only in schema 1
    InputDelay,     // rollback input delay in ticks
    KoTarget,       // JSP_KO_TARGET, 0 = the game's own
    TimerSeconds,   // match timer, 0 = the game's own
    StageId,        // stage choice id
    TrainInfStamina,  // training/gameplay cheat, forced off online
    TrainNormalGain,  // training/gameplay cheat, forced off online
    UnlockAll,      // JSP_UNLOCK_ALL save cheat, forced off online
    ModelDetail,    // jpss::Key::ModelDetail   (choice index)
    ObjectDistance, // jpss::Key::ObjectDistance
    ShadowRange,    // jpss::Key::ShadowRange
    Pipe,           // jpss::Key::Pipe          (startup)
    Render60,       // jpss::Key::Render60      (startup)
    GraceSeconds,   // reconnect grace window and disconnect timeout of the rollback session, seconds (both peers agree by construction)
    Count
};
inline constexpr size_t kCount = static_cast<size_t>(Id::Count);

// What a setting means for a networked match.
enum class NetClass : uint8_t {
    MatchRule,      // host supplies it, every peer applies the same value
    SharedProfile,  // unproven timing/graphics option held to a conservative shared value
    CheatOff,       // must be 0 in an ordinary online match
    LocalOnly       // never synchronized (video, audio, pads, window, machine)
};
// When the value can take effect.
enum class Boundary : uint8_t {
    Live,           // readers ask the effective-value path on every use
    MatchStart,     // read when the match is created; the session value must be installed before then
    Startup         // static-cached at process start: the session value must equal what is already applied, else restart
};
enum class Type : uint8_t { Flag, Enum, UInt };

struct Rule {
    const char* name;      // the launcher/environment key (or the port name for new fields)
    NetClass cls;
    Type type;
    Boundary boundary;
    uint32_t lo, hi;       // inclusive range of valid canonical values
    uint32_t def;          // canonical value when absent
    uint32_t supported;    // default capability: bit i set = canonical value i supported (values < 32), else all of lo..hi
    const char* restore;   // what restoring means
};
inline constexpr uint32_t kAnyValue = 0xffffffffu;
inline constexpr const char* kRestoreBase = "effective value falls back to the local preference or launcher value";

inline constexpr std::array<Rule, kCount> kRules{{
    { "JSP_ROLLBACK",       NetClass::MatchRule,     Type::Flag, Boundary::MatchStart, 0, 1, 0, kAnyValue, kRestoreBase },
    { "JSP_SIM_RATE_HZ",    NetClass::MatchRule,     Type::UInt, Boundary::MatchStart, 30, 30, 30, kAnyValue, kRestoreBase },   // 30 Hz only
    { "JSP_INPUT_DELAY",    NetClass::MatchRule,     Type::UInt, Boundary::MatchStart, 0, 8, 2, kAnyValue, kRestoreBase },      // (A) range and default
    // The KO target and the training/cheat fields are read ONCE at registration by their reader (the hook is installed only when
    // a value is set), so their effective boundary is Startup: the session value must equal the value the reader already holds.
    // Making them live (hook always installed, value read from the effective path) is a later reader change.
    { "JSP_KO_TARGET",      NetClass::MatchRule,     Type::UInt, Boundary::Startup,    0, 5, 0, kAnyValue, kRestoreBase },
    { "JSP_TIMER_SECONDS",  NetClass::MatchRule,     Type::UInt, Boundary::MatchStart, 0, 999, 0, kAnyValue, kRestoreBase },    // (A)
    { "JSP_STAGE_ID",       NetClass::MatchRule,     Type::UInt, Boundary::MatchStart, 0, 255, 0, kAnyValue, kRestoreBase },    // (A)
    { "JSP_TRAIN_INF_STAMINA", NetClass::CheatOff,   Type::Flag, Boundary::Startup,    0, 0, 0, kAnyValue, kRestoreBase },
    { "JSP_TRAIN_NORMAL_GAIN", NetClass::CheatOff,   Type::Flag, Boundary::Startup,    0, 0, 0, kAnyValue, kRestoreBase },
    { "JSP_UNLOCK_ALL",     NetClass::CheatOff,      Type::Flag, Boundary::Startup,    0, 0, 0, kAnyValue, kRestoreBase },
    { "MODEL_DETAIL",       NetClass::SharedProfile, Type::Enum, Boundary::Live,    0, 2, 0, 0x1, kRestoreBase },
    { "OBJECT_DISTANCE",    NetClass::SharedProfile, Type::Enum, Boundary::Live,    0, 3, 1, 0x2, kRestoreBase },
    { "SHADOW_RANGE",       NetClass::SharedProfile, Type::Enum, Boundary::Live,    0, 1, 0, 0x1, kRestoreBase },
    { "PIPE",               NetClass::SharedProfile, Type::Enum, Boundary::Startup, 0, 1, 1, 0x2, kRestoreBase },
    { "JSP_RENDER60",       NetClass::SharedProfile, Type::Enum, Boundary::Startup, 0, 3, 0, 0x1, kRestoreBase },
    { "JSP_NET_GRACE_S",    NetClass::MatchRule,     Type::UInt, Boundary::MatchStart, 1, 30, 5, kAnyValue, kRestoreBase },    // (A) range; default 5 s = GekkoNet's own
}};

inline const Rule& rule(Id id) { return kRules[static_cast<size_t>(id)]; }
inline bool find_id(const char* name, Id* out)
{
    for (size_t i = 0; i < kCount; ++i)
        if (!std::strcmp(kRules[i].name, name)) { *out = static_cast<Id>(i); return true; }
    return false;
}

// The synchronized graphics-profile fields use the same keys and choice tables as jpss (jsp_session_settings.h).
inline bool shares_jpss(Id id, jpss::Key* k)
{
    if (id < Id::ModelDetail || id > Id::Render60) return false;
    *k = static_cast<jpss::Key>(static_cast<size_t>(id) - static_cast<size_t>(Id::ModelDetail));
    return true;
}

struct LocalSetting { const char* name; const char* why; };
// Everything the launcher, the in-game pages and the controls offer that is not synchronized. The test requires that every key of
// jsp_settings.h (items, launcher-only, actions) is either a registry field or listed here.
inline constexpr LocalSetting kLocalOnly[] = {
    { "RES_SCALE", "video" }, { "FULLSCREEN", "window" }, { "WINDOW_SIZE", "window" }, { "TITLE_STATS", "window" },
    { "SHADOW_SCALE", "video: presentation only" }, { "UI_SCALE", "video: HUD size" }, { "AA_MODE", "video" },
    { "ANISO", "video: presentation only" }, { "SHARPEN", "video: presentation only" },
    { "FX_FULL", "video" }, { "FPS_COUNTER", "overlay" }, { "INPUT_MODE", "pad/device" }, { "MOUSE_SENS", "pad/device" },
    { "BOOST_OFF_ON_EXIT", "machine power plan" }, { "CPU_BOOST", "machine power plan" },
    { "(disc, EBOOT)", "install" }, { "(Online tab)", "network identity: never copied from the host" },
    { "(key and pad bindings)", "pad/device: tick inputs are synchronized, bindings are not" },
    { "(audio)", "audio device and volume" },
};
inline bool is_local_only(const char* name)
{
    for (const LocalSetting& l : kLocalOnly) if (!std::strcmp(l.name, name)) return true;
    return false;
}
// Debug and measurement switches outside jsp_settings.h that change simulation timing, input, randomness or scene flow. They are
// debug-only: a session refuses to start while any is in effect (Reason::DebugSwitch). A switch counts only when its value differs from
// the reader's default behaviour, checked against each reader:
//   NonEmpty         any non-empty value changes behaviour (a script path, a number, a seed)
//   NonZero          set and not starting with '0' (the reader treats "0" as off)
//   StartsWithZero   the default is on; only a value starting with '0' changes behaviour (LATE_FLIP, VBLANK_QPC, TRAINING_MODE, ...)
//   StartsWithOne    the reader reacts only to a value starting with '1' (USLEEP_SPIN, PAD_P2_ALWAYS)
//   NotEqual         a non-empty value other than `arg`, the value the port sets itself when unset (CRI_LEAD, default 12)
// Method (limits): the getenv names in the port's sources (jsp_hle.cpp, main.cpp, jsp_game.cpp, jsp_autopilot.cpp, jsp_fight_hash.cpp and
// the development builds' probe and measurement readers) and the getenv lines added by the toolkit patches were read, and listed those that feed pad input or scripted menus/fights, the simulation seed or anchor, SPU/SPURS timing or
// loss, sleep/flip/vblank/vsync pacing, probes that perturb timing, or training mode. Video, shader, texture, log and snapshot/diagnostic
// names are not listed (the snapshot switches are the rollback machinery's own). Names read only inside the third-party
// runtime sources outside port/ were not searched, so THE LIST MAY STILL MISS SOME. The flag a peer sends is self-reported: it protects
// honest peers from a mistake, not from a modified client.
enum class Trigger : uint8_t { NonEmpty, NonZero, StartsWithZero, StartsWithOne, NotEqual };
struct DebugSwitch { const char* name; Trigger trigger; const char* arg = nullptr; };
inline constexpr DebugSwitch kDebugSwitches[] = {
    { "JSP_FIGHT_SEED", Trigger::NonEmpty }, { "JSP_FIGHT_HASH", Trigger::NonZero }, { "JSP_FIGHT_PROBE", Trigger::NonEmpty },
    { "JSP_PAD_TICKS", Trigger::NonEmpty }, { "JSP_TICK_ANCHOR", Trigger::NonEmpty }, { "JSP_ANCHOR_PRESSES", Trigger::NonEmpty },
    { "JSP_PROBE_ROOT_NUDGE", Trigger::NonZero }, { "JSP_PROBE_SPURS_PHASE", Trigger::NonZero },
    { "PAD_SCRIPT", Trigger::NonEmpty }, { "PAD_SCRIPT2", Trigger::NonEmpty }, { "PAD_CLOCK", Trigger::NonEmpty },
    { "PAD_P2_ALWAYS", Trigger::StartsWithOne },
    { "AUTO_FIGHT", Trigger::NonEmpty }, { "AUTO_MENU", Trigger::NonEmpty }, { "AUTO_FB", Trigger::NonEmpty },
    { "AUTO_FB_P1", Trigger::NonEmpty }, { "AUTO_2P", Trigger::NonEmpty },
    { "TRAINING", Trigger::NonZero }, { "TRAINING_MODE", Trigger::StartsWithZero }, { "TRAINING_TEST", Trigger::NonEmpty },
    { "TRAINING_TEST_LOW", Trigger::NonEmpty }, { "TRAINING_NODUMMY", Trigger::NonEmpty }, { "TRAINING_NOLOCK", Trigger::NonEmpty },
    { "TRAINING_DUMMY_TEST", Trigger::NonEmpty },
    { "TRAIN_CPU", Trigger::NonZero }, { "TRAIN_NOCOOL", Trigger::NonZero }, { "TRAIN_BURST", Trigger::NonZero },   // feat_training_opts.cpp: a Training state set from the start (the file itself does nothing in a session: jsp_tr_session)
    { "USLEEP_AFTER_US", Trigger::NonEmpty }, { "USLEEP_GAP_US", Trigger::NonEmpty }, { "USLEEP_REAL_US", Trigger::NonEmpty },
    { "USLEEP_SPIN", Trigger::StartsWithOne },
    { "LATE_FLIP", Trigger::StartsWithZero }, { "LATE_VBLANK_MS", Trigger::NonEmpty }, { "VBLANK_QPC", Trigger::StartsWithZero },
    { "D3D12_VSYNC", Trigger::NonEmpty }, { "CRI_LEAD", Trigger::NotEqual, "12" },
    { "JSP_SPU_HASH", Trigger::NonZero }, { "JSP_SPU_DROP", Trigger::NonEmpty }, { "JSP_SPU_DELAY", Trigger::NonEmpty },
    { "JSP_SPU_JITTER_US", Trigger::NonEmpty }, { "JSP_SPU_JITTER_IMGS", Trigger::NonEmpty }, { "JSP_SPU_JITTER_SEED", Trigger::NonEmpty },
    { "SPURS_JC_DELAY", Trigger::NonEmpty }, { "SPURS_GUARD_EVENT", Trigger::StartsWithZero }, { "SPURS_CTX_PAD", Trigger::StartsWithZero },
    { "JSP_RENDER60_PADROW", Trigger::NonZero },
};
inline bool trigger_hit(Trigger t, const char* v, const char* arg = nullptr)
{
    if (!v || !*v) return false;
    switch (t) {
    case Trigger::NonEmpty: return true;
    case Trigger::NonZero: return v[0] != '0';
    case Trigger::StartsWithZero: return v[0] == '0';
    case Trigger::StartsWithOne: return v[0] == '1';
    case Trigger::NotEqual: return !arg || std::strcmp(v, arg) != 0;
    }
    return false;
}
// Lookup returns nullptr (or an empty string) for an unset name. Returns the first switch in effect, else nullptr.
template<class Lookup> const char* first_debug_switch(Lookup lookup)
{
    for (const DebugSwitch& d : kDebugSwitches) if (trigger_hit(d.trigger, lookup(d.name), d.arg)) return d.name;
    return nullptr;
}

// A key is synchronized when it is a registry field (matching the key name, e.g. "PIPE", "JSP_UNLOCK_ALL").
inline bool is_synchronized(const char* name) { Id id; return find_id(name, &id); }

// Values: canonical, one per synchronized field.
struct Values {
    std::array<uint32_t, kCount> v{};
    bool operator==(const Values&) const = default;
};
inline Values defaults()
{
    Values d;
    for (size_t i = 0; i < kCount; ++i) d.v[i] = kRules[i].def;
    return d;
}
inline bool in_range(Id id, uint32_t v) { const Rule& r = rule(id); return v >= r.lo && v <= r.hi; }
inline bool values_valid(const Values& x, Id* bad = nullptr)
{
    for (size_t i = 0; i < kCount; ++i)
        if (!in_range(static_cast<Id>(i), x.v[i])) { if (bad) *bad = static_cast<Id>(i); return false; }
    return true;
}
// The shared profile in force in schema 1: conservative, rollback off until the host asks for it.
inline Values conservative_profile()
{
    Values d = defaults();
    return d;
}

// What a host proposes. Caps accepts only the shared profile for the Live SharedProfile fields (anything else is Unsupported), so proposing
// the host's own Model detail, Object draw distance or Shadow range would refuse every match it hosts. The SharedProfile fields exist
// to hold every peer to one conservative value, so the host proposes jpss::conservative() for each Live one and
// keeps its own value for everything else (MatchRule fields are the host's to choose). Its own preference is untouched: it comes back when
// the session layer is removed (kRestoreBase). The Startup SharedProfile fields (PIPE, JSP_RENDER60) are set from the value the renderer
// actually started with by the overload in jsp_session_effective.h.
inline Values host_proposal(const Values& local)
{
    Values v = local;
    const jpss::Profile c = jpss::conservative();
    for (size_t i = 0; i < kCount; ++i) {
        const Id id = static_cast<Id>(i);
        jpss::Key k;
        if (rule(id).cls == NetClass::SharedProfile && rule(id).boundary == Boundary::Live && shares_jpss(id, &k)) v.v[i] = c.choices[static_cast<size_t>(k)];
    }
    return v;
}

// Canonical text of a value (what an environment or file reader would see), for the readers that still take text.
inline std::string value_text(Id id, uint32_t v)
{
    jpss::Key k;
    if (shares_jpss(id, &k)) {
        const jpss::Rule* r = jpss::rule(k);
        return v < r->count ? r->values[v] : "";
    }
    return std::to_string(v);
}

// What one peer can honour. Built from the registry defaults; a newer build widens it, an older build lacks the rollback bit.
struct Caps {
    uint16_t schema = kSchema;
    uint32_t build = 0;            // simulation build/content fingerprint: must be equal on all peers
    bool rollback = true;          // this build contains the rollback netcode
    uint32_t epoch = 0;            // per-join counter chosen by the client; binds repeated/late messages to one join
    bool debug_active = false;     // a debug-only switch is set on this peer (see kDebugSwitches); sessions refuse it
    std::array<uint32_t, kCount> lo{}, hi{}, mask{};
    Caps()
    {
        for (size_t i = 0; i < kCount; ++i) {
            lo[i] = kRules[i].lo; hi[i] = kRules[i].hi; mask[i] = kRules[i].supported;
        }
    }
    bool supports(Id id, uint32_t v) const
    {
        const size_t i = static_cast<size_t>(id);
        if (v < lo[i] || v > hi[i]) return false;
        return mask[i] == kAnyValue || (v < 32 && (mask[i] & (1u << v)));
    }
};

} // namespace jpsm
#endif
