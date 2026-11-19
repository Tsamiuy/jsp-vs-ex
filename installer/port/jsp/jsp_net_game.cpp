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

#ifndef JSP_ROLLBACK_BUILD
extern "C" void jsp_net_game_install(void) {}
extern "C" void jsp_net_env_part(int) {}
#else

#include <atomic>
#include <memory>
#include <mutex>
#include <random>
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
#include "jsp_net_repsn_client.h"
#include "jsp_net_glue.h"
#include "jsp_net_m2_link.h"
#include "jsp_session_handshake.h"
#include "jsp_session_reader.h"
#include "jsp_session_manifest.h"

extern "C" {
int jsp_online_repsn_identity(char* server, int sn, char* name, int nn, char* comm, int cn, int* report_rtt);   // jsp_online.cpp: repsn.ini even with enabled=0
extern int (*jsp_session_text_hook)(const char* key, char* out, int n);   // jsp_settings.cpp
int jsp_setting_get(const char* key, char* out, int n);
void jsp_setting_changed(void);   // jsp_settings.cpp: bumps jsp_setting_gen so the cached readers re-read
uint64_t jsp_snapshot_ext_pool_evicted(void);
void jsp_snapshot_ext_set_budget_ms(unsigned ms);   // the commit gate's wall-time budget
void jsp_snapshot_ext_budget_reset(void);           // after every session update: one budget from the last update across all gates
void jsp_snapshot_ext_set_idle_hook(void (*fn)());
void jsp_snapshot_ext_guard_summary(void);
int jsp_snapshot_ext_rsx_write_hazard(void);       // write guard: the previous capture had RSX commands pending and an RSX-side write into saved memory landed after it
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
int jsp_snapshot_ext_rollback(uint32_t k, uint32_t to, ppu_context* c);
void ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));
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
void decide_env()
{
    std::string why;
    const jpno::Boot r = jpno::resolve_env([](const char* k) { return std::getenv(k); },
                                           [](const char* k, const char* v) { _putenv_s(k, v ? v : ""); },   // "" removes the variable on Windows
                                           &g_own_env, &why);
    std::fprintf(stderr, "[net] env decision: %s (static constructors arrived: %s first, %s last)\n",
                 r == jpno::Boot::Go ? "rollback start" : r == jpno::Boot::Refused ? "REFUSED" : "rollback not requested",
                 g_rv.order[0] == 1 ? "EnvInit" : "online boot", g_rv.order[1] == 1 ? "EnvInit" : "online boot");
    if (r == jpno::Boot::Refused) std::fprintf(stderr, "[net] REFUSED: %s\n", why.c_str());
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
    std::string m2_text; uint32_t base = 0;
    NetSession sess; jsx::Stepper stepper; std::unique_ptr<Glue> glue;
    ppu_context* ctx = nullptr; uint32_t done_now = 0;
    uint64_t cancel_since = 0;
    bool skip_sim = false, skip_pad = false, was_replaying = false;
    uint64_t last_run_ms = 0, started_ms = 0, settings_shown_ms = 0;
    std::string shown;
    // ENHANCEMENT (status badge, JSP_ROLLBACK_BADGE=1): main-thread only (boundary, the glue's rollback, end_with). path_word is set once the plan exists.
    bool badge_on = false; std::string badge_shown; jpnb::Values bv; jpnb::Every badge_every, status_every;
    std::atomic<int> path_kind{0};   // 0 unknown, 1 direct, 2 relay: written by the pump thread when the rendezvous ends, read by the main thread
} g;

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

