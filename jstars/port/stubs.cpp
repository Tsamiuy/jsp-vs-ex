/*
 * ps3recomp game project -- game-specific hooks and overrides.
 *
 * main.cpp is the runner and is game-agnostic. This file is where a port puts
 * everything that is not: the PRX loading hook it must answer, overrides for
 * firmware functions the HLE library gets wrong for this title, and patches
 * over recompiled code.
 *
 * It starts out as the empty version of all of that, which is what a game with
 * no lifted PRX and no overrides needs.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ps3emu/ps3types.h"
#include "ps3emu/error_codes.h"

#include "ppu_recomp.h"     /* ppu_context, vm_base, vm_read32/64, vm_write32 */
#include "spu_workload.h"   /* spu_workload_dispatch_job */

extern "C" void ps3_hle_register_ctx(uint32_t nid, const char* name,
                                     void (*fn)(ppu_context*));

/* ---------------------------------------------------------------------------
 * cellSpursJq (job queue), Port2 flavour
 *
 * CRI ADX2 (and J-Stars' own engine) submit SPU jobs through a SPURS job
 * queue: _cellSpursCreateJobQueueWithJobDescriptorPool, then
 * cellSpursJobQueuePort2Create and _cellSpursJobQueuePort2(Copy)PushJobBody,
 * then cellSpursJobQueuePort2Sync to wait for them. The runtime's cellSpursJq
 * completes every push without running anything, and four of these entry
 * points are not implemented there at all, so CRI's decoder jobs never ran.
 *
 * Here a push runs its job synchronously on the pushing thread through the
 * same dispatcher the job-chain walker uses (spu_workload_dispatch_job: jm2
 * staging, fingerprint lookup of the lifted binary). Because every job has
 * finished by the time its push returns, Sync has nothing to wait for and the
 * queue itself needs no host-side state.
 *
 * Arguments are logged for the first few calls of each entry so the assumed
 * register layout can be checked against real traffic.
 * -----------------------------------------------------------------------*/

static void jq_log_args(const char* name, ppu_context* ctx, int* count)
{
    if ((*count)++ >= 4) return;
    std::printf("[jstars-jq] %s(r3=%08llX r4=%08llX r5=%08llX r6=%08llX r7=%08llX r8=%08llX)\n",
                name,
                (unsigned long long)(uint32_t)ctx->gpr[3], (unsigned long long)(uint32_t)ctx->gpr[4],
                (unsigned long long)(uint32_t)ctx->gpr[5], (unsigned long long)(uint32_t)ctx->gpr[6],
                (unsigned long long)(uint32_t)ctx->gpr[7], (unsigned long long)(uint32_t)ctx->gpr[8]);
}

extern "C" unsigned long long ps3_ms_now(void);

/* r3 = port, r4 = job descriptor (CellSpursJobHeader + user data), r5 = its
 * size in bytes, r6 = tag, r7 = isSync. */
