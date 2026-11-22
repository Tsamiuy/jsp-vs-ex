// jsp_net_game.cpp -- ENHANCEMENT (EXPERIMENTAL rollback netcode): the game-side glue of the rollback session. OPT-IN: JSP_ROLLBACK=1; with the switch unset
// nothing is installed and nothing changes (the hooks below are never registered). Built with the vendored GekkoNet (gekkonet/, JSP_ROLLBACK_BUILD); without
// JSP_ROLLBACK_BUILD this file is an inert stub. What has run and the known limits: docs/ROLLBACK.md.
// Game-specific; the game-independent logic is in jsp_net_glue.h, jsp_net_session.h, jsp_net_m2_link.h and the settings handshake's jsp_session_*.h.
//
// Switches (JSP_ROLLBACK=1 sets sensible defaults for the snapshot ring, the fight hash and the canonical pad hook before main(); the player never sets those):
//   JSP_ROLLBACK=1            opt-in (any non-zero value: not empty, first character not '0')
//   JSP_ROLLBACK_LISTEN=PORT  host: bind this UDP port and wait for one client (direct-IP host). The in-game Online page (Port Settings - Online: Netcode = Rollback, Role,
//   JSP_ROLLBACK_JOIN=IP:PORT client: join that host (direct-IP join).      Port / Host address, jsp_net_online.h) writes these keys to play_settings.txt; the launcher turns them into
//                             environment variables at the next start (a variable set by hand does the same; the file line wins over an inherited variable, as for every key).
//                             Rollback and the game's own online (PS3_NET_ONLINE) both on: refused before main(), neither runs (docs/ROLLBACK.md).
//   JSP_ROLLBACK_BENCH=N      ENHANCEMENT (test tooling): instead of LISTEN / JOIN, a local stress session rolls the fight back N (1..8) ticks every tick and logs the cost
//                             ([bench] lines, start_bench / bench_iteration); JSP_ROLLBACK_RECORD=1 saves each match's replay (save_replay). docs/ROLLBACK.md
//   JSP_ROLLBACK_BADGE=1      ENHANCEMENT (display, opt-in): the status badge "Rollback | path | delay Nf | last Mf" in the top-left corner while the session
//                             runs, and a "[net] status:" line every 30 s and at the end (jsp_net_badge.h; docs/ROLLBACK.md)
//   JSP_ROLLBACK_ALLOW_DEBUG=1  test tooling only (WORKAROUND to replace): the handshake's debug-switch refusal is off (the two-instance test runs the auto-pilot route; both peers need the same fight
//                             seed until the host's plan seed is applied to the game, see the doc)
// Flow: settings handshake on a pump thread (starts at once; survives scene loads) -> Started -> at the first fight tick the session starts: GekkoNet session,
// stepper (frame base = that tick), snapshot ring as save/load (jsp_snapshot_ext_*), canonical pad table, handover() of the handshake on both sides.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "jsp_net_lobby.h"   // ENHANCEMENT (lobby): jsp_net_lobby_open / close at runtime

#ifndef JSP_ROLLBACK_BUILD
extern "C" int jsp_net_lobby_open(const JspLobbyCfg*) { return 0; }
extern "C" void jsp_net_lobby_close(void) {}
extern "C" const char* jsp_net_lobby_reason(void) { return "this build has no rollback netcode"; }
extern "C" int jsp_net_lobby_phase(void) { return 0; }
extern "C" int jsp_net_match_begin(void) { return 0; }
extern "C" void jsp_net_match_end(void) {}
extern "C" unsigned jsp_net_match_index(void) { return 0; }
extern "C" void jsp_net_game_install(void) {}
extern "C" void jsp_net_env_part(int) {}
extern "C" int jsp_net_notice(char*, int, char*, int) { return 0; }
extern "C" unsigned jsp_net_notice_gen(void) { return 0; }
extern "C" int jsp_net_presence(int* players, int* host) { *players = 0; *host = 0; return 0; }
extern "C" unsigned long long jsp_net_presence_room(char* server, int sn) { if (server && sn > 0) server[0] = 0; return 0; }
#else

#include <atomic>
#include <memory>
#include <mutex>
#include <random>
#include <array>
#include <cmath>
#include <string>
#include <vector>
#include <utility>
#include <windows.h>

#include "ps3emu/ps3types.h"
#include "ppu_recomp.h"
#include "jsp_hook_chain.h"
#include "jsp_overlay_api.h"
#include "jsp_net_session.h"
#include "jsp_net_online.h"
#include "jsp_net_badge.h"
#include "jsp_net_delay_rule.h"   // ENHANCEMENT (adaptive input delay, rule 2): the decision, shared with the host simulator
#include "jsp_net_perf.h"   // ENHANCEMENT (match perf summary): input latency, rollback depth, hitches, replay shown; five [perf] lines per match
#include "jsp_net_ko_rule.h"      // FIX (review of PR 248): the KO / time-up end rules and the live confirmation watermark, shared with net_session_test
#include "jsp_net_repsn_client.h"
#include "jsp_net_glue.h"
#include "jsp_net_report.h"
extern uint8_t* vm_base;   // the guest address space (DIAGNOSTIC page_dump)
// The per-tick log lines ([net] commit / rollback to tick) cost an unbuffered write each in the hot path: off unless JSP_NET_TICK_LOG=1 (the two-copy test
// compares them, build/bench/iterate.py turns it on). JSP_NET_CLOCK_DIAG=1: count which threads read the guest clock (printed at the end of a match).
static bool tick_log() { static const bool v = [] { const char* e = std::getenv("JSP_NET_TICK_LOG"); return e && e[0] == '1'; }(); return v; }
static bool clock_diag() { static const bool v = [] { const char* e = std::getenv("JSP_NET_CLOCK_DIAG"); return e && e[0] == '1'; }(); return v; }   // ENHANCEMENT (testers): the match report file
#include "jsp_net_m2_link.h"
#include "jsp_session_handshake.h"
#include "jsp_net_match.h"   // ENHANCEMENT (same-link rematch): the match counter's session ids
#include "jsp_net_native_fight.h"   // ENHANCEMENT (native fight takeover): arm / disarm around the rollback session
#include "jsp_session_reader.h"
#include "jsp_session_manifest.h"
#include "jsp_mods_glue.h"   // rollback_gate, kModsNotAsListed (ENHANCEMENT, mods)
#include "jsp_version_line.h"   // parse_version_txt (ENHANCEMENT, version mismatch message)
#include "jsp_thread_bit.h"   // jtb::thread_bit: guest thread id 1..64 -> mask bit t - 1 (helper-thread register save / restore)

extern "C" {
int jsp_online_repsn_identity(char* server, int sn, char* name, int nn, char* comm, int cn, int* report_rtt);   // jsp_online.cpp: repsn.ini even with enabled=0
int jsp_online_source(void);   // jsp_online.cpp: where the game's own online came from (jpno::OnlineSource)
extern int (*jsp_session_text_hook)(const char* key, char* out, int n);   // jsp_settings.cpp
int jsp_setting_get(const char* key, char* out, int n);
void jsp_setting_changed(void);   // jsp_settings.cpp: bumps jsp_setting_gen so the cached readers re-read
uint64_t jsp_snapshot_ext_pool_evicted(void);
void jsp_snapshot_ext_set_budget_ms(unsigned ms);   // the commit gate's wall-time budget
void jsp_snapshot_ext_budget_reset(void);           // after every session update: one budget from the last update across all gates
void jsp_snapshot_ext_set_idle_hook(void (*fn)());
void jsp_snapshot_ext_guard_summary(void);
void jsp_snapshot_ext_session_end(void);           // FIX (same-link rematch): the next session starts with a full baseline at its tick 1
int jsp_snapshot_ext_rsx_write_hazard(void);      // write guard: the previous capture had RSX commands pending and an RSX-side write into saved memory landed after it
int rsx_d3d12_pipe_effective_state(void);
void jsp_stepper_abort(void);
struct JspTickState { uint32_t done; uint32_t anc_done; uint8_t anc_set, anc_skip; };
void jsp_fight_hash_get_state(JspTickState* s);
void jsp_stepper_net_enable(void* table);
void jsp_stepper_net_disable(void);
int jsp_stepper_local_pad(int port, uint8_t canon[8]);
void jsp_stepper_replay(int on, uint32_t done);
int jsp_snapshot_ext_active(void);
int jsp_snapshot_ext_commit(uint32_t k, ppu_context* c, uint32_t* hash);
uint32_t jsp_snapshot_ext_hash(uint32_t k);
int jsp_snapshot_ext_rollback(uint32_t k, uint32_t to, ppu_context* c);
void ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));
void jsp_fight_hash_reset_fight(void);   // jsp_fight_hash.cpp (ENHANCEMENT, online rematch): forget the fight that ended, the next one anchors again
void jsp_fight_hash_native_online(int on);   // jsp_fight_hash.cpp (FIX, review of PR 248): 1 = the battle setup table is out of the checksum (native online fight)
int jsp_fight_end_state(void);           // jsp_fight_hash.cpp (ENHANCEMENT, time-up): -1 unknown, 0 running, 1 red, 2 blue, 3 time up (winner not written yet)
int jsp_fight_ko_winner(void);           // jsp_fight_hash.cpp (ENHANCEMENT, online match end): -1 unknown, 0 nobody yet, 1 red won, 2 blue won (guest words: rolled back)
extern volatile int g_ko_track;          // jsp_game.cpp: 1 = find the KO counters even with the default KO count (a rollback match)
}

