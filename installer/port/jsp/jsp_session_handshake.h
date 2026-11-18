// ENHANCEMENT: host/client handshake state machines for host-controlled session settings.
// Flow: Hello (client caps) -> Propose (manifest) -> apply + read back -> Ack (digests) -> Start, or Reject/Cancel at any step.
// Transport-agnostic: the caller supplies Transport::send and feeds received datagrams to on_message(from, ...); `from` is the
// peer id the transport authenticated (peer id == roster slot), never a field of the message. The same bytes can travel over a
// direct connection, a rollback session channel or a room-server message. Time is injected (milliseconds) so tests are exact and
// nothing here reads a clock; timeouts only end the lobby handshake and never gate simulation. Single-threaded.
// Datagrams may be lost: Hello, Propose, Ack, Reject and Cancel are repeated a bounded number of times (Config::retries every
// retry_ms) until the answer arrives; Start and Cancel have no answer and are simply repeated that many times (duplicates are no-ops).
// Every exit path (reject, cancel, timeout, leave, error, destruction) removes the session layer this object installed, and only that:
// a newer session's layer in the shared EffectiveValues survives the delayed teardown of an older object.
#ifndef JSP_SESSION_HANDSHAKE_H
#define JSP_SESSION_HANDSHAKE_H

#include "jsp_session_effective.h"
#include "jsp_session_manifest.h"
#include <chrono>
#include <random>

namespace jpsm {

class Transport {
public:
    virtual ~Transport() = default;
    virtual void send(uint8_t peer, const uint8_t* data, size_t n) = 0;
};

enum class State : uint8_t { Idle, Open, Proposed, Ready, Started, Ended, Cancelled, TimedOut, Failed };
inline bool terminal(State s) { return s >= State::Ended; }

enum class Channel : uint8_t { DirectIp, RoomServer };   // informational: the host is the room owner either way

struct Config {
    uint64_t hello_timeout_ms = 15000;    // host: all hellos after begin
    uint64_t ack_timeout_ms = 15000;      // host: all acks after a proposal
    uint64_t keepalive_ms = 2000;         // both sides: interval of the liveness message while a session is open
    uint64_t liveness_ms = 10000;         // both sides: silence after which the other side is considered lost (no fixed start timeout:
                                          // the host may sit in character select for as long as its keep-alives arrive)
    uint64_t retry_ms = 1000;             // repeat interval of an unanswered message
    uint32_t retries = 5;                 // bounded number of repeats after the first send (0 = none)
    uint32_t epoch_salt = 0;              // client: XORed into the process id part of the join epoch (24 bits used)
    Channel channel = Channel::DirectIp;
};

namespace detail {
inline Res install_local(EffectiveValues& ev, const Manifest& m, Digest* applied, Reason* why, Id* id, const void* owner)
{
    const Res r = ev.install(m.session, m.revision, m.values, applied, owner);
    *id = ev.fail_id();
    switch (r) {
    case Res::Ok: *why = Reason::None; break;
    case Res::OutOfRange: *why = Reason::OutOfRange; break;
    case Res::NoStartupEvidence: *why = Reason::NoStartupEvidence; break;
    case Res::RestartRequired: *why = rule(*id).cls == NetClass::CheatOff ? Reason::CheatActive : Reason::RestartRequired; break;
    case Res::ReaderError: *why = Reason::ReaderError; break;
    default: *why = Reason::ApplyFailed; break;
    }
    return r;
}
inline void put_digest(std::vector<uint8_t>& b, const Digest& d) { b.insert(b.end(), d.begin(), d.end()); }
inline bool get_digest(wire::Cur& c, Digest* d) { for (auto& x : *d) x = static_cast<uint8_t>(c.get(1)); return !c.bad; }
// Shared by both roles: can this peer honour every value (Caps)? Returns the first problem.
inline Reason check_caps(const Caps& c, const Values& v, Id* id)
{
    if (v.v[static_cast<size_t>(Id::Rollback)] == 1 && !c.rollback) { *id = Id::Rollback; return Reason::RollbackUnsupported; }
    for (size_t i = 0; i < kCount; ++i)
        if (!c.supports(static_cast<Id>(i), v.v[i])) { *id = static_cast<Id>(i); return Reason::Unsupported; }
    return Reason::None;
}

// Bounded repetition of unanswered messages.
class Resender {
public:
    void send(Transport& tx, uint8_t peer, Msg type, std::vector<uint8_t> bytes, uint64_t now, const Config& cfg)
    {
        tx.send(peer, bytes.data(), bytes.size());
        if (cfg.retries) items_.push_back({ peer, type, std::move(bytes), cfg.retries, now + cfg.retry_ms });
    }
    void drop(Msg type, int peer = -1)
    {
        for (size_t i = 0; i < items_.size();) {
            if (items_[i].type == type && (peer < 0 || items_[i].peer == peer)) items_.erase(items_.begin() + i); else ++i;
        }
    }
    void tick(Transport& tx, uint64_t now, const Config& cfg)
    {
        for (size_t i = 0; i < items_.size();) {
            Item& it = items_[i];
            if (now >= it.due) {
                tx.send(it.peer, it.bytes.data(), it.bytes.size());
                it.due = now + cfg.retry_ms;
                if (--it.left == 0) { items_.erase(items_.begin() + i); continue; }
            }
            ++i;
        }
    }
    size_t pending() const { return items_.size(); }
private:
    struct Item { uint8_t peer; Msg type; std::vector<uint8_t> bytes; uint32_t left; uint64_t due; };
    std::vector<Item> items_;
};
// Cancel payload: reason u8 + the client's join epoch u32 (a Cancel only ends the join it was meant for). The header revision lets a client
// ignore a Cancel that is older than the revision it already applied.
inline std::vector<uint8_t> cancel_bytes(uint64_t session, uint32_t rev, Reason r, uint32_t epoch)
{
    std::vector<uint8_t> b; put_header(b, Msg::Cancel, session, rev); wire::put(b, static_cast<uint8_t>(r), 1); wire::put(b, epoch, 4); return b;
}
// Join epoch = 24-bit process id (random per process) << 8 | 8-bit join counter. Never 0. Within one process later joins are "newer"
// (serial arithmetic on the counter), so a late Hello of an older join is recognised and refused; a different process id means a
// restarted client and is accepted as a new join. The process id comes from OS randomness mixed with the clock and an address (a
// session id, not game logic, so the clock use is allowed): a restarted process does not reuse the ids of its previous run (collision
// about 2^-24). Tests can fix the start with epoch_start_override.
inline uint32_t& epoch_start_override() { static uint32_t v = 0; return v; }
inline bool epoch_newer(uint32_t a, uint32_t b)
{
    if ((a >> 8) != (b >> 8)) return true;                 // another process: a restart
    const uint32_t d = (a - b) & 0xffu;
    return d >= 1 && d <= 127;
}
inline uint32_t next_epoch()
{
    static uint32_t pid = 0, counter = 0;
    static bool seeded = false;
    if (!seeded) {
        seeded = true;
        if (epoch_start_override()) { pid = epoch_start_override() >> 8; counter = epoch_start_override() & 0xffu; }
        else {
            // The OS random device, mixed with the clock and an address so that a random_device that is deterministic on some library
            // builds (or throws) still differs between runs.
            uint32_t rnd = 0;
            try { std::random_device rd; rnd = static_cast<uint32_t>(rd()); } catch (...) { rnd = 0; }
            const uint64_t t = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
            const uint32_t addr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&counter) >> 4);
            pid = (rnd ^ static_cast<uint32_t>(t * 2654435761u) ^ static_cast<uint32_t>(t >> 32) ^ addr) & 0xffffffu;
        }
        if (!pid) pid = 1;
    }
    counter = (counter + 1) & 0xffu;
    return (pid << 8) | counter;
}
}