static void jq_push(ppu_context* ctx, const char* name, int* count)
{
    jq_log_args(name, ctx, count);
    uint32_t job_ea    = (uint32_t)ctx->gpr[4];
    uint32_t desc_size = (uint32_t)ctx->gpr[5];
    ctx->gpr[3] = CELL_OK;
    if (!job_ea || job_ea >= 0xD0000000u) return;

    uint32_t ea_bin   = (uint32_t)(vm_read64(job_ea) & ~7ull);   /* low 3 bits = flags */
    uint32_t size_bin = ((vm_read32(job_ea + 8) >> 16) & 0xFFFFu) << 4;
    if (!ea_bin || !size_bin || ea_bin >= 0x10000000u || size_bin > 0x40000u) {
        static int bad = 0;
        if (bad++ < 8)
            std::printf("[jstars-jq] %s: implausible job 0x%08X (eaBinary=0x%08X size=%u) -- not run\n",
                        name, job_ea, ea_bin, size_bin);
        return;
    }
    /* JQ_DBG=<n> (diagnostic): per-binary push counts every 5 s, and the first
     * n descriptors of the AtomDecoder job (bin 0x00819780) word by word. */
    { static int dbg = -2; if (dbg == -2) { const char* e = getenv("JQ_DBG"); dbg = e ? atoi(e) : -1; }
      if (dbg >= 0) {
          static uint32_t bins[16]; static long cnts[16]; static unsigned long long last = 0;
          int k; for (k = 0; k < 16 && bins[k] && bins[k] != ea_bin; k++) {}
          if (k < 16) { bins[k] = ea_bin; cnts[k]++; }
          unsigned long long now = ps3_ms_now(); if (!last) last = now;
          if (now - last >= 5000) { last = now; std::printf("[jq-dbg] pushes:");
              for (int i = 0; i < 16 && bins[i]; i++) std::printf(" %08X=%ld", bins[i], cnts[i]);
              std::printf("\n"); }
          if (ea_bin == 0x00819780u && dbg > 0) { dbg--;
              std::printf("[jq-dbg] %s decoder job 0x%08X size %u tid %llu:", name, job_ea, desc_size,
                          (unsigned long long)ctx->thread_id);
              for (uint32_t o = 0; o < desc_size && o < 256; o += 4) std::printf(" %08X", vm_read32(job_ea + o));
              std::printf("\n");
              /* the two pointers in its user data, before and after the run */
              uint32_t p1 = vm_read32(job_ea + 0x30), p2 = vm_read32(job_ea + 0x60);
              std::printf("[jq-dbg]   before p1=%08X:", p1);
              for (uint32_t o = 0; o < 0x80; o += 4) std::printf(" %08X", p1 ? vm_read32(p1 + o) : 0);
              std::printf("\n[jq-dbg]   before p2=%08X:", p2);
              for (uint32_t o = 0; o < 0x40; o += 4) std::printf(" %08X", p2 ? vm_read32(p2 + o) : 0);
              std::printf("\n");
              spu_workload_dispatch_job(vm_base + ea_bin, size_bin, job_ea, desc_size);
              std::printf("[jq-dbg]   after  p1=%08X:", p1);
              for (uint32_t o = 0; o < 0x80; o += 4) std::printf(" %08X", p1 ? vm_read32(p1 + o) : 0);
              std::printf("\n[jq-dbg]   after  p2=%08X:", p2);
              for (uint32_t o = 0; o < 0x40; o += 4) std::printf(" %08X", p2 ? vm_read32(p2 + o) : 0);
              std::printf("\n");
              { uint32_t in = vm_read32(p2 + 0x18), out = vm_read32(p2 + 0x20); int nzo = 0;
                for (uint32_t o = 0; o < 0x600; o += 4) if (vm_read32(out + o)) nzo++;
                std::printf("[jq-dbg]   in@%08X: %08X %08X %08X %08X  out@%08X nonzero words %d/384\n",
                            in, vm_read32(in), vm_read32(in + 4), vm_read32(in + 8), vm_read32(in + 12), out, nzo);
                if (vm_read32(in) == 0x48434100u) { std::printf("[jq-dbg]   HCA header:");
                    for (uint32_t o = 0; o < 0x60; o += 4) std::printf(" %08X", vm_read32(in + o));
                    std::printf("\n"); } }
              return; } } }
    spu_workload_dispatch_job(vm_base + ea_bin, size_bin, job_ea, desc_size);
}

static void jq_create(ppu_context* ctx)
{
    static int n = 0; jq_log_args("_cellSpursCreateJobQueueWithJobDescriptorPool", ctx, &n);
    ctx->gpr[3] = CELL_OK;
}
static void jq_port2_create(ppu_context* ctx)
{
    static int n = 0; jq_log_args("cellSpursJobQueuePort2Create", ctx, &n);
    ctx->gpr[3] = CELL_OK;
}
static void jq_port2_destroy(ppu_context* ctx) { ctx->gpr[3] = CELL_OK; }
static void jq_port2_push_job(ppu_context* ctx)
{
    static int n = 0; jq_push(ctx, "_cellSpursJobQueuePort2PushJobBody", &n);
}
static void jq_port2_copy_push_job(ppu_context* ctx)
{
    static int n = 0; jq_push(ctx, "_cellSpursJobQueuePort2CopyPushJobBody", &n);
}
static void jq_port2_sync(ppu_context* ctx) { ctx->gpr[3] = CELL_OK; }   /* pushes are synchronous */
static void jq_shutdown(ppu_context* ctx)   { ctx->gpr[3] = CELL_OK; }
/* r3 = job queue, r4 = int* exitCode (may be null). */
static void jq_join(ppu_context* ctx)
{
    uint32_t exit_ea = (uint32_t)ctx->gpr[4];
    if (exit_ea) vm_write32(exit_ea, 0);
    ctx->gpr[3] = CELL_OK;
}

