/*
 * jstars_debug.cpp -- PS3_DEBUG debug console for the J-Stars port.
 *
 * A trimmed copy of the console in runtime/ppu/tests/boot_main.cpp, which the
 * project template's main.cpp does not carry. Set PS3_DEBUG=<path>; write one
 * command into <path> and the answer is appended to <path>.out (and stderr):
 *
 *   threads          every thread: guest threads inside an HLE, guest lr per
 *                    thread, and each host thread's lifted call chain
 *   stat             uptime, flip count, last HLE call
 *   hle              last HLE call
 *   mem <hex> [len]  hexdump of guest memory
 *   help
 *
 * See docs/DEBUG_CONSOLE.md for the full version and why it is a file.
 */

#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include "ppu_recomp.h"   /* vm_base, function_table */

extern "C" uint32_t    g_last_hle_nid;      /* ppu_hle.cpp breadcrumb */
extern "C" const char* g_last_hle_name;
extern "C" const char* g_hle_inflight[];    /* ppu_hle.cpp; 64 entries */
extern "C" void        ppu_report_guest_lrs(void);   /* ppu_loader.cpp */
extern "C" volatile uint32_t jstars_dbg[16];          /* JSTARS-DIAG4 counters, defined below */
extern "C" unsigned    cellGcm_flip_request_count(void);
extern "C" void        rsx_d3d12_cap_now(void);              /* rsx_d3d12_backend.c */

namespace {

FILE* s_out = nullptr;

/* While a thread is suspended, format into this buffer instead of a stream:
 * the suspended thread may hold the CRT's stderr lock. */
char   s_capbuf[16384];
char*  s_cap = nullptr;
size_t s_capn = 0;

void dbg_printf(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (s_cap) {
        int n = vsnprintf(s_cap + s_capn, sizeof s_capbuf - s_capn, fmt, ap);
        if (n > 0) s_capn += (size_t)n;
        if (s_capn >= sizeof s_capbuf) s_capn = sizeof s_capbuf - 1;
        va_end(ap);
        return;
    }
    if (s_out) { va_list c; va_copy(c, ap); vfprintf(s_out, fmt, c); va_end(c); }
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* The lifted function containing host address v, or 0 when it is not inside
 * one (runtime code, CRT, or past the last lifted function). */
uint32_t guest_func_for(uintptr_t v, uintptr_t* off)
{
    uint32_t best = 0;
    uintptr_t bestHost = 0, maxHost = 0;
    for (uint64_t q = 0; q < function_table_count; q++) {
        uintptr_t h = (uintptr_t)function_table[q].func;
        if (h > maxHost) maxHost = h;
        if (h <= v && h > bestHost) { bestHost = h; best = (uint32_t)function_table[q].addr; }
    }
    if (bestHost == maxHost || v - bestHost > 0x4000u) return 0;
    *off = v - bestHost;
    return best;
}

void dump_threads(HMODULE self)
{
    dbg_printf("[dbg] last HLE call = 0x%08X (%s)\n",
               g_last_hle_nid, g_last_hle_name ? g_last_hle_name : "");
    int any = 0;
    for (unsigned t = 0; t < 64u; t++) {
        const char* n = g_hle_inflight[t];
        if (!n) continue;
        if (!any) { dbg_printf("[dbg] guest threads inside an HLE:\n"); any = 1; }
        dbg_printf("[dbg]   guest-tid %-3u in %s\n", t, n);
    }
    if (!any) dbg_printf("[dbg] no guest thread is inside an HLE\n");
    dbg_printf("[dbg] guest threads, by guest lr:\n");
    ppu_report_guest_lrs();

    DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te; te.dwSize = sizeof te;
    if (snap == INVALID_HANDLE_VALUE) return;
    if (Thread32First(snap, &te)) do {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
        HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!th) continue;
        s_capn = 0; s_capbuf[0] = 0; s_cap = s_capbuf;
        SuspendThread(th);
        CONTEXT ctx; ctx.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(th, &ctx)) {
            HMODULE m = NULL;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)ctx.Rip, &m);
            if (m == self) {
                dbg_printf("[dbg]   tid %5lu in exe rva=0x%llX\n", (unsigned long)te.th32ThreadID,
                           (unsigned long long)((char*)ctx.Rip - (char*)self));
            } else {
                char path[MAX_PATH] = "?";
                if (m) GetModuleFileNameA(m, path, sizeof path);
                const char* base = strrchr(path, '\\');
                dbg_printf("[dbg]   tid %5lu in %s\n", (unsigned long)te.th32ThreadID, base ? base + 1 : path);
            }
            /* Scan the stack for return addresses inside lifted functions:
             * the guest call chain, innermost first (stack-scan hits, so a
             * few false positives are expected). */
            uint64_t* sp = (uint64_t*)ctx.Rsp;
            MEMORY_BASIC_INFORMATION mbi;
            uint64_t region_end = (uint64_t)sp;
            if (VirtualQuery((LPCVOID)sp, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT &&
                !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
                (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
                region_end = (uint64_t)mbi.BaseAddress + mbi.RegionSize;
            int maxk = (int)((region_end - (uint64_t)sp) / 8);
            if (maxk > 0x200000 / 8) maxk = 0x200000 / 8;
            int found = 0;
            for (int k = 0; k < maxk && found < 24; k++) {
                uint64_t v = sp[k];
                if (v < (uint64_t)self) continue;
                HMODULE mm = NULL;
                GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)v, &mm);
                if (mm != self) continue;
                uintptr_t off = 0;
                uint32_t g = guest_func_for((uintptr_t)v, &off);
                if (!g) continue;
                dbg_printf("[dbg]       func_%08X+0x%llX\n", g, (unsigned long long)off);
                found++;
            }
        }
        ResumeThread(th);
        s_cap = nullptr;
        if (s_capn) dbg_printf("%s", s_capbuf);
        CloseHandle(th);
    } while (Thread32Next(snap, &te));
    CloseHandle(snap);
}