class Host {
public:
    Host(Transport& t, EffectiveValues& local, const Caps& caps, uint8_t host_slot, const Config& cfg = {})
        : tx_(t), local_(local), caps_(caps), slot_(host_slot), cfg_(cfg) {}
    ~Host() { local_.clear_owned(this); }
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    // Opens a session for a room. Fresh session id per match; roster includes the host.
    Reason begin(uint64_t session, uint8_t roster, uint64_t seed, uint64_t now)
    {
        if (!session || !(roster & (1u << slot_)) || (roster & ~0x0f)) return Reason::Malformed;
        release();
        rs_.drop(Msg::Propose); rs_.drop(Msg::Start); rs_.drop(Msg::Cancel);
        session_ = session; roster_ = roster; seed_ = seed; revision_ = 0; hello_ = 0; acks_ = 0; now_ = now;
        have_values_ = false; auto_repropose_ = false; handover_ = false; lost_mask_ = 0; lost_reason_ = Reason::None;
        last_heard_.fill(now); next_ka_ = now + cfg_.keepalive_ms;
        for (auto& v : seen_epochs_) v.clear();
        peers_caps_.fill(Caps{});
        state_ = State::Open; deadline_ = now + cfg_.hello_timeout_ms; reason_ = Reason::None;
        return Reason::None;
    }
    // A lobby change (join/leave) invalidates readiness: acks cleared, the layer removed, removed peers told. When a configuration
    // was proposed before, it is proposed again (new revision, new roster) to the remaining clients as soon as every client of the
    // new roster has said hello; the result of an immediate re-proposal is returned.
    Reason set_roster(uint8_t roster, uint64_t now)
    {
        if (state_ == State::Idle || terminal(state_) || state_ == State::Started) return Reason::NotReady;
        if (!(roster & (1u << slot_)) || (roster & ~0x0f)) return Reason::Malformed;
        return reroster(roster, now, Reason::RosterChanged);
    }
    // Liveness of this host's own tick/keep-alive is over: the rollback session (jsp_net_session.h) tracks the peers from here. Keep-alives and the silence
    // checks stop; nothing else changes.
    // Only a running match can be handed over (H2). Handing over stops THIS side's silence checks and its reaction to liveness Cancels
    // (HostLost/PeerLost); keep-alives keep flowing so a peer that has not handed over yet does not lose us. One-sided handover cannot
    // clear this side's layer through a liveness Cancel; both sides call it when the rollback session owns liveness.
    bool handover() { if (state_ != State::Started) return false; handover_ = true; return true; }
    uint8_t lost_peers() const { return lost_mask_; }          // peers removed for silence while the roster could still change
    Reason lost_reason() const { return lost_reason_; }
    uint64_t silent_ms(uint8_t peer, uint64_t now) const { return peer < kMaxRoster && now >= last_heard_[peer] ? now - last_heard_[peer] : 0; }
private:
    Reason reroster(uint8_t roster, uint64_t now, Reason why)
    {
        now = now_ = (now > now_ ? now : now_);   // time is monotonic inside the object: an older `now` never makes `now - last_heard` wrap
        const uint8_t removed = roster_ & ~roster;
        rs_.drop(Msg::Propose);
        for (uint8_t p = 0; p < kMaxRoster; ++p) if (removed & (1u << p)) rs_.send(tx_, p, Msg::Cancel, detail::cancel_bytes(session_, revision_, why, peers_caps_[p].epoch), now, cfg_);
        for (uint8_t p = 0; p < kMaxRoster; ++p) if ((roster & ~roster_) & (1u << p)) last_heard_[p] = now;   // newly added peers start fresh
        roster_ = roster; hello_ &= roster; acks_ = 0;
        release();
        state_ = State::Open; deadline_ = now + cfg_.hello_timeout_ms;
        if (!have_values_) return Reason::None;
        const uint8_t clients = roster_ & ~(1u << slot_);
        if ((hello_ & clients) != clients) { auto_repropose_ = true; return Reason::None; }
        return propose(last_values_, now);
    }
public:
    // Propose (or re-propose after a lobby change) a configuration. Not fatal when refused before anything was sent: the host can
    // pick other values. On success the host's own layer is installed and read back before any client is asked.
    Reason propose(const Values& v, uint64_t now)
    {
        if (state_ != State::Open && state_ != State::Proposed && state_ != State::Ready) return Reason::NotReady;
        now = now_ = (now > now_ ? now : now_);   // time is monotonic inside the object: an older `now` never makes `now - last_heard` wrap
        const uint8_t clients = roster_ & ~(1u << slot_);
        if ((hello_ & clients) != clients) { bad_peer_ = first_missing(clients & ~hello_); return Reason::NotReady; }
        if (local_.debug_switch() || caps_.debug_active) { bad_peer_ = slot_; return Reason::DebugSwitch; }
        for (uint8_t p = 0; p < kMaxRoster; ++p)
            if ((clients & (1u << p)) && peers_caps_[p].debug_active) { bad_peer_ = p; return Reason::DebugSwitch; }
        Id id;
        if (!values_valid(v, &id)) { bad_id_ = id; return Reason::OutOfRange; }
        Reason r = detail::check_caps(caps_, v, &id);
        if (r != Reason::None) { bad_peer_ = slot_; bad_id_ = id; return r; }
        for (uint8_t p = 0; p < kMaxRoster; ++p) {
            if (!(clients & (1u << p))) continue;
            r = detail::check_caps(peers_caps_[p], v, &id);
            if (r != Reason::None) { bad_peer_ = p; bad_id_ = id; return r; }
        }
        Manifest m; m.session = session_; m.revision = revision_ + 1; m.host = slot_; m.roster = roster_; m.seed = seed_; m.values = v;
        Digest applied;
        Reason why;
        if (detail::install_local(local_, m, &applied, &why, &id, this) != Res::Ok) {
            bad_peer_ = slot_; bad_id_ = id;
            if (state_ != State::Open) fail(State::Failed, why, slot_);
            return why;
        }
        revision_ = m.revision; manifest_ = m; digest_ = manifest_digest(m); applied_expect_ = values_digest(session_, revision_, v);
        last_values_ = v; have_values_ = true; auto_repropose_ = false;
        acks_ = static_cast<uint8_t>(1u << slot_);
        std::vector<uint8_t> b; put_header(b, Msg::Propose, session_, revision_);
        const auto mb = encode_manifest(m); b.insert(b.end(), mb.begin(), mb.end()); wire::put(b, caps_.build, 4);
        rs_.drop(Msg::Propose); propose_bytes_ = b;
        for (uint8_t p = 0; p < kMaxRoster; ++p) if (clients & (1u << p)) rs_.send(tx_, p, Msg::Propose, b, now, cfg_);
        deadline_ = now + cfg_.ack_timeout_ms;
        state_ = acks_ == roster_ ? State::Ready : State::Proposed;
        return Reason::None;
    }
    // Every roster member acked the current revision with matching digests.
    Reason start(uint32_t anchor)
    {
        if (state_ != State::Ready || acks_ != roster_) return Reason::NotReady;
        if (!local_.owned_by(this)) { fail(State::Failed, Reason::LayerLost, slot_); return Reason::LayerLost; }
        std::vector<uint8_t> b; put_header(b, Msg::Start, session_, revision_);
        detail::put_digest(b, digest_); wire::put(b, anchor, 4);
        for (uint8_t p = 0; p < kMaxRoster; ++p) if ((roster_ & (1u << p)) && p != slot_) rs_.send(tx_, p, Msg::Start, b, now_, cfg_);
        state_ = State::Started; anchor_ = anchor;
        return Reason::None;
    }
    void cancel(Reason r = Reason::HostCancelled) { if (!terminal(state_) && state_ != State::Idle) fail(State::Cancelled, r, slot_); else release(); }
    void peer_left(uint8_t peer) { if (!terminal(state_) && state_ != State::Idle) fail(State::Failed, Reason::PeerLeft, peer, false); }
    // The match is over (every peer calls this locally): restore the local preferences.
    void end_match() { release(); rs_.drop(Msg::Start); if (!terminal(state_)) state_ = State::Ended; }