static void register_job_queue_overrides(void)
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

/* ---------------------------------------------------------------------------
 * CRI sound lead (CRI_LEAD)
 *
 * CRI's output driver, func_006A5BA4, keeps its write position `lead` blocks
 * ahead of cellAudio's read index. `lead` lives in the driver object at +0x2008
 * (set at open to 5, about 27 ms) and is read after the call to
 * cellAudioGetPortConfig (call site lr 0x006A5BD8, port number = *(obj+0x2010),
 * obj in r30). A late CRI server tick (22-50 ms, every ~2.7 s) then costs
 * blocks: 12% of blocks were silent. docs/notes/AUDIO.md round 8 and FINDINGS
 * (2026-10-27): CRI_LEAD=7 cut that to 0.9-2.6%; AUDIO_NBLOCK=16 with
 * CRI_LEAD=12 (64 ms) made it 0.0% (main.cpp sets both defaults).
 *
 * This used to be a hand edit in the lifted ppu_recomp_035.cpp, which a fresh
 * lift loses. Here the same write is done at run time, in an override of
 * cellAudioGetPortConfig (NID 0x74A66AF0; the lifted import stub passes the
 * guest context straight through) that calls the runtime's function and, only
 * for that call site, writes min(CRI_LEAD, nBlock-1) to obj+0x2008. No lifted
 * code is touched. CRI_LEAD unset or 0 keeps CRI's own lead.
 * ASSUMPTION: the lr and r30 of the guest context are those of the call site
 * when the import runs; checked by *(obj+0x2010) == port number, and the write
 * is skipped (once logged) if that does not hold.
 * -----------------------------------------------------------------------*/
extern "C" int32_t cellAudioGetPortConfig(uint32_t portNum, void* config);

static void audio_get_port_config(ppu_context* ctx)
{
    const uint32_t port = (uint32_t)ctx->gpr[3];
    const uint32_t cfg  = (uint32_t)ctx->gpr[4];
    const int32_t rc = cellAudioGetPortConfig(port, (void*)(uintptr_t)cfg);
    ctx->gpr[3] = (uint64_t)(int64_t)rc;
    if (rc != 0 || (uint32_t)ctx->lr != 0x006A5BD8u) return;       /* @0x006A5BD8: the CRI driver's call */
    static int want = -1;
    if (want < 0) { const char* e = getenv("CRI_LEAD"); want = e ? atoi(e) : 0; }
    if (want <= 0) return;
    const uint32_t obj = (uint32_t)ctx->gpr[30];                   /* func_006A5BA4: r30 = the driver object */
    static int warned = 0;
    if (obj < 0x10000u || vm_read32(obj + 0x2010) != port) {
        if (!warned++) fprintf(stderr, "[cri-lead] driver object check failed (r30=0x%08X), lead left alone\n", obj);
        return;
    }
    const uint64_t nblock = vm_read64(cfg + 16);
    if (nblock < 2) return;
    uint32_t lead = (uint32_t)want;
    if (lead > nblock - 1) lead = (uint32_t)(nblock - 1);          /* the driver treats read == write as full */
    if (vm_read32(obj + 0x2008) != lead) {
        static int logged = 0;
        if (!logged++) fprintf(stderr, "[cri-lead] lead %u -> %u (nBlock %llu)\n", vm_read32(obj + 0x2008), lead,
                               (unsigned long long)nblock);
        vm_write32(obj + 0x2008, lead);
    }
}