namespace {

// The optional 60 Hz render gate exists only in development builds (JSP_SESSION_RENDER60_PROBE, see jsp_session_startup.cpp); this build has none, so its mode is off.
#if defined(JSP_SESSION_RENDER60_PROBE) && JSP_SESSION_RENDER60_PROBE == 1
extern "C" int jsp_render60_mode(void);
int render60_mode() { return jsp_render60_mode(); }
#else
int render60_mode() { return 0; }
#endif

using namespace jspnet;
constexpr uint32_t kMaxPrediction = 8, kRingDepth = 12;   // ring depth must exceed the prediction window (NetSession::start refuses otherwise)

bool env_on(const char* n) { const char* e = std::getenv(n); return e && *e && e[0] != '0'; }
std::vector<std::string> g_own_env;   // names this module set itself: they must not count as debug switches

// ENHANCEMENT (in-game netcode option): ONE place decides the rollback start, before main(), once the online files have been applied (jsp_online.cpp's boot sets PS3_NET_ONLINE).
// The two static constructors (this file's EnvInit and jsp_online_boot, which calls jsp_net_env_part) run in an unspecified order, so each reports in and the last one
// decides (jpno::Rendezvous). The decision (jpno::resolve_env, host-tested) sets the defaults of the snapshot ring, the per-tick fight hash and the turbo stepper
// (what JSP_ROLLBACK=1 always set before) ONLY when it is a valid start; a refused one (the game's own online also on, or not exactly one valid role key) sets none,
// clears JSP_ROLLBACK (sets it to 0) and sets JSP_NET_REFUSED=1; for the pair it also removes PS3_NET_ONLINE so neither runs. Why removing that one variable is enough (toolkit
// libs/network, runtime/ppu/ppu_sysprx.cpp): the game's own online needs PS3_NET_ONLINE in every gate (np_psnr_enabled = PS3_NET_ONLINE and PSNR_SERVER, netctl_online, the host
// socket registration), so PSNR_SERVER and the other keys jsp_online.cpp sets stay inert without it. ASSUMPTION: the runtime reads it lazily (after main()) and nothing in the jsp
// exe sets it later (np_psnr_setup only sets it from a --psnr argument, which the runtime's own boot_main handles, not this exe).
// Remaining: jsp_thread_registry.cpp's TLS callback reads JSP_ROLLBACK at process attach, before any decision, and enables the thread registry (bookkeeping only).
jpno::Rendezvous g_rv;                // constant-initialized (no constructor): safe to use from another translation unit's static constructor
// ENHANCEMENT (display only, default on): why rollback did not start, for the on-screen notice (jsp_net_notice below; jsp_port_menu.cpp draws it on the main menu's top list).
// Plain atomics (constant-initialized): decide_env may run from another translation unit's static constructor, before this file's dynamic initializers.
std::atomic<int> g_refusal{0}, g_refusal_src{0};
void note_off(jpno::Refusal r) { g_refusal_src.store(jsp_online_source()); g_refusal.store(static_cast<int>(r)); }
// ENHANCEMENT (display only, default on): the re-psn rendezvous on the same notice (waiting for the other player, refreshed every 10 s; why the two games
// did not meet). Written by the pump thread (RePsnRendezvous::on_progress, the failure), read by jsp_net_notice; the generation tells the menu to re-read it.
RendezvousNotice g_rdv;
// ENHANCEMENT (lobby): the start decision's lobby outcome (JSP_ROLLBACK=1 with no role key): jsp_net_game_install installs the hooks and opens no session; the
// in-game lobby opens one later (jsp_net_lobby_open). Constant-initialized, like g_refusal (decide_env may run from another translation unit's constructor).
std::atomic<bool> g_boot_lobby{false};
void decide_env()
{
    std::string why; jpno::Refusal code = jpno::Refusal::None; bool lobby = false;
    const jpno::Boot r = jpno::resolve_env([](const char* k) { return std::getenv(k); },
                                           [](const char* k, const char* v) { _putenv_s(k, v ? v : ""); },   // "" removes the variable on Windows
                                           &g_own_env, &why, &code, &lobby);
    if (r == jpno::Boot::Refused) note_off(code);
    g_boot_lobby.store(r == jpno::Boot::Go && lobby);
    std::fprintf(stderr, "[net] env decision: %s (static constructors arrived: %s first, %s last)\n",
                 r == jpno::Boot::Go ? "rollback start" : r == jpno::Boot::Refused ? "REFUSED" : "rollback not requested",
                 g_rv.order[0] == 1 ? "EnvInit" : "online boot", g_rv.order[1] == 1 ? "EnvInit" : "online boot");
    if (r == jpno::Boot::Refused) std::fprintf(stderr, "[net] REFUSED: %s\n", why.c_str());
    if (g_boot_lobby.load()) std::fprintf(stderr, "[net] rollback lobby: no role key at start; the session is opened at runtime (jsp_net_lobby_open)\n");
}
struct EnvInit { EnvInit() { g_rv.arrive(1, decide_env); } } g_env_init;

// ---- the temporary session layer: what jsp_setting_get answers while a session is installed -----------------------------------------------------------------
std::mutex g_pub_mu;
bool g_pub_active = false;
jpsm::Values g_pub_values;

bool canon_to_text(jpsm::Id id, uint32_t v, char* out, int n)
{
    jpss::Key k;
    if (jpsm::shares_jpss(id, &k)) {
        const auto& r = jpss::kRules[static_cast<size_t>(k)];
        if (v >= r.count) return false;
        std::snprintf(out, (size_t)n, "%s", r.values[v]);
        return true;
    }
    std::snprintf(out, (size_t)n, "%u", v);
    return true;
}
bool text_to_canon(jpsm::Id id, const char* t, uint32_t* out)
{
    jpss::Key k;
    if (jpsm::shares_jpss(id, &k)) {
        const auto& r = jpss::kRules[static_cast<size_t>(k)];
        for (uint8_t i = 0; i < r.count; ++i) if (!std::strcmp(t, r.values[i])) { *out = i; return true; }
        return false;
    }
    char* e = nullptr; const unsigned long v = std::strtoul(t, &e, 10);
    if (e == t || *e) return false;
    *out = (uint32_t)v;
    return true;
}
// Called with the settings mutex held (jsp_settings.cpp): takes only g_pub_mu, never calls back into jsp_setting_*.
int session_text(const char* key, char* out, int n)
{
    jpsm::Id id;
    if (!key || !jpsm::find_id(key, &id) || jpsm::rule(id).boundary == jpsm::Boundary::Startup) return 0;
    std::lock_guard<std::mutex> lk(g_pub_mu);
    if (!g_pub_active) return 0;
    return canon_to_text(id, g_pub_values.v[static_cast<size_t>(id)], out, n) ? 1 : 0;
}

// ---- settings-handshake Reader against the real settings path --------------------------------------------------------------------------------------------------------------
class GameReader : public jpsm::Reader {
public:
    bool startup_value(jpsm::Id id, uint32_t* out) override
    {
        jpss::Key k;
        if (jpsm::shares_jpss(id, &k)) {   // Pipe / Render60: only the actual initialized reader counts, never configuration
            const jpss::ReaderStartup rs = jpss::reader_startup();
            if (!rs.available) return false;
            *out = rs.applied.choices[static_cast<size_t>(k)];
            return true;
        }
        return from_environment(id, out);   // KO target, training cheats, unlock: read once at registration from the environment, so that is their startup value
    }
    bool read_back(jpsm::Id id, uint32_t* out) override
    {
        if (jpsm::rule(id).boundary == jpsm::Boundary::Startup) return startup_value(id, out);
        return from_environment(id, out);   // jsp_setting_get: the session layer (hook) first, else the live value, else the environment
    }
    void applied(const jpsm::Values& v) override { { std::lock_guard<std::mutex> lk(g_pub_mu); g_pub_values = v; g_pub_active = true; } jsp_setting_changed(); }
    // Called only when the layer is removed (the session really ended: end_with calls end_match / match_ended; a mid-match Cancel ends the match, see boundary()).
    void restored() override { { std::lock_guard<std::mutex> lk(g_pub_mu); g_pub_active = false; } jsp_setting_changed(); }
    const char* debug_switch() override
    {
        // WORKAROUND to replace (test tooling only): the two-instance test runs the auto-pilot route and a fixed fight seed, all debug switches by the handshake's list.
        if (env_on("JSP_ROLLBACK_ALLOW_DEBUG")) return nullptr;
        for (const jpsm::DebugSwitch& d : jpsm::kDebugSwitches) {
            bool own = false;
            for (const std::string& s : g_own_env) if (s == d.name) own = true;
            if (own) continue;
            if (jpsm::trigger_hit(d.trigger, std::getenv(d.name), d.arg)) return d.name;
        }
        return nullptr;
    }
private:
    static bool from_environment(jpsm::Id id, uint32_t* out)
    {
        char b[64];
        if (!jsp_setting_get(jpsm::rule(id).name, b, sizeof b) || !b[0]) { *out = jpsm::rule(id).def; return true; }
        return text_to_canon(id, b, out);
    }
};

// ---- state ------------------------------------------------------------------------------------------------------------------------------------------------
enum class Phase { Off, Handshake, Armed, Running, Ended };
struct State {
    bool host = false;
    bool fight_seen = false; uint32_t hud_mark = 0, hud_tick = 0;   // FIX (loading): the fight HUD was drawn since the session started (neutral pad and loading barrier before it)
    uint16_t listen_port = 0; Endpoint join{};
    UdpMux mux; std::unique_ptr<MessageHub> hub; std::unique_ptr<IRendezvous> rz;
    std::unique_ptr<PsnrClientTransport> rp; RePsnRendezvous* rrz = nullptr;   // ENHANCEMENT (rollback over re-psn): the client transport and the rendezvous over it (rz owns the rendezvous)
    std::string path_line; uint64_t path_until = 0;   // the "direct" / "relay" line shown for the first seconds of the match
    std::unique_ptr<M2LinkT<jpsm::Transport>> link; M2Pump pump;
    GameReader reader; jpsm::EffectiveValues ev{&reader}; jpsm::Caps caps;
    std::unique_ptr<jpsm::Host> mh; std::unique_ptr<jpsm::Client> mc;
    SessionPlan plan; bool plan_ready = false, proposed = false; uint32_t propose_tries = 0;
    std::mutex txt_mu; std::string fail_text;          // fail_text / m2_text: written by the pump thread or main, under txt_mu
    std::atomic<Phase> phase{Phase::Off};
    std::atomic<bool> m2_started{false}, m2_ended{false}, evidence_ready{false};
    std::string m2_text, m2_display; uint32_t base = 0;   // m2_display: what the message line shows instead of m2_text ("" with m2_quiet: the menu notice carries it)
    bool m2_quiet = false;
    NetSession sess; jsx::Stepper stepper; std::unique_ptr<Glue> glue;
    ppu_context* ctx = nullptr; uint32_t done_now = 0;
    uint64_t cancel_since = 0;
    bool skip_sim = false, skip_pad = false, was_replaying = false;
    uint64_t last_run_ms = 0, started_ms = 0, settings_shown_ms = 0;
    std::string shown;
    // ENHANCEMENT (status badge, JSP_ROLLBACK_BADGE=1): main-thread only (boundary, the glue's rollback, end_with). path_word is set once the plan exists.
    bool badge_on = false; std::string badge_shown; jpnb::Values bv; jpnb::Every badge_every, status_every;
    // ENHANCEMENT (ping counter, default on; JSP_ROLLBACK_PING=0 hides it): the round trip under the fight timer (jsp_net_badge.h). Main thread only.
    bool ping_on = true; std::string ping_shown; uint32_t ping_rgba = 0; jpnb::Every ping_every;
    std::atomic<uint64_t> presence_room{0};   // ENHANCEMENT (Discord Join): the re-psn room this game is in (host: the room it created), 0 = none; written by the pump thread
    char presence_srv[160] = {};               // the re-psn server (host:port), set once before the pump thread starts
    std::atomic<int> path_kind{0};   // 0 unknown, 1 direct, 2 relay: written by the pump thread when the rendezvous ends, read by the main thread
    bool record = false;             // ENHANCEMENT (replays, JSP_ROLLBACK_RECORD=1): save the match's recording when it ends (save_replay)
    // ENHANCEMENT (rollback bench, JSP_ROLLBACK_BENCH=N): the fight runs through a local stress session that rolls back N ticks on every tick (no peer, no socket).
    // What it measures, per 10 s window: live ticks per second (30 = the rollback load fits), the wall time of a main-loop iteration that ran a live tick, a re-run tick or
    // no tick, and the snapshot save / restore calls. Main thread only.
    int bench = 0;
    // ENHANCEMENT (replay-driven bench, JSP_ROLLBACK_INPUTS=<replay_dump csv>): player 1's input of every session update comes from a recorded fight instead of the
    // pad, one row per update in order (then a centred, connected pad), so a bench run replays a real human fight with no one at the pad and two runs are comparable.
    std::vector<std::array<uint8_t, 8>> feed; size_t feed_i = 0; bool feed_end_said = false;
    struct Acc { double sum = 0, max = 0; uint64_t n = 0; void add(double v) { sum += v; n++; if (v > max) max = v; } double avg() const { return n ? sum / (double)n : 0; } };
    // ph[k][i]: phase i (1 F.vt[8], 2 F.vt[0xC], 3 pad_update, 4 the scene update) of iterations of kind k (0 live, 1 re-run)
    struct Bench { Acc live, rerun, idle, save, restore; Acc ph[2][5]; uint64_t desyncs = 0; int first_desync = -1; uint64_t win_ms = 0; int64_t last_qpc = 0; int last_kind = -1; } bs, bs_total;
    // ENHANCEMENT (lobby): per-match state that used to be function statics (boundary's HUD-draw watch before the start; reset by reset_match), and the pump
    // thread's per-connection timers and last logged handshake state (pump_tick; reset by conn_close, with the pump thread stopped).
    uint32_t hud_seen = 0; bool hud_seen_set = false;
    uint64_t ev_first = 0, ev_next = 0, wait_next = 0; int hs_last = -2;
    // ENHANCEMENT (online match end): the KO winner of the first CONFIRMED tick that has one (GameGlue commit / hash_only, main thread): 1 red, 2 blue. boundary
    // ends the match on it. ended_ms: when end_with ran; the session object lingers until kLingerMs after it (boundary), then NetSession::stop().
    int match_winner = 0; uint64_t ended_ms = 0;
    // ENHANCEMENT (same-link rematch, jsp_net_match.h): the match counter of the open link (written by the main thread under the pump mutex, read by the pump thread
    // under it), the newest match the other side was seen at (pump thread -> main), the host's beacon timer (pump thread). fresh_gate: a match ended in a fight, so a
    // re-armed match waits until that fight is gone (boundary) before the fight hash may anchor again. last_end_ok: the last end was a normal one (end_with).
    uint32_t match_k = 0; std::atomic<uint32_t> match_jump{0}; uint64_t beacon_next = 0;
    bool fresh_gate = false, last_end_ok = false;
    // FIX (review of PR 248): the pump thread ended this match's session on the frame clock's failure (pump_tick); cleared by start_session
    std::atomic<bool> stall_ended{false};
} g;
// FIX (a missed KO): the KO / time-up winner of EVERY saved tick (commit and hash_only, predicted or not), keyed by tick; a re-save after a
// rollback overwrites its entry and a rollback drops the entries after its target. The boundary ends the match on the earliest recorded winner whose tick is
// confirmed (ko_scan). Before, only saves of ticks that were already final were read, so a tick saved while predicted (its prediction right, never saved
// again) hid the KO on one PC: that PC stalled into "Opponent disconnected" while the other ended normally. Main thread; cleared by reset_match.
// FIX (review of PR 248: a predicted result must not survive a disconnect): the rules are in jsp_net_ko_rule.h (host-tested by net_session_test --ko-from);
// g_ko_live is the live confirmation watermark, read at every boundary whose events showed no disconnect (boundary, before ko_scan).
jkr::Ring g_ko;
jkr::Watermark g_ko_live;
void ko_clear() { g_ko.clear(); g_ko_live.reset(); }
// ENHANCEMENT (lobby): an ended match keeps its GekkoNet session this long (its keepalive thread still answers), so the other PC can confirm the last ticks
// (a natural match end has no abort, see end_match_over) before it is destroyed; stopped earlier by a new session or jsp_net_lobby_close.
constexpr uint64_t kLingerMs = 2000;

// ENHANCEMENT (rollback bench): milliseconds from QueryPerformanceCounter (timers only, never simulation).
double qpc_ms(int64_t from, int64_t to) { static LARGE_INTEGER f{}; if (!f.QuadPart) QueryPerformanceFrequency(&f); return (double)(to - from) * 1000.0 / (double)f.QuadPart; }
int64_t qpc_now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
void bench_line(const char* what, const State::Bench& b, double secs)
{
    std::fprintf(stderr, "[bench] %s depth=%d secs=%.1f live_ticks=%llu (%.1f/s) rerun_ticks=%llu | iter ms live avg %.2f max %.2f, rerun avg %.2f max %.2f, idle avg %.2f max %.2f"
                 " | save avg %.2f max %.2f (%llu), restore avg %.2f max %.2f (%llu) | desyncs %llu first %d\n",
                 what, g.bench, secs, (unsigned long long)b.live.n, secs > 0 ? (double)b.live.n / secs : 0.0, (unsigned long long)b.rerun.n, b.live.avg(), b.live.max,
                 b.rerun.avg(), b.rerun.max, b.idle.avg(), b.idle.max, b.save.avg(), b.save.max, (unsigned long long)b.save.n, b.restore.avg(), b.restore.max,
                 (unsigned long long)b.restore.n, (unsigned long long)b.desyncs, b.first_desync);
}
void bench_merge(State::Acc& to, const State::Acc& a) { to.sum += a.sum; to.n += a.n; if (a.max > to.max) to.max = a.max; }
// A phase hook's time, filed by what this iteration runs (bench_iteration sets last_kind at the boundary before the phases run).
void bench_phase(int i, double ms) { if (g.bs.last_kind == 1 || g.bs.last_kind == 2) g.bs.ph[g.bs.last_kind - 1][i].add(ms); }
void bench_phase_line(const State::Bench& b)
{
    const State::Acc (&p)[2][5] = b.ph;
    std::fprintf(stderr, "[bench] phases ms (avg/max): live P1 %.2f/%.2f P2 %.2f/%.2f pad %.2f/%.2f scene %.2f/%.2f | rerun P1 %.2f/%.2f P2 %.2f/%.2f pad %.2f/%.2f scene %.2f/%.2f\n",
                 p[0][1].avg(), p[0][1].max, p[0][2].avg(), p[0][2].max, p[0][3].avg(), p[0][3].max, p[0][4].avg(), p[0][4].max,
                 p[1][1].avg(), p[1][1].max, p[1][2].avg(), p[1][2].max, p[1][3].avg(), p[1][3].max, p[1][4].avg(), p[1][4].max);
}

uint32_t build_id()
{
    const char* e = std::getenv("JSP_NET_BUILD");   // test override
    if (e && *e) return (uint32_t)std::strtoul(e, nullptr, 0);
#ifdef JSP_NET_BUILD_ID
    return (uint32_t)JSP_NET_BUILD_ID;              // from the release content (jsp_build_id.cmake): every PC that builds the same release agrees
#else
    uint32_t h = 2166136261u;                       // built outside the project's CMake (syntax checks): FNV-1a of the compile date and time
    for (const char* p = __DATE__ " " __TIME__; *p; ++p) { h ^= (uint8_t)*p; h *= 16777619u; }
    return h;
#endif
}

// The glue's overlay layer holds the message line (bottom left), when on the status badge (top left) and the ping counter (under the fight timer, bottom right);
// a change of any redraws all.
void redraw()
{
    const std::string& s = g.shown; const std::string& b = g.badge_shown; const std::string& p = g.ping_shown;
    if (s.empty() && b.empty() && p.empty()) { jsp_overlay_close(); return; }
    if (!jsp_overlay_begin(1280, 720)) return;
    if (!b.empty()) {
        const float bw = jsp_overlay_text_width(b.c_str(), jpnb::kScale);
        jsp_overlay_fill_rect(jpnb::kBoxX, jpnb::kBoxY, (int)(bw > 0 ? bw : 360) + 2 * jpnb::kBoxPad, jpnb::kBoxH, JSP_RGBA(0, 0, 0, 160));
        jsp_overlay_text_style(jpnb::kX, jpnb::kY, b.c_str(), jpnb::kScale, JSP_RGBA(255, 255, 255, 255), 0);
    }
    if (!p.empty()) {
        const float pw = jsp_overlay_text_width(p.c_str(), jpnb::kPingScale);
        jsp_overlay_fill_rect(jpnb::ping_box_x(p, pw), jpnb::kPingBoxY, jpnb::ping_box_w(p, pw), jpnb::kPingBoxH, JSP_RGBA(0, 0, 0, 160));
        jsp_overlay_text_style(jpnb::ping_x(p, pw), jpnb::kPingY, p.c_str(), jpnb::kPingScale, g.ping_rgba, 0);
    }
    if (!s.empty()) {
        const float w1 = jsp_overlay_text_width(s.c_str(), 1.0f), sc = w1 > 1180 ? 1180 / w1 : 1.0f, w = w1 * sc;   // a long line (the version mismatch) shrinks to fit the width
        jsp_overlay_fill_rect(30, 640, (int)(w > 0 ? w : 800) + 40, 50, JSP_RGBA(0, 0, 0, 190));
        jsp_overlay_text_style(50, 652 + 10.5f * (1 - sc), s.c_str(), sc, JSP_RGBA(255, 255, 255, 255), 0);
    }
    jsp_overlay_end();
}
void show(const std::string& s) { if (s == g.shown) return; g.shown = s; redraw(); }
void badge(const std::string& b) { if (b == g.badge_shown) return; g.badge_shown = b; redraw(); }
void ping_hide() { if (g.ping_shown.empty()) return; g.ping_shown.clear(); redraw(); }
std::string path_word() { const int k = g.path_kind.load(); return k == 1 ? "direct" : k == 2 ? "relay" : ""; }
// ENHANCEMENT (input lag, smoothness audit cause 2): ON by default since 2026-11-21 (JSP_INPUT_DELAY_AUTO=0 turns it off). On a good link (ping <= ~40 ms with little jitter) it settles on 1 tick (the start value is the
// manifest's InputDelay, default 2), and it only goes higher when the trip needs it. The peers need not agree on it: GekkoNet stamps each input with its
// frame, both PCs simulate the same stamped inputs, and a delay change only moves which frame a local input lands on (sync.cpp SetDelay: a longer delay
// repeats the last input to fill the gap; a shorter one drops local inputs until the clock catches up). So the determinism rule is not touched.
// ENHANCEMENT (adaptive input delay). Every 2 s this peer sets its OWN input delay from the measured round trip: the delay that covers the one-way trip plus
// the jitter, so the other peer's inputs arrive before they are needed and a rollback (whose re-run costs more than a frame in this port today) stays rare.
// The decision is jsp_net_delay_rule.h (pure, so net_four_sim runs the same code): rule 2 by default (since 2026-11-21, tester support bundle 1: the match
// ran at 2 ticks while it needed 3, the averaged jitter hid the spikes, and 5-8 tick rollbacks went unanswered): a percentile of GekkoNet's single round
// trips over the last 8 s instead of its average and an EMA of its jitter, a first reading 4 s after the sync (raising at most one tick before 10 s: the
// fight-start stall), a larger margin from 80 ms, one tick up at once after two rollbacks deeper than 2 ticks within 10 s, and down one tick at a time
// after 10 calm seconds. JSP_INPUT_DELAY_RULE=1 is the earlier rule
// (EMA, first reading at 10 s, two readings to raise). Switches: JSP_INPUT_DELAY_AUTO (0 = off), _MIN, _MAX, _MARGIN_MS, and for rule 2 _FIRST_MS, _PCT,
// _SAMPLES, _HIGH_PING_MS, _HIGH_MARGIN_MS, _UP_VOTES, _DOWN_VOTES, _DEEP_TICKS, _DEEP_COUNT (0 = no depth trigger), _DEEP_WINDOW_MS, _HOLD_MS
// (all JSP_INPUT_DELAY_AUTO_*; see the header for each). Read once per process.
// Each peer changes only its own delay: inputs are stamped with their tick, so the two peers need not agree (the manifest's InputDelay is the start value).
// FIX (rematch inherited delay state): the rule's state is per match (reset_match).
// FIX (fight-start stall, runs native-rb1/rb2: the first reading of a match said ping 243-416 ms on a 60 ms link and jumped the delay to 4): see the guard above.
// FIX (ping includes the answer wait): GekkoNet answers a ping at its next poll, once a tick, so the measured round trip carries up to one tick (33 ms) of
// waiting that is no network time (rb1: 30 ms each way measured 94-100 ms); the rule takes up to half the round trip, capped at one tick, off before halving.
// FIX (delay flapping, two-copy run native-rb1: 2 -> 3 -> 2 every 6-10 s on a steady 30 ms link): lowering needs five calm readings (10 s) in both rules.
jpdr::State g_ad;   // per match (reset_match)
int g_ad_on = -1;   // the switches are read once per process
jpdr::Params g_ad_p;
// ENHANCEMENT (rollback hitches, JSP_ROLLBACK_PREDICTION=N, 1..8, default 6; 8 = as before): how many ticks this PC may run ahead on predicted inputs before
// it waits (GekkoNet's input prediction window, set at the first synced boundary of each match; the session is created with 8). It caps the deepest
// rollback, and so the longest hitch, at N ticks (testers: 8-tick rollbacks). Each PC alone: it only changes when this PC waits, never which inputs a tick
// simulates. Host simulation (PR #248, Update 3): 6 is neutral to slightly better than 8 on 99-129 ms links; 4 or lower is worse (waits replace hitches).
int g_ad_pred = 6;
void adapt_settings()
{
    if (g_ad_on >= 0) return;
    char b[16] = {};
    g_ad_on = !(jsp_setting_get("JSP_INPUT_DELAY_AUTO", b, sizeof b) && b[0] == '0');   // default on (see above)
    auto num = [&b](const char* key, int& v) { b[0] = 0; if (jsp_setting_get(key, b, sizeof b) && b[0]) v = std::atoi(b); };
    jpdr::Params& p = g_ad_p;
    num("JSP_INPUT_DELAY_RULE", p.rule);
    num("JSP_INPUT_DELAY_AUTO_MIN", p.lo); num("JSP_INPUT_DELAY_AUTO_MAX", p.hi); num("JSP_INPUT_DELAY_AUTO_MARGIN_MS", p.margin_ms);
    num("JSP_INPUT_DELAY_AUTO_FIRST_MS", p.first_ms); num("JSP_INPUT_DELAY_AUTO_PCT", p.pct); num("JSP_INPUT_DELAY_AUTO_SAMPLES", p.samples);
    num("JSP_INPUT_DELAY_AUTO_HIGH_PING_MS", p.high_ping_ms); num("JSP_INPUT_DELAY_AUTO_HIGH_MARGIN_MS", p.high_margin_ms);
    num("JSP_INPUT_DELAY_AUTO_UP_VOTES", p.up_votes); num("JSP_INPUT_DELAY_AUTO_DOWN_VOTES", p.down_votes);
    num("JSP_INPUT_DELAY_AUTO_DEEP_TICKS", p.deep_ticks); num("JSP_INPUT_DELAY_AUTO_DEEP_COUNT", p.deep_count);
    num("JSP_INPUT_DELAY_AUTO_DEEP_WINDOW_MS", p.deep_window_ms); num("JSP_INPUT_DELAY_AUTO_HOLD_MS", p.hold_ms);
    num("JSP_ROLLBACK_PREDICTION", g_ad_pred);
    g_ad_pred = std::min(std::max(g_ad_pred, 1), 8);
    if (g_ad_pred != 8) std::fprintf(stderr, "[net] prediction window %d ticks (JSP_ROLLBACK_PREDICTION; 8 = as before)\n", g_ad_pred);
    p.clamp();
    if (!g_ad_on) std::fprintf(stderr, "[net] adaptive input delay off (JSP_INPUT_DELAY_AUTO=0: the manifest's InputDelay holds)\n");
    else if (p.rule == 1) std::fprintf(stderr, "[net] adaptive input delay on (%d..%d ticks, margin %d ms; JSP_INPUT_DELAY_AUTO=0 turns it off)\n", p.lo, p.hi, p.margin_ms);
    else std::fprintf(stderr, "[net] adaptive input delay on (%d..%d ticks, margin %d ms; rule 2: p%d of %d round trips, margin %d ms from %d ms, first reading %d ms, "
                      "depth trigger %d rollbacks > %d ticks in %d ms; JSP_INPUT_DELAY_AUTO=0 turns it off)\n", p.lo, p.hi, p.margin_ms, p.pct, p.samples, p.high_margin_ms,
                      p.high_ping_ms, p.first_ms, p.deep_count, p.deep_ticks, p.deep_window_ms);
}
// A rollback of `depth` ticks (the glue's rollback, main thread): rule 2's depth trigger.
void adapt_note_rollback(int64_t depth) { if (g_ad_on > 0 && !g.bench) jpdr::note_rollback(g_ad, g_ad_p, GetTickCount64(), depth); }
void adapt_delay(uint64_t now)
{
    adapt_settings();
    if (!g.sess.synced()) return;
    if (!g_ad.start && g_ad_pred != 8) g.sess.set_prediction_window((uint8_t)g_ad_pred);   // once per match (g_ad is reset per match)
    if (!g_ad_on) { if (!g_ad.start) g_ad.start = now; return; }
    const jpdr::Decision d = jpdr::step(g_ad, g_ad_p, now, [] {
        const SessionStats st = g.sess.stats();
        jpdr::Reading r; r.ping_avg = st.ping_ms; r.jitter = st.jitter_ms; r.last_ping = st.last_ping_ms; r.rtt_seq = st.rtt_seq; r.cur = st.local_delay;
        return r;
    });
    if (d.want < 0 || d.want == d.cur) return;
    g.sess.set_local_delay((uint8_t)d.want); g.bv.delay = d.want;
    // The line starts as before ("input delay A -> B ticks (ping P ms, jitter J ms"): GekkoNet's averages, whatever the rule.
    if (g_ad_p.rule == 1) std::fprintf(stderr, "[net] input delay %d -> %d ticks (ping %.1f ms, jitter %.1f ms)\n", d.cur, d.want, d.ping_avg, d.jitter_avg);
    else if (d.why[0] == 'r') std::fprintf(stderr, "[net] input delay %d -> %d ticks (ping %.1f ms, jitter %.1f ms; %d rollbacks deeper than %d ticks within %d ms)\n",
                                           d.cur, d.want, d.ping_avg, d.jitter_avg, g_ad_p.deep_count, g_ad_p.deep_ticks, g_ad_p.deep_window_ms);
    else std::fprintf(stderr, "[net] input delay %d -> %d ticks (ping %.1f ms, jitter %.1f ms; round trip median %.1f ms, p%d +%.1f ms, need %.1f ms)\n",
                      d.cur, d.want, d.ping_avg, d.jitter_avg, d.ping, g_ad_p.pct, d.jitter, d.need_ms);
}
void log_status() { g.bv.path = path_word(); std::fprintf(stderr, "[net] %s\n", jpnb::log_line(g.bv).c_str()); }
// ENHANCEMENT (match perf summary, jsp_net_perf.h): per match (reset_match), main thread; from the first fight HUD draw, never in the bench.
jpperf::Match g_perf; int64_t g_perf_t0 = 0;
double perf_now() { if (!g_perf_t0) g_perf_t0 = qpc_now(); return qpc_ms(g_perf_t0, qpc_now()); }
bool perf_on() { return !g.bench && g.fight_seen; }
void perf_log()
{
    if (g.bench || !g.started_ms) return;
    const std::string s = jpperf::lines(g_perf, path_word().c_str(), g.rrz ? "re-psn" : "direct IP", g.host ? "host" : "join", g.match_k,
                                        (double)(GetTickCount64() - g.started_ms) / 1000.0);
    std::fputs(s.c_str(), stderr);
}

uint64_t rz_rejected() { if (auto* d = dynamic_cast<DirectRendezvous*>(g.rz.get())) return d->rejected_hello; return g.rrz ? g.rrz->rejected_members : 0; }
// FIX (rollback determinism, JSP_AUDIO_TICKLOCK=0 turns it off): tick-locked audio (patch 3005). The game's sound library advances on the audio device's
// clock, and the game acts on it (a voice line that ends starts the next sound; the pre-fight intro camera waits for one): two PCs did that on ticks 1-4 apart
// (two-copy tests: the playback count at 0x0094DEC4 at ticks 969-1031, then the intro camera at tick 1068). In a match the audio advances 6.25 blocks
// (1/30 s) per tick, and the next tick waits until the game's audio thread took them; a rollback moves the audio back with the tick, re-runs are muted.
extern "C" volatile long long g_ps3_audio_lock_target, g_ps3_audio_lock_done;
extern "C" volatile int g_ps3_audio_lock_mute;
extern "C" int ps3_audio_lock_idle(void);
extern "C" void ps3_audio_lock_rewind(long long blocks);
extern "C" void ps3_audio_lock_set_target(long long blocks);   // sets the target and wakes the mix thread
bool audio_lock_on() { static const bool v = [] { char b[8] = {}; return !(jsp_setting_get("JSP_AUDIO_TICKLOCK", b, sizeof b) && b[0] == '0'); }(); return v; }
long long audio_blocks_at(uint32_t done) { return (long long)(done - g.base) * 25 / 4; }   // 48000 / 256 / 30 = 6.25 blocks per tick
uint64_t g_audio_waits = 0, g_audio_timeouts = 0; double g_audio_wait_ms = 0;
void audio_lock_wait()
{
    const int64_t t0 = qpc_now();
    for (int spins = 0; !ps3_audio_lock_idle(); spins++) {
        if (qpc_ms(t0, qpc_now()) > 250.0) { if (g_audio_timeouts++ < 5) std::fprintf(stderr, "[net] audio lock: the audio thread did not take its blocks within 250 ms (target %lld, mixed %lld)\n", (long long)g_ps3_audio_lock_target, (long long)g_ps3_audio_lock_done); break; }
        if (spins > 50) { Sleep(0); if ((spins & 63) == 0) g.sess.poll(); }
    }
    g_audio_waits++; g_audio_wait_ms += qpc_ms(t0, qpc_now());
}
void audio_lock_start()
{
    if (!audio_lock_on() || g.bench) return;
    g_ps3_audio_lock_mute = 0; g_ps3_audio_lock_done = 0; ps3_audio_lock_set_target(0);
    audio_lock_wait();
    std::fprintf(stderr, "[net] audio locked to the game tick (6.25 blocks per tick from base %u)\n", g.base);
}
void audio_lock_end()
{
    if (g_ps3_audio_lock_target < 0) return;
    ps3_audio_lock_set_target(-1); g_ps3_audio_lock_mute = 0;
    std::fprintf(stderr, "[net] audio lock ended: %llu waits, mean %.2f ms, %llu timeouts\n", (unsigned long long)g_audio_waits, g_audio_waits ? g_audio_wait_ms / g_audio_waits : 0.0, (unsigned long long)g_audio_timeouts);
}
// FIX (rollback determinism, JSP_NET_GUEST_CLOCK=0 turns it off): the game's main thread reads a clock that advances with the game tick (patch 3006).
// The guest reads the timebase (mftb, 129 sites) and sys_time_get_system_time, both the PC's clock; with the audio locked, the pre-fight intro camera
// still started on different ticks on two copies (tick 879). In a match the main thread's clock is the session start time + 1/30 s per tick + 1 us per read
// within the tick (time still moves for a loop that waits on it); a rollback re-run reads the same times. Other threads keep the real clock.
extern "C" int64_t (*g_ps3_guest_tb_shift)(uint64_t real_tb);
extern "C" void jsp_stepper_lockstep(int on);   // jsp_stepper.cpp
extern "C" int jsp_stepper_lockstep_stretch_us(uint32_t us);   // jsp_stepper.cpp: the time-sync stretch of the next lockstep period (0 = lockstep clock off)
extern "C" int jsp_stepper_lockstep_failed(void);   // jsp_stepper.cpp: 1 = the guest took no vblank for 60 s of real ack waiting (drains excluded); cleared by jsp_stepper_lockstep(0)
extern "C" void jsp_stepper_rerun_tick(uint32_t tick, int log);   // jsp_stepper.cpp: DIAGNOSTIC, a re-run tick was granted
extern "C" volatile int g_jsp_main_stage;   // jsp_stepper.cpp: DIAGNOSTIC, where the main thread is in the boundary (the re-run stall lines name it)
extern "C" uint64_t ppu_timebase_real_now(void);
std::atomic<DWORD> g_vc_tid{0}; uint64_t g_vc_t0 = 0; uint32_t g_vc_reads = 0; uint64_t g_vc_total_reads = 0;
constexpr uint64_t kTbPerTick = 79800000ull / 30;   // 2,660,000 timebase ticks
// DIAGNOSTIC (always on while the hook is set, cheap): which other threads read the guest clock in a match, and how often (printed at the end).
struct ClockReader { std::atomic<DWORD> tid{0}; std::atomic<uint64_t> n{0}; };
ClockReader g_clock_readers[64];
void note_clock_reader(DWORD tid)
{
    for (ClockReader& r : g_clock_readers) {
        DWORD cur = r.tid.load(std::memory_order_relaxed);
        if (cur == tid) { r.n.fetch_add(1, std::memory_order_relaxed); return; }
        if (cur == 0) { DWORD z = 0; if (r.tid.compare_exchange_strong(z, tid) || z == tid) { r.n.fetch_add(1, std::memory_order_relaxed); return; } }
    }
}
extern "C" int ps3_ppu_thread_view(uint32_t tid, uint64_t* out, char* name);   // patch 7916: out[0] = host thread id
extern "C" void ps3_hle_thread_census(uint32_t* kind, uint32_t* id, uint32_t* obj, uint32_t* lr, uint8_t* alive, uint8_t* unreliable, uint32_t* overflows, uint32_t* unbalanced);
void print_clock_readers()
{
    for (ClockReader& r : g_clock_readers) {
        const DWORD tid = r.tid.load(); if (!tid) break;
        char nm[64] = "(not a guest thread)"; uint32_t gt = 0;
        for (uint32_t t = 1; t < 65; t++) { uint64_t o[5] = {}; char n2[64] = {}; if (ps3_ppu_thread_view(t, o, n2) && (DWORD)o[0] == tid) { std::snprintf(nm, sizeof nm, "%s", n2); gt = t; break; } }
        std::fprintf(stderr, "[net] clock reads in the match: host tid %lu guest tid %u (%s): %llu%s\n", (unsigned long)tid, gt, nm, (unsigned long long)r.n.load(),
                     tid == g_vc_tid.load() ? " (main thread: game clock)" : "");
        r.tid.store(0); r.n.store(0);
    }
}
// FIX (rollback determinism, JSP_NET_CRI_LOCK=0 turns it off): the sound library's server thread (guest "CRI Server Manager", tid 17) is the only other thread
// that reads the clock in a match (13,000 reads in a fight, the main thread 2,000-3,000): it sleeps (sys_timer_usleep from 0x004D4F2C), reads the time, runs the
// library's timers (voice cues; the fight's idle camera cut waits on one), and sleeps again, on the PC's clock. Two copies started that camera cut 23-25 ticks
// apart with every input equal. In a match its sleep waits for the game instead (patch 3006, g_ps3_usleep_hook): at every tick boundary the game releases
// it for exactly one cycle, with the clock at the tick's time, and waits until it sleeps again before the tick runs.
extern "C" int (*g_ps3_usleep_hook)(ppu_context* ctx, uint64_t usec);
uint64_t g_regs_saved = 0, g_regs_restored = 0, g_regs_skipped = 0, g_regs_allow = 0;
double g_hold_ms = 0; uint64_t g_holds = 0, g_hold_renders = 0;   // FIX (hold at the boundary while no tick may run): totals for the end line   // helper thread registers (regs_save / regs_restore below)
std::atomic<bool> g_cri_on{false}; std::atomic<uint32_t> g_cri_go{0}, g_cri_ack{0}; std::atomic<int> g_cri_parked{0}; std::atomic<DWORD> g_cri_tid{0};
HANDLE g_cri_ev = nullptr; uint64_t g_cri_cycles = 0, g_cri_timeouts = 0; double g_cri_wait_ms = 0;
int cri_usleep_hook(ppu_context* c, uint64_t)
{
    if ((uint32_t)c->lr != 0x004D4F30u || !g_cri_on.load()) return 0;
    g_cri_tid.store(GetCurrentThreadId());
    const uint32_t my = g_cri_go.load();
    g_cri_ack.store(my); g_cri_parked.store(1);
    while (g_cri_on.load() && g_cri_go.load() == my) WaitForSingleObject(g_cri_ev, 5);
    g_cri_parked.store(0);
    return 1;
}
bool cri_lock_on() { static const bool v = [] { char b[8] = {}; return !(jsp_setting_get("JSP_NET_CRI_LOCK", b, sizeof b) && b[0] == '0'); }(); return v; }
void cri_wait(uint32_t want, double budget_ms)
{
    const int64_t t0 = qpc_now();
    for (int spins = 0; !(g_cri_ack.load() == want && g_cri_parked.load()); spins++) {
        if (qpc_ms(t0, qpc_now()) > budget_ms) { if (g_cri_timeouts++ < 5) std::fprintf(stderr, "[net] sound server lock: no cycle within %.0f ms (released %u, done %u)\n", budget_ms, want, g_cri_ack.load()); break; }
        if (spins > 50) { Sleep(0); if ((spins & 63) == 0) g.sess.poll(); }
    }
    g_cri_wait_ms += qpc_ms(t0, qpc_now());
}
// FIX (rollback determinism, with the sound server lock): the sound library's file threads ("CRI FS File Access" x4, "CRI FS Data Decompression",
// "CRI FS Memory File System") stream voice and music data from disk; a read finishes when the PC's disk is done, so a voice that has to wait for its data
// ended 6 ticks apart on two copies (camera cut at tick 2355 vs 2361, every input equal, sound server already per tick). At every boundary, before and after
// the server's cycle, the game waits until each of them sits in its idle wait (lr 0x004D3D4C, the lwcond wait of the CRI servers) on two samples in a row:
// a read started in one tick is always finished before the next one runs.
uint32_t g_fs_tids[16]; int g_fs_n = 0; uint64_t g_fs_waits = 0, g_fs_timeouts = 0; double g_fs_wait_ms = 0, g_fs_wait_max = 0;
void fs_find_threads()
{
    g_fs_n = 0;
    g_regs_allow = 0;
    for (uint32_t t = 1; t < 65; t++) { uint64_t o[5] = {}; char nm[64] = {}; if (!ps3_ppu_thread_view(t, o, nm)) continue;
        if (!std::strncmp(nm, "CRI", 3)) g_regs_allow |= jtb::thread_bit(t);   // the register save / restore covers the sound library's threads only (a shared wait wrapper)
        if (!std::strncmp(nm, "CRI FS", 6) && g_fs_n < 16) g_fs_tids[g_fs_n++] = t; }
    std::fprintf(stderr, "[net] sound library file threads: %d (guest tids", g_fs_n);
    for (int i = 0; i < g_fs_n; i++) std::fprintf(stderr, " %u", g_fs_tids[i]);
    std::fprintf(stderr, ")\n");
}
extern "C" uint32_t jsp_snapshot_spu_busy(void);   // jsp_snapshot.cpp: SPU jobs in flight (job chains, HLE queue, the job server's dispatched records)
// FIX (rollback determinism): SPU jobs (the sound library decodes on them) also finish on the PC's timing: the barrier waits for them too (two copies were
// still 2 ticks apart on the camera cut with the file threads idle at every boundary).
bool fs_idle_once()
{
    if (jsp_snapshot_spu_busy() != 0) return false;
    static uint32_t kind[65], id[65], obj[65], lr[65]; static uint8_t alive[65], unrel[65]; uint32_t ov = 0, ub = 0;
    ps3_hle_thread_census(kind, id, obj, lr, alive, unrel, &ov, &ub);
    for (int i = 0; i < g_fs_n; i++) { const uint32_t t = g_fs_tids[i]; if (alive[t] && !(kind[t] != 0 && lr[t] == 0x004D3D4Cu)) return false; }
    return true;
}
void fs_wait()
{
    const int64_t t0 = qpc_now();
    for (int spins = 0;; spins++) {
        if (fs_idle_once()) { YieldProcessor(); if (fs_idle_once()) break; }
        const double ms = qpc_ms(t0, qpc_now());
        if (ms > 500.0) { if (g_fs_timeouts++ < 5) std::fprintf(stderr, "[net] file threads still busy after 500 ms at tick %u\n", g.done_now); break; }
        if (spins > 20) { Sleep(0); if ((spins & 63) == 0) g.sess.poll(); }
    }
    const double ms = qpc_ms(t0, qpc_now()); g_fs_waits++; g_fs_wait_ms += ms; if (ms > g_fs_wait_max) g_fs_wait_max = ms;
}
void cri_cycle()   // boundary, before a tick runs (live or re-run): one server cycle at this tick's time
{
    if (!g_cri_on.load()) return;
    fs_wait();   // reads started in the tick before are done
    const uint32_t want = g_cri_go.load() + 1;
    g_cri_go.store(want); SetEvent(g_cri_ev);
    cri_wait(want, 250.0);
    g_cri_cycles++;
    fs_wait();   // reads the cycle started are done
}
void cri_lock_start()
{
    if (!cri_lock_on() || g.bench) return;
    if (!g_cri_ev) g_cri_ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_cri_go.store(0); g_cri_ack.store(0xFFFFFFFFu); g_cri_parked.store(0); g_cri_cycles = g_cri_timeouts = 0; g_cri_wait_ms = 0;
    g_ps3_usleep_hook = cri_usleep_hook;
    fs_find_threads(); g_fs_waits = g_fs_timeouts = 0; g_fs_wait_ms = g_fs_wait_max = 0;
    g_cri_on.store(true);
    cri_wait(0, 1500.0);   // its current sleep (at most 1 s) ends, and the next one is ours
    std::fprintf(stderr, "[net] sound server runs one cycle per game tick%s\n", g_cri_parked.load() ? "" : " (NOT parked yet: it was not seen sleeping within 1.5 s)");
}
void cri_lock_end()
{
    if (!g_cri_on.load()) return;
    g_cri_on.store(false); SetEvent(g_cri_ev);
    std::fprintf(stderr, "[net] sound server lock ended: %llu cycles, mean wait %.2f ms, %llu timeouts\n", (unsigned long long)g_cri_cycles, g_cri_cycles ? g_cri_wait_ms / g_cri_cycles : 0.0, (unsigned long long)g_cri_timeouts);
    std::fprintf(stderr, "[net] helper thread registers: saved at %llu ticks, written back %llu times, %llu not (not parked in the same wait, or no save)\n", (unsigned long long)g_regs_saved, (unsigned long long)g_regs_restored, (unsigned long long)g_regs_skipped);
    std::fprintf(stderr, "[net] waits for a tick at the boundary: %llu, %.0f ms in all, %llu render-only frames after 400 ms\n", (unsigned long long)g_holds, g_hold_ms, (unsigned long long)g_hold_renders);
    std::fprintf(stderr, "[net] file thread waits: %llu, mean %.3f ms, max %.1f ms, %llu timeouts\n", (unsigned long long)g_fs_waits, g_fs_waits ? g_fs_wait_ms / g_fs_waits : 0.0, g_fs_wait_max, (unsigned long long)g_fs_timeouts);
}
int64_t guest_tb_shift(uint64_t real)
{
    if (clock_diag()) note_clock_reader(GetCurrentThreadId());
    if (g_cri_on.load(std::memory_order_relaxed) && GetCurrentThreadId() == g_cri_tid.load(std::memory_order_relaxed))   // the sound server: the tick's time, exactly
        return (int64_t)(g_vc_t0 + (uint64_t)(g.done_now - g.base) * kTbPerTick - real);
    if (GetCurrentThreadId() != g_vc_tid.load(std::memory_order_relaxed)) return 0;
    const uint64_t vt = g_vc_t0 + (uint64_t)(g.done_now - g.base) * kTbPerTick + (uint64_t)(++g_vc_reads) * 80;
    g_vc_total_reads++;
    return (int64_t)(vt - real);
}
bool guest_clock_on() { static const bool v = [] { char b[8] = {}; return !(jsp_setting_get("JSP_NET_GUEST_CLOCK", b, sizeof b) && b[0] == '0'); }(); return v; }
void guest_clock_start()
{
    if (!guest_clock_on() || g.bench) return;
    g_vc_t0 = ppu_timebase_real_now(); g_vc_reads = 0; g_vc_total_reads = 0;
    g_vc_tid.store(GetCurrentThreadId());
    g_ps3_guest_tb_shift = guest_tb_shift;
    std::fprintf(stderr, "[net] game clock follows the game tick on the main thread (tid %lu)\n", (unsigned long)GetCurrentThreadId());
}
void guest_clock_end()
{
    if (!g_ps3_guest_tb_shift) return;
    g_ps3_guest_tb_shift = nullptr;
    print_clock_readers();
    g_vc_tid.store(0);
    std::fprintf(stderr, "[net] game clock back to the PC clock: %llu main-thread reads in the match\n", (unsigned long long)g_vc_total_reads);
}
// FIX (rollback determinism, JSP_NET_THREAD_REGS=0 turns it off): a rollback restores guest memory, not the registers of the helper threads. A thread parked
// in a wait loop keeps loop values in registers (the sound server's last time and counters): after a rollback it went on with the values of the undone
// timeline, so its timers came out differently with each PC's own rollback history (camera cut 2-8 ticks apart, the copies at 452 and 310 rollbacks). At
// every save the registers of each helper thread parked in an HLE wait are copied (patch 3006, ps3_ppu_thread_ctx); a rollback writes them back to every
// thread that is parked in the same wait (same call site) again. At the boundary those threads are held: the sound server in its lock, the audio thread
// waiting for blocks the mix thread only mixes when asked, the file threads idle.
extern "C" ppu_context* ps3_ppu_thread_ctx(uint32_t tid);
struct ThrRegs { uint32_t kind, lr; uint64_t gpr[32]; double fpr[32]; u128 vr[32]; uint32_t cr, xer, fpscr, vscr; uint64_t lrr, ctr; };
struct TickRegs { uint32_t tick = 0xFFFFFFFFu; uint64_t mask = 0; ThrRegs t[65]; };
constexpr int kRegRing = 16;
TickRegs* g_regs = nullptr;
bool thread_regs_on() { static const bool v = [] { char b[8] = {}; return !(jsp_setting_get("JSP_NET_THREAD_REGS", b, sizeof b) && b[0] == '0'); }(); return v; }
void regs_save(uint32_t tick)
{
    if (g.bench || !thread_regs_on()) return;
    if (!g_regs) g_regs = new TickRegs[kRegRing];
    TickRegs& R = g_regs[tick % kRegRing]; R.tick = tick; R.mask = 0;
    static uint32_t kind[65], id[65], obj[65], lr[65]; static uint8_t alive[65], unrel[65]; uint32_t ov = 0, ub = 0;
    ps3_hle_thread_census(kind, id, obj, lr, alive, unrel, &ov, &ub);
    for (uint32_t t = 1; t < 65; t++) {
        if (!jtb::has_thread(g_regs_allow, t) || !alive[t] || kind[t] == 0 || unrel[t]) continue;   // running guest code (or an unreliable marker): nothing safe to take
        ppu_context* c = ps3_ppu_thread_ctx(t); if (!c) continue;
        ThrRegs& r = R.t[t]; r.kind = kind[t]; r.lr = lr[t];
        std::memcpy(r.gpr, c->gpr, sizeof r.gpr); std::memcpy(r.fpr, c->fpr, sizeof r.fpr); std::memcpy(r.vr, c->vr, sizeof r.vr);
        r.cr = c->cr; r.xer = c->xer; r.fpscr = c->fpscr; r.vscr = c->vscr; r.lrr = c->lr; r.ctr = c->ctr;
        R.mask |= jtb::thread_bit(t);
    }
    g_regs_saved++;
}
void regs_restore(uint32_t tick)
{
    if (!g_regs || g.bench) return;
    TickRegs& R = g_regs[tick % kRegRing];
    if (R.tick != tick) { g_regs_skipped++; return; }
    static uint32_t kind[65], id[65], obj[65], lr[65]; static uint8_t alive[65], unrel[65]; uint32_t ov = 0, ub = 0;
    ps3_hle_thread_census(kind, id, obj, lr, alive, unrel, &ov, &ub);
    for (uint32_t t = 1; t < 65; t++) {
        if (!jtb::has_thread(R.mask, t)) continue;
        const ThrRegs& r = R.t[t];
        if (!alive[t] || kind[t] != r.kind || lr[t] != r.lr) { g_regs_skipped++; continue; }   // not parked in the same wait: leave it
        ppu_context* c = ps3_ppu_thread_ctx(t); if (!c) continue;
        std::memcpy(c->gpr, r.gpr, sizeof r.gpr); std::memcpy(c->fpr, r.fpr, sizeof r.fpr); std::memcpy(c->vr, r.vr, sizeof r.vr);
        c->cr = r.cr; c->xer = r.xer; c->fpscr = r.fpscr; c->vscr = r.vscr; c->lr = r.lrr; c->ctr = r.ctr;
        g_regs_restored++;
    }
}
// DIAGNOSTIC (JSP_NET_PAGE_DUMP=N, test runs only): every N session ticks of a CONFIRMED tick (a save of a final frame, hash_only), a fingerprint of every committed
// 4 KB guest page below 0x40000000 is appended to pagedump-<host|join>.bin in the run folder: u32 tick, u32 count, then count x (u32 page index, u64 hash).
// Comparing two copies' files (tools: build/bench/pagediff.py) names the first pages that stop matching, long before the checksum's parts show it.
void page_dump(uint32_t tick)
{
    static const uint32_t every = [] { char b[16] = {}; return jsp_setting_get("JSP_NET_PAGE_DUMP", b, sizeof b) ? (uint32_t)std::strtoul(b, nullptr, 10) : 0u; }();
    if (!every || g.bench || (tick - g.base) % every) return;
    static FILE* f = nullptr;
    if (!f) f = std::fopen(g.host ? "pagedump-host.bin" : "pagedump-join.bin", "wb");
    if (!f) return;
    std::vector<uint32_t> idx; std::vector<uint64_t> hs;
    for (uint64_t a = 0x10000; a < 0x40000000ull;) {
        MEMORY_BASIC_INFORMATION mi{};
        if (!VirtualQuery(vm_base + a, &mi, sizeof mi)) break;
        const uint64_t rb = (uint64_t)((uint8_t*)mi.BaseAddress - vm_base), re = rb + mi.RegionSize;
        if (mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
            for (uint64_t p = (rb < a ? a : rb) & ~0xFFFull; p < re && p < 0x40000000ull; p += 0x1000) {
                const uint64_t* w = (const uint64_t*)(vm_base + p); uint64_t h = 0x9E3779B97F4A7C15ull;
                for (int i = 0; i < 512; i++) h = (h ^ w[i]) * 0x100000001B3ull + (h >> 29);
                idx.push_back((uint32_t)(p >> 12)); hs.push_back(h);
            }
        }
        a = re;
    }
    const uint32_t hdr[2] = {tick - g.base, (uint32_t)idx.size()};
    std::fwrite(hdr, 4, 2, f);
    for (size_t i = 0; i < idx.size(); i++) { std::fwrite(&idx[i], 4, 1, f); std::fwrite(&hs[i], 8, 1, f); }
    std::fflush(f);
}   // FIX (hold at the boundary while no tick may run): totals for the end line
// ENHANCEMENT (testers): a match that ends on a desync or a failure writes one small text file to "reports" (JSP_NET_REPORT_DIR) that a tester can send back:
// build, settings in effect, session facts, the last 120 confirmed-tick checksums and rollbacks, the end reason. Addresses and the user's folders are removed.
jsnr::Report g_rep;
void report_start()
{
    g_rep.clear();
    char b[16]; std::snprintf(b, sizeof b, "%08x", build_id()); g_rep.set_build(b);
    for (const jpno::KV& e : jpno::kDefaults) { char v[128] = {}; if (jsp_setting_get(e.k, v, sizeof v)) g_rep.add_setting(e.k, v); }
    { char v[16] = {}; g_rep.add_setting("JSP_SHAREPAD_WATCHDOG", jsp_setting_get("JSP_SHAREPAD_WATCHDOG", v, sizeof v) && v[0] ? v : "1 (default)"); }   // FIX (rematch black screen)
    jsnr::SessionFacts f; f.local_slot = (int)g.plan.local_first; f.players = (int)g.plan.total_players; f.input_delay = g.bv.delay; f.base_tick = g.base;
    f.link = path_word(); g_rep.set_session(f);
    g_rep.add_machine_redactions();
}
void report_write(const std::string& why)
{
    if (g.bench || !g.started_ms) return;
    g_rep.set_end_reason(why);
    char dir[400] = {};
    if (!jsp_setting_get("JSP_NET_REPORT_DIR", dir, sizeof dir) || !dir[0]) std::snprintf(dir, sizeof dir, "reports");
    CreateDirectoryA(dir, nullptr);
    std::string err; const std::string p = g_rep.write(dir, jsnr::now_local(), &err);
    if (p.empty()) std::fprintf(stderr, "[net] match report not written: %s\n", err.c_str());
    else std::fprintf(stderr, "[net] match report written: %s\n", p.c_str());
}
void log(const char* what, const std::string& s) { std::fprintf(stderr, "[net] %s%s%s\n", what, s.empty() ? "" : ": ", s.c_str()); }
std::string take_text() { std::lock_guard<std::mutex> lk(g.txt_mu); return g.fail_text; }
void set_text(const std::string& t) { std::lock_guard<std::mutex> lk(g.txt_mu); g.fail_text = t; }
// ENHANCEMENT (replays, opt-in: JSP_ROLLBACK_RECORD=1 in play_settings.txt): the session records the confirmed inputs and the checksum of every confirmed tick
// (GekkoNet's replay format). The user data names what a playback must reproduce: "JSPR", format 1, the build word (version and gameplay mods), the base tick,
// the slots, then every agreed setting of the match manifest. Saved when the match ends to JSP_ROLLBACK_RECORD_DIR, else the folder "replays" under the game's
// working folder (next to play_stderr.txt): rollback-<local date>-<time>-<build>-<host|join>.gkrp. The fight start itself (characters, stage, seed) is not in it
// yet: a playback inside the game is a later step (docs/ROLLBACK.md, replays).
std::vector<uint8_t> replay_user_data(const jpsm::Values& vals)
{
    std::vector<uint8_t> b = {'J', 'S', 'P', 'R', 1};
    auto put32 = [&](uint32_t x) { for (int i = 0; i < 4; i++) b.push_back((uint8_t)(x >> (8 * i))); };
    put32(build_id()); put32(g.done_now);
    // players, first local slot, role: 0 join, 1 host, 2 the one-machine rollback bench (no match plan: one player, slot 0)
    if (g.bench) { b.push_back(1); b.push_back(0); b.push_back(2); }
    else { b.push_back(g.plan.total_players); b.push_back(g.plan.local_first); b.push_back(g.host ? 1 : 0); }
    put32((uint32_t)vals.v.size());
    for (uint32_t x : vals.v) put32(x);
    return b;
}
// final = the match ended (logged, never written again); otherwise a periodic copy (the rollback bench writes one every 10 s, so a run that is ended by
// closing the game keeps its replay). One file per match: the name is fixed at the first write.
std::string g_replay_path;
void save_replay(bool final = true)
{
    if (!g.record || !g.started_ms) return;
    if (final) g.record = false;   // once per match
    std::vector<uint8_t> bytes;
    if (!g.sess.recording(bytes)) { if (final) log("replay", "nothing recorded"); return; }
    if (g_replay_path.empty()) {
        char dir[260] = {};
        if (!jsp_setting_get("JSP_ROLLBACK_RECORD_DIR", dir, sizeof dir) || !dir[0]) std::snprintf(dir, sizeof dir, "replays");
        CreateDirectoryA(dir, nullptr);   // fails harmlessly when it exists
        SYSTEMTIME t; GetLocalTime(&t);
        char path[400];
        std::snprintf(path, sizeof path, "%s\\%s-%04u%02u%02u-%02u%02u%02u-%08x-%s.gkrp", dir, g.bench ? "bench" : "rollback", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                      build_id(), g.bench ? "local" : g.host ? "host" : "join");
        g_replay_path = path;
    }
    FILE* f = std::fopen(g_replay_path.c_str(), "wb");
    const bool ok = f && std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    if (f) std::fclose(f);
    if (final || !ok) log(ok ? "replay saved" : "replay NOT saved", g_replay_path + " (" + std::to_string(bytes.size()) + " bytes)");
    if (final) g_replay_path.clear();
}

// ENHANCEMENT (lobby): the per-match state back to what a fresh process has, so another session can start in this process (a second jsp_net_lobby_open, or a
// rematch later). Main thread; the audio / clock / sound-server locks are already ended (end_with runs their end functions first). The GekkoNet session itself
// is stopped separately (kLingerMs, boundary / conn_close / the next start), so the other PC can still confirm the last ticks.
void reset_match()
{
    g.glue.reset();   // holds references to sess and stepper; a new one is made at the next start
    g.base = 0; g.fight_seen = false; g.hud_mark = g.hud_tick = 0; g.hud_seen = 0; g.hud_seen_set = false;
    g.skip_sim = g.skip_pad = g.was_replaying = false; g.cancel_since = 0;
    g.last_run_ms = g.started_ms = 0; g.settings_shown_ms = 0;   // started_ms = 0: the presence no longer counts a match
    g.bv = jpnb::Values(); g.badge_every = jpnb::Every(); g.status_every = jpnb::Every(); g.ping_every = jpnb::Every();
    g.record = false; g.match_winner = 0; ko_clear();
    g_ad = jpdr::State();   // ENHANCEMENT (adaptive input delay): per match
    g_perf = jpperf::Match();   // ENHANCEMENT (match perf summary): per match
    g_audio_waits = g_audio_timeouts = 0; g_audio_wait_ms = 0;
    g_hold_ms = 0; g_holds = g_hold_renders = 0;
    g_regs_saved = g_regs_restored = g_regs_skipped = 0;
    if (g_regs) for (int i = 0; i < kRegRing; i++) { g_regs[i].tick = 0xFFFFFFFFu; g_regs[i].mask = 0; }   // tick numbers repeat in the next fight
    g_ko_track = 0;   // the offline game as before (set again by start_session)
    // ENHANCEMENT (native fight takeover): the game's own online fight code runs as before (arm(0) is a no-op when not armed). DECISION (changed by the native
    // fight fix): the disarm restores share pad +0x6D8 and +0x6DC to what they held when armed, when the share pad object is still the same one (a new object
    // is left alone). The fix chose this over calling 0x454EE8 from the main thread. Unverified in a game run.
    jsp_native_fight_arm(0);
    jsp_fight_hash_native_online(0);   // FIX (review of PR 248): offline fights (and the bench) keep the battle setup table in the checksum
    // The fight that ended is forgotten: the next fight anchors again from done = 0 (without this a second fight never anchors and done stays where it was),
    // so a re-armed session waits for the NEW fight's anchor (ts.done >= 1) and its first HUD draw (hud_seen above).
    jsp_fight_hash_reset_fight();
}

// The match is over (session failure, Cancel, disconnect, End match): the local settings come back (handshake end_match / match_ended), the pad hook goes, the fight runs on locally.
// ENHANCEMENT (lobby): the link (socket, rendezvous, handshake objects, pump thread) stays open and the per-match state is reset (reset_match), so a new
// session can start in this process; the GekkoNet session lingers kLingerMs (boundary stops it).
void end_with(const std::string& text, bool quiet = false)
{
    if (text != "Match ended" && text.rfind("Match over", 0) != 0) report_write(text);   // ENHANCEMENT (testers): a desync / failure / disconnect leaves a report to send (not a normal end)
    audio_lock_end();
    jsp_stepper_lockstep(0);
    cri_lock_end();
    guest_clock_end();
    save_replay();   // before the stepper is torn down; the session object still holds the recording after a failure
    if (g.glue) std::fprintf(stderr, "[net] saves %llu, of which without a snapshot (final frame) %llu\n", (unsigned long long)g.glue->commits(), (unsigned long long)g.glue->skipped());
    if (g.bench && g.started_ms) bench_line("total", g.bs_total, (double)(GetTickCount64() - g.started_ms) / 1000.0);
    jsp_snapshot_ext_set_idle_hook(nullptr);   // the gate must not call into a finished session
    jsp_snapshot_ext_guard_summary();
    if (g.started_ms) log_status();   // the session ran: its last status line
    perf_log();   // ENHANCEMENT (match perf summary): the five [perf] lines of this match
    badge(""); ping_hide();
    set_text(text); g.phase = Phase::Ended; g.skip_sim = g.skip_pad = false;
    if (g.was_replaying || g.stepper.replaying()) jsp_stepper_replay(0, g.done_now);   // an end during a replay: gate, clock and flag off
    g.was_replaying = false;
    jsp_stepper_abort();
    jsp_stepper_net_disable();
    jsp_snapshot_ext_session_end();   // FIX (same-link rematch, run native-rm1): the snapshot ring of this session goes; the next match baselines afresh at its tick 1
    if (jkr::normal_end(text)) jsp_native_fight_end_normal();   // FIX (rematch black screen): a normal end lets the share pad watchdog act on the exit
    jsp_native_fight_arm(0);   // ENHANCEMENT (native fight takeover): after the last tick (reset_match below does it again; a no-op when not armed)
    { std::lock_guard<std::mutex> lk(g.pump.mutex()); if (g.host && g.mh) g.mh->end_match(); else if (!g.host && g.mc) g.mc->match_ended(); }
    log("ended", text);
    if (quiet) set_text(""); else show(text);   // quiet: the rendezvous texts are on the main menu's notice (jsp_net_notice); the message line stays empty so it does not clear that layer
    g.ended_ms = GetTickCount64();   // ENHANCEMENT (lobby): the session is stopped kLingerMs from now (boundary)
    // ENHANCEMENT (same-link rematch): a match that ran ended inside its fight; a re-armed match waits until that fight is gone (boundary, fresh_gate)
    g.last_end_ok = text == "Match ended" || text.rfind("Match over", 0) == 0;
    if (g.started_ms) g.fresh_gate = true;
    reset_match();
}
// After a local failure the peer is told WHY by a handshake Cancel (Gekko's disconnect alone would show it only 'Opponent disconnected'). The peer waits a moment for it.
void send_failure_cancel()
{
    std::lock_guard<std::mutex> lk(g.pump.mutex());
    if (g.host && g.mh) g.mh->cancel(jpsm::Reason::SessionFailed); else if (!g.host && g.mc) g.mc->abort(jpsm::Reason::SessionFailed);
}
bool m2_cancelled_within(int ms, std::string* text)
{
    for (int waited = 0;; waited += 20) {
        { std::lock_guard<std::mutex> lk(g.pump.mutex());
          if (g.plan_ready) {
              const jpsm::State st = g.host ? g.mh->state() : g.mc->state();
              if (jpsm::terminal(st) && st != jpsm::State::Ended) { *text = std::string("match cancelled: ") + jpsm::explain(g.host ? g.mh->reason() : g.mc->reason()); return true; }
          } }
        if (waited >= ms) return false;
        Sleep(20);
    }
}
extern "C" int jsp_mods_copy_mismatch(void);   // jsp_mods.cpp: 1 = a gameplay archive copy opened after start differs from the planned copy
void pump_fail(const std::string& t);
// True (and the session ends with the start-up refusal's text) when the mod set no longer runs as listed. Pump thread only.
bool mods_refuse_now()
{
    const char* why = jmods::rollback_gate(jsp_mods_copy_mismatch() != 0);
    if (!why) return false;
    std::fprintf(stderr, "[net] rollback refused: %s: off\n", why);
    pump_fail(std::string("rollback refused: ") + why);
    return true;
}
void pump_fail(const std::string& t, bool quiet)
{
    log("settings layer / rendezvous ended", t);
    if (!quiet && g.rrz && !g.plan_ready) g_rdv.ended();   // ended before the games met by another path (a mods refusal, the startup-evidence wait): no "waiting" band is left behind
    { std::lock_guard<std::mutex> lk(g.txt_mu); if (!g.m2_ended.load()) { g.m2_text = t; g.m2_quiet = quiet; } }
    g.m2_ended = true;   // the main thread acts on it (boundary)
}
void pump_fail(const std::string& t) { pump_fail(t, false); }

uint64_t random_session_id()
{
    uint64_t r = 0;
    try { std::random_device rd; r = ((uint64_t)rd() << 32) | rd(); } catch (...) {}
    r ^= now_ms() * 0x9e3779b97f4a7c15ull;
    return r ? r : 1;
}

// ---- the pump thread: rendezvous, handshake objects, handshake driving (everything under the pump mutex) ----------------------------------------------------------
void pump_tick(uint64_t t)
{
    if (g.phase.load() == Phase::Off || g.m2_ended.load()) return;   // once ended nothing more is logged or driven (no 10 lines a second after a refusal)
    // FIX (review of PR 248, the freeze timeout's owner): the frame clock's failure (jsp_stepper_lockstep_failed: no flip for 60 s since the last flip or the
    // clock's start, or a vblank not taken for 60 s) was consumed only at the tick boundary, on the game thread a stalled guest never gives back, while the
    // session's keepalive thread went on answering the peer, so neither side ever ended. This thread stays responsive: it ends the session at once (the
    // local actor leaves, the peer sees the disconnect, the keepalive stops) and tells the peer why (handshake Cancel). The boundary, when it runs again
    // (the frame clock lets go of the guest kReleaseMs later, jsp_stall_rule.h), ends the match locally with the same reason. The flag is cleared by the
    // lockstep clock's start and stop, so only this match's failure counts; the abort is idempotent if the boundary got there first.
    if (g.phase.load() == Phase::Running && !g.bench && jsp_stepper_lockstep_failed() && !g.stall_ended.exchange(true)) {
        std::fprintf(stderr, "[net] the frame clock failed (the game took no flip or vblank for 60 s): the pump thread ends the session now\n");
        g.sess.abort("frame clock stalled");
        if (g.plan_ready) { if (g.host && g.mh) g.mh->cancel(jpsm::Reason::SessionFailed); else if (!g.host && g.mc) g.mc->abort(jpsm::Reason::SessionFailed); }
    }
    if (g.rp) g.rp->pump();   // re-psn: from the very first tick, BEFORE the startup-evidence wait (up to 240 s): the server drops a client that does not poll for 90 s (heartbeat inside psnr_poll); this thread is the only one that calls the psnr client
    if (!g.evidence_ready.load()) {   // the handshake belongs after boot: Startup-field evidence (renderer pipe state, installed render60 mode) must exist when a proposal is checked
        uint64_t& first = g.ev_first; uint64_t& next = g.ev_next;   // per connection (conn_close resets them)
        if (!first) first = t;
        if (!jpss::reader_startup().available) {
            if (t >= next) { next = t + 5000; std::fprintf(stderr, "[net] waiting for startup evidence (render60 mode=%d, pipe state=%d, %llu s)\n", render60_mode(), rsx_d3d12_pipe_effective_state(), (unsigned long long)((t - first) / 1000)); }
            // Cases where it can never arrive: another render60 mode is installed (no canonical choice), or the renderer's pipe witness stays unavailable past a generous bound.
            if (render60_mode() != 0) pump_fail(std::string(jpsm::explain(jpsm::Reason::NoStartupEvidence)) + " (render60 mode is on: rollback needs it off)");
            else if (t - first > 240000) pump_fail(std::string(jpsm::explain(jpsm::Reason::NoStartupEvidence)) + " (the renderer pipe state never became available)");
            return;
        }
        g.evidence_ready = true; log("startup evidence available", "starting the rendezvous");
    }
    g.hub->pump();   // channels keep retransmitting / acknowledging after the plan too
    {   // one line every 5 s: where a failed run stopped
        uint64_t& next = g.wait_next;
        if (t >= next) {
            next = t + (g.phase.load() == Phase::Running ? 30000 : 5000);   // dense before the session starts, sparse after
            std::fprintf(stderr, "[net] waiting: phase=%d plan=%d m2=%d m2_started=%d m2_ended=%d sent=%llu recv=%llu dropped=%llu unknown_tag=%llu send_fail=%llu (err %d) hub_ignored=%llu rz_rejected=%llu\n",
                         (int)g.phase.load(), (int)g.plan_ready, g.plan_ready ? (int)(g.host ? g.mh->state() : g.mc->state()) : -1, (int)g.m2_started.load(), (int)g.m2_ended.load(),
                         (unsigned long long)g.mux.sent, (unsigned long long)g.mux.received, (unsigned long long)g.mux.dropped, (unsigned long long)g.mux.unknown_tag,
                         (unsigned long long)g.mux.send_fail, g.mux.last_send_err, (unsigned long long)g.hub->ignored, (unsigned long long)rz_rejected());
        }
    }
    if (g.m2_ended.load()) return;
    {   // every state change of the handshake, both roles
        int& last = g.hs_last;
        const int cur = g.plan_ready ? (int)(g.host ? g.mh->state() : g.mc->state()) : -1;
        if (cur != last) { last = cur; std::fprintf(stderr, "[net] handshake state -> %d (%s)\n", cur, g.host ? "host" : "client"); }
    }
    if (!g.plan_ready) {
        // ENHANCEMENT (mods, D40): a gameplay archive copy that was first opened after start and differs (jsp_mods.cpp) means this process
        // no longer runs the set its build word names: no rendezvous and no Hello from then on. A session already past its Hello is not ended here.
        if (mods_refuse_now()) return;
        const int r = g.rz->poll();
        if (r < 0) {
            if (g.rrz && g.rrz->version_mismatch()) {   // ENHANCEMENT (version mismatch message): on the message line too (the Friendly Match room shows no main menu notice); then the menu notice
                std::fprintf(stderr, "[net] rendezvous failed: %s\n", g.rz->error().c_str());
                pump_fail(g.rrz->notice1(), false); g_rdv.failed(g.rrz->notice1(), g.rrz->notice2());
            }
            else if (g.rrz && !g.rrz->notice1().empty()) { g_rdv.failed(g.rrz->notice1(), g.rrz->notice2()); pump_fail("rendezvous failed: " + g.rz->error(), true); }   // on the menu notice
            else pump_fail("rendezvous failed: " + g.rz->error());
            return;
        }
        if (r == 0) return;
        if (g.rrz) g_rdv.met();   // met: the waiting notice goes
        g.plan = g.rz->plan();
        g.link->set_peer(g.host ? 1 : 0, g.plan.remotes[0].ep);
        if (mods_refuse_now()) return;   // the same check right before the Hello (begin / join send it)
        if (g.host) {
            g.mh.reset(new jpsm::Host(*g.link, g.ev, g.caps, 0));
            if (g.mh->begin(g.plan.session_id, 0b11, g.plan.session_id ^ 0x5eedull, t) != jpsm::Reason::None) { pump_fail("settings handshake: cannot begin"); return; }
        } else {
            g.mc.reset(new jpsm::Client(*g.link, g.ev, g.caps, 1));
            if (g.mc->join(g.plan.session_id, 0, t) != jpsm::Reason::None) { pump_fail("settings handshake: cannot join"); return; }
        }
        g.plan_ready = true;
        if (g.rrz && g.rrz->room_id()) g.presence_room = g.rrz->room_id();
        log("rendezvous", to_string(g.plan.remotes[0].ep) + (g.rrz ? std::string(" via re-psn, path ") + path_name(g.rrz->path()) + ", room " + std::to_string((unsigned long long)g.rrz->room_id()) : std::string()));
        if (g.rrz) g.path_line = g.rrz->path() == Path::Relay ? std::string("Rollback on: connected through the server") : std::string("Rollback on: ") + path_name(g.rrz->path()) + " connection";   // FIX (player wording): no "re-psn" jargon on screen
        g.path_kind = !g.rrz ? 1 : g.rrz->path() == Path::Direct ? 1 : g.rrz->path() == Path::Relay ? 2 : 0;   // direct IP is always direct
        return;
    }
    // The handshake keeps ticking for the whole match (keep-alives, Start retransmits, Cancel handling); after handover() it only stops its own silence checks.
    if (g.host) {
        g.mh->tick(t);
        const jpsm::State st = g.mh->state();
        if (st == jpsm::State::Open && (g.mh->hellos() & 0b10) && g.propose_tries < 20) {
            jpsm::Values v = g.ev.local_values();
            v.v[static_cast<size_t>(jpsm::Id::Rollback)] = 1;
            const jpsm::Reason r = g.mh->propose(v, t);
            g.propose_tries++;
            if (r != jpsm::Reason::None) {
                const jpsm::Id bad = g.mh->bad_id();   // Host::bad_id names the field of a capability / range refusal (ev.fail_id() is only for install failures)
                log("propose refused", std::string(jpsm::explain(r)) + " (field " + jpsm::rule(bad).name + ")");
                if (r != jpsm::Reason::NotReady || g.propose_tries >= 20) {   // only NotReady can change by waiting; every other refusal is deterministic: no retry
                    g.mh->cancel(r);   // the client gets a handshake Cancel with this reason and ends at once with its text
                    pump_fail(std::string("host settings refused: ") + jpsm::explain(r));
                }
            }
        }
        if (st == jpsm::State::Ready) g.mh->start(0);
        if (jpsm::terminal(st) && st != jpsm::State::Ended) pump_fail(std::string("match cancelled: ") + jpsm::explain(g.mh->reason()));
        if (st == jpsm::State::Started) g.m2_started = true;
        // ENHANCEMENT (same-link rematch): a re-armed host waiting for its client's Hello says which match it is at, so a client that is still behind follows
        // (jsp_net_match.h; the first match on a link sends nothing new)
        if (g.match_k > 0 && st == jpsm::State::Open && !(g.mh->hellos() & 0b10) && t >= g.beacon_next) {
            g.beacon_next = t + 1000;
            const std::vector<uint8_t> b = match_beacon(g.plan.session_id, g.match_k);
            g.link->send(1, b.data(), b.size());
        }
    } else {
        g.mc->tick(t);
        const jpsm::State st = g.mc->state();
        if (jpsm::terminal(st) && st != jpsm::State::Ended) pump_fail(std::string("match cancelled: ") + jpsm::explain(g.mc->reason()) + " (field " + jpsm::rule(g.mc->bad_id()).name + ")");
        if (st == jpsm::State::Started) g.m2_started = true;
    }
}

// ENHANCEMENT (same-link rematch): arms match k on the open link (the plan is known): new handshake objects under that match's session id (jsp_net_match.h) and
// Phase::Handshake; the old objects go (with any settings layer they still own). Main thread, never while a match runs (end_with first). false: no plan yet.
bool match_arm(uint32_t k)
{
    {
        std::lock_guard<std::mutex> lk(g.pump.mutex());
        if (!g.plan_ready) return false;
        g.mh.reset(); g.mc.reset();
        g.match_k = k; g.beacon_next = 0; g.propose_tries = 0; g.hs_last = -2;
        g.m2_started = false;
        { std::lock_guard<std::mutex> tl(g.txt_mu); g.m2_text.clear(); g.m2_quiet = false; }
        g.m2_ended = false;
        const uint64_t sid = match_session_id(g.plan.session_id, k), t = now_ms();
        bool ok;
        if (g.host) { g.mh.reset(new jpsm::Host(*g.link, g.ev, g.caps, 0)); ok = g.mh->begin(sid, 0b11, sid ^ 0x5eedull, t) == jpsm::Reason::None; }
        else { g.mc.reset(new jpsm::Client(*g.link, g.ev, g.caps, 1)); ok = g.mc->join(sid, 0, t) == jpsm::Reason::None; }
        if (!ok) pump_fail("settings handshake: cannot begin match " + std::to_string(k));
    }
    g.settings_shown_ms = 0;
    g.phase = Phase::Handshake;
    log("match armed", "match " + std::to_string(k) + " on the same link (" + (g.host ? "host" : "client") + ")");
    return true;
}

// ---- the game side of the glue ----------------------------------------------------------------------------------------------------------------------------
class GameGlue : public IGlueGame {
public:
    uint32_t done() override { return g.done_now; }
    bool commit(uint32_t tick, uint32_t* hash, uint32_t* token) override
    {
        const int64_t t0 = g.bench ? qpc_now() : 0;
        const int r = jsp_snapshot_ext_commit(tick, g.ctx, hash);
        if (g.bench) g.bs.save.add(qpc_ms(t0, qpc_now()));
        if (r == 0) { log("snapshot commit failed", std::to_string(tick)); return false; }
        if (r == 2) { log("snapshot layout changed", "older rollback targets are gone: ending the session"); return false; }
        const uint32_t raw = *hash;
        if (raw == 0) { log("no per-tick hash (the fight hash tap gave 0): the session cannot check state", std::to_string(tick)); return false; }   // raw 0 is never mixed or hidden
        if (jsp_snapshot_ext_rsx_write_hazard()) { log("RSX wrote into guest memory with a commit pending (JSP_NET_RSX_WRITE_GUARD=0 turns the guard off)", std::to_string(tick)); return false; }
        if (jsp_snapshot_ext_pool_evicted() != 0) { log("snapshot ring pool too short: an entry was evicted early", std::to_string(tick)); return false; }
        *hash = raw ^ ((tick - g.base) * 2654435761u);   // the session-relative tick number is part of the checksum (the base is each PC's own tick count at the session start)
        if (*hash == 0) *hash = 1;
        *token = ++serial_;
        regs_save(tick);
        if ((int)(tick - g.base) - 1 < g.sess.final_frame()) page_dump(tick);   // DIAGNOSTIC (JSP_NET_PAGE_DUMP): only a confirmed tick (its save frame is final)
        ko_record(tick);   // FIX (a missed KO): every save, predicted or not (ko_scan ends the match once the tick is confirmed)
        g_rep.commit(tick - g.base, *hash, raw);
        if (!g.bench && tick_log()) std::fprintf(stderr, "[net] commit tick=%u hash=%08x raw=%08x\n", tick, *hash, raw);   // a two-instance test compares the LAST commit line per tick across the two peers (the bench: no per-tick lines, they would be timed)
        return true;
    }
    // ENHANCEMENT (rollback latency, JSP_ROLLBACK_SKIP_FINAL): a save GekkoNet can never load again: the same checksum as commit(), no snapshot (no freeze, no copy).
    bool hash_only(uint32_t tick, uint32_t* hash) override
    {
        const uint32_t raw = jsp_snapshot_ext_hash(tick);
        if (raw == 0) return false;   // no hash for this tick: the full commit runs (and reports it)
        *hash = raw ^ ((tick - g.base) * 2654435761u);
        if (*hash == 0) *hash = 1;
        g_rep.commit(tick - g.base, *hash, raw);
        regs_save(tick);
        page_dump(tick);   // DIAGNOSTIC (JSP_NET_PAGE_DUMP): a confirmed tick
        ko_record(tick);
        if (!g.bench && tick_log()) std::fprintf(stderr, "[net] commit tick=%u hash=%08x raw=%08x final-no-snapshot\n", tick, *hash, raw);
        return true;
    }
    bool rollback(uint32_t tick) override
    {
        // A rollback to the tick just completed restores nothing the simulation did: it would only undo what background threads (the file loader, decoders)
        // wrote since that tick's save, while their host-side state (file offsets, signalled events) moves on, and a loading screen then never finishes
        // (the depth-1 bench stalled before every fight). Kept as it is, at no cost.
        if (tick == g.done_now) return true;
        const int64_t t0 = g.bench ? qpc_now() : 0;
        const bool ok = jsp_snapshot_ext_rollback(g.done_now, tick, g.ctx) == 1;
        if (g.bench) g.bs.restore.add(qpc_ms(t0, qpc_now()));
        if (!ok) { log("snapshot rollback failed", std::to_string(tick)); return false; }
        jpnb::note_rollback(g.bv, (int64_t)g.done_now - (int64_t)tick);
        adapt_note_rollback((int64_t)g.done_now - (int64_t)tick);   // ENHANCEMENT (adaptive input delay, rule 2): the depth trigger
        if (perf_on()) jpperf::on_rollback(g_perf, perf_now(), (int64_t)g.done_now - (int64_t)tick);   // ENHANCEMENT (match perf summary)
        g_rep.rollback(g.done_now - g.base, g.done_now - tick);
        // the audio goes back with the tick (the mix thread is idle here). FIX (audio rewind dropped blocks): the save of tick T is taken at the boundary BEFORE that boundary raises
        // the audio to blocks_at(T), so the snapshot holds the guest's audio at blocks_at(T - 1): rewinding to blocks_at(T) dropped 6-7 blocks per rollback
        // (voices ended later on the copy with more rollbacks). The boundary that follows mixes them again.
        if (g_ps3_audio_lock_target >= 0) ps3_audio_lock_rewind(tick > g.base ? audio_blocks_at(tick - 1) : 0);
        g_ko.drop_after(tick);   // FIX (a missed KO): those ticks run again; their re-saves record them anew
        regs_restore(tick);   // FIX (helper thread registers): back to what they held at that save   // the audio goes back with the tick (the mix thread is idle here)   // the ticks gone back and run again (status badge and status line)
        g.done_now = tick;
        if (!g.bench && tick_log()) std::fprintf(stderr, "[net] rollback to tick=%u (longest game-thread stretch without a session call since the last rollback: %u ms, keepalive polls so far: %llu)\n", tick, g.sess.take_max_gap_ms(), (unsigned long long)g.sess.keepalive_polls());
        return true;
    }
    void local_pad(uint8_t, uint8_t wire[8]) override
    {
        uint8_t canon[8];
        if (!jsp_stepper_local_pad(0, canon)) { jsi::Pad n; n.connected = true; const auto b = n.pack(); for (int i = 0; i < 8; i++) canon[i] = b[i]; }
        if (!g.feed.empty()) {   // replay-driven bench: the recorded fight's input of this update
            if (g.feed_i < g.feed.size()) { for (int i = 0; i < 8; i++) canon[i] = g.feed[g.feed_i][i]; g.feed_i++; }
            else { jsi::Pad n; n.connected = true; const auto b = n.pack(); for (int i = 0; i < 8; i++) canon[i] = b[i];
                   if (!g.feed_end_said) { g.feed_end_said = true; std::fprintf(stderr, "[net] rollback bench: the recorded inputs ended at update %zu\n", g.feed_i); } }
        }
        // ENHANCEMENT (rollback, A/B, off by default): JSP_NET_STICK_DEADZONE=N (0..64) sends a stick within N of centre as centred, and
        // JSP_NET_STICK_LEVELS=L (2..128) rounds each axis's deflection to L steps over the full range. The value sent is what both PCs simulate, so nothing has to
        // agree; it removes the mispredictions of stick jitter. Measured on a recorded fight (build/replays, replay_inputs.py): 954 input changes, 731 with
        // dead zone 24 and 4 levels, 782 with 8 levels; the dead zone alone 901. Not a default until a fight confirms the game ignores the dropped precision.
        { static const int dz = [] { char b[16] = {}; const int v = jsp_setting_get("JSP_NET_STICK_DEADZONE", b, sizeof b) ? std::atoi(b) : 0; return v < 0 ? 0 : v > 64 ? 64 : v; }();
          static const int lv = [] { char b[16] = {}; const int v = jsp_setting_get("JSP_NET_STICK_LEVELS", b, sizeof b) ? std::atoi(b) : 0; return v < 2 ? 0 : v > 128 ? 128 : v; }();
          for (int i = 2; i <= 5 && (dz || lv); i++) {
              int d = (int)canon[i] - 128;
              if (d > -dz && d < dz) d = 0;
              else if (lv) { const int step = 256 / lv; d = (d >= 0 ? d + step / 2 : d - step / 2) / step * step; }
              canon[i] = (uint8_t)(d + 128 < 0 ? 0 : d + 128 > 255 ? 255 : d + 128);
          } }
        // TEST (A/B, JSP_NET_LOAD_NEUTRAL=1): until the fight HUD is drawn, the input sent is the neutral pad (no rollback before the fight). Not usable as is:
        // the first pre-fight step (task count 0x0B) waits for a press; with neutral input a two-copy match stayed there 4,700 ticks (0 desyncs, 30 ticks/s).
        { static const bool ln = [] { char b[8] = {}; return jsp_setting_get("JSP_NET_LOAD_NEUTRAL", b, sizeof b) && b[0] == '1'; }();
          if (ln && !g.fight_seen && !g.bench) { jsi::Pad n; n.connected = true; const auto b = n.pack(); for (int i = 0; i < 8; i++) canon[i] = b[i]; } }
        pad_pack_to_wire(canon, wire);   // the device is physical port 0 on every machine; the table slot it feeds is the plan's local slot
        if (perf_on()) jpperf::on_local(g_perf, perf_now(), (uint16_t)(canon[0] | (canon[1] << 8)));   // ENHANCEMENT (match perf summary): the press sample
    }
    void advanced(int32_t frame, bool rolling_back, const jsi::Pad* in, unsigned players) override   // ENHANCEMENT (match perf summary): the press's frame
    {
        if (perf_on() && g.plan.local_first < players) jpperf::on_advance(g_perf, frame, rolling_back, in[g.plan.local_first].buttons);
    }
private:
    // ENHANCEMENT (online match end): the guest memory now holds a CONFIRMED tick's state (both PCs simulated it with the same inputs), so the KO counters read
    // here agree on both: the first confirmed tick with a winner ends the match (boundary, end_match_over). The counters are guest heap words and roll back
    // with the snapshot, so a predicted KO that a rollback undoes is never seen here. Time-up: the round result object (jsp_fight_end_state), also guest heap.
    // FIX (a missed KO): recorded on every save (the guest memory holds that tick's state, predicted or confirmed); ko_scan decides once the tick is final.
    static void ko_record(uint32_t tick)
    {
        // not before this session saw the fight HUD drawn: jsp_game.cpp finds the KO counters at that draw, and until then the addresses may still name the
        // previous fight's (freed) objects. ASSUMPTION: the HUD draw re-finds them for every fight.
        if (g.bench || g.match_winner || !g.fight_seen) return;
        int w = jsp_fight_ko_winner();
        if (w <= 0) { const int e = jsp_fight_end_state(); if (e > 0) w = e; }   // 1 / 2: the game's judge named the winner; 3: time up, winner not written yet
        g_ko.record(tick, w);
    }
    uint32_t serial_ = 100;
} g_game_glue;
// FIX (a missed KO): the earliest recorded winner whose tick is confirmed (its save frame is below GekkoNet's final frame, the test confirmed() used) ends the
// match: both PCs confirm the same ticks with the same inputs, so they end on the same tick whichever of them saved it while predicted. Main thread (boundary).
void ko_scan()
{
    if (g.bench || g.match_winner) return;
    const int ff = g.sess.final_frame();
    if (ff < 0) return;
    uint32_t best = 0;
    const int bw = g_ko.earliest_covered(g.base, ff, &best);
    if (!bw) return;
    g.match_winner = bw;
    std::fprintf(stderr, "[net] match over at confirmed tick %u (session-relative): %s\n", best - g.base, bw == 1 ? "red wins" : bw == 2 ? "blue wins" : "time up");
}
// FIX (review of PR 248): the result a disconnect may keep, only one covered by the live confirmation watermark (jkr::disconnect_result). A predicted
// KO / time-up above it ends the match as a disconnect, as a disconnect without any recorded result. Main thread (boundary, the Disconnected event).
int ko_after_disconnect()
{
    g_ko_live.lost();
    uint32_t at = 0, any_at = 0;
    const int w = jkr::disconnect_result(g_ko, g.base, g_ko_live, &at);
    const int any = g_ko.earliest_any(&any_at);
    if (w) std::fprintf(stderr, "[net] the opponent left after a confirmed result at tick %u (session-relative; live confirmed up to frame %d)\n", at - g.base, g_ko_live.ff);
    else if (any) std::fprintf(stderr, "[net] the opponent left; %d predicted result(s) from tick %u (session-relative) are above the live confirmed frame %d: ignored, a disconnect\n",
                               g_ko.uncovered(g.base, g_ko_live.ff), any_at - g.base, g_ko_live.ff);
    return w;
}
std::string match_over_text(int w) { return jkr::match_over_text(w); }

// WORKAROUND TO REPLACE (a bound, not a cause): the commit gate waits for the frame pipeline threads to reach their park points; a slow frame (a cold pipeline build took 4.4 s on a client and
// 14 s on a host in two-copy tests) keeps them away. The session network is polled while the gate waits (idle hook); every other long stretch on the game thread (a replayed
// frame, flips, vblank acks, a pipeline build before the boundary) is NOT covered by the hook: NetSession's keepalive thread polls whenever the game thread has not (GekkoNet's Poll
// sends the 250-500 ms health ping the peer's disconnect timer counts). This constant is only the point where a gate that never clears is declared hung. Warm pipeline caches shorten the frames.
constexpr unsigned kGateBudgetMs = 15000;

// ENHANCEMENT (replay-driven tests): JSP_ROLLBACK_INPUTS=<a replay_dump csv> feeds this copy's local player from a recorded fight, one row per session update
// (JSP_ROLLBACK_INPUTS_SKIP=N starts N rows in, so two copies fed from one recording do not press alike). Bench and two-copy test runs only; the pad is not read.
// DIAGNOSTIC (JSP_NET_TASK_WATCH=1, local test only): a hardware write watchpoint (DR0) on the game's task count 0x0094DEC4, set on every thread of the
// process when a session starts. Each write logs the writing thread, the new count, the tick and the host call stack as jsp+0xRVA frames (the recompiled
// functions func_XXXXXXXX, symbolized afterwards with llvm-nm), i.e. the guest call chain that added or removed a task.
} // namespace
#include <tlhelp32.h>
extern uint8_t* vm_base;
namespace {
std::atomic<uint32_t> g_wp_tick{0};
struct WpHit { uint32_t tid, tick, count, nf; uint64_t fr[24]; };
uint32_t g_wp_addr[4] = {0, 0, 0, 0}; uint64_t g_wp_last[4] = {0, 0, 0, 0};
WpHit g_wp_ring[256]; std::atomic<uint32_t> g_wp_w{0}; uint32_t g_wp_r = 0;
// No formatting, lock or allocation here: a rollback writes the word while the other threads are frozen (one may hold the stdio lock); printed by task_watch_flush.
LONG CALLBACK task_watch_veh(EXCEPTION_POINTERS* ep)
{
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(ep->ContextRecord->Dr6 & 0xF)) return EXCEPTION_CONTINUE_SEARCH;
    const int which = (ep->ContextRecord->Dr6 & 1) ? 0 : (ep->ContextRecord->Dr6 & 2) ? 1 : (ep->ContextRecord->Dr6 & 4) ? 2 : 3;
    ep->ContextRecord->Dr6 = 0;
    if (g.stepper.replaying()) return EXCEPTION_CONTINUE_EXECUTION;   // restores and re-runs: not the question
    if (g_wp_addr[which]) {   // camera watch: only a change of the watched 8 bytes, and at most 48 records
        const uint64_t v = vm_read64(g_wp_addr[which]);
        if (v == g_wp_last[which]) return EXCEPTION_CONTINUE_EXECUTION;
        g_wp_last[which] = v;
        if (g_wp_w.load() >= 48) return EXCEPTION_CONTINUE_EXECUTION;
    }
    const uint32_t i = g_wp_w.fetch_add(1);
    if (i - g_wp_r >= 256) return EXCEPTION_CONTINUE_EXECUTION;
    WpHit& h = g_wp_ring[i & 255];
    const uint64_t base = (uint64_t)GetModuleHandleA(nullptr);
    h.tid = GetCurrentThreadId(); h.tick = g_wp_tick.load(); h.count = (uint32_t)which; h.nf = 0;
    CONTEXT cx = *ep->ContextRecord;   // walk the WRITER's stack (x64 unwind data), not the handler's
    for (int f = 0; f < 24 && cx.Rip; f++) {
        h.fr[h.nf++] = cx.Rip - base;
        DWORD64 ib = 0; PRUNTIME_FUNCTION rf = RtlLookupFunctionEntry(cx.Rip, &ib, nullptr);
        if (!rf) { cx.Rip = *(DWORD64*)cx.Rsp; cx.Rsp += 8; continue; }   // leaf function
        void* hd = nullptr; DWORD64 est = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, ib, cx.Rip, rf, &cx, &hd, &est, nullptr);
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}
void task_watch_flush()   // boundary, main thread, nothing frozen
{
    const uint32_t w = g_wp_w.load();
    for (; g_wp_r != w; g_wp_r++) {
        const WpHit& h = g_wp_ring[g_wp_r & 255];
        char line[700]; int n = std::snprintf(line, sizeof line, "[taskwp] thread=%lu main=%d tick=%u dr=%u stack", (unsigned long)h.tid, h.tid == GetCurrentThreadId() ? 1 : 0, h.tick, h.count);
        for (uint32_t f = 0; f < h.nf && n < (int)sizeof line - 24; f++) if (h.fr[f] < 0x10000000ull) n += std::snprintf(line + n, sizeof line - n, " jsp+0x%llx", (unsigned long long)h.fr[f]);
        std::fprintf(stderr, "%s%c", line, 10);
    }
}
DWORD WINAPI task_watch_arm(void*)
{
    const DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te; te.dwSize = sizeof te; int armed = 0;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
        HANDLE h = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!h) continue;
        if (SuspendThread(h) != (DWORD)-1) {
            CONTEXT c = {}; c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(h, &c)) {
                if (g_wp_addr[0]) {   // camera watch: DR0 = cam+0x10, DR1 = cam+0xF0, 8 bytes each, write
                    c.Dr0 = (DWORD64)(vm_base + g_wp_addr[0]); c.Dr1 = (DWORD64)(vm_base + g_wp_addr[1]);
                    c.Dr7 = (c.Dr7 & ~0xFF000Full) | 1ull | 4ull | (1ull << 16) | (2ull << 18) | (1ull << 20) | (2ull << 22);
                } else {
                    c.Dr0 = (DWORD64)(vm_base + 0x0094DEC4u);
                    c.Dr7 = (c.Dr7 & ~0xF0003ull) | 1ull | (1ull << 16) | (3ull << 18);   // L0, write, 4 bytes
                }
                if (SetThreadContext(h, &c)) armed++;
            }
            ResumeThread(h);
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
    std::fprintf(stderr, "[taskwp] watchpoint on 0x0094DEC4 armed on %d threads\n", armed);
    return 0;
}
extern "C" uint32_t jsp_fight_hash_cam_addr(int i);
// DIAGNOSTIC (JSP_NET_CAM_WATCH=1): 60 ticks after the fight HUD, watch camera 0 (+0x10 and +0xF0) and log the first changes with the writer's stack.
void cam_watch_maybe(uint32_t hud_tick)
{
    static bool done = false;
    char b[8] = {};
    if (done || !(jsp_setting_get("JSP_NET_CAM_WATCH", b, sizeof b) && b[0] == '1') || g.done_now != hud_tick + 60) return;
    const uint32_t c = jsp_fight_hash_cam_addr(0);
    if (!c) return;
    done = true;
    g_wp_addr[0] = c + 0x10; g_wp_addr[1] = c + 0xF0; g_wp_last[0] = vm_read64(c + 0x10); g_wp_last[1] = vm_read64(c + 0xF0);
    AddVectoredExceptionHandler(1, task_watch_veh);
    HANDLE t = CreateThread(nullptr, 0, task_watch_arm, nullptr, 0, nullptr);
    if (t) { WaitForSingleObject(t, 5000); CloseHandle(t); }
    std::fprintf(stderr, "[taskwp] camera watch armed at tick %u on %08X\n", g.done_now, c);
}
void task_watch_start()
{
    char b[8] = {};
    if (!(jsp_setting_get("JSP_NET_TASK_WATCH", b, sizeof b) && b[0] == '1')) return;
    AddVectoredExceptionHandler(1, task_watch_veh);
    HANDLE t = CreateThread(nullptr, 0, task_watch_arm, nullptr, 0, nullptr);
    if (t) { WaitForSingleObject(t, 5000); CloseHandle(t); }
}
// TEST (A/B): JSP_ROLLBACK_START=fight starts a match session at the first fight HUD draw instead of the camera anchor (see boundary()). Not the default: the
// tick count spent loading then differs between the PCs, and per-tick state the checksum does not cover drifts (the CPU opponent acted 1 tick apart, two-copy test).
extern "C" volatile uint32_t g_hud_calls;   // jsp_game.cpp: calls of the fight HUD draw
bool start_at_anchor() { static const bool v = [] { char b[16] = {}; return !(jsp_setting_get("JSP_ROLLBACK_START", b, sizeof b) && !std::strcmp(b, "fight")); }(); return v; }
void load_feed()
{
    char fp[400] = {}, sk[16] = {};
    if (!jsp_setting_get("JSP_ROLLBACK_INPUTS", fp, sizeof fp) || !fp[0] || !g.feed.empty()) return;
    const unsigned skip = jsp_setting_get("JSP_ROLLBACK_INPUTS_SKIP", sk, sizeof sk) ? (unsigned)std::strtoul(sk, nullptr, 10) : 0u;
    unsigned row = 0;
    if (FILE* fi = std::fopen(fp, "rb")) {
        char line[256];
        while (std::fgets(line, sizeof line, fi)) {
            if (line[0] < '0' || line[0] > '9') continue;   // "# meta" and the header
            unsigned t = 0, bt = 0, lx = 128, ly = 128, rx = 128, ry = 128, cn = 1;
            if (std::sscanf(line, "%u,%u,%u,%u,%u,%u,%u", &t, &bt, &lx, &ly, &rx, &ry, &cn) < 6) continue;
            if (row++ < skip) continue;
            jsi::Pad pd; pd.buttons = (uint16_t)bt; pd.lx = (uint8_t)lx; pd.ly = (uint8_t)ly; pd.rx = (uint8_t)rx; pd.ry = (uint8_t)ry; pd.connected = cn != 0;
            g.feed.push_back(pd.pack());
        }
        std::fclose(fi);
    }
    std::fprintf(stderr, "[net] TEST: %zu recorded input ticks from %s drive the local player (skipped %u)%s\n", g.feed.size(), fp, skip,
                 g.feed.empty() ? " (NONE: the file is missing or empty; the pad is used)" : "");
}
void start_session(ppu_context* c)
{
    load_feed();   // TEST: a recorded fight drives the local player (JSP_ROLLBACK_INPUTS; nothing without it)
    (void)c;
    jpsm::Values vals;
    { std::lock_guard<std::mutex> lk(g.pump.mutex()); vals = g.host ? g.mh->manifest().values : g.mc->manifest().values; }
    g.base = g.done_now;
    jsp_snapshot_ext_set_budget_ms(kGateBudgetMs);   // gate time between two session updates; the network is polled while the gate waits, so the grace window is not the limit
    jsp_snapshot_ext_set_idle_hook([] { g.sess.poll(); });
    SessionConfig cfg;
    cfg.max_prediction = (uint8_t)kMaxPrediction; cfg.ring_depth = kRingDepth;
    cfg.local_delay.assign(g.plan.local_count, (uint8_t)vals.v[static_cast<size_t>(jpsm::Id::InputDelay)]);          // from the manifest: both peers agree
    g.bv.delay = (int)vals.v[static_cast<size_t>(jpsm::Id::InputDelay)];   // status badge: the match's input delay (the same on both PCs)
    cfg.disconnect_timeout_ms = vals.v[static_cast<size_t>(jpsm::Id::GraceSeconds)] * 1000u;                          // the grace window = the disconnect timeout
    { char ls[8] = {}; cfg.limited_saving = jsp_setting_get("JSP_ROLLBACK_LIMITED_SAVING", ls, sizeof ls) && ls[0] == '1';   // TEST (A/B, both PCs alike): GekkoNet sparse saving (SessionConfig::limited_saving)
      if (cfg.limited_saving) std::fprintf(stderr, "[net] TEST: sparse saving on (JSP_ROLLBACK_LIMITED_SAVING=1)\n"); }
    { char rb[16] = {}; g.record = jsp_setting_get("JSP_ROLLBACK_RECORD", rb, sizeof rb) && rb[0] && rb[0] != '0'; }   // ENHANCEMENT (replays): see save_replay
    if (g.record) { cfg.record = true; cfg.record_user_data = replay_user_data(vals); }
    std::string why;
    g.sess.stop(); g.ended_ms = 0;   // ENHANCEMENT (lobby): a previous match's lingering session goes (no-op for the first match)
    // ENHANCEMENT (native fight takeover, jsp_net_native_fight.h): in the game's OWN online fight the rollback session replaces the game's fight netcode.
    // The battle slot the game gave this PC (host 0, join 1) must be the rollback slot (plan.local_first), or each PC would drive the other's fighter.
    // Not adjusted here: the two plans must agree, so a mismatch refuses the session on this side (and the handshake Cancel tells the other one).
    const bool native_online = jsp_native_fight_online() == 1;
    if (native_online) {
        const int slot = jsp_native_fight_slot();
        if (slot != (int)g.plan.local_first)
            why = "the game's battle slot (" + std::to_string(slot) + ") is not the rollback slot (" + std::to_string((int)g.plan.local_first) + "): the native room owner must host";
        // FIX (swapped controls, testers on 0.7.0-test1): the builder numbers the fighters' net slots in team placement order, not record order, so each
        // net slot is mapped to its owner's rollback slot (room record) for the pad take, and this PC's own fighter must come out on this PC's slot.
        if (why.empty()) {
            char om[600] = {};
            // FIX (review of PR 248): the room's humans and the fighters must be exactly the plan's rollback seats (no missing, extra or stale member)
            const int ok = jsp_native_fight_map((int)g.plan.local_first, (1u << g.plan.total_players) - 1u, om, sizeof om);
            std::fprintf(stderr, "[net] ownership: %s\n", om);
            if (!ok) why = "this PC's fighter is not the one its pad would drive (the game's fighter slots do not match the rollback slots)";
        }
    }
    if (why.empty()) {
        if (!jsp_snapshot_ext_active()) why = "the snapshot ring is not active (JSP_SNAPSHOT_EXT/WW did not start)";
        else if (!g.sess.start(g.mux, g.plan, cfg)) why = "session refused: " + g.sess.error();
    }
    if (!why.empty()) {   // a refusal becomes a handshake Cancel with a reason, so the other side learns why (and the layer is removed on both)
        { std::lock_guard<std::mutex> lk(g.pump.mutex()); if (g.host) g.mh->cancel(jpsm::Reason::SessionRefused); else g.mc->abort(jpsm::Reason::SessionRefused); }
        end_with(std::string("Rollback session could not start: ") + why);   // (the handshake Cancel above already told the other side)
        return;
    }
    // TEST ONLY (two copies on one PC): JSP_NET_LINK="delay_ms,jitter_ms,loss_permille" impairs every datagram this copy sends from here on (the plan
    // exchange before it is not impaired), so a loopback match sees a real link's latency. Unset: nothing changes. Logged, so a run says it was impaired.
    if (const char* le = std::getenv("JSP_NET_LINK"); le && *le) {
        LinkModel lm; unsigned d = 0, j = 0, l = 0;
        std::sscanf(le, "%u,%u,%u", &d, &j, &l);
        lm.delay_ms = d; lm.jitter_ms = j; lm.loss_permille = l > 1000 ? 1000 : l; lm.seed = g.host ? 11 : 12;
        g.mux.set_link_model(lm);
        std::fprintf(stderr, "[net] TEST link impairment on this copy's sends: delay %u ms + jitter 0..%u ms, loss %u per mille\n", d, j, lm.loss_permille);
    }
    if (native_online) {   // ENHANCEMENT (native fight takeover): before the stepper and the session's first save (arm writes guest words that must be in it)
        if (!jsp_native_fight_arm(1)) {   // no share pad object: the game's own netcode would still run under the rollback
            g.sess.abort("the game's online fight could not be taken over");
            { std::lock_guard<std::mutex> lk(g.pump.mutex()); if (g.host) g.mh->cancel(jpsm::Reason::SessionRefused); else g.mc->abort(jpsm::Reason::SessionRefused); }
            end_with("Rollback session could not start: the game's online fight could not be taken over (no share pad)");
            return;
        }
        std::fprintf(stderr, "[net] native fight taken over: the game's online fight runs on the rollback session (battle slot %d = rollback slot %u)\n", jsp_native_fight_slot(), (unsigned)g.plan.local_first);
    }
    // FIX (review of PR 248): the battle setup table 0x009540E4 is Free Battle's; a native online fight never reads it (jsp_fight_hash.cpp says where that
    // was checked), so it leaves this session's checksum before the first save instead of being cleared. Both PCs run the same mode (the slot and map checks).
    jsp_fight_hash_native_online(native_online ? 1 : 0);
    g.stepper.begin(g.done_now, kRingDepth);
    g.stepper.table().set_mask((1u << g.plan.total_players) - 1);
    jsp_stepper_net_enable(&g.stepper.table());
    g.glue.reset(new Glue(g.sess, g.stepper, g_game_glue));
    {   // ENHANCEMENT (rollback latency, JSP_ROLLBACK_SKIP_FINAL=1): saves GekkoNet can never load again take no snapshot (jsp_net_glue.h); each PC decides alone
        char sf[8] = {}; const bool on = jsp_setting_get("JSP_ROLLBACK_SKIP_FINAL", sf, sizeof sf) && sf[0] == '1';
        g.glue->set_skip_final(on);
        if (on) std::fprintf(stderr, "[net] saves of final frames take no snapshot (JSP_ROLLBACK_SKIP_FINAL=1)\n");
    }
    { std::lock_guard<std::mutex> lk(g.pump.mutex()); if (g.host) g.mh->handover(); else g.mc->handover(); }   // the rollback session owns liveness from here
    g.fight_seen = false; g.hud_mark = 0;
    g_ko_track = 1;   // ENHANCEMENT (online match end): the KO counters are found even with the default KO count (jsp_fight_ko_winner, GameGlue::confirmed)
    g.stall_ended = false;   // FIX (review of PR 248): the pump's frame clock watchdog, per match
    g.phase = Phase::Running; g.last_run_ms = GetTickCount64(); g.started_ms = g.last_run_ms;
    log("session started", std::string(start_at_anchor() ? "at the camera anchor, " : "at the fight scene, ") + "base tick " + std::to_string(g.done_now) + " slots " + std::to_string(g.plan.local_first) + " of " + std::to_string(g.plan.total_players));
    task_watch_start();   // DIAGNOSTIC (JSP_NET_TASK_WATCH=1)
    report_start();
    audio_lock_start();
    jsp_stepper_lockstep(1);   // FIX (rollback determinism): 2 vblanks per flip in the live ticks too (jsp_stepper.cpp)
    guest_clock_start();
    cri_lock_start();
    show("Connecting to opponent...");
}

