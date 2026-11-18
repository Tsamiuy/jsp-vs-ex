// jsp_thread_registry.h -- DIAGNOSTIC (rollback): a fixed table of the process's host threads for the snapshot freeze, kept up to date by
// the threads themselves (an exe TLS callback in jsp_thread_registry.cpp calls add() on DLL_THREAD_ATTACH and remove() on DLL_THREAD_DETACH once JSP_ROLLBACK=1 or JSP_SNAPSHOT_THREADS=registry is set at process start). Pure: std::atomic
// only, no OS headers; the wait/wake/yield/close operations are injected so a host test can drive the same code with std::thread.
//
// Why : the per-tick freeze listed the threads with CreateToolhelp32Snapshot and opened each with
// OpenThread (prep ~6 ms per tick in soak-s2, plus ~65 CloseHandle per tick that no timer counted). The registry holds one duplicated handle per thread for
// the thread's whole life, so a freeze only copies the LIVE slots.
//
// The birth gate. A thread born while a freeze is in progress must not run unfrozen and unseen. freeze_begin() sets `freezing` (seq_cst) and waits until no
// add()/remove() is between its two fences (`inflight`); add()/remove() increment `inflight` (seq_cst) and then read `freezing`: with seq_cst on both sides
// either the thread sees `freezing` and waits (before it publishes anything, before its own code runs) or the freezer sees `inflight` and waits for the
// publication to finish, so copy_live() lists it. freeze_end() clears `freezing` and wakes the waiters. A thread blocked in add() holds the loader lock
// (it is inside a TLS callback): the freezer must not take the loader lock between freeze_begin() and freeze_end() (it does not: SuspendThread,
// GetThreadContext, table reads, GetWriteWatch, memcpy).
// remove() frees the slot and closes the handle at once when the platform set close_fn (the boot creates many short-lived threads and the
// slots must not wait for the next snapshot tick); without close_fn it only marks the slot DEAD and reclaim() frees it later. Both run outside a freeze (remove()
// and add() wait in enter(); the snapshot thread's reclaim() runs between freezes), so a handle copied by a freeze is never closed under it. A slot changes owner only
// through a CAS to Claim (remove, reclaim, place), so a handle is closed exactly once even when a reclaimer and a remove race. Slots are fixed (kSlots); a thread that finds no free slot is counted in `full` and the freeze fails loudly (REGISTRY-FULL).
// Nothing here allocates (host test: a counting operator new around freeze_begin .. freeze_end).
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace jtr {

constexpr int kSlots = 512;
enum : uint32_t { Free = 0, Claim = 1, Live = 2, Dead = 3 };

struct Slot { std::atomic<uint32_t> state{Free}; uint32_t tid = 0; void* h = nullptr; uint8_t seeded = 0; };
struct Entry { void* h = nullptr; uint32_t tid = 0; uint8_t seeded = 0; };

struct Registry {
    Slot s[kSlots];
    std::atomic<uint32_t> freezing{0};    // birth gate: 1 between freeze_begin and freeze_end
    std::atomic<uint32_t> inflight{0};    // add()/remove() between the gate check and the publication
    std::atomic<uint32_t> enabled{0};     // set once by the exe at process attach (JSP_SNAPSHOT_THREADS=registry): add()/remove() are no-ops before
    std::atomic<uint32_t> full{0}, births{0}, deaths{0}, gated{0}, seeded{0}, not_found{0};   // gated: add/remove calls that waited on a freeze
    std::atomic<uint32_t> handle_failed{0};   // the platform could not make a handle for a thread (it stays unregistered: the freeze fails loudly)
    std::atomic<uint32_t> gen{0};         // bumped by every add/remove/reclaim (tests; the audit compares listings, not this)
    // thread exit frees its own slot (and closes its handle) when the platform set close_fn at init (before any other thread exists); add() on a
    // full table first reclaims slots of threads that ended without a detach. `full` stays sticky on purpose: a thread that found no slot even after a reclaim is
    // unregistered for good, and clearing the counter would hide it from the freeze's check (the audit names it).
    void (*close_fn)(void*) = nullptr;     // CloseHandle
    bool (*ended_fn)(void*) = nullptr;     // GetExitCodeThread != STILL_ACTIVE
    std::atomic<uint32_t> reaped{0};       // slots freed at thread exit or by a reclaim
    std::atomic<uint32_t> live{0}, live_peak{0};   // gauge of LIVE slots (add +1, Live->Dead/Claim -1) and its peak
    std::atomic<uint32_t> reclaiming{0};   // one reclaimer at a time (a handle must not be closed twice)
    // A/B (JSP_SNAPSHOT_BIRTHGATE=0): 0 = freeze_begin does not raise `freezing`, so add()/remove() never wait; a thread born during a freeze then runs unfrozen until the next
    // freeze (the Toolhelp listing's limitation), and one exiting during a freeze is not held in its detach callback. Only to tell whether the gate is what an earlier post-rollback hang needed.
    std::atomic<uint32_t> gate_births{1};
    // Hooks kept for a diagnostic build, unused here: skip_tid = a thread the freeze must leave alone (0 = none, the value in this build); `waiting` = threads
    // blocked in add()/remove() on the birth gate right now (a gauge nothing reads in this build).
    std::atomic<uint32_t> skip_tid{0}, waiting{0};

    // wait(addr, expected): block while *addr == expected (WaitOnAddress / std::atomic::wait); spurious returns are fine.
    template<class Wait> void enter(Wait wait)
    {
        for (;;) {
            inflight.fetch_add(1, std::memory_order_seq_cst);
            if (!freezing.load(std::memory_order_seq_cst)) return;
            inflight.fetch_sub(1, std::memory_order_seq_cst);
            gated.fetch_add(1, std::memory_order_relaxed);
            waiting.fetch_add(1, std::memory_order_relaxed);
            while (freezing.load(std::memory_order_seq_cst)) wait(&freezing, 1u);
            waiting.fetch_sub(1, std::memory_order_relaxed);
        }
    }
    void leave() { inflight.fetch_sub(1, std::memory_order_seq_cst); }

    // A thread registers itself (h: a handle the registry owns from now on). Returns the slot, or -1 when the table is full (counted; the freeze then fails).
    template<class Wait> int add(uint32_t tid, void* h, Wait wait, bool is_seeded = false)
    {
        enter(wait);
        int got = place(tid, h, is_seeded);
        if (got < 0 && close_fn && ended_fn && reclaim(close_fn, ended_fn, true, true) > 0) got = place(tid, h, is_seeded);   // a table full of ended threads is not full (waits for a concurrent reclaimer)
        if (got < 0) full.fetch_add(1, std::memory_order_relaxed);
        else {
            births.fetch_add(1, std::memory_order_relaxed); if (is_seeded) seeded.fetch_add(1, std::memory_order_relaxed);
            const uint32_t n = live.fetch_add(1, std::memory_order_relaxed) + 1;
            uint32_t pk = live_peak.load(std::memory_order_relaxed);
            while (n > pk && !live_peak.compare_exchange_weak(pk, n, std::memory_order_relaxed)) {}
        }
        gen.fetch_add(1, std::memory_order_relaxed);
        leave();
        return got;
    }
    int place(uint32_t tid, void* h, bool is_seeded)
    {
        for (int i = 0; i < kSlots; i++) {
            uint32_t f = Free;
            if (!s[i].state.compare_exchange_strong(f, Claim, std::memory_order_acq_rel)) continue;
            s[i].tid = tid; s[i].h = h; s[i].seeded = is_seeded ? 1 : 0;
            s[i].state.store(Live, std::memory_order_release);
            return i;
        }
        return -1;
    }
    // A thread ends (its own DLL_THREAD_DETACH): with close_fn the slot is freed and the handle closed here; without it the slot becomes DEAD and the handle stays open until reclaim().
    template<class Wait> bool remove(uint32_t tid, Wait wait)
    {
        enter(wait);
        bool hit = false;
        for (int i = 0; i < kSlots; i++) {
            if (s[i].state.load(std::memory_order_acquire) != Live || s[i].tid != tid) continue;   // all matching slots: a thread seeded while it registered itself has two
            uint32_t e = Live;
            if (!s[i].state.compare_exchange_strong(e, Claim, std::memory_order_acq_rel)) continue;   // a reclaimer took it first: it frees the slot, not us
            live.fetch_sub(1, std::memory_order_relaxed);
            hit = true;
            if (close_fn) {   // the thread is leaving and no freeze is in progress (enter() waited): nobody holds a copy of this handle, free the slot now
                void* hh = s[i].h; s[i].h = nullptr; s[i].tid = 0; s[i].seeded = 0;
                close_fn(hh);
                s[i].state.store(Free, std::memory_order_release);
                reaped.fetch_add(1, std::memory_order_relaxed);
            } else s[i].state.store(Dead, std::memory_order_release);
        }
        if (hit) deaths.fetch_add(1, std::memory_order_relaxed); else not_found.fetch_add(1, std::memory_order_relaxed);
        gen.fetch_add(1, std::memory_order_relaxed);
        leave();
        return hit;
    }
    // Is `tid` registered and live? (audit; racy by nature, the caller retries)
    bool has(uint32_t tid) const
    {
        for (int i = 0; i < kSlots; i++) if (s[i].state.load(std::memory_order_acquire) == Live && s[i].tid == tid) return true;
        return false;
    }
    // LIVE, or DEAD and not reclaimed yet (only without close_fn; a thread inside its own exit still runs other DLLs' detach code): the audit counts both as registered. With close_fn an exiting thread is
    // briefly in neither state; the audit re-checks it (OpenThread / GetExitCodeThread) for 20 ms before calling it unregistered.
    bool has_any(uint32_t tid) const
    {
        for (int i = 0; i < kSlots; i++) { const uint32_t st = s[i].state.load(std::memory_order_acquire); if ((st == Live || st == Dead) && s[i].tid == tid) return true; }
        return false;
    }
    // The snapshot thread, outside a freeze: a DEAD slot whose thread has ended (ended(h)) closes its handle and becomes FREE; a DEAD slot whose thread is still
    // running its exit stays (the audit still finds it). LIVE seeded slots whose thread has ended are retired too (a thread seeded at enable time may never
    // deliver a detach). Returns the number of slots freed. ended() is called only for DEAD and seeded slots (a few per tick at most), and for every LIVE slot when all_live.
    // all_live: also retire LIVE slots (not only seeded ones) whose thread handle is signalled: a thread that was terminated never runs its detach callback;
    // that costs one GetExitCodeThread per live slot, so only the audit asks for it. wait_lock: spin until a concurrent reclaimer is done instead of returning 0 (add() on a
    // full table, "0 freed" must mean nothing was reclaimable, not that someone else was busy).
    template<class CloseFn, class EndedFn> unsigned reclaim(CloseFn close, EndedFn ended, bool all_live = false, bool wait_lock = false)
    {
        for (;;) {
            uint32_t none = 0;
            if (reclaiming.compare_exchange_strong(none, 1, std::memory_order_acq_rel)) break;
            if (!wait_lock) return 0;   // another thread is reclaiming
        }
        unsigned n = 0;
        for (int i = 0; i < kSlots; i++) {
            const uint32_t st = s[i].state.load(std::memory_order_acquire);
            if (st != Dead && !(st == Live && (all_live || s[i].seeded))) continue;
            if (!ended(s[i].h)) continue;
            uint32_t e = st;
            if (!s[i].state.compare_exchange_strong(e, Claim, std::memory_order_acq_rel)) continue;   // changed under us (a remove)
            if (st == Live) live.fetch_sub(1, std::memory_order_relaxed);
            close(s[i].h); s[i].h = nullptr; s[i].tid = 0; s[i].seeded = 0;
            s[i].state.store(Free, std::memory_order_release);
            reaped.fetch_add(1, std::memory_order_relaxed);
            gen.fetch_add(1, std::memory_order_relaxed); n++;
        }
        reclaiming.store(0, std::memory_order_release);
        return n;
    }

    // Freezer side. yield(): spin politely while an add/remove finishes its publication (a few stores).
    template<class Yield> void freeze_begin(Yield yield)
    {
        if (gate_births.load(std::memory_order_relaxed)) freezing.store(1, std::memory_order_seq_cst);
        while (inflight.load(std::memory_order_seq_cst)) yield();
    }
    // Copies the LIVE slots except `self` into out[0..cap). Returns the count; *overflow when more were live than fit. Call between freeze_begin and freeze_end.
    size_t copy_live(Entry* out, size_t cap, uint32_t self, bool* overflow) const
    {
        size_t n = 0; *overflow = false;
        const uint32_t skip = skip_tid.load(std::memory_order_relaxed);
        for (int i = 0; i < kSlots; i++) {
            if (s[i].state.load(std::memory_order_acquire) != Live || s[i].tid == self || (skip && s[i].tid == skip)) continue;
            if (n == cap) { *overflow = true; break; }
            out[n].h = s[i].h; out[n].tid = s[i].tid; out[n].seeded = s[i].seeded; n++;
        }
        return n;
    }
    template<class Wake> void freeze_end(Wake wake)
    {
        freezing.store(0, std::memory_order_seq_cst);
        wake(&freezing);
    }
    // the exe's TLS callback is evidently not delivered when many threads exist but (almost) none registered itself: births counts every
    // registration, seeded the ones added from a Toolhelp listing because the callback had not seen them. Used at the first audit after the game's threads
    // started (the baseline), where a working callback has registered dozens.
    static bool callback_missing(unsigned listed, uint32_t births_total, uint32_t seeded_total) { return listed >= 4 && births_total - seeded_total <= 1; }
    uint32_t live_count() const { uint32_t n = 0; for (int i = 0; i < kSlots; i++) n += s[i].state.load(std::memory_order_acquire) == Live; return n; }
};

}  // namespace jtr

// Windows glue (jsp_thread_registry.cpp; no-op stand-ins off Windows). The registry instance and its platform operations for jsp_snapshot.cpp.
jtr::Registry& jsp_thread_registry();
bool jsp_thread_registry_enabled();                                   // JSP_SNAPSHOT_THREADS=registry was set at process start
void jsp_thread_registry_wait(std::atomic<uint32_t>* a, uint32_t v);  // WaitOnAddress, INFINITE
void jsp_thread_registry_wake(std::atomic<uint32_t>* a);              // WakeByAddressAll
void jsp_thread_registry_yield();                                     // SwitchToThread
void jsp_thread_registry_close(void* h);                              // CloseHandle
bool jsp_thread_registry_ended(void* h);                              // GetExitCodeThread != STILL_ACTIVE
unsigned jsp_thread_registry_seed();                                  // add the threads alive now that are not registered (seeded)
int jsp_thread_registry_audit(char* why, size_t n, unsigned* listed, unsigned* rechecked);   // 0 = all registered, else 1 + why
