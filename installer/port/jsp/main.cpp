/*
 * J-Stars Victory VS+ -- entry point (jsp.exe).
 *
 * Starts from the ps3recomp v0.18.0 project template (templates/project/main.cpp, MIT). Two blocks are from
 * jsp-vs-ex jstars/port/main.cpp (Tsamiuy, MIT), adapted to ps3recomp v0.18.0:
 *   - cellGame_init_from_paramsfo (real title id from the disc's PARAM.SFO)
 *   - the cellfs_add_path_mapping calls for /dev_hdd0 and /dev_flash (PS3_HDD0_ROOT, PS3_DEV_FLASH)
 * ENHANCEMENT: the disc/hdd0/flash folders can also be given as arguments (see usage), for launchers that
 * cannot set the environment (some launchers pass arguments only).
 * Left out for later: the VBLANK_QPC/VBLANK_THREAD frame clock, the debug console (jstars_debug.cpp), fibers
 * (jstars_fiber.cpp), game install (jstars_game.cpp), sysutil overrides (jstars_sysutil.cpp), the v0.15 default environment (VDEC_ALPHA, AUDIO_NBLOCK, ...), the sampler, SPU job registration.
 *
 * Original template header follows.
 *
 * ps3recomp game project -- entry point template.
 *
 * This is the runner. It boots a lifted title through the toolkit's PPU
 * scaffold: allocate the flat guest VM, load the PPU ELF named on the command
 * line, register the lifted function table and the HLE NID handlers, start a
 * frame clock, and dispatch the entry OPD.
 *
 * The scaffold does the work; nothing below reimplements it. ppu_load_elf,
 * ppu_recomp_register, ppu_hle_init, ppu_sysprx_register, ppu_fs_register,
 * lv2_init_syscalls and ppu_run all come from the toolkit, compiled into this
 * project from PS3RECOMP_DIR (see CMakeLists.txt). What is left here is the
 * part a port owns: which backend to present through, how the frame clock is
 * paced, and whatever diagnostics the title turns out to need.
 *
 * runtime/ppu/tests/boot_main.cpp in the toolkit is the same boot in its
 * fully-instrumented form -- crash filter, hang watchdog, guest-PC sampling
 * profiler. Read it when a boot goes wrong; copy from it what the title
 * needs. It is deliberately not what a fresh project starts with.
 *
 * The one part that IS duplicated from it is the frame clock below, because a
 * port is expected to change its pacing and a shared one would be the wrong
 * shape for that. It was copied faithfully, comments and all. If a pacing bug
 * is fixed in one of the two, look at the other.
 *
 * This builds on Windows, macOS and Linux. CI builds and runs it against the
 * boot smoke title on the last two, and compile-checks it against clang-cl on
 * the first (the toolkit's scaffold check).
 */

/* The lifter's generated header, which declares ppu_context and the function
 * table. It comes first: everything below is written against that struct. */
#include "ppu_recomp.h"

/* The Win32 names -- Sleep, GetTickCount64, CreateThread, InterlockedIncrement,
 * VirtualAlloc, the scalar typedefs -- from one place. On Windows this is a
 * passthrough to <windows.h>; off Windows the toolkit's shim supplies the same
 * names over pthreads and mmap, so the call sites below are written once. */
#include "win32_compat.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/mman.h>   /* mmap: the guest arena, see alloc_guest_vm */
#endif

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>   /* the main thread's run loop, see run_guest */
#include <pthread.h>
#endif

#ifdef _WIN32
/* timeBeginPeriod: <windows.h> arrives with WIN32_LEAN_AND_MEAN set, which
 * excludes the multimedia timer API, so it is asked for by name -- and after
 * windows.h, since timeapi.h uses UINT. No shim exists off Windows because
 * there is nothing to shim: the 15.6 ms default timer granularity this works
 * around is a Windows problem. */
#include <timeapi.h>
#endif

