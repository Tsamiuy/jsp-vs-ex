// jsp_net_session.cpp -- see jsp_net_session.h. GekkoNet [src:gekkonet] (BSD-2-Clause, Jamie Meyer) is linked as a static library built
// WITHOUT its asio adapter (GEKKONET_NO_ASIO): the transport here is our own UdpMux (a decision of this port).
#include "jsp_net_session.h"

#include <gekkonet.h>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t_;
typedef SOCKET SOCK_T;
static void sock_close(intptr_t s) { closesocket((SOCKET)s); }
static bool sock_nonblock(intptr_t s) { u_long one = 1; return ioctlsocket((SOCKET)s, FIONBIO, &one) == 0; }
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef socklen_t socklen_t_;
typedef int SOCK_T;
static void sock_close(intptr_t s) { ::close((int)s); }
static bool sock_nonblock(intptr_t s) { return fcntl((int)s, F_SETFL, fcntl((int)s, F_GETFL, 0) | O_NONBLOCK) == 0; }
#endif

namespace jspnet {

static uint64_t now_us()
{
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}


uint64_t now_ms() { return now_us() / 1000; }

// ---- Endpoint ---------------------------------------------------------------------------------------------------------------
std::array<uint8_t, 6> Endpoint::wire() const
{
    std::array<uint8_t, 6> w{};
    std::memcpy(w.data(), &ip_be, 4);
    w[4] = (uint8_t)(port & 0xFF); w[5] = (uint8_t)(port >> 8);
    return w;
}
bool Endpoint::from_wire(const uint8_t* p, size_t n, Endpoint* out)
{
    if (n != 6) return false;
    Endpoint e{};
    std::memcpy(&e.ip_be, p, 4);
    e.port = (uint16_t)(p[4] | (p[5] << 8));
    *out = e;
    return true;
}
bool parse_endpoint(const std::string& s, Endpoint& out)
{
    const size_t c = s.rfind(':');
    if (c == std::string::npos) return false;
    unsigned a, b, cc, d, port;
    char tail = 0;
    if (std::sscanf(s.substr(0, c).c_str(), "%u.%u.%u.%u%c", &a, &b, &cc, &d, &tail) != 4) return false;
    if (std::sscanf(s.c_str() + c + 1, "%u%c", &port, &tail) != 1) return false;
    if (a > 255 || b > 255 || cc > 255 || d > 255 || port > 65535) return false;
    const uint32_t host = (a << 24) | (b << 16) | (cc << 8) | d;
    out.ip_be = htonl(host);
    out.port = (uint16_t)port;
    return true;
}
std::string to_string(const Endpoint& e)
{
    const uint32_t h = ntohl(e.ip_be);
    char b[48];
    std::snprintf(b, sizeof b, "%u.%u.%u.%u:%u", h >> 24, (h >> 16) & 255, (h >> 8) & 255, h & 255, (unsigned)e.port);
    return b;
}

// ---- UdpMux -----------------------------------------------------------------------------------------------------------------
bool UdpMux::open(uint16_t port, std::string* err)
{
    close();
    std::lock_guard<std::recursive_mutex> lk(mu_);
#ifdef _WIN32
    static bool wsa = false;
    if (!wsa) { WSADATA d; if (WSAStartup(MAKEWORD(2, 2), &d) != 0) { if (err) *err = "WSAStartup failed"; return false; } wsa = true; }
#endif
    const intptr_t s = (intptr_t)::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == -1) { if (err) *err = "socket() failed"; return false; }
    sockaddr_in a{};
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons(port);
    if (::bind((SOCK_T)s, (sockaddr*)&a, sizeof a) != 0 || !sock_nonblock(s)) {
        if (err) *err = "bind/nonblock failed on port " + std::to_string(port);
        sock_close(s);
        return false;
    }
    socklen_t_ l = sizeof a;
    ::getsockname((SOCK_T)s, (sockaddr*)&a, &l);
    sock_ = s; port_ = ntohs(a.sin_port);
    return true;
}
void UdpMux::close()
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (sock_ != -1) sock_close(sock_);
    sock_ = -1; delayed_.clear(); q_.clear();
}
void UdpMux::set_link_model(const LinkModel& m)
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    lm_ = m; rng_ = m.seed ? m.seed : 1; lm_start_ms_ = now_ms();
}
uint32_t UdpMux::rnd() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 7; rng_ ^= rng_ << 17; return (uint32_t)(rng_ >> 11); }