/* ---------------------------------------------------------------------------
 * PRX loading
 *
 * main.cpp calls this once, after the lifted function table is registered and
 * vm_base is live, before the game runs. A title that lifts a real system PRX
 * -- libsre, which is cellSpurs and cellSync -- loads its image into guest RAM
 * here and registers its exports. Most ports never need to: the HLE library
 * answers those modules instead.
 *
 * J-Stars uses it to register its HLE overrides, which must be in place
 * before ppu_hle_init and before the game runs.
 * -----------------------------------------------------------------------*/

/* ---------------------------------------------------------------------------
 * cellL10n UCS2toUTF8 (NID 0xA9A76FB8)
 *
 * SDK: int32_t UCS2toUTF8(uint16_t ucs2, uint8_t* utf8) -- one code unit in,
 * its 1-3 UTF-8 bytes written to utf8, byte count returned. The runtime's
 * libs/font/cellL10n.c has a buffer-to-buffer function by this name instead
 * and is not registered, so the import was faked as CELL_OK (0 bytes).
 * -----------------------------------------------------------------------*/
static void l10n_ucs2_to_utf8(ppu_context* ctx)
{
    uint32_t c   = (uint32_t)ctx->gpr[3] & 0xFFFFu;
    uint32_t dst = (uint32_t)ctx->gpr[4];
    uint8_t  b[3];
    int n;
    if (c < 0x80)       { b[0] = (uint8_t)c; n = 1; }
    else if (c < 0x800) { b[0] = (uint8_t)(0xC0 | (c >> 6)); b[1] = (uint8_t)(0x80 | (c & 0x3F)); n = 2; }
    else                { b[0] = (uint8_t)(0xE0 | (c >> 12)); b[1] = (uint8_t)(0x80 | ((c >> 6) & 0x3F));
                          b[2] = (uint8_t)(0x80 | (c & 0x3F)); n = 3; }
    if (dst) for (int i = 0; i < n; i++) vm_write8(dst + i, b[i]);
    ctx->gpr[3] = (uint64_t)n;
}

/* ---------------------------------------------------------------------------
 * cellVdecQueryAttrEx (0xC982A84A) / cellVdecOpenEx (0x0053E2D8)
 *
 * J-Stars opens its movie decoder (MPEG-2 .m2v) with the Ex entry points,
 * which the runtime does not have; faked as CELL_OK, the title's handle stayed
 * 0 and every later call (StartSeq(0), ...) addressed a slot never opened.
 * The Ex structs extend the plain ones: CellVdecTypeEx starts with
 * {codecType, profileLevel} (then codecSpecificInfo), CellVdecResourceEx with
 * the same memAddr/memSize/thread fields -- and the runtime's decoder reads
 * only codecType -- so forward to cellVdecQueryAttr / cellVdecOpen.
 * -----------------------------------------------------------------------*/
extern "C" int32_t cellVdecQueryAttr(const void* type, void* attr);
extern "C" int32_t cellVdecOpen(const void* type, const void* res, const void* cb, void* handle);

static void vdec_query_attr_ex(ppu_context* ctx)
{
    int32_t rc = cellVdecQueryAttr((const void*)(uintptr_t)(uint32_t)ctx->gpr[3],
                                   (void*)(uintptr_t)(uint32_t)ctx->gpr[4]);
    ctx->gpr[3] = (uint64_t)(int64_t)rc;
}

static void vdec_open_ex(ppu_context* ctx)
{
    uint32_t type = (uint32_t)ctx->gpr[3];
    std::printf("[jstars-vdec] OpenEx(codecType=%u profileLevel=0x%X codecSpecificInfo=0x%08X)\n",
                type ? vm_read32(type) : 0u, type ? vm_read32(type + 4) : 0u,
                type ? vm_read32(type + 8) : 0u);
    int32_t rc = cellVdecOpen((const void*)(uintptr_t)type,
                              (const void*)(uintptr_t)(uint32_t)ctx->gpr[4],
                              (const void*)(uintptr_t)(uint32_t)ctx->gpr[5],
                              (void*)(uintptr_t)(uint32_t)ctx->gpr[6]);
    ctx->gpr[3] = (uint64_t)(int64_t)rc;
}