/* ---------------------------------------------------------------------------
 * What the scaffold gives this file
 * -----------------------------------------------------------------------*/
extern "C" {
uint32_t ppu_load_elf(const char* path);      /* ELF -> guest RAM, returns entry OPD */
void     ppu_recomp_register(void);           /* generated: lifted table -> address map */
void     ppu_hle_init(void);                  /* firmware import NID -> HLE handler */
void     ppu_sysprx_register(void);           /* boot-critical CRT (sys_initialize_tls, ...) */
void     ppu_fs_register(void);               /* cellFs over the real game directory */
void     lv2_init_syscalls(void);             /* the lv2 syscall table */
void     cellGame_init_from_paramsfo(const char* sfo_path);   /* libs/system/cellGame.c */
void     cellfs_add_path_mapping(const char* ps3_prefix, const char* host_path);   /* libs/filesystem/cellFs.c */
int      ppu_run(uint32_t entry_opd, uint32_t stack_top);

/* This project's own: load whatever real system PRX modules the title needs
 * into guest RAM and register their exports. stubs.cpp has the empty version
 * a game with no lifted PRX wants. */
void     ps3_load_prx_modules(void);

extern const char* ppu_vfs_root;              /* host dir the PS3 mount points map into */
void     ps3_host_quit(const char* why);      /* cellSysutil.c: REQUEST_EXITGAME, then exit 0 */

/* sys_ppu_thread.c: the main thread's stack, as sys_ppu_thread_get_stack_information
 * reports it. Left at 0 it names VM_STACK_BASE, the first created thread's stack. */
extern uint32_t g_ppu_main_stack_addr;
extern uint32_t g_ppu_main_stack_size;

/* The status the guest handed to sys_process_exit. A title that exits that way
 * never returns through ppu_run, so this is only read on the path where the
 * entry function unwound instead. */
extern int     g_sys_process_exit_called;
extern int32_t g_sys_process_exit_code;

/* Host-provided symbols the runtime and the HLE libraries link against. */
uint8_t* vm_base = nullptr;
extern uint32_t ppu_vm_size;                  /* ppu_loader.cpp: the OOB guard */

/* Guest-callback dispatch. g_ps3_guest_caller is the hook the HLE runtime
 * calls back into recompiled code through -- cellSysutil events and the GCM
 * vblank/flip handlers. ppu_guest_call does the OPD -> dispatch. */
typedef void (*ps3_guest_caller_fn)(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t);
extern ps3_guest_caller_fn g_ps3_guest_caller;
uint64_t ppu_guest_call(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t, uint64_t, uint64_t);

/* The GCM half of the frame clock. */
void     cellGcmTickVBlank(void);
void     cellGcm_fifo_kick_wait(unsigned ms);   /* idle wait, woken by a FIFO kick (cellGcmSys) */
void     cellGcmTickFlip(void);
int      cellGcm_take_flip_pending(void);
void     cellGcm_rsx_process_fifo(void);      /* drain get -> put */
unsigned cellGcm_flip_request_count(void);
}

/* ---------------------------------------------------------------------------
 * RSX present backend
 * -----------------------------------------------------------------------*/
/* D3D12 on Windows, Metal on Apple, and the null backend's headless software
 * path anywhere else -- the same selection runtime/host/host_posix.c and the
 * toolkit's boot harness make. All three expose the same three entry points,
 * so the frame clock below is backend-agnostic. */
