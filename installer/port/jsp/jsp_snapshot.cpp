// jsp_snapshot.cpp -- DIAGNOSTIC (rollback prototype, route A "whole guest memory"): JSP_SNAPSHOT=1.
// Off by default: jsp_snapshot_init() returns false when the switch is unset, jsp_fight_hash.cpp then never calls in (zero cost).
// Needs JSP_FIGHT_HASH (per-tick hash lines are the comparison) and JSP_ANCHOR_PRESSES via FIGHT_PRESSES=anchor (tick-keyed presses, so a
// replayed tick sees the same buttons).
//   JSP_SNAPSHOT=1
//   JSP_SNAPSHOT_AT=S          hash tick k at which the first snapshot is taken (state after scene pass S, @0x0014EA68 return)
//   JSP_SNAPSHOT_K=60,300,1800 rounds: save at S, run K ticks, restore, replay K ticks, compare per-tick hashes; the next round
//                              saves at S+K (after the replay) and continues. The hash window must cover S+1..S+sum(K)
//                              (JSP_FIGHT_HASH_START=S+1, JSP_FIGHT_HASH>=sum(K)); the module refuses otherwise.
//   JSP_SNAPSHOT_SUSPEND=1     (default) suspend every other host thread while memory is copied (Windows only; census logged)
//   JSP_SNAPSHOT_LS=0          1 = also save/restore the SPU local-store windows 0x30000000..0x3FFFFFFF
//   JSP_SNAPSHOT_DIRTY=1       per tick of pass 1: pages that changed since the previous tick (shadow compare, costs a full pass)
//   JSP_SNAPSHOT_EXCLUDE=a+n,..  extra guest ranges left out of the save (bisect a hang or a mismatch by region without a rebuild)
//   JSP_SNAPSHOT_WW=1          incremental mode: the guest reservation carries MEM_WRITE_WATCH
//                              (main.cpp), the first save is a full baseline, then EVERY tick commits only the pages GetWriteWatch
//                              reports into a ring of per-tick undo pages (jsp_snapshot_ring.h); a restore rolls back K ticks
//                              through the ring. =2 also compares the whole shadow with memory after each commit/rollback (missed
//                              writers; costs a full pass). JSP_SNAPSHOT_WW_DEPTH=10 (max K), JSP_SNAPSHOT_WW_POOL=20480 (undo pages).
//                             
//   JSP_SNAPSHOT_SYNC=1        (default) census of the HLE kernel sync objects at every save/commit/restore/rollback, logged as `[snap] sync ...`
//                              =0 off, =2 also WAIT until it is idle (nothing held, no event in flight, semaphore counts equal to
//                              nothing in transit: no live-held lock, no woken receiver/waiter, no thread inside a lock call) before copying, else
//                              the capture fails loudly. JSP_SNAPSHOT_SYNC_TRIES=2000 = the loud stop; JSP_SNAPSHOT_SYNC_STRICT=1 also waits for
//                              threads running guest code or unclassified HLE calls (likely never idle).
//                              Not run yet.
//   cost switches (each keeps the earlier path as the default, for A/B comparison):
//   JSP_SNAPSHOT_THREADS=toolhelp|registry  how the freeze finds the host threads: a Toolhelp listing + OpenThread per tick (default) or the self-registering
//                              table of jsp_thread_registry.cpp (read at process start; audited against Toolhelp at the baseline and every
//                              JSP_SNAPSHOT_THREADS_AUDIT=60 ticks (0 = baseline only; a baseline where the TLS callback registered (almost) nobody stops the run),
//                              THREAD-NOT-REGISTERED / REGISTRY-FULL / TLS-CALLBACK-NOT-DELIVERED stop the run)
//   JSP_SNAPSHOT_GATE=poll|ev  WW mode quiesce wait: resume + Sleep(1) + resuspend (default) or jsy::gate_ev (unfrozen precheck, WaitOnAddress on patch
//                              7914's gate sequence, one freeze per clean precheck); JSP_SNAPSHOT_GATE_TIMEOUT_MS=4 (safety bound of a wait; a timeout is counted),
//                              JSP_SNAPSHOT_GATE_RETRY_MS=2 (bounded wait after a frozen refusal the precheck missed), JSP_SNAPSHOT_GATE_WAKES=20000 (loud stop)
//                              (JSP_SNAPSHOT_GATE_PUMP was removed: it ran the guest's flip handler on the main thread, see patch 7914)
//   JSP_SNAPSHOT_BIRTHGATE=0 (registry only) keeps the registry but does not hold born/exiting threads in their TLS callback during a freeze (A/B).
//   JSP_SNAPSHOT_RING=copy2|ptr  undo ring: two copies per committed page (default) or one copy + a page-index swap (jss::RingMode::PagePtr)
// The save point is the return of the scene pass hook (a fixed call site on the main thread). The main thread's ppu_context is saved
// and restored with the memory. ASSUMPTION (checked at every restore by the ctx diff line): the lifted callers above that call site keep
// no state in host C locals that differs between the save and the restore tick.
// Saved: the committed pages of jss::kSaveWindows (main memory 0x00010000.. and, since run snap-1, the LV2 user-memory window
// 0x10000000..0x20000000 that holds the game heap), the thread and callback stacks, optionally the SPU LS.
// Not saved (kept as they are at restore): the HLE inject region (gcm control, stubs), the GCM default context, its command buffer, the
// title's context slot and every GCM IO mapping (RSX-visible main memory: the RSX thread reads them asynchronously), RSX local memory
// 0xC0000000.., the HLE heap / audio / sys_vm windows (their committed size is logged at each save by the `[snap] elsewhere` line).
// Diagnostics added after snap-1 (all behind JSP_SNAPSHOT): the saved range list, the Random object (address, covered or not, state words)
// at save, after restore and at every tick of both passes, and per running thread at a save/restore its tid and module+offset.
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <string>
#include <vector>
#include <algorithm>
#include "jsp_snapshot_core.h"
#include "jsp_snapshot_ring.h"
#include "jsp_snapshot_ops.h"
#include "jsp_snapshot_sync.h"
#include "jsp_snapshot_flow.h"
#include "jsp_thread_registry.h"
#include "jsp_audit_start.h"   // jau::Starter: the background audit worker's start and the synchronous fallback (registry_audit_tick)

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <intrin.h>
#endif

#include "ps3emu/ps3types.h"
#include "ps3emu/nid.h"            // ps3_compute_nid: the HLE imports the sync gate classifies
#include "ppu_recomp.h"

extern "C" uint32_t ppu_hle_inject_base;                  // ps3recomp runtime/memory/vm.h (HLE inject window, 16 MB)
extern "C" uint32_t cellGcm_default_context_ea(void);     // patch 2001
extern "C" uint32_t cellGcm_context_slot_ea(void);        // patch 2001
extern "C" int cellGcm_io_mapping(uint32_t i, uint32_t* ea, uint32_t* size);   // patch 7910: -1 past the table, 0 unused slot, 1 = (ea, size)
struct jsp_vm_stack_alloc { uint32_t next_addr, region_end; };   // same layout as vm_stack_alloc (vm.h)
extern "C" jsp_vm_stack_alloc g_vm_stack_alloc;
// patch 7911: census accessors and the per-thread HLE-call marker (runtime/syscalls/sys_snapshot_census.c, ppu_sysprx.cpp)
extern "C" void ps3_lwmutex_census(const uint8_t* thread_alive, uint32_t* out, uint32_t* first_live_ea, uint32_t* first_stale_ea);
extern "C" void ps3_hle_thread_census(uint32_t* kind, uint32_t* id, uint32_t* obj, uint32_t* lr, uint8_t* alive, uint8_t* unreliable, uint32_t* overflows, uint32_t* unbalanced);
extern "C" volatile int g_ps3_hle_mark_on;
extern "C" void ps3_spurs_census(uint32_t* out);          // patch 7913: out[7] job-chain jobs in flight, HLE queue pending, pm slots busy, jc walkers, jc total, instances, async SPU threads
extern "C" void ps3_fiber_census(uint32_t* out);          // patch 7913: out[9] fibers by state, away, schedulers running, lock held (call on the thread that runs the hook)
extern "C" void cellGcm_flip_census(uint32_t* out);       // patch 7913: out[9] flip status, req/tick seq, queued/reached/completed, direct, pump bits, flip lock
extern "C" volatile long g_ps3_hle_mark_fiber_swaps;   // patch 7912
extern "C" void ps3_syscall_census(int32_t* evq_count, int32_t* evq_wait, int32_t* sem_val, int32_t* sem_wait, uint32_t* mtx_held, uint32_t* rw_held);
extern "C" int jsp_vm_write_watch;                        // main.cpp: 1 = the guest reservation carries MEM_WRITE_WATCH (JSP_SNAPSHOT_WW)
// patch 7914: the gate sequence and its bump sites, the per-site bump counters
struct Ps3GateLine { volatile uint32_t seq; volatile int32_t waiters; volatile uint32_t bumps[12]; uint8_t pad[8]; };   // one 64-byte line of everything the bumps write
extern "C" Ps3GateLine g_ps3_gate_line;
extern "C" void ps3_lwm_census_invalidate(void);   // patch 7914: drop the lwmutex census's page-readability cache
extern "C" volatile unsigned long long g_lwm_rd_hits, g_lwm_rd_vq, g_lwm_rd_uncacheable, g_lwm_rd_vq_ns, g_lwm_rd_invalidates;   // its page-readability cache counters (census profile)
extern "C" void ps3_gate_arm(int on, const uint32_t* park_lrs, int n);
extern "C" void ps3_gate_arm_transit(const uint32_t* nids, int n);
extern "C" void ps3_gate_bump_counts(uint32_t* out);   // out[12]

// jsp_fight_hash.cpp: host-side tick counters that must go back with the memory.
struct JspTickState { uint32_t done; uint32_t anc_done; uint8_t anc_set, anc_skip; };
extern "C" void jsp_fight_hash_get_state(JspTickState* s);
extern "C" void jsp_fight_hash_set_state(const JspTickState* s);
extern "C" void jsp_stepper_abort(void);   // jsp_stepper.cpp: the run ended (capture_failed / finish): a replay clock must not stay on
extern "C" void jsp_stepper_replay(int on, uint32_t done);   // jsp_stepper.cpp (JSP_STEP_TURBO fast replay clock, JSP_STEP_CANON input table rewind)

namespace {

using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }
bool g_fw_on = true;   // frozen-interval write check (JSP_SNAPSHOT_FROZEN_WRITES=0 off)

bool g_on = false, g_failed = false, g_suspend = true, g_ls = false, g_dirty = false;
// A failed ResumeThread can leave a process lock held by a frozen worker. No CRT, logging, mutex or cleanup path is safe.
// Stop this opt-in diagnostic process without application callbacks; do not continue with partially restored host state.
// The thread whose ResumeThread failed last (recorded by the resume call) and the Win32 error.
volatile void* g_rf_handle = nullptr; volatile unsigned long g_rf_err = 0, g_rf_count = 0;
#ifdef _WIN32
// Crash note without the CRT (a frozen thread may hold its locks): kernel32 calls only, the text built by hand. Appended to snapshot_fatal.txt in the
// working folder (game/run), so a run that ends here says why instead of vanishing.
void write_fatal_note()
{
    char b[160]; int n = 0;
    auto put = [&](const char* t) { while (*t && n < (int)sizeof b - 1) b[n++] = *t++; };
    auto hex = [&](unsigned long long v) { char d[17]; int k = 0; do { d[k++] = "0123456789ABCDEF"[v & 15]; v >>= 4; } while (v && k < 16); put("0x"); while (k) { if (n < (int)sizeof b - 1) b[n++] = d[--k]; } };
    put("[snap] FATAL resume failed: tid="); hex(g_rf_handle ? GetThreadId((HANDLE)g_rf_handle) : 0);
    put(" err="); hex(g_rf_err); put(" failed_resumes="); hex(g_rf_count); put("\r\n");
    HANDLE f = CreateFileA("snapshot_fatal.txt", FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) { DWORD w = 0; WriteFile(f, b, (DWORD)n, &w, nullptr); FlushFileBuffers(f); CloseHandle(f); }
    HANDLE e = GetStdHandle(STD_ERROR_HANDLE);
    if (e && e != INVALID_HANDLE_VALUE) { DWORD w = 0; WriteFile(e, b, (DWORD)n, &w, nullptr); }
}
#endif
[[noreturn]] void resume_failed()
{
#ifdef _WIN32
    write_fatal_note();
    TerminateProcess(GetCurrentProcess(), 0xE0534E50u); // snapshot resume failure; deliberately no stdio diagnostic
    __fastfail(7); // FAST_FAIL_FATAL_APP_EXIT: fallback if self-termination unexpectedly returns
#else
    std::_Exit(126); // unreachable in the non-Windows adapter, whose resume has no thread operations
#endif
}
void report();
void capture_failed(const char* what, unsigned failures = 0)
{
    g_failed = true;
    g_on = false; // no later save/load may use a partially collected or unverifiable history
    g_ps3_hle_mark_on = 0;   // the HLE-call marker stops with the driver
    jsp_stepper_abort();     // two atomic stores, safe here
    std::fprintf(stderr, "[snap] FAILED %s failures=0x%X\n", what, failures);
    report();
}
jsy::Mode g_sync = jsy::Mode::Log;   // JSP_SNAPSHOT_SYNC
unsigned g_sync_tries = 2000;       // JSP_SNAPSHOT_SYNC_TRIES
unsigned g_sync_ms = 0;             // JSP_SNAPSHOT_SYNC_MS: wall-time budget of the commit gates since the last budget reset (0 = none; rollback mode sets kGateBudgetMs of jsp_net_game.cpp)
double g_gate_used_ms = 0;          // gate time (waits, freezes, resumes) summed since the last jsp_snapshot_ext_budget_reset(): ONE budget across all gates between two session updates
void (*g_idle_hook)() = nullptr;     // jsp_snapshot_ext_set_idle_hook(): called every ~20 ms from the gate's waits so the network is serviced while a commit waits (main thread only)
Clock::time_point g_idle_last = Clock::now();
unsigned gate_budget_ms()            // what is left of g_sync_ms; 1 when used up (the gate then tries once more and fails loudly)
{
    if (!g_sync_ms) return 0;
    return g_gate_used_ms >= (double)g_sync_ms ? 1u : std::max(1u, (unsigned)((double)g_sync_ms - g_gate_used_ms));   // never 0 (= unbounded) for a fractional remainder
}
void idle_service()
{
    if (!g_idle_hook) return;
    if (std::chrono::duration<double, std::milli>(Clock::now() - g_idle_last).count() < 20.0) return;
    g_idle_last = Clock::now(); g_idle_hook();
}
jsy::View g_view_save;              // sync census at the full-mode save
jsy::Cfg g_cfg;                     // classifier tables + strict flag + the calling thread
unsigned g_last_commit_notes = 0;   // note set of the last logged commit
jsy::ViewLog g_views;               // incremental mode: census at each commit, by tick
jsy::Roles g_roles;                 // frame-pipeline park points (JSP_SNAPSHOT_QUIESCE), membership learned at the gates
int g_quiesce = 0;                  // JSP_SNAPSHOT_QUIESCE: 0 off, 1 park points in the log (notes), 2 enforce (implies SYNC=2) and refuse an unclean target
bool g_io = false;                  // JSP_SNAPSHOT_IO=1: save the GCM IO mapping too (see discover())
bool g_save_clean = false;          // full mode: the save's census had no blocking reason
uint32_t g_rebases = 0;   // incremental mode: how many re-baselines after a committed-set change
int g_ww = 0;                     // JSP_SNAPSHOT_WW: 0 off, 1 ring, 2 ring + full verify
bool g_ext = false;               // JSP_SNAPSHOT_EXT=1 (rollback session): no scripted rounds; the netcode drives commit/rollback through jsp_snapshot_ext_*
uint32_t g_ext_hash = 0, g_ext_hash_tick = 0xffffffffu;   // the port's per-tick hash (fight hash `all` component folded to 32 bits) of the last counted pass
uint32_t g_ww_depth = 10; size_t g_ww_pool = 20480;
std::vector<jss::Range> g_user_ex;   // JSP_SNAPSHOT_EXCLUDE
uint32_t g_S = 0;
std::vector<uint32_t> g_ks;
jss::Driver g_drv;
jss::Rec g_rec; uint32_t g_rec_n = 0;
jss::Store g_store, g_shadow;
ppu_context g_saved_ctx;
JspTickState g_saved_ts;
std::vector<size_t> g_dirty_hist;
double g_save_ms = 0, g_restore_ms = 0;
uint32_t g_save_k = 0;
// soak (JSP_SNAPSHOT_SOAK=1): rounds with K drawn from the seed, a gap between them, to the end of the hash window. Wall-clock times are measured for the
// log only (nothing in the game depends on them).
bool g_soak = false; uint64_t g_soak_seed = 1; uint32_t g_soak_gap = 6, g_soak_max = 0;
struct SoakStats {
    uint32_t rounds = 0, equal = 0, mismatch = 0, aborted = 0, k_sum = 0, k_max = 0;
    uint32_t saves = 0; double save_sum = 0, save_max = 0; uint32_t save_max_tick = 0;     // one commit's REAL cost (gate incl. wait + prep + collect + copy + layout + close + audit), per tick (earlier: total+prep)
    uint32_t rbs = 0; double rb_sum = 0, rb_max = 0;                                         // one rollback's REAL cost
    uint32_t recov_n = 0; double recov_sum = 0, recov_max = 0; uint32_t recov_max_k = 0;     // rollback request .. end of the replay, wall
    double recov_max_rb = 0, recov_max_saves = 0, recov_max_w = 0;                           // the worst round's rollback ms and the commits' ms inside its replay (real sums), and the gate wait W inside both
    uint32_t gate_tries_max = 0, gate_tries_sum = 0, gate_retried = 0, first_block[16] = {0}, first_note[16] = {0}, final_note[16] = {0};   // first_block[b]: commits whose FIRST gate sample had reason bit b
    double suspend_sum = 0, suspend_max = 0;                                                      // gate + freeze time inside the transaction (what the retries cost)
} g_ss;
Clock::time_point g_t_rb; bool g_t_rb_valid = false; double g_replay_saves = 0, g_last_rb_ms = 0;   // real per-operation ms (see CostStats), not total+prep
double g_replay_w = 0, g_last_rb_w = 0;   // the gate wait W inside the replay's commits / the rollback (recovery incl. W)
// Toolhelp audits of the thread registry: cost and count; the audit ms of an operation is part of its real cost.
uint32_t g_audits = 0; double g_audit_sum = 0, g_audit_max = 0, g_audit_pending_ms = 0;
// cost switches (see the header) and the cost counters of the `[snap] ww` lines and the soak line.
bool g_thr_registry = false;        // JSP_SNAPSHOT_THREADS=registry
uint32_t g_audit_every = 60;        // JSP_SNAPSHOT_THREADS_AUDIT (0 = baseline only)
bool g_gate_ev = false;             // JSP_SNAPSHOT_GATE=ev
bool g_gate_bumps = false;          // JSP_SNAPSHOT_GATE_BUMPS=1: arm patch 7914's bump sites under GATE=poll too (measures the bumps' cost alone); implied by GATE=ev
jsy::EvLimits g_ev_lim;             // max_freezes = JSP_SNAPSHOT_SYNC_TRIES, max_wakes = JSP_SNAPSHOT_GATE_WAKES, timeout = JSP_SNAPSHOT_GATE_TIMEOUT_MS, retry_wait = JSP_SNAPSHOT_GATE_RETRY_MS
jss::RingMode g_ring_mode = jss::RingMode::Copy2;   // JSP_SNAPSHOT_RING
double g_layout_ms = 0;             // the last discover() of ww_layout_changed (jsp_snapshot_flow.h runs it before every commit/rollback)
struct CostStats {
    // the REAL per-tick cost the main thread pays = save (gate incl. the wait W, prep, collect, copy) + layout (the flow's discover() before the
    // operation) + close + audit (the Toolhelp audit that ran before it). `real_ex_w` leaves the time blocked on the gate sequence out (the game's other threads run then).
    // save = total + prep (the earlier `save ms/tick`).
    uint32_t n = 0; double real = 0, real_ex_w = 0, save = 0, prep = 0, layout = 0, close = 0, audit = 0, presample = 0, gate_wait = 0, frozen = 0, collect = 0, apply = 0;
    uint32_t prechecks = 0, freezes = 0, freezes_max = 0, wakes = 0, wake_timeouts = 0, retry_waits = 0, retry_timeouts = 0, retry_immediate = 0, threads_max = 0;
    uint32_t persist[16] = {0}, timeout_by[16] = {0};   // per reason bit, see jsy::GateResult
    double real_max = 0;
    static double real_of(double sv, double l, double c, double au) { return sv + l + c + au; }
    void add(double sv, double p, double l, double c, double au, double ps, double w, double f, double co, double ap, const jsy::GateResult& g, uint32_t thr)
    {
        const double r = real_of(sv, l, c, au);
        n++; real += r; real_ex_w += r - w; if (r > real_max) real_max = r; save += sv; prep += p; layout += l; close += c; audit += au; presample += ps; gate_wait += w; frozen += f; collect += co; apply += ap;
        prechecks += g.prechecks; freezes += g.freezes; if (g.freezes > freezes_max) freezes_max = g.freezes;
        wakes += g.wakes; wake_timeouts += g.wake_timeouts; retry_waits += g.retry_waits; retry_timeouts += g.retry_timeouts; retry_immediate += g.retry_immediate; if (thr > threads_max) threads_max = thr;
        for (int b = 0; b < 16; b++) { persist[b] += g.persist[b]; timeout_by[b] += g.timeout_by[b]; }
    }
} g_cost_commit, g_cost_rb;