void UdpMux::raw_send(const Endpoint& to, const std::vector<uint8_t>& bytes)
{
    if (tx_hook && tx_hook(to, bytes)) return;   // re-psn: this peer is reached through the server's relay
    raw_send_plain(to, bytes);
}
void UdpMux::set_hooks(std::function<int(Endpoint&, uint8_t*, int&)> rx, std::function<bool(const Endpoint&, const std::vector<uint8_t>&)> tx)
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    rx_hook = std::move(rx); tx_hook = std::move(tx);
}
void UdpMux::send_control(const Endpoint& to, const uint8_t* data, size_t len)
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (sock_ == -1) return;
    raw_send_plain(to, std::vector<uint8_t>(data, data + len));
}
void UdpMux::raw_send_plain(const Endpoint& to, const std::vector<uint8_t>& bytes)
{
    sockaddr_in a{};
    a.sin_family = AF_INET; a.sin_addr.s_addr = to.ip_be; a.sin_port = htons(to.port);
    const int r = (int)::sendto((SOCK_T)sock_, (const char*)bytes.data(), (int)bytes.size(), 0, (sockaddr*)&a, sizeof a);
    if (r < 0) {
        send_fail++;
#ifdef _WIN32
        last_send_err = (int)WSAGetLastError();
#else
        last_send_err = errno;
#endif
    }
}
void UdpMux::send(const Endpoint& to, uint8_t tag, const uint8_t* data, size_t len)
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (sock_ == -1) return;
    if (len + 1 > kMaxDatagram) { oversize++; return; }
    sent++;
    std::vector<uint8_t> b(len + 1);
    b[0] = tag; if (len) std::memcpy(b.data() + 1, data, len);
    if (lm_.blackout_to_ms > lm_.blackout_from_ms) {
        const uint64_t t = now_ms() - lm_start_ms_;
        if (t >= lm_.blackout_from_ms && t < lm_.blackout_to_ms) { dropped++; return; }
    }
    if (lm_.loss_permille && rnd() % 1000 < lm_.loss_permille) { dropped++; return; }
    const uint32_t d = lm_.delay_ms + (lm_.jitter_ms ? rnd() % (lm_.jitter_ms + 1) : 0);
    if (!d) { raw_send(to, b); return; }
    delayed_.push_back({now_us() + (uint64_t)d * 1000, to, std::move(b)});
}
void UdpMux::pump()
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (sock_ == -1) return;
    if (!delayed_.empty()) {
        const uint64_t t = now_us();
        for (size_t i = 0; i < delayed_.size();) {
            if (delayed_[i].due_us <= t) { raw_send(delayed_[i].to, delayed_[i].bytes); delayed_.erase(delayed_.begin() + i); }
            else i++;
        }
    }
    uint8_t buf[kMaxDatagram + 1 + 64];   // 64 spare bytes: a relayed datagram carries the relay header (re-psn), unwrapped below
    for (;;) {
        sockaddr_in a{}; socklen_t_ l = sizeof a;
        int n = (int)::recvfrom((SOCK_T)sock_, (char*)buf, sizeof buf, 0, (sockaddr*)&a, &l);
        if (n < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAECONNRESET) continue;   // an earlier send bounced (ICMP); the next datagram can still be read
#endif
            break;
        }
        if (n < 1) continue;
        Endpoint from{}; from.ip_be = a.sin_addr.s_addr; from.port = ntohs(a.sin_port);
        if (rx_hook) { const int r = rx_hook(from, buf, n); if (r == 1) continue; }
        if (n < 1) continue;
        if ((size_t)n > kMaxDatagram) { oversize++; continue; }
        received++;
        if (buf[0] != kTagGekko && buf[0] != kTagMessage && buf[0] != kTagSettings && buf[0] != kTagHeartbeat) { unknown_tag++; continue; }   // never queue an unknown tag
        auto& q = q_[buf[0]];
        if (q.size() >= kMaxQueued) { q.pop_front(); overflow++; }
        q.push_back({from, std::vector<uint8_t>(buf + 1, buf + n), now_ms()});
    }
}
bool UdpMux::pop(uint8_t tag, Endpoint& from, std::vector<uint8_t>& data, uint64_t* arrived_ms)
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    auto it = q_.find(tag);
    if (it == q_.end() || it->second.empty()) return false;
    from = it->second.front().from; data = std::move(it->second.front().data);
    if (arrived_ms) *arrived_ms = it->second.front().ms;
    it->second.pop_front();
    return true;
}

// ---- PumpThread / heartbeat -------------------------------------------------------------------------------------------------
struct PumpThread::Impl { std::thread th; std::atomic<bool> stop{false}; };
void PumpThread::start(UdpMux& mux, uint64_t session_id, uint8_t my_peer, std::vector<Endpoint> peers, uint32_t period_ms)
{
    stop();
    p_ = new Impl;
    Impl* im = p_;
    std::atomic<uint64_t>* beats = &beats_;
    im->th = std::thread([=, &mux]() {
        uint64_t next = now_ms();
        while (!im->stop.load()) {
            mux.pump();
            const uint64_t t = now_ms();
            if (t >= next) {
                next = t + period_ms;
                uint8_t b[9];
                for (int i = 0; i < 8; i++) b[i] = (uint8_t)(session_id >> (8 * i));
                b[8] = my_peer;
                for (const Endpoint& e : peers) { mux.send(e, kTagHeartbeat, b, 9); beats->fetch_add(1); }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
    });
}
void PumpThread::stop()
{
    if (!p_) return;
    p_->stop.store(true);
    if (p_->th.joinable()) p_->th.join();
    delete p_; p_ = nullptr;
}
bool heartbeat_pop(UdpMux& mux, Heartbeat* out)
{
    Endpoint from{}; std::vector<uint8_t> d; uint64_t ms = 0;
    while (mux.pop(kTagHeartbeat, from, d, &ms)) {
        if (d.size() != 9) continue;
        uint64_t s = 0;
        for (int i = 0; i < 8; i++) s |= (uint64_t)d[i] << (8 * i);
        *out = Heartbeat{s, d[8], from, ms};
        return true;
    }
    return false;
}

// ---- MessageChannel ---------------------------------------------------------------------------------------------------------
// packet: [type u8: 0 data, 1 ack][src_epoch u64][dst_epoch u64][seq u32][payload]. Stop-and-wait: one message in flight, 50 ms retransmit, 10 s budget.
static constexpr size_t kChHdr = 21;
static void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i))); }
static void put64(std::vector<uint8_t>& v, uint64_t x) { for (int i = 0; i < 8; i++) v.push_back((uint8_t)(x >> (8 * i))); }
static uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t get64(const uint8_t* p) { uint64_t x = 0; for (int i = 0; i < 8; i++) x |= (uint64_t)p[i] << (8 * i); return x; }

