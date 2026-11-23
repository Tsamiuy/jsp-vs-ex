// jsp_fight_hash.cpp -- the per-tick fight state hash and the tick counter (replay determinism test). The experimental rollback netcode
// (JSP_ROLLBACK=1, jsp_net_game.cpp) turns JSP_FIGHT_HASH on for its tick count and per-tick checksum; otherwise it is a diagnostic.
// Two modes, both off by default (no hook installed, no overhead):
//   JSP_FIGHT_SEED=<hex>  the cellRtcGetCurrentTick HLE returns (0x00E31FF5<<32 | seed) for the caller lr 0x005A09AC, so
//                         setup+0x1C (the game's RNG seed, read from the clock once per scene start) is the seed.
//   JSP_FIGHT_HASH=N      per flip n = flip - F0 in [START, START+N) (N <= 1 or non-number: 3000), once the first camera step
//                         was seen, one line "[hash] n= f= all= rng= glob= seed= setup= cam= fi= pad= rngc=" on stderr.
//                         (n is the simulation tick k, hashed at the end of the pass, see the SIMULATION TICK block below; the BeginFrame
//                         hook only logs ticks per flip.)
//   JSP_FIGHT_HASH_START=S  first hashed n (default 3880 ticks): the first 4000+ flips after F0 are the static pre-fight
//                         period (a2/b2: rng, glob, pad, fi, cam each one value for 1800 flips from F0), the fight timer
//                         runs by flip ~30500 (a2 shot) and the Square mash starts near 29000: k 3880..6880 = flip ~28600..31600.
// F0 = flip of the first call of the fighter message handler @0x00084FF0 (the fallback of the run A probe; the ctor
// @0x00085420 is never reached through the table). FNV-1a 64 over guest bytes only (vm_read8), never host floats or pointers.
// Field set and order as in section 9.5: rng, glob, seed, setup, cam[i], fi[j], pad[k]; `all` = FNV over their concatenation.
// rngc (per-flip call counts of the table-reachable RNG entries of 9.4) is logged, not hashed.
// Full-object dumps (cam 0x3C0, fighter 0x950 raw and pointer-masked, RNG 0x28, setup header 0x60..0x300) go to the file
// JSP_FIGHT_DUMP (only when it is set) at the first hashed flip and 300 hashed flips later.
// SIMULATION TICK (the flip is not the game's clock: two hash runs compared): lines are keyed by k, the
//   number of completed scene passes since the anchor, and pad presses come from JSP_PAD_TICKS (tick-keyed, see below).
//   tick source: the scene update @0x0014EA68(this) (table-reachable; its only job here: it calls the camera list loop
//   @0x003E2B7C once per pass, which calls the camera step @0x0006FF10 once per camera, lr 0x003E2C18). A pass counts when at
//   least one camera step ran inside it (so 4 cameras = 1 tick) and it is the scene the first camera step came from.
//   k = passes completed since the anchor: JSP_TICK_ANCHOR=cam (default) the first counted pass is k=1; =move the first counted
//   pass whose camera state (bytes +0x10..+0xB0 of every known camera) differs from the previous pass is k=1.
//   [hash] n= is now k, hashed at the END of pass k (state after the step), once per pass, not per flip. The line
//   "[hash] key=tick anchor=..." says the lines are keyed by tick. Window: k in [JSP_FIGHT_HASH_START, +N) (default 3880, 3000).
//   JSP_PAD_TICKS="T:mask[:hold],..." : the press is seen by scene pass T (T >= 2) and held for `hold` passes (default 1), T counts
//   from the anchor; installed on the pad filter of patch 6501 (g_cellpad_filter, port 0, OR-ed into the buttons after every
//   other source; chained behind any filter installed before). Reads made while k passes are done belong to pass k+1 (same
//   whether the pad read is before or after the scene update in the frame), so the mask is live while done in [T-1, T-1+hold).
//   Logs: "[tick] pad ev=i t=T mask=.. first read at done=.. f=.." (two runs compare only at equal k), "[tick] f=.."
//   every 600 flips after F0 (ticks per flip over time), "[tick] k=.." every 300 passes (flips and pad reads per 300 ticks).
//   ASSUMPTIONS, unproven until the first run: (1) the scene update does not run its camera loop during loading or pause;
//   (2) one pass per main-loop iteration while a fight runs (hash-c1: 3000 BeginFrame lines for 3000 flips, camera state changed on 2889 of 2999 transitions;
//   the rest can be an idle camera). The tick lines are there to check both.
// NOTE: if the camera first steps at F0+300 (as in fightprobe-a2) the first hashed n is 300, so "n=0 / n=300" become
// "first hashed n / +300".

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <utility>
#include <string>
#include "jsp_fight_setup_rule.h"   // FIX (review of PR 248): the battle setup table in the checksum (native online: out)