// ENHANCEMENT (rollback latency, JSP_SNAPSHOT_COPY_THREADS=N, default 3 in rollback mode, 0 = off): the page copies of a commit or a rollback (the ring's jss::PageCopy
// batches: ~3,500 pages, 28 MB a commit in a two-copy match, 2.7 ms commit / 5.2 ms rollback on one thread) are split across N worker threads plus the main thread
// while the other threads are frozen. Every op of a batch touches pages no other op touches (jsp_snapshot_ring.h; host-tested run in reverse order), and the main
// thread returns only after every op ran, so memory after a batch is byte for byte what the serial loop leaves. The workers are exempt from the freeze (freeze_exempt),
// idle on a kernel semaphore between batches (ReleaseSemaphore is one system call: WakeByAddress* could spin on an ntdll wait-table lock that a frozen thread holds),
// and never allocate, log or lock. Claims: one 64-bit word = generation (24 bits) | batch size (20) | next op (20), taken by CAS,
// so a worker late from an older batch can never claim an op of a newer one; the ops of generation g sit in slot g & 1, rewritten only after batch g has finished.
// The main thread copies too: a worker that is slow to wake (or still registering) only means the main thread does more of the batch.
void exempt_add(uint32_t tid);   // with freeze_exempt, below
namespace pcopy {
constexpr int kMaxWorkers = 6; constexpr uint32_t kChunk = 8, kMinOps = 64, kMaxOps = 0xFFFFF;
std::atomic<uint64_t> g_st{0};
std::atomic<const jss::PageCopy*> g_ops[2];
std::atomic<uint32_t> g_done{0};
uint32_t g_gen = 0; int g_workers = 0;
#ifdef _WIN32
HANDLE g_sem = nullptr;
#endif
uint64_t g_batches = 0, g_serial = 0, g_ops_total = 0; std::atomic<uint64_t> g_ops_workers{0};
inline void run_op(const jss::PageCopy& o) { std::memcpy(o.d1, o.s1, jss::kPage); if (o.d2) std::memcpy(o.d2, o.s2, jss::kPage); }
uint32_t drain()   // claims and runs chunks of the current batch until none is left; the ops this thread ran
{
    uint32_t mine = 0;
    for (;;) {
        uint64_t s = g_st.load(std::memory_order_acquire);
        const uint32_t n = (uint32_t)(s >> 20) & 0xFFFFFu, i = (uint32_t)s & 0xFFFFFu;
        if (i >= n) return mine;
        const jss::PageCopy* ops = g_ops[(s >> 40) & 1].load(std::memory_order_acquire);
        const uint32_t c = std::min(kChunk, n - i);
        if (!g_st.compare_exchange_weak(s, s + c, std::memory_order_acq_rel, std::memory_order_acquire)) continue;
        for (uint32_t j = i; j < i + c; j++) run_op(ops[j]);
        g_done.fetch_add(c, std::memory_order_release);
        mine += c;
    }
}
#ifdef _WIN32
DWORD WINAPI worker_main(void*)
{
    for (;;) {
        if (WaitForSingleObject(g_sem, INFINITE) != WAIT_OBJECT_0) return 0;   // a late count only finds an empty (or a newer) batch: harmless
        g_ops_workers.fetch_add(drain(), std::memory_order_relaxed);
    }
}
#endif
int start(int n)   // main thread, outside any freeze (the baseline): the workers live for the process
{
#ifdef _WIN32
    if (!g_sem) g_sem = CreateSemaphoreW(nullptr, 0, 0x7FFFFFFF, nullptr);
    if (!g_sem) return g_workers;
    for (int i = g_workers; i < n && i < kMaxWorkers; i++) {
        DWORD tid = 0;
        HANDLE h = CreateThread(nullptr, 64 * 1024, worker_main, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, &tid);
        if (!h) break;
        exempt_add((uint32_t)tid);   // before the next freeze, which only this thread takes
        SetThreadPriority(h, THREAD_PRIORITY_ABOVE_NORMAL);
        CloseHandle(h); g_workers++;
    }
#else
    (void)n;
#endif
    return g_workers;
}
void run(void*, const jss::PageCopy* ops, size_t n)   // jss::CopyFn, frozen: no allocation, no lock
{
    g_ops_total += n;
    if (!g_workers || n < kMinOps || n > kMaxOps) { for (size_t i = 0; i < n; i++) run_op(ops[i]); g_serial++; return; }
    g_batches++;
    const uint32_t gen = ++g_gen & 0xFFFFFFu;
    g_ops[gen & 1].store(ops, std::memory_order_release);
    g_done.store(0, std::memory_order_relaxed);
    g_st.store(((uint64_t)gen << 40) | ((uint64_t)n << 20), std::memory_order_release);
#ifdef _WIN32
    ReleaseSemaphore(g_sem, g_workers, nullptr);
#endif
    drain();
    while (g_done.load(std::memory_order_acquire) < n) YieldProcessor();
}
}  // namespace pcopy
bool g_audit_exempt = false;   // JSP_SNAPSHOT_AUDIT_EXEMPT (see freeze_exempt)

// ENHANCEMENT (rollback latency, measurement): one `[snap] cost window` line every JSP_SNAPSHOT_COST_WINDOW_S seconds (default 10, 0 = off) and one at the session end
// (jsp_snapshot_ext_guard_summary), with the threads running: what a commit (save), a rollback (restore) and a whole recovery (rollback request .. the save of the
// last re-run tick) cost the main thread, as percentiles. save own = save minus the unfrozen waits (the SPU job drain and the precheck: game work the boundary's
// file/SPU barrier would wait for anyway when no snapshot is taken) and minus the gate-sequence wait. rerun sim/tick = (recovery - restore - re-run saves) / ticks.
struct CostWindow {
    std::vector<float> save, own, restore, rerun, sim;
    double copy_save = 0, copy_rb = 0, freezes_save = 0, freezes_rb = 0; uint32_t rerun_ticks = 0, rerun_ticks_max = 0;
    Clock::time_point t0 = Clock::now(); unsigned secs = 10; bool read = false;
    // rerun tracking: set by a rollback, closed by the commit / hash-only save of the tick the rollback came from
    bool rr_on = false; uint32_t rr_from = 0, rr_ticks = 0; Clock::time_point rr_t0{}; double rr_restore = 0, rr_saves = 0;
    void init() {
        if (read) return; read = true;
        const char* e = std::getenv("JSP_SNAPSHOT_COST_WINDOW_S"); if (e && *e) secs = (unsigned)std::strtoul(e, nullptr, 10);
        for (auto* v : {&save, &own, &restore, &rerun, &sim}) v->reserve(4096);
    }
    static void put(std::vector<float>& v, double x) { if (v.size() < v.capacity()) v.push_back((float)x); }
    static void pct(std::vector<float>& v, char* b, size_t n) {
        if (v.empty()) { std::snprintf(b, n, "-"); return; }
        std::sort(v.begin(), v.end()); double s = 0; for (float x : v) s += x;
        auto at = [&](double q) { return (double)v[std::min(v.size() - 1, (size_t)(q * (double)(v.size() - 1) + 0.5))]; };
        std::snprintf(b, n, "p50=%.2f p90=%.2f p99=%.2f max=%.2f mean=%.2f", at(0.5), at(0.9), at(0.99), (double)v.back(), s / (double)v.size());
    }
    void note_op(bool commit, double real, double waits, double copy, uint32_t freezes) {
        init();
        if (commit) { put(save, real); put(own, real - waits); copy_save += copy; freezes_save += freezes; if (rr_on) rr_saves += real; }
        else { put(restore, real); copy_rb += copy; freezes_rb += freezes; }
    }
    void rollback_begin(uint32_t from, uint32_t to) { rr_on = true; rr_from = from; rr_ticks = from - to; rr_t0 = Clock::now(); rr_restore = 0; rr_saves = 0; }
    void rollback_done(double real) { if (rr_on) rr_restore = real; }
    void rollback_failed() { rr_on = false; }
    void saved(uint32_t k, bool snapshot) {   // a commit (after note_op) or a hash-only save of tick k
        (void)snapshot;
        if (!rr_on || k < rr_from) return;
        rr_on = false; init();
        const double ms = ms_since(rr_t0);
        put(rerun, ms); put(sim, rr_ticks ? (ms - rr_restore - rr_saves) / rr_ticks : 0.0);
        rerun_ticks += rr_ticks; if (rr_ticks > rerun_ticks_max) rerun_ticks_max = rr_ticks;
    }
    void maybe_print(bool force) {
        init();
        if (!secs && !force) return;
        const double el = ms_since(t0) / 1000.0;
        if (!force && el < (double)secs) return;
        if (save.empty() && restore.empty()) { t0 = Clock::now(); return; }
        char a[160], b[160], c[160], d[160], e[160];
        const size_t ns = save.size(), nr = restore.size(), nrr = rerun.size();
        pct(save, a, sizeof a); pct(own, b, sizeof b); pct(restore, c, sizeof c); pct(rerun, d, sizeof d); pct(sim, e, sizeof e);
        const uint64_t ow = pcopy::g_ops_workers.load();
        std::fprintf(stderr, "[snap] cost window secs=%.1f%s | saves=%zu ms %s | save own (excl. job/precheck wait) %s | copy mean %.3f, freezes/save %.2f | restores=%zu ms %s | copy mean %.3f, freezes/restore %.2f | "
                     "reruns=%zu ticks mean %.2f max %u, recovery ms %s | sim ms/tick %s | copy_threads=%d batches=%llu serial=%llu worker_share=%.0f%% precheck_us=%u audit_exempt=%d\n",
                     el, force ? " (end)" : "", ns, a, b, ns ? copy_save / (double)ns : 0.0, ns ? freezes_save / (double)ns : 0.0, nr, c, nr ? copy_rb / (double)nr : 0.0, nr ? freezes_rb / (double)nr : 0.0,
                     nrr, nrr ? (double)rerun_ticks / (double)nrr : 0.0, rerun_ticks_max, d, e, pcopy::g_workers, (unsigned long long)pcopy::g_batches, (unsigned long long)pcopy::g_serial,
                     pcopy::g_ops_total ? 100.0 * (double)ow / (double)pcopy::g_ops_total : 0.0, g_cfg.precheck_us, g_audit_exempt ? 1 : 0);
        for (auto* v : {&save, &own, &restore, &rerun, &sim}) v->clear();
        copy_save = copy_rb = freezes_save = freezes_rb = 0; rerun_ticks = rerun_ticks_max = 0; t0 = Clock::now();
    }
} g_cw;
bool g_pass1 = false;   // between a save and its restore
bool g_active = false;  // from the first save on (per-tick rng lines)
std::vector<jss::Range> g_keep;   // the saved set of the current round (for the coverage checks)
std::vector<jss::Range> g_keep_ex;   // the exclusion list of the baseline (it must stay the same in rollback mode)
std::vector<jss::Range> g_keep_raw, g_pending_raw;   // committed ranges before the exclusions: the last accepted set / the last discovery
std::vector<jss::Range> g_ex_exact, g_pres;   // the exclusions as discovered; the exact excluded bytes that lie inside saved pages
std::vector<uint8_t> g_pres_buf;
// A restore/rollback rewrites whole pages; the excluded bytes inside them (the title's GCM context slot on page 0x0096E000, 4 bytes among per-frame SPURS state) must stay as they are.
void pres_prepare(const std::vector<jss::Range>& keep) { g_pres = jss::preserved_bytes(keep, g_ex_exact); g_pres_buf.assign((size_t)jss::total(g_pres), 0); }
void pres_save() { size_t o = 0; for (const jss::Range& r : g_pres) { std::memcpy(g_pres_buf.data() + o, vm_base + r.a, r.n); o += r.n; } }
void pres_apply() { size_t o = 0; for (const jss::Range& r : g_pres) { std::memcpy(vm_base + r.a, g_pres_buf.data() + o, r.n); o += r.n; } }

// The Random object : pointer word in .data, object 0x28 bytes, xorshift128 state u32[4] at +0x14.
constexpr uint32_t kRngPtr = 0x009512A4u;
void rng_line(const char* what, uint32_t k)
{
    const uint32_t P = vm_read32(kRngPtr);
    if (!P) { std::fprintf(stderr, "[snap] rng %s k=%u P=0\n", what, k); return; }
    std::fprintf(stderr, "[snap] rng %s k=%u P=0x%08X %s s=%08X %08X %08X %08X\n", what, k, P,
                 jss::covers(g_keep, P, 0x28) ? "saved" : "NOT-SAVED", vm_read32(P + 0x14), vm_read32(P + 0x18), vm_read32(P + 0x1C), vm_read32(P + 0x20));
}

// ENHANCEMENT (rollback latency): host threads of this module that the freeze leaves running. They never touch guest memory outside what the frozen main thread
// hands them, and no frozen-path step waits for them to be suspended: the parallel page copy workers (always exempt: they do the copy while the others are frozen)
// and the background thread audit (JSP_SNAPSHOT_AUDIT_EXEMPT, default 1 in rollback mode: frozen inside its Toolhelp system-wide listing, GetThreadContext waits
// until the listing call returns to user mode, tens of ms). Set on the main thread before any freeze that could see the thread; read during the listing.
constexpr int kExempt = 8;
std::atomic<uint32_t> g_exempt_tid[kExempt];
void exempt_add(uint32_t tid) { for (auto& e : g_exempt_tid) { uint32_t z = 0; if (e.compare_exchange_strong(z, tid)) return; } }
bool freeze_exempt(unsigned long tid) { for (const auto& e : g_exempt_tid) { const uint32_t x = e.load(std::memory_order_relaxed); if (!x) return false; if (x == tid) return true; } return false; }

struct Th { void* h; unsigned long tid; bool paused = false, context = false; uintptr_t ip = 0; bool gone = false; };
// Why the last enumeration / freeze failed (raw numbers; formatted only after the threads run again). step: 1 snapshot, 2 enumeration end, 3 OpenThread, 4 SuspendThread, 5 GetThreadContext.
// run soak-s1 stopped with a bare `failures=0x1` at k=3943, right after cellAudio's thread reported a failed endpoint, i.e. a thread was coming or going.
struct PauseFail { int step = 0; unsigned long tid = 0, err = 0; };
struct Mod { uintptr_t base, size; char name[40]; };

#ifdef _WIN32
std::vector<Mod> collect_modules()
{
    std::vector<Mod> v;
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (s == INVALID_HANDLE_VALUE) return v;
    MODULEENTRY32 m; m.dwSize = sizeof m;
    for (BOOL ok = Module32First(s, &m); ok; ok = Module32Next(s, &m)) {
        Mod x; x.base = (uintptr_t)m.modBaseAddr; x.size = m.modBaseSize;
        std::snprintf(x.name, sizeof x.name, "%s", m.szModule);
        v.push_back(x);
    }
    CloseHandle(s);
    return v;
}
std::vector<Th> collect_threads(bool* complete, PauseFail* why = nullptr, unsigned* gone = nullptr)
{
    *complete = true;
    std::vector<Th> v;
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (s == INVALID_HANDLE_VALUE) { *complete = false; if (why) *why = {1, 0, GetLastError()}; return v; }
    THREADENTRY32 t; t.dwSize = sizeof t;
    const DWORD pid = GetCurrentProcessId(), self = GetCurrentThreadId();
    BOOL found = Thread32First(s, &t);
    if (!found) { *complete = GetLastError() == ERROR_NO_MORE_FILES; if (!*complete && why) *why = {2, 0, GetLastError()}; }
    for (; found; found = Thread32Next(s, &t)) {
        if (t.th32OwnerProcessID != pid || t.th32ThreadID == self || freeze_exempt(t.th32ThreadID)) continue;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, t.th32ThreadID);
        if (h) v.push_back({h, t.th32ThreadID});
        else {
            const DWORD e = GetLastError();
            if (e == ERROR_INVALID_PARAMETER) { if (gone) (*gone)++; continue; }   // the thread ended after the snapshot listed it: an ended thread cannot touch memory, nothing to freeze
            *complete = false; if (why && !why->step) *why = {3, t.th32ThreadID, e};
        }
    }
    if (GetLastError() != ERROR_NO_MORE_FILES) { *complete = false; if (why && !why->step) *why = {2, 0, GetLastError()}; }
    CloseHandle(s);
    return v;
}
#else
std::vector<Mod> collect_modules() { return {}; }
std::vector<Th> collect_threads(bool* complete, PauseFail* = nullptr, unsigned* = nullptr) { *complete = true; return {}; }
#endif

// Census of the suspended threads: where is each one? In a kernel/wait module (ntdll, win32u, kernelbase) = parked, in anything else = was running.
struct Census { int n = 0, waiting = 0, running = 0; char run_in[1024] = {0}; size_t rl = 0; };   // formatted only after threads resume

// Committed, writable pages of [lo, hi) from VirtualQuery (no allocation inside the loop apart from the push_back).
void committed_ranges(uint32_t lo, uint64_t hi, std::vector<jss::Range>* out, bool* complete = nullptr)
{
#ifdef _WIN32
    uint64_t a = lo;
    while (a < hi) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(vm_base + a, &mbi, sizeof mbi)) { if (complete) *complete = false; break; }
        const uint64_t rb = (uint64_t)((uint8_t*)mbi.BaseAddress - vm_base), re = rb + mbi.RegionSize;
        const bool rw = mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
        const uint64_t s = std::max<uint64_t>(a, rb), e = std::min<uint64_t>(hi, re);
        if (rw && s < e) out->push_back({(uint32_t)s, (uint32_t)(e - s)});
        if (re <= a) { if (complete) *complete = false; break; }
        a = re;
    }
#else
    out->push_back({lo, (uint32_t)(hi - lo)});   // host tests / non-Windows: the whole window (never used by the game build)
#endif
}

std::vector<jss::Range> discover(uint64_t* excluded_bytes, bool* complete)
{
    *complete = true;
    std::vector<jss::Range> in;
    for (const jss::Window& w : jss::kSaveWindows) committed_ranges(w.lo, w.hi, &in, complete);
    // stacks: the bump-allocated thread stacks and the callback stacks at the top of the stack region (vm.h VM_CB_STACK_*)
    const uint32_t sb = 0xD0000000u, cb = 0xDF000000u, sn = g_vm_stack_alloc.next_addr;
    if (sn > sb && sn <= cb) committed_ranges(sb, sn, &in, complete);
    committed_ranges(cb, cb + 0x01000000ull, &in, complete);
    if (g_ls) committed_ranges(0x30000000u, 0x40000000ull, &in, complete);
    g_pending_raw = jss::normalise(in);   // what the frozen walk compares against
    std::vector<jss::Range> ex;
    ex.push_back({ppu_hle_inject_base, 0x01000000u});
    if (const uint32_t ctx = cellGcm_default_context_ea()) {
        ex.push_back({ctx, 16});
        const uint32_t b = vm_read32(ctx), e = vm_read32(ctx + 4);
        // the context's end is b + size - 4 (cellGcmSys.c): the word at `end` is the wrap JUMP, part of the ring, so the exclusion is [b, e + 4)
        if (b && e > b && e - b < 0x10000000u) ex.push_back({b, e - b + 4});
    }
    if (const uint32_t slot = cellGcm_context_slot_ea()) ex.push_back({slot, 4});
    ex.insert(ex.end(), g_user_ex.begin(), g_user_ex.end());
    // the IO mapping (J-Stars: cellGcmInit ioAddr 0x16300000, 58 MB) is NOT only GPU data. In run ww8-dump the video thread's first frame after a rollback
    // walked objects at 0x193FFB50 / vtable 0x16677830 (null calls from func_00525FFC, tid 19): game objects live there, and leaving them at their later state
    // while everything else went back is a split memory image. JSP_SNAPSHOT_IO=1 saves the mapping (the default context, its command buffer and the title's slot
    // stay excluded, above); the gate's RSX check (JSP_SNAPSHOT_QUIESCE_RSX=1, GCM put == get) is what makes overwriting command-buffer segments safe.
    for (uint32_t i = 0; !g_io; i++) {   // RSX-visible main memory (J-Stars: cellGcmInit's IO region inside its heap)
        uint32_t ea = 0, size = 0;
        const int r = cellGcm_io_mapping(i, &ea, &size);
        if (r < 0) break;
        if (r > 0) ex.push_back({ea, size});
    }
    g_ex_exact = jss::normalise(ex);
    std::vector<jss::Range> keep = jss::plan_save(in, ex);
    if (excluded_bytes) *excluded_bytes = jss::total(jss::page_align(in)) - jss::total(keep);   // committed bytes the exclusions removed
    return keep;
}

