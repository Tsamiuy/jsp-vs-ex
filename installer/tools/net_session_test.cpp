// net_session_test.cpp -- host test for jsp_net_session. One PROCESS of a two-process test: a fake deterministic game (state = hash chain over
// every player's pad) driven through NetSession over real localhost UDP and through the REAL stepper state machine (jsp_stepper.h, in
// installer/port/jsp). Events are queued and the stepper runs ONE tick per main-loop iteration, like the game, so a Save's hash only exists
// after its Commit. Seeded loss/jitter, forced rollbacks, a cheat, a leave, a too-shallow ring, the settings-handshake stand-in, dirty Endpoint padding.
// installer/tools/net_session_test.py starts both processes and compares the outputs.
//
//   net_session_test --role host|client [--port P] [--peer ip:port] [--local N] [--delays a,b] [--ticks T] [--seed S] [--loss permille]
//       [--jitter ms] [--delay ms] [--cheat TICK] [--leave TICK] [--settings STR] [--pred N] [--depth N] [--stepper-depth N] [--dirty 1]
//       [--tick-ms N] [--blackout from,to] [--timeout-ms N] [--busy-stall TICK,MS] [--keepalive-ms N] [--max-ms N] [--out FILE]
//       [--ko-from TICK] [--ko-every N] [--ko-scan 0|1] [--mute-at TICK] [--idle-from UPDATE] [--gap-at TICK,MS]
// FIX (review of PR 248, the end rules of jsp_net_ko_rule.h, the same classes as jsp_net_game.cpp): --ko-from T records a winner on every save of a
// tick >= T whose state has fold % N == 0 (--ko-every, default 1 = every such tick), predicted or not, and drops the entries a rollback undoes.
// After each pass's events: a Disconnected decides once with jkr::disconnect_result ("KO disconnect: ..." line, with the old any-recorded-winner rule
// beside it); otherwise the live watermark takes the final frame and, with --ko-scan 1 (default), a covered winner ends it ("KO end: ..." line).
// --mute-at TICK: this copy stops sending at that tick (all its datagrams dropped from then on), so the peer predicts and then times out.
// --gap-at TICK,MS: once, the loop sleeps MS between taking the events (with their final frame, one lock) and using that frame ("GAP ..." line).
// Output: FILE gets "tick hash" per tick (last simulation of that tick), stdout one SUMMARY line.
// Exit: 0 ran to the end (or left on request), 2 rendezvous failed, 3 settings mismatch, 4 session failed, 5 reference mismatch, 6 start refused, 7 live-tick rate below --min-rate.
#include "jsp_net_session.h"
#include "jsp_stepper.h"
#include "jsp_net_glue.h"
#include "jsp_net_ko_rule.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <new>
#include <string>
#include <thread>
#include <vector>

using namespace jspnet;

static uint64_t mix(uint64_t x) { x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL; x ^= x >> 33; return x; }

struct Game {
    uint64_t h = 0x1234;
    uint32_t steps = 0;
    uint32_t sum[4] = {0, 0, 0, 0};
};
static void step(Game& g, uint32_t tick, const jsi::Pad* pads, unsigned players)
{
    for (unsigned p = 0; p < players; p++) {
        g.h = mix(g.h ^ mix(pads[p].key() + p * 0x9e37 + (uint64_t)tick));
        g.sum[p] += (uint32_t)(pads[p].buttons & 0xff);
    }
    g.steps++;
}
static uint32_t fold(const Game& g) { return (uint32_t)(g.h ^ (g.h >> 32)) ^ g.steps * 2654435761u ^ g.sum[0] ^ g.sum[1] * 3 ^ g.sum[2] * 5 ^ g.sum[3] * 7; }

static int g_change = 3;   // --change N: loops between input changes (3 = dense: many mispredictions; 40 = sparse)
static jsi::Pad pad_for(uint64_t seed, unsigned slot, int frame)
{
    const uint64_t v = mix(seed * 31 + slot * 1000003ull + (uint64_t)(frame / g_change));   // changes every 3 loops: some mispredictions
    jsi::Pad p;
    p.buttons = (uint16_t)v; p.lx = (uint8_t)(v >> 16); p.ly = (uint8_t)(v >> 24); p.rx = (uint8_t)(v >> 32); p.ry = (uint8_t)(v >> 40); p.connected = true;
    return p;
}
static bool wire_to_pad(const uint8_t* wire, jsi::Pad* out)
{
    uint8_t canon[8];
    pad_wire_to_pack(wire, canon);
    return jsi::Pad::unpack(canon, 8, out);
}

