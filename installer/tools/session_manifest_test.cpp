// Host test for the host-controlled session settings of the rollback netcode: installer/port/jsp/jsp_session_{digest,registry,effective,manifest,handshake}.h.
// No game, no GPU. installer/tools/session_manifest_test.sh builds and runs it.
#include "../port/jsp/jsp_session_handshake.h"
#include "../port/jsp/jsp_mods_glue.h"   // the mod-set fold into Caps::build (jmods::net_build)
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <memory>
#include <map>

using namespace jpsm;
static int g_checks = 0, g_fail = 0;
static bool g_print_epoch = false;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static std::string hex(const Digest& d) { char b[3]; std::string s; for (uint8_t x : d) { std::snprintf(b, 3, "%02x", x); s += b; } return s; }

// ---- fakes ----
struct FakeReader : Reader {
    std::array<uint32_t, kCount> startup{};   // actual startup values
    std::array<bool, kCount> have_startup{};
    Values live{};                             // what the "game" reads now
    int lie_id = -1;                           // read_back returns a wrong value for this field
    int applied_calls = 0, restored_calls = 0;
    EffectiveValues* ev = nullptr;
    bool no_restore = false, throw_applied = false, throw_restored = false;
    const char* dbg = nullptr;
    FakeReader() { have_startup.fill(true); for (size_t i = 0; i < kCount; ++i) startup[i] = kRules[i].def; startup[(size_t)Id::KoTarget] = 3; }
    const char* debug_switch() override { return dbg; }
    bool startup_value(Id id, uint32_t* o) override { const size_t i = (size_t)id; if (!have_startup[i]) return false; *o = startup[i]; return true; }
    void applied(const Values& v) override { ++applied_calls; if (throw_applied) throw 1; live = v; }
    bool read_back(Id id, uint32_t* o) override { *o = live.v[(size_t)id] + ((int)id == lie_id ? 1 : 0); return true; }
    void restored() override { ++restored_calls; if (throw_restored) throw 2; if (!no_restore && ev) live = ev->local_values(); }
};
struct Msgs { uint8_t from, to; std::vector<uint8_t> b; };
struct Net : Transport {
    std::deque<Msgs> q;
    uint8_t from = 0;
    int drop_type = -1;   // drop messages of this Msg type
    int hold = -1; std::vector<uint8_t> held;   // capture (and do not deliver) the next message of this Msg type
    int drop_once = -1;   // drop the next message of this Msg type only
    void send(uint8_t peer, const uint8_t* d, size_t n) override
    {
        if (hold >= 0 && n > 4 && d[4] == hold) { hold = -1; held.assign(d, d + n); return; }
        if (drop_type >= 0 && n > 4 && d[4] == drop_type) return;
        if (drop_once >= 0 && n > 4 && d[4] == drop_once) { drop_once = -1; return; }
        q.push_back({ from, peer, std::vector<uint8_t>(d, d + n) });
    }
};
struct Peer {
    FakeReader rd; EffectiveValues ev; Net net;
    std::unique_ptr<Client> cl; std::unique_ptr<Host> host;
    Peer() : ev(&rd) { rd.ev = &ev; }
};
// One shared medium: each peer has its own Net as the send hook (stamps `from`), the pump routes by `to`.
struct Room {
    Peer p[4]; uint8_t roster = 0; uint64_t now = 0;
    Caps caps; bool reverse = false;
    Room(uint8_t roster_, Caps c = Caps()) : roster(roster_), caps(c) { caps.build = 0xB001; for (int i = 0; i < 4; ++i) p[i].net.from = (uint8_t)i; }
    void make_host(Config cfg = {}) { p[0].host = std::make_unique<Host>(p[0].net, p[0].ev, caps, 0, cfg); }
    void make_client(int i, Caps c, Config cfg = {}) { p[i].cl = std::make_unique<Client>(p[i].net, p[i].ev, c, (uint8_t)i, cfg); }
    void pump()
    {
        for (int guard = 0; guard < 200; ++guard) {
            std::vector<Msgs> all;
            for (auto& q : p) { while (!q.net.q.empty()) { all.push_back(q.net.q.front()); q.net.q.pop_front(); } }
            if (all.empty()) return;
            if (reverse) std::reverse(all.begin(), all.end());
            for (auto& m : all) {
                if (m.to == 0 && p[0].host) p[0].host->on_message(m.from, m.b.data(), m.b.size(), now);
                else if (m.to < 4 && p[m.to].cl) p[m.to].cl->on_message(m.from, m.b.data(), m.b.size(), now);
            }
        }
    }
    void run_to(uint64_t t, bool host = true, bool clients = true)   // 1 s steps, like a frame loop: silence is real silence
    {
        while (now + 1000 < t) { now += 1000; step(host, clients); }
        now = t; step(host, clients);
    }
    void step(bool host, bool clients) { if (host && p[0].host) p[0].host->tick(now); if (clients) for (int i = 1; i < 4; ++i) if (p[i].cl) p[i].cl->tick(now); pump(); }
    void tick(uint64_t t) { now = t; if (p[0].host) p[0].host->tick(now); for (int i = 1; i < 4; ++i) if (p[i].cl) p[i].cl->tick(now); pump(); }
    void join_all() { for (int i = 1; i < 4; ++i) if (p[i].cl) p[i].cl->join(77, 0, now); pump(); }
};
static Values rb_values() { Values v = defaults(); v.v[(size_t)Id::Rollback] = 1; v.v[(size_t)Id::InputDelay] = 3; v.v[(size_t)Id::KoTarget] = 3; v.v[(size_t)Id::StageId] = 7; return v; }
static bool all_clean(Room& r) { for (auto& q : r.p) if (q.ev.active()) return false; return true; }
static Room* happy(uint8_t roster, std::unique_ptr<Room>& hold, Values v = rb_values())
{
    hold = std::make_unique<Room>(roster);
    Room& r = *hold;
    r.make_host();
    for (int i = 1; i < 4; ++i) if (roster & (1u << i)) r.make_client(i, r.caps);
    r.p[0].host->begin(77, roster, 0x1234, r.now);
    r.join_all();
    CHECK(r.p[0].host->propose(v, r.now) == Reason::None);
    r.pump();
    return &r;
}