// Thread enumeration/storage is prepared before freezing. Freeze only records raw IPs; formatting/logging is after resume.
// Suspending workers does not rewind their host stacks: a proven simulation safe point is still required for shipping rollback.
struct Suspended {
    std::vector<Th> th; bool complete = true;
    PauseFail fail; unsigned gone_n = 0, retries = 0;
    uint64_t ppu_alive_at_listing = 0;   // which PPU thread ids were alive when the host threads were listed
    // Alive PPU threads as a bit set (allocation-free table read). A PPU thread started or ended between the listing and the freeze changes it. Host-only threads
    // (cellAudio's wasapi opener, SPU job threads) are NOT PPU threads: they are covered by the ended-thread skip and the retry, not by this check; a host thread CREATED
    // after the listing stays unseen.
    static uint64_t ppu_alive_mask()
    {
#ifdef _WIN32
        uint32_t kd[jsy::kThr], id[jsy::kThr], ob[jsy::kThr], lr[jsy::kThr], ov = 0, ub = 0; uint8_t al[jsy::kThr], un[jsy::kThr];
        ps3_hle_thread_census(kd, id, ob, lr, al, un, &ov, &ub);
        uint64_t m = 0; for (int i = 1; i < jsy::kThr; i++) if (al[i]) m |= jtb::thread_bit((uint32_t)i);
        return m;
#else
        return 0;
#endif
    }
    // (JSP_SNAPSHOT_THREADS=registry): no listing here. reclaim() closes the handles of threads that ended (outside any freeze); the LIVE slots are
    // copied inside suspend(), under the registry's birth gate, into storage reserved here (no allocation while frozen).
    bool reg_frozen = false;
    void prepare()
    {
        fail = PauseFail(); gone_n = 0;
        if (g_thr_registry) {
            jtr::Registry& R = jsp_thread_registry();
            R.reclaim([](void* h) { jsp_thread_registry_close(h); }, [](void* h) { return jsp_thread_registry_ended(h); });
            th.clear(); th.reserve(jtr::kSlots); complete = true;
            return;
        }
        ppu_alive_at_listing = ppu_alive_mask(); th = collect_threads(&complete, &fail, &gone_n);
    }
#ifdef _WIN32
    static bool exited(void* h) { DWORD c = 0; return GetExitCodeThread((HANDLE)h, &c) && c != STILL_ACTIVE; }
#endif
    // A thread that ended between the enumeration and the freeze is skipped (it cannot run guest code or write memory; gone_n counts them); any other failure
    // is named in `fail` and fails the freeze (the caller retries with a fresh enumeration, bounded, then stops loudly). Threads CREATED after the enumeration are
    // not seen: a limitation of the Toolhelp snapshot that every freeze here already had (listing again while frozen would allocate).
    bool suspend()
    {
        if (!complete) return false;
#ifdef _WIN32
        if (g_thr_registry) {   // the listing IS this copy: births wait in their attach callback until resume() (freeze_end)
            jtr::Registry& R = jsp_thread_registry();
            R.freeze_begin([] { jsp_thread_registry_yield(); });
            reg_frozen = true;
            if (R.full.load() || R.handle_failed.load()) { fail = {7, 0, R.full.load() ? 0ul : 1ul}; return false; }
            static jtr::Entry buf[jtr::kSlots];
            bool ovf = false;
            const size_t n = R.copy_live(buf, jtr::kSlots, (uint32_t)GetCurrentThreadId(), &ovf);
            if (ovf) { fail = {7, 0, 2}; return false; }
            th.clear();
            for (size_t i = 0; i < n; i++) if (!freeze_exempt(buf[i].tid)) th.push_back({buf[i].h, buf[i].tid});   // capacity reserved in prepare(); exempt threads stay running
            ppu_alive_at_listing = ppu_alive_mask();
        }
        for (Th& t : th) {
            if (SuspendThread((HANDLE)t.h) == (DWORD)-1) {
                const DWORD e = GetLastError();
                if (exited(t.h)) { t.gone = true; gone_n++; continue; }
                fail = {4, t.tid, e}; return false;
            }
            t.paused = true;
        }
        for (Th& t : th) {
            if (t.gone) continue;
            CONTEXT cx; cx.ContextFlags = CONTEXT_CONTROL;
            if (!GetThreadContext((HANDLE)t.h, &cx)) {
                const DWORD e = GetLastError();
                if (exited(t.h)) { t.gone = true; t.paused = false; gone_n++; continue; }   // killed while suspended: nothing left to resume
                fail = {5, t.tid, e}; return false;
            }
            t.ip = (uintptr_t)cx.Rip; t.context = true;
        }
        if (ppu_alive_mask() != ppu_alive_at_listing) { fail = {6, 0, 0}; return false; }   // a PPU thread started or ended since the listing: this freeze is not of the thread set that exists now
#endif
        return true;
    }
    bool retryable() const { return fail.step != 0 && fail.step != 7; }   // step 7 (the registry is full or lost a thread) is not a race: stop at once
    void log_failure(const char* what, uint32_t k) const
    {
        static const char* const kStep[] = {"?", "CreateToolhelp32Snapshot", "Thread32First/Next", "OpenThread", "SuspendThread", "GetThreadContext", "PPU thread set changed since the listing",
                                            "REGISTRY-FULL (err 0: no free slot, 1: a thread got no handle, 2: more live threads than the copy holds)"};
        std::fprintf(stderr, "[snap] pause failed (%s k=%u): %s tid=%lu GetLastError=%lu threads=%zu ended_since_listed=%u retries=%u\n", what, k, kStep[fail.step < 8 ? fail.step : 0], fail.tid, fail.err, th.size(), gone_n, retries);
    }
    bool resume()
    {
#ifdef _WIN32
        const bool ok = jso::resume_threads(th, [](void* h) {
            if (ResumeThread((HANDLE)h) != (DWORD)-1) return true;
            g_rf_handle = h; g_rf_err = GetLastError(); g_rf_count = g_rf_count + 1;   // read by write_fatal_note (resume_failed)
            return false; });
        if (reg_frozen) { reg_frozen = false; jsp_thread_registry().freeze_end([](std::atomic<uint32_t>* a) { jsp_thread_registry_wake(a); }); }   // births go on
        return ok;
#else
        return true;
#endif
    }
    void census(const std::vector<Mod>& mods, Census* c) const
    {
        for (const Th& t : th) if (t.context) {
#ifdef _WIN32
                c->n++;
                const uintptr_t ip = t.ip;
                const Mod* hit = nullptr;
                for (const Mod& m : mods) if (ip >= m.base && ip < m.base + m.size) { hit = &m; break; }
                const bool wait = hit && (!_stricmp(hit->name, "ntdll.dll") || !_stricmp(hit->name, "win32u.dll") || !_stricmp(hit->name, "kernelbase.dll") || !_stricmp(hit->name, "kernel32.dll"));
                if (wait) c->waiting++;
                else {   // tid:module+offset (offset from the module base, for addr2line/objdump on that module)
                    c->running++;
                    if (c->rl < sizeof c->run_in - 96)
                        c->rl += (size_t)std::snprintf(c->run_in + c->rl, sizeof c->run_in - c->rl, "%lu:%s+0x%llx ", t.tid, hit ? hit->name : "?",
                                                       (unsigned long long)(hit ? ip - hit->base : ip));
                }
#else
            (void)t; (void)mods; (void)c;
#endif
        }
    }
    void close() {
#ifdef _WIN32
        if (!g_thr_registry) for (const Th& t : th) CloseHandle((HANDLE)t.h);   // registry handles stay with the registry (reclaim closes them)
#endif
        th.clear();
    }
    ~Suspended() { resume(); close(); }
};

// the frame pipeline's park sites, from the dump and WAIT lines of run ww8-dump (US disc build). ASSUMPTION: the marker's lr for an HLE/syscall wait is the
// caller's return address, which is what the [WAIT] lines print (lr=0x00644F28 for tid 19); to be confirmed by the first `[snap] sync   park` line of a run.
//  video  @0x00644EB4 (the video server's loop; its q=7 receive returns to 0x00644F28): required, and its queue (the per-frame signal) must be empty.
//  jobs   @0x00538218 the job workers (tid 4-9) waiting for work on their queue.   cri @0x004D3D4C the CRI servers' lwcond wait (tid 10-16; 0x004D4328/0x004D435C
//  are return addresses further up their stacks, not what the marker records: it takes c->lr at the HLE call, i.e. the call site).
//  fiber  @0x00578568 the fiber runner's polling receive (q=2, timeout 20 ms).    A member that is anywhere else at a gate is running a frame's work.
void default_roles(jsy::Roles* r)
{
    r->n = 0;
    if (jsy::Role* x = r->add("video", true, true)) x->add_site(0x00644F28);   // @0x00644F28
    if (jsy::Role* x = r->add("jobs", false, false)) x->add_site(0x00538218);   // @0x00538218
    if (jsy::Role* x = r->add("cri", false, false)) x->add_site(0x004D3D4C);   // @0x004D3D4C: the call to the sys_lwcond_wait stub func_00734110 (tid 10-16); tid 17 runs another loop
    if (jsy::Role* x = r->add("fiber", false, false)) x->add_site(0x00578568);   // @0x00578568
}

// Census of the HLE kernel sync objects. Runs while the other threads are suspended: reads tables, takes no lock, allocates nothing.
// fibers = false (gate_ev precheck, threads running): skip ps3_fiber_census, whose linked-list walk is only safe with the other threads suspended
// (fiber_valid stays 0, so FiberAway is judged on the frozen sample only). Everything else read here is a fixed table or a scalar.
// SPU job server work state from guest memory. The manager (func_005784EC's r30 = *(arg+0x44), constant for the process) is learned once from the job server thread
// (name sp_job_server_spu_thread) while it sits at its receive call site (lr 0x578568, where r30 is that value), checked against guest memory (a record table pointer and a sane
// capacity), then cached. ASSUMPTION: the record pool holds mgr+0x94 records of 12 bytes at *(mgr+0x90) (the ring capacity), state word at +0.
extern "C" void ps3_gcm_walker_view(uint32_t* out);   // patch 7917 (read only)
uint32_t g_walk0[14] = {0}; bool g_walk0_set = false;
uint32_t g_last_gate_reasons = 0; bool g_rsx_hazard = false;
// RSX write guard. Patch 7917 reports every RSX-side store into guest main memory (ea, bytes, kind) to this hook; the walker thread appends to a ring, and the
// commit gate (threads frozen) checks the entries added since the previous commit against the saved ranges. The hazard: the PREVIOUS capture had RSX commands pending and a counted
// write landed in saved memory after it (a restore to that capture would find the guest memory its pending commands wrote, and replay them again).
struct RsxWrite { uint32_t ea, n, kind; std::atomic<uint32_t> seq; };   // seq = index + 1, stored LAST (release): an entry a scanner sees with another seq is not published yet
constexpr uint32_t kRsxWCap = 4096;
RsxWrite g_rsx_wlog[kRsxWCap]; std::atomic<uint32_t> g_rsx_widx{0};
uint32_t g_rg_commits = 0, g_rg_armed = 0, g_rg_hit_commits = 0, g_rg_data = 0, g_rg_label = 0, g_rg_wrapped = 0, g_rg_noted = 0;   // guard summary: commits checked / with the previous capture pending / with a hit; hits by kind; ring wraps; all writes noted
uint32_t g_rsx_wscan = 0, g_rsx_prev_k = 0; bool g_rsx_prev_set = false, g_rsx_prev_pending = false, g_rsx_hazard_logged = false;
extern "C" void (*ps3_rsx_guest_write_hook)(uint32_t ea, uint32_t n, uint32_t kind);   // patch 7917
void rsx_write_hook(uint32_t ea, uint32_t n, uint32_t kind)
{
    const uint32_t i = g_rsx_widx.fetch_add(1, std::memory_order_relaxed); RsxWrite& w = g_rsx_wlog[i % kRsxWCap];
    w.ea = ea; w.n = n; w.kind = kind; w.seq.store(i + 1, std::memory_order_release);
}
   // the walker view when a gate first saw the RSX busy (drain passes since = progress)
extern "C" int ps3_ppu_thread_view(uint32_t tid, uint64_t* out, char* name);   // patch 7916
extern "C" int ps3_ppu_thread_regs(uint32_t tid, uint64_t* io, int set);       // patch 7916
bool g_js_learned_note = false;
uint32_t g_js_mgr = 0, g_js_unlearned = 0;   // g_js_unlearned: commits taken before the manager was learned (no job-server check applied to them)
constexpr uint64_t kSpuJobServerRecvLr = 0x578568;   // @0x578568: event_queue_receive call in func_005784EC (sp_job_server_spu_thread)
void sample_jobserver(jsy::View* v)
{
#ifdef _WIN32
    auto ok_addr = [](uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; };
    if (!g_js_mgr) {
        for (int t = 1; t < jsy::kThr; t++) {
            uint64_t o[5]; char nm[64] = {0};
            if (!ps3_ppu_thread_view((uint32_t)t, o, nm) || std::strcmp(nm, "sp_job_server_spu_thread") != 0) continue;
            uint64_t io[22];
            if (!ps3_ppu_thread_regs((uint32_t)t, io, 0) || io[18] != kSpuJobServerRecvLr) continue;
            const uint32_t m = (uint32_t)io[16];
            if (ok_addr(m) && ok_addr(vm_read32(m + 0x90u)) && vm_read32(m + 0x94u) >= 1 && vm_read32(m + 0x94u) <= 4096) { g_js_mgr = m; g_js_learned_note = true; break; }   // printed by report() after the resume (nothing is formatted while the threads are frozen)
        }
    }
    if (!g_js_mgr) return;
    const uint32_t base = vm_read32(g_js_mgr + 0x90u), cap = vm_read32(g_js_mgr + 0x94u);
    if (!ok_addr(base) || cap < 1 || cap > 4096) return;
    uint32_t n2 = 0;
    for (uint32_t i = 0; i < cap; i++) if (vm_read32(base + i * 12u) == 2) n2++;
    v->js_valid = 1; v->js_bits = vm_read32(g_js_mgr + 0x6Cu); v->js_state2 = n2;
#else
    (void)v;
#endif
}
// ENHANCEMENT (rollback latency, measurement): JSP_SNAPSHOT_CENSUS_PROF=1 times each part of the census (frozen samples only) and prints the means every 300
// samples AFTER the threads run again (report_census_prof, called from the commit path), so nothing is formatted while frozen. Off: one branch per sample.
// ENHANCEMENT (rollback latency, A/B, off by default): JSP_SNAPSHOT_LWM_CACHE=keep. The lwmutex census caches the readability of each guest page it reads (patch 7914);
// by default the cache is dropped before every frozen sample and after every per-tick layout discovery, so every sample re-queries Windows for each page (VirtualQuery on the
// write-watched reservation): 7.6 ms of an 8 ms census, measured (JSP_SNAPSHOT_CENSUS_PROF). With keep it is dropped only when the discovery finds a committed set that differs
// from the accepted one. Residual race (accepted for the A/B): memory holding a live lwmutex decommitted between this tick's unfrozen discovery and the freeze.
inline bool lwm_cache_keep() { static const int v = [] { const char* e = std::getenv("JSP_SNAPSHOT_LWM_CACHE"); return e && !std::strcmp(e, "keep") ? 1 : 0; }(); return v != 0; }
struct CensusProf { bool on = false, read = false; uint64_t n = 0; double part[7] = {0, 0, 0, 0, 0, 0, 0}; } g_cprof;
inline bool census_prof_on() { if (!g_cprof.read) { g_cprof.read = true; const char* e = std::getenv("JSP_SNAPSHOT_CENSUS_PROF"); g_cprof.on = e && e[0] == '1'; } return g_cprof.on; }
void report_census_prof()
{
    if (!g_cprof.on || g_cprof.n < 300) return;
    const double n = (double)g_cprof.n; const double* p = g_cprof.part;
    std::fprintf(stderr, "[snap] census-prof n=%llu ms mean: threads=%.3f lwmutex=%.3f syscalls=%.3f spurs=%.3f fibers=%.3f jobserver=%.3f flip=%.3f\n",
                 (unsigned long long)g_cprof.n, p[0] / n, p[1] / n, p[2] / n, p[3] / n, p[4] / n, p[5] / n, p[6] / n);
    {   // the lwmutex census's page-readability cache (toolkit counters, cumulative): hits, VirtualQuery calls and their time, uncacheable addresses, invalidations
        std::fprintf(stderr, "[snap] census-prof lwm cache: hits=%llu vq=%llu vq_ms=%.3f uncacheable=%llu invalidates=%llu\n", (unsigned long long)g_lwm_rd_hits, (unsigned long long)g_lwm_rd_vq,
                     (double)g_lwm_rd_vq_ns / 1e6, (unsigned long long)g_lwm_rd_uncacheable, (unsigned long long)g_lwm_rd_invalidates);
    }
    g_cprof = CensusProf{}; g_cprof.read = true; g_cprof.on = true;
}
void sample_view(jsy::View* v, bool fibers = true)
{
    *v = jsy::View{};
#ifdef _WIN32
    const bool prof = fibers && census_prof_on();   // frozen samples only (the unfrozen presample passes fibers = false)
    auto t = prof ? Clock::now() : Clock::time_point{};
    auto lap = [&](int i) { if (prof) { g_cprof.part[i] += ms_since(t); t = Clock::now(); } };
    ps3_hle_thread_census(v->t_kind, v->t_id, v->t_obj, v->t_lr, v->t_alive, v->t_unrel, &v->mark_overflows, &v->mark_unbalanced);
    lap(0);
    uint32_t o[7] = {0, 0, 0, 0, 0, 0, 0};
    ps3_lwmutex_census(v->t_alive, o, &v->lwm_first_live, &v->lwm_first_stale);
    v->lwm_tracked = o[0]; v->lwm_live_held = o[1]; v->lwm_abandoned = o[2]; v->lwm_dead = o[3]; v->lwm_stale = o[4]; v->lwm_unreadable = o[5]; v->lwm_held_unowned = o[6];
    lap(1);
    ps3_syscall_census(v->evq_count, v->evq_wait, v->sem_val, v->sem_wait, &v->mtx_held, &v->rw_held);
    lap(2);
    if (ppu_hle_inject_base) {   // GCM control words (runtime/memory: GCM_CONTROL_GUEST_ADDR = inject base + 0x2000): put at +0, get at +4
        v->rsx_put = vm_read32(ppu_hle_inject_base + 0x2000u); v->rsx_get = vm_read32(ppu_hle_inject_base + 0x2004u); v->rsx_valid = 1;
    }
    {   // (patch 7913) SPURS jobs, fibers, flip state. Read-only, no lock, no allocation; the fiber census must run on the thread that runs the hook (its own fiber is not "away")
        uint32_t s[8] = {0}, f[9] = {0}, g[9] = {0};
        ps3_spurs_census(s); lap(3); if (fibers) ps3_fiber_census(f); lap(4); cellGcm_flip_census(g); lap(6);
        v->jobs_valid = 1; v->jobs_inflight = s[0]; v->jq_pending = s[1]; v->pm_busy = s[2]; v->jc_running = s[3]; v->jc_total = s[4]; v->spurs_inst = s[5]; v->async_running = s[6];
        v->fiber_valid = fibers ? 1 : 0; v->fb_total = f[0]; v->fb_ready = f[1]; v->fb_running = f[2]; v->fb_wait_signal = f[3]; v->fb_wait_join = f[4]; v->fb_done = f[5]; v->fb_away = f[6];
        v->fb_sched_running = f[7]; v->fb_lock_held = f[8];
        sample_jobserver(v); lap(5);
        if (prof) g_cprof.n++;
        v->flip_valid = 1; v->flip_status = g[0]; v->flip_req_seq = g[1]; v->flip_tick_seq = g[2]; v->flips_queued = g[3]; v->flips_reached = g[4]; v->flips_completed = g[5];
        v->flip_direct = g[6]; v->gcm_pending = g[7]; v->flip_lock = g[8];
    }
#endif
}

// ENHANCEMENT (rollback latency, A/B: JSP_SNAPSHOT_QUICK=1): most saves found the SPURS jobs or the job server still busy on the first freeze (first_reasons
// 0x12600 on ~90% of depth-2 saves, ~2.6 freezes a save, each a full suspend of ~73 threads plus the census). Two cheap steps around the same frozen judge:
//  - before each freeze, the job words are read UNFROZEN (read-only, as the gate_ev presample does) and the freeze waits, spinning in 50 us steps, until
//    they read idle (bounded: quick_wait_us, default 4000), so the freeze lands after the jobs drained instead of finding them busy;
//  - a frozen sample reads the job words first; when they block, the try is refused right there and the thread, lwmutex and syscall census are skipped
//    (that try is retried in any case; only a full sample can pass the judge, so what is captured is unchanged).
inline bool quick_gate_on() { static const int v = [] { const char* e = std::getenv("JSP_SNAPSHOT_QUICK"); return e && e[0] == '1' ? 1 : 0; }(); return v != 0; }
// JSP_SNAPSHOT_QUICK_SLEEP=1: the unfrozen wait gives its time slice away (Sleep(0)) between reads instead of spinning, in case the spin starves the job threads.
inline bool quick_wait_sleep() { static const int v = [] { const char* e = std::getenv("JSP_SNAPSHOT_QUICK_SLEEP"); return e && e[0] == '1' ? 1 : 0; }(); return v != 0; }
inline unsigned quick_wait_us() { static const unsigned v = [] { const char* e = std::getenv("JSP_SNAPSHOT_QUICK_WAIT_US"); return e && *e ? (unsigned)std::strtoul(e, nullptr, 0) : 4000u; }(); return v; }
// The job words only (no thread walk: the job server's manager is learned by a frozen sample first). True when they would block the judge.
bool quick_jobs_view(jsy::View* v)
{
#ifdef _WIN32
    *v = jsy::View{};
    uint32_t sp[8] = {0};
    ps3_spurs_census(sp);
    v->jobs_valid = 1; v->jobs_inflight = sp[0]; v->jq_pending = sp[1]; v->pm_busy = sp[2]; v->jc_running = sp[3]; v->jc_total = sp[4]; v->spurs_inst = sp[5]; v->async_running = sp[6];
    if (g_js_mgr) sample_jobserver(v);
    const bool jobs = v->jobs_inflight + v->jq_pending != 0, jobsrv = v->js_bits != 0 || v->js_state2 != 0;
    return (g_cfg.block_jobs && (jobs || jobsrv)) || (g_cfg.block_jobserver && jobsrv);
#else
    (void)v; return false;
#endif
}

