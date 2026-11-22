// net_repsn_client_test.cpp -- host test of the REAL re-psn transport (jsp_net_repsn_client.h over the C client psnr.c, toolkit patch 7918) against a FAKE re-psn server that speaks the
// real wire format of docs/api.md v0.8.0 over loopback TCP + UDP (HELLO / HELLO_ACK, CREATE_ROOM / SEARCH_ROOMS / JOIN_ROOM / LEAVE_ROOM, ROOM_JOINED / ROOM_LIST / MEMBER_JOINED /
// MEMBER_LEFT, UDP PROBE / PROBE_REPLY / RELAY). Two clients (host + client) each with their own UdpMux run the real RePsnRendezvous: room created with a u64 id above 2^32, searched,
// joined, the member endpoints used, PUNCH, then the HELO/PLAN over the mux; relay only when allowed and offered; control and forged relay datagrams never reach the session.
// POSIX sockets: native Linux only. Build and run: installer/tools/net_repsn_test.sh
#include "jsp_net_repsn_client.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <mutex>

using namespace jspnet;
// Embedded NULs exercise byte-counted external and member records.
static const jpsm::Digest content = [] { jpsm::Digest d{}; d.back() = 0xff; return d; }();

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { g_n++; if (!(c)) { g_fail++; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

// ---- the fake server --------------------------------------------------------------------------------------------------------------------------------
struct FakeRepsn {
    struct Conn { int fd = -1; std::vector<uint8_t> in; uint32_t user = 0; std::string name; uint16_t p2p = 0; uint32_t token = 0; uint64_t room = 0; uint16_t member = 0; sockaddr_in probed{}; bool probed_ok = false; };
    struct Room { uint64_t id; uint32_t flags; std::string external; uint8_t max; uint16_t owner; std::vector<Conn*> members; uint16_t next_member = 1; std::map<Conn*, std::pair<uint16_t, std::string>> data; };
    int tcp = -1, udp = -1; uint16_t port = 0;
    std::thread th; std::atomic<bool> stop{false};
    std::mutex mu;
    bool relay_flag = false, blackhole = false, search_error = false, drop_on_search = false;   // drop_on_search: the server cuts the connection when the SEARCH arrives
       // search_error: SEARCH_ROOMS answers ERROR 6
    std::vector<uint8_t> last_create, last_search, last_join, last_leave;   // the request bodies (after the request id) exactly as received, for the golden vectors   // relay_flag: HELLO_ACK bit 0; blackhole: members get an unreachable endpoint for each other (a router that cannot be punched)
    std::vector<Conn*> conns; std::vector<Room> rooms; uint64_t next_room = 0x100000007ull; uint32_t next_user = 1000;
    int probes = 0, relayed = 0, hellos = 0, leaves = 0, searches = 0;
    std::string last_name;

    bool start()
    {
        tcp = ::socket(AF_INET, SOCK_STREAM, 0); udp = ::socket(AF_INET, SOCK_DGRAM, 0);
        int one = 1; setsockopt(tcp, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
        if (::bind(tcp, (sockaddr*)&a, sizeof a) != 0) return false;
        socklen_t l = sizeof a; getsockname(tcp, (sockaddr*)&a, &l); port = ntohs(a.sin_port);
        if (::listen(tcp, 8) != 0) return false;
        sockaddr_in u = a; if (::bind(udp, (sockaddr*)&u, sizeof u) != 0) return false;   // UDP on the same port number as TCP
        th = std::thread([this] { loop(); });
        return true;
    }
    ~FakeRepsn() { stop = true; if (th.joinable()) th.join(); for (Conn* c : conns) { if (c->fd >= 0) ::close(c->fd); delete c; } if (tcp >= 0) ::close(tcp); if (udp >= 0) ::close(udp); }

    static void p16(std::vector<uint8_t>& b, uint16_t v) { b.push_back((uint8_t)(v >> 8)); b.push_back((uint8_t)v); }
    static void p32(std::vector<uint8_t>& b, uint32_t v) { p16(b, (uint16_t)(v >> 16)); p16(b, (uint16_t)v); }
    static void p64(std::vector<uint8_t>& b, uint64_t v) { p32(b, (uint32_t)(v >> 32)); p32(b, (uint32_t)v); }
    static void blob(std::vector<uint8_t>& b, const std::string& s) { p16(b, (uint16_t)s.size()); b.insert(b.end(), s.begin(), s.end()); }
    static void fixed(std::vector<uint8_t>& b, const std::string& s, size_t n) { for (size_t i = 0; i < n; i++) b.push_back(i < s.size() ? (uint8_t)s[i] : 0); }
    static uint16_t g16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
    static uint32_t g32(const uint8_t* p) { return (uint32_t)g16(p) << 16 | g16(p + 2); }
    static uint64_t g64(const uint8_t* p) { return (uint64_t)g32(p) << 32 | g32(p + 4); }

    void send_frame(Conn* c, uint8_t type, const std::vector<uint8_t>& payload)
    {
        std::vector<uint8_t> f; f.push_back(type); p16(f, (uint16_t)payload.size()); f.insert(f.end(), payload.begin(), payload.end());
        size_t off = 0; while (off < f.size()) { const ssize_t n = ::send(c->fd, f.data() + off, f.size() - off, MSG_NOSIGNAL); if (n <= 0) return; off += (size_t)n; }
    }
    void reply(Conn* c, uint8_t type, uint32_t req, const std::vector<uint8_t>& body) { std::vector<uint8_t> p; p32(p, req); p.insert(p.end(), body.begin(), body.end()); send_frame(c, type, p); }
    // the endpoint `viewer` is given for `m` (blackhole: unreachable, as for a symmetric NAT)
    void member_entry(std::vector<uint8_t>& b, Conn* viewer, Room& r, Conn* m)
    {
        p16(b, r.data[m].first); p32(b, m->user); fixed(b, m->name, 16);
        const uint8_t ip[4] = { 127, 0, 0, 1 }; b.insert(b.end(), ip, ip + 4);
        uint16_t port = m->probed_ok ? ntohs(m->probed.sin_port) : m->p2p;
        if (blackhole && viewer != m) port = 9;
        p16(b, port); b.push_back(r.data[m].first == r.owner ? 1 : 0); blob(b, r.data[m].second);
    }
    void room_joined(Conn* c, uint32_t req, Room& r)
    {
        std::vector<uint8_t> b; p64(b, r.id); p16(b, r.data[c].first); p16(b, r.owner); b.push_back(r.max); p32(b, r.flags); blob(b, r.external); blob(b, ""); b.push_back((uint8_t)r.members.size());
        for (Conn* m : r.members) member_entry(b, c, r, m);
        reply(c, 0x90, req, b);
    }
    Room* find_room(uint64_t id) { for (auto& r : rooms) if (r.id == id) return &r; return nullptr; }
    void leave(Conn* c)
    {
        if (!c->room) return;
        Room* r = find_room(c->room); c->room = 0; if (!r) return;
        const uint16_t mid = r->data[c].first;
        r->members.erase(std::remove(r->members.begin(), r->members.end(), c), r->members.end()); r->data.erase(c);
        if (r->owner == mid && !r->members.empty()) r->owner = r->data[r->members[0]].first;
        for (Conn* m : r->members) { std::vector<uint8_t> b; p64(b, r->id); p16(b, mid); p16(b, r->owner); send_frame(m, 0xA2, b); }
        if (r->members.empty()) rooms.erase(std::remove_if(rooms.begin(), rooms.end(), [&](const Room& x) { return x.id == r->id; }), rooms.end());
    }
    void handle(Conn* c, uint8_t type, const uint8_t* p, size_t n)
    {
        if (n < 4) return;
        const uint32_t req = g32(p); p += 4; n -= 4;
        std::lock_guard<std::mutex> lk(mu);
        if (type == 0x01 && n >= 30) {   // HELLO: comm_id[12] online_id[16] p2p_port u16
            c->user = next_user++; c->token = 0xC0DE0000u + c->user; c->p2p = g16(p + 28); hellos++;
            char nm[17] = {}; std::memcpy(nm, p + 12, 16); c->name = nm; last_name = c->name;
            std::vector<uint8_t> b; p32(b, c->user); const uint8_t pub[4] = { 127, 0, 0, 1 }; b.insert(b.end(), pub, pub + 4); p32(b, c->token); b.push_back(relay_flag ? 1 : 0);
            reply(c, 0x81, req, b);
        } else if (type == 0x10 && n >= 1 + 4 + 6) {   // CREATE_ROOM: max u8 flags u32 external blob internal blob member_data blob
            last_create.assign(p, p + n);
            Room r; r.id = next_room++; r.max = p[0]; r.flags = g32(p + 1); size_t o = 5;
            const uint16_t el = g16(p + o); r.external.assign((const char*)p + o + 2, el); o += 2 + el;
            const uint16_t il = g16(p + o); o += 2 + il;
            const uint16_t dl = g16(p + o); const std::string data((const char*)p + o + 2, dl);
            r.owner = r.next_member; r.data[c] = { r.next_member++, data }; r.members.push_back(c); c->room = r.id; c->member = r.owner;
            rooms.push_back(r); room_joined(c, req, rooms.back());
        } else if (type == 0x11) {   // SEARCH_ROOMS: start u16 max u16
            last_search.assign(p, p + n); searches++;
            if (drop_on_search) { ::shutdown(c->fd, SHUT_RDWR); return; }
            if (search_error) { std::vector<uint8_t> e; p32(e, 6); reply(c, 0x8F, req, e); return; }
            std::vector<uint8_t> b; std::vector<Room*> list; for (auto& r : rooms) if (r.members.size() < r.max) list.push_back(&r);
            p16(b, (uint16_t)list.size());
            size_t start = std::min<size_t>(g16(p), list.size());
            size_t end = std::min(list.size(), start + g16(p + 2));
            list = std::vector<Room*>(list.begin() + start, list.begin() + end);
            p16(b, (uint16_t)list.size());
            for (Room* r : list) { p64(b, r->id); Conn* o = nullptr; for (Conn* m : r->members) if (r->data[m].first == r->owner) o = m; fixed(b, o ? o->name : "", 16); b.push_back((uint8_t)r->members.size()); b.push_back(r->max); p32(b, r->flags); blob(b, r->external); }
            reply(c, 0x91, req, b);
        } else if (type == 0x12 && n >= 10) {   // JOIN_ROOM: room_id u64 member_data blob
            last_join.assign(p, p + n);
            Room* r = find_room(g64(p));
            if (!r) { std::vector<uint8_t> b; p32(b, 2); reply(c, 0x8F, req, b); return; }
            if (r->members.size() >= r->max) { std::vector<uint8_t> b; p32(b, 3); reply(c, 0x8F, req, b); return; }
            const uint16_t dl = g16(p + 8); const std::string data((const char*)p + 10, dl);
            r->data[c] = { r->next_member++, data }; r->members.push_back(c); c->room = r->id; c->member = r->data[c].first;
            room_joined(c, req, *r);
            for (Conn* m : r->members) if (m != c) { std::vector<uint8_t> b; p64(b, r->id); member_entry(b, m, *r, c); send_frame(m, 0xA1, b); }
        } else if (type == 0x13) {   // LEAVE_ROOM
            leaves++; last_leave.assign(p, p + n); leave(c); reply(c, 0x93, req, {});
        }
    }
    void loop()
    {
        while (!stop.load()) {
            std::vector<pollfd> fds; fds.push_back({ tcp, POLLIN, 0 }); fds.push_back({ udp, POLLIN, 0 });
            { std::lock_guard<std::mutex> lk(mu); for (Conn* c : conns) fds.push_back({ c->fd, POLLIN, 0 }); }
            if (::poll(fds.data(), fds.size(), 20) <= 0) continue;
            if (fds[0].revents & POLLIN) { const int fd = ::accept(tcp, nullptr, nullptr); if (fd >= 0) { Conn* c = new Conn; c->fd = fd; std::lock_guard<std::mutex> lk(mu); conns.push_back(c); } }
            if (fds[1].revents & POLLIN) udp_in();
            for (size_t i = 2; i < fds.size(); i++) {
                if (!(fds[i].revents & (POLLIN | POLLHUP))) continue;
                Conn* c; { std::lock_guard<std::mutex> lk(mu); c = conns[i - 2]; }
                uint8_t tmp[4096]; const ssize_t n = ::recv(c->fd, tmp, sizeof tmp, 0);
                if (n <= 0) { std::lock_guard<std::mutex> lk(mu); leave(c); ::close(c->fd); c->fd = -1; conns.erase(std::remove(conns.begin(), conns.end(), c), conns.end()); delete c; break; }
                c->in.insert(c->in.end(), tmp, tmp + n);
                while (c->in.size() >= 3) {
                    const size_t len = g16(c->in.data() + 1);
                    if (c->in.size() < 3 + len) break;
                    std::vector<uint8_t> pl(c->in.begin() + 3, c->in.begin() + 3 + len); const uint8_t type = c->in[0];
                    c->in.erase(c->in.begin(), c->in.begin() + 3 + len);
                    handle(c, type, pl.data(), pl.size());
                }
            }
        }
    }
    void udp_in()
    {
        uint8_t b[2048]; sockaddr_in from{}; socklen_t fl = sizeof from;
        const ssize_t n = ::recvfrom(udp, b, sizeof b, 0, (sockaddr*)&from, &fl);
        if (n < 5 || std::memcmp(b, "PSNR", 4) != 0) return;
        std::lock_guard<std::mutex> lk(mu);
        if (b[4] == 0x01 && n >= 17) {   // PROBE: user u32 token u32 local_ip[4]
            Conn* c = nullptr; for (Conn* x : conns) if (x->user == g32(b + 5) && x->token == g32(b + 9)) c = x;
            if (!c) return;
            c->probed = from; c->probed_ok = true; probes++;
            uint8_t r[11] = { 'P', 'S', 'N', 'R', 0x81 }; std::memcpy(r + 5, &from.sin_addr.s_addr, 4); r[9] = (uint8_t)(ntohs(from.sin_port) >> 8); r[10] = (uint8_t)ntohs(from.sin_port);
            ::sendto(udp, r, sizeof r, 0, (sockaddr*)&from, fl);
        } else if (b[4] == 0x03 && n >= 17) {   // RELAY: from u32 token u32 to u32 + datagram -> to the target's probed endpoint as "PSNR" 03 from u32 + datagram
            Conn* src = nullptr; for (Conn* x : conns) if (x->user == g32(b + 5) && x->token == g32(b + 9)) src = x;
            Conn* dst = nullptr; for (Conn* x : conns) if (x->user == g32(b + 13)) dst = x;
            if (!src || !dst || !dst->probed_ok) return;
            std::vector<uint8_t> o = { 'P', 'S', 'N', 'R', 0x03 }; p32(o, src->user); o.insert(o.end(), b + 17, b + n);
            ::sendto(udp, o.data(), o.size(), 0, (sockaddr*)&dst->probed, sizeof dst->probed); relayed++;
        }
    }
};

// ---- two players ---------------------------------------------------------------------------------------------------------------------------------
struct Player {
    UdpMux mux; std::unique_ptr<MessageHub> hub; std::unique_ptr<PsnrClientTransport> tr; std::unique_ptr<RePsnRendezvous> rz; Path last = Path::None; int result = 0;
    void init(FakeRepsn& sv, const std::string& name, bool host, uint32_t build, bool allow_relay, int punch_ms = 4000, int give_up_ms = 20000, int lookup_every_ms = 300, const std::string& code = {})
    {
        std::string err; CHECK(mux.open(0, &err));
        RepsnIdentity id; id.host = "127.0.0.1"; id.port = sv.port; id.name = name; id.comm_id = "NPWR08138_00";
        hub.reset(new MessageHub(mux)); tr.reset(new PsnrClientTransport(mux, id));
        RePsnRendezvous::Config c; c.host = host; c.build = build; c.content = content; c.code = code; c.allow_relay = allow_relay; c.punch_ms = punch_ms; c.give_up_ms = give_up_ms; c.lookup_every_ms = lookup_every_ms;
        rz.reset(new RePsnRendezvous(*tr, mux, *hub, c));
        rz->on_path = [this](Path p, uint32_t u) { last = p; tr->set_peer_user(u); tr->set_relay_mode(p == Path::Relay); };
        tr->start(allow_relay);
    }
    void step() { mux.pump(); hub->pump(); if (tr) tr->pump(); if (result == 0) result = rz->poll(); }   // after the plan the channels keep acknowledging (the glue keeps pumping hub and transport)
};
static void drive(Player& h, Player& c, int ms, bool stagger = true)
{
    const uint64_t t0 = now_ms();
    while (now_ms() - t0 < (uint64_t)ms && (h.result == 0 || c.result == 0)) {
        h.step();
        if (!stagger || h.tr->self_user() != 0) c.step();   // the client searches only after the host connected (it needs the room to exist: give the host a head start)
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
}

int main()
{
    const uint32_t B = 0x1234ABCDu;
    // ---- direct: create / search / join over the real wire format, u64 room id, endpoints from the member entries, PUNCH, then HELO/PLAN ----
    { FakeRepsn sv; CHECK(sv.start()); Player h, c; h.init(sv, "HostPlayer", true, B, false); c.init(sv, "GuestPlayer", false, B, false);
      // the host first: wait until its room exists, then let the client search
      { const uint64_t t0 = now_ms(); while (now_ms() - t0 < 5000) { h.step(); { std::lock_guard<std::mutex> lk(sv.mu); if (!sv.rooms.empty()) break; } std::this_thread::sleep_for(std::chrono::milliseconds(3)); } }
      CHECK(sv.rooms.size() == 1 && sv.rooms[0].id == 0x100000007ull && (sv.rooms[0].flags & kRoomFlagRollback) && sv.rooms[0].external == make_record(B, content) && sv.rooms[0].max == 2);
      drive(h, c, 10000);
      if (h.result != 1 || c.result != 1) std::printf("direct: host %d '%s' client %d '%s' rooms %zu hellos %d probes %d\n", h.result, h.rz->error().c_str(), c.result, c.rz->error().c_str(), sv.rooms.size(), sv.hellos, sv.probes);
      CHECK(h.result == 1 && c.result == 1);
      CHECK(h.rz->path() == Path::Direct && c.rz->path() == Path::Direct && h.last == Path::Direct);
      CHECK(h.rz->room_id() == 0x100000007ull && c.rz->room_id() == 0x100000007ull && h.rz->plan().session_id == 0x100000007ull && c.rz->plan().session_id == 0x100000007ull);   // the u64 survives
      CHECK(h.rz->local_index() == 0 && c.rz->local_index() == 1 && h.rz->plan().host && !c.rz->plan().host);
      CHECK(sv.hellos == 2 && sv.last_name.size() > 0 && sv.probes >= 2 && h.tr->probed() && c.tr->probed());   // both HELLOs carried the mux port, both muxes probed from their own socket
      CHECK(h.mux.received > 0 && c.mux.received > 0 && h.tr->control_dropped > 0 && c.tr->control_dropped > 0);    // PROBE_REPLY / PUNCH stayed away from the tag queues
      CHECK(h.mux.unknown_tag == 0 && c.mux.unknown_tag == 0);
      CHECK(h.tr->relay_tx == 0 && c.tr->relay_tx == 0 && sv.relayed == 0);
      CHECK(c.rz->plan().remotes.size() >= 1 && c.rz->plan().remotes[0].ep.port == h.mux.local_port() && h.rz->plan().remotes[0].ep.port == c.mux.local_port()); }

    // ---- golden request bodies: written out by hand from the field tables of the re-psn protocol document (docs/api.md, v0.8.0, "Rooms"; it has tables, no byte dumps),
    //      NOT produced by the transport's own helpers ----
    //   record = JRB2, build and 32 content bytes (40 bytes = 0x0028), room flag bit 30 = 0x40000000, room id 0x0000000100000007
    { auto hex = [](const char* h) { std::vector<uint8_t> v; for (const char* q = h; q[0] && q[1]; q += 2) { auto d = [](char ch) { return ch <= '9' ? ch - '0' : ch - 'a' + 10; }; v.push_back((uint8_t)(d(q[0]) << 4 | d(q[1]))); } return v; };
      for (bool coded : { false, true }) {
      const std::string tag = "ce973beee4b76122b831913719bb34a0953dc4f256133feac337e369141ad15a";
      std::string R = std::string(coded ? "0068" : "0028") + "4a5242321234abcd" + std::string(62, '0') + "ff";
      if (coded) for (char ch : tag) { char b[3]; std::snprintf(b, sizeof b, "%02x", (unsigned char)ch); R += b; }
      FakeRepsn sv;
      const size_t decoys = coded ? 64 : 0;
      for (size_t i = 0; i < decoys; ++i) {
          FakeRepsn::Room r{}; r.id = i + 1; r.max = 2; r.flags = kRoomFlagRollback;
          auto other = content; other.back() ^= 1;
          r.external = make_record(B, other, tag); sv.rooms.push_back(r);
      }
      CHECK(sv.start()); Player h, c;
      h.init(sv, "HostPlayer", true, B, false, 4000, 20000, 300, coded ? "T60CDATX" : "");
      c.init(sv, "GuestPlayer", false, B, false, 4000, 20000, 300, coded ? "T60CDATX" : "");
      { const uint64_t t0 = now_ms(); while (now_ms() - t0 < 5000) { h.step(); { std::lock_guard<std::mutex> lk(sv.mu); if (sv.rooms.size() > decoys) break; } std::this_thread::sleep_for(std::chrono::milliseconds(3)); } }
      drive(h, c, 10000);
      c.tr->leave_room(); { const uint64_t t0 = now_ms(); while (now_ms() - t0 < 1000) { c.step(); { std::lock_guard<std::mutex> lk(sv.mu); if (!sv.last_leave.empty()) break; } std::this_thread::sleep_for(std::chrono::milliseconds(3)); } }
      std::lock_guard<std::mutex> lk(sv.mu);
      CHECK(sv.last_create == hex((std::string("02" "40000000") + R + "0000" + R).c_str()));      // max u8 | flags u32 | external blob | internal blob (empty) | member_data blob
      CHECK(sv.last_search == hex(coded ? "00400040" : "00000040"));                                                  // start u16 = 0 | max u16 = 64
      CHECK(sv.last_join == hex((std::string("0000000100000007") + R).c_str()));                 // room_id u64 | member_data blob
      CHECK(sv.last_leave == hex("0000000100000007")); } }                                         // room_id u64
    // ---- a refused SEARCH ends the lookup at once (not a re-send until the give-up); a name over 16 characters is refused before any connection ----
    { FakeRepsn sv; sv.search_error = true; CHECK(sv.start()); Player c; c.init(sv, "GuestPlayer", false, B, false);
      const uint64_t t0 = now_ms(); while (now_ms() - t0 < 6000 && c.result == 0) { c.step(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
      CHECK(c.result == -1 && c.rz->error().find("room search was refused (error 6)") != std::string::npos && now_ms() - t0 < 6000); }
    // ---- a dead connection during the lookup (the server cuts it): the rendezvous ends at once with a reason; it reads no events in that state, so failure() must report it ----
    { FakeRepsn sv; sv.drop_on_search = true; CHECK(sv.start()); Player c; c.init(sv, "GuestPlayer", false, B, false);
      const uint64_t t0 = now_ms(); while (now_ms() - t0 < 6000 && c.result == 0) { c.step(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
      CHECK(c.result == -1 && c.rz->error().find("connection to the server was lost") != std::string::npos && now_ms() - t0 < 6000); }
    { FakeRepsn sv; CHECK(sv.start()); Player c; c.init(sv, "ThisNameIsFarTooLong", false, B, false); c.step();
      CHECK(c.result == -1 && c.rz->error().find("online name must be 1 to 16") != std::string::npos && sv.hellos == 0); }

    // ---- a symmetric-NAT-like endpoint: unreachable directly. Relay NOT allowed: refused with the reason, never relayed ----
    { FakeRepsn sv; sv.blackhole = true; sv.relay_flag = true; CHECK(sv.start()); Player h, c; h.init(sv, "HostPlayer", true, B, false, 1500); c.init(sv, "GuestPlayer", false, B, false, 1500);
      { const uint64_t t0 = now_ms(); while (now_ms() - t0 < 5000) { h.step(); { std::lock_guard<std::mutex> lk(sv.mu); if (!sv.rooms.empty()) break; } std::this_thread::sleep_for(std::chrono::milliseconds(3)); } }
      drive(h, c, 8000);
      CHECK(h.result == -1 && c.result == -1 && (h.rz->error() + c.rz->error()).find("relay is not allowed") != std::string::npos);   // the side that gives up first leaves the room: the other may report that instead
      CHECK(sv.relayed == 0 && h.tr->relay_tx == 0 && c.tr->relay_tx == 0); }
    // ---- allowed but the server offers no relay (HELLO_ACK bit 0 clear) ----
    { FakeRepsn sv; sv.blackhole = true; sv.relay_flag = false; CHECK(sv.start()); Player h, c; h.init(sv, "HostPlayer", true, B, true, 1500); c.init(sv, "GuestPlayer", false, B, true, 1500);
      { const uint64_t t0 = now_ms(); while (now_ms() - t0 < 5000) { h.step(); { std::lock_guard<std::mutex> lk(sv.mu); if (!sv.rooms.empty()) break; } std::this_thread::sleep_for(std::chrono::milliseconds(3)); } }
      drive(h, c, 8000);
      CHECK(h.result == -1 && (h.rz->error() + c.rz->error()).find("offers no relay") != std::string::npos && sv.relayed == 0); }
    // ---- allowed and offered: the rendezvous completes through the server, the path says relay, datagrams are wrapped / unwrapped with the source check ----
    { FakeRepsn sv; sv.blackhole = true; sv.relay_flag = true; CHECK(sv.start()); Player h, c; h.init(sv, "HostPlayer", true, B, true, 1500); c.init(sv, "GuestPlayer", false, B, true, 1500);
      { const uint64_t t0 = now_ms(); while (now_ms() - t0 < 5000) { h.step(); { std::lock_guard<std::mutex> lk(sv.mu); if (!sv.rooms.empty()) break; } std::this_thread::sleep_for(std::chrono::milliseconds(3)); } }
      drive(h, c, 15000);
      CHECK(h.result == 1 && c.result == 1 && h.rz->path() == Path::Relay && c.rz->path() == Path::Relay && h.tr->relay_mode() && c.tr->relay_mode());
      CHECK(sv.relayed > 0 && h.tr->relay_tx > 0 && c.tr->relay_tx > 0 && h.tr->relay_rx > 0 && c.tr->relay_rx > 0);
      // a datagram through the relay arrives on the session's tag queue as if it came from the peer's endpoint
      { const Endpoint peer_of_c = h.rz->plan().remotes[0].ep; const uint8_t pl[3] = { 1, 2, 3 }; h.mux.send(peer_of_c, kTagGekko, pl, 3);
        std::vector<uint8_t> d; Endpoint from; bool got = false; const uint64_t t0 = now_ms();
        while (now_ms() - t0 < 2000 && !got) { h.mux.pump(); c.mux.pump(); got = c.mux.pop(kTagGekko, from, d); std::this_thread::sleep_for(std::chrono::milliseconds(3)); }
        CHECK(got && d.size() == 3 && d[0] == 1 && d[2] == 3 && from == c.rz->plan().remotes[0].ep); }
      // a forged relay datagram (right header, source is NOT the server) is dropped before the session sees it, and counted
      { int s = ::socket(AF_INET, SOCK_DGRAM, 0); sockaddr_in to{}; to.sin_family = AF_INET; to.sin_addr.s_addr = htonl(INADDR_LOOPBACK); to.sin_port = htons(c.mux.local_port());
        std::vector<uint8_t> f = { 'P', 'S', 'N', 'R', 0x03, 0, 0, 0, 1, 'x', 'y' }; const uint64_t before = c.tr->relay_rx_dropped;
        ::sendto(s, f.data(), f.size(), 0, (sockaddr*)&to, sizeof to); ::close(s);
        std::vector<uint8_t> d; Endpoint from; bool got = false; const uint64_t t0 = now_ms();
        while (now_ms() - t0 < 500) { c.mux.pump(); if (c.mux.pop(kTagGekko, from, d)) got = true; std::this_thread::sleep_for(std::chrono::milliseconds(3)); }
        CHECK(!got && c.tr->control_dropped > 0 && c.tr->relay_rx_dropped == before); }   // dropped as control (not from the server: unwrap_from refuses it, no payload escapes)
    }

    // ---- build mismatch: the client never joins (room list shows the record); it keeps reading the list at its interval and gives up at its window, naming the build it saw ----
    { FakeRepsn sv; CHECK(sv.start()); Player h, c; h.init(sv, "HostPlayer", true, B + 1, false); c.init(sv, "GuestPlayer", false, B, false, 4000, 3000, 300);
      { const uint64_t t0 = now_ms(); while (now_ms() - t0 < 5000) { h.step(); { std::lock_guard<std::mutex> lk(sv.mu); if (!sv.rooms.empty()) break; } std::this_thread::sleep_for(std::chrono::milliseconds(3)); } }
      const uint64_t t0 = now_ms();
      while (now_ms() - t0 < 8000 && c.result == 0) { h.step(); c.step(); std::this_thread::sleep_for(std::chrono::milliseconds(3)); }   // until the client decides
      const uint64_t took = now_ms() - t0; int n; { std::lock_guard<std::mutex> lk(sv.mu); n = sv.searches; }
      if (c.result != -1) std::printf("mismatch: client %d '%s'\n", c.result, c.rz->error().c_str());
      if (took < 3000 || took >= 5000) std::printf("mismatch took %llu ms\n", (unsigned long long)took);
      CHECK(c.result == -1 && took >= 3000 && took < 5000);
      CHECK(c.rz->error() == "re-psn (rollback): no host with this version found within 3 seconds (seen: a rollback room with build 1234abce) (is the other player set to Host?): start both games at about the same time "
                             "(the host first is best; each waits 3 seconds after its own start-up); both need the same version (build 1234abcd) and the same enabled mods in the same order");
      CHECK(n >= 3 && n <= 3000 / 300 + 1 && (uint64_t)n == c.rz->lookups); }
    // ---- the joiner starts first: it reads the room list over the real wire for 1.5 s with no room, then the host starts and they meet ----
    { FakeRepsn sv; CHECK(sv.start()); Player h, c; c.init(sv, "GuestPlayer", false, B, false);
      { const uint64_t t0 = now_ms(); while (now_ms() - t0 < 1500 && c.result == 0) { c.step(); std::this_thread::sleep_for(std::chrono::milliseconds(3)); } }
      int n; { std::lock_guard<std::mutex> lk(sv.mu); n = sv.searches; }
      CHECK(c.result == 0 && n >= 2 && n <= 1500 / 300 + 1);
      h.init(sv, "HostPlayer", true, B, false);
      drive(h, c, 10000, false);
      if (h.result != 1 || c.result != 1) std::printf("joiner first: host %d '%s' client %d '%s'\n", h.result, h.rz->error().c_str(), c.result, c.rz->error().c_str());
      CHECK(h.result == 1 && c.result == 1 && h.rz->path() == Path::Direct && c.rz->room_id() == h.rz->room_id()); }
    // ---- the server is not there: the failure text is shown at once, not after the 20 s give-up ----
    { FakeRepsn sv; CHECK(sv.start()); const uint16_t dead_port = sv.port; sv.stop = true; if (sv.th.joinable()) sv.th.join(); ::close(sv.tcp); sv.tcp = -1;
      Player c; c.init(sv, "GuestPlayer", false, B, false); (void)dead_port;
      const uint64_t t0 = now_ms(); while (now_ms() - t0 < 8000 && c.result == 0) { c.step(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
      CHECK(c.result == -1 && c.rz->error().find("cannot connect") != std::string::npos && now_ms() - t0 < 8000); }

    std::printf("%d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