/* ---------------------------------------------------------------------------
 * cellNetCtlGetState (0x8B3EBA69)
 *
 * The runtime answers an offline GetState with CELL_NET_CTL_ERROR_NOT_INITIALIZED
 * (a workaround for LittleBigPlanet). A real console with no network returns
 * CELL_OK and state = Disconnected (0); J-Stars polls this every frame after
 * the opening movie. Report the hardware result.
 * -----------------------------------------------------------------------*/
static void netctl_get_state(ppu_context* ctx)
{
    uint32_t state_ea = (uint32_t)ctx->gpr[3];
    if (!state_ea) { ctx->gpr[3] = (uint64_t)(int64_t)(int32_t)0x80130106u; return; }  /* INVALID_ADDR */
    vm_write32(state_ea, 0);   /* CELL_NET_CTL_STATE_Disconnected */
    static int n = 0;
    if (n++ < 3) std::printf("[jstars-net] cellNetCtlGetState -> CELL_OK, Disconnected\n");
    ctx->gpr[3] = CELL_OK;
}

/* ---------------------------------------------------------------------------
 * cellGcmSetDefaultCommandBuffer (0xBC982946)
 *
 * libgcm: point gCellGcmCurrentContext back at the default context cellGcmInit
 * built, so the inline command writes that follow go to the default ring. The
 * runtime only zeroes a host struct (its opt-in GCM_DEFAULT_CTX_REPOINT also
 * rewinds the default context to its start, which J-Stars -- calling this three
 * times a frame -- does not survive). Repoint only, no rewind.
 *   gCellGcmCurrentContext = 0x0096E12C (ctx_out given to _cellGcmInitBody)
 *   default context        = 0x0F800000 (runtime-built; begin 0x45300000)
 * -----------------------------------------------------------------------*/
static void gcm_set_default_command_buffer(ppu_context* ctx)
{
    const uint32_t ctx_out = 0x0096E12Cu, default_ctx = 0x0F800000u;
    if (vm_read32(default_ctx + 0x0) == 0x45300000u) {
        static int n = 0;
        if (n++ < 3)
            std::printf("[jstars-gcm] SetDefaultCommandBuffer: gCellGcmCurrentContext 0x%08X -> 0x%08X "
                        "(default current=0x%08X)\n", vm_read32(ctx_out), default_ctx,
                        vm_read32(default_ctx + 0x8));
        vm_write32(ctx_out, default_ctx);
    }
    ctx->gpr[3] = CELL_OK;
}

/* ---------------------------------------------------------------------------
 * cellSysutilGetBgmPlaybackStatus2 (0x6CFD856F)
 *
 * Unresolved before: the call returned without touching the out-struct, so the
 * game read whatever was there as the XMB music-player state (audio
 * investigation, docs/notes FINDINGS "Audio: silent"). No user BGM is playing:
 * CellSysutilBgmPlaybackStatus2 { u8 playerState (0 = stopped); u8 reserved[7]; }.
 * -----------------------------------------------------------------------*/
static void sysutil_get_bgm_status2(ppu_context* ctx)
{
    uint32_t st = (uint32_t)ctx->gpr[3];
    if (!st) { ctx->gpr[3] = (uint64_t)(int64_t)(int32_t)0x8002B102u; return; }  /* CELL_SYSUTIL_ERROR_VALUE */
    for (int i = 0; i < 8; i++) vm_write8(st + i, 0);
    static int n = 0;
    if (n++ < 3) std::printf("[jstars-sysutil] GetBgmPlaybackStatus2 -> stopped\n");
    ctx->gpr[3] = CELL_OK;
}

/* ---------------------------------------------------------------------------
 * On-screen-keyboard options with no host effect (deck rename, J-Customize)
 *
 * cellOskDialogAddSupportLanguage, SetKeyLayoutOption, and the Ext calls
 * (SetInitialScale, Register{ConfirmWordFilter,KeyboardEventHook,ForceFinish}
 * Callback) were unresolved, so they returned whatever was in r3. The host text
 * box (cellOskDialog.c) needs none of them: answer CELL_OK.
 * -----------------------------------------------------------------------*/
static void osk_option_ok(ppu_context* ctx) { ctx->gpr[3] = CELL_OK; }

