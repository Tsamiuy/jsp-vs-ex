/*
 * jsp_hle.cpp -- single-call HLE overrides J-Stars needs on ps3recomp v0.18.0.
 *
 * After this repository's v0.15 port (jstars/port/stubs.cpp: l10n_ucs2_to_utf8, netctl_get_state,
 * sysutil_get_bgm_status2; jsp-vs-ex, Tsamiuy, MIT). Each one is still missing or answers differently from
 * hardware in v0.18.0 (0xA9A76FB8 and 0x6CFD856F unresolved, GetState NOT_INITIALIZED every frame
 * after the opening movie although the game called cellNetCtlInit).
 */

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif

#include "ps3emu/ps3types.h"
#include "ps3emu/error_codes.h"
#include "ppu_recomp.h"     /* ppu_context, vm_write8/32 */

extern "C" void ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*));

namespace {

/* cellL10n UCS2toUTF8 (0xA9A76FB8). SDK: int32_t UCS2toUTF8(uint16_t ucs2, uint8_t* utf8): one code unit in, its
 * 1-3 UTF-8 bytes written to utf8, byte count returned. v0.18.0's libs/font/cellL10n.c has a buffer-to-buffer
 * function of that name and does not register it, so the import is unresolved. */
void l10n_ucs2_to_utf8(ppu_context* ctx)
{
    const uint32_t c = (uint32_t)ctx->gpr[3] & 0xFFFFu, dst = (uint32_t)ctx->gpr[4];
    uint8_t b[3];
    int n;
    if (c < 0x80)       { b[0] = (uint8_t)c; n = 1; }
    else if (c < 0x800) { b[0] = (uint8_t)(0xC0 | (c >> 6)); b[1] = (uint8_t)(0x80 | (c & 0x3F)); n = 2; }
    else                { b[0] = (uint8_t)(0xE0 | (c >> 12)); b[1] = (uint8_t)(0x80 | ((c >> 6) & 0x3F));
                          b[2] = (uint8_t)(0x80 | (c & 0x3F)); n = 3; }
    if (dst) for (int i = 0; i < n; i++) vm_write8(dst + i, b[i]);
    ctx->gpr[3] = (uint64_t)n;
}

/* cellNetCtlGetState (0x8B3EBA69). v0.18.0 answers an offline GetState with CELL_NET_CTL_ERROR_NOT_INITIALIZED
 * (its LittleBigPlanet workaround). A console with no network, after cellNetCtlInit, returns CELL_OK and
 * state = Disconnected (0); J-Stars polls it every frame after the opening movie. Report the hardware result.
 * Online (PS3_NET_ONLINE, the runtime's switch) the console has its IP: IPObtained (3). The game's net manager
 * (func_004561CC @0x004561CC) stores state > 1 as "connected", and Online Battle raises "You have lost connection
 * to PSN" (error 0x2A, @0x004812EC) when it reads 0 there during an online session. */
extern "C" int ps3_netctl_online(void);   /* libs/network/cellNetCtl.c */
void netctl_get_state(ppu_context* ctx)
{
    const uint32_t state_ea = (uint32_t)ctx->gpr[3];
    if (!state_ea) { ctx->gpr[3] = (uint64_t)(int64_t)(int32_t)0x80130106u; return; }  /* INVALID_ADDR */
    const int online = ps3_netctl_online();
    vm_write32(state_ea, online ? 3u : 0u);   /* CELL_NET_CTL_STATE_IPObtained : CELL_NET_CTL_STATE_Disconnected */
    static int n = 0;
    if (n++ < 3) std::printf("[jsp-net] cellNetCtlGetState -> CELL_OK, %s\n", online ? "IPObtained" : "Disconnected");
    ctx->gpr[3] = CELL_OK;
}

/* cellSysutilGetBgmPlaybackStatus2 (0x6CFD856F), unresolved in v0.18.0: the game read whatever the out-struct held
 * as the XMB music-player state. No user BGM plays: CellSysutilBgmPlaybackStatus2 { u8 playerState (0 = stopped);
 * u8 reserved[7]; }. */
void sysutil_get_bgm_status2(ppu_context* ctx)
{
    const uint32_t st = (uint32_t)ctx->gpr[3];
    if (!st) { ctx->gpr[3] = (uint64_t)(int64_t)(int32_t)0x8002B102u; return; }  /* CELL_SYSUTIL_ERROR_VALUE */
    for (int i = 0; i < 8; i++) vm_write8(st + i, 0);
    ctx->gpr[3] = CELL_OK;
}

/* JSP_THREADDUMP_MS=<ms>: every <ms> print "[jsp-diag]" and each guest thread's last direct call site (lr), r3 and
 * sp (runtime ppu_report_guest_lrs, ppu_loader.cpp). Diagnostic for what each thread does after the opening movie;
 * pair with BCTRL_RING=1 for indirect call targets. Off unless the variable is set. */
extern "C" void ppu_report_guest_lrs(void);
extern "C" uint32_t cellGcm_flip_request_count(void);   /* flip clock, same as PAD_CLOCK=flips */
extern "C" ppu_context* ppu_resv_ctx_at(long i);   /* patch 6001 */
extern "C" const char* g_hle_inflight[];           /* ppu_hle.cpp, 64 entries indexed by guest thread id */
extern "C" const char* g_hle_last[];               /* patch 6002: last HLE call of the thread, finished or not */

/* Guest call chain of every thread: r1 back chain, the caller's LR saved at back+0x10 (PPC64 ELFv1). Racy reads of
 * running threads, diagnostic only. */
void report_guest_stacks(unsigned depth)
{
    for (long i = 0; ppu_context* c = ppu_resv_ctx_at(i); i++) {
        const unsigned t = (unsigned)c->thread_id;
        const char* in = t < 64 ? g_hle_inflight[t] : nullptr;   /* HLE handler the thread is inside, if any */
        const char* last = t < 64 ? g_hle_last[t] : nullptr;
        std::fprintf(stderr, "[jsp-stack] tid %-3u in=%-28s last=%-28s lr=%08X |", t, in ? in : "-", last ? last : "-",
                     (uint32_t)c->lr);
        uint32_t sp = (uint32_t)c->gpr[1];
        for (unsigned d = 0; d < depth && sp >= 0x10000; d++) {
            const uint32_t back = (uint32_t)vm_read64(sp);
            if (back <= sp || back - sp > 0x100000u) break;
            std::fprintf(stderr, " %08X", (uint32_t)vm_read64(back + 0x10));
            sp = back;
        }
        std::fprintf(stderr, "\n");
    }
}

#ifdef _WIN32
DWORD WINAPI thread_dump_main(LPVOID arg)
{
    long v = (long)(uintptr_t)arg;
    if (v <= 0) {
        /* Switch file for runs that cannot set the environment (a fixed run command):
         * <hdd0 folder>/../boot/threaddump_ms holds the interval. */
        Sleep(2000);
        const char* h = std::getenv("PS3_HDD0_ROOT");
        if (!h) return 0;
        char path[1200];
        std::snprintf(path, sizeof path, "%s/../boot/threaddump_ms", h);
        FILE* f = std::fopen(path, "r");
        if (!f) return 0;
        if (std::fscanf(f, "%ld", &v) != 1) v = 0;
        std::fclose(f);
        if (v <= 0) return 0;
    }
    const DWORD ms = (DWORD)v;
    /* Optional second number in the switch file: guest address of a struct to dump (0x74 bytes of words) and the
     * 0x20-byte object its +0x20 word points to, each dump. Used for the SpMovie thread state. */
    unsigned long dump_ea = 0;
    {
        const char* h = std::getenv("PS3_HDD0_ROOT");
        char path[1200];
        std::snprintf(path, sizeof path, "%s/../boot/threaddump_ms", h ? h : ".");
        if (FILE* f = std::fopen(path, "r")) {
            long skip;
            if (std::fscanf(f, "%ld %lx", &skip, &dump_ea) != 2) dump_ea = 0;
            std::fclose(f);
        }
    }
    for (unsigned n = 0;; n++) {
        Sleep(ms);
        std::fprintf(stderr, "[jsp-diag] thread dump #%u flips=%u\n", n, (unsigned)cellGcm_flip_request_count());
        report_guest_stacks(10);
        /* Scene probe (FINDINGS 12 of the v0.15 notes): registry *(0x95BD9C), resource manager *(0x95BBF4),
         * scene objects = guest words equal to the scene vtable 0x8822C8 (scan once, 16-byte aligned, below 0x20000000);
         * scene state byte-word at +0x3C (func_00444088: 0 -> 1 wait -> 2 -> 3 -> 4 done). */
        {
            static uint32_t scenes[8];
            static unsigned nscenes = 0;
            /* The scan found no object with vtable word 0x8822C8 at +0: the scene object r3 seen by
             * the tracer (0x444088) is used instead; add its address to the switch file's second number. */
            if (dump_ea && nscenes == 0) scenes[nscenes++] = (uint32_t)dump_ea;
            std::fprintf(stderr, "[jsp-scene] registry=%08X resmgr=%08X reg+58=%08X reg+64=%08X scenes=%u",
                         (unsigned)vm_read32(0x95BD9C), (unsigned)vm_read32(0x95BBF4),
                         (unsigned)vm_read32(vm_read32(0x95BD9C) + 0x58), (unsigned)vm_read32(vm_read32(0x95BD9C) + 0x64),
                         nscenes);
            for (unsigned i = 0; i < nscenes; i++)
                std::fprintf(stderr, " [%08X +3C=%u]", (unsigned)scenes[i], (unsigned)vm_read32(scenes[i] + 0x3C));
            std::fprintf(stderr, "\n");
        }
        if (dump_ea) {
            std::fprintf(stderr, "[jsp-mem] %08lX:", dump_ea);
            for (unsigned o = 0; o < 0x74; o += 4) std::fprintf(stderr, " %08X", (unsigned)vm_read32(dump_ea + o));
            const uint32_t obj = vm_read32(dump_ea + 0x20);
            std::fprintf(stderr, "\n[jsp-mem] obj %08X:", obj);
            for (unsigned o = 0; o < 0x20; o += 4) std::fprintf(stderr, " %08X", (unsigned)vm_read32(obj + o));
            std::fprintf(stderr, "\n");
        }
    }
    return 0;
}
#endif

/* ENHANCEMENT (test only, off by default): JSP_SHOT_AT_FLIPS=a,b,c writes shot_<n>.req into the working folder when the
 * flip clock first reaches each value; a test harness that watches the working folder answers with a window grab.
 * Flips are the deterministic clock the pad script uses (PAD_CLOCK=flips). */
DWORD WINAPI shot_at_flips_main(LPVOID arg)
{
    static unsigned list[64]; unsigned n = 0;
    for (const char* p = (const char*)arg; p && *p && n < 64;) {
        list[n++] = (unsigned)std::strtoul(p, (char**)&p, 10);
        if (*p == ',') p++; else if (*p) break;
    }
    for (unsigned i = 0; i < n;) {
        Sleep(20);
        const unsigned f = cellGcm_flip_request_count();
        if (f < list[i]) continue;
        char name[64]; std::snprintf(name, sizeof name, "shot_%u.req", list[i]);
        if (FILE* q = std::fopen(name, "w")) { std::fprintf(q, "%u\n", f); std::fclose(q); }
        std::fprintf(stderr, "[jsp-shot] flips=%u requested %u\n", f, list[i]);
        i++;
    }
    return 0;
}

void start_thread_dump(void)
{
#ifdef _WIN32
    if (const char* sf = std::getenv("JSP_SHOT_AT_FLIPS"))
        if (*sf) CreateThread(nullptr, 0, shot_at_flips_main, (LPVOID)sf, 0, nullptr);
    const char* e = std::getenv("JSP_THREADDUMP_MS");
    const long ms = (e && *e) ? std::atol(e) : 0;
    CreateThread(nullptr, 0, thread_dump_main, (LPVOID)(uintptr_t)ms, 0, nullptr);
#endif
}

/* Guest-function call tracer. Present only when <hdd0 folder>/../boot/trace_funcs exists (diagnostic, off otherwise):
 * wraps the lifted function at each listed address in the function table, logs the first 30 calls and then every
 * 256th with r3..r6, the caller's lr and, for a scene object in r3, its state word +0x3C before and after.
 * Addresses: 0x444088 scene state machine, 0x2F01FC scene query, 0x42EDC8 character-image queue, 0x3EFA20 search
 * object init, 0x3EF680 search callback (the v0.15 notes FINDINGS 12/14). */
typedef void (*ppu_fn_t)(ppu_context*);
extern "C" void ppu_register_function(uint64_t addr, ppu_fn_t fn);

template <int I> struct fn_trace {
    static ppu_fn_t orig;
    static uint64_t addr;
    static const char* name;
    static unsigned n;
    static void wrap(ppu_context* c)
    {
        const unsigned k = n++;
        const bool log = k < 30 || (k & 255) == 0;
        const uint32_t r3 = (uint32_t)c->gpr[3];
        const bool sm = addr == 0x444088;
        const uint32_t before = sm && r3 ? vm_read32(r3 + 0x3C) : 0;
        if (log)
            std::fprintf(stderr, "[jsp-fn] %s #%u tid=%u r3=%08X r4=%08X r5=%08X r6=%08X lr=%08X%s", name, k,
                         (unsigned)c->thread_id, r3, (uint32_t)c->gpr[4], (uint32_t)c->gpr[5], (uint32_t)c->gpr[6],
                         (uint32_t)c->lr, sm ? "" : "\n");
        orig(c);
        if (log && sm)
            std::fprintf(stderr, " state %u -> %u ret=%08X\n", (unsigned)before, (unsigned)vm_read32(r3 + 0x3C),
                         (uint32_t)c->gpr[3]);
    }
};
template <int I> ppu_fn_t fn_trace<I>::orig;
template <int I> uint64_t fn_trace<I>::addr;
template <int I> const char* fn_trace<I>::name;
template <int I> unsigned fn_trace<I>::n;

template <int I> void install_trace(uint64_t a)
{
    for (uint64_t i = 0; i < function_table_count; i++)
        if (function_table[i].addr == a) {
            fn_trace<I>::orig = function_table[i].func;
            fn_trace<I>::addr = a;
            fn_trace<I>::name = function_table[i].name;
            ppu_register_function(a, fn_trace<I>::wrap);
            return;
        }
    std::fprintf(stderr, "[jsp-fn] no lifted function at %08llX\n", (unsigned long long)a);
}

void install_traces(void)
{
    const char* h = std::getenv("PS3_HDD0_ROOT");
    if (!h) return;
    char path[1200];
    std::snprintf(path, sizeof path, "%s/../boot/trace_funcs", h);
    FILE* f = std::fopen(path, "r");
    if (!f) return;
    std::fclose(f);
    install_trace<0>(0x444088);
    install_trace<1>(0x2F01FC);
    install_trace<2>(0x42EDC8);
    install_trace<3>(0x3EFA20);
    install_trace<4>(0x3EF680);
}

}  // namespace

extern "C" void jsp_register_hle_overrides(void)
{
    install_traces();
    start_thread_dump();
    ps3_hle_register_ctx(0xA9A76FB8u, "UCS2toUTF8", l10n_ucs2_to_utf8);
    ps3_hle_register_ctx(0x8B3EBA69u, "cellNetCtlGetState", netctl_get_state);
    ps3_hle_register_ctx(0x6CFD856Fu, "cellSysutilGetBgmPlaybackStatus2", sysutil_get_bgm_status2);
}