namespace jft {   // tick-keyed pad events (pure)

struct Ev { uint32_t t = 0, hold = 1; uint16_t mask = 0; bool seen = false; };

// "T:mask[:hold],..." (T decimal >= 2, mask 0x.. or decimal 1..0xFFFF, hold decimal >= 1, default 1); whitespace ignored.
// Returns the event count, or -1 with *bad = the 1-based index of the offending item.
inline int parse(const char* s, std::vector<Ev>* out, int* bad)
{
    out->clear();
    if (bad) *bad = 0;
    std::string cur;
    int idx = 0;
    auto item = [&](const std::string& it) -> bool {
        idx++;
        if (it.empty()) return true;
        char* e = nullptr;
        const char* p = it.c_str();
        unsigned long t = std::strtoul(p, &e, 10);
        if (e == p || *e != ':' || t < 2) return false;
        p = e + 1;
        unsigned long m = std::strtoul(p, &e, 0);
        if (e == p || m == 0 || m > 0xFFFF) return false;
        unsigned long h = 1;
        if (*e == ':') { p = e + 1; h = std::strtoul(p, &e, 10); if (e == p || h == 0) return false; }
        if (*e) return false;
        Ev v; v.t = (uint32_t)t; v.mask = (uint16_t)m; v.hold = (uint32_t)h;
        out->push_back(v);
        return true;
    };
    for (const char* c = s ? s : ""; ; c++) {
        if (*c == ',' || !*c) {
            if (!item(cur)) { if (bad) *bad = idx; out->clear(); return -1; }
            cur.clear();
            if (!*c) break;
        } else if (*c != ' ' && *c != '\t' && *c != '\n' && *c != '\r') cur += *c;
    }
    return (int)out->size();
}

// Buttons of the pass that follows `done` completed passes: pass p = done+1 sees event i when t <= p < t+hold, so the mask is
// live for done in [t-1, t-1+hold). Events newly live are appended to *fresh (their index), once each.
inline uint16_t mask_at(std::vector<Ev>& v, uint32_t done, std::vector<int>* fresh)
{
    uint16_t m = 0;
    const uint32_t p = done + 1;
    for (size_t i = 0; i < v.size(); i++)
        if (p >= v[i].t && p - v[i].t < v[i].hold) {
            m |= v[i].mask;
            if (!v[i].seen) { v[i].seen = true; if (fresh) fresh->push_back((int)i); }
        }
    return m;
}

// The pad filter body: port 0 gets the scripted mask OR-ed in FIRST (only once anchored), then the filter installed before
// (the port menu) runs on the result, so an open page swallows scripted presses like real ones and stays outermost.
typedef void (*PrevFilter)(uint32_t, uint16_t*, uint16_t*, uint16_t*);
inline void filter(std::vector<Ev>& ev, uint32_t done, bool anchored, uint32_t port, uint16_t* b, uint16_t* lx, uint16_t* ly,
                   PrevFilter prev, std::vector<int>* fresh)
{
    if (port == 0 && anchored) *b = (uint16_t)(*b | mask_at(ev, done, fresh));
    if (prev) prev(port, b, lx, ly);
}

// presses keyed to the ANCHOR = the first id 4 message (the 4/5/6 broadcast, handler @0x00084FF0) after F0, not to F0 or the camera
// (the F0-to-fight gap varies by ~5 ticks between runs, the anchor sits at a fixed place in the fight).
// Tick 0 is the scene pass that contains the message; T counts scene passes (every @0x0014EA68 call) after it: a press with T is seen by
// the pass T passes after the message's pass (T >= 2), so the old F0-relative @3380 (trace-2: due at k=3381, anchor message at k=2897 logged,
// i.e. pass 2898) is T=483. Same pass numbering as mask_at (done = passes completed after the message's pass).
struct Anchor {
    std::vector<Ev> ev;
    bool set = false, skip = false;
    uint32_t done = 0;
    bool on_msg(uint32_t id) { if (set || id != 4 || ev.empty()) return false; set = true; skip = true; return true; }
    void pass_end() { if (!set) return; if (skip) { skip = false; return; } done++; }   // the message's own pass is tick 0
    uint16_t mask(std::vector<int>* fresh) { return set ? mask_at(ev, done, fresh) : 0; }
};

}  // namespace jft

#ifndef JSP_FIGHT_TICK_TEST
#include <atomic>
#include <mutex>
#include "ps3emu/ps3types.h"
#include "ppu_recomp.h"
#include "jsp_hook_chain.h"

extern "C" uint32_t cellGcm_flip_request_count(void);
extern "C" void (*g_cellpad_filter)(uint32_t port_no, uint16_t* buttons, uint16_t* lx, uint16_t* ly);   // patch 6501
extern "C" void ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));
extern "C" int32_t cellRtcGetCurrentTick(void* pTick);
extern "C" void ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*));

// rollback prototype (jsp_snapshot.cpp, JSP_SNAPSHOT=1)
struct JspTickState { uint32_t done; uint32_t anc_done; uint8_t anc_set, anc_skip; };
extern "C" int jsp_snapshot_init(uint32_t hash_start, uint32_t hash_n);
extern "C" void jsp_snapshot_tap(uint32_t n, const uint64_t* v);
extern "C" void jsp_snapshot_pass_end(uint32_t k, ppu_context* c);

extern "C" int jsp_anchor_presses_init(void);
extern "C" void jsp_anchor_msg(uint32_t id, uint32_t k, uint32_t flip);
extern "C" void jsp_anchor_pass_end(void);

