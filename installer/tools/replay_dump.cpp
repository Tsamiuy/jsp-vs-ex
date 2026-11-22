// replay_dump.cpp -- ENHANCEMENT (replays): print the inputs of a rollback replay (.gkrp, GekkoNet's replay format as NetSession records it) as text, one line per
// tick, so they can be read and analysed (installer/tools/replay_inputs.py). Unpacked by GekkoNet's own replay session (the pinned copy in installer/port/jsp/gekkonet),
// so the bytes are read exactly as a playback reads them. No game needed.
//
//   replay_dump FILE.gkrp [--out FILE.csv]
// Output: comment lines "# key value" (format, players, input size, user data as hex and, for the game's own "JSPR" user data, its fields), then a header and one
// CSV line per tick: tick,p0_buttons,p0_lx,p0_ly,p0_rx,p0_ry,p0_connected,p1_buttons,... in the CANONICAL pad form (jsi::Pad: buttons = DIGITAL2 << 8 | DIGITAL1,
// sticks 0..255 with 128 = centre); the wire form's stick XOR (pad_wire_to_pack) is undone here. Tick 1 is the first recorded tick (GekkoNet frame 0).
// Exit: 0 ok, 2 unreadable file, 3 not a replay this build reads.
#include "jsp_net_session.h"
#include "gekkonet.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace jspnet;

static uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: replay_dump FILE.gkrp [--out FILE.csv]\n"); return 2; }
    const char* outp = nullptr;
    for (int i = 2; i + 1 < argc; i++) if (!std::strcmp(argv[i], "--out")) outp = argv[i + 1];
    std::vector<uint8_t> b;
    { FILE* f = std::fopen(argv[1], "rb"); if (!f) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
      uint8_t buf[65536]; size_t n; while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n); std::fclose(f); }
    // The same head NetSession::start_replay checks (zpp::bits, little endian): magic, version, compressed, then GekkoConfig member by member.
    if (b.size() < 24 || le32(b.data()) != 0x474B5250u || le32(b.data() + 4) != 2) { std::fprintf(stderr, "not a GKRP version 2 replay\n"); return 3; }
    const unsigned players = b[9], input_size = le32(b.data() + 16);
    if (!players || players > 8 || input_size != 8) { std::fprintf(stderr, "unexpected replay config: %u players, input size %u\n", players, input_size); return 3; }

    GekkoSession* s = nullptr;
    if (!gekko_create(&s, GekkoReplaySession) || !gekko_load_replay(s, b.data(), (unsigned)b.size())) { std::fprintf(stderr, "GekkoNet could not read the replay\n"); return 3; }
    FILE* o = outp ? std::fopen(outp, "w") : stdout;
    if (!o) { std::fprintf(stderr, "cannot write %s\n", outp); return 2; }

    std::fprintf(o, "# format GKRP 2\n# players %u\n# input_size %u\n# compressed %u\n# file_bytes %zu\n", players, input_size, (unsigned)b[8], b.size());
    unsigned ul = 0; const unsigned char* ud = gekko_replay_user_data(s, &ul);
    if (ud && ul) {
        std::fprintf(o, "# user_data_hex ");
        for (unsigned i = 0; i < ul && i < 256; i++) std::fprintf(o, "%02x", ud[i]);
        std::fprintf(o, "\n");
        if (ul >= 22 && !std::memcmp(ud, "JSPR", 4) && ud[4] == 1) {   // jsp_net_game.cpp replay_user_data: "JSPR", 1, build, base tick, players, local_first, host, n, values
            std::fprintf(o, "# jspr_build %08x\n# jspr_base_tick %u\n# jspr_players %u\n# jspr_local_first %u\n# jspr_recorded_by %s\n", le32(ud + 5), le32(ud + 9), ud[13], ud[14], ud[15] == 2 ? "local" : ud[15] ? "host" : "join");
            const unsigned nv = le32(ud + 16);
            if (nv < 256 && 20u + 4u * nv <= ul) { std::fprintf(o, "# jspr_settings"); for (unsigned i = 0; i < nv; i++) std::fprintf(o, " %u", le32(ud + 20 + 4 * i)); std::fprintf(o, "\n"); }
        } else {
            std::string t; for (unsigned i = 0; i < ul; i++) t += (ud[i] >= 32 && ud[i] < 127) ? (char)ud[i] : '.';
            std::fprintf(o, "# user_data_text %s\n", t.c_str());
        }
    }
    std::fprintf(o, "tick");
    for (unsigned p = 0; p < players; p++) std::fprintf(o, ",p%u_buttons,p%u_lx,p%u_ly,p%u_rx,p%u_ry,p%u_connected", p, p, p, p, p, p);
    std::fprintf(o, "\n");

    unsigned ticks = 0; bool finished = false;
    for (int guard = 0; guard < 10000000 && !finished; guard++) {
        int n = 0;
        GekkoGameEvent** ev = gekko_update_session(s, &n);
        for (int i = 0; i < n; i++) {
            const GekkoGameEvent& e = *ev[i];
            if (e.type == GekkoSaveEvent) { if (e.data.save.state_len) *e.data.save.state_len = 0; if (e.data.save.checksum) *e.data.save.checksum = 0; continue; }
            if (e.type != GekkoAdvanceEvent || e.data.adv.input_len != players * input_size) continue;
            std::fprintf(o, "%d", e.data.adv.frame + 1);
            for (unsigned p = 0; p < players; p++) {
                uint8_t c[8];
                pad_wire_to_pack(e.data.adv.inputs + p * 8, c);
                std::fprintf(o, ",%u,%u,%u,%u,%u,%u", (unsigned)(c[0] | c[1] << 8), c[2], c[3], c[4], c[5], c[6]);
            }
            std::fprintf(o, "\n");
            ticks++;
        }
        int sn = 0; GekkoSessionEvent** se = gekko_session_events(s, &sn);
        for (int i = 0; i < sn; i++) if (se[i]->type == GekkoReplayFinished) finished = true;
    }
    if (o != stdout) std::fclose(o);
    gekko_destroy(&s);
    std::fprintf(stderr, "replay_dump: %u ticks, %u players%s\n", ticks, players, finished ? "" : " (no ReplayFinished event: stopped at the guard)");
    return 0;
}