// The glue's overlay layer holds the message line (bottom left) and, when on, the status badge (top left); a change of either redraws both.
void redraw()
{
    const std::string& s = g.shown; const std::string& b = g.badge_shown;
    if (s.empty() && b.empty()) { jsp_overlay_close(); return; }
    if (!jsp_overlay_begin(1280, 720)) return;
    if (!b.empty()) {
        const float bw = jsp_overlay_text_width(b.c_str(), jpnb::kScale);
        jsp_overlay_fill_rect(jpnb::kBoxX, jpnb::kBoxY, (int)(bw > 0 ? bw : 360) + 2 * jpnb::kBoxPad, jpnb::kBoxH, JSP_RGBA(0, 0, 0, 160));
        jsp_overlay_text_style(jpnb::kX, jpnb::kY, b.c_str(), jpnb::kScale, JSP_RGBA(255, 255, 255, 255), 0);
    }
    if (!s.empty()) {
        const float w = jsp_overlay_text_width(s.c_str(), 1.0f);
        jsp_overlay_fill_rect(30, 640, (int)(w > 0 ? w : 800) + 40, 50, JSP_RGBA(0, 0, 0, 190));
        jsp_overlay_text_style(50, 652, s.c_str(), 1.0f, JSP_RGBA(255, 255, 255, 255), 0);
    }
    jsp_overlay_end();
}
void show(const std::string& s) { if (s == g.shown) return; g.shown = s; redraw(); }
void badge(const std::string& b) { if (b == g.badge_shown) return; g.badge_shown = b; redraw(); }
std::string path_word() { const int k = g.path_kind.load(); return k == 1 ? "direct" : k == 2 ? "relay" : ""; }
void log_status() { g.bv.path = path_word(); std::fprintf(stderr, "[net] %s\n", jpnb::log_line(g.bv).c_str()); }

uint64_t rz_rejected() { if (auto* d = dynamic_cast<DirectRendezvous*>(g.rz.get())) return d->rejected_hello; return g.rrz ? g.rrz->rejected_members : 0; }
void log(const char* what, const std::string& s) { std::fprintf(stderr, "[net] %s%s%s\n", what, s.empty() ? "" : ": ", s.c_str()); }
std::string take_text() { std::lock_guard<std::mutex> lk(g.txt_mu); return g.fail_text; }
void set_text(const std::string& t) { std::lock_guard<std::mutex> lk(g.txt_mu); g.fail_text = t; }
// The match is over (session failure, Cancel, disconnect, End match): the local settings come back (handshake end_match / match_ended), the pad hook goes, the fight runs on locally.
void end_with(const std::string& text)
{
    jsp_snapshot_ext_set_idle_hook(nullptr);   // the gate must not call into a finished session
    jsp_snapshot_ext_guard_summary();
    if (g.started_ms) log_status();   // the session ran: its last status line
    badge("");
    set_text(text); g.phase = Phase::Ended; g.skip_sim = g.skip_pad = false;
    if (g.was_replaying || g.stepper.replaying()) jsp_stepper_replay(0, g.done_now);   // an end during a replay: gate, clock and flag off
    g.was_replaying = false;
    jsp_stepper_abort();
    jsp_stepper_net_disable();
    { std::lock_guard<std::mutex> lk(g.pump.mutex()); if (g.host && g.mh) g.mh->end_match(); else if (!g.host && g.mc) g.mc->match_ended(); }
    log("ended", text); show(text);
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
void pump_fail(const std::string& t) { log("settings layer / rendezvous ended", t); { std::lock_guard<std::mutex> lk(g.txt_mu); if (!g.m2_ended.load()) g.m2_text = t; } g.m2_ended = true; }   // the main thread acts on it (boundary)

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
    if (g.rp) g.rp->pump();   // re-psn: from the very first tick, BEFORE the startup-evidence wait (up to 240 s): the server drops a client that does not poll for 90 s (heartbeat inside psnr_poll); this thread is the only one that calls the psnr client
    if (!g.evidence_ready.load()) {   // the handshake belongs after boot: Startup-field evidence (renderer pipe state, installed render60 mode) must exist when a proposal is checked
        static uint64_t first = 0, next = 0;
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
        static uint64_t next = 0;
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
        static int last = -2;
        const int cur = g.plan_ready ? (int)(g.host ? g.mh->state() : g.mc->state()) : -1;
        if (cur != last) { last = cur; std::fprintf(stderr, "[net] handshake state -> %d (%s)\n", cur, g.host ? "host" : "client"); }
    }
    if (!g.plan_ready) {
        const int r = g.rz->poll();
        if (r < 0) { pump_fail("rendezvous failed: " + g.rz->error()); return; }
        if (r == 0) return;
        g.plan = g.rz->plan();
        g.link->set_peer(g.host ? 1 : 0, g.plan.remotes[0].ep);
        if (g.host) {
            g.mh.reset(new jpsm::Host(*g.link, g.ev, g.caps, 0));
            if (g.mh->begin(g.plan.session_id, 0b11, g.plan.session_id ^ 0x5eedull, t) != jpsm::Reason::None) { pump_fail("settings handshake: cannot begin"); return; }
        } else {
            g.mc.reset(new jpsm::Client(*g.link, g.ev, g.caps, 1));
            if (g.mc->join(g.plan.session_id, 0, t) != jpsm::Reason::None) { pump_fail("settings handshake: cannot join"); return; }
        }
        g.plan_ready = true;
        log("rendezvous", to_string(g.plan.remotes[0].ep) + (g.rrz ? std::string(" via re-psn, path ") + path_name(g.rrz->path()) + ", room " + std::to_string((unsigned long long)g.rrz->room_id()) : std::string()));
        if (g.rrz) g.path_line = std::string("re-psn: ") + path_name(g.rrz->path()) + (g.rrz->path() == Path::Relay ? " (through the server)" : "");
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
    } else {
        g.mc->tick(t);
        const jpsm::State st = g.mc->state();
        if (jpsm::terminal(st) && st != jpsm::State::Ended) pump_fail(std::string("match cancelled: ") + jpsm::explain(g.mc->reason()) + " (field " + jpsm::rule(g.mc->bad_id()).name + ")");
        if (st == jpsm::State::Started) g.m2_started = true;
    }
}

