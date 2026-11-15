/* ENHANCEMENT: opt-in bounded socket-call discovery, never a readiness gate. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#endif
#include <atomic>
#include <cerrno>
#include <cstdio>
#include "ppu_recomp.h"
#include "win32_compat.h"
#include "sysNet_probe.h"

extern "C" int32_t cellSysutilCheckCallback(void);
extern "C" void ps3_hle_register_ctx(uint32_t, const char*, void (*)(ppu_context*));

namespace {
thread_local unsigned depth;
std::atomic_flag consumer = ATOMIC_FLAG_INIT;
bool header_written;
uint32_t last_state = ~uint32_t(0);
uint64_t last_stats_ms;
struct HostErrors {
#ifdef _WIN32
    DWORD system = GetLastError();
    int socket = WSAGetLastError();
#endif
    int crt = errno; // save OS stores before calling a CRT errno accessor
    ~HostErrors() {
        errno = crt;
#ifdef _WIN32
        WSASetLastError(socket);
        SetLastError(system);
#endif
    }
};
struct Depth { Depth() { ++depth; } ~Depth() { --depth; } };

void drain()
{
    if (consumer.test_and_set(std::memory_order_acquire)) return;
    struct Release { ~Release() { consumer.clear(std::memory_order_release); } } release;
    ps3_net_probe_record rows[32];
    ps3_net_probe_stats stats;
    const int count = ps3_net_probe_poll(rows, 32, &stats);
    bool failed = false, wrote = false;
    if (!header_written) {
        failed |= std::fprintf(stdout, "[net-probe] schema=1 clock=GetTickCount64_ms scope=socket_api_returns "
            "arm_script=%u duration_ms=%u budget=%u registration_entry_seen=%u completeness=unknown\n",
            stats.arm_index, stats.duration_ms, stats.budget, stats.online_registered) < 0;
        header_written = true;
        wrote = true;
    }
    for (int i = 0; i < count; ++i) {
        wrote = true;
        const auto& r = rows[i];
        failed |= std::fprintf(stdout, "[net-probe] epoch=%llu seq=%llu kind=%s source=%u "
            "op=%u tid=%llu lr_raw=%016llX fd_slot=%d input_port=%d requested=%u flags=%08X api_result=%08X "
            "begin_ms=%llu end_ms=%llu late=%u script=%u mask=%04X final_mask=%04X\n",
            (unsigned long long)r.epoch, (unsigned long long)r.sequence,
            r.kind == PS3_NET_PROBE_SCRIPT_REPORT ? "SCRIPT_REPORT_PRESENT" : "SOCKET_API_RETURN",
            r.source, r.operation, (unsigned long long)r.thread_id, (unsigned long long)r.lr_raw,
            r.kind == PS3_NET_PROBE_SCRIPT_REPORT ? -1 : r.socket_slot,
            r.kind == PS3_NET_PROBE_SCRIPT_REPORT ? r.socket_slot : -1,
            r.requested, (unsigned)r.flags, (unsigned)r.api_result,
            (unsigned long long)r.begin_ms, (unsigned long long)r.end_ms, r.late,
            r.script_index, r.mask, r.final_mask) < 0;
    }
    const uint64_t now = GetTickCount64();
    if (count > 0 || stats.state != last_state || now - last_stats_ms >= 1000) {
        failed |= std::fprintf(stdout, "[net-probe-stats] snapshot=live completeness=unknown state=%u "
            "now_ms=%llu attempted=%llu admitted=%llu returned=%llu in_flight=%llu filtered_wouldblock=%llu "
            "accepted=%llu copied=%llu full_drop=%llu contention_drop=%llu budget_drop=%llu late_drop=%llu "
            "queue_sampled=%u queued=%u high_water=%u incomplete=%u marker_failed=%u io_failed=%u\n",
            stats.state, (unsigned long long)now, (unsigned long long)stats.attempted,
            (unsigned long long)stats.admitted, (unsigned long long)stats.returned,
            (unsigned long long)stats.in_flight, (unsigned long long)stats.filtered_wouldblock,
            (unsigned long long)stats.accepted, (unsigned long long)stats.copied,
            (unsigned long long)stats.full_drop, (unsigned long long)stats.contention_drop,
            (unsigned long long)stats.budget_drop, (unsigned long long)stats.late_drop,
            stats.queue_sampled, stats.queued, stats.high_water, stats.incomplete,
            stats.marker_failed, stats.io_failed) < 0;
        wrote = true;
        last_state = stats.state;
        last_stats_ms = now;
    }
    if (wrote && std::fflush(stdout) != 0) failed = true;
    if (failed) ps3_net_probe_output_failed();
}
void check_callback(ppu_context* ctx)
{
    Depth call_depth;
    const int32_t result = cellSysutilCheckCallback(); // direct C call, never redispatch this NID
    HostErrors preserve_result_errors;
    ctx->gpr[3] = (uint64_t)(int64_t)result;
    if (depth == 1) drain(); // original callback/UDP progress always occurs first
}
}

extern "C" void jsp_register_net_probe(void)
{
    HostErrors preserve_startup_errors;
    if (!ps3_net_probe_init()) return;
    g_cellpad_script_report_index = ps3_net_probe_index();
    g_cellpad_script_report_observer = ps3_net_probe_script_report;
    ps3_hle_register_ctx(0x189A74DAu, "cellSysutilCheckCallback probe", check_callback);
}
