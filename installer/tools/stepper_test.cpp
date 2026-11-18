// Host test of the rollback stepper pieces: canonical input (jsp_step_input.h), the side-effect gate (jsp_step_effects.h) and the control state machine
// (jsp_stepper.h) against a fake game (a hash chain over the inputs) and a fake snapshot ring. Same code as the game build, no runtime.
// installer/tools/stepper_test.sh builds and runs it
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <map>
#include <vector>
#include "jsp_step_effects.h"
#include "jsp_step_input.h"
#include "jsp_stepper.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint64_t mix(uint64_t h, uint64_t v) { h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2); h *= 0xFF51AFD7ED558CCDull; return h ^ (h >> 29); }
static uint64_t rnd_state = 1;
static uint32_t rnd() { rnd_state = mix(rnd_state, 0x1234); return (uint32_t)(rnd_state >> 20); }

// ---------------------------------------------------------------------------------------------------- input
static jsi::Pad mk(uint16_t b, uint8_t lx = 128, bool c = true) { jsi::Pad p; p.buttons = b; p.lx = lx; p.connected = c; return p; }

static void test_input()
{
    jsi::Pad p = mk(0xA55A, 7); p.ly = 250; p.rx = 3; p.ry = 4;
    auto w = p.pack(); jsi::Pad q;
    CHECK(jsi::Pad::unpack(w.data(), 8, &q) && q == p);
    uint8_t bad[8] = {0, 0, 128, 128, 128, 128, 2, 0}; CHECK(!jsi::Pad::unpack(bad, 8, &q));
    uint8_t bad2[8] = {0, 0, 128, 128, 128, 128, 1, 9}; CHECK(!jsi::Pad::unpack(bad2, 8, &q));
    CHECK(!jsi::Pad::unpack(w.data(), 7, &q));
    CHECK(w[0] == 0x5A && w[1] == 0xA5 && w[2] == 7 && w[6] == 1);   // fixed byte order

    uint16_t b[64]; int32_t len = 0;
    jsi::to_record(p, jsi::kLenDefault, b, &len);
    CHECK(len == 8 && b[1] == 0x74 && b[jsi::kOffDigital1] == 0x5A && b[jsi::kOffDigital2] == 0xA5 && b[jsi::kOffLX] == 7 && b[jsi::kOffLY] == 250 && b[jsi::kOffRX] == 3 && b[jsi::kOffRY] == 4);
    CHECK(b[jsi::kOffPressRight] == 0 && b[jsi::kOffSensorX] == 0);
    jsi::to_record(mk(0x8000 | 0x0020 | 0x0400), jsi::kLenSensor, b, &len);   // SQUARE, RIGHT, L1
    CHECK(len == 28 && b[jsi::kOffPressRight] == 0xFF && b[jsi::kOffPressRight + 3] == 0 && b[jsi::kOffPressRight + 7] == 0xFF && b[jsi::kOffPressRight + 8] == 0xFF && b[jsi::kOffSensorX + 1] == 399);
    jsi::Pad r = jsi::from_record(b, true); CHECK(r.buttons == (0x8000 | 0x0020 | 0x0400));

    jsi::Table t; t.set_mask(0x3);
    jsi::Pad got;
    CHECK(!t.serve(5, 2, &got));                                   // slot outside the mask: never a packet
    CHECK(t.serve(5, 0, &got) && got.connected && t.starved() == 1);   // no value known: neutral, counted
    CHECK(!t.serve(5, 0, &got));                                   // second read of tick 5: drain (len 0)
    CHECK(t.serve(6, 0, &got));
    t.set(7, 0, mk(1), false);
    CHECK(t.serve(7, 0, &got) && got.buttons == 1);
    CHECK(t.set(7, 0, mk(2), true));                               // correction of a tick already used: misprediction
    CHECK(t.first_wrong() == 7);
    t.rewind(6);                                                    // rollback to 6 completed: tick 7 is next again
    CHECK(t.first_wrong() == jsi::kNone && t.serve(7, 0, &got) && got.buttons == 2 && t.confirmed(7, 0));
    CHECK(!t.set(7, 0, mk(2), true));                              // identical re-set is not a misprediction
    // local capture: once per new tick, never in a replay
    jsi::Table c; c.set_mask(1);
    CHECK(c.capture_local(10, 0, mk(4)) && !c.capture_local(10, 0, mk(8)) && !c.capture_local(9, 0, mk(8)) && c.capture_local(11, 0, mk(16)));
    CHECK(c.get(10, 0).buttons == 4);
    CHECK(!c.capture_local(10, 1, mk(1)) && !c.has(10, 1));          // slot outside the mask: nothing stored
    c.forget_before(11); CHECK(!c.has(10, 0) && c.has(11, 0));
    // per slot, in any read order (one shared top made slot 1 never captured once slot 0 had read the tick)
    for (uint32_t mask : {3u, 0xFu, 0xAu, 0x5u})
        for (int order = 0; order < 24; order++) {
            int perm[4] = {0, 1, 2, 3}; int o = order;
            for (int i = 3; i > 0; i--) { std::swap(perm[i], perm[o % (i + 1)]); o /= (i + 1); }
            jsi::Table m; m.set_mask(mask);
            for (uint32_t t = 1; t <= 3; t++)
                for (int k = 0; k < 4; k++) {
                    const int sl = perm[k];
                    const bool in = (mask >> sl) & 1;
                    CHECK(m.capture_local(t, sl, mk((uint16_t)(t * 16 + sl))) == in);
                    if (in) { CHECK(m.get(t, sl).buttons == t * 16 + sl); jsi::Pad g; CHECK(m.serve(t, sl, &g) && g.buttons == t * 16 + sl); }
                    else CHECK(!m.has(t, sl));
                }
        }
}

