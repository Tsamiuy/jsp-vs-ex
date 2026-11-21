// jsp_thread_registry.cpp -- DIAGNOSTIC (rollback snapshot): the Windows side of jsp_thread_registry.h. JSP_ROLLBACK=1 or JSP_SNAPSHOT_THREADS=registry (read at
// process attach, before any other thread exists) turns it on; otherwise the TLS callback returns at once and the snapshot keeps the Toolhelp listing.
//   * an exe TLS callback: DLL_PROCESS_ATTACH (the main thread) and DLL_THREAD_ATTACH register the thread with a duplicated handle
//     (THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION) the registry owns; DLL_THREAD_DETACH frees the slot and closes the handle (close_fn; without it the slot is marked DEAD). Both pass the birth gate.
//   * jsp_thread_registry_seed(): threads that existed before the callback ran or that never deliver an attach notification are added from one Toolhelp
//     listing at snapshot init (marked seeded; retired when their thread ends).
//   * jsp_thread_registry_audit(): a Toolhelp listing compared with the registry (baseline and every N ticks, outside any freeze). A live thread that is not
//     registered after 200 ms of re-checks fails the run loudly (THREAD-NOT-REGISTERED, with its start address module) instead of being left unfrozen.
// ASSUMPTION (Wine/Proton, not run): exe TLS callbacks receive DLL_THREAD_ATTACH/DETACH for every thread the process creates, including threads created
// by Wine's own DLLs (wasapi, the threadpool); a thread created with THREAD_CREATE_FLAGS_SKIP_THREAD_ATTACH is the known exception, which the audit names.
#include "jsp_thread_registry.h"
#include <cstdio>
#include <cstring>

namespace {
constinit jtr::Registry g_reg;   // constant-initialised: usable from the TLS callback at process attach, before C++ dynamic initialisation
}
jtr::Registry& jsp_thread_registry() { return g_reg; }

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

namespace {
void reg_wait(std::atomic<uint32_t>* a, uint32_t v) { WaitOnAddress(reinterpret_cast<volatile void*>(a), &v, sizeof v, INFINITE); }
constexpr DWORD kAccess = THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION;

bool ended(void* h);
void reg_close(void* h) { CloseHandle((HANDLE)h); }
void register_current(bool seeded)
{
    HANDLE h = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &h, kAccess, FALSE, 0) || !h) {
        g_reg.handle_failed.fetch_add(1, std::memory_order_relaxed);   // unregistered: the next audit names it, prepare() fails on the counter
        return;
    }
    if (g_reg.add(GetCurrentThreadId(), h, reg_wait, seeded) < 0) CloseHandle(h);   // REGISTRY-FULL: counted, the freeze fails
}

void NTAPI jsp_tls_cb(PVOID, DWORD reason, PVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {   // kernel32 only: the CRT is not initialised yet
        char v[16] = {0};
        const DWORD n = GetEnvironmentVariableA("JSP_SNAPSHOT_THREADS", v, sizeof v);
        char rb[64] = {0};   // rollback mode defaults to the registry (jsp_net_game.cpp sets the rest of the rollback snapshot defaults); an explicit JSP_SNAPSHOT_THREADS wins
        const DWORD nr = GetEnvironmentVariableA("JSP_ROLLBACK", rb, sizeof rb);
        const bool rollback_default = !n && nr && nr < sizeof rb && rb[0] != '0';   // the same predicate as env_on() in jsp_net_game.cpp (non-empty, first char not '0')
        if ((n && n < sizeof v && !std::strcmp(v, "registry")) || rollback_default) {
            g_reg.close_fn = reg_close; g_reg.ended_fn = ended;   // before `enabled` and before any other thread can register (slots are freed at thread exit)
            g_reg.enabled.store(1, std::memory_order_seq_cst);
        }
    }
    if (!g_reg.enabled.load(std::memory_order_relaxed)) return;
    if (reason == DLL_PROCESS_ATTACH || reason == DLL_THREAD_ATTACH) register_current(false);
    else if (reason == DLL_THREAD_DETACH) g_reg.remove(GetCurrentThreadId(), reg_wait);
}

bool ended(void* h) { DWORD c = 0; return GetExitCodeThread((HANDLE)h, &c) && c != STILL_ACTIVE; }

// Start address of a thread and the module holding it (failure report only; NtQueryInformationThread ThreadQuerySetWin32StartAddress = 9).
void start_module(DWORD tid, char* out, size_t n)
{
    std::snprintf(out, n, "?");
    HANDLE h = OpenThread(THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!h) return;
    using Q = LONG(NTAPI*)(HANDLE, int, PVOID, ULONG, PULONG);
    Q q = reinterpret_cast<Q>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread")));
    void* start = nullptr;
    if (q && q(h, 9, &start, sizeof start, nullptr) == 0 && start) {
        HMODULE m = nullptr; char path[MAX_PATH] = {0};
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)start, &m) && GetModuleFileNameA(m, path, sizeof path)) {
            const char* b = std::strrchr(path, '\\'); b = b ? b + 1 : path;
            std::snprintf(out, n, "%s+0x%llx", b, (unsigned long long)((uintptr_t)start - (uintptr_t)m));
        } else std::snprintf(out, n, "0x%llx (no module)", (unsigned long long)(uintptr_t)start);
    }
    CloseHandle(h);
}
}  // namespace

