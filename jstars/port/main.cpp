/*
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
 * the first (tools/check_ppu_scaffold.py).
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
int      ppu_run(uint32_t entry_opd, uint32_t stack_top);

/* This project's own: load whatever real system PRX modules the title needs
 * into guest RAM and register their exports. stubs.cpp has the empty version
 * a game with no lifted PRX wants. */
void     ps3_load_prx_modules(void);
void     jstars_debug_console_start(void);    /* jstars_debug.cpp */
void     cellGame_init_from_paramsfo(const char* sfo_path);   /* libs/system/cellGame.c */
void     cellfs_add_path_mapping(const char* ps3_prefix, const char* host_path);   /* libs/filesystem/cellFs.c */

extern const char* ppu_vfs_root;              /* host dir the PS3 mount points map into */

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
void     cellGcmTickFlip(void);
int      cellGcm_take_flip_pending(void);
void     cellGcm_rsx_process_fifo(void);      /* drain get -> put */
void     cellGcm_fifo_kick_wait(unsigned ms); /* idle wait, woken by a kick */
void*    cellGcm_fifo_kick_event(void);       /* the kick that ends that wait */
void     cellGcm_flip_gate_present(int on);   /* flips complete only once presented */
void     cellGcm_flip_presented(unsigned n);  /* ...and this many have been */
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

/* VBLANK_THREAD=1 (opt-in): the 60 Hz vblank/flip tick on its own thread.
 *
 * In frame_clock below, the tick is fired by the drawing thread between
 * drains, so while it decodes and draws a frame (15-20 ms on Hidden Leaf) no
 * tick happens; the late ones then fire in a burst. J-Stars waits for a
 * vblank after every flip request before it flushes the next frame, so that
 * wait ended late too: FRAME_TL measured 20-23 ms from a flip request to the
 * first tick after it, where on-time ticks allow 16.7 at most
 * (docs/notes CPU_PROFILE §38-39). The game then flushes its next frame
 * late and the drawing thread waits for it. Here the tick keeps time on its
 * own, as the hardware vblank does; it kicks the drawing thread, which
 * drains (labels) and presents as before.
 *
 * A tick that no longer waits for the drain must not complete flips the
 * drawing thread has not shown yet: the first version did, the title ran
 * ahead and rewrote buffers still to be drawn (HUD as black boxes, broken
 * geometry; a run stuck on a black screen; CPU_PROFILE §40). So with the
 * thread on, a FIFO flip completes only after it is presented (cellGcmSys
 * present gate; PRESENT_GATE=0 turns the gate off for comparison). */
#if defined(_WIN32)
static int vblank_thread_on(void)
{
    static int v = -1;
    if (v < 0) { const char* e = getenv("VBLANK_THREAD"); v = (e && *e && *e != '0') ? 1 : 0; }
    return v;
}

static DWORD WINAPI vblank_thread(LPVOID)
{
    LARGE_INTEGER qf, qn; QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&qn);
    const double period = 1000.0 / 60.0;
    double next = (double)qn.QuadPart * 1000.0 / (double)qf.QuadPart + period;
    HANDLE kick = (HANDLE)cellGcm_fifo_kick_event();
    HANDLE tm = CreateWaitableTimerExW(NULL, NULL, 0x00000002 /* HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
    for (;;) {
        QueryPerformanceCounter(&qn);
        const double now = (double)qn.QuadPart * 1000.0 / (double)qf.QuadPart;
        if (now < next) {
            LARGE_INTEGER due; due.QuadPart = -(LONGLONG)((next - now) * 10000.0);
            if (tm && SetWaitableTimer(tm, &due, 0, NULL, NULL, FALSE)) WaitForSingleObject(tm, INFINITE);
            else Sleep(1);
            continue;   /* a timer can end early: check the clock again */
        }
        cellGcmTickVBlank();
        cellGcmTickFlip();
        if (kick) SetEvent(kick);
        next += period;
        if (now - next > 100.0) next = now + period;   /* stalled (debugger): resync */
    }
    return 0;
}
#else
static int vblank_thread_on(void) { return 0; }   /* the frame clock ticks as before */
#endif

