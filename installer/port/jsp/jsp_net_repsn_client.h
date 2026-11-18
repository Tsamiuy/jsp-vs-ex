// jsp_net_repsn_client.h -- ENHANCEMENT (rollback over re-psn, direct or hole-punched): the real IPsnrTransport,
// over the re-psn C client (toolkit patch 7918: libs/network/psnr, re-psn v0.8.0, MIT). It opens its OWN client connection (the game's sceNp client stays off: JSP_ROLLBACK and the
// game's own online exclude each other, jpno::resolve_env), speaks the room messages of re-psn docs/api.md "Rooms" and "UDP", and makes the UdpMux socket THE P2P socket.
//
// Wire format, transcribed from re-psn docs/api.md at v0.8.0 (every integer big-endian; request payload = u32 req + body, written by psnr_send; replies/pushes arrive in psnr_msg.data
// WITHOUT the request id; blob = u16 length + bytes):
//   CREATE_ROOM 0x10  body: max u8 | flags u32 | external blob | internal blob | member_data blob            -> ROOM_JOINED 0x90
//   SEARCH_ROOMS 0x11 body: start u16 | max u16                                                              -> ROOM_LIST 0x91
//   JOIN_ROOM 0x12    body: room_id u64 | member_data blob                                                   -> ROOM_JOINED 0x90
//   LEAVE_ROOM 0x13   body: room_id u64                                                                      -> OK 0x93
//   ROOM_JOINED  : room_id u64 | my_member u16 | owner u16 | max u8 | flags u32 | external blob | internal blob | count u8 | member x count   (psnr_room_joined_parse)
//   ROOM_LIST    : total u16 | count u16 | count x (room_id u64 | owner online_id[16] | cur u8 | max u8 | flags u32 | external blob)             (psnr_room_list_parse / psnr_room_next)
//   member       : member_id u16 | user_id u32 | online_id[16] | ip[4] | p2p_port u16 | owner u8 | data blob                                          (psnr_member_next / psnr_member_parse)
//   MEMBER_JOINED 0xA1 push: room_id u64 | member ;  MEMBER_LEFT 0xA2 push: room_id u64 | member_id u16 | owner u16 (the owner after the leave) ;  ERROR 0x8F: code u32
//   UDP (same port as TCP): PROBE from the P2P socket (psnr_probe_packet, repeated until the PROBE_REPLY, then every 15 s), PUNCH to a peer (psnr_punch_packet), RELAY 0x03
//   (psnr_relay_wrap out, psnr_relay_unwrap_from in: never the plain unwrap, a room member could forge from_user), anything starting with "PSNR" is control (psnr_is_control).
// Threading: psnr_connect_timeout runs on a DETACHED worker thread that owns a shared result (DNS is unbounded, so nothing may wait for it: an abort abandons the result and the worker
// closes a late client itself); every other psnr_* call is made from ONE thread, the one that calls pump() and the IPsnrTransport methods (the glue's pump thread, from the start).
// ASSUMPTION: a SEARCH lists at most 64 rooms (start 0, max 64); a server with more rollback rooms of other builds could hide the matching one. The UdpMux rx/tx hooks run on whichever thread pumps the mux and only read the client's constant fields plus atomics here.
#ifndef JSP_NET_REPSN_CLIENT_H
#define JSP_NET_REPSN_CLIENT_H

#include "jsp_net_repsn.h"
#include "psnr.h"
#include <atomic>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace jspnet {

struct RepsnIdentity {
    std::string host; uint16_t port = 36100;   // repsn.ini server=host[:port]
    std::string name;                           // repsn.ini name= (the online id)
    std::string comm_id = "NPWR08138_00";       // repsn.ini comm_id=, else the title's NP communication id
    bool report_rtt = false;                    // repsn.ini report_rtt= (psnr_report_rtt)
};
// "host" or "host:port" (port 1..65535)
inline bool parse_server(const std::string& s, std::string& host, uint16_t& port)
{
    if (s.empty()) return false;
    const size_t c = s.rfind(':');
    if (c == std::string::npos) { host = s; port = 36100; return true; }
    const long p = std::strtol(s.c_str() + c + 1, nullptr, 10);
    if (c == 0 || p < 1 || p > 65535) return false;
    host = s.substr(0, c); port = (uint16_t)p; return true;
}

