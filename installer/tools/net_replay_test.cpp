// net_replay_test.cpp -- host test for the replay and stress modes of jsp_net_session (ENHANCEMENT: replays and the rollback bench). One process, no
// socket: the same fake deterministic game as net_session_test.cpp, driven through the REAL glue and stepper (jsp_net_glue.h, jsp_stepper.h), one tick per
// main-loop iteration like the game.
//
//   net_replay_test --mode replay --replay FILE [--cheat TICK] [--out FILE]
//       plays back a recording net_session_test wrote with --record. --out gets "tick hash" per tick, the same form as net_session_test's --out, so the
//       driver can compare them with the recording peer's ticks. --cheat flips the state after TICK: the replay must report a desync at that tick.
//   net_replay_test --mode stress --distance D [--players N] [--ticks T] [--flaky TICK] [--seed S]
//       GekkoNet's stress session: every update rolls back D ticks and re-simulates them. --flaky makes the RE-simulation of TICK differ from its
//       first pass (a hidden input such as an unshared clock): the session must report a desync at that tick.
//   net_replay_test --mode bad-replay --replay FILE   start_replay must refuse a truncated and a corrupted header (exit 0 when both are refused).
// Output: one SUMMARY line. Exit: 0 done, 4 session failed, 6 start refused, 8 a mode-specific expectation failed.
#include "jsp_net_session.h"
#include "jsp_stepper.h"
#include "jsp_net_glue.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace jspnet;

// The same fake game as net_session_test.cpp (kept identical: the replay test compares hashes with that test's output).
static uint64_t mix(uint64_t x) { x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL; x ^= x >> 33; return x; }
struct Game { uint64_t h = 0x1234; uint32_t steps = 0; uint32_t sum[4] = {0, 0, 0, 0}; };
static void step(Game& g, uint32_t tick, const jsi::Pad* pads, unsigned players)
{
    for (unsigned p = 0; p < players; p++) { g.h = mix(g.h ^ mix(pads[p].key() + p * 0x9e37 + (uint64_t)tick)); g.sum[p] += (uint32_t)(pads[p].buttons & 0xff); }
    g.steps++;
}
static uint32_t fold(const Game& g) { return (uint32_t)(g.h ^ (g.h >> 32)) ^ g.steps * 2654435761u ^ g.sum[0] ^ g.sum[1] * 3 ^ g.sum[2] * 5 ^ g.sum[3] * 7; }
static jsi::Pad pad_for(uint64_t seed, unsigned slot, int frame)
{
    const uint64_t v = mix(seed * 31 + slot * 1000003ull + (uint64_t)(frame / 3));
    jsi::Pad p; p.buttons = (uint16_t)v; p.lx = (uint8_t)(v >> 16); p.ly = (uint8_t)(v >> 24); p.rx = (uint8_t)(v >> 32); p.ry = (uint8_t)(v >> 40); p.connected = true;
    return p;
}
static const char* arg(int argc, char** argv, const char* k, const char* d = nullptr)
{
    for (int i = 1; i + 1 < argc; i++) if (!std::strcmp(argv[i], k)) return argv[i + 1];
    return d;
}
static bool read_file(const char* path, std::vector<uint8_t>& out)
{
    FILE* f = path ? std::fopen(path, "rb") : nullptr;
    if (!f) return false;
    out.clear(); uint8_t b[4096]; size_t n;
    while ((n = std::fread(b, 1, sizeof b, f)) > 0) out.insert(out.end(), b, b + n);
    std::fclose(f);
    return true;
}

