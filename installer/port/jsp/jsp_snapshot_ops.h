// Snapshot platform-operation sequencing shared by the Windows wiring and fake-backend host tests.
// No OS/runtime headers, allocation, logging or callbacks into the game. This does not establish a simulation safe point.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace jso {

enum class Action { Baseline, Commit, Rollback };
enum Failure : unsigned { Pause = 1, Query = 2, Apply = 4, Reset = 8, Verify = 16, Resume = 32 };
struct Result { unsigned failures = 0; bool applied = false; bool ok() const { return failures == 0; } };

// Ops prepares all storage before this call. collect() may consume/reset part of a watch even when it fails:
// never apply that partial result. Rollback metadata must follow restored RAM even when the subsequent reset fails.
template<class Ops> Result capture(Ops& ops, Action action)
{
    Result r;
    if (!ops.pause()) r.failures |= Pause;
    if (!r.failures) {
        if (action == Action::Baseline) {
            if (!ops.reset()) r.failures |= Reset;
        } else if (!ops.collect()) r.failures |= Query;
    }
    if (!r.failures) {
        r.applied = ops.apply(action);
        if (!r.applied) r.failures |= Apply;
    }
    if (r.applied && action == Action::Rollback && !ops.reset()) r.failures |= Reset;
    if (r.applied && !r.failures && !ops.verify()) r.failures |= Verify;
    if (!ops.resume()) {
        r.failures |= Resume;
        return r; // a paused lock owner may remain: no mutex/logging callbacks; production must fail-stop
    }
    if (r.applied && action == Action::Rollback) ops.restore_metadata(); // mutex-taking work only after resume
    return r;
}

// A thread slot has .h and .paused. Pause/resume functions return whether their own operation succeeded.
// Do not decrement another owner's suspend count after our SuspendThread call failed.
template<class Threads, class PauseFn> bool pause_threads(Threads& threads, PauseFn pause)
{
    for (auto& t : threads) {
        if (!pause(t.h)) return false;
        t.paused = true;
    }
    return true;
}
template<class Threads, class ResumeFn> bool resume_threads(Threads& threads, ResumeFn resume)
{
    bool ok = true;
    for (auto& t : threads) if (t.paused) {
        if (!resume(t.h)) ok = false;
        else t.paused = false;
    }
    return ok;
}

// Canonical range sets: equality of byte totals is insufficient when the addresses changed.
template<class Ranges> bool same_ranges(const Ranges& a, const Ranges& b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
        [](const auto& x, const auto& y) { return x.a == y.a && x.n == y.n; });
}

// Query a bounded address span with enough output entries for every page in it. Query and emit are injected;
// returned addresses may be unordered. A malformed/failed result aborts the whole collection. Buffers are caller-owned.
template<class QueryFn, class EmitFn>
bool collect_pages(uintptr_t begin, size_t bytes, uint32_t page, void** addresses, size_t capacity,
                   QueryFn query, EmitFn emit)
{
    if (!page || !capacity || begin % page || bytes % page || !addresses
        || capacity > std::numeric_limits<size_t>::max() / page
        || bytes > std::numeric_limits<uintptr_t>::max() - begin) return false;
    const uintptr_t end = begin + bytes;
    while (begin < end) {
        const size_t span = std::min<size_t>(end - begin, capacity * page);
        size_t count = capacity; uint32_t granularity = 0;
        if (!query(begin, span, addresses, &count, &granularity)
            || granularity != page || count > span / page) return false;
        for (size_t i = 0; i < count; i++) {
            const uintptr_t a = reinterpret_cast<uintptr_t>(addresses[i]);
            if (a < begin || a >= begin + span || a % page || !emit(a)) return false;
        }
        begin += span;
    }
    return true;
}

} // namespace jso