// ---- the game side of the glue ----------------------------------------------------------------------------------------------------------------------------
class GameGlue : public IGlueGame {
public:
    uint32_t done() override { return g.done_now; }
    bool commit(uint32_t tick, uint32_t* hash, uint32_t* token) override
    {
        const int r = jsp_snapshot_ext_commit(tick, g.ctx, hash);
        if (r == 0) { log("snapshot commit failed", std::to_string(tick)); return false; }
        if (r == 2) { log("snapshot layout changed", "older rollback targets are gone: ending the session"); return false; }
        const uint32_t raw = *hash;
        if (raw == 0) { log("no per-tick hash (the fight hash tap gave 0): the session cannot check state", std::to_string(tick)); return false; }   // raw 0 is never mixed or hidden
        if (jsp_snapshot_ext_rsx_write_hazard()) { log("RSX wrote into guest memory with a commit pending (JSP_NET_RSX_WRITE_GUARD=0 turns the guard off)", std::to_string(tick)); return false; }
        if (jsp_snapshot_ext_pool_evicted() != 0) { log("snapshot ring pool too short: an entry was evicted early", std::to_string(tick)); return false; }
        *hash = raw ^ (tick * 2654435761u);   // the tick number is part of the checksum: peers whose base ticks differ desync at once
        if (*hash == 0) *hash = 1;
        *token = ++serial_;
        std::fprintf(stderr, "[net] commit tick=%u hash=%08x raw=%08x\n", tick, *hash, raw);   // a two-instance test compares the LAST commit line per tick across the two peers
        return true;
    }
    bool rollback(uint32_t tick) override
    {
        if (jsp_snapshot_ext_rollback(g.done_now, tick, g.ctx) != 1) { log("snapshot rollback failed", std::to_string(tick)); return false; }
        jpnb::note_rollback(g.bv, (int64_t)g.done_now - (int64_t)tick);   // the ticks gone back and run again (status badge and status line)
        g.done_now = tick;
        std::fprintf(stderr, "[net] rollback to tick=%u (longest game-thread stretch without a session call since the last rollback: %u ms, keepalive polls so far: %llu)\n", tick, g.sess.take_max_gap_ms(), (unsigned long long)g.sess.keepalive_polls());
        return true;
    }
    void local_pad(uint8_t, uint8_t wire[8]) override
    {
        uint8_t canon[8];
        if (!jsp_stepper_local_pad(0, canon)) { jsi::Pad n; n.connected = true; const auto b = n.pack(); for (int i = 0; i < 8; i++) canon[i] = b[i]; }
        pad_pack_to_wire(canon, wire);   // the device is physical port 0 on every machine; the table slot it feeds is the plan's local slot
    }
private:
    uint32_t serial_ = 100;
} g_game_glue;

