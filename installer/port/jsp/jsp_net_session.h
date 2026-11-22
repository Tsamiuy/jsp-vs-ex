// jsp_net_session.h -- ENHANCEMENT (experimental rollback netcode, off unless JSP_ROLLBACK=1): a session layer over GekkoNet (BSD-2-Clause,
// pinned copy in gekkonet/, commit 45dac7e5) that has no game dependency: host tests drive it against a fake game that runs the real stepper
// state machine (jsp_stepper.h).
//
// Layers, bottom to top:
//   UdpMux           one UDP socket per process, 1-byte channel tag in front of every datagram: 'G' GekkoNet, 'M' rendezvous channel, 'S' settings-handshake
//                    datagrams (unreliable: the handshake has its own Resender), 'K' heartbeat. Thread-safe; queues are bounded.
//   MessageChannel   reliable ordered messages to one peer (stop-and-wait) with a per-run epoch: used by the rendezvous only (HELO/PLAN).
//   IRendezvous      how two peers learn each other: DirectRendezvous (host/join by IP:port, implemented) or a re-psn room (design only).
//   NetSession       GekkoNet wrapper: slots, local delay, QUEUED events (the stepper runs them one tick per main-loop iteration), desync/disconnect events.
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace jspnet {

// ---- addresses (IPv4 only) ------------------------------------------------------------------------------------------------
// The struct has 2 padding bytes: NEVER memcmp or memcpy it. GekkoNet compares actor addresses as raw bytes, so the address handed to it is the
// explicit 6-byte wire form (ip as stored, port little endian) built by wire().
struct Endpoint {
    uint32_t ip_be = 0;   // network order
    uint16_t port = 0;    // host order
    bool operator==(const Endpoint& o) const { return ip_be == o.ip_be && port == o.port; }
    bool operator<(const Endpoint& o) const { return ip_be != o.ip_be ? ip_be < o.ip_be : port < o.port; }
    std::array<uint8_t, 6> wire() const;
    static bool from_wire(const uint8_t* p, size_t n, Endpoint* out);
};
bool parse_endpoint(const std::string& s, Endpoint& out);   // "a.b.c.d:port"
std::string to_string(const Endpoint& e);

// Test-only link impairment applied to every outgoing datagram (default: none). Seeded.
struct LinkModel {
    uint32_t loss_permille = 0;   // 0..1000
    uint32_t delay_ms = 0;        // fixed one-way delay
    uint32_t jitter_ms = 0;       // plus uniform 0..jitter_ms (reorders packets)
    uint64_t seed = 1;
    uint64_t blackout_from_ms = 0, blackout_to_ms = 0;   // all sends in [from, to) since set_link_model are dropped (a multi-second outage)
};

enum : uint8_t { kTagGekko = 'G', kTagMessage = 'M', kTagSettings = 'S', kTagHeartbeat = 'K' };

uint64_t now_ms();   // monotonic milliseconds (steady clock); timers only, never simulation

class UdpMux {
public:
    static constexpr size_t kMaxQueued = 512;                // datagrams per tag; beyond it the OLDEST is dropped (overflow counts)
    static constexpr size_t kMaxDatagram = 1400;
    ~UdpMux() { close(); }
    bool open(uint16_t port, std::string* err = nullptr);   // port 0 = any
    void close();
    bool is_open() const { return sock_ != -1; }
    uint16_t local_port() const { return port_; }
    void set_link_model(const LinkModel& m);
    void send(const Endpoint& to, uint8_t tag, const uint8_t* data, size_t len);
    void pump();                                             // flush delayed sends, read every pending datagram into the tag queues
    // ENHANCEMENT (rollback over re-psn; jsp_net_repsn_client.h): hooks of the re-psn client. Both run with the mux lock held and must not call pump().
    //   rx_hook: every received datagram, before the tag test. 0 = not mine, 1 = consumed (dropped), 2 = rewritten: buf / n / from now hold the unwrapped relay payload (it is never longer than the datagram).
    //   tx_hook: the final bytes of every send (tag included). true = the hook sent them through the relay itself.
    std::function<int(Endpoint& from, uint8_t* buf, int& n)> rx_hook;
    std::function<bool(const Endpoint& to, const std::vector<uint8_t>& bytes)> tx_hook;
    void set_hooks(std::function<int(Endpoint&, uint8_t*, int&)> rx, std::function<bool(const Endpoint&, const std::vector<uint8_t>&)> tx);   // under the mux lock: no hook is running when it returns
    void send_control(const Endpoint& to, const uint8_t* data, size_t len);   // PROBE / PUNCH / relay-wrapped bytes from THIS socket: no tag, no link model, no tx_hook
    bool pop(uint8_t tag, Endpoint& from, std::vector<uint8_t>& data, uint64_t* arrived_ms = nullptr);
    uint64_t sent = 0, received = 0, dropped = 0, overflow = 0, oversize = 0, unknown_tag = 0, send_fail = 0; int last_send_err = 0;   // counters (dropped = by the LinkModel)
private:
    struct Delayed { uint64_t due_us; Endpoint to; std::vector<uint8_t> bytes; };
    struct Queued { Endpoint from; std::vector<uint8_t> data; uint64_t ms; };
    void raw_send(const Endpoint& to, const std::vector<uint8_t>& bytes);
    void raw_send_plain(const Endpoint& to, const std::vector<uint8_t>& bytes);
    uint32_t rnd();
    std::recursive_mutex mu_;
    intptr_t sock_ = -1;
    uint16_t port_ = 0;
    LinkModel lm_;
    uint64_t rng_ = 1, lm_start_ms_ = 0;
    std::vector<Delayed> delayed_;
    std::map<uint8_t, std::deque<Queued>> q_;
};

// A host thread that keeps the socket alive through scene loading: pumps the mux every few ms and sends a tiny heartbeat ('K', session id, peer id)
// to each registered peer every `period_ms`, so a peer's liveness timers see traffic while this process's main thread is blocked.
class PumpThread {
public:
    ~PumpThread() { stop(); }
    void start(UdpMux& mux, uint64_t session_id, uint8_t my_peer, std::vector<Endpoint> peers, uint32_t period_ms = 500);
    void stop();
    uint64_t beats_sent() const { return beats_.load(); }
private:
    struct Impl; Impl* p_ = nullptr;
    std::atomic<uint64_t> beats_{0};
};
// Heartbeats received so far from `peer` (drained from the mux by heartbeat_drain; returns arrival times).
struct Heartbeat { uint64_t session_id; uint8_t peer; Endpoint from; uint64_t arrived_ms; };
bool heartbeat_pop(UdpMux& mux, Heartbeat* out);

// ---- message channel (rendezvous only) ----------------------------------------------------------------------------------------
struct IMessageTransport {
    virtual ~IMessageTransport() {}
    virtual bool send(const uint8_t* data, size_t len) = 0;   // false: too large (limit kMaxMessage), queue full or channel failed
    virtual bool recv(std::vector<uint8_t>& out) = 0;         // false: nothing waiting
    virtual void pump() = 0;                                  // call every loop iteration
    virtual bool failed() const = 0;                          // peer unreachable (retry budget spent)
};

// Packet: [type u8: 0 data, 1 ack][src_epoch u64][dst_epoch u64][seq u32][payload]. Epoch = a random run identifier (random bits mixed with the
// monotonic clock; never the wall clock, not ordered). A restarted peer is recognised by an unknown epoch opening with sequence 0 (channel state
// reset, the old epoch is retired); packets of a retired run, of an unknown epoch mid-stream, or addressed to a different run of ours are refused.
class MessageChannel : public IMessageTransport {
public:
    static constexpr size_t kMaxMessage = 1000, kMaxQueue = 64;
    MessageChannel(UdpMux& mux, const Endpoint& peer, uint64_t my_epoch) : mux_(mux), peer_(peer), epoch_(my_epoch) {}
    const Endpoint& peer() const { return peer_; }
    bool send(const uint8_t* data, size_t len) override;
    bool recv(std::vector<uint8_t>& out) override;
    void pump() override;
    bool failed() const override { return failed_; }
    bool idle() const { return out_.empty(); }
    bool on_packet(const std::vector<uint8_t>& pkt);          // false = refused (stale epoch, malformed)
    uint64_t peer_epoch() const { return peer_epoch_; }
    uint64_t refused = 0, resets = 0;
private:
    void reset_for_new_peer_run(uint64_t e);
    UdpMux& mux_;
    Endpoint peer_;
    uint64_t epoch_, peer_epoch_ = 0;
    std::vector<uint64_t> retired_;                           // earlier runs of the peer (bounded): their stragglers are refused
    std::deque<std::vector<uint8_t>> out_;                    // unacked, front is in flight
    std::deque<std::vector<uint8_t>> in_;
    uint32_t next_seq_ = 0, expect_seq_ = 0;
    uint64_t last_tx_us_ = 0;
    uint32_t tries_ = 0;
    bool failed_ = false;
};

// Routes tag 'M' datagrams to the channel of their sender. A datagram from an unknown endpoint is accepted only if it is a well-formed DATA packet
// of sequence 0 (the first message of a peer); anything else is counted in `ignored`. At most kMaxChannels channels exist.
class MessageHub {
public:
    static constexpr size_t kMaxChannels = 8;
    explicit MessageHub(UdpMux& m, uint64_t epoch = 0);
    MessageChannel* open(const Endpoint& peer);               // find or create (nullptr when full)
    MessageChannel* find(const Endpoint& peer);
    void close(const Endpoint& peer);
    void pump();
    uint64_t epoch() const { return epoch_; }
    std::vector<Endpoint> new_peers;                          // senders seen for the first time since last cleared (host side)
    uint64_t ignored = 0;
private:
    UdpMux& mux_;
    uint64_t epoch_;
    std::map<Endpoint, std::unique_ptr<MessageChannel>> ch_;
};

// ---- rendezvous --------------------------------------------------------------------------------------------------------------
struct PeerSlots { Endpoint ep; uint8_t first_slot = 0; uint8_t count = 0; };
struct SessionPlan {
    uint64_t session_id = 0;        // same width as the handshake's session id (jpsm::Host::begin / Client::join)
    bool host = false;
    uint8_t total_players = 0;      // over all peers; slot s is GekkoNet handle s
    uint8_t local_first = 0, local_count = 0;
    std::vector<PeerSlots> remotes; // 1 entry for the 2-peer case
    // Handshake roster mapping: peer id (== handshake roster slot, host 0, client 1) -> mask of game slots that peer owns, e.g. 2+2 = {0b0011, 0b1100}.
    std::array<uint8_t, 4> peer_slot_mask() const;
};

// Pluggable: (a) DirectRendezvous below; (b) a re-psn room (design in the doc, not implemented). poll() never blocks.
class IRendezvous {
public:
    virtual ~IRendezvous() {}
    virtual int poll() = 0;                                   // 0 pending, 1 plan ready, -1 failed
    virtual const SessionPlan& plan() const = 0;
    virtual const std::string& error() const = 0;
    virtual MessageChannel* peer_channel() = 0;
};

// Host: waits for one client on `mux`'s port; client: sends HELO to `host`. Slots: host first, then the client. The host validates the first
// message of every new sender: only a well-formed HELO makes a client; a stray datagram is dropped and does not fail the host.
class DirectRendezvous : public IRendezvous {
public:
    DirectRendezvous(UdpMux& mux, MessageHub& hub, uint8_t local_count, uint64_t session_id) // host
        : mux_(mux), hub_(hub), host_(true), local_count_(local_count) { plan_.session_id = session_id; }
    DirectRendezvous(UdpMux& mux, MessageHub& hub, uint8_t local_count, const Endpoint& host_ep)  // client
        : mux_(mux), hub_(hub), host_(false), local_count_(local_count), host_ep_(host_ep) {}
    int poll() override;
    const SessionPlan& plan() const override { return plan_; }
    const std::string& error() const override { return err_; }
    MessageChannel* peer_channel() override { return ch_; }
    uint64_t rejected_hello = 0;
    // ENHANCEMENT (rollback over re-psn): the host takes a HELO only from this endpoint (the room's other player as the server gave it), so another host of the
    // internet that knows the UDP port cannot take the client's seat before the plan is agreed. Not set = any endpoint (direct IP).
    void set_only_peer(const Endpoint& e) { only_ = e; has_only_ = true; }
private:
    Endpoint only_{}; bool has_only_ = false;
    UdpMux& mux_; MessageHub& hub_;
    bool host_; uint8_t local_count_; Endpoint host_ep_;
    SessionPlan plan_; std::string err_; MessageChannel* ch_ = nullptr; bool sent_hello_ = false; int state_ = 0; uint8_t hello_count_ = 0;
};

// ---- wire form of one pad (8 bytes) ---------------------------------------------------------------------------------------------
// GekkoNet fills the input of a disconnected player with ZERO bytes, and our own buffers start zeroed. The stepper's canonical pad (jsi::Pad::pack) has the
// sticks at 128 when neutral, so an all-zero blob would be "sticks fully up-left". The wire form XORs the four stick bytes (offsets 2..5) with 0x80:
// all-zero wire = buttons 0, sticks centre, connected flag 0 (an absent pad) = neutral. Applied in this adapter, not in the stepper's files.
inline void pad_pack_to_wire(const uint8_t canon[8], uint8_t wire[8]) { for (int i = 0; i < 8; i++) wire[i] = (i >= 2 && i <= 5) ? uint8_t(canon[i] ^ 0x80) : canon[i]; }
inline void pad_wire_to_pack(const uint8_t wire[8], uint8_t canon[8]) { pad_pack_to_wire(wire, canon); }   // involution

// ---- session -----------------------------------------------------------------------------------------------------------------
// Event order of GekkoNet (read game_session.cpp 237-246, 465-471, 778-830 and verified by the tests): per update, [Load(L)] then for each
// frame F: Advance(F) followed by Save(F); the first Save of a session is Save(-1). Save(F) is the state AFTER advancing F; Load(L) returns to
// the state Save(L) took, the replay starts with Advance(L+1). With the stepper: Save(F)->Commit(F+1), Load(L)->Rollback(L+1), Advance(F)->Run(F+1).
// Events are QUEUED to the caller (the stepper runs one tick per main-loop iteration), so a Save's checksum does not exist when the event arrives.
enum class EvKind : uint8_t { Save, Load, Advance };
struct NetEvent {
    EvKind kind = EvKind::Advance;
    int32_t frame = 0;                 // signed: the first Save is -1
    bool rolling_back = false;         // Advance: resimulation after a load
    uint32_t token = 0;                // Load: the token complete_save() stored for that frame; Save: the id to complete
    std::vector<uint8_t> inputs;       // Advance: total_players * input_size bytes, slot order, wire form
};

struct SessionConfig {
    uint32_t input_size = 8;                 // bytes per player per tick (the 8-byte wire pad)
    uint8_t max_prediction = 8;              // rollback depth window (GekkoNet caps at 32)
    uint32_t ring_depth = 10;                // depth of the stepper's snapshot ring: max_prediction must be < ring_depth or start() refuses
    // Per local player. GekkoNet builds the input packets of ALL local players from one local queue, so unequal delays on one peer desync under
    // loss (read in GekkoNet's backend.cpp): start() refuses unequal values. Different peers may use different delays.
    std::vector<uint8_t> local_delay;        // missing entries = 0
    uint32_t disconnect_timeout_ms = 5000;   // 0 = never
    bool desync_detection = true;            // without limited_saving: every saved frame's checksum is compared; with it: a health frame every 16 ticks (GekkoNet's HEALTH_CHECK_INTERVAL)
    // ENHANCEMENT (rollback latency, A/B, off by default): GekkoNet's limited (sparse) saving. Off, every live and every re-simulated tick is a Save (a full snapshot
    // commit here, ~13 ms measured); on, only the confirmed frame is saved (once per rollback, or a catch-up rollback when the last save is a prediction window old) plus a
    // health-check save every 16 ticks. Fewer saves, longer re-simulations: chosen by measurement (docs/ROLLBACK.md, the bench). Game sessions only.
    bool limited_saving = false;
    // Keepalive thread: GekkoNet's Poll() sends its NetworkHealth ping every 500 ms and the peer's disconnect timer counts any packet received, but only
    // while somebody CALLS poll. When the game thread is busy for seconds (a replay, a slow render), a thread polls instead once no poll happened for this long.
    // 0 = no thread. All GekkoNet calls are serialized by one mutex.
    uint32_t keepalive_stale_ms = 250;
    // ENHANCEMENT (replays, opt-in): record the match's CONFIRMED inputs and the checksum of every confirmed tick (GekkoNet's replay format, GKRP v2), so a
    // later replay session can play the match back and name the first tick whose state differs. The recording starts at GekkoNet frame 0 (the session's
    // base tick) without an initial state: a playback must start from the same fight start, which the user data has to identify (the game glue stores its
    // build id and the settings there). Game sessions only.
    bool record = false;
    std::vector<uint8_t> record_user_data;   // copied into the replay (GekkoNet keeps it as opaque bytes)
    // GekkoNet writes a confirmed tick's checksum into the recording at the start of the NEXT update; our saves are completed asynchronously (complete_save), so
    // a recording read while a save is pending would carry an unfilled checksum. recording() therefore serves a copy taken at the start of an update (no save
    // pending), refreshed every `record_copy_every` updates (0 = never: recording() then needs an idle session).
    uint32_t record_copy_every = 60;
};

// ENHANCEMENT (replays and the rollback bench): what kind of GekkoNet session a NetSession runs.
//   Game    two or more peers over the UdpMux (start)
//   Stress  every player local, no network: GekkoNet's stress session rolls back `distance` ticks EVERY update and replays them, comparing the checksum of
//           each re-simulated tick with its first pass (a determinism check, and the worst-case rollback load for timing). start_stress.
//   Replay  plays a recorded match back from its bytes, comparing every recorded checksum. start_replay.
enum class SessionMode : uint8_t { Game, Stress, Replay };

enum class SessionEventKind { Syncing, Connected, Started, Disconnected, Desync, Failed, ReplayFinished };
struct SessionEvent {
    SessionEventKind kind;
    int handle = -1;                         // slot
    int tick = -1;                           // Desync: stepper tick numbering: the tick whose state differs (= GekkoNet Save frame + 1)
    uint32_t local_hash = 0, remote_hash = 0;
    std::string text;
};

struct SessionStats {
    uint32_t rollbacks = 0, max_rollback_frames = 0, stalls = 0;
    uint64_t advances = 0, resimulated = 0, saves = 0, loads = 0, deferred = 0;
    float frames_ahead = 0;
    float ping_ms = -1, jitter_ms = 0;        // GekkoNet's average round trip to the first remote player and its jitter (-1: no remote / not known yet)
    float last_ping_ms = -1;                 // ENHANCEMENT (adaptive input delay, rule 2): GekkoNet's last single round trip to that player (one every 500 ms)
    uint32_t rtt_seq = 0;                    // FIX (RTT sampling): round trips measured so far for that player (GekkoNet's count, +1 per ping answer): a reader
                                             // that sees the same count again has no new round trip, whatever last_ping_ms says
    int32_t acked_frame = -1;                // ENHANCEMENT (match perf summary): the newest frame of this PC's input that player acknowledged (-1 none)
    uint8_t local_delay = 0;                  // the input delay this peer's local players run with now
    uint64_t keepalive_polls = 0;            // polls made by the keepalive thread (the game thread was busy)
};

class NetSession {
public:
    NetSession();
    ~NetSession();
    // Creates the GekkoNet session and adds actors in slot order (every peer adds the same order, so handles agree). One NetSession per
    // process: GekkoNet's adapter has no user pointer, so the adapter is a process-wide singleton bound to `mux`. false + error() on refusal.
    bool start(UdpMux& mux, const SessionPlan& plan, const SessionConfig& cfg);
    // ENHANCEMENT (rollback bench): a local session of `players` local players (slots 0..players-1, set_local_input for each) that rolls back `distance`
    // ticks every update (1..max_prediction). No socket, no keepalive thread, no recording. A Desync event names a tick whose re-simulation gave a
    // different checksum than its first pass.
    bool start_stress(uint8_t players, uint32_t distance, const SessionConfig& cfg);
    // ENHANCEMENT (replays): plays back a recording (bytes of recording()). The replay's own config is used; its input size must equal cfg.input_size and
    // its player count must be 1..4. set_local_input is ignored. Ends with a ReplayFinished event; the first checksum that differs from the recording
    // raises a Desync event (tick = stepper numbering, local_hash = this run, remote_hash = the recording). The stored user data: replay_user_data().
    bool start_replay(const std::vector<uint8_t>& bytes, const SessionConfig& cfg);
    // ENHANCEMENT (lobby: sessions opened and closed at runtime): ends the session (keepalive thread joined, GekkoNet session destroyed, the adapter
    // singleton unbound) and returns this object to its constructed state, so start(), start_stress() or start_replay() may run again in the same process.
    // The peer is NOT told: abort() first for that. Read recording() / stats() before it. Idempotent; never call it from inside a session callback.
    void stop();
    SessionMode mode() const { return mode_; }
    // ENHANCEMENT (replays): the recording so far, ready to save to disk (only with cfg.record). Read live when no save is pending, else the last copy (see
    // SessionConfig::record_copy_every: it misses at most that many updates at the end). false: not recording, or nothing to serve yet.
    bool recording(std::vector<uint8_t>& out);
    bool replay_user_data(std::vector<uint8_t>& out) const;   // Replay mode: the user data stored in the replay
    bool replay_finished() const { return replay_finished_; }
    const std::string& error() const { return start_error_; }
    bool running() const { return gs_ != nullptr && !failed_; }
    bool synced() const { return started_; }                  // all remotes connected and sync handshake done
    // Local input for the coming update for local player n (0..local_count-1), in wire form; every local player once per update().
    void set_local_input(uint8_t local_player, const uint8_t* wire_input);
    // Runs one GekkoNet update and APPENDS its events to `out`. Does nothing (returns false, counts `deferred`) when `consumer_idle` is false (the
    // stepper queue is not empty) or when a Save is still waiting for complete_save(): GekkoNet reuses its event buffers and the checksum pointer on
    // the next update, so the next update must not start before the Saves of the last one are completed.
    bool update(std::vector<NetEvent>& out, bool consumer_idle);
    // Fill the checksum and token of a Save event after the snapshot commit produced them. `id` = NetEvent::token of that Save.
    bool complete_save(uint32_t id, uint32_t checksum, uint32_t token);
    bool saves_pending() const { return pending_saves_ != 0; }
    // End the session now with a reason: the peer is told at once (the local actor leaves, which sends the disconnect immediately).
    void abort(const std::string& why);
    // Leave the session: disconnects one local actor (GekkoNet: the peer leaves, every remote actor is dropped).
    bool leave(uint8_t local_player);
    // Receive and send without advancing the session (time sync, acks, disconnect timer). frames_ahead() is recomputed from received messages only, so a caller that
    // waits for frames_ahead() < 1 MUST call this while it waits, or it waits forever and the peer times out.
    void poll();
    // Longest stretch (ms) the game thread went without calling poll/update since the last call of this (reset). What the keepalive thread had to cover.
    uint32_t take_max_gap_ms();
    uint64_t keepalive_polls() const { return ka_polls_.load(); }
    // Pacing, as GekkoNet's own example does it: after a loop iteration that CALLED update (the figure is recomputed only when a new local input is sent, i.e. in
    // update), a small proportional stretch of the frame while frames_ahead() > 0.5 (1.6% of a tick, accumulated in microseconds, no 1 ms floor). Never a long or one-shot wait: both peers measure the
    // other's advantage, so waiting on it feeds back (runaway sleeps of seconds). The ONLY deadlock rule is the caller's: update must be
    // called every iteration it may (a two-copy test: the game skipped update while the figure was >= 1, so the figure never changed again).
    uint32_t pace_sleep_ms(uint32_t tick_ms);
    float frames_ahead() const;                               // >= ~1: the caller should wait a tick (pacing)
    bool failed() const { return failed_; }
    const std::string& failure() const { return failure_; }
    bool desynced() const { return desync_tick_ != kNoTick; }
    int first_desync_tick() const { return desync_tick_ == kNoTick ? -1 : desync_tick_; }   // lowest tick (stepper tick numbering) reported different
    std::vector<SessionEvent> take_events();
    // FIX (review of PR 248, the live watermark): the events and GekkoNet's final frame (as final_frame()) read under ONE lock, so a keepalive poll cannot
    // land between them: when no Disconnected is among the events, *final_frame is from a moment every peer was still connected.
    std::vector<SessionEvent> take_events(int* final_frame);
    SessionStats stats() const;
    const SessionPlan& plan() const { return plan_; }
    // ENHANCEMENT (adaptive input delay, test): the input delay of this peer's local players from now on (GekkoNet SetLocalDelay: a longer delay repeats
    // the last input, a shorter one drops the overlap). Each peer sets only its own delay; the other peer sees only its input stamps. All local players alike.
    void set_local_delay(uint8_t d);
    // ENHANCEMENT (rollback hitches): how many ticks this peer may run ahead on predicted inputs (gekko_set_input_prediction_window; at most the
    // max_prediction the session was created with). Local only: the peers need not agree.
    void set_prediction_window(uint8_t w);
    // ENHANCEMENT (rollback latency): every later Load names this frame or a later one (GekkoNet's confirmed frame, held back by a pending rollback),
    // so the saved states of earlier frames are never loaded again (gekko_final_frame, a local GekkoNet addition: gekkonet/PIN.md). -1 = none / not a game session.
    int final_frame() const;
private:
    uint8_t cur_delay_ = 0;   // the local players' input delay now (set_local_delay)

    static constexpr int kNoTick = 0x7fffffff;
    void fail(const std::string& why);
    bool start_local(SessionMode m, const SessionPlan& plan, const SessionConfig& cfg, uint32_t distance, const std::vector<uint8_t>* bytes);
    void copy_recording();   // under gmu_, no save pending
    SessionMode mode_ = SessionMode::Game;
    std::vector<uint8_t> rec_copy_; uint32_t rec_updates_ = 0; bool recording_ = false, replay_finished_ = false;
    void stop_keepalive();
    void poll_impl();
    void note_game_call();
    std::atomic<uint64_t> game_call_ms_{0}; std::atomic<uint32_t> max_gap_ms_{0};
    mutable std::recursive_mutex gmu_;                         // every GekkoNet call and events_ / psaves_ / pending_ access
    std::thread ka_; std::atomic<bool> ka_stop_{false}; std::atomic<uint64_t> last_poll_ms_{0}, ka_polls_{0};
    void* gs_ = nullptr;                                       // GekkoSession*
    SessionPlan plan_; SessionConfig cfg_;
    std::vector<uint8_t> pending_;                            // local inputs for the coming tick
    std::vector<SessionEvent> events_;
    struct PendingSave { uint32_t id; int frame; unsigned* checksum; unsigned char* state; };
    std::vector<PendingSave> psaves_;
    uint32_t next_id_ = 1, pending_saves_ = 0;
    uint32_t pace_us_ = 0;   // pacing stretch not yet slept (microseconds)
    int polled_events_ = 0;  // session events already read (by update or by poll), see poll()
    bool started_ = false, failed_ = false; std::string failure_, start_error_; int desync_tick_ = kNoTick;
    uint64_t advances_ = 0, resim_ = 0, saves_ = 0, loads_ = 0, deferred_ = 0;
};

} // namespace jspnet