// jsy::gate adapter: suspend every other thread, sample, and (JSP_SNAPSHOT_SYNC=2) resume and retry until the sync state is idle.
void rsx_write_guard(uint32_t k, uint32_t restore_to, const jsy::View* saved, const jsy::View& now);   // defined after g_ring
void rsx_guard_flush_log();
struct GateOps {
    Suspended& sp; uint32_t k = 0;   // k: the tick, for the retry log
    double sample_ms = 0;   // census cost, summed over the tries of one capture (logged)
    double presample_ms = 0, wait_ms = 0;   // gate_ev: unfrozen prechecks; time blocked in WaitOnAddress (gate_wait, "W")
    Clock::time_point t_frozen{};           // the last successful freeze (frozen_ms = resume - this)
    bool suspend() {
        if (!g_suspend) { t_frozen = Clock::now(); return true; }
        if (quick_gate_on()) {   // unfrozen: let the jobs drain before the freeze (bounded)
            const auto t = Clock::now(); jsy::View qv;
            while (quick_jobs_view(&qv) && ms_since(t) * 1000.0 < (double)quick_wait_us()) { if (quick_wait_sleep()) Sleep(0); else { const auto s0 = Clock::now(); while (ms_since(s0) < 0.05) YieldProcessor(); } }
            presample_ms += ms_since(t);
        }
        for (;;) {   // a thread set that changed under the freeze (a thread listed, then gone or refusing) is retried with a fresh listing, bounded; the cause is logged either way
            if (sp.suspend()) { t_frozen = Clock::now(); return true; }
            if (!sp.retryable() || sp.retries >= 4) return false;
            const unsigned r = sp.retries + 1;
            if (!sp.resume()) resume_failed();
            sp.log_failure("retry", k);
            sp.close(); sp.prepare(); sp.retries = r;
        }
    }
    bool resume() { return sp.resume(); }
    // frozen: a fresh page-readability cache by default (the unfrozen precheck reuses the per-tick one). ENHANCEMENT (rollback latency, A/B):
    // JSP_SNAPSHOT_LWM_CACHE=keep keeps it while the committed layout is unchanged (lwm_cache_keep): measured 7.6 ms of the 8 ms census per sample was this re-query.
    void sample(jsy::View* v) {
        const auto t = Clock::now();
        if (quick_gate_on() && g_sync == jsy::Mode::Wait && quick_jobs_view(v)) { sample_ms += ms_since(t); return; }   // the jobs block: refused without the full census (retried)
        if (!lwm_cache_keep()) ps3_lwm_census_invalidate(); sample_view(v); sample_ms += ms_since(t); }
    void yield() {
#ifdef _WIN32
        Sleep(1);   // gives the parked threads the CPU to finish what they were in the middle of; the exit is the condition, not this time
        idle_service();   // the threads are running here (resumed): the session's network is polled so the peer's grace timer does not run
#endif
    }
    // ENHANCEMENT (rollback latency, jsy::Cfg::precheck_us): the precheck's poll step, threads running: the time slice is offered once, then ~0.1 ms of pause
    // instructions (Sleep(1) is 1-2 ms with timeBeginPeriod(1), longer than most of the waits it would cover).
    void spin() {
#ifdef _WIN32
        const auto s0 = Clock::now();
        SwitchToThread();
        while (ms_since(s0) < 0.1) YieldProcessor();
        idle_service();
#endif
    }
    bool rsx_busy()
    {
        const bool busy = vm_read32(ppu_hle_inject_base + 0x2000u) != vm_read32(ppu_hle_inject_base + 0x2004u);
        if (busy && !g_walk0_set) { ps3_gcm_walker_view(g_walk0); g_walk0_set = true; }
        return busy;
    }   // unfrozen read of the GCM put / get words (the gate's RSX wait)
    // gate_ev (jsp_snapshot_sync.h): sequence read, unfrozen precheck, wait on the sequence
    uint32_t seq() { return __atomic_load_n(&g_ps3_gate_line.seq, __ATOMIC_SEQ_CST); }
    void presample(jsy::View* v) { const auto t = Clock::now(); sample_view(v, false); presample_ms += ms_since(t); }
    bool wait(uint32_t s0, unsigned ms)
    {
#ifdef _WIN32
        const auto t = Clock::now();
        __atomic_add_fetch(&g_ps3_gate_line.waiters, 1, __ATOMIC_SEQ_CST);   // before WaitOnAddress compares: a bump after this point also wakes (patch 7914)
        const BOOL r = WaitOnAddress((volatile void*)&g_ps3_gate_line.seq, &s0, sizeof s0, ms);
        __atomic_sub_fetch(&g_ps3_gate_line.waiters, 1, __ATOMIC_SEQ_CST);
        wait_ms += ms_since(t);
        return r != FALSE;   // FALSE = ERROR_TIMEOUT (or a failed wait, counted the same: loud in wake_timeouts)
#else
        (void)s0; (void)ms; return false;
#endif
    }
};
// FIX (smoothness audit cause 7): in a rollback match the per-save [snap] ww line and the routine commit [snap] sync lines are unbuffered writes on the main
// thread every tick; they print only with JSP_NET_TICK_LOG=1 (the two-copy test sets it). Rollbacks, blocked or failed gates and slow saves always print.
static bool snap_tick_log() { static const bool v = [] { const char* e = std::getenv("JSP_NET_TICK_LOG"); return e && e[0] == '1'; }(); return v; }
void log_sync(const char* what, uint32_t k, const jsy::GateResult& g, double census_ms, bool no_saved_view = false)
{
    rsx_guard_flush_log();   // the guard's finding of this gate, printed with the threads running
    const jsy::View& v = g.view; const jsy::Verdict& d = g.verdict;
    std::fprintf(stderr, "[snap] sync %s k=%u mode=%d tries=%u ok=%d reasons=0x%X notes=0x%X%s lwm tracked=%u live_held=%u held_unowned=%u abandoned=%u stale=%u dead=%u unreadable=%u first_live=0x%08X first_stale=0x%08X "
                 "evq woken=%u buffered=%u drift=%u sem woken=%u drift=%u mtx_held=%u rw_held=%u thr parked=%u transit=%u running=%u outside=%u unknown=%u strict=%d mark overflows=%u unbalanced=%u census_ms=%.3f\n", what, k, (int)g_sync,
                 g.tries, g.ok ? 1 : 0, g.reasons, d.notes, no_saved_view ? " no_saved_view=1" : "", v.lwm_tracked, v.lwm_live_held, v.lwm_held_unowned, v.lwm_abandoned, v.lwm_stale, v.lwm_dead,
                 v.lwm_unreadable, v.lwm_first_live, v.lwm_first_stale, d.evq_woken, d.evq_buffered, d.evq_drift, d.sem_woken, d.sem_drift, v.mtx_held, v.rw_held, d.n_parked,
                 d.n_transit, d.n_running, d.n_outside, d.n_unknown, g_cfg.strict ? 1 : 0, v.mark_overflows, v.mark_unbalanced, census_ms);
    if ((g.reasons & jsy::RsxPending) || g.unfrozen_waits) {   // where the RSX walker is: drain passes since the gate first saw it busy, get, flips, the 7 words at get
        uint32_t w[14]; ps3_gcm_walker_view(w);
        std::fprintf(stderr, "[snap] sync   rsx walker k=%u drain_passes=%u->%u get=0x%X->0x%X flips q/r/c=%u/%u/%u drain_tid=%u words@get=%08X %08X %08X %08X %08X %08X %08X\n", k,
                     g_walk0_set ? g_walk0[0] : w[0], w[0], g_walk0_set ? g_walk0[1] : w[1], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10], w[11], w[12]);
    }
    g_walk0_set = false;
    if (g_js_learned_note) { g_js_learned_note = false; std::fprintf(stderr, "[snap] job server manager 0x%08X learned; %u earlier commits had no job-server check (they count as clean on the other conditions)\n", g_js_mgr, g_js_unlearned); }
    if (!g.ok) {
        if (g.reasons & jsy::PauseFailed) std::fprintf(stderr, "[snap] FAILED gate: a thread could not be paused (tries=%u, reasons=0x%X)\n", g.tries, g.reasons);
        else if (g_ext && g_sync_ms) std::fprintf(stderr, "[snap] FAILED gate not idle: %.0f ms of gate time since the last session update (budget %u ms; tries=%u, reasons=0x%X, unfrozen waits=%u)\n", g_gate_used_ms, g_sync_ms, g.tries, g.reasons, g.unfrozen_waits);
        else std::fprintf(stderr, "[snap] FAILED gate not idle: out of tries (%u tries, no time budget; reasons=0x%X, unfrozen waits=%u)\n", g.tries, g.reasons, g.unfrozen_waits);
    }
    if (g.unfrozen_waits) std::fprintf(stderr, "[snap] sync   rsx wait k=%u unfrozen_waits=%u (1 ms each, threads running)\n", k, g.unfrozen_waits);
    if (g_cfg.roles)   // park points. at = threads found at a role site, away = learned members elsewhere, absent = required roles with nobody at a site, queue = a role queue already holds an event
        std::fprintf(stderr, "[snap] sync   park k=%u at=%u away=%u absent=%u queue=%u block=%d rsx put=0x%X get=0x%X%s fiber_swaps=%ld\n", k, d.role_at, d.role_away, d.role_absent, d.role_queue,
                     g_cfg.quiesce_block ? 1 : 0, v.rsx_put, v.rsx_get, d.rsx_pending ? " PENDING" : "", g_ps3_hle_mark_fiber_swaps);
    if (v.jobs_valid)   // the three boundary checks (blocks only where block=1)
        std::fprintf(stderr, "[snap] sync   bound k=%u jobs=%u(job-chain inflight=%u jq=%u pm_busy=%u jc_run=%u/%u spurs_inst=%u async_threads=%u) fibers_away=%u(n=%u ready=%u run=%u wsig=%u wjoin=%u done=%u sched_run=%u lock=%u) "
                     "flip=%u(status=%u req_seq=%u tick_seq=%u queued=%u reached=%u completed=%u direct=%u pump=0x%X lock=%u) host_drift=%u block jobs=%d fibers=%d flip=%d\n", k, d.jobs, v.jobs_inflight, v.jq_pending, v.pm_busy,
                     v.jc_running, v.jc_total, v.spurs_inst, v.async_running, d.fibers_away, v.fb_total, v.fb_ready, v.fb_running, v.fb_wait_signal, v.fb_wait_join, v.fb_done, v.fb_sched_running, v.fb_lock_held,
                     d.flip_pending, v.flip_status, v.flip_req_seq, v.flip_tick_seq, v.flips_queued, v.flips_reached, v.flips_completed, v.flip_direct, v.gcm_pending, v.flip_lock, d.host_drift,
                     g_cfg.block_jobs ? 1 : 0, g_cfg.block_fibers ? 1 : 0, g_cfg.block_flip ? 1 : 0);
    for (int i = 0; i < d.naway; i++)
        std::fprintf(stderr, "[snap] sync   away tid=%u lr=0x%08X\n", d.away_tid[i], d.away_lr[i]);
    for (int i = 0; i < d.nthr; i++)   // thread ids and call sites of every live thread that is not parked in a known wait (first 8)
        std::fprintf(stderr, "[snap] sync   thr tid=%u %s kind=%u id=0x%X obj=0x%08X lr=0x%08X\n", d.thr[i].tid,
                     d.thr[i].cls == jsy::TC::Transit ? "TRANSIT" : d.thr[i].cls == jsy::TC::Running ? "running-in-HLE" : d.thr[i].cls == jsy::TC::Unknown ? "marker-unreliable" : "guest-code", d.thr[i].kind, d.thr[i].id, d.thr[i].obj, d.thr[i].lr);
}
// Committed bytes per 256 MB window outside the saved set (what the snapshot leaves alone, so the next run shows where else state could be).
void log_elsewhere(const std::vector<jss::Range>& keep)
{
    char buf[512]; size_t n = 0;
    for (uint64_t lo = 0; lo < 0x100000000ull; lo += 0x10000000ull) {
        std::vector<jss::Range> c;
        committed_ranges((uint32_t)lo, lo + 0x10000000ull, &c);
        const uint64_t out = jss::total(jss::subtract(jss::page_align(c), keep));
        if (out && n < sizeof buf - 40) n += (size_t)std::snprintf(buf + n, sizeof buf - n, " 0x%02X:%.1fMB", (unsigned)(lo >> 24), out / 1048576.0);
    }
    std::fprintf(stderr, "[snap] elsewhere (committed, not saved, per 256 MB window):%s\n", n ? buf : " none");
}

// What changed between two saved-set layouts: pages gained / lost (first 4 ranges of each), so a COMMITTED-SET-CHANGED names the region.
void log_layout_diff(const std::vector<jss::Range>& now, const std::vector<jss::Range>& was, const char* what)
{
    const std::vector<jss::Range> add = jss::subtract(jss::page_align(now), was), gone = jss::subtract(jss::page_align(was), now);
    char buf[400]; size_t n = 0;
    auto list = [&](const std::vector<jss::Range>& v, const char* tag) {
        n += (size_t)std::snprintf(buf + n, sizeof buf - n, " %s %zu ranges %.1f KB:", tag, v.size(), jss::total(v) / 1024.0);
        for (size_t i = 0; i < v.size() && i < 4 && n < sizeof buf - 40; i++) n += (size_t)std::snprintf(buf + n, sizeof buf - n, " %08X+%X", v[i].a, v[i].n);
    };
    list(add, "gained"); list(gone, "lost");
    std::fprintf(stderr, "[snap] layout %s:%s\n", what, buf);
}

void log_ranges(const std::vector<jss::Range>& keep)
{
    std::string s;
    char b[48];
    for (const jss::Range& r : keep) { std::snprintf(b, sizeof b, " %08X+%X", r.a, r.n); s += b; }
    std::fprintf(stderr, "[snap] ranges n=%zu:%s\n", keep.size(), s.c_str());
}

void do_save(uint32_t k, ppu_context* c)
{
    const auto t0 = Clock::now();
    uint64_t exb = 0;
    bool complete = false;
    std::vector<jss::Range> keep = discover(&exb, &complete);
    if (!complete || keep.empty()) { capture_failed("full save layout discovery"); return; }
    const double disc_ms = ms_since(t0);
    g_keep = keep; pres_prepare(keep);
    g_store.reset(keep);
    if (g_dirty) g_shadow.reset(keep);
    std::vector<Mod> mods; Suspended sp;
    if (g_suspend) { mods = collect_modules(); sp.prepare(); }
    Census cen;
    jsp_fight_hash_get_state(&g_saved_ts);   // takes the hash module's mutexes: never while a thread that may hold one is suspended
    const auto t1 = Clock::now();
    g_cfg.self_tid = (uint32_t)c->thread_id;
    GateOps gop{sp};
    const jsy::GateResult gr = jsy::gate(gop, g_sync, nullptr, g_sync_tries, g_cfg);
    const bool paused = gr.ok;
    if (paused) {
        g_store.save(vm_base);
        std::memcpy(&g_saved_ctx, c, sizeof g_saved_ctx);
        g_view_save = gr.view; g_save_clean = gr.reasons == 0;
    }
    const bool resumed = sp.resume();
    if (!resumed) resume_failed();
    g_save_ms = ms_since(t1);
    if (resumed) sp.census(mods, &cen);
    sp.close();
    if (g_sync != jsy::Mode::Off) log_sync("save", k, gr, gop.sample_ms);
    if (!paused || !resumed) { g_store.valid = false; capture_failed(!paused && !(gr.reasons & jsy::PauseFailed) ? "full save SYNC-NOT-IDLE (JSP_SNAPSHOT_SYNC=2)" : "full save thread pause/resume", !paused ? jso::Pause : jso::Resume); return; }
    if (g_dirty) { std::memcpy(g_shadow.buf.data(), g_store.buf.data(), g_store.buf.size()); g_shadow.valid = true; }
    g_save_k = k; g_dirty_hist.clear(); g_pass1 = true;
    std::fprintf(stderr, "[snap] save k=%u ms=%.2f (discover %.2f) bytes=%zu ranges=%zu excluded=%llu threads=%d waiting=%d running=%d%s%s ts.done=%u anc.done=%u\n",
                 k, g_save_ms, disc_ms, g_store.bytes(), g_store.r.size(), (unsigned long long)exb, cen.n, cen.waiting, cen.running,
                 cen.running ? " running_in=" : "", cen.run_in, g_saved_ts.done, g_saved_ts.anc_done);
    log_ranges(keep);
    log_elsewhere(keep);
    g_active = true;
    rng_line("save", k);
    const uint32_t P = vm_read32(kRngPtr);
    if (P && !jss::covers(keep, P, 0x28))
        std::fprintf(stderr, "[snap] WARNING the Random object 0x%08X is NOT in the saved set: the replay cannot be equal\n", P);
}

void dirty_summary(size_t round)
{
    if (g_dirty_hist.empty()) return;
    std::vector<size_t> v = g_dirty_hist; std::sort(v.begin(), v.end());
    size_t sum = 0; for (size_t x : v) sum += x;
    std::fprintf(stderr, "[snap] dirty-summary round=%zu ticks=%zu pages/tick min=%zu median=%zu p95=%zu max=%zu mean=%.1f (4 KB pages; %.2f MB/tick at the mean)\n",
                 round, v.size(), v.front(), v[v.size() / 2], v[(size_t)((v.size() - 1) * 0.95)], v.back(), (double)sum / v.size(), (double)sum / v.size() * 4096.0 / 1048576.0);
}

void do_restore(uint32_t k, ppu_context* c)
{
    // diagnostics first (not timed): nonvolatile register diff, pages that differ, committed-set drift
    int dg = 0, df = 0, dv = 0;
    for (int i = 0; i < 32; i++) {
        if (i == 1 || i == 2 || i == 13 || i >= 14) dg += c->gpr[i] != g_saved_ctx.gpr[i];
        if (i >= 14) df += std::memcmp(&c->fpr[i], &g_saved_ctx.fpr[i], 8) != 0;
        if (i >= 20) dv += std::memcmp(&c->vr[i], &g_saved_ctx.vr[i], 16) != 0;
    }
    const uint32_t cr_mask = 0x00FFF000u;   // CR2..CR4 are the non-volatile condition fields (CR0 = bits 31:28 ... CR7 = bits 3:0)
    const int dcr = ((c->cr ^ g_saved_ctx.cr) & cr_mask) != 0;
    uint64_t exb = 0;
    bool complete = false;
    std::vector<jss::Range> now = discover(&exb, &complete);
    const uint64_t now_bytes = jss::total(now), saved_bytes = g_store.bytes();
    if (!complete || !jso::same_ranges(now, g_store.r)) { log_layout_diff(now, g_store.r, "changed before a full restore"); capture_failed("full restore COMMITTED-SET-CHANGED"); return; }
    if (g_quiesce >= 2 && !g_save_clean) { capture_failed("full restore ROLLBACK-TARGET-NOT-CLEAN"); return; }
    if (dg || df || dv || dcr) { capture_failed("full restore nonvolatile context changed"); return; }
    const auto td = Clock::now();
    const size_t changed = g_store.dirty_pages(vm_base);
    const double diag_ms = ms_since(td);
    std::vector<Mod> mods; Suspended sp;
    if (g_suspend) { mods = collect_modules(); sp.prepare(); }
    Census cen;
    const auto t1 = Clock::now();
    g_cfg.self_tid = (uint32_t)c->thread_id;
    GateOps gop{sp};
    const jsy::GateResult gr = jsy::gate(gop, g_sync, &g_view_save, g_sync_tries, g_cfg);
    const bool paused = gr.ok;
    if (paused) {
        pres_save(); g_store.restore(vm_base); pres_apply();
        std::memcpy(c, &g_saved_ctx, sizeof g_saved_ctx);
    }
    const bool resumed = sp.resume();
    if (!resumed) resume_failed();
    g_restore_ms = ms_since(t1);
    if (resumed) sp.census(mods, &cen);
    sp.close();
    if (paused) jsp_fight_hash_set_state(&g_saved_ts);   // follows restored memory only after every thread resumed
    if (g_sync != jsy::Mode::Off) log_sync("restore", k, gr, gop.sample_ms);
    if (!paused || !resumed) { capture_failed(!paused && !(gr.reasons & jsy::PauseFailed) ? "full restore SYNC-NOT-IDLE (JSP_SNAPSHOT_SYNC=2)" : "full restore thread pause/resume", !paused ? jso::Pause : jso::Resume); return; }
    std::fprintf(stderr, "[snap] restore k=%u -> tick %u ms=%.2f bytes=%zu pages_changed=%zu (of %zu) committed_now=%llu saved=%llu %s threads=%d waiting=%d running=%d "
                 "ctx_nonvolatile_diff gpr=%d fpr=%d vr=%d cr=%d (diag pass %.2f ms)%s%s\n",
                 k, g_saved_ts.done, g_restore_ms, g_store.bytes(), changed, g_store.bytes() / jss::kPage, (unsigned long long)now_bytes,
                 (unsigned long long)saved_bytes, "committed-set-equal", cen.n, cen.waiting, cen.running, dg, df, dv, dcr, diag_ms,
                 cen.running ? " running_in=" : "", cen.run_in);
    rng_line("restored", g_saved_ts.done);   // must print the same P and s= as the save line
    jsp_stepper_replay(1, g_saved_ts.done);
    std::fprintf(stderr, "[snap] cost round=%zu save=%.2f restore=%.2f save+restore=%.2f ms = %.2f of a 33.3 ms tick budget\n",
                 g_drv.round(), g_save_ms, g_restore_ms, g_save_ms + g_restore_ms, (g_save_ms + g_restore_ms) / 33.333);
    dirty_summary(g_drv.round());
    g_pass1 = false;
}


