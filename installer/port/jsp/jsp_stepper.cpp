// jsp_stepper.cpp -- Windows glue of the rollback tick stepper. ENHANCEMENT (experimental rollback netcode): every switch off by default (nothing installed,
// zero cost); JSP_ROLLBACK=1 sets JSP_STEP_TURBO=1 (jsp_net_game.cpp), and the session turns the canonical pad on at match start.
// The logic is pure and host-tested: jsp_step_input.h (canonical input), jsp_step_effects.h (side-effect gate), jsp_stepper.h (control state machine).
//
//   JSP_STEP_TURBO=1       a rollback replay runs on a virtual clock: no 60 Hz grid, no sleeping, no present of the intermediate frames. jsp_snapshot.cpp
//                          calls jsp_stepper_replay(1) when it restores and jsp_stepper_replay(0) on the tick that ends the replay; the frame clock
//                          (main.cpp) asks jsp_rollback_clock_turbo() first on every pass. While it is on, one pass = the FIFO is
//                          drained, and when the guest has queued a flip (the drain reached it) the vblank / flip ticks the grid would have delivered
//                          (2 vblanks per flip, the N the game's EndFrame waits for) fire at once and the flip is taken WITHOUT a present.
//   JSP_STEP_CANON=1       the pad record the game reads is the canonical one (patch 7915: g_cellpad_canon_hook): from tick JSP_STEP_CANON_FROM (default
//                          JSP_SNAPSHOT_AT) every slot in the session mask gets exactly one packet per tick (len 0 on a second read of the same tick, as
//                          the drain contract of cellPadGetData), the first read of a new tick latches the device, a replayed tick reads the table.
//                          JSP_STEP_CANON_SLOTS=1 (default; bit mask, 1 = slot 0 only: the keyboard / first pad). JSP_STEP_CANON_POISON=1: a pad filter
//                          (chained after the earlier ones, so it sees the source record) ORs garbage into port 0 while a replay runs, i.e. the SOURCE
//                          of the replayed tick differs from its first pass. Without CANON that must make the replay diverge (the control run proves the
//                          poison reaches the simulation); with CANON the replay must stay EQUAL (the recorded input, not the source, is what is read).
//   (patch 7915 also adds g_cellpad_info_hook for the session slots' connected status; info_hook below installs it while a rollback session runs.)
//   Side effects: jsp_effects_request() is the one question the audio / file / save / net wrappers ask (jsp_step_effects.h); no caller is wired yet
//   (the CRI atom start/stop addresses are not located), so it is inert until the audio side wires it.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <windows.h>
#include "jsp_step_effects.h"
#include "jsp_step_input.h"
#include "jsp_stall_rule.h"
#include "cellPad.h"   // toolkit libs/input (CMakeLists: this file only): CellPadInfo2 and patch 7915's g_cellpad_canon_hook / g_cellpad_info_hook

extern "C" {
void cellGcmTickVBlank(void);
void cellGcm_fifo_kick_wait(unsigned ms);
void cellGcmTickFlip(void);
int cellGcm_take_flip_pending(void);
int cellGcm_flip_ready(void);
uint32_t cellGcm_vblank_delivered(void);   // patch 7915: vblank notifications the GCM pump delivered to the guest
uint32_t cellGcm_flip_request_count(void);
extern void (*g_cellpad_filter)(uint32_t port_no, uint16_t* buttons, uint16_t* lx, uint16_t* ly);   // patch 6501
void cellGcm_rsx_process_fifo(void);
int cellGcm_flip_write_off_lost(int want_write_off, uint32_t* out);   // patch 7927
struct JspTickState { uint32_t done; uint32_t anc_done; uint8_t anc_set, anc_skip; };
void jsp_fight_hash_get_state(JspTickState* s);   // jsp_fight_hash.cpp: done = completed scene passes
// patch 7915: g_cellpad_canon_hook and g_cellpad_info_hook are declared by cellPad.h
}