static DWORD WINAPI frame_clock(LPVOID)
{
    const char* title = getenv("PS3_TITLE");
    if (!title || !*title) title = "ps3recomp";

    int rsx_ok = (rsx_backend_init(WINDOW_WIDTH, WINDOW_HEIGHT, title) == 0);
    fprintf(stderr, "[rsx] %s backend init %s\n", RSX_BACKEND_NAME,
            rsx_ok ? "OK -- window open" : "FAILED");

    unsigned  last_flip = 0;
    ULONGLONG next_tick = GetTickCount64();

    /* VBLANK_QPC (on by default, VBLANK_QPC=0 = old tick): run the 60 Hz vblank/flip tick off
     * QueryPerformanceCounter at exactly 1000/60 ms, and never idle past the
     * next tick.
     *
     * GetTickCount64 only advances in 15-16 ms steps, even with
     * timeBeginPeriod(1) (measured on this machine: 15,16,15,16,...). With
     * `next_tick += 16` a tick therefore fires late by up to a whole step, or
     * two fire in one burst, and the flip completions the guest paces its
     * frames on (flip mode VSYNC) land on an irregular 15.6/31.2 ms grid.
     * A frame ready in 30 ms then often waits for a third vblank:
     * perf/strip-carry-2026-10-20 [PERF4] shows half the frames at 37-54 ms.
     * The kick wait below is also capped at the time left to the next tick,
     * so a tick is never late by the 4 ms idle wait either. */
    static int vqpc = -1;
    if (vqpc < 0) { const char* e = getenv("VBLANK_QPC"); vqpc = (e && *e == '0') ? 0 : 1; }
    LARGE_INTEGER qf, qn; QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&qn);
    const double q_period = 1000.0 / 60.0;
    double q_next = (double)qn.QuadPart * 1000.0 / (double)qf.QuadPart;
    if (vqpc) fprintf(stderr, "[rsx] VBLANK_QPC: vblank tick from QPC, %.4f ms\n", q_period);
    const int vthread = vblank_thread_on();
#if defined(_WIN32)
    if (vthread) {
        HANDLE h = CreateThread(NULL, 0, vblank_thread, NULL, 0, NULL);
        if (h) { SetThreadPriority(h, THREAD_PRIORITY_HIGHEST); CloseHandle(h); }
        const char* pg = getenv("PRESENT_GATE");
        const int gate = !(pg && *pg == '0');
        cellGcm_flip_gate_present(gate);
        fprintf(stderr, "[rsx] VBLANK_THREAD: vblank/flip tick on its own thread, present gate %s\n",
                gate ? "on" : "off");
    }