#if defined(_WIN32)
extern "C" int  rsx_d3d12_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_d3d12_backend_present(void);
extern "C" int  rsx_d3d12_backend_pump_messages(void);
#  define rsx_backend_init    rsx_d3d12_backend_init
#  define rsx_backend_present rsx_d3d12_backend_present
#  define rsx_backend_pump    rsx_d3d12_backend_pump_messages
#  define RSX_BACKEND_NAME    "D3D12"
#elif defined(__APPLE__)
extern "C" int  rsx_metal_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_metal_backend_present(void);
extern "C" int  rsx_metal_backend_pump_messages(void);
#  define rsx_backend_init    rsx_metal_backend_init
#  define rsx_backend_present rsx_metal_backend_present
#  define rsx_backend_pump    rsx_metal_backend_pump_messages
#  define RSX_BACKEND_NAME    "Metal"
#else
extern "C" int  rsx_null_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_null_backend_present(void);
extern "C" int  rsx_null_backend_pump_messages(void);
#  define rsx_backend_init    rsx_null_backend_init
#  define rsx_backend_present rsx_null_backend_present
#  define rsx_backend_pump    rsx_null_backend_pump_messages
#  define RSX_BACKEND_NAME    "null (headless software)"
#endif

/* ---------------------------------------------------------------------------
 * Configuration
 * -----------------------------------------------------------------------*/
/* The flat VM must span every region the PS3 memory map uses, not just the
 * game image: sys_ppu_thread_create puts thread stacks at 0xD0000000, so a
 * smaller arena makes every spawned thread's stack access out of bounds. */
#define VM_SIZE       0x100010000ull   /* the full 32-bit guest space + a 64K guard */
#define STACK_TOP     0x0FF00000u      /* main-thread stack, below the 0x10000000 segment */
#define STACK_SIZE    0x00100000u      /* what the main thread is told it has */
#define WINDOW_WIDTH  1280
#define WINDOW_HEIGHT 720

/* ---------------------------------------------------------------------------
 * Frame clock
 * -----------------------------------------------------------------------*/
/* On real hardware the RSX raises a vblank interrupt about 60 times a second
 * and that is what drives the game's frame loop. With no RSX it is synthesized
 * here: a host thread calls cellGcmTickVBlank/TickFlip, which invoke the
 * handlers the guest registered, and drains the GCM FIFO. Without this the
 * title initialises, registers its handlers and then waits forever.
 *
 * The ticks are driven off real elapsed time rather than off how long present()
 * took. A hidden or occluded window makes present block hard, and pacing the
 * ticks behind it paces the whole game behind it. */
static volatile LONG g_frames_presented = 0;

/* Frames handed to the backend at a guest FLIP boundary -- one per frame the
 * guest actually finished. The presents made before the guest's first flip, so
 * a fresh window is not left blank through a long boot, carry no guest frame
 * and are deliberately not counted. */
extern "C" unsigned ppu_boot_frames_presented(void)
{
    return (unsigned)g_frames_presented;
}

static void present_guest_frame(void)
{
    rsx_backend_present();
    /* This thread increments and guest threads read, so interlocked rather
     * than a volatile ++, which on arm64 is neither atomic nor a fence. */
    InterlockedIncrement(&g_frames_presented);
}