namespace {

std::atomic<int> g_turbo{0};          // the replay clock is on
std::atomic<int> g_lockstep{0};       // FIX (rollback determinism): the live ticks of a match run on the lockstep clock (jsp_stepper_lockstep)
std::atomic<int> g_in_replay{0};      // a replay is running (with or without the clock)
bool g_turbo_on_switch = false, g_canon = false, g_poison = false;
uint32_t g_canon_from = 0;
uint32_t g_slots = 1;
std::mutex g_mu;
jsi::Table g_table;
jsi::Table* g_ext_table = nullptr;     // rollback session: the session's own table (the Stepper's); canon_hook and the replay bookkeeping use it instead of g_table
jsi::Pad g_hw[jsi::kSlots];           // the last device record seen per port (source of the local pad given to the netcode)
bool g_hw_have[jsi::kSlots] = {};
inline jsi::Table& tbl() { return g_ext_table ? *g_ext_table : g_table; }
bool g_masked = false;
jse::Gate g_gate;
// per replay (reset when the replay starts): the log line of a replay must not show cumulative numbers
// (atomics: the frame clock thread, the main thread and the pad filter all update them; reset by the main thread when a replay starts)
struct Counters {
    std::atomic<uint64_t> flips{0}, idle{0}, vblank_waits{0}, ack_timeouts{0}, packets{0}, replay_reads{0}, poisoned{0}, src_differs{0};
    void reset() { flips = 0; idle = 0; vblank_waits = 0; ack_timeouts = 0; packets = 0; replay_reads = 0; poisoned = 0; src_differs = 0; }
};
Counters g_c;
uint64_t g_replays = 0;
uint32_t g_rb_tick = 0;              // the tick the rollback went back to (the gate's end_replay needs it, not the tick that ended the replay)
ULONGLONG g_turbo_t0 = 0;
std::atomic<ULONGLONG> g_last_flip{0};
void (*g_prev_filter)(uint32_t, uint16_t*, uint16_t*, uint16_t*) = nullptr;
// DIAGNOSTIC (rollback freezes, smoothness audit cause 1): replays that fired an idle vblank took 0.5-1.1 s. The guest wait behind them is not proven from the
// code (see the comment at the idle vblank below), so every re-run that goes 100 ms without a flip logs what each thread is in, the flip state, the main
// thread's place in the boundary and the guest's vblank counter, once per stall and again when the idle vblank fires. Nothing here changes the clock.
std::atomic<uint32_t> g_rr_ticks{0};          // re-run ticks started in this replay (jsp_stepper_rerun_tick, main thread)
std::atomic<uint32_t> g_rr_drain_max_us{0};   // the longest FIFO drain of this replay's turbo passes (a cold pipeline compiles inside the drain)
int g_rr_stall_logged = 0;                    // frame clock thread only: this stall was logged at 100 ms
uint32_t g_rr_stall_lines = 0;                // frame clock thread only: cap on stall lines per process
std::atomic<int> g_rr_ls_b{0};                // the lockstep clock still owed the vblank after a flip when the replay started (delivered after the replay)
// FIX (rollback freezes, smoothness audit cause 1): vblanks the first run of the restored tick got after its save point, owed to re-run tick 1 (see
// owed_vblank_pass). Set by the main thread at the restore, taken by the frame clock thread.
std::atomic<int> g_rr_owed{0};
std::atomic<int> g_rr_owed_given{0}, g_rr_owed_at_start{0}, g_rr_counter_at_start{-1};
constexpr int kVblanksPerFrame = 2;           // N of the game's frame pacing (func_006507DC step 3 waits until its counter >= N; 30 fps = 2)
std::atomic<int> g_ls_second{0};              // lockstep: the vblank after the flip is due on the next pass
// ENHANCEMENT (input lag, JSP_PAD_PEEK default on): the last live read of each port came out of the pad filters changed (the port menu open, a script, the
// autopilot): the netcode then takes that filtered record instead of a fresh device read, which would bypass the filters.
bool g_filtered[jsi::kSlots] = {};
extern "C" int ps3_pad_peek_record(uint32_t port_no, uint16_t* button, int* connected);   // patch 7922
extern "C" int32_t jsp_net_vblank_counter(void);      // jsp_net_game.cpp: *(*(TOC - 0x4A80)), the counter the game's frame pacing waits on; -1 = unknown
bool pad_peek_on() { static const int v = [] { const char* e = std::getenv("JSP_PAD_PEEK"); return e && e[0] == '0' ? 0 : 1; }(); return v != 0; }

uint32_t env_u32(const char* n, uint32_t d) { const char* e = std::getenv(n); return e && *e ? (uint32_t)std::strtoul(e, nullptr, 0) : d; }

// FIX (dead / split input on rollback slots 1-3, testers on 0.7.0-test1): cellPadGetInfo2 reports port 0 always and any other port only while a LOCAL
// device is on it, and the game reads a port's data only while it is reported connected. The session serves rollback slot s on port s, so with one device
// per PC the remote player on slot 1 pressed nothing (both PCs alike: the hash agreed), and with a second local device on one PC only (a Parsec virtual pad,
// padjoin "next player 2") that PC simulated slot 1's input while the other did not. While a session's table is enabled every PC reports EXACTLY the
// session's slots (the table mask) as connected standard pads and every other port as disconnected, whatever is plugged in locally (no hot-plug change bits).
// The hook gets the toolkit's own CellPadInfo2 (libs/input/cellPad.h, CELL_PAD_MAX_PORT_NUM = 7 ports), the host struct before the copy to the guest.
static_assert(jsi::kSlots <= CELL_PAD_MAX_PORT_NUM, "session slots are pad ports");
std::atomic<uint32_t> g_info_mask{0};
void info_hook(CellPadInfo2* in)
{
    const uint32_t m = g_info_mask.load();
    if (!m || !in) return;
    const uint32_t setting = in->port_setting[0];   // port 0 is always reported (the game sets one setting on every port: SetPortSetting 0x2 x 7)
    uint32_t n = 0;
    for (uint32_t i = 0; i < CELL_PAD_MAX_PORT_NUM; i++) {
        if (i < (uint32_t)jsi::kSlots && (m & (1u << i))) {
            in->port_status[i] = CELL_PAD_STATUS_CONNECTED;
            in->port_setting[i] = setting;
            in->device_capability[i] = CELL_PAD_CAPABILITY_PS3_CONFORMITY | CELL_PAD_CAPABILITY_PRESS_MODE | CELL_PAD_CAPABILITY_SENSOR_MODE |
                                       CELL_PAD_CAPABILITY_HP_ANALOG_STICK | CELL_PAD_CAPABILITY_ACTUATOR;   // 0x1F, as port 0
            in->device_type[i] = CELL_PAD_DEV_TYPE_STANDARD;
            n++;
        } else {
            in->port_status[i] = CELL_PAD_STATUS_DISCONNECTED; in->port_setting[i] = 0u; in->device_capability[i] = 0u; in->device_type[i] = 0u;
        }
    }
    in->now_connect = n;
}

// patch 7915 hook. `button`/`len` = the record the host layer built; return 1 to replace it.
int canon_hook(uint32_t port, uint16_t* button, int32_t* len, int setting_len, int host_connected)
{
    if (port >= (uint32_t)jsi::kSlots) return 0;
    JspTickState ts; jsp_fight_hash_get_state(&ts);
    const uint32_t tick = ts.done + 1;                       // the scene pass this read feeds
    if (tick < g_canon_from) return 0;
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_masked && !g_ext_table) { g_table.set_mask(g_slots); g_masked = true; }
    const bool replay = g_in_replay.load() != 0;
    const jsi::Pad hw = jsi::from_record(button, host_connected != 0);
    if (!replay) { g_hw[port] = hw; g_hw_have[port] = true; }   // the local sample is a LIVE read only: a replay read must not become the next live input
    static uint32_t s_peek_tick[jsi::kSlots] = {};   // one comparison per port and tick (the first live read), not per read
    if (!replay && g_ext_table && pad_peek_on() && s_peek_tick[port] != tick) {   // ENHANCEMENT (JSP_PAD_PEEK): did a pad filter change this read? (the peek's poll is a try-lock: no deadlock)
        s_peek_tick[port] = tick;
        uint16_t raw[8]; int conn = 0;
        g_filtered[port] = ps3_pad_peek_record(port, raw, &conn) ? jsi::from_record(raw, conn != 0) != hw : true;
    }
    // FIX (dead / split input): in a rollback session (g_ext_table) every slot's value comes from the session (Advance inputs); a local device on another
    // port (a second pad, a Parsec virtual pad) must never fill a session slot, so the device is latched only without one (the local bench / canon tests).
    if (!g_ext_table) tbl().capture_local(tick, (int)port, hw);   // mask first, then: first read of a NEW tick latches the device; a replayed tick finds its value
    jsi::Pad p;
    if (!tbl().serve(tick, (int)port, &p)) { *len = 0; return 1; }   // second read of the tick, or a slot outside the mask: no packet
    jsi::to_record(p, setting_len, button, len);
    g_c.packets.fetch_add(1);
    if (replay) { g_c.replay_reads.fetch_add(1); if (hw != p) g_c.src_differs.fetch_add(1); }   // the source read at replay time differs from what the table gave
    return 1;
}

// JSP_STEP_CANON_POISON: while a replay runs, the SOURCE of port 0 is not what it was in the first pass (chained behind the earlier filters, patch 6501).
void poison_filter(uint32_t port, uint16_t* b, uint16_t* lx, uint16_t* ly)
{
    if (g_prev_filter) g_prev_filter(port, b, lx, ly);
    if (port == 0 && g_in_replay.load()) { *b = (uint16_t)(*b ^ 0x4000 ^ 0x0020); *lx = 0; g_c.poisoned.fetch_add(1); }   // CROSS and RIGHT flipped, stick hard left
}

}  // namespace

// ENHANCEMENT (rollback latency, opt-in: JSP_STEP_SKIP_DRAWS=1, with JSP_STEP_TURBO): the frames of a rollback re-run are never shown, so the RSX command handler skips
// their draws, clears and per-draw state flushes (toolkit patch, g_rsx_skip_draws); register state, labels, semaphores and flips still run. Raised with the replay and
// dropped when its last tick ends (or the run aborts), so the live tick after it draws normally; its first draw applies the state the skipped frames left dirty.
extern "C" volatile int g_rsx_skip_draws;
static int skip_draws_on() { static const int v = [] { const char* e = std::getenv("JSP_STEP_SKIP_DRAWS"); return e && e[0] == '1' ? 1 : 0; }(); return v; }

