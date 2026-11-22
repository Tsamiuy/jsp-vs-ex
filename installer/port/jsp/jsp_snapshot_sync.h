// jsp_snapshot_sync.h -- DIAGNOSTIC / safety gate for the rollback prototype. Pure logic, no OS or runtime headers: the Windows
// glue in jsp_snapshot.cpp fills a View from the ps3recomp HLE kernel objects (patch 7911), a host test can drive the same
// code with a toy kernel.
//
// Why : a snapshot rewinds guest memory and the main thread's PPU context, but NOT the host
// state of the HLE kernel objects (lwmutex slots in ppu_sysprx.cpp, event queues, semaphores, mutexes) and not the other threads' PPU
// contexts or host stacks. lwmutex is the sharp case: the runtime keeps the lock in a host state word and mirrors the owner and recursion count
// into the guest sys_lwmutex_t, then DECIDES from the guest copy (a re-lock by the owner only bumps the count; an unlock with count > 1 only
// decrements). A rollback that rewinds the mirror under a held or free host lock therefore leaks the host lock or skips taking it.
// Serialising the host state back is not sound ("thread T holds L" is a fact about T's execution). The gate instead requires a moment where
// nothing is in transit: for lwmutexes, mutexes and rwlocks that makes mirror and host agree (both free); for event queues and semaphores it
// means no receiver/waiter has been woken and not yet through. It is NOT whole-system consistency: event flags, sys_cond, timers, the thread
// table, cellFiber, SPURS/job queues, GCM labels and the other threads' wait sites and host stacks are not covered (see the doc).
//
// What blocks (reasons) and what is only reported (notes) -- see judge():
//   blocks:  lwmutex held on the host by a live thread, or held with guest owner 0 (holder unknown: the lwcond_wait reacquire window); heavy mutex or rwlock held; an event queue with a receiver inside AND events buffered
//            (a woken receiver); a semaphore with a waiter AND a positive value (a woken waiter); a thread marked as inside lwmutex lock/
//            reacquire or other "in transit" HLE call; with strict also any thread running guest code or an unclassified HLE call.
//   notes:   lwmutex slots abandoned (held by an ended thread) or stale (free with a non-zero guest mirror: reused heap address),
//            destroyed slots (skipped), slots on uncommitted pages (skipped); event queues with buffered events and no receiver (fed but not
//            drained) and their drift against the save's view; semaphore values that differ from the save's; threads running guest code.
//
// FRAME-PIPELINE PARK POINTS. The lock census above says nothing about WHERE each pipeline thread is
// parked. A role is a set of threads that must sit at a known wait site (a caller lr, optionally the syscall/NID) at a frame boundary: the video thread blocked
// in its q=7 receive, the job workers in their job wait, the CRI servers in their lwcond poll. Members are learned (a live thread seen parked at a site of the
// role joins it); a member that is alive and not at one of the role's sites at a gate blocks (RoleAway), a required role with nobody at a site blocks
// (RoleAbsent), and a role queue that already holds an event (a frame signal sent and not yet consumed) blocks (RoleQueue). The gate then waits for the
// condition, as before. RsxPending (opt-in): the GCM control put != get, i.e. the RSX has not consumed the last frame's commands.
#pragma once
#include <chrono>
#include <cstdint>
#include "jsp_thread_bit.h"   // jtb::thread_bit: the park roles' member masks (bit tid - 1)