static DWORD WINAPI frame_clock(LPVOID)
{
    const char* title = getenv("PS3_TITLE");
    if (!title || !*title) title = "J-Stars Victory VS+";   /* the launcher sets PS3_TITLE; scripted runs may not */

    int rsx_ok = (rsx_backend_init(WINDOW_WIDTH, WINDOW_HEIGHT, title) == 0);
    fprintf(stderr, "[rsx] %s backend init %s\n", RSX_BACKEND_NAME,
            rsx_ok ? "OK -- window open" : "FAILED");

    unsigned  last_flip = 0;
    ULONGLONG next_tick = GetTickCount64();

    /* VBLANK_QPC (on by default, VBLANK_QPC=0 = the old tick): the 60 Hz vblank/flip tick off
     * QueryPerformanceCounter at exactly 1000/60 ms, and the idle wait never runs past the next tick.
     * GetTickCount64 only advances in 15-16 ms steps, even with timeBeginPeriod(1), so with
     * `next_tick += 16` a tick fires late by up to a whole step or two fire in one burst; J-Stars waits
     * for a vblank after each flip request, so a frame ready in 30 ms often waited for a third vblank.
     * The idle wait is cellGcm_fifo_kick_wait (a guest fence wait or flush wakes it) instead of
     * Sleep(4). From the v0.15 port (jstars/port/main.cpp, VBLANK_QPC, default on there). */
    static int vqpc = -1;
    if (vqpc < 0) { const char* e = getenv("VBLANK_QPC"); vqpc = (e && *e == '0') ? 0 : 1; }
    LARGE_INTEGER qf, qn; QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&qn);
    const double q_period = 1000.0 / 60.0;
    double q_next = (double)qn.QuadPart * 1000.0 / (double)qf.QuadPart;
    if (vqpc) fprintf(stderr, "[rsx] VBLANK_QPC: vblank tick from QPC, %.4f ms\n", q_period);

    for (;;) {
        if (vqpc) {
            QueryPerformanceCounter(&qn);
            const double qms = (double)qn.QuadPart * 1000.0 / (double)qf.QuadPart;
            const double left = q_next - qms;
            unsigned w = left <= 0.0 ? 0u : (unsigned)(left + 0.999);
            if (w > 4) w = 4;
            if (w) cellGcm_fifo_kick_wait(w);
            QueryPerformanceCounter(&qn);
            const double qnow = (double)qn.QuadPart * 1000.0 / (double)qf.QuadPart;
            int qfired = 0;
            while (qnow >= q_next && qfired < 240) {
                cellGcmTickVBlank();
                cellGcmTickFlip();
                if (rsx_ok && cellGcm_take_flip_pending()) {
                    present_guest_frame();
                    last_flip = cellGcm_flip_request_count();
                }
                if (rsx_ok) cellGcm_rsx_process_fifo();
                q_next += q_period;
                qfired++;
            }
            if (qfired >= 240) q_next = qnow + q_period;   /* fell too far behind -- resync */
            goto outer_cadence;
        }
        {
        Sleep(4);
        ULONGLONG now = GetTickCount64();

        int fired = 0;
        while ((long long)(now - next_tick) >= 0 && fired < 240) {
            cellGcmTickVBlank();
            cellGcmTickFlip();
            /* Present a pending flip BEFORE draining any further. The flip
             * fires at a get==put frame boundary on the guest thread, so the
             * batch held right now is exactly the completed frame; presenting
             * after the drain races the guest's next-frame writes and shows a
             * mixed one. */
            if (rsx_ok && cellGcm_take_flip_pending()) {
                present_guest_frame();
                last_flip = cellGcm_flip_request_count();
            }
            /* Drain the FIFO every tick. This is what writes the RSX sync-fence
             * labels the game's per-frame logic blocks on, so it has to keep
             * advancing at 60 Hz even while present() throttles. */
            if (rsx_ok) cellGcm_rsx_process_fifo();
            next_tick += 16;             /* ~60 Hz */
            fired++;
        }
        if (fired >= 240) next_tick = now;   /* fell too far behind -- resync */
        }
outer_cadence:

        /* Drain at the outer cadence too, not only on the 16 ms tick: titles
         * fence every render pass on a label the drain writes, and one wave of
         * those at 16 ms apiece paces the guest into single-figure frame rates.
         * The real RSX writes them in microseconds. */
        if (rsx_ok) {
            if (cellGcm_take_flip_pending()) {
                present_guest_frame();
                last_flip = cellGcm_flip_request_count();
            }
            cellGcm_rsx_process_fifo();

            if (rsx_backend_pump() != 0) {
                rsx_ok = 0;              /* window closed */
                /* ...so quit, as the console's Quit Game does, instead of
                 * leaving the game running with no window. */
                ps3_host_quit("window closed");
                continue;
            }
            /* Present on a guest flip. A present on a fixed clock can catch the
             * drain mid-frame and flash a partial one. Before the first flip
             * present freely, so the window is not blank during boot. */
            unsigned fc = cellGcm_flip_request_count();
            if (fc != last_flip) {
                present_guest_frame();
                last_flip = fc;
            } else if (fc == 0) {
                rsx_backend_present();
            }
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * VFS root
 * -----------------------------------------------------------------------*/
/* Derive the directory holding PS3_GAME from the EBOOT path
 * <root>/PS3_GAME/USRDIR/EBOOT.elf -> <root>. $PS3_VFS_ROOT overrides. */
static char s_vfs_root[1024];

static void derive_vfs_root(const char* eboot)
{
    const char* env = getenv("PS3_VFS_ROOT");
    if (env && *env) { ppu_vfs_root = env; return; }

    strncpy(s_vfs_root, eboot, sizeof s_vfs_root - 1);
    for (char* p = s_vfs_root; *p; p++) if (*p == '\\') *p = '/';
    /* strip EBOOT.elf, USRDIR and PS3_GAME */
    for (int i = 0; i < 3; i++) { char* s = strrchr(s_vfs_root, '/'); if (s) *s = 0; }
    if (!s_vfs_root[0]) strcpy(s_vfs_root, ".");
    ppu_vfs_root = s_vfs_root;
}

/* ---------------------------------------------------------------------------
 * Guest VM
 * -----------------------------------------------------------------------*/
#ifdef _WIN32
/* Demand-paging for the flat VM: reserve the whole 4 GB guest space, which
 * costs no commit, and commit each 64 KB page on first access. Every 32-bit
 * guest offset is then valid, so a garbage guest pointer reads zero instead of
 * killing the process. Faults outside the arena fall through untouched. */
static LONG WINAPI vm_commit_veh(EXCEPTION_POINTERS* ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        ULONG_PTR fault = ep->ExceptionRecord->ExceptionInformation[1];
        uintptr_t base  = (uintptr_t)vm_base;
        if (vm_base && fault >= base && fault < base + VM_SIZE) {
            void* page = (void*)(fault & ~(uintptr_t)0xFFFF);
            if (VirtualAlloc(page, 0x10000, MEM_COMMIT, PAGE_READWRITE))
                return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static bool alloc_guest_vm(void)
{
#ifdef _WIN32
    AddVectoredExceptionHandler(1, vm_commit_veh);
    vm_base = (uint8_t*)VirtualAlloc(NULL, VM_SIZE, MEM_RESERVE, PAGE_READWRITE);
    ppu_vm_size = 0;              /* the whole space is backed; no OOB guard needed */
#else
    /* No vectored exception handlers off Windows, so the arena is mapped up
     * front and lazily backed by the OS: only pages the title touches cost
     * anything. It spans the whole 32-bit space plus the guard, as on Windows.
     * It used to stop below 0xE0000000, but the raw SPU windows (local store
     * and problem-state registers, runtime/spu/spu_raw.c) live from there up.
     * Every load and store a title made into one was reported as an
     * out-of-bounds access and dropped, so a raw SPU was never loaded or
     * started, and sys_raw_spu_create, which clears the registers through
     * vm_base directly, wrote past the end of the mapping. With the whole
     * space backed, ppu_vm_size 0 also gives the loader's bounds check the
     * one-compare fast path Windows already has.
     *
     * mmap, not calloc: vm_commit() hands vm_base + addr to mprotect, which
     * takes page-aligned addresses only. glibc serves an allocation this big
     * from its own mmap chunk and returns a pointer 16 bytes past the page
     * start, so every commit failed with EINVAL and sys_memory_allocate
     * answered ENOMEM to every call. mmap's base is page-aligned and zeroed. */
    void* p = mmap(NULL, VM_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    vm_base = (p == MAP_FAILED) ? nullptr : (uint8_t*)p;
    ppu_vm_size = 0;              /* the whole space is backed; no OOB guard needed */
#endif
    return vm_base != nullptr;
}

/* ---------------------------------------------------------------------------
 * cellGcmSetDefaultCommandBuffer (NID 0xBC982946)
 *
 * Adapted from this repository's v0.15 port (jstars/port/stubs.cpp, gcm_set_default_command_buffer).
 * libgcm points gCellGcmCurrentContext back at the default context cellGcmInit built, so the
 * inline command writes that follow go to the default ring. The runtime only zeroes a host
 * struct (and its opt-in GCM_DEFAULT_CTX_REPOINT also rewinds the default context, which
 * J-Stars, calling this three times a frame, does not survive). Repoint only, no rewind.
 *   gCellGcmCurrentContext = 0x0096E12C (ctx_out given to _cellGcmInitBody)
 * Both addresses come from the runtime (cellGcm_context_slot_ea / cellGcm_default_context_ea,
 * patch 2001) rather than being hard-coded as in v0.15.
 * -----------------------------------------------------------------------*/
extern "C" void ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*));
extern "C" uint32_t cellGcm_default_context_ea(void);
extern "C" uint32_t cellGcm_context_slot_ea(void);
static void gcm_set_default_command_buffer(ppu_context* ctx)
{
    const uint32_t ctx_out = cellGcm_context_slot_ea(), default_ctx = cellGcm_default_context_ea();
    if (ctx_out && default_ctx)
        vm_write32(ctx_out, default_ctx);
    ctx->gpr[3] = 0;   /* CELL_OK */
}

/* ---------------------------------------------------------------------------
 * Main
 * -----------------------------------------------------------------------*/
static void harness_guest_caller(uint32_t opd, uint64_t a0, uint64_t a1,
                                 uint64_t a2, uint64_t a3, uint64_t a4,
                                 uint64_t a5, uint64_t a6, uint64_t a7)
{
    ppu_guest_call(opd, a0, a1, a2, a3, a4, a5, a6, a7);
}

/* ---------------------------------------------------------------------------
 * Running the guest
 * -----------------------------------------------------------------------*/
#if defined(__APPLE__)
/* AppKit only runs on the process's main thread. The Metal backend creates
 * its window and pumps its events with dispatch_sync onto the main queue
 * when it is called from any other thread, as it is from the frame clock.
 * With the guest on the main thread nothing ever serviced that queue: the
 * frame clock blocked in its first rsx_backend_init, no vblank or flip was
 * ever delivered and the FIFO never drained. A title that waits for a flip
 * hangs there. So the guest runs on a thread of its own, with the same
 * 256 MB host stack every guest thread gets (sys_ppu_thread.c), and the
 * main thread turns the run loop until ppu_run returns. Headless runs never
 * touch the main queue, which is why CI does not see the difference. */
struct guest_run { uint32_t entry, stack_top; int rc; volatile int done; };

static void* guest_thread(void* arg)
{
    guest_run* g = (guest_run*)arg;
    g->rc = ppu_run(g->entry, g->stack_top);
    __atomic_store_n(&g->done, 1, __ATOMIC_RELEASE);
    CFRunLoopStop(CFRunLoopGetMain());
    return nullptr;
}

static int run_guest(uint32_t entry, uint32_t stack_top)
{
    static guest_run g;
    g.entry = entry; g.stack_top = stack_top; g.rc = 0; g.done = 0;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 256u * 1024u * 1024u);
    pthread_t th;
    int err = pthread_create(&th, &attr, guest_thread, &g);
    pthread_attr_destroy(&attr);
    if (err != 0) {
        fprintf(stderr, "[boot] could not start the guest thread (%d)\n", err);
        return -1;
    }
    /* Short slices: CFRunLoopStop only ends a run that is already going. */
    while (!__atomic_load_n(&g.done, __ATOMIC_ACQUIRE))
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
    pthread_join(th, nullptr);
    return g.rc;
}
#else
static int run_guest(uint32_t entry, uint32_t stack_top) { return ppu_run(entry, stack_top); }
#endif

/* Arguments after the ELF: --disc DIR (folder holding PS3_GAME, read-only), --hdd0 DIR (writable folder for
 * /dev_hdd0), --flash DIR (dev_flash, read-only). They set the same environment the runtime reads
 * (PS3_VFS_ROOT, PS3_HDD0_ROOT, PS3_DEV_FLASH); an argument wins over the environment. ENHANCEMENT. */
static int parse_paths(int argc, char** argv)
{
    static const struct { const char* flag; const char* var; } opts[] = {
        { "--disc", "PS3_VFS_ROOT" }, { "--hdd0", "PS3_HDD0_ROOT" }, { "--flash", "PS3_DEV_FLASH" } };
    for (int i = 2; i < argc; i++) {
        bool known = false;
        for (auto& o : opts) {
            if (strcmp(argv[i], o.flag) != 0) continue;
            known = true;
            if (i + 1 >= argc) { fprintf(stderr, "ERROR: %s needs a folder\n", o.flag); return -1; }
            static char buf[3][1100]; static int n = 0;
            snprintf(buf[n], sizeof buf[n], "%s=%s", o.var, argv[++i]);
#ifdef _WIN32
            _putenv(buf[n]);
#else
            putenv(buf[n]);
#endif
            n++;
            break;
        }
        /* --env NAME=VALUE: set a runtime switch (the RT_*, VP_*, RSX_* probes) without a shell prefix.
         * ENHANCEMENT. */
        if (!known && strcmp(argv[i], "--env") == 0 && i + 1 < argc) {
            static char ebuf[16][512]; static int en = 0;
            if (en < 16) {
                snprintf(ebuf[en], sizeof ebuf[en], "%s", argv[++i]);
#ifdef _WIN32
                _putenv(ebuf[en]);
#else
                putenv(ebuf[en]);
#endif
                en++;
                continue;
            }
        }
        if (!known) { fprintf(stderr, "ERROR: unknown argument %s\n", argv[i]); return -1; }
    }
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("usage: %s <EBOOT.elf> [--disc DIR] [--hdd0 DIR] [--flash DIR]\n", argv[0]);
        return 2;
    }
    if (parse_paths(argc, argv) != 0) return 2;

#ifdef _WIN32
#pragma comment(lib, "winmm.lib")
    /* 1 ms timer resolution. The default granularity is about 15.6 ms, which
     * inflates every shorter wait the title makes and throttles the whole
     * thing. POSIX timers are already fine-grained. */
    timeBeginPeriod(1);
    setvbuf(stdout, NULL, _IONBF, 0);   /* unbuffered: do not lose prints on a kill */
    /* from jsp-vs-ex jstars/port/main.cpp: sound with a 16-block cellAudio ring (the game asks for 8) and CRI
     * keeping 12 blocks (64 ms) ahead instead of 5, which rides out the late ticks of its sound server (v0.15:
     * 12% of blocks silent -> 0%). Set in the environment to override; CRI_LEAD=0 keeps CRI's own lead. */
    if (!getenv("AUDIO_NBLOCK")) _putenv("AUDIO_NBLOCK=16");
    if (!getenv("CRI_LEAD")) _putenv("CRI_LEAD=12");
#else
    setenv("AUDIO_NBLOCK", "16", 0);
    setenv("CRI_LEAD", "12", 0);
#endif

    printf("=== jsp (J-Stars Victory VS+, ps3recomp v0.19.0) ===\n");

    if (!alloc_guest_vm()) {
        fprintf(stderr, "ERROR: could not allocate the guest address space\n");
        return 1;
    }

    uint32_t entry = ppu_load_elf(argv[1]);
    if (!entry) {
        fprintf(stderr, "ERROR: could not load %s\n", argv[1]);
        return 1;
    }

    derive_vfs_root(argv[1]);
    printf("[boot] VFS root: %s\n", ppu_vfs_root);

    /* from jsp-vs-ex jstars/port/main.cpp (Tsamiuy, MIT), adapted to ps3recomp v0.18.0:
     * the real title id from the disc's PARAM.SFO (the VFS root holds PS3_GAME). */
    {
        char sfo[1100];
        snprintf(sfo, sizeof sfo, "%s/PS3_GAME/PARAM.SFO", ppu_vfs_root);
        cellGame_init_from_paramsfo(sfo);
    }

    /* from jsp-vs-ex jstars/port/main.cpp (Tsamiuy, MIT), adapted to ps3recomp v0.18.0:
     * libs/filesystem/cellFs.c (cellFsRename and friends) keeps its own mount table, defaulting /dev_hdd0 to
     * ./gamedata/dev_hdd0 -- not where ppu_fs (cellFsOpen) puts it. Point both at the same host directories. */
    {
        static char hdd0[1100], flash[1100];
        const char* h = getenv("PS3_HDD0_ROOT");
        /* The savedata/trophy root (PS3_GAMEDATA_ROOT) defaults to "<hdd0>_gamedata" next to the hdd0 folder
         * (ASSUMPTION: a sibling name, not inside hdd0), else saves land in the cwd and vanish per run. An explicit
         * env value wins. */
        if (h && *h && !(getenv("PS3_GAMEDATA_ROOT") && *getenv("PS3_GAMEDATA_ROOT"))) {
            static char gd[1200], envbuf[1300];
            size_t n = strlen(h);
            while (n > 1 && (h[n-1] == '/' || h[n-1] == '\\')) n--;
            snprintf(gd, sizeof gd, "%.*s_gamedata", (int)n, h);
            snprintf(envbuf, sizeof envbuf, "PS3_GAMEDATA_ROOT=%s", gd);
            _putenv(envbuf);
            printf("[jsp] PS3_GAMEDATA_ROOT defaulted to %s\n", gd);
        }
        const char* fl = getenv("PS3_DEV_FLASH");
        if (h && *h)   { snprintf(hdd0, sizeof hdd0, "%s/", h);    cellfs_add_path_mapping("/dev_hdd0/", hdd0); }
        if (fl && *fl) { snprintf(flash, sizeof flash, "%s/", fl); cellfs_add_path_mapping("/dev_flash/", flash); }
    }

    ppu_recomp_register();   /* lifted function table -> address map */
    ps3_load_prx_modules();  /* this game's lifted system PRX, if it has any */
    ppu_hle_init();          /* firmware import NID -> HLE handlers */
    ps3_hle_register_ctx(0xBC982946u, "cellGcmSetDefaultCommandBuffer", gcm_set_default_command_buffer);
    ppu_sysprx_register();   /* boot-critical CRT */
    ppu_fs_register();       /* cellFs over the game directory */
    lv2_init_syscalls();     /* the lv2 syscall table */

    /* Install the guest-callback hook, then start the frame clock. It no-ops
     * until the title registers its vblank and flip handlers during init. */
    g_ps3_guest_caller = harness_guest_caller;
    CreateThread(NULL, 4u * 1024 * 1024, frame_clock, NULL, 0, NULL);

    printf("\n[boot] dispatching entry OPD 0x%08X (stack top 0x%08X)\n\n",
           entry, STACK_TOP);

    /* A title that reads its own stack bounds (to find data its parent left
     * below the top, say) gets the stack it really runs on. */
    g_ppu_main_stack_addr = STACK_TOP - STACK_SIZE;
    g_ppu_main_stack_size = STACK_SIZE;

    int rc = run_guest(entry, STACK_TOP);
    printf("\n[boot] ppu_run returned %d (entry function unwound)\n", rc);

    /* A title that called sys_process_exit never reaches this line: that path
     * ends in the host exit(). Getting here means the entry function returned
     * instead, so hand back the status the guest published if it published
     * one. Returning a hardcoded 0 would report success for a run that never
     * got anywhere. */
    return g_sys_process_exit_called ? (int)g_sys_process_exit_code : 0;
}