int main(int argc, char** argv)
{
    g_print_epoch = argc > 1 && !std::strcmp(argv[1], "--epoch");
    // --- digest ---
    CHECK(hex(sha256((const uint8_t*)"abc", 3)) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(hex(sha256(nullptr, 0)) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    { const char* t = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
      CHECK(hex(sha256((const uint8_t*)t, 56)) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
      std::vector<uint8_t> a(1000000, 'a'); Sha256 s; s.update(a.data(), 333333); s.update(a.data() + 333333, 666667);
      CHECK(hex(s.finish()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"); }

    // --- registry ---
    for (int i = 0; i < jps::k_item_count; ++i) { const char* k = jps::k_items[i].key; CHECK(is_synchronized(k) || is_local_only(k)); }
    for (int i = 0; i < jps::k_launcher_only_count; ++i) { const char* k = jps::k_launcher_only[i].key; CHECK(is_synchronized(k) || is_local_only(k)); }
    CHECK(jps::k_action_count > 0 && is_local_only("(key and pad bindings)"));
    for (int i = 0; i < jps::k_action_count; ++i) CHECK(!is_synchronized(jps::k_actions[i].id));   // no binding is a synchronized field
    for (size_t i = 0; i < kCount; ++i) { CHECK(rule((Id)i).lo <= rule((Id)i).def && rule((Id)i).def <= rule((Id)i).hi); CHECK(!is_local_only(rule((Id)i).name)); }
    for (size_t i = (size_t)Id::ModelDetail; i <= (size_t)Id::Render60; ++i) {   // the graphics profile follows jpss
        jpss::Key k; CHECK(shares_jpss((Id)i, &k));
        CHECK(!std::strcmp(rule((Id)i).name, jpss::rule(k)->name));
        CHECK(rule((Id)i).hi + 1 == jpss::rule(k)->count);
        CHECK(rule((Id)i).cls == NetClass::SharedProfile);
    }
    CHECK(defaults() == Values{ defaults() });
    CHECK(Caps().supports(Id::ModelDetail, 0) && !Caps().supports(Id::ModelDetail, 1));   // unproven options held conservative
    CHECK(!Caps().supports(Id::SimRateHz, 60) && Caps().supports(Id::SimRateHz, 30));
    { Values v = defaults(); CHECK(v.v[(size_t)Id::Rollback] == 0); CHECK(value_text(Id::ObjectDistance, 1) == "1"); }
    { Id id; CHECK(!values_valid([] { Values v = defaults(); v.v[(size_t)Id::KoTarget] = 6; return v; }(), &id) && id == Id::KoTarget); }
    { Id id; CHECK(!values_valid([] { Values v = defaults(); v.v[(size_t)Id::TrainInfStamina] = 1; return v; }(), &id) && id == Id::TrainInfStamina); }

    // --- manifest ---
    Manifest m; m.session = 77; m.revision = 1; m.host = 0; m.roster = 0x07; m.seed = 0xdeadbeef; m.values = rb_values();
    const auto mb = encode_manifest(m);
    CHECK(mb.size() == 29 + 5 * kCount && mb.size() <= kMaxMessage);
    { Manifest o; CHECK(decode_manifest(mb.data(), mb.size(), &o) == Reason::None && o == m); }
    CHECK(manifest_digest(m) == manifest_digest(m));
    for (size_t i = 0; i < kCount; ++i) { Manifest x = m; x.values.v[i] ^= (i == (size_t)Id::StageId ? 1 : 0); if (i == (size_t)Id::StageId) CHECK(manifest_digest(x) != manifest_digest(m)); }
    { Manifest x = m; x.seed++; CHECK(manifest_digest(x) != manifest_digest(m)); x = m; x.revision++; CHECK(manifest_digest(x) != manifest_digest(m)); x = m; x.roster = 0x0f; CHECK(manifest_digest(x) != manifest_digest(m)); }
    {   // strict decode
        Manifest o; auto b = mb;
        for (size_t cut = 0; cut < b.size(); ++cut) CHECK(decode_manifest(b.data(), cut, &o) != Reason::None);   // every truncation
        b = mb; b.push_back(0); CHECK(decode_manifest(b.data(), b.size(), &o) == Reason::Malformed);               // trailing byte
        b = mb; b[0] = 'X'; CHECK(decode_manifest(b.data(), b.size(), &o) == Reason::Malformed);
        b = mb; b[4] = 3; CHECK(decode_manifest(b.data(), b.size(), &o) == Reason::SchemaMismatch);
        b = mb; b[28] = (uint8_t)(kCount + 1); CHECK(decode_manifest(b.data(), b.size(), &o) == Reason::Malformed);   // unknown/extra field count
        b = mb; b[29] = 1; CHECK(decode_manifest(b.data(), b.size(), &o) == Reason::Malformed);                      // field out of order
        Manifest bad = m; bad.roster = 0x06; CHECK(decode_manifest(encode_manifest(bad).data(), encode_manifest(bad).size(), &o) == Reason::Malformed);   // host not in roster
        bad = m; bad.session = 0; CHECK(decode_manifest(encode_manifest(bad).data(), encode_manifest(bad).size(), &o) == Reason::Malformed);
        std::vector<uint8_t> big(kMaxMessage + 1, 0); CHECK(decode_manifest(big.data(), big.size(), &o) == Reason::Malformed);
    }
    { uint8_t slot; Caps c, o; c.build = 5; c.rollback = false; auto h = encode_hello(9, 2, c); CHECK(h.size() <= kMaxMessage);
      CHECK(decode_hello(h.data(), h.size(), &slot, &o) == Reason::None && slot == 2 && o.build == 5 && !o.rollback && o.hi == c.hi);
      h.push_back(1); CHECK(decode_hello(h.data(), h.size(), &slot, &o) == Reason::Malformed); }

    // --- effective-value path ---
    {
        FakeReader rd; EffectiveValues ev(&rd); rd.ev = &ev; int persisted = 0; ev.set_persist([&](Id, uint32_t) { ++persisted; });
        CHECK(ev.get(Id::InputDelay).src == Src::Default && ev.get(Id::InputDelay).value == 2);
        CHECK(ev.set_local(Id::InputDelay, 5) && persisted == 1 && ev.get(Id::InputDelay).src == Src::Local && ev.get(Id::InputDelay).value == 5);
        CHECK(!ev.set_local(Id::InputDelay, 99));   // out of range refused
        Digest ad; Values req = rb_values();
        CHECK(ev.install(1, 1, req, &ad) == Res::Ok && ev.active());
        CHECK(ev.get(Id::InputDelay).value == 3 && ev.get(Id::InputDelay).src == Src::Session && ev.get(Id::InputDelay).controlled);
        CHECK(ev.controlled_by_host("MODEL_DETAIL") && ev.controlled_by_host("JSP_KO_TARGET") && !ev.controlled_by_host("RES_SCALE"));
        CHECK(!ev.set_local(Id::InputDelay, 7) && ev.get(Id::InputDelay).value == 3);   // menu/env/file reload cannot change a required value
        CHECK(ev.effective() == req);
        CHECK(ad == values_digest(1, 1, req) && ad != values_digest(1, 2, req) && ad != values_digest(2, 1, req));
        CHECK(persisted == 2);   // only the two set_local calls above; install never persists
        ev.clear(); CHECK(!ev.active() && ev.get(Id::InputDelay).value == 7 && ev.get(Id::InputDelay).src == Src::Local);   // the change made during the session survives
        CHECK(persisted == 2 && rd.restored_calls == 1);
        ev.clear(); CHECK(rd.restored_calls == 1);   // idempotent
        // failures leave no layer
        rd.lie_id = (int)Id::StageId; CHECK(ev.install(1, 2, req, &ad) == Res::ReadbackMismatch && !ev.active()); rd.lie_id = -1;
        rd.have_startup[(size_t)Id::Pipe] = false; CHECK(ev.install(1, 2, req, &ad) == Res::NoStartupEvidence && !ev.active() && ev.fail_id() == Id::Pipe);
        rd.have_startup[(size_t)Id::Pipe] = true; rd.startup[(size_t)Id::Pipe] = 0; CHECK(ev.install(1, 2, req, &ad) == Res::RestartRequired && !ev.active());
        rd.startup[(size_t)Id::Pipe] = 1;
        Values bad = req; bad.v[(size_t)Id::KoTarget] = 9; CHECK(ev.install(1, 2, bad, &ad) == Res::OutOfRange && !ev.active());
        CHECK(ev.install(1, 3, req, &ad) == Res::Ok);
        CHECK(ev.install(1, 4, bad, &ad) == Res::OutOfRange && ev.active() && ev.revision() == 3);   // failed validation keeps the previous layer
    }

    // --- handshake: happy path, 3 players, in order and reversed ---
    for (int rev = 0; rev < 2; ++rev) {
        std::unique_ptr<Room> hold; Room& r = *happy(0x07, hold);
        r.reverse = rev;
        CHECK(r.p[0].host->state() == State::Proposed || r.p[0].host->state() == State::Ready);
        r.pump();
        CHECK(r.p[0].host->state() == State::Ready && r.p[0].host->acks() == 0x07 && r.p[0].host->revision() == 1);
        CHECK(r.p[1].cl->applied() && r.p[2].cl->applied());
        for (int i = 0; i < 3; ++i) CHECK(r.p[i].ev.effective() == rb_values());
        CHECK(r.p[0].host->start(500) == Reason::None); r.pump();
        CHECK(r.p[1].cl->state() == State::Started && r.p[2].cl->state() == State::Started && r.p[1].cl->anchor() == 500);
        for (int i = 0; i < 3; ++i) CHECK(r.p[i].ev.active() && r.p[i].ev.get(Id::Rollback).value == 1);
        r.p[0].host->end_match(); r.p[1].cl->match_ended(); r.p[2].cl->match_ended();
        CHECK(all_clean(r));
        CHECK(r.p[0].host->dropped() == 0);
    }
    {   // the manifest is identical on every peer (also over a room-server style channel with the other config)
        std::unique_ptr<Room> hold; Room& r = *happy(0x03, hold);
        CHECK(r.p[1].cl->manifest() == r.p[0].host->manifest() && r.p[0].host->channel() == Channel::DirectIp);
    }

    // --- mismatch: a client that cannot honour rollback refuses with a clear reason ---
    {
        Room r(0x03); r.make_host(); Caps old; old.build = r.caps.build; old.rollback = false; r.make_client(1, old);
        r.p[0].host->begin(77, 0x03, 1, 0); r.join_all();
        // the host already knows from the Hello: the proposal is refused locally, nothing was sent, nothing installed
        CHECK(r.p[0].host->propose(rb_values(), 0) == Reason::RollbackUnsupported && r.p[0].host->bad_peer() == 1 && r.p[0].host->bad_id() == Id::Rollback);
        CHECK(r.p[0].host->state() == State::Open && !r.p[0].ev.active() && !r.p[1].ev.active());
        CHECK(std::strstr(explain(Reason::RollbackUnsupported), "rollback") != nullptr);
        // rollback off is accepted by the same client
        Values off = rb_values(); off.v[(size_t)Id::Rollback] = 0;
        CHECK(r.p[0].host->propose(off, 0) == Reason::None); r.pump(); CHECK(r.p[0].host->state() == State::Ready);
    }
    {   // client-side refusal when the host did not check (a forged/older host): Reject, client restored, host cancels the rest
        Room r(0x07); r.make_host(); Caps old = r.caps; old.rollback = false; r.make_client(1, old); r.make_client(2, r.caps);
        r.p[0].host->begin(77, 0x07, 1, 0);
        // client 1 says it supports rollback in its Hello (a lie) but refuses the manifest because its own Caps say no: use a hello from a good caps
        Caps liar = old; liar.rollback = true; auto h = encode_hello(77, 1, liar);
        r.p[0].host->on_message(1, h.data(), h.size(), 0);
        r.p[1].cl->join(77, 0, 0); r.p[2].cl->join(77, 0, 0);
        // the real client-1 hello is replaced by the liar's: drop the queued honest one
        auto& q = r.p[1].net.q; q.clear();
        r.pump();
        CHECK(r.p[0].host->propose(rb_values(), 0) == Reason::None); r.pump();
        CHECK(r.p[1].cl->state() == State::Failed && r.p[1].cl->reason() == Reason::RollbackUnsupported && !r.p[1].ev.active());
        CHECK(r.p[0].host->state() == State::Failed && r.p[0].host->reason() == Reason::RollbackUnsupported && r.p[0].host->bad_peer() == 1);
        CHECK(r.p[2].cl->state() == State::Cancelled && r.p[2].cl->reason() == Reason::RollbackUnsupported);
        CHECK(all_clean(r));
    }
    {   // host refuses values a peer's caps do not support (unproven option) and out-of-range values
        Room r(0x03); r.make_host(); r.make_client(1, r.caps); r.p[0].host->begin(77, 0x03, 1, 0); r.join_all();
        Values v = rb_values(); v.v[(size_t)Id::ModelDetail] = 2; CHECK(r.p[0].host->propose(v, 0) == Reason::Unsupported && r.p[0].host->bad_id() == Id::ModelDetail);
        v = rb_values(); v.v[(size_t)Id::SimRateHz] = 60; CHECK(r.p[0].host->propose(v, 0) == Reason::OutOfRange);
        v = rb_values(); v.v[(size_t)Id::UnlockAll] = 1; CHECK(r.p[0].host->propose(v, 0) == Reason::OutOfRange);
        CHECK(r.p[0].host->state() == State::Open && r.p[0].host->revision() == 0 && all_clean(r));
    }
    {   // client-side limits narrower than the host's (a client without a wide delay range)
        Room r(0x03); Caps narrow = r.caps; narrow.build = 0xB001; narrow.hi[(size_t)Id::InputDelay] = 1; r.make_host(); r.make_client(1, narrow);
        r.p[0].host->begin(77, 0x03, 1, 0); r.join_all();
        CHECK(r.p[0].host->propose(rb_values(), 0) == Reason::Unsupported && r.p[0].host->bad_peer() == 1 && r.p[0].host->bad_id() == Id::InputDelay);
    }
    {   // build mismatch fails at the Hello with the peer named
        Room r(0x03); r.make_host(); Caps other = r.caps; other.build = 0xB002; r.make_client(1, other);
        r.p[0].host->begin(77, 0x03, 1, 0); r.join_all();
        CHECK(r.p[0].host->state() == State::Failed && r.p[0].host->reason() == Reason::BuildMismatch && r.p[0].host->bad_peer() == 1);
    }
    // --- the mod set folded into the build word (jsp_mods_glue.h net_build, used by jsp_net_game.cpp for Caps::build) ---
    {
        const uint32_t stock = 0xB001;
        const char* s1 = "set one"; const char* s2 = "set two";
        const Digest d1 = sha256((const uint8_t*)s1, 8), d2 = sha256((const uint8_t*)s2, 8);
        uint32_t m1 = 0, m1b = 0, m2 = 0, none = 0;
        CHECK(jmods::net_build(stock, jmods::DigestKind::Value, d1, m1) && jmods::net_build(stock, jmods::DigestKind::Value, d1, m1b) && m1 == m1b);
        CHECK(jmods::net_build(stock, jmods::DigestKind::Value, d2, m2) && m2 != m1 && m1 != stock && m2 != stock);
        CHECK(jmods::net_build(stock, jmods::DigestKind::None, d1, none) && none == stock);   // no gameplay digest: build_id() unchanged
        uint32_t refused = 0;
        CHECK(!jmods::net_build(stock, jmods::DigestKind::Refused, jmods::refused_digest(), refused));   // refused: no word, rollback never starts
        auto run = [&](uint32_t host_build, uint32_t client_build, State* host_state, Reason* host_reason) {
            Room r(0x03); r.caps.build = host_build; r.make_host();
            Caps cc = r.caps; cc.build = client_build; r.make_client(1, cc);
            r.p[0].host->begin(77, 0x03, 1, 0); r.join_all();
            if (r.p[0].host->state() == State::Open) { CHECK(r.p[0].host->propose(rb_values(), 0) == Reason::None); r.pump(); }
            if (r.p[0].host->state() == State::Ready) { CHECK(r.p[0].host->start(500) == Reason::None); r.pump(); }
            *host_state = r.p[0].host->state(); *host_reason = r.p[0].host->reason();
            return r.p[1].cl->state();
        };
        State hs; Reason hr;
        CHECK(run(m1, m1b, &hs, &hr) == State::Started && hs == State::Started);                    // the same set: the session starts
        CHECK(run(m1, m2, &hs, &hr) != State::Started && hs == State::Failed && hr == Reason::BuildMismatch);   // another set: BuildMismatch
        CHECK(run(m1, none, &hs, &hr) != State::Started && hs == State::Failed && hr == Reason::BuildMismatch); // modded host, stock client
        CHECK(run(none, stock, &hs, &hr) == State::Started && hs == State::Started);                // stock with the fold = an old stock peer
        CHECK(std::strstr(explain(Reason::BuildMismatch), "build") != nullptr);
    }
    {   // build mismatch seen by the client (host build in the proposal differs from the client's)
        Room r(0x03); r.make_host(); r.make_client(1, r.caps); r.p[0].host->begin(77, 0x03, 1, 0); r.join_all();
        Manifest mm; mm.session = 77; mm.revision = 1; mm.host = 0; mm.roster = 3; mm.seed = 1; mm.values = defaults();
        std::vector<uint8_t> b; put_header(b, Msg::Propose, 77, 1); auto e = encode_manifest(mm); b.insert(b.end(), e.begin(), e.end()); wire::put(b, 0xBAD, 4);
        r.p[1].cl->on_message(0, b.data(), b.size(), 0);
        CHECK(r.p[1].cl->state() == State::Failed && r.p[1].cl->reason() == Reason::BuildMismatch);
    }
    {   // schema mismatch from a future/older peer, at host and client
        Room r(0x03); r.make_host(); r.make_client(1, r.caps); r.p[0].host->begin(77, 0x03, 1, 0);
        std::vector<uint8_t> b; put_header(b, Msg::Hello, 77, 0); b[5] = 9;
        r.p[0].host->on_message(1, b.data(), b.size(), 0);
        CHECK(r.p[0].host->state() == State::Failed && r.p[0].host->reason() == Reason::SchemaMismatch);
        Room r2(0x03); r2.make_host(); r2.make_client(1, r2.caps); r2.p[0].host->begin(77, 3, 1, 0); r2.join_all();
        std::vector<uint8_t> c2; put_header(c2, Msg::Propose, 77, 1); c2[5] = 9;
        r2.p[1].cl->on_message(0, c2.data(), c2.size(), 0);
        CHECK(r2.p[1].cl->state() == State::Failed && r2.p[1].cl->reason() == Reason::SchemaMismatch);
    }
    {   // startup-only mismatch and cheat on at the client: Reject with RestartRequired / CheatActive, never "applied"
        for (int which = 0; which < 3; ++which) {
            Room r(0x03); r.make_host(); r.make_client(1, r.caps);
            if (which == 0) r.p[1].rd.startup[(size_t)Id::Pipe] = 0;
            if (which == 1) r.p[1].rd.startup[(size_t)Id::UnlockAll] = 1;
            if (which == 2) r.p[1].rd.have_startup[(size_t)Id::Pipe] = false;
            r.p[0].host->begin(77, 3, 1, 0); r.join_all(); r.p[0].host->propose(rb_values(), 0); r.pump();
            const Reason want = which == 0 ? Reason::RestartRequired : which == 1 ? Reason::CheatActive : Reason::NoStartupEvidence;
            CHECK(r.p[1].cl->state() == State::Failed && r.p[1].cl->reason() == want && !r.p[1].cl->applied());
            CHECK(r.p[0].host->state() == State::Failed && r.p[0].host->reason() == want && all_clean(r));
        }
    }
    {   // read-back mismatch (the real reader did not take the value)
        Room r(0x03); r.make_host(); r.make_client(1, r.caps); r.p[1].rd.lie_id = (int)Id::InputDelay;
        r.p[0].host->begin(77, 3, 1, 0); r.join_all(); r.p[0].host->propose(rb_values(), 0); r.pump();
        CHECK(r.p[1].cl->state() == State::Failed && r.p[1].cl->reason() == Reason::ApplyFailed && all_clean(r) && r.p[0].host->reason() == Reason::ApplyFailed);
        Room r3(0x03); r3.make_host(); r3.make_client(1, r3.caps); r3.p[0].rd.lie_id = (int)Id::KoTarget;   // the host's own read-back
        r3.p[0].host->begin(77, 3, 1, 0); r3.join_all();
        CHECK(r3.p[0].host->propose(rb_values(), 0) == Reason::ApplyFailed && !r3.p[0].ev.active() && !r3.p[1].ev.active());
    }
    {   // forged ack digests and stale/wrong-sender acks
        std::unique_ptr<Room> hold; Room& r = *happy(0x07, hold); (void)r;
        Room r2(0x07); r2.make_host(); r2.make_client(1, r2.caps); r2.make_client(2, r2.caps); r2.p[0].host->begin(77, 7, 1, 0); r2.join_all();
        r2.p[2].net.drop_type = (int)Msg::Ack;   // client 2's ack never arrives
        r2.p[0].host->propose(rb_values(), 0); r2.pump();
        CHECK(r2.p[0].host->acks() == 0x03);
        auto good = r2.p[1].cl->manifest(); (void)good;
        std::vector<uint8_t> b; put_header(b, Msg::Ack, 77, 1); wire::put(b, 2, 1); wire::put(b, r2.p[2].cl->epoch(), 4); Digest z{}; b.insert(b.end(), z.begin(), z.end()); b.insert(b.end(), z.begin(), z.end());
        r2.p[0].host->on_message(1, b.data(), b.size(), 0);               // sender 1 claims slot 2: dropped
        CHECK(r2.p[0].host->state() == State::Proposed && r2.p[0].host->dropped() >= 1);
        // a stale revision
        std::vector<uint8_t> st; put_header(st, Msg::Ack, 77, 9); wire::put(st, 2, 1); wire::put(st, r2.p[2].cl->epoch(), 4); st.insert(st.end(), z.begin(), z.end()); st.insert(st.end(), z.begin(), z.end());
        r2.p[0].host->on_message(2, st.data(), st.size(), 0);
        CHECK(r2.p[0].host->state() == State::Proposed && r2.p[0].host->acks() == 0x03);   // stale ack cannot complete readiness
        std::vector<uint8_t> forged; put_header(forged, Msg::Ack, 77, 1); wire::put(forged, 2, 1); wire::put(forged, r2.p[2].cl->epoch(), 4); forged.insert(forged.end(), z.begin(), z.end()); forged.insert(forged.end(), z.begin(), z.end());
        r2.p[0].host->on_message(2, forged.data(), forged.size(), 0);
        CHECK(r2.p[0].host->state() == State::Failed && r2.p[0].host->reason() == Reason::DigestMismatch && r2.p[0].host->bad_peer() == 2); r2.pump(); CHECK(all_clean(r2));
        CHECK(r2.p[0].host->start(1) == Reason::NotReady);
    }
    {   // wrong session, non-roster sender, non-host sender, garbage: dropped without effect
        Room r(0x03); r.make_host(); r.make_client(1, r.caps); r.p[0].host->begin(77, 3, 1, 0); r.join_all(); r.p[0].host->propose(rb_values(), 0); r.pump();
        std::vector<uint8_t> g(40, 0xAA);
        r.p[0].host->on_message(1, g.data(), g.size(), 0); r.p[1].cl->on_message(0, g.data(), g.size(), 0); r.p[1].cl->on_message(2, g.data(), g.size(), 0);
        auto h = encode_hello(55, 1, r.caps); r.p[0].host->on_message(1, h.data(), h.size(), 0);
        std::vector<uint8_t> pr; put_header(pr, Msg::Propose, 78, 5); r.p[1].cl->on_message(0, pr.data(), pr.size(), 0);
        std::vector<uint8_t> cn = detail::cancel_bytes(77, 1, Reason::HostCancelled, r.p[1].cl->epoch()); r.p[1].cl->on_message(3, cn.data(), cn.size(), 0);   // not from the host
        CHECK(r.p[0].host->state() == State::Ready && r.p[1].cl->state() == State::Proposed && r.p[1].cl->applied());
        CHECK(r.p[1].cl->dropped() >= 4);
    }
    {   // duplicate proposal is idempotent; a newer revision replaces; an older one is ignored; same revision different values is rejected
        Room r(0x03); r.make_host(); r.make_client(1, r.caps); r.p[0].host->begin(77, 3, 1, 0); r.join_all(); r.p[0].host->propose(rb_values(), 0); r.pump();
        auto first = r.p[0].host->manifest();
        Peer& c = r.p[1]; const int ap = c.rd.applied_calls;
        std::vector<uint8_t> b; put_header(b, Msg::Propose, 77, 1); auto e = encode_manifest(first); b.insert(b.end(), e.begin(), e.end()); wire::put(b, 0xB001, 4);
        c.cl->on_message(0, b.data(), b.size(), 0); r.pump();
        CHECK(c.rd.applied_calls == ap && c.cl->state() == State::Proposed && c.net.q.empty());
        Values v2 = rb_values(); v2.v[(size_t)Id::InputDelay] = 5;   // lobby change: revision 2, readiness invalidated
        r.p[0].host->propose(v2, 0); CHECK(r.p[0].host->state() == State::Proposed && r.p[0].host->acks() == 1); r.pump();
        CHECK(r.p[0].host->state() == State::Ready && r.p[0].host->revision() == 2 && c.ev.get(Id::InputDelay).value == 5);
        c.cl->on_message(0, b.data(), b.size(), 0);   // delayed old proposal
        CHECK(c.cl->revision() == 2 && c.ev.get(Id::InputDelay).value == 5);
        // same revision, different content
        Manifest forge = r.p[0].host->manifest(); forge.values.v[(size_t)Id::StageId] = 1;
        std::vector<uint8_t> f; put_header(f, Msg::Propose, 77, 2); auto fe = encode_manifest(forge); f.insert(f.end(), fe.begin(), fe.end()); wire::put(f, 0xB001, 4);
        c.cl->on_message(0, f.data(), f.size(), 0);
        CHECK(c.cl->state() == State::Failed && c.cl->reason() == Reason::StaleRevision && !c.ev.active());
    }
    {   // a start message for an older revision / wrong digest cannot start a changed configuration
        Room r(0x03); r.make_host(); r.make_client(1, r.caps); r.p[0].host->begin(77, 3, 1, 0); r.join_all(); r.p[0].host->propose(rb_values(), 0); r.pump();
        std::vector<uint8_t> s; put_header(s, Msg::Start, 77, 1); Digest z{}; s.insert(s.end(), z.begin(), z.end()); wire::put(s, 5, 4);
        r.p[1].cl->on_message(0, s.data(), s.size(), 0); CHECK(r.p[1].cl->state() == State::Proposed);
        std::vector<uint8_t> s2; put_header(s2, Msg::Start, 77, 4); const Digest d = manifest_digest(r.p[0].host->manifest()); s2.insert(s2.end(), d.begin(), d.end()); wire::put(s2, 5, 4);
        r.p[1].cl->on_message(0, s2.data(), s2.size(), 0); CHECK(r.p[1].cl->state() == State::Proposed);
    }

    // --- roster change invalidates readiness ---
    {
        Room r(0x07); r.make_host(); r.make_client(1, r.caps); r.make_client(2, r.caps); r.p[0].host->begin(77, 7, 1, 0); r.join_all();
        r.p[0].host->propose(rb_values(), 0); r.pump(); CHECK(r.p[0].host->state() == State::Ready);
        CHECK(r.p[0].host->set_roster(0x03, 10) == Reason::None);
        CHECK(r.p[0].host->state() == State::Proposed && r.p[0].host->revision() == 2 && r.p[0].host->acks() == 1);   // re-proposed by itself
        CHECK(r.p[0].host->start(1) == Reason::NotReady);
        r.pump();
        CHECK(r.p[2].cl->state() == State::Cancelled && r.p[2].cl->reason() == Reason::RosterChanged && !r.p[2].ev.active());
        CHECK(r.p[0].host->state() == State::Ready && r.p[0].host->revision() == 2 && r.p[0].host->manifest().roster == 0x03);
        CHECK(r.p[1].cl->revision() == 2);
    }
    // --- timeouts ---
    {   // host: a client never says hello (an older build that does not speak this protocol)
        Room r(0x07); r.make_host(); r.make_client(1, r.caps); r.p[0].host->begin(77, 7, 1, 0);
        r.p[1].cl->join(77, 0, 0); r.pump();
        CHECK(r.p[0].host->propose(rb_values(), 0) == Reason::NotReady && r.p[0].host->bad_peer() == 2);
        r.run_to(14999); CHECK(r.p[0].host->state() == State::Open);
        r.run_to(15000); CHECK(r.p[0].host->state() == State::TimedOut && r.p[0].host->reason() == Reason::Timeout && r.p[0].host->bad_peer() == 2);
        CHECK(terminal(r.p[1].cl->state()) && all_clean(r));
    }
    {   // host: a client applied but its ack is lost
        Room r(0x03); r.make_host(); r.make_client(1, r.caps); r.p[0].host->begin(77, 3, 1, 0); r.join_all();
        r.p[1].net.drop_type = (int)Msg::Ack; r.p[0].host->propose(rb_values(), 100); r.pump();
        CHECK(r.p[1].ev.active() && r.p[0].host->state() == State::Proposed);
        r.now = 100; r.run_to(100 + 14999); CHECK(r.p[0].host->state() == State::Proposed);
        r.run_to(100 + 15000); CHECK(r.p[0].host->state() == State::TimedOut && r.p[0].host->bad_peer() == 1);
        r.pump(); CHECK(!r.p[0].ev.active());
    }
    {   // client: a silent host (no keep-alive) is lost after 10 s; a host that only waits is waited for indefinitely
        Room r(0x03); r.make_host(); r.make_client(1, r.caps); r.p[0].host->begin(77, 3, 1, 0); r.p[1].cl->join(77, 0, 0); r.pump();
        r.p[1].cl->tick(9999); CHECK(r.p[1].cl->state() == State::Open);
        r.p[1].cl->tick(10000); CHECK(r.p[1].cl->state() == State::Failed && r.p[1].cl->reason() == Reason::HostLost && !r.p[1].ev.active());
        Room q(0x03); q.make_host(); q.make_client(1, q.caps); q.p[0].host->begin(77, 3, 1, 0); q.join_all(); q.p[0].host->propose(rb_values(), 0); q.pump();
        CHECK(q.p[1].ev.active());
        q.run_to(300000);                                    // five minutes in character select: both sides tick, keep-alives flow
        CHECK(q.p[1].cl->state() == State::Proposed && q.p[1].ev.active() && q.p[0].host->state() == State::Ready && q.p[0].ev.active());
        const auto w = q.p[1].cl->host_wait(q.now);
        CHECK(w.waiting && w.waited_ms == 300000 && w.silent_ms < 2000 && w.host_state == State::Ready);   // what the UI shows
        CHECK(q.p[0].host->start(7) == Reason::None); q.pump(); CHECK(q.p[1].cl->state() == State::Started && !q.p[1].cl->host_wait(q.now).waiting);
        q.run_to(400000); CHECK(q.p[1].cl->state() == State::Started && q.p[0].host->state() == State::Started);   // still alive during the match
        q.p[0].host->handover(); q.p[1].cl->handover(); q.run_to(500000, false, true);                          // the rollback session owns liveness: no more silence checks
        CHECK(q.p[1].cl->state() == State::Started && q.p[1].ev.active());
        // keep-alive loss while applied: HostLost, layer removed, host notices the client's Cancel
        Room z(0x03); z.make_host(); z.make_client(1, z.caps); z.p[0].host->begin(77, 3, 1, 0); z.join_all(); z.p[0].host->propose(rb_values(), 0); z.pump();
        z.run_to(4000);                                                                    // the host's last keep-alive left at 4000
        z.run_to(13999, false, true); CHECK(z.p[1].cl->state() == State::Proposed);         // host silent for 9.999 s
        z.run_to(14000, false, true); CHECK(z.p[1].cl->state() == State::Failed && z.p[1].cl->reason() == Reason::HostLost && !z.p[1].ev.active());
        z.pump(); CHECK(z.p[0].host->state() == State::Failed && !z.p[0].ev.active() && std::strstr(explain(Reason::HostLost), "host"));
        // lost keep-alives only (messages of other types still flow) also end it
        Room y(0x03); y.make_host(); y.make_client(1, y.caps); y.p[0].host->begin(77, 3, 1, 0); y.join_all(); y.p[0].host->propose(rb_values(), 0); y.pump();
        y.p[0].net.drop_type = (int)Msg::KeepAlive; y.run_to(20000);
        CHECK(y.p[1].cl->reason() == Reason::HostLost && !y.p[1].ev.active());
    }
    {   // host: a silent client is dropped from the roster before Start (and the rest are re-proposed); after Start it ends the match
        Room r(0x07); r.make_host(); r.make_client(1, r.caps); r.make_client(2, r.caps); r.p[0].host->begin(77, 7, 1, 0); r.join_all(); r.p[0].host->propose(rb_values(), 0); r.pump();
        CHECK(r.p[0].host->state() == State::Ready);
        r.p[2].net.drop_type = (int)Msg::KeepAlive;        // client 2 stops sending keep-alives
        r.run_to(20000);
        CHECK(r.p[0].host->lost_peers() == 0x04 && r.p[0].host->lost_reason() == Reason::PeerLost);
        CHECK(r.p[0].host->state() == State::Ready && r.p[0].host->manifest().roster == 0x03 && r.p[0].host->revision() == 2 && r.p[1].cl->applied());
        CHECK(r.p[2].cl->state() == State::Cancelled && r.p[2].cl->reason() == Reason::PeerLost && !r.p[2].ev.active());
        CHECK(r.p[0].host->start(1) == Reason::None);
        Room q(0x03); q.make_host(); q.make_client(1, q.caps); q.p[0].host->begin(77, 3, 1, 0); q.join_all(); q.p[0].host->propose(rb_values(), 0); q.pump(); q.p[0].host->start(1); q.pump();
        q.p[1].net.drop_type = (int)Msg::KeepAlive; q.run_to(12000);
        CHECK(q.p[0].host->state() == State::Failed && q.p[0].host->reason() == Reason::PeerLost && q.p[0].host->bad_peer() == 1 && !q.p[0].ev.active());
        CHECK(q.p[1].cl->state() == State::Cancelled && q.p[1].cl->reason() == Reason::PeerLost && !q.p[1].ev.active());
    }
    {   // a client that restarts mid-match (new process, new epoch): its Hello is ignored, the old epoch goes silent, the host ends with PeerLost
        Room r(0x03); r.make_host(); r.make_client(1, r.caps); r.p[0].host->begin(77, 3, 1, 0); r.join_all(); r.p[0].host->propose(rb_values(), 0); r.pump(); r.p[0].host->start(1); r.pump();
        r.run_to(4000);
        r.p[1].cl.reset(); r.p[1].net.q.clear();                        // the old process is gone
        r.make_client(1, r.caps); r.p[1].cl->join(77, 0, r.now); r.pump();
        CHECK(r.p[0].host->state() == State::Started);                  // the new Hello alone does not change a running match
        r.run_to(13000); CHECK(r.p[0].host->state() == State::Started);
        r.run_to(14001);
        CHECK(r.p[0].host->state() == State::Failed && r.p[0].host->reason() == Reason::PeerLost && !r.p[0].ev.active());
        CHECK(r.p[1].cl->state() == State::Failed && r.p[1].cl->reason() == Reason::HostLost && !r.p[1].ev.active());   // the new process was never served
    }
    {   // every reason has an explanation the overlay can show
        for (uint8_t i = 1; i <= kLastReason; ++i) { const char* t = explain((Reason)i); CHECK(t && std::strcmp(t, "unknown") != 0 && std::strlen(t) > 10); }
    }
    // --- restore on every exit path ---
    for (int path = 0; path < 6; ++path) {
        std::unique_ptr<Room> hold; Room& r = *happy(0x07, hold);
        CHECK(!all_clean(r) && r.p[0].ev.active() && r.p[1].ev.active() && r.p[2].ev.active());
        r.p[1].ev.set_local(Id::InputDelay, 6);
        switch (path) {
        case 0: r.p[1].cl->leave(); r.pump(); break;                       // client leaves after applying
        case 1: r.p[0].host->cancel(); r.pump(); break;                    // host cancels
        case 2: r.p[0].host->peer_left(2); r.p[2].cl->leave(); r.pump(); break;                // a peer drops
        case 3: r.p[0].host->start(1); r.pump(); r.p[0].host->end_match(); r.p[1].cl->match_ended(); r.p[2].cl->match_ended(); break;   // clean end
        case 4: r.p[0].host.reset(); r.p[1].cl.reset(); r.p[2].cl.reset(); break;      // destruction (process exit/error unwinding)
        case 5: { std::vector<uint8_t> cn = detail::cancel_bytes(77, 1, Reason::PeerLeft, r.p[2].cl->epoch());
                  r.p[0].host->on_message(2, cn.data(), cn.size(), 0); r.p[2].cl->leave(); r.pump(); break; }   // a client's own Cancel
        }
        CHECK(all_clean(r));
        CHECK(r.p[0].ev.effective() == r.p[0].ev.local_values());
        CHECK(r.p[1].ev.get(Id::InputDelay).value == 6 && r.p[1].ev.get(Id::InputDelay).src == Src::Local);
        CHECK(r.p[0].rd.restored_calls >= 1 && r.p[1].rd.restored_calls >= 1);
        if (path != 4) { CHECK(terminal(r.p[0].host->state())); }
        if (path == 0 || path == 1 || path == 2 || path == 5) CHECK(terminal(r.p[2].cl->state()) || path == 0 || path == 5 || path == 2);
        if (path == 1) CHECK(r.p[1].cl->state() == State::Cancelled && r.p[1].cl->reason() == Reason::HostCancelled);
        if (path == 0) CHECK(r.p[0].host->state() == State::Failed && r.p[2].cl->state() == State::Cancelled);
    }
    {   // a fresh session after cancel starts from the clean state
        std::unique_ptr<Room> hold; Room& r = *happy(0x03, hold); r.p[0].host->cancel(); r.pump();
        CHECK(r.p[0].host->begin(78, 3, 9, 0) == Reason::None);
        r.p[1].cl->join(78, 0, 0); r.pump(); CHECK(r.p[0].host->propose(rb_values(), 0) == Reason::None); r.pump(); CHECK(r.p[0].host->state() == State::Ready);
    }

    // --- local files, environment and identities untouched ---
    {
        namespace fs = std::filesystem;
        const char* tmp = std::getenv("TMPDIR"); fs::path dir = fs::path(tmp ? tmp : "/tmp") / ("jsp_session_test_" + std::to_string((long)std::rand()));
        fs::create_directories(dir);
        const char* files[] = { "play_settings.txt", "repsn.ini", "save.dat" };
        std::map<std::string, std::string> before;
        for (const char* f : files) { std::ofstream o(dir / f, std::ios::binary); o << "KEY=" << f << "\nINPUT_DELAY=1\n"; }
        auto snap = [&] { std::map<std::string, std::string> s; for (auto& e : fs::directory_iterator(dir)) { std::ifstream i(e.path(), std::ios::binary); std::stringstream ss; ss << i.rdbuf(); s[e.path().filename().string()] = ss.str() + "|" + std::to_string(fs::file_size(e.path())); } return s; };
        before = snap();
        std::string env_before; for (const char* k : { "JSP_ROLLBACK", "JSP_INPUT_DELAY", "JSP_KO_TARGET", "PIPE", "MODEL_DETAIL", "PS3_NP_ONLINE_ID" }) env_before += std::string(k) + "=" + (std::getenv(k) ? std::getenv(k) : "<unset>") + ";";
        int persisted = 0;
        for (int path = 0; path < 3; ++path) {
            std::unique_ptr<Room> hold; Room& r = *happy(0x07, hold);
            for (auto& p : r.p) p.ev.set_persist([&](Id, uint32_t) { ++persisted; });
            // run again with persist hooks installed: a second session on the same objects
            r.p[0].host->cancel(); r.pump();
            r.p[0].host->begin(90 + path, 7, 5, 0);
            for (int i = 1; i < 3; ++i) r.p[i].cl->join(90 + path, 0, 0);
            r.pump(); r.p[0].host->propose(rb_values(), 0); r.pump();
            if (path == 0) r.p[0].host->start(3);
            if (path == 1) r.p[0].host->cancel();
            r.pump();
        }
        CHECK(persisted == 0);
        CHECK(snap() == before);
        std::string env_after; for (const char* k : { "JSP_ROLLBACK", "JSP_INPUT_DELAY", "JSP_KO_TARGET", "PIPE", "MODEL_DETAIL", "PS3_NP_ONLINE_ID" }) env_after += std::string(k) + "=" + (std::getenv(k) ? std::getenv(k) : "<unset>") + ";";
        CHECK(env_after == env_before);
        std::error_code ec; fs::remove_all(dir, ec);
    }

    // ================= review cases, part 1 =================
    auto propose_bytes = [](const Manifest& m, uint32_t build) { std::vector<uint8_t> b; put_header(b, Msg::Propose, m.session, m.revision); auto e = encode_manifest(m); b.insert(b.end(), e.begin(), e.end()); wire::put(b, build, 4); return b; };
    // duplicate Propose after Start is a no-op; a higher revision after Start is refused
    {
        std::unique_ptr<Room> hold; Room& r = *happy(0x03, hold);
        const Manifest man = r.p[0].host->manifest();
        CHECK(r.p[0].host->start(5) == Reason::None); r.pump();
        CHECK(r.p[1].cl->state() == State::Started);
        const auto dup = propose_bytes(man, 0xB001);
        r.p[1].cl->on_message(0, dup.data(), dup.size(), 0); r.pump();
        CHECK(r.p[1].cl->state() == State::Started && r.p[1].ev.active() && r.p[0].host->state() == State::Started && r.p[0].ev.active());
        Manifest m2 = man; m2.revision = 2; m2.values.v[(size_t)Id::InputDelay] = 7;
        const auto up = propose_bytes(m2, 0xB001);
        r.p[1].cl->on_message(0, up.data(), up.size(), 0); r.pump();
        CHECK(r.p[1].cl->state() == State::Started && r.p[1].ev.get(Id::InputDelay).value == 3 && r.p[0].host->state() == State::Started);
        // in Proposed state too: duplicate re-acks, never rejects
        std::unique_ptr<Room> h2; Room& q = *happy(0x03, h2);
        const auto d2 = propose_bytes(q.p[0].host->manifest(), 0xB001);
        q.p[1].cl->on_message(0, d2.data(), d2.size(), 0); q.pump();
        CHECK(q.p[1].cl->state() == State::Proposed && q.p[0].host->state() == State::Ready);
    }
    // Delayed teardown of an older object never clears a newer session's layer in the shared EffectiveValues
    {
        Room r(0x03); r.make_host(); r.make_client(1, r.caps);
        auto old = std::move(r.p[1].cl); old->join(76, 0, 0); old->leave(); r.p[1].net.q.clear();
        r.make_client(1, r.caps);
        r.p[0].host->begin(77, 3, 1, 0); r.join_all(); r.p[0].host->propose(rb_values(), 0); r.pump(); r.p[0].host->start(9); r.pump();
        old.reset();
        CHECK(r.p[1].cl->state() == State::Started && r.p[1].cl->applied() && r.p[1].ev.active() && r.p[1].ev.get(Id::Rollback).value == 1);
        // a fresh join/begin of the new object does not clear a layer owned by someone else either
        Room q(0x03); q.make_host(); q.make_client(1, q.caps);
        q.p[0].host->begin(77, 3, 1, 0); q.join_all(); q.p[0].host->propose(rb_values(), 0); q.pump();
        auto oldh = std::move(q.p[0].host); q.p[0].host.reset();
        q.make_host(); q.p[0].host->begin(78, 3, 2, 0); q.p[1].cl->leave(); q.p[1].net.q.clear(); q.p[1].cl->join(78, 0, 0); q.pump();
        q.p[0].host->propose(rb_values(), 0); q.pump();
        CHECK(q.p[0].ev.active());
        oldh.reset();
        CHECK(q.p[0].ev.active() && q.p[0].host->state() == State::Ready && q.p[0].ev.owned_by(q.p[0].host.get()));
        // EffectiveValues-level: clear_owned on another owner is a no-op, unconditional clear still works
        FakeReader rd; EffectiveValues ev(&rd); rd.ev = &ev; int a = 0, b = 0; Digest d;
        CHECK(ev.install(1, 1, rb_values(), &d, &a) == Res::Ok && ev.install(2, 1, rb_values(), &d, &b) == Res::Ok);
        ev.clear_owned(&a); CHECK(ev.active()); ev.clear_owned(&b); CHECK(!ev.active());
    }
    // boundaries match the real readers (registration-time reads)
    {
        CHECK(rule(Id::KoTarget).boundary == Boundary::Startup && rule(Id::TrainInfStamina).boundary == Boundary::Startup && rule(Id::TrainNormalGain).boundary == Boundary::Startup);
        FakeReader rd; EffectiveValues ev(&rd); rd.ev = &ev; Digest d; Values v = rb_values();
        rd.startup[(size_t)Id::KoTarget] = 0;   // the reader read no KO target at registration
        CHECK(ev.install(1, 1, v, &d) == Res::RestartRequired && ev.fail_id() == Id::KoTarget && !ev.active());
        rd.startup[(size_t)Id::KoTarget] = 3; rd.startup[(size_t)Id::TrainInfStamina] = 1;
        CHECK(ev.install(1, 1, v, &d) == Res::RestartRequired && ev.fail_id() == Id::TrainInfStamina);
    }
    // m2: strict Reject/Cancel decode
    {
        std::unique_ptr<Room> hold; Room& r = *happy(0x03, hold);
        std::vector<uint8_t> cn; put_header(cn, Msg::Cancel, 77, 1);
        r.p[1].cl->on_message(0, cn.data(), cn.size(), 0); CHECK(r.p[1].cl->state() == State::Proposed);             // header only
        cn = detail::cancel_bytes(77, 1, Reason::HostCancelled, r.p[1].cl->epoch()); cn[kHeaderSize] = 200; r.p[1].cl->on_message(0, cn.data(), cn.size(), 0); CHECK(r.p[1].cl->state() == State::Proposed);   // reason out of range
        cn[kHeaderSize] = (uint8_t)Reason::HostCancelled; cn.push_back(0); r.p[1].cl->on_message(0, cn.data(), cn.size(), 0); CHECK(r.p[1].cl->state() == State::Proposed);   // trailing byte
        std::vector<uint8_t> rj; put_header(rj, Msg::Reject, 77, 1); rj.push_back(200); rj.push_back(2);
        r.p[0].host->on_message(1, rj.data(), rj.size(), 0); CHECK(r.p[0].host->state() == State::Ready);             // reason out of range
        rj[rj.size() - 2] = (uint8_t)Reason::OutOfRange; rj[rj.size() - 1] = 250; r.p[0].host->on_message(1, rj.data(), rj.size(), 0); CHECK(r.p[0].host->state() == State::Ready);   // id out of range
        rj[rj.size() - 1] = 2; rj.push_back(1); r.p[0].host->on_message(1, rj.data(), rj.size(), 0); CHECK(r.p[0].host->state() == State::Ready);   // trailing byte
        std::vector<uint8_t> hd; put_header(hd, Msg::Reject, 77, 1); r.p[0].host->on_message(1, hd.data(), hd.size(), 0); CHECK(r.p[0].host->state() == State::Ready);
        rj.pop_back(); r.p[0].host->on_message(1, rj.data(), rj.size(), 0);
        CHECK(r.p[0].host->state() == State::Failed && r.p[0].host->reason() == Reason::OutOfRange && r.p[0].host->bad_id() == (Id)2);   // the valid one ends it
        CHECK(r.p[0].host->dropped() >= 4);
    }
    // m3: a client that rejected an older revision ends the handshake with its own reason, not a generic timeout
    {
        Room r(0x03); r.make_host(); r.make_client(1, r.caps); r.p[1].rd.startup[(size_t)Id::Pipe] = 0;
        r.p[0].host->begin(77, 3, 1, 0); r.join_all();
        r.p[0].host->propose(rb_values(), 0); Values v2 = rb_values(); v2.v[(size_t)Id::InputDelay] = 4; r.p[0].host->propose(v2, 0); r.pump();
        CHECK(r.p[1].cl->state() == State::Failed && r.p[0].host->state() == State::Failed && r.p[0].host->reason() == Reason::RestartRequired && r.p[0].host->bad_id() == Id::Pipe);
        CHECK(all_clean(r));
    }
    // m4: fail_id names the real field
    {
        FakeReader rd; EffectiveValues ev(&rd); rd.ev = &ev; Digest d;
        rd.lie_id = (int)Id::KoTarget; CHECK(ev.install(1, 1, rb_values(), &d) == Res::ReadbackMismatch && ev.fail_id() == Id::KoTarget); rd.lie_id = -1;
        Values bad = rb_values(); bad.v[(size_t)Id::StageId] = 1000; CHECK(ev.install(1, 1, bad, &d) == Res::OutOfRange && ev.fail_id() == Id::StageId);
        rd.have_startup[(size_t)Id::Pipe] = false; CHECK(ev.install(1, 1, rb_values(), &d) == Res::NoStartupEvidence && ev.fail_id() == Id::Pipe); rd.have_startup[(size_t)Id::Pipe] = true;
        CHECK(ev.install(1, 1, rb_values(), &d) == Res::Ok && ev.fail_id() == Id::Rollback);   // reset on success
        // and the handshake carries it
        Room r(0x03); r.make_host(); r.make_client(1, r.caps); r.p[1].rd.lie_id = (int)Id::TimerSeconds; r.p[0].host->begin(77, 3, 1, 0); r.join_all(); r.p[0].host->propose(rb_values(), 0); r.pump();
        CHECK(r.p[1].cl->bad_id() == Id::TimerSeconds && r.p[0].host->bad_id() == Id::TimerSeconds);
    }
    // m5: Hello after the proposal
    {
        std::unique_ptr<Room> hold; Room& r = *happy(0x03, hold);
        Caps same = r.caps; same.epoch = r.p[1].cl->epoch(); auto h = encode_hello(77, 1, same); r.p[0].host->on_message(1, h.data(), h.size(), 0);
        CHECK(r.p[0].host->state() == State::Ready);                                       // repeated Hello: ignored
        Caps narrow = r.caps; narrow.epoch = r.p[1].cl->epoch(); narrow.hi[(size_t)Id::InputDelay] = 1; h = encode_hello(77, 1, narrow); r.p[0].host->on_message(1, h.data(), h.size(), 0);
        CHECK(r.p[0].host->state() == State::Failed && r.p[0].host->reason() == Reason::Unsupported && r.p[0].host->bad_id() == Id::InputDelay);   // re-checked against the open proposal
        std::unique_ptr<Room> h2; Room& q = *happy(0x03, h2); q.p[0].host->start(1); q.pump();
        Caps other = q.caps; other.build = 1; h = encode_hello(77, 1, other); q.p[0].host->on_message(1, h.data(), h.size(), 0);
        CHECK(q.p[0].host->state() == State::Started && q.p[0].ev.active());               // mid-match Hello never ends the match
        std::unique_ptr<Room> h3; Room& w = *happy(0x03, h3); Caps dbg = w.caps; dbg.epoch = w.p[1].cl->epoch(); dbg.debug_active = true; h = encode_hello(77, 1, dbg); w.p[0].host->on_message(1, h.data(), h.size(), 0);
        CHECK(w.p[0].host->state() == State::Failed && w.p[0].host->reason() == Reason::DebugSwitch);
    }
    // m6: a roster change with a new member re-proposes once the new member said hello
    {
        Room r(0x03); r.make_host(); r.make_client(1, r.caps); r.make_client(2, r.caps); r.p[0].host->begin(77, 3, 1, 0); r.join_all();
        r.p[0].host->propose(rb_values(), 0); r.pump();
        CHECK(r.p[0].host->set_roster(0x07, 5) == Reason::None && r.p[0].host->state() == State::Open);
        r.p[2].cl->join(77, 0, 5); r.pump();
        CHECK(r.p[0].host->state() == State::Ready && r.p[0].host->revision() == 2 && r.p[0].host->manifest().roster == 0x07 && r.p[2].cl->applied() && r.p[1].cl->revision() == 2);
    }
    // m7: lost datagrams are repeated (bounded) so no client stays applied without a decision
    {
        Config cfg; cfg.retry_ms = 1000; cfg.retries = 3;
        Room r(0x03); r.make_host(cfg); r.make_client(1, r.caps, cfg);
        r.p[0].net.drop_once = (int)Msg::Propose;                                        // the first Propose is lost
        r.p[0].host->begin(77, 3, 1, 0); r.join_all(); r.p[0].host->propose(rb_values(), 0); r.pump();
        CHECK(r.p[1].cl->state() == State::Open && !r.p[1].ev.active());
        r.tick(1000); CHECK(r.p[1].cl->state() == State::Proposed && r.p[0].host->state() == State::Ready);
        r.p[0].net.drop_once = (int)Msg::Start; r.p[0].host->start(4); r.pump(); CHECK(r.p[1].cl->state() == State::Proposed);
        r.tick(2000); CHECK(r.p[1].cl->state() == State::Started && r.p[1].cl->anchor() == 4);
        r.tick(3000); r.tick(4000); r.tick(5000); r.tick(6000);                          // bounded: nothing pending, still started
        CHECK(r.p[0].host->pending_repeats() == 0 && r.p[1].cl->pending_repeats() == 0 && r.p[1].cl->state() == State::Started);
        // a lost Cancel: the client is told again
        Room q(0x03); q.make_host(cfg); q.make_client(1, q.caps, cfg); q.p[0].host->begin(77, 3, 1, 0); q.join_all(); q.p[0].host->propose(rb_values(), 0); q.pump();
        q.p[0].net.drop_once = (int)Msg::Cancel; q.p[0].host->cancel(); q.pump(); CHECK(q.p[1].ev.active());
        q.tick(1000); CHECK(q.p[1].cl->state() == State::Cancelled && !q.p[1].ev.active());
        // unbounded repetition never happens: a peer that never answers gets a bounded number of Proposes
        Room w(0x03); w.make_host(cfg); w.make_client(1, w.caps, cfg); w.p[0].host->begin(77, 3, 1, 0); w.join_all();
        w.p[1].net.drop_type = (int)Msg::Ack; w.p[0].host->propose(rb_values(), 0); w.pump();
        for (uint64_t t = 1000; t <= 9000; t += 1000) { w.now = t; w.p[0].host->tick(t); w.p[0].net.q.clear(); }
        CHECK(w.p[0].host->pending_repeats() == 0);
    }
    // m8: a throwing reader cannot leave the layer active
    {
        for (int which = 0; which < 3; ++which) {
            Room r(0x03); r.make_host(); r.make_client(1, r.caps);
            if (which == 0) r.p[1].rd.throw_applied = true;
            if (which == 1) { r.p[1].rd.throw_applied = true; r.p[1].rd.throw_restored = true; }
            r.p[0].host->begin(77, 3, 1, 0); r.join_all();
            if (which == 2) { r.p[0].host->propose(rb_values(), 0); r.pump(); r.p[1].rd.throw_restored = true; r.p[1].cl->leave(); CHECK(!r.p[1].ev.active()); continue; }
            r.p[0].host->propose(rb_values(), 0); r.pump();
            CHECK(r.p[1].cl->state() == State::Failed && r.p[1].cl->reason() == Reason::ReaderError && !r.p[1].ev.active() && r.p[0].host->reason() == Reason::ReaderError && all_clean(r));
        }
        FakeReader tr; tr.throw_applied = true; EffectiveValues ev(&tr); Digest d; CHECK(ev.install(1, 1, rb_values(), &d) == Res::ReaderError && !ev.active());
    }
    // m9: clear() verifies the reader shows the local values again
    {
        FakeReader rd; EffectiveValues ev(&rd); rd.ev = &ev; Digest d; ev.set_local(Id::InputDelay, 6);
        CHECK(ev.install(1, 1, rb_values(), &d) == Res::Ok && ev.clear() && ev.restore_ok() && rd.live.v[(size_t)Id::InputDelay] == 6);
        rd.no_restore = true; CHECK(ev.install(1, 2, rb_values(), &d) == Res::Ok && !ev.clear() && !ev.restore_ok() && !ev.active());
    }
    // debug-only switches: listed, and a session refuses to start
    {
        const char* want[] = { "JSP_FIGHT_SEED", "JSP_PAD_TICKS", "JSP_TICK_ANCHOR", "PAD_SCRIPT", "PAD_SCRIPT2", "PAD_CLOCK", "AUTO_FIGHT", "AUTO_MENU", "AUTO_FB", "AUTO_FB_P1",
            "TRAINING_TEST", "USLEEP_AFTER_US", "USLEEP_GAP_US", "USLEEP_REAL_US", "USLEEP_SPIN", "LATE_FLIP", "VBLANK_QPC", "D3D12_VSYNC", "JSP_SPU_DELAY", "JSP_SPU_DROP",
            "JSP_SPU_JITTER_US", "JSP_SPU_JITTER_SEED", "JSP_RENDER60_PADROW" };
        for (const char* w : want) { bool f = false; for (const DebugSwitch& n : kDebugSwitches) f = f || !std::strcmp(n.name, w); CHECK(f); }
        auto only = [](const char* name, const char* val) { return [=](const char* n) -> const char* { return !std::strcmp(n, name) ? val : nullptr; }; };
        CHECK(first_debug_switch([](const char*) -> const char* { return nullptr; }) == nullptr);
        // switches whose default is "on": 1 (the default) is not a debug switch, 0 is
        CHECK(first_debug_switch(only("LATE_FLIP", "1")) == nullptr && !std::strcmp(first_debug_switch(only("LATE_FLIP", "0")), "LATE_FLIP"));
        CHECK(first_debug_switch(only("VBLANK_QPC", "1")) == nullptr && !std::strcmp(first_debug_switch(only("VBLANK_QPC", "0")), "VBLANK_QPC"));
        CHECK(first_debug_switch(only("TRAINING_MODE", "1")) == nullptr && first_debug_switch(only("TRAINING_MODE", "0")) != nullptr);
        CHECK(first_debug_switch(only("PAD_SCRIPT", "")) == nullptr && first_debug_switch(only("PAD_SCRIPT", "x.txt")) != nullptr);   // empty = unset
        CHECK(first_debug_switch(only("USLEEP_SPIN", "0")) == nullptr && first_debug_switch(only("USLEEP_SPIN", "1")) != nullptr);
        CHECK(first_debug_switch(only("JSP_PAD_TICKS", "5")) != nullptr && first_debug_switch(only("D3D12_VSYNC", "0")) != nullptr && first_debug_switch(only("D3D12_VSYNC", "1")) != nullptr);
        for (const DebugSwitch& d : kDebugSwitches) { CHECK(first_debug_switch(only(d.name, d.trigger == Trigger::StartsWithZero ? "0" : "1")) != nullptr); }   // every listed name can fire
        Room r(0x03); r.make_host(); r.make_client(1, r.caps); r.p[0].rd.dbg = "JSP_FIGHT_SEED"; r.p[0].host->begin(77, 3, 1, 0); r.join_all();
        CHECK(r.p[0].host->propose(rb_values(), 0) == Reason::DebugSwitch && !r.p[0].ev.active() && std::strstr(explain(Reason::DebugSwitch), "debug-only"));
        Room q(0x03); q.make_host(); q.make_client(1, q.caps); q.p[1].rd.dbg = "PAD_SCRIPT"; q.p[0].host->begin(77, 3, 1, 0); q.join_all();   // the client's Hello carries it
        CHECK(q.p[0].host->propose(rb_values(), 0) == Reason::DebugSwitch && q.p[0].host->bad_peer() == 1);
        Room w(0x03); w.make_host(); w.make_client(1, w.caps); w.p[0].host->begin(77, 3, 1, 0); w.join_all(); w.p[1].rd.dbg = "LATE_FLIP";   // set after the Hello
        w.p[0].host->propose(rb_values(), 0); w.pump();
        CHECK(w.p[1].cl->reason() == Reason::DebugSwitch && w.p[0].host->reason() == Reason::DebugSwitch && all_clean(w));
    }
    // m10: files-untouched test with a positive control (the detector sees a real preference write)
    {
        namespace fs = std::filesystem;
        const char* tmp = std::getenv("TMPDIR"); fs::path dir = fs::path(tmp ? tmp : "/tmp") / ("jsp_session_test2_" + std::to_string((long)std::rand()));
        fs::create_directories(dir); const fs::path pf = dir / "play_settings.txt"; { std::ofstream o(pf); o << "JSP_INPUT_DELAY=1\n"; }
        auto rd_file = [&] { std::ifstream i(pf); std::stringstream ss; ss << i.rdbuf(); return ss.str(); };
        auto writer = [&](Id id, uint32_t v) { std::ofstream o(pf, std::ios::app); o << rule(id).name << "=" << v << "\n"; };   // what the real preference writer does
        std::unique_ptr<Room> hold; Room& r = *happy(0x03, hold);
        for (auto& p : r.p) p.ev.set_persist(writer);
        const std::string before = rd_file();
        r.p[0].host->start(1); r.pump(); r.p[0].host->end_match(); r.p[1].cl->match_ended();
        r.p[0].host->begin(79, 3, 1, 0); r.p[1].cl->join(79, 0, 0); r.pump(); r.p[0].host->propose(rb_values(), 0); r.pump(); r.p[0].host->cancel(); r.pump();
        CHECK(rd_file() == before);
        r.p[1].ev.set_local(Id::InputDelay, 4);                                          // positive control: a player's change does write
        CHECK(rd_file() != before);
        std::error_code ec; fs::remove_all(dir, ec);
    }

    // ================= review cases, part 2 =================
    // a repeated or late Cancel(RosterChanged) must not split a match after a quick leave and rejoin
    for (int variant = 0; variant < 3; ++variant) {
        Room r(0x07); r.make_host(); r.make_client(1, r.caps); r.make_client(2, r.caps); r.p[0].host->begin(77, 7, 1, 0); r.join_all();
        r.p[0].host->propose(rb_values(), 0); r.pump();
        CHECK(r.p[0].host->set_roster(0x03, 10) == Reason::None); r.pump();
        const uint32_t old_epoch = r.p[2].cl->epoch();
        CHECK(r.p[2].cl->state() == State::Cancelled && r.p[0].host->state() == State::Ready);
        std::vector<uint8_t> stale = detail::cancel_bytes(77, 2, Reason::RosterChanged, old_epoch);   // a copy still in flight
        r.p[2].cl->join(77, 0, 20); r.p[0].host->set_roster(0x07, 20); r.pump();                       // quick leave and rejoin, same session id
        CHECK(r.p[0].host->state() == State::Ready && r.p[0].host->revision() == 3 && r.p[2].cl->applied() && r.p[2].cl->epoch() != old_epoch);
        if (variant == 0) {                                                                           // queued repeats: dropped on the rejoin
            r.p[0].host->tick(1010);
            bool cancel_to_2 = false; for (auto& m : r.p[0].net.q) if (m.to == 2 && m.b.size() > 4 && m.b[4] == (uint8_t)Msg::Cancel) cancel_to_2 = true;
            CHECK(!cancel_to_2);
            r.tick(2010); r.tick(3010);
        }
        if (variant == 1) { r.p[2].cl->on_message(0, stale.data(), stale.size(), 30); }                // a late copy: wrong join
        if (variant == 2) { std::vector<uint8_t> old_rev = detail::cancel_bytes(77, 1, Reason::HostCancelled, r.p[2].cl->epoch()); r.p[2].cl->on_message(0, old_rev.data(), old_rev.size(), 30); }   // right join, older revision
        r.pump();
        CHECK(r.p[2].cl->state() == State::Proposed && r.p[2].ev.active() && r.p[0].host->state() == State::Ready);
        CHECK(r.p[0].host->start(1) == Reason::None); r.pump();
        CHECK(r.p[1].cl->state() == State::Started && r.p[2].cl->state() == State::Started && r.p[2].ev.get(Id::Rollback).value == 1);
    }
    {   // a client that joins again while the host holds a proposal gets it again; its old ack no longer counts
        std::unique_ptr<Room> hold; Room& r = *happy(0x03, hold);
        CHECK(r.p[0].host->state() == State::Ready);
        r.p[1].cl->leave(); r.p[0].net.q.clear(); r.p[1].net.q.clear();
        r.p[0].host->begin(77, 3, 1, 0);   // fresh handshake on the same session id
        r.p[1].cl->join(77, 0, 0); r.pump(); CHECK(r.p[0].host->propose(rb_values(), 0) == Reason::None); r.pump(); CHECK(r.p[0].host->state() == State::Ready);
        r.p[1].cl->join(77, 0, 5);   // restarts its join while the host is Ready: new epoch
        r.pump();
        CHECK(r.p[0].host->state() == State::Ready && r.p[1].cl->applied());   // proposal re-sent, re-acked
        CHECK(r.p[0].host->acks() == 0x03);
    }
    // the epoch binds client->host Cancels too: a late Cancel of an older join does not end the new one
    {
        std::unique_ptr<Room> hold; Room& r = *happy(0x03, hold);
        const uint32_t e = r.p[1].cl->epoch();
        std::vector<uint8_t> c0 = detail::cancel_bytes(77, 1, Reason::Timeout, e + 7);
        r.p[0].host->on_message(1, c0.data(), c0.size(), 0); CHECK(r.p[0].host->state() == State::Ready);
        c0 = detail::cancel_bytes(77, 1, Reason::LocalLeave, e); r.p[0].host->on_message(1, c0.data(), c0.size(), 0);
        CHECK(r.p[0].host->state() == State::Failed && r.p[0].host->reason() == Reason::LocalLeave);
    }
    // a client whose layer was taken over by another session does not ack or start on values it no longer owns
    for (int variant = 0; variant < 2; ++variant) {
        Room r(0x03); r.make_host(); r.make_client(1, r.caps); r.p[0].host->begin(77, 3, 1, 0); r.join_all(); r.p[0].host->propose(rb_values(), 0); r.pump();
        CHECK(r.p[1].cl->applied());
        int other = 0; Digest d; Values v2 = rb_values(); v2.v[(size_t)Id::InputDelay] = 6;
        CHECK(r.p[1].ev.install(900, 1, v2, &d, &other) == Res::Ok);   // a newer session installed over it
        CHECK(!r.p[1].cl->applied());
        if (variant == 0) { r.p[0].host->start(3); r.pump(); }
        else { auto dup = propose_bytes(r.p[0].host->manifest(), 0xB001); r.p[1].cl->on_message(0, dup.data(), dup.size(), 0); r.pump(); }
        CHECK(r.p[1].cl->state() == State::Failed && r.p[1].cl->reason() == Reason::LayerLost && r.p[1].cl->state() != State::Started);
        CHECK(r.p[1].ev.active() && r.p[1].ev.get(Id::InputDelay).value == 6);   // the other session's layer is intact
        CHECK(r.p[0].host->state() == State::Failed && r.p[0].host->reason() == Reason::LayerLost && std::strstr(explain(Reason::LayerLost), "replaced"));
    }
    {   // the host too
        Room r(0x03); r.make_host(); r.make_client(1, r.caps); r.p[0].host->begin(77, 3, 1, 0); r.join_all(); r.p[0].host->propose(rb_values(), 0); r.pump();
        int other = 0; Digest d; r.p[0].ev.install(901, 1, rb_values(), &d, &other);
        CHECK(r.p[0].host->start(1) == Reason::LayerLost && r.p[0].host->state() == State::Failed);
        r.pump(); CHECK(r.p[1].cl->state() == State::Cancelled && !r.p[1].ev.active());
    }
    // a failed restore is reported
    {
        Room r(0x03); r.make_host(); r.make_client(1, r.caps); r.p[1].rd.no_restore = true;
        r.p[0].host->begin(77, 3, 1, 0); r.join_all(); r.p[0].host->propose(rb_values(), 0); r.pump(); r.p[0].host->start(1); r.pump();
        r.p[1].cl->match_ended();
        CHECK(!r.p[1].ev.active() && r.p[1].cl->restore_failed() && r.p[1].cl->reason() == Reason::RestoreFailed && std::strstr(explain(Reason::RestoreFailed), "restart"));
        CHECK(!r.p[0].host->restore_failed());
        r.p[0].host->end_match(); CHECK(!r.p[0].host->restore_failed() && r.p[0].host->reason() == Reason::None);
        Room q(0x03); q.make_host(); q.make_client(1, q.caps); q.p[1].rd.no_restore = true; q.p[0].host->begin(77, 3, 1, 0); q.join_all(); q.p[0].host->propose(rb_values(), 0); q.pump();
        q.p[0].host->cancel(); q.pump();
        CHECK(q.p[1].cl->state() == State::Cancelled && q.p[1].cl->reason() == Reason::HostCancelled && q.p[1].cl->restore_failed());   // the cause stays, the failure is a flag
    }

    // ================= review cases, part 3 =================
    // E1: a delayed Ack of an older join must not validate a peer that applied nothing in its new join
    {
        Room r(0x03); r.make_host(); r.make_client(1, r.caps); r.p[0].host->begin(77, 3, 1, 0); r.join_all();
        r.p[1].net.hold = (int)Msg::Ack;
        r.p[0].host->propose(rb_values(), 0); r.pump();
        CHECK(!r.p[1].net.held.empty() && r.p[0].host->state() == State::Proposed && r.p[0].host->acks() == 1);
        r.p[0].net.drop_once = (int)Msg::Propose;                          // the re-proposal to the rejoined client is lost
        r.p[1].cl->join(77, 0, 5); r.pump();
        CHECK(r.p[1].cl->state() == State::Open && !r.p[1].ev.active());
        r.p[0].host->on_message(1, r.p[1].net.held.data(), r.p[1].net.held.size(), 6);   // the old Ack arrives late
        CHECK(r.p[0].host->state() == State::Proposed && r.p[0].host->acks() == 1 && r.p[0].host->start(1) == Reason::NotReady);
        // and the lost re-proposal is repeated: the new join then completes normally
        r.tick(1005 + 0); r.pump(); r.tick(2005); r.pump();
        CHECK(r.p[1].cl->applied() && r.p[0].host->state() == State::Ready && r.p[0].host->acks() == 3);
    }
    // E2: the first join epoch of a process is random (two runs differ: checked by the runner), never 0, and increases per join
    {
        const uint32_t a = detail::next_epoch(), b = detail::next_epoch();
        CHECK(a != 0 && b != 0 && a != b);
        Room r(0x03); r.make_host(); r.make_client(1, r.caps); r.p[1].cl->join(77, 0, 0); const uint32_t e1 = r.p[1].cl->epoch(); r.p[1].cl->join(77, 0, 0);
        CHECK(e1 != 0 && r.p[1].cl->epoch() != e1);
        if (g_print_epoch) std::printf("first-epoch %u\n", (unsigned)a);
    }
    // more switches, exact triggers
    {
        auto only = [](const char* name, const char* val) { return [=](const char* n) -> const char* { return !std::strcmp(n, name) ? val : nullptr; }; };
        CHECK(first_debug_switch(only("PAD_P2_ALWAYS", "1")) != nullptr && first_debug_switch(only("PAD_P2_ALWAYS", "2")) == nullptr && first_debug_switch(only("PAD_P2_ALWAYS", "0")) == nullptr);
        CHECK(first_debug_switch(only("USLEEP_SPIN", "1")) != nullptr && first_debug_switch(only("USLEEP_SPIN", "2")) == nullptr);
        CHECK(first_debug_switch(only("CRI_LEAD", "12")) == nullptr && first_debug_switch(only("CRI_LEAD", "0")) != nullptr && first_debug_switch(only("CRI_LEAD", "8")) != nullptr && first_debug_switch(only("CRI_LEAD", "")) == nullptr);
        CHECK(first_debug_switch(only("SPURS_GUARD_EVENT", "1")) == nullptr && first_debug_switch(only("SPURS_GUARD_EVENT", "0")) != nullptr);
        CHECK(first_debug_switch(only("SPURS_CTX_PAD", "0")) != nullptr && first_debug_switch(only("SPURS_CTX_PAD", "1")) == nullptr);
        for (const char* n : { "JSP_PROBE_ROOT_NUDGE", "JSP_PROBE_SPURS_PHASE", "LATE_VBLANK_MS", "JSP_FIGHT_PROBE" }) CHECK(first_debug_switch(only(n, "1")) != nullptr && first_debug_switch(only(n, nullptr)) == nullptr);
    }
    // a failed restore is sticky until the process ends (by design); a fresh session does not report it as its own failure
    {
        Room r(0x03); r.make_host(); r.make_client(1, r.caps); r.p[1].rd.no_restore = true;
        r.p[0].host->begin(77, 3, 1, 0); r.join_all(); r.p[0].host->propose(rb_values(), 0); r.pump(); r.p[0].host->start(1); r.pump(); r.p[1].cl->match_ended();
        CHECK(r.p[1].cl->restore_failed() && r.p[1].cl->reason() == Reason::RestoreFailed);
        r.p[1].cl->join(78, 0, 0);
        CHECK(r.p[1].cl->restore_failed() && r.p[1].cl->state() == State::Open && r.p[1].cl->reason() == Reason::None);   // sticky flag, no false session failure
        Room q(0x03); q.make_host(); q.make_client(1, q.caps); q.p[0].rd.no_restore = true;
        q.p[0].host->begin(77, 3, 1, 0); q.join_all(); q.p[0].host->propose(rb_values(), 0); q.pump(); q.p[0].host->cancel();
        CHECK(q.p[0].host->restore_failed()); q.p[0].host->begin(78, 3, 1, 0); CHECK(q.p[0].host->restore_failed() && q.p[0].host->reason() == Reason::None);
    }

    // ================= final fix-up =================
    auto ka_bytes = [](uint64_t session, uint32_t rev, uint32_t epoch, State st) { std::vector<uint8_t> b; put_header(b, Msg::KeepAlive, session, rev); wire::put(b, epoch, 4); wire::put(b, (uint8_t)st, 1); return b; };
    // T1: a `now` slightly older than the last message never wraps into instant silence
    {
        std::unique_ptr<Room> hold; Room& r = *happy(0x03, hold);
        const auto kc = ka_bytes(77, 1, r.p[1].cl->epoch(), State::Ready);
        r.p[1].cl->on_message(0, kc.data(), kc.size(), 5001); r.p[1].cl->tick(5000);
        CHECK(r.p[1].cl->state() == State::Proposed && r.p[1].ev.active());
        r.p[1].cl->tick(4000); CHECK(r.p[1].cl->state() == State::Proposed);
        const auto kh = ka_bytes(77, 1, r.p[1].cl->epoch(), State::Proposed);
        r.p[0].host->on_message(1, kh.data(), kh.size(), 5001); r.p[0].host->tick(5000); r.p[0].host->tick(3000);
        CHECK(r.p[0].host->state() == State::Ready && r.p[0].host->lost_peers() == 0 && r.p[0].ev.active());
        CHECK(r.p[0].host->silent_ms(1, 4000) == 0);   // read-only views do not wrap either
        // real silence after the clamped time still ends it, from the newest time seen
        r.p[1].cl->tick(5001 + 9999); CHECK(r.p[1].cl->state() == State::Proposed);
        r.p[1].cl->tick(5001 + 10000); CHECK(r.p[1].cl->reason() == Reason::HostLost);
    }
    // H2: handover only for a running match
    {
        std::unique_ptr<Room> hold; Room& r = *happy(0x03, hold);
        CHECK(!r.p[1].cl->handover() && !r.p[0].host->handover());                          // Proposed/Ready: refused
        r.p[1].cl->tick(10000); CHECK(r.p[1].cl->reason() == Reason::HostLost);           // liveness was not switched off
        std::unique_ptr<Room> h2; Room& q = *happy(0x03, h2); q.p[0].host->start(1); q.pump();
        CHECK(q.p[0].host->handover() && q.p[1].cl->handover());
    }
    // H1: one-sided handover cannot tear the other side's layer down, and a handed-over side ignores liveness cancels
    {
        // host handed over, the client's host-silence Cancel arrives: the host keeps its layer and the match
        std::unique_ptr<Room> hold; Room& r = *happy(0x03, hold); r.p[0].host->start(1); r.pump();
        CHECK(r.p[0].host->handover());
        r.p[0].net.drop_type = (int)Msg::KeepAlive; r.run_to(15000);                       // the client stops hearing the host and gives up
        CHECK(r.p[1].cl->reason() == Reason::HostLost && !r.p[1].ev.active());
        CHECK(r.p[0].host->state() == State::Started && r.p[0].ev.active());              // the late HostLost Cancel did not clear the host
        // client handed over only: the host keeps getting its keep-alives and nobody is dropped
        std::unique_ptr<Room> h2; Room& q = *happy(0x03, h2); q.p[0].host->start(1); q.pump();
        CHECK(q.p[1].cl->handover());
        q.run_to(60000);
        CHECK(q.p[0].host->state() == State::Started && q.p[1].cl->state() == State::Started && q.p[0].ev.active() && q.p[1].ev.active());
        // a handed-over client ignores the host's PeerLost/HostLost Cancel, but honours a real HostCancelled
        auto lost = detail::cancel_bytes(77, 1, Reason::PeerLost, q.p[1].cl->epoch()); q.p[1].cl->on_message(0, lost.data(), lost.size(), 60001);
        CHECK(q.p[1].cl->state() == State::Started && q.p[1].ev.active());
        auto hc = detail::cancel_bytes(77, 1, Reason::HostCancelled, q.p[1].cl->epoch()); q.p[1].cl->on_message(0, hc.data(), hc.size(), 60002);
        CHECK(q.p[1].cl->state() == State::Cancelled && !q.p[1].ev.active());
        // keep-alives still flow after handover (the non-handed side keeps seeing the other)
        std::unique_ptr<Room> h3; Room& w = *happy(0x03, h3); w.p[0].host->start(1); w.pump(); w.p[0].host->handover(); w.p[0].net.q.clear();
        w.p[0].host->tick(w.now + 2000);
        bool ka = false; for (auto& m : w.p[0].net.q) if (m.b.size() > 4 && m.b[4] == (uint8_t)Msg::KeepAlive) ka = true;
        CHECK(ka);
    }
    // O1: a late Hello of an older join must not rebind the peer to the old epoch
    {
        Room r(0x03); r.make_host(); r.make_client(1, r.caps); r.p[0].host->begin(77, 3, 1, 0);
        r.p[1].net.hold = (int)Msg::Hello; r.p[1].cl->join(77, 0, 0);                  // the first Hello is held back
        const std::vector<uint8_t> old_hello = r.p[1].net.held; const uint32_t e1 = r.p[1].cl->epoch();
        r.p[1].cl->join(77, 0, 1); r.pump();                                            // a newer join, delivered
        CHECK(r.p[1].cl->epoch() != e1 && r.p[0].host->hellos() == 0x02);
        r.p[0].host->on_message(1, old_hello.data(), old_hello.size(), 2);              // the old one arrives late
        const uint32_t dr = r.p[0].host->dropped(); (void)dr;
        CHECK(r.p[0].host->propose(rb_values(), 2) == Reason::None); r.pump();
        CHECK(r.p[0].host->state() == State::Ready && r.p[1].cl->applied());             // the Ack of the current join counts
        const auto c_old = detail::cancel_bytes(77, 1, Reason::LocalLeave, e1); r.p[0].host->on_message(1, c_old.data(), c_old.size(), 3);
        CHECK(r.p[0].host->state() == State::Ready);                                    // a Cancel of the old join stays ignored
        // the same old Hello a second time after the host Ready: still ignored (it is a known old epoch), not a rejoin
        r.p[0].host->on_message(1, old_hello.data(), old_hello.size(), 4);
        CHECK(r.p[0].host->state() == State::Ready && r.p[0].host->acks() == 0x03);
    }
    std::printf("session settings test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