namespace jsy {

constexpr int kEvq = 128, kSem = 256, kThr = 65;   // table sizes of the runtime; kThr: thread ids 1..64

// Plain-data census taken while the other threads are suspended. Counts of -1 mark objects that are not alive.
struct View {
    uint32_t lwm_tracked = 0, lwm_live_held = 0, lwm_abandoned = 0, lwm_dead = 0, lwm_stale = 0, lwm_unreadable = 0, lwm_held_unowned = 0;
    uint32_t lwm_first_live = 0, lwm_first_stale = 0;     // first guest address of each, 0 = none
    int32_t evq_count[kEvq], evq_wait[kEvq];              // buffered events / receivers inside the receive (count -1 = no queue)
    int32_t sem_val[kSem], sem_wait[kSem];                // value / waiters (wait -1 = no semaphore)
    uint32_t mtx_held = 0, rw_held = 0;
    uint32_t t_kind[kThr], t_id[kThr], t_obj[kThr], t_lr[kThr]; uint8_t t_alive[kThr];   // per-thread marker (kind 0 = outside any HLE call)
    uint8_t t_unrel[kThr];                                // marker unreliable (nesting overflow, or the thread ran cellFibers)
    uint32_t mark_overflows = 0, mark_unbalanced = 0;     // global counts from the runtime
    uint32_t rsx_put = 0, rsx_get = 0; uint8_t rsx_valid = 0;   // GCM control put/get words (guest memory), valid = the read succeeded
    // (patch 7913 census functions; valid = the runtime export was called). SPURS: jobs executing on a host thread + jobs the HLE queues count as pending (blocks),
    // busy policy-module slots / job-chain walkers (notes: a waiting policy or a walker parked on a guard is idle).
    uint8_t jobs_valid = 0; uint32_t jobs_inflight = 0, jq_pending = 0, pm_busy = 0, jc_running = 0, jc_total = 0, spurs_inst = 0, async_running = 0;   // async_running: taskset tasks / SPU thread-group threads (note only)
    // cellFiber: fibers by state; `fb_away` = RUNNING and not the caller's own fiber; the scheduler lock taken = a thread was suspended inside the scheduler.
    uint8_t fiber_valid = 0; uint32_t fb_total = 0, fb_ready = 0, fb_running = 0, fb_wait_signal = 0, fb_wait_join = 0, fb_done = 0, fb_away = 0, fb_sched_running = 0, fb_lock_held = 0;
    // GCM flip state: status (1 = WAITING), flip commands queued in the FIFO / executed by the drain / completed, a direct flip waiting for its tick, the pump bits not yet delivered.
    uint8_t flip_valid = 0; uint32_t flip_status = 0, flip_req_seq = 0, flip_tick_seq = 0, flips_queued = 0, flips_reached = 0, flips_completed = 0, flip_direct = 0, gcm_pending = 0, flip_lock = 0;
    // the SPU job server's work state, read from guest memory (jsp_snapshot.cpp): js_bits = job manager word mgr+0x6C (set per dispatched job, cleared by the completion
    // fiber), js_state2 = job records in state 2 ("taken by a worker / dispatched, completion not yet run") among the manager's record pool. valid = the manager was found.
    uint8_t js_valid = 0; uint32_t js_bits = 0, js_state2 = 0;
    View() {
        for (int i = 0; i < kEvq; i++) { evq_count[i] = -1; evq_wait[i] = 0; }
        for (int i = 0; i < kSem; i++) { sem_val[i] = 0; sem_wait[i] = -1; }
        for (int i = 0; i < kThr; i++) { t_kind[i] = t_id[i] = t_obj[i] = t_lr[i] = 0; t_alive[i] = 0; t_unrel[i] = 0; }
    }
};

enum Reason : unsigned {
    LwmHeld = 1, MutexHeld = 2, RwHeld = 4, EvqWoken = 8, SemWoken = 16, ThreadTransit = 32, ThreadRunning = 64, PauseFailed = 128, LwmUnowned = 256,
    RoleAway = 512, RoleAbsent = 1024, RoleQueue = 2048, RsxPending = 4096,   // only with Cfg::roles set and Cfg::quiesce_block (else notes)
    JobsInFlight = 8192, FiberAway = 16384, FlipPending = 32768, JobServerBusy = 65536,            // block only with the matching Cfg::block_* flag (else notes)
    SiteMoved = 131072,   // a restore: a thread parked at the save is parked elsewhere now (Cfg::block_site, bounded by Cfg::site_wait_ms)
};
enum Note : unsigned {
    NLwmAbandoned = 1, NLwmStale = 2, NLwmUnreadable = 4, NEvqBuffered = 8, NEvqDrift = 16, NSemDrift = 32, NThreadsRunning = 64, NNoSavedView = 128, NMarkerUnreliable = 256,
    NRoleAway = 512, NRoleAbsent = 1024, NRoleQueue = 2048, NRsxPending = 4096,
    NJobsInFlight = 8192, NFiberAway = 16384, NFlipPending = 32768, NAsyncRunning = 65536, NHostObjDrift = 131072, NJobServerBusy = 262144, NSiteMoved = 524288,   // async SPU threads running; fibers / job chains / SPURS instances differ from the save's view
};

// Marker kinds (patch 7911, sys_snapshot_census.c): 1 = HLE import (id = NID), 2 = lv2 syscall (id = number), 3 = internal in-transit window
// (id 1 = lwmutex acquire + guest write-back in sys_lwcond_wait, 2 = event dequeue to copy-out, 3 = semaphore wake to value re-check).
// A park site: the guest lr of the HLE call the thread is blocked in (what the marker records), plus an optional syscall number / NID (0 = any).
struct Site { uint32_t lr = 0, kind = 0, id = 0; };
struct Role {
    char name[12] = {0}; Site site[4]; int nsite = 0;
    bool required = false;        // at least one thread must be at a site
    bool queue_empty = false;     // the marker's object (r3 = the event queue id) must hold no buffered event
    uint64_t members = 0;         // learned: bit (tid - 1)
    void add_site(uint32_t lr, uint32_t kind = 0, uint32_t id = 0) { if (nsite < 4) site[nsite++] = {lr, kind, id}; }
};
struct Roles {
    Role r[8]; int n = 0;
    Role* add(const char* name, bool required, bool queue_empty) {
        if (n >= 8) return nullptr;
        Role& x = r[n++]; x = Role(); x.required = required; x.queue_empty = queue_empty;
        for (int i = 0; i < 11 && name[i]; i++) x.name[i] = name[i];
        return &x;
    }
    void forget() { for (int i = 0; i < n; i++) r[i].members = 0; }
};

struct Cfg {
    Roles* roles = nullptr;                 // frame-pipeline park points (learned membership lives in the pointee); nullptr = not checked
    bool quiesce_block = false;             // JSP_SNAPSHOT_QUIESCE=2: role violations block (else notes only)
    // ENHANCEMENT (rollback latency, A/B, 0 = off): gate() first waits UNFROZEN, at most this long, while an unfrozen precheck (judged against a scratch copy of the roles,
    // as gate_ev does) is blocked, sleeping on the runtime's gate sequence between prechecks; only then the frozen loop runs. A rollback re-run tick reaches its save with the SPU
    // jobs and the frame-pipeline threads still busy (measured: first try blocked by JobsInFlight|JobServerBusy|RoleAway|RoleAbsent on ~90% of depth-2 saves, ~3 freezes per
    // save). The frozen judge still decides: a wrong precheck only costs a freeze. Needs Ops::seq / presample / wait (the port's GateOps); other Ops skip it.
    unsigned prewait_ms = 0;
    // ENHANCEMENT (rollback latency, JSP_SNAPSHOT_PRECHECK_US, 0 = off): gate() runs the unfrozen precheck before EVERY freeze in Wait mode, polling in short spins
    // (Ops::spin, ~0.1 ms) while it is blocked, at most precheck_us in all per gate; then the frozen loop goes on exactly as before. A two-copy run (it-repeat-l66,
    // host) had the first freeze refused on 66% of commits (RoleAway|RoleAbsent) and 99% of rollbacks (RsxPending, SiteMoved): each refusal is a full suspend
    // and resume of ~69 threads plus Sleep(1). The frozen judge still decides what is captured; the precheck only picks the moment of the freeze.
    unsigned precheck_us = 0;
    bool rsx_idle = false;                 // JSP_SNAPSHOT_QUIESCE_RSX=1: GCM put != get blocks
    bool block_jobserver = false;   // SPU job server work outstanding (mgr+0x6C != 0 or a job record in state 2) blocks (JSP_SNAPSHOT_QUIESCE_JOBS or GATE_JOBSERVER)
    bool block_jobs = false, block_fibers = false, block_flip = false;   // SPURS jobs in flight / fibers away / flip outstanding block (JSP_SNAPSHOT_QUIESCE=2 sets all three; JSP_SNAPSHOT_QUIESCE_JOBS|FIBERS|FLIP=0 clears one)
    // ENHANCEMENT (rollback, JSP_SNAPSHOT_SITE=1): a RESTORE waits until every thread that was parked at the target tick's save is parked at the SAME wait site
    // (marker kind, id and caller lr) again. Restoring memory rewinds a helper thread's stack and the work queues it serves, not the thread itself: a CRI loader
    // worker that took a job after the save and is parked inside that job when the memory goes back runs on with a stack from before the job (seen: the main
    // thread's load wait never ended, 2026-11-21 R1). Once the thread is back at its save-time site, the rewound memory and the thread agree again. Bounded:
    // after site_wait_ms the restore goes ahead with the note (a thread that moved for good, e.g. a new loading phase, must not stall the match).
    bool block_site = false; unsigned site_wait_ms = 50;
    bool strict = false;                    // JSP_SNAPSHOT_SYNC_STRICT=1: threads running guest code or unclassified HLE calls also block
    uint32_t self_tid = 0;                  // the calling (main) thread: not suspended, not judged
    uint32_t nid_parked[16]; int n_parked = 0;     // HLE imports that block waiting for something (sys_lwcond_wait, timers, ...)
    uint32_t nid_transit[8]; int n_transit = 0;    // HLE imports that are a lock in progress (sys_lwmutex_lock, trylock)
    void add_parked(uint32_t n) { if (n_parked < 16) nid_parked[n_parked++] = n; }
    void add_transit(uint32_t n) { if (n_transit < 8) nid_transit[n_transit++] = n; }
};

enum class TC { Outside, Parked, Transit, Running, Unknown };   // Unknown: the marker is unreliable (overflow, fibers): reported, blocks only in strict
inline TC classify_thread(uint32_t kind, uint32_t id, const Cfg& c)
{
    if (kind == 0) return TC::Outside;
    if (kind == 3) return (id >= 1 && id <= 3) ? TC::Transit : TC::Running;
    if (kind == 2) {
        switch (id) {   // lv2 syscall numbers: exit, join, event_flag_wait, semaphore_wait, mutex_lock, cond_wait, lwcond_wait, rwlock r/wlock, event_queue_receive, usleep, sleep
        case 42: case 44: case 85: case 92: case 102: case 107: case 113: case 122: case 125: case 130: case 141: case 142: return TC::Parked;
        case 97: case 99: return TC::Transit;   // _sys_lwmutex_lock, _sys_lwmutex_trylock
        default: return TC::Running;
        }
    }
    for (int i = 0; i < c.n_transit; i++) if (c.nid_transit[i] == id) return TC::Transit;
    for (int i = 0; i < c.n_parked; i++) if (c.nid_parked[i] == id) return TC::Parked;
    return TC::Running;
}

struct ThrRep { uint32_t tid, kind, id, obj, lr; TC cls; };
struct Verdict {
    unsigned reasons = 0, notes = 0;
    uint32_t n_parked = 0, n_transit = 0, n_running = 0, n_outside = 0, n_unknown = 0;
    uint32_t evq_woken = 0, evq_buffered = 0, evq_drift = 0, sem_woken = 0, sem_drift = 0;
    uint32_t role_away = 0, role_absent = 0, role_queue = 0, role_at = 0, rsx_pending = 0;
    uint32_t site_moved = 0, site_tid = 0, site_lr_was = 0, site_lr_now = 0;   // threads parked elsewhere than at the save (first one for the log)
    uint32_t host_drift = 0;   // fb_total / jc_total / spurs_inst that differ from the saved view (host objects created or destroyed in the window)
    uint32_t jobsrv = 0;   // (js_bits != 0) + job records in state 2
    uint32_t jobs = 0, fibers_away = 0, flip_pending = 0;   // jobs in flight (+ HLE queue pending); fibers running elsewhere + scheduler lock held; flip outstanding (status WAITING, command ahead of the drain, direct flip, undelivered flip pump, flip lock)
    uint32_t away_tid[8] = {0}, away_lr[8] = {0}; int naway = 0;   // members not at their site, first 8, for the log
    ThrRep thr[8]; int nthr = 0;           // non-parked live threads, first 8, for the log
};

// A save and a restore both need reasons == 0. saved == nullptr (a save, a commit) skips the drift notes.
inline Verdict judge(const View& v, const View* saved, const Cfg& c)
{
    Verdict d;
    if (v.lwm_live_held) d.reasons |= LwmHeld;
    if (v.lwm_held_unowned) d.reasons |= LwmUnowned;   // held on the host with guest owner 0: holder unknown, possibly between the word and the mirror stamp
    if (v.mtx_held) d.reasons |= MutexHeld;
    if (v.rw_held) d.reasons |= RwHeld;
    if (v.lwm_abandoned) d.notes |= NLwmAbandoned;
    if (v.lwm_stale) d.notes |= NLwmStale;
    if (v.lwm_unreadable) d.notes |= NLwmUnreadable;
    for (int i = 0; i < kEvq; i++) {
        if (v.evq_count[i] < 0) continue;
        if (v.evq_count[i] > 0 && v.evq_wait[i] > 0) d.evq_woken++;                  // a receiver is inside and an event is buffered: woken, not through
        else if (v.evq_count[i] > 0) d.evq_buffered += (uint32_t)v.evq_count[i];     // fed, nobody inside: not drained (or its receiver is in guest code)
        if (saved && saved->evq_count[i] >= 0 && saved->evq_count[i] != v.evq_count[i]) d.evq_drift++;
    }
    for (int i = 0; i < kSem; i++) {
        if (v.sem_wait[i] < 0) continue;
        if (v.sem_wait[i] > 0 && v.sem_val[i] > 0) d.sem_woken++;
        if (saved && saved->sem_wait[i] >= 0 && saved->sem_val[i] != v.sem_val[i]) d.sem_drift++;
    }
    if (d.evq_woken) d.reasons |= EvqWoken;
    if (d.sem_woken) d.reasons |= SemWoken;
    if (d.evq_buffered) d.notes |= NEvqBuffered;
    if (d.evq_drift) d.notes |= NEvqDrift;
    if (d.sem_drift) d.notes |= NSemDrift;
    auto cls = [&](int t) { return v.t_unrel[t] ? TC::Unknown : classify_thread(v.t_kind[t], v.t_id[t], c); };
    for (int pass = 0; pass < 2; pass++)   // pass 0 lists the in-transit threads first, so the first 8 of the log are the ones that block
        for (int t = 1; t < kThr; t++) {
            if (!v.t_alive[t] || (uint32_t)t == c.self_tid) continue;
            const TC k = cls(t);
            if (k == TC::Parked || (k == TC::Transit) != (pass == 0)) continue;
            if (pass == 0) d.n_transit++; else if (k == TC::Running) d.n_running++; else if (k == TC::Unknown) d.n_unknown++; else d.n_outside++;
            if (d.nthr < 8) d.thr[d.nthr++] = {(uint32_t)t, v.t_kind[t], v.t_id[t], v.t_obj[t], v.t_lr[t], k};
        }
    for (int t = 1; t < kThr; t++)
        if (v.t_alive[t] && (uint32_t)t != c.self_tid && cls(t) == TC::Parked) d.n_parked++;
    if (saved)   // wait sites against the save's (threads parked at the save only; a thread born or ended in between is not compared)
        for (int t = 1; t < kThr; t++) {
            if (!saved->t_alive[t] || !v.t_alive[t] || (uint32_t)t == c.self_tid || saved->t_unrel[t] || v.t_unrel[t]) continue;
            if (classify_thread(saved->t_kind[t], saved->t_id[t], c) != TC::Parked) continue;
            if (saved->t_kind[t] == v.t_kind[t] && saved->t_id[t] == v.t_id[t] && saved->t_lr[t] == v.t_lr[t]) continue;
            if (!d.site_moved) { d.site_tid = (uint32_t)t; d.site_lr_was = saved->t_lr[t]; d.site_lr_now = v.t_lr[t]; }
            d.site_moved++;
        }
    if (d.site_moved) { d.notes |= NSiteMoved; if (c.block_site) d.reasons |= SiteMoved; }
    if (c.roles) {   // park points
        for (int i = 0; i < c.roles->n; i++) {
            Role& ro = c.roles->r[i];
            uint32_t at = 0;
            for (int t = 1; t < kThr; t++) {
                const uint64_t bit = jtb::thread_bit((uint32_t)t);
                if (!v.t_alive[t]) { ro.members &= ~bit; continue; }          // an ended thread: ids are reused, forget it
                if ((uint32_t)t == c.self_tid) continue;
                bool here = false;
                if (!v.t_unrel[t] && classify_thread(v.t_kind[t], v.t_id[t], c) == TC::Parked)
                    for (int s = 0; s < ro.nsite; s++)
                        if (ro.site[s].lr == v.t_lr[t] && (!ro.site[s].kind || ro.site[s].kind == v.t_kind[t]) && (!ro.site[s].id || ro.site[s].id == v.t_id[t])) here = true;
                if (here) {
                    ro.members |= bit; at++;
                    if (ro.queue_empty && v.t_obj[t] >= 1 && v.t_obj[t] <= (uint32_t)kEvq && v.evq_count[v.t_obj[t] - 1] > 0) d.role_queue++;   // queue id = table slot + 1 (sys_event.c)
                } else if (ro.members & bit) {
                    d.role_away++;
                    if (d.naway < 8) { d.away_tid[d.naway] = (uint32_t)t; d.away_lr[d.naway] = v.t_lr[t]; d.naway++; }
                }
            }
            d.role_at += at;
            if (ro.required && at == 0) d.role_absent++;
        }
        if (d.role_away) d.notes |= NRoleAway;
        if (d.role_absent) d.notes |= NRoleAbsent;
        if (d.role_queue) d.notes |= NRoleQueue;
        if (c.quiesce_block) {
            if (d.role_away) d.reasons |= RoleAway;
            if (d.role_absent) d.reasons |= RoleAbsent;
            if (d.role_queue) d.reasons |= RoleQueue;
        }
    }
    if (v.rsx_valid && v.rsx_put != v.rsx_get) { d.rsx_pending = 1; d.notes |= NRsxPending; if (c.rsx_idle) d.reasons |= RsxPending; }
    // the three boundaries (SPURS work finished, fibers at scheduler-defined states, RSX flip/label sync)
    if (v.jobs_valid) {
        d.jobs = v.jobs_inflight + v.jq_pending;
        if (v.async_running) d.notes |= NAsyncRunning;
        if (saved && saved->jobs_valid && (saved->jc_total != v.jc_total || saved->spurs_inst != v.spurs_inst)) { d.host_drift++; d.notes |= NHostObjDrift; }
        if (d.jobs) { d.notes |= NJobsInFlight; if (c.block_jobs) d.reasons |= JobsInFlight; }
    }
    if (v.js_valid) {
        d.jobsrv = (v.js_bits != 0 ? 1u : 0u) + v.js_state2;
        if (d.jobsrv) { d.notes |= NJobServerBusy; if (c.block_jobserver || c.block_jobs) d.reasons |= JobServerBusy; }
    }
    if (v.fiber_valid) {
        d.fibers_away = v.fb_away + v.fb_lock_held;
        if (saved && saved->fiber_valid && saved->fb_total != v.fb_total) { d.host_drift++; d.notes |= NHostObjDrift; }
        if (d.fibers_away) { d.notes |= NFiberAway; if (c.block_fibers) d.reasons |= FiberAway; }
    }
    if (v.flip_valid) {
        d.flip_pending = (v.flip_status != 0 ? 1u : 0u) + (v.flips_queued != v.flips_reached ? 1u : 0u) + (v.flip_direct ? 1u : 0u) + ((v.gcm_pending & 2u) ? 1u : 0u) + (v.flip_lock ? 1u : 0u);
        if (d.flip_pending) { d.notes |= NFlipPending; if (c.block_flip) d.reasons |= FlipPending; }
    }
    if (d.n_transit) d.reasons |= ThreadTransit;
    if (d.n_running + d.n_outside + d.n_unknown) { d.notes |= NThreadsRunning; if (c.strict) d.reasons |= ThreadRunning; }
    if (d.n_unknown) d.notes |= NMarkerUnreliable;
    return d;
}

enum class Mode { Off = 0, Log = 1, Wait = 2 };   // JSP_SNAPSHOT_SYNC: 0 no census, 1 census in the log only (default), 2 wait for idle

struct GateResult {
    unsigned unfrozen_waits = 0;   // rollback session: unfrozen 1 ms waits for the RSX to go idle (gate())
    bool ok = true; unsigned tries = 0; unsigned reasons = 0; unsigned first_reasons = 0, first_notes = 0; Verdict verdict; View view;   // first_reasons: the reasons of the first sample (what made the gate retry)
    // (gate_ev): prechecks = unfrozen samples, freezes = suspend + frozen judge (== tries), wakes = waits on the gate sequence, wake_timeouts = waits that ended
    // by the timeout (no bump site fired: the condition changed somewhere the runtime does not signal). retry_waits = the bounded wait after a frozen judge that
    // refused a capture the precheck had passed (counted apart: it paces the re-freeze, it is not a missed wake-up), retry_timeouts = those that ended by their timeout.
    // first_reasons is the first UNFROZEN precheck's under gate_ev and the first frozen sample's under gate() (the soak line labels which).
    // suspended: the other threads are suspended on return (ok, or a frozen judge that ran out of freezes); false when the gate stopped in a precheck wait.
    unsigned prechecks = 0, freezes = 0, wakes = 0, wake_timeouts = 0, retry_waits = 0, retry_timeouts = 0; bool suspended = false; bool wake_stop = false;
    // retry_immediate = post-freeze waits whose sequence had already moved after the frozen sample was read (bumps arrive faster than the pacing: the wait returned at once).
    // persist[b] = prechecks that were woken by a bump and still blocked by reason bit b (a bump that did not end that reason: noise, or a reason with no site of its own);
    // timeout_by[b] = waits that ended by the timeout while reason bit b blocked (a reason that ended without any bump).
    unsigned retry_immediate = 0, persist[16] = {0}, timeout_by[16] = {0};
    bool site_waited = false, site_gave_up = false;   // SiteMoved blocked at least once / was dropped after Cfg::site_wait_ms
    uint32_t site_tid = 0, site_lr_was = 0, site_lr_now = 0;   // the first moved thread of the first sample that saw one (the log)
};

// Ops: bool suspend() / bool resume() (all other threads), void sample(View*) (while suspended: no allocation, no logging), void yield().
// Mode::Wait loops resume -> yield -> suspend -> sample until nothing is in transit: it waits for the condition (what the parked threads are
// blocked on), not for a time. max_tries is a loud safety stop (the caller fails the capture with the reasons), not a way to carry on.
// Returns with the threads SUSPENDED when ok (the caller copies, then resumes); suspended also after a failed gate in Wait mode.
// A failed suspend/resume is reported with reasons == PauseFailed and the flags `pause_failed` / `resume_failed` so the caller can label it.
// Rollback mode: the RSX is the one blocking condition that is read-only observable while the world runs (the GCM put/get words). Waiting for it WITH the threads frozen
// gives the draining thread 1 ms in ~4 (a freeze + resume cycle costs ~3 ms): at the first fight frame (a two-copy test, k=176: 456 tries, 2 s, put 0xD458 > get 0xD43C)
// the RSX never finished its frame. So after a try that was blocked by RsxPending, the gate waits UNFROZEN (ops.rsx_busy(), when the ops have it) before it freezes again.
template<class O> auto rsx_busy_(O& o, int) -> decltype(o.rsx_busy()) { return o.rsx_busy(); }
template<class O> bool rsx_busy_(O&, long) { return false; }
// max_ms (0 = no bound; when set it replaces max_tries as the limit): wall-time bound on the whole wait (rollback mode: the match's grace window is the budget, the gate must give up well inside it, loudly, like running out of tries).
// ENHANCEMENT (Cfg::prewait_ms): the unfrozen pre-wait of gate(), for Ops that have the gate_ev calls (seq / presample / wait); others get the no-op overload.
// FiberAway is judged only frozen (the unfrozen presample does not walk the fiber lists), as in gate_ev.
template<class O> auto prewait_(O& ops, const View* saved, const Cfg& cfg, GateResult& g, int) -> decltype(ops.seq(), ops.presample((View*)nullptr), ops.wait(0u, 0u), void())
{
    if (!cfg.prewait_ms) return;
    const auto t0 = std::chrono::steady_clock::now();
    Roles scratch; Cfg pc = cfg; View pv;
    for (;;) {
        const uint32_t s0 = ops.seq();
        if (cfg.roles) { scratch = *cfg.roles; pc.roles = &scratch; }
        ops.presample(&pv);
        const unsigned r = judge(pv, saved, pc).reasons & ~(unsigned)FiberAway;
        g.prechecks++;
        if (!r) return;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() >= (long long)cfg.prewait_ms) return;
        g.wakes++;
        if (!ops.wait(s0, 1)) g.wake_timeouts++;
    }
}
template<class O> void prewait_(O&, const View*, const Cfg&, GateResult&, long) {}
// ENHANCEMENT (Cfg::precheck_us): before a freeze, wait UNFROZEN while the precheck is blocked (FiberAway is judged frozen only; SiteMoved is dropped after
// site_wait_ms, as the frozen loop does). *used_us = precheck time spent by this gate so far (the budget is per gate). Counted in prechecks / wakes (wakes =
// spins here). For Ops with presample() and spin(); others skip it.
template<class O> auto precheck_(O& ops, const View* saved, const Cfg& cfg, GateResult& g, std::chrono::steady_clock::time_point t_start, unsigned max_ms, double* used_us, int)
    -> decltype(ops.presample((View*)nullptr), ops.spin(), void())
{
    using SC = std::chrono::steady_clock;
    if (!cfg.precheck_us || *used_us >= (double)cfg.precheck_us) return;
    const auto t0 = SC::now();
    Roles scratch; Cfg pc = cfg; View pv;
    for (;;) {
        if (cfg.roles) { scratch = *cfg.roles; pc.roles = &scratch; }   // membership is learned by the frozen judge only
        ops.presample(&pv);
        const Verdict d = judge(pv, saved, pc);
        unsigned r = d.reasons & ~(unsigned)FiberAway;
        const long long since_ms = std::chrono::duration_cast<std::chrono::milliseconds>(SC::now() - t_start).count();
        if (r & SiteMoved) {
            if (!g.site_tid) { g.site_tid = d.site_tid; g.site_lr_was = d.site_lr_was; g.site_lr_now = d.site_lr_now; }
            g.site_waited = true;
            if (since_ms >= (long long)cfg.site_wait_ms) r &= ~(unsigned)SiteMoved;
        }
        g.prechecks++;
        const double us = std::chrono::duration<double, std::micro>(SC::now() - t0).count();
        if (!r || *used_us + us >= (double)cfg.precheck_us || (max_ms && since_ms >= (long long)max_ms)) { *used_us += us; return; }
        g.wakes++;
        ops.spin();
    }
}
template<class O> void precheck_(O&, const View*, const Cfg&, GateResult&, std::chrono::steady_clock::time_point, unsigned, double*, long) {}

