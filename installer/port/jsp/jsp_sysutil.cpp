// jsp_sysutil.cpp -- cellSearch / cellMusic (system BGM) result callbacks for J-Stars.
//
// After this repository's v0.15 port (jstars/port/jstars_sysutil.cpp; jsp-vs-ex, Tsamiuy, MIT), ported to
// ps3recomp v0.18.0, which still has neither library (cellSearch 8 and cellMusic 7 imports
// missing). Notes: jstars-notes/FINDINGS.md section 14.
//
// From the original header:
//
// Neither library is in the runtime, so the imports were faked as CELL_OK with no
// callback. After the opening movie the game runs
//   cellSearchInitialize -> (wait for INITIALIZE_RESULT) -> cellSearchFinalize
//   -> (wait for FINALIZE_RESULT) -> cellSearchInitialize -> cellMusicInitialize2SystemWorkload
// (seen in an RPCS3 reference run). Without the result callbacks it never gets past the
// first wait. Like RPCS3, every call that has a "result" event queues it to the next
// cellSysutilCheckCallback poll; there is no music library, so searches find nothing.

#include <cstdio>
#include <cstdint>

#include "ps3emu/ps3types.h"
#include "ps3emu/error_codes.h"
#include "ppu_recomp.h"

extern "C" void ps3_hle_register_ctx(uint32_t nid, const char* name,
                                     void (*fn)(ppu_context*));
extern "C" int32_t cellSysutilQueueGuestCallbackArgs(uint32_t opd, const uint64_t args[8]);

namespace {

// CellSearchEvent
enum { SEARCH_EV_INITIALIZE = 1, SEARCH_EV_FINALIZE = 2 };
// CellMusic2Event
enum { MUSIC2_EV_INITIALIZE = 1, MUSIC2_EV_FINALIZE = 2, MUSIC2_EV_SET_PLAYBACK_COMMAND = 4,
       MUSIC2_EV_SET_VOLUME = 5, MUSIC2_EV_SET_SELECTION_CONTEXT = 6 };

uint32_t s_search_func, s_search_user;
uint32_t s_music_func, s_music_user;
float s_music_volume = 1.0f;

int log_budget = 40;
#define SLOG(...) do { if (log_budget > 0) { log_budget--; fprintf(stderr, __VA_ARGS__); } } while (0)

// void CellSearchSystemCallback(CellSearchEvent event, int result, const void* param, void* userData)
void search_cb(uint32_t event, int32_t result)
{
    const uint64_t a[8] = {event, (uint64_t)(int64_t)result, 0, s_search_user, 0, 0, 0, 0};
    cellSysutilQueueGuestCallbackArgs(s_search_func, a);
}

// void CellMusic2Callback(uint32_t event, void* param, void* userData); param carries the result
void music_cb(uint32_t event, int32_t result)
{
    const uint64_t a[8] = {event, (uint64_t)(uint32_t)result, s_music_user, 0, 0, 0, 0, 0};
    cellSysutilQueueGuestCallbackArgs(s_music_func, a);
}

// cellSearchInitialize(mode, container, func, userData)
void search_initialize(ppu_context* ctx)
{
    fprintf(stderr, "[jsp-sysutil] search_initialize entered\n");
    s_search_func = (uint32_t)ctx->gpr[5];
    s_search_user = (uint32_t)ctx->gpr[6];
    SLOG("[jsp-sysutil] cellSearchInitialize(mode=%u, func=0x%08X, user=0x%08X)\n",
         (uint32_t)ctx->gpr[3], s_search_func, s_search_user);
    if (s_search_func) search_cb(SEARCH_EV_INITIALIZE, CELL_OK);
    ctx->gpr[3] = CELL_OK;
}

void search_finalize(ppu_context* ctx)
{
    SLOG("[jsp-sysutil] cellSearchFinalize()\n");
    if (s_search_func) search_cb(SEARCH_EV_FINALIZE, CELL_OK);
    ctx->gpr[3] = CELL_OK;
}

// Searches, selection contexts: not supported (no media library).
void search_unsupported(ppu_context* ctx)
{
    SLOG("[jsp-sysutil] cellSearch* (unsupported) r3=0x%X\n", (uint32_t)ctx->gpr[3]);
    ctx->gpr[3] = 0x8002C10Cu;   // CELL_SEARCH_ERROR_NOT_SUPPORTED_SEARCH
}

void search_end(ppu_context* ctx) { ctx->gpr[3] = CELL_OK; }

// cellMusicInitialize2SystemWorkload(mode, func, userData, spurs, priority, attr)
void music_initialize_sw(ppu_context* ctx)
{
    s_music_func = (uint32_t)ctx->gpr[4];
    s_music_user = (uint32_t)ctx->gpr[5];
    SLOG("[jsp-sysutil] cellMusicInitialize2SystemWorkload(mode=%u, func=0x%08X)\n",
         (uint32_t)ctx->gpr[3], s_music_func);
    if (s_music_func) music_cb(MUSIC2_EV_INITIALIZE, CELL_OK);
    ctx->gpr[3] = CELL_OK;
}

void music_finalize(ppu_context* ctx)
{
    SLOG("[jsp-sysutil] cellMusicFinalize2()\n");
    if (s_music_func) music_cb(MUSIC2_EV_FINALIZE, CELL_OK);
    ctx->gpr[3] = CELL_OK;
}

// cellMusicSetVolume2(float level): the level arrives in f1
void music_set_volume(ppu_context* ctx)
{
    s_music_volume = (float)ctx->fpr[1];
    if (s_music_func) music_cb(MUSIC2_EV_SET_VOLUME, CELL_OK);
    ctx->gpr[3] = CELL_OK;
}

// cellMusicGetVolume2(float* level)
void music_get_volume(ppu_context* ctx)
{
    const uint32_t ea = (uint32_t)ctx->gpr[3];
    if (ea) { union { float f; uint32_t u; } v; v.f = s_music_volume; vm_write32(ea, v.u); }
    ctx->gpr[3] = CELL_OK;
}

// cellMusicGetPlaybackStatus2(int32_t* status): always STOP (0)
void music_get_status(ppu_context* ctx)
{
    const uint32_t ea = (uint32_t)ctx->gpr[3];
    if (ea) vm_write32(ea, 0);
    ctx->gpr[3] = CELL_OK;
}

void music_set_command(ppu_context* ctx)
{
    SLOG("[jsp-sysutil] cellMusicSetPlaybackCommand2(cmd=%d)\n", (int32_t)ctx->gpr[3]);
    if (s_music_func) music_cb(MUSIC2_EV_SET_PLAYBACK_COMMAND, CELL_OK);
    ctx->gpr[3] = CELL_OK;
}

void music_set_context(ppu_context* ctx)
{
    if (s_music_func) music_cb(MUSIC2_EV_SET_SELECTION_CONTEXT, CELL_OK);
    ctx->gpr[3] = CELL_OK;
}

}  // namespace