#endif

    for (;;) {
        if (vthread) {   /* the tick runs elsewhere: wait for a kick (tick or guest), then drain */
            cellGcm_fifo_kick_wait(4);
            goto outer_cadence;
        }
        if (vqpc) {
            QueryPerformanceCounter(&qn);
            const double qms = (double)qn.QuadPart * 1000.0 / (double)qf.QuadPart;
            double left = q_next - qms;
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
        /* Was Sleep(4), as in the template. cellGcmSys's FIFO kick event is
         * created by its first waiter, so with no waiter a guest thread whose
         * fence wait found the queue dry could not wake this loop, and paid
         * up to the full 4 ms for every fence. The toolkit's boot harness
         * (runtime/ppu/tests/boot_main.cpp) made the same change. Same 4 ms
         * ceiling when nothing kicks; GCM_FIFO_KICK=0 restores the plain
         * sleep. */
        cellGcm_fifo_kick_wait(4);
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
                /* The guest has no way to hear about it and runs on, so the
                 * process outlived its window: the X, Esc, the Options Exit
                 * Game and the launcher's Exit all left it running unseen
                 * (disc mounted, a core busy). End it here.
                 * WINDOW_CLOSE_EXIT=0: old behaviour, keep running. */
                const char* wce = getenv("WINDOW_CLOSE_EXIT");
                if (!wce || wce[0] != '0') {
                    printf("[boot] game window closed, exiting\n");
                    fflush(NULL);
                    TerminateProcess(GetCurrentProcess(), 0);
                }
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
            /* VBLANK_THREAD: every flip decoded so far has been presented. */
            if (vthread) cellGcm_flip_presented(last_flip);
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
    /* No vectored exception handlers off Windows, so the arena is committed up
     * front and lazily backed by the OS: only pages the title touches cost
     * anything. It stops below the 0xE0000000 mark, so ppu_vm_size arms the
     * loader's bounds check for what is left. */
    vm_base = (uint8_t*)calloc(1, 0xE0000000u);
    ppu_vm_size = 0xE0000000u;
#endif
    return vm_base != nullptr;
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

int main(int argc, char** argv)
{
    /* Unbuffered stdout: run_test.ps1 kills the process at the end of a run,
     * and a redirected (block-buffered) stdout then lost its last few KB --
     * exactly the part of the log that shows where a run got stuck. */
    setvbuf(stdout, NULL, _IONBF, 0);
    /* Opening movie: J-Stars asks cellVdecGetPicture for ARGB with alpha 0,
     * and with that alpha the frames draw fully transparent (black screen).
     * Opaque alpha shows the movie (docs/notes FINDINGS, 2026-10-26).
     * VDEC_ALPHA set in the environment wins; VDEC_ALPHA=-1 keeps the game's. */
#ifdef _WIN32
    if (!getenv("VDEC_ALPHA")) _putenv("VDEC_ALPHA=255");
    /* Sound: a 16-block cellAudio ring (the game asks for 8) with CRI keeping 12
     * blocks (64 ms) ahead instead of 5, which rides out the late ticks of its
     * sound server (glitchy sound: 12% of blocks silent -> 0% in play).
     * CRI_LEAD=0 keeps CRI's own lead; AUDIO_NBLOCK=8 the game's ring size. */
    if (!getenv("AUDIO_NBLOCK")) _putenv("AUDIO_NBLOCK=16");
    if (!getenv("CRI_LEAD")) _putenv("CRI_LEAD=12");
    if (!getenv("CLEAR_PRESENT")) _putenv("CLEAR_PRESENT=0");   /* present on flips only: a stray clear present froze the screen */
    if (!getenv("ZPASS_LOD0")) _putenv("ZPASS_LOD0=1");         /* shadow casters sample mip 0: leaf shadows stop blinking */
    if (!getenv("PAD_KEYS_SPLIT")) _putenv("PAD_KEYS_SPLIT=1"); /* arrows = left stick, I/J/K/L = d-pad (the maintainer) */
#endif
    if (argc < 2) {
        printf("usage: %s <PPU ELF>\n", argv[0]);
        return 2;
    }

#ifdef _WIN32
#pragma comment(lib, "winmm.lib")
    /* 1 ms timer resolution. The default granularity is about 15.6 ms, which
     * inflates every shorter wait the title makes and throttles the whole
     * thing. POSIX timers are already fine-grained. */
    timeBeginPeriod(1);
    setvbuf(stdout, NULL, _IONBF, 0);   /* unbuffered: do not lose prints on a kill */
#endif

    printf("=== ps3recomp game runner ===\n");

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

    /* Real title id from the disc's PARAM.SFO (the VFS root holds PS3_GAME).
     * Without it cellGame keeps its "BLES00000" placeholder. */
    {
        char sfo[1100];
        snprintf(sfo, sizeof sfo, "%s/PS3_GAME/PARAM.SFO", ppu_vfs_root);
        cellGame_init_from_paramsfo(sfo);
    }

    /* libs/filesystem/cellFs.c (cellFsRename and friends) keeps its own mount
     * table, defaulting /dev_hdd0 to ./gamedata/dev_hdd0 -- not where ppu_fs
     * (cellFsOpen) puts it. Point both at the same host directories, or a file
     * written through one cannot be renamed through the other. */
    {
        static char hdd0[1100], flash[1100];
        const char* h = getenv("PS3_HDD0_ROOT");
        const char* fl = getenv("PS3_DEV_FLASH");
        if (h && *h)   { snprintf(hdd0, sizeof hdd0, "%s/", h);    cellfs_add_path_mapping("/dev_hdd0/", hdd0); }
        if (fl && *fl) { snprintf(flash, sizeof flash, "%s/", fl); cellfs_add_path_mapping("/dev_flash/", flash); }
    }

    ppu_recomp_register();   /* lifted function table -> address map */
    jstars_debug_console_start();   /* PS3_DEBUG=<file>: query a running title */
    ps3_load_prx_modules();  /* this game's lifted system PRX, if it has any */
    ppu_hle_init();          /* firmware import NID -> HLE handlers */
    ppu_sysprx_register();   /* boot-critical CRT */
    ppu_fs_register();       /* cellFs over the game directory */
    lv2_init_syscalls();     /* the lv2 syscall table */

    /* Install the guest-callback hook, then start the frame clock. It no-ops
     * until the title registers its vblank and flip handlers during init. */
    g_ps3_guest_caller = harness_guest_caller;
    CreateThread(NULL, 4u * 1024 * 1024, frame_clock, NULL, 0, NULL);

#ifdef _WIN32
    /* PS3_SAMPLE=<ms>: the runtime's per-thread sampler (ppu_loader.cpp). Only
     * the toolkit's boot harness started it, so the variable did nothing here.
     * Started after ppu_recomp_register(): it maps the lifted function table. */
    { extern void ps3_sampler_start(void); ps3_sampler_start(); }
#endif

    printf("\n[boot] dispatching entry OPD 0x%08X (stack top 0x%08X)\n\n",
           entry, STACK_TOP);

    int rc = ppu_run(entry, STACK_TOP);
    printf("\n[boot] ppu_run returned %d (entry function unwound)\n", rc);

    /* A title that called sys_process_exit never reaches this line: that path
     * ends in the host exit(). Getting here means the entry function returned
     * instead, so hand back the status the guest published if it published
     * one. Returning a hardcoded 0 would report success for a run that never
     * got anywhere. */
    return g_sys_process_exit_called ? (int)g_sys_process_exit_code : 0;
}