template<class Ops> GateResult gate(Ops& ops, Mode mode, const View* saved, unsigned max_tries, const Cfg& cfg, unsigned max_ms = 0)
{
    const auto t_start = std::chrono::steady_clock::now();
    GateResult g;
    if (mode == Mode::Wait) prewait_(ops, saved, cfg, g, 0);
    if (mode == Mode::Off) { g.ok = ops.suspend(); g.suspended = g.ok; g.freezes = g.ok ? 1u : 0u; g.reasons = g.ok ? 0u : (unsigned)PauseFailed; g.verdict.reasons = g.reasons; return g; }
    // With a time bound the bound is the only limit: a tries limit (2000 tries at ~4.4 ms was ~9 s) would end a commit gate before its budget and misstate why
    const unsigned limit = max_ms ? 0xFFFFFFFFu : (max_tries ? max_tries : 1);
    double precheck_used_us = 0;
    for (unsigned t = 1; t <= limit; t++) {
        g.tries = t;
        if (mode == Mode::Wait) precheck_(ops, saved, cfg, g, t_start, max_ms, &precheck_used_us, 0);
        if (!ops.suspend()) { g.ok = false; g.reasons = PauseFailed; g.verdict.reasons = PauseFailed; g.suspended = false; return g; }
        g.suspended = true; g.freezes = t;
        ops.sample(&g.view);
        g.verdict = judge(g.view, saved, cfg);
        g.reasons = g.verdict.reasons;
        if (t == 1) { g.first_reasons = g.reasons; g.first_notes = g.verdict.notes; }
        if (g.verdict.site_moved && !g.site_tid) { g.site_tid = g.verdict.site_tid; g.site_lr_was = g.verdict.site_lr_was; g.site_lr_now = g.verdict.site_lr_now; }
        if (g.reasons & SiteMoved) {
            g.site_waited = true;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t_start).count() >= (long long)cfg.site_wait_ms) {
                g.reasons &= ~(unsigned)SiteMoved; g.verdict.reasons = g.reasons; g.site_gave_up = true; }
        }
        if (mode == Mode::Log || g.reasons == 0) { g.ok = true; return g; }
        if (t == limit) break;
        if (max_ms && std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t_start).count() >= (long long)max_ms) break;
        if (!ops.resume()) { g.ok = false; g.reasons |= PauseFailed; g.verdict.reasons = g.reasons; return g; }
        g.suspended = false;
        ops.yield();
        if (g.reasons & RsxPending) {   // blocked by the RSX: let it run unfrozen until it is idle (or the time bound)
            while (rsx_busy_(ops, 0)) {
                g.unfrozen_waits++;
                if (max_ms && std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t_start).count() >= (long long)max_ms) break;
                ops.yield();
            }
        }
    }
    g.ok = false;   // Wait mode ran out of tries: threads are still suspended, the caller resumes them and fails loudly with the reasons
    return g;
}

