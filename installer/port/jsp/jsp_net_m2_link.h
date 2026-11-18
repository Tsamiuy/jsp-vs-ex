// jsp_net_m2_link.h -- ENHANCEMENT: the adapter between the settings handshake's state machines (jsp_session_handshake.h: jpsm::Host / Client,
// Transport::send(peer, ...) push + on_message(from, bytes, now)) and jsp_net_session.h's UdpMux. No game addresses; header-only; needs the handshake headers on the include path.
//
// DECISION (single retransmit layer): the handshake's messages travel as plain UNRELIABLE datagrams (UdpMux tag 'S') and the handshake's own Resender (Config::retries x
// retry_ms, KeepAlive every keepalive_ms) is the only retransmission. The reliable MessageChannel (stop-and-wait) is NOT under the handshake: stacking it would make
// two layers retransmit and delay each other (a 1 s handshake repeat queued behind a channel retry), and the handshake already has the liveness and failure semantics
// (PeerLost / HostLost / Timeout) that a channel failure would have to be mapped onto. The channel is used only by the rendezvous (HELO/PLAN) before the handshake.
// Channel failure therefore reaches the handshake as silence: no datagrams -> liveness_ms -> Reason::HostLost / PeerLost.
//
// Peer ids: the handshake's peer id == roster slot (0..3), "one roster slot per peer". For a 2-peer match host = 0, client = 1 (SessionPlan::peer_slot_mask() maps a peer to
// the game slots it owns: 2+2 players = {0b0011, 0b1100}). The sender of a datagram is identified by its source endpoint (never by a field of the message);
// datagrams from an unknown endpoint are dropped. Session id: SessionPlan::session_id is the same u64 as jpsm's `session`.
//
// Threading and time (handshake integration contract): drain + tick must run on a thread that scene loading cannot block, tick every ~100-250 ms, ONE monotonic ms
// clock (jspnet::now_ms()), datagrams are drained into on_message BEFORE tick, each stamped with its arrival time. M2Pump does exactly that; every call into the
// handshake objects from other threads must hold the same mutex (M2Pump::mutex()). ASSUMPTION: on_message may install the settings layer
// (EffectiveValues::install, Reader callbacks) from the pump thread; if the game's settings readers are not thread-safe, run those callbacks on the main
// thread instead (the pump then only drains into a queue).
#pragma once
#include "jsp_net_session.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace jspnet {

template <class TransportBase>
class M2LinkT : public TransportBase {
public:
    explicit M2LinkT(UdpMux& mux) : mux_(mux) {}
    // peer id -> endpoint (roster slot == peer id). Peers not set are unreachable.
    void set_peer(uint8_t peer, const Endpoint& ep) { if (peer < 4) { ep_[peer] = ep; have_[peer] = true; } }
    void send(uint8_t peer, const uint8_t* d, size_t n) override
    {
        if (peer < 4 && have_[peer]) mux_.send(ep_[peer], kTagSettings, d, n);
        else unroutable++;
    }
    // Drains every queued datagram of tag 'S' into sink(peer, data, n, arrival_ms). Sender endpoint -> peer id; unknown sources are dropped.
    template <class Sink> size_t drain(Sink&& sink)
    {
        size_t k = 0; Endpoint from{}; std::vector<uint8_t> d; uint64_t ms = 0;
        while (mux_.pop(kTagSettings, from, d, &ms)) {
            int peer = -1;
            for (int i = 0; i < 4; i++) if (have_[i] && ep_[i] == from) peer = i;
            if (peer < 0) { unknown_source++; continue; }
            sink((uint8_t)peer, d.data(), d.size(), ms); k++;
        }
        return k;
    }
    uint64_t unroutable = 0, unknown_source = 0;
private:
    UdpMux& mux_;
    Endpoint ep_[4]{};
    bool have_[4] = {false, false, false, false};
};

// Host thread: every ~10 ms pump the socket and drain datagrams into `on_message(peer, data, n, arrival_ms)`; every `tick_ms` call `tick(now_ms)` AFTER the drain.
// All under mutex(): the main thread takes it around any direct call into the handshake objects.
class M2Pump {
public:
    ~M2Pump() { stop(); }
    template <class Link, class OnMsg, class Tick>
    void start(UdpMux& mux, Link& link, OnMsg on_message, Tick tick, uint32_t tick_ms = 100)
    {
        stop();
        run_.store(true);
        th_ = std::thread([this, &mux, &link, on_message, tick, tick_ms]() {
            uint64_t next = now_ms();
            while (run_.load()) {
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    mux.pump();
                    link.drain(on_message);
                    const uint64_t t = now_ms();
                    if (t >= next) { next = t + tick_ms; tick(t); ticks_.fetch_add(1); }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    void stop() { run_.store(false); if (th_.joinable()) th_.join(); }
    std::mutex& mutex() { return mu_; }
    uint64_t ticks() const { return ticks_.load(); }
private:
    std::mutex mu_;
    std::thread th_;
    std::atomic<bool> run_{false};
    std::atomic<uint64_t> ticks_{0};
};

} // namespace jspnet