int main(int argc, char** argv)
{
    const std::string mode = arg(argc, argv, "--mode", "stress");
    SessionConfig cfg; cfg.input_size = 8; cfg.max_prediction = 10; cfg.ring_depth = 12;

    if (mode == "bad-replay") {
        std::vector<uint8_t> b;
        if (!read_file(arg(argc, argv, "--replay"), b) || b.size() < 24) { std::printf("SUMMARY mode=bad-replay no input\n"); return 8; }
        std::vector<uint8_t> cut(b.begin(), b.begin() + 10), bad = b; bad[0] ^= 0xFF;
        std::vector<uint8_t> wrong_size = b; wrong_size[16] = 9;   // input_size 9 (the session's is 8)
        NetSession a, c, d;
        const bool r1 = !a.start_replay(cut, cfg), r2 = !c.start_replay(bad, cfg), r3 = !d.start_replay(wrong_size, cfg);
        std::printf("SUMMARY mode=bad-replay truncated_refused=%d magic_refused=%d input_size_refused=%d (%s | %s | %s)\n", r1, r2, r3,
                    a.error().c_str(), c.error().c_str(), d.error().c_str());
        return r1 && r2 && r3 ? 0 : 8;
    }

    const bool replay = mode == "replay";
    const uint64_t seed = strtoull(arg(argc, argv, "--seed", "1"), nullptr, 10);
    const int ticks = atoi(arg(argc, argv, "--ticks", "600"));
    const int cheat = atoi(arg(argc, argv, "--cheat", "-1")), flaky = atoi(arg(argc, argv, "--flaky", "-1"));
    const unsigned distance = (unsigned)atoi(arg(argc, argv, "--distance", "4"));
    NetSession s;
    if (replay) {
        std::vector<uint8_t> bytes;
        if (!read_file(arg(argc, argv, "--replay"), bytes)) { std::printf("SUMMARY mode=replay cannot read --replay\n"); return 6; }
        if (!s.start_replay(bytes, cfg)) { std::printf("SUMMARY mode=replay start refused: %s\n", s.error().c_str()); return 6; }
        std::vector<uint8_t> ud; s.replay_user_data(ud);
        std::printf("USERDATA %s\n", std::string(ud.begin(), ud.end()).c_str());
    } else {
        const char* rec = arg(argc, argv, "--record", nullptr);   // ENHANCEMENT (replays): a stress session records its inputs (no checksums)
        if (rec) { cfg.record = true; cfg.record_copy_every = 7; const std::string ud = "stress test"; cfg.record_user_data.assign(ud.begin(), ud.end()); }
        if (!s.start_stress((uint8_t)atoi(arg(argc, argv, "--players", "2")), distance, cfg)) { std::printf("SUMMARY mode=stress start refused: %s\n", s.error().c_str()); return 6; }
    }
    const unsigned players = s.plan().total_players;

    jsx::Stepper stepper; stepper.begin(0, cfg.ring_depth);
    stepper.table().set_mask((1u << players) - 1);
    Game cur; std::map<uint32_t, Game> ring; uint32_t done = 0, serial = 100;
    std::map<uint32_t, uint64_t> hash_after;
    int frame = 0; uint32_t max_tick = 0; uint64_t live = 0, replayed = 0; int desync_events = 0, first_desync = -1, finished = 0;
    struct TestGame : IGlueGame {
        std::function<uint32_t()> d; std::function<bool(uint32_t, uint32_t*, uint32_t*)> c; std::function<bool(uint32_t)> r; std::function<void(uint8_t, uint8_t*)> lp;
        uint32_t done() override { return d(); }
        bool commit(uint32_t t, uint32_t* h, uint32_t* k) override { return c(t, h, k); }
        bool rollback(uint32_t t) override { return r(t); }
        void local_pad(uint8_t n, uint8_t* w) override { lp(n, w); }
    } tg;
    tg.d = [&] { return done; };
    tg.c = [&](uint32_t tick, uint32_t* h, uint32_t* k) { ring[tick] = cur; *h = fold(cur); *k = ++serial; while (ring.size() > 64) ring.erase(ring.begin()); return true; };
    tg.r = [&](uint32_t tick) { auto it = ring.find(tick); if (it == ring.end()) return false; cur = it->second; done = tick; return true; };
    tg.lp = [&](uint8_t n, uint8_t* wire) { const jsi::Pad p = pad_for(seed, n, frame); pad_pack_to_wire(p.pack().data(), wire); };
    Glue glue(s, stepper, tg);

    for (int iter = 0; iter < 200000; iter++) {
        const uint64_t u0 = glue.updates();
        const jsx::Op op = glue.iteration(true);
        if (glue.updates() != u0) frame++;
        for (const SessionEvent& e : s.take_events()) {
            if (e.kind == SessionEventKind::Desync) { desync_events++; if (first_desync < 0) first_desync = e.tick; }
            if (e.kind == SessionEventKind::ReplayFinished) finished = 1;
        }
        if (s.failed()) break;
        if (op.kind == jsx::OpKind::Run) {
            std::vector<jsi::Pad> pads(players);
            for (unsigned p = 0; p < players; p++) { jsi::Pad pd; pads[p] = stepper.table().serve(op.tick, (int)p, &pd) ? pd : stepper.table().get(op.tick, (int)p); }
            step(cur, op.tick, pads.data(), players);
            if (cheat >= 0 && (int)op.tick == cheat) cur.h ^= 0xDEADBEEF;
            if (flaky >= 0 && (int)op.tick == flaky && op.replay) cur.h ^= 0x5A5A5A5A;   // the re-simulation differs from the first pass
            done = op.tick; hash_after[op.tick] = cur.h; max_tick = std::max(max_tick, op.tick);
            (op.replay ? replayed : live)++;
        }
        if (replay ? (finished && stepper.queued() == 0) : (int)max_tick >= ticks) break;   // a stress session queues new work every update: no drain
    }
    if (const char* outf = arg(argc, argv, "--out", nullptr)) {
        if (FILE* f = std::fopen(outf, "w")) { for (auto& kv : hash_after) std::fprintf(f, "%u %016llx\n", kv.first, (unsigned long long)kv.second); std::fclose(f); }
    }
    if (const char* rec = replay ? nullptr : arg(argc, argv, "--record", nullptr)) {
        std::vector<uint8_t> b;
        FILE* f = s.recording(b) ? std::fopen(rec, "wb") : nullptr;
        if (f) { std::fwrite(b.data(), 1, b.size(), f); std::fclose(f); }
        std::printf("RECORD bytes=%zu written=%d\n", b.size(), f != nullptr);
        if (!f) return 8;
    }
    const SessionStats st = s.stats();
    std::printf("SUMMARY mode=%s players=%u maxtick=%u live=%llu replayed=%llu rollbacks=%u maxdepth=%u saves=%llu loads=%llu finished=%d desync_events=%d desync=%d failed=%s\n",
                mode.c_str(), players, max_tick, (unsigned long long)live, (unsigned long long)replayed, st.rollbacks, st.max_rollback_frames,
                (unsigned long long)st.saves, (unsigned long long)st.loads, finished, desync_events, first_desync, s.failed() ? s.failure().c_str() : "-");
    if (s.failed()) return 4;
    if (replay && !finished) return 8;
    const int expect = cheat >= 0 ? cheat : flaky;   // a planted difference must be reported at exactly that tick, and a clean run must report none
    if (expect >= 0 ? first_desync != expect : first_desync >= 0) return 8;
    if (!replay && (int)replayed < (ticks - (int)distance - 2) * ((int)distance - 1)) return 8;   // every update (after the first `distance`) re-simulated distance-1 ticks
    return 0;
}