// MinGW: callbacks between .CRT$XLA and .CRT$XLZ (mingw-w64 tlssup.c), kept by the linker script's KEEP(*(SORT(.CRT$XL*))); referencing _tls_used keeps the
// TLS directory in the image. MSVC: the same section through const_seg and /INCLUDE.
// The callback is in every build: rollback mode (JSP_ROLLBACK=1) uses the registry. With neither JSP_ROLLBACK nor JSP_SNAPSHOT_THREADS=registry set it
// reads the two variables once at process attach and then returns at once on every thread start and exit.
extern "C" const IMAGE_TLS_DIRECTORY _tls_used;
#if defined(_MSC_VER) && !defined(__clang__)
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:jsp_tls_callback")
#pragma const_seg(".CRT$XLF")
extern "C" const PIMAGE_TLS_CALLBACK jsp_tls_callback = jsp_tls_cb;
#pragma const_seg()
#else
extern "C" __attribute__((section(".CRT$XLF"), used)) const PIMAGE_TLS_CALLBACK jsp_tls_callback = jsp_tls_cb;
static const void* volatile s_keep_tls_dir __attribute__((used)) = &_tls_used;
#endif

bool jsp_thread_registry_enabled() { return g_reg.enabled.load(std::memory_order_relaxed) != 0; }
void jsp_thread_registry_wait(std::atomic<uint32_t>* a, uint32_t v) { reg_wait(a, v); }
void jsp_thread_registry_wake(std::atomic<uint32_t>* a) { WakeByAddressAll(reinterpret_cast<void*>(a)); }
void jsp_thread_registry_yield() { SwitchToThread(); }
void jsp_thread_registry_close(void* h) { CloseHandle((HANDLE)h); }
bool jsp_thread_registry_ended(void* h) { return ended(h); }

// Threads alive now and not registered (they existed before the callback ran, or never deliver an attach): added as seeded. Returns how many.
unsigned jsp_thread_registry_seed()
{
    if (!jsp_thread_registry_enabled()) return 0;
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (s == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 t; t.dwSize = sizeof t;
    const DWORD pid = GetCurrentProcessId();
    unsigned n = 0;
    for (BOOL ok = Thread32First(s, &t); ok; ok = Thread32Next(s, &t)) {
        if (t.th32OwnerProcessID != pid || g_reg.has_any(t.th32ThreadID)) continue;
        HANDLE h = OpenThread(kAccess, FALSE, t.th32ThreadID);
        if (!h) continue;   // ended since the listing
        if (g_reg.add(t.th32ThreadID, h, reg_wait, true) < 0) CloseHandle(h); else n++;
    }
    CloseHandle(s);
    return n;
}

// 0 = every live thread of the process is registered (LIVE, or DEAD and still ending, or gone/ended after 200 ms of re-checks); else 1 and `why` names the first one that is not.
int jsp_thread_registry_audit(char* why, size_t n, unsigned* listed, unsigned* rechecked)
{
    *listed = 0; *rechecked = 0;
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (s == INVALID_HANDLE_VALUE) { std::snprintf(why, n, "CreateToolhelp32Snapshot failed, GetLastError=%lu", GetLastError()); return 1; }
    THREADENTRY32 t; t.dwSize = sizeof t;
    const DWORD pid = GetCurrentProcessId();
    DWORD missing[64]; unsigned nm = 0;
    for (BOOL ok = Thread32First(s, &t); ok; ok = Thread32Next(s, &t)) {
        if (t.th32OwnerProcessID != pid) continue;
        (*listed)++;
        if (!g_reg.has_any(t.th32ThreadID) && nm < 64) missing[nm++] = t.th32ThreadID;
    }
    CloseHandle(s);
    for (unsigned i = 0; i < nm; i++) {   // a thread inside its own attach notification registers within microseconds; one that ended is fine
        bool ok = false;
        // 200 ms (was 20): two copies on one loaded PC once took longer than 20 ms to run a new pool thread up to its attach notification
        for (int tries = 0; tries < 200 && !ok; tries++) {
            if (g_reg.has_any(missing[i])) { ok = true; break; }
            HANDLE h = OpenThread(THREAD_QUERY_INFORMATION, FALSE, missing[i]);
            if (!h) { ok = true; break; }   // gone
            const bool e = ended(h); CloseHandle(h);
            if (e) { ok = true; break; }
            (*rechecked)++;
            Sleep(1);
        }
        if (!ok) {   // FIX (rollback): a thread that never delivers an attach notification (Windows' own pool / loader workers, start ntdll.dll+0x24990, were seen
                     // mid-match on two copies) is adopted as seeded, like the threads that existed before the callback ran: it is frozen with the others
            HANDLE h = OpenThread(kAccess, FALSE, missing[i]);
            if (!h) continue;   // ended meanwhile
            if (g_reg.add(missing[i], h, reg_wait, true) >= 0) {
                char mod[160]; start_module(missing[i], mod, sizeof mod);
                std::fprintf(stderr, "[snap] thread registry: adopted tid=%lu start=%s (no attach notification within 200 ms)\n", missing[i], mod);
                continue;
            }
            CloseHandle(h);
            char mod[160]; start_module(missing[i], mod, sizeof mod);
            std::snprintf(why, n, "tid=%lu start=%s (alive, no attach notification seen after 200 ms)", missing[i], mod);
            return 1;
        }
    }
    return 0;
}
#else
bool jsp_thread_registry_enabled() { return false; }
void jsp_thread_registry_wait(std::atomic<uint32_t>* a, uint32_t v) { a->wait(v); }
void jsp_thread_registry_wake(std::atomic<uint32_t>* a) { a->notify_all(); }
void jsp_thread_registry_yield() {}
void jsp_thread_registry_close(void*) {}
bool jsp_thread_registry_ended(void*) { return false; }
unsigned jsp_thread_registry_seed() { return 0; }
int jsp_thread_registry_audit(char* why, size_t n, unsigned* listed, unsigned* rechecked) { *listed = *rechecked = 0; if (n) why[0] = 0; return 0; }
#endif