// jsp_snapshot.cpp: a rollback was applied (on = 1, `done` = the tick restored to) / the replay's last tick ended (0, `done` = that tick).
extern "C" void jsp_stepper_replay(int on, uint32_t done)
{
    if (skip_draws_on() && g_turbo_on_switch) g_rsx_skip_draws = on ? 1 : 0;
    if (on) {
        std::lock_guard<std::mutex> lk(g_mu);
        tbl().rewind(done); g_gate.rollback(done); g_gate.set_replay(true);
        g_replays++; g_rb_tick = done; g_c.reset();
        g_rr_ticks.store(0); g_rr_drain_max_us.store(0); g_rr_ls_b.store(g_ls_second.load());   // DIAGNOSTIC (re-run stalls)
        {   // FIX (re-run vblanks, see owed_vblank_pass): the memory is already restored here, so the counter is the one the save point held
            const int32_t c = jsp_net_vblank_counter();
            const int owed = (c >= 0 && c < kVblanksPerFrame) ? kVblanksPerFrame - c : 0;
            g_rr_counter_at_start.store(c); g_rr_owed_at_start.store(owed); g_rr_owed_given.store(0);
            g_rr_owed.store(g_lockstep.load() ? owed : 0);   // in a match only (the bench and the offline tests keep the old clock)
        }
        g_turbo_t0 = GetTickCount64();
        g_last_flip.store(0);                       // the liveness clock of a replay starts with the replay
        g_in_replay.store(1);
        if (g_turbo_on_switch) g_turbo.store(1);
    } else {
        const int was = g_turbo.exchange(0);
        g_in_replay.store(0);
        std::lock_guard<std::mutex> lk(g_mu);
        g_gate.end_replay(g_rb_tick);
        if (was || g_canon || g_poison) std::fprintf(stderr, "[step] replay %llu (back to tick %u) ended at tick %u: turbo=%d %llu ms, flips=%llu vblank_acks=%llu idle_vblanks=%llu ack_timeouts=%llu canon packets=%llu (replay reads %llu, source differed %llu, poisoned %llu, starved %u) re-run ticks=%u drain_max=%.1f ms lockstep_b_at_start=%d owed_vblanks=%d/%d (restored counter %d)\n",
                                         (unsigned long long)g_replays, g_rb_tick, done, was, (unsigned long long)(GetTickCount64() - g_turbo_t0), (unsigned long long)g_c.flips.load(),
                                         (unsigned long long)g_c.vblank_waits.load(), (unsigned long long)g_c.idle.load(), (unsigned long long)g_c.ack_timeouts.load(), (unsigned long long)g_c.packets.load(), (unsigned long long)g_c.replay_reads.load(),
                                         (unsigned long long)g_c.src_differs.load(), (unsigned long long)g_c.poisoned.load(), tbl().starved(),
                                         g_rr_ticks.load(), g_rr_drain_max_us.load() / 1000.0, g_rr_ls_b.load(), g_rr_owed_given.load(), g_rr_owed_at_start.load(), g_rr_counter_at_start.load());
    }
}

// The run ended (capture_failed / finish_run, possibly mid-replay): nothing may keep the replay clock or the replay flag on. Atomics and one log line, no lock
// (capture_failed logs right after this anyway).
extern "C" void jsp_stepper_abort(void)
{
    const int was = g_turbo.exchange(0);
    const int rep = g_in_replay.exchange(0);
    g_rsx_skip_draws = 0;   // ENHANCEMENT (skip draws): never left on after a run ends
    if (was || rep) std::fprintf(stderr, "[step] replay %llu ABORTED (run ended mid-replay, back to tick %u): turbo=%d flips=%llu ack_timeouts=%llu\n", (unsigned long long)g_replays, g_rb_tick, was,
                                 (unsigned long long)g_c.flips.load(), (unsigned long long)g_c.ack_timeouts.load());
}