static const char* arg(int argc, char** argv, const char* k, const char* d = nullptr)
{
    for (int i = 1; i + 1 < argc; i++) if (!std::strcmp(argv[i], k)) return argv[i + 1];
    return d;
}

int main(int argc, char** argv)
{
    const std::string role = arg(argc, argv, "--role", "host");
    const bool host = role == "host";
    const unsigned local = (unsigned)atoi(arg(argc, argv, "--local", "2"));
    const int ticks = atoi(arg(argc, argv, "--ticks", "600"));
    const uint64_t seed = strtoull(arg(argc, argv, "--seed", "1"), nullptr, 10);
    const int tick_ms = atoi(arg(argc, argv, "--tick-ms", "8"));
    const int leave_at = atoi(arg(argc, argv, "--leave", "-1"));
    const int cheat = atoi(arg(argc, argv, "--cheat", "-1"));
    g_change = std::max(1, atoi(arg(argc, argv, "--change", "3")));
    const std::string settings = arg(argc, argv, "--settings", "ruleset=default");
    const char* outf = arg(argc, argv, "--out", nullptr);
    const int max_ms = atoi(arg(argc, argv, "--max-ms", "90000"));
    const int pred = atoi(arg(argc, argv, "--pred", "10"));
    const int depth = atoi(arg(argc, argv, "--depth", "12"));                       // the ring depth the session is told
    const int stepper_depth = atoi(arg(argc, argv, "--stepper-depth", arg(argc, argv, "--depth", "12")));   // the ring depth the game really has
    const int ko_from = atoi(arg(argc, argv, "--ko-from", "-1"));                   // FIX (review of PR 248): the end rules, see the header
    const uint32_t ko_every = (uint32_t)std::max(1, atoi(arg(argc, argv, "--ko-every", "1")));
    const bool ko_scan_on = atoi(arg(argc, argv, "--ko-scan", "1")) != 0;
    const int mute_at = atoi(arg(argc, argv, "--mute-at", "-1"));
    int gap_at = -1, gap_ms = 0; bool gap_done = false;   // --gap-at TICK,MS (see the event loop)
    if (const char* g = arg(argc, argv, "--gap-at", nullptr)) std::sscanf(g, "%d,%d", &gap_at, &gap_ms);

    UdpMux mux; std::string err;
    if (!mux.open((uint16_t)atoi(arg(argc, argv, "--port", "0")), &err)) { std::printf("SUMMARY role=%s open failed: %s\n", role.c_str(), err.c_str()); return 2; }
    LinkModel lm; lm.loss_permille = atoi(arg(argc, argv, "--loss", "0")); lm.jitter_ms = atoi(arg(argc, argv, "--jitter", "0"));
    lm.delay_ms = atoi(arg(argc, argv, "--delay", "0")); lm.seed = seed * 7919 + (host ? 1 : 2);
    if (const char* b = arg(argc, argv, "--blackout")) { unsigned f = 0, t = 0; std::sscanf(b, "%u,%u", &f, &t); lm.blackout_from_ms = f; lm.blackout_to_ms = t; }
    std::printf("PORT %u\n", mux.local_port()); std::fflush(stdout);

    MessageHub hub(mux);
    Endpoint peer{};
    std::unique_ptr<DirectRendezvous> rz;
    if (host) rz.reset(new DirectRendezvous(mux, hub, (uint8_t)local, 0xC0FFEE0000000001ull));
    else {
        if (!parse_endpoint(arg(argc, argv, "--peer", ""), peer)) { std::printf("SUMMARY role=client bad --peer\n"); return 2; }
        rz.reset(new DirectRendezvous(mux, hub, (uint8_t)local, peer));
    }
    auto sleep_ms = [](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
    auto t0 = std::chrono::steady_clock::now();
    auto since = [&] { return (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(); };
    int rs;
    while ((rs = rz->poll()) == 0 && since() < 15000) sleep_ms(2);
    if (rs != 1) { std::printf("SUMMARY role=%s rendezvous failed: %s\n", role.c_str(), rz->error().c_str()); return 2; }
    SessionPlan plan = rz->plan();
    MessageChannel* ch = rz->peer_channel();
    mux.set_link_model(lm);   // impairment starts after the rendezvous (the plan exchange has its own tests)

    // Settings-handshake stand-in: settings manifest compare, then a READY barrier (the real settings handshake is exercised in net_m2_link_test).
    {
        const std::string m = "SET:" + settings;
        ch->send((const uint8_t*)m.data(), m.size());
        std::vector<uint8_t> in; std::string theirs; bool got = false;
        while (!got && since() < 15000) { hub.pump(); if (ch->recv(in)) { theirs = std::string(in.begin(), in.end()); got = true; } else sleep_ms(2); }
        if (!got) { std::printf("SUMMARY role=%s handshake timeout\n", role.c_str()); return 2; }
        if (theirs != m) { std::printf("SUMMARY role=%s settings mismatch mine=%s theirs=%s\n", role.c_str(), m.c_str(), theirs.c_str()); return 3; }
        const uint8_t ok[5] = {'R', 'E', 'A', 'D', 'Y'};
        ch->send(ok, 5);
        got = false;
        while (!got && since() < 15000) { hub.pump(); if (ch->recv(in)) got = in.size() == 5; else sleep_ms(2); }
        if (!got) { std::printf("SUMMARY role=%s ready barrier timeout\n", role.c_str()); return 2; }
        while (!ch->idle() && since() < 15000) { hub.pump(); sleep_ms(2); }   // our READY acknowledged
    }

    if (atoi(arg(argc, argv, "--dirty", "0"))) {
        // Endpoint has 2 padding bytes: rebuild the remote endpoint in memory filled with 0xA5 so dirty padding would reach GekkoNet's byte compare.
        alignas(Endpoint) static unsigned char raw[sizeof(Endpoint)];
        std::memset(raw, 0xA5, sizeof raw);
        Endpoint* e = reinterpret_cast<Endpoint*>(raw);
        e->ip_be = plan.remotes[0].ep.ip_be; e->port = plan.remotes[0].ep.port;
        std::memcpy(&plan.remotes[0].ep, raw, sizeof(Endpoint));
    }

    // ENHANCEMENT (lobby: sessions opened and closed at runtime): --rounds N plays N matches one after another with the SAME NetSession object, mux and
    // message channel (as the game reuses its one NetSession for every lobby and match): NetSession::stop(), a ROUND barrier on the channel, start() again (the
    // GekkoNet adapter singleton must rebind). Each round prints its SUMMARY line (round=K/N); the tick log and recording are the last round's. The loop body
    // below is not re-indented, to keep the diff small.
    const int rounds = std::max(1, atoi(arg(argc, argv, "--rounds", "1")));
    NetSession s;
    for (int round = 1;; round++) {
    SessionConfig cfg; cfg.input_size = 8; cfg.max_prediction = (uint8_t)pred; cfg.ring_depth = (uint32_t)depth;
    if (const char* d = arg(argc, argv, "--delays")) { for (const char* p = d; *p;) { cfg.local_delay.push_back((uint8_t)atoi(p)); while (*p && *p != ',') p++; if (*p) p++; } }
    cfg.disconnect_timeout_ms = (uint32_t)atoi(arg(argc, argv, "--timeout-ms", "3000"));
    cfg.keepalive_stale_ms = (uint32_t)atoi(arg(argc, argv, "--keepalive-ms", "250"));   // 0 = no keepalive thread (negative control)
    cfg.limited_saving = atoi(arg(argc, argv, "--limited", "0")) != 0;   // ENHANCEMENT (rollback latency A/B): GekkoNet's sparse saving
    const char* recf = arg(argc, argv, "--record", nullptr);   // ENHANCEMENT (replays): write the session's recording here at the end (net_replay_test plays it back)
    if (recf) { cfg.record = true; const std::string ud = "test " + role; cfg.record_user_data.assign(ud.begin(), ud.end()); cfg.record_copy_every = 7; }
    if (!s.start(mux, plan, cfg)) { std::printf("SUMMARY role=%s start refused: %s\n", role.c_str(), s.error().c_str()); return 6; }

    const unsigned players = plan.total_players;
    jsx::Stepper stepper;
    stepper.begin(0, (uint32_t)stepper_depth);
    stepper.table().set_mask((1u << players) - 1);
    Game cur;
    std::map<uint32_t, Game> ring;                               // by done (completed ticks)
    uint32_t done = 0, serial = 100;
    std::map<uint32_t, uint64_t> hash_after;                     // tick -> h after the tick, last simulation
    std::map<uint32_t, std::vector<jsi::Pad>> pads_of;           // tick -> pads used, last simulation
    int frame = 0; bool left = false; int linger_until = -1; uint32_t max_tick = 0, cheats_applied = 0;
    std::string evlog; uint64_t commits = 0;
    jkr::Ring kr; jkr::Watermark kw; bool ko_decided = false, muted = false;   // FIX (review of PR 248): the game's end rules (--ko-from)
    uint32_t progress_tick = 0; int progress_ms = 0;                             // when max_tick last grew (the DISCONNECT-EVENT line)
    auto ko_rec = [&](uint32_t tick) {   // as GameGlue::ko_record: every save, predicted or not
        if (ko_from < 0) return;
        const uint32_t f = fold(cur);
        kr.record(tick, (int)tick >= ko_from && f % ko_every == 0 ? 1 + (int)((f >> 3) & 1u) : 0);
    };

    // The fake game behind the REAL glue (jsp_net_glue.h): the same Glue::iteration() the game calls once per main-loop iteration.
    struct TestGame : IGlueGame {
        std::function<uint32_t()> d; std::function<bool(uint32_t, uint32_t*, uint32_t*)> c; std::function<bool(uint32_t)> r; std::function<void(uint8_t, uint8_t*)> lp;
        uint32_t done() override { return d(); }
        bool commit(uint32_t t, uint32_t* h, uint32_t* k) override { return c(t, h, k); }
        bool rollback(uint32_t t) override { return r(t); }
        void local_pad(uint8_t n, uint8_t* w) override { lp(n, w); }
        // ENHANCEMENT (rollback latency, --skip-final 1): the checksum without a stored state; the tick is ERASED from the fake ring, so a Load of it fails the run
        bool hash_only(uint32_t t, uint32_t* h) override { if (!hp) return false; return hp(t, h); }
        std::function<bool(uint32_t, uint32_t*)> hp;
    } tg;
    const bool skip_final = atoi(arg(argc, argv, "--skip-final", "0")) != 0;
    if (skip_final) tg.hp = [&](uint32_t tick, uint32_t* h) { ring.erase(tick); *h = fold(cur); ko_rec(tick); return true; };
    tg.d = [&] { return done; };
    tg.c = [&](uint32_t tick, uint32_t* h, uint32_t* k) { ring[tick] = cur; commits++; *h = fold(cur); *k = ++serial; while (ring.size() > 64) ring.erase(ring.begin()); ko_rec(tick); return true; };
    tg.r = [&](uint32_t tick) { auto it = ring.find(tick); if (it == ring.end()) return false; cur = it->second; done = tick; kr.drop_after(tick); return true; };
    // --idle-from N: from update N on this copy sends the all-zero wire input, which is also what GekkoNet fills in for a disconnected peer (so the peer's
    // predictions of it are never corrected when it goes: its predicted saves stay, as when a real player stops pressing before the link dies)
    const int idle_from = atoi(arg(argc, argv, "--idle-from", "-1"));
    tg.lp = [&](uint8_t n, uint8_t* wire) {
        if (idle_from >= 0 && frame >= idle_from) { std::memset(wire, 0, 8); return; }
        const jsi::Pad p = pad_for(seed, plan.local_first + n, frame); pad_pack_to_wire(p.pack().data(), wire);
    };
    Glue glue(s, stepper, tg);
    glue.set_skip_final(skip_final);
    // One iteration = ONE call of glue.iteration(); a Run executes the granted tick. Rate bookkeeping for --min-rate (see below).
    int poll_stall_at = -1, poll_stall_ms = 0; bool poll_stalled = false; if (const char* ps = arg(argc, argv, "--poll-stall", "")) std::sscanf(ps, "%d,%d", &poll_stall_at, &poll_stall_ms);   // --poll-stall TICK,MS: from that tick the loop only polls (as a long commit gate does) for MS
    // --busy-stall TICK,MS: from that tick the loop sleeps for MS WITHOUT calling the session (a replay or a slow render on the game thread); only the keepalive thread can keep the peer alive
    int busy_at = -1, busy_ms = 0; bool busy_done = false; if (const char* bs = arg(argc, argv, "--busy-stall", "")) std::sscanf(bs, "%d,%d", &busy_at, &busy_ms);
    // FIX (review of PR 248, the freeze timeout's owner): --stall-abort MS: during the busy stall another thread (the game's pump thread) ends the session MS
    // after it began, as pump_tick does on the frame clock's failure, while the game thread is still away; the keepalive thread alone would keep the peer waiting
    const int stall_abort_ms = atoi(arg(argc, argv, "--stall-abort", "-1"));
    uint64_t iters_after_first_run = 0, runs_after_first = 0, live_after_first = 0; bool seen_run = false;   // window: first tick .. the tick `ticks` (the linger at the end is not counted)
    auto one_iteration = [&]() {
        const uint64_t u0 = glue.updates();
        const jsx::Op op = glue.iteration(true);   // pacing is the sleep at the end of the loop (as the game: pace_sleep_ms, update still called)
        if (glue.updates() != u0) frame++;
        else s.poll();   // as the game: replay / regrant / stall iterations still service the network
        if (poll_stall_at >= 0 && !poll_stalled && (int)max_tick >= poll_stall_at) { poll_stalled = true; const int e = since() + poll_stall_ms; while (since() < e) { hub.pump(); s.poll(); sleep_ms(20); } }
        if (busy_at >= 0 && !busy_done && (int)max_tick >= busy_at) {
            busy_done = true;
            std::thread owner;
            if (stall_abort_ms >= 0) owner = std::thread([&] { sleep_ms(stall_abort_ms); s.abort("frame clock stalled"); std::printf("STALL-ABORT by the owner thread at %d ms\n", since()); std::fflush(stdout); });
            sleep_ms(busy_ms);
            if (owner.joinable()) owner.join();
        }
        const bool in_window = seen_run && (int)max_tick < ticks;
        if (in_window) iters_after_first_run++;
        if (op.kind != jsx::OpKind::Run) return;
        std::vector<jsi::Pad> pads(players);
        for (unsigned p = 0; p < players; p++) {
            jsi::Pad pd;
            if (stepper.table().serve(op.tick, (int)p, &pd)) pads[p] = pd; else pads[p] = stepper.table().get(op.tick, (int)p);
        }
        step(cur, op.tick, pads.data(), players);
        if (cheat >= 0 && (int)op.tick == cheat) { cur.h ^= 0xDEADBEEF; cheats_applied++; }   // this peer's state now differs
        done = op.tick;
        hash_after[op.tick] = cur.h; pads_of[op.tick] = pads;
        max_tick = std::max(max_tick, op.tick);
        if (in_window) { runs_after_first++; if (!op.replay) live_after_first++; }
        seen_run = true;
    };
    uint64_t upd_prev = 0, paced_n = 0, paced_ms = 0;
    while (true) {
        hub.pump();
        if (!s.failed()) one_iteration();
        bool lost_now = false;
        int events_ff = -1;   // as the boundary: the final frame read with the events, under the session lock
        const std::vector<SessionEvent> evs = s.take_events(&events_ff);
        // --gap-at TICK,MS: once, at the first pass at or past TICK, the game thread stops for MS between taking the events and using their frame,
        // so the keepalive thread polls in that gap (and may queue the disconnect there). The watermark must still be the frame read with the events.
        if (gap_at >= 0 && !gap_done && (int)max_tick >= gap_at) {
            const uint64_t p0 = s.keepalive_polls();
            const int ff0 = s.final_frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(gap_ms));
            gap_done = true;
            std::printf("GAP at=%u ms=%d events_ff=%d ff_before=%d ff_after=%d polls=%llu\n", max_tick, gap_ms, events_ff, ff0, s.final_frame(),
                        (unsigned long long)(s.keepalive_polls() - p0));
            std::fflush(stdout);
        }
        for (const SessionEvent& e : evs) {
            char b[200];
            const char* k[] = {"syncing", "connected", "started", "disconnected", "desync", "failed", "replay-finished"};
            std::snprintf(b, sizeof b, " %s(h=%d t=%d)", k[(int)e.kind], e.handle, e.tick);
            if (e.kind != SessionEventKind::Syncing) { if (evlog.size() < 600) evlog += b; }
            if (e.kind == SessionEventKind::Disconnected) {
                if (!lost_now) std::printf("DISCONNECT-EVENT %d ms after this copy's last new tick %u\n", since() - progress_ms, max_tick);
                lost_now = true;
            }
        }
        if (max_tick != progress_tick) { progress_tick = max_tick; progress_ms = since(); }
        // FIX (review of PR 248): the boundary's order in jsp_net_game.cpp: the events first (a Disconnected decides with the live watermark), then the
        // watermark takes this pass's final frame and ko_scan ends on a covered winner.
        if (ko_from >= 0 && !ko_decided) {
            if (lost_now) {
                kw.lost();
                uint32_t at = 0, old_at = 0;
                const int w = jkr::disconnect_result(kr, 0, kw, &at);
                const int old_w = kr.earliest_any(&old_at);
                std::printf("KO disconnect: result=%s at=%u watermark=%d final_frame=%d old_rule=%s old_at=%u uncovered=%d\n", w ? "match" : "disconnect", at, kw.ff,
                            s.final_frame(), old_w ? "match" : "disconnect", old_at, kr.uncovered(0, kw.ff));
                ko_decided = true;
            } else {
                kw.live(events_ff);
                uint32_t at = 0;
                if (ko_scan_on && kr.earliest_covered(0, s.final_frame(), &at)) {
                    std::printf("KO end: confirmed result at=%u final_frame=%d\n", at, s.final_frame());
                    ko_decided = true;
                }
            }
            std::fflush(stdout);
        }
        if (mute_at >= 0 && !muted && (int)max_tick >= mute_at) { LinkModel m = lm; m.loss_permille = 1000; mux.set_link_model(m); muted = true; std::printf("MUTED at tick %u\n", max_tick); std::fflush(stdout); }
        if (s.failed()) { linger_until = linger_until < 0 ? since() + 700 : linger_until; if (since() > linger_until) break; }
        if (leave_at >= 0 && !left && (int)max_tick >= leave_at) { s.leave(0); left = true; linger_until = since() + 500; }
        if (linger_until < 0 && (int)max_tick >= ticks) linger_until = since() + 2500;   // stay alive so the peer can finish
        if (linger_until >= 0 && since() > linger_until) break;
        if (since() > max_ms) break;
        { const uint32_t pm = (glue.updates() != upd_prev) ? s.pace_sleep_ms((uint32_t)tick_ms) : 0; upd_prev = glue.updates(); if (pm) { paced_n++; paced_ms += pm; }
          sleep_ms(tick_ms + pm); }
    }

    // Drain: finish the replay in progress (the reference below needs every tick re-simulated from its corrected predecessor).
    for (int i = 0; i < 600 && !s.failed() && stepper.queued() > 0; i++) {   // no updates while draining: allow_update=false
        const jsx::Op op = glue.iteration(false);
        if (op.kind != jsx::OpKind::Run) continue;
        std::vector<jsi::Pad> pads(players);
        for (unsigned p = 0; p < players; p++) { jsi::Pad pd; if (stepper.table().serve(op.tick, (int)p, &pd)) pads[p] = pd; else pads[p] = stepper.table().get(op.tick, (int)p); }
        step(cur, op.tick, pads.data(), players);
        if (cheat >= 0 && (int)op.tick == cheat) { cur.h ^= 0xDEADBEEF; cheats_applied++; }
        done = op.tick; hash_after[op.tick] = cur.h; pads_of[op.tick] = pads; max_tick = std::max(max_tick, op.tick);
    }

    // Reference: replay the final pads straight from tick 1 and compare to what the rollback run produced.
    int ref_bad = 0, ref_n = 0;
    {
        Game g;
        for (uint32_t t = 1; t <= max_tick; t++) {
            auto it = pads_of.find(t);
            if (it == pads_of.end()) break;
            step(g, t, it->second.data(), players);
            if ((int)t == cheat && cheats_applied) g.h ^= 0xDEADBEEF;
            ref_n++;
            if (g.h != hash_after[t]) { ref_bad++; if (ref_bad == 1) std::printf("REF first bad tick %u\n", t); }
        }
    }
    // Neutral check: after a peer left, the slots it owned must read neutral (buttons 0, sticks centre, not connected) at the end.
    int tail_neutral = -1;
    if (!s.failed() && max_tick > 6) {
        tail_neutral = 1;
        for (uint32_t t = max_tick - 3; t <= max_tick; t++)
            for (unsigned p = 0; p < players; p++) {
                if (p >= plan.local_first && p < (unsigned)plan.local_first + plan.local_count) continue;
                const jsi::Pad& pd = pads_of[t][p];
                if (pd.buttons || pd.lx != 128 || pd.ly != 128 || pd.rx != 128 || pd.ry != 128) {   // `connected` is forced by jsi::Table::serve (slot in the mask)
                    if (tail_neutral) std::printf("TAIL tick %u slot %u buttons=%04x sticks=%u,%u,%u,%u connected=%d\n", t, p, pd.buttons, pd.lx, pd.ly, pd.rx, pd.ry, (int)pd.connected);
                    tail_neutral = 0;
                }
            }
    }
    if (recf) {
        std::vector<uint8_t> rec;
        const bool ok = s.recording(rec);
        FILE* f = ok ? std::fopen(recf, "wb") : nullptr;
        if (f) { std::fwrite(rec.data(), 1, rec.size(), f); std::fclose(f); }
        std::printf("RECORD bytes=%zu written=%d\n", rec.size(), f != nullptr);
    }
    if (outf) {
        FILE* f = std::fopen(outf, "w");
        if (f) { for (auto& kv : hash_after) std::fprintf(f, "%u %016llx\n", kv.first, (unsigned long long)kv.second); std::fclose(f); }
    }
    // One live tick per main-loop iteration (the glue fix): after the first tick, runs / iterations. The old two-step loop gave ~0.5.
    // `rate` counts LIVE ticks only (replay Runs are not progress): live Runs / iterations. all_rate also counts replay Runs. Pre-fix the loop gave about 0.5.
    const double rate = iters_after_first_run ? (double)live_after_first / (double)iters_after_first_run : 0.0;
    const double all_rate = iters_after_first_run ? (double)runs_after_first / (double)iters_after_first_run : 0.0;
    const double min_rate = atof(arg(argc, argv, "--min-rate", "0")), min_all = atof(arg(argc, argv, "--min-all-rate", "0"));
    const SessionStats st = s.stats();
    std::printf("SUMMARY role=%s slots=%u..%u/%u maxtick=%u rollbacks=%u maxdepth=%u resim=%llu saves=%llu loads=%llu stalls=%u deferred=%llu desync=%d failed=%s "
                "ref_ticks=%d ref_bad=%d tail_neutral=%d rate=%.3f all_rate=%.3f iters=%llu paced=%llu paced_ms=%llu keepalive_polls=%llu sent=%llu dropped=%llu skipped=%llu round=%d/%d events:%s\n",
                role.c_str(), plan.local_first, plan.local_first + plan.local_count - 1, plan.total_players, max_tick, st.rollbacks, st.max_rollback_frames,
                (unsigned long long)st.resimulated, (unsigned long long)st.saves, (unsigned long long)st.loads, st.stalls, (unsigned long long)st.deferred,
                s.first_desync_tick(), s.failed() ? s.failure().c_str() : "-", ref_n, ref_bad, tail_neutral, rate, all_rate, (unsigned long long)iters_after_first_run, (unsigned long long)paced_n, (unsigned long long)paced_ms, (unsigned long long)st.keepalive_polls, (unsigned long long)mux.sent,
                (unsigned long long)mux.dropped, (unsigned long long)glue.skipped(), round, rounds, evlog.c_str());
    std::fflush(stdout);
    // two rules for 7: live ticks per iteration (sparse inputs) and Runs (live + replay) per iteration
    const int rc = s.failed() ? 4 : ((min_rate > 0 && rate < min_rate) || (min_all > 0 && all_rate < min_all)) ? 7 : ref_bad ? 5 : 0;
    if (rc || round >= rounds) return rc;
    // ENHANCEMENT (lobby): the next round on the same objects. stop() must leave nothing of this session behind (no events, not synced, not failed, no stats).
    s.stop();
    { const SessionStats z = s.stats();
      if (s.running() || s.synced() || s.failed() || !s.take_events().empty() || z.advances || z.saves || z.rollbacks) { std::printf("SUMMARY role=%s stop() left session state behind (round %d)\n", role.c_str(), round); return 8; } }
    {   // both peers have stopped before either starts again (stragglers of the old session carry its magic and are dropped by the new one)
        const std::string m = "ROUND" + std::to_string(round + 1);
        ch->send((const uint8_t*)m.data(), m.size());
        std::vector<uint8_t> in; bool got = false; const int until = since() + 15000;
        while (!got && since() < until) { hub.pump(); if (ch->recv(in)) got = std::string(in.begin(), in.end()) == m; else sleep_ms(2); }
        if (!got) { std::printf("SUMMARY role=%s round barrier timeout (round %d)\n", role.c_str(), round + 1); return 2; }
        while (!ch->idle() && since() < until) { hub.pump(); sleep_ms(2); }
    }
    }   // rounds
}