    void tick(uint64_t now)
    {
        now = now_ = (now > now_ ? now : now_);   // time is monotonic inside the object: an older `now` never makes `now - last_heard` wrap
        rs_.tick(tx_, now, cfg_);
        if (state_ == State::Idle || terminal(state_)) return;
        {
            // liveness (the checks stop after handover(), the keep-alives do not): a peer that said hello and then went silent is dropped from the roster (before Start) or ends the match (after)
            for (uint8_t p = 0; p < kMaxRoster && !handover_; ++p) {
                if (!(roster_ & (1u << p)) || p == slot_ || !(hello_ & (1u << p))) continue;
                if (now - last_heard_[p] < cfg_.liveness_ms) continue;
                if (state_ == State::Started) { fail(State::Failed, Reason::PeerLost, p); return; }
                lost_mask_ |= static_cast<uint8_t>(1u << p); lost_reason_ = Reason::PeerLost;
                const Reason r = reroster(static_cast<uint8_t>(roster_ & ~(1u << p)), now, Reason::PeerLost);
                if (r != Reason::None) { bad_peer_ = p; fail(State::Failed, r, p, false); return; }
            }
            if (now >= next_ka_) {
                next_ka_ = now + cfg_.keepalive_ms;
                for (uint8_t p = 0; p < kMaxRoster; ++p)
                    if ((roster_ & (1u << p)) && p != slot_ && (hello_ & (1u << p))) {
                        std::vector<uint8_t> b; put_header(b, Msg::KeepAlive, session_, revision_);
                        wire::put(b, peers_caps_[p].epoch, 4); wire::put(b, static_cast<uint8_t>(state_), 1);
                        tx_.send(p, b.data(), b.size());
                    }
            }
        }
        if ((state_ == State::Open || state_ == State::Proposed) && now >= deadline_) {
            uint8_t missing = state_ == State::Open ? (roster_ & ~(1u << slot_) & ~hello_) : (roster_ & ~acks_);
            fail(State::TimedOut, Reason::Timeout, first_missing(missing));
        }
    }