// ---------------------------------------------------------------------------------------------------- effects
static void test_effects()
{
    jse::Gate g;
    using jse::Cat; using jse::Action;
    CHECK(g.request(Cat::SoundEffect, 10, 111) == Action::Perform);
    CHECK(g.request(Cat::SoundEffect, 10, 111) == Action::Perform);   // twice in one tick: two voices
    CHECK(g.request(Cat::SoundEffect, 11, 222) == Action::Perform);
    CHECK(g.request(Cat::Music, 11, 900) == Action::Perform);
    CHECK(g.request(Cat::Net, 11, 1) == Action::Perform);
    CHECK(g.request(Cat::SaveData, 11, 5, 77) == Action::Defer);
    CHECK(g.request(Cat::FileRead, 11, 6) == Action::Perform);
    CHECK(g.pending() == 1);
    // rollback to 9, replay 10..12
    g.rollback(9); g.set_replay(true);
    CHECK(g.pending() == 0);                                           // the deferred save of tick 11 is dropped: replay asks again
    CHECK(g.request(Cat::SoundEffect, 10, 111) == Action::Skip);
    CHECK(g.request(Cat::SoundEffect, 10, 111) == Action::Skip);
    CHECK(g.request(Cat::SoundEffect, 10, 111) == Action::Perform);   // a third one is new: late
    CHECK(g.request(Cat::Music, 11, 900) == Action::Skip);
    CHECK(g.request(Cat::Music, 12, 901) == Action::Skip);             // music never starts late
    CHECK(g.request(Cat::Net, 11, 1) == Action::Skip);
    CHECK(g.request(Cat::SaveData, 11, 5, 77) == Action::Defer);
    CHECK(g.request(Cat::FileRead, 11, 6) == Action::Perform);
    CHECK(g.request(Cat::SoundEffect, 12, 333) == Action::Perform);    // exists only in the corrected timeline
    auto orph = g.end_replay(9);
    CHECK(orph.size() == 1 && orph[0].tick == 11 && orph[0].key == 222 && orph[0].count == 1 && !g.replay());   // 222 was heard, not reproduced
    { jse::Gate h; h.request(Cat::SoundEffect, 11, 5); h.rollback(9); h.set_replay(true);
      CHECK(h.end_replay(12).empty());                                // the tick that ENDED the replay hides the orphan: the rollback tick is the argument
      jse::Gate h2; h2.request(Cat::SoundEffect, 11, 5); h2.rollback(9); h2.set_replay(true); CHECK(h2.end_replay(9).size() == 1); }
    CHECK(g.confirm(10).empty());                                      // tick 11 not confirmed yet
    auto rel = g.confirm(11); CHECK(rel.size() == 1 && rel[0].id == 77 && g.pending() == 0);
    CHECK(g.confirm(99).empty());                                      // released exactly once
    CHECK(g.stats().deduped == 3 && g.stats().late == 2 && g.stats().suppressed == 1 && g.stats().released == 1);
    g.set_late_new_sound(false); g.set_replay(true);
    CHECK(g.request(Cat::SoundEffect, 50, 1) == Action::Skip);
    g.forget_before(100);
}

