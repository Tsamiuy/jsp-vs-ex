#include "gekko_types.h"
#include "net.h"
#include "../port/jsp/jsp_net_delay_rule.h"
#include <cstdio>

static int failures;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "RTT sample check failed at %d: %s\n", __LINE__, #x); ++failures; } } while (0)

int main()
{
    Gekko::NetStats stats;
    CHECK(stats.rtt_samples == 0);
    CHECK(stats.last_rtt_ms == 0);
    const auto before = jpdr::clock_ms();
    stats.AddRTT(100);
    const auto after = jpdr::clock_ms();
    CHECK(stats.rtt_samples == 1 && stats.LastRTT() == 100);
    CHECK(stats.last_rtt_ms >= before && stats.last_rtt_ms <= after);
    const auto received = stats.last_rtt_ms;
    for (int i = 0; i < 100; ++i) {
        stats.CalculateAvgRTT(); stats.CalculateJitter(); stats.LastRTT();
    }
    CHECK(stats.rtt_samples == 1 && stats.last_rtt_ms == received && stats.rtt.size() == 1);
    stats.AddRTT(100);
    CHECK(stats.rtt_samples == 2 && stats.LastRTT() == 100);
    stats.AddRTT(0);
    CHECK(stats.rtt_samples == 3 && stats.LastRTT() == 0);
    for (int i = 0; i < 40; ++i) stats.AddRTT(75);
    CHECK(stats.rtt_samples == 43 && stats.rtt.size() == Gekko::NetStats::RTT_HISTORY_SIZE);
    CHECK(stats.last_rtt_ms >= received);
    std::printf("RTT sample producer: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