    void on_message(uint8_t from, const uint8_t* d, size_t n, uint64_t now)
    {
        now = now_ = (now > now_ ? now : now_);   // time is monotonic inside the object: an older `now` never makes `now - last_heard` wrap
        if (terminal(state_) || state_ == State::Idle) { ++dropped_; return; }
        Header h;
        const Reason hr = decode_header(d, n, &h);
        if (hr == Reason::Malformed) { ++dropped_; return; }
        if (from >= kMaxRoster || !(roster_ & (1u << from)) || from == slot_) { ++dropped_; return; }
        if (hr == Reason::SchemaMismatch) { fail(State::Failed, Reason::SchemaMismatch, from); return; }
        if (h.session != session_) { ++dropped_; return; }
        wire::Cur c{ d + kHeaderSize, n - kHeaderSize };
        const size_t payload = n - kHeaderSize;
        switch (h.type) {
        case Msg::Hello: {
            uint8_t slot; Caps pc;
            if (decode_hello(d, n, &slot, &pc) != Reason::None || slot != from) { ++dropped_; return; }
            if (state_ == State::Started) { ++dropped_; return; }                 // a mid-match Hello never ends the match
            if (pc.build != caps_.build) { fail(State::Failed, Reason::BuildMismatch, from); return; }
            const bool rejoin = pc.epoch != peers_caps_[from].epoch;
            if (rejoin) {
                // A late Hello of an older join must not rebind the peer to the old epoch: only an epoch not seen before is accepted.
                if (peers_caps_[from].epoch != 0 && !detail::epoch_newer(pc.epoch, peers_caps_[from].epoch)) { ++dropped_; return; }
                auto& seen = seen_epochs_[from];
                for (uint32_t e : seen) if (e == pc.epoch) { ++dropped_; return; }
                if (seen.size() >= 16) seen.erase(seen.begin());
                seen.push_back(pc.epoch);
            }
            last_heard_[from] = now;              // a new join of this peer (or its first Hello)
            if (state_ != State::Open) {
                // an open proposal: a peer that can no longer honour it fails the handshake; a repeated Hello of the same join is ignored
                Id id = Id::Rollback; Reason r = detail::check_caps(pc, manifest_.values, &id);
                if (r == Reason::None && pc.debug_active) r = Reason::DebugSwitch;
                if (r != Reason::None) { bad_id_ = id; fail(State::Failed, r, from); return; }
                if (!rejoin) { ++dropped_; return; }
                // The peer joined again (it lost its state): older Cancels to it are void, its old ack is void, it gets the proposal again.
                rs_.drop(Msg::Cancel, from);
                peers_caps_[from] = pc; acks_ &= static_cast<uint8_t>(~(1u << from));
                if (state_ == State::Ready) state_ = State::Proposed;
                rs_.drop(Msg::Propose, from);
                rs_.send(tx_, from, Msg::Propose, propose_bytes_, now, cfg_);
                deadline_ = now + cfg_.ack_timeout_ms;
                break;
            }
            if (rejoin) rs_.drop(Msg::Cancel, from);                              // queued repeats of an older Cancel to this peer are void
            peers_caps_[from] = pc; hello_ |= static_cast<uint8_t>(1u << from);
            const uint8_t clients = roster_ & ~(1u << slot_);
            if (auto_repropose_ && (hello_ & clients) == clients) {
                auto_repropose_ = false;
                const Reason r = propose(last_values_, now);
                if (r != Reason::None) { fail(State::Failed, r, bad_peer_); }
            }
            break;
        }
        case Msg::Ack: {
            if (state_ != State::Proposed || h.revision != revision_ || payload != 69) { ++dropped_; return; }
            const uint8_t slot = static_cast<uint8_t>(c.get(1));
            const uint32_t ep = static_cast<uint32_t>(c.get(4));
            if (ep != peers_caps_[from].epoch) { ++dropped_; return; }                // an Ack of an older join of this peer counts for nothing
            last_heard_[from] = now;
            Digest dg, ad;
            if (!detail::get_digest(c, &dg) || !detail::get_digest(c, &ad) || !c.done() || slot != from) { ++dropped_; return; }
            if (dg != digest_ || ad != applied_expect_) { fail(State::Failed, Reason::DigestMismatch, from); return; }
            acks_ |= static_cast<uint8_t>(1u << from);
            rs_.drop(Msg::Propose, from);
            if (acks_ == roster_) state_ = State::Ready;
            break;
        }
        case Msg::Reject: {
            // Also a Reject for an older revision: that client is finished and cannot ack a newer one, so its own reason ends the
            // handshake instead of a generic timeout later. A Reject for a future revision, or after Start, is dropped.
            if (state_ == State::Started || payload != 2 || (h.revision > revision_ && state_ != State::Open)) { ++dropped_; return; }
            const uint8_t r = static_cast<uint8_t>(c.get(1)), id = static_cast<uint8_t>(c.get(1));
            if (!valid_reason(r) || id >= kCount) { ++dropped_; return; }
            bad_id_ = static_cast<Id>(id);
            fail(State::Failed, static_cast<Reason>(r), from, false);
            break;
        }
        case Msg::KeepAlive: {
            if (payload != 5) { ++dropped_; return; }
            const uint32_t ep = static_cast<uint32_t>(c.get(4));
            if (ep != peers_caps_[from].epoch || !(hello_ & (1u << from))) { ++dropped_; return; }   // not this join: counts for nothing
            last_heard_[from] = now;
            break;
        }
        case Msg::Cancel: {
            if (payload != 5) { ++dropped_; return; }
            const uint8_t r = static_cast<uint8_t>(c.get(1));
            const uint32_t ep = static_cast<uint32_t>(c.get(4));
            if (!valid_reason(r) || ep != peers_caps_[from].epoch) { ++dropped_; return; }   // not this join: ignore
            if (handover_ && (static_cast<Reason>(r) == Reason::HostLost || static_cast<Reason>(r) == Reason::PeerLost)) { ++dropped_; return; }   // the rollback session owns liveness
            fail(State::Failed, static_cast<Reason>(r), from, false);
            break;
        }
        default: ++dropped_; break;
        }
    }