// ENHANCEMENT (rollback bench): the first fight tick starts a local stress session instead of a peer session. One local player (slot 0, physical port 0) is fed through
// the table, so the opponent must be the CPU: a second human pad would be read live on a re-run and give a different tick. Same stepper, glue and snapshot ring as a match.
void start_bench()
{
    g.base = g.done_now;
    jsp_snapshot_ext_set_budget_ms(kGateBudgetMs);
    SessionConfig cfg;
    cfg.max_prediction = (uint8_t)kMaxPrediction; cfg.ring_depth = kRingDepth;
    // ENHANCEMENT (replays): JSP_ROLLBACK_RECORD=1 records the bench fight's inputs (no checksums in a stress session), written every 10 s and at the end
    { char rb[16] = {}; g.record = jsp_setting_get("JSP_ROLLBACK_RECORD", rb, sizeof rb) && rb[0] && rb[0] != '0'; }
    if (g.record) { cfg.record = true; cfg.record_copy_every = 30; cfg.record_user_data = replay_user_data(jpsm::defaults()); }
    std::string why;
    g.sess.stop(); g.ended_ms = 0;   // ENHANCEMENT (lobby): no-op for the first run
    if (!jsp_snapshot_ext_active()) why = "the snapshot ring is not active (JSP_SNAPSHOT_EXT/WW did not start)";
    else if (!g.sess.start_stress(1, (uint32_t)g.bench, cfg)) why = "session refused: " + g.sess.error();
    if (!why.empty()) { end_with("Rollback bench could not start: " + why); return; }
    g.plan = g.sess.plan();
    g.stepper.begin(g.done_now, kRingDepth);
    g.stepper.table().set_mask(1u);
    jsp_stepper_net_enable(&g.stepper.table());
    g.glue.reset(new Glue(g.sess, g.stepper, g_game_glue));
    g.phase = Phase::Running; g.last_run_ms = GetTickCount64(); g.started_ms = g.last_run_ms;
    g.bs = State::Bench(); g.bs_total = State::Bench(); g.bs.win_ms = g.started_ms;
    log("bench started", "rolling back " + std::to_string(g.bench) + " ticks on every tick from base tick " + std::to_string(g.done_now) + " (F9 held: end)");
    show("Rollback bench: " + std::to_string(g.bench) + " ticks re-run every tick");
}
// Called at the top of every Running boundary: the time since the previous boundary is the cost of the iteration that just ended, filed by what it ran.
void bench_iteration()
{
    const int64_t now = qpc_now();
    if (g.bs.last_qpc && g.bs.last_kind >= 0) {
        const double ms = qpc_ms(g.bs.last_qpc, now);
        (g.bs.last_kind == 1 ? g.bs.live : g.bs.last_kind == 2 ? g.bs.rerun : g.bs.idle).add(ms);
    }
    g.bs.last_qpc = now;
    const uint64_t t = GetTickCount64();
    if (t - g.bs.win_ms >= 10000) {
        bench_line("window", g.bs, (double)(t - g.bs.win_ms) / 1000.0);
        bench_phase_line(g.bs);
        save_replay(false);   // ENHANCEMENT (replays): the bench is usually ended by closing the game, so keep the file current
        bench_merge(g.bs_total.live, g.bs.live); bench_merge(g.bs_total.rerun, g.bs.rerun); bench_merge(g.bs_total.idle, g.bs.idle);
        bench_merge(g.bs_total.save, g.bs.save); bench_merge(g.bs_total.restore, g.bs.restore);
        g.bs_total.desyncs += g.bs.desyncs; if (g.bs_total.first_desync < 0) g.bs_total.first_desync = g.bs.first_desync;
        const int64_t q = g.bs.last_qpc; const int k = g.bs.last_kind;
        g.bs = State::Bench(); g.bs.win_ms = t; g.bs.last_qpc = q; g.bs.last_kind = k;
    }
}