bool MessageChannel::send(const uint8_t* data, size_t len)
{
    if (failed_ || len > kMaxMessage || out_.size() >= kMaxQueue) return false;
    std::vector<uint8_t> p; p.push_back(0); put64(p, epoch_); put64(p, 0 /*dst filled at transmit*/); put32(p, next_seq_++);
    p.insert(p.end(), data, data + len);
    out_.push_back(std::move(p));
    return true;
}
bool MessageChannel::recv(std::vector<uint8_t>& out)
{
    if (in_.empty()) return false;
    out = std::move(in_.front()); in_.pop_front();
    return true;
}
void MessageChannel::pump()
{
    if (out_.empty() || failed_) return;
    const uint64_t t = now_us();
    if (tries_ && t - last_tx_us_ < 50000) return;
    if (tries_ >= 200) { failed_ = true; return; }
    std::vector<uint8_t> p = out_.front();
    for (int i = 0; i < 8; i++) p[9 + i] = (uint8_t)(peer_epoch_ >> (8 * i));   // dst epoch = the peer run we know (0 before first contact)
    mux_.send(peer_, kTagMessage, p.data(), p.size());
    last_tx_us_ = t; tries_++;
}
void MessageChannel::reset_for_new_peer_run(uint64_t e)
{
    peer_epoch_ = e; in_.clear(); out_.clear(); expect_seq_ = 0; next_seq_ = 0; tries_ = 0; failed_ = false; resets++;
}
bool MessageChannel::on_packet(const std::vector<uint8_t>& pkt)
{
    if (pkt.size() < kChHdr || pkt[0] > 1) { refused++; return false; }
    const uint64_t src = get64(pkt.data() + 1), dst = get64(pkt.data() + 9);
    const uint32_t seq = get32(pkt.data() + 17);
    if (!src || (dst && dst != epoch_) || (pkt[0] == 1 && dst != epoch_)) { refused++; return false; }   // earlier run of the sender or of us
    // Epochs are random identifiers, not ordered: a run of the peer is "new" when it is not the current one, was never retired, and opens with a
    // data packet of sequence 0 (the first message of a run). A packet of a retired (earlier) run, or any other unknown epoch, is refused.
    if (src != peer_epoch_) {
        if (std::find(retired_.begin(), retired_.end(), src) != retired_.end()) { refused++; return false; }
        if (!peer_epoch_) peer_epoch_ = src;
        else if (pkt[0] == 0 && seq == 0) { retired_.push_back(peer_epoch_); if (retired_.size() > 16) retired_.erase(retired_.begin()); reset_for_new_peer_run(src); }
        else { refused++; return false; }
    }
    if (pkt[0] == 1) {                                        // ack
        if (!out_.empty() && get32(out_.front().data() + 17) == seq) { out_.pop_front(); tries_ = 0; }
        return true;
    }
    std::vector<uint8_t> ack; ack.push_back(1); put64(ack, epoch_); put64(ack, peer_epoch_); put32(ack, seq);
    if (seq == expect_seq_) {
        if (in_.size() >= kMaxQueue) return false;           // not acked: the sender retransmits when we drain
        in_.emplace_back(pkt.begin() + kChHdr, pkt.end()); expect_seq_++; mux_.send(peer_, kTagMessage, ack.data(), ack.size());
    } else if (seq < expect_seq_) mux_.send(peer_, kTagMessage, ack.data(), ack.size());   // duplicate: our ack was lost
    return true;
}

// A run identifier: random bits mixed with the monotonic clock, never 0, never the wall clock (identification only; not ordered).
static uint64_t random_epoch()
{
    static std::atomic<uint64_t> serial{0};
    uint64_t r = 0;
    try { std::random_device rd; r = ((uint64_t)rd() << 32) ^ rd(); } catch (...) {}
    r ^= now_ms() * 0x9e3779b97f4a7c15ull;
    r ^= (serial.fetch_add(1) + 1) * 0xc2b2ae3d27d4eb4full;
    r ^= (uint64_t)(uintptr_t)&serial;
    return r ? r : 1;
}
MessageHub::MessageHub(UdpMux& m, uint64_t epoch) : mux_(m), epoch_(epoch ? epoch : random_epoch()) {}
MessageChannel* MessageHub::find(const Endpoint& p) { auto i = ch_.find(p); return i == ch_.end() ? nullptr : i->second.get(); }
MessageChannel* MessageHub::open(const Endpoint& p)
{
    auto it = ch_.find(p);
    if (it != ch_.end()) return it->second.get();
    if (ch_.size() >= kMaxChannels) return nullptr;
    auto& c = ch_[p];
    c.reset(new MessageChannel(mux_, p, epoch_));
    return c.get();
}
void MessageHub::close(const Endpoint& p) { ch_.erase(p); }
void MessageHub::pump()
{
    mux_.pump();
    Endpoint from; std::vector<uint8_t> d;
    while (mux_.pop(kTagMessage, from, d)) {
        MessageChannel* c = find(from);
        if (!c) {
            if (d.size() < kChHdr || d[0] != 0 || get32(d.data() + 17) != 0) { ignored++; continue; }   // only a first DATA message opens a channel
            c = open(from);
            if (!c) { ignored++; continue; }
            new_peers.push_back(from);
        }
        c->on_packet(d);
    }
    for (auto& kv : ch_) kv.second->pump();
}

// ---- DirectRendezvous -------------------------------------------------------------------------------------------------------
// Messages: HELO {'H','E','L','O', local_count} client -> host; PLAN {'P','L','A','N', session_id u64, host_count, client_first, client_count}.
std::array<uint8_t, 4> SessionPlan::peer_slot_mask() const
{
    std::array<uint8_t, 4> m{};
    auto mask = [](unsigned first, unsigned n) { return (uint8_t)(((1u << n) - 1) << first); };
    const uint8_t mine = mask(local_first, local_count);
    if (remotes.empty()) { m[0] = mine; return m; }
    const uint8_t theirs = mask(remotes[0].first_slot, remotes[0].count);
    if (local_first == 0) { m[0] = mine; m[1] = theirs; } else { m[0] = theirs; m[1] = mine; }
    return m;
}