// ---------------------------------------------------------------------------------------------------- stepper protocol (GekkoNet frame semantics)
using namespace jsx;
static Event ev_save(int32_t f) { Event e; e.kind = Kind::Save; e.frame = f; return e; }
static Event ev_load(int32_t f) { Event e; e.kind = Kind::Load; e.frame = f; return e; }
static Event ev_rsave(int32_t f) { Event e; e.kind = Kind::RunaheadSave; e.frame = f; return e; }
static Event ev_rload(int32_t f) { Event e; e.kind = Kind::RunaheadLoad; e.frame = f; return e; }
static Event ev_adv(int32_t f, bool rb, bool ahead = false) { Event e; e.kind = Kind::Advance; e.frame = f; e.rolling_back = rb; e.running_ahead = ahead; for (int s = 0; s < jsi::kSlots; s++) { e.in[s] = mk((uint16_t)(f + s), (uint8_t)s); e.confirmed[s] = true; } return e; }

static void test_protocol()
{
    Stepper st; st.table().set_mask(0xF);
    CHECK(st.boundary(0).err == Err::NoSession);
    st.begin(3900, 4);                                                     // a real session starts at the fight's tick (frame base), GekkoNet frames at 0
    CHECK(st.boundary(3899).err == Err::TickLost);
    st.push(ev_save(-1)); Op o = st.boundary(3900); CHECK(o.kind == OpKind::Commit && o.tick == 3900);   // the first Save of a session is Save(-1) = done base
    st.push(ev_adv(0, false)); o = st.boundary(3900); CHECK(o.kind == OpKind::Run && o.tick == 3901 && !o.replay && !o.regrant);
    CHECK(st.table().has(3901, 3) && st.table().get(3901, 3).buttons == 3);   // 4 slots reach the table
    // livelock and pad-edge cases: the granted tick has not completed; the permission stays as a Run again, never None, and says `regrant`
    o = st.boundary(3900); CHECK(o.kind == OpKind::Run && o.tick == 3901 && o.regrant && st.stats().stalled == 1);
    o = st.boundary(3900); CHECK(o.kind == OpKind::Run && o.tick == 3901 && o.regrant && st.stats().stalled == 2);
    st.push(ev_save(0));
    o = st.boundary(3901); CHECK(o.kind == OpKind::Commit && o.tick == 3901 && st.current() == 3901);   // the tick completed; Save(0) follows Advance(0)
    o = st.boundary(3901); CHECK(o.kind == OpKind::None);
    st.push(ev_adv(1, false)); o = st.boundary(3901); CHECK(o.kind == OpKind::Run && !o.regrant);   // a new grant is not a re-grant
    o = st.boundary(3905); CHECK(o.kind == OpKind::Error && o.err == Err::TickLost);
    { Stepper z; z.begin(3900, 4); z.push(ev_save(3899)); CHECK(z.boundary(3900).err == Err::SaveFrame); }   // an absolute frame (the old begin(100)+Save(99) shape) is not GekkoNet's
    Stepper s2; s2.begin(0, 4);
    s2.push(ev_save(1)); CHECK(s2.boundary(0).err == Err::SaveFrame);
    Stepper s2b; s2b.begin(0, 4); s2b.push(ev_save(0)); CHECK(s2b.boundary(0).err == Err::SaveFrame);   // Save(0) before Advance(0): the old (wrong) order is refused
    Stepper s3; s3.begin(0, 4); s3.push(ev_adv(3, false)); CHECK(s3.boundary(0).err == Err::AdvanceFrame);
    auto live = [](Stepper& x, int32_t f) { x.push(ev_adv(f, false)); Op r = x.boundary((uint32_t)f); x.push(ev_save(f)); Op c = x.boundary((uint32_t)f + 1); return r.kind == OpKind::Run && c.kind == OpKind::Commit; };
    Stepper s4; s4.begin(0, 2); s4.push(ev_save(-1)); CHECK(s4.boundary(0).kind == OpKind::Commit);
    for (int32_t f = 0; f < 6; f++) CHECK(live(s4, f));
    CHECK(s4.boundary(6).kind == OpKind::None);
    s4.push(ev_load(5)); CHECK(s4.boundary(6).kind == OpKind::Rollback);                    // Save(5) = done 6 = current: allowed
    Stepper s5; s5.begin(0, 2); s5.push(ev_save(-1)); s5.boundary(0);
    for (int32_t f = 0; f < 6; f++) live(s5, f);
    s5.boundary(6); s5.push(ev_load(1)); CHECK(s5.boundary(6).err == Err::DepthExceeded);   // Save(1) = done 2 evicted (6 - 2 = 4)
    Stepper s6; s6.begin(0, 8); s6.push(ev_load(0)); CHECK(s6.boundary(0).err == Err::StaleToken);   // never saved
    // rollback + replay: Load(L); Advance(L+1,rb) Save(L+1) ...; flags follow the events
    Stepper s7; s7.begin(0, 8); s7.push(ev_save(-1)); s7.boundary(0);
    for (int32_t f = 0; f < 5; f++) live(s7, f);
    s7.boundary(5);
    s7.push(ev_load(1)); s7.push(ev_adv(2, true)); s7.push(ev_save(2)); s7.push(ev_adv(3, true)); s7.push(ev_save(3)); s7.push(ev_adv(4, false)); s7.push(ev_save(4));
    o = s7.boundary(5); CHECK(o.kind == OpKind::Rollback && o.tick == 2 && s7.replaying());   // Load(1) = done 2
    o = s7.boundary(2); CHECK(o.kind == OpKind::Run && o.tick == 3 && o.replay && s7.replaying());
    o = s7.boundary(3); CHECK(o.kind == OpKind::Commit && o.tick == 3);
    o = s7.boundary(3); CHECK(o.kind == OpKind::Run && o.tick == 4 && o.replay);
    o = s7.boundary(4); CHECK(o.kind == OpKind::Commit && o.tick == 4);
    o = s7.boundary(4); CHECK(o.kind == OpKind::Run && o.tick == 5 && !o.replay && !s7.replaying());   // the live tick: normal clock, presented
    o = s7.boundary(5); CHECK(o.kind == OpKind::Commit && o.tick == 5);
    o = s7.boundary(5); CHECK(o.kind == OpKind::None && s7.stats().rollbacks == 1 && s7.stats().rollback_ticks == 3 && s7.stats().replayed == 2);
    s7.push(ev_load(3)); CHECK(s7.boundary(5).kind == OpKind::Rollback);                    // Save(3) = done 4: valid
    s7.push(ev_load(4)); CHECK(s7.boundary(4).err == Err::StaleToken);                       // Save(4) = done 5 was dropped by that rollback
    // run-ahead: RunaheadSave(F) before the hidden advances (done F), RunaheadLoad(F) at the next update; hidden ticks run on the replay clock
    Stepper s8; s8.allow_runahead(true); s8.begin(0, 8); s8.push(ev_save(-1)); s8.boundary(0); live(s8, 0); live(s8, 1);   // cur 2
    s8.push(ev_rsave(2)); s8.push(ev_adv(2, false, true)); s8.push(ev_adv(3, false, false));
    CHECK(s8.boundary(2).kind == OpKind::Commit);
    o = s8.boundary(2); CHECK(o.kind == OpKind::Run && o.replay);                         // hidden
    o = s8.boundary(3); CHECK(o.kind == OpKind::Run && o.tick == 4 && !o.replay);         // the display frame
    s8.push(ev_rload(2)); o = s8.boundary(4); CHECK(o.kind == OpKind::Rollback && o.tick == 2 && !o.replay && !s8.replaying());
    s8.push(ev_rload(2)); CHECK(s8.boundary(2).err == Err::NoRunaheadSave);              // already consumed
    Stepper s9; s9.begin(0, 8); s9.push(ev_save(-1)); s9.boundary(0); s9.push(ev_rsave(0)); CHECK(s9.boundary(0).err == Err::RunaheadOff);   // refused by default
    Stepper s10; s10.begin(0, 8); s10.push(ev_save(-1)); s10.boundary(0); s10.push(ev_adv(0, false, true)); CHECK(s10.boundary(0).err == Err::RunaheadOff);
    // review cases: rollback right after a run-ahead whose load was skipped (rollback pending), and Load(-1)
    Stepper sc; sc.allow_runahead(true); sc.begin(0, 8); sc.push(ev_save(-1)); sc.boundary(0);
    for (int32_t f = 0; f < 3; f++) live(sc, f);
    sc.push(ev_rsave(3)); sc.push(ev_adv(3, false, true)); sc.push(ev_adv(4, false));
    sc.boundary(3); sc.boundary(3); sc.boundary(4);                                          // hidden 4, display 5: done 5
    sc.push(ev_load(1)); sc.push(ev_adv(2, true)); sc.push(ev_save(2));
    o = sc.boundary(5); CHECK(o.kind == OpKind::Rollback && o.tick == 2);
    o = sc.boundary(2); CHECK(o.kind == OpKind::Run && o.tick == 3 && o.replay);
    Stepper sd; sd.begin(7000, 4); sd.push(ev_save(-1)); sd.boundary(7000); sd.push(ev_adv(0, false)); sd.boundary(7000); sd.push(ev_save(0)); sd.boundary(7001); sd.push(ev_load(-1)); o = sd.boundary(7001); CHECK(o.kind == OpKind::Rollback && o.tick == 7000);
}