// The cancel / end-match key is F9 (Esc quits the game: patch 2500). Held for 1.5 s while a match runs; a press while waiting.
bool window_focused() { DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid); return pid == GetCurrentProcessId(); }
bool cancel_key() { return window_focused() && (GetAsyncKeyState(VK_F9) & 0x8000) != 0; }
void ui_and_cancel()
{
    const Phase ph = g.phase;
    if (ph == Phase::Handshake || ph == Phase::Armed) {
        // FIX (tester video: an F9 tap at fight start cancelled rollback): F9 must be held 1.5 s here too, as in a running match (jpno::cancel_held)
        const bool held = jpno::cancel_held(cancel_key(), GetTickCount64(), g.cancel_since);
        std::lock_guard<std::mutex> lk(g.pump.mutex());
        if (g.host) {
            if (held && g.plan_ready) { std::fprintf(stderr, "[net] F9 held 1.5 s: the player cancelled rollback (host)\n"); g.mh->cancel(); g.phase = Phase::Ended; set_text("Rollback cancelled (F9 held): local settings restored"); show(take_text()); return; }
            if (ph == Phase::Handshake && g.plan_ready) { show("Waiting for client... (hold F9 to cancel)"); return; }   // only until Start
        } else if (g.plan_ready) {
            const auto hw = g.mc->host_wait(now_ms());
            const jpsm::State st = g.mc->state();
            if (held) { std::fprintf(stderr, "[net] F9 held 1.5 s: the player left the rollback session (client)\n"); g.mc->leave(); g.phase = Phase::Ended; set_text("Left the rollback session (F9 held): local settings restored"); show(take_text()); return; }
            if (g.mc->applied() && g.ev.active()) {
                if (!g.settings_shown_ms) g.settings_shown_ms = GetTickCount64();
                if (GetTickCount64() - g.settings_shown_ms < 6000) { show("Settings received: controlled by host"); return; }
            }
            if (hw.waiting && st != jpsm::State::Started) { show("Waiting for host... (" + std::to_string(hw.waited_ms / 1000) + " s, hold F9 to cancel)"); return; }
        }
        if (ph == Phase::Armed) show("");
    }
    if (ph == Phase::Ended) { const std::string t = take_text(); if (!t.empty()) show(t); }
}

