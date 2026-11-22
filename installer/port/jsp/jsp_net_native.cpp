/* jsp_net_native.cpp -- test only, off by default: a trace of the game's OWN online (Online Battle over re-psn) and an
 * auto-pilot that walks a copy into a native Friendly Match (on only with JSP_NATIVE_TRACE=1 or AUTO_ONLINE), and the Discord Join menu drive below.
 *
 * JSP_NATIVE_TRACE=1 logs on stderr, prefixed [ntrace], with the flip count:
 *   - sys_net socket / bind / socketclose (type 6 = SYS_NET_SOCK_DGRAM_P2P marks the game's UDP P2P socket, opened by
 *     0x0046A35C, vport 0x124D), and per second the sendto / recvfrom counts and bytes on that socket and on the others.
 *     Done by registering the sys_net NIDs before the runtime does (the first registration wins), calling the runtime's own
 *     sys_net_bnet_* functions; only when PS3_NET_ONLINE is set at startup (else the offline model stays).
 *   - per second, the indirect-call counts of the game's net modules (0x446000-0x488000: matching, manager, room, share pad,
 *     event, host share, easy share, user error ...), the lobby / select users of EasyShare (0x2958A0-0x2B1000, 0x2ED000-0x2F0000)
 *     and the share-pad game callers (0x0EDFDC, 0x0F7234 ...). Direct calls (bl) are not seen: the lift calls those without
 *     the dispatcher, so e.g. share pad 0x00484690 (called directly by the manager update 0x0045222C) shows only through
 *     its virtual parents. The share-pad object (manager +0x38FE0) fields +0x6C8..+0x6EC are logged when they change.
 *   - each new indirect-call target after the main menu is first up (target, r3, r3's vtable, scene word), capped: this is
 *     how the online menus' screens are found.
 *   - the scene word 0x0094DEC4 and the submenu word 0x0095654C on change; the first call of the scene update 0x0014EA68 and
 *     the fight HUD 0x00085CEC in each scene; the battle setup records 0x009540E4 + 0x2C*slot when the fight HUD first runs.
 * AUTO_ONLINE=host|join: walks from boot through a script of steps (built in per role; AUTO_ONLINE_SCRIPT replaces it).
 *   Tokens, separated by spaces:
 *     menu              wait for the main menu list (Left / Cross in turn meanwhile, as AUTO_FIGHT does)
 *     rowN              move the main menu cursor to row N
 *     battle            FIX (two-page Main Menu, feat_menu_pages.cpp): onto the Battle Modes page (Up to its top row Battle Modes, then
 *                       Cross); nothing with MENU_PAGES=0. The game's row numbers (0x00956550) of J-Adventure .. Online Battle (0..4)
 *                       are the same on that page and on the one-page menu, so "battle row4" reaches Online Battle either way; on the
 *                       top page row 4 is Port Settings / Options.
 *     X O T Q S U D L R one press (Cross Circle Triangle Square Start Up Down Left Right), then 700 ms
 *     wN                wait N ms
 *     B~COND            press B every AUTO_ONLINE_EVERY ms (default 5000) until COND
 *     ?COND             wait until COND
 *     end               hand the pad back
 *     room / units      log the room data (fighter slots, teams) / the battle world units (team, driver, net slot)
 *   COND: tgHEX (that indirect-call target ran in the last 500 ms), !tgHEX (has not), vtHEX (an object with that vtable
 *   was the r3 of an indirect call in the last 500 ms), !vtHEX, sceneHEX (scene word ==), fight (the fight HUD runs),
 *   p1 (Player 1 entered its first state).
 *   ENHANCEMENT (post-fight phase, rematch tests): overN (N rollback matches have ended: the phase left RUNNING N times, e.g. after
 *   "[net] ended: Match over"), runN (N rollback matches have started running). Token
 *   p1reset forgets p1 (so ?p1 waits for the NEXT fight's Player 1). While a rollback match runs (jsp_net_lobby_phase RUNNING) the
 *   autopilot never writes the pad (the steps still advance): the rollback owns the fight's input.
 *   Each step logs "[auto-online] step N ..." and a stall line every 30 s on one step.
 * JSP_DISCORD_AUTO_NAV (ENHANCEMENT, on unless 0; only with the game's own online on and JSP_DISCORD not 0): a Discord Join
 *   (jsp_discord_take_join) runs "home battle row4 X~sub4@7000 w2500 D w1000 X w4000 D w1000 J end" for the player: home = Circle back to the
 *   main menu list, J = Cross every 5 s until jsp_discord_join_state says joined (one more search on a failure, then it gives up).
 *   FIX: it hands the pad back on the player's own input, after 60 s in any case, when Circle does not reach the main menu in 15 s (a
 *   Yes/No box: it never answers one) and when the join is dropped (state 0: no Cross on an unfiltered room list); between its presses the
 *   player's input passes through. The AUTO_ONLINE test autopilot keeps its old behaviour (no limit, pad zeroed).
 * Addresses: the US 1.00 EBOOT's. */
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "ps3emu/ps3types.h"
#include "ps3emu/nid.h"
#include "ppu_recomp.h"
extern "C" void ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
#include "jsp_hook_chain.h"
#include "jsp_discord.h"
#include "jsp_net_lobby.h"   // ENHANCEMENT (AUTO_ONLINE post-fight phase): jsp_net_lobby_phase for the over / run conditions

extern "C" int (*g_ppu_icall_hook)(uint32_t target, uint64_t* gpr);                                  /* 0500 */
extern "C" void (*g_cellpad_filter)(uint32_t port_no, uint16_t* buttons, uint16_t* lx, uint16_t* ly); /* 6501 */
extern "C" uint32_t cellGcm_flip_request_count(void);
/* FIX (two-page Main Menu): feat_menu_pages.cpp, read only here: the pages are on (MENU_PAGES, default on), the page shown (0 top, 1 Battle Modes),
 * a page change or the Port Settings screen is under way */
bool mp_on(); int mp_page_now(); bool mp_busy();
extern "C" void ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*));
/* libs/network/sysNet.c (guest addresses passed as pointers, as its own handlers do) */
extern "C" int32_t sys_net_bnet_socket(int32_t domain, int32_t type, int32_t protocol);
extern "C" int32_t sys_net_bnet_close(int32_t s);
extern "C" int32_t sys_net_bnet_bind(int32_t s, const void* addr, uint32_t addrlen);
extern "C" int32_t sys_net_bnet_sendto(int32_t s, const void* buf, uint32_t len, int32_t flags, const void* addr, uint32_t addrlen);
extern "C" int32_t sys_net_bnet_recvfrom(int32_t s, void* buf, uint32_t len, int32_t flags, void* addr, void* addrlen);

