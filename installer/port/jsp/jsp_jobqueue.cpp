/*
 * jsp_jobqueue.cpp -- cellSpursJq (job queue, Port2 flavour) for J-Stars.
 *
 * After this repository's v0.15 port (jstars/port/stubs.cpp, register_job_queue_overrides; JSP-Project, Tsamiuy,
 * MIT), ported to ps3recomp v0.18.0. Notes: jstars-notes/FINDINGS.md section 3.
 *
 * CRI ADX2 and the game's engine submit SPU jobs through a SPURS job queue:
 * _cellSpursCreateJobQueueWithJobDescriptorPool, cellSpursJobQueuePort2Create, then
 * _cellSpursJobQueuePort2(Copy)PushJobBody, then cellSpursJobQueuePort2Sync to wait. v0.18.0's
 * libs/spurs/cellSpursJq.c still completes every push without running it, and four of these entry points
 * (0x983BE7FC, 0xC39173A4, 0xFACB3CED, 0xCF89F218) are not registered at all, so CRI's decoder jobs never ran.
 *
 * Here a push runs its job synchronously on the pushing thread through the dispatcher the job-chain walker uses
 * (spu_workload_dispatch_job: jm2 staging into LS, fingerprint lookup of the lifted binary). Every job has finished
 * by the time its push returns, so Sync has nothing to wait for and the queue needs no host-side state.
 * ASSUMPTION (reading of the v0.15 port, unchanged): r3 = port, r4 = job descriptor (CellSpursJobHeader + user data),
 * r5 = its size in bytes; the first calls of each entry are logged so the layout can be checked.
 * Registered from ps3_load_prx_modules (before ppu_hle_init), so these context handlers take precedence over the
 * runtime's placeholders.
 */

#include <cstdio>
#include <cstdint>

#include "ps3emu/ps3types.h"
#include "ps3emu/error_codes.h"

#include "ppu_recomp.h"     /* ppu_context, vm_base, vm_read32/64, vm_write32 */
#include "spu_workload.h"   /* spu_workload_dispatch_job */

extern "C" void ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*));

namespace {

void jq_log_args(const char* name, ppu_context* ctx, int* count)
{
    if ((*count)++ >= 4) return;
    std::printf("[jsp-jq] %s(r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X r8=%08X)\n", name,
                (uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[4], (uint32_t)ctx->gpr[5],
                (uint32_t)ctx->gpr[6], (uint32_t)ctx->gpr[7], (uint32_t)ctx->gpr[8]);
}

void jq_push(ppu_context* ctx, const char* name, int* count)
{
    jq_log_args(name, ctx, count);
    const uint32_t job_ea    = (uint32_t)ctx->gpr[4];
    const uint32_t desc_size = (uint32_t)ctx->gpr[5];
    ctx->gpr[3] = CELL_OK;
    if (!job_ea || job_ea >= 0xD0000000u) return;

    const uint32_t ea_bin   = (uint32_t)(vm_read64(job_ea) & ~7ull);            /* low bits are flags */
    const uint32_t size_bin = ((vm_read32(job_ea + 8) >> 16) & 0xFFFFu) << 4;  /* sizeBinary, quadwords */
    if (!ea_bin || !size_bin || ea_bin >= 0x10000000u || size_bin > 0x40000u) {
        static int bad = 0;
        if (bad++ < 8)
            std::printf("[jsp-jq] %s: implausible job 0x%08X (eaBinary=0x%08X size=%u), not run\n",
                        name, job_ea, ea_bin, size_bin);
        return;
    }
    spu_workload_dispatch_job(vm_base + ea_bin, size_bin, job_ea, desc_size);
}

void jq_create(ppu_context* ctx)
{
    static int n = 0; jq_log_args("_cellSpursCreateJobQueueWithJobDescriptorPool", ctx, &n);
    ctx->gpr[3] = CELL_OK;
}
void jq_port2_create(ppu_context* ctx)
{
    static int n = 0; jq_log_args("cellSpursJobQueuePort2Create", ctx, &n);
    ctx->gpr[3] = CELL_OK;
}
void jq_port2_destroy(ppu_context* ctx) { ctx->gpr[3] = CELL_OK; }
void jq_port2_push_job(ppu_context* ctx)
{
    static int n = 0; jq_push(ctx, "_cellSpursJobQueuePort2PushJobBody", &n);
}
void jq_port2_copy_push_job(ppu_context* ctx)
{
    static int n = 0; jq_push(ctx, "_cellSpursJobQueuePort2CopyPushJobBody", &n);
}
void jq_port2_sync(ppu_context* ctx) { ctx->gpr[3] = CELL_OK; }   /* pushes are synchronous */
void jq_shutdown(ppu_context* ctx)   { ctx->gpr[3] = CELL_OK; }
/* r3 = job queue, r4 = int* exitCode (may be null). */
void jq_join(ppu_context* ctx)
{
    const uint32_t exit_ea = (uint32_t)ctx->gpr[4];
    if (exit_ea) vm_write32(exit_ea, 0);
    ctx->gpr[3] = CELL_OK;
}

}  // namespace

extern "C" void jsp_register_job_queue_overrides(void)
{
    ps3_hle_register_ctx(0x983BE7FCu, "_cellSpursCreateJobQueueWithJobDescriptorPool", jq_create);
    ps3_hle_register_ctx(0xF11FE0F1u, "cellSpursJobQueuePort2Create", jq_port2_create);
    ps3_hle_register_ctx(0xC485D207u, "cellSpursJobQueuePort2Destroy", jq_port2_destroy);
    ps3_hle_register_ctx(0xCF89F218u, "_cellSpursJobQueuePort2PushJobBody", jq_port2_push_job);
    ps3_hle_register_ctx(0xC39173A4u, "_cellSpursJobQueuePort2CopyPushJobBody", jq_port2_copy_push_job);
    ps3_hle_register_ctx(0xFACB3CEDu, "cellSpursJobQueuePort2Sync", jq_port2_sync);
    ps3_hle_register_ctx(0x8C97A96Cu, "cellSpursShutdownJobQueue", jq_shutdown);
    ps3_hle_register_ctx(0x8CE4CFFAu, "cellSpursJoinJobQueue", jq_join);
}