// DIAGNOSTIC (loading, JSP_NET_LOAD_DIAG=1): while the fight loads, one line per boundary: the tick, the game's task count and the HLE marker of threads 10-17
// (the CRI servers and its polling thread: kind 0 = running guest code, else the HLE call it is inside and the call site). Racy reads, log only.
extern "C" void ps3_hle_thread_census(uint32_t* kind, uint32_t* id, uint32_t* obj, uint32_t* lr, uint8_t* alive, uint8_t* unreliable, uint32_t* overflows, uint32_t* unbalanced);
void load_diag()
{
    static const bool on = [] { char b[8] = {}; return jsp_setting_get("JSP_NET_LOAD_DIAG", b, sizeof b) && b[0] == '1'; }();
    if (!on) return;
    static uint32_t kind[65], id[65], obj[65], lr[65]; static uint8_t alive[65], unrel[65]; uint32_t ov = 0, ub = 0;
    ps3_hle_thread_census(kind, id, obj, lr, alive, unrel, &ov, &ub);
    char line[600]; int n = std::snprintf(line, sizeof line, "[load] tick=%u tasks=%02x rep=%d", g.done_now, vm_read32(0x0094DEC4u), g.stepper.replaying() ? 1 : 0);
    for (int t = 4; t <= 20 && n < (int)sizeof line - 40; t++)
        if (alive[t]) n += std::snprintf(line + n, sizeof line - n, " %d:%x:%x", t, kind[t], lr[t]);
    std::fprintf(stderr, "%s\n", line);
}
// ENHANCEMENT (native-room pairing / same-link rematch): main-thread steps of the boundary, defined after the connection code (below conn_close).
void native_poll();
void match_poll();
// Boundaries in a row without a fight HUD draw (g_hud_calls unchanged): the fight scene is not on screen. Main thread.
uint32_t g_hud_last = 0, g_hud_idle = 0;
constexpr uint32_t kHudIdleFrames = 45;   // about 1.5 s: never (re)open a link or let a re-armed match anchor while a fight is drawn
constexpr uint64_t kSyncDeadlineMs = 45000;   // FIX (a peer that never reaches the fight): the session must synchronize this long after it started on this PC
// ---- the boundary: BeginFrame entry, once per main-loop iteration ------------------------------------------------------------------------------------------------
// DIAGNOSTIC (re-run stalls, jsp_stepper.cpp): the game's frame-pacing vblank counter *(*(TOC - 0x4A80)) (func_006507DC waits until it is >= N, then zeroes
// it). The TOC is r2 at BeginFrame. Read by the frame clock thread for a log line only (a racy read of guest memory); -1 = not known yet.
std::atomic<uint32_t> g_toc{0};
extern "C" int32_t jsp_net_vblank_counter(void)
{
    const uint32_t toc = g_toc.load(std::memory_order_relaxed);
    if (toc < 0x10000u) return -1;
    const uint32_t p = vm_read32(toc - 0x4A80u);
    return (p >= 0x10000u && p < 0xC0000000u) ? (int32_t)vm_read32(p) : -1;
}
struct MainStage { MainStage() { g_jsp_main_stage = 1; } ~MainStage() { g_jsp_main_stage = 0; } };   // 1 = in the boundary; 0 = back in guest code
void boundary(ppu_context* c)
{
    g_toc.store((uint32_t)c->gpr[2], std::memory_order_relaxed);
    const MainStage stage_mark;
    { const uint32_t hc = g_hud_calls; if (hc != g_hud_last) { g_hud_last = hc; g_hud_idle = 0; } else if (g_hud_idle < 0x7FFFFFFFu) g_hud_idle++; }
    if (!g.bench) { native_poll(); match_poll(); }   // may open, close or re-arm the link: the phase is read below
    JspTickState ts; jsp_fight_hash_get_state(&ts);
    g.ctx = c; g.done_now = ts.done;
    g_wp_tick.store(ts.done);
    g_vc_reads = 0;   // FIX (game clock): the reads of the tick about to run start at its time
    task_watch_flush();
    if (g.fight_seen && g.phase == Phase::Running) cam_watch_maybe(g.hud_tick);
    Phase ph = g.phase;
    if (g.ended_ms && GetTickCount64() - g.ended_ms > kLingerMs) { g.sess.stop(); g.ended_ms = 0; }   // ENHANCEMENT (lobby): the ended match's session goes after its linger
    if (ph == Phase::Off) return;
    if (g.m2_ended.load() && ph != Phase::Ended) {   // a Cancel / failure of the settings layer while waiting, armed or in the match: the match ends on this side too
        std::string t; bool quiet = false; { std::lock_guard<std::mutex> lk(g.txt_mu); t = g.m2_text; quiet = g.m2_quiet; }
        if (ph == Phase::Running) g.sess.abort("settings layer ended: " + t);
        end_with(t, quiet); return;
    }
    if (ph == Phase::Handshake && g.m2_started.load()) { g.phase = Phase::Armed; ph = Phase::Armed; log("phase", "armed (settings agreed, waiting for the first fight tick)"); }
    // ENHANCEMENT (same-link rematch): a match re-armed after a fight waits until that fight is off the screen, then forgets it (the hash anchored again on the ended
    // fight's scene would freeze the next fight's count), so the session starts at the NEXT fight's anchor. jsp_net_match_begin releases it at once.
    if (ph == Phase::Armed && g.fresh_gate) {
        if (g_hud_idle < kHudIdleFrames) { ui_and_cancel(); return; }
        g.fresh_gate = false; jsp_fight_hash_reset_fight(); g.hud_seen = 0; g.hud_seen_set = false;
        log("rematch", "the last fight is gone: the next fight starts match " + std::to_string(g.match_k));
        return;   // ts was read before the reset
    }
    // done >= 1 already implies the camera anchor (g_done counts only after it is set; the hash tap needs the cameras), so tick `done` has a raw hash. ts.anc_set is the PRESS anchor and is never set here.
    // FIX (pre-fight loading desync): a match session starts at the first boundary after the game drew the fight HUD (func_00085CEC, counted by the guest
    // call, not timed), not at the camera anchor. While the fight loads, the game adds a task (count at 0x0094DEC4) when a file read finishes, which is timed
    // by the host: two PCs added the 0x0E -> 0x0F task on different ticks (two-copy tests, every desync seen). The hashed state does not change from tick to
    // tick while loading, so the tick count spent loading does not matter, and the checksum's tick number is session-relative. (The task count is no scene
    // id: it stayed at 0x0D through a whole fight in one test.) JSP_ROLLBACK_START=anchor: the old start (A/B). The bench keeps the anchor start.
    if (ph == Phase::Armed && ts.done >= 1 && !g.bench && !start_at_anchor()) {
        uint32_t& seen = g.hud_seen; bool& seen_set = g.hud_seen_set;   // ENHANCEMENT (lobby): per match (reset_match), so a second session waits for its own fight's HUD
        const uint32_t n = g_hud_calls;
        const bool drawn = seen_set && n != seen;
        seen = n; seen_set = true;
        if (!drawn) { ui_and_cancel(); return; }
    }
    if (ph == Phase::Armed && ts.done >= 1) { if (g.bench) start_bench(); else start_session(c); ph = g.phase; if (ph != Phase::Running) return; }
    else if (ph != Phase::Running) { ui_and_cancel(); return; }
    if (g.bench) bench_iteration();
    if (!g.fight_seen) {   // FIX (loading): the first fight HUD draw since the session started
        const uint32_t n = g_hud_calls;
        if (g.hud_mark == 0) g.hud_mark = n + 1;   // (+1: 0 means unset)
        else if (n + 1 != g.hud_mark) { g.fight_seen = true; g.hud_tick = g.done_now; std::fprintf(stderr, "[net] fight HUD drawn: loading is over (tick %u)\n", g.done_now); }
        load_diag();
    }
    if (jpno::cancel_held(cancel_key(), GetTickCount64(), g.cancel_since)) { g.sess.abort("match ended by the player"); end_with("Match ended"); return; }   // End match: F9 held 1.5 s
    if (g.path_until && GetTickCount64() > g.path_until) { g.path_until = 0; if (g.shown == g.path_line) show(""); }   // never blanks a later message (a reconnect or an end line)
    if (perf_on() && g.sess.synced()) {   // ENHANCEMENT (match perf summary): the frame the last iteration presented (none after a replay tick)
        const double pn = perf_now(); jpperf::on_frame(g_perf, pn, !g.was_replaying, g.bv.delay);
        if (jpperf::wants_ack(g_perf)) jpperf::on_ack(g_perf, pn, g.sess.stats().acked_frame);
    }
    const uint64_t u0 = g.glue->updates();
    jsx::Op op = g.glue->iteration(true);   // update is called whenever the stepper has nothing queued (skipping it for pacing stalled both peers in a two-copy test)
    // FIX (time sync, smoothness audit cause 4): GekkoNet's frames-ahead slowdown was a Sleep here, swallowed by the lockstep clock's 33.3 ms hold, so the
    // peer that ran ahead never slowed down (it predicted every tick, saved a snapshot every tick and took every rollback). In a match it now stretches the
    // next lockstep period instead: (frames ahead - 0.5) ms per tick, at most 2 ms (6%), only on the peer that is ahead. Timing only: the vblanks per flip and
    // the inputs do not change, so determinism is untouched. Without the lockstep clock (the bench) the old proportional sleep stays.
    if (g.glue->updates() != u0) {
        jsp_snapshot_ext_budget_reset();
        const float ahead = g.bench ? 0.f : g.sess.frames_ahead();
        const uint32_t us = ahead > 0.5f ? (uint32_t)std::min(2000.0f, (ahead - 0.5f) * 1000.0f) : 0u;
        if (g.bench || !jsp_stepper_lockstep_stretch_us(us)) { if (const uint32_t ms = g.sess.pace_sleep_ms(33)) Sleep(ms); }
    }
    else g.sess.poll();   // replay / regrant / stall iterations: the network is still serviced
    // FIX (rollback determinism: a render-only frame flipped): an iteration where no tick may run still ran the game's frame outside the simulation phases and flipped, and every
    // flip is 2 vblanks the game counts; how many such iterations a PC has depends on its timing. While the session is synchronized, the main thread now waits
    // here (polling, asking the session again every millisecond) for up to 400 ms before a render-only frame is let through (below the 500 ms idle vblank).
    if (op.kind != jsx::OpKind::Run && g.sess.synced() && !g.bench) {
        g_jsp_main_stage = 2;   // DIAGNOSTIC: the hold for a tick (the replay clock is still on here after a replay's last tick)
        const int64_t h0 = qpc_now();
        while (op.kind != jsx::OpKind::Run && qpc_ms(h0, qpc_now()) < 400.0 && !g.sess.failed()) {
            Sleep(1); g.sess.poll();
            const uint64_t u1 = g.glue->updates();
            op = g.glue->iteration(true);
            if (g.glue->updates() != u1) jsp_snapshot_ext_budget_reset();
        }
        g_hold_ms += qpc_ms(h0, qpc_now()); g_holds++; if (op.kind != jsx::OpKind::Run) g_hold_renders++;
        g_jsp_main_stage = 5;
    }
    int events_ff = -1;   // FIX (review of PR 248): GekkoNet's final frame read with the events, under the session lock (the live watermark below)
    for (const SessionEvent& e : g.sess.take_events(&events_ff)) {
        switch (e.kind) {
        case SessionEventKind::Started: log("gekko session synchronized", ""); if (!g.path_line.empty()) { show(g.path_line); g.path_until = GetTickCount64() + 6000; } else show(""); break;
        case SessionEventKind::Disconnected:   // GekkoNet fires it after the disconnect timeout = the grace window: the match is over
            { std::string why; if (m2_cancelled_within(300, &why)) { g.sess.abort(why); end_with(why); return; } }   // the peer's handshake Cancel (a local failure there) names the reason
            // FIX (a missed KO): a KO was already recorded here: the other PC ended its match on it and its session went; this is a normal end, not a failure.
            // FIX (review of PR 248): only a result the live confirmation watermark covers; a predicted one is not a completed match (ko_after_disconnect).
            if (const int w = ko_after_disconnect()) { g.sess.abort("match over (the opponent ended on the KO)"); std::fprintf(stderr, "[net] match over, not a disconnect\n"); end_with(jkr::disconnect_end_text(w), true); return; }
            g.sess.abort("opponent disconnected"); end_with(jkr::disconnect_end_text(0)); return;
        case SessionEventKind::Desync:   // includes a base-tick mismatch: the per-tick hash carries the tick number
            if (g.bench) {   // ENHANCEMENT (rollback bench): a re-run tick differed from its first run (non-determinism the snapshot misses): counted, logged, the bench goes on
                if (g.bs.first_desync < 0) g.bs.first_desync = e.tick;
                if (g.bs.desyncs++ < 20) std::fprintf(stderr, "[bench] RE-RUN DIFFERS at tick %d (session-relative; base %u): first run %08x, re-run %08x\n", e.tick, g.base, e.remote_hash, e.local_hash);
                break;
            }
            std::fprintf(stderr, "[net] DESYNC at tick %u (stepper tick numbering, session-relative; add the base %u): local=%08x remote=%08x\n", (unsigned)e.tick, g.base, e.local_hash, e.remote_hash);
            if (e.tick <= 3) std::fprintf(stderr, "[net] base mismatch suspected: the desync is at the first compared ticks; the two peers probably armed at different ticks (local base %u; the base is part of the per-tick hash, it is not exchanged)\n", g.base);
            g.sess.abort("desync detected"); end_with("Desync detected: match ended"); return;
        default: break;
        }
    }
    // FIX (frame clock stall): the lockstep clock gave up on the guest (no vblank taken for 60 s); end_with's jsp_stepper_lockstep(0) clears the flag.
    // FIX (review of PR 248): checked before the session failure, because the pump thread may have ended the session on it already (pump_tick); the
    // peer was told there, so this end only names the reason locally.
    if (jsp_stepper_lockstep_failed()) { const bool told = g.stall_ended.exchange(true); g.sess.abort("frame clock stalled"); if (!told) send_failure_cancel(); end_with("Match ended: the game stopped taking vblanks"); return; }
    if (g.sess.failed()) { send_failure_cancel(); end_with("Rollback session ended: " + g.sess.failure()); return; }
    // FIX (a peer that never reaches the fight): handover() switched the handshake's silence checks off, so a peer that never reaches its fight left this PC on "Connecting to
    // opponent..." for good. GekkoNet's own disconnect timer only runs once the session is synchronized; until then this deadline ends the match.
    if (!g.bench && g.started_ms && !g.sess.synced() && GetTickCount64() - g.started_ms > kSyncDeadlineMs) {
        std::fprintf(stderr, "[net] the opponent's game did not join the fight within %u s of this one (no session sync)\n", (unsigned)(kSyncDeadlineMs / 1000));
        send_failure_cancel(); g.sess.abort("no session sync"); end_with("Opponent did not reach the fight: match ended"); return;
    }
    // ENHANCEMENT (online match end): a confirmed tick had a KO winner (GameGlue::confirmed). Ended quietly (no UI yet: the post-game menu is a later step) and
    // with NO abort: the GekkoNet session lingers kLingerMs and its keepalive thread keeps answering, so the other PC confirms the same tick and ends the same way
    // instead of seeing a disconnect. Without this end the boundary would wait for ticks forever once the fight scene goes and done stops.
    // FIX (review of PR 248): no Disconnected among this boundary's events (it returned above), so GekkoNet's final frame here is confirmed by every peer's
    // real inputs: the live watermark a later disconnect may end on (a disconnect fills the gone peer's slots with neutral input INSIDE an update, and
    // that update's Disconnected event is always taken before this line). The frame is the one read with the events under the session lock
    // (take_events(&ff)): a keepalive poll that queues a disconnect after them cannot move it.
    if (!g.bench) g_ko_live.live(events_ff);
    ko_scan();   // FIX (a missed KO): the earliest confirmed tick among the recorded winners
    if (g.match_winner > 0) { end_with(match_over_text(g.match_winner), true); return; }
    const uint64_t now = GetTickCount64();
    if (g.bench) g.bs.last_kind = op.kind != jsx::OpKind::Run ? 0 : op.replay ? 2 : 1;   // what this iteration runs (bench_iteration files its time at the next boundary)
    if (op.kind == jsx::OpKind::Run) {
        g.skip_sim = false; g.skip_pad = op.regrant;
        if (!op.regrant) g.last_run_ms = now;
        if (g.shown.rfind("Reconnecting", 0) == 0) show("");
    } else {
        g.skip_sim = true; g.skip_pad = true;   // no tick may run: skip phases 1, 2 and the pad poll (a render-only iteration) and keep presenting
        if (g.sess.synced() && now - g.last_run_ms > 1000)
            show("Reconnecting... " + std::to_string((now - g.last_run_ms) / 1000) + " s (F9 held: end match)");
    }
    if (op.kind == jsx::OpKind::Run && !op.regrant && g_ps3_audio_lock_target >= 0) {   // FIX (tick-locked audio): the audio of every tick before this one, taken
        g_ps3_audio_lock_mute = op.replay ? 1 : 0;
        ps3_audio_lock_set_target(audio_blocks_at(g.done_now));
        g_jsp_main_stage = 3;   // DIAGNOSTIC
        audio_lock_wait();
    }
    if (op.kind == jsx::OpKind::Run && !op.regrant) { g_jsp_main_stage = 4; cri_cycle(); }   // FIX (sound server lock): one cycle at this tick's time, after the audio blocks
    g_jsp_main_stage = 5;
    if (op.kind == jsx::OpKind::Run && op.replay && !op.regrant) jsp_stepper_rerun_tick(op.tick, (!g.bench && tick_log()) ? 1 : 0);   // DIAGNOSTIC (re-run stalls)
    const bool rep = g.stepper.replaying();
    if (g.was_replaying && !rep) jsp_stepper_replay(0, g.done_now);   // the replay's last tick is done: fast clock and effect gate off
    g.was_replaying = rep;
    if (g.badge_every.due(now, jpnb::kRefreshMs)) {   // the status badge: at most four times a second; drawn again if another layer user closed the layer
        g.bv.path = path_word();
        if (g.badge_on) { const std::string b = jpnb::text(g.bv); if (b != g.badge_shown || !jsp_overlay_active()) { g.badge_shown = b; redraw(); } }
    }
    if (g.ping_on && !g.bench && g.ping_every.due(now, jpnb::kPingRefreshMs)) {   // the ping counter: at most twice a second; drawn again if the layer was closed
        const float ms = g.sess.stats().ping_ms; const std::string pt = jpnb::ping_text(ms); const uint32_t col = jpnb::ping_rgba(ms);
        if (pt != g.ping_shown || (!pt.empty() && (col != g.ping_rgba || !jsp_overlay_active()))) { g.ping_shown = pt; g.ping_rgba = col; redraw(); }
    }
    if (g.status_every.due(now, jpnb::kLogMs) && now - g.started_ms >= 1000) {
        log_status();
        if (g_jsp_native_fight_armed) {   // FIX (swapped controls): the ownership check of the running fight, for the logs and the test drivers
            char om[256] = {}; jsp_native_fight_owner_check(om, sizeof om);
            std::fprintf(stderr, "[net] ownership check: %s\n", om);
        }
    }
    if (!g.bench) adapt_delay(now);   // not at the session's first iteration: the line at the end covers a short one
}