// ---------------------------------------------------------------------------------------------------- randomized netplay model in GekkoNet's event order
// Fake game: state = hash chain of the served inputs. Fake ring: state per completed tick (what jsp_snapshot's ring gives back).
struct FakeGame {
    uint64_t S = 0x1111; uint32_t done = 0;
    std::map<uint32_t, uint64_t> ring;
    Stepper st; jse::Gate gate;
    int sim_ticks = 0, replay_ticks = 0, live_ticks = 0, presented = 0, errors = 0, hidden = 0;
    uint64_t chain[8192] = {};               // state after tick t (absolute), as last simulated
    Op iterate() {                            // one main-loop iteration at the boundary
        for (;;) {
            Op o = st.boundary(done);
            switch (o.kind) {
            case OpKind::Commit: ring[done] = S; break;
            case OpKind::Rollback: S = ring.at(o.tick); done = o.tick; gate.rollback(done); gate.set_replay(o.replay); break;
            case OpKind::Run: {
                uint64_t h = S;
                for (int s = 0; s < jsi::kSlots; s++) { jsi::Pad p; if (st.table().serve(done + 1, s, &p)) h = mix(h, p.key() + (uint64_t)s); }
                S = h; done++; chain[done] = S; sim_ticks++;
                if (o.replay) replay_ticks++; else { live_ticks++; presented++; }
                return o; }
            case OpKind::Error: errors++; return o;
            case OpKind::None: return o;
            }
        }
    }
    void drain() { while (st.queued() > 0 && !errors) { Op o = iterate(); if (o.kind == OpKind::Error) std::printf("  stepper error %s at done=%u\n", err_name(o.err), done); } }
};