    State state() const { return state_; }
    // The reason the session ended; RestoreFailed when it ended without one but the local settings could not be confirmed afterwards.
    Reason reason() const { return reason_ != Reason::None ? reason_ : (terminal(state_) && restore_failed_) ? Reason::RestoreFailed : Reason::None; }
    // The reader did not show the local values after a layer was removed. STICKY BY DESIGN: it stays set across begin()/join() until the
    // process ends, because the process state is then unknown and the only honest advice is "restart the game".
    bool restore_failed() const { return restore_failed_; }
    uint8_t bad_peer() const { return bad_peer_; }
    Id bad_id() const { return bad_id_; }
    uint32_t revision() const { return revision_; }
    uint8_t acks() const { return acks_; }
    uint8_t hellos() const { return hello_; }
    uint32_t dropped() const { return dropped_; }
    uint32_t anchor() const { return anchor_; }
    const Manifest& manifest() const { return manifest_; }
    Channel channel() const { return cfg_.channel; }
    size_t pending_repeats() const { return rs_.pending(); }
private:
    static uint8_t first_missing(uint8_t mask) { for (uint8_t p = 0; p < kMaxRoster; ++p) if (mask & (1u << p)) return p; return 0; }
    void release() { if (!local_.clear_owned(this)) restore_failed_ = true; }
    void fail(State s, Reason r, uint8_t culprit, bool tell_culprit = true)
    {
        rs_.drop(Msg::Propose); rs_.drop(Msg::Start);
        for (uint8_t p = 0; p < kMaxRoster; ++p)
            if ((roster_ & (1u << p)) && p != slot_ && (tell_culprit || p != culprit))
                rs_.send(tx_, p, Msg::Cancel, detail::cancel_bytes(session_, revision_, r, peers_caps_[p].epoch), now_, cfg_);
        release();
        state_ = s; reason_ = r; bad_peer_ = culprit;
    }
    Transport& tx_; EffectiveValues& local_; Caps caps_; uint8_t slot_; Config cfg_;
    State state_ = State::Idle; Reason reason_ = Reason::None;
    uint64_t session_ = 0, seed_ = 0, deadline_ = 0, now_ = 0;
    uint32_t revision_ = 0, dropped_ = 0, anchor_ = 0;
    uint8_t roster_ = 0, hello_ = 0, acks_ = 0, bad_peer_ = 0;
    bool have_values_ = false, auto_repropose_ = false, restore_failed_ = false, handover_ = false;
    uint64_t next_ka_ = 0;
    std::array<uint64_t, kMaxRoster> last_heard_{};
    uint8_t lost_mask_ = 0; Reason lost_reason_ = Reason::None;
    std::array<std::vector<uint32_t>, kMaxRoster> seen_epochs_;   // epochs already bound to a peer in this session (bounded)
    std::vector<uint8_t> propose_bytes_;
    Id bad_id_ = Id::Rollback;
    std::array<Caps, kMaxRoster> peers_caps_{};
    Manifest manifest_; Values last_values_{}; Digest digest_{}, applied_expect_{};
    detail::Resender rs_;
};