class PsnrClientTransport : public IPsnrTransport {
public:
    PsnrClientTransport(UdpMux& mux, const RepsnIdentity& id) : mux_(mux), id_(id), sh_(std::make_shared<Shared>()) {}
    ~PsnrClientTransport() override { stop(); }
    // Start connecting (detached worker). The mux must be open: its port is the P2P port in HELLO. allow_relay: the player's choice; the relay is only ever used when offered too.
    void start(bool allow_relay)
    {
        allow_relay_ = allow_relay;
        if (id_.name.empty() || id_.name.size() > PSNR_ONLINE_ID_MAX) { sticky_fail("re-psn: the online name must be 1 to " + std::to_string(PSNR_ONLINE_ID_MAX) + " characters (repsn.ini name=)"); return; }
        if (id_.comm_id.size() > PSNR_COMM_ID_MAX) { sticky_fail("re-psn: the communication id is longer than " + std::to_string(PSNR_COMM_ID_MAX) + " characters (repsn.ini comm_id=)"); return; }
        install_hooks();
        std::shared_ptr<Shared> sh = sh_; const RepsnIdentity id = id_; const uint16_t p2p = mux_.local_port();
        std::thread([sh, id, p2p] {
            uint32_t user = 0; uint8_t pub[4] = {};
            psnr_client* c = psnr_connect_timeout(id.host.c_str(), id.port, id.comm_id.c_str(), id.name.c_str(), p2p, &user, pub, 10000);
            std::lock_guard<std::mutex> lk(sh->mu);
            if (!c) {
                const int e = psnr_connect_error();
                sh->fail = "re-psn: cannot connect to " + id.host + ":" + std::to_string(id.port) + (e ? " (error " + std::to_string(e) + (e == PSNR_E_NAME_TAKEN ? ", the online name is taken" : "") + ")" : "");
                sh->state = 2; return;
            }
            if (sh->abandoned) { psnr_close(c); return; }   // stop() ran while we connected: nobody wants this client
            sh->c = c; sh->user = user; psnr_server_addr(c, sh->ip, &sh->port); sh->state = 1;
        }).detach();
    }
    void stop()
    {
        mux_.set_hooks(nullptr, nullptr);   // under the mux lock: no hook is running after this returns
        { std::lock_guard<std::mutex> lk(sh_->mu); sh_->abandoned = true; if (!sh_->taken && sh_->c) { psnr_close(sh_->c); sh_->c = nullptr; } }
        psnr_client* c = c_.exchange(nullptr);
        if (c) { if (room_) { std::vector<uint8_t> b; put64(b, room_); psnr_send(c, PSNR_LEAVE_ROOM, b.data(), (uint32_t)b.size()); room_ = 0; } psnr_close(c); }
    }
    // The pump thread calls this every iteration from the start (heartbeat every 20 s from psnr_poll: re-psn drops a client that is silent for 90 s; PROBE every 15 s; events queued for poll_event).
    // It does nothing until the connection is up.
    void pump()
    {
        if (!adopt()) return;
        psnr_client* c = c_.load();
        probe_tick(c);
        psnr_msg m; int r;
        while ((r = psnr_poll(c, &m)) == 1) { handle(m); psnr_msg_free(&m); }
        if (r < 0 && !dead_) {
            dead_ = true; PsnrEvent e; e.kind = PsnrEvent::Error; e.err = -1000 - psnr_dead_reason(c); ev_.push_back(e);
            // The rendezvous reads events only once a room exists; before that (connect, lookup) failure() is what it asks. A dead connection stays failed.
            sticky_fail("re-psn: the connection to the server was lost (reason " + std::to_string(psnr_dead_reason(c)) + ")");
        }
    }
    // ---- IPsnrTransport ----
    const char* failure() override
    {
        if (!sticky_.empty()) return sticky_.c_str();
        std::lock_guard<std::mutex> lk(sh_->mu);
        if (sh_->state != 2) return nullptr;
        sticky_ = sh_->fail; return sticky_.c_str();
    }
    bool ready() override
    {
        pump();
        if (!c_.load()) return false;
        // The members' endpoints come from the PROBE: wait for the reply (a few seconds at most; without it the server hands out the TCP address and the HELLO port, which works on a LAN).
        return probe_ok_.load() || now_ms() - t_first_probe_ > 3000;
    }
    uint32_t self_user() override { return self_user_; }
    bool relay_offered() override { psnr_client* c = c_.load(); return c && psnr_relay_available(c) != 0; }
    bool search(std::vector<PsnrRoom>& out) override
    {
        pump();
        psnr_client* c = c_.load();
        if (!c || !sticky_.empty()) return false;
        if (!search_req_ && !search_done_) {
            uint8_t b[4]; psnr_put16(b, 0); psnr_put16(b + 2, 64); search_req_ = psnr_send(c, PSNR_SEARCH_ROOMS, b, 4);
            if (!search_req_) { sticky_fail("re-psn: the room search could not be sent (the connection is gone)"); return false; }   // 0 = the send failed
        }
        if (!search_done_) return false;
        out = rooms_; search_done_ = false; search_req_ = 0; return true;
    }
    void create_room(uint32_t flags, const std::string& external, const std::string& member_data) override
    {
        std::vector<uint8_t> b; b.push_back(2);   // max = 2: a rollback match has two players
        put32(b, flags); put_blob(b, external); put_blob(b, std::string()); put_blob(b, member_data);
        room_req_ = c_.load() ? psnr_send(c_.load(), PSNR_CREATE_ROOM, b.data(), (uint32_t)b.size()) : 0;
        if (!room_req_) sticky_fail("re-psn: the room could not be created (the request could not be sent)");
    }
    void join_room(uint64_t room_id, const std::string& member_data) override
    {
        std::vector<uint8_t> b; put64(b, room_id); put_blob(b, member_data);
        room_req_ = c_.load() ? psnr_send(c_.load(), PSNR_JOIN_ROOM, b.data(), (uint32_t)b.size()) : 0;
        if (!room_req_) sticky_fail("re-psn: the room could not be joined (the request could not be sent)");
    }
    bool poll_event(PsnrEvent& ev) override
    {
        pump();
        if (ev_.empty()) return false;
        ev = ev_.front(); ev_.pop_front(); return true;
    }
    void send_punch(const Endpoint& to) override
    {
        psnr_client* c = c_.load();
        if (!c) return;
        set_peer_ep(to);
        uint8_t p[PSNR_UDP_PUNCH_LEN]; psnr_punch_packet(c, p); mux_.send_control(to, p, sizeof p);
    }
    void leave_room() override
    {
        psnr_client* c = c_.load();
        if (!c || !room_) return;
        std::vector<uint8_t> b; put64(b, room_); psnr_send(c, PSNR_LEAVE_ROOM, b.data(), (uint32_t)b.size());
        room_ = 0; members_.clear();
    }
    void kick(uint32_t user) override   // KICK_MEMBER 0x16 body: room_id u64 | member_id u16 (owner only)
    {
        psnr_client* c = c_.load();
        if (!c || !room_) return;
        for (const auto& m : members_) if (m.second == user) { std::vector<uint8_t> b; put64(b, room_); uint8_t t[2]; psnr_put16(t, m.first); b.insert(b.end(), t, t + 2); psnr_send(c, PSNR_KICK_MEMBER, b.data(), (uint32_t)b.size()); return; }
    }
    // The peer is known (RePsnRendezvous::on_path): its user id (relayed datagrams are only accepted from it). Its endpoint is set by send_punch.
    void set_peer_user(uint32_t peer_user) { peer_user_ = peer_user; }
    // relay on = every datagram to the peer's endpoint goes through the server (psnr_relay_wrap) and datagrams from the server unwrap to it; the player allowed it and the server offers it.
    // Relayed datagrams are accepted ONLY while this is on (the other side may switch first: its retransmissions arrive once we switch too).
    void set_relay_mode(bool on) { relay_on_ = on; }
    bool relay_mode() const { return relay_on_.load(); }
    bool probed() const { return probe_ok_.load(); }
    std::atomic<uint64_t> relay_tx{0}, relay_rx{0}, relay_rx_dropped{0}, control_dropped{0};   // counters
    int rtt_ms() const { psnr_client* c = c_.load(); return c ? psnr_rtt_ms(c) : -1; }
private:
    struct Shared { std::mutex mu; int state = 0; std::string fail; psnr_client* c = nullptr; uint32_t user = 0; uint8_t ip[4] = {}; uint16_t port = 0; bool abandoned = false, taken = false; };   // state: 0 connecting, 1 up, 2 failed
    void sticky_fail(const std::string& why) { sticky_ = why; }
    // Takes the connected client over from the worker (once). True when the client is up.
    bool adopt()
    {
        if (c_.load()) return true;
        std::lock_guard<std::mutex> lk(sh_->mu);
        if (sh_->state != 1 || !sh_->c) return false;
        std::memcpy(srv_ip_, sh_->ip, 4); srv_port_ = sh_->port; self_user_ = sh_->user; sh_->taken = true;
        if (id_.report_rtt) psnr_report_rtt(sh_->c, 1);
        t_first_probe_ = now_ms();
        c_.store(sh_->c, std::memory_order_release);   // last: the hooks read srv_ip_ / srv_port_ only after they see c_
        return true;
    }
    static void put32(std::vector<uint8_t>& b, uint32_t v) { uint8_t t[4]; psnr_put32(t, v); b.insert(b.end(), t, t + 4); }
    static void put64(std::vector<uint8_t>& b, uint64_t v) { uint8_t t[8]; psnr_put64(t, v); b.insert(b.end(), t, t + 8); }
    static void put_blob(std::vector<uint8_t>& b, const std::string& s) { uint8_t t[2]; psnr_put16(t, (uint16_t)s.size()); b.insert(b.end(), t, t + 2); b.insert(b.end(), s.begin(), s.end()); }
    static Endpoint ep_of(const psnr_member& m) { Endpoint e; std::memcpy(&e.ip_be, m.ip, 4); e.port = m.p2p_port; return e; }
    static PsnrMember conv(const psnr_member& m)
    {
        PsnrMember r; r.user = m.user_id; r.member_id = m.member_id; r.ep = ep_of(m);
        if (m.data && m.data_len) r.data.assign((const char*)m.data, m.data_len);
        return r;
    }
    Endpoint server_ep() const { Endpoint e; std::memcpy(&e.ip_be, srv_ip_, 4); e.port = srv_port_; return e; }
    // Thread rule: both hooks run on whichever thread pumps the mux (under the mux lock) while the pump thread may be inside psnr_poll. They call only psnr_is_control,
    // psnr_probe_reply, psnr_relay_unwrap_from and psnr_relay_wrap, which read the client's server_ip, server_port, user_id and token and never write. Those fields are written
    // only while psnr_connect_timeout builds the client (psnr.c, before it returns the pointer), never again, so they are immutable once the client is adopted (c_ is stored
    // last, with release order; the hooks load it with acquire). Everything else of the client (psnr_poll, psnr_send, psnr_close) is called from the pump thread only.
    void install_hooks()
    {
        mux_.set_hooks(
            [this](Endpoint& from, uint8_t* buf, int& n) -> int {
                psnr_client* c = c_.load(std::memory_order_acquire);
                if (!c || n < 5 || !psnr_is_control(buf, (size_t)n)) return 0;
                uint8_t ip[4]; uint16_t port;
                // PROBE_REPLY only from the server's UDP address (anything else could move our idea of "probed")
                if (from == server_ep() && psnr_probe_reply(buf, (size_t)n, ip, &port)) { probe_ok_ = true; return 1; }
                uint8_t sip[4]; std::memcpy(sip, &from.ip_be, 4);
                uint32_t fu = 0; const uint8_t* pl = nullptr; size_t pn = 0;
                if (psnr_relay_unwrap_from(c, sip, from.port, buf, (size_t)n, &fu, &pl, &pn)) {
                    // only while the relay path is chosen, only from the room's other player
                    if (!relay_on_.load() || !have_peer_.load() || fu != peer_user_.load() || pn == 0) { relay_rx_dropped++; return 1; }
                    std::memmove(buf, pl, pn); n = (int)pn; from = peer_ep(); relay_rx++; return 2;
                }
                control_dropped++;   // a PUNCH from the peer, or anything else that starts with "PSNR": never reaches the session
                return 1;
            },
            [this](const Endpoint& to, const std::vector<uint8_t>& bytes) -> bool {
                psnr_client* c = c_.load(std::memory_order_acquire);
                if (!c || !relay_on_.load() || !have_peer_.load() || !(to == peer_ep())) return false;
                uint8_t out[UdpMux::kMaxDatagram + 64];
                const size_t n = psnr_relay_wrap(c, peer_user_.load(), bytes.data(), bytes.size(), out, sizeof out);
                if (!n) return true;   // does not fit: dropped like any lost datagram (never sent direct while the relay is chosen)
                mux_.send_control(server_ep(), out, n); relay_tx++; return true;
            });
    }
    void probe_tick(psnr_client* c)
    {
        const uint64_t t = now_ms();
        // until the reply: every second; afterwards every 15 s (home gateways drop an idle UDP mapping after about 30 s)
        const uint64_t every = probe_ok_.load() ? 15000 : 1000;
        if (last_probe_ && t - last_probe_ < every) return;
        last_probe_ = t;
        uint8_t p[PSNR_UDP_PROBE_LEN]; psnr_probe_packet(c, p);
        mux_.send_control(server_ep(), p, sizeof p);
    }
    void handle(const psnr_msg& m)
    {
        const uint8_t T = m.type;
        if (T == PSNR_ROOM_LIST && m.req == search_req_ && search_req_) {
            psnr_cursor cur; uint16_t total = 0; rooms_.clear();
            if (psnr_room_list_parse(&m, &cur, &total)) {
                psnr_room r;
                while (psnr_room_next(&cur, &r)) {
                    PsnrRoom x; x.id = r.room_id; x.flags = r.flags; x.members = r.cur; x.max_members = r.max; x.owner_name = r.owner;
                    if (r.external && r.external_len) x.external.assign((const char*)r.external, r.external_len);
                    rooms_.push_back(x);
                }
            }
            search_done_ = true;
        } else if (T == PSNR_ROOM_JOINED && room_req_ && m.req == room_req_) {
            psnr_room_joined rj;
            if (!psnr_room_joined_parse(&m, &rj)) { push_err(-1); return; }
            PsnrEvent e; e.kind = PsnrEvent::RoomJoined; e.room_id = rj.room_id; room_ = rj.room_id; room_req_ = 0; members_.clear();
            psnr_member mb;
            while (psnr_member_next(&rj.members, &mb)) {
                PsnrMember x = conv(mb); e.members.push_back(x); members_[x.member_id] = x.user;
                if (mb.member_id == rj.owner) e.owner = mb.user_id;
            }
            ev_.push_back(e);
        } else if (T == PSNR_MEMBER_JOINED && m.len > 8 && psnr_get64(m.data) == room_) {
            psnr_member mb; size_t used = 0;
            if (!psnr_member_parse(m.data + 8, m.len - 8, &mb, &used)) return;
            PsnrEvent e; e.kind = PsnrEvent::MemberJoined; e.room_id = room_; e.members.push_back(conv(mb)); members_[mb.member_id] = mb.user_id; ev_.push_back(e);
        } else if (T == PSNR_MEMBER_LEFT && m.len >= 12 && psnr_get64(m.data) == room_) {
            const uint16_t id = psnr_get16(m.data + 8);
            auto it = members_.find(id);
            PsnrEvent e; e.kind = PsnrEvent::MemberLeft; e.room_id = room_; e.user = it == members_.end() ? 0 : it->second;
            e.owner = [&] { auto o = members_.find(psnr_get16(m.data + 10)); return o == members_.end() ? 0u : o->second; }();
            if (it != members_.end()) members_.erase(it);
            ev_.push_back(e);
        } else if (T == PSNR_KICKED) {
            push_err(-2);
        } else if (T == PSNR_ERROR && m.len >= 4 && m.req && (m.req == room_req_ || m.req == search_req_)) {
            const int code = (int)psnr_get32(m.data);
            if (m.req == search_req_) { search_req_ = 0; search_done_ = false; sticky_fail("re-psn: the room search was refused (error " + std::to_string(code) + ")"); return; }   // ends the lookup (failure() is checked every poll)
            room_req_ = 0; push_err(code);
        }
    }
    void push_err(int code) { PsnrEvent e; e.kind = PsnrEvent::Error; e.err = code; ev_.push_back(e); }