void dump_mem(uint32_t ea, uint32_t len)
{
    if (!vm_base) { dbg_printf("  vm_base not live yet\n"); return; }
    if (len == 0 || len > 4096) len = 64;
    for (uint32_t off = 0; off < len; off += 16) {
        char asc[17];
        dbg_printf("  0x%08X ", ea + off);
        for (uint32_t i = 0; i < 16; i++) {
            if (off + i < len) {
                uint8_t b = vm_base[(size_t)(ea + off + i)];
                dbg_printf("%02X ", b);
                asc[i] = (b >= 32 && b < 127) ? (char)b : '.';
            } else { dbg_printf("   "); asc[i] = ' '; }
        }
        asc[16] = 0;
        dbg_printf(" |%s|\n", asc);
    }
}

/* "res": the post-movie resource-wait state (FINDINGS §12). Walks
 * the chain a reviewer mapped: resource registry -> containers (+0x58 character images,
 * +0x64 second container) -> handles H (busy +0x10/+0x14) -> file tasks T (state
 * +0x24, 4 = done); plus the boot loader and file server flags and the counters
 * the JSTARS-DIAG4 hooks in the lifted code maintain. */
inline bool plausible(uint32_t ea) { return ea >= 0x10000u && ea < 0xE0000000u; }
inline uint32_t r32(uint32_t ea) { return plausible(ea) ? vm_read32(ea) : 0xDEADDEADu; }