// WORKAROUND TO REPLACE (a bound, not a cause): the commit gate waits for the frame pipeline threads to reach their park points; a slow frame (a cold pipeline build took 4.4 s on a client and
// 14 s on a host in two-copy tests) keeps them away. The session network is polled while the gate waits (idle hook); every other long stretch on the game thread (a replayed
// frame, flips, vblank acks, a pipeline build before the boundary) is NOT covered by the hook: NetSession's keepalive thread polls whenever the game thread has not (GekkoNet's Poll
// sends the 250-500 ms health ping the peer's disconnect timer counts). This constant is only the point where a gate that never clears is declared hung. Warm pipeline caches shorten the frames.
constexpr unsigned kGateBudgetMs = 15000;

void start_session(ppu_context* c)
{
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
    std::string why;
    if (!jsp_snapshot_ext_active()) why = "the snapshot ring is not active (JSP_SNAPSHOT_EXT/WW did not start)";
    else if (!g.sess.start(g.mux, g.plan, cfg)) why = "session refused: " + g.sess.error();
    if (!why.empty()) {   // a refusal becomes a handshake Cancel with a reason, so the other side learns why (and the layer is removed on both)
        { std::lock_guard<std::mutex> lk(g.pump.mutex()); if (g.host) g.mh->cancel(jpsm::Reason::SessionRefused); else g.mc->abort(jpsm::Reason::SessionRefused); }
        end_with(std::string("Rollback session could not start: ") + why);   // (the handshake Cancel above already told the other side)
        return;
    }
    g.stepper.begin(g.done_now, kRingDepth);
    g.stepper.table().set_mask((1u << g.plan.total_players) - 1);
    jsp_stepper_net_enable(&g.stepper.table());
    g.glue.reset(new Glue(g.sess, g.stepper, g_game_glue));
    { std::lock_guard<std::mutex> lk(g.pump.mutex()); if (g.host) g.mh->handover(); else g.mc->handover(); }   // the rollback session owns liveness from here
    g.phase = Phase::Running; g.last_run_ms = GetTickCount64(); g.started_ms = g.last_run_ms;
    log("session started", "base tick " + std::to_string(g.done_now) + " slots " + std::to_string(g.plan.local_first) + " of " + std::to_string(g.plan.total_players));
    show("Connecting to opponent...");
}

// The cancel / end-match key is F9 (Esc quits the game: patch 2500). Held for 1.5 s while a match runs; a press while waiting.
bool window_focused() { DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid); return pid == GetCurrentProcessId(); }
bool cancel_key() { return window_focused() && (GetAsyncKeyState(VK_F9) & 0x8000) != 0; }
void ui_and_cancel()
{
    static bool was = false;
    const bool k = cancel_key(), edge = k && !was; was = k;
    const Phase ph = g.phase;
    if (ph == Phase::Handshake || ph == Phase::Armed) {
        std::lock_guard<std::mutex> lk(g.pump.mutex());
        if (g.host) {
            if (edge && g.plan_ready) { g.mh->cancel(); g.phase = Phase::Ended; set_text("Cancelled: local settings restored"); show(take_text()); return; }
            if (ph == Phase::Handshake && g.plan_ready) { show("Waiting for client... (F9 cancels)"); return; }   // only until Start
        } else if (g.plan_ready) {
            const auto hw = g.mc->host_wait(now_ms());
            const jpsm::State st = g.mc->state();
            if (edge) { g.mc->leave(); g.phase = Phase::Ended; set_text("Left the session: local settings restored"); show(take_text()); return; }
            if (g.mc->applied() && g.ev.active()) {
                if (!g.settings_shown_ms) g.settings_shown_ms = GetTickCount64();
                if (GetTickCount64() - g.settings_shown_ms < 6000) { show("Settings received: controlled by host"); return; }
            }
            if (hw.waiting && st != jpsm::State::Started) { show("Waiting for host... (" + std::to_string(hw.waited_ms / 1000) + " s, F9 cancels)"); return; }
        }
        if (ph == Phase::Armed) show("");
    }
    if (ph == Phase::Ended) { const std::string t = take_text(); if (!t.empty()) show(t); }
}