// ---- incremental mode (JSP_SNAPSHOT_WW) ------------------------------------------------------------------------------
// Per tick: [suspend] -> GetWriteWatch(RESET) over the ring's ranges -> commit (or rollback) -> [ResetWriteWatch after a rollback,
// whose own memcpy marked the pages] -> [resume]. Copy correctness still depends on all relevant writers being quiescent.
constexpr uint32_t kNoTick = 0xFFFFFFFFu;
// Rollback mode: in ext mode a COMMIT does not wait for the RSX to be idle; a RESTORE still does. The RSX writes nothing into the saved image that the simulation
// reads (its guest-visible writes: labels, reference and get words in the HLE inject region, which is excluded; GET_REPORT timestamps / the constant ZPASS count into the report
// area, cellGcmSys.c ~2410), the default command ring is excluded too, and render targets live in RSX local memory (not saved): a capture with commands still pending is the
// same simulation state as one taken idle. What must not happen is overwriting IO-mapped data the pending draws still read, so the rollback gate keeps put == get.
// Waiting at every commit made the session's survival depend on the speed of the slowest frame (a two-copy test: a 4.4 s frame failed a 2000 ms budget at k=80).
const jsy::Cfg& commit_cfg(uint32_t restore_to)
{
    static jsy::Cfg c; c = g_cfg;
    if (g_ext && restore_to == kNoTick) c.rsx_idle = false;
    // ENHANCEMENT (rollback latency, A/B): JSP_SNAPSHOT_PREWAIT=1 waits unfrozen (at most 30 ms) for the helpers to go idle before the first freeze (jsy::Cfg::prewait_ms)
    static const unsigned pw = [] { const char* e = std::getenv("JSP_SNAPSHOT_PREWAIT"); return e && e[0] == '1' ? 30u : 0u; }();
    c.prewait_ms = pw;
    return c;
}
struct TickBlob { ppu_context ctx; JspTickState ts; };
jss::DeltaRing g_ring;
// Called at every ext-mode capture and restore gate with the threads frozen: no allocation, no getenv, no printing here (the result is stored and printed after the resume by
// rsx_guard_flush_log). Writes since a capture are the ring entries after that capture's index. restore_to == kNoTick: a commit: compare with the previous capture, then it becomes the
// previous one. Otherwise a restore to T: if T's capture had RSX commands pending, every counted write since T's capture is a hazard; the previous capture then is T (its pending flag
// and ring index) and the pending flag of the idle restore gate is not used.
struct RsxCap { uint32_t k = 0xFFFFFFFFu, idx = 0; bool pending = false; };
std::vector<RsxCap> g_rg_caps;   // sized depth + 2 at the baseline (not a fixed 64)
int g_rg_on = -1;   // JSP_NET_RSX_WRITE_GUARD, read once (not while frozen)
struct RsxLog { bool set = false; uint32_t hits = 0, data = 0, label = 0, first_ea = 0, first_n = 0, prev_k = 0, k = 0; bool restore = false; } g_rg_log;
uint32_t rsx_scan(uint32_t from, uint32_t to, uint32_t* data, uint32_t* label, uint32_t* first_ea, uint32_t* first_n)
{
    const uint32_t n = to - from; uint32_t hits = 0;
    if (n > kRsxWCap) { *data += n; g_rg_wrapped++; return n; }   // the ring wrapped: cannot tell, count them all
    const auto& rg = g_ring.map().ranges();
    for (uint32_t i = from; i != to; i++) {
        const RsxWrite& w = g_rsx_wlog[i % kRsxWCap]; bool in = false;
        if (w.seq.load(std::memory_order_acquire) != i + 1) in = true;   // not published (a writer is in the middle of it): count it
        else for (const jss::Range& r : rg) if (w.ea < r.a + r.n && r.a < (uint64_t)w.ea + w.n) { in = true; break; }
        if (!in) continue;
        if (!hits) { *first_ea = w.ea; *first_n = w.n; }
        hits++; (w.kind ? *label : *data)++;
    }
    return hits;
}
void rsx_write_guard(uint32_t k, uint32_t restore_to, const jsy::View* saved, const jsy::View& now)
{
    (void)saved;
    const uint32_t to = g_rsx_widx.load(std::memory_order_acquire);
    const bool pending_now = now.rsx_valid && now.rsx_put != now.rsx_get;
    uint32_t from = 0; bool armed = false, restore = false; uint32_t prev_k = g_rsx_prev_k;
    if (restore_to == kNoTick) { g_rg_commits++; g_rg_noted += to - g_rsx_wscan; from = g_rsx_wscan; armed = g_rsx_prev_set && g_rsx_prev_pending; if (armed) g_rg_armed++; }
    else {
        restore = true; prev_k = restore_to;
        const RsxCap* cpp = g_rg_caps.empty() ? nullptr : &g_rg_caps[restore_to % g_rg_caps.size()];
        if (cpp && cpp->k == restore_to && cpp->pending) { from = cpp->idx; armed = true; g_rg_armed++; }
    }
    if (armed && to != from) {
        uint32_t data = 0, label = 0, fe = 0, fn = 0; const uint32_t hits = rsx_scan(from, to, &data, &label, &fe, &fn);
        if (hits) {
            g_rg_hit_commits++; g_rg_data += data; g_rg_label += label;
            if (!g_rg_log.set) g_rg_log = RsxLog{true, hits, data, label, fe, fn, restore ? restore_to : g_rsx_prev_k, k, restore};
            if (g_rg_on != 0) g_rsx_hazard = true;
        }
    }
    // this gate's state is the previous capture of the next one: at a commit the frozen view, at a restore the (idle) view of the restore gate itself
    g_rsx_prev_pending = pending_now; g_rsx_prev_set = true; g_rsx_prev_k = restore ? restore_to : k;
    (void)prev_k;
    if (restore_to == kNoTick && !g_rg_caps.empty()) { RsxCap& cp = g_rg_caps[k % g_rg_caps.size()]; cp.k = k; cp.idx = to; cp.pending = pending_now; }
    g_rsx_wscan = to;
}
void rsx_guard_flush_log()   // after the resume
{
    if (!g_rg_log.set || g_rsx_hazard_logged) return;
    g_rsx_hazard_logged = true;
    std::fprintf(stderr, "[snap] RSX write guard: %u RSX-side writes into saved guest memory (data %u, label/semaphore %u; first 0x%08X+%u) landed since the capture k=%u, which had RSX commands pending; %s k=%u%s\n",
                 g_rg_log.hits, g_rg_log.data, g_rg_log.label, g_rg_log.first_ea, g_rg_log.first_n, g_rg_log.prev_k, g_rg_log.restore ? "restore to" : "commit", g_rg_log.k,
                 g_rg_on != 0 ? " (ends the session; JSP_NET_RSX_WRITE_GUARD=0 only logs)" : " (guard off, logged only)");
}
std::vector<uint32_t> g_ext_new_slots;       // slots added by a ring extension, listed as written in the next commit or rollback (their older state is zero)
// A block committed (and written) between the unfrozen layout check and the freeze would be captured nowhere. Commits do not all fault (SPU DMA, vm_commit, mmapper), so
// no counter: with the threads frozen the committed raw ranges are walked again (allocation-free VirtualQuery) and compared with the ones the check saw (g_keep_raw).
double g_walk_ms_max = 0, g_walk_ms_sum = 0; uint64_t g_walk_n = 0;
bool g_layout_redo_failed = false; uint32_t g_layout_redo_count = 0;   // frozen counter comparisons that found a demand commit after the layout check (gate repeated)
uint32_t g_ww_grown = 0;                     // ring extensions so far (the committed set grew without a re-baseline)
std::vector<uint32_t> g_ww_slots;            // reused per tick (reserved at baseline: no allocation while threads are suspended)
std::vector<void*> g_ww_addrs;               // GetWriteWatch output batch
uint64_t g_ww_query_fail = 0;
struct WwTick { uint32_t pages; double collect_ms, commit_ms, total_ms; };
std::vector<WwTick> g_ww_hist;

// Collect bounded address spans; every queried span can fit its entire dirty set in the output buffer.
// Partial/invalid results invalidate the diagnostic, so no caller may apply a partially collected slot list.
bool ww_collect(std::vector<uint32_t>* out)
{
    out->clear();
#ifdef _WIN32
    const auto& ranges = g_ring.map().ranges();
    for (size_t i = 0; i < ranges.size(); i++) {
        const uintptr_t begin = reinterpret_cast<uintptr_t>(vm_base + ranges[i].a);
        const bool ok = jso::collect_pages(begin, ranges[i].n, jss::kPage,
            g_ww_addrs.data(), g_ww_addrs.size(),
            [](uintptr_t p, size_t n, void** addresses, size_t* count, uint32_t* granularity) {
                ULONG_PTR count_out = *count; ULONG gran = 0;
                const UINT status = GetWriteWatch(WRITE_WATCH_FLAG_RESET, reinterpret_cast<void*>(p), n,
                                                  addresses, &count_out, &gran);
                *count = count_out; *granularity = gran;
                return status == 0;
            },
            [&](uintptr_t address) {
                if (out->size() == out->capacity()) return false; // no allocation while threads are frozen
                out->push_back(g_ring.map().first_slot(i) + static_cast<uint32_t>((address - begin) / jss::kPage));
                return true;
            });
        if (!ok) { g_ww_query_fail++; return false; }
    }
#endif
    return true;
}

bool ww_reset_all()
{
#ifdef _WIN32
    for (const jss::Range& x : g_ring.map().ranges())
        if (ResetWriteWatch(vm_base + x.a, x.n) != 0) return false;
#endif
    return true;
}

// pages of the saved ranges that were written since the watch was last reset, WITHOUT resetting it (WRITE_WATCH_FLAG 0). Called while frozen just before the resume:
// after the commit's collect (which resets) or the rollback's reset_all, any page counted here was written by a thread that was not frozen (or by main itself) during the frozen
// interval, i.e. a write the freeze did not cover. No allocation (the scratch is g_ww_addrs). *first = guest address of the first page seen.
uint32_t ww_peek_dirty(uint32_t* first)
{
    uint32_t total = 0; *first = 0;
#ifdef _WIN32
    const auto& ranges = g_ring.map().ranges();
    for (size_t i = 0; i < ranges.size(); i++) {
        const uintptr_t begin = reinterpret_cast<uintptr_t>(vm_base + ranges[i].a);
        jso::collect_pages(begin, ranges[i].n, jss::kPage, g_ww_addrs.data(), g_ww_addrs.size(),
            [](uintptr_t p, size_t n, void** addresses, size_t* count, uint32_t* granularity) {
                ULONG_PTR count_out = *count; ULONG gran = 0;
                const UINT status = GetWriteWatch(0, reinterpret_cast<void*>(p), n, addresses, &count_out, &gran);
                *count = count_out; *granularity = gran;
                return status == 0;
            },
            [&](uintptr_t address) { if (!total) *first = (uint32_t)(address - reinterpret_cast<uintptr_t>(vm_base)); total++; return true; });
    }
#endif
    return total;
}

// This adapter executes the same transaction tested with fake operations on the host. No formatted output or allocation
// occurs inside its frozen interval. Get/set of hash counters stays outside that interval because it takes mutexes.
bool g_gate_jobserver = false;   // JSP_SNAPSHOT_GATE_JOBSERVER=1: the job server work state is a quiesce condition of the gate (also on with QUIESCE_JOBS)
bool ww_layout_changed();   // defined after WwOps
extern bool g_layout_force;   // set before a forced re-discovery (defined with ww_layout_changed)
bool frozen_layout_same();
struct WwOps {
    Suspended& threads;
    uint32_t k;
    ppu_context* ctx;
    uint32_t restore_to;
    TickBlob blob{};
    size_t missed = 0;
    double suspend_ms = 0, collect_ms = 0, apply_ms = 0, reset_ms = 0, verify_ms = 0;
    bool verified = false;
    double census_ms = 0;
    bool no_saved = false;   // a rollback whose target tick has no recorded census view (logged)
    jsy::GateResult gr = {};   // sync census taken with the freeze (a commit records it by tick, a rollback compares with the target tick's)
    double presample_ms = 0, gate_wait_ms = 0, frozen_ms = 0;   // unfrozen prechecks, time blocked on the gate sequence, threads frozen (last freeze .. resume)
    Clock::time_point t_frozen{}; bool frozen = false;
    bool pause() {
        const auto t = Clock::now();
        GateOps gop{threads}; gop.k = k;
        const jsy::View* saved = restore_to == kNoTick ? nullptr : g_views.find(restore_to);
        g_walk0_set = false;   // each gate starts its own walker comparison
        for (unsigned redo = 0;; redo++) {
            const auto t_gate = Clock::now();
            gr = g_gate_ev ? jsy::gate_ev(gop, g_sync, saved, g_ev_lim, g_cfg) : jsy::gate(gop, g_sync, saved, g_sync_tries, commit_cfg(restore_to), gate_budget_ms());
            g_gate_used_ms += ms_since(t_gate);
            // The layout check runs unfrozen. A block committed AND written after it (by a fault, an SPU DMA, vm_commit, the mmapper: any path) is
            // captured nowhere and a commit would store a zero pre-image for it. With the threads frozen the committed ranges are walked again and must equal what the check saw;
            // otherwise resume, check the layout again unfrozen (a growth extends the ring again, a non-growth change ends the capture loudly) and gate once more, inside the gate budget.
            if (!(g_ext && gr.ok && gr.suspended && !frozen_layout_same())) break;
            const auto t_redo = Clock::now();
            gop.resume();
            if (g_sync_ms && g_gate_used_ms >= (double)g_sync_ms) { std::fprintf(stderr, "[snap] FAILED: guest memory kept being committed during the gate (%.0f ms of gate time, budget %u ms, %u re-checks)\n", g_gate_used_ms, g_sync_ms, redo + 1); g_layout_redo_failed = true; gr.ok = false; gr.suspended = false; break; }
            g_layout_force = true;   // the frozen walk saw a different set: discover for real (a commit path that did not bump the generation)
            const bool chg = ww_layout_changed();
            if (!g_on) { gr.ok = false; gr.suspended = false; break; }   // the re-discovery failed (capture_failed ran): no commit on top of it
            if (chg) { std::fprintf(stderr, "[snap] FAILED: the committed set changed (not a pure growth: a thread born in between can lose a guard page of an already committed stack block, or memory was decommitted) between the layout check and the freeze at k=%u\n", k); g_layout_redo_failed = true; gr.ok = false; gr.suspended = false; break; }
            g_layout_redo_count++; g_gate_used_ms += ms_since(t_redo);   // the walk and the re-discovery count against the gate budget
        }
        g_last_gate_reasons = gr.reasons;
        if (g_ext && gr.suspended) rsx_write_guard(k, restore_to, saved, gr.view);   // threads frozen: the ring of RSX-side writes is stable
        census_ms = gop.sample_ms; presample_ms = gop.presample_ms; gate_wait_ms = gop.wait_ms;
        t_frozen = gop.t_frozen; frozen = gr.suspended;
        no_saved = restore_to != kNoTick && !saved;
        if (restore_to == kNoTick && g_gate_jobserver && !gr.view.js_valid) g_js_unlearned++;
        if (gr.ok && restore_to == kNoTick) g_views.put(k, gr.view, gr.reasons == 0);   // preallocated plain stores; the clean bit: a rollback may only return to a clean tick
        suspend_ms = ms_since(t); return gr.ok;
    }
    bool collect() {
        const auto t = Clock::now();
        bool ok = ww_collect(&g_ww_slots);
        for (uint32_t s : g_ext_new_slots) { if (g_ww_slots.size() == g_ww_slots.capacity()) { ok = false; break; } g_ww_slots.push_back(s); }   // no allocation while frozen
        collect_ms = ms_since(t); return ok;
    }
    bool reset() {
        const auto t = Clock::now();
        const bool ok = ww_reset_all();
        reset_ms += ms_since(t); return ok;
    }
    bool apply(jso::Action action) {
        const auto t = Clock::now();
        bool ok = true;
        if (action == jso::Action::Baseline) g_ring.baseline(k, vm_base, &blob);
        else if (action == jso::Action::Commit)
            ok = g_ring.newest() < k /* newest + 1, or later after saves that took no snapshot */ && g_ring.commit(k, vm_base, g_ww_slots.data(), g_ww_slots.size(), &blob);
        else { pres_save(); ok = g_ring.rollback(restore_to, vm_base, g_ww_slots.data(), g_ww_slots.size(), &blob); pres_apply(); }
        if (ok && action != jso::Action::Baseline) g_ext_new_slots.clear();   // consumed: the commit stored their zero pre-image / the rollback zeroed them
        apply_ms = ms_since(t); return ok;
    }
    bool verify() {
        if (g_ww < 2) return true;
        const auto t = Clock::now();
        verified = true; missed = g_ring.verify(vm_base, &g_pres);
        verify_ms = ms_since(t); return missed == 0;
    }
    uint32_t fw_pages = 0, fw_first = 0;   // pages written during the frozen interval, logged after resume
    bool resume() {
        if (frozen && g_fw_on && g_ring.map().pages()) fw_pages = ww_peek_dirty(&fw_first);
        if (frozen) { frozen_ms = ms_since(t_frozen); frozen = false; }
        return threads.resume();
    }
    void restore_metadata() {
        std::memcpy(ctx, &blob.ctx, sizeof blob.ctx);
        jsp_fight_hash_set_state(&blob.ts);
    }
    void verification_line(const char* what) const {
        if (verified) std::fprintf(stderr, "[snap] ww-verify %s k=%u missed_pages=%zu (%.2f ms)%s\n",
            what, k, missed, verify_ms, missed ? " WRITER-NOT-SEEN" : "");
    }
};

// (JSP_SNAPSHOT_THREADS=registry): the registry against a Toolhelp listing, outside any freeze. A live thread the registry does not hold would never
// be frozen: stop the run and name it. Also stops on REGISTRY-FULL (no free slot) and on a thread that got no handle. `first` (the baseline, when the
// game's threads exist) also stops when the exe's TLS callback evidently was not delivered (dozens of threads, none registered itself: init seeded them all, which
// would otherwise hide a Wine that never calls the callback); every audit also reclaims the slots of seeded threads that ended . The audit's own time is counted:
// it is added to the real cost of the operation that follows it (g_audit_pending_ms).
bool registry_audit(const char* what, uint32_t k, bool first = false)
{
    jtr::Registry& R = jsp_thread_registry();
    const auto t = Clock::now();
    char why[256] = {0}; unsigned listed = 0, rechecked = 0;
    // reclaim BEFORE the listing, also LIVE slots whose thread handle is signalled (a terminated thread never runs its detach): the audit then compares a fresh table
    const unsigned reclaimed = R.reclaim([](void* h) { jsp_thread_registry_close(h); }, [](void* h) { return jsp_thread_registry_ended(h); }, true);
    const int bad = jsp_thread_registry_audit(why, sizeof why, &listed, &rechecked);
    const double ms = ms_since(t);
    g_audits++; g_audit_sum += ms; if (ms > g_audit_max) g_audit_max = ms; g_audit_pending_ms += ms;
    const uint32_t births = R.births.load(), seeded = R.seeded.load();
    const bool cb_missing = first && jtr::Registry::callback_missing(listed, births, seeded);
    std::fprintf(stderr, "[snap] threads audit %s k=%u listed=%u live=%u live_peak=%u seeded=%u cb_births=%u births=%u deaths=%u reaped=%u reclaimed_now=%u gated=%u full=%u handle_failed=%u rechecked=%u ms=%.3f%s%s%s\n", what, k, listed,
                 R.live_count(), R.live_peak.load(), seeded, births - seeded, births, R.deaths.load(), R.reaped.load(), reclaimed, R.gated.load(), R.full.load(), R.handle_failed.load(), rechecked, ms,
                 cb_missing ? " TLS-CALLBACK-NOT-DELIVERED" : "", bad ? " THREAD-NOT-REGISTERED " : "", bad ? why : "");
    if (cb_missing) { std::fprintf(stderr, "[snap] the exe's TLS callback registered %u thread(s) of %u listed (%u were seeded from the listing): Wine did not deliver DLL_THREAD_ATTACH to it; JSP_SNAPSHOT_THREADS=registry cannot be trusted here\n", births - seeded, listed, seeded); capture_failed("TLS-CALLBACK-NOT-DELIVERED"); return false; }
    if (bad) { capture_failed("THREAD-NOT-REGISTERED"); return false; }
    if (R.full.load() || R.handle_failed.load()) { capture_failed("REGISTRY-FULL"); return false; }
    return true;
}

// ENHANCEMENT (rollback smoothness, A/B: JSP_SNAPSHOT_AUDIT_ASYNC=1): the periodic audit's Toolhelp listing walks every thread of the system and cost
// 41-46 ms on the game thread every JSP_SNAPSHOT_THREADS_AUDIT ticks (a ~50 ms save every 2 s: a visible hitch). It reads the registry only through
// has_any (atomic), outside any freeze by nature, so it runs on its own thread at the same cadence; the game thread keeps the cheap part (reclaim of
// ended threads) and fails the run at its next audit tick when the background listing found an unregistered thread. The audit thread registers itself
// (TLS callback) and is frozen with the others; it never formats or allocates while the game thread holds a freeze it could wait on (it does not wait on one).
std::atomic<int> g_async_audit_bad{0};
std::atomic<uint32_t> g_async_audits{0};
std::atomic<uint64_t> g_async_audit_us_sum{0}, g_async_audit_us_max{0};
char g_async_audit_why[256];
inline bool audit_async_on() { static const int v = [] { const char* e = std::getenv("JSP_SNAPSHOT_AUDIT_ASYNC"); return e && e[0] == '1' ? 1 : 0; }(); return v != 0; }
#ifdef _WIN32
DWORD g_async_audit_period_ms = 0;   // ENHANCEMENT (rollback smoothness): JSP_SNAPSHOT_AUDIT_PERIOD_MS, default the tick cadence below (JSP_SNAPSHOT_THREADS_AUDIT x 33 ms)
DWORD WINAPI async_audit_main(void*)
{
    const DWORD period = g_async_audit_period_ms ? g_async_audit_period_ms : (DWORD)(g_audit_every ? g_audit_every : 60) * 33u;
    for (;;) {   // the first listing at once: the scheduled audit that started this worker is covered by it
        char why[256] = {0}; unsigned listed = 0, rechecked = 0;
        const auto t = Clock::now();
        const int bad = jsp_thread_registry_audit(why, sizeof why, &listed, &rechecked);
        const uint64_t us = (uint64_t)(ms_since(t) * 1000.0);
        g_async_audit_us_sum.fetch_add(us); if (us > g_async_audit_us_max.load()) g_async_audit_us_max.store(us);
        g_async_audits.fetch_add(1);
        if (bad && !g_async_audit_bad.load()) { std::memcpy(g_async_audit_why, why, sizeof why); g_async_audit_bad.store(1); }
        Sleep(period);
    }
}
#endif
// FIX (review: a failed worker start skipped the audits): the worker start is retried at every scheduled audit until it runs (jau::Starter), and a
// scheduled audit whose start failed runs here, on the game thread, as the synchronous audit does.
jau::Starter g_audit_starter;
bool start_audit_worker()
{
#ifdef _WIN32
    { const char* e = std::getenv("JSP_SNAPSHOT_AUDIT_PERIOD_MS"); if (e && *e) g_async_audit_period_ms = (DWORD)std::strtoul(e, nullptr, 10); }
    DWORD tid = 0; HANDLE h = CreateThread(nullptr, 0, async_audit_main, nullptr, 0, &tid);
    if (!h) {
        if (g_audit_starter.failures < 5)
            std::fprintf(stderr, "[snap] threads audit (background): the worker thread did not start (error %lu); this audit runs on the game thread, the start is tried again at the next audit\n", (unsigned long)GetLastError());
        return false;
    }
    if (g_audit_exempt) exempt_add((uint32_t)tid);   // exempt: listed in freeze_exempt before the next freeze (this thread takes them)
    CloseHandle(h);
    std::fprintf(stderr, "[snap] threads audit (background) every %lu ms, %s%s\n", (unsigned long)(g_async_audit_period_ms ? g_async_audit_period_ms : (DWORD)(g_audit_every ? g_audit_every : 60) * 33u),
                 g_audit_exempt ? "left running by the freeze (JSP_SNAPSHOT_AUDIT_EXEMPT=1)" : "frozen with the others (JSP_SNAPSHOT_AUDIT_EXEMPT=0)",
                 g_audit_starter.failures ? " (started after earlier failed starts; those audits ran on the game thread)" : "");
    return true;
#else
    return false;   // no worker here: every scheduled audit runs synchronously
#endif
}
bool registry_audit_tick(const char* what, uint32_t k)
{
    if (!audit_async_on()) return registry_audit(what, k);
    if (g_audit_starter.at_audit(start_audit_worker) == jau::Path::Sync) return registry_audit(what, k);
    const auto t = Clock::now();
    jtr::Registry& R = jsp_thread_registry();
    R.reclaim([](void* h) { jsp_thread_registry_close(h); }, [](void* h) { return jsp_thread_registry_ended(h); }, true);
    g_audit_pending_ms += ms_since(t);
    if (g_async_audit_bad.load()) {
        std::fprintf(stderr, "[snap] threads audit (background) %s k=%u THREAD-NOT-REGISTERED %s\n", what, k, g_async_audit_why);
        capture_failed("THREAD-NOT-REGISTERED"); return false;
    }
    if (R.full.load() || R.handle_failed.load()) { capture_failed("REGISTRY-FULL"); return false; }
    const uint32_t n = g_async_audits.load();
    if (n && k % (g_audit_every * 10u) == 0)
        std::fprintf(stderr, "[snap] threads audit (background) k=%u audits=%u ms mean=%.3f max=%.3f\n", k, n, (double)g_async_audit_us_sum.load() / 1000.0 / n, (double)g_async_audit_us_max.load() / 1000.0);
    return true;
}