namespace {

typedef void (*fn_t)(ppu_context*);

enum Kind { K_BEGIN, K_CAM, K_MSG, K_FACT, K_PAD, K_RNG, K_SCENE };
struct Ent { uint32_t addr; Kind kind; };
constexpr Ent k_ents[] = {
    {0x00642C3C, K_BEGIN}, {0x0006FF10, K_CAM}, {0x00084FF0, K_MSG}, {0x00085728, K_FACT}, {0x0009D87C, K_PAD},
    {0x00024914, K_RNG}, {0x00024980, K_RNG}, {0x00024A24, K_RNG}, {0x00024A9C, K_RNG}, {0x00024B00, K_RNG}, {0x00024B98, K_RNG},
    {0x000119A4, K_RNG}, {0x00012E00, K_RNG}, {0x000130D0, K_RNG}, {0x00016138, K_RNG}, {0x0001F758, K_RNG},
    {0x00021A78, K_RNG}, {0x000238BC, K_RNG},
    {0x0014EA68, K_SCENE},   // scene update, the tick source (after the RNG entries: k_first_rng indexes stay)
};
constexpr int k_nrng = 13, k_first_rng = 5;
constexpr uint32_t A_RNGP = 0x009512A4, A_GLOB = 0x007B72B0, A_SETUPP = 0x0095158C, A_BSETUP = 0x009540E4, A_SCENE = 0x0094DEC4;
constexpr uint32_t k_rtc_lr = 0x005A09AC;

std::mutex g_mu;
uint32_t g_n = 3000, g_start = 3880;   // k (ticks since the anchor); was flips 4180 (= k 3880 with the 300-flip lead-in)
uint32_t g_seed = 0xC0FFEE01u; bool g_seed_on = false;
bool g_fight = false; uint32_t g_f0 = 0;
bool g_first_hashed = false; uint32_t g_first_n = 0;
bool g_hash_on = false;
// tick state (all under g_mu)
uint32_t g_scene = 0, g_cur_scene = 0; bool g_pass_cam = false;   // scene object of the camera loop; scene being updated; a camera stepped in this pass
bool g_anchor_move = false, g_anchored = false, g_have_prev = false; uint64_t g_prev_cam_fp = 0;
uint32_t g_last_log = 0; bool g_stall_logged = false; uint32_t g_stall_k = 0;   // STALL: k unchanged over 600 flips, or not anchored 600 flips after F0
uint32_t g_done = 0;                                               // k: counted passes completed since the anchor
uint32_t g_anchor_flip = 0, g_last_tick_flip = 0, g_last_tick_reads = 0, g_scene_passes = 0, g_cam_calls = 0, g_reads = 0, g_outside = 0;
std::vector<jft::Ev> g_ev;
void (*g_prev_filter)(uint32_t, uint16_t*, uint16_t*, uint16_t*) = nullptr;
std::vector<uint32_t> g_cams, g_fighters, g_pads;
uint32_t g_rngc[k_nrng];
FILE* g_dump = nullptr;
bool g_snap_on = false;   // JSP_SNAPSHOT=1 accepted by jsp_snapshot_init()
std::atomic<int> g_setup_out{0};   // FIX (review of PR 248): 1 = a native online fight: the battle setup table is not in `all` (jsp_fight_hash_native_online)

struct Fnv {
    uint64_t h = 0xCBF29CE484222325ull;
    void b(uint8_t v) { h ^= v; h *= 0x100000001B3ull; }
};
struct Hasher {   // one field hash plus the running `all`
    Fnv all;
    void bytes(Fnv& f, uint32_t a, uint32_t n) { for (uint32_t i = 0; i < n; i++) { uint8_t v = vm_read8(a + i); f.b(v); all.b(v); } }
};

void add_unique(std::vector<uint32_t>& v, uint32_t x, size_t cap) {
    if (!x) return;
    for (uint32_t y : v) if (y == x) return;
    if (v.size() < cap) v.push_back(x);
}

std::string hex_list(const std::vector<uint64_t>& v) {
    std::string s; char t[24];
    for (size_t i = 0; i < v.size(); i++) { std::snprintf(t, sizeof t, "%s%016llx", i ? "," : "", (unsigned long long)v[i]); s += t; }
    return s.empty() ? "-" : s;
}
std::string addr_list(const std::vector<uint32_t>& v) {
    std::string s; char t[16];
    for (size_t i = 0; i < v.size(); i++) { std::snprintf(t, sizeof t, "%s0x%08X", i ? "," : "", v[i]); s += t; }
    return s.empty() ? "-" : s;
}

void dump_range(const char* tag, uint32_t a, uint32_t n, bool mask, uint32_t vt)
{
    if (!g_dump) return;
    std::fprintf(g_dump, "%s 0x%08X +0x%X%s\n", tag, a, n, mask ? " (pointer-range words masked)" : "");
    for (uint32_t o = 0; o + 4 <= n; o += 4) {
        uint32_t w = vm_read32(a + o);
        if (mask && w != vt && w >= 0x00800000u && w < 0x50000000u) w = 0;
        if ((o & 15) == 0) std::fprintf(g_dump, "%s%04X:", o ? "\n" : "", o);
        std::fprintf(g_dump, " %08X", w);
    }
    std::fputc('\n', g_dump);
}

void dump_all(uint32_t n)
{
    if (!g_dump) return;
    std::fprintf(g_dump, "== n=%u\n", n);
    const uint32_t P = vm_read32(A_RNGP), S = vm_read32(A_SETUPP);
    if (P) dump_range("rng", P, 0x28, false, 0);
    if (S) dump_range("setup-header", S + 0x60, 0x2A0, false, 0);
    for (uint32_t c : g_cams) dump_range("cam", c, 0x3C0, false, 0);
    for (uint32_t f : g_fighters) { dump_range("fighter", f, 0x950, false, 0); dump_range("fighter-masked", f, 0x950, true, vm_read32(f)); }
    std::fflush(g_dump);
}

// @ the end of counted scene pass n (= k), state after the step. n is the tick, not a flip.
void hash_tick(uint32_t n, uint32_t flip)
{
    if (!g_hash_on || n < g_start || n >= g_start + g_n || g_cams.empty()) return;
    Hasher H; Fnv rng, glob, seed, setup;
    const uint32_t P = vm_read32(A_RNGP), S = vm_read32(A_SETUPP);
    // FIX (two-PC desync at tick 1, testers on 0.7.0-test3, bundles tb-204931 / tb-204935): the RNG object (0x28 bytes at *(0x009512A4), made by
    // 0x0001972C, filled by 0x00019604) starts with FIVE HEAP POINTERS (+0x00..+0x10: its sub-objects of 0x790, 0x1C, 0x1C, 0x14 and 0x820 bytes);
    // only +0x14..+0x23 is the generator's state (xorshift128, stepped by 0x00018E20) and +0x24 a word set to 0 at creation. The object and its
    // sub-objects are allocated again when a fight is built after other fights (a 3v3, a re-select, cancelled matches), so their addresses follow
    // what the process did before: a first fight in a fresh process has P=0x1A02D3A0, later ones 0x19FECB70 / 0x19FECB40. Both testers' failing
    // matches paired different P (host 0x1A02D3A0, joiner 0x19FECB70), so `rng` (and `all`, the rollback checksum) differed from the very first
    // compared tick while the game state was the same; two copies on one PC always had the same history and the same P. Hashed now: +0x14..+0x27
    // (the state and +0x24), never the pointers. JSP_FIGHT_HASH_RNG_PTRS=1 hashes the old 0x28 bytes again (A/B).
    static const bool rng_ptrs = [] { const char* e = std::getenv("JSP_FIGHT_HASH_RNG_PTRS"); return e && e[0] == '1'; }();
    if (P) { if (rng_ptrs) H.bytes(rng, P, 0x28); else H.bytes(rng, P + 0x14, 0x14); }
    H.bytes(glob, A_GLOB, 0x10);
    if (S) { H.bytes(seed, S + 0x1C, 4); H.bytes(seed, S + 0x48140, 4); }
    // FIX (second cause of the tick-1 desync; review of PR 248): the battle setup table, see jsp_fight_hash_native_online. In a native online fight it is
    // still logged (setup=) but left out of `all`, the rollback checksum: that fight never reads it. JSP_FIGHT_HASH_SETUP=1 keeps it in (A/B), only together
    // with JSP_ROLLBACK_ALLOW_DEBUG=1 (test tooling): set on one PC alone it would only make a false desync, so a player's settings cannot turn it on.
    {   static const bool setup_keep = [] {
            const char* e = std::getenv("JSP_FIGHT_HASH_SETUP"); const char* d = std::getenv("JSP_ROLLBACK_ALLOW_DEBUG");
            return e && e[0] == '1' && d && d[0] && d[0] != '0'; }();
        jfs::hash([](uint32_t a) { return (uint8_t)vm_read8(a); }, setup, H.all, jfs::in_checksum(g_setup_out.load(std::memory_order_relaxed) != 0, setup_keep)); }
    // FIX (rollback false desync): 0x0094DEC4 is no scene step in a fight: it counts the sound library's active playbacks (the stores are in func_00662784 /
    // func_006624DC, called from func_0048DC70 and the CRI range 0x0048xxxx-0x0057xxxx). A playback ends on the host's audio clock, so two PCs saw the count
    // change 0-4 ticks apart while every gameplay part matched (two-copy tests, ticks 969-1031). Left out of the per-tick hash; JSP_FIGHT_HASH_SCENE=1 keeps it (A/B).
    { static const bool keep = [] { const char* e = std::getenv("JSP_FIGHT_HASH_SCENE"); return e && e[0] == '1'; }(); if (keep) H.bytes(setup, A_SCENE, 4); }
    // DIAGNOSTIC (JSP_CAM_DUMP=idx:from:to, test runs only): the raw camera bytes the hash covers (+0x10..+0xB0, +0xF0, +0xF4) of camera idx on every
    // hashed tick from..to, so two copies' logs name the first byte that splits (a re-run prints again: the last line of a tick is the confirmed one).
    {   static int ci = -2; static uint32_t c_from = 0, c_to = 0;
        if (ci == -2) { ci = -1; const char* e = std::getenv("JSP_CAM_DUMP"); if (e && std::sscanf(e, "%d:%u:%u", &ci, &c_from, &c_to) != 3) ci = -1; }
        if (ci >= 0 && (size_t)ci < g_cams.size() && n >= c_from && n <= c_to) {
            const uint32_t c = g_cams[(size_t)ci];
            std::fprintf(stderr, "[camdump] n=%u cam%d=%08X:", n, ci, c);
            for (uint32_t o = 0x10; o < 0xB0; o += 4) std::fprintf(stderr, " %08X", vm_read32(c + o));
            std::fprintf(stderr, " | F0=%02X F4=%08X\n", vm_read8(c + 0xF0), vm_read32(c + 0xF4));
        }
    }
    std::vector<uint64_t> cam, fi, pad;
    // FIX (false desync online, two-copy runs native-rb1..rb4: camera 2 "split" at ticks 1848-3776 with every other part equal): +0x10..+0x8F is the view
    // matrix and its inverse, a CACHE the camera step never writes (0x6F688 stores eye +0x90 and target +0xA0 and sets the dirty flag +0x4); a reader that needs
    // the matrices rebuilds them on demand (vtable slot 0, 0x523D78), and the render does so only for the cameras this PC draws, which differ between the two
    // players' views. The eye and target stayed equal tick for tick after the "split". Hashed now: the camera's own state (+0x90..+0xAF eye/target,
    // +0xF0, +0xF4); the cache is left out. JSP_FIGHT_HASH_CAM_CACHE=1 hashes the old range again (A/B).
    static const bool cam_cache = [] { const char* e = std::getenv("JSP_FIGHT_HASH_CAM_CACHE"); return e && e[0] == '1'; }();
    for (uint32_t c : g_cams) { Fnv f; if (cam_cache) H.bytes(f, c + 0x10, 0xA0); else H.bytes(f, c + 0x90, 0x20); H.bytes(f, c + 0xF0, 1); H.bytes(f, c + 0xF4, 4); cam.push_back(f.h); }
    // FIX (false desync on a same-link rematch, two-copy runs native-rm2 / rm3, object dumps): fighter +0x90..+0xA7 is memory the fight never writes (all
    // zero, constant for a whole first fight). In a later fight of the same process the objects reuse heap that still holds the previous fight's leftovers,
    // which differ between the two PCs (their after-fight screens ran apart), so every rematch "desynced" at tick 1 with every other part equal, and the
    // values never changed afterwards. The fi part is still computed and logged, but it is no longer in `all` (the rollback checksum);
    // JSP_FIGHT_HASH_FIGHTER=1 puts it back (A/B).
    static const bool fi_in_all = [] { const char* e = std::getenv("JSP_FIGHT_HASH_FIGHTER"); return e && e[0] == '1'; }();
    for (uint32_t x : g_fighters) {
        Fnv f;
        if (fi_in_all) H.bytes(f, x + 0x90, 0x18); else for (uint32_t i = 0; i < 0x18; i++) f.b(vm_read8(x + 0x90 + i));
        fi.push_back(f.h);
    }
    for (uint32_t k : g_pads) { Fnv f; H.bytes(f, k + 0x204, 0x14); pad.push_back(f.h); }
    // ENHANCEMENT (testers): the per-tick [hash] line is an unbuffered write on every tick of a rollback match (JSP_FIGHT_HASH defaults on there); it prints only
    // with JSP_NET_TICK_LOG=1 or outside a rollback start. The hash itself still feeds jsp_snapshot_tap below (the rollback checksum), unchanged.
    static const bool show = [] { const char* t = std::getenv("JSP_NET_TICK_LOG"); const char* r = std::getenv("JSP_ROLLBACK"); return (t && t[0] == '1') || !(r && r[0] == '1'); }();
    // DIAGNOSTIC (two-PC desync at tick 1): the part line of the first 3 hashed ticks of every fight always prints (testers run without
    // JSP_NET_TICK_LOG), with the raw RNG object and glob words once per fight, so a bundle names the part that splits (the rollback checksum
    // is only `all` folded to 32 bits).
    const bool early = !g_first_hashed || n <= g_first_n + 2;
    if (!g_first_hashed) {
        std::fprintf(stderr, "[hash] raw n=%u P=0x%08X rng:", n, P);
        for (uint32_t o = 0; P && o < 0x28; o += 4) std::fprintf(stderr, " %08X", vm_read32(P + o));
        std::fprintf(stderr, " glob:");
        for (uint32_t o = 0; o < 0x10; o += 4) std::fprintf(stderr, " %08X", vm_read32(A_GLOB + o));
        std::fputc('\n', stderr);
    }
    if (show || early) {
    std::fprintf(stderr, "[hash] n=%u f=%u all=%016llx rng=%016llx glob=%016llx seed=%016llx setup=%016llx cam=%s fi=%s pad=%s rngc=",
                 n, flip, (unsigned long long)H.all.h, (unsigned long long)rng.h, (unsigned long long)glob.h,
                 (unsigned long long)seed.h, (unsigned long long)setup.h, hex_list(cam).c_str(), hex_list(fi).c_str(), hex_list(pad).c_str());
    for (int i = 0; i < k_nrng; i++) std::fprintf(stderr, "%s%u", i ? "," : "", g_rngc[i]);
    std::fputc('\n', stderr);
    }
    std::memset(g_rngc, 0, sizeof g_rngc);
    {   // DIAGNOSTIC: the battle setup words whenever their hash changes (a desync in `setup` alone names the word)
        static uint64_t last_setup = 0;
        if (setup.h != last_setup) {
            last_setup = setup.h;
            std::fprintf(stderr, "[hash] setup-words n=%u scene=%08X:", n, vm_read32(A_SCENE));
            for (uint32_t o = 0; o < 0xB0; o += 4) std::fprintf(stderr, " %08X", vm_read32(A_BSETUP + o));
            std::fputc('\n', stderr);
        }
    }
    if (g_snap_on) {
        auto fold = [](const std::vector<uint64_t>& v) { Fnv f; for (uint64_t x : v) for (int i = 0; i < 8; i++) f.b((uint8_t)(x >> (8 * i))); return f.h; };
        const uint64_t tv[8] = {H.all.h, rng.h, glob.h, seed.h, setup.h, fold(cam), fold(fi), fold(pad)};   // order = jss::kCompNameImpl
        jsp_snapshot_tap(n, tv);
    }
    if (!g_first_hashed) {
        g_first_hashed = true; g_first_n = n;
        std::fprintf(stderr, "[hash] objs cams=%s fighters=%s pads=%s F0=%u S=0x%08X P=0x%08X first_n=%u START=%u N=%u\n",
                     addr_list(g_cams).c_str(), addr_list(g_fighters).c_str(), addr_list(g_pads).c_str(), g_f0, S, P, n, g_start, g_n);
        dump_all(n);
    } else if (n == g_first_n + 300) dump_all(n);
}

uint64_t cam_fp()
{
    Fnv f;
    for (uint32_t c : g_cams) for (uint32_t i = 0; i < 0xA0; i++) f.b(vm_read8(c + 0x10 + i));
    return f.h;
}

// A counted pass finished (a camera stepped inside the update of the scene g_scene). Called with g_mu held, after the original.
void end_pass(uint32_t flip)
{
    g_scene_passes++;
    if (!g_anchored) {
        if (g_anchor_move) {
            const uint64_t fp = cam_fp();
            const bool moved = g_have_prev && fp != g_prev_cam_fp;
            g_prev_cam_fp = fp; g_have_prev = true;
            if (!moved) return;
        }
        g_anchored = true; g_anchor_flip = g_last_tick_flip = flip; g_last_tick_reads = g_reads;
        std::fprintf(stderr, "[tick] anchor (%s) at f=%u (F0=%u, +%u flips): this pass is k=1; scene=0x%08X cams=%zu\n",
                     g_anchor_move ? "first camera movement" : "first counted pass", flip, g_f0, flip - g_f0, g_scene, g_cams.size());
    }
    g_done++;
    if (g_done % 300 == 0) {
        const uint32_t df = flip - g_last_tick_flip, dr = g_reads - g_last_tick_reads;
        std::fprintf(stderr, "[tick] k=%u f=%u: 300 ticks in %u flips (%.3f ticks/flip), %u pad reads (%.3f per tick)\n", g_done, flip, df,
                     df ? 300.0 / df : 0.0, dr, dr / 300.0);
        g_last_tick_flip = flip; g_last_tick_reads = g_reads;
    }
    hash_tick(g_done, flip);
}

void pad_filter(uint32_t port, uint16_t* buttons, uint16_t* lx, uint16_t* ly)
{
    std::vector<int> fresh;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (port == 0) g_reads++;
        jft::filter(g_ev, g_done, g_anchored, port, buttons, lx, ly, g_prev_filter, &fresh);   // mask first (t >= 2), then the earlier filter (port menu)
        for (int i : fresh)
            std::fprintf(stderr, "[tick] pad ev=%d t=%u mask=0x%04X hold=%u first read at done=%u f=%u\n", i, g_ev[i].t, g_ev[i].mask, g_ev[i].hold, g_done, cellGcm_flip_request_count());
    }
}

