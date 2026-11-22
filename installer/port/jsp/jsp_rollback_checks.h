#pragma once

#include <cstdint>

namespace jsp_rollback {

constexpr uint64_t thread_id_bit(uint32_t id)
{
    return id >= 1 && id <= 64 ? uint64_t{1} << (id - 1) : 0;
}

// A failed worker start must still audit this tick. Retry at the next scheduled audit.
template<class Start, class Sync, class Poll>
bool periodic_audit(bool& running, Start start, Sync sync, Poll poll)
{
    if (!running) running = start();
    return running ? poll() : sync();
}

struct KoRecord {
    uint32_t tick = 0;
    int winner = 0;
};

template<unsigned N>
KoRecord first_confirmed_result(const KoRecord (&records)[N], uint32_t base, int final_frame)
{
    KoRecord first;
    if (final_frame < 0) return first;
    for (const auto& record : records) {
        if (record.winner && record.tick >= base && record.tick - base <= static_cast<uint32_t>(final_frame)
            && (!first.winner || record.tick < first.tick)) first = record;
    }
    return first;
}

enum class PeerState { Live, Disconnected };

// Only publish a sampled frame after draining its session events and finding no disconnect.
// A disconnect can settle a result from the preceding live boundary even if its save arrived later.
class ResultConfirmation {
public:
    template<unsigned N>
    KoRecord result_at_boundary(const KoRecord (&records)[N], uint32_t base, int sampled_frame, PeerState peer)
    {
        if (peer == PeerState::Live) last_live_frame_ = sampled_frame;
        return first_confirmed_result(records, base, last_live_frame_);
    }
private:
    int last_live_frame_ = -1;
};

}  // namespace jsp_rollback