// initial = the first save of the run; false = a re-baseline after the saved layout changed: the history is dropped, the round in progress is aborted by the caller.
void ww_baseline(uint32_t k, ppu_context* c, bool initial = true)
{
    uint64_t exb = 0; bool complete = false;
    std::vector<jss::Range> keep = discover(&exb, &complete);
    if (!complete || keep.empty()) { capture_failed("WW baseline layout discovery"); return; }
    g_keep = keep; g_keep_raw = g_pending_raw; g_keep_ex = g_ex_exact; pres_prepare(keep);
    g_rg_caps.assign((size_t)g_ww_depth + 2, RsxCap{});   // per-tick guard records, indexed k % size (the ring never reaches further back than depth)
    g_ring.reset(keep, g_ww_depth, g_ww_pool, sizeof(TickBlob), g_ring_mode);
    g_views.clear();
    g_ww_slots.reserve(g_ring.map().pages());
    g_ext_new_slots.clear();
    g_ww_addrs.assign(4096, nullptr);
    {   // ENHANCEMENT (rollback latency, JSP_SNAPSHOT_COPY_THREADS): workers started here, outside any freeze; the ring hands them its Copy2 batches
        static const int want = [] { const char* e = std::getenv("JSP_SNAPSHOT_COPY_THREADS"); return e && *e ? (int)std::strtol(e, nullptr, 10) : (g_ext ? 3 : 0); }();
        g_ring.set_copier(want > 0 && pcopy::start(want) > 0 ? pcopy::run : nullptr, nullptr);
        if (initial) std::fprintf(stderr, "[snap] page copy: %d worker thread(s) + the main thread (JSP_SNAPSHOT_COPY_THREADS=%d; 0 = the main thread alone, as before)\n", pcopy::g_workers, want);
    }
    if (g_thr_registry) { if (!registry_audit("baseline", k, initial)) return; g_audit_pending_ms = 0; }   // the baseline is not a per-tick operation
    std::vector<Mod> mods; Suspended sp; Census cen;
    if (g_suspend) { mods = collect_modules(); sp.prepare(); }
    g_cfg.self_tid = (uint32_t)c->thread_id;
    WwOps op{sp, k, c, kNoTick};
    std::memcpy(&op.blob.ctx, c, sizeof op.blob.ctx);
    jsp_fight_hash_get_state(&op.blob.ts);
    const auto t1 = Clock::now();
    const jso::Result result = jso::capture(op, jso::Action::Baseline);
    if (result.failures & jso::Resume) resume_failed();
    const double ms = ms_since(t1);
    if (!(result.failures & jso::Resume)) sp.census(mods, &cen);
    sp.close();
    if (g_sync != jsy::Mode::Off) log_sync("baseline", k, op.gr, op.census_ms);
    op.verification_line("baseline");
    if (!result.ok()) { capture_failed("WW baseline transaction", result.failures); return; }
    if (initial) { g_save_k = k; g_pass1 = true; g_active = true; }
    else g_rebases++;
    g_ww_hist.clear();
    std::fprintf(stderr, "[snap] ww %s k=%u ms=%.2f bytes=%zu ranges=%zu pages=%u ring_bytes=%zu depth=%u pool=%zu threads=%d running=%d rebases=%u\n", initial ? "baseline" : "REBASELINE", k, ms,
                 (size_t)g_ring.map().pages() * jss::kPage, keep.size(), g_ring.map().pages(), g_ring.bytes(), g_ww_depth, g_ww_pool, cen.n, cen.running, g_rebases);
    std::fprintf(stderr, "[snap] cost-mode k=%u threads=%s gate=%s ring=%s timeout_ms=%u retry_ms=%u max_wakes=%u audit_every=%u\n", k, g_thr_registry ? "registry" : "toolhelp", g_gate_ev ? "ev" : "poll",
                 g_ring_mode == jss::RingMode::PagePtr ? "ptr" : "copy2", g_ev_lim.timeout_ms, g_ev_lim.retry_wait_ms, g_ev_lim.max_wakes, g_audit_every);
    log_ranges(keep);
    if (initial) rng_line("save", k);
}

// One tick in incremental mode. `restore_to` = kNoTick for a commit, else the tick to roll back to.
// Allocation-free: the committed raw ranges of the saved windows against g_keep_raw (same windows and predicate as discover()). Runs with the threads frozen; its cost is logged in the summary.
bool frozen_walk_skippable();   // defined with ww_layout_changed
uint64_t g_walk_skipped = 0;
bool frozen_layout_same()
{
#ifdef _WIN32
    if (frozen_walk_skippable()) { g_walk_skipped++; return true; }
    const auto t0 = Clock::now();
    size_t gi = 0; bool have = false, bad = false; uint64_t rs = 0, re = 0;
    auto flush = [&]() { if (!have) return; if (gi >= g_keep_raw.size() || g_keep_raw[gi].a != rs || (uint64_t)g_keep_raw[gi].a + g_keep_raw[gi].n != re) bad = true; gi++; have = false; };
    auto walk = [&](uint32_t lo, uint64_t hi) {
        uint64_t a = lo;
        while (a < hi && !bad) {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(vm_base + a, &mbi, sizeof mbi)) { bad = true; break; }
            const uint64_t rb = (uint64_t)((uint8_t*)mbi.BaseAddress - vm_base), rend = rb + mbi.RegionSize;
            const bool rw = mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
            const uint64_t s = std::max<uint64_t>(a, rb), e = std::min<uint64_t>(hi, rend);
            if (rw && s < e) { if (have && s <= re) re = std::max(re, e); else { flush(); if (bad) break; have = true; rs = s; re = e; } }
            if (rend <= a) { bad = true; break; }
            a = rend;
        }
    };
    for (const jss::Window& w : jss::kSaveWindows) walk(w.lo, w.hi);   // ascending address order, so the runs come out sorted and merged like normalise() makes them
    if (g_ls) walk(0x30000000u, 0x40000000ull);
    const uint32_t sb = 0xD0000000u, cb = 0xDF000000u, sn = g_vm_stack_alloc.next_addr;
    if (sn > sb && sn <= cb) walk(sb, sn);
    walk(cb, cb + 0x01000000ull);
    flush();
    if (gi != g_keep_raw.size()) bad = true;
    const double ms = ms_since(t0); g_walk_n++; g_walk_ms_sum += ms; if (ms > g_walk_ms_max) g_walk_ms_max = ms;
    return !bad;
#else
    return true;
#endif
}
// True when the committed set differs from the saved layout (logged with the diff). Exact discovery is outside the freeze because it allocates; a writer committing
// or remapping memory between this check and the suspension is a known race. Incomplete discovery stops the run.
// ENHANCEMENT (rollback latency, A/B: JSP_SNAPSHOT_LAYOUT_GEN=1): the per-tick discovery walks the whole write-watched reservation (2-3 ms a tick, measured).
// The toolkit bumps g_vm_layout_gen after every commit or protection change of guest memory; while it is unchanged since the last discovery, that discovery
// still holds and is not repeated. The generation is read BEFORE a discovery (a commit racing it bumps it again, so the next tick discovers). The frozen walk
// (frozen_layout_same) still runs on every gate as the net for a commit path that does not bump; its redo forces a full discovery (g_layout_force).
extern "C" volatile uint32_t g_vm_layout_gen;
inline bool layout_gen_on() { static const int v = [] { const char* e = std::getenv("JSP_SNAPSHOT_LAYOUT_GEN"); return e && e[0] == '1' ? 1 : 0; }(); return v != 0; }
uint32_t g_layout_gen_seen = 0; bool g_layout_gen_valid = false, g_layout_force = false; uint64_t g_layout_skips = 0;
bool ww_layout_changed_full();
// ENHANCEMENT (rollback latency, A/B: JSP_SNAPSHOT_FROZEN_WALK_EVERY=N, default 1 = every gate): the frozen layout walk (frozen_layout_same, ~2 ms of VirtualQuery
// inside every freeze, measured) runs only on every Nth frozen gate while the toolkit's layout generation still equals the one the last full discovery saw
// (JSP_SNAPSHOT_LAYOUT_GEN=1). The walk is the net for a commit path that does not bump the generation; in 16 logged sessions it never fired (re-checks 0).
// Read with the threads frozen: an atomic load and a counter, no allocation.
bool frozen_walk_skippable()
{
    static const unsigned every = [] { const char* e = std::getenv("JSP_SNAPSHOT_FROZEN_WALK_EVERY"); const unsigned v = e && *e ? (unsigned)std::strtoul(e, nullptr, 10) : 1u; return v ? v : 1u; }();
    static unsigned n = 0;
    if (every <= 1 || !layout_gen_on() || !g_layout_gen_valid || g_layout_force) return false;
    if (__atomic_load_n(&g_vm_layout_gen, __ATOMIC_SEQ_CST) != g_layout_gen_seen) return false;   // something committed since the last discovery: walk
    return ++n % every != 0;
}
bool ww_layout_changed()
{
    const uint32_t gen0 = __atomic_load_n(&g_vm_layout_gen, __ATOMIC_SEQ_CST);
    if (layout_gen_on() && !g_layout_force && g_layout_gen_valid && gen0 == g_layout_gen_seen) { g_layout_ms = 0; g_layout_skips++; return false; }
    g_layout_force = false;
    const bool chg = ww_layout_changed_full();
    g_layout_gen_seen = gen0; g_layout_gen_valid = g_on;
    return chg;
}
bool ww_layout_changed_full()
{
    uint64_t exb = 0; bool complete = false;
    const auto tl = Clock::now();
    const auto now = discover(&exb, &complete);
    // the committed set is re-read once per tick: the census's cached page readability is valid until the next one (keep: until the set really changes)
    if (!lwm_cache_keep() || !complete || !jso::same_ranges(g_pending_raw, g_keep_raw)) ps3_lwm_census_invalidate();
    g_layout_ms = ms_since(tl);   // logged on the commit/rollback line that follows (flow.h runs this check first on every tick)
    if (!complete) { capture_failed("WW layout discovery incomplete"); return false; }
    if (!jso::same_ranges(g_ex_exact, g_keep_ex)) { std::fprintf(stderr, "[snap] the exclusion list changed (the GCM command buffer or the IO map moved): the saved layout is not comparable, re-baselining\n"); return true; }   // exclusions are static in rollback mode
    if (jso::same_ranges(now, g_keep)) { g_keep_raw = g_pending_raw; return false; }   // a block committed entirely inside an exclusion changes the raw set only; accept it, or the frozen walk differs forever
    // A pure GROWTH (demand-committed memory: the runtime commits 64 KB blocks of the guest stacks on first touch) does not drop the history. Those pages were zero-filled and
    // unmapped at every earlier tick, so the ring takes them with zero shadow pages and the next commit / rollback lists them as written: a rollback across the growth zeroes them again,
    // identical on both peers (the page content is guest memory that only the thread owning the block ever wrote). A LOSS or a layout the ring cannot extend still re-baselines.
    if (g_ext && jss::subtract(g_keep, now).empty()) {
        const std::vector<jss::Range> add = jss::subtract(jss::page_align(now), g_keep);
        const uint32_t np0 = g_ring.map().pages();
        if (!add.empty() && g_ring.extend(add)) {
            log_layout_diff(now, g_keep, "committed set grew (ring extended, history kept)");
            g_keep = jss::normalise(now); g_keep_raw = g_pending_raw; pres_prepare(g_keep);
            for (uint32_t s = np0; s < g_ring.map().pages(); s++) g_ext_new_slots.push_back(s);
            g_ww_slots.reserve(g_ring.map().pages() + g_ext_new_slots.size());
            g_ww_grown++;
            return false;
        }
    }
    log_layout_diff(now, g_keep, "committed set changed");
    return true;
}

// One commit (restore_to == kNoTick) or one rollback of the ring; the caller (jsf::ww_pass_end) has already ruled out a layout change. false = fatal (capture_failed ran).
bool ww_transaction(uint32_t k, ppu_context* c, uint32_t restore_to)
{
    if (restore_to != kNoTick && g_quiesce >= 2 && !g_views.is_clean(restore_to)) {   // only a tick whose capture was clean is a rollback target
        std::fprintf(stderr, "[snap] rollback k=%u -> tick %u refused: the capture of that tick has no clean census (never stored, or it had blocking reasons)\n", k, restore_to);
        capture_failed("WW ROLLBACK-TARGET-NOT-CLEAN"); return false;
    }
    if (restore_to != kNoTick && !g_ring.has(restore_to)) {   // named, never "tick transaction failures=0x4" (outside the window, or a tick that took no snapshot)
        std::fprintf(stderr, "[snap] ww EVICTED k=%u target=%u oldest=%u newest=%u depth=%u pool_used=%zu pool=%zu\n", k, restore_to, g_ring.oldest(), g_ring.newest(), g_ring.depth_now(), g_ring.pool_used(), g_ww_pool);
        capture_failed("WW ROLLBACK-TARGET-EVICTED"); return false;
    }
    g_audit_pending_ms = 0;
    if (g_thr_registry && g_audit_every && k % g_audit_every == 0 && !registry_audit_tick(restore_to == kNoTick ? "commit" : "rollback", k)) return false;
    const double audit_ms = g_audit_pending_ms; g_audit_pending_ms = 0;
    const double layout_ms = g_layout_ms;
    const auto tp = Clock::now();
    Suspended sp;   // no module list and no census here (run soak-s1: prep was ~6 ms per tick, and the per-tick transaction never printed the census); baseline and full mode still take them
    if (g_suspend) sp.prepare();
    const double prep_ms = ms_since(tp);
    g_cfg.self_tid = (uint32_t)c->thread_id;
    WwOps op{sp, k, c, restore_to};
    if (restore_to == kNoTick) {
        std::memcpy(&op.blob.ctx, c, sizeof op.blob.ctx);
        jsp_fight_hash_get_state(&op.blob.ts);
    }
    const auto t0 = Clock::now();
    const jso::Result result = jso::capture(op, restore_to == kNoTick ? jso::Action::Commit : jso::Action::Rollback);
    if (result.failures & jso::Resume) resume_failed();
    rsx_guard_flush_log();   // threads running again: the guard's finding is printed whether or not this commit is logged
    report_census_prof();    // ENHANCEMENT (measurement, JSP_SNAPSHOT_CENSUS_PROF=1): threads running again
    const double total_ms = ms_since(t0);
    if ((result.failures & jso::Pause) && (!g_last_gate_reasons || (g_last_gate_reasons & jsy::PauseFailed))) sp.log_failure(restore_to == kNoTick ? "commit" : "rollback", k);
    if (op.gr.wake_stop) std::fprintf(stderr, "[snap] gate stopped after %u wakes (JSP_SNAPSHOT_GATE_WAKES=%u) at k=%u: the precheck never cleared, reasons=0x%X\n", op.gr.wakes, g_ev_lim.max_wakes, k, op.gr.reasons);
    const size_t nthr = sp.th.size();
    const auto tc = Clock::now();
    sp.close();
    const double close_ms = ms_since(tc);   // Toolhelp mode closes one handle per thread here (not counted in total= or prep=)
    const double save_ms = total_ms + prep_ms;   // gate (incl. the wait W) + collect + copy + prep, the earlier `save` figure
    const double real_ms = CostStats::real_of(save_ms, layout_ms, close_ms, audit_ms);   // what the main thread really pays for this operation
    char tail[700];   // cost fields, appended to the ww / ww-rollback line
    std::snprintf(tail, sizeof tail, " | thr=%s threads=%zu layout=%.3f close=%.3f audit=%.3f real=%.3f gate=%s prechecks=%u freezes=%u wakes=%u wake_timeouts=%u retry_waits=%u retry_timeouts=%u retry_immediate=%u presample=%.3f gate_wait=%.3f frozen=%.3f ring=%s first_reasons=0x%X first_notes=0x%X",
                  g_thr_registry ? "registry" : "toolhelp", nthr, layout_ms, close_ms, audit_ms, real_ms, g_gate_ev ? "ev" : "poll", op.gr.prechecks, op.gr.freezes, op.gr.wakes, op.gr.wake_timeouts, op.gr.retry_waits, op.gr.retry_timeouts, op.gr.retry_immediate,
                  op.presample_ms, op.gate_wait_ms, op.frozen_ms, g_ring_mode == jss::RingMode::PagePtr ? "ptr" : "copy2", op.gr.first_reasons, op.gr.first_notes);
    (restore_to == kNoTick ? g_cost_commit : g_cost_rb).add(save_ms, prep_ms, layout_ms, close_ms, audit_ms, op.presample_ms, op.gate_wait_ms, op.frozen_ms, op.collect_ms, op.apply_ms, op.gr, (uint32_t)nthr);
    if (g_ext && result.ok()) {   // ENHANCEMENT (measurement): the `[snap] cost window` percentiles (threads running)
        g_cw.note_op(restore_to == kNoTick, real_ms, op.presample_ms + op.gate_wait_ms, op.apply_ms, op.gr.freezes);
        if (restore_to != kNoTick) g_cw.rollback_done(real_ms);
    }
    if (g_sync != jsy::Mode::Off && (restore_to != kNoTick || op.gr.reasons || !op.gr.ok || ((!g_ext || snap_tick_log()) && (op.gr.verdict.notes != g_last_commit_notes || k % 300 == 0)))) {
        log_sync(restore_to == kNoTick ? "commit" : "rollback", k, op.gr, op.census_ms, op.no_saved);
        if (restore_to == kNoTick) g_last_commit_notes = op.gr.verdict.notes;   // commits log on a change of the note set, on a block, and every 300th tick
    }
    op.verification_line(restore_to == kNoTick ? "commit" : "rollback");
    if (op.fw_pages) { std::fprintf(stderr, "[snap] FROZEN-WRITES k=%u %s pages=%u first=0x%08X: guest pages written between the freeze's collect/reset and the resume (a thread the freeze did not cover, or main)\n", k, restore_to == kNoTick ? "commit" : "rollback", op.fw_pages, op.fw_first); }
    if (!result.ok()) { capture_failed("WW tick transaction", result.failures); return false; }
    if (restore_to == kNoTick) {
        g_ww_hist.push_back({g_ring.stats().last_commit_pages, op.collect_ms, op.apply_ms, total_ms + prep_ms});
        g_ss.saves++; g_ss.save_sum += real_ms; if (real_ms > g_ss.save_max) { g_ss.save_max = real_ms; g_ss.save_max_tick = k; }
        if (op.gr.tries > g_ss.gate_tries_max) g_ss.gate_tries_max = op.gr.tries;
        g_ss.gate_tries_sum += op.gr.tries; if (op.gr.tries > 1) g_ss.gate_retried++;
        for (int b = 0; b < 16; b++) if (op.gr.first_reasons & (1u << b)) g_ss.first_block[b]++;
        for (int b = 0; b < 16; b++) { if (op.gr.first_notes & (1u << b)) g_ss.first_note[b]++; if (op.gr.verdict.notes & (1u << b)) g_ss.final_note[b]++; }
        g_ss.suspend_sum += op.suspend_ms; if (op.suspend_ms > g_ss.suspend_max) g_ss.suspend_max = op.suspend_ms;
        if (g_t_rb_valid) { g_replay_saves += real_ms; g_replay_w += op.gate_wait_ms; }   // commits inside the replay count toward the recovery (real cost, W apart)
        if (!g_ext || snap_tick_log() || real_ms > 33.0) std::fprintf(stderr, "[snap] ww k=%u pages=%u collect=%.3f commit=%.3f suspend=%.3f total=%.3f prep=%.3f ms depth=%u pool_used=%zu reset=%.3f%s\n", k,
                     g_ring.stats().last_commit_pages, op.collect_ms, op.apply_ms, op.suspend_ms, total_ms, prep_ms, g_ring.depth_now(), g_ring.pool_used(), op.reset_ms, tail);
        // the pool is shared by all entries; when a commit evicted a tick that the round in progress still needs, the rollback is impossible. Say so now, loudly,
        // with the size that would have been enough, instead of failing the rollback later as an unnamed Apply failure.
        if (!g_ext && g_pass1 && g_ring.oldest() > g_save_k) {   // scripted rounds only; in ext mode the oldest entry leaving by capacity is normal and the loss signal is pool_evicted
            uint32_t mx = 0; for (const WwTick& w : g_ww_hist) mx = std::max(mx, w.pages);
            std::fprintf(stderr, "[snap] ww EVICTED k=%u round_start=%u oldest=%u depth=%u pool_used=%zu pool=%zu pages_last=%u pages_max=%u needed_pool>=%zu (JSP_SNAPSHOT_WW_POOL)\n", k, g_save_k,
                         g_ring.oldest(), g_ring.depth_now(), g_ring.pool_used(), g_ww_pool, g_ring.stats().last_commit_pages, mx, (size_t)mx * g_ww_depth);
            capture_failed("WW ROLLBACK-TARGET-EVICTED"); return false;
        }
        return true;
    }
    if (op.gr.site_waited || (op.gr.first_notes & jsy::NSiteMoved))   // a thread was parked elsewhere than at the target's save (first sample): JSP_SNAPSHOT_SITE=1 waits for it
        std::fprintf(stderr, "[snap] site k=%u -> tick %u tid=%u lr 0x%08X -> 0x%08X waited=%d gave_up=%d still_moved=%u\n", k, restore_to,
                     op.gr.site_tid, op.gr.site_lr_was, op.gr.site_lr_now, op.gr.site_waited ? 1 : 0, op.gr.site_gave_up ? 1 : 0, op.gr.verdict.site_moved);
    std::fprintf(stderr, "[snap] ww-rollback k=%u -> tick %u ok=1 pages=%u pending=%zu collect=%.3f rollback=%.3f suspend=%.3f total=%.3f ms prep=%.3f ms reset=%.3f%s\n", k, restore_to,
                 g_ring.stats().last_rollback_pages, g_ww_slots.size(), op.collect_ms, op.apply_ms, op.suspend_ms, total_ms, prep_ms, op.reset_ms, tail);
    rng_line("restored", restore_to);
    jsp_stepper_replay(1, restore_to);
    g_ss.rbs++; g_ss.rb_sum += real_ms; if (real_ms > g_ss.rb_max) g_ss.rb_max = real_ms;
    g_last_rb_ms = real_ms; g_last_rb_w = op.gate_wait_ms; g_replay_saves = 0; g_replay_w = 0;
    if (!g_ww_hist.empty()) {
        std::vector<double> t; std::vector<uint32_t> pg;
        for (const WwTick& w : g_ww_hist) { t.push_back(w.total_ms); pg.push_back(w.pages); }
        std::sort(t.begin(), t.end()); std::sort(pg.begin(), pg.end());
        double sum = 0; for (double x : t) sum += x;
        std::fprintf(stderr, "[snap] ww-summary round=%zu ticks=%zu pages/tick median=%u p95=%u max=%u | save ms/tick mean=%.3f p95=%.3f max=%.3f | query_fail=%llu overflows=%llu\n",
                     g_drv.round(), t.size(), pg[pg.size() / 2], pg[(size_t)((pg.size() - 1) * 0.95)], pg.back(), sum / t.size(), t[(size_t)((t.size() - 1) * 0.95)], t.back(),
                     (unsigned long long)g_ww_query_fail, (unsigned long long)g_ring.stats().overflows);
        g_ww_hist.clear();
    }
    return true;
}