int DirectRendezvous::poll()
{
    if (state_ != 0) return state_;
    hub_.pump();
    std::vector<uint8_t> m;
    if (host_) {
        if (!ch_) {
            const std::vector<Endpoint> cand = hub_.new_peers;
            hub_.new_peers.clear();
            for (const Endpoint& p : cand) {
                MessageChannel* c = hub_.find(p);
                if (has_only_ && !(p == only_)) { rejected_hello++; hub_.close(p); continue; }   // re-psn: only the room's other player
                const bool ok = c && c->recv(m) && m.size() == 5 && std::memcmp(m.data(), "HELO", 4) == 0 && m[4] >= 1 && m[4] + local_count_ <= 4 && local_count_ >= 1;
                if (ok && !ch_) { ch_ = c; hello_count_ = m[4]; }
                else { rejected_hello++; hub_.close(p); }
            }
            if (!ch_) return 0;
        }
        if (ch_->failed()) { err_ = "client channel failed"; return state_ = -1; }
        const uint8_t cc = hello_count_;
        std::vector<uint8_t> r = {'P', 'L', 'A', 'N'}; put64(r, plan_.session_id);
        r.push_back(local_count_); r.push_back(local_count_); r.push_back(cc);
        ch_->send(r.data(), r.size());
        plan_.host = true; plan_.total_players = local_count_ + cc; plan_.local_first = 0; plan_.local_count = local_count_;
        plan_.remotes = {{ch_->peer(), local_count_, cc}};
        return state_ = 1;
    }
    if (!ch_) { ch_ = hub_.open(host_ep_); if (!ch_) { err_ = "no channel"; return state_ = -1; } }
    if (!sent_hello_) { const uint8_t h[5] = {'H', 'E', 'L', 'O', local_count_}; ch_->send(h, 5); sent_hello_ = true; }
    if (ch_->failed()) { err_ = "host unreachable"; return state_ = -1; }
    if (!ch_->recv(m)) return 0;
    if (m.size() != 15 || std::memcmp(m.data(), "PLAN", 4) != 0) { err_ = "bad PLAN"; return state_ = -1; }
    plan_.session_id = get64(m.data() + 4);
    const uint8_t hc = m[12], cf = m[13], cc = m[14];
    if (!plan_.session_id || cc != local_count_ || !hc || hc + cc > 4 || cf != hc) { err_ = "plan disagrees with local player count"; return state_ = -1; }
    plan_.host = false; plan_.total_players = hc + cc; plan_.local_first = cf; plan_.local_count = cc;
    plan_.remotes = {{host_ep_, 0, hc}};
    return state_ = 1;
}

// ---- NetSession -------------------------------------------------------------------------------------------------------------
namespace {
// GekkoNetAdapter has no user pointer: one adapter per process, bound to the mux of the running session.
UdpMux* g_mux = nullptr;
std::vector<GekkoNetResult*> g_results;

void ad_send(GekkoNetAddress* a, const char* data, int len)
{
    Endpoint e{};
    if (!g_mux || !Endpoint::from_wire((const uint8_t*)a->data, a->size, &e)) return;
    g_mux->send(e, kTagGekko, (const uint8_t*)data, (size_t)len);
}
GekkoNetResult** ad_recv(int* n)
{
    g_results.clear();
    if (g_mux) {
        g_mux->pump();
        Endpoint from{}; std::vector<uint8_t> d;
        while (g_mux->pop(kTagGekko, from, d)) {
            GekkoNetResult* r = (GekkoNetResult*)std::malloc(sizeof(GekkoNetResult));
            const std::array<uint8_t, 6> w = from.wire();
            r->addr.data = std::malloc(w.size()); r->addr.size = (unsigned)w.size();
            std::memcpy(r->addr.data, w.data(), w.size());
            r->data_len = (unsigned)d.size(); r->data = std::malloc(d.size() ? d.size() : 1);
            if (!d.empty()) std::memcpy(r->data, d.data(), d.size());
            g_results.push_back(r);
        }
    }
    *n = (int)g_results.size();
    return g_results.data();
}
void ad_free(void* p) { std::free(p); }
GekkoNetAdapter g_adapter = {ad_send, ad_recv, ad_free};

// The state buffer GekkoNet stores per saved frame. It names the save; the bytes live in the snapshot ring (the buffer cannot be a spectator
// state transfer, see the plan). Fixed little-endian-agnostic: only read back by the same process.
struct Token { uint32_t magic, frame, token, checksum; };
constexpr uint32_t kTokenMagic = 0x4A534E32;   // "JSN2" (frame format version 2)

// ENHANCEMENT (replays): the head of a GekkoNet replay (GKRP), as the pinned replay.cpp writes it with zpp::bits (little endian, no padding): header u32,
// version u32, compressed u8, then GekkoConfig member by member: num_players u8, max_spectators u8, max_input_prediction_window u8, spectator_delay u32,
// input_size u32, state_size u32, ... GekkoNet has no call that returns a loaded replay's config, and the state size must be ours before a Save writes a
// Token into GekkoNet's buffer. The host test reads back recordings this session wrote, so a layout change in a GekkoNet update fails there first.
struct ReplayHead { uint32_t version = 0; uint8_t players = 0; uint32_t input_size = 0, state_size = 0; };
bool read_replay_head(const std::vector<uint8_t>& b, ReplayHead* h)
{
    auto u32 = [&](size_t o) { return (uint32_t)b[o] | (uint32_t)b[o + 1] << 8 | (uint32_t)b[o + 2] << 16 | (uint32_t)b[o + 3] << 24; };
    if (b.size() < 24 || u32(0) != 0x474B5250u) return false;   // "GKRP"
    h->version = u32(4); h->players = b[9]; h->input_size = u32(16); h->state_size = u32(20);
    return h->version == 2;
}
}

