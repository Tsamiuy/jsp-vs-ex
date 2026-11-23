// jsp_audit_start.h -- when a scheduled thread-registry audit runs in the background (JSP_SNAPSHOT_AUDIT_ASYNC=1, jsp_snapshot.cpp registry_audit_tick):
// the start of the background audit worker, and what the game thread does at a scheduled audit while that worker is not running.
// FIX (review: audit skipped after a failed start): the worker was started once; when CreateThread failed, that one audit ran on the game thread and
// every later scheduled audit was skipped (the "started" flag was set, no worker existed, nothing listed the threads again). Now every scheduled audit
// is covered: while no worker runs, the game thread tries to start it again (at each scheduled audit, so "later" = the next audit tick), and when the
// start fails, that scheduled audit runs synchronously on the game thread. Pure header (the thread start is injected), host-tested (thread_audit_test.sh).
#pragma once
#include <cstdint>

namespace jau {
enum class Path { Background, Sync };   // Background: the running worker covers this audit; Sync: run it on the game thread now

struct Starter {
    bool running = false;          // the worker started (it never stops in a process: it lists the threads every period until exit)
    uint32_t tries = 0, failures = 0, sync_runs = 0;
    // At a scheduled audit. `start` tries to start the worker and returns true when it is running (it audits at once, then every period).
    template <class Start>
    Path at_audit(Start&& start)
    {
        if (running) return Path::Background;
        tries++;
        if (start()) { running = true; return Path::Background; }
        failures++; sync_runs++;
        return Path::Sync;
    }
};
}  // namespace jau