class Client {
public:
    Client(Transport& t, EffectiveValues& local, const Caps& caps, uint8_t slot, const Config& cfg = {})
        : tx_(t), local_(local), caps_(caps), slot_(slot), cfg_(cfg) {}
    ~Client() { local_.clear_owned(this); }
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // Local join binds the session id; a proposal cannot choose it.
    Reason join(uint64_t session, uint8_t host_peer, uint64_t now)
    {
        if (!session || host_peer >= kMaxRoster || host_peer == slot_ || slot_ >= kMaxRoster) return Reason::Malformed;
        release();
        rs_.drop(Msg::Hello); rs_.drop(Msg::Ack); rs_.drop(Msg::Reject); rs_.drop(Msg::Cancel);
        session_ = session; host_ = host_peer; revision_ = 0; reason_ = Reason::None; now_ = now;
        state_ = State::Open; last_heard_ = now; wait_since_ = now; next_ka_ = now + cfg_.keepalive_ms; handover_ = false; host_state_ = State::Idle;
        epoch_ = detail::next_epoch() ^ (cfg_.epoch_salt << 8); if (!epoch_) epoch_ = 1;
        Caps c = caps_; c.debug_active = c.debug_active || local_.debug_switch() != nullptr; c.epoch = epoch_;
        rs_.send(tx_, host_, Msg::Hello, encode_hello(session_, slot_, c), now, cfg_);
        return Reason::None;
    }
    // Leave before or after applying: tells the host and restores the local preferences.
    void leave()
    {
        if (state_ == State::Idle || terminal(state_)) { release(); return; }
        rs_.send(tx_, host_, Msg::Cancel, detail::cancel_bytes(session_, revision_, Reason::LocalLeave, epoch_), now_, cfg_);
        end(State::Cancelled, Reason::LocalLeave);
    }
    // The local rollback session refused to start after the match began (rollback session): tell the host with a reason and end.
    void abort(Reason r)
    {
        if (state_ == State::Idle || terminal(state_)) { release(); return; }
        rs_.send(tx_, host_, Msg::Cancel, detail::cancel_bytes(session_, revision_, r, epoch_), now_, cfg_);
        end(State::Cancelled, r);
    }
    void match_ended() { release(); rs_.drop(Msg::Ack); if (!terminal(state_)) state_ = State::Ended; }