// (JSP_SNAPSHOT_GATE=ev): the same rule, decided by the same FROZEN judge, but the waiting is
// event-driven instead of resume + Sleep(1) + resuspend (soak-s2: 3-4 tries per commit, ~3 ms each).
//   1. s0 = ops.seq() (the runtime's gate sequence, bumped by patch 7914 at the events that end a block), then an UNFROZEN precheck: ops.presample() reads the
//      fixed tables only (thread markers, lwmutex / syscall tables, put/get, flip scalars, SPURS arrays) and NOT the fiber lists (a linked-list walk while the
//      threads run can follow a node being freed): fiber_valid = 0, so FiberAway is not judged here. The precheck is judged against a scratch copy of the roles,
//      so it never changes the learned membership.
//   2. precheck blocked: ops.wait(s0, timeout_ms) sleeps until the sequence moves (or returns at once if it already did since s0); a timeout is counted
//      (wake_timeouts: a missed bump site, the safety bound is a workaround to replace until it is 0).
//   3. precheck clean: freeze once, sample, frozen judge (identical to gate()). Clean = return suspended. Not clean: read s1 = ops.seq() WHILE STILL FROZEN (
//      s0 predates the precheck, so with frequent bumps a wait on s0 returned at once and the gate re-froze back to back), resume and wait on s1: for a reason only the
//      freeze can see (FiberAway) the full timeout (a fiber entering its wait bumps it); for a reason the precheck missed (a race: clean precheck, dirty frozen) a bounded
//      retry_wait_ms. Only a bump after the frozen sample ends the wait early (the other threads were stopped at s1); one that already happened by the time
//      the wait starts is counted in retry_immediate.
// Loud stops: lim.max_freezes (JSP_SNAPSHOT_SYNC_TRIES) frozen judges -> ok=false, suspended (as gate()); lim.max_wakes waits -> ok=false, wake_stop, NOT
// suspended (the caller's resume is a no-op). A wrong precheck can only cost a freeze: it never admits a capture. (An earlier flip pump was removed, see patch 7914.)
// Ops: everything gate() needs plus uint32_t seq(), void presample(View*), bool wait(uint32_t s0, unsigned ms) (false = timed out).
struct EvLimits { unsigned max_freezes = 2000, max_wakes = 20000, timeout_ms = 4, retry_wait_ms = 2; };
constexpr unsigned kFreezeOnlyReasons = FiberAway;   // judged only on the frozen sample (presample leaves fiber_valid = 0)
template<class Ops> GateResult gate_ev(Ops& ops, Mode mode, const View* saved, const EvLimits& lim, const Cfg& cfg)
{
    GateResult g;
    if (mode != Mode::Wait) {   // Off / Log: one freeze, exactly as gate()
        g = gate(ops, mode, saved, 1, cfg);
        return g;
    }
    Roles scratch; Cfg pc = cfg;
    View pv;
    const unsigned max_freezes = lim.max_freezes ? lim.max_freezes : 1;
    bool woke_prev = false; unsigned prev_reasons = 0;
    for (;;) {
        const uint32_t s0 = ops.seq();
        if (cfg.roles) { scratch = *cfg.roles; pc.roles = &scratch; }
        ops.presample(&pv);
        const Verdict pd = judge(pv, saved, pc);
        g.prechecks++;
        if (g.prechecks == 1) { g.first_reasons = pd.reasons; g.first_notes = pd.notes; }
        if (pd.reasons) {
            if (g.wakes >= lim.max_wakes) { g.ok = false; g.wake_stop = true; g.suspended = false; g.reasons = pd.reasons; g.verdict = pd; g.view = pv; return g; }
            if (woke_prev) for (int b = 0; b < 16; b++) if (pd.reasons & prev_reasons & (1u << b)) g.persist[b]++;   // woken by a bump, same reason still there
            g.wakes++;
            const bool woken = ops.wait(s0, lim.timeout_ms);
            if (!woken) { g.wake_timeouts++; for (int b = 0; b < 16; b++) if (pd.reasons & (1u << b)) g.timeout_by[b]++; }
            woke_prev = woken; prev_reasons = pd.reasons;
            continue;
        }
        woke_prev = false;
        if (!ops.suspend()) { g.ok = false; g.reasons = PauseFailed; g.verdict.reasons = PauseFailed; g.suspended = false; return g; }
        g.suspended = true; g.freezes++; g.tries = g.freezes;
        ops.sample(&g.view);
        g.verdict = judge(g.view, saved, cfg);
        g.reasons = g.verdict.reasons;
        if (g.reasons == 0) { g.ok = true; return g; }
        if (g.freezes >= max_freezes) { g.ok = false; return g; }   // suspended, as gate(): the caller resumes and fails loudly
        const uint32_t s1 = ops.seq();   // while frozen: nothing bumps between the frozen sample and here
        if (!ops.resume()) { g.ok = false; g.reasons |= PauseFailed; g.verdict.reasons = g.reasons; return g; }
        g.suspended = false;
        if (g.reasons & kFreezeOnlyReasons) {
            if (g.wakes >= lim.max_wakes) { g.ok = false; g.wake_stop = true; return g; }
            g.wakes++;
            if (ops.seq() != s1) g.retry_immediate++;
            if (!ops.wait(s1, lim.timeout_ms)) g.wake_timeouts++;
        } else {
            g.retry_waits++;
            if (ops.seq() != s1) g.retry_immediate++;
            if (!ops.wait(s1, lim.retry_wait_ms)) g.retry_timeouts++;
        }
    }
}

// Per-tick views for the incremental mode: a rollback to tick T compares the drift notes with the view taken when T was committed.
// Fixed storage so nothing allocates while the threads are frozen.
struct ViewLog {
    static constexpr uint32_t N = 64;
    uint32_t tick[N]; View v[N]; bool used[N] = {}, clean[N] = {};
    // clean = the capture of that tick had no blocking reason (a rollback may only return to a clean tick; with the gate in Wait mode every stored tick is)
    void put(uint32_t t, const View& x, bool is_clean = true) { const uint32_t i = t % N; tick[i] = t; v[i] = x; used[i] = true; clean[i] = is_clean; }
    const View* find(uint32_t t) const { const uint32_t i = t % N; return used[i] && tick[i] == t ? &v[i] : nullptr; }
    bool is_clean(uint32_t t) const { const uint32_t i = t % N; return used[i] && tick[i] == t && clean[i]; }
    void clear() { for (uint32_t i = 0; i < N; i++) used[i] = false; }
};

}  // namespace jsy