    UdpMux& mux_; RepsnIdentity id_;
    std::shared_ptr<Shared> sh_;
    std::string sticky_;          // a failure that stays (pump thread only)
    bool dead_ = false;
    std::atomic<psnr_client*> c_{nullptr};   // set once by adopt(); the hooks read it with acquire
    uint32_t self_user_ = 0;
    uint8_t srv_ip_[4] = {}; uint16_t srv_port_ = 0;   // written by adopt() before c_
    std::atomic<bool> probe_ok_{false}, relay_on_{false}, have_peer_{false}; bool allow_relay_ = false;
    std::atomic<uint32_t> peer_user_{0}; std::atomic<uint64_t> peer_key_{0};   // peer endpoint as ip_be << 16 | port
    void set_peer_ep(const Endpoint& e) { peer_key_ = ((uint64_t)e.ip_be << 16) | e.port; have_peer_ = true; }
    Endpoint peer_ep() const { const uint64_t k = peer_key_.load(); Endpoint e; e.ip_be = (uint32_t)(k >> 16); e.port = (uint16_t)(k & 0xFFFF); return e; }
    uint64_t t_first_probe_ = 0, last_probe_ = 0;
    uint32_t search_req_ = 0, room_req_ = 0; bool search_done_ = false; std::vector<PsnrRoom> rooms_;
    uint64_t room_ = 0; std::map<uint16_t, uint32_t> members_;
    std::deque<PsnrEvent> ev_;
};

}  // namespace jspnet

#endif