// mean cost per commit and per rollback and the gate's wait counters; appended to the soak line.
// real = save (gate incl. the wait W + prep + collect + copy) + layout + close + audit: what the main thread pays per operation; real_excl_wait leaves W out.
// In the first-sample histogram above, GATE=ev counts the first UNFROZEN precheck (what made the gate wait), GATE=poll the first frozen sample (`first-sample blocks (precheck|frozen)`).
std::string cost_segment()
{
    auto one = [](const char* what, const CostStats& c) {
        char b[640]; const double n = c.n ? (double)c.n : 1.0;
        std::snprintf(b, sizeof b, " | cost %s n=%u ms mean: real=%.3f real_excl_wait=%.3f real_max=%.3f save_incl_wait=%.3f prep=%.3f layout=%.3f close=%.3f audit=%.3f presample=%.3f gate_wait=%.3f frozen=%.3f collect=%.3f apply=%.3f | per op: prechecks=%.2f freezes=%.2f freezes_max=%u wakes=%.2f retry_waits=%.2f | wake_timeouts=%u retry_timeouts=%u retry_immediate=%u threads_max=%u",
                      what, c.n, c.real / n, c.real_ex_w / n, c.real_max, c.save / n, c.prep / n, c.layout / n, c.close / n, c.audit / n, c.presample / n, c.gate_wait / n, c.frozen / n, c.collect / n, c.apply / n,
                      c.prechecks / n, c.freezes / n, c.freezes_max, c.wakes / n, c.retry_waits / n, c.wake_timeouts, c.retry_timeouts, c.retry_immediate, c.threads_max);
        return std::string(b);
    };
    uint32_t bumps[12] = {0};
    if (g_gate_bumps) ps3_gate_bump_counts(bumps);
    char b[640];
    const jtr::Registry& R = jsp_thread_registry();
    int n = std::snprintf(b, sizeof b, " | modes threads=%s gate=%s bumps=%d ring=%s | gate bumps park=%u flip_pending=%u flip_done=%u fifo_drained=%u jobs_zero=%u fiber_wait=%u lwm_release=%u transit_leave=%u", g_thr_registry ? "registry" : "toolhelp",
                          g_gate_ev ? "ev" : "poll", g_gate_bumps ? 1 : 0, g_ring_mode == jss::RingMode::PagePtr ? "ptr" : "copy2", bumps[0], bumps[1], bumps[2], bumps[3], bumps[4], bumps[5], bumps[6], bumps[7]);
    if (g_thr_registry && n > 0 && (size_t)n < sizeof b)
        std::snprintf(b + n, sizeof b - n, " | registry live=%u live_peak=%u births=%u cb_births=%u seeded=%u deaths=%u reaped=%u gated=%u full=%u audits=%u audit_ms mean=%.3f max=%.3f", R.live_count(), R.live_peak.load(), R.births.load(),
                      R.births.load() - R.seeded.load(), R.seeded.load(), R.deaths.load(), R.reaped.load(), R.gated.load(), R.full.load(), g_audits, g_audits ? g_audit_sum / g_audits : 0.0, g_audit_max);
    char w[160];
    std::snprintf(w, sizeof w, " | recovery incl. gate wait W: worst round W=%.3f ms (rollback + replay commits)", g_ss.recov_max_w);
    // a wait that ends proves nothing about site coverage (MutexHeld / RwHeld, jq_pending, fb_lock_held, flip_lock / gcm_pending have no bump of their own):
    // per reason, the prechecks woken by a bump that still saw the reason, and the waits that timed out with it (commits + rollbacks)
    static const char* const kNames[16] = {"lwm", "mutex", "rwlock", "evq", "sem", "transit", "strict", "pause", "lwm_unowned", "role_away", "role_absent", "role_queue", "rsx", "jobs", "fiber", "flip"};
    auto per = [&](const char* title, const uint32_t* c, const uint32_t* r) {
        std::string o = std::string(" | ") + title + ":";
        bool any = false;
        for (int i = 0; i < 16; i++) if (c[i] + r[i]) { char t[48]; std::snprintf(t, sizeof t, " %s=%u", kNames[i], c[i] + r[i]); o += t; any = true; }
        return any ? o : o + " none";
    };
    return one("commit", g_cost_commit) + one("rollback", g_cost_rb) + b + w + per("woken-but-same-reason-persists", g_cost_commit.persist, g_cost_rb.persist) + per("timeouts-by-reason", g_cost_commit.timeout_by, g_cost_rb.timeout_by);
}

// Cumulative soak summary, printed after every round (a killed run still has its last line; the checker reads the last one).
void soak_line(size_t scheduled)
{
    auto mean = [](double s, uint32_t n) { return n ? s / n : 0.0; };
    std::fprintf(stderr, "[snap] soak rounds=%u equal=%u mismatch=%u aborted=%u scheduled=%zu K_mean=%.2f K_max=%u | rollback ms max=%.3f mean=%.3f | save ms/tick (real: gate incl. wait + prep + layout + close + audit) max=%.3f (tick %u) mean=%.3f ticks=%u | "
                 "worst recovery ms=%.3f (K=%u: rollback %.3f + replay saves %.3f + game ticks; real costs incl. W) mean=%.3f | gate tries max=%u seed=%llu gap=%u | gate retried %u of %u commits, tries mean %.2f, suspend ms mean=%.3f max=%.3f, "
                 "first-sample blocks (%s): lwm=%u mutex=%u rwlock=%u evq=%u sem=%u transit=%u strict=%u pause=%u lwm_unowned=%u role_away=%u role_absent=%u role_queue=%u rsx=%u jobs=%u fiber=%u flip=%u | notes first/final: jobs=%u/%u fibers=%u/%u flip=%u/%u (a note-only boundary: commits taken with it set)%s\n", g_ss.rounds, g_ss.equal, g_ss.mismatch, g_ss.aborted, scheduled,
                 mean(g_ss.k_sum, g_ss.rounds), g_ss.k_max, g_ss.rb_max, mean(g_ss.rb_sum, g_ss.rbs), g_ss.save_max, g_ss.save_max_tick, mean(g_ss.save_sum, g_ss.saves), g_ss.saves,
                 g_ss.recov_max, g_ss.recov_max_k, g_ss.recov_max_rb, g_ss.recov_max_saves, mean(g_ss.recov_sum, g_ss.recov_n), g_ss.gate_tries_max, (unsigned long long)g_soak_seed, g_soak_gap,
                 g_ss.gate_retried, g_ss.saves, mean(g_ss.gate_tries_sum, g_ss.saves), mean(g_ss.suspend_sum, g_ss.saves), g_ss.suspend_max, g_gate_ev ? "precheck" : "frozen",
                 g_ss.first_block[0], g_ss.first_block[1], g_ss.first_block[2], g_ss.first_block[3], g_ss.first_block[4], g_ss.first_block[5], g_ss.first_block[6], g_ss.first_block[7], g_ss.first_block[8], g_ss.first_block[9], g_ss.first_block[10], g_ss.first_block[11], g_ss.first_block[12],
                 g_ss.first_block[13], g_ss.first_block[14], g_ss.first_block[15], g_ss.first_note[13], g_ss.final_note[13], g_ss.first_note[14], g_ss.final_note[14], g_ss.first_note[15], g_ss.final_note[15], cost_segment().c_str());
}
// Finished rounds are printed once (a round ends on the tick that may also ask for the next save).
void flush_rounds()
{
    static size_t reported = 0;
    for (; reported < g_drv.round() && reported < g_drv.results().size(); reported++) {
        const jss::RoundResult& rr = g_drv.results()[reported];
        std::fprintf(stderr, "[snap] round %zu done: S=%u K=%u %s bad_ticks=%u missing=%u first_bad_tick=%d first_bad_component=%s%s\n", reported, rr.S, rr.K,
                     rr.aborted ? "ABORTED-LAYOUT-CHANGE" : rr.equal ? "EQUAL" : "MISMATCH", rr.bad_ticks, rr.missing, rr.first_bad_tick,
                     rr.first_bad_comp >= 0 ? jss::kCompNameImpl[rr.first_bad_comp] : (rr.first_bad_tick ? "all-only" : "-"), rr.layout_diverged ? " layout-changed-in-replay" : "");
        if (!g_soak) continue;
        g_ss.rounds++; g_ss.k_sum += rr.K; if (rr.K > g_ss.k_max) g_ss.k_max = rr.K;
        if (rr.aborted) g_ss.aborted++; else if (rr.equal) g_ss.equal++; else g_ss.mismatch++;
        if (!rr.aborted && g_t_rb_valid) {   // load + replayed ticks + saves: from the rollback request to the end of the replay
            const double rec = ms_since(g_t_rb);
            g_ss.recov_n++; g_ss.recov_sum += rec;
            if (rec > g_ss.recov_max) { g_ss.recov_max = rec; g_ss.recov_max_k = rr.K; g_ss.recov_max_rb = g_last_rb_ms; g_ss.recov_max_saves = g_replay_saves; g_ss.recov_max_w = g_last_rb_w + g_replay_w; }
        }
        g_t_rb_valid = false;
        soak_line(g_drv.results().size());
    }
}
// The end of the run: the RESULT line, the marker off. Called once, by whichever tick finished the driver.
void finish_run() { report(); g_on = false; g_ps3_hle_mark_on = 0; jsp_stepper_abort(); }


// The Windows environment of jsf::ww_pass_end (jsp_snapshot_flow.h).
struct WwEnv {
    uint32_t k; ppu_context* c;
    bool on() const { return g_on; }
    bool ring_valid() const { return g_ring.valid(); }
    bool layout_changed(uint32_t) { return ww_layout_changed(); }
    bool baseline(uint32_t kk) { ww_baseline(kk, c, true); return g_on; }
    bool rebaseline(uint32_t kk) { ww_baseline(kk, c, false); return g_on; }
    bool commit(uint32_t kk) { return ww_transaction(kk, c, kNoTick); }
    bool rollback(uint32_t kk, uint32_t to) { g_t_rb = Clock::now(); g_t_rb_valid = true; return ww_transaction(kk, c, to); }
    void saved(uint32_t kk) { g_save_k = kk; g_pass1 = true; }
    void restored() { g_pass1 = false; }
    void flush_rounds() { ::flush_rounds(); }
    void finish() { finish_run(); }
    void note(const char* what, uint32_t kk) { std::fprintf(stderr, "[snap] round %zu layout: %s (tick %u)\n", g_drv.round(), what, kk); }
    void rng(const char* what, uint32_t kk) { if (g_on) rng_line(!std::strcmp(what, "save") ? "save" : g_pass1 ? "p1" : "p2", kk); }
};

void report()
{
    bool ok = !g_failed && g_drv.all_equal();
    uint32_t n_abort = 0;
    if (g_soak) {   // soak judges mismatches only; an aborted round (the saved layout changed) proves nothing either way and is counted (the checker's --max-aborted is the limit)
        ok = !g_failed && !g_drv.results().empty();
        for (const jss::RoundResult& r : g_drv.results()) { if (r.aborted) n_abort++; else if (!r.done || !r.equal) ok = false; }
    }
    for (size_t i = 0; i < g_drv.results().size(); i++) {
        const jss::RoundResult& r = g_drv.results()[i];
        if (g_soak && r.done && r.equal) continue;   // soak: the equal rounds were printed as they finished; the end lists only the rest
        std::fprintf(stderr, "[snap] round %zu S=%u K=%u %s bad_ticks=%u missing=%u first_bad_tick=%d first_bad_component=%s\n", i, r.S, r.K,
                     !r.done ? "NOT-FINISHED" : r.aborted ? "ABORTED-LAYOUT-CHANGE" : r.equal ? "EQUAL" : "MISMATCH", r.bad_ticks, r.missing, r.first_bad_tick,
                     r.first_bad_comp >= 0 ? jss::kCompNameImpl[r.first_bad_comp] : (r.first_bad_tick ? "all-only" : "-"));
    }
    if (g_soak) soak_line(g_drv.results().size());
    if (g_soak) std::fprintf(stderr, "[snap] RESULT %s aborted=%u)\n", ok ? "PASS (no mismatch;" : "FAIL (mismatch or failure;", n_abort);
    else std::fprintf(stderr, "[snap] RESULT %s\n", ok ? "PASS (all rounds equal)" : "FAIL");
}

}  // namespace

