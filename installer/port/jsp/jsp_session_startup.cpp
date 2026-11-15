// ENHANCEMENT: real startup inputs for the temporary session-settings layer.
// This is point-in-time evidence, not a renderer lifetime pin or a live ACK.
#include "jsp_session_reader.h"

#if defined(_WIN32) && defined(JSP_SESSION_RENDER60_ABSENT) && JSP_SESSION_RENDER60_ABSENT == 1
extern "C" int rsx_d3d12_pipe_effective_state(void); // runtime patch7904; never initializes PIPE
#endif

namespace jpss {
ReaderStartup reader_startup()
{
    ReaderStartup observation;
#if defined(_WIN32) && defined(JSP_SESSION_RENDER60_ABSENT) && JSP_SESSION_RENDER60_ABSENT == 1
    const int state = rsx_d3d12_pipe_effective_state();
    if (state != 0 && state != 1) return observation;
    // Only these two fields are observed startup capabilities. In particular,
    // do not fill live choices from a conservative request and call them applied.
    observation.applied.choices[(size_t)Key::Pipe] = static_cast<uint8_t>(state);
    observation.applied.choices[(size_t)Key::Render60] = 0; // explicit canonical build capability below
    observation.available = true;
#endif
    return observation;
}
} // namespace jpss
