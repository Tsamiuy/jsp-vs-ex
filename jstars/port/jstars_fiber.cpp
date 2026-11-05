/*
 * jstars_fiber.cpp -- cellFiber PPU scheduler for the J-Stars port.
 *
 * J-Stars' job system ("sp_job_cpu_thread_*" workers) runs its CPU jobs as
 * cellFiber fibers: each worker thread initializes a scheduler, creates fibers
 * and calls cellFiberPpuRunFibers; fibers park in cellFiberPpuWaitSignal and
 * are woken by cellFiberPpuSendSignal. The runtime's libs/spurs/cellFiber.c
 * implements a different, host-function API and none of the scheduler entry
 * points, so every one of these calls used to be faked as CELL_OK and no CPU
 * job ever ran.
 *
 * Model: one Windows fiber per guest fiber. A guest fiber runs its entry (a
 * guest OPD) on its own ppu_context, with the guest stack the title supplied
 * and the worker thread's TLS (r13) and identity, on its own host stack --
 * lifted code nests host calls as deeply as the guest nests guest calls, so a
 * fiber that parks mid-call keeps that host stack alive until it resumes.
 *
 * Switching fibers on one host thread must also switch the runtime's
 * per-thread execution state, which would otherwise be shared by every fiber
 * on that thread: g_active_ctx, the pending tail-call trampoline, and the two
 * counters the runtime exposes for coroutine layers
 * (ps3_ppu_dispatch_depth_slot, ps3_ppu_vcall_sp_slot).
 *
 * Signatures follow the SDK:
 *   _cellFiberPpuInitialize(void)
 *   cellFiberPpuInitializeScheduler(scheduler, attr)
 *   cellFiberPpuFinalizeScheduler(scheduler)
 *   cellFiberPpuRunFibers(scheduler)
 *   cellFiberPpuCreateFiber(scheduler, fiber, entry, arg, priority,
 *                           eaStack, sizeStack, attr)
 *   cellFiberPpuWaitSignal(void)
 *   cellFiberPpuSendSignal(fiber, numWorker*)
 *   cellFiberPpuJoinFiber(fiber, status*)
 * The first calls of each are logged so the layout can be checked.
 */

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "ps3emu/error_codes.h"
#include "ppu_recomp.h"     /* ppu_context, vm_write32, function_table */

extern "C" void ps3_hle_register_ctx(uint32_t nid, const char* name,
                                     void (*fn)(ppu_context*));
extern "C" int  ppu_opd_resolve(uint32_t opd, uint32_t* code, uint32_t* toc);
extern "C" PPU_THREAD_LOCAL ppu_context* g_active_ctx;
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
extern "C" int* ps3_ppu_dispatch_depth_slot(void);
extern "C" int* ps3_ppu_vcall_sp_slot(void);

namespace {

enum : uint32_t {
    FIBER_ERROR_INVAL = 0x80760004u,   /* CELL_FIBER_ERROR_INVAL */
    FIBER_ERROR_PERM  = 0x80760009u,   /* CELL_FIBER_ERROR_PERM  */
    FIBER_ERROR_STAT  = 0x8076000Fu,   /* CELL_FIBER_ERROR_STAT  */
};

/* Guest CellFiberPpu layout, as J-Stars' SPU job (spujob_80FD00) reads and
 * writes it when it signals a PPU fiber from the SPU: a getllar/putllc on the
 * fiber's 128-byte line that updates word 0, the state flags.
 *   w == 0 or w & DONE  -> the job reports an error and signals nothing
 *   w & WAITING         -> w = (w & ~WAITING) | READY, and the fiber is linked
 *                          into the scheduler at +0x08 by its priority (+0x19)
 *   otherwise           -> w |= PENDING (a signal for a fiber that is not
 *                          waiting yet; its next WaitSignal returns at once)
 * So the signal state lives in guest memory, not on the host: this scheduler
 * keeps word 0 in those terms and reads it back to see what the SPU did. */
enum : uint32_t {
    GF_READY   = 0x01,
    GF_RUNNING = 0x02,
    GF_WAITING = 0x08,
    GF_PENDING = 0x10,
    GF_DONE    = 0x40,
    GF_OFF_STATE = 0x00, GF_OFF_NEXT = 0x04, GF_OFF_SCHED = 0x08, GF_OFF_PRIO_WORD = 0x18,
};
/* JSTARS_FIBER_LOG=1: log every signal pickup, park and resume. */
inline bool fiber_log()
{
    static int v = -1;
    if (v < 0) v = std::getenv("JSTARS_FIBER_LOG") ? 1 : 0;
    return v != 0;
}
inline uint32_t gf_get(uint32_t fea)             { return vm_read32(fea + GF_OFF_STATE); }
inline void     gf_set(uint32_t fea, uint32_t w) { vm_write32(fea + GF_OFF_STATE, w); }

/* Per-thread runtime state that belongs to whichever fiber is running. */
struct ExecState {
    ppu_context* active = nullptr;
    void (*trampoline)(void*) = nullptr;
    int dispatch_depth = 0;
    int vcall_sp = 0;