// --------------------------------------------------------------------------------------------------------------------------------
extern "C" int jsp_snapshot_init(uint32_t hash_start, uint32_t hash_n)
{
    const char* e = std::getenv("JSP_SNAPSHOT");
    if (!e || !*e || e[0] == '0') return 0;
    if (g_failed) { std::fprintf(stderr, "[snap] REFUSED: a previous capture failed; restart the process before retrying\n"); return 0; }
    { const char* xe = std::getenv("JSP_SNAPSHOT_EXT"); g_ext = xe && *xe && xe[0] != '0'; }
    { const char* gd = std::getenv("JSP_NET_RSX_WRITE_GUARD"); g_rg_on = (gd && gd[0] == '0') ? 0 : 1; }
    if (g_ext) ps3_rsx_guest_write_hook = rsx_write_hook;   // patch 7917: the write guard's observer

    const char* es = std::getenv("JSP_SNAPSHOT_AT");
    if (!g_ext && (!es || !*es)) { std::fprintf(stderr, "[snap] REFUSED: JSP_SNAPSHOT_AT=<tick> is required\n"); return 0; }
    g_S = g_ext ? 0 : (uint32_t)std::strtoul(es, nullptr, 10);
    const char* ek = std::getenv("JSP_SNAPSHOT_K");
    { const char* sk = std::getenv("JSP_SNAPSHOT_SOAK"); g_soak = sk && *sk && sk[0] != '0'; }
    if (g_soak) {   // the K list is drawn per round from the seed, JSP_SNAPSHOT_K is ignored
        const char* v;
        v = std::getenv("JSP_SNAPSHOT_SOAK_SEED"); if (v && *v) g_soak_seed = std::strtoull(v, nullptr, 0);
        v = std::getenv("JSP_SNAPSHOT_SOAK_GAP"); if (v && *v) g_soak_gap = (uint32_t)std::strtoul(v, nullptr, 10);
        v = std::getenv("JSP_SNAPSHOT_SOAK_ROUNDS"); if (v && *v) g_soak_max = (uint32_t)std::strtoul(v, nullptr, 10);
        g_ks.clear();
    } else if (g_ext) { g_ks.assign(1, 1u);   // no scripted rounds; the value only satisfies the depth check below
    } else if (!jss::parse_ks(ek && *ek ? ek : "60", &g_ks)) { std::fprintf(stderr, "[snap] REFUSED: JSP_SNAPSHOT_K is malformed (want 60,300,1800)\n"); return 0; }
    uint64_t sum = 0; for (uint32_t k : g_ks) sum += k;
    if (g_soak) { if (g_S < 1 || hash_start > g_S + 1 || hash_n < 2) { std::fprintf(stderr, "[snap] REFUSED: soak needs JSP_SNAPSHOT_AT >= 1 and the hash window to start at or before tick %u\n", g_S + 1); return 0; } }
    else if (!g_ext && (g_S < 1 || hash_start > g_S + 1 || (uint64_t)hash_start + hash_n < (uint64_t)g_S + sum + 1)) {
        std::fprintf(stderr, "[snap] REFUSED: hash window [%u,%llu) must cover ticks %u..%llu (set JSP_FIGHT_HASH_START=%u JSP_FIGHT_HASH=%llu)\n", hash_start,
                     (unsigned long long)hash_start + hash_n, g_S + 1, (unsigned long long)g_S + sum, g_S + 1, (unsigned long long)sum + 1);
        return 0;
    }
    const char* ev;
    ev = std::getenv("JSP_SNAPSHOT_SUSPEND"); g_suspend = !(ev && ev[0] == '0');
    ev = std::getenv("JSP_SNAPSHOT_SYNC"); if (ev && *ev) g_sync = ev[0] == '0' ? jsy::Mode::Off : ev[0] == '2' ? jsy::Mode::Wait : jsy::Mode::Log;
    ev = std::getenv("JSP_SNAPSHOT_SYNC_STRICT"); g_cfg.strict = ev && *ev && ev[0] != '0';
    ev = std::getenv("JSP_SNAPSHOT_SITE"); g_cfg.block_site = ev && *ev && ev[0] != '0';
    ev = std::getenv("JSP_SNAPSHOT_SITE_MS"); if (ev && *ev) g_cfg.site_wait_ms = (unsigned)std::strtoul(ev, nullptr, 10);
    ev = std::getenv("JSP_SNAPSHOT_SYNC_MS"); if (ev && *ev) g_sync_ms = (unsigned)std::strtoul(ev, nullptr, 10);
    ev = std::getenv("JSP_SNAPSHOT_SYNC_TRIES"); if (ev && *ev) g_sync_tries = (unsigned)std::strtoul(ev, nullptr, 10);
    ev = std::getenv("JSP_SNAPSHOT_QUIESCE"); g_quiesce = ev && *ev ? (ev[0] == '2' ? 2 : ev[0] == '1' ? 1 : 0) : 0;
    ev = std::getenv("JSP_SNAPSHOT_QUIESCE_RSX"); g_cfg.rsx_idle = ev && *ev && ev[0] != '0';
    {   // the IO mapping is saved whenever the write-watch ring is on (a rollback without it diverged in 2 of 2 runs), unless JSP_SNAPSHOT_IO=0 says otherwise
        const char* w = std::getenv("JSP_SNAPSHOT_WW"); const bool ww_on = w && *w && w[0] != '0';
        ev = std::getenv("JSP_SNAPSHOT_IO"); g_io = ev && *ev ? ev[0] != '0' : ww_on;
        if (g_io && !(ev && *ev)) std::fprintf(stderr, "[snap] JSP_SNAPSHOT_IO defaults to 1 with JSP_SNAPSHOT_WW (set JSP_SNAPSHOT_IO=0 to leave the GCM IO mapping out)\n");
    }
    if (g_io) {   // the IO block is only safe to overwrite with the RSX idle and the video thread parked, so IO=1 turns both gates on
        if (g_quiesce < 2) { std::fprintf(stderr, "[snap] JSP_SNAPSHOT_IO=1 implies JSP_SNAPSHOT_QUIESCE=2 and JSP_SNAPSHOT_QUIESCE_RSX=1\n"); g_quiesce = 2; }
        g_cfg.rsx_idle = true;
    }
    if (g_quiesce) {   // frame-pipeline park points; 2 = enforce, which needs the gate to wait
        if (g_sync == jsy::Mode::Off) g_sync = jsy::Mode::Log;
        if (g_quiesce >= 2) g_sync = jsy::Mode::Wait;
        default_roles(&g_roles);
        g_cfg.roles = &g_roles; g_cfg.quiesce_block = g_quiesce >= 2;
        if (g_quiesce >= 2) {   // the job / fiber / flip boundaries enforce with the rest of mode 2; =0 turns one off for an A/B (it stays a census note)
            auto on = [](const char* n) { const char* x = std::getenv(n); return !(x && *x && x[0] == '0'); };
            g_cfg.block_jobs = on("JSP_SNAPSHOT_QUIESCE_JOBS"); g_cfg.block_fibers = on("JSP_SNAPSHOT_QUIESCE_FIBERS");
            // WORKAROUND TO REPLACE: the flip boundary is a note unless JSP_SNAPSHOT_QUIESCE_FLIP=1. Run soak-s1 (flip on) took 3-4 gate tries per commit (suspend 8-11 ms vs 1.7 ms in an earlier
            // run, same K) and the only new per-tick waits were the 7913 boundaries; a flip stays WAITING until the next vblank tick. Earlier runs passed 3 of 3 rounds with put == get only. Whether the flip wait is
            // needed for equal replays is decided by the A/B (SNAP_QUIESCE_FLIP=0 vs 1), not assumed.
            { const char* x = std::getenv("JSP_SNAPSHOT_QUIESCE_FLIP"); g_cfg.block_flip = x && *x && x[0] != '0'; }
        }
    }
    ev = std::getenv("JSP_SNAPSHOT_LS"); g_ls = ev && *ev && ev[0] != '0';
    ev = std::getenv("JSP_SNAPSHOT_DIRTY"); g_dirty = ev && *ev && ev[0] != '0';
    ev = std::getenv("JSP_SNAPSHOT_WW"); g_ww = ev && *ev && ev[0] != '0' ? (ev[0] == '2' ? 2 : 1) : 0;
    ev = std::getenv("JSP_SNAPSHOT_WW_DEPTH"); if (ev && *ev) g_ww_depth = (uint32_t)std::strtoul(ev, nullptr, 10);
    ev = std::getenv("JSP_SNAPSHOT_WW_POOL"); if (ev && *ev) g_ww_pool = (size_t)std::strtoull(ev, nullptr, 10);
    if (g_ww && g_io && !(ev = std::getenv("JSP_SNAPSHOT_WW_POOL"), ev && *ev)) {   // ~3000 pages/tick with the IO block (run q2-io-ww8) x depth 10 overflowed 20480
        g_ww_pool = (size_t)g_ww_depth * 6144; std::fprintf(stderr, "[snap] JSP_SNAPSHOT_IO=1: undo pool defaults to depth*6144 = %zu pages (JSP_SNAPSHOT_WW_POOL overrides)\n", g_ww_pool);
    }
    if (g_ww) {
        if (!jsp_vm_write_watch) { std::fprintf(stderr, "[snap] REFUSED: JSP_SNAPSHOT_WW needs the MEM_WRITE_WATCH reservation (set it in the environment before start)\n"); return 0; }
        for (uint32_t k : g_ks)
            if (k > g_ww_depth) { std::fprintf(stderr, "[snap] REFUSED: JSP_SNAPSHOT_K item %u is deeper than JSP_SNAPSHOT_WW_DEPTH=%u\n", k, g_ww_depth); return 0; }
        if (g_dirty) { std::fprintf(stderr, "[snap] JSP_SNAPSHOT_DIRTY ignored with JSP_SNAPSHOT_WW (the ww lines carry the pages per tick)\n"); g_dirty = false; }
    }
    {   // cost switches (see the header); each defaults to the earlier path
        const char* x;
        x = std::getenv("JSP_SNAPSHOT_THREADS");
        if (x && *x && std::strcmp(x, "registry") && std::strcmp(x, "toolhelp")) { std::fprintf(stderr, "[snap] REFUSED: JSP_SNAPSHOT_THREADS=%s (want toolhelp or registry)\n", x); return 0; }
        g_thr_registry = x && !std::strcmp(x, "registry");
        if (g_thr_registry && !jsp_thread_registry_enabled()) { std::fprintf(stderr, "[snap] REFUSED: JSP_SNAPSHOT_THREADS=registry must be in the environment at process start (the TLS callback reads it once)\n"); return 0; }
        x = std::getenv("JSP_SNAPSHOT_GATE_JOBSERVER"); g_gate_jobserver = x && *x && x[0] == '1'; g_cfg.block_jobserver = g_gate_jobserver;
        x = std::getenv("JSP_SNAPSHOT_FROZEN_WRITES"); g_fw_on = !(x && *x && x[0] == '0');
        // ENHANCEMENT (rollback latency; defaults on in rollback mode, off otherwise; =0 turns each off for an A/B): the gate's unfrozen precheck before every freeze
        // (jsy::Cfg::precheck_us, total per gate), the background thread audit left running by the freeze (freeze_exempt). JSP_SNAPSHOT_COPY_THREADS: see pcopy.
        x = std::getenv("JSP_SNAPSHOT_PRECHECK_US"); g_cfg.precheck_us = x && *x ? (unsigned)std::strtoul(x, nullptr, 10) : (g_ext ? 20000u : 0u);
        x = std::getenv("JSP_SNAPSHOT_AUDIT_EXEMPT"); g_audit_exempt = x && *x ? x[0] != '0' : g_ext;
        std::fprintf(stderr, "[snap] latency switches: precheck_us=%u (JSP_SNAPSHOT_PRECHECK_US) audit_exempt=%d (JSP_SNAPSHOT_AUDIT_EXEMPT) copy_threads=%s (JSP_SNAPSHOT_COPY_THREADS, default 3 in rollback) cost window every %s s (JSP_SNAPSHOT_COST_WINDOW_S)\n",
                     g_cfg.precheck_us, g_audit_exempt ? 1 : 0, std::getenv("JSP_SNAPSHOT_COPY_THREADS") ? std::getenv("JSP_SNAPSHOT_COPY_THREADS") : "default", std::getenv("JSP_SNAPSHOT_COST_WINDOW_S") ? std::getenv("JSP_SNAPSHOT_COST_WINDOW_S") : "10");
        x = std::getenv("JSP_SNAPSHOT_BIRTHGATE");
        if (x && *x == '0') { if (g_thr_registry) { jsp_thread_registry().gate_births.store(0); std::fprintf(stderr, "[snap] JSP_SNAPSHOT_BIRTHGATE=0: the registry does not hold threads born or exiting during a freeze (A/B)\n"); } else std::fprintf(stderr, "[snap] JSP_SNAPSHOT_BIRTHGATE=0 ignored without JSP_SNAPSHOT_THREADS=registry\n"); }
        x = std::getenv("JSP_SNAPSHOT_THREADS_AUDIT"); if (x && *x) g_audit_every = (uint32_t)std::strtoul(x, nullptr, 10);
        x = std::getenv("JSP_SNAPSHOT_GATE");
        if (x && *x && std::strcmp(x, "ev") && std::strcmp(x, "poll")) { std::fprintf(stderr, "[snap] REFUSED: JSP_SNAPSHOT_GATE=%s (want poll or ev)\n", x); return 0; }
        g_gate_ev = x && !std::strcmp(x, "ev");
        if (g_gate_ev && g_ext) { std::fprintf(stderr, "[snap] REFUSED: JSP_SNAPSHOT_GATE=ev with the net session (the event gate has no time budget, no idle hook and no commit config)\n"); return 0; }
        x = std::getenv("JSP_SNAPSHOT_GATE_BUMPS"); g_gate_bumps = g_gate_ev || (x && *x && x[0] != '0');
        x = std::getenv("JSP_SNAPSHOT_GATE_TIMEOUT_MS"); if (x && *x) g_ev_lim.timeout_ms = (unsigned)std::strtoul(x, nullptr, 10);
        x = std::getenv("JSP_SNAPSHOT_GATE_WAKES"); if (x && *x) g_ev_lim.max_wakes = (unsigned)std::strtoul(x, nullptr, 10);
        x = std::getenv("JSP_SNAPSHOT_GATE_RETRY_MS"); if (x && *x) g_ev_lim.retry_wait_ms = (unsigned)std::strtoul(x, nullptr, 10);
        x = std::getenv("JSP_SNAPSHOT_GATE_PUMP");
        if (x && *x && x[0] != '0') { std::fprintf(stderr, "[snap] REFUSED: JSP_SNAPSHOT_GATE_PUMP was removed (it ran the guest's flip handler on the main thread; see patch 7914)\n"); return 0; }
        x = std::getenv("JSP_SNAPSHOT_RING");
        if (x && *x && std::strcmp(x, "ptr") && std::strcmp(x, "copy2")) { std::fprintf(stderr, "[snap] REFUSED: JSP_SNAPSHOT_RING=%s (want copy2 or ptr)\n", x); return 0; }
        g_ring_mode = x && !std::strcmp(x, "ptr") ? jss::RingMode::PagePtr : jss::RingMode::Copy2;
        if ((g_gate_ev || g_thr_registry || g_ring_mode == jss::RingMode::PagePtr) && !g_ww) { std::fprintf(stderr, "[snap] REFUSED: JSP_SNAPSHOT_THREADS/GATE/RING apply to the incremental mode only (set JSP_SNAPSHOT_WW)\n"); return 0; }
    }
    if (!jss::parse_ranges(std::getenv("JSP_SNAPSHOT_EXCLUDE"), &g_user_ex)) { std::fprintf(stderr, "[snap] REFUSED: JSP_SNAPSHOT_EXCLUDE is malformed (want 0xADDR+0xSIZE,...)\n"); return 0; }
    if (g_soak) {
        if (!g_ww) { std::fprintf(stderr, "[snap] REFUSED: JSP_SNAPSHOT_SOAK needs JSP_SNAPSHOT_WW (full-copy rounds are too slow for a soak)\n"); return 0; }
        if (g_ww_depth < 1) { std::fprintf(stderr, "[snap] REFUSED: JSP_SNAPSHOT_WW_DEPTH must be >= 1\n"); return 0; }
        const uint64_t last = (uint64_t)hash_start + hash_n - 1;   // a round compares a hash for every tick, so the last one must lie inside the hash window
        g_drv.configure_soak(g_S, g_ww_depth, g_soak_seed, g_soak_gap, g_soak_max, last);
        if (g_drv.finished()) { std::fprintf(stderr, "[snap] REFUSED: the hash window ends at tick %llu, too early for a soak round at S=%u\n", (unsigned long long)last, g_S); return 0; }
        std::fprintf(stderr, "[snap] soak: seed=%llu gap=%u K in 1..%u rounds_cap=%u (0 = until tick %llu, the end of the hash window)\n", (unsigned long long)g_soak_seed, g_soak_gap, g_ww_depth, g_soak_max, (unsigned long long)last);
    } else g_drv.configure(g_S, g_ks);
    g_on = true;
    std::fprintf(stderr, "[snap] JSP_SNAPSHOT on: S=%u rounds=%zu (K sum %llu) suspend=%d ls=%d dirty=%d ww=%d (depth %u, pool %zu pages) user_exclude=%zu\n", g_S, g_ks.size(), (unsigned long long)sum, (int)g_suspend, (int)g_ls, (int)g_dirty, g_ww, g_ww_depth, g_ww_pool, g_user_ex.size());
    if (g_sync != jsy::Mode::Off) {
        for (const char* n : {"sys_lwcond_wait", "sys_timer_usleep", "sys_timer_sleep", "sys_ppu_thread_exit", "sys_ppu_thread_join", "sys_cond_wait", "sys_semaphore_wait",
                              "sys_event_queue_receive", "sys_mutex_lock", "sys_event_flag_wait"}) g_cfg.add_parked(ps3_compute_nid(n));
        for (const char* n : {"sys_lwmutex_lock", "sys_lwmutex_trylock"}) g_cfg.add_transit(ps3_compute_nid(n));
        g_ps3_hle_mark_on = 1;   // patch 7911: ps3_hle_call / lv2_syscall record what each thread is inside
        if (!g_suspend) std::fprintf(stderr, "[snap] WARNING JSP_SNAPSHOT_SUSPEND=0: the sync census reads tables that other threads are changing (it races)\n");
    }
    std::fprintf(stderr, "[snap] sync census mode=%d (0 off, 1 log, 2 wait for idle) tries=%u strict=%d quiesce=%d (0 off, 1 park points in the log, 2 enforce) rsx_idle=%d io_mapping_saved=%d\n", (int)g_sync, g_sync_tries,
                 g_cfg.strict ? 1 : 0, g_quiesce, g_cfg.rsx_idle ? 1 : 0, g_io ? 1 : 0);
    g_ev_lim.max_freezes = g_sync_tries;
    if (g_gate_bumps) {   // arm patch 7914's bump sites (GATE=ev, or GATE_BUMPS=1 under poll); site 0 fires when a thread enters an HLE call at one of the roles' park lrs
        if (g_sync == jsy::Mode::Off || !g_cfg.roles) std::fprintf(stderr, "[snap] note: JSP_SNAPSHOT_GATE=ev without the quiesce roles: only sites 1-7 other than 0 can wake the gate\n");
        uint32_t lrs[8]; int n = 0;
        if (g_cfg.roles) for (int i = 0; i < g_cfg.roles->n; i++) for (int j = 0; j < g_cfg.roles->r[i].nsite && n < 8; j++) lrs[n++] = g_cfg.roles->r[i].site[j].lr;
        ps3_gate_arm(1, lrs, n);
        ps3_gate_arm_transit(g_cfg.nid_transit, g_cfg.n_transit);   // site 7 fires when a thread leaves an lwmutex lock / trylock (and kind 3 / syscall 97, 99 transits)
    }
    if (g_thr_registry) {
        const uint32_t births0 = jsp_thread_registry().births.load();   // registrations by the TLS callback before the seeding below
        const unsigned seeded = jsp_thread_registry_seed();
        std::fprintf(stderr, "[snap] thread registry on: live=%u seeded_now=%u (threads alive before the TLS callback saw them) cb_births_before_seed=%u audit_every=%u\n", jsp_thread_registry().live_count(), seeded, births0, g_audit_every);
        if (births0 == 0) std::fprintf(stderr, "[snap] WARNING: the exe's TLS callback registered no thread before init, not even the main thread (DLL_PROCESS_ATTACH not delivered?); the baseline audit decides whether it is delivered for new threads\n");
    }
    std::fprintf(stderr, "[snap] cost modes: threads=%s gate=%s (timeout %u ms, retry wait %u ms, max wakes %u) ring=%s\n", g_thr_registry ? "registry" : "toolhelp", g_gate_ev ? "ev" : "poll", g_ev_lim.timeout_ms,
                 g_ev_lim.retry_wait_ms, g_ev_lim.max_wakes, g_ring_mode == jss::RingMode::PagePtr ? "ptr" : "copy2");
    return 1;
}

// Tap from hash_tick(): the hash components of tick n. v = {all, rng, glob, seed, setup, cam, fi, pad}.
extern "C" void jsp_snapshot_tap(uint32_t n, const uint64_t* v)
{
    if (!g_on) return;
    g_rec_n = n; g_rec.have = true;
    for (int i = 0; i < jss::kComps; i++) g_rec.v[i] = v[i];
}

// A counted scene pass ended and the hash line of tick k (if in the window) was produced. Runs on the main thread, outside g_mu.
static void pass_end_impl(uint32_t k, ppu_context* c);
// the tick that closes pass 2 ends the replay (fast clock off, effects gate off).
extern "C" void jsp_snapshot_pass_end(uint32_t k, ppu_context* c)
{
    if (!g_on) return;
    const bool was_p2 = g_drv.in_pass2();
    pass_end_impl(k, c);
    if (was_p2 && !g_drv.in_pass2()) jsp_stepper_replay(0, k);
}
static void pass_end_impl(uint32_t k, ppu_context* c)
{
    jss::Rec r;
    if (g_rec.have && g_rec_n == k) r = g_rec;
    g_rec.have = false;
    if (g_ext) {   // rollback session: the netcode decides when to commit / roll back (jsp_snapshot_ext_*); here only the per-tick hash is kept
        if (r.have) { g_ext_hash = (uint32_t)(r.v[0] ^ (r.v[0] >> 32)); g_ext_hash_tick = k; }
        return;
    }
    if (g_ww) {   // incremental mode: the decision flow is jsp_snapshot_flow.h (host-tested with a fake environment); this is its Windows environment
        WwEnv env{k, c};
        jsf::ww_pass_end(env, g_drv, k, r);
        return;
    }
    if (g_drv.finished()) return;
    const jss::Act a = g_drv.on_tick(k, r);
    flush_rounds();
    if (g_drv.finished()) { finish_run(); return; }
    if (a == jss::Act::Save) { do_save(k, c); return; }
    if (a == jss::Act::Restore) { do_restore(k, c); return; }
    if (g_active) rng_line(g_pass1 ? "p1" : "p2", k);
    if (g_dirty && g_pass1 && g_shadow.valid && k > g_save_k) {
        const auto t0 = Clock::now();
        const size_t d = g_shadow.diff_update(vm_base);
        g_dirty_hist.push_back(d);
        std::fprintf(stderr, "[snap] dirty k=%u pages=%zu (%.2f MB) pass %.2f ms\n", k, d, d * 4096.0 / 1048576.0, ms_since(t0));
    }
}

// ---- rollback session: external driver (JSP_SNAPSHOT_EXT=1 with JSP_SNAPSHOT_WW=1). ------------------------------------------------
// The netcode (jsp_net_game.cpp) calls these at ITS boundary (the BeginFrame entry wrapper), with the
// main thread's ppu_context of that call. Returns: 1 ok, 2 ok but the committed layout changed (re-baselined: every older rollback target is gone, the caller
// must end the session), 0 fatal (capture_failed ran). *hash = the port's per-tick hash of tick k (0 when the tick has none).
extern "C" int jsp_snapshot_ext_active(void) { return g_on && g_ext && g_ww; }
// FIX (rollback determinism, jsp_net_game.cpp background barrier): SPU work in flight right now, threads running (racy, read-only): SPURS job-chain jobs + the HLE
// queue (ps3_spurs_census out[0] + out[1]) + the job server's dispatched records and its busy word (sample_jobserver; 0 until the manager was learned at a gate).
extern "C" uint32_t jsp_snapshot_spu_busy(void)
{
    uint32_t sp[7] = {0, 0, 0, 0, 0, 0, 0};
    ps3_spurs_census(sp);
    jsy::View v; sample_jobserver(&v);
    return sp[0] + sp[1] + (v.js_valid ? (v.js_bits != 0 ? 1u : 0u) + v.js_state2 : 0u);
}
static uint32_t ext_hash_of(uint32_t k) { return g_ext_hash_tick == k ? g_ext_hash : 0; }
extern "C" int jsp_snapshot_ext_commit(uint32_t k, ppu_context* c, uint32_t* hash)
{
    if (!jsp_snapshot_ext_active()) return 0;
    WwEnv env{k, c};
    *hash = ext_hash_of(k);
    if (!env.ring_valid()) return env.baseline(k) ? 1 : 0;      // the first call of a session: the full baseline
    const bool changed = env.layout_changed(k);
    if (!g_on) return 0;
    if (changed) return env.rebaseline(k) ? 2 : 0;
    const bool ok = env.commit(k);
    if (ok) { g_cw.saved(k, true); g_cw.maybe_print(false); }   // ENHANCEMENT (measurement): threads running
    return ok ? 1 : 0;
}
// ENHANCEMENT (rollback latency): the per-tick hash of tick k without a commit (a save that can never be loaded takes no snapshot). 0 = none.
extern "C" uint32_t jsp_snapshot_ext_hash(uint32_t k)
{
    if (!jsp_snapshot_ext_active()) return 0u;
    const uint32_t h = ext_hash_of(k);
    if (h) g_cw.saved(k, false);   // a hash-only save of a re-run tick also ends its recovery (cost window)
    return h;
}
// Restore the state with `to` completed ticks while `k` ticks are done now.
extern "C" int jsp_snapshot_ext_rollback(uint32_t k, uint32_t to, ppu_context* c)
{
    if (!jsp_snapshot_ext_active()) return 0;
    WwEnv env{k, c};
    if (!env.ring_valid()) return 0;
    const bool changed = env.layout_changed(k);
    if (!g_on) return 0;
    if (changed) { std::fprintf(stderr, "[snap] ext: layout changed at the rollback request (tick %u): not rolled back\n", k); return 0; }
    g_cw.rollback_begin(k, to);   // ENHANCEMENT (measurement): the recovery runs until the save of tick k (cost window)
    const bool ok = env.rollback(k, to);
    if (!ok) g_cw.rollback_failed();
    return ok ? 1 : 0;
}
// Entries the ring evicted because its page POOL was short while an entry slot was free (the reachable depth shrank). 0 = fine; > 0 = the pool is too small
// (JSP_SNAPSHOT_WW_POOL). The SPAN newest-oldest is not a signal: after a rollback of >= 2 ticks it is below the depth until the ring refills.
extern "C" uint64_t jsp_snapshot_ext_pool_evicted(void)
{
    if (!jsp_snapshot_ext_active() || !g_ring.valid()) return 0;
    return g_ring.stats().pool_evicted;
}

// Rollback mode: the session owner (jsp_net_game.cpp) sets the gate's wall-time budget (kGateBudgetMs), resets it after every session update, and gives the gate a
// hook that polls the session while a commit waits (so a long frame cannot starve the peer's disconnect timer).
extern "C" void jsp_snapshot_ext_set_budget_ms(unsigned ms) { g_sync_ms = ms; g_gate_used_ms = 0; }
extern "C" void jsp_snapshot_ext_budget_reset(void) { g_gate_used_ms = 0; }
extern "C" void jsp_snapshot_ext_set_idle_hook(void (*fn)()) { g_idle_hook = fn; }
extern "C" void jsp_snapshot_ext_guard_summary(void)   // session end: how often RSX-side writes into saved memory happened (the log-only measurement)
{
    if (g_ext) g_cw.maybe_print(true);   // ENHANCEMENT (measurement): the last cost window of the session
    if (!g_ext || !g_rg_commits) return;
    rsx_guard_flush_log();
    std::fprintf(stderr, "[snap] RSX write guard summary: %u commits checked, %u with the previous capture RSX-pending, %u of those had saved-memory RSX writes (data %u, label/semaphore %u, ring wraps %u); %u RSX-side main-memory writes noted in all; hazard=%d; ring extensions (committed set grew, history kept): %u; frozen layout walks %llu (mean %.3f ms, max %.3f ms, skipped %llu), re-checks %u\n",
                 g_rg_commits, g_rg_armed, g_rg_hit_commits, g_rg_data, g_rg_label, g_rg_wrapped, g_rg_noted, g_rsx_hazard ? 1 : 0, g_ww_grown, (unsigned long long)g_walk_n, g_walk_n ? g_walk_ms_sum / (double)g_walk_n : 0.0, g_walk_ms_max, (unsigned long long)g_walk_skipped, g_layout_redo_count);
}
// FIX (same-link rematch, run native-rm1): the rollback session is over. Its ring, per-tick capture views, last hash and the cost window's open recovery are
// dropped, so the next session in this process starts with a full baseline at its tick 1 (jsp_snapshot_ext_commit -> ww_baseline resets the write watch,
// the layout, the guard records and the ring). Without it the rematch's first commit failed (the ring's newest tick was the first match's last one; "WW tick
// transaction failures=0x4"), which also set g_failed and left rollback off for the rest of the process. A capture that FAILED stays failed (g_failed: the
// guest state is unverifiable; restart the process). Main thread, no capture in progress.
extern "C" void jsp_snapshot_ext_session_end(void)
{
    if (!g_ext) return;
    g_ring.invalidate();
    g_views.clear();
    g_ext_hash = 0; g_ext_hash_tick = 0;
    g_cw.rollback_failed();   // no recovery is open across sessions
    g_gate_used_ms = 0;
}
extern "C" int jsp_snapshot_ext_rsx_write_hazard(void) { return g_rsx_hazard ? 1 : 0; }   // an RSX-side write into guest memory happened with commands pending at a commit (write guard)