extern "C" volatile int g_jsp_main_stage;   // DIAGNOSTIC (defined below): where the main thread is in the boundary
namespace {
// One vblank the guest OBSERVES: the pump coalesces back-to-back ticks into one pending bit, so wait until it delivered this one. Returns false when both
// clocks are off (the run ended meanwhile). Outside a match (the bench, the offline tests) a guest that did not take it within kAckMs of waiting turns the
// replay clock OFF (counted, logged once) and the normal frame clock takes over: a loud fail-safe, not a pacing mechanism (ack_timeouts must stay 0).
// FIX (rollback determinism: a 2 s ack timeout turned the lockstep clock off for the rest of the match on ONE PC): in a match (lockstep clock on)
// the wait NEVER switches a clock off. Off, that PC ran on the 60 Hz grid with timing-dependent vblanks per flip (a desync) and no time-sync stretch / owed
// vblanks. Now: (1) the time inside the FIFO drains is not waiting time (a cold pipeline compile holds one drain ~2 s while the guest would take the vblank
// right after), (2) delivery is re-checked after every drain, (3) a long wait logs every kAckLogMs and goes on waiting, (4) after kAckFailMs of real waiting
// (drains excluded) jsp_stepper_lockstep_failed() turns 1 for jsp_net_game.cpp to end the match with a reason; the clock still stays on until it does.
// `resumable` (the lockstep clock): give the pass back to main.cpp after kAckSliceMs (present, drain, window pump) with the vblank still open; the next pass
// resumes the SAME vblank (g_ack_open), never a second tick. The replay clock blocks, as before (main.cpp does not pump during a replay anyway).
constexpr ULONGLONG kAckMs = 2000;          // outside a match: the old fail-safe
constexpr ULONGLONG kAckLogMs = 2000;       // in a match: one line per 2 s of waiting
constexpr ULONGLONG kAckFailMs = 60000;     // in a match: 60 s of waiting outside the drains = the guest is gone; the match must end
constexpr ULONGLONG kAckSliceMs = 20;       // a resumable wait gives the pass back after this (wall time; when, never whether)
bool g_ack_open = false;                    // frame clock thread only: a vblank was ticked and the guest has not taken it yet
uint32_t g_ack_before = 0;
long long g_ack_wait_qpc = 0, g_ack_drain_qpc = 0;   // this vblank's waiting time outside / inside the drains (QPC units)
uint32_t g_ack_logs = 0;
std::atomic<int> g_ls_failed{0};            // the lockstep clock gave up on the guest (kAckFailMs): read by jsp_stepper_lockstep_failed
}  // namespace
static jsr::State g_lf;                     // frame clock thread only (and jsp_stepper_lockstep): the lost-flip rule and its failure timers (jsp_stall_rule.h)
namespace {
bool vblank_ack(int rsx_ok, bool resumable = false)
{
    LARGE_INTEGER qf; QueryPerformanceFrequency(&qf);
    const long long qms = qf.QuadPart / 1000;
    if (!g_ack_open) { g_ack_before = cellGcm_vblank_delivered(); cellGcmTickVBlank(); g_ack_open = true; g_ack_wait_qpc = g_ack_drain_qpc = 0; g_ack_logs = 0; }
    long long slice = 0;
    for (;;) {
        if (cellGcm_vblank_delivered() != g_ack_before) {
            g_ack_open = false;
            g_c.vblank_waits.fetch_add(1);
            if (g_ack_logs) std::fprintf(stderr, "[step] the guest took the vblank after %.1f s of waiting (+%.1f s in drains)\n", (double)g_ack_wait_qpc / qf.QuadPart, (double)g_ack_drain_qpc / qf.QuadPart);
            return true;
        }
        const bool match = g_lockstep.load(std::memory_order_relaxed) != 0;
        if (!match && !g_turbo.load(std::memory_order_relaxed)) { g_ack_open = false; return false; }
        if (!match && g_ack_wait_qpc > (long long)kAckMs * qms) {
            if (g_c.ack_timeouts.fetch_add(1) == 0) std::fprintf(stderr, "[step] vblank not delivered within %llu ms: the replay clock is switched off for this replay\n", (unsigned long long)kAckMs);
            g_turbo.store(0); g_ack_open = false;
            return false;
        }
        if (match && g_ack_wait_qpc >= (long long)(g_ack_logs + 1) * (long long)kAckLogMs * qms) {
            if (g_ack_logs++ == 0) g_c.ack_timeouts.fetch_add(1);   // counted once per vblank (the replay summary shows it); the clock stays on
            std::fprintf(stderr, "[step] vblank not taken after %.1f s of waiting (+%.1f s in drains), main=%d vblank_counter=%d: still waiting, the lockstep clock stays on\n",
                         (double)g_ack_wait_qpc / qf.QuadPart, (double)g_ack_drain_qpc / qf.QuadPart, (int)g_jsp_main_stage, (int)jsp_net_vblank_counter());
            if (g_ack_wait_qpc >= (long long)kAckFailMs * qms) {
                jsr::note_fail(g_lf, GetTickCount64());   // FIX (review of PR 248): the same release timer as a no-flip failure (jsp_rollback_clock_turbo)
                if (!g_ls_failed.exchange(1))
                    std::fprintf(stderr, "[step] FAILED: the guest has not taken a vblank for %llu s: the match must end (jsp_stepper_lockstep_failed)\n", (unsigned long long)(kAckFailMs / 1000));
            }
        }
        if (resumable && slice >= (long long)kAckSliceMs * qms) return false;   // g_ack_open stays set: the next pass resumes this vblank
        // The guest may be busy-waiting on an RSX label that only the drain writes (a tick computing > 20 ms without a flip): drain while waiting, or the ack
        // and the label wait deadlock. Same thread as the drain call in the pass, serialised by the drain's own lock like the normal clock's calls.
        LARGE_INTEGER a, b, c;
        QueryPerformanceCounter(&a);
        if (rsx_ok) cellGcm_rsx_process_fifo();
        QueryPerformanceCounter(&b);
        g_ack_drain_qpc += b.QuadPart - a.QuadPart; slice += b.QuadPart - a.QuadPart;
        if (cellGcm_vblank_delivered() != g_ack_before) continue;   // re-checked after every drain, before any time counts
        cellGcm_fifo_kick_wait(1);
        QueryPerformanceCounter(&c);
        g_ack_wait_qpc += c.QuadPart - b.QuadPart; slice += c.QuadPart - b.QuadPart;
    }
}
}  // namespace
// jsp_net_game.cpp (at the boundary): 1 = the lockstep clock waited kAckFailMs (drains excluded) for the guest to take a vblank; the match must end.
extern "C" int jsp_stepper_lockstep_failed(void) { return g_ls_failed.load(); }