// ---- the boundary: BeginFrame entry, once per main-loop iteration ------------------------------------------------------------------------------------------------
void boundary(ppu_context* c)
{
    JspTickState ts; jsp_fight_hash_get_state(&ts);
    g.ctx = c; g.done_now = ts.done;
    Phase ph = g.phase;
    if (ph == Phase::Off) return;
    if (g.m2_ended.load() && ph != Phase::Ended) {   // a Cancel / failure of the settings layer while waiting, armed or in the match: the match ends on this side too
        std::string t; { std::lock_guard<std::mutex> lk(g.txt_mu); t = g.m2_text; }
        if (ph == Phase::Running) g.sess.abort("settings layer ended: " + t);
        end_with(t); return;
    }
    if (ph == Phase::Handshake && g.m2_started.load()) { g.phase = Phase::Armed; ph = Phase::Armed; log("phase", "armed (settings agreed, waiting for the first fight tick)"); }
    // done >= 1 already implies the camera anchor (g_done counts only after it is set; the hash tap needs the cameras), so tick `done` has a raw hash. ts.anc_set is the PRESS anchor and is never set here.
    if (ph == Phase::Armed && ts.done >= 1) { start_session(c); ph = g.phase; if (ph != Phase::Running) return; }
    else if (ph != Phase::Running) { ui_and_cancel(); return; }
    if (cancel_key()) {   // End match: F9 held 1.5 s
        if (!g.cancel_since) g.cancel_since = GetTickCount64();
        if (GetTickCount64() - g.cancel_since > 1500) { g.sess.abort("match ended by the player"); end_with("Match ended"); return; }
    } else g.cancel_since = 0;
    if (g.path_until && GetTickCount64() > g.path_until) { g.path_until = 0; if (g.shown == g.path_line) show(""); }   // never blanks a later message (a reconnect or an end line)
    const uint64_t u0 = g.glue->updates();
    const jsx::Op op = g.glue->iteration(true);   // update is called whenever the stepper has nothing queued (skipping it for pacing stalled both peers in a two-copy test)
    if (g.glue->updates() != u0) { jsp_snapshot_ext_budget_reset(); if (const uint32_t ms = g.sess.pace_sleep_ms(33)) Sleep(ms); }
    else g.sess.poll();   // replay / regrant / stall iterations: the network is still serviced
    for (const SessionEvent& e : g.sess.take_events()) {
        switch (e.kind) {
        case SessionEventKind::Started: log("gekko session synchronized", ""); if (!g.path_line.empty()) { show(g.path_line); g.path_until = GetTickCount64() + 6000; } else show(""); break;
        case SessionEventKind::Disconnected:   // GekkoNet fires it after the disconnect timeout = the grace window: the match is over
            { std::string why; if (m2_cancelled_within(300, &why)) { g.sess.abort(why); end_with(why); return; } }   // the peer's handshake Cancel (a local failure there) names the reason
            g.sess.abort("opponent disconnected"); end_with("Opponent disconnected"); return;
        case SessionEventKind::Desync:   // includes a base-tick mismatch: the per-tick hash carries the tick number
            std::fprintf(stderr, "[net] DESYNC at tick %u (stepper tick numbering, session-relative; add the base %u): local=%08x remote=%08x\n", (unsigned)e.tick, g.base, e.local_hash, e.remote_hash);
            if (e.tick <= 3) std::fprintf(stderr, "[net] base mismatch suspected: the desync is at the first compared ticks; the two peers probably armed at different ticks (local base %u; the base is part of the per-tick hash, it is not exchanged)\n", g.base);
            g.sess.abort("desync detected"); end_with("Desync detected: match ended"); return;
        default: break;
        }
    }
    if (g.sess.failed()) { send_failure_cancel(); end_with("Rollback session ended: " + g.sess.failure()); return; }
    const uint64_t now = GetTickCount64();
    if (op.kind == jsx::OpKind::Run) {
        g.skip_sim = false; g.skip_pad = op.regrant;
        if (!op.regrant) g.last_run_ms = now;
        if (g.shown.rfind("Reconnecting", 0) == 0) show("");
    } else {
        g.skip_sim = true; g.skip_pad = true;   // no tick may run: skip phases 1, 2 and the pad poll (a render-only iteration) and keep presenting
        if (g.sess.synced() && now - g.last_run_ms > 1000)
            show("Reconnecting... " + std::to_string((now - g.last_run_ms) / 1000) + " s (F9 held: end match)");
    }
    const bool rep = g.stepper.replaying();
    if (g.was_replaying && !rep) jsp_stepper_replay(0, g.done_now);   // the replay's last tick is done: fast clock and effect gate off
    g.was_replaying = rep;
    if (g.badge_every.due(now, jpnb::kRefreshMs)) {   // the status badge: at most four times a second; drawn again if another layer user closed the layer
        g.bv.path = path_word();
        if (g.badge_on) { const std::string b = jpnb::text(g.bv); if (b != g.badge_shown || !jsp_overlay_active()) { g.badge_shown = b; redraw(); } }
    }
    if (g.status_every.due(now, jpnb::kLogMs) && now - g.started_ms >= 1000) log_status();   // not at the session's first iteration: the line at the end covers a short one
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
        orig(c);
    }
    static void install()
    {
        if (!jsp_hook_chain::install(k_ents[I].addr, &run, &orig)) std::fprintf(stderr, "[net] no lifted function at %08X (%s)\n", k_ents[I].addr, k_ents[I].name);
    }
};
template <int I> fn_t Hook<I>::orig;
template <int... Is> void install_all(std::integer_sequence<int, Is...>) { (Hook<Is>::install(), ...); }

}  // namespace