// The one place that knows where the pad hook lives (toolkit patch 6501); chained behind an earlier filter.
void install_pad_ticks()
{
    g_prev_filter = g_cellpad_filter;
    g_cellpad_filter = pad_filter;
}

// anchor-keyed presses (JSP_ANCHOR_PRESSES="T:mask[:hold],..."), used by this file (hash mode).
// Own pad filter (chained like install_pad_ticks), own mutex; the callers report the anchor message and the end of every scene pass.
jft::Anchor g_anc;
std::mutex g_anc_mu;
void (*g_anc_prev)(uint32_t, uint16_t*, uint16_t*, uint16_t*) = nullptr;
void anchor_filter(uint32_t port, uint16_t* b, uint16_t* lx, uint16_t* ly)
{
    std::vector<int> fresh;
    {
        std::lock_guard<std::mutex> lk(g_anc_mu);
        if (port == 0) *b = (uint16_t)(*b | g_anc.mask(&fresh));
        for (int i : fresh)
            std::fprintf(stderr, "[apress] ev=%d T=%u mask=0x%04X hold=%u first read at done=%u f=%u\n", i, g_anc.ev[i].t, g_anc.ev[i].mask, g_anc.ev[i].hold, g_anc.done, cellGcm_flip_request_count());
    }
    if (g_anc_prev) g_anc_prev(port, b, lx, ly);
}