// FIX (rollback determinism): the game counts vblanks (main.cpp LATE_FLIP) and times things by them; on the 60 Hz grid a tick that took longer (a rollback
// before it, a slow PC) gets 3 or 4 vblanks instead of 2. Two copies started the fight's idle camera cut 3 ticks apart (camera watch: func_0014F608 ->
// func_00523D78, every input equal). In a match every flip gets exactly the replay clock's 2 vblanks (one before the flip, one after), each observed by the
// guest, and the flip is held to a 30 Hz schedule so the match still runs at 30 frames a second. Presenting, the drain and the window pump stay with
// main.cpp (return 2 = handled, go on to the outer cadence). A guest with no flip for 500 ms gets one vblank (liveness, counted).
// FIX (online clock 29.1 Hz instead of 30, smoothness audit cause 3): the hold was "33.3 ms after the previous flip", waited in whole milliseconds with no
// spin, so every flip came a little late and the lateness added up (29.0-29.1 renders/s in every two-copy run, also at 0 ms link with 0 rollbacks). The
// flips now follow a fixed schedule: deadline(n+1) = deadline(n) + 33.333 ms (+ the time-sync stretch); the hold sleeps on the FIFO kick wait until about
// 1.5 ms before the deadline and spins the rest on QueryPerformanceCounter, like the offline LATE_FLIP hold (main.cpp). A flip more than half a period late
// restarts the schedule from its own time (no burst of short frames after a long stall); a smaller lateness is made up by the next frame.
// Determinism is untouched: only the moment of a flip moves, every flip still gets exactly its 2 vblanks.
// ENHANCEMENT (time sync, smoothness audit cause 4): jsp_stepper_lockstep_stretch_us adds GekkoNet's frames-ahead slowdown to the next period (the boundary's
// old Sleep was swallowed by this hold, so the peer that ran ahead never slowed down).
namespace {
LARGE_INTEGER g_ls_qf{};
long long g_ls_next = 0;            // QPC deadline of the next flip (0 = no schedule yet: the first flip goes at once)
long long g_ls_last = 0;            // QPC of the last flip (the liveness timer)
uint64_t g_ls_flips = 0, g_ls_idle = 0, g_ls_restarts = 0, g_ls_stretch_total_us = 0;
std::atomic<uint32_t> g_ls_stretch_us{0};   // the main thread adds, the frame clock takes it at the next flip
constexpr uint32_t kStretchMaxUs = 2000;    // at most 2 ms (6%) longer per tick
}
static ULONGLONG g_ls_last_ms = 0;                                 // frame clock thread only: GetTickCount64 of the last lockstep flip
// FIX (review of PR 248, the freeze timeout): on = a match starts: the no-flip deadline runs from now (jsr::start), not from a first flip that may never come.
// off (end_with, after the boundary read the flag) also clears the failure flag, so a later match's pump watchdog (jsp_net_game.cpp) never sees this one's.
extern "C" void jsp_stepper_lockstep(int on)
{
    if (on) { QueryPerformanceFrequency(&g_ls_qf); g_ls_next = 0; g_ls_last = 0; g_ls_second = 0; g_ls_flips = g_ls_idle = g_ls_restarts = g_ls_stretch_total_us = 0; g_ls_stretch_us.store(0); g_ls_failed.store(0); g_ls_last_ms = 0; jsr::start(g_lf, GetTickCount64()); }
    else {
        if (g_lockstep.load()) std::fprintf(stderr, "[step] lockstep clock off: %llu flips, %llu idle vblanks, %llu schedule restarts (a flip over half a period late), time-sync stretch %.1f ms in all\n",
                                            (unsigned long long)g_ls_flips, (unsigned long long)g_ls_idle, (unsigned long long)g_ls_restarts, g_ls_stretch_total_us / 1000.0);
        g_ls_failed.store(0);
    }
    g_lockstep.store(on ? 1 : 0);
}
// The boundary (main thread, after a session update): stretch the next lockstep period by `us` (capped at kStretchMaxUs per period). Returns 0 when the
// lockstep clock is off (the caller sleeps instead, as before).
extern "C" int jsp_stepper_lockstep_stretch_us(uint32_t us)
{
    if (!g_lockstep.load(std::memory_order_relaxed)) return 0;
    if (us) g_ls_stretch_us.fetch_add(us);
    return 1;
}
static bool guest_in_step3_without_flip();                       // below (frame clock thread only)
static void log_rerun_stall(const char* what, ULONGLONG gap_ms);   // below
static bool lost_flip_pass(ULONGLONG last_flip_ms);                // below (frame clock thread only): true = a lost flip was written off
static int g_ls_ack_site = 0;       // frame clock thread only: the open vblank (g_ack_open) is the one before a flip (1: the flip follows it) or another (0)
static uint32_t g_ls_wait_logs = 0; // frame clock thread only: "no flip, no step-3 wait" lines of this stall
// The flip after its first vblank: complete it and advance the 30 Hz schedule.
static void lockstep_take_flip()
{
    LARGE_INTEGER now;
    const long long f = g_ls_qf.QuadPart;
    const long long period = (f + 15) / 30;   // 33.333 ms
    cellGcmTickFlip();
    QueryPerformanceCounter(&now);
    const uint32_t st = std::min(g_ls_stretch_us.exchange(0), kStretchMaxUs);
    g_ls_stretch_total_us += st;
    const long long step = period + (long long)st * f / 1000000;
    if (!g_ls_next || now.QuadPart - g_ls_next > period / 2) { if (g_ls_next) g_ls_restarts++; g_ls_next = now.QuadPart + step; }   // over half a period late: restart
    else g_ls_next += step;                                                                                                         // on (or near) the schedule
    g_ls_last = now.QuadPart; g_ls_last_ms = GetTickCount64();
    g_ls_second = 1; g_ls_flips++; g_ls_wait_logs = 0;
    jsr::flip_taken(g_lf);
}
// FIX (lockstep clock never off mid-match): a vblank of the lockstep clock the guest has not taken yet comes before anything else (no second tick, no grid), and the
// step it belongs to goes on from where it stopped. Also called from a replay pass (a replay can start while one is open, as when the wait blocked).
static int lockstep_resume(int rsx_ok)
{
    if (!vblank_ack(rsx_ok, true)) return 2;          // still open (main.cpp presents / drains / pumps, we come back), or both clocks off
    if (g_ls_ack_site == 1) lockstep_take_flip();
    g_ls_ack_site = 0;
    return 2;
}
static int lockstep_pass(int rsx_ok)
{
    if (g_ack_open) return lockstep_resume(rsx_ok);
    if (g_ls_second) { if (rsx_ok) cellGcm_rsx_process_fifo(); g_ls_second = 0; g_ls_ack_site = 0; (void)vblank_ack(rsx_ok, true); return 2; }   // the vblank after the flip (after its present)
    if (rsx_ok) cellGcm_rsx_process_fifo();
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    const long long f = g_ls_qf.QuadPart;
    if (cellGcm_flip_ready()) {
        if (g_ls_next && now.QuadPart < g_ls_next) {   // hold to the schedule: sleep in whole ms to ~1.5 ms before the deadline, then spin
            const long long left_us = (g_ls_next - now.QuadPart) * 1000000 / f;
            if (left_us > 2500) { cellGcm_fifo_kick_wait((unsigned)((left_us - 1500) / 1000)); return 2; }   // a kick wakes it early: main.cpp drains, we come back
            while (now.QuadPart < g_ls_next) { YieldProcessor(); QueryPerformanceCounter(&now); }
        }
        g_ls_ack_site = 1;
        if (!vblank_ack(rsx_ok, true)) return 2;      // open: the next pass resumes it and then takes the flip
        g_ls_ack_site = 0;
        lockstep_take_flip();
        return 2;   // main.cpp presents the frame, drains and pumps the window; the next pass gives the second vblank
    }
    cellGcm_fifo_kick_wait(1);
    QueryPerformanceCounter(&now);
    // FIX (rollback determinism: the idle vblank was wall-clock timed): a vblank with no flip goes ONLY to a guest that is provably in its frame
    // pacing step 3 (main thread in an HLE wait at lr 0x006508A0, the vblank counter below N) with no flip outstanding (queued = reached = completed, no
    // direct flip), the test owed_vblank_pass uses; one per pass, each observed, until the counter reaches N and step 3 lets go. Before, any 500 ms without a
    // flip gave one, and a FIFO drain over 500 ms on one PC (a cold pipeline compile) gave that PC an extra vblank in the middle of a busy tick.
    // Why the same on both PCs: the guest is blocked in step 3 and every flip it requested is complete, and the lockstep clock delivers vblanks only with a
    // flip (2 each) or here; the flips it requested, the counter and where the main thread waits are guest state, equal on both PCs at that tick. Nothing
    // else can move the guest on (on a PS3 the free-running vblank would), so each PC reaches the same wait with the same counter and gets the same N - c
    // vblanks at the same guest position, whenever its host gets there. Time only decides WHEN (the 20 ms below only saves the census on busy passes).
    // Any other wait without a flip gets nothing: it keeps waiting, with a stall line after 500 ms and every 5 s (the threads' lr names the wait).
    // rbpub1 (2 idle vblanks on each copy, 0 in rb5) fits one N = 2 step-3 wait without a flip per match; its logs do not say where they fired.
    if (!g_ls_last) { g_ls_last = now.QuadPart; return 2; }
    const long long gap = now.QuadPart - g_ls_last;
    if (gap > f / 50 && guest_in_step3_without_flip()) {
        g_ls_idle++; g_ls_last = now.QuadPart; g_ls_wait_logs = 0; g_ls_ack_site = 0;
        (void)vblank_ack(rsx_ok, true);
        return 2;
    }
    if (gap > f / 2 + (long long)g_ls_wait_logs * 5 * f && g_ls_wait_logs < 20) {
        g_ls_wait_logs++;
        log_rerun_stall(g_in_replay.load() ? "lockstep, in a replay: no flip and no step-3 wait, waiting"
                                           : "lockstep, LIVE (no replay running; the replay fields are the last replay's): no flip and no step-3 wait, waiting",
                        (ULONGLONG)(gap * 1000 / f));
    }
    // FIX (rollback freeze, jsp_stall_rule.h): a flip queued in the ring that the walker never reaches, with the guest polling for it in step 1
    if (gap > f / 2) (void)lost_flip_pass(g_ls_last_ms);   // the next pass sees the flip ready and takes it with its 2 vblanks, as always
    return 2;
}