    void tick(uint64_t now)
    {
        now = now_ = (now > now_ ? now : now_);   // time is monotonic inside the object: an older `now` never makes `now - last_heard` wrap
        rs_.tick(tx_, now, cfg_);
        if (state_ != State::Open && state_ != State::Proposed && state_ != State::Started) return;
        // No fixed start timeout: the host may sit in character select. The client waits while the host's keep-alives arrive.
        if (!handover_ && now - last_heard_ >= cfg_.liveness_ms) {
            rs_.send(tx_, host_, Msg::Cancel, detail::cancel_bytes(session_, revision_, Reason::HostLost, epoch_), now, cfg_);
            end(State::Failed, Reason::HostLost);
            return;
        }
        if (now >= next_ka_) {
            next_ka_ = now + cfg_.keepalive_ms;
            std::vector<uint8_t> b; put_header(b, Msg::KeepAlive, session_, revision_);
            wire::put(b, epoch_, 4); wire::put(b, static_cast<uint8_t>(state_), 1);
            tx_.send(host_, b.data(), b.size());
        }
    }
    // The rollback session tracks the host from here: keep-alives and the silence check stop.
    // Only a running match can be handed over (H2); stops this side's silence check and its reaction to liveness Cancels; keep-alives keep
    // flowing (see Host::handover).
    bool handover() { if (state_ != State::Started) return false; handover_ = true; return true; }
    // For the UI: waiting for the host (joined or applied, match not started). `host_state` is what the host's last keep-alive said
    // (Open: gathering, Proposed: waiting for acks, Ready: everyone acked, e.g. host in character select); Idle until one arrived.
    struct HostWait { bool waiting; uint64_t waited_ms; uint64_t silent_ms; State host_state; };
    HostWait host_wait(uint64_t now) const
    {
        const bool w = state_ == State::Open || state_ == State::Proposed;
        return { w, w && now >= wait_since_ ? now - wait_since_ : 0, now >= last_heard_ ? now - last_heard_ : 0, host_state_ };
    }

    void on_message(uint8_t from, const uint8_t* d, size_t n, uint64_t now)
    {
        now = now_ = (now > now_ ? now : now_);   // time is monotonic inside the object: an older `now` never makes `now - last_heard` wrap
        if (state_ == State::Idle || terminal(state_)) { ++dropped_; return; }
        if (from != host_) { ++dropped_; return; }
        Header h;
        const Reason hr = decode_header(d, n, &h);
        if (hr == Reason::Malformed) { ++dropped_; return; }
        if (hr == Reason::SchemaMismatch) { reject(h.revision, Reason::SchemaMismatch, Id::Rollback); return; }
        if (h.session != session_) { ++dropped_; return; }
        wire::Cur c{ d + kHeaderSize, n - kHeaderSize };
        const size_t payload = n - kHeaderSize;
        switch (h.type) {
        case Msg::KeepAlive: {
            if (payload != 5) { ++dropped_; return; }
            const uint32_t ep = static_cast<uint32_t>(c.get(4));
            const uint8_t hs = static_cast<uint8_t>(c.get(1));
            if (ep != epoch_ || hs > static_cast<uint8_t>(State::Failed)) { ++dropped_; return; }   // not this join: counts for nothing
            last_heard_ = now; host_state_ = static_cast<State>(hs);
            break;
        }
        case Msg::Propose: last_heard_ = now; on_propose(h, d + kHeaderSize, payload); break;
        case Msg::Start: {
            Digest dg;
            if (state_ == State::Started) { ++dropped_; return; }              // repeated Start: no-op
            if (state_ != State::Proposed || h.revision != revision_ || payload != 36) { ++dropped_; return; }
            if (!detail::get_digest(c, &dg)) { ++dropped_; return; }
            const uint32_t anchor = static_cast<uint32_t>(c.get(4));
            if (!c.done() || dg != digest_) { ++dropped_; return; }
            if (!local_.owned_by(this)) { layer_lost(); return; }
            last_heard_ = now;
            rs_.drop(Msg::Ack);
            state_ = State::Started; anchor_ = anchor;
            break;
        }
        case Msg::Cancel: {
            if (payload != 5) { ++dropped_; return; }
            const uint8_t r = static_cast<uint8_t>(c.get(1));
            const uint32_t ep = static_cast<uint32_t>(c.get(4));
            // Only a Cancel for this join and not older than the revision already applied ends it: a late repeat of an older Cancel
            // (for example RosterChanged before a quick leave and rejoin) must not split a match.
            if (!valid_reason(r) || ep != epoch_ || h.revision < revision_) { ++dropped_; return; }
            if (handover_ && (static_cast<Reason>(r) == Reason::HostLost || static_cast<Reason>(r) == Reason::PeerLost)) { ++dropped_; return; }   // the rollback session owns liveness
            end(State::Cancelled, static_cast<Reason>(r));
            break;
        }
        default: ++dropped_; break;
        }
    }