// ---- hooks ------------------------------------------------------------------------------------------------------------------------------------------------
typedef void (*fn_t)(ppu_context*);
enum Ev { E_BEGIN, E_P1, E_P2, E_PAD };
struct Ent { uint32_t addr; Ev ev; const char* name; };
constexpr Ent k_ents[] = { {0x00642C3C, E_BEGIN, "BeginFrame"}, {0x0064EBEC, E_P1, "F.vt[8]"}, {0x0064EF10, E_P2, "F.vt[0xC]"}, {0x003F7E2C, E_PAD, "pad_update"} };

template <int I> struct Hook {
    static fn_t orig;
    static void run(ppu_context* c)
    {
        const Ev ev = k_ents[I].ev;
        if (ev == E_BEGIN) boundary(c);
        else if (g.phase == Phase::Running) {
            if ((ev == E_P1 || ev == E_P2) && g.skip_sim) return;
            if (ev == E_PAD && (g.skip_sim || g.skip_pad)) return;   // regrant: the pad must not be polled twice for one tick (see Stepper::boundary)
        }
        if (g.bench && ev != E_BEGIN && g.phase == Phase::Running) {   // ENHANCEMENT (rollback bench): the phase's cost, filed under live or re-run (bench_phase)
            const int64_t t0 = qpc_now(); orig(c); bench_phase((int)ev, qpc_ms(t0, qpc_now())); return;
        }
        orig(c);
    }
    static void install()
    {
        if (!jsp_hook_chain::install(k_ents[I].addr, &run, &orig)) std::fprintf(stderr, "[net] no lifted function at %08X (%s)\n", k_ents[I].addr, k_ents[I].name);
    }
};
template <int I> fn_t Hook<I>::orig;
// DIAGNOSTIC (JSP_NET_LOAD_DIAG=1): the game's task list (count at 0x0094DEC4): who adds (func_00662784, func_00662F94) and removes (func_006624DC) a task
// while a session runs. One line per call: the tick, replay flag, count before and after, lr and a guest back-chain of 6 frames.
template <uint32_t A> struct TaskDiag {
    static fn_t orig;
    static void run(ppu_context* c)
    {
        const uint32_t before = vm_read32(0x0094DEC4u);
        char ch[120]; int n = 0; ch[0] = 0;
        uint32_t sp = (uint32_t)c->gpr[1];
        for (int i = 0; i < 6 && sp && !(sp & 7) && sp < 0xF0000000u; i++) {
            const uint32_t next = (uint32_t)vm_read64(sp);
            if (!next || next <= sp) break;
            n += std::snprintf(ch + n, sizeof ch - n, " %08X", (uint32_t)vm_read64(next + 0x10));
            sp = next;
        }
        const uint32_t lr = (uint32_t)c->lr;
        orig(c);
        if (g.phase == Phase::Running)
            std::fprintf(stderr, "[task] %08X tick=%u rep=%d count %x->%x lr=%08X chain%s\n", A, g.done_now, g.stepper.replaying() ? 1 : 0, before, vm_read32(0x0094DEC4u), lr, ch);
    }
    static void install() { if (!jsp_hook_chain::install(A, &run, &orig)) std::fprintf(stderr, "[task] no function at %08X\n", A); }
};
template <uint32_t A> fn_t TaskDiag<A>::orig;
// DIAGNOSTIC (local build only: the lifted file ppu_recomp_056.cpp is edited by hand to call it at the three stores to the task count 0x0094DEC4).
extern "C" void jsp_task_note(ppu_context* c, uint32_t fn, uint32_t newcount)
{
    if (g.phase != Phase::Running) return;
    char ch[160]; int n = 0; ch[0] = 0;
    uint32_t sp = (uint32_t)c->gpr[1];
    for (int i = 0; i < 10 && sp && !(sp & 7) && sp < 0xF0000000u; i++) {
        const uint32_t next = (uint32_t)vm_read64(sp);
        if (!next || next <= sp) break;
        n += std::snprintf(ch + n, sizeof ch - n, " %08X", (uint32_t)vm_read64(next + 0x10));
        sp = next;
    }
    std::fprintf(stderr, "[task] %08X tick=%u rep=%d thread=%lu count %x->%x lr=%08X chain%s%c", fn, g.done_now, g.stepper.replaying() ? 1 : 0,
                 GetCurrentThreadId(), vm_read32(0x0094DEC4u), newcount, (uint32_t)c->lr, ch, 10);
}
void task_diag_install()
{
    char b[8] = {};
    if (!(jsp_setting_get("JSP_NET_LOAD_DIAG", b, sizeof b) && b[0] == '1')) return;
    TaskDiag<0x00662784u>::install(); TaskDiag<0x00662F94u>::install(); TaskDiag<0x006624DCu>::install();
}
template <int... Is> void install_all(std::integer_sequence<int, Is...>) { (Hook<Is>::install(), ...); }
// ENHANCEMENT (rollback bench only): times the fight's scene update @0x0014EA68 (the simulation tick, also hooked by the fight hash; chained, so both run).
struct SceneTimer {
    static fn_t orig;
    static void run(ppu_context* c) { if (g.phase != Phase::Running) { orig(c); return; } const int64_t t0 = qpc_now(); orig(c); bench_phase(4, qpc_ms(t0, qpc_now())); }
    static void install() { if (!jsp_hook_chain::install(0x0014EA68u, &run, &orig)) std::fprintf(stderr, "[bench] no function at 0014EA68: no scene timing\n"); }
};
fn_t SceneTimer::orig;