NetSession::NetSession() {}
NetSession::~NetSession() { stop(); }

// ENHANCEMENT (lobby): see the header. Everything start() / start_local() set or counted goes back to its initial value; the next start creates a new GekkoNet
// session (a new session magic, so stragglers of the old one addressed to it are dropped by GekkoNet) and binds the adapter to its own mux again.
void NetSession::stop()
{
    stop_keepalive();   // first: the thread takes gmu_ and calls into GekkoNet
    std::lock_guard<std::recursive_mutex> lk(gmu_);
    if (gs_) { GekkoSession* g = (GekkoSession*)gs_; gekko_destroy(&g); gs_ = nullptr; }
    g_mux = nullptr;   // one session per process (see start): nothing else is bound to the adapter
    g_results.clear();
    mode_ = SessionMode::Game; cur_delay_ = 0;
    rec_copy_.clear(); rec_updates_ = 0; recording_ = false; replay_finished_ = false;
    game_call_ms_.store(0); max_gap_ms_.store(0); ka_stop_.store(false); last_poll_ms_.store(0); ka_polls_.store(0);
    plan_ = SessionPlan{}; cfg_ = SessionConfig{};
    pending_.clear(); events_.clear(); psaves_.clear();
    next_id_ = 1; pending_saves_ = 0; pace_us_ = 0; polled_events_ = 0;
    started_ = false; failed_ = false; failure_.clear(); start_error_.clear(); desync_tick_ = kNoTick;
    advances_ = resim_ = saves_ = loads_ = deferred_ = 0;
}

void NetSession::stop_keepalive()
{
    ka_stop_.store(true);
    if (ka_.joinable()) ka_.join();
}
void NetSession::fail(const std::string& why)
{
    std::lock_guard<std::recursive_mutex> lk(gmu_);
    if (failed_) return;
    failed_ = true; failure_ = why;
    events_.push_back({SessionEventKind::Failed, -1, -1, 0, 0, why});
    // Tell the peer at once: the local actor leaves, which flushes the disconnect message right away (GekkoNet drops every remote with it).
    if (gs_ && plan_.local_count) gekko_disconnect_actor((GekkoSession*)gs_, plan_.local_first);
}
void NetSession::abort(const std::string& why) { fail(why); }

bool NetSession::start(UdpMux& mux, const SessionPlan& plan, const SessionConfig& cfg)
{
    auto refuse = [&](const std::string& why) { start_error_ = why; return false; };
    if (gs_) return refuse("already started");
    if (!plan.total_players || plan.total_players > 4 || !plan.local_count || !cfg.input_size) return refuse("bad plan or input size");
    if (cfg.max_prediction >= cfg.ring_depth) return refuse("max_prediction (" + std::to_string(cfg.max_prediction) + ") must be below the snapshot ring depth (" + std::to_string(cfg.ring_depth) + ")");
    const unsigned d0 = cfg.local_delay.empty() ? 0 : cfg.local_delay[0];
    for (unsigned n = 0; n < plan.local_count; n++) {
        const unsigned dn = n < cfg.local_delay.size() ? cfg.local_delay[n] : 0;
        if (dn != d0) return refuse("local delays on one peer must be equal (GekkoNet desyncs under loss with unequal local delays)");
    }
    plan_ = plan; cfg_ = cfg;
    g_mux = &mux;
    GekkoSession* g = nullptr;
    if (!gekko_create(&g, GekkoGameSession)) return refuse("gekko_create failed");
    GekkoConfig c{};
    c.num_players = plan.total_players;
    c.max_spectators = 0;
    c.max_input_prediction_window = cfg.max_prediction;
    c.input_size = cfg.input_size;
    c.state_size = sizeof(Token);
    c.limited_saving = cfg.limited_saving;       // see SessionConfig::limited_saving (desync detection then runs on GekkoNet's health frames)
    c.desync_detection = cfg.desync_detection;
    c.check_distance = 0;
    gekko_start(g, &c);
    gekko_net_adapter_set(g, &g_adapter);
    gekko_set_disconnect_timeout(g, cfg.disconnect_timeout_ms);
    // Actors in slot order so every peer gets the same handle for the same slot.
    for (unsigned s = 0; s < plan.total_players; s++) {
        if (s >= plan.local_first && s < (unsigned)plan.local_first + plan.local_count) {
            const int h = gekko_add_actor(g, GekkoLocalPlayer, nullptr);
            if (h != (int)s) { gekko_destroy(&g); return refuse("local actor handle mismatch"); }
            gekko_set_local_delay(g, h, (unsigned char)d0); cur_delay_ = (uint8_t)d0;
        } else {
            const PeerSlots* ps = nullptr;
            for (const PeerSlots& r : plan.remotes) if (s >= r.first_slot && s < (unsigned)r.first_slot + r.count) ps = &r;
            if (!ps) { gekko_destroy(&g); return refuse("slot without an owner"); }
            std::array<uint8_t, 6> w = ps->ep.wire();     // the explicit 6-byte form: GekkoNet compares address bytes, the struct has padding
            GekkoNetAddress a{w.data(), (unsigned)w.size()};
            if (gekko_add_actor(g, GekkoRemotePlayer, &a) != (int)s) { gekko_destroy(&g); return refuse("remote actor handle mismatch"); }
        }
    }
    if (cfg.record) {   // ENHANCEMENT (replays): from frame 0, no initial state (see SessionConfig::record), every confirmed tick's checksum
        const bool ok = gekko_start_recording(g, false, false) && gekko_record_checksums(g, 1) &&
                        (cfg.record_user_data.empty() || gekko_set_replay_user_data(g, cfg.record_user_data.data(), (unsigned)cfg.record_user_data.size()));
        if (!ok) { gekko_destroy(&g); return refuse("the replay recording could not start"); }
        recording_ = true;
    }
    gs_ = g;
    pending_.assign((size_t)plan.local_count * cfg.input_size, 0);   // zero wire bytes = neutral (see pad_pack_to_wire)
    last_poll_ms_.store(now_ms());
    if (cfg.keepalive_stale_ms) {
        const uint32_t stale = cfg.keepalive_stale_ms;
        ka_stop_.store(false);
        ka_ = std::thread([this, stale]() {
            while (!ka_stop_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(std::max<uint32_t>(5, stale / 5)));
                if (now_ms() - last_poll_ms_.load() < stale) continue;
                std::unique_lock<std::recursive_mutex> lk(gmu_, std::try_to_lock);
                if (!lk.owns_lock()) continue;   // the game thread is inside the session: it polls anyway
                if (ka_stop_.load() || failed_) break;   // a failed session sends nothing more (fail() already told the peer)
                poll_impl();
                ka_polls_.fetch_add(1);
            }
        });
    }
    return true;
}