namespace {

bool g_trace = false;
int g_role = 0;   /* 0 off, 1 host, 2 join */
unsigned long long g_t0 = 0;

unsigned long long now_ms() { return GetTickCount64(); }
double secs() { return (now_ms() - g_t0) / 1000.0; }
uint32_t flips() { return cellGcm_flip_request_count(); }
uint32_t scene_word() { return vm_read32(0x0094DEC4u); }

#define NLOG(...) do { std::fprintf(stderr, __VA_ARGS__); std::fflush(stderr); } while (0)

/* ---- sockets ---- */
std::atomic<int> g_p2p_fd{-1};
std::atomic<uint32_t> g_snd_n{0}, g_snd_b{0}, g_rcv_n{0}, g_rcv_b{0}, g_osnd_n{0}, g_orcv_n{0}, g_snd_err{0};
std::atomic<uint32_t> g_tot_snd{0}, g_tot_rcv{0};

#define R32(n) ((uint32_t)ctx->gpr[n])
#define S32(n) ((int32_t)(uint32_t)ctx->gpr[n])
#define PTR(n) ((void*)(uintptr_t)(uint32_t)ctx->gpr[n])
#define RET_S(v) (ctx->gpr[3] = (uint64_t)(int64_t)(int32_t)(v))

void h_socket(ppu_context* ctx)
{
    const int32_t type = S32(4);
    const int32_t r = sys_net_bnet_socket(S32(3), type, S32(5));
    NLOG("[ntrace] f=%u t=%.1f socket(domain %d, type %d, proto %d) -> %d lr=%08X%s\n", flips(), secs(), S32(3), type, S32(5), r,
         (uint32_t)ctx->lr, type == 6 ? " (P2P)" : "");
    if (type == 6 && r >= 0) g_p2p_fd = r;
    RET_S(r);
}
void h_close(ppu_context* ctx)
{
    const int32_t s = S32(3);
    const int32_t r = sys_net_bnet_close(s);
    NLOG("[ntrace] f=%u t=%.1f socketclose(%d) -> %d lr=%08X%s (P2P totals: sent %u recv %u)\n", flips(), secs(), s, r, (uint32_t)ctx->lr,
         s == g_p2p_fd ? " (P2P)" : "", g_tot_snd.load(), g_tot_rcv.load());
    if (s == g_p2p_fd) g_p2p_fd = -1;
    RET_S(r);
}
void h_bind(ppu_context* ctx)
{
    const uint32_t a = R32(4);
    const int32_t r = sys_net_bnet_bind(S32(3), PTR(4), R32(5));
    NLOG("[ntrace] f=%u t=%.1f bind(%d, port %u vport %u) -> %d lr=%08X\n", flips(), secs(), S32(3),
         a ? (unsigned)vm_read16(a + 2) : 0u, a ? (unsigned)vm_read16(a + 8) : 0u, r, (uint32_t)ctx->lr);
    RET_S(r);
}
void h_sendto(ppu_context* ctx)
{
    const int32_t s = S32(3);
    const int32_t r = sys_net_bnet_sendto(s, PTR(4), R32(5), S32(6), PTR(7), R32(8));
    if (s == g_p2p_fd) {
        if (r >= 0) { g_snd_n++; g_snd_b += (uint32_t)r; g_tot_snd++; } else g_snd_err++;
    } else g_osnd_n++;
    RET_S(r);
}
void h_recvfrom(ppu_context* ctx)
{
    const int32_t s = S32(3);
    const int32_t r = sys_net_bnet_recvfrom(s, PTR(4), R32(5), S32(6), PTR(7), PTR(8));
    if (r > 0) {
        if (s == g_p2p_fd) { g_rcv_n++; g_rcv_b += (uint32_t)r; g_tot_rcv++; } else g_orcv_n++;
    }
    RET_S(r);
}

/* ---- indirect calls ---- */
int (*g_prev_icall)(uint32_t, uint64_t*) = nullptr;
void (*g_prev_pad)(uint32_t, uint16_t*, uint16_t*, uint16_t*) = nullptr;

std::mutex g_mu;
std::unordered_map<uint32_t, uint32_t> g_hist;     /* per second, net targets */
std::unordered_set<uint32_t> g_seen_targets;      /* every target seen (for the "new target" log) */
std::atomic<bool> g_menu_seen{false};
unsigned g_new_logged = 0;
std::atomic<unsigned long long> g_hud_ms{0}, g_scene_upd_ms{0};
std::atomic<bool> g_p1_moved{false};
std::atomic<uint32_t> g_main_obj{0};
std::atomic<unsigned long long> g_main_ms{0};
/* ENHANCEMENT (post-fight phase): rollback matches started / ended, counted from jsp_net_lobby_phase on every pad poll */
std::atomic<int> g_rb_runs{0}, g_rb_overs{0}, g_rb_phase{0};
void rb_phase_watch()
{
    const int ph = jsp_net_lobby_phase();
    const int was = g_rb_phase.exchange(ph);
    if (ph == was) return;
    if (ph == JSP_LOBBY_RUNNING) g_rb_runs++;
    if (was == JSP_LOBBY_RUNNING) g_rb_overs++;
    if (g_trace || ph == JSP_LOBBY_RUNNING || was == JSP_LOBBY_RUNNING)
        std::fprintf(stderr, "[auto-online] rollback phase %d -> %d (matches run %d, over %d) at %.1f s (scene %X)\n", was, ph, g_rb_runs.load(), g_rb_overs.load(),
                     (GetTickCount64() - g_t0) / 1000.0, vm_read32(0x0094DEC4u));
}

/* watched targets / vtables of the script's conditions: last time seen */
struct Watch { uint32_t key; bool vt; std::atomic<unsigned long long> ms; };
Watch g_watch[32];
std::atomic<int> g_nwatch{0};
std::atomic<bool> g_watch_vt{false};

bool net_target(uint32_t t)
{
    return (t >= 0x00446000u && t < 0x00488000u) || (t >= 0x002958A0u && t < 0x002B1000u) || (t >= 0x002ED000u && t < 0x002F0000u) ||
           t == 0x000EDFDCu || t == 0x000F7234u || t == 0x000F73E8u || t == 0x000F7880u || t == 0x000F7D1Cu;
}

bool plausible_obj(uint32_t r3) { return r3 >= 0x10000u && r3 < 0xF0000000u && !(r3 & 3); }

void per_second_and_scene();
uint32_t share_pad_obj();

/* ---- JSP_ONLINE_VERSUS_DEFAULT (on unless 0; only with the game's own online on): two humans fight each other ----
 * The Friendly Match battle settings (Square in the room) hold only character, stage and BGM; there is no team, COM-count or
 * 1v1 option: a fight is always 2v2 (4 fighter slots, empty ones COM). The room data R = *(0x0095839C) (0x14C bytes, ctor
 * 0x0029FB20) has one team byte a slot at R+0x56..0x59 (2 = not set) and 4 fighter records R+0x68 + 0x24*i (read by 0x0029ED60):
 * +0x16 team, +0x1A non-zero = COM (the battle builder 0x002EEE90 then gives the unit driver 3 and no net slot). The builder
 * (online loading scene, vtable 0x00882220, update 0x002ECD30) places the team-0 records, then the team-1 records.
 * With exactly 2 room members (matching object *(*(TOC+0x59B8)) +0xA0) and both human records on one team, the 2nd human swaps
 * team with a COM of the other team: human + COM against human + COM. If the humans are already on opposite teams (what the
 * game did in the two-copy test), nothing is written. Both copies apply the same rule to their own R, on every call of the
 * room / loading scene updates. */
bool g_versus = false;
void versus_apply()
{
    const uint32_t R = vm_read32(0x0095839Cu);
    if (!plausible_obj(R)) return;
    const uint32_t mp = vm_read32(0x008D3888u + 0x59B8u);
    const uint32_t matching = plausible_obj(mp) ? vm_read32(mp) : 0;
    if (!plausible_obj(matching) || vm_read32(matching + 0xA0u) != 2) return;
    uint8_t t[4], c[4], s[4];
    int hum[4], nh = 0;
    for (uint32_t i = 0; i < 4; i++) {
        const uint32_t r = R + 0x68u + 0x24u * i;
        t[i] = vm_read8(r + 0x16u); c[i] = vm_read8(r + 0x1Au); s[i] = vm_read8(R + 0x56u + i);
        if (c[i] == 0 && t[i] <= 1) hum[nh++] = (int)i;
    }
    static uint32_t last_sig = 0xFFFFFFFFu; static int said = 0;
    const uint32_t sig = ((uint32_t)t[0] | t[1] << 2 | t[2] << 4 | t[3] << 6) | (uint32_t)(c[0] != 0) << 8 | (uint32_t)(c[1] != 0) << 9 |
                         (uint32_t)(c[2] != 0) << 10 | (uint32_t)(c[3] != 0) << 11;
    if (sig != last_sig && said < 16) {
        said++; last_sig = sig;
        NLOG("[online] versus check: room data %08X teams %u,%u,%u,%u COM %u,%u,%u,%u slot teams %u,%u,%u,%u -> %s\n", R, t[0], t[1], t[2], t[3],
             c[0] != 0, c[1] != 0, c[2] != 0, c[3] != 0, s[0], s[1], s[2], s[3],
             nh != 2 ? "not 2 humans set, wait" : t[hum[0]] != t[hum[1]] ? "humans already on opposite teams" : "humans on one team: swap");
    }
    if (nh != 2 || t[hum[0]] != t[hum[1]]) return;
    // FIX (swapped controls, testers on 0.7.0-test1): the builder 0x002EEE90 numbers the humans' net slots in placement order (team 0 first), so the
    // LOWER record (the owner, record 0) must end up on team 0: humans on team 1 move the owner to team 0, humans on team 0 move the other one to team 1.
    // (Moving record 1 to team 0 gave the joiner net slot 0, so each PC's pad drove the other's fighter; the rollback pad take maps net slots by owner now too,
    // jsp_native_fight_map, so this keeps the game's own netcode right as well.)
    const int h = t[hum[0]] == 1 ? hum[0] : hum[1];
    const uint8_t other = (uint8_t)(t[h] ^ 1);
    int com = -1;
    for (int i = 0; i < 4; i++) if (c[i] != 0 && t[i] == other) { com = i; break; }
    if (com < 0) return;
    vm_write8(R + 0x68u + 0x24u * (uint32_t)h + 0x16u, other);
    vm_write8(R + 0x68u + 0x24u * (uint32_t)com + 0x16u, t[h]);
    if (s[h] <= 1 && s[com] <= 1 && s[h] != s[com]) { vm_write8(R + 0x56u + (uint32_t)h, s[com]); vm_write8(R + 0x56u + (uint32_t)com, s[h]); }
    NLOG("[online] versus default applied: human record %d -> team %u, COM record %d -> team %u (room data %08X)\n", h, other, com, t[h], R);
}

/* function hooks (indirect calls only: the lift calls direct targets without the dispatcher) */
typedef void (*fn_t)(ppu_context*);
std::atomic<uint32_t> g_take_n{0}, g_take_ret[8], g_take_other{0}, g_stallchk_n{0}, g_stall2_n{0}, g_mode_obj{0};
std::atomic<uint32_t> g_take6_logged{0};
fn_t g_orig_take = nullptr, g_orig_stall = nullptr;

void drain(ppu_context* c) { while (g_trampoline_fn) { void (*f)(void*) = g_trampoline_fn; g_trampoline_fn = nullptr; f((void*)c); } }

/* 0x0F76C4: online mode vtable 0x878A70 slot 4; tail-calls 0x0F7234 (pop one 12-byte sample per battle player); r3 after it:
 * 1 ok, 2 inactive, 3 base not ready, 5 paused, 6 queue empty */
void take_wrap(ppu_context* c)
{
    const uint32_t mode = (uint32_t)c->gpr[3];
    const uint32_t prev_mode = g_mode_obj.exchange(mode);
    if (prev_mode != mode)
        NLOG("[ntrace] f=%u t=%.1f battle mode object %08X vtable %08X (%s)\n", flips(), secs(), mode, vm_read32(mode),
             vm_read32(mode) == 0x00878A70u ? "ONLINE 0x878A70" : vm_read32(mode) == 0x008789D0u ? "offline 0x8789D0" : "other");
    g_orig_take(c);
    drain(c);
    const uint32_t r = (uint32_t)c->gpr[3];
    g_take_n++;
    if (r < 8) g_take_ret[r]++; else g_take_other++;
    if (r == 6 && g_take6_logged++ < 20) {
        const uint32_t sp = share_pad_obj();
        NLOG("[ntrace] f=%u t=%.1f pad take returned 6 (queue empty): sync(6D4)=%u active(6D8)=%u stall(6E8)=%u mode+1C=%08X\n", flips(), secs(),
             sp ? vm_read32(sp + 0x6D4u) : 0u, sp ? vm_read32(sp + 0x6D8u) : 0u, sp ? vm_read32(sp + 0x6E8u) : 0u, vm_read32(mode + 0x1Cu));
    }
}
/* 0x0F7704: slot 2, calls 0x0F73E8 (pauses the battle while share pad +0x6D4 == 2) */
void stall_wrap(ppu_context* c)
{
    g_stallchk_n++;
    const uint32_t sp = share_pad_obj();
    if (sp && vm_read32(sp + 0x6D4u) == 2) g_stall2_n++;
    g_orig_stall(c);
    drain(c);
}

int icall(uint32_t target, uint64_t* gpr)
{
    const uint32_t r3 = (uint32_t)gpr[3];
    const unsigned long long t = now_ms();
    if (g_versus && (target == 0x002ECD30u || target == 0x00292090u)) versus_apply();   /* loading / room scene updates */
    if (target == 0x001DE098u) { g_main_obj = r3; g_main_ms = t; if (!g_menu_seen && plausible_obj(r3) && vm_read32(r3) == 0x0087D600u) g_menu_seen = true; }
    else if (target == 0x00085CECu) {
        const unsigned long long prev = g_hud_ms.exchange(t);
        if (g_trace && t - prev > 3000) {
            const uint32_t gm = vm_read32(0x008D3888u - 0x7A90u);
            const uint32_t gw = plausible_obj(gm) ? vm_read32(gm) : 0;
            NLOG("[ntrace] f=%u t=%.1f fight HUD 0x00085CEC first call (scene %X; p=*(TOC-0x7A90)=%08X p+4=%d *p=%08X *p+4=%d; mode obj %08X vt %08X); setup records 0x009540E4:\n",
                 flips(), secs(), scene_word(), gm, plausible_obj(gm) ? (int)vm_read32(gm + 4) : -1, gw, plausible_obj(gw) ? (int)vm_read32(gw + 4) : -1,
                 g_mode_obj.load(), g_mode_obj ? vm_read32(g_mode_obj) : 0u);
            for (int slot = 0; slot < 6; slot++) {
                char line[256]; int p = std::snprintf(line, sizeof line, "[ntrace]   slot %d:", slot);
                for (uint32_t o = 0; o < 0x2C; o += 4) p += std::snprintf(line + p, sizeof line - p, " %08X", vm_read32(0x009540E4u + 0x2Cu * slot + o));
                NLOG("%s\n", line);
            }
        }
    }
    else if (target == 0x0014EA68u) {
        const unsigned long long prev = g_scene_upd_ms.exchange(t);
        if (g_trace && t - prev > 3000) NLOG("[ntrace] f=%u t=%.1f scene update 0x0014EA68 first call (r3 %08X, scene %X)\n", flips(), secs(), r3, scene_word());
    }
    else if (target == 0x0004296Cu) { if (plausible_obj(r3) && vm_read32(r3 + 4) == 0) g_p1_moved = true; }

    const int nw = g_nwatch.load(std::memory_order_relaxed);
    if (nw) {
        uint32_t vt = 0; bool have_vt = false;
        for (int i = 0; i < nw; i++) {
            Watch& w = g_watch[i];
            if (!w.vt) { if (w.key == target) w.ms = t; continue; }
            if (!have_vt) { have_vt = true; vt = plausible_obj(r3) ? vm_read32(r3) : 0; }
            if (vt == w.key) w.ms = t;
        }
    }
    if (g_trace) {
        const bool net = net_target(target);
        if (net || g_menu_seen) {
            std::lock_guard<std::mutex> lk(g_mu);
            if (net) g_hist[target]++;
            if (g_menu_seen && g_new_logged < 6000 && g_seen_targets.insert(target).second) {
                g_new_logged++;
                const uint32_t vt = plausible_obj(r3) ? vm_read32(r3) : 0;
                NLOG("[ntrace] f=%u t=%.1f new target %08X r3=%08X vt=%08X scene=%X sub=%X%s\n", flips(), secs(), target, r3,
                     (vt >= 0x00800000u && vt < 0x00A00000u) ? vt : 0u, scene_word(), vm_read32(0x0095654Cu), net ? " NET" : "");
            } else if (!g_menu_seen) g_seen_targets.insert(target);
        } else {
            std::lock_guard<std::mutex> lk(g_mu);
            g_seen_targets.insert(target);
        }
        per_second_and_scene();
    }
    return g_prev_icall ? g_prev_icall(target, gpr) : 0;
}

/* the share-pad object: manager = *(*(TOC + 0x59F8)), share pad = *(manager + 0x40000 - 0x7020) (getters 0x451108 / 0x451114) */
uint32_t share_pad_obj()
{
    const uint32_t p = vm_read32(0x008D3888u + 0x59F8u);
    if (!plausible_obj(p)) return 0;
    const uint32_t mgr = vm_read32(p);
    if (!plausible_obj(mgr)) return 0;
    const uint32_t sp = vm_read32(mgr + 0x38FE0u);
    return plausible_obj(sp) ? sp : 0;
}

void per_second_and_scene()
{
    static std::atomic<unsigned long long> last{0};
    const unsigned long long t = now_ms();
    unsigned long long l = last.load();
    if (t - l < 1000 || !last.compare_exchange_strong(l, t)) {
        return;
    }
    static uint32_t ls = 0xFFFFFFFFu, lsub = 0xFFFFFFFFu;
    const uint32_t s = scene_word(), sub = vm_read32(0x0095654Cu);
    if (s != ls || sub != lsub) { ls = s; lsub = sub; NLOG("[ntrace] f=%u t=%.1f scene %X sub %X cursor %u\n", flips(), secs(), s, sub, vm_read32(0x00956550u)); }
    /* share pad fields */
    { static uint32_t lv[11]; static uint32_t lobj = 0;
      const uint32_t sp = share_pad_obj();
      if (sp) {
          uint32_t v[11]; bool ch = sp != lobj;
          for (int i = 0; i < 11; i++) { v[i] = vm_read32(sp + 0x6C8u + 4u * i); if (i != 0 && i != 8 && i != 10 && v[i] != lv[i]) ch = true; }
          if (ch || t - g_hud_ms < 1000) {   /* on a change (not counting the counters), and every second in a fight */
              NLOG("[ntrace] f=%u t=%.1f sharepad %08X sent-unready(6C8)=%u port(6CC)=%d started(6D0)=%u sync(6D4)=%u active(6D8)=%u startreq(6DC)=%u "
                   "myslot(6E0)=%d hsdone(6E4)=%u stall(6E8)=%u hsbegun(6EC)=%u resend(6F0)=%u members(+10)=%u\n", flips(), secs(), sp,
                   v[0], (int)v[1], v[2], v[3], v[4], v[5], (int)v[6], v[7], v[8], v[9], v[10], vm_read32(sp + 0x10u));
              std::memcpy(lv, v, sizeof v); lobj = sp;
          }
      } }
    /* the online battle mode's per-frame pad take (slot 4 0x0F76C4 -> 0x0F7234) and stall check (slot 2 0x0F7704 -> 0x0F73E8) */
    { const uint32_t tk = g_take_n.exchange(0), st = g_stallchk_n.exchange(0), st2 = g_stall2_n.exchange(0);
      if (tk || st) {
          char rl[160]; int p = 0;
          for (int i = 0; i < 8; i++) { const uint32_t n = g_take_ret[i].exchange(0); if (n) p += std::snprintf(rl + p, sizeof rl - p, " r%d:%u", i, n); }
          const uint32_t no = g_take_other.exchange(0);
          if (no) p += std::snprintf(rl + p, sizeof rl - p, " other:%u", no);
          NLOG("[ntrace] f=%u t=%.1f sec: pad take 0x0F7234 calls %u returns%s | stall check 0x0F7704 calls %u with sync==2 %u | mode %08X vt %08X\n",
               flips(), secs(), tk, rl, st, st2, g_mode_obj.load(), g_mode_obj ? vm_read32(g_mode_obj) : 0u);
      } }
    const uint32_t sn = g_snd_n.exchange(0), sb = g_snd_b.exchange(0), rn = g_rcv_n.exchange(0), rb = g_rcv_b.exchange(0);
    const uint32_t on = g_osnd_n.exchange(0), orr = g_orcv_n.exchange(0), se = g_snd_err.exchange(0);
    std::vector<std::pair<uint32_t, uint32_t>> h;
    { std::lock_guard<std::mutex> lk(g_mu); h.assign(g_hist.begin(), g_hist.end()); g_hist.clear(); }
    if (!sn && !rn && !on && !orr && h.empty()) return;
    std::sort(h.begin(), h.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    { /* quiet seconds (no packets, the same set of net targets as the last printed second) are not printed, except every 30 s */
      static std::vector<uint32_t> lastkeys; static unsigned long long lastp = 0;
      std::vector<uint32_t> keys; for (auto& e : h) keys.push_back(e.first);
      std::sort(keys.begin(), keys.end());
      const bool same = keys == lastkeys;
      lastkeys = keys;
      if (!sn && !rn && !on && !orr && same && t - lastp < 30000 && t - g_hud_ms > 1000) return;
      lastp = t; }
    char line[2048];
    int p = std::snprintf(line, sizeof line, "[ntrace] f=%u t=%.1f sec: p2p fd %d send %u (%u B, err %u) recv %u (%u B) other send %u recv %u | hud %d upd %d | net icalls:",
                          flips(), secs(), g_p2p_fd.load(), sn, sb, se, rn, rb, on, orr, (int)(t - g_hud_ms < 1000), (int)(t - g_scene_upd_ms < 1000));
    for (size_t i = 0; i < h.size() && i < 40 && p < (int)sizeof line - 24; i++) p += std::snprintf(line + p, sizeof line - p, " %X:%u", h[i].first, h[i].second);
    NLOG("%s\n", line);
}

/* ---- auto-pilot ---- */
std::vector<std::string> g_steps;
size_t g_step = 0;
unsigned long long g_step_t = 0, g_said_t = 0, g_press_t = 0, g_press_until = 0;
uint16_t g_button = 0;
unsigned g_every = 5000;
bool g_auto_on = false;
int g_sub = 0;   /* per-step sub state */
/* FIX (Discord auto-nav): the running script is the Discord one (not the AUTO_ONLINE test autopilot): it ends at g_nav_deadline, hands the pad
 * back on the player's own input (buttons not already held when it started, g_nav_held0; a stick once it was centred) and passes the player's
 * input through between its presses. */
bool g_discord_run = false, g_nav_stick_ok = false;
unsigned long long g_nav_deadline = 0;
uint16_t g_nav_held0 = 0;
const unsigned long long k_nav_limit_ms = 60000;

/* Main menu row 4 = Online Battle (submenu word 0x0095654C becomes 4); its list: Rank Match, Friendly Match (Down once), ...;
 * Friendly Match: Create a Room (first), Search for a Room (Down once). Host: Create a Room, Set Channel (Cross), then in the room
 * Cross marks it OK and then means "Go to battle". Join: Search for a Room; Cross every 5 s closes "No rooms ... found", searches
 * again, joins the first room of the list and marks OK. Both: until the fight HUD runs, then until Player 1 can move. Cross on
 * Online Battle repeats every 7 s until the submenu is up: after a killed online fight the game first shows "The last battle did
 * not end normally" (counted as a disconnect). */
const char* k_host_script =
    "menu battle row4 X~sub4@7000 w2500 D w1000 X w3000 X~tg2C619C@2500 w6000 X~fight ?p1 units end";
const char* k_join_script =
    "menu battle row4 X~sub4@7000 w2500 D w1000 X w4000 D w1000 X~fight ?p1 units end";

uint16_t button_of(char c)
{
    switch (c) {
    case 'X': return 0x4000; case 'O': return 0x2000; case 'T': return 0x1000; case 'Q': return 0x8000;
    case 'S': return 0x0008; case 'U': return 0x0010; case 'D': return 0x0040; case 'L': return 0x0080; case 'R': return 0x0020;
    default: return 0;
    }
}

bool main_list_open()
{
    const uint32_t o = g_main_obj;
    return now_ms() - g_main_ms < 300 && plausible_obj(o) && vm_read32(o) == 0x0087D600u && vm_read32(0x0095654Cu) == 0 &&
           scene_word() < 0xB && now_ms() - g_hud_ms > 500;
}

Watch* watch_for(const std::string& c, bool* neg, bool* known)
{
    *neg = false; *known = true;
    std::string s = c;
    if (!s.empty() && s[0] == '!') { *neg = true; s = s.substr(1); }
    if (s.compare(0, 2, "tg") == 0 || s.compare(0, 2, "vt") == 0) {
        const bool vt = s[0] == 'v';
        const uint32_t key = (uint32_t)std::strtoul(s.c_str() + 2, nullptr, 16);
        const int n = g_nwatch.load();
        for (int i = 0; i < n; i++) if (g_watch[i].key == key && g_watch[i].vt == vt) return &g_watch[i];
        if (n >= 32) return nullptr;
        g_watch[n].key = key; g_watch[n].vt = vt; g_watch[n].ms = 0;
        if (vt) g_watch_vt = true;
        g_nwatch = n + 1;
        return &g_watch[n];
    }
    *known = false;
    return nullptr;
}

bool cond(const std::string& c)
{
    bool neg, known;
    if (Watch* w = watch_for(c, &neg, &known)) { const bool up = now_ms() - w->ms < 500; return neg ? !up : up; }
    std::string s = c; bool n = false;
    if (!s.empty() && s[0] == '!') { n = true; s = s.substr(1); }
    bool r = false;
    if (s == "fight") r = now_ms() - g_hud_ms < 500;
    else if (s == "p1") r = g_p1_moved;
    else if (s == "menu") r = main_list_open();
    else if (s.compare(0, 5, "scene") == 0) r = scene_word() == (uint32_t)std::strtoul(s.c_str() + 5, nullptr, 16);
    else if (s.compare(0, 3, "sub") == 0) r = vm_read32(0x0095654Cu) == (uint32_t)std::strtoul(s.c_str() + 3, nullptr, 16);
    else if (s.compare(0, 4, "over") == 0) r = g_rb_overs >= std::atoi(s.c_str() + 4);   /* ENHANCEMENT (post-fight phase) */
    else if (s.compare(0, 3, "run") == 0) r = g_rb_runs >= std::atoi(s.c_str() + 3);
    return n ? !r : r;
}

void next_step(unsigned long long now)
{
    g_step++; g_sub = 0; g_step_t = g_said_t = now; g_press_t = 0;
    if (g_step < g_steps.size())
        NLOG("[auto-online] step %zu '%s' at %.1f s (f=%u scene %X sub %X)\n", g_step, g_steps[g_step].c_str(), secs(), flips(), scene_word(), vm_read32(0x0095654Cu));
}

void press(uint16_t b, unsigned long long now, unsigned hold) { g_button = b; g_press_until = now + hold; g_press_t = now; }

void auto_pad(uint16_t* b, uint16_t* lx, uint16_t* ly)
{
    const unsigned long long now = now_ms();
    if (g_discord_run) {   /* FIX: *b / *lx / *ly are still the player's real pad here (read before anything below writes them) */
        g_nav_held0 &= *b;   /* a button held at the start counts once it was let go */
        const bool stick_out = std::abs((int)*lx - 128) > 48 || std::abs((int)*ly - 128) > 48;
        if (!stick_out) g_nav_stick_ok = true;
        const char* why = (*b & ~g_nav_held0) || (stick_out && g_nav_stick_ok) ? "the player's input" : now >= g_nav_deadline ? "the 60 s limit" : nullptr;
        if (why) {
            g_auto_on = false; g_discord_run = false; g_press_until = 0;
            NLOG("[auto-online] discord auto-nav stopped at step %zu (%s) at %.1f s: pad handed back\n", g_step, why, secs());
            return;   /* the player's input as read */
        }
    }
    if (g_step >= g_steps.size()) { g_auto_on = false; g_discord_run = false; NLOG("[auto-online] script done at %.1f s\n", secs()); return; }
    if (now - g_said_t >= 30000) {
        g_said_t = now;
        NLOG("[auto-online] STALL step %zu '%s' for %.0f s (f=%u scene %X sub %X cursor %u menu %d fight %d)\n", g_step, g_steps[g_step].c_str(),
             (now - g_step_t) / 1000.0, flips(), scene_word(), vm_read32(0x0095654Cu), vm_read32(0x00956550u), (int)main_list_open(), (int)cond("fight"));
    }
    const std::string& s = g_steps[g_step];
    if (s == "end") { g_auto_on = false; g_discord_run = false; g_press_until = 0; NLOG("[auto-online] end at %.1f s: pad handed back\n", secs()); return; }
    if (s == "home") {   /* back to the main menu list from wherever the player is: Circle only (FIX: no Yes/No answers) */
        if (main_list_open()) { g_press_until = 0; next_step(now); }
        else if (now - g_step_t > 15000) {   /* FIX: a Yes/No box (or another screen) that Circle does not close: give up, the player decides */
            g_auto_on = false; g_discord_run = false; g_press_until = 0;
            NLOG("[auto-online] discord auto-nav: no main menu after 15 s of Circle (a Yes/No box or another screen in the way; scene %X sub %X): gave up, pad handed back\n",
                 scene_word(), vm_read32(0x0095654Cu));
            return;
        }
        else if (scene_word() < 4) { if (now - g_press_t >= 1000) { press(g_sub & 1 ? 0x0080 : 0x4000, now, 120); g_sub++; } }
        else if (now - g_press_t >= 1200) { press(0x2000, now, 120); g_sub++; }
    } else if (s == "J") {   /* the Discord join: Cross every AUTO_ONLINE_EVERY ms (search, close "no rooms", join the listed room) */
        uint64_t room = 0;
        const int st = jsp_discord_join_state(&room);
        static int last_st = 0;
        if (st != last_st) { NLOG("[auto-online] discord join state %d (room %llX) at %.1f s\n", st, (unsigned long long)room, secs()); last_st = st; }
        if (st == 0) {   /* FIX: the join was dropped (or the search is no longer pointed at its room): never press Cross on an unfiltered list */
            jsp_discord_join_done(0); NLOG("[auto-online] discord join: dropped (no join target): gave up, pad handed back\n");
            g_auto_on = false; g_discord_run = false; g_press_until = 0; return;
        }
        if (st == 3) { jsp_discord_join_done(1); NLOG("[auto-online] discord join: joined\n"); g_press_until = 0; next_step(now); }
        else if (st < 0 && g_sub == 0) { g_sub = 1; g_step_t = now; NLOG("[auto-online] discord join: state %d, searching once more\n", st); }
        else if ((st < 0 && g_sub == 1 && now - g_step_t > 12000) || now - g_step_t > 90000) {
            jsp_discord_join_done(0); NLOG("[auto-online] discord join: gave up (state %d)\n", st);
            g_auto_on = false; g_discord_run = false; g_press_until = 0; return;
        } else if (now - g_press_t >= g_every) press(0x4000, now, 150);
    } else if (s == "menu") {
        if (main_list_open()) { g_press_until = 0; next_step(now); }
        else if (now - g_t0 > 3000 && now - g_press_t >= 1000) { press(g_sub & 1 ? 0x0080 : 0x4000, now, 120); g_sub++; }
    } else if (s == "battle") {   /* FIX (two-page Main Menu): the Battle Modes page first (its rows 0..4 are the game's own rows 0..4) */
        if (!mp_on()) next_step(now);
        else if (mp_busy()) {}   /* the list slides between pages: wait */
        else if (mp_page_now() == 1) { if (main_list_open() && now - g_press_t > 400) next_step(now); }
        else if (main_list_open() && now - g_step_t > 400) {
            if (vm_read32(0x00956550u) != 0) { if (now - g_press_t >= 300) press(0x0010, now, 80); }   /* Up to Battle Modes (top row) */
            else if (now - g_press_t >= 1000) press(0x4000, now, 120);   /* Cross: feat_menu_pages.cpp turns the page (it sees the press, see pad()) */
        }
    } else if (s.compare(0, 3, "row") == 0) {
        const int row = std::atoi(s.c_str() + 3);
        if (main_list_open() && now - g_step_t > 400 && !(mp_on() && mp_busy())) {
            if ((int)vm_read32(0x00956550u) == row) { if (now - g_press_t > 400) next_step(now); }
            else if (now - g_press_t >= 300) press(0x0040, now, 80);
        }
    } else if (s == "units") {   /* log the battle world units (world = **(TOC-0x7A90), 0x94 each from +0x60) */
        const uint32_t w0 = vm_read32(0x008D3888u - 0x7A90u);
        const uint32_t world = plausible_obj(w0) ? vm_read32(w0) : 0;
        if (plausible_obj(world))
            for (uint32_t k = 0; k < 4; k++) {
                const uint32_t u = world + 0x60u + 0x94u * k;
                NLOG("[auto-online] unit %u: team %u ctrl %u netslot %d member %u\n", k, vm_read32(u + 0x0C), vm_read32(u + 0x20),
                     (int)vm_read32(u + 0x70), (unsigned)vm_read16(u + 0x78));
            }
        next_step(now);
    } else if (s == "p1reset") {   /* ENHANCEMENT (post-fight phase): ?p1 waits for the next fight's Player 1 */
        g_p1_moved = false;
        next_step(now);
    } else if (s == "room") {   /* log the Friendly Match room data (0x14C bytes at *(0x0095839C)) */
        const uint32_t R = vm_read32(0x0095839Cu);
        NLOG("[auto-online] room data %08X:\n", R);
        if (plausible_obj(R))
            for (uint32_t o = 0; o < 0x14C; o += 0x20) {
                char line[200]; int p = std::snprintf(line, sizeof line, "[auto-online]   +%03X:", o);
                for (uint32_t k = 0; k < 0x20 && o + k < 0x14C; k += 4) p += std::snprintf(line + p, sizeof line - p, " %08X", vm_read32(R + o + k));
                NLOG("%s\n", line);
            }
        next_step(now);
    } else if (s[0] == 'w' && s.size() > 1 && isdigit((unsigned char)s[1])) {
        if (now - g_step_t >= (unsigned long long)std::atoll(s.c_str() + 1)) next_step(now);
    } else if (s[0] == '?') {
        if (cond(s.substr(1))) next_step(now);
    } else if (s.size() > 2 && s[1] == '~') {
        /* B~COND@MS: press every MS ms instead of AUTO_ONLINE_EVERY */
        const size_t at = s.find('@');
        const unsigned every = at != std::string::npos ? (unsigned)std::atoi(s.c_str() + at + 1) : g_every;
        if (cond(s.substr(2, at == std::string::npos ? std::string::npos : at - 2))) { g_press_until = 0; next_step(now); }
        else if (now - g_press_t >= every) press(button_of(s[0]), now, 150);
    } else if (s.size() == 1 && button_of(s[0])) {
        if (g_sub == 0) { press(button_of(s[0]), now, 150); g_sub = 1; }
        else if (now - g_press_t >= 700) next_step(now);
    } else {
        NLOG("[auto-online] unknown step '%s', skipped\n", s.c_str());
        next_step(now);
    }
    if (g_discord_run) {   /* FIX: the player's input passes through between the scripted presses (never zeroed) */
        if (now < g_press_until) *b = g_button;
        return;
    }
    if (g_rb_phase == JSP_LOBBY_RUNNING) return;   /* ENHANCEMENT (post-fight phase): a running rollback match owns the fight's input: pad untouched */
    *b = now < g_press_until ? g_button : 0;
    *lx = 128; *ly = 128;
}

std::vector<std::string> split_steps(const std::string& script)
{
    std::vector<std::string> v;
    size_t i = 0;
    while (i < script.size()) {
        while (i < script.size() && script[i] == ' ') i++;
        size_t j = i;
        while (j < script.size() && script[j] != ' ') j++;
        if (j > i) v.push_back(script.substr(i, j - i));
        i = j;
    }
    return v;
}

/* JSP_DISCORD_AUTO_NAV (on unless 0): a Discord Join (jsp_discord.h) drives the menus from wherever the player is to Friendly Match >
 * Search for a Room, joins the one room the search lists (the runtime's join target, patch 7924) and hands the pad back. */
bool g_discord_nav = false;
const char* k_discord_script = "home battle row4 X~sub4@7000 w2500 D w1000 X w4000 D w1000 J end";
void discord_poll(unsigned long long now, uint16_t held, bool stick_centred)
{
    static unsigned long long last = 0;
    if (now - last < 250) return;
    last = now;
    if (now - g_hud_ms < 2000) return;   /* not in a fight: the request waits */
    uint64_t room = 0; char srv[128] = "";
    if (!jsp_discord_take_join(&room, srv, (int)sizeof srv)) return;
    if (!g_t0) g_t0 = now;
    g_steps = split_steps(k_discord_script);
    g_step = 0; g_sub = 0; g_step_t = g_said_t = now; g_press_t = 0; g_press_until = 0;
    g_auto_on = true;
    g_discord_run = true; g_nav_deadline = now + k_nav_limit_ms; g_nav_held0 = held; g_nav_stick_ok = stick_centred;   /* FIX: deadline and give-back */
    NLOG("[auto-online] discord join request (room %llX on %s): %s\n", (unsigned long long)room, srv, k_discord_script);
}

/* FIX (two-page Main Menu): the scripted presses are written BEFORE the filters this one chains behind (jsp_game.cpp's game_pad_filter with
 * feat_menu_pages.cpp's mp_pad, and the rest), so the port's menu code sees them as it sees a player's. Written after them (as before), a
 * Cross on the top page's Battle Modes row reached the game as its row 0 (J-Adventure), and Down to row 4 there opened Online Battle under a
 * Port Settings / Options highlight. The player's pad that auto_pad reads to hand the pad back is now the raw one (no filter ate a press). */
void pad(uint32_t port, uint16_t* b, uint16_t* lx, uint16_t* ly)
{
    if (port == 0) {
        if (g_role) rb_phase_watch();   /* ENHANCEMENT (post-fight phase): the over / run counts of the AUTO_ONLINE script */
        if (!g_auto_on && g_discord_nav) discord_poll(now_ms(), *b, std::abs((int)*lx - 128) <= 48 && std::abs((int)*ly - 128) <= 48);
        if (g_auto_on) auto_pad(b, lx, ly);
    }
    if (g_prev_pad) g_prev_pad(port, b, lx, ly);
}

}  // namespace

/* Called from stubs.cpp after the other pad filters and the icall hook are set (it chains behind them). */
extern "C" void jsp_register_native_online(void)
{
    const char* tr = std::getenv("JSP_NATIVE_TRACE");
    g_trace = tr && *tr && tr[0] != '0';
    const char* ao = std::getenv("AUTO_ONLINE");
    if (ao && !std::strcmp(ao, "host")) g_role = 1;
    else if (ao && !std::strcmp(ao, "join")) g_role = 2;
    {   /* Discord auto-nav: on by default, only when the game's own online is on and Discord is not turned off */
        const char* dn = std::getenv("JSP_DISCORD_AUTO_NAV");
        const char* dd = std::getenv("JSP_DISCORD");
        g_discord_nav = !(dn && dn[0] == '0') && !(dd && dd[0] == '0') && std::getenv("PS3_NET_ONLINE") != nullptr;
    }
    {   /* players-versus safeguard: ON by default when the game's own online is on (JSP_ONLINE_VERSUS_DEFAULT=0 turns it off). FIX (2026-11-21):
         * runs native-rm5 and native-gin3 put both humans on one team against two COMs in every match (4 of 4), so the swap is needed; the
         * runs native-vs1/vs2 had them apart by chance. */
        const char* vd = std::getenv("JSP_ONLINE_VERSUS_DEFAULT");
        g_versus = !(vd && vd[0] == '0') && std::getenv("PS3_NET_ONLINE") != nullptr;
        if (g_versus) NLOG("[online] versus safeguard on: 2 humans in a Friendly Match fight each other, each with a COM partner (JSP_ONLINE_VERSUS_DEFAULT=0 turns it off)\n");
    }
    if (!g_trace && !g_role && !g_discord_nav && !g_versus) return;
    g_t0 = now_ms();
    if (g_trace) {
        if (std::getenv("PS3_NET_ONLINE")) {
            ps3_hle_register_ctx(ps3_compute_nid("socket"), "socket (native trace)", h_socket);
            ps3_hle_register_ctx(ps3_compute_nid("socketclose"), "socketclose (native trace)", h_close);
            ps3_hle_register_ctx(ps3_compute_nid("bind"), "bind (native trace)", h_bind);
            ps3_hle_register_ctx(ps3_compute_nid("sendto"), "sendto (native trace)", h_sendto);
            ps3_hle_register_ctx(ps3_compute_nid("recvfrom"), "recvfrom (native trace)", h_recvfrom);
            NLOG("[ntrace] on: sys_net socket/bind/close/sendto/recvfrom traced\n");
        } else NLOG("[ntrace] on, but PS3_NET_ONLINE is not set: the game's online is off, no socket trace\n");
        const bool a = jsp_hook_chain::install<fn_t>(0x000F76C4u, take_wrap, &g_orig_take);
        const bool b = jsp_hook_chain::install<fn_t>(0x000F7704u, stall_wrap, &g_orig_stall);
        NLOG("[ntrace] hooks: pad take 0x0F76C4 %s, stall check 0x0F7704 %s\n", a ? "on" : "MISSING", b ? "on" : "MISSING");
    }
    g_prev_icall = g_ppu_icall_hook;
    g_ppu_icall_hook = icall;
    if (g_role) {
        const char* sc = std::getenv("AUTO_ONLINE_SCRIPT");
        std::string script = (sc && *sc) ? sc : (g_role == 1 ? k_host_script : k_join_script);
        g_steps = split_steps(script);
        if (const char* ev = std::getenv("AUTO_ONLINE_EVERY")) if (std::atoi(ev) > 100) g_every = (unsigned)std::atoi(ev);
        for (const auto& s : g_steps) {   /* FIX: a B~COND@MS step registers COND without its @MS (the watch key would otherwise be parsed with it) */
            bool n, k;
            if (s.size() > 2 && s[1] == '~') { const size_t at = s.find('@'); watch_for(s.substr(2, at == std::string::npos ? std::string::npos : at - 2), &n, &k); }
            else if (s[0] == '?') watch_for(s.substr(1), &n, &k);
        }
        g_auto_on = !g_steps.empty();
        g_step_t = g_said_t = g_t0;
        NLOG("[auto-online] %s: %zu steps: %s\n", g_role == 1 ? "host" : "join", g_steps.size(), script.c_str());
        if (!g_steps.empty()) NLOG("[auto-online] step 0 '%s' at 0.0 s\n", g_steps[0].c_str());
    }
    if (g_role || g_discord_nav) {
        g_prev_pad = g_cellpad_filter;
        g_cellpad_filter = pad;
        if (g_discord_nav) NLOG("[auto-online] Discord Join auto-nav on (JSP_DISCORD_AUTO_NAV=0 turns it off)\n");
    }
}