// DIAGNOSTIC (re-run stalls): where the main thread is (jsp_net_game.cpp sets it around the boundary's waits) and the guest's vblank counter.
extern "C" { volatile int g_jsp_main_stage = 0; }
extern "C" void cellGcm_flip_census(uint32_t* out);   // patch 7913 (read-only, no lock)
extern "C" void ps3_hle_thread_census(uint32_t* kind, uint32_t* id, uint32_t* obj, uint32_t* lr, uint8_t* alive, uint8_t* unreliable, uint32_t* overflows, uint32_t* unbalanced);
static const char* main_stage_name(int s)
{
    switch (s) { case 0: return "guest-code"; case 1: return "boundary:session"; case 2: return "boundary:hold-for-a-tick"; case 3: return "boundary:audio-lock";
                 case 4: return "boundary:sound-server"; case 5: return "boundary:other"; }
    return "?";
}
// Frame clock thread only (the census arrays are its own). One line: the stall, the flip state and every live guest thread as t:kind:lr (kind 0 = guest code,
// else the HLE call it is in; lr = its call site). The main thread's frame pacing func_006507DC waits at lr 0x00650820 (step 1: the previous flip DONE) and
// at 0x006508A0 / the usleep after it (step 3: the vblank counter >= N).
static void log_rerun_stall(const char* what, ULONGLONG gap_ms)
{
    if (g_rr_stall_lines >= 200) return;
    g_rr_stall_lines++;
    uint32_t fc[9] = {}; cellGcm_flip_census(fc);
    static uint32_t kind[65], id[65], obj[65], lr[65]; static uint8_t alive[65], unrel[65]; uint32_t ov = 0, ub = 0;
    ps3_hle_thread_census(kind, id, obj, lr, alive, unrel, &ov, &ub);
    char thr[900]; int n = 0; thr[0] = 0;
    for (int t = 1; t < 65 && n < (int)sizeof thr - 24; t++)
        if (alive[t]) n += std::snprintf(thr + n, sizeof thr - n, " %d:%x:%08X", t, kind[t], lr[t]);
    std::fprintf(stderr, "[step] re-run stall (%s): replay %llu (back to tick %u) re-run tick %u, no flip for %llu ms: flips=%llu vblank_acks=%llu idle_vblanks=%llu "
                 "main=%s vblank_counter=%d flip(status=%u req_seq=%u tick_seq=%u queued=%u reached=%u completed=%u direct=%u pending=0x%X) drain_max=%.1f ms "
                 "lockstep_b_at_start=%d owed_left=%d threads(t:kind:lr)%s\n", what, (unsigned long long)g_replays, g_rb_tick, g_rr_ticks.load(), (unsigned long long)gap_ms,
                 (unsigned long long)g_c.flips.load(), (unsigned long long)g_c.vblank_waits.load(), (unsigned long long)g_c.idle.load(), main_stage_name(g_jsp_main_stage),
                 (int)jsp_net_vblank_counter(), fc[0], fc[1], fc[2], fc[3], fc[4], fc[5], fc[6], fc[7], g_rr_drain_max_us.load() / 1000.0, g_rr_ls_b.load(), g_rr_owed.load(), thr);
}
// The main thread, at the boundary that grants a re-run tick (not a regrant): counts it, and with JSP_NET_TICK_LOG=1 logs one line per re-run tick with what
// the replay clock gave so far (the flips and vblanks of the ticks before it).
extern "C" void jsp_stepper_rerun_tick(uint32_t tick, int log)
{
    const uint32_t k = g_rr_ticks.fetch_add(1) + 1;
    if (k >= 2) g_rr_owed.store(0);   // FIX (re-run vblanks): owed to re-run tick 1 only; tick 1 got past its step 3 without them
    if (log) std::fprintf(stderr, "[step] re-run tick=%u (%u of replay %llu): flips=%llu vblank_acks=%llu idle_vblanks=%llu, %llu ms into the replay, drain_max=%.1f ms\n", tick, k,
                          (unsigned long long)g_replays, (unsigned long long)g_c.flips.load(), (unsigned long long)g_c.vblank_waits.load(), (unsigned long long)g_c.idle.load(),
                          (unsigned long long)(GetTickCount64() - g_turbo_t0), g_rr_drain_max_us.load() / 1000.0);
}

// FIX (rollback freezes, smoothness audit cause 1; found by the stall lines of the two-copy run native-rb2): re-run tick 1 sat in the game's frame pacing
// step 3 (main thread lr 0x006508A0: func_006507DC waits until the vblank counter *(*(TOC - 0x4A80)) >= N = 2, then zeroes it) with the restored counter
// at 0 and no flip outstanding (queued = reached = completed), so only the 500 ms idle vblank moved it: ~1 s per rollback (2 idle vblanks).
// Why: step 3 of frame j consumes the 2 vblanks of flip j-1. In the first run of tick T, flip T-1 completed AFTER tick T's save point (the boundary), so
// its vblanks reached the guest after the save and were consumed by tick T's step 3. A rollback restores the save (counter as it was then), but flip T-1 is
// long done on the host, so the re-run of tick T has no flip to carry them. From re-run tick 2 on, step 3 of tick j again consumes the re-run's flip j-1,
// which this clock gives its 2 vblanks (and the last re-run tick's flip goes to the lockstep clock with its 2): flips = ticks - 1, as the logs show.
// The rule (deterministic: it depends only on the restored memory and the guest's position, not on time): at the restore the replay owes re-run tick 1
// N - c vblanks, c = the restored counter (the vblanks of flip T-1 the guest already had at the save point; the first run gave it exactly N - c more
// before its step 3 let go). They are delivered one at a time, each observed, only while the main thread is provably in step 3 (lr 0x006508A0, in an HLE
// wait) with no flip outstanding (a flip that is coming brings its own 2, and then nothing is owed: the first turbo flip clears the debt). So re-run tick 1
// sees exactly the vblanks its first run saw, at the same place (inside its step 3 wait), and the per-tick count matches the first run.
// Frame clock thread only (the census arrays are its own). Returns true when it delivered one.
// The guest is provably in step 3 waiting for a vblank with no flip outstanding (owed_vblank_pass, and the lockstep / replay idle vblank in a match).
static bool guest_in_step3_without_flip()
{
    uint32_t fc[9] = {}; cellGcm_flip_census(fc);
    if (fc[3] != fc[4] || fc[4] != fc[5] || fc[6] != 0) return false;   // a flip is queued / reached / direct: it brings its own vblanks
    static uint32_t kind[65], id[65], obj[65], lr[65]; static uint8_t alive[65], unrel[65]; uint32_t ov = 0, ub = 0;
    ps3_hle_thread_census(kind, id, obj, lr, alive, unrel, &ov, &ub);
    bool in_step3 = false;
    for (int t = 1; t < 65 && !in_step3; t++) in_step3 = alive[t] && !unrel[t] && kind[t] != 0 && lr[t] == 0x006508A0u;
    if (!in_step3) return false;
    const int32_t c = jsp_net_vblank_counter();
    return c >= 0 && c < kVblanksPerFrame;              // else not waiting for a vblank (it leaves step 3 on its own)
}
// FIX (rollback freeze: a flip the FIFO walker never reaches; the rule and the evidence are in jsp_stall_rule.h). Frame clock thread only, on a pass
// with no flip ready. Samples the flip census and whether a guest thread is in the frame pacing's step 1 (func_006507DC polling cellGcmGetFlipStatus:
// lr 0x00650820 after the poll, 0x00650854 after its every-10th call), and asks the toolkit (patch 7927) to write the flip off when the rule says so.
// The toolkit refuses unless the walker is idle at put with exactly that one flip outstanding; either way one line says where get, put and the marker
// were (the cause of the lost flip is not proven: that line is what proves it next time).
static bool lost_flip_pass(ULONGLONG last_flip_ms)
{
    if (!g_lockstep.load(std::memory_order_relaxed)) return false;
    uint32_t fc[9] = {}; cellGcm_flip_census(fc);
    jsr::Sample s;
    s.now_ms = GetTickCount64(); s.last_flip_ms = last_flip_ms;
    s.status = fc[0]; s.queued = fc[3]; s.reached = fc[4]; s.completed = fc[5]; s.direct = fc[6];
    if (s.status == 1 && s.queued == s.reached + 1u) {   // the census of the threads only when the cheap part already matches
        static uint32_t kind[65], id[65], obj[65], lr[65]; static uint8_t alive[65], unrel[65]; uint32_t ov = 0, ub = 0;
        ps3_hle_thread_census(kind, id, obj, lr, alive, unrel, &ov, &ub);
        for (int t = 1; t < 65 && !s.main_in_step1; t++) s.main_in_step1 = alive[t] && !unrel[t] && kind[t] != 0 && (lr[t] == 0x00650820u || lr[t] == 0x00650854u);
    }
    const jsr::Act a = jsr::step(g_lf, s);
    if (a == jsr::Act::Fail) {
        if (!g_ls_failed.exchange(1))
            std::fprintf(stderr, "[step] FAILED: no flip for %llu s%s (flip status %u, queued %u reached %u completed %u): the match must end (jsp_stepper_lockstep_failed)\n",
                         (unsigned long long)(jsr::kFailMs / 1000), last_flip_ms ? "" : " since the lockstep clock started", s.status, s.queued, s.reached, s.completed);
        return false;
    }
    if (a != jsr::Act::WriteOff) return false;
    uint32_t w[9] = {};
    const int done = cellGcm_flip_write_off_lost(1, w);
    std::fprintf(stderr, "[step] lost flip %s: %llu ms without a flip, the guest in step 1 waiting for flip %u (reached %u): get=0x%08X put=0x%08X marker at 0x%08X "
                 "word there 0x%08X (%s), status %u, in a replay %d\n",
                 done ? "WRITTEN OFF (completed as if the walker had reached it)" : "NOT written off (the walker is not idle at put: still waiting)",
                 (unsigned long long)(s.now_ms - last_flip_ms), w[4], w[5], w[0], w[1], w[2], w[3],
                 (w[3] & 0xFFFFFF00u) == 0xFEAD0100u ? "the marker" : "overwritten",
                 w[7], g_in_replay.load());
    if (done) jsr::flip_taken(g_lf);
    return done != 0;
}