// ---- the process-wide install and the per-session connection ------------------------------------------------------------------------------------------------
// ENHANCEMENT (lobby: sessions opened and closed at runtime, jsp_net_lobby.h). jsp_net_game_install used to do both at once; now:
//   install_process_wide  once per process: the frame hooks, the F9 binding, the settings text hook, the badge switch (nothing per peer)
//   conn_open / conn_close  per session: role, socket, re-psn client and room, rendezvous, link, handshake objects, pump thread; Phase Off <-> Handshake
// The start-up role paths (LISTEN / JOIN, VIA=repsn + ROLE) and jsp_net_lobby_open both call conn_open: one implementation.
struct Conn { bool host = true, repsn = false, relay = false; uint16_t port = 0; Endpoint join{}; uint64_t room = 0;
              uint64_t pair = 0; };   // ENHANCEMENT (native-room pairing): the game's own room id (re-psn only; 0 = not paired)
uint32_t g_net_build = 0;     // build_id() with the mod set folded in (jsp_net_game_install)
bool g_installed = false;     // install_process_wide ran: a rollback-capable process (not the bench), so jsp_net_lobby_open may run
std::string g_lobby_why;      // jsp_net_lobby_reason (main thread)

void install_process_wide()
{
    if (g_installed) return;
    g_installed = true;
    // F9 is the cancel key: a VK 0x78 binding (settings pages save PAD_KEY_* as 0x78 / decimal) is dropped from the environment the pad layer reads.
    for (char** e = environ; e && *e; ++e) {
        if (std::strncmp(*e, "PAD_KEY_", 8) != 0) continue;
        const char* eq = std::strchr(*e, '='); if (!eq) continue;
        char* end = nullptr; const long vk = std::strtol(eq + 1, &end, 0);
        if (end != eq + 1 && vk == 0x78) { const std::string name(*e, (size_t)(eq - *e)); _putenv_s(name.c_str(), ""); std::fprintf(stderr, "[net] binding %s = F9 dropped (F9 is the rollback cancel key)\n", name.c_str()); e = environ - 1; }
    }
    { char bb[16] = {}; g.badge_on = jsp_setting_get(jpnb::kKey, bb, sizeof bb) && jpnb::key_on(bb); }
    { char pb[16] = {}; g.ping_on = !jsp_setting_get(jpnb::kPingKey, pb, sizeof pb) || jpnb::ping_key_on(pb); }   // default on
    jsp_session_text_hook = session_text;
    jsp_native_fight_install();   // ENHANCEMENT (native fight takeover): the hooks, all pass-through until start_session arms them in an online fight
    install_all(std::make_integer_sequence<int, sizeof k_ents / sizeof k_ents[0]>());   // inert while the phase is Off (boundary returns, the phase hooks run the original)
    task_diag_install();
}

// ENHANCEMENT (version mismatch message): this game's release name as the main menu's version line reads it (JSP_PORT_VERSION, then release.txt beside the
// exe or in its parent folder; jsp_port_menu.cpp load_version_line); "" for a local build (the message then names the build word).
std::string own_release()
{
    if (const char* v = std::getenv("JSP_PORT_VERSION")) if (jpv::valid_version(v)) return v;
    char exe[1024] = "";
    const unsigned long n = GetModuleFileNameA(nullptr, exe, sizeof exe);
    std::string dir(exe, n < sizeof exe ? n : 0);
    for (int i = 0; i < 2; i++) {
        const size_t sl = dir.find_last_of("\\/");
        if (sl == std::string::npos) break;
        dir.resize(sl);
        std::string t;
        if (FILE* f = std::fopen((dir + "\\release.txt").c_str(), "rb")) { char b[256]; t.assign(b, std::fread(b, 1, sizeof b, f)); std::fclose(f); }
        const std::string v = jpv::parse_version_txt(t);
        if (!v.empty()) return v;
    }
    return "";
}
// Opens the connection of one session and enters Phase::Handshake, as the start-up path always did. false + why / code: nothing is left open (the caller logs
// why and calls conn_close to be sure). Main thread, with the pump thread stopped.
bool conn_open(const Conn& c, std::string* why, jpno::Refusal* code)
{
    RepsnIdentity rid;
    g.host = c.host; g.listen_port = c.port; g.join = c.join;
    if (c.repsn) {   // ENHANCEMENT (rollback over re-psn): a room on the player's re-psn server arranges the session; our UdpMux socket is the P2P socket
        char srv[160] = {}, nm[64] = {}, cid[24] = {}; int rep_rtt = 0;
        if (!jsp_online_repsn_identity(srv, sizeof srv, nm, sizeof nm, cid, sizeof cid, &rep_rtt) || !parse_server(srv, rid.host, rid.port) || !nm[0]) {
            *code = jpno::Refusal::RepsnNoIdentity; *why = "re-psn: no server / online name in repsn.ini (set them on the Online page under Delay)"; return false;
        }
        if (std::strlen(nm) > 16) { *code = jpno::Refusal::RepsnLongName; *why = std::string("re-psn: the online name '") + nm + "' is longer than 16 characters (repsn.ini name=)"; return false; }
        std::snprintf(g.presence_srv, sizeof g.presence_srv, "%s", srv);
        rid.name = nm; if (cid[0]) rid.comm_id = cid; rid.report_rtt = rep_rtt != 0;
        if (c.pair) {   // ENHANCEMENT (native-room pairing): our client runs NEXT to the game's own one on the same server: a name and a communication id of its own (jsp_net_repsn.h)
            rid.name = rollback_name(rid.name);
            char nc[24] = {};
            if (!jsp_setting_get("JSP_ROLLBACK_NATIVE_COMM", nc, sizeof nc) || !nc[0]) rid.comm_id = rollback_comm_id(rid.comm_id);
            else if (std::strcmp(nc, "same") != 0) rid.comm_id = nc;
        }
    }
    std::string err;
    // the host binds its port; over re-psn a fixed port is optional (0 = any, firewalls); a direct join takes any port
    if (!g.mux.open(c.host || c.repsn ? c.port : 0, &err)) { *code = jpno::Refusal::NoSocket; *why = "cannot open the UDP socket: " + err; g.presence_srv[0] = 0; return false; }
    g.caps.build = g_net_build;   // build_id() with the mod set folded in
    // local preferences into the effective-value object (the proposal of a host starts from these; a client's own layer sits under the host's). Read per
    // session, so a setting changed in the game before a lobby opens counts.
    for (size_t i = 0; i < jpsm::kCount; ++i) {
        const jpsm::Id id = static_cast<jpsm::Id>(i);
        char t[64]; uint32_t v;
        if (jsp_setting_get(jpsm::rule(id).name, t, sizeof t) && t[0] && text_to_canon(id, t, &v) && jpsm::in_range(id, v)) g.ev.set_local(id, v);
    }
    g.ev.set_local(jpsm::Id::Rollback, 1);
    g.hub.reset(new MessageHub(g.mux));
    if (c.repsn) {
        g.rp.reset(new PsnrClientTransport(g.mux, rid));
        RePsnRendezvous::Config rc; rc.host = c.host; rc.build = g.caps.build; rc.allow_relay = c.relay; rc.local_count = 1;
        rc.release = own_release();   // ENHANCEMENT (version mismatch message)
        rc.pair = c.pair;   // ENHANCEMENT (native-room pairing): only the rollback room of the same native room (0: the plain record, as before)
        // TEST ONLY (JSP_ROLLBACK_FORCE_RELAY=1, gated by JSP_ROLLBACK_ALLOW_DEBUG=1 like the other test knobs): skip the direct try, use the relay at once
        if (env_on("JSP_ROLLBACK_ALLOW_DEBUG") && env_on("JSP_ROLLBACK_FORCE_RELAY")) {
            rc.force_relay = true;
            std::fprintf(stderr, "[net] TEST: re-psn relay forced (JSP_ROLLBACK_FORCE_RELAY=1; used only when relay is allowed (%s) and the server offers it)\n", c.relay ? "yes" : "no");
        }
        if (!c.host && c.room) { rc.join_room = c.room; std::fprintf(stderr, "[net] re-psn: joining room %llu\n", (unsigned long long)rc.join_room); }   // ENHANCEMENT (Discord Join): that room, not the oldest
        g.rrz = new RePsnRendezvous(*g.rp, g.mux, *g.hub, rc);
        g.rrz->on_progress = [](const std::string& line, const std::string& n1, const std::string& n2) {   // runs inside rz->poll() (the pump thread): every 10 s while waiting
            std::fprintf(stderr, "[net] %s\n", line.c_str());
            if (g.rrz && g.rrz->room_id()) g.presence_room = g.rrz->room_id();   // same thread as the rendezvous
            g_rdv.waiting(n1, n2);
        };
        g.rrz->on_path = [](Path p, uint32_t peer_user) {   // runs inside rz->poll() (the pump thread)
            g.rp->set_peer_user(peer_user);
            g.rp->set_relay_mode(p == Path::Relay);   // Relay: every datagram to the peer goes through the server from now on, until the session ends
            std::fprintf(stderr, "[net] re-psn path: %s\n", path_name(p));
        };
        g.rz.reset(g.rrz);
        g.rp->start(c.relay);
    } else g.rz.reset(g.host ? new DirectRendezvous(g.mux, *g.hub, 1, random_session_id()) : new DirectRendezvous(g.mux, *g.hub, 1, g.join));
    g.link.reset(new M2LinkT<jpsm::Transport>(g.mux));
    g.phase = Phase::Handshake;
    if (c.repsn) std::fprintf(stderr, "[net] re-psn: %s a room on %s:%u as %s (relay %s), local UDP port %u%s\n", g.host ? "creating" : "joining", rid.host.c_str(), (unsigned)rid.port, rid.name.c_str(), c.relay ? "allowed" : "not allowed", (unsigned)g.mux.local_port(),
                              c.pair ? (" (paired with the game's room " + std::to_string((unsigned long long)c.pair) + ", comm id " + rid.comm_id + ")").c_str() : "");
    else std::fprintf(stderr, "[net] listening/joining: %s, local UDP port %u\n", g.host ? "host" : to_string(g.join).c_str(), (unsigned)g.mux.local_port());
    g.pump.start(g.mux, *g.link,
        [](uint8_t peer, const uint8_t* d, size_t n, uint64_t ms) {   // runs on the pump thread under its mutex
            if (g.plan_ready) {   // ENHANCEMENT (same-link rematch): which match of this link the datagram belongs to (jsp_net_match.h)
                uint32_t to = 0;
                const MatchMsg mm = match_peek(g.plan.session_id, g.match_k, d, n, &to);
                if (mm == MatchMsg::Beacon || mm == MatchMsg::Older) return;   // a beacon is not for the objects; an earlier match's straggler is dropped
                if (mm == MatchMsg::Newer) {   // the other side is at a later match: the main thread re-arms there (match_poll); its next repeat reaches the new objects
                    uint32_t cur = g.match_jump.load();
                    while (to > cur && !g.match_jump.compare_exchange_weak(cur, to)) {}
                    return;
                }
            }
            if (g.host && g.mh) g.mh->on_message(peer, d, n, ms);
            else if (!g.host && g.mc) g.mc->on_message(peer, d, n, ms);
        },
        [](uint64_t t) { pump_tick(t); }, 100);
    return true;
}

// Tears down what conn_open built and returns to Phase::Off with the per-connection state reset. Tell the peer BEFORE (a Cancel / leave, or the session's
// abort): this only closes. Main thread. A match must already be ended (end_with): the stepper hook, the locks and the settings layer are its business.
void conn_close()
{
    g.pump.stop();                    // joins the pump thread: nothing touches the handshake objects, the rendezvous or the re-psn client after this
    g.ev.clear();                     // a settings layer still installed (closed mid-handshake) is removed; restored() tells the settings reader
    g.mh.reset(); g.mc.reset(); g.link.reset();
    g.rz.reset(); g.rrz = nullptr;    // the rendezvous uses the re-psn client and the hub: first
    g.rp.reset();                     // leaves the room, closes the server connection, takes its hooks off the mux
    g.hub.reset();
    g.sess.stop(); g.ended_ms = 0;    // GekkoNet's adapter is bound to this mux: before the socket goes
    g.mux.close(); g.mux.set_link_model(LinkModel());   // a TEST link impairment (JSP_NET_LINK) does not carry over to the next session
    g_rdv.ended();
    g.presence_room = 0; g.presence_srv[0] = 0; g.path_kind = 0; g.path_line.clear(); g.path_until = 0;
    g.plan = SessionPlan(); g.plan_ready = g.proposed = false; g.propose_tries = 0;
    g.m2_started = false; g.m2_ended = false; g.evidence_ready = false;
    { std::lock_guard<std::mutex> lk(g.txt_mu); g.fail_text.clear(); g.m2_text.clear(); }
    g.m2_display.clear(); g.m2_quiet = false;
    g.ev_first = g.ev_next = g.wait_next = 0; g.hs_last = -2;
    g.listen_port = 0; g.join = Endpoint{};
    g.match_k = 0; g.match_jump = 0; g.beacon_next = 0; g.fresh_gate = false;   // ENHANCEMENT (same-link rematch): the counter belongs to the link
    g.phase = Phase::Off;
    show(""); badge(""); ping_hide();
}

