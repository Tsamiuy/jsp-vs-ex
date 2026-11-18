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
//   (patch 7915 also adds g_cellpad_info_hook for the session slots' connected status; nothing installs it yet.)
//   Side effects: jsp_effects_request() is the one question the audio / file / save / net wrappers ask (jsp_step_effects.h); no caller is wired yet
//   (the CRI atom start/stop addresses are not located), so it is inert until the audio side wires it.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <windows.h>
#include "jsp_step_effects.h"
#include "jsp_step_input.h"

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
struct JspTickState { uint32_t done; uint32_t anc_done; uint8_t anc_set, anc_skip; };
void jsp_fight_hash_get_state(JspTickState* s);   // jsp_fight_hash.cpp: done = completed scene passes
// patch 7915
extern int (*g_cellpad_canon_hook)(uint32_t port_no, uint16_t* button, int32_t* len, int setting_len, int host_connected);
}

namespace {

std::atomic<int> g_turbo{0};          // the replay clock is on
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

uint32_t env_u32(const char* n, uint32_t d) { const char* e = std::getenv(n); return e && *e ? (uint32_t)std::strtoul(e, nullptr, 0) : d; }

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
    tbl().capture_local(tick, (int)port, hw);              // mask first, then: first read of a NEW tick latches the device; a replayed tick finds its value
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

// jsp_snapshot.cpp: a rollback was applied (on = 1, `done` = the tick restored to) / the replay's last tick ended (0, `done` = that tick).
extern "C" void jsp_stepper_replay(int on, uint32_t done)
{
    if (on) {
        std::lock_guard<std::mutex> lk(g_mu);
        tbl().rewind(done); g_gate.rollback(done); g_gate.set_replay(true);
        g_replays++; g_rb_tick = done; g_c.reset();
        g_turbo_t0 = GetTickCount64();
        g_last_flip.store(0);                       // the liveness clock of a replay starts with the replay
        g_in_replay.store(1);
        if (g_turbo_on_switch) g_turbo.store(1);
    } else {
        const int was = g_turbo.exchange(0);
        g_in_replay.store(0);
        std::lock_guard<std::mutex> lk(g_mu);
        g_gate.end_replay(g_rb_tick);
        if (was || g_canon || g_poison) std::fprintf(stderr, "[step] replay %llu (back to tick %u) ended at tick %u: turbo=%d %llu ms, flips=%llu vblank_acks=%llu idle_vblanks=%llu ack_timeouts=%llu canon packets=%llu (replay reads %llu, source differed %llu, poisoned %llu, starved %u)\n",
                                         (unsigned long long)g_replays, g_rb_tick, done, was, (unsigned long long)(GetTickCount64() - g_turbo_t0), (unsigned long long)g_c.flips.load(),
                                         (unsigned long long)g_c.vblank_waits.load(), (unsigned long long)g_c.idle.load(), (unsigned long long)g_c.ack_timeouts.load(), (unsigned long long)g_c.packets.load(), (unsigned long long)g_c.replay_reads.load(),
                                         (unsigned long long)g_c.src_differs.load(), (unsigned long long)g_c.poisoned.load(), tbl().starved());
    }
}

// The run ended (capture_failed / finish_run, possibly mid-replay): nothing may keep the replay clock or the replay flag on. Atomics and one log line, no lock
// (capture_failed logs right after this anyway).
extern "C" void jsp_stepper_abort(void)
{
    const int was = g_turbo.exchange(0);
    const int rep = g_in_replay.exchange(0);
    if (was || rep) std::fprintf(stderr, "[step] replay %llu ABORTED (run ended mid-replay, back to tick %u): turbo=%d flips=%llu ack_timeouts=%llu\n", (unsigned long long)g_replays, g_rb_tick, was,
                                 (unsigned long long)g_c.flips.load(), (unsigned long long)g_c.ack_timeouts.load());
}

namespace {
// One vblank the guest OBSERVES: the pump coalesces back-to-back ticks into one pending bit, so wait until it delivered this one. Returns false when the replay
// ended meanwhile, or when the guest did not take it within kAckMs: then the replay clock turns itself OFF (counted, logged once), the normal frame clock (which
// also pumps window messages) takes over and the game finishes the replay paced. A fail-safe that is loud, not a pacing mechanism: ack_timeouts must stay 0.
constexpr ULONGLONG kAckMs = 2000;
bool vblank_ack(int rsx_ok)
{
    const uint32_t before = cellGcm_vblank_delivered();
    cellGcmTickVBlank();
    const ULONGLONG t0 = GetTickCount64();
    while (cellGcm_vblank_delivered() == before) {
        if (!g_turbo.load(std::memory_order_relaxed)) return false;
        if (GetTickCount64() - t0 > kAckMs) {
            if (g_c.ack_timeouts.fetch_add(1) == 0) std::fprintf(stderr, "[step] vblank not delivered within %llu ms: the replay clock is switched off for this replay\n", (unsigned long long)kAckMs);
            g_turbo.store(0);
            return false;
        }
        // The guest may be busy-waiting on an RSX label that only the drain writes (a tick computing > 20 ms without a flip): drain while waiting, or the ack
        // and the label wait deadlock for kAckMs. Same thread as the drain call in the pass, serialised by the drain's own lock like the normal clock's calls.
        if (rsx_ok) cellGcm_rsx_process_fifo();
        cellGcm_fifo_kick_wait(1);
    }
    g_c.vblank_waits.fetch_add(1);
    return true;
}
}  // namespace

// The frame clock (main.cpp). Returns 1 when this pass was handled by the replay clock.
extern "C" int jsp_rollback_clock_turbo(int rsx_ok)
{
    if (!g_turbo.load(std::memory_order_relaxed)) return 0;
    if (rsx_ok) cellGcm_rsx_process_fifo();                  // executes what the guest queued, including its flip command ("reached")
    if (cellGcm_flip_ready()) {
        if (!vblank_ack(rsx_ok)) return 1;
        cellGcmTickFlip();
        (void)cellGcm_take_flip_pending();                   // taken, not presented: the intermediate frames of a replay are never shown
        if (rsx_ok) cellGcm_rsx_process_fifo();
        if (!vblank_ack(rsx_ok)) return 1;                         // N = 2 vblanks per flip , each observed by the guest
        g_c.flips.fetch_add(1); g_last_flip.store(GetTickCount64());
        return 1;
    }
    cellGcm_fifo_kick_wait(1);                               // the guest is computing: wait for its next FIFO kick, not for the clock
    // Liveness only: a guest that waits for a vblank with no flip queued (something the grid always answered) gets one after 20 ms of nothing, counted.
    // ASSUMPTION: never needed; the replay summary prints idle_vblanks, and a non-zero count means the virtual clock misses a wait to name.
    const ULONGLONG now = GetTickCount64();
    ULONGLONG lf = g_last_flip.load();
    if (!lf) { g_last_flip.store(now); return 1; }
    if (now - lf > 20) { g_c.idle.fetch_add(1); g_last_flip.store(now); (void)vblank_ack(rsx_ok); }   // observed like the others (a lone unacknowledged tick could be mistaken for the next ack)
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
}
extern "C" void jsp_stepper_net_disable(void)
{
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_cellpad_canon_hook == canon_hook) g_cellpad_canon_hook = nullptr;
    g_ext_table = nullptr; g_canon = false;
}
extern "C" int jsp_stepper_local_pad(int port, uint8_t canon[8])
{
    if (port < 0 || port >= jsi::kSlots) return 0;
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_hw_have[port]) return 0;
    const auto b = g_hw[port].pack();
    for (int i = 0; i < 8; i++) canon[i] = b[i];
    return 1;
}