extern "C" void jstars_register_fiber_overrides(void);   /* jstars_fiber.cpp */
extern "C" void jstars_register_game_overrides(void);    /* jstars_game.cpp */
extern "C" void jstars_register_sysutil_overrides(void); /* jstars_sysutil.cpp */

extern "C" void ps3_load_prx_modules(void)
{
    register_job_queue_overrides();
    jstars_register_fiber_overrides();
    jstars_register_game_overrides();
    jstars_register_sysutil_overrides();
    ps3_hle_register_ctx(0x74A66AF0u, "cellAudioGetPortConfig", audio_get_port_config);
    ps3_hle_register_ctx(0xA9A76FB8u, "UCS2toUTF8", l10n_ucs2_to_utf8);
    ps3_hle_register_ctx(0xC982A84Au, "cellVdecQueryAttrEx", vdec_query_attr_ex);
    ps3_hle_register_ctx(0x0053E2D8u, "cellVdecOpenEx", vdec_open_ex);
    ps3_hle_register_ctx(0x8B3EBA69u, "cellNetCtlGetState", netctl_get_state);
    ps3_hle_register_ctx(0xBC982946u, "cellGcmSetDefaultCommandBuffer", gcm_set_default_command_buffer);
    ps3_hle_register_ctx(0x6CFD856Fu, "cellSysutilGetBgmPlaybackStatus2", sysutil_get_bgm_status2);
    ps3_hle_register_ctx(0x7F21C918u, "cellOskDialogAddSupportLanguage", osk_option_ok);
    ps3_hle_register_ctx(0xB53C54FAu, "cellOskDialogSetKeyLayoutOption", osk_option_ok);
    ps3_hle_register_ctx(0x75370397u, "cellOskDialogExtSetInitialScale", osk_option_ok);
    ps3_hle_register_ctx(0x8692FCD2u, "cellOskDialogExtRegisterConfirmWordFilterCallback", osk_option_ok);
    ps3_hle_register_ctx(0x1E1B4C97u, "cellOskDialogExtRegisterKeyboardEventHookCallback", osk_option_ok);
    ps3_hle_register_ctx(0xE800F586u, "cellOskDialogExtRegisterForceFinishCallback", osk_option_ok);
}

/* ---------------------------------------------------------------------------
 * Example: Override a specific NID
 *
 * The module system resolves function imports by NID.  To replace a default
 * HLE stub with custom logic, register your function at init time.
 * -----------------------------------------------------------------------*/

/*
 * Example: override cellGcmSetFlipMode (NID 0xA53D12AE) to force vsync.
 *
 *   static void my_cellGcmSetFlipMode(u32 mode)
 *   {
 *       printf("[hook] cellGcmSetFlipMode: forcing VSYNC (was %u)\n", mode);
 *       // Force vsync regardless of what the game requested
 *       cellGcmSetFlipMode(CELL_GCM_DISPLAY_VSYNC);
 *   }
 *
 * Then in your init code:
 *
 *   ps3::modules::override_nid(0xA53D12AE,
 *       reinterpret_cast<void*>(my_cellGcmSetFlipMode));
 */

/* ---------------------------------------------------------------------------
 * Example: Hook into module loading
 *
 * You can intercept cellSysmoduleLoadModule to perform custom actions
 * when a particular module is loaded.
 * -----------------------------------------------------------------------*/

/*
 *   static s32 my_loadmodule_hook(u16 id)
 *   {
 *       printf("[hook] Module 0x%04X loaded\n", id);
 *
 *       if (id == CELL_SYSMODULE_AUDIO) {
 *           // Perform custom audio backend initialization
 *           init_my_audio_backend();
 *       }
 *
 *       // Call the original implementation
 *       return cellSysmoduleLoadModule(id);
 *   }
 */

/* ---------------------------------------------------------------------------
 * Game-specific patches
 *
 * Sometimes you need to patch recompiled code at specific addresses.
 * Use the patch system to modify behavior without editing generated code.
 * -----------------------------------------------------------------------*/

/*
 *   // Skip an integrity check at guest address 0x00812FA0
 *   ps3::patches::nop_range(0x00812FA0, 0x00812FB0);
 *
 *   // Replace a function call with a direct return
 *   ps3::patches::force_return(0x009A1000, 0);  // return 0
 */
