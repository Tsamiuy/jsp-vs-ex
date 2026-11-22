// ENHANCEMENT: read-only online session observations and player-facing status.
// This is display data. Admission and Start remain owned by the peer handshake.
#ifndef JSP_ONLINE_STATUS_H
#define JSP_ONLINE_STATUS_H
#include <stdint.h>

typedef struct JspOnlineStatus {
    uint32_t version;
    uint32_t phase;
    uint32_t path;             // 0 unknown, 1 direct, 2 relay
    uint32_t content_agreed;   // only after the full peer-content/settings gate
    int32_t input_delay;       // simulation ticks, -1 unknown; never an RTT estimate
    int32_t last_rollback;     // simulation ticks, -1 unknown
    uint64_t session_id;
    char reason[192];          // local, bounded display message; never transmitted
} JspOnlineStatus;

#ifdef __cplusplus
extern "C" {
#endif
int jsp_net_status_snapshot(JspOnlineStatus* out, unsigned bytes);
#ifdef __cplusplus
}

#include <string>
#include <algorithm>
namespace jpon {
inline constexpr uint32_t kVersion = 1;
enum class Phase : uint32_t {
    Offline, NativeEnabled, Preparing, Searching, Verifying, Ready, Connecting,
    Playing, Reconnecting, Refused, Ended, Unavailable
};
enum class Tone { Quiet, Working, Good, Warning };
struct Display { std::string title, detail; Tone tone = Tone::Quiet; };

inline JspOnlineStatus empty()
{
    JspOnlineStatus s{};
    s.version = kVersion;
    s.input_delay = s.last_rollback = -1;
    return s;
}

inline bool badge_enabled(bool present, const char* value)
{
    return !present || (value && *value && value[0] != '0');
}

inline Display describe(const JspOnlineStatus& s)
{
    if (s.version != kVersion || s.phase > static_cast<uint32_t>(Phase::Unavailable))
        return { "Online status unavailable", "Connection details have not been reported.", Tone::Quiet };
    const Phase phase = static_cast<Phase>(s.phase);
    if ((phase == Phase::Ready || phase == Phase::Connecting || phase == Phase::Playing || phase == Phase::Reconnecting) && !s.content_agreed)
        return { "Content agreement unavailable", "This session has not confirmed matching content and settings.", Tone::Warning };
    switch (phase) {
    case Phase::Offline: return { "Offline", "Choose an online method and restart to connect.", Tone::Quiet };
    case Phase::NativeEnabled: return { "re-psn standard enabled", "Use Online Battle. A live connection has not been reported here.", Tone::Quiet };
    case Phase::Preparing: return { "Preparing online play", "Checking the local game, mods and startup settings.", Tone::Working };
    case Phase::Searching: return { "Finding an opponent", "Waiting for a compatible opponent.", Tone::Working };
    case Phase::Verifying: return { "Checking content and settings", "Both players must agree before the match can start.", Tone::Working };
    case Phase::Ready: return { "Ready for the match", "Content and settings match. Waiting for the game to reach the match.", Tone::Good };
    case Phase::Connecting: return { "Synchronizing the match", "Waiting for the rollback session to synchronize.", Tone::Working };
    case Phase::Playing: {
        std::string detail = s.path == 1 ? "Direct connection" : s.path == 2 ? "Relay connection" : "Connection path unknown";
        detail += s.input_delay >= 0 ? " | input delay " + std::to_string(s.input_delay) + "f" : " | input delay unknown";
        if (s.last_rollback >= 0) detail += " | last rollback " + std::to_string(s.last_rollback) + "f";
        return { "Online match", detail, Tone::Good };
    }
    case Phase::Reconnecting: return { "Reconnecting", "The simulation is waiting for the other player. Hold F9 to end the match.", Tone::Warning };
    case Phase::Refused:
    case Phase::Ended: {
        const auto end = std::find(s.reason, s.reason + sizeof s.reason, '\0');
        std::string reason(s.reason, end);
        for (char& ch : reason) if (static_cast<unsigned char>(ch) < 32 || static_cast<unsigned char>(ch) > 126) ch = '?';
        return { phase == Phase::Refused ? "Online could not start" : "Online session ended",
                 reason.empty() ? phase == Phase::Refused ? "Check the online settings and restart." : "The session is no longer connected." : reason, Tone::Warning };
    }
    default: return { "Online status unavailable", "Connection details have not been reported.", Tone::Quiet };
    }
}
} // namespace jpon
#endif
#endif