    State state() const { return state_; }
    Reason reason() const { return reason_ != Reason::None ? reason_ : (terminal(state_) && restore_failed_) ? Reason::RestoreFailed : Reason::None; }
    bool restore_failed() const { return restore_failed_; }   // sticky by design, see Host::restore_failed
    uint32_t epoch() const { return epoch_; }
    Id bad_id() const { return bad_id_; }
    uint32_t revision() const { return revision_; }
    uint32_t dropped() const { return dropped_; }
    uint32_t anchor() const { return anchor_; }
    const Manifest& manifest() const { return manifest_; }
    size_t pending_repeats() const { return rs_.pending(); }
    // "Applied and acknowledged" means the layer is installed and was read back, not that the match runs.
    bool applied() const { return local_.owned_by(this) && (state_ == State::Proposed || state_ == State::Started); }
private:
    void on_propose(const Header& h, const uint8_t* p, size_t n)
    {
        Manifest m;
        Reason dr = n < 4 ? Reason::Malformed : decode_manifest(p, n - 4, &m);
        uint32_t host_build = 0;
        if (dr == Reason::None) { wire::Cur bc{ p + n - 4, 4 }; host_build = static_cast<uint32_t>(bc.get(4)); }
        if (dr == Reason::None && (m.session != h.session || m.revision != h.revision)) dr = Reason::Malformed;
        if (state_ == State::Started) {
            // The match is running: a repeated proposal is a no-op, anything else is ignored. Settings cannot change mid-match.
            ++dropped_; return;
        }
        if (dr != Reason::None) { reject(h.revision, dr, Id::Rollback); return; }
        rs_.drop(Msg::Hello);
        if (m.revision < revision_) { ++dropped_; return; }            // stale: not an error
        const Digest dg = manifest_digest(m);
        if (m.revision == revision_) {
            if (dg == digest_) { if (state_ == State::Proposed) { if (!local_.owned_by(this)) layer_lost(); else send_ack(); } return; }    // same revision and digest: idempotent
            reject(h.revision, Reason::StaleRevision, Id::Rollback); return;
        }
        Id id = Id::Rollback;
        if (m.host != host_ || !(m.roster & (1u << slot_))) { reject(m.revision, Reason::NotInRoster, id); return; }
        if (host_build != caps_.build) { reject(m.revision, Reason::BuildMismatch, id); return; }
        if (local_.debug_switch() || caps_.debug_active) { reject(m.revision, Reason::DebugSwitch, id); return; }
        Reason r = detail::check_caps(caps_, m.values, &id);
        if (r == Reason::None && !values_valid(m.values, &id)) r = Reason::OutOfRange;
        if (r != Reason::None) { reject(m.revision, r, id); return; }
        Reason why;
        if (detail::install_local(local_, m, &applied_, &why, &id, this) != Res::Ok) { reject(m.revision, why, id); return; }
        manifest_ = m; digest_ = dg; revision_ = m.revision;
        state_ = State::Proposed;
        send_ack();
    }
    void send_ack()
    {
        std::vector<uint8_t> b; put_header(b, Msg::Ack, session_, revision_);
        wire::put(b, slot_, 1); wire::put(b, epoch_, 4); detail::put_digest(b, digest_); detail::put_digest(b, applied_);
        rs_.drop(Msg::Ack);
        rs_.send(tx_, host_, Msg::Ack, std::move(b), now_, cfg_);
    }
    void reject(uint32_t rev, Reason r, Id id)
    {
        std::vector<uint8_t> b; put_header(b, Msg::Reject, session_, rev);
        wire::put(b, static_cast<uint8_t>(r), 1); wire::put(b, static_cast<uint8_t>(id), 1);
        rs_.send(tx_, host_, Msg::Reject, std::move(b), now_, cfg_);
        bad_id_ = id;
        end(State::Failed, r);
    }
    // Another session took over the shared layer: this object must not ack or start on values it no longer owns.
    void layer_lost()
    {
        rs_.send(tx_, host_, Msg::Cancel, detail::cancel_bytes(session_, revision_, Reason::LayerLost, epoch_), now_, cfg_);
        end(State::Failed, Reason::LayerLost);
    }
    void release() { if (!local_.clear_owned(this)) restore_failed_ = true; }
    void end(State s, Reason r) { release(); rs_.drop(Msg::Hello); rs_.drop(Msg::Ack); state_ = s; reason_ = r; }
    Transport& tx_; EffectiveValues& local_; Caps caps_; uint8_t slot_; Config cfg_;
    State state_ = State::Idle; Reason reason_ = Reason::None;
    uint64_t session_ = 0, now_ = 0, last_heard_ = 0, wait_since_ = 0, next_ka_ = 0;
    bool handover_ = false;
    State host_state_ = State::Idle;
    uint32_t revision_ = 0, dropped_ = 0, anchor_ = 0;
    uint8_t host_ = 0;
    uint32_t epoch_ = 0;
    bool restore_failed_ = false;
    Id bad_id_ = Id::Rollback;
    Manifest manifest_; Digest digest_{}, applied_{};
    detail::Resender rs_;
};

} // namespace jpsm
#endif