static bool owed_vblank_pass(int rsx_ok)
{
    if (g_rr_owed.load(std::memory_order_relaxed) <= 0 || !g_lockstep.load(std::memory_order_relaxed)) return false;
    if (!guest_in_step3_without_flip()) return false;
    g_rr_owed.fetch_sub(1); g_rr_owed_given.fetch_add(1);
    (void)vblank_ack(rsx_ok);                           // observed, like every vblank of the replay clock
    g_last_flip.store(GetTickCount64());                // progress: the liveness timer restarts
    g_rr_stall_logged = 0;
    return true;
}

// The frame clock (main.cpp). Returns 1 when this pass was handled by the replay clock, 2 when by the lockstep clock (main.cpp goes on to present / drain / pump).
extern "C" int jsp_rollback_clock_turbo(int rsx_ok)
{
    // FIX (review of PR 248, the freeze timeout's owner): a failed stall (jsr::kFailMs without a flip, or kAckFailMs without a vblank taken) is ended by the
    // pump thread (jsp_net_game.cpp: session aborted, the peer told). If the guest still has not flipped kReleaseMs later, the game thread cannot reach the
    // boundary that would turn this clock off, so this thread (which stays responsive: main.cpp calls it every pass) lets go of the guest itself: the
    // lockstep clock goes off and the normal frame clock takes over (an open vblank closes in vblank_ack). The failure flag stays set for the boundary.
    if (g_lockstep.load(std::memory_order_relaxed) && jsr::release_due(g_lf, GetTickCount64())) {
        std::fprintf(stderr, "[step] the match failed %llu s ago and the guest still has not flipped: the lockstep clock lets go (the normal frame clock takes over)\n",
                     (unsigned long long)(jsr::kReleaseMs / 1000));
        g_lockstep.store(0);
    }
    if (g_ack_open) return lockstep_resume(rsx_ok);          // FIX (never off mid-match): only the lockstep clock leaves one open; it comes first
    if (!g_turbo.load(std::memory_order_relaxed)) return g_lockstep.load(std::memory_order_relaxed) ? lockstep_pass(rsx_ok) : 0;
    if (rsx_ok) {                                            // executes what the guest queued, including its flip command ("reached")
        LARGE_INTEGER a, b, qf; QueryPerformanceCounter(&a);
        cellGcm_rsx_process_fifo();
        QueryPerformanceCounter(&b); QueryPerformanceFrequency(&qf);
        const uint32_t us = (uint32_t)((b.QuadPart - a.QuadPart) * 1000000 / qf.QuadPart);   // DIAGNOSTIC: a cold pipeline compiles inside the drain (PSO_ASYNC=0 in rollback)
        if (us > g_rr_drain_max_us.load(std::memory_order_relaxed)) g_rr_drain_max_us.store(us);
    }
    if (cellGcm_flip_ready()) {
        if (!vblank_ack(rsx_ok)) return 1;
        cellGcmTickFlip();
        (void)cellGcm_take_flip_pending();                   // taken, not presented: the intermediate frames of a replay are never shown
        if (rsx_ok) cellGcm_rsx_process_fifo();
        if (!vblank_ack(rsx_ok)) return 1;                         // N = 2 vblanks per flip , each observed by the guest
        g_c.flips.fetch_add(1); g_last_flip.store(GetTickCount64()); jsr::flip_taken(g_lf);
        g_rr_stall_logged = 0;
        g_rr_owed.store(0);                                  // FIX (re-run vblanks): a flip came first: its 2 vblanks are the ones tick 1 needed
        return 1;
    }
    if (owed_vblank_pass(rsx_ok)) return 1;                  // FIX (re-run vblanks): re-run tick 1 waits in step 3 for the vblanks of a flip done before the restore
    cellGcm_fifo_kick_wait(1);                               // the guest is computing: wait for its next FIFO kick, not for the clock
    // Liveness only: a guest that waits for a vblank with no flip queued (something the grid always answered) gets one after 20 ms of nothing, counted.
    // ASSUMPTION: never needed; the replay summary prints idle_vblanks, and a non-zero count means the virtual clock misses a wait to name.
    // FINDING (smoothness audit cause 1, 2026-11-21, from the lifted code and the two-copy logs; NOT proven, so diagnostic lines below and no new rule): the
    // game paces every frame in func_006507DC: (1) wait until the previous flip is DONE, (2) queue this frame's flip and flush, (3) with N > 1 wait until the
    // vblank handler's counter *(*(TOC - 0x4A80)) >= N, then zero it. In a match step 3 of frame j consumes the 2 vblanks of flip j-1; a re-run's flips are
    // taken here with 2 each, so a guest in step 3 has a flip coming whenever the drain reaches one. The stalled replays of it-repsn-l66 (flips = ticks - 1,
    // idle 1-2) coincide with the drawing thread stalling (MIDFLUSH "2.4 s since the last flush", RSXFPS 5.5: pipelines.bin full, PSO_ASYNC=0), and their
    // rollback boundaries had no flip outstanding (flip=0, DONE). So the 500 ms may be a drain (a compile) or the boundary's own wait (the hold for a tick runs
    // with the replay clock still on), not a missed vblank. Delivering "the vblanks a tick is owed" early is only safe when the guest is provably in step 3
    // (an early vblank in a busy tick lands mid-simulation); the lines below say which case it is: main=, vblank_counter, the flip state, drain_max, the lr.
    const ULONGLONG now = GetTickCount64();
    ULONGLONG lf = g_last_flip.load();
    if (!lf) { g_last_flip.store(now); return 1; }
    const bool in_match = g_lockstep.load(std::memory_order_relaxed) != 0;
    if (in_match && now - lf >= 100 && !g_rr_stall_logged) { g_rr_stall_logged = 1; log_rerun_stall("100 ms", now - lf); }
    // FIX (rollback determinism): in a match (lockstep clock on) a re-run's idle vblank comes after 500 ms, not 20: a slow re-run tick is not a guest waiting
    // for a vblank, and an extra vblank there is counted by the game (replays logged idle_vblanks 1-2 on 19 and 8 of ~460 replays on two copies).
    // FIX (re-run idle vblank was wall-clock timed): the 500 ms was still time, and a drain over 500 ms on one PC gave its re-run an extra vblank. In a match it now
    // follows the lockstep clock's rule (lockstep_pass): only a guest provably in step 3 with no flip outstanding gets one, so a re-run sees what its first
    // run saw; any other wait keeps waiting (the 100 ms stall line above names it).
    if (in_match ? (now - lf > 20u && guest_in_step3_without_flip()) : now - lf > 20u) {
        if (in_match) log_rerun_stall("idle vblank", now - lf);
        g_c.idle.fetch_add(1); g_last_flip.store(now); (void)vblank_ack(rsx_ok);   // observed like the others (a lone unacknowledged tick could be mistaken for the next ack)
        g_rr_stall_logged = 0;
        return 1;
    }
    if (in_match && now - lf >= 500u) (void)lost_flip_pass(lf);   // FIX (rollback freeze, jsp_stall_rule.h): same rule in a re-run; the next pass takes the flip
    return 1;
}