void rtc_hle(ppu_context* c)
{
    const uint32_t p = (uint32_t)c->gpr[3], lr = (uint32_t)c->lr;
    c->gpr[3] = (uint64_t)(int64_t)cellRtcGetCurrentTick((void*)(uintptr_t)p);
    if (g_seed_on && p && lr == k_rtc_lr) {
        vm_write64(p, (0x00E31FF5ull << 32) | g_seed);
        std::fprintf(stderr, "[hash] f=%u seed override: tick=0x%016llX lr=0x%08X\n", cellGcm_flip_request_count(),
                     (unsigned long long)vm_read64(p), lr);
    }
}

template <int I> struct Hook {
    static fn_t orig;
    static void run(ppu_context* c)
    {
        const uint32_t flip = cellGcm_flip_request_count(), r3 = (uint32_t)c->gpr[3];
        const Kind k = k_ents[I].kind;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            if (k == K_BEGIN) {
                // every 600 flips from F0 (anchored or not: shows what the scene update does while the fight loads)
                if (g_fight && flip - g_last_log >= 600) {   // not `% 600`: the flip counter sometimes steps 0 then 2
                    g_last_log = flip;
                    std::fprintf(stderr, "[tick] f=%u n=%u k=%u anchored=%d scene=0x%08X scene_passes=%u cam_calls=%u outside_scene=%u reads=%u\n",
                                 flip, flip - g_f0, g_done, (int)g_anchored, g_scene, g_scene_passes, g_cam_calls, g_outside, g_reads);
                    // Stall diagnostic, no timeout and no fallback press: say once what stood still (cause hunting only).
                    if (!g_stall_logged && flip > g_f0 + 300 && (!g_anchored || g_done == g_stall_k)) {
                        g_stall_logged = true;
                        std::fprintf(stderr, "[tick] STALL f=%u k=%u anchored=%d scene_passes=%u cam_calls=%u\n", flip, g_done, (int)g_anchored, g_scene_passes, g_cam_calls);
                    }
                    g_stall_k = g_done;
                }
            }
            else if (k == K_CAM) {
                add_unique(g_cams, r3, 8);
                g_cam_calls++;
                if (g_cur_scene) {
                    g_pass_cam = true;
                    if (!g_scene && g_fight) { g_scene = g_cur_scene; std::fprintf(stderr, "[tick] f=%u scene object 0x%08X (@0x0014EA68 this) stepped camera 0x%08X first\n", flip, g_scene, r3); }
                } else if (g_fight && g_outside++ == 0)
                    std::fprintf(stderr, "[tick] WARNING f=%u camera step outside the scene update @0x0014EA68: the tick source is incomplete\n", flip);
            }
            else if (k == K_SCENE) { /* handled around the original, below */ }
            else if (k == K_MSG) {
                bool f0_now = false;
                if (!g_fight) {
                    f0_now = true;
                    g_fight = true; g_f0 = g_last_log = flip;
                    const uint32_t S = vm_read32(A_SETUPP), v = S ? vm_read32(S + 0x1C) : 0;
                    std::fprintf(stderr, "[hash] f=%u fight start (first @0x00084FF0), F0=%u setup=0x%08X +0x1C=0x%08X want=0x%08X %s\n",
                                 flip, flip, S, v, g_seed, !g_seed_on ? "(no override)" : v == g_seed ? "seed OK" : "SEED MISMATCH");
                }
                if (!f0_now) { const uint32_t r4 = (uint32_t)c->gpr[4]; if (r4) jsp_anchor_msg(vm_read32(r4 + 8), g_done, flip); }
                add_unique(g_fighters, r3, 16);
            }
            else if (k == K_PAD) add_unique(g_pads, r3, 8);
            else if (k == K_RNG) g_rngc[I - k_first_rng]++;
        }
        if (k == K_SCENE) {
            uint32_t prev_scene, was_cam;
            { std::lock_guard<std::mutex> lk(g_mu); prev_scene = g_cur_scene; was_cam = g_pass_cam; g_cur_scene = r3; g_pass_cam = false; }
            orig(c);
            jsp_anchor_pass_end();
            uint32_t kdone = 0; bool counted;
            {
                std::lock_guard<std::mutex> lk(g_mu);
                counted = g_pass_cam && g_scene == r3 && g_fight;
                g_cur_scene = prev_scene; g_pass_cam = was_cam;
                if (counted) end_pass(flip);
                kdone = g_done;
            }
            if (counted && g_snap_on) jsp_snapshot_pass_end(kdone, c);   // save/restore at this fixed call site (outside g_mu)
            return;
        }
        orig(c);
        if (k == K_FACT) { std::lock_guard<std::mutex> lk(g_mu); add_unique(g_fighters, (uint32_t)c->gpr[3], 16); }
    }
    static void install()
    {
        if (jsp_hook_chain::install(k_ents[I].addr, &run, &orig)) return;   // chains to an earlier port hook on the address
        std::fprintf(stderr, "[hash] no lifted function at %08X (its data will be missing)\n", k_ents[I].addr);
    }
};
template <int I> fn_t Hook<I>::orig;
template <int... Is> void install_all(std::integer_sequence<int, Is...>) { (Hook<Is>::install(), ...); }