bool NetSession::start_stress(uint8_t players, uint32_t distance, const SessionConfig& cfg)
{
    SessionPlan p; p.host = true; p.total_players = players; p.local_first = 0; p.local_count = players;
    return start_local(SessionMode::Stress, p, cfg, distance, nullptr);
}
bool NetSession::start_replay(const std::vector<uint8_t>& bytes, const SessionConfig& cfg)
{
    return start_local(SessionMode::Replay, SessionPlan{}, cfg, 0, &bytes);
}
// Stress and Replay sessions: no socket (the adapter is never set, GekkoNet sends nothing), no keepalive thread, no disconnect timer.
bool NetSession::start_local(SessionMode m, const SessionPlan& plan, const SessionConfig& cfg, uint32_t distance, const std::vector<uint8_t>* bytes)
{
    auto refuse = [&](const std::string& why) { start_error_ = why; return false; };
    if (gs_) return refuse("already started");
    if (cfg.record && m != SessionMode::Stress) return refuse("a replay session cannot record");
    if (!cfg.input_size) return refuse("bad input size");
    if (cfg.max_prediction >= cfg.ring_depth) return refuse("max_prediction (" + std::to_string(cfg.max_prediction) + ") must be below the snapshot ring depth (" + std::to_string(cfg.ring_depth) + ")");
    SessionPlan p = plan;
    GekkoSession* g = nullptr;
    if (m == SessionMode::Stress) {
        if (!p.total_players || p.total_players > 4) return refuse("stress: 1 to 4 players");
        if (!distance || distance > cfg.max_prediction) return refuse("stress: the rollback distance must be 1.." + std::to_string(cfg.max_prediction));
        if (!gekko_create(&g, GekkoStressSession)) return refuse("gekko_create failed");
        GekkoConfig c{};
        c.num_players = p.total_players; c.max_input_prediction_window = cfg.max_prediction; c.input_size = cfg.input_size; c.state_size = sizeof(Token);
        c.limited_saving = false; c.desync_detection = true; c.check_distance = distance;
        gekko_start(g, &c);
        for (unsigned s = 0; s < p.total_players; s++)
            if (gekko_add_actor(g, GekkoLocalPlayer, nullptr) != (int)s) { gekko_destroy(&g); return refuse("local actor handle mismatch"); }
        // ENHANCEMENT (replays): a stress session records the INPUTS only (the bench: real local play for the input analysis, installer/tools/replay_inputs.py).
        // No checksums: the stress session stores a frame's checksum at the end of the update that issued its save, before our asynchronous commit fills it.
        if (cfg.record) {
            const bool ok = gekko_start_recording(g, false, false) &&
                            (cfg.record_user_data.empty() || gekko_set_replay_user_data(g, cfg.record_user_data.data(), (unsigned)cfg.record_user_data.size()));
            if (!ok) { gekko_destroy(&g); return refuse("the replay recording could not start"); }
            recording_ = true;
        }
    } else {
        ReplayHead h;
        if (!bytes || !read_replay_head(*bytes, &h)) return refuse("not a replay this build can read (GKRP version 2)");
        if (h.input_size != cfg.input_size) return refuse("replay input size " + std::to_string(h.input_size) + ", expected " + std::to_string(cfg.input_size));
        if (h.state_size != sizeof(Token)) return refuse("replay state size " + std::to_string(h.state_size) + " is not this port's");
        if (!h.players || h.players > 4) return refuse("replay: 1 to 4 players");
        if (!gekko_create(&g, GekkoReplaySession)) return refuse("gekko_create failed");
        if (!gekko_load_replay(g, bytes->data(), (unsigned)bytes->size())) { gekko_destroy(&g); return refuse("the replay could not be read"); }
        p = SessionPlan{}; p.host = true; p.total_players = h.players;   // local_count 0: every input comes from the replay
    }
    plan_ = p; cfg_ = cfg; cfg_.keepalive_stale_ms = 0; mode_ = m;
    gs_ = g;
    pending_.assign((size_t)p.local_count * cfg.input_size, 0);
    last_poll_ms_.store(now_ms());
    return true;
}