void dump_resources()
{
    dbg_printf("  scene=0x%08X busy(+0xA0)=%u polls=%u | task-done calls=%u (last task 0x%08X) | "
               "res-ack calls=%u\n", jstars_dbg[0], jstars_dbg[1], jstars_dbg[2],
               jstars_dbg[3], jstars_dbg[4], jstars_dbg[5]);
    dbg_printf("  func_00444088 calls=%u last scene=0x%08X state-at-entry=%u flags(+0x38)=0x%X | "
               "busy site 2 polls=%u last=%u\n", jstars_dbg[6], jstars_dbg[7], jstars_dbg[8],
               jstars_dbg[11], jstars_dbg[9], jstars_dbg[10]);
    uint32_t scene = jstars_dbg[0];
    if (plausible(scene))
        dbg_printf("  scene+0x3C state=%u  +0x40=0x%08X\n", r32(scene + 0x3C), r32(scene + 0x40));
    uint32_t reg = r32(0x0095BD9C), mgr = r32(0x0095BBF4);
    dbg_printf("  registry=0x%08X  resource manager=0x%08X\n", reg, mgr);
    for (uint32_t off : { 0x58u, 0x64u }) {
        uint32_t R = r32(reg + off);
        if (!plausible(R)) { dbg_printf("  registry+0x%02X = 0x%08X (no container)\n", off, R); continue; }
        uint32_t base = r32(R + 4);
        dbg_printf("  registry+0x%02X container R=0x%08X vtbl=0x%08X R+0x28=%u handles@0x%08X\n",
                   off, R, r32(R), r32(R + 0x28), base);
        for (uint32_t i = 0; i < 4 && plausible(base); i++) {
            uint32_t H = r32(base + 8 * i);
            if (!plausible(H)) break;
            uint32_t T = r32(H + 8);
            dbg_printf("    slot %u: H=0x%08X H+0x10=%u H+0x14=%u  T=0x%08X T+0x24=%u T+0x1C=0x%X T+0x20=0x%X\n",
                       i, H, r32(H + 0x10), r32(H + 0x14), T, r32(T + 0x24), r32(T + 0x1C), r32(T + 0x20));
        }
    }
    dbg_printf("  boot loader sm 0x48E06C90: state=%u task=0x%08X | file server +0x40=%u +0x4C=%u\n",
               r32(0x48E06C94), r32(0x48E06C98), r32(0x48E07510), r32(0x48E0751C));
}

/* "dumpall <file>": every committed, readable page of guest memory, as records of
 * [u32 guest address][u32 size][bytes] (little-endian header). For offline searches
 * (decompressed game files, strings, textures). */
void dump_all(const char* path)
{
    if (!vm_base) { dbg_printf("  vm_base not live yet\n"); return; }
    FILE* f = fopen(path, "wb");
    if (!f) { dbg_printf("  cannot open '%s'\n", path); return; }
    unsigned long long total = 0; unsigned regions = 0;
    uint64_t ea = 0;
    while (ea < 0x100000000ull) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery(vm_base + ea, &mi, sizeof mi)) break;
        uint64_t end = (uint64_t)((uint8_t*)mi.BaseAddress - vm_base) + mi.RegionSize;
        if (end > 0x100000000ull) end = 0x100000000ull;
        const DWORD rd = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                         PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
        if (mi.State == MEM_COMMIT && (mi.Protect & rd) && !(mi.Protect & PAGE_GUARD)) {
            uint32_t hdr[2] = { (uint32_t)ea, (uint32_t)(end - ea) };
            fwrite(hdr, sizeof hdr, 1, f);
            fwrite(vm_base + ea, 1, (size_t)(end - ea), f);
            total += end - ea; regions++;
        }
        ea = end;
    }
    fclose(f);
    dbg_printf("  wrote %u regions, %.1f MB to '%s'\n", regions, total / 1048576.0, path);
}

/* "save <hex ea> <hex len> <file>": one range in the dumpall record format, cheap
 * enough to take every second or two (state hunting by sampling over time). */
void save_range(uint32_t ea, uint32_t len, const char* path)
{
    if (!vm_base) { dbg_printf("  vm_base not live yet\n"); return; }
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery(vm_base + ea, &mi, sizeof mi) || mi.State != MEM_COMMIT ||
        (uint64_t)((uint8_t*)mi.BaseAddress - vm_base) + mi.RegionSize < (uint64_t)ea + len) {
        dbg_printf("  0x%08X+0x%X is not one committed region\n", ea, len); return;
    }
    FILE* f = fopen(path, "wb");
    if (!f) { dbg_printf("  cannot open '%s'\n", path); return; }
    uint32_t hdr[2] = { ea, len };
    fwrite(hdr, sizeof hdr, 1, f);
    fwrite(vm_base + ea, 1, len, f);
    fclose(f);
    dbg_printf("  saved 0x%08X+0x%X to '%s'\n", ea, len, path);
}

char s_path[1024];