bool env_on(const char* n, const char** out) { const char* e = std::getenv(n); *out = e; return e && *e && e[0] != '0'; }

// JSP_FIGHT_SEED is hex and counts as set whenever it is non-empty: a leading '0' is part of the value, not an off switch.
bool parse_seed(const char* s, uint32_t* out)
{
    if (!s || !*s) return false;
    char* end = nullptr;
    const unsigned long v = std::strtoul(s, &end, 16);
    if (end == s) { std::fprintf(stderr, "[hash] JSP_FIGHT_SEED=%s is not hex (seed override off)\n", s); return false; }
    *out = (uint32_t)v;
    return true;
}

}  // namespace

// Returns 1 when anchor presses are installed. FIGHT_PRESSES=none runs set no JSP_ANCHOR_PRESSES at all (no in-fight presses after the anchor).
extern "C" int jsp_anchor_presses_init(void)
{
    const char* e = std::getenv("JSP_ANCHOR_PRESSES");
    if (!e || !*e || g_anc.ev.size()) return g_anc.ev.size() ? 1 : 0;
    int bad = 0;
    if (jft::parse(e, &g_anc.ev, &bad) < 0) { std::fprintf(stderr, "[anchor] JSP_ANCHOR_PRESSES item %d is malformed (want T:mask[:hold], T >= 2)\n", bad); return 0; }
    g_anc_prev = g_cellpad_filter;
    g_cellpad_filter = anchor_filter;
    std::fprintf(stderr, "[anchor] JSP_ANCHOR_PRESSES: %zu events, filter installed (prev filter %s)\n", g_anc.ev.size(), g_anc_prev ? "chained" : "none");
    return 1;
}
// A message with `id` reached the fighter handler after F0 (k, flip: the caller's tick counter and flip). Logs "[anchor] k= f=" once.
extern "C" void jsp_anchor_msg(uint32_t id, uint32_t k, uint32_t flip)
{
    std::lock_guard<std::mutex> lk(g_anc_mu);
    if (g_anc.on_msg(id)) std::fprintf(stderr, "[anchor] k=%u f=%u first id 4 message after F0 (anchor tick 0 = this scene pass)\n", k, flip);
}
// A scene pass (@0x0014EA68 call) ended.
extern "C" void jsp_anchor_pass_end(void)
{
    std::lock_guard<std::mutex> lk(g_anc_mu);
    g_anc.pass_end();
}