    void save()
    {
        active = g_active_ctx;
        trampoline = g_trampoline_fn;
        dispatch_depth = *ps3_ppu_dispatch_depth_slot();
        vcall_sp = *ps3_ppu_vcall_sp_slot();
    }
    void restore() const
    {
        g_active_ctx = active;
        g_trampoline_fn = trampoline;
        *ps3_ppu_dispatch_depth_slot() = dispatch_depth;
        *ps3_ppu_vcall_sp_slot() = vcall_sp;
    }
};

enum class FState { Ready, Running, WaitSignal, WaitJoin, Done };

struct Scheduler;

struct Fiber {
    uint32_t   ea = 0;
    Scheduler* sched = nullptr;
    uint32_t   entry_opd = 0;
    uint64_t   arg = 0;
    uint32_t   priority = 0;
    uint32_t   stack_ea = 0, stack_size = 0;
    FState     state = FState::Ready;
    int32_t    exit_status = 0;
    uint32_t   join_target = 0;     /* fiber EA this one waits on (WaitJoin) */
    LPVOID     host = nullptr;      /* Windows fiber */
    ExecState  exec;                /* saved while switched out */
    ppu_context ctx;                /* the fiber's guest registers */
    /* Worker thread it first ran on: r13 and identity are inherited from it. */
    uint64_t   thread_r13 = 0, thread_id = 0;
};

struct Scheduler {
    uint32_t           ea = 0;
    std::mutex         lock;
    std::vector<Fiber*> fibers;
    size_t             rr = 0;      /* round-robin cursor */
};

std::mutex                     g_tables_lock;
std::map<uint32_t, Scheduler*> g_scheds;
std::map<uint32_t, Fiber*>     g_fibers;

/* Per host thread: the fiber the thread itself runs as (the scheduler side),
 * and the guest fiber currently running on it, if any. */
thread_local LPVOID    t_home = nullptr;
thread_local ExecState t_home_exec;
thread_local Fiber*    t_current = nullptr;

Scheduler* sched_get(uint32_t ea, bool create)
{
    std::lock_guard<std::mutex> g(g_tables_lock);
    auto it = g_scheds.find(ea);
    if (it != g_scheds.end()) return it->second;
    if (!create) return nullptr;
    Scheduler* s = new Scheduler;
    s->ea = ea;
    g_scheds[ea] = s;
    return s;
}

Fiber* fiber_get(uint32_t ea)
{
    std::lock_guard<std::mutex> g(g_tables_lock);
    auto it = g_fibers.find(ea);
    return it == g_fibers.end() ? nullptr : it->second;
}

void (*lookup_fn(uint32_t code))(ppu_context*)
{
    static std::mutex m;
    static std::map<uint32_t, void (*)(ppu_context*)> cache;
    std::lock_guard<std::mutex> g(m);
    auto it = cache.find(code);
    if (it != cache.end()) return it->second;
    void (*fn)(ppu_context*) = nullptr;
    for (uint64_t i = 0; i < function_table_count; i++)
        if ((uint32_t)function_table[i].addr == code) { fn = function_table[i].func; break; }
    cache[code] = fn;
    return fn;
}

void ensure_home()
{
    if (t_home) return;
    t_home = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(nullptr);
}

/* Wake every fiber of `s` joined on `target`. Caller holds s->lock. */
void wake_joiners(Scheduler* s, uint32_t target)
{
    for (Fiber* f : s->fibers)
        if (f->state == FState::WaitJoin && f->join_target == target) {
            f->state = FState::Ready;
            f->join_target = 0;
        }
}

void WINAPI fiber_main(LPVOID p)
{
    Fiber* f = static_cast<Fiber*>(p);
    uint32_t code = 0, toc = 0;
    ppu_opd_resolve(f->entry_opd, &code, &toc);
    void (*fn)(ppu_context*) = lookup_fn(code);

    std::memset(&f->ctx, 0, sizeof f->ctx);
    f->ctx.gpr[1]  = ((uint64_t)f->stack_ea + f->stack_size - 0x80) & ~0xFull;
    f->ctx.gpr[2]  = toc;
    f->ctx.gpr[3]  = f->arg;
    f->ctx.gpr[13] = f->thread_r13;
    f->ctx.thread_id = f->thread_id;
    f->ctx.cia = code;

    if (fn) {
        g_active_ctx = &f->ctx;
        g_trampoline_fn = nullptr;
        *ps3_ppu_dispatch_depth_slot() = 0;
        *ps3_ppu_vcall_sp_slot() = 0;
        fn(&f->ctx);
        while (g_trampoline_fn) { void (*tf)(void*) = g_trampoline_fn; g_trampoline_fn = nullptr; tf(&f->ctx); }
        f->exit_status = (int32_t)f->ctx.gpr[3];
    } else {
        std::fprintf(stderr, "[jstars-fiber] fiber 0x%08X: entry OPD 0x%08X -> code 0x%08X not lifted\n",
                     f->ea, f->entry_opd, code);
        f->exit_status = -1;
    }

    {
        std::lock_guard<std::mutex> g(f->sched->lock);
        f->state = FState::Done;
        gf_set(f->ea, GF_DONE);
        wake_joiners(f->sched, f->ea);
    }
    /* Back to the scheduler for good; it deletes this fiber's host stack. */
    SwitchToFiber(t_home);
}

/* Park the running guest fiber (its state already set under the lock) and
 * return to the scheduler loop; returns when the scheduler resumes it. */
void park_current()
{
    Fiber* self = t_current;
    if (fiber_log()) std::fprintf(stderr, "[jstars-fiber] 0x%08X parks\n", self->ea);
    self->exec.save();
    SwitchToFiber(t_home);
    self->exec.restore();
    if (fiber_log()) std::fprintf(stderr, "[jstars-fiber] 0x%08X resumes\n", self->ea);
}

/* Run `s`'s ready fibers on this thread until none is ready. */
void run_fibers(Scheduler* s, ppu_context* caller)
{
    ensure_home();
    for (;;) {
        Fiber* next = nullptr;
        {
            std::lock_guard<std::mutex> g(s->lock);
            size_t n = s->fibers.size();
            /* A waiting fiber whose guest word lost WAITING was signalled from
             * the SPU (or by SendSignal): it is runnable again. */
            for (Fiber* f : s->fibers)
                if (f->state == FState::WaitSignal && !(gf_get(f->ea) & GF_WAITING)) {
                    f->state = FState::Ready;
                    if (fiber_log())
                        std::fprintf(stderr, "[jstars-fiber] 0x%08X signalled (word0=0x%X) -> ready\n",
                                     f->ea, gf_get(f->ea));
                }
            /* Lowest priority value first; round-robin among equals. */
            for (size_t k = 0; k < n; k++) {
                Fiber* f = s->fibers[(s->rr + k) % n];
                if (f->state != FState::Ready) continue;
                if (!next || f->priority < next->priority) next = f;
            }
            if (!next) return;
            s->rr = (s->rr + 1) % (n ? n : 1);
            next->state = FState::Running;
            gf_set(next->ea, (gf_get(next->ea) & GF_PENDING) | GF_RUNNING);
        }
        if (!next->host) {
            next->thread_r13 = caller->gpr[13];
            next->thread_id  = caller->thread_id;
            next->host = CreateFiberEx(64 * 1024, 16 * 1024 * 1024, 0, fiber_main, next);
            if (!next->host) {
                std::fprintf(stderr, "[jstars-fiber] CreateFiberEx failed for 0x%08X\n", next->ea);
                std::lock_guard<std::mutex> g(s->lock);
                next->state = FState::Done;
                continue;
            }
        }
        t_home_exec.save();
        t_current = next;
        SwitchToFiber(next->host);
        t_current = nullptr;
        t_home_exec.restore();

        std::lock_guard<std::mutex> g(s->lock);
        if (next->state == FState::Running) {                              /* yielded */
            next->state = FState::Ready;
            gf_set(next->ea, (gf_get(next->ea) & GF_PENDING) | GF_READY);
        }
        if (next->state == FState::Done && next->host) { DeleteFiber(next->host); next->host = nullptr; }
    }
}

int g_log_n[8];
bool log_first(int which) { return g_log_n[which]++ < 6; }

/* ---- HLE entry points ------------------------------------------------- */

void h_initialize(ppu_context* ctx)
{
    std::printf("[jstars-fiber] _cellFiberPpuInitialize()\n");
    ctx->gpr[3] = CELL_OK;
}

void h_init_scheduler(ppu_context* ctx)
{
    uint32_t sea = (uint32_t)ctx->gpr[3];
    if (log_first(0))
        std::printf("[jstars-fiber] InitializeScheduler(sched=0x%08X attr=0x%08X)\n",
                    sea, (uint32_t)ctx->gpr[4]);
    if (!sea) { ctx->gpr[3] = FIBER_ERROR_INVAL; return; }
    sched_get(sea, true);
    ctx->gpr[3] = CELL_OK;
}

void h_finalize_scheduler(ppu_context* ctx)
{
    uint32_t sea = (uint32_t)ctx->gpr[3];
    std::printf("[jstars-fiber] FinalizeScheduler(sched=0x%08X)\n", sea);
    ctx->gpr[3] = CELL_OK;
}

void h_create_fiber(ppu_context* ctx)
{
    uint32_t sea = (uint32_t)ctx->gpr[3], fea = (uint32_t)ctx->gpr[4];
    if (log_first(1))
        std::printf("[jstars-fiber] CreateFiber(sched=0x%08X fiber=0x%08X entry=0x%08X arg=0x%llX "
                    "prio=%u stack=0x%08X size=0x%X attr=0x%08X)\n",
                    sea, fea, (uint32_t)ctx->gpr[5], (unsigned long long)ctx->gpr[6],
                    (uint32_t)ctx->gpr[7], (uint32_t)ctx->gpr[8], (uint32_t)ctx->gpr[9],
                    (uint32_t)ctx->gpr[10]);
    Scheduler* s = sched_get(sea, true);
    if (!fea || !ctx->gpr[5] || !ctx->gpr[8] || !ctx->gpr[9]) { ctx->gpr[3] = FIBER_ERROR_INVAL; return; }

    Fiber* f = new Fiber;
    f->ea = fea;
    f->sched = s;
    f->entry_opd = (uint32_t)ctx->gpr[5];
    f->arg = ctx->gpr[6];
    f->priority = (uint32_t)ctx->gpr[7];
    f->stack_ea = (uint32_t)ctx->gpr[8];
    f->stack_size = (uint32_t)ctx->gpr[9];
    /* The guest fields the SPU-side signal reads (see GF_*). */
    gf_set(fea, GF_READY);
    vm_write32(fea + GF_OFF_NEXT, 0);
    vm_write32(fea + GF_OFF_SCHED, sea);
    {
        uint32_t w = vm_read32(fea + GF_OFF_PRIO_WORD);   /* priority byte at +0x19 */
        vm_write32(fea + GF_OFF_PRIO_WORD, (w & ~0x00FF0000u) | ((f->priority & 0xFFu) << 16));
    }
    {
        std::lock_guard<std::mutex> g(g_tables_lock);
        g_fibers[fea] = f;   /* a reused fiber EA replaces the finished one */
    }
    {
        std::lock_guard<std::mutex> g(s->lock);
        s->fibers.push_back(f);
    }
    ctx->gpr[3] = CELL_OK;
}

void h_run_fibers(ppu_context* ctx)
{
    uint32_t sea = (uint32_t)ctx->gpr[3];
    if (log_first(2)) std::printf("[jstars-fiber] RunFibers(sched=0x%08X)\n", sea);
    Scheduler* s = sched_get(sea, false);
    if (!s) { ctx->gpr[3] = FIBER_ERROR_INVAL; return; }
    if (t_current) { ctx->gpr[3] = FIBER_ERROR_PERM; return; }   /* not from inside a fiber */
    run_fibers(s, ctx);
    ctx->gpr[3] = CELL_OK;
}

void h_wait_signal(ppu_context* ctx)
{
    Fiber* self = t_current;
    if (log_first(3))
        std::printf("[jstars-fiber] WaitSignal() from fiber 0x%08X\n", self ? self->ea : 0);
    if (!self) { ctx->gpr[3] = FIBER_ERROR_PERM; return; }
    {
        std::lock_guard<std::mutex> g(self->sched->lock);
        uint32_t w = gf_get(self->ea);
        if (w & GF_PENDING) {                      /* signalled before it waited */
            gf_set(self->ea, w & ~GF_PENDING);
            ctx->gpr[3] = CELL_OK;
            return;
        }
        gf_set(self->ea, (w & ~(GF_READY | GF_RUNNING)) | GF_WAITING);
        self->state = FState::WaitSignal;
    }
    park_current();
    ctx->gpr[3] = CELL_OK;
}

void h_send_signal(ppu_context* ctx)
{
    uint32_t fea = (uint32_t)ctx->gpr[3], nworker_ea = (uint32_t)ctx->gpr[4];
    if (log_first(4))
        std::printf("[jstars-fiber] SendSignal(fiber=0x%08X numWorker=0x%08X)\n", fea, nworker_ea);
    Fiber* f = fiber_get(fea);
    if (!f) { ctx->gpr[3] = FIBER_ERROR_INVAL; return; }
    uint32_t woken = 0;
    {
        /* The same transition the SPU-side signal makes on the guest word. */
        std::lock_guard<std::mutex> g(f->sched->lock);
        uint32_t w = gf_get(f->ea);
        if (w & GF_WAITING) {
            gf_set(f->ea, (w & ~GF_WAITING) | GF_READY);
            f->state = FState::Ready;
            woken = 1;
        } else if (f->state != FState::Done) {
            gf_set(f->ea, w | GF_PENDING);
        }
    }
    if (nworker_ea) vm_write32(nworker_ea, woken);
    ctx->gpr[3] = CELL_OK;
}

void h_join_fiber(ppu_context* ctx)
{
    uint32_t fea = (uint32_t)ctx->gpr[3], status_ea = (uint32_t)ctx->gpr[4];
    if (log_first(5))
        std::printf("[jstars-fiber] JoinFiber(fiber=0x%08X status=0x%08X) from %s\n",
                    fea, status_ea, t_current ? "a fiber" : "a thread");
    Fiber* f = fiber_get(fea);
    if (!f) { ctx->gpr[3] = FIBER_ERROR_INVAL; return; }
    for (;;) {
        {
            std::lock_guard<std::mutex> g(f->sched->lock);
            if (f->state == FState::Done) break;
            if (t_current) {                        /* a fiber parks until woken */
                if (t_current->sched != f->sched) { ctx->gpr[3] = FIBER_ERROR_STAT; return; }
                t_current->state = FState::WaitJoin;
                t_current->join_target = fea;
            }
        }
        if (t_current) park_current();
        else Sleep(1);                              /* a plain thread polls */
    }
    if (status_ea) vm_write32(status_ea, (uint32_t)f->exit_status);
    ctx->gpr[3] = CELL_OK;
}

}  // namespace

extern "C" void jstars_register_fiber_overrides(void)
{
    ps3_hle_register_ctx(0x55870804u, "_cellFiberPpuInitialize",          h_initialize);
    ps3_hle_register_ctx(0xEE3B604Du, "cellFiberPpuInitializeScheduler",  h_init_scheduler);
    ps3_hle_register_ctx(0x8B6BAA01u, "cellFiberPpuFinalizeScheduler",    h_finalize_scheduler);
    ps3_hle_register_ctx(0x12B1ACF0u, "cellFiberPpuRunFibers",            h_run_fibers);
    ps3_hle_register_ctx(0x6C164B3Bu, "cellFiberPpuWaitSignal",           h_wait_signal);
    ps3_hle_register_ctx(0x8AFB8356u, "cellFiberPpuSendSignal",           h_send_signal);
    ps3_hle_register_ctx(0xA6004249u, "cellFiberPpuJoinFiber",            h_join_fiber);
    /* The runtime registers its own cellFiberPpuCreateFiber, with a host
     * function-pointer signature; this one takes precedence. */
    ps3_hle_register_ctx(0x7C2F4034u, "cellFiberPpuCreateFiber",          h_create_fiber);
}