static jsi::Pad true_input(int slot, uint32_t frame, uint64_t seed)
{
    uint64_t h = mix(mix(seed, (uint64_t)slot), frame / 3);   // changes every 3 frames, so predictions (repeat last) are right some of the time
    return mk((uint16_t)(h & 0xFFFF), (uint8_t)(h >> 20), true);
}

// GekkoNet's Update() as the stepper must see it (game_session.cpp:200-250, 778-830, 997-1034): [RunaheadLoad] [first Save(cur-1)] [Load(min-1), (Advance rb, Save)*]
// Advance(cur) Save(cur) [RunaheadSave(cur) Advance(ahead)*]. Frames are 0-based; Advance(F) simulates tick F+1; remote inputs arrive `delay` updates after
// their frame was first simulated; the prediction window stalls the live advance when cur - confirmed >= depth.
static void run_netplay(uint64_t seed, int slots, uint32_t total, uint32_t maxdelay, uint32_t depth, bool force_every_tick, uint32_t runahead, int* corrections_out, uint32_t base = 0)
{
    FakeGame g; g.done = base; g.st.allow_runahead(runahead > 0); g.st.table().set_mask((1u << slots) - 1); g.st.begin(base, depth);
    rnd_state = seed;
    uint64_t ref = 0x1111; std::vector<uint64_t> refchain(total + 1, 0);
    for (uint32_t t = 1; t <= total; t++) { uint64_t h = ref; for (int s = 0; s < slots; s++) h = mix(h, true_input(s, t - 1, seed).key() + (uint64_t)s); ref = h; refchain[t] = ref; }
    std::vector<std::vector<uint32_t>> delay(total + 2, std::vector<uint32_t>(jsi::kSlots, 0)), first_up(total + 2, std::vector<uint32_t>(jsi::kSlots, jsi::kNone));
    for (uint32_t f = 0; f < total; f++) for (int s = 1; s < slots; s++) delay[f][s] = force_every_tick ? 1 : (maxdelay ? rnd() % (maxdelay + 1) : 0);
    std::vector<std::vector<jsi::Pad>> used(total + 2, std::vector<jsi::Pad>(jsi::kSlots));
    std::vector<std::vector<char>> known(total + 2, std::vector<char>(jsi::kSlots, 0));
    jsi::Pad last_remote[jsi::kSlots];
    uint32_t cf = 0;                          // confirmed frames: every remote input of frames < cf is known
    uint32_t cur = 0;                         // GekkoNet's current frame (next to advance)
    bool first = true; int64_t ra_frame = -1; int corrections = 0;
    uint32_t up = 0; const uint32_t guard = total * 40 + 400;
    auto inputs_for = [&](int32_t f, Event* e) {
        for (int s = 0; s < slots; s++) {
            if (s == 0) { e->in[s] = true_input(0, (uint32_t)f, seed); e->confirmed[s] = true; continue; }
            if ((uint32_t)f < total && known[f][s]) { e->in[s] = true_input(s, (uint32_t)f, seed); e->confirmed[s] = true; }
            else { e->in[s] = last_remote[s]; e->in[s].connected = true; e->confirmed[s] = false; }
        }
    };
    auto note_used = [&](int32_t f, const Event& e) { for (int s = 0; s < slots; s++) used[f][s] = e.in[s]; };
    while ((cur < total || cf < total) && up++ < guard) {
        // arrivals
        uint32_t minbad = jsi::kNone;
        for (uint32_t f = cf; f < cur; f++) for (int s = 1; s < slots; s++) {
            if (known[f][s]) continue;
            if (first_up[f][s] == jsi::kNone || up < first_up[f][s] + delay[f][s]) continue;
            known[f][s] = 1; last_remote[s] = true_input(s, f, seed);
            if (used[f][s] != true_input(s, f, seed)) minbad = std::min(minbad, f);
        }
        while (cf < cur) { bool all = true; for (int s = 1; s < slots; s++) all = all && known[cf][s]; if (!all) break; cf++; }
        // 1. RewindRunahead (skipped when a rollback will load anyway)
        if (ra_frame >= 0) { if (minbad == jsi::kNone) g.st.push(ev_rload((int32_t)ra_frame)); ra_frame = -1; }
        // 2. HandleRollback
        if (first) { g.st.push(ev_save((int32_t)cur - 1)); first = false; }
        if (minbad != jsi::kNone) {
            corrections++;
            const int32_t sync = (int32_t)minbad - 1;
            g.st.push(ev_load(sync));
            for (int32_t f = sync + 1; f < (int32_t)cur; f++) { Event e; e.kind = Kind::Advance; e.frame = f; e.rolling_back = true; inputs_for(f, &e); g.st.push(e); note_used(f, e); g.st.push(ev_save(f)); }
        }
        // 3. live advance + Save
        const bool can = cur < total && (cur - cf) < depth;
        if (can) {
            Event e; e.kind = Kind::Advance; e.frame = (int32_t)cur; e.rolling_back = false; inputs_for((int32_t)cur, &e); g.st.push(e); note_used((int32_t)cur, e);
            g.st.push(ev_save((int32_t)cur));
            for (int s = 1; s < slots; s++) if (first_up[cur][s] == jsi::kNone) first_up[cur][s] = up;
            cur++;
            // 4. run-ahead
            if (runahead && cur < total) {
                ra_frame = cur; g.st.push(ev_rsave((int32_t)cur));
                for (uint32_t i = 0; i < runahead && cur + i < total; i++) { Event e2; e2.kind = Kind::Advance; e2.frame = (int32_t)(cur + i); e2.running_ahead = (i + 1 < runahead); inputs_for((int32_t)(cur + i), &e2); g.st.push(e2); }
            }
        }
        g.drain();
        if (g.errors) break;
        // first_up for ticks first simulated inside a rollback is already set; remote slots only need the first live time
    }
    if (ra_frame >= 0 && !g.errors) { g.st.push(ev_rload((int32_t)ra_frame)); g.drain(); g.iterate(); }   // the last update's run-ahead is rewound by the next one
    CHECK(g.errors == 0);
    CHECK(g.done == base + total);
    CHECK(g.S == ref);                                         // final state equals straight-through input replay
    uint32_t bad = 0; for (uint32_t t = 1; t <= cf && t <= total; t++) if (g.chain[base + t] != refchain[t]) bad++;
    CHECK(bad == 0);
    CHECK(g.st.stats().max_rollback <= depth + runahead);
    CHECK((uint64_t)g.live_ticks + (uint64_t)g.replay_ticks == (uint64_t)g.sim_ticks);
    CHECK(g.presented == g.live_ticks);
    if (corrections_out) *corrections_out += corrections;
}

static void test_netplay()
{
    int corr = 0;
    for (int slots : {2, 4})
        for (uint32_t depth : {1u, 2u, 4u, 8u})
            for (uint64_t seed = 1; seed <= 6; seed++) {
                run_netplay(seed * 977 + slots * 31 + depth, slots, 400, depth, depth, false, 0, &corr);
                run_netplay(seed * 131 + slots * 7 + depth, slots, 300, depth, depth + 1, false, 2, &corr);   // with run-ahead (allowed explicitly)
            for (uint64_t seed = 1; seed <= 3; seed++) run_netplay(seed * 7 + slots, slots, 300, depth, depth, false, 0, &corr, 3900);   // a session that starts at the fight's tick
            }
    int rapid = 0;
    for (uint64_t seed = 1; seed <= 4; seed++) run_netplay(seed, 2, 300, 1, 8, true, 0, &rapid);
    std::printf("netplay model (GekkoNet event order): %d corrections in the random runs, %d in the rapid runs\n", corr, rapid);
    CHECK(corr > 100 && rapid > 100);
}

int main()
{
    test_input(); test_effects(); test_protocol(); test_netplay();
    std::printf("stepper test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