// jsp_snapshot.cpp: host-side tick counters that go back with the guest memory (the pad presses are keyed to them).
extern "C" void jsp_fight_hash_get_state(JspTickState* s)
{
    { std::lock_guard<std::mutex> lk(g_mu); s->done = g_done; }
    { std::lock_guard<std::mutex> lk(g_anc_mu); s->anc_done = g_anc.done; s->anc_set = g_anc.set; s->anc_skip = g_anc.skip; }
}
extern "C" void jsp_fight_hash_set_state(const JspTickState* s)
{
    { std::lock_guard<std::mutex> lk(g_mu); g_done = s->done; }
    { std::lock_guard<std::mutex> lk(g_anc_mu); g_anc.done = s->anc_done; g_anc.set = s->anc_set; g_anc.skip = s->anc_skip; }
}

extern "C" void jsp_register_fight_hash_overrides(void)
{
    const char* eh;
    uint32_t seed_v = 0;
    const bool seed = parse_seed(std::getenv("JSP_FIGHT_SEED"), &seed_v), hash = env_on("JSP_FIGHT_HASH", &eh);
    const char* et = std::getenv("JSP_PAD_TICKS");
    const bool ticks = et && *et;
    if (!seed && !hash && !ticks) return;
    g_seed_on = seed;
    if (seed) g_seed = seed_v;
    else if (hash) { g_seed_on = true; }   // hash mode defaults the seed override on (9.5: default 0xC0FFEE01)
    if (hash) { const unsigned long v = std::strtoul(eh, nullptr, 10); if (v > 1) g_n = (uint32_t)v; }
    { const char* es = std::getenv("JSP_FIGHT_HASH_START"); if (es && *es) g_start = (uint32_t)std::strtoul(es, nullptr, 10); }
    ps3_hle_register_ctx(0x9DAFC0D9u, "cellRtcGetCurrentTick", rtc_hle);
    std::fprintf(stderr, "[hash] seed override %s seed=0x%08X hash=%s START=%u N=%u\n", g_seed_on ? "on" : "off", g_seed, hash ? "on" : "off", g_start, g_n);
    g_hash_on = hash;
    { const char* ea = std::getenv("JSP_TICK_ANCHOR"); g_anchor_move = ea && !std::strcmp(ea, "move"); }
    std::fprintf(stderr, "[hash] key=tick anchor=%s\n", g_anchor_move ? "move" : "cam");
    if (ticks) {
        int bad = 0;
        if (jft::parse(et, &g_ev, &bad) < 0) {
            std::fprintf(stderr, "[tick] JSP_PAD_TICKS item %d is malformed (want T:mask[:hold], T >= 2): no tick-keyed pad events\n", bad);
        } else {
            install_pad_ticks();
            std::fprintf(stderr, "[tick] JSP_PAD_TICKS: %zu events, filter installed (g_cellpad_filter, prev filter %s)\n", g_ev.size(), g_prev_filter ? "chained" : "none");
        }
    }
    if (!hash && !ticks) return;
    jsp_anchor_presses_init();   // the trace (JSP_FIGHT_TRACE, no hash) calls this itself
    if (hash) {
        const char* d = std::getenv("JSP_FIGHT_DUMP");   // object dumps only when asked for (the rollback netcode writes no file)
        if (d && *d) { g_dump = std::fopen(d, "w"); if (!g_dump) std::fprintf(stderr, "[hash] cannot open the dump file (dumps skipped)\n"); }
    }
    install_all(std::make_integer_sequence<int, sizeof k_ents / sizeof k_ents[0]>());
    if (hash) g_snap_on = jsp_snapshot_init(g_start, g_n) != 0;
    std::fprintf(stderr, "[hash] hooks installed (%zu addresses)\n", sizeof k_ents / sizeof k_ents[0]);
}
#endif  // JSP_FIGHT_TICK_TEST
// ENHANCEMENT (online rematch): forget the fight that just ended so the next fight (Fight again / a re-select) finds its own scene object,
// cameras, fighters and pads and anchors again from k=0. Without this g_fight / g_anchored / g_scene stay set from the first fight: a new
// scene object at another heap address is never counted (k freezes) and one at the same address continues the old count. Call it on the
// main thread once the rollback session of the old fight has ended (no save or rollback may run during it). Keeps the hooks, the seed
// override and the parsed pad / anchor press lists; clears the per-fight counters and the anchor's tick state.
// FIX: also the KO counter addresses and their heap search (jsp_game.cpp), so a rematch never reads the previous fight's KO counters.
extern "C" void jsp_ko_counters_reset(void);
extern "C" void jsp_fight_hash_reset_fight(void)
{
    jsp_ko_counters_reset();
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_fight = false; g_f0 = 0; g_first_hashed = false; g_first_n = 0;
        g_scene = g_cur_scene = 0; g_pass_cam = false;
        g_anchored = g_have_prev = false; g_prev_cam_fp = 0;
        g_last_log = 0; g_stall_logged = false; g_stall_k = 0;
        g_done = 0;
        g_anchor_flip = g_last_tick_flip = g_last_tick_reads = g_scene_passes = g_cam_calls = g_reads = g_outside = 0;
        g_cams.clear(); g_fighters.clear(); g_pads.clear();
        for (uint32_t& c : g_rngc) c = 0;
    }
    { std::lock_guard<std::mutex> lk(g_anc_mu); g_anc.done = 0; g_anc.set = false; g_anc.skip = 0; }
    std::fprintf(stderr, "[tick] fight state reset (the next fight anchors again)\n");
    // FIX (two-PC desync at tick 1, second cause; testers on 0.7.0-test3): 0x009540E4 is Free Battle's battle setup table (0x2C per slot: player /
    // CPU type, character, CPU level ...), hashed as `setup`. Free Battle's character select writes it; an online fight neither writes nor clears
    // it (two-copy runs: all zero through whole online matches and rematches). So a PC that played an offline Free Battle before the online match
    // still held that fight's picks there while a fresh PC held zeroes, and `setup` (and `all`) differed from the first compared tick (run os-fix-a:
    // host after one offline Free Battle vs a fresh joiner, rng / glob / seed / cam / pad equal).
    // FIX (review of PR 248): this used to CLEAR the table (44 words), and this function also runs on a desync, a disconnect and a lobby open, where the
    // title goes on locally. Nothing is written now: the native online fight never reads the table (jsp_fight_hash_native_online), so it is left out of
    // that mode's checksum instead, and teardown and failed opens leave the player's Free Battle picks alone. Read only: the count is logged.
    std::fprintf(stderr, "[tick] battle setup table 0x%08X: %u nonzero words (left as they are)\n", A_BSETUP,
                 jfs::nonzero_words([](uint32_t a) { return (uint32_t)vm_read32(a); }));
}
// FIX (review of PR 248, the battle setup table at 0x009540E4 + 0xB0): which code reads it, from the lifted code of the US 1.00 EBOOT. The table is reached
// only through two TOC words, r2-0x31D0 = 0x009540A4 (the table at +0x40) and r2-0x3194 = 0x009540E8 (= record +0x04); no lis/addi pair builds it. Every
// function that loads either word lies in 0x00183794..0x001898B8, the battle setup / character select module. Following their callers (direct calls) up to
// the functions nothing calls directly and resolving those through the descriptor tables: the character select scene (ctor 0x001898B8, vtable
// 0x0087BDB0 and its neighbours 0x0087BD48..0x0087BE48, created by the scene factories 0x00155F68 and 0x001EB430), the menu pages of 0x0018D278..
// 0x00190298 (descriptors at 0x0087BF28..0x0087BF74) and the module's static constructor / destructor (0x00184C60 / 0x00184C70 / 0x00185090 /
// 0x001850A0 in the init and fini lists). The online battle mode (vtable 0x00878A70), the online battle builder 0x002EEE90 and its callers
// (0x002EF788, 0x002EFBC8, 0x002EFCEC), the online pad take 0x0F76C4 / 0x0F7234, the scene update 0x0014EA68 and the manager update 0x0045222C reach none
// of them by direct calls. The runs agree: the table stays all zero through whole native
// online matches while the fighters are the room's picks. So a native online fight's state does not depend on it, and its checksum leaves it out
// (hash_tick). The offline fight (the rollback bench, the non-native path) is BUILT from it by Free Battle, so there it stays in the checksum.
// Set by jsp_net_game.cpp at the session start (before its first save), 0 again when the match is reset. Any thread; read by the hash on the game thread.
extern "C" void jsp_fight_hash_native_online(int on)
{
    if (g_setup_out.exchange(on ? 1 : 0) != (on ? 1 : 0))
        std::fprintf(stderr, "[tick] battle setup table %s the rollback checksum (%s)\n", on ? "left out of" : "back in", on ? "a native online fight never reads it" : "not a native online fight");
}
// ENHANCEMENT (online match end): KO state of the current fight, read from the two KO counter objects (vtable 0x00876558, +0x24 KOs scored,
// +0x28 KOs needed; jsp_game.cpp finds them). Guest heap words: they roll back with the snapshot, so on a confirmed tick both copies agree.
// Returns -1 while the counters are not known, 0 while nobody has won, 1 red won, 2 blue won.
extern "C" uint32_t g_ko_counter_addr(int side);
extern "C" int jsp_fight_ko_winner(void)
{
    int won = 0;
    for (int s = 0; s < 2; s++) {
        const uint32_t a = g_ko_counter_addr(s);
        if (!a || vm_read32(a) != 0x00876558u) return -1;
        const int32_t cnt = (int32_t)vm_read32(a + 0x24), lim = (int32_t)vm_read32(a + 0x28);
        if (lim > 0 && cnt >= lim && !won) won = s + 1;
    }
    return won;
}
// ENHANCEMENT (online match end, time-up): the round result object M = *(u32*)0x00952310 (0x14 bytes, guest heap, reset to 0 at round start by
// 0x000E1A4C; written inside the tick's update, so it rolls back and is deterministic). M+0: a low nibble other than 0 = decided (4 = time up),
// bit0 red won / bit1 blue won once the judge 0x000E62E8 ran; M+4 the reason (2 = time up). The timer that sets it: rule object vtable 0x00879730,
// 60 per game second (static analysis; the winner bits are to be confirmed by a run that reaches time-up).
// Returns -1 unknown (no object), 0 running, 1 red won, 2 blue won, 3 time up with the winner not written yet.
extern "C" int jsp_fight_end_state(void)
{
    const uint32_t m = vm_read32(0x00952310u);
    if (m < 0x10000000u || m >= 0x20000000u) return -1;
    const uint32_t ph = vm_read32(m), why = vm_read32(m + 4);
    if (ph & 1u) return 1;
    if (ph & 2u) return 2;
    if (why == 2u && (ph & 0xFu)) return 3;
    return 0;
}
// DIAGNOSTIC (jsp_net_game.cpp JSP_NET_CAM_WATCH): the guest address of camera i, 0 = none.
extern "C" uint32_t jsp_fight_hash_cam_addr(int i) { return i >= 0 && (size_t)i < g_cams.size() ? g_cams[(size_t)i] : 0u; }