extern "C" void jsp_register_sysutil_overrides(void)
{
    ps3_hle_register_ctx(0xC81CCF8Au, "cellSearchInitialize", search_initialize);
    ps3_hle_register_ctx(0xBFAB7616u, "cellSearchFinalize", search_finalize);
    ps3_hle_register_ctx(0x774033D6u, "cellSearchEnd", search_end);
    ps3_hle_register_ctx(0x0A4C8295u, "cellSearchStartListSearch", search_unsupported);
    ps3_hle_register_ctx(0x64FB0B76u, "cellSearchStartContentSearchInList", search_unsupported);
    ps3_hle_register_ctx(0x3B210319u, "cellSearchGetContentInfoByOffset", search_unsupported);
    ps3_hle_register_ctx(0x025CE169u, "cellSearchGetMusicSelectionContext", search_unsupported);
    ps3_hle_register_ctx(0xED20E079u, "cellSearchGetMusicSelectionContextOfSingleTrack", search_unsupported);

    ps3_hle_register_ctx(0x61865281u, "cellMusicInitialize2SystemWorkload", music_initialize_sw);
    ps3_hle_register_ctx(0x7BE4DC31u, "cellMusicFinalize2", music_finalize);
    ps3_hle_register_ctx(0x4014C246u, "cellMusicSetVolume2", music_set_volume);
    ps3_hle_register_ctx(0xF9073A24u, "cellMusicGetVolume2", music_get_volume);
    ps3_hle_register_ctx(0x648B7611u, "cellMusicGetPlaybackStatus2", music_get_status);
    ps3_hle_register_ctx(0x98947A6Eu, "cellMusicSetPlaybackCommand2", music_set_command);
    ps3_hle_register_ctx(0x2BDC5D6Bu, "cellMusicSetSelectionContext2", music_set_context);
}