DWORD WINAPI console_thread(LPVOID)
{
    char outpath[1100];
    snprintf(outpath, sizeof outpath, "%s.out", s_path);
    HMODULE self = GetModuleHandleA(NULL);
    const DWORD t0 = GetTickCount();
    fprintf(stderr, "[dbg] console on '%s' -- write a command, read '%s'\n", s_path, outpath);

    for (;;) {
        Sleep(100);
        FILE* f = fopen(s_path, "rb");
        if (!f) continue;
        char cmd[512] = {0};
        if (!fgets(cmd, sizeof cmd, f)) { fclose(f); continue; }
        fclose(f);
        char* nl = strpbrk(cmd, "\r\n"); if (nl) *nl = 0;
        if (!cmd[0]) continue;
        FILE* t = fopen(s_path, "wb"); if (t) fclose(t);   /* consume it */

        s_out = fopen(outpath, "ab");
        dbg_printf("\n[dbg] > %s\n", cmd);
        char verb[64] = {0};
        unsigned a = 0, b = 0;
        int n = 0;
        sscanf(cmd, "%63s", verb);
        if (!strcmp(verb, "threads")) {
            dump_threads(self);
        } else if (!strcmp(verb, "stat")) {
            dbg_printf("  uptime   %.1f s\n", (GetTickCount() - t0) / 1000.0);
            dbg_printf("  flips    %u\n", cellGcm_flip_request_count());
            dbg_printf("  last HLE 0x%08X (%s)\n", g_last_hle_nid, g_last_hle_name ? g_last_hle_name : "");
        } else if (!strcmp(verb, "hle")) {
            dbg_printf("  last HLE = 0x%08X (%s)\n", g_last_hle_nid, g_last_hle_name ? g_last_hle_name : "");
        } else if (!strcmp(verb, "mem") && sscanf(cmd, "%*s %x %u", &a, &b) >= 1) {
            dump_mem(a, b);
        } else if (!strcmp(verb, "dumpall") && cmd[7] == ' ') {
            dump_all(cmd + 8);
        } else if ((!strcmp(verb, "w16") || !strcmp(verb, "w32")) && sscanf(cmd, "%*s %x %x", &a, &b) == 2) {
            /* "w16 <hex ea> <hex value>" / "w32 ...": poke guest memory (big-endian), to
             * test what a found address does. Prints the old value. */
            if (verb[1] == '1') { dbg_printf("  0x%08X: %04X -> %04X\n", a, vm_read16(a), b & 0xFFFFu); vm_write16(a, (uint16_t)b); }
            else                { dbg_printf("  0x%08X: %08X -> %08X\n", a, vm_read32(a), b); vm_write32(a, b); }
        } else if (!strcmp(verb, "zero") && sscanf(cmd, "%*s %x %x", &a, &b) == 2) {
            /* "zero <hex ea> <hex len>": clear a guest range, to test what depends on it. */
            for (uint32_t o = 0; o + 4 <= b; o += 4) vm_write32(a + o, 0);
            dbg_printf("  zeroed 0x%X bytes at 0x%08X\n", b, a);
        } else if (!strcmp(verb, "save") && sscanf(cmd, "%*s %x %x %n", &a, &b, &n) >= 2 && n) {
            save_range(a, b, cmd + n);
        } else if (!strcmp(verb, "cap")) {
            rsx_d3d12_cap_now();
            dbg_printf("  capturing the next frame (RSX_CAP_DIR, RSX_CAP_TEX=1 for textures)\n");
        } else if (!strcmp(verb, "res")) {
            dump_resources();
        } else if (!strcmp(verb, "help")) {
            dbg_printf("  threads | stat | hle | res | mem <hex> [len] | dumpall <file> | save <hex> <hexlen> <file> | cap\n");
        } else {
            dbg_printf("  ? '%s' -- try 'help'\n", verb);
        }
        if (s_out) { fclose(s_out); s_out = nullptr; }
    }
    return 0;
}

}  // namespace

/* Counters written by the JSTARS-DIAG4 hooks in the lifted code (see dump_resources). */
extern "C" volatile uint32_t jstars_dbg[16] = { 0 };

/* Start the console if PS3_DEBUG names a command file. */
extern "C" void jstars_debug_console_start(void)
{
    const char* p = getenv("PS3_DEBUG");
    if (!p || !*p) return;
    strncpy(s_path, p, sizeof s_path - 1);
    HANDLE h = CreateThread(NULL, 0, console_thread, NULL, 0, NULL);
    if (h) CloseHandle(h);
}