void NetSession::copy_recording()
{
    unsigned len = 0;
    const unsigned char* d = gekko_peek_recording((GekkoSession*)gs_, &len);
    if (d && len) rec_copy_.assign(d, d + len);
}
bool NetSession::recording(std::vector<uint8_t>& out)
{
    std::lock_guard<std::recursive_mutex> lk(gmu_);
    if (!gs_ || !recording_) return false;
    if (!pending_saves_) copy_recording();   // every save completed: the live recording has every confirmed checksum
    if (rec_copy_.empty()) return false;
    out = rec_copy_;
    return true;
}
bool NetSession::replay_user_data(std::vector<uint8_t>& out) const
{
    std::lock_guard<std::recursive_mutex> lk(gmu_);
    if (!gs_ || mode_ != SessionMode::Replay) return false;
    unsigned len = 0;
    const unsigned char* d = gekko_replay_user_data((GekkoSession*)gs_, &len);
    out.assign(d, d + (d ? len : 0));
    return d != nullptr;
}

void NetSession::set_local_input(uint8_t n, const uint8_t* in)
{
    std::lock_guard<std::recursive_mutex> lk(gmu_);
    if (n >= plan_.local_count) return;
    std::memcpy(pending_.data() + (size_t)n * cfg_.input_size, in, cfg_.input_size);
}

uint32_t NetSession::pace_sleep_ms(uint32_t tick_ms)
{
    if (!gs_ || failed_ || frames_ahead() <= 0.5f) return 0;
    pace_us_ += tick_ms * 16u;   // 1.6% of a tick (GekkoNet's example), accumulated in microseconds: no 1 ms floor
    const uint32_t ms = pace_us_ / 1000u; pace_us_ %= 1000u;
    return ms;
}
void NetSession::note_game_call()
{
    const uint64_t t = now_ms(), p = game_call_ms_.exchange(t);
    if (p && t - p > max_gap_ms_.load()) max_gap_ms_.store((uint32_t)(t - p));
}
uint32_t NetSession::take_max_gap_ms() { return max_gap_ms_.exchange(0); }
void NetSession::poll()
{
    std::lock_guard<std::recursive_mutex> lk(gmu_);
    note_game_call();
    poll_impl();
}
void NetSession::poll_impl()
{
    last_poll_ms_.store(now_ms());
    if (!gs_ || failed_) return;
    gekko_network_poll((GekkoSession*)gs_);
    // GekkoNet resets its session-event list at the start of the next UpdateSession, so a Disconnected / Connected / Syncing raised by a poll outside update would be wiped
    // and never fire again (the actor is already marked): take what is new now.
    int cnt = 0; GekkoSessionEvent** se = gekko_session_events((GekkoSession*)gs_, &cnt);
    for (int i = polled_events_; i < cnt; i++) {
        const GekkoSessionEvent& s = *se[i]; SessionEvent o{};
        switch (s.type) {
        case GekkoPlayerDisconnected: o.kind = SessionEventKind::Disconnected; o.handle = s.data.disconnected.handle; break;
        case GekkoPlayerConnected: o.kind = SessionEventKind::Connected; o.handle = s.data.connected.handle; break;
        case GekkoPlayerSyncing: o.kind = SessionEventKind::Syncing; o.handle = s.data.syncing.handle; o.tick = s.data.syncing.current; break;
        default: continue;   // Started and Desync come with update's own read
        }
        events_.push_back(o);
    }
    polled_events_ = cnt;
}
float NetSession::frames_ahead() const { std::lock_guard<std::recursive_mutex> lk(gmu_); return gs_ ? gekko_frames_ahead((GekkoSession*)gs_) : 0.f; }

bool NetSession::leave(uint8_t n)
{
    std::lock_guard<std::recursive_mutex> lk(gmu_);
    if (!gs_ || n >= plan_.local_count) return false;
    return gekko_disconnect_actor((GekkoSession*)gs_, plan_.local_first + n);
}

bool NetSession::complete_save(uint32_t id, uint32_t checksum, uint32_t token)
{
    std::lock_guard<std::recursive_mutex> lk(gmu_);
    for (size_t i = 0; i < psaves_.size(); i++) {
        if (psaves_[i].id != id) continue;
        Token t{kTokenMagic, (uint32_t)psaves_[i].frame, token, checksum};
        std::memcpy(psaves_[i].state, &t, sizeof t);
        *psaves_[i].checksum = checksum;
        psaves_.erase(psaves_.begin() + i);
        pending_saves_--; saves_++;
        return true;
    }
    return false;
}