extern "C" void jsp_net_game_install(void)
{
    if (!env_on("JSP_ROLLBACK")) return;
    char b[96] = {};
    const bool have_listen = jsp_setting_get("JSP_ROLLBACK_LISTEN", b, sizeof b) && b[0];
    char j[96] = {};
    const bool have_join = jsp_setting_get("JSP_ROLLBACK_JOIN", j, sizeof j) && j[0];
    char via[24] = {}, role[24] = {}, relay[8] = {};
    const bool via_repsn = jsp_setting_get(jpno::kKeyVia, via, sizeof via) && jps::ieq(via, "repsn");
    std::string err;
    RepsnIdentity rid; bool repsn_host = true, repsn_relay = false;
    if (via_repsn) {   // ENHANCEMENT (rollback over re-psn): a room on the player's re-psn server arranges the session; our UdpMux socket is the P2P socket
        jsp_setting_get(jpno::kKeyRole, role, sizeof role);
        repsn_host = jps::ieq(role, "host");
        repsn_relay = jsp_setting_get(jpno::kKeyRelay, relay, sizeof relay) && jpno::flag_on(relay);
        char srv[160] = {}, nm[64] = {}, cid[24] = {}; int rep_rtt = 0;
        if (!jsp_online_repsn_identity(srv, sizeof srv, nm, sizeof nm, cid, sizeof cid, &rep_rtt) || !parse_server(srv, rid.host, rid.port) || !nm[0]) {
            std::fprintf(stderr, "[net] re-psn: no server / online name in repsn.ini (set them on the Online page under Delay): off\n"); return;
        }
        if (std::strlen(nm) > 16) { std::fprintf(stderr, "[net] re-psn: the online name '%s' is longer than 16 characters (repsn.ini name=): off\n", nm); return; }
        rid.name = nm; if (cid[0]) rid.comm_id = cid; rid.report_rtt = rep_rtt != 0;
        g.host = repsn_host;
        g.listen_port = have_listen ? (uint16_t)std::atoi(b) : 0;   // a fixed UDP port is optional (firewalls); 0 = any
    } else {
        if (have_listen == have_join) { std::fprintf(stderr, "[net] BUG: the start decision (jpno::resolve_env) let JSP_ROLLBACK=1 through without exactly one role key: off\n"); return; }   // unreachable: refused before main()
        g.host = have_listen;
        if (g.host) g.listen_port = (uint16_t)std::atoi(b);
        else if (!parse_endpoint(j, g.join)) { std::fprintf(stderr, "[net] JSP_ROLLBACK_JOIN is not IP:PORT: off\n"); return; }
    }
    if (!g.mux.open(g.host ? g.listen_port : (via_repsn ? g.listen_port : 0), &err)) { std::fprintf(stderr, "[net] cannot open the UDP socket: %s: off\n", err.c_str()); return; }
    // F9 is the cancel key: a VK 0x78 binding (settings pages save PAD_KEY_* as 0x78 / decimal) is dropped from the environment the pad layer reads.
    for (char** e = environ; e && *e; ++e) {
        if (std::strncmp(*e, "PAD_KEY_", 8) != 0) continue;
        const char* eq = std::strchr(*e, '='); if (!eq) continue;
        char* end = nullptr; const long vk = std::strtol(eq + 1, &end, 0);
        if (end != eq + 1 && vk == 0x78) { const std::string name(*e, (size_t)(eq - *e)); _putenv_s(name.c_str(), ""); std::fprintf(stderr, "[net] binding %s = F9 dropped (F9 is the rollback cancel key)\n", name.c_str()); e = environ - 1; }
    }
    g.caps.build = build_id();
    { char bb[16] = {}; g.badge_on = jsp_setting_get(jpnb::kKey, bb, sizeof bb) && jpnb::key_on(bb); }
    // local preferences into the effective-value object (the proposal of a host starts from these; a client's own layer sits under the host's)
    for (size_t i = 0; i < jpsm::kCount; ++i) {
        const jpsm::Id id = static_cast<jpsm::Id>(i);
        char t[64]; uint32_t v;
        if (jsp_setting_get(jpsm::rule(id).name, t, sizeof t) && t[0] && text_to_canon(id, t, &v) && jpsm::in_range(id, v)) g.ev.set_local(id, v);
    }
    g.ev.set_local(jpsm::Id::Rollback, 1);
    jsp_session_text_hook = session_text;
    g.hub.reset(new MessageHub(g.mux));
    if (via_repsn) {
        g.rp.reset(new PsnrClientTransport(g.mux, rid));
        RePsnRendezvous::Config rc; rc.host = repsn_host; rc.build = g.caps.build; rc.allow_relay = repsn_relay; rc.local_count = 1;
        g.rrz = new RePsnRendezvous(*g.rp, g.mux, *g.hub, rc);
        g.rrz->on_path = [](Path p, uint32_t peer_user) {   // runs inside rz->poll() (the pump thread)
            g.rp->set_peer_user(peer_user);
            g.rp->set_relay_mode(p == Path::Relay);   // Relay: every datagram to the peer goes through the server from now on, until the session ends
            std::fprintf(stderr, "[net] re-psn path: %s\n", path_name(p));
        };
        g.rz.reset(g.rrz);
        g.rp->start(repsn_relay);
    } else g.rz.reset(g.host ? new DirectRendezvous(g.mux, *g.hub, 1, random_session_id()) : new DirectRendezvous(g.mux, *g.hub, 1, g.join));
    g.link.reset(new M2LinkT<jpsm::Transport>(g.mux));
    g.phase = Phase::Handshake;
    install_all(std::make_integer_sequence<int, sizeof k_ents / sizeof k_ents[0]>());
    if (via_repsn) std::fprintf(stderr, "[net] re-psn: %s a room on %s:%u as %s (relay %s), local UDP port %u\n", g.host ? "creating" : "joining", rid.host.c_str(), (unsigned)rid.port, rid.name.c_str(), repsn_relay ? "allowed" : "not allowed", (unsigned)g.mux.local_port());
    else std::fprintf(stderr, "[net] listening/joining: %s, local UDP port %u\n", g.host ? "host" : to_string(g.join).c_str(), (unsigned)g.mux.local_port());
    g.pump.start(g.mux, *g.link,
        [](uint8_t peer, const uint8_t* d, size_t n, uint64_t ms) {   // runs on the pump thread under its mutex
            if (g.host && g.mh) g.mh->on_message(peer, d, n, ms);
            else if (!g.host && g.mc) g.mc->on_message(peer, d, n, ms);
        },
        [](uint64_t t) { pump_tick(t); }, 100);
    std::fprintf(stderr, "[net] rollback opt-in: %s, build %08x, hooks installed\n", g.host ? "host" : "client", g.caps.build);
    std::fprintf(stderr, "[net] status badge: %s\n", g.badge_on ? "on" : "off");
}

// Called by jsp_online.cpp's boot when the online files and --psnr have been applied; EnvInit reports through the same rendezvous.
extern "C" void jsp_net_env_part(int part) { g_rv.arrive(part, decide_env); }

#endif  // JSP_ROLLBACK_BUILD