// The rollback session / the audio and file wrappers: the one question before an effect that cannot be taken back.
extern "C" int jsp_effects_request(int cat, uint32_t tick, uint64_t key, uint64_t id)
{
    std::lock_guard<std::mutex> lk(g_mu);
    return (int)g_gate.request((jse::Cat)cat, tick, key, id);
}

extern "C" void jsp_stepper_install(void)
{
    const char* t = std::getenv("JSP_STEP_TURBO");
    const char* c = std::getenv("JSP_STEP_CANON");
    g_turbo_on_switch = t && *t && t[0] != '0';
    g_canon = c && *c && c[0] != '0';
    g_poison = std::getenv("JSP_STEP_CANON_POISON") && std::getenv("JSP_STEP_CANON_POISON")[0] == '1';
    if (!g_turbo_on_switch && !g_canon && !g_poison) return;
    g_slots = env_u32("JSP_STEP_CANON_SLOTS", 1);
    g_canon_from = env_u32("JSP_STEP_CANON_FROM", env_u32("JSP_SNAPSHOT_AT", 0));
    if (g_canon) g_cellpad_canon_hook = canon_hook;
    if (g_poison) { g_prev_filter = g_cellpad_filter; g_cellpad_filter = poison_filter; }
    std::fprintf(stderr, "[step] turbo=%d canon=%d (from tick %u, slots 0x%X, poison=%d)\n", g_turbo_on_switch, g_canon, g_canon_from, g_slots, g_poison);
}

// Rollback session (jsp_net_game.cpp): the session's table and canonical pad hook. Slot s of the table is the pad port s of the game; the table must have
// its mask set by the caller. Local device sample of port `port` (the last record the pad read saw, before it was replaced), canonical 8 bytes; 0 = none yet.
extern "C" void jsp_stepper_net_enable(void* table)
{
    std::lock_guard<std::mutex> lk(g_mu);
    g_ext_table = static_cast<jsi::Table*>(table);
    g_canon = true; g_canon_from = 0;
    g_cellpad_canon_hook = canon_hook;
    g_info_mask = g_ext_table ? g_ext_table->mask() : 0u;   // FIX (dead / split input): the caller set the mask before (jsp_net_game.cpp start_session)
    g_cellpad_info_hook = info_hook;
}
extern "C" void jsp_stepper_net_disable(void)
{
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_cellpad_canon_hook == canon_hook) g_cellpad_canon_hook = nullptr;
    if (g_cellpad_info_hook == info_hook) g_cellpad_info_hook = nullptr;   // FIX (dead / split input): the local devices again
    g_info_mask = 0;
    g_ext_table = nullptr; g_canon = false;
}
// ENHANCEMENT (rollback latency, JSP_PAD_PEEK, default ON since 2026-11-21; JSP_PAD_PEEK=0 = the old way): the local pad given to the netcode is read from the
// host device at the tick boundary (toolkit ps3_pad_peek_record) instead of the last record the game itself read, which is one tick old (canon_hook stores it
// during the previous tick): 33 ms less input lag. DETERMINISM: this value is only the LOCAL input handed to GekkoNet (Glue::local_pad); GekkoNet sends it to
// the peer stamped with its tick and both PCs simulate the table's copy of it (canon_hook serves the table, never the device, live and in re-runs). So the
// peek only changes WHEN the local pad is sampled, not what the two PCs agree on. The peek reads the device without the pad filters (port menu, scripts,
// autopilot): whenever the last live read came out of the filters changed (g_filtered), the filtered record is used instead, as before.
extern "C" int jsp_stepper_local_pad(int port, uint8_t canon[8])
{
    if (port < 0 || port >= jsi::kSlots) return 0;
    bool filtered; { std::lock_guard<std::mutex> lk(g_mu); filtered = g_filtered[port]; }
    if (pad_peek_on() && !filtered) {
        uint16_t rec[8]; int conn = 0;
        if (ps3_pad_peek_record((uint32_t)port, rec, &conn)) { const auto b = jsi::from_record(rec, conn != 0).pack(); for (int i = 0; i < 8; i++) canon[i] = b[i]; return 1; }
    }
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_hw_have[port]) return 0;
    const auto b = g_hw[port].pack();
    for (int i = 0; i < 8; i++) canon[i] = b[i];
    return 1;
}