// ---- ENHANCEMENT (native-room pairing): the rollback link follows the game's own Online Battle room ----------------------------------------------------------------
// Players meet in the game's own Online Battle menus (a Friendly Match room over re-psn, the runtime's Matching2 emulation; patch 7924 lets us read the room).
// While this game is in a native room with exactly one other member, a rollback link to that player is opened through re-psn and PAIRED BY THE NATIVE ROOM: the
// native room owner hosts (battle slot 0, GekkoNet slot 0), and the rollback room's record carries the native room id (jsp_net_repsn.h make_record), so a
// joiner takes only the rollback room of its own native room. It opens while the players are still in the room (the fight starts tens of seconds later), so the
// fight start costs nothing; the first match arms as soon as the settings handshake is done and starts at the fight's anchor, as every rollback match does.
// It closes when the native room is left (not in a room, another room, or fewer than 2 members, for 1 s). After every match the same link arms the next one
// (match_arm, after the fight is gone: boundary's fresh_gate). The link uses its own UDP port (any free one; the game's P2P socket keeps its own) and the player's
// relay choice (JSP_ROLLBACK_RELAY). JSP_ROLLBACK_NATIVE: on by default in the lobby start (JSP_ROLLBACK=1 with no role key); 0 turns it off. The start-up role
// paths and the bench never use it. A link opened by jsp_net_lobby_open is left alone.
extern "C" int ps3_np_matching2_room(uint64_t* room, int* members, int* max, int* is_owner, char* peer, int peer_n);   // toolkit patch 7924; any thread
struct Native {
    bool on = false;              // jsp_net_game_install: lobby start and JSP_ROLLBACK_NATIVE not 0
    bool link = false;            // the open link is ours (opened by native_open)
    uint64_t room = 0;            // the native room the link is paired with
    uint64_t seen_room = 0, seen_since = 0, away_since = 0, next_poll = 0, retry_at = 0, rearm_at = 0;
    int tries = 0, fails = 0;     // link opens for this native room; matches in a row that did not end normally on this link
    uint64_t given_up = 0;        // a native room this game stopped trying with (until it is in another one)
} g_nat;

void native_close(const char* why)
{
    log("native room", std::string(why) + ": closing the rollback link");
    g_nat.link = false; g_nat.room = 0; g_nat.away_since = 0; g_nat.rearm_at = 0;
    jsp_net_lobby_close();   // tells the other player (the session's disconnect, or a handshake Cancel / leave) and closes everything
}
void native_open(uint64_t room, bool owner)
{
    Conn c; c.repsn = true; c.host = owner; c.pair = room;
    { char r[8] = {}; c.relay = jsp_setting_get(jpno::kKeyRelay, r, sizeof r) && jpno::flag_on(r); }   // the player's relay choice
    if (const char* w = jmods::rollback_gate(jsp_mods_copy_mismatch() != 0)) { log("native room: no rollback link", std::string("rollback refused: ") + w); g_nat.given_up = room; return; }
    reset_match();   // the fight hash forgets any earlier fight (no fight runs: native_poll waits for the HUD to be gone)
    std::string why; jpno::Refusal code = jpno::Refusal::None;
    if (!conn_open(c, &why, &code)) { conn_close(); log("native room: no rollback link", why); g_nat.given_up = room; return; }
    g_nat.link = true; g_nat.room = room; g_nat.tries++; g_nat.fails = 0; g_nat.rearm_at = 0; g_nat.away_since = 0;
    std::fprintf(stderr, "[net] native room %llu: 2 players, rollback link opening as %s (try %d), build %08x\n", (unsigned long long)room, owner ? "host (room owner)" : "client", g_nat.tries, g.caps.build);
}
// ENHANCEMENT (version mismatch message): the message line kept after the link closed (conn_close clears it), until the fight is drawn or kVersionMsgMs passed.
constexpr uint64_t kVersionMsgMs = 30000;
std::string g_vmsg; uint64_t g_vmsg_until = 0;
// The open link of a native room, between matches: a failed rendezvous is tried again a little later (3 times per native room); an ended match arms the next one.
void native_maintain(uint64_t now)
{
    if (g.phase.load() != Phase::Ended) { g_nat.rearm_at = 0; return; }
    bool ready, vm = false; std::string v1, v2;   // ENHANCEMENT (version mismatch message): the failure's notice, which conn_close clears
    { std::lock_guard<std::mutex> lk(g.pump.mutex()); ready = g.plan_ready; if (g.rrz) { vm = g.rrz->version_mismatch(); v1 = g.rrz->notice1(); v2 = g.rrz->notice2(); } }
    if (!ready) {   // the two games did not meet over the rollback room (the reason is in the log and on the menu notice)
        const uint64_t room = g_nat.room;
        g_nat.link = false; g_nat.room = 0;
        conn_close();
        g_nat.seen_room = room; g_nat.seen_since = now;
        if (!v1.empty()) g_rdv.failed(v1, v2);   // the menu notice comes back (a retry's waiting text replaces it)
        if (vm) {   // another version: no retry can help; the message line comes back too
            g_nat.given_up = room; log("native room", "the other player runs another version: no rollback in this room");
            show(v1); g_vmsg = v1; g_vmsg_until = now + kVersionMsgMs;
        }
        else if (g_nat.tries >= 3) { g_nat.given_up = room; log("native room", "the rollback link could not be made 3 times: no rollback in this room"); }
        else g_nat.retry_at = now + 5000;
        return;
    }
    if (!g_nat.rearm_at) { if (g.last_end_ok) g_nat.fails = 0; else g_nat.fails++; g_nat.rearm_at = now + (g.last_end_ok ? 300 : 2000); return; }
    if (now < g_nat.rearm_at) return;
    g_nat.rearm_at = 0;
    if (g_nat.fails >= 5) { const uint64_t room = g_nat.room; native_close("5 matches in a row ended badly on this link"); g_nat.given_up = room; return; }
    match_arm(std::max(g.match_k + 1, g.match_jump.load()));
}
void native_poll()
{
    if (!g_nat.on) return;
    const uint64_t now = GetTickCount64();
    if (now < g_nat.next_poll) return;
    g_nat.next_poll = now + 100;
    if (g_vmsg_until && (now >= g_vmsg_until || g_hud_idle == 0)) { g_vmsg_until = 0; if (g.shown == g_vmsg) show(""); }   // the version mismatch line: until the fight or its time
    uint64_t room = 0; int members = 0, max = 0, own = 0;
    const int st = ps3_np_matching2_room(&room, &members, &max, &own, nullptr, 0);
    if (g_nat.link && g.phase.load() == Phase::Off && !g.mux.is_open()) {   // closed by someone else (jsp_net_lobby_close): not reopened in this room
        log("native room", "the rollback link was closed by the lobby API: not reopened in this room");
        g_nat.given_up = g_nat.room; g_nat.link = false; g_nat.room = 0; g_nat.away_since = 0; g_nat.rearm_at = 0;
        return;
    }
    if (g_nat.link) {
        if (st == 2 && room == g_nat.room && members >= 2) g_nat.away_since = 0;
        else if (!g_nat.away_since) g_nat.away_since = now;
        // FIX (a brief room-view gap ended a live fight): during a running match a brief matching2 reading (a re-psn hiccup) must not end a live fight: 10 s there (the rollback link's own
        // disconnect timer ends a match whose peer is really gone first); 1 s between matches, as before.
        const uint64_t away_ms = g.phase.load() == Phase::Running ? 10000 : 1000;
        if (g_nat.away_since && now - g_nat.away_since >= away_ms) { native_close(st != 2 ? "this game left the native room" : room != g_nat.room ? "this game is in another native room" : "the other player left the native room"); return; }
        if (!g_nat.away_since) native_maintain(now);
        return;
    }
    if (g.phase.load() != Phase::Off || g.mux.is_open()) return;   // a link of the lobby API: not ours
    if (st != 2 || members != 2 || !room || room == g_nat.given_up) { if (st != 2 || room != g_nat.seen_room) g_nat.seen_room = 0; return; }
    if (room != g_nat.seen_room) { g_nat.seen_room = room; g_nat.seen_since = now; g_nat.tries = 0; g_nat.retry_at = 0; return; }
    if (now - g_nat.seen_since < 500 || now < g_nat.retry_at) return;   // two members for half a second (a join in progress settles)
    if (g_hud_idle < kHudIdleFrames) return;   // never during a fight: the open resets the fight hash (jsp_net_lobby.h contract)
    native_open(room, own != 0);
}
// A newer match of the other side was seen (the pump's match_peek): follow it while this side is armed or waiting. An ended match follows only on a link that
// re-arms by itself (native); a start-up or lobby link that ended stays ended (unchanged) unless jsp_net_match_begin arms it.
void match_poll()
{
    const uint32_t j = g.match_jump.load();
    if (j <= g.match_k) return;
    const Phase ph = g.phase.load();
    if (ph == Phase::Off || ph == Phase::Running || (ph == Phase::Ended && !g_nat.link)) return;
    if (ph == Phase::Ended && g_nat.away_since) return;   // leaving the native room: native_poll closes the link
    log("rematch", "the other player is at match " + std::to_string(j) + " (this side " + std::to_string(g.match_k) + "): following");
    if (match_arm(j)) g_nat.rearm_at = 0;
}

}  // namespace

extern "C" int jsp_mods_net_build(uint32_t stock, uint32_t* out);   // jsp_mods.cpp: the build word with the mod set folded in; 0 = refused

extern "C" void jsp_net_game_install(void)
{
    if (!env_on("JSP_ROLLBACK")) return;
    // ENHANCEMENT (mods, D40): two peers must run the same gameplay mod set. The set's digest is folded into the build word both sides
    // compare (Hello/Propose BuildMismatch, the re-psn room filter); with no gameplay digest it is exactly build_id(). Gameplay mods that
    // were listed but not all applied refuse rollback here, before any socket opens (jsp_mods.cpp logs why).
    uint32_t net_build = 0;
    if (!jsp_mods_net_build(build_id(), &net_build)) { std::fprintf(stderr, "[net] rollback refused: %s: off\n", jmods::kModsNotAsListed); return; }
    g_net_build = net_build;
    {   // ENHANCEMENT (rollback bench): no socket, no handshake; armed at once, the first fight tick starts the local stress session (start_bench)
        char bb[8] = {};
        if (jsp_setting_get(jpno::kKeyBench, bb, sizeof bb) && jpno::flag_on(bb)) {
            g.bench = jpno::bench_depth(bb);
            if (!g.bench) { std::fprintf(stderr, "[net] JSP_ROLLBACK_BENCH must be 1 to 8: off\n"); return; }
            g.phase = Phase::Armed;
            load_feed();
            install_all(std::make_integer_sequence<int, sizeof k_ents / sizeof k_ents[0]>());
            SceneTimer::install();
            std::fprintf(stderr, "[net] rollback bench armed: depth %d, build %08x, hooks installed (play a fight against the CPU)\n", g.bench, net_build);
            return;
        }
    }
    if (g_boot_lobby.load()) {   // ENHANCEMENT (lobby): rollback-capable, no session: the hooks only (Phase::Off); jsp_net_lobby_open opens a session at runtime
        install_process_wide();
        {   // ENHANCEMENT (native-room pairing): on unless JSP_ROLLBACK_NATIVE is 0 (only here: the start-up role paths and the bench never pair with the game's room)
            char nb[8] = {};
            g_nat.on = !(jsp_setting_get("JSP_ROLLBACK_NATIVE", nb, sizeof nb) && nb[0] && !jpno::flag_on(nb));
            // FIX (EU disc guard): the native fight takeover knows the US 1.00 addresses only; other discs get the menu notice instead of a broken pairing
            if (g_nat.on && !jsp_native_fight_supported()) { g_nat.on = false; note_off(jpno::Refusal::NativeUsOnly); }
            std::fprintf(stderr, "[net] native-room pairing: %s (JSP_ROLLBACK_NATIVE)\n", g_nat.on ? "on: a rollback link opens when the game's own Online Battle room has 2 players" : "off");
        }
        std::fprintf(stderr, "[net] rollback lobby: build %08x, hooks installed, no session until the lobby opens one\n", net_build);
        std::fprintf(stderr, "[net] status badge: %s\n", g.badge_on ? "on" : "off");
        return;
    }
    // The start-up role (play_settings.txt keys, validated before main() by jpno::resolve_env) -> the same connection code as the lobby (conn_open).
    char b[96] = {};
    const bool have_listen = jsp_setting_get("JSP_ROLLBACK_LISTEN", b, sizeof b) && b[0];
    char j[96] = {};
    const bool have_join = jsp_setting_get("JSP_ROLLBACK_JOIN", j, sizeof j) && j[0];
    char via[24] = {}, role[24] = {}, relay[8] = {};
    Conn c;
    c.repsn = jsp_setting_get(jpno::kKeyVia, via, sizeof via) && jps::ieq(via, "repsn");
    if (c.repsn) {
        jsp_setting_get(jpno::kKeyRole, role, sizeof role);
        c.host = jps::ieq(role, "host");
        c.relay = jsp_setting_get(jpno::kKeyRelay, relay, sizeof relay) && jpno::flag_on(relay);
        c.port = have_listen ? (uint16_t)std::atoi(b) : 0;   // a fixed UDP port is optional (firewalls); 0 = any
        char rm[32] = {};   // ENHANCEMENT (Discord Join): JSP_ROLLBACK_ROOM = that room, not the oldest
        if (!c.host && jsp_setting_get("JSP_ROLLBACK_ROOM", rm, sizeof rm) && rm[0]) c.room = std::strtoull(rm, nullptr, 10);
    } else {
        if (have_listen == have_join) { std::fprintf(stderr, "[net] BUG: the start decision (jpno::resolve_env) let JSP_ROLLBACK=1 through without exactly one role key: off\n"); return; }   // unreachable: a lobby or refused before main()
        c.host = have_listen;
        if (c.host) c.port = (uint16_t)std::atoi(b);
        else if (!parse_endpoint(j, c.join)) { std::fprintf(stderr, "[net] JSP_ROLLBACK_JOIN is not IP:PORT: off\n"); note_off(jpno::Refusal::BadJoin); return; }
    }
    std::string why; jpno::Refusal code = jpno::Refusal::None;
    if (!conn_open(c, &why, &code)) { std::fprintf(stderr, "[net] %s: off\n", why.c_str()); note_off(code); conn_close(); return; }   // nothing installed, as before
    install_process_wide();
    std::fprintf(stderr, "[net] rollback opt-in: %s, build %08x, hooks installed\n", g.host ? "host" : "client", g.caps.build);
    std::fprintf(stderr, "[net] status badge: %s\n", g.badge_on ? "on" : "off");
}

// ---- ENHANCEMENT (lobby): the runtime API (jsp_net_lobby.h). Main thread only. ----------------------------------------------------------------------------------
extern "C" int jsp_net_lobby_open(const JspLobbyCfg* cfg)
{
    auto refuse = [](const std::string& w) { g_lobby_why = w; std::fprintf(stderr, "[net] lobby open refused: %s\n", w.c_str()); return 0; };
    if (!g_installed) return refuse(g.bench ? "the rollback bench is running (JSP_ROLLBACK_BENCH)" : "rollback is not on in this session (JSP_ROLLBACK=1 at start, not refused)");
    if (!cfg) return refuse("no configuration");
    const Phase ph = g.phase.load();
    if (ph == Phase::Handshake || ph == Phase::Armed || ph == Phase::Running) return refuse("a session is already open: close it first");
    if ((cfg->role != JSP_LOBBY_HOST && cfg->role != JSP_LOBBY_JOIN) || (cfg->via != JSP_LOBBY_DIRECT && cfg->via != JSP_LOBBY_REPSN)) return refuse("unknown role or connection");
    Conn c; c.host = cfg->role == JSP_LOBBY_HOST; c.repsn = cfg->via == JSP_LOBBY_REPSN; c.relay = cfg->allow_relay != 0; c.port = cfg->port;
    if (!c.repsn) {
        if (c.host && !c.port) return refuse("the host needs a UDP port from 1 to 65535");
        if (!c.host && (!cfg->join || !jpno::valid_join(cfg->join) || !parse_endpoint(cfg->join, c.join))) return refuse("the host address is IP:port, for example 192.0.2.10:36200");
    } else if (!c.host) c.room = cfg->room;
    // ENHANCEMENT (mods, D40): a gameplay archive copy opened since start that differs: no new session (the pump checks it again before the Hello)
    if (const char* w = jmods::rollback_gate(jsp_mods_copy_mismatch() != 0)) return refuse(std::string("rollback refused: ") + w);
    if (ph == Phase::Ended || g.mux.is_open()) conn_close();   // an ended match's link (or a start-up one) is replaced
    if (g_nat.link) { g_nat.given_up = g_nat.room; g_nat.link = false; g_nat.room = 0; g_nat.away_since = 0; g_nat.rearm_at = 0; }   // ENHANCEMENT (native-room pairing): the caller's link replaces ours
    reset_match();   // includes the fight hash reset: the session arms on the NEXT fight's anchor (see the contract in jsp_net_lobby.h)
    std::string why; jpno::Refusal code = jpno::Refusal::None;
    if (!conn_open(c, &why, &code)) { conn_close(); return refuse(why); }
    g_lobby_why.clear();
    std::fprintf(stderr, "[net] lobby open: %s over %s, build %08x\n", g.host ? "host" : "client", c.repsn ? "re-psn" : "direct IP", g.caps.build);
    return 1;
}

extern "C" void jsp_net_lobby_close(void)
{
    const Phase ph = g.phase.load();
    if (ph == Phase::Off && !g.mux.is_open() && !g.ended_ms) return;
    if (g.bench) return;   // the bench has no connection; it ends with F9
    if (ph == Phase::Running) { g.sess.abort("left the lobby"); end_with("Left the lobby", true); }   // the peer is told by the session's disconnect
    else if (ph == Phase::Handshake || ph == Phase::Armed) {
        {   // the peer is told by a handshake Cancel (host) or leave (client); before the plan exists there is nobody to tell (the re-psn room is left by conn_close)
            std::lock_guard<std::mutex> lk(g.pump.mutex());
            if (g.plan_ready) { if (g.host && g.mh) g.mh->cancel(); else if (!g.host && g.mc) g.mc->leave(); }
        }
        if (g.plan_ready) Sleep(100);   // ASSUMPTION: one pump pass (10 ms) and a retransmit chance are enough for that message to leave before the socket closes
    }
    log("lobby closed", "");
    conn_close();
}

extern "C" const char* jsp_net_lobby_reason(void) { return g_lobby_why.c_str(); }
extern "C" int jsp_net_lobby_phase(void) { return (int)g.phase.load(); }

// ---- ENHANCEMENT (same-link rematch): the match API (jsp_net_lobby.h). Main thread only. ----------------------------------------------------------------------------
extern "C" int jsp_net_match_begin(void)
{
    if (!g_installed || g.bench) return 0;
    const Phase ph = g.phase.load();
    if (ph == Phase::Off || ph == Phase::Running) return 0;
    bool ready; { std::lock_guard<std::mutex> lk(g.pump.mutex()); ready = g.plan_ready; }
    if (!ready) return ph == Phase::Handshake ? 1 : 0;   // still meeting the other player: its first match arms by itself; an ended (failed) meeting has no link
    if (ph == Phase::Ended) { if (!match_arm(std::max(g.match_k + 1, g.match_jump.load()))) return 0; g_nat.rearm_at = 0; }
    if (g.fresh_gate) { g.fresh_gate = false; jsp_fight_hash_reset_fight(); g.hud_seen = 0; g.hud_seen_set = false; }   // the caller's contract: from the menus, the last fight is gone
    return 1;
}
extern "C" void jsp_net_match_end(void)
{
    if (!g_installed || g.bench) return;
    const Phase ph = g.phase.load();
    if (ph == Phase::Running) { g.sess.abort("match ended"); end_with("Match ended", true); return; }   // the other player is told by the session's disconnect
    if (ph != Phase::Handshake && ph != Phase::Armed) return;
    {
        std::lock_guard<std::mutex> lk(g.pump.mutex());
        if (!g.plan_ready) return;   // still meeting the other player: there is no match to end yet
        if (g.host && g.mh) g.mh->cancel(); else if (!g.host && g.mc) g.mc->leave();   // the other side is told (handshake Cancel / leave)
    }
    g.phase = Phase::Ended; g.last_end_ok = true;
    log("match ended", "before it started (jsp_net_match_end); the link stays");
}
extern "C" unsigned jsp_net_match_index(void) { return g.match_k; }

// Called by jsp_online.cpp's boot when the online files and --psnr have been applied; EnvInit reports through the same rendezvous.
extern "C" void jsp_net_env_part(int part) { g_rv.arrive(part, decide_env); }

// ENHANCEMENT (display only): the two lines of the refused-rollback notice (jpno::notice_text), else of the re-psn rendezvous notice (waiting / why the games did not meet),
// for jsp_port_menu.cpp. 1 = there is one, 0 = none (rollback not requested, or it started and the rendezvous is not waiting or failed). Any thread; the code is written before main() or by jsp_net_game_install, before the menu exists.
extern "C" int jsp_net_notice(char* line1, int n1, char* line2, int n2)
{
    const int c = g_refusal.load();
    if (c <= 0 || c >= static_cast<int>(jpno::Refusal::Count)) {   // not refused: the re-psn rendezvous notice, if any (waiting, or why the games did not meet)
        std::string a, b;
        if (!g_rdv.read(a, b)) return 0;
        std::snprintf(line1, (size_t)n1, "%s", a.c_str()); std::snprintf(line2, (size_t)n2, "%s", b.c_str());
        return 1;
    }
    const jpno::Notice t = jpno::notice_text(static_cast<jpno::Refusal>(c), g_refusal_src.load());
    if (!t.any()) return 0;
    std::snprintf(line1, (size_t)n1, "%s", t.line1.c_str()); std::snprintf(line2, (size_t)n2, "%s", t.line2.c_str());
    return 1;
}

// Changes whenever the rendezvous notice text changes (jsp_port_menu.cpp re-reads jsp_net_notice while the top list is up). Any thread.
extern "C" unsigned jsp_net_notice_gen(void) { return g_rdv.gen(); }
// ENHANCEMENT (Discord Rich Presence, jsp_discord.cpp): 1 while an online rollback match is running (not the one-machine bench), with its player count
// and whether this copy hosts. Racy plain reads from the presence thread; a stale value shows for one 2 s update at most.
// ENHANCEMENT (Discord Join): the re-psn room of this game (0 = none) and its server; any thread.
extern "C" unsigned long long jsp_net_presence_room(char* server, int sn)
{
    const uint64_t r = g.presence_room.load();
    if (server && sn > 0) std::snprintf(server, (size_t)sn, "%s", r ? g.presence_srv : "");
    return r;
}
extern "C" int jsp_net_presence(int* players, int* host)
{
    *players = g.started_ms && !g.bench ? (int)g.plan.total_players : 0; *host = g.host ? 1 : 0;
    return *players > 0 ? 1 : 0;
}

#endif  // JSP_ROLLBACK_BUILD