bool NetSession::update(std::vector<NetEvent>& out, bool consumer_idle)
{
    std::lock_guard<std::recursive_mutex> lk(gmu_);
    note_game_call(); last_poll_ms_.store(now_ms());
    if (!gs_ || failed_) return false;
    if (!consumer_idle || pending_saves_) { deferred_++; return false; }
    GekkoSession* g = (GekkoSession*)gs_;
    psaves_.clear();
    // Replays: every save of the last update is complete here, so the live recording is whole; keep a copy for recording() calls made mid-tick.
    if (recording_ && cfg_.record_copy_every && ++rec_updates_ >= cfg_.record_copy_every) { rec_updates_ = 0; copy_recording(); }
    for (unsigned n = 0; n < plan_.local_count; n++)
        gekko_add_local_input(g, plan_.local_first + n, pending_.data() + (size_t)n * cfg_.input_size);
    int cnt = 0;
    GekkoGameEvent** ev = gekko_update_session(g, &cnt);
    for (int i = 0; i < cnt && !failed_; i++) {
        const GekkoGameEvent& e = *ev[i];
        NetEvent o;
        switch (e.type) {
        case GekkoAdvanceEvent:
            if (e.data.adv.input_len != (unsigned)plan_.total_players * cfg_.input_size) { fail("advance input length mismatch"); break; }
            o.kind = EvKind::Advance; o.frame = e.data.adv.frame; o.rolling_back = e.data.adv.rolling_back;
            o.inputs.assign(e.data.adv.inputs, e.data.adv.inputs + e.data.adv.input_len);
            advances_++; if (e.data.adv.rolling_back) resim_++;
            out.push_back(std::move(o));
            break;
        case GekkoSaveEvent:
            o.kind = EvKind::Save; o.frame = e.data.save.frame; o.token = next_id_++;
            *e.data.save.state_len = sizeof(Token);
            psaves_.push_back({o.token, o.frame, e.data.save.checksum, e.data.save.state});
            pending_saves_++;
            out.push_back(std::move(o));
            break;
        case GekkoLoadEvent: {
            Token t{};
            if (e.data.load.state_len != sizeof t) { fail("load: state buffer size"); break; }
            std::memcpy(&t, e.data.load.state, sizeof t);
            if (t.magic != kTokenMagic || (int)t.frame != e.data.load.frame) { fail("load: stored state does not name frame " + std::to_string(e.data.load.frame)); break; }
            o.kind = EvKind::Load; o.frame = e.data.load.frame; o.token = t.token; loads_++;
            out.push_back(std::move(o));
            break;
        }
        default: break;
        }
    }
    cnt = 0;
    GekkoSessionEvent** se = gekko_session_events(g, &cnt);
    for (int i = 0; i < cnt; i++) {
        const GekkoSessionEvent& s = *se[i];
        SessionEvent o{};
        switch (s.type) {
        case GekkoPlayerSyncing: o.kind = SessionEventKind::Syncing; o.handle = s.data.syncing.handle; o.tick = s.data.syncing.current; break;
        case GekkoPlayerConnected: o.kind = SessionEventKind::Connected; o.handle = s.data.connected.handle; break;
        case GekkoSessionStarted: o.kind = SessionEventKind::Started; started_ = true; break;
        case GekkoPlayerDisconnected: o.kind = SessionEventKind::Disconnected; o.handle = s.data.disconnected.handle; break;
        case GekkoDesyncDetected:
            o.kind = SessionEventKind::Desync; o.handle = s.data.desynced.remote_handle; o.tick = s.data.desynced.frame + 1;   // stepper tick numbering: the state after advancing frame F is tick F+1
            o.local_hash = s.data.desynced.local_checksum; o.remote_hash = s.data.desynced.remote_checksum;
            if (o.tick < desync_tick_) desync_tick_ = o.tick;
            break;
        case GekkoReplayDesync:   // ENHANCEMENT (replays): this run's checksum of a recorded tick differs from the recording (reported once, the first)
            o.kind = SessionEventKind::Desync; o.tick = s.data.replay_desynced.frame + 1;
            o.local_hash = s.data.replay_desynced.checksum; o.remote_hash = s.data.replay_desynced.recorded_checksum;
            if (o.tick < desync_tick_) desync_tick_ = o.tick;
            break;
        case GekkoReplayFinished: o.kind = SessionEventKind::ReplayFinished; replay_finished_ = true; break;
        default: continue;
        }
        events_.push_back(o);
    }
    polled_events_ = cnt;   // update's own read is complete: a later poll reads only what is added after it
    return true;
}

std::vector<SessionEvent> NetSession::take_events() { std::lock_guard<std::recursive_mutex> lk(gmu_); std::vector<SessionEvent> v; v.swap(events_); return v; }
std::vector<SessionEvent> NetSession::take_events(int* ff)
{
    std::lock_guard<std::recursive_mutex> lk(gmu_);   // the keepalive thread polls (and queues a Disconnected) only under this lock
    std::vector<SessionEvent> v; v.swap(events_);
    if (ff) *ff = final_frame();                        // recursive: the same lock
    return v;
}

SessionStats NetSession::stats() const
{
    std::lock_guard<std::recursive_mutex> lk(gmu_);
    SessionStats s{}; s.keepalive_polls = ka_polls_.load();
    s.advances = advances_; s.resimulated = resim_; s.saves = saves_; s.loads = loads_; s.deferred = deferred_;
    if (gs_) {
        GekkoRollbackStats r{};
        gekko_rollback_stats((GekkoSession*)gs_, &r);
        s.rollbacks = r.total_count; s.max_rollback_frames = r.max_frames; s.stalls = r.total_stalls;
        s.frames_ahead = gekko_frames_ahead((GekkoSession*)gs_);
        s.local_delay = cur_delay_;
        for (unsigned sl = 0; sl < plan_.total_players; sl++) {
            if (sl >= plan_.local_first && sl < (unsigned)plan_.local_first + plan_.local_count) continue;
            GekkoNetworkStats ns{}; gekko_network_stats((GekkoSession*)gs_, (int)sl, &ns);
            if (ns.avg_ping > 0) { s.ping_ms = ns.avg_ping; s.jitter_ms = ns.jitter; s.last_ping_ms = ns.last_ping; }
            s.rtt_seq = ns.rtt_count; s.acked_frame = ns.last_acked_frame;
            break;
        }
    }
    return s;
}

int NetSession::final_frame() const
{
    std::lock_guard<std::recursive_mutex> lk(gmu_);
    if (!gs_ || mode_ != SessionMode::Game) return -1;
    return gekko_final_frame((GekkoSession*)gs_);
}

void NetSession::set_prediction_window(uint8_t w)
{
    std::lock_guard<std::recursive_mutex> lk(gmu_);
    if (gs_ && mode_ == SessionMode::Game && w) gekko_set_input_prediction_window((GekkoSession*)gs_, w);
}

void NetSession::set_local_delay(uint8_t d)
{
    std::lock_guard<std::recursive_mutex> lk(gmu_);
    if (!gs_ || d == cur_delay_) return;
    for (unsigned n = 0; n < plan_.local_count; n++) gekko_set_local_delay((GekkoSession*)gs_, (int)(plan_.local_first + n), d);   // equal on all local players (see start())
    cur_delay_ = d;
}

} // namespace jspnet
