#include <set>
#include <map>
/*
 * jstars_game.cpp -- cellGame game-data install handshake for the J-Stars port.
 *
 * J-Stars installs data to /dev_hdd0/game/BLUS31519-INSTALL on first boot.
 * The firmware handshake is:
 *
 *   cellGameDataCheck(GAMEDATA, dir)  -> CELL_GAME_RET_NONE   (no data yet)
 *   cellGameCreateGameData(init, ...) -> temp content/USRDIR paths
 *   ... the title writes its files ...
 *   cellGameContentPermit(...)        -> data becomes real, PARAM.SFO written
 *
 * and a directory only counts as game data once it holds a PARAM.SFO.
 *
 * The runtime's libs/system/cellGame.c differs in two ways that together
 * produced BROKEN_EXIT_GAMEDATA: ContentPermit creates the game-data
 * directories even when the check found nothing and nothing was created, and
 * DataCheck treats any existing directory as installed data. So the first
 * boot left an empty BLUS31519-INSTALL behind, and every later boot found
 * "data" with nothing in it.
 *
 * These overrides wrap the runtime's functions (which keep the check-session
 * state ContentPermit relies on) and fix just those two points, plus write a
 * PARAM.SFO from the title's CellGameSetInitParams in CreateGameData so the
 * installed data is recognised on the next boot.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>        /* GetTickCount64 */
#include <tlhelp32.h>       /* SIX_WATCH: the game's threads */

#include "ps3emu/error_codes.h"
#include "ppu_recomp.h"     /* ppu_context, vm_base */

extern "C" void ps3_hle_register_ctx(uint32_t nid, const char* name,
                                     void (*fn)(ppu_context*));
/* Runtime implementations; their pointer parameters are guest EAs. */
extern "C" int32_t cellGameDataCheck(uint32_t type, const char* dirName, void* size);
extern "C" int32_t cellGameContentPermit(char* contentInfoPath, char* usrdirPath);
extern "C" int32_t cellGameCreateGameData(void* init, char* tmp_contentInfoPath, char* tmp_usrdirPath);
extern "C" volatile unsigned long long g_ppu_fs_last_read_ms;   /* ppu_fs.cpp: last cellFsRead */
extern "C" void (*g_spu_put_hook_img)(int image_id, uint32_t ea, uint32_t size);   /* spu_channels.c */
extern "C" void (*g_cellpad_filter)(uint32_t port_no, uint16_t* buttons, uint16_t* lx, uint16_t* ly);   /* cellPad.c */
extern "C" void (*g_cellpad_rstick_filter)(uint32_t port_no, uint16_t* rx, uint16_t* ry);   /* cellPad.c */
extern "C" int g_cellpad_want_p2;   /* cellPad.c */
extern "C" { int g_jstars_fb_rows = 2; }   /* rows in the Free Battle list, read by func_001E6E98 (hand edit in ppu_recomp_010.cpp) */
extern "C" void (*g_tex_patch_hook)(uint32_t off, uint32_t w, uint32_t h, uint32_t fmt);   /* rsx_d3d12_backend.c */
extern "C" int (*g_pad_arrows_dpad)(void);   /* cellPad.c */
extern "C" int (*g_pad_mouse_look)(void);    /* cellPad.c, INPUT_MODE=kbm */
extern "C" int (*g_pad_mouse_menus)(void);   /* cellPad.c, INPUT_MODE=kbm */
extern "C" float (*g_rsx_ui_scale)(uint32_t rt_off, const float* box, float* ax, float* ay);   /* rsx_d3d12_backend.c */
extern "C" int (*g_rsx_aa_mode)(uint32_t rt_off);   /* rsx_d3d12_backend.c */
extern "C" int (*g_pad_menu_jump)(int index, int from);   /* cellPad.c */
extern "C" int (*g_pad_menu_layout)(int* cursor, const short (**rects)[4], int* count);   /* cellPad.c */
extern "C" int  cellOskDialog_overlay_active(void);              /* cellOskDialog.c */
extern "C" int  cellOskDialog_overlay_key(unsigned vk);
extern "C" int  cellOskDialog_confirm_open(const wchar_t* prompt);
extern "C" int  cellOskDialog_confirm_poll(void);
extern "C" int  cellOskDialog_choice_open(const wchar_t* prompt, const wchar_t* left, const wchar_t* right);
extern "C" void rsx_d3d12_request_close(void);                   /* rsx_d3d12_backend.c */
extern "C" int (*g_rsx_copy_rects)(short (*rects)[6], int max);   /* rsx_d3d12_backend.c */
extern "C" const uint32_t* (*g_rsx_stamp)(short* x, short* y, short* w, short* h, short* show_w, short* show_h);   /* rsx_d3d12_backend.c */
extern "C" const uint32_t* (*g_rsx_stamp2)(short* x, short* y, short* w, short* h, short* show_w, short* show_h);  /* rsx_d3d12_backend.c */
extern "C" int (*g_ppu_icall_hook)(uint32_t target, uint64_t* gpr);   /* ppu_loader.cpp */

namespace {

enum : uint32_t {
    GAMETYPE_DISC = 1,
    RET_NONE      = 2,      /* CELL_GAME_RET_NONE */
};

/* State of the current check session, as the overrides see it. */
bool        s_data_absent = false;   /* last GAMEDATA check found no data ... */
bool        s_created     = false;   /* ... and CreateGameData has not run since */
std::string s_dir;                   /* dirName of the last check */
/* PARAM.SFO for data being created, written at ContentPermit -- the point
 * firmware commits an install -- so an install that is interrupted leaves no
 * PARAM.SFO and is offered again on the next boot. */
std::vector<std::pair<std::string, std::string>> s_pending_sfo;

/* Host directory that /dev_hdd0/game/<dir> maps to (ppu_fs: PS3_HDD0_ROOT). */
std::string host_game_dir(const std::string& dir)
{
    const char* root = std::getenv("PS3_HDD0_ROOT");
    std::string r = root && *root ? root : ".";
    return r + "/game/" + dir;
}

bool file_exists(const std::string& p)
{
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

void set_ret(ppu_context* ctx, int32_t rc) { ctx->gpr[3] = (uint64_t)(int64_t)rc; }

/* Minimal PSF (PARAM.SFO) writer: UTF-8 string entries only, keys sorted as
 * the format requires. Little-endian header and index, as sfo_read_string in
 * cellGame.c expects. */
bool write_param_sfo(const std::string& path,
                     std::vector<std::pair<std::string, std::string>> kv)
{
    std::sort(kv.begin(), kv.end());
    std::vector<uint8_t> keys, data;
    struct Ent { uint16_t key_off; uint32_t len, max, data_off; };
    std::vector<Ent> ents;
    for (auto& e : kv) {
        Ent en;
        en.key_off = (uint16_t)keys.size();
        keys.insert(keys.end(), e.first.begin(), e.first.end());
        keys.push_back(0);
        en.len = (uint32_t)e.second.size() + 1;
        en.max = (en.len + 3u) & ~3u;
        en.data_off = (uint32_t)data.size();
        data.insert(data.end(), e.second.begin(), e.second.end());
        data.resize(data.size() + (en.max - (uint32_t)e.second.size()), 0);
        ents.push_back(en);
    }
    while (keys.size() % 4) keys.push_back(0);
    const uint32_t key_tab  = 0x14 + 0x10 * (uint32_t)ents.size();
    const uint32_t data_tab = key_tab + (uint32_t)keys.size();

    std::vector<uint8_t> out;
    auto u16 = [&](uint32_t v) { out.push_back(v & 0xFF); out.push_back((v >> 8) & 0xFF); };
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) out.push_back((v >> (8 * i)) & 0xFF); };
    out.insert(out.end(), { 0x00, 'P', 'S', 'F' });
    u32(0x00000101);                  /* version 1.1 */
    u32(key_tab);
    u32(data_tab);
    u32((uint32_t)ents.size());
    for (auto& en : ents) { u16(en.key_off); u16(0x0204); u32(en.len); u32(en.max); u32(en.data_off); }
    out.insert(out.end(), keys.begin(), keys.end());
    out.insert(out.end(), data.begin(), data.end());

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

std::string guest_cstr(uint32_t ea, size_t max)
{
    if (!ea) return std::string();
    const char* p = (const char*)(vm_base + ea);
    size_t n = 0;
    while (n < max && p[n]) n++;
    return std::string(p, n);
}

/* ---- HLE entry points ------------------------------------------------- */

/* cellGameDataCheck(type, dirName, size) */
void h_data_check(ppu_context* ctx)
{
    uint32_t type = (uint32_t)ctx->gpr[3];
    uint32_t dir_ea = (uint32_t)ctx->gpr[4], size_ea = (uint32_t)ctx->gpr[5];
    int32_t rc = cellGameDataCheck(type, (const char*)(uintptr_t)dir_ea, (void*)(uintptr_t)size_ea);
    std::fprintf(stderr, "[jstars-game] DataCheck(type=%u) t=%llums\n", type,
                 (unsigned long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now().time_since_epoch()).count());

    s_dir = guest_cstr(dir_ea, 32);
    s_created = false;
    s_data_absent = false;
    if (type != GAMETYPE_DISC && !s_dir.empty()) {
        std::string sfo = host_game_dir(s_dir) + "/PARAM.SFO";
        if (rc == CELL_OK && !file_exists(sfo)) {
            /* A directory without PARAM.SFO is not game data. */
            std::printf("[jstars-game] DataCheck('%s'): directory has no PARAM.SFO -> RET_NONE\n",
                        s_dir.c_str());
            if (size_ea) vm_write32(size_ea + 4, 0);   /* sizeKB = 0 */
            rc = (int32_t)RET_NONE;
        }
        s_data_absent = (rc == (int32_t)RET_NONE);

        /* Firmware takes real time here (PARAM.SFO read, size accounting) and
         * the partition switch J-Stars does right after a successful check
         * opens every CPK on the Blu-ray drive, so on hardware the boot logo
         * (game/logo/logo_US_v.pak, ~0.8 MB streamed from game.cpk) has long
         * finished streaming before the CPKs are re-bound. Here the check and
         * the re-bind are instant, the re-bind lands mid-stream, the next
         * block's CRI load finds no binder (it falls back to a loose-file open
         * that fails), the file server skips that block, and its chunk parser
         * then memcpys ~470 MB over the job system's hash table.
         *
         * A fixed delay was not enough (a slower run lost the race again), so
         * hold the check until file streaming has gone quiet: at least
         * JSTARS_DATACHECK_MS (default 1000), then until no cellFsRead for
         * 750 ms, at most 15 s. */
        if (rc == CELL_OK) {
            const char* ms_env = std::getenv("JSTARS_DATACHECK_MS");
            int min_ms = ms_env ? std::atoi(ms_env) : 1000;
            if (min_ms > 0) {
                using namespace std::chrono;
                auto t0 = steady_clock::now();
                std::this_thread::sleep_for(milliseconds(min_ms));
                for (;;) {
                    unsigned long long quiet = GetTickCount64() - g_ppu_fs_last_read_ms;
                    if (quiet >= 750) break;
                    if (steady_clock::now() - t0 > seconds(15)) {
                        std::fprintf(stderr, "[jstars-game] DataCheck: file I/O never went quiet, continuing\n");
                        break;
                    }
                    std::this_thread::sleep_for(milliseconds(50));
                }
                std::fprintf(stderr, "[jstars-game] DataCheck held %lld ms for file I/O to settle\n",
                             (long long)duration_cast<milliseconds>(steady_clock::now() - t0).count());
            }
        }
    }
    set_ret(ctx, rc);
}

/* cellGameCreateGameData(init, tmp_contentInfoPath, tmp_usrdirPath) */
void h_create_game_data(ppu_context* ctx)
{
    uint32_t init_ea = (uint32_t)ctx->gpr[3];
    int32_t rc = cellGameCreateGameData((void*)(uintptr_t)init_ea,
                                        (char*)(uintptr_t)(uint32_t)ctx->gpr[4],
                                        (char*)(uintptr_t)(uint32_t)ctx->gpr[5]);
    if (rc == CELL_OK && init_ea && !s_dir.empty()) {
        /* CellGameSetInitParams: title[128], titleId[10], reserved[2], version[6]. */
        std::string title    = guest_cstr(init_ea + 0x00, 128);
        std::string title_id = guest_cstr(init_ea + 0x80, 10);
        std::string version  = guest_cstr(init_ea + 0x8C, 6);
        s_pending_sfo = { { "CATEGORY", "GD" }, { "TITLE", title }, { "TITLE_ID", title_id },
                          { "VERSION", version }, { "APP_VER", version } };
        std::printf("[jstars-game] CreateGameData('%s'): title='%s' id='%s' ver='%s' "
                    "(PARAM.SFO written at ContentPermit)\n",
                    s_dir.c_str(), title.c_str(), title_id.c_str(), version.c_str());
        s_created = true;
        s_data_absent = false;
    }
    set_ret(ctx, rc);
}

/* cellGameContentPermit(contentInfoPath, usrdirPath) */
void h_content_permit(ppu_context* ctx)
{
    uint32_t cip_ea = (uint32_t)ctx->gpr[3], usr_ea = (uint32_t)ctx->gpr[4];
    if (s_data_absent && !s_created) {
        /* Nothing to permit: no data and none created. Firmware returns empty
         * paths here and creates nothing. */
        std::printf("[jstars-game] ContentPermit: no game data for '%s' -> empty paths, nothing created\n",
                    s_dir.c_str());
        if (cip_ea) vm_write8(cip_ea, 0);
        if (usr_ea) vm_write8(usr_ea, 0);
        set_ret(ctx, CELL_OK);
        return;
    }
    int32_t rc = cellGameContentPermit((char*)(uintptr_t)cip_ea, (char*)(uintptr_t)usr_ea);
    if (rc == CELL_OK && s_created && !s_pending_sfo.empty()) {
        std::string sfo = host_game_dir(s_dir) + "/PARAM.SFO";
        bool ok = write_param_sfo(sfo, s_pending_sfo);
        std::printf("[jstars-game] ContentPermit: install of '%s' committed -> PARAM.SFO %s\n",
                    s_dir.c_str(), ok ? "written" : "NOT written");
        s_pending_sfo.clear();
        s_created = false;
    }
    set_ret(ctx, rc);
}

/* ---------------------------------------------------------------------------
 * PC wording for the save, install and trophy messages (PC_TEXT=0: off)
 *
 * The game's message boxes say "Do not turn off your PlayStation(R)3 system."
 * while it saves, loads, creates save data or installs. The texts live in the
 * game's packed data files (UTF-16LE, one string per entry, NUL-terminated),
 * which the decompressor SPU job (spujob_80B700, image 3) unpacks into guest
 * memory. Every PUT that job makes is checked for "PlayStation(R)3"; a string
 * holding it is rewritten in place with the PC wording ("close the game"),
 * shorter, so the rest of the entry is NUL padding and no offsets move.
 * The window reaches back 512 bytes so a string split across two PUTs is
 * caught on the second. PC_TEXT_DBG=1 logs every rewrite.
 * -----------------------------------------------------------------------*/
const int kDecompressorImage = 3;

struct TextRule { const char16_t* from; const char16_t* to; };
const TextRule k_text_rules[] = {
    { u"remove the hard disk drive or turn off \nthe PlayStation®3 system", u"close the game" },
    { u"turn off your PlayStation®3 system", u"close the game" },
    { u"switch off the PlayStation®3 system", u"close the game" },
};
/* Exit Game (EXIT_OPTION, below): the Options help texts for Cross-Save. */
const TextRule k_exit_rules[] = {
    { u"Upload or download game progress data.", u"Exit the game." },
    { u"Change game settings or Cross-Save.", u"Change game settings or quit." },
};
/* A string is looked at when it holds one of these. */
const char16_t* const k_text_markers[] = { u"PlayStation®3", u"Upload or download game progress", u"or Cross-Save." };

inline char16_t rd16(const uint8_t* p) { return (char16_t)(p[0] | (p[1] << 8)); }

void pc_text_put(int image_id, uint32_t ea, uint32_t size)
{
    if (image_id != kDecompressorImage || !vm_base || size < 2) return;
    static int dbg = -1;
    if (dbg < 0) { const char* e = std::getenv("PC_TEXT_DBG"); dbg = (e && e[0] != '0') ? 1 : 0; }
    uint32_t lo = ea >= 512u ? ea - 512u : 0u;
    MEMORY_BASIC_INFORMATION mi;                /* reach back only inside committed memory */
    if (!VirtualQuery(vm_base + ea, &mi, sizeof mi)) return;
    const uint32_t base = (uint32_t)((uint8_t*)mi.AllocationBase - vm_base);
    if (VirtualQuery(vm_base + lo, &mi, sizeof mi) && mi.State != MEM_COMMIT) lo = ea;
    if (lo < base) lo = ea;
    const uint32_t hi = ea + size;
    static int exit_text = -1;
    if (exit_text < 0) { const char* e = std::getenv("EXIT_OPTION"); exit_text = (e && e[0] == '0') ? 0 : 1; }
    for (uint32_t a = lo; a + 2 <= hi; a++) {
        const uint8_t* p = vm_base + a;
        if ((p[0] != 'P' && p[0] != 'U' && p[0] != 'o') || p[1] != 0) continue;
        bool hit = false;
        for (const char16_t* m : k_text_markers) {
            size_t n = 0; while (m[n]) n++;
            if (a + 2 * n > hi) continue;
            bool same = true;
            for (size_t i = 0; i < n && same; i++) same = rd16(p + 2 * i) == m[i];
            if (same) { hit = true; break; }
        }
        if (!hit) continue;
        uint32_t s = a;                                       /* start: after a NUL or BOM */
        while (s >= lo + 2 && a - s < 1024) {
            const char16_t c = rd16(vm_base + s - 2);
            if (c == 0 || c == 0xFEFF) break;
            s -= 2;
        }
        uint32_t e = a;                                       /* end: the NUL */
        while (e + 2 <= hi && rd16(vm_base + e)) e += 2;
        if (e + 2 > hi) continue;                             /* rest not written yet */
        std::u16string str;
        for (uint32_t q = s; q < e; q += 2) str.push_back(rd16(vm_base + q));
        const std::u16string before = str;
        for (const TextRule& r : k_text_rules) {
            const std::u16string from(r.from), to(r.to);
            for (size_t at; (at = str.find(from)) != std::u16string::npos; )
                str.replace(at, from.size(), to);
        }
        if (exit_text)
            for (const TextRule& r : k_exit_rules) {
                const std::u16string from(r.from), to(r.to);
                for (size_t at; (at = str.find(from)) != std::u16string::npos; )
                    str.replace(at, from.size(), to);
            }
        if (str == before || str.size() > before.size()) { a = e; continue; }
        for (size_t i = 0; i < before.size(); i++) {         /* rewrite, NUL-pad the rest */
            const char16_t c = i < str.size() ? str[i] : 0;
            vm_base[s + 2 * i] = (uint8_t)c; vm_base[s + 2 * i + 1] = (uint8_t)(c >> 8);
        }
        if (dbg) {
            std::string ascii;
            for (char16_t c : str) ascii.push_back(c == '\n' ? '|' : (c < 0x80 ? (char)c : '?'));
            std::fprintf(stderr, "[pc-text] 0x%08X: \"%s\"\n", s, ascii.c_str());
        }
        a = e;
    }
}

/* ---------------------------------------------------------------------------
 * Exit Game in Options (EXIT_OPTION=0: off)
 *
 * The Options list's last entry, Cross-Save (PSN upload/download, dead on PC),
 * becomes Exit Game: Cross on it opens the port's Yes/No box (the text panel
 * in confirm mode); Yes closes the game window, No returns to the list. Read
 * from the game's own state (static data in the EBOOT, found by diffing
 * memory dumps on each screen, docs/notes FINDINGS "Exit Game"):
 *   0x008E63FC  Options cursor, counted from the bottom (Cross-Save = 0)
 *   0x0096CD60 / 0x0096CCB8  2 / 2 while the Options list itself is up
 *   0x0095654C  5 on the Options list (9 in the Cross-Save submenu)
 * The pad filter sees every cellPadGetData: it swallows that Cross, drives
 * the box from the pad while it is open (the keyboard reaches it through the
 * window), then holds input back until every button is released.
 * -----------------------------------------------------------------------*/
/* Scene step 0x0094DEC4 for the menus: 4 after boot, but 3 once a battle has
 * been played (the maintainer 10-29: Exit Game missed after a fight with step 3). */
/* A fight is up while the HUD reads a fighter's health (getter 0x00085CEC, every
 * frame; never on the menu screens, pv1). 0x0094DEC4 is no scene step to rely on:
 * it read 5 in Options and something outside 0x10-0x14 in the maintainer's Training fight
 * (10-30: no refill and no log in that fight). */
volatile unsigned long long g_hud_ms = 0;
bool fight_hud_up() { return GetTickCount64() - g_hud_ms < 500; }

bool in_menu_scene()
{
    if (fight_hud_up()) return false;
    const uint32_t s = vm_read32(0x0094DEC4u);
    /* 3 after a fight, 4 normally, 5 in one of the maintainer's sessions (10-30, the whole
     * menu system: Exit Game and the Free Battle box both failed): not fixed
     * values, so everything before the loading steps (0xB on) counts. */
    return s < 0xB;
}

bool exit_option_on_entry()
{
    static int fallback_only = -1;   /* EXIT_OPTION_FALLBACK_ONLY=1: test the submenu back-out path */
    if (fallback_only < 0) { const char* e = std::getenv("EXIT_OPTION_FALLBACK_ONLY"); fallback_only = e && e[0] == '1'; }
    if (fallback_only) return false;
    /* 0x0096CD60/CCB8 were 2/2 here only until another Options submenu had
     * been opened (then 3/1, the maintainer 10-29), so they are not checked. Inside the
     * other submenus 654C stays 5 but the cursor keeps that entry's index
     * (1-4), so cursor 0 + 654C 5 is the list with Exit Game highlighted. */
    return vm_read32(0x008E63FCu) == 0 && vm_read32(0x0095654Cu) == 5 && in_menu_scene();
}

void exit_option_filter(uint32_t port, uint16_t* b, uint16_t* lx, uint16_t* ly)
{
    if (port != 0) return;
    const uint16_t CROSS = 0x4000, CIRCLE = 0x2000, LEFT = 0x0080, RIGHT = 0x0020;
    const uint16_t START = 0x0008;   /* the menus take Start (Enter on the keyboard) as confirm too */
    static uint16_t prev = 0, prev_dir = 0;
    static int state = 0;   /* 0 idle, 1 box open, 2 waiting for release, 3 backing out of Cross-Save */
    static int polls = 0;
    static uint32_t prev_screen = 0;
    const uint16_t cur = *b, pressed = (uint16_t)(cur & ~prev);
    prev = cur;
    const uint32_t screen = vm_read32(0x0095654Cu);   /* 5 Options list, 9 Cross-Save submenu */
    const bool entered_submenu = screen == 9 && prev_screen == 5;
    prev_screen = screen;
    if (state == 3) {
        /* The Cross-Save submenu opened anyway (the maintainer, 10-28: the press-time check
         * missed on his run): pulse Circle until the list is back, then ask. */
        polls++;
        if (screen == 5 || polls > 300) {
            state = cellOskDialog_confirm_open(L"Exit the game?") ? 1 : 2; prev_dir = 0;
            *b = 0;
        } else {
            *b = (uint16_t)(((polls / 4) & 1) ? 0 : CIRCLE);
        }
        *lx = 128; *ly = 128;
        return;
    }
    if (state == 0 && entered_submenu) {
        std::printf("[jstars-game] Exit Game: Cross-Save submenu opened, backing out\n");
        state = 3; polls = 0; *b = 0;
        return;
    }
    if (state == 1) {
        if (!cellOskDialog_overlay_active()) {
            if (cellOskDialog_confirm_poll() == 1) {
                std::printf("[jstars-game] Exit Game: Yes, closing\n");
                rsx_d3d12_request_close();
            }
            state = 2;
        } else {
            /* d-pad or left stick moves the highlight, Cross presses, Circle = No */
            const uint16_t dir = (uint16_t)(((cur & LEFT) || *lx < 64) ? LEFT : ((cur & RIGHT) || *lx > 192) ? RIGHT : 0);
            if (dir && dir != prev_dir) cellOskDialog_overlay_key(dir == LEFT ? 0x25u : 0x27u);   /* VK_LEFT / VK_RIGHT */
            prev_dir = dir;
            if (pressed & CROSS) cellOskDialog_overlay_key(0x0Du);    /* VK_RETURN */
            if (pressed & CIRCLE) cellOskDialog_overlay_key(0x1Bu);   /* VK_ESCAPE */
        }
        *b = 0; *lx = 128; *ly = 128;
        return;
    }
    if (state == 2) {
        if (cur == 0) state = 0;
        else { *b = 0; return; }
    }
    const uint16_t OK = CROSS | START;
    if ((pressed & OK) && vm_read32(0x008E63FCu) == 0 && !exit_option_on_entry())
        std::printf("[jstars-game] Exit Game: %s at cursor 0 not taken (CD60=%u CCB8=%u 654C=%u)\n",
                    (pressed & CROSS) ? "Cross" : "Start", vm_read32(0x0096CD60u), vm_read32(0x0096CCB8u), screen);
    if ((pressed & OK) && vm_read32(0x008E63FCu) == 0 && !exit_option_on_entry())
        std::printf("[jstars-game]   scene step %u\n", vm_read32(0x0094DEC4u));
    if ((pressed & OK) && exit_option_on_entry() && cellOskDialog_confirm_open(L"Exit the game?")) {
        state = 1; prev_dir = 0;
        *b = (uint16_t)(cur & ~OK);
    }
}

/* The entry's label is a picture, in two 512x64 DXT5 textures (RSX 0x88) the
 * game uploads into RSX memory: white letters normally, black ones while the
 * entry is highlighted. Before the backend reads a texture, swap those exact
 * bytes (one block compared, then FNV-1a of the whole image) for the same-
 * styled "Exit Game" (exit_label_light/dark.inc, drawn by jstars-notes/tools/
 * exit_label.ps1 and packed by dxt5enc.py). */
const uint8_t k_exit_label_light[32768] = {
#include "exit_label_light.inc"
};
const uint8_t k_exit_label_dark[32768] = {
#include "exit_label_dark.inc"
};
struct ExitLabel { uint8_t block[16]; uint32_t fnv; const uint8_t* with; const char* name; };
const ExitLabel k_exit_labels[] = {
    /* block 0x18A0 (row 3 of 4x4 blocks, column 10) of each Cross-Save picture */
    {{0x00,0xFF,0x49,0x92,0x24,0x49,0x92,0x24,0xDB,0xDE,0x00,0x00,0x5C,0x54,0x56,0x95}, 0x79C18A8Bu, k_exit_label_light, "normal"},
    {{0xFB,0x76,0xE0,0x23,0xB6,0x23,0x49,0x72,0xFF,0xFF,0xE3,0x18,0x09,0x03,0x02,0x80}, 0x9ACC2F24u, k_exit_label_dark, "highlighted"},
};

void exit_label_patch(uint32_t off, uint32_t w, uint32_t h, uint32_t fmt)
{
    if (w != 512 || h != 64 || (fmt & 0x9F) != 0x88 || !vm_base) return;
    uint8_t* p = vm_base + off;
    for (const ExitLabel& l : k_exit_labels) {
        if (std::memcmp(p + 0x18A0, l.block, 16)) continue;
        uint32_t hsh = 0x811C9DC5u;
        for (uint32_t i = 0; i < 32768u; i++) hsh = (hsh ^ p[i]) * 0x01000193u;
        if (hsh != l.fnv) continue;
        std::memcpy(p, l.with, 32768u);
        std::printf("[jstars-game] Exit Game: %s label swapped at 0x%08X\n", l.name, off);
        return;
    }
}

/* ---------------------------------------------------------------------------
 * Arrow keys in menus (MENU_ARROWS_DPAD=0: off)
 *
 * PAD_KEYS_SPLIT makes the arrows move the left stick only, so in a fight they
 * no longer fire d-pad commands. Menus need the d-pad though (the shop adds to
 * the cart with right), so outside a fight the arrows press it as well.
 * 0x0094DEC4 (static EBOOT data; memory sampled every 1.5 s through a match,
 * FINDINGS "Arrow keys") is the game's scene step: 4 in the menus (main menu,
 * shop, Options, J-Customize, character select), 0xF-0x13 from stage select
 * through loading and the intro, 0x14 for the fight itself, its pause menu
 * included.
 * -----------------------------------------------------------------------*/
volatile bool g_training_mode = false;

/* The port's own screen tracking. The menu objects are on the heap and move
 * (after Online Mode the main menu was at 0x48FEF150 instead of 0x48FAD1A0, pv1;
 * The maintainer's log had another object at the old address), and the menu state values
 * change with what was opened before. What does not change is the code: each
 * screen calls its own virtual methods every frame, and they are seen in the
 * indirect-call hook with the object in r3. A screen is up while its method was
 * called in the last moment and the object still has that vtable. */
struct SeenScreen { volatile uint32_t obj; volatile unsigned long long ms; };
SeenScreen g_seen_main, g_seen_charsel, g_seen_panel, g_seen_stage, g_seen_stage_time;

bool screen_up(const SeenScreen& sc, uint32_t vtable)
{
    const uint32_t obj = sc.obj;
    return obj && GetTickCount64() - sc.ms < 300 && vm_read32(obj) == vtable;
}

/* Called from the indirect-call hook. */
void screens_note(uint32_t target, uint32_t r3)
{
    SeenScreen* sc;
    switch (target) {
    case 0x001DE098u: sc = &g_seen_main; break;        /* main menu list and 1P/2P, vtable 0x0087D600 */
    case 0x0019AA74u: sc = &g_seen_charsel; break;     /* character select, vtable 0x0087C3D0 */
    case 0x0019E2E8u:                                  /* its panels: the settings panel, vtable 0x0087C4A0 */
        if (vm_read32(r3) != 0x0087C4A0u) return;
        sc = &g_seen_panel; break;
    case 0x0026EF94u: sc = &g_seen_stage; break;       /* stage select, vtable 0x0087FCF8 */
    case 0x00279F0Cu: sc = &g_seen_stage_time; break;  /* its time limit box, vtable 0x00880018 */
    default: return;
    }
    sc->obj = r3;
    sc->ms = GetTickCount64();
}

int arrows_press_dpad()
{
    return vm_read32(0x0094DEC4u) != 0x14 && !fight_hud_up();
}

/* INPUT_MODE=kbm: the mouse steers the camera (right stick) in the same scene. */
int in_fight()
{
    return vm_read32(0x0094DEC4u) == 0x14 || fight_hud_up();
}

/* INPUT_MODE=kbm: mouse menus (cursor shown, wheel, clicks) from the main menu
 * on: scene step 3/4 (menus) and 0xF (stage select), not title, loading, fight. */
/* Point-and-click layouts (INPUT_MODE=kbm), in the 1280x720 guest screen.
 * Main menu: 9 entries on a slant, cursor 0x00956550 (0 = J-Adventure ..
 * 8 = Options). Open while 0x0096CCB0 (the menu state machine's current state)
 * is 0x40D98E98: the same in every main-menu dump of 3 runs, other values in
 * the shop (..E50), Options (..E88), Free Battle and character select
 * (mm1/mm2/dumps1/dumps2 dumps). 0x0095654C stays 0 in the shop and Arcade's
 * character select too, so it cannot tell them apart.
 * Rects measured on the 720p main menu: rows 38.5 px apart from y 170. */
const short k_main_menu_rects[9][4] = {
    { 110, 151, 320, 190 }, { 140, 190, 330, 228 }, { 160, 228, 300, 267 },
    { 195, 267, 365, 306 }, { 230, 306, 415, 344 }, { 265, 344, 435, 383 },
    { 295, 383, 470, 421 }, { 320, 421, 445, 459 }, { 350, 459, 480, 498 },
};

/* The main menu list is up: no submenu open (0x0095654C 0; 2 = Free Battle's
 * 1 Player / 2 Players, 5 Options) on the main menu screen. */
bool main_list_open()
{
    return in_menu_scene() && vm_read32(0x0095654Cu) == 0 && screen_up(g_seen_main, 0x0087D600u);
}

int menu_layout(int* cursor, const short (**rects)[4], int* count)
{
    if (main_list_open()) {
        const uint32_t c = vm_read32(0x00956550u);
        if (c > 8) return 0;
        *cursor = (int)c; *rects = k_main_menu_rects; *count = 9;
        return 1;
    }
    return 0;
}

/* UI_SCALE=<0.5..1> (launcher slider): the fight HUD's size. In a fight
 * (scene step 0x14) the HUD is drawn into the 1280x720 composite 0x0C6ABF40
 * after the scene is copied there (FINDINGS "UI size"); menus are left alone.
 * The backend passes each HUD draw's box (NDC, y up); pieces go by cluster so
 * a cluster stays in one piece: the top bars and emblem toward the top centre,
 * the bottom-left player block (portraits, health, gauge, mode box) toward
 * the bottom-left corner, the map and timer toward the bottom-right corner.
 * Everything in between (name tags and markers that follow the fighters,
 * the full-screen copy, dialogs) keeps its size and place. */
bool training_active();
/* Training setting "Hide HUD" (the maintainer 11-04, for screenshots): every HUD draw of the fight, name tags included,
 * is shrunk to nothing off screen; the scene and full-screen effects stay. Through the same hook as UI_SCALE,
 * so HUD pieces that hook does not see (FINDINGS "UI size") would stay. */
static bool env_on(const char* n) { const char* e = std::getenv(n); return e && *e && *e != '0'; }
bool g_set_hidehud = env_on("HIDEHUD_TEST");   /* HIDEHUD_TEST=1 (test only): on from the start; empty or 0 is off */

float ui_scale(uint32_t rt_off, const float* box, float* ax, float* ay)
{
    static float s = -1.0f;
    if (s < 0.0f) {
        const char* e = std::getenv("UI_SCALE");
        s = e ? (float)std::atof(e) : 1.0f;
        if (s < 0.5f) s = 0.5f;
        if (s > 1.0f) s = 1.0f;
    }
    const bool hide = g_set_hidehud && training_active();
    if ((s >= 1.0f && !hide) || (rt_off & 0x0FFFFFFFu) != 0x0C6ABF40u) return 1.0f;
    { static int dbg = -1; static uint32_t last = 0xFFFFFFFFu;
      if (dbg < 0) dbg = std::getenv("UI_SCALE_DBG") ? 1 : 0;
      const uint32_t st = vm_read32(0x0094DEC4u);
      if (dbg && st != last) { last = st; std::fprintf(stderr, "[UIDBG] scene step %u\n", st); } }
    /* 0x14 is the fight, but the step drops to 0x13 for a frame now and then
     * during it (ui6 log), and the HUD already shows at 0x13 before the fight
     * starts: both count, or the HUD pops to full size for a frame. */
    { const uint32_t st = vm_read32(0x0094DEC4u); if (st != 0x13 && st != 0x14 && !fight_hud_up()) return 1.0f; }
    /* Split screen (2 players on different teams) has its own HUD layout;
     * the 1P clusters below would scatter it, so it keeps its size. Each frame
     * the HUD starts with the full-screen copy of the scene (resets the flag)
     * and, in split screen, four team boxes x -1..0 / 0..1, y 0.647..1 /
     * -1..-0.353 (FINDINGS "2-player split screen"). */
    static bool split = false;
    /* The name tags over the fighters are drawn right after the scene copy, before every other HUD part: name
     * strip and text, plate, bars. They follow the fighter into the top or bottom cluster's area, and going by
     * position there split the bars from the plate (the maintainer 10-30, pf1). So they are told apart by order and height:
     * the first part of another height ends them for that frame. */
    static bool tags = false;
    /* The Victory Burst banner (a row per fighter: star, "Friendship Burst!" text, name bar, 720p y 555-630,
     * x 640-1240) sits across the bottom-left and bottom-right areas: shrunk by area its star went to the
     * corner, its bar and name to the map's corner and its text stayed (the maintainer 11-04 screenshot). The fight
     * HUD is hidden while it shows, so a frame with banner pieces keeps the whole bottom at full size. The
     * piece that tells: the "Friendship Burst!" text (centre x 0.15..0.35, 0.3..0.45 wide, 0.055..0.078 high,
     * on screen), where no fight HUD part sits; looked at after the name tags, which have the same width but
     * are taller and also come off screen (vb7 log). Seen in this frame or the one before. */
    static bool banner_prev = false, banner_cur = false;
    static float s_bbox[4] = { 0, 0, 0, 0 };   /* UI_SCALE_DBG: the first banner piece of the frame */
    if (box[2] - box[0] > 1.8f && box[3] - box[1] > 1.8f) {   /* full-screen pass */
        /* only the scene copy (exactly -1..1) starts a frame; other big quads
         * (fades, the vs5 overlay at +-1.05) come later in the same frame */
        if (box[0] > -1.001f && box[1] > -1.001f && box[2] < 1.001f && box[3] < 1.001f) {
            split = false; tags = true;
            if (banner_cur != banner_prev) {
                static int bdbg = -1; if (bdbg < 0) bdbg = std::getenv("UI_SCALE_DBG") ? 1 : 0;
                if (bdbg) std::fprintf(stderr, "[UIDBG] burst banner %s t=%llu (%+.3f..%+.3f, %+.3f..%+.3f)\n", banner_cur ? "on" : "off",
                                       (unsigned long long)GetTickCount64(), s_bbox[0], s_bbox[2], s_bbox[1], s_bbox[3]);
            }
            banner_prev = banner_cur; banner_cur = false;
        }
        return 1.0f;
    }
    if (hide) { *ax = 8.0f; *ay = 8.0f; return 0.0001f; }   /* Hide HUD: to a point well off screen */
    if (box[0] <= -0.99f && box[2] >= -0.01f && box[2] <= 0.01f && box[1] >= 0.62f && box[3] >= 0.99f) split = true;
    if (box[0] <= -0.99f && box[2] >= -0.01f && box[2] <= 0.01f && box[1] <= -0.99f && box[3] > -0.4f && box[3] < -0.3f) split = true;   /* bottom box, drawn first */
    if (box[0] <= -0.99f && box[2] >= 0.99f && box[3] - box[1] < 0.1f && box[1] > 0.6f && box[3] < 0.7f) split = true;   /* separator strip */
    if (split) return 1.0f;
    if (tags) {
        const float w = box[2] - box[0], h = box[3] - box[1];
        if (w < 0.7f && ((h > 0.03f && h < 0.05f) || (h > 0.078f && h < 0.098f) || (h > 0.19f && h < 0.22f))) return 1.0f;
        tags = false;
    }
    { const float bcx = (box[0] + box[2]) * 0.5f, bcy = (box[1] + box[3]) * 0.5f, bw = box[2] - box[0], bh = box[3] - box[1];
      if (box[0] > -1.0f && box[2] < 1.0f && bcy > -0.76f && bcy < -0.48f && bh > 0.055f && bh < 0.078f &&
          bcx > 0.15f && bcx < 0.35f && bw > 0.3f && bw < 0.45f)
      { if (!banner_cur) { s_bbox[0] = box[0]; s_bbox[1] = box[1]; s_bbox[2] = box[2]; s_bbox[3] = box[3]; } banner_cur = true; } }
    const float cx = (box[0] + box[2]) * 0.5f, cy = (box[1] + box[3]) * 0.5f;
    if (cy > 0.65f) { *ax = 0.0f; *ay = 1.0f; return s; }                  /* top: y < 126 px (bars, emblem, its pointer) */
    if (cy < -0.45f && !banner_prev && !banner_cur) {                      /* bottom: y > 522 px */
        if (cx < 0.2f)  { *ax = -1.0f; *ay = -1.0f; return s; }            /* x < 768 px */
        if (cx > 0.45f) { *ax = 1.0f;  *ay = -1.0f; return s; }            /* x > 928 px */
    }
    return 1.0f;
}

/* AA_MODE=off|fxaa|fxaa2|smaa (launcher): edge smoothing, in fights only, on the scene as it is
 * copied into the HUD's target: menus and the HUD are left alone. Same fight test as ui_scale. */
int aa_mode(uint32_t rt_off)
{
    static int m = -1;
    if (m < 0) {
        const char* e = std::getenv("AA_MODE");
        m = !e ? 0 : !std::strcmp(e, "fxaa") ? 1 : !std::strcmp(e, "fxaa2") ? 2 : !std::strcmp(e, "smaa") ? 3 : 0;
    }
    if (!m || (rt_off & 0x0FFFFFFFu) != 0x0C6ABF40u) return 0;
    const uint32_t st = vm_read32(0x0094DEC4u);
    return st == 0x13 || st == 0x14 || fight_hud_up() ? m : 0;
}

/* Point-and-click: move the main menu's cursor straight to the entry next to
 * the hovered one (the maintainer: no stepping through the list); cellPad's one d-pad
 * step then lands on it, so the game moves its highlight itself (pc5: writing
 * the cursor onto the target directly left the highlight on the old entry). */
int menu_jump(int target, int from)
{
    const int index = target > from ? target - 1 : target + 1;
    if (!(main_list_open()) || index < 0 || index > 8) return 0;
    /* 0x00956550 alone is only a copy (pc4: the game put it back). The menu
     * keeps the index as u16 in six fields of the menu object too (mm1, mm2,
     * dumps1): write them all. */
    static const uint32_t k_cursor_u16[6] = { 0x46u, 0x272u, 0x39Eu, 0x4F2u, 0x4FAu, 0x63Eu };
    const uint32_t cur = vm_read32(0x00956550u);
    const uint32_t menu = g_seen_main.obj;
    for (uint32_t a : k_cursor_u16)
        if (vm_read16(menu + a) != cur) return 0;   /* layout not as measured: step instead */
    for (uint32_t a : k_cursor_u16) vm_write16(menu + a, (uint16_t)index);
    vm_write32(0x00956550u, (uint32_t)index);
    return 1;
}

int in_mouse_menus()
{
    const uint32_t s = vm_read32(0x0094DEC4u);
    return !fight_hud_up() && (s < 0xB || s == 0xF);
}

/* TRAINING_PROBE=1 (research only): counts the indirect calls made 10-30 s into
 * a fight, per target and per object, and prints them with a dump of every pad
 * object. Used to find the CPU fighters' pads (called on the 3 CPU fighters,
 * not on the human's). */
struct ProbeStat { unsigned count; int n; uint32_t obj[8]; unsigned objc[8]; int nr4; uint32_t r4[6]; };
std::unordered_map<uint32_t, ProbeStat>* g_probe = nullptr;
std::mutex g_probe_mu;
volatile int g_probe_on = 0;   /* 0 off, 1 counting, 2 done */

void probe_icall(uint32_t target, uint32_t r3, uint32_t r4)
{
    std::lock_guard<std::mutex> lk(g_probe_mu);
    if (g_probe_on != 1) return;
    ProbeStat& st = (*g_probe)[target];
    st.count++;
    { int k = 0; for (; k < st.nr4; k++) if (st.r4[k] == r4) break;
      if (k == st.nr4 && st.nr4 < 6) st.r4[st.nr4++] = r4; }
    for (int i = 0; i < st.n; i++) if (st.obj[i] == r3) { st.objc[i]++; return; }
    if (st.n < 8) { st.obj[st.n] = r3; st.objc[st.n] = 1; st.n++; }
}

bool probe_called_on(uint32_t target, uint32_t obj)
{
    auto it = g_probe->find(target);
    if (it == g_probe->end()) return false;
    for (int k = 0; k < it->second.n; k++) if (it->second.obj[k] == obj) return true;
    return false;
}

void probe_tick()
{
    static unsigned long long fight_t0 = 0;
    if (g_probe_on == 2) return;
    if (vm_read32(0x0094DEC4u) != 0x14) return;
    const unsigned long long now = GetTickCount64();
    if (!fight_t0) fight_t0 = now;
    if (g_probe_on == 0 && now - fight_t0 > 10000) {
        std::lock_guard<std::mutex> lk(g_probe_mu);
        g_probe = new std::unordered_map<uint32_t, ProbeStat>(); g_probe_on = 1;
        std::printf("[probe] counting\n");
    } else if (g_probe_on == 1 && now - fight_t0 > 30000) {
        std::lock_guard<std::mutex> lk(g_probe_mu);
        g_probe_on = 2;
        auto pads = g_probe->find(0x0009D6A8u);
        if (pads != g_probe->end()) {
            const ProbeStat& st = pads->second;
            for (int i = 0; i < st.n; i++) {
                const uint32_t o = st.obj[i];
                if (vm_read32(o) != 0x00875858u) continue;
                std::printf("[probe] pad %08X %s\n", o, probe_called_on(0x0009D680u, o) ? "cpu" : "human?");
                for (uint32_t off = 0; off < 0x300; off += 16) {
                    std::printf("[probe] pad %08X +%03X:", o, off);
                    for (uint32_t w = 0; w < 16; w += 4) std::printf(" %08X", vm_read32(o + off + w));
                    std::printf("\n");
                }
            }
        }
        for (auto& kv : *g_probe) {
            const ProbeStat& st = kv.second;
            if (st.count < 200) continue;
            std::printf("[probe] t %08X n %u objs %d:", kv.first, st.count, st.n);
            for (int i = 0; i < st.n; i++)
                std::printf(" %08X/%08X x%u", st.obj[i], (st.obj[i] && st.obj[i] < 0xF0000000u) ? vm_read32(st.obj[i]) : 0u, st.objc[i]);
            std::printf("\n");
        }
        std::printf("[probe] done\n");
        std::fflush(stdout);
    }
}

/* TRAINING_PROBE_FLAG=<file> (research only): count while that file exists;
 * when it goes away, print every target called in that window (with up to 6
 * distinct r4 values) and get ready for the next window. */
void probe_flag_tick()
{
    static const char* path = std::getenv("TRAINING_PROBE_FLAG");
    static int window = 0;
    if (!path) return;
    const bool on = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    std::lock_guard<std::mutex> lk(g_probe_mu);
    if (on && g_probe_on != 1) {
        if (!g_probe) g_probe = new std::unordered_map<uint32_t, ProbeStat>();
        g_probe->clear(); g_probe_on = 1; window++;
        std::printf("[probe] window %d counting\n", window);
    } else if (!on && g_probe_on == 1) {
        g_probe_on = 0;
        for (auto& kv : *g_probe) {
            const ProbeStat& st = kv.second;
            std::printf("[probe] w%d t %08X n %u objs %d r4:", window, kv.first, st.count, st.n);
            for (int k = 0; k < st.nr4; k++) std::printf(" %X", st.r4[k]);
            std::printf(" |");
            for (int i = 0; i < st.n && i < 3; i++) std::printf(" %08X/%08X", st.obj[i], (st.obj[i] && st.obj[i] < 0xF0000000u) ? vm_read32(st.obj[i]) : 0u);
            std::printf("\n");
        }
        std::printf("[probe] window %d done\n", window);
        std::fflush(stdout);
    }
}

/* The battle setup character select edits (FINDINGS "Training mode step 3"):
 * one 0x2C record per slot at 0x009540E4 (0 P1, 1 P1's partner, 2 the
 * opponent, 3 the opponent's partner; the pads' +0x14 slot numbers), with +0x00
 * driver (0 Player 1, 2 CPU), +0x08 state (0 picked, 1 random, 2 not taking
 * part), +0x0C character, +0x18 COM level (0-4 = 1-5 stars). */
uint32_t setup_field(int slot, uint32_t off) { return 0x009540E4u + 0x2Cu * (uint32_t)slot + off; }

/* 0-star choice: the slots whose CPU is a training dummy. The game keeps COM
 * level 0 (1 star); the port draws it as 0 stars and empties that CPU's pad. */
volatile bool g_dummy[4] = { false, false, false, false };
volatile int g_dummy_live = 0;

/* In a fight the pads number the fighters that take part, in setup order
 * (cs7: P1 against one opponent gave the opponent's pad slot 1, not 2). */
int training_dummy_mask()
{
    int m = 0, fighter = 0;
    for (int k = 0; k < 4; k++) {
        if (vm_read32(setup_field(k, 0x08)) == 2) continue;   /* not taking part */
        if (g_dummy[k] && vm_read32(setup_field(k, 0x00)) == 2 && vm_read32(setup_field(k, 0x18)) == 0) m |= 1 << fighter;
        fighter++;
    }
    return m;
}

/* Every fighter reads its buttons from a pad object (vtable 0x00875858): the
 * human's is filled from the controller, a CPU's by the AI. Its getters test
 * bits of +0x204/+0x20C/+0x210/+0x214. The dummy's getters answer "nothing
 * pressed". pr2 dump: +0x14 is the fighter's slot (0 P1, 1 P1's partner, 2-3 the
 * other team) and +0x18 the driver (1 human pad, 3 CPU).
 * TRAINING_DUMMY_TEST=<hex slot mask> (test): those slots are dummies if CPU. */
bool training_pad_getter(uint32_t target)
{
    switch (target) {
    case 0x0009D5ACu: case 0x0009D680u: case 0x0009D6A8u: case 0x0009D6D0u:
    case 0x0009D6D8u: case 0x0009D728u: case 0x0009D750u: return true;
    default: return false;
    }
}

/* Training mode is on while the player is in it (chosen in the Free Battle box,
 * off again once the main menu shows), or always with TRAINING_TEST=1. */
bool g_training_test = false;

bool training_active() { return g_training_test || g_training_mode; }


/* Training mode (the maintainer's plan, step 1): no
 * fighter is knocked out and health refills after 1.5 s without a hit.
 * Fighter health lives in each fighter object, x100: +0x90 current (it
 * regenerates up to +0x94), +0x94 the recoverable part, +0x98 max. Damage goes
 * through the virtual call 0x00085B84 (obj, damage to +0x90, damage to +0x94),
 * which checks for the KO right after subtracting, so the cap has to happen
 * before it runs: the indirect-call hook shrinks the damage to leave 1 health. */
struct TrainFighter { uint32_t obj; unsigned long long last_hit; };
/* The gauges over the characters (class 0x00879DA0) show the combo number: +0x3EC is 1 while it is up and
 * +0x3F0 the hit count; both go back to 0 when it goes away, about 0.9 s after the last hit of a combo (pf3). */
uint32_t g_combo_gauge[8]; int g_ncombo_gauge = 0;
unsigned long long g_combo_end = 0;   /* when the last combo number went away */
bool g_combo_up = false;
TrainFighter g_train[8];
volatile int g_train_count = 0;

bool training_fight_scene()
{
    return fight_hud_up();
}

/* TRAINING_TEST_LOW=N (test only): a fighter starts the fight at N/100
 * health, so a few hits would knock it out without the cap. */
int training_low()
{
    static int low = -1;
    if (low < 0) { const char* e = std::getenv("TRAINING_TEST_LOW"); low = e ? std::atoi(e) : 0; }
    return low;
}

int training_find(uint32_t obj)
{
    for (int i = 0; i < g_train_count; i++) if (g_train[i].obj == obj) return i;
    return -1;
}

/* Balance log (run\balance_log.txt, written in Training, new each launch): every hit
 * with its return addresses, P1's button changes and the fighters' stat fields, so a
 * Training session can be matched to moves afterwards (jstars-notes\Balance). */
FILE* g_bal = nullptr;
uint32_t g_bal_obj[8];
int g_bal_nobj = 0;

FILE* bal_file()
{
    static bool tried = false;
    if (!tried) { tried = true; g_bal = std::fopen("balance_log.txt", "w"); }
    return g_bal;
}

/* No knockdowns in Training (the maintainer 11-03), so every attack can be tried on the dummy: the first time a hit comes
 * from an attack file (slots of 0x9C bytes, reaction at +0x39) or a projectile file (rows of 232 bytes, reaction at
 * +0xAF), every damaging entry whose reaction knocks down is set to 1, a plain flinch (numbers: Balance/Knockdowns.md).
 * The old bytes are kept and put back when the Training fight ends. TRAINING_KNOCKDOWN=1 keeps the game's own. */
extern bool g_tb_mode;
struct KdUndo { uint32_t ea; uint8_t v; };
std::vector<KdUndo> g_kd_undo;
std::vector<uint32_t> g_kd_files;
/* Off since 11-05 (the maintainer: the normal Training Mode, fighters get knocked away like normal); TRAINING_NO_KNOCKDOWN=1 turns it on again. */
bool training_no_knockdown() { static const bool on = std::getenv("TRAINING_NO_KNOCKDOWN") != nullptr; return on && training_active() && !g_tb_mode; }
bool kd_knocks(uint8_t v) { return (v >= 3 && v <= 6) || (v >= 8 && v <= 12) || v == 14 || (v >= 17 && v <= 21); }
void kd_flatten(uint32_t base, uint32_t stride, uint32_t dmg_off, uint32_t react_off)
{
    if (!training_no_knockdown()) return;
    for (uint32_t b : g_kd_files) if (b == base) return;
    g_kd_files.push_back(base);
    const uint32_t n = vm_read32(base);
    int changed = 0;
    for (uint32_t k = 0; k < n && k < 2000; k++) {
        const uint32_t e = base + 0x10u + k * stride;
        const uint8_t v = vm_read8(e + react_off);
        (void)dmg_off;   /* rows without damage count too: Gintoki's special knocked down from them (nk1) */
        if (!kd_knocks(v)) continue;
        g_kd_undo.push_back({ e + react_off, v });
        vm_write8(e + react_off, 1);
        changed++;
    }
    std::printf("[training] no knockdowns: file %08X, %d of %u entries\n", base, changed, n); std::fflush(stdout);
}
void kd_restore()
{
    if (g_kd_files.empty()) return;
    for (const KdUndo& u : g_kd_undo) vm_write8(u.ea, u.v);
    std::printf("[training] knockdowns put back: %zu entries\n", g_kd_undo.size()); std::fflush(stdout);
    g_kd_undo.clear(); g_kd_files.clear();
}
/* At the start of a Training fight, find every loaded attack file and projectile file and flatten them before any
 * hit: done only at the first hit, a fighter's first special still knocked the dummy down (Gintoki, nk2). A file is
 * the entry count, three zero words, then the entries, in the packs' area (0x41000000-0x42000000 in nk3). At least
 * 85% of the entries with damage must look right: damage below 100000 (attack +0x28, projectile +0x9C), a ratio of 0
 * to 1 (+0x2C, +0xA0) and a known reaction number. Checked against all 42 fighters' files on disk: only Dio's and
 * Dai's attack files fail, and those still get flattened at their first hit. */
void kd_flatten_all()
{
    int found = 0;
    for (uint32_t base = 0x41000000u; base < 0x42000000u; base += 0x10) {
        const uint32_t cnt = vm_read32(base);
        if (cnt < 10 || cnt > 2000 || vm_read32(base + 4) || vm_read32(base + 8) || vm_read32(base + 12)) continue;
        for (int kind = 0; kind < 2; kind++) {
            const uint32_t stride = kind ? 232u : 0x9Cu, dmg = kind ? 0x9Cu : 0x28u, ratio = kind ? 0xA0u : 0x2Cu, react = kind ? 0xAFu : 0x39u;
            if (kind == 0 && cnt % 10) continue;
            if (base + 0x10u + cnt * stride >= 0x42000000u) continue;
            uint32_t with = 0, bad = 0;
            for (uint32_t k = 0; k < cnt && bad < 200; k++) {
                const uint32_t e = base + 0x10u + k * stride, d = vm_read32(e + dmg);
                if (!d) continue;
                union { uint32_t u; float f; } r; r.u = vm_read32(e + ratio);
                const uint8_t v = vm_read8(e + react);
                if (!(d < 100000u && r.f >= 0.0f && r.f <= 1.0f && (v <= 26 || v == 128 || v == 204 || v == 255))) bad++;
                with++;
            }
            if (with >= (kind ? 5u : 10u) && bad * 100 <= with * 15) { kd_flatten(base, stride, dmg, react); found++; break; }
        }
    }
    std::printf("[training] no knockdowns: %d files found at the fight start\n", found); std::fflush(stdout);
}

/* The projectile file a hit record's row (+0x9C) belongs to: the row count at +0, then 3 zero words. */
uint32_t kd_bullet_file(uint32_t row)
{
    for (uint32_t k = 0; row && k < 1000; k++) {
        const uint32_t base = row - 0x10u - k * 232u, cnt = vm_read32(base);
        if (cnt > k && cnt < 1000 && vm_read32(base + 4) == 0 && vm_read32(base + 8) == 0 && vm_read32(base + 12) == 0) return base;
    }
    return 0;
}

/* KD_DUMP=1 (test only): kd_dump.bin gets the hit fighter's owner object (from -0x400, 0x2400 bytes) and its
 * health object (from -0x400, 0x800 bytes) every 100 ms in a Training fight, and right at each hit. */
volatile uint32_t g_kd_owner = 0, g_kd_obj = 0; volatile unsigned long long g_kd_hit_ms = 0;
void kd_dump(bool at_hit)
{
    static const bool on = std::getenv("KD_DUMP") != nullptr;
    if (!on || !g_kd_owner) return;
    static FILE* f = std::fopen("kd_dump.bin", "wb");
    static unsigned long long last = 0;
    const unsigned long long now = GetTickCount64();
    if (!f || (!at_hit && now - last < 100)) return;
    last = now;
    const uint32_t hdr[4] = { (uint32_t)now, at_hit ? 1u : 0u, g_kd_owner, g_kd_obj };
    std::fwrite(hdr, 4, 4, f);
    for (uint32_t k = 0; k < 0x900; k++) { const uint32_t v = vm_read32(g_kd_owner - 0x400 + k * 4); std::fwrite(&v, 4, 1, f); }
    for (uint32_t k = 0; k < 0x200; k++) { const uint32_t v = vm_read32(g_kd_obj - 0x400 + k * 4); std::fwrite(&v, 4, 1, f); }
    std::fflush(f);
}

void bal_hit(uint32_t obj, const uint64_t* gpr, int32_t cur, int32_t rec, int32_t d90, int32_t d94)
{
    FILE* f = bal_file();
    if (!f) return;
    const ppu_context* ctx = reinterpret_cast<const ppu_context*>(
        reinterpret_cast<const char*>(gpr) - offsetof(ppu_context, gpr));
    std::fprintf(f, "%llu hit obj %08X health %d rec %d dmg %d rec_dmg %d r6-10 %08X %08X %08X %08X %08X lr %08X chain",
                 GetTickCount64(), obj, cur, rec, d90, d94, (uint32_t)gpr[6], (uint32_t)gpr[7], (uint32_t)gpr[8],
                 (uint32_t)gpr[9], (uint32_t)gpr[10], (uint32_t)ctx->lr);
    uint32_t sp = (uint32_t)gpr[1];
    for (int i = 0; i < 10 && sp && !(sp & 7) && sp < 0xF0000000u; i++) {
        const uint32_t next = (uint32_t)vm_read64(sp);
        if (!next || next <= sp) break;
        std::fprintf(f, " %08X", (uint32_t)vm_read64(next + 0x10));
        sp = next;
    }
    std::fprintf(f, "\n");
    /* The pending hit records this damage came from (fighter +0x230, 0xB0 each). */
    const uint32_t owner = (uint32_t)gpr[27];   /* func_00082944 keeps the record owner there */
    g_kd_owner = owner; g_kd_obj = obj; g_kd_hit_ms = GetTickCount64();
    const uint32_t pending = vm_read32(owner + 0x22C);
    for (uint32_t i = 0; i < pending && i < 4; i++) {
        std::fprintf(f, "%llu rec %u", GetTickCount64(), i);
        const uint32_t rec = owner + 0x230 + i * 0xB0;
        const uint32_t slot = vm_read32(rec + 0x98);
        for (uint32_t k = 0; slot && k < 800; k++) {
            /* walk back to the file: its first word is the slot count, then zeros */
            const uint32_t base = slot - k * 0x9Cu - 0x10u;
            if (vm_read32(base) == 0x1B8u || (vm_read32(base + 4) == 0 && vm_read32(base + 8) == 0 && vm_read32(base + 12) == 0 &&
                                              vm_read32(base) >= k && vm_read32(base) <= 800 && vm_read32(base) % 10 == 0)) {
                std::fprintf(f, " row %u hit %u file %08X", k / 10, k % 10, base);
                kd_flatten(base, 0x9Cu, 0x28u, 0x39u);
                /* BAL_TEST=<row>,<hit>,<damage>,<gauge>: change one attack slot in memory after the first logged hit */
                /* KD_FLINCH=<n> (test only): after the first logged hit, every slot of that attack file whose reaction
                 * byte (+0x39: 1 flinch, 3 heavy flinch, 6 knocked away) is above n is set to n. */
                /* KD_SETFILE=<file> (test only): on every logged hit, the number in that file (if changed) is written to
                 * the reaction byte of every slot with damage in this attack file, so one run can try each value. */
                if (const char* kf = std::getenv("KD_SETFILE")) {
                    static int applied = -1;
                    int v = -1;
                    if (FILE* ff = std::fopen(kf, "r")) { if (std::fscanf(ff, "%d", &v) != 1) v = -1; std::fclose(ff); }
                    if (v >= 0 && v != applied) {
                        applied = v;
                        const uint32_t n = vm_read32(base);
                        for (uint32_t k2 = 0; k2 < n && k2 < 2000; k2++) {
                            const uint32_t s2 = base + 0x10u + k2 * 0x9Cu;
                            if (vm_read32(s2 + 0x28)) vm_write8(s2 + 0x39, (uint8_t)v);
                        }
                        std::fprintf(f, " kd-set %d", v);
                    }
                }
                static bool kd_done = false;
                if (!kd_done) {
                    kd_done = true;
                    if (const char* e = std::getenv("KD_FLINCH")) {
                        const int lim = std::atoi(e);
                        const uint32_t n = vm_read32(base);
                        int changed = 0;
                        for (uint32_t k2 = 0; k2 < n && k2 < 2000; k2++) {
                            const uint32_t s2 = base + 0x10u + k2 * 0x9Cu;
                            if ((int)vm_read8(s2 + 0x39) > lim) { vm_write8(s2 + 0x39, (uint8_t)lim); changed++; }
                        }
                        std::fprintf(f, " kd-flinch %d slots", changed);
                    }
                }
                static bool bal_test_done = false;
                if (!bal_test_done) {
                    bal_test_done = true;
                    int r = 0, h = 0, dm = 0, ga = 0;
                    if (const char* e = std::getenv("BAL_TEST")) {
                        if (std::sscanf(e, "%d,%d,%d,%d", &r, &h, &dm, &ga) == 4) {
                            const uint32_t s = base + 0x10u + (uint32_t)r * 0x618u + (uint32_t)h * 0x9Cu;
                            vm_write32(s + 0x28, (uint32_t)dm);
                            vm_write16(s + 0x30, (uint16_t)ga);
                            std::fprintf(f, " test-set %08X", s);
                        }
                    }
                }
                break;
            }
        }
        if (const uint32_t bf = kd_bullet_file(vm_read32(rec + 0x9C))) kd_flatten(bf, 232u, 0x9Cu, 0xAFu);
        /* KD_BULLET=<n> (test only): the first projectile hit's file (rows of 232 bytes from +0x10, the row count
         * at +0) gets n in every damaging row's reaction byte, +0xAF (the same numbers as attack slot +0x39). */
        if (const char* kb = std::getenv("KD_BULLET")) {
            static bool kb_done = false;
            const uint32_t row = vm_read32(rec + 0x9C);
            if (!kb_done && row) {
                for (uint32_t k = 0; k < 1000; k++) {
                    const uint32_t base = row - 0x10u - k * 232u;
                    const uint32_t cnt = vm_read32(base);
                    if (cnt > k && cnt < 1000 && vm_read32(base + 4) == 0 && vm_read32(base + 8) == 0 && vm_read32(base + 12) == 0) {
                        kb_done = true;
                        int changed = 0;
                        for (uint32_t r = 0; r < cnt; r++) {
                            const uint32_t q = base + 0x10u + r * 232u;
                            if (vm_read32(q + 0x9C)) { vm_write8(q + 0xAF, (uint8_t)std::atoi(kb)); changed++; }
                        }
                        std::fprintf(f, " kd-bullet file %08X row %u, %d rows", base, k, changed);
                        break;
                    }
                }
            }
        }
        const uint32_t mod = vm_read32(rec + 0x94);
        if (mod) std::fprintf(f, " mod %08X %08X %08X %08X", vm_read32(mod), vm_read32(mod + 4), vm_read32(mod + 8), vm_read32(mod + 12));
        for (uint32_t o = 0; o < 0xB0; o += 4) std::fprintf(f, " %08X", vm_read32(rec + o));
        std::fprintf(f, "\n");
    }
    std::fflush(f);
    kd_dump(true);
}

void bal_note_fighter(uint32_t obj)
{
    for (int i = 0; i < g_bal_nobj; i++) if (g_bal_obj[i] == obj) return;
    if (g_bal_nobj >= 8) return;
    g_bal_obj[g_bal_nobj++] = obj;
    if (FILE* f = bal_file()) {
        std::fprintf(f, "%llu fighter %08X stats", GetTickCount64(), obj);
        for (uint32_t o = 0x90; o <= 0xA4; o += 4) std::fprintf(f, " %d", (int32_t)vm_read32(obj + o));
        std::fprintf(f, "\n");
        std::fflush(f);
    }
}

void act_window(uint16_t pressed);

/* Once per pad poll in Training. */
void bal_tick(uint16_t buttons, uint16_t lx, uint16_t ly)
{
    /* KO_TEST_MAX=<n>: test only. Change the 3 KOs held next to each KO counter (addresses from fight ko4). */
    static int ko_max = -2;
    if (ko_max == -2) { const char* e = std::getenv("KO_TEST_MAX"); ko_max = e ? std::atoi(e) : -1; }
    if (ko_max > 0 && training_fight_scene()) {
        for (uint32_t ea : { 0x490238E4u, 0x492D1F20u }) if (vm_read32(ea) == 3u) vm_write32(ea, (uint32_t)ko_max);
    }
    FILE* f = bal_file();
    if (!f) return;
    const unsigned long long now = GetTickCount64();
    static bool in_fight_log = false;
    const bool fight = training_fight_scene();
    /* ICALL_OFF_IN_FIGHT: test only, for the freeze: removes the indirect-call hook once a fight is on. */
    if (fight && g_ppu_icall_hook && std::getenv("ICALL_OFF_IN_FIGHT")) {
        static unsigned long long since = 0;
        if (!since) since = now;
        else if (now - since > 15000) { g_ppu_icall_hook = nullptr; std::fprintf(f, "%llu icall hook off\n", now); std::fflush(f); }
    }
    if (fight != in_fight_log) {
        in_fight_log = fight;
        std::fprintf(f, "%llu %s step %u setup", now, fight ? "fight-start" : "fight-end", vm_read32(0x0094DEC4u));
        for (int k = 0; k < 6; k++) std::fprintf(f, " %d/%d", (int32_t)vm_read32(setup_field(k, 0x08)), (int32_t)vm_read32(setup_field(k, 0x0C)));
        std::fprintf(f, "\n");
        if (!fight) g_bal_nobj = 0;
    }
    if (!fight) return;
    static uint16_t prev = 0;
    const int dx = lx < 64 ? -1 : lx > 192 ? 1 : 0, dy = ly < 64 ? -1 : ly > 192 ? 1 : 0;
    static int pdx = 0, pdy = 0;
    if (buttons != prev || dx != pdx || dy != pdy) {
        act_window((uint16_t)(buttons & ~prev));
        prev = buttons; pdx = dx; pdy = dy;
        std::fprintf(f, "%llu pad %04X stick %d %d\n", now, buttons, dx, dy);
    }
    static unsigned long long last = 0;
    static uint32_t seen[8][6];
    if (now - last >= 200) {
        last = now;
        for (int i = 0; i < g_bal_nobj; i++) {
            uint32_t v[6]; bool changed = false;
            for (int j = 0; j < 6; j++) {
                v[j] = vm_read32(g_bal_obj[i] + 0x90 + 4u * j);
                if (v[j] != seen[i][j]) changed = true;
                seen[i][j] = v[j];
            }
            if (changed) std::fprintf(f, "%llu stats %08X %d %d %d %d %d %d\n", now, g_bal_obj[i], (int32_t)v[0], (int32_t)v[1],
                                      (int32_t)v[2], (int32_t)v[3], (int32_t)v[4], (int32_t)v[5]);
        }
    }
    std::fflush(f);
}

/* Balance log: every state a fighter enters (its action and mode, from the .ocm action
 * list) and every motion started. With the hit, pad and stats lines this is the whole
 * record of what a fighter did in Training; tools/balance/act_read.py prints it. */
void bal_action(uint32_t state)
{
    FILE* f = bal_file();
    if (!f) return;
    const uint32_t e = vm_read32(state + 0x34);
    if (!e) return;
    std::fprintf(f, "%llu act slot %u action %u mode %u\n", GetTickCount64(), vm_read32(state + 4), vm_read32(e), vm_read32(e + 4));
    std::fflush(f);
}

void bal_motion(uint32_t obj, uint32_t motion, uint32_t action)
{
    static uint32_t seen_obj[8], seen_motion[8];
    int i = 0;
    for (; i < 8; i++) if (seen_obj[i] == obj || !seen_obj[i]) break;
    if (i == 8) return;
    if (seen_obj[i] == obj && seen_motion[i] == motion) return;
    seen_obj[i] = obj; seen_motion[i] = motion;
    FILE* f = bal_file();
    if (!f) return;
    std::fprintf(f, "%llu mot obj %08X motion %u action %u\n", GetTickCount64(), obj, motion, action);
    std::fflush(f);
}

/* ACT_TRACE=1 (test only, act_trace.txt): after each new P1 button press in a fight,
 * the first time each indirect call target + caller is seen in the next 700 ms, so a
 * move that starts can be compared with one that does not. */
std::mutex g_act_mu;
FILE* g_act = nullptr;
std::unordered_map<uint64_t, int>* g_act_seen = nullptr;
volatile unsigned long long g_act_start = 0;

void act_window(uint16_t pressed)
{
    static const bool on = std::getenv("ACT_TRACE") != nullptr;
    if (!on || !pressed) return;
    std::lock_guard<std::mutex> lk(g_act_mu);
    if (!g_act) g_act = std::fopen("act_trace.txt", "w");
    if (!g_act) return;
    if (!g_act_seen) g_act_seen = new std::unordered_map<uint64_t, int>();
    g_act_seen->clear();
    g_act_start = GetTickCount64();
    std::fprintf(g_act, "== %llu press %04X\n", g_act_start, pressed);
    std::fflush(g_act);
}

void act_note(uint32_t target, const uint64_t* gpr)
{
    if (!g_act_start) return;
    const unsigned long long now = GetTickCount64();
    if (now - g_act_start > 700) return;
    const ppu_context* ctx = reinterpret_cast<const ppu_context*>(
        reinterpret_cast<const char*>(gpr) - offsetof(ppu_context, gpr));
    const uint32_t lr = (uint32_t)ctx->lr, r3 = (uint32_t)gpr[3];
    std::lock_guard<std::mutex> lk(g_act_mu);
    if (!g_act || !g_act_seen) return;
    if (target == 0x0007A4D4u) {   /* the action chooser's messages: +8 type, +0x14 the request */
        const uint32_t m = (uint32_t)gpr[4], req = vm_read32(m + 0x14);
        std::fprintf(g_act, "%4llu msg r3 %08X type %u +14 %08X", now - g_act_start, r3, vm_read32(m + 8), req);
        if (req >= 0x10000u && req < 0xF0000000u && !(req & 3))
            for (uint32_t o = 0; o < 0x20; o += 4) std::fprintf(g_act, " %08X", vm_read32(req + o));
        std::fprintf(g_act, " chain");
        uint32_t sp2 = (uint32_t)gpr[1];
        for (int i = 0; i < 8 && sp2 && !(sp2 & 7) && sp2 < 0xF0000000u; i++) {
            const uint32_t next = (uint32_t)vm_read64(sp2);
            if (!next || next <= sp2) break;
            std::fprintf(g_act, " %08X", (uint32_t)vm_read64(next + 0x10));
            sp2 = next;
        }
        std::fprintf(g_act, "\n");
    }
    int& seen = (*g_act_seen)[((uint64_t)target << 32) | lr];
    if (seen++) return;
    std::fprintf(g_act, "%4llu t %08X lr %08X r3 %08X vt %08X r4 %08X r5 %08X chain", now - g_act_start, target, lr, r3,
                 (r3 && r3 < 0xF0000000u) ? vm_read32(r3) : 0u, (uint32_t)gpr[4], (uint32_t)gpr[5]);
    uint32_t sp = (uint32_t)gpr[1];
    for (int i = 0; i < 5 && sp && !(sp & 7) && sp < 0xF0000000u; i++) {
        const uint32_t next = (uint32_t)vm_read64(sp);
        if (!next || next <= sp) break;
        std::fprintf(g_act, " %08X", (uint32_t)vm_read64(next + 0x10));
        sp = next;
    }
    std::fprintf(g_act, "\n");
}

/* ACT_SWAP=<file> (test only): when P1 enters the state for action 88 (Circle), start the
 * action numbered in that file instead, so any action can be reached with one button.
 * The state object (vtable 0x00873C60) holds its entry of the .ocm action list at +0x34:
 * the first word is the action. */
void act_swap(uint32_t state)
{
    static const char* path = std::getenv("ACT_SWAP");
    static uint32_t entry = 0;
    if (!path) return;
    const uint32_t e = vm_read32(state + 0x34);
    if (!entry && e && vm_read32(state + 4) == 0 && vm_read32(e) == 88u) entry = e;
    if (!entry || e != entry) return;
    int to = 0;
    if (FILE* f = std::fopen(path, "r")) { if (std::fscanf(f, "%d", &to) != 1) to = 0; std::fclose(f); }
    if (to <= 0) return;
    vm_write32(entry, (uint32_t)to);
    if (FILE* f = bal_file()) { std::fprintf(f, "%llu act-swap %d\n", GetTickCount64(), to); std::fflush(f); }
}

/* The pads seen in this fight and the training dummy's (reset between fights). */
uint32_t g_pads[8]; int g_npads = 0;
uint32_t g_dummy_pads[4]; int g_ndummy = 0;

/* Pause menu (object with vtable 0x00876710). Its tables are the entries of an STPK the parent
 * object (+4) holds; 0x00498D48 finds one as pack + the offset in entry N. A table is a count at +0,
 * then 0x4C-byte rows from +0x10: +0 type (0 root, 1 title, 2 row), +4 text number in the V_Pause pack,
 * +8 next, +0x48 flags (bit 2: selected at the start). Next of 0x54 or less opens that table in place
 * (Restart's Yes/No), 94 goes back, others are handed to the game (93 Skill table).
 * The object keeps a stack of 0x58-byte levels from +0x3C: +0x3AC depth, level +4 selected row.
 * In Training, table 3 gets one more row and opens table 12, an empty slot pointed at our own table.
 * Our copies sit at the top of the heap; outside Training the entries go back. */
const uint32_t k_pause_vt = 0x00876710u, k_pause_copy = 0x4B8FF000u, k_pause_row = 0x4Cu;
const uint32_t k_stpk = 0x5354504Bu;
const uint32_t k_table_pause = 3, k_table_settings = 12, k_next_back = 94;
uint32_t g_pause_obj = 0;
unsigned long long g_pause_ms = 0;
uint32_t g_pause_offset = 0, g_settings_offset = 0;   /* the two entries' original offsets */

void pause_set_text(uint32_t pack, uint32_t index, const char* text);

/* Training settings. Text slots are replay and world map texts this mode never shows.
 * To add a setting, add a line here. */
bool g_set_stamina = false;
bool g_set_freecam = false;
bool g_set_turnbased = false;
uint32_t g_tb_bar0 = 0;    /* the first gauge over a character; they are 0x480 apart and the second is the partner's (pd4) */
uint32_t g_tb_gauge = 0;   /* P1's health bar widget; its state leaves 0 once "Fight" has gone */
bool g_tb_mode = false, g_tb_ready = false;   /* the Turn-based row of Training's list, and its fight has begun */
bool g_tb_running = false;
struct PauseSetting { const char* name; bool* on; uint32_t text; };
const PauseSetting k_settings[] = {
    { "Unlimited Stamina", &g_set_stamina, 15 },
    { "Free Camera", &g_set_freecam, 18 },
    { "Hide HUD", &g_set_hidehud, 21 },
};
const int k_setting_count = (int)(sizeof k_settings / sizeof k_settings[0]);
const uint32_t k_text_row = 12, k_text_back = 13;
const uint32_t k_copy_settings = k_pause_copy + 0x400u;
uint32_t g_pause_texts = 0;

void pause_setting_text(const PauseSetting& st)
{
    char line[64];
    std::snprintf(line, sizeof line, "%s: %s", st.name, *st.on ? "On" : "Off");
    pause_set_text(g_pause_texts, st.text, line);
}

/* The pack can be loaded again for a new fight, so this runs at every open. */
uint32_t pause_find_pack(uint32_t parent)
{
    for (uint32_t o = 0x0C; o <= 0x34; o += 4) {
        const uint32_t pack = vm_read32(parent + o);
        if (pack < 0x10000u || pack >= 0xF0000000u || (pack & 3) || vm_read32(pack) != k_stpk) continue;
        if (k_table_settings >= vm_read32(pack + 8) || k_table_pause >= vm_read32(pack + 8)) continue;
        const uint32_t table = pack + vm_read32(pack + 16 + k_table_pause * 48);
        if (table == k_pause_copy || (vm_read32(table) == 6 && vm_read32(table + 0x10 + k_pause_row) == 2)) return pack;
    }
    return 0;
}

void pause_open(uint64_t* gpr)
{
    const uint32_t obj = (uint32_t)gpr[3], kind = (uint32_t)gpr[4], parent = vm_read32(obj + 4);
    const uint32_t pack = pause_find_pack(parent);
    if (!pack) return;
    const uint32_t entry = pack + 16 + k_table_pause * 48, entry2 = pack + 16 + k_table_settings * 48;
    const uint32_t table = pack + vm_read32(entry);
    const bool ours = table == k_pause_copy;
    if (!training_active() || kind != k_table_pause) {
        if (ours) { vm_write32(entry, g_pause_offset); vm_write32(entry2, g_settings_offset); }
        return;
    }
    if (!ours) {
        /* the pause menu with one more row */
        const uint32_t rows = vm_read32(table);
        const uint32_t size = 0x10u + rows * k_pause_row;
        for (uint32_t i = 0; i < size; i += 4) vm_write32(k_pause_copy + i, vm_read32(table + i));
        const uint32_t last = k_pause_copy + 0x10u + (rows - 1) * k_pause_row, added = last + k_pause_row;
        for (uint32_t i = 0; i < k_pause_row; i += 4) vm_write32(added + i, vm_read32(last + i));
        vm_write32(added + 4, k_text_row);
        vm_write32(added + 8, k_table_settings);
        vm_write32(k_pause_copy, rows + 1);
        /* Training settings, built from the Yes/No box: a title, one row per setting, then Back */
        const uint32_t box = pack + vm_read32(pack + 16 + 23 * 48);   /* Restart's Yes/No */
        const uint32_t set_rows = 2u + (uint32_t)k_setting_count;
        for (uint32_t i = 0; i < 0x10u + k_pause_row; i += 4) vm_write32(k_copy_settings + i, vm_read32(box + i));
        vm_write32(k_copy_settings, set_rows);
        vm_write32(k_copy_settings + 0x10u + 4, k_text_row);
        const uint32_t no_row = box + 0x10u + 2 * k_pause_row;
        for (uint32_t r = 1; r < set_rows; r++) {
            const uint32_t to = k_copy_settings + 0x10u + r * k_pause_row;
            for (uint32_t i = 0; i < k_pause_row; i += 4) vm_write32(to + i, vm_read32(no_row + i));
            const bool back = r == set_rows - 1;
            vm_write32(to + 4, back ? k_text_back : k_settings[r - 1].text);
            vm_write32(to + 8, k_next_back);
            vm_write32(to + 0x48, r == 1 ? 5u : 1u);
        }
        g_pause_offset = vm_read32(entry); g_settings_offset = vm_read32(entry2);
        vm_write32(entry, k_pause_copy - pack);
        vm_write32(entry2, k_copy_settings - pack);
        std::printf("[pause] Training rows added: table %08X pack %08X\n", table, pack);
        std::fflush(stdout);
    }
    g_pause_texts = vm_read32(parent + 0x10);
    pause_set_text(g_pause_texts, k_text_row, "Training settings");
    pause_set_text(g_pause_texts, k_text_back, "Back");
    for (const PauseSetting& st : k_settings) pause_setting_text(st);
}

/* Replaces a text in the V_Pause pack (STPK, held at object +0x38) in memory. UTF-16 little-endian after a BOM. */
void pause_set_text(uint32_t pack, uint32_t index, const char* text)
{
    if (vm_read32(pack) != k_stpk || index >= vm_read32(pack + 8)) return;
    const uint32_t entry = pack + 16 + index * 48;
    const uint32_t at = pack + vm_read32(entry), size = vm_read32(entry + 4);
    uint32_t i = 2;
    auto put = [&](uint32_t where, uint8_t v) {
        const uint32_t w = where & ~3u; const int sh = (3 - (int)(where & 3)) * 8;
        vm_write32(w, (vm_read32(w) & ~(0xFFu << sh)) | ((uint32_t)v << sh));
    };
    for (const char* c = text; *c && i + 4 <= size; c++, i += 2) { put(at + i, (uint8_t)*c); put(at + i + 1, 0); }
    put(at + i, 0); put(at + i + 1, 0);
}

bool pause_up()
{
    return g_pause_obj && GetTickCount64() - g_pause_ms < 200;
}

/* In Training settings, Cross on a setting flips it and the game doesn't see the press.
 * Back and everything else is the game's own menu. */
void pause_tick(uint16_t* b)
{
    const uint16_t CROSS = 0x4000;
    static uint16_t prev = 0;
    const uint16_t cur = *b, pressed = (uint16_t)(cur & ~prev);
    prev = cur;
    if (!pause_up() || vm_read32(g_pause_obj + 0x30) != k_copy_settings) return;
    const uint32_t depth = vm_read32(g_pause_obj + 0x3AC);
    const int i = (int)vm_read32(g_pause_obj + 0x3C + depth * 0x58 + 4) - 1;
    if (i < 0 || i >= k_setting_count || !(cur & CROSS)) return;
    if (pressed & CROSS) {
        *k_settings[i].on = !*k_settings[i].on;
        pause_setting_text(k_settings[i]);
        std::printf("[pause] %s %s\n", k_settings[i].name, *k_settings[i].on ? "on" : "off");
        std::fflush(stdout);
    }
    *b = (uint16_t)(cur & ~CROSS);
}

/* What the settings do, on every pad read. */
/* Free camera (Training setting). Each fighter has a camera object, vtable 0x00874DF8: +0x10 the view
 * matrix, +0x50 its inverse (rows: right, up, the way it looks, position), +0x90 the position again. 0x0006FF10
 * steers it each frame, and with that call skipped the game leaves it alone. While the setting is on, the
 * pad moves the camera and the fighters see nothing pressed except Start, which opens the pause menu.
 * Left stick moves, right stick looks, L1 and R1 go down and up, Cross held is faster. */
const uint32_t k_cam_vt = 0x00874DF8u, k_cam_update = 0x0006FF10u;
struct FreeCamObj { uint32_t obj; unsigned long long ms; };
FreeCamObj g_cam_objs[8];
bool g_cam_running = false;
float g_cam_pos[3], g_cam_yaw, g_cam_pitch;
uint16_t g_cam_rx = 128, g_cam_ry = 128;

void freecam_note(uint32_t obj)
{
    const unsigned long long now = GetTickCount64();
    int slot = 0;
    for (int i = 0; i < 8; i++) {
        if (g_cam_objs[i].obj == obj) { slot = i; break; }
        if (g_cam_objs[i].ms < g_cam_objs[slot].ms) slot = i;
    }
    g_cam_objs[slot].obj = obj; g_cam_objs[slot].ms = now;
}

void cam_put(uint32_t ea, float v) { union { float f; uint32_t u; } c; c.f = v; vm_write32(ea, c.u); }
float cam_get(uint32_t ea) { union { float f; uint32_t u; } c; c.u = vm_read32(ea); return c.f; }

void freecam_rstick(uint32_t port, uint16_t* rx, uint16_t* ry)
{
    if (port != 0) { if (g_set_freecam && training_active() && !pause_up()) { *rx = 128; *ry = 128; } return; }
    g_cam_rx = *rx; g_cam_ry = *ry;
    if (g_set_freecam && training_active() && !pause_up()) { *rx = 128; *ry = 128; }
}

void freecam_write()
{
    const float sy = std::sin(g_cam_yaw), cy = std::cos(g_cam_yaw), sp = std::sin(g_cam_pitch), cp = std::cos(g_cam_pitch);
    const float f[3] = { sy * cp, sp, cy * cp };   /* the way it looks: the third row, not its negative */
    const float r[3] = { cy, 0.0f, -sy };
    const float u[3] = { f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0] };
    const float* p = g_cam_pos;
    const unsigned long long now = GetTickCount64();
    for (const FreeCamObj& c : g_cam_objs) {
        if (!c.obj || now - c.ms > 500 || vm_read32(c.obj) != k_cam_vt) continue;
        const uint32_t o = c.obj;
        const float world[16] = { r[0], r[1], r[2], 0, u[0], u[1], u[2], 0, f[0], f[1], f[2], 0, p[0], p[1], p[2], 1 };
        const float view[16] = { r[0], u[0], f[0], 0, r[1], u[1], f[1], 0, r[2], u[2], f[2], 0,
                                 -(p[0] * r[0] + p[1] * r[1] + p[2] * r[2]), -(p[0] * u[0] + p[1] * u[1] + p[2] * u[2]),
                                 -(p[0] * f[0] + p[1] * f[1] + p[2] * f[2]), 1 };
        for (int i = 0; i < 16; i++) { cam_put(o + 0x10 + i * 4, view[i]); cam_put(o + 0x50 + i * 4, world[i]); }
        for (int i = 0; i < 4; i++) cam_put(o + 0x90 + i * 4, i < 3 ? p[i] : 1.0f);
    }
}

void freecam_tick(uint16_t* b, uint16_t* lx, uint16_t* ly)
{
    if (!g_set_freecam) { g_cam_running = false; return; }
    if (pause_up()) return;
    const uint16_t START = 0x0008, CROSS = 0x4000, L1 = 0x0400, R1 = 0x0800;
    const uint16_t held = *b; const int mx = *lx - 128, my = *ly - 128;
    *b = (uint16_t)(held & START); *lx = 128; *ly = 128;
    /* The player's camera is the first one made in the fight, so the lowest address */
    uint32_t first = 0;
    const unsigned long long now = GetTickCount64();
    for (const FreeCamObj& c : g_cam_objs) if (c.obj && now - c.ms <= 500 && (!first || c.obj < first)) first = c.obj;
    if (!first) return;
    if (!g_cam_running) {
        g_cam_running = true;
        for (int i = 0; i < 3; i++) g_cam_pos[i] = cam_get(first + 0x80 + i * 4);
        const float fx = cam_get(first + 0x70), fy = cam_get(first + 0x74), fz = cam_get(first + 0x78);
        g_cam_yaw = std::atan2(fx, fz); g_cam_pitch = std::asin(fy < -1.0f ? -1.0f : fy > 1.0f ? 1.0f : fy);
        std::printf("[freecam] on at %.2f %.2f %.2f yaw %.2f pitch %.2f\n", g_cam_pos[0], g_cam_pos[1], g_cam_pos[2], g_cam_yaw, g_cam_pitch);
        std::fflush(stdout);
    }
    const auto axis = [](int v) { return (v > -24 && v < 24) ? 0.0f : v / 128.0f; };
    const float mv = (held & CROSS) ? 0.5f : 0.15f, turn = 0.03f;
    g_cam_yaw += axis(g_cam_rx - 128) * turn;
    g_cam_pitch -= axis(g_cam_ry - 128) * turn;
    if (g_cam_pitch > 1.5f) g_cam_pitch = 1.5f;
    if (g_cam_pitch < -1.5f) g_cam_pitch = -1.5f;
    const float sy = std::sin(g_cam_yaw), cy = std::cos(g_cam_yaw), cp = std::cos(g_cam_pitch), sp = std::sin(g_cam_pitch);
    const float go = -axis(my) * mv, side = axis(mx) * mv;
    g_cam_pos[0] += sy * cp * go + cy * side;
    g_cam_pos[1] += sp * go;
    g_cam_pos[2] += cy * cp * go - sy * side;
    if (held & R1) g_cam_pos[1] += mv;
    if (held & L1) g_cam_pos[1] -= mv;
    freecam_write();
}

/* Turn-based: a row above 1 Player in Training's list. Everything else is 1 Player until the fight, on Namek.
 * Neither fighter sees the controller. The menu takes Up, Down and Cross; a command goes in as button presses
 * through the fighter's own port, because a fighter's presses are read straight from its pad object
 * (0x0009D214), past the virtual getters. The opponent reads port 1, which this mode asks to exist.
 * A fighter's place is 0x70 before its health object: x, y, z, and a copy 0x10 later (pos2). */
const uint16_t k_tb_square = 0x8000, k_tb_r1 = 0x0800;   /* Square is atk_1, the normal attack */
const float k_tb_support_back = 0.6f, k_tb_support_left = 2.6f;
const float k_tb_apart = 8.0f, k_tb_reach = 2.6f, k_tb_home = 0.5f, k_tb_tilt = 1.0f;
enum TbState { TB_MENU, TB_APPROACH, TB_TAP, TB_WAIT, TB_RETURN, TB_FACE, TB_GUARD };
uint16_t g_tb_buttons[2];
uint16_t g_tb_lx[2] = { 128, 128 }, g_tb_ly[2] = { 128, 128 };
bool g_tb_guard[2];
unsigned long long g_tb_last_act = 0;
TbState g_tb_state = TB_MENU;
int g_tb_turn = 0, g_tb_row = 0, g_tb_ticks = 0;
volatile unsigned g_tb_gen = 1;     /* changes when the menu picture has to be redrawn */
volatile bool g_tb_show = false;

/* A fighter's camera holds that fighter's place at +0xA0, and its right and look rows at +0x50 and +0x70.
 * The lowest address is P1's. */
uint32_t g_tb_cam[3];

/* The Support standing on the map is Player 1's partner: slot 1 takes part. Its objects are made between P1's and
 * the opponent's (pc2). */
bool tb_has_partner() { return vm_read32(setup_field(1, 0x08)) != 2; }

/* The n lowest addresses, in order */
int tb_lowest(uint32_t a, uint32_t* low, int n, int have)
{
    for (int i = 0; i < have; i++) if (low[i] == a) return have;
    int i = have < n ? have++ : n;
    if (i == n && a >= low[n - 1]) return have;
    if (i == n) i = n - 1;
    while (i > 0 && low[i - 1] > a) { low[i] = low[i - 1]; i--; }
    low[i] = a;
    return have;
}

/* who: 0 P1, 1 the opponent, 2 the partner */
uint32_t tb_camera(int who)
{
    if (g_tb_cam[who] && vm_read32(g_tb_cam[who]) == k_cam_vt) return g_tb_cam[who];
    const int n = tb_has_partner() ? 3 : 2;
    uint32_t cams[3] = { 0, 0, 0 };
    int have = 0;
    const unsigned long long now = GetTickCount64();
    for (const FreeCamObj& c : g_cam_objs) {
        if (!c.obj || now - c.ms > 500) continue;
        have = tb_lowest(c.obj, cams, n, have);
    }
    if (have < n) return 0;
    /* taken once, before anything else gets a camera */
    g_tb_cam[0] = cams[0]; g_tb_cam[1] = cams[n - 1]; g_tb_cam[2] = n == 3 ? cams[1] : 0;
    return g_tb_cam[who];
}

/* A fighter's place is x, y, z, 1 twice over. It sits 0x70 before the health object on a fighter with no AI
 * and up to about 0x3000 before it on a CPU, varying by fighter (rn1, cc2), so it is looked for before the health
 * object, matching the camera's +0xA0. */
uint32_t g_tb_place[3];

uint32_t tb_find_place(uint32_t health, uint32_t cam)
{
    const float cx = cam_get(cam + 0xA0), cz = cam_get(cam + 0xA8);
    uint32_t best = 0, best_d = 0xFFFFFFFFu;
    for (uint32_t ea = health - 0x20000; ea < health; ea += 0x10) {   /* always before it; copies after it don't move the fighter (cc2) */
        if (vm_read32(ea + 0xC) != 0x3F800000u || vm_read32(ea + 0x1C) != 0x3F800000u) continue;
        const float x = cam_get(ea), y = cam_get(ea + 4), z = cam_get(ea + 8);
        if (std::fabs(x - cx) > 2.0f || std::fabs(z - cz) > 2.0f || std::fabs(y) > 60.0f) continue;
        if (std::fabs(x - cam_get(ea + 0x10)) > 0.5f || std::fabs(z - cam_get(ea + 0x18)) > 0.5f) continue;
        const uint32_t d = health - ea;
        if (d < best_d) { best_d = d; best = ea; }
    }
    return best;
}

/* The two fighters' places + 0x70, P1 first: its objects are made first, so at the lower address. */
bool tb_fighters(uint32_t* obj)
{
    obj[0] = obj[1] = 0;
    const int n = tb_has_partner() ? 3 : 2;
    uint32_t low[3] = { 0, 0, 0 };
    int have = 0;
    for (int i = 0; i < g_bal_nobj; i++) have = tb_lowest(g_bal_obj[i], low, n, have);
    if (have < n) return false;
    const uint32_t h[3] = { low[0], low[n - 1], n == 3 ? low[1] : 0 };
    for (int k = 0; k < n; k++) {
        if (!g_tb_place[k]) {
            const uint32_t cam = tb_camera(k);
            if (!cam) return false;
            g_tb_place[k] = tb_find_place(h[k], cam);
            if (!g_tb_place[k]) return false;
            std::printf("[tb] fighter %d health %08X place %08X, %d from it\n", k, h[k], g_tb_place[k], (int)(g_tb_place[k] - h[k]));
            std::fflush(stdout);
        }
        if (k < 2) obj[k] = g_tb_place[k] + 0x70;
    }
    return true;
}

float g_tb_spawn[2][2];
bool g_tb_spawned = false;

float tb_dist(uint32_t obj, float x, float z)
{
    const float dx = cam_get(obj - 0x70) - x, dz = cam_get(obj - 0x68) - z;
    return std::sqrt(dx * dx + dz * dz);
}

/* The left stick for fighter "who" that points at a place: sticks are read relative to its camera. */
void tb_stick_to(int who, uint32_t obj, float x, float z, float tilt)
{
    const uint32_t cam = tb_camera(who);
    if (!cam) return;
    float dx = x - cam_get(obj - 0x70), dz = z - cam_get(obj - 0x68);
    const float len = std::sqrt(dx * dx + dz * dz);
    if (len < 0.01f) return;
    dx /= len; dz /= len;
    const float sx = dx * cam_get(cam + 0x50) + dz * cam_get(cam + 0x58);
    const float sy = dx * cam_get(cam + 0x70) + dz * cam_get(cam + 0x78);
    g_tb_lx[who] = (uint16_t)(128.0f + sx * 127.0f * tilt);
    g_tb_ly[who] = (uint16_t)(128.0f - sy * 127.0f * tilt);
}

/* The opponent is brought to Player 1's ground, along the line between their spawn points, so they still
 * face each other. (The middle of that line on Namek is higher ground, and a fighter placed there ends up under it: tm2b.) */
void tb_place(const uint32_t* obj)
{
    if (!g_tb_spawned) {
        g_tb_spawned = true;
        const float ax = cam_get(obj[0] - 0x70), az = cam_get(obj[0] - 0x68), bx = cam_get(obj[1] - 0x70), bz = cam_get(obj[1] - 0x68);
        const float dx = bx - ax, dz = bz - az, len = std::sqrt(dx * dx + dz * dz);
        const float ux = len > 0.01f ? dx / len : 1.0f, uz = len > 0.01f ? dz / len : 0.0f;
        g_tb_spawn[0][0] = ax; g_tb_spawn[0][1] = az;
        g_tb_spawn[1][0] = ax + ux * k_tb_apart; g_tb_spawn[1][1] = az + uz * k_tb_apart;
        std::printf("[tb] spawns were %.1f %.1f and %.1f %.1f, height %.2f, opponent now %.1f %.1f\n", ax, az, bx, bz,
                    cam_get(obj[0] - 0x6C), g_tb_spawn[1][0], g_tb_spawn[1][1]);
        std::fflush(stdout);
    }
    const float y = cam_get(obj[0] - 0x6C);
    cam_put(obj[1] - 0x70, g_tb_spawn[1][0]); cam_put(obj[1] - 0x6C, y); cam_put(obj[1] - 0x68, g_tb_spawn[1][1]);
    cam_put(obj[1] - 0x60, g_tb_spawn[1][0]); cam_put(obj[1] - 0x5C, y); cam_put(obj[1] - 0x58, g_tb_spawn[1][1]);
}

/* The partner stands behind P1's left, as far back as k_tb_support_back, for the whole fight. */
/* Training (the maintainer 11-03): at the start of the fight the opponent is moved to k_train_apart in front of Player 1, on
 * the line between their spawns, and held there (x and z; height is free, so it can still fall and lie down), so
 * every skill reaches it without walking and it can't be knocked away. Off since 11-05 (the maintainer: the normal Training Mode,
 * normal spawns and knockdowns); TRAINING_HOLD=1 turns it on again. */
const float k_train_apart = 2.5f;
void pin_dummy()
{
    static const bool off = !(std::getenv("TRAINING_HOLD") && std::getenv("TRAINING_HOLD")[0] == '1');   /* off unless TRAINING_HOLD=1 (the maintainer 11-05) */
    static float px = 0, pz = 0; static uint32_t pinned = 0;
    if (off || !training_active() || g_tb_mode) return;
    if (!training_fight_scene()) { pinned = 0; return; }
    uint32_t obj[2];
    if (!pinned) {   /* once per fight: looking the places up again each frame drifted with Player 1 (nk1) */
        if (!tb_fighters(obj)) return;
        pinned = obj[1];
        const float ax = cam_get(obj[0] - 0x70), az = cam_get(obj[0] - 0x68), bx = cam_get(obj[1] - 0x70), bz = cam_get(obj[1] - 0x68);
        const float dx = bx - ax, dz = bz - az, len = std::sqrt(dx * dx + dz * dz);
        px = len > 0.01f ? ax + dx / len * k_train_apart : bx; pz = len > 0.01f ? az + dz / len * k_train_apart : bz;
        std::printf("[training] opponent held at %.1f %.1f, %.1f in front of player 1 (was %.1f away)\n", px, pz, k_train_apart, len); std::fflush(stdout);
    }
    cam_put(pinned - 0x70, px); cam_put(pinned - 0x68, pz);
    cam_put(pinned - 0x60, px); cam_put(pinned - 0x58, pz);
}

void tb_place_partner()
{
    if (!g_tb_place[2] || !g_tb_spawned) return;
    const float ax = g_tb_spawn[0][0], az = g_tb_spawn[0][1];
    float ux = g_tb_spawn[1][0] - ax, uz = g_tb_spawn[1][1] - az;
    const float len = std::sqrt(ux * ux + uz * uz);
    if (len < 0.01f) return;
    ux /= len; uz /= len;
    const float x = ax - ux * k_tb_support_back - uz * k_tb_support_left, z = az - uz * k_tb_support_back + ux * k_tb_support_left;
    const float y = cam_get(g_tb_place[0] + 4);
    const uint32_t p = g_tb_place[2];
    cam_put(p, x); cam_put(p + 4, y); cam_put(p + 8, z);
    cam_put(p + 0x10, x); cam_put(p + 0x14, y); cam_put(p + 0x18, z);
}

void tb_next_turn()
{
    g_tb_turn ^= 1;
    g_tb_guard[g_tb_turn] = false;   /* a guard lasts until that fighter's next turn */
    g_tb_state = TB_MENU; g_tb_row = 0; g_tb_ticks = 0;
    g_tb_gen = g_tb_gen + 1;
    std::printf("[tb] turn: fighter %d\n", g_tb_turn); std::fflush(stdout);
}

void tb_tick(uint16_t* b, uint16_t* lx, uint16_t* ly)
{
    static uint16_t prev = 0xFFFF; static int prev_stick = 0;
    static unsigned long long last_fight = 0;
    const uint16_t UP = 0x0010, DOWN = 0x0040, CROSS = 0x4000, START = 0x0008;
    const uint16_t buttons = *b, pressed = (uint16_t)(buttons & ~prev);
    prev = buttons;
    const unsigned long long now = GetTickCount64();
    const bool paused = pause_up();
    g_set_turnbased = g_tb_mode;
    if (training_fight_scene() || paused) last_fight = now;
    /* The HUD isn't drawn under the pause menu, so only a longer gap, a reload, ends the fight */
    if (!g_tb_mode || now - last_fight > 3000) {
        g_tb_running = false; g_tb_ready = false; g_tb_spawned = false; g_tb_show = false;
        g_tb_place[0] = g_tb_place[1] = g_tb_place[2] = 0;
        g_tb_cam[0] = g_tb_cam[1] = g_tb_cam[2] = 0;
        g_tb_bar0 = 0;
        g_tb_buttons[0] = g_tb_buttons[1] = 0;
        return;
    }
    g_cellpad_want_p2 = 1;
    if (paused) { g_tb_show = false; return; }
    uint32_t obj[2];
    if (!training_fight_scene() || !tb_fighters(obj)) return;
    tb_place_partner();
    /* Restart puts the fighters back on their spawn points without a reload: start over. Only once the turns
     * have begun: rival intros (Luffy and Ace) move fighters around before that (cc1). */
    if (g_tb_running && g_tb_spawned && (tb_dist(obj[0], g_tb_spawn[0][0], g_tb_spawn[0][1]) > 20.0f ||
                                       tb_dist(obj[1], g_tb_spawn[1][0], g_tb_spawn[1][1]) > 20.0f)) {
        g_tb_ready = false; g_tb_running = false; g_tb_spawned = false; g_tb_show = false;
        g_tb_buttons[0] = g_tb_buttons[1] = 0;
        std::printf("[tb] fight restarted\n"); std::fflush(stdout);
    }
    /* Through the intros, which the pad can still skip, until the health bar comes in */
    if (!g_tb_ready || !g_tb_gauge || vm_read32(g_tb_gauge) != 0x00879EA0u || vm_read32(g_tb_gauge + 0x14) == 0) { tb_place(obj); return; }
    if (!g_tb_running) {
        tb_place(obj);
        g_tb_running = true; g_tb_turn = 1; g_tb_guard[0] = g_tb_guard[1] = false;
        tb_next_turn();
    }
    const int stick = *ly < 64 ? -1 : *ly > 192 ? 1 : 0;
    const bool go_up = (pressed & UP) || (stick == -1 && prev_stick != -1);
    const bool go_down = (pressed & DOWN) || (stick == 1 && prev_stick != 1);
    prev_stick = stick;
    uint16_t cmd[2] = { 0, 0 };
    g_tb_lx[0] = g_tb_lx[1] = 128; g_tb_ly[0] = g_tb_ly[1] = 128;
    g_tb_ticks++;
    const int me = g_tb_turn, foe = me ^ 1;
    const float foe_x = cam_get(obj[foe] - 0x70), foe_z = cam_get(obj[foe] - 0x68);
    switch (g_tb_state) {
    case TB_MENU:
        g_tb_show = true;
        if (go_up || go_down) { g_tb_row ^= 1; g_tb_gen = g_tb_gen + 1; }
        if (pressed & CROSS) {
            g_tb_show = false; g_tb_ticks = 0;
            if (g_tb_row == 0) { g_tb_state = TB_APPROACH; std::printf("[tb] fighter %d attacks from %.1f\n", me, tb_dist(obj[me], foe_x, foe_z)); }
            else { g_tb_guard[me] = true; g_tb_state = TB_GUARD; std::printf("[tb] fighter %d defends\n", me); }
            std::fflush(stdout);
        }
        break;
    case TB_APPROACH:
        if (tb_dist(obj[me], foe_x, foe_z) > k_tb_reach && g_tb_ticks < 400) { tb_stick_to(me, obj[me], foe_x, foe_z, k_tb_tilt); break; }
        std::printf("[tb] fighter %d reached %.1f after %d\n", me, tb_dist(obj[me], foe_x, foe_z), g_tb_ticks); std::fflush(stdout);
        g_tb_state = TB_TAP; g_tb_ticks = 0;
        break;
    case TB_TAP:
        if (g_tb_ticks >= 6 && g_tb_ticks < 9) cmd[me] = k_tb_square;
        if (g_tb_ticks >= 9) { g_tb_state = TB_WAIT; g_tb_ticks = 0; g_tb_last_act = now; }
        break;
    case TB_WAIT:   /* both fighters have stopped starting new states */
        if ((g_tb_ticks >= 20 && now - g_tb_last_act > 700) || g_tb_ticks > 300) { g_tb_state = TB_RETURN; g_tb_ticks = 0; }
        break;
    case TB_RETURN:
        if (tb_dist(obj[me], g_tb_spawn[me][0], g_tb_spawn[me][1]) > k_tb_home && g_tb_ticks <= 500) {
            tb_stick_to(me, obj[me], g_tb_spawn[me][0], g_tb_spawn[me][1], k_tb_tilt);
            break;
        }
        g_tb_state = TB_FACE; g_tb_ticks = 0;
        break;
    case TB_FACE:   /* a short tap turns them to face each other; the pin below keeps them on their spots */
        if (g_tb_ticks >= 8 && g_tb_ticks < 14)
            for (int k = 0; k < 2; k++)
                if (!g_tb_guard[k]) tb_stick_to(k, obj[k], cam_get(obj[k ^ 1] - 0x70), cam_get(obj[k ^ 1] - 0x68), k_tb_tilt);
        if (g_tb_ticks >= 34) tb_next_turn();
        break;
    case TB_GUARD:
        if (g_tb_ticks >= 30) tb_next_turn();
        break;
    }
    /* Hits don't push anyone: a fighter stays on its spot unless it is the one moving and attacking */
    static float pushed = 0.0f;
    const bool moving = g_tb_state == TB_APPROACH || g_tb_state == TB_TAP || g_tb_state == TB_WAIT || g_tb_state == TB_RETURN;
    for (int k = 0; k < 2; k++) {
        if (moving && k == g_tb_turn) continue;
        const float d = tb_dist(obj[k], g_tb_spawn[k][0], g_tb_spawn[k][1]);
        if (k != g_tb_turn && d > pushed) pushed = d;
        cam_put(obj[k] - 0x70, g_tb_spawn[k][0]); cam_put(obj[k] - 0x68, g_tb_spawn[k][1]);
        cam_put(obj[k] - 0x60, g_tb_spawn[k][0]); cam_put(obj[k] - 0x58, g_tb_spawn[k][1]);
    }
    if (g_tb_state == TB_MENU && pushed > 0.0f) { std::printf("[tb] largest push on the other fighter since the last menu: %.3f\n", pushed); std::fflush(stdout); pushed = 0.0f; }
    if (g_tb_guard[0]) cmd[0] |= k_tb_r1;
    if (g_tb_guard[1]) cmd[1] |= k_tb_r1;
    g_tb_buttons[0] = cmd[0]; g_tb_buttons[1] = cmd[1];
    *b = (uint16_t)((buttons & START) | cmd[0]); *lx = g_tb_lx[0]; *ly = g_tb_ly[0];
}

/* The menu, drawn with GDI at twice the size it is shown. Runs on the drawing thread. */
const uint32_t* tb_stamp(short* x, short* y, short* w, short* h, short* show_w, short* show_h)
{
    if (!g_tb_show || !training_active()) return nullptr;
    const int W = 480, H = 280;
    static uint32_t out[480 * 280];
    static unsigned drawn = 0;
    static const wchar_t* const rows[2] = { L"Attack", L"Defend" };
    if (drawn != g_tb_gen) {
        drawn = g_tb_gen;
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bm && bits) {
            HGDIOBJ old = SelectObject(dc, bm);
            uint32_t* p = (uint32_t*)bits;
            for (int j = 0; j < H; j++) for (int i = 0; i < W; i++) {
                const int v = 58 - j * 36 / H;
                uint32_t c = (uint32_t)((v << 16) | (v << 8) | v);
                if (i < 6 || j < 6 || i >= W - 6 || j >= H - 6) c = 0x00C89B28u;
                const int row_top = 104 + g_tb_row * 84;
                if (j >= row_top && j < row_top + 76 && i >= 14 && i < W - 14) {
                    const int edge = i - 14 < W - 15 - i ? i - 14 : W - 15 - i;
                    const int t = edge > 120 ? 255 : edge * 255 / 120;
                    const int rr = (v * (255 - t) + 232 * t) / 255, gg = (v * (255 - t) + 186 * t) / 255, bb = (v * (255 - t) + 40 * t) / 255;
                    c = (uint32_t)((rr << 16) | (gg << 8) | bb);
                }
                p[j * W + i] = c;
            }
            SetBkMode(dc, TRANSPARENT);
            HFONT big = CreateFontW(52, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            HFONT title = CreateFontW(44, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            HGDIOBJ oldf = SelectObject(dc, title);
            wchar_t head[32];
            swprintf(head, 32, L"Player %d's turn", g_tb_turn + 1);
            SetTextColor(dc, RGB(240, 180, 50));
            RECT rc = { 0, 18, W, 90 };
            DrawTextW(dc, head, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, big);
            for (int r = 0; r < 2; r++) {
                RECT rr = { 0, 104 + r * 84, W, 180 + r * 84 };
                SetTextColor(dc, r == g_tb_row ? RGB(255, 255, 255) : RGB(210, 210, 210));
                DrawTextW(dc, rows[r], -1, &rr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            SelectObject(dc, oldf); DeleteObject(big); DeleteObject(title);
            GdiFlush();
            for (int i = 0; i < W * H; i++) {
                const uint32_t c = p[i];
                out[i] = 0xFF000000u | ((c & 0xFF) << 16) | (c & 0xFF00u) | ((c >> 16) & 0xFF);
            }
            SelectObject(dc, old);
            DeleteObject(bm);
        }
        DeleteDC(dc);
    }
    *x = 1016; *y = 200; *w = (short)W; *h = (short)H; *show_w = 240; *show_h = 140;
    return out;
}

void settings_tick()
{
    if (!g_set_stamina) return;
    for (int i = 0; i < g_bal_nobj; i++) vm_write32(g_bal_obj[i] + 0x9Cu, vm_read32(g_bal_obj[i] + 0xA0u));
}

/* State of the AUTO_FIGHT auto-pilot, which is further down. */
struct AutoFight { bool on; int stage; int step; unsigned long long t0, t, press_until; uint16_t button; bool p1_moved; };
AutoFight g_auto = { false, -1, 0, 0, 0, 0, 0, false };

void pf_note_hud(uint32_t o);
extern uint32_t g_vg_arc;

volatile bool g_six_loading = false;   /* SIX_ICALL: from leaving the character select to the fight */

/* SIX_WATCH=<hex guest address> (test only): a hardware data breakpoint (debug register 0, 4 bytes, read or
 * write) on that guest word in every thread of the game, set from a helper thread once the match is being picked.
 * Each distinct host instruction address that touches it is kept with a count and printed as an offset into
 * JStarsRecomp.exe ("[sixwatch] +0x..."), to be named with llvm-symbolizer and the build-pdb symbols: that names
 * the recompiled game function (func_XXXXXXXX) that reads the slot. */
uint32_t g_six_role_ea[2] = { 0, 0 };
struct SixHit { uint64_t rip; unsigned n; uint64_t callers[8]; int ncallers; };
SixHit g_six_hits[64]; volatile int g_six_nhits = 0;
LONG CALLBACK six_veh(EXCEPTION_POINTERS* e)
{
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    /* SIX_3V3=2: debug registers 1 and 2 sit on the two Support units' role words; right after the game writes
     * one, a Support (3) becomes a partner (2), before anything reads it */
    if (e->ContextRecord->Dr6 & 6) {
        extern uint32_t g_six_role_ea[2];
        for (int k = 0; k < 2; k++)
            if ((e->ContextRecord->Dr6 & (2ull << k)) && g_six_role_ea[k] && vm_read32(g_six_role_ea[k]) == 3u) vm_write32(g_six_role_ea[k], 2u);
        e->ContextRecord->Dr6 = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (!(e->ContextRecord->Dr6 & 1)) return EXCEPTION_CONTINUE_SEARCH;
    const uint64_t rip = e->ContextRecord->Rip;
    int i = 0;
    while (i < g_six_nhits && g_six_hits[i].rip != rip) i++;
    if (i == g_six_nhits && g_six_nhits < 64) {
        g_six_hits[i].rip = rip; g_six_hits[i].n = 0; g_six_hits[i].ncallers = 0;
        /* who called it: host frames unwound from the faulting context (named the same way, func_XXXXXXXX) */
        CONTEXT c = *e->ContextRecord;
        for (int f = 0; f < 8; f++) {
            DWORD64 img = 0;
            PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &img, nullptr);
            if (!fn) break;
            void* hd = nullptr; DWORD64 est = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, img, c.Rip, fn, &c, &hd, &est, nullptr);
            if (!c.Rip) break;
            g_six_hits[i].callers[g_six_hits[i].ncallers++] = c.Rip;
        }
        g_six_nhits = i + 1;
    }
    if (i < 64) g_six_hits[i].n++;
    e->ContextRecord->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}
DWORD WINAPI six_arm(LPVOID p)
{
    const uint64_t host = (uint64_t)(vm_base + (uint32_t)(uintptr_t)p);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te; te.dwSize = sizeof te;
    int armed = 0;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId()) continue;
        HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!th) continue;
        if (SuspendThread(th) != (DWORD)-1) {
            CONTEXT c = {}; c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(th, &c)) {
                if (host != (uint64_t)vm_base) {
                    c.Dr0 = host;
                    c.Dr7 = (c.Dr7 & ~0xF0003ull) | 1ull | (3ull << 16) | (3ull << 18);   /* L0, read/write, 4 bytes */
                }
                if (g_six_role_ea[0]) {   /* L1, L2: write only (01), 4 bytes */
                    c.Dr1 = (uint64_t)(vm_base + g_six_role_ea[0]); c.Dr2 = (uint64_t)(vm_base + g_six_role_ea[1]);
                    c.Dr7 = (c.Dr7 & ~(0xFFull << 20) & ~0x3Cull) | (1ull << 2) | (1ull << 4) | (1ull << 20) | (3ull << 22) | (1ull << 24) | (3ull << 26);
                }
                if (SetThreadContext(th, &c)) armed++;
            }
            ResumeThread(th);
        }
        CloseHandle(th);
    }
    CloseHandle(snap);
    std::printf("[sixwatch] guest %08X (host %llX) armed in %d threads, exe base %p\n", (uint32_t)(uintptr_t)p,
                (unsigned long long)host, armed, (void*)GetModuleHandleA(nullptr));
    std::fflush(stdout);
    return 0;
}
/* SIX_STACKS=<seconds> (test only): that long after the character select is left, from a helper thread, every
 * other thread of the game is paused and its host call stack (12 frames) printed as exe offsets, to name with
 * llvm-symbolizer on build-pdb: where a stuck "Now loading" waits. Once. */
DWORD WINAPI six_stacks(LPVOID)
{
    const uint64_t base = (uint64_t)GetModuleHandleA(nullptr);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te; te.dwSize = sizeof te;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId()) continue;
        HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!th) continue;
        if (SuspendThread(th) != (DWORD)-1) {
            CONTEXT c = {}; c.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(th, &c)) {
                std::printf("[sixstack] thread %lu:", te.th32ThreadID);
                for (int f = 0; f < 12 && c.Rip; f++) {
                    const uint64_t rip = c.Rip;
                    if (rip >= base && rip < base + 0x10000000ull) std::printf(" +0x%llX", (unsigned long long)(rip - base));
                    else std::printf(" (%llX)", (unsigned long long)rip);
                    DWORD64 img = 0;
                    PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &img, nullptr);
                    if (!fn) break;
                    void* hd = nullptr; DWORD64 est = 0;
                    RtlVirtualUnwind(UNW_FLAG_NHANDLER, img, c.Rip, fn, &c, &hd, &est, nullptr);
                }
                std::printf("\n");
            }
            ResumeThread(th);
        }
        CloseHandle(th);
    }
    CloseHandle(snap);
    std::fflush(stdout);
    return 0;
}
void six_stacks_tick(bool picking)
{
    static const char* e = std::getenv("SIX_STACKS");
    static unsigned long long left = 0; static bool done = false, was = false;
    if (!e || done) return;
    const unsigned long long now = GetTickCount64();
    if (was && !picking && !left) left = now;
    was = picking;
    if (left && now - left >= (unsigned long long)std::atoi(e) * 1000ull) {
        done = true;
        HANDLE t = CreateThread(nullptr, 0, six_stacks, nullptr, 0, nullptr);
        if (t) { WaitForSingleObject(t, 10000); CloseHandle(t); }
    }
}
void six_watch_tick(bool picking)
{
    six_stacks_tick(picking);
    static const char* e0 = std::getenv("SIX_WATCH");
    static const bool role = std::getenv("SIX_3V3") && std::getenv("SIX_3V3")[0] == '2';
    static const char* e = e0 ? e0 : (role ? "0" : nullptr);
    static bool armed = false; static int printed = 0;
    if (!e) return;
    if (role && !g_six_role_ea[0]) {
        const uint32_t w = vm_read32(vm_read32(0x008D3888u - 0x7A90u));
        if (w > 0x10000u && w < 0xF0000000u) {
            g_six_role_ea[0] = w + 0x60 + 4 * 0x94 + 8; g_six_role_ea[1] = w + 0x60 + 5 * 0x94 + 8;
            std::printf("[six] role watch on %08X %08X\n", g_six_role_ea[0], g_six_role_ea[1]); std::fflush(stdout);
        }
    }
    /* armed again every half second until 20 s after the character select is left: the match is loaded by threads
     * made after the first arming (sixwatch: no game hit after the select) */
    static unsigned long long last = 0, left = 0;
    const unsigned long long now = GetTickCount64();
    if (armed && !picking && !left) left = now;
    if (picking) left = 0;
    const bool again = armed && now - last > 500 && (picking || (left && now - left < 20000));
    if ((!armed && picking) || again) {
        if (!armed) AddVectoredExceptionHandler(1, six_veh);
        armed = true; last = now;
        HANDLE t = CreateThread(nullptr, 0, six_arm, (LPVOID)(uintptr_t)std::strtoul(e, nullptr, 16), 0, nullptr);
        if (t) { WaitForSingleObject(t, 5000); CloseHandle(t); }
    }
    const uint64_t base = (uint64_t)GetModuleHandleA(nullptr);
    while (printed < g_six_nhits) {
        const SixHit& h = g_six_hits[printed++];
        std::printf("[sixwatch] new hit +0x%llX at %llu callers", (unsigned long long)(h.rip - base), GetTickCount64());
        for (int f = 0; f < h.ncallers; f++) std::printf(" +0x%llX", (unsigned long long)(h.callers[f] - base));
        std::printf("\n");
        std::fflush(stdout);
    }
}
int training_icall(uint32_t target, uint64_t* gpr)
{
    if (g_probe_on == 1) probe_icall(target, (uint32_t)gpr[3], (uint32_t)gpr[4]);
    /* SIX_ICALL=1 (test only): calls into the setup-table functions (0x00183528..0x00186E00) while the match loads,
     * each target with its first arguments, the first 6 times each, to find what asks which slot is a Support. */
    { static const bool on = std::getenv("SIX_ICALL") != nullptr;
      /* the call func_001855B4 makes with its stack copy of the 6 slots (r5; slot 0's character at +0x18): the
       * battle side's receiver */
      if (on) {
          const uint32_t c0 = vm_read32(0x009540F0u), c1 = vm_read32(0x009540F0u + 0x2C);
          for (int ri = 3; ri <= 6 && c0; ri++) {
              const uint32_t r = (uint32_t)gpr[ri];
              if (r < 0x00010000u || r >= 0xF0000000u || (r & 3)) continue;
              for (uint32_t o = 0; o <= 0x30; o += 4)
                  if (vm_read32(r + o) == c0 && vm_read32(r + o + 0x2C) == c1) {
                      static int n = 0;
                      if (n++ < 12) { std::printf("[sixcall] receiver %08X r%d=%08X (+0x%X) r3=%08X\n", target, ri, r, o, (uint32_t)gpr[3]); std::fflush(stdout); }
                      break;
                  }
          }
      }
      if (on && target >= 0x00183528u && target < 0x00186E00u) {
          static uint32_t seen[256]; static int cnt[256]; static int ns = 0;
          int i = 0; while (i < ns && seen[i] != target) i++;
          if (i == ns && ns < 256) { seen[ns] = target; cnt[ns] = 0; ns++; }
          if (i < 256 && cnt[i]++ < 6) {
              std::printf("[sixcall] %08X r3=%08X r4=%d r5=%d r6=%d\n", target, (uint32_t)gpr[3], (int)(int32_t)gpr[4],
                          (int)(int32_t)gpr[5], (int)(int32_t)gpr[6]);
              std::fflush(stdout);
          }
      } }
    if (g_tb_mode) {
        /* Battle HUD parts share a layout: slot 0x10 updates, slot 0x14 draws, +0x14 is 0 until they come in.
         * Skipping the draw hides the minimap, the timer, the top bar and the arc in the middle. */
        const uint32_t o = (uint32_t)gpr[3];
        if (o > 0x40000000u && o < 0x4CA30000u && (o & 3) == 0) {
            const uint32_t vt = vm_read32(o);
            if (vt >= 0x00879A48u && vt < 0x0087A400u) {
                if (vt == 0x00879EA0u) g_tb_gauge = o;   /* the health bar */
                if (vt == 0x00879DA0u) {
                    if (!g_tb_bar0 || o < g_tb_bar0) g_tb_bar0 = o;
                    if (o == g_tb_bar0 + 0x480u && target == vm_read32(vm_read32(vt + 0x14))) return 1;
                }
                const bool hide = vt == 0x00879FA0u || vt == 0x0087A160u || vt == 0x0087A2E0u || vt == 0x0087A1E0u || vt == 0x0087A260u;
                if (hide && target == vm_read32(vm_read32(vt + 0x14))) return 1;
            }
        }
    }
    { static const bool pf = std::getenv("PROBE_FADE") != nullptr;
      const uint32_t o = (uint32_t)gpr[3];
      if (pf && o > 0x40000000u && o < 0x4CA30000u && (o & 3) == 0) { const uint32_t vt = vm_read32(o); if (vt >= 0x00879000u && vt < 0x0087B000u) pf_note_hud(o); } }
    if (training_active()) {
        const uint32_t o = (uint32_t)gpr[3];
        if (o > 0x40000000u && o < 0x4CA30000u && (o & 3) == 0 && vm_read32(o) == 0x0087A1E0u) g_vg_arc = o;
        if (o > 0x40000000u && o < 0x4CA30000u && (o & 3) == 0 && vm_read32(o) == 0x00879DA0u) {
            int i = 0;
            for (; i < g_ncombo_gauge; i++) if (g_combo_gauge[i] == o) break;
            if (i == g_ncombo_gauge && g_ncombo_gauge < 8) g_combo_gauge[g_ncombo_gauge++] = o;
        }
    }
    if (target == k_cam_update && vm_read32((uint32_t)gpr[3]) == k_cam_vt) {
        freecam_note((uint32_t)gpr[3]);
        if (training_active() && g_set_freecam && !g_tb_running) return 1;   /* the game stops steering the camera */
    }
    act_note(target, gpr);
    if (target == 0x0004296Cu) act_swap((uint32_t)gpr[3]);
    if (target == 0x0004296Cu && g_tb_running) g_tb_last_act = GetTickCount64();
    if (target == 0x0004296Cu && g_tb_mode && vm_read32((uint32_t)gpr[3] + 4) == 0) g_tb_ready = true;
    if ((target == 0x0049A490u || target == 0x00498EE0u) && vm_read32((uint32_t)gpr[3]) == k_pause_vt) {
        if (target == 0x0049A490u) pause_open(gpr);
        else { g_pause_obj = (uint32_t)gpr[3]; g_pause_ms = GetTickCount64(); }
    }
    if (training_active()) {
        if (target == 0x0004296Cu) bal_action((uint32_t)gpr[3]);
        else if (target == 0x00029E04u) bal_motion((uint32_t)gpr[3], (uint32_t)gpr[4], (uint32_t)gpr[5]);
    }
    if (target == 0x0004296Cu && g_auto.on && vm_read32((uint32_t)gpr[3] + 4) == 0) g_auto.p1_moved = true;
    screens_note(target, (uint32_t)gpr[3]);
    static int dummy_mask = -1;
    if (dummy_mask < 0) { const char* e = std::getenv("TRAINING_DUMMY_TEST"); dummy_mask = e ? (int)std::strtol(e, nullptr, 16) : 0; }
    if (training_pad_getter(target) && vm_read32((uint32_t)gpr[3]) == 0x00875858u) {
        /* First time a fight reads a pad: a 0-star CPU's pad is handed to a
         * human driver (+0x18 and +0x1FC 3 -> 1). With the CPU driver its AI
         * still jumps and attacks when the player comes close (the maintainer 10-30,
         * tw1), even with the pad's getters answering "nothing pressed"; with
         * the human driver it never hit back in 12 hits (tw2). The getters
         * keep answering "nothing pressed" too, so a pad on port 2 can't move
         * it either. */
        const uint32_t o = (uint32_t)gpr[3];
        int i = 0;
        for (; i < g_npads; i++) if (g_pads[i] == o) break;
        if (i == g_npads && g_npads < 8) {
            g_pads[g_npads++] = o;
            const uint32_t slot = vm_read32(o + 0x14);
            const int mask = dummy_mask | g_dummy_live | (g_tb_mode ? 0xE : 0);   /* Turn-based: no AI, whatever the stars */
            if (training_active() && vm_read32(o + 0x18) == 3 && slot < 4 && ((mask >> slot) & 1) && g_ndummy < 4) {
                g_dummy_pads[g_ndummy++] = o;
                vm_write32(o + 0x18, 1);
                vm_write32(o + 0x1FC, 1);
                std::printf("[training] pad 0x%08X (fighter %u) is the training dummy\n", o, slot);
            }
        }
        for (int k = 0; k < g_ndummy; k++)
            if (g_dummy_pads[k] == o && !(training_active() && g_set_turnbased)) { gpr[3] = 0; return 1; }
    }
    /* SIX_PROBE: func_0008A540 makes a fighter's health object (class 0x00875198, 0x230 bytes) through a factory call;
     * the host frames above it name the battle code that creates the fighters (llvm-symbolizer on build-pdb). */
    if (target == 0x0008A540u) {
        static const bool sp = std::getenv("SIX_PROBE") != nullptr; static int n = 0;
        if (sp && n < 12) { n++;
            void* fr[16]; const USHORT nf = RtlCaptureStackBackTrace(0, 16, fr, nullptr);
            const uint64_t base = (uint64_t)GetModuleHandleA(nullptr);
            std::printf("[six] health object made (#%d) at %llu, frames", n, GetTickCount64());
            for (USHORT f = 0; f < nf; f++) std::printf(" +0x%llX", (unsigned long long)((uint64_t)fr[f] - base));
            std::printf("\n"); std::fflush(stdout); }
    }
    if (target == 0x00085CECu) {
        g_hud_ms = GetTickCount64();
        /* SIX_PROBE: a fighter's health object and its class (vtable), and the TOC entries that hold that vtable,
         * so its constructor -- run once per fighter while the match loads -- can be found by r2 offset. Once. */
        { static const bool sp = std::getenv("SIX_PROBE") != nullptr; static bool done = false;
          if (sp && !done) { done = true;
              const uint32_t o = (uint32_t)gpr[3], vt = vm_read32(o);
              std::printf("[six] health object %08X, vtable %08X\n", o, vt);
              for (int32_t off = -0x8000; off < 0x8000; off += 4)
                  if (vm_read32(0x008D3888u + (uint32_t)off) == vt) std::printf("[six] TOC r2%s0x%X holds that vtable\n", off < 0 ? "-" : "+", off < 0 ? -off : off);
              std::fflush(stdout); } }
        if (training_active() && training_fight_scene()) bal_note_fighter((uint32_t)gpr[3]);
    }
    if (target == 0x00085CECu && training_low() > 0) {   /* health getter: the HUD reads every fighter */
        const uint32_t obj = (uint32_t)gpr[3];
        const int32_t max = (int32_t)vm_read32(obj + 0x98);
        if (max <= 0 || max > 1000000 || !training_fight_scene() || training_find(obj) >= 0) return 0;
        const int n = g_train_count;
        if (n >= 8) return 0;
        g_train[n].obj = obj; g_train[n].last_hit = 0; g_train_count = n + 1;
        vm_write32(obj + 0x90, (uint32_t)training_low()); vm_write32(obj + 0x94, (uint32_t)training_low());
        std::printf("[training] fighter 0x%08X max %d, test start at %d\n", obj, max / 100, training_low());
        return 0;
    }
    if (target != 0x00085B84u || !training_active()) return 0;
    const uint32_t obj = (uint32_t)gpr[3];
    const int32_t max = (int32_t)vm_read32(obj + 0x98);
    if (max <= 0 || max > 1000000) return 0;
    const int32_t cur = (int32_t)vm_read32(obj + 0x90), rec = (int32_t)vm_read32(obj + 0x94);
    const int32_t d90 = (int32_t)gpr[4], d94 = (int32_t)gpr[5];
    /* TRAINING_KO_TEST: real KOs in Training, to study the KO count */
    static const bool ko_test = std::getenv("TRAINING_KO_TEST") != nullptr;
    if (ko_test) { if (d90 > 0 || d94 > 0) bal_hit(obj, gpr, cur, rec, d90, d94); return 0; }
    if (d90 > 0 && cur - d90 < 100) gpr[4] = (uint64_t)(int64_t)(cur > 100 ? cur - 100 : 0);
    if (d94 > 0 && rec - d94 < 100) gpr[5] = (uint64_t)(int64_t)(rec > 100 ? rec - 100 : 0);
    if (d90 <= 0 && d94 <= 0) return 0;   /* a heal */
    const unsigned long long now = GetTickCount64();
    bal_hit(obj, gpr, cur, rec, d90, d94);
    std::printf("[training] hit 0x%08X health %d (%d) damage %d (%d) -> %d (%d)\n", obj, cur, rec, d90, d94,
                (int32_t)gpr[4], (int32_t)gpr[5]);
    const int i = training_find(obj);
    if (i >= 0) { g_train[i].last_hit = now; return 0; }
    const int n = g_train_count;
    if (n < 8) {
        g_train[n].obj = obj; g_train[n].last_hit = now; g_train_count = n + 1;
        std::printf("[training] fighter 0x%08X max %d\n", obj, max / 100);
    }
    return 0;
}


/* Once per pad poll: refill whoever has been hit, 1 s after the combo number has gone away (the maintainer 11-03).
 * If no combo number came up after the hit, the old rule: 1.5 s without a hit. */
void training_tick()
{
    { /* KD_STUNFILE=<file> (test only): when the number in the file changes, write it to the last hit fighter's
       * build-up (health object +0x1A4) once, so the next hit shows whether that level knocks down. */
      static const char* kf = std::getenv("KD_STUNFILE");
      static int last = -1; static unsigned long long t = 0;
      if (kf && g_kd_obj && GetTickCount64() - t > 200) {
          t = GetTickCount64();
          int v = -1;
          if (FILE* ff = std::fopen(kf, "r")) { if (std::fscanf(ff, "%d", &v) != 1) v = -1; std::fclose(ff); }
          if (v >= 0 && v != last) {
              last = v; vm_write32(g_kd_obj + 0x1A4, (uint32_t)v);
              if (FILE* f = bal_file()) { std::fprintf(f, "%llu kd-stun %d\n", GetTickCount64(), v); std::fflush(f); }
          }
      }
    }
    { /* KD_CLEAR=<file> (test only): mode 1 holds the hit fighter's owner +0x1B8 and -0x70 at 0, mode 2 its health
       * object +0x1AC at 0: candidates for the get-up immunity (pin3). */
      static const char* kf = std::getenv("KD_CLEAR");
      static int mode = 0; static unsigned long long t = 0;
      if (kf && GetTickCount64() - t > 200) { t = GetTickCount64(); int v = 0; if (FILE* ff = std::fopen(kf, "r")) { if (std::fscanf(ff, "%d", &v) != 1) v = 0; std::fclose(ff); } mode = v; }
      if (kf && g_kd_owner && mode == 1) { vm_write32(g_kd_owner + 0x1B8, 0); vm_write32(g_kd_owner - 0x70, 0); }
      if (kf && g_kd_obj && mode == 2) vm_write32(g_kd_obj + 0x1AC, 0);
    }
    { /* OAM_RECOVER=<n> (test only): once per fight, every loaded get-up action file (dmg_recover .oam: 232 bytes,
       * OAFB, motion 200) gets n at +0x40, which holds 64 in every fighter's data. */
      static const char* e = std::getenv("OAM_RECOVER");
      static bool done = false;
      if (!training_fight_scene()) done = false;
      else if (e && !done && g_kd_owner) {
          done = true;
          int n = 0;
          for (uint32_t ea = 0x40000000u; ea < 0x4CA30000u; ea += 0x10) {
              if (vm_read32(ea) != 0x4F414642u || vm_read32(ea + 4) != 2u || vm_read32(ea + 0x4C) != 0x00C8FFFFu || vm_read32(ea + 0x44) != 8u) continue;
              vm_write32(ea + 0x40, (uint32_t)std::atoi(e)); n++;
          }
          if (FILE* f = bal_file()) { std::fprintf(f, "%llu oam-recover %d files set to %d\n", GetTickCount64(), n, std::atoi(e)); std::fflush(f); }
      }
    }
    pin_dummy();
    { static bool scanned = false; static unsigned long long up = 0;   /* once per Training fight, 1 s in, before the first hit */
      if (!training_fight_scene()) { scanned = false; up = 0; }
      else if (!up) up = GetTickCount64();
      else if (!scanned && training_no_knockdown() && GetTickCount64() - up > 1000) { scanned = true; kd_flatten_all(); } }
    if (training_no_knockdown() && training_fight_scene())   /* the too-many-hits build-up (health object +0x1A4, knockdown at 150) */
        for (int i = 0; i < g_train_count; i++) vm_write32(g_train[i].obj + 0x1A4, 0);
    kd_dump(false);
    if (!training_fight_scene()) { g_train_count = 0; g_npads = 0; g_ndummy = 0; g_ncombo_gauge = 0; g_combo_up = false; kd_restore(); return; }
    const unsigned long long now = GetTickCount64();
    bool up = false;
    for (int k = 0; k < g_ncombo_gauge; k++)
        if (vm_read32(g_combo_gauge[k]) == 0x00879DA0u && vm_read32(g_combo_gauge[k] + 0x3EC) != 0) up = true;
    if (g_combo_up && !up) g_combo_end = now;
    g_combo_up = up;
    for (int i = 0; i < g_train_count; i++) {
        const uint32_t obj = g_train[i].obj;
        const unsigned long long hit = g_train[i].last_hit;
        if (hit == 0 || up) continue;   /* 0: not hit yet; up: the combo number is still showing */
        const bool shown = g_combo_end > hit;   /* a combo number came and went after this hit */
        if (shown ? now - g_combo_end < 1000 : now - hit < 1500) continue;
        const uint32_t max = vm_read32(obj + 0x98);
        if (max == 0 || max > 1000000) continue;
        if (vm_read32(obj + 0x90) < max || vm_read32(obj + 0x94) < max) {
            vm_write32(obj + 0x94, max);
            vm_write32(obj + 0x90, max);
            std::printf("[training] refill 0x%08X to %u at %llu (combo number gone at %llu)\n", obj, max / 100, now, g_combo_end);
        }
    }
}

/* Character select in Training, found by the screen tracking: the slot cursor is
 * at screen +0x38 (setup slot numbers), the screen's two copies of the setup
 * records at +0x38 and +0x168; settings panel +0x2C == 2 open, +0x9C row (1 = COM
 * Level), +0x48 the level (written through to the setup record live). */
bool on_charsel()
{
    return screen_up(g_seen_charsel, 0x0087C3D0u);
}

uint32_t charsel_cursor() { return vm_read32(g_seen_charsel.obj + 0x38); }
bool panel_up() { return screen_up(g_seen_panel, 0x0087C4A0u); }
uint32_t panel_open() { return panel_up() ? vm_read32(g_seen_panel.obj + 0x2C) : 0; }
uint32_t panel_row() { return vm_read32(g_seen_panel.obj + 0x9C); }
uint32_t panel_level() { return vm_read32(g_seen_panel.obj + 0x48); }

/* Training's slot locks: the CPU partners (slots 1 and 3) do not take part and
 * show the game's own padlock, as the opponent's slots do in Arcade (ar1
 * against cs2): setup record +0x04 flags bit 0, +0x08 state 2, +0x0C character
 * 0, +0x18 level 1, +0x20 0, also in the screen's two copies of the records.
 * Written on every poll of the screen, whichever way it was reached (from the
 * main menu, or Re-select after a match: The maintainer 10-30). A human Player 2 in a slot
 * (driver not CPU) stays free. */
bool g_locked[4] = { false, false, false, false };

void training_lock_slots()
{
    if (!on_charsel()) return;
    const uint32_t screen = g_seen_charsel.obj;
    const uint32_t bases[3] = { 0x009540E4u, screen + 0x38, screen + 0x168 };
    const int32_t sup_id = (int32_t)vm_read32(setup_field(4, 0x0C));
    /* 208 has no pack */
    const int sup = vm_read32(setup_field(4, 0x08)) != 2 && ((sup_id >= 0 && sup_id < 42) || (sup_id >= 200 && sup_id < 214 && sup_id != 208)) ? sup_id : -1;
    if (g_tb_mode && sup >= 200 && !std::getenv("PS3_FS_ALIAS_DIR")) {
        /* port\build\JStarsRecomp.exe -> jstars\support_fighters, wherever the game was started from */
        char dir[MAX_PATH + 32] = { 0 };
        GetModuleFileNameA(nullptr, dir, MAX_PATH);
        for (int up = 0; up < 3; up++) if (char* cut = std::strrchr(dir, '\\')) *cut = 0;
        std::strcat(dir, "\\support_fighters");
        _putenv_s("PS3_FS_ALIAS_DIR", dir);
    }
    for (int k = 1; k < 4; k += 2) {
        if (vm_read32(setup_field(k, 0x00)) != 2) continue;
        if (!g_locked[k]) { g_locked[k] = true; std::printf("[training] slot %d locked\n", k); }
        for (uint32_t base : bases) {
            const uint32_t r = base + 0x2Cu * (uint32_t)k;
            vm_write32(r + 0x04, 1);
            vm_write32(r + 0x08, 2);
            vm_write32(r + 0x0C, 0);
            vm_write32(r + 0x18, 1);
            vm_write32(r + 0x20, 0);
            /* Turn-based: the chosen Support stands on the map as Player 1's partner, with no AI. The game has no
             * fighter pack for a Support-only character, so jstars\\support_fighters holds one for each (pc4). */
            if (g_tb_mode && k == 1 && sup >= 0) {
                vm_write32(r + 0x08, 0);
                vm_write32(r + 0x0C, (uint32_t)sup);
                vm_write32(r + 0x18, 0);
            }
        }
    }
}

void training_unlock_slots()
{
    for (int k = 0; k < 4; k++) { g_locked[k] = false; g_dummy[k] = false; }
    g_dummy_live = 0;
}

/* On character select: the game itself ignores Cross and Triangle on a padlocked slot,
 * and the port must not filter them: its "Return to the previous screen?" box opened
 * from such a slot takes Cross to answer (the maintainer 10-30, tp1).
 * Select (Default, which would bring them back) does nothing, and on the COM
 * Level row of a CPU 0 stars sits between 5 and 1 (the maintainer 10-30): Left from 1
 * star and Right from 5 stars give 0, Right from 0 gives 1, Left from 0 gives
 * 5. The game itself wraps 1 <-> 5, and it repeats a held direction, so the
 * port watches the level rather than single presses. */
void training_charsel(uint16_t* b, uint16_t* lx)
{
    const uint16_t LEFT = 0x0080, RIGHT = 0x0020, SELECT = 0x0001;
    static uint16_t prev_dir = 0;
    static bool hold = false;       /* swallow left/right until they are let go */
    static int want_zero = 0;       /* Right at 5 stars: polls left to see the game wrap to 1 (then 0 stars) */
    static uint32_t last_level = 0xFFFFFFFFu;
    const uint32_t cursor = charsel_cursor();
    *b &= (uint16_t)~SELECT;
    const uint16_t dir = (uint16_t)(((*b & LEFT) || *lx < 64) ? LEFT : ((*b & RIGHT) || *lx > 192) ? RIGHT : 0);
    const bool edge = dir && dir != prev_dir;
    prev_dir = dir;
    if (!dir) hold = false;
    const bool com_row = panel_open() == 2 && panel_row() == 1 &&
                         cursor >= 1 && cursor < 4 && vm_read32(setup_field((int)cursor, 0x00)) == 2;
    /* the panel flags blink for a poll while the stars change (st3), so state
     * carries over polls where the row does not read as the COM Level row */
    if (!com_row) { if (want_zero > 0) want_zero--; }
    else {
        const uint32_t level = panel_level();   /* 0-4 = 1-5 stars */
        if (g_dummy[cursor] && level != 0) g_dummy[cursor] = false;   /* the game moved on */
        if (want_zero && level == 0 && last_level == 4) {             /* 5 -> 1 just happened */
            g_dummy[cursor] = true; want_zero = 0; hold = (dir != 0);
            std::printf("[training] slot %u: 0 stars (training dummy)\n", cursor);
        }
        if (!hold) {
            if (g_dummy[cursor]) {
                if (dir == RIGHT && edge) {          /* 0 -> 1: stay on the game's 1 star */
                    g_dummy[cursor] = false; hold = true;
                    std::printf("[training] slot %u: 1 star\n", cursor);
                } else if (dir == LEFT && edge) {    /* 0 -> 5: let the game wrap 1 -> 5 */
                    g_dummy[cursor] = false;
                    std::printf("[training] slot %u: 5 stars\n", cursor);
                } else if (dir) {
                    hold = true;
                }
            } else if (dir == LEFT && level == 0) {  /* 1 -> 0, also when Left is held */
                g_dummy[cursor] = true; hold = true;
                std::printf("[training] slot %u: 0 stars (training dummy)\n", cursor);
            } else if (dir == RIGHT && level == 4) { /* 5 -> 0: the game goes to 1, the port adds 0 */
                want_zero = 60;   /* the level can change after the press is let go */
            }
        }
        if (want_zero > 0) { if (level != 4) want_zero = 0; else want_zero--; }   /* went elsewhere, or timed out */
        last_level = level;
    }
    if (hold) { *b &= (uint16_t)~(LEFT | RIGHT); *lx = 128; }
}

/* The stage select boxes slide with their scene: +0x1C is how far right of their place they are, in
 * 1280x720 pixels (732 off the screen, 0 in place). Box state +0x2C is 0 once the screen has finished
 * with them, so with no slide left they are about to go under the fade to the fight: -1.
 * The game is 2 frames ahead of the frame being drawn (screenshots tm5: the padlock at 153 with the
 * box at 406), so the pictures use the value from 2 frames ago. Runs once per drawn frame. */
int g_slide_drawn = 732;
float g_lock_white = 0.0f;   /* how far the screen has faded to white, for the padlock (0 none, 1 white) */

void stage_slide_tick()
{
    const uint32_t box = g_seen_stage_time.obj;
    union { uint32_t u; float f; } slide; slide.u = vm_read32(vm_read32(vm_read32(box + 0x28) + 4) + 0x1C);
    const int off = slide.f > 0.0f && slide.f < 2000.0f ? (int)slide.f : 0;
    static int hist[2] = { 732, 732 };
    g_slide_drawn = hist[1];
    hist[1] = hist[0];
    hist[0] = off == 0 && vm_read32(box + 0x2C) == 0 ? -1 : off;
    /* Finish fades the screen to white: the screen's state (+0x48) is 9 while it is up, 0xD on Start,
     * 0xE while it fades (500 ms, pf1) and 0xF once it is white. The padlock fades with it. */
    static unsigned long long fade_t0 = 0;
    const uint32_t state = vm_read32(g_seen_stage.obj + 0x48);
    const unsigned long long now = GetTickCount64();
    if (state == 0xE && fade_t0 == 0) fade_t0 = now;
    if (state >= 1 && state < 0xD) fade_t0 = 0;   /* 0 comes after 0xF, while the frames drawn still show the box (cf3) */
    float w = 0.0f;
    if (state == 0xF || (state == 0 && fade_t0)) w = 1.0f;
    else if (fade_t0 && now > fade_t0 + 50) w = (float)(now - fade_t0 - 50) / 500.0f;   /* the frame drawn is 2 frames behind */
    g_lock_white = w > 1.0f ? 1.0f : w;
}

/* The 0-star look: the game draws 1 lit star and 4 empty ones, so the second
 * (empty) star is copied over the first in the finished frame (1280x720
 * positions measured on screenshots cs2/cs3). */
int training_copy_rects(short (*r)[6], int max)
{
    if (training_active() && screen_up(g_seen_stage_time, 0x00880018u)) {
        stage_slide_tick();
        const int off = g_slide_drawn;
        if (off < 0 || 1065 + off + 24 > 1280) return 0;
        r[0][0] = (short)(1040 + off); r[0][1] = 372; r[0][2] = (short)(1065 + off); r[0][3] = 372; r[0][4] = 24; r[0][5] = 22;   /* the right arrow */
        return 1;
    }
    if (!training_active() || !on_charsel()) return 0;
    static const short k_slot_star1[4][2] = { { 0, 0 }, { 429, 237 }, { 212, 413 }, { 429, 500 } };
    int n = 0;
    for (int k = 1; k < 4 && n < max; k++) {
        if (!g_dummy[k] || vm_read32(setup_field(k, 0x18)) != 0 || vm_read32(setup_field(k, 0x08)) == 2) continue;
        const short x = k_slot_star1[k][0], y = k_slot_star1[k][1];
        r[n][0] = (short)(x + 30); r[n][1] = y; r[n][2] = x; r[n][3] = y; r[n][4] = 24; r[n][5] = 22; n++;
    }
    const uint32_t cursor = charsel_cursor();
    if (n < max && panel_open() == 2 && cursor < 4 && g_dummy[cursor] && panel_level() == 0) {
        r[n][0] = 832; r[n][1] = 281; r[n][2] = 792; r[n][3] = 281; r[n][4] = 35; r[n][5] = 35; n++;
    }
    return n;
}

/* Stage select in Training: the time limit is locked to Unlimited. The screen holds the seconds at
 * +0x78 (0 = Unlimited) and Finish copies them to the battle settings (0x0026FAA0, field +0x18). The
 * time limit box shows its own choice at +0x40. The screen keeps the focused box at +0x58
 * (0 stage, 2 music, 3 time). */
bool on_stagesel()
{
    return screen_up(g_seen_stage, 0x0087FCF8u) && screen_up(g_seen_stage_time, 0x00880018u);
}

uint32_t stage_focus() { return vm_read32(g_seen_stage.obj + 0x58); }
uint32_t stage_time() { return vm_read32(g_seen_stage_time.obj + 0x40); }

void stage_lock(uint16_t* b, uint16_t* lx)
{
    if (!on_stagesel()) return;
    vm_write32(g_seen_stage.obj + 0x78, 0);
    vm_write32(g_seen_stage_time.obj + 0x40, 0);
    const uint16_t CROSS = 0x4000, LEFT = 0x0080, RIGHT = 0x0020;
    if (stage_focus() == 3) {   /* the time limit can't be opened or changed */
        *b = (uint16_t)(*b & ~(CROSS | LEFT | RIGHT));
        *lx = 128;
    }
}

/* The padlock on the time limit box, 24x24, on a dark badge that covers the left arrow. */
/* FPS_COUNTER=1 ("FPS counter" in the launcher, the maintainer 11-04): the game's own frames per second (its flips, as the
 * window title counts them), small in the bottom-left corner. White on a dark box, drawn at twice the shown size so
 * it stays sharp; redrawn when the number changes, counted over the last second. */
extern "C" unsigned cellGcm_flip_request_count(void);
const uint32_t* fps_stamp(short* x, short* y, short* w, short* h, short* show_w, short* show_h)
{
    const int W = 136, H = 44;
    static uint32_t out[136 * 44];
    static unsigned long long t0 = 0;
    static unsigned f0 = 0;
    static int fps = -1, drawn = -2;
    const unsigned long long now = GetTickCount64();
    const unsigned flips = cellGcm_flip_request_count();
    /* Hidden through the logos and the opening movie (scene steps 0-2, then 3 for the black moment between): it comes
     * with the title screen (step 4, title1/title2 runs), fading in from black over half a second as the title does,
     * and stays from then on. */
    static unsigned long long shown_at = 0;
    if (!shown_at) {
        if (vm_read32(0x0094DEC4u) != 4) return nullptr;
        shown_at = now;
    }
    if (t0 == 0) { t0 = now; f0 = flips; }
    if (now - t0 >= 1000) { fps = (int)((flips - f0) * 1000ull / (now - t0)); t0 = now; f0 = flips; }
    if (drawn != fps) {
        drawn = fps;
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bm && bits) {
            HGDIOBJ old = SelectObject(dc, bm);
            uint32_t* p = (uint32_t*)bits;
            for (int i = 0; i < W * H; i++) p[i] = 0x00181818u;
            SetBkMode(dc, TRANSPARENT);
            HFONT f = CreateFontW(34, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            HGDIOBJ oldf = SelectObject(dc, f);
            wchar_t txt[24];
            if (fps < 0) swprintf(txt, 24, L"FPS --"); else swprintf(txt, 24, L"FPS %d", fps);
            SetTextColor(dc, RGB(255, 255, 255));
            RECT rc = { 0, 0, W, H };
            DrawTextW(dc, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, oldf); DeleteObject(f);
            GdiFlush();
            for (int i = 0; i < W * H; i++) {
                const uint32_t c = p[i];
                out[i] = 0xFF000000u | ((c & 0xFF) << 16) | (c & 0xFF00u) | ((c >> 16) & 0xFF);
            }
            SelectObject(dc, old);
            DeleteObject(bm);
        }
        DeleteDC(dc);
    }
    *x = 8; *y = 720 - 8 - 22; *w = (short)W; *h = (short)H; *show_w = 68; *show_h = 22;
    if (now - shown_at >= 500) return out;
    static uint32_t fading[136 * 44];   /* the picture scaled toward black: the stamp has no transparency */
    const uint32_t k = (uint32_t)((now - shown_at) * 256 / 500);
    for (int i = 0; i < W * H; i++) {
        uint32_t c = 0xFF000000u;
        for (int sft = 0; sft < 24; sft += 8) c |= ((((out[i] >> sft) & 255) * k) >> 8) << sft;
        fading[i] = c;
    }
    return fading;
}

const uint32_t* stage_lock_stamp(short* x, short* y, short* w, short* h, short* show_w, short* show_h)
{
    if (const uint32_t* menu = tb_stamp(x, y, w, h, show_w, show_h)) return menu;
    if (!training_active() || !on_stagesel()) return nullptr;
    static uint32_t px[24 * 24];
    static bool made = false;
    if (!made) {
        made = true;
        const uint32_t badge = 0xFF2A2626u, gold = 0xFF43BEE8u, dark = 0xFF1A1616u;   /* RGBA bytes, little-endian */
        for (int j = 0; j < 24; j++) for (int i = 0; i < 24; i++) {
            uint32_t c = badge;
            const int dx = i - 12;
            if (j >= 11 && j <= 20 && i >= 5 && i <= 18) c = gold;                     /* body */
            const int d2 = dx * dx + (j - 10) * (j - 10);
            if (j <= 10 && d2 >= 16 && d2 <= 36) c = gold;                              /* shackle */
            if (j >= 10 && j <= 11 && (i == 6 || i == 7 || i == 16 || i == 17)) c = gold;
            if (dx * dx + (j - 14) * (j - 14) <= 3) c = dark;                           /* keyhole */
            if (j >= 15 && j <= 18 && dx >= -1 && dx <= 0) c = dark;
            px[j * 24 + i] = c;
        }
    }
    const int off = g_slide_drawn;
    if (off < 0 || 674 + off >= 1280 || g_lock_white >= 1.0f) return nullptr;
    *x = (short)(674 + off); *y = 371; *w = 24; *h = 24; *show_w = 24; *show_h = 24;
    if (g_lock_white <= 0.0f) return px;
    static uint32_t faded[24 * 24];   /* the game fades to white, so each colour moves toward white */
    const uint32_t k = (uint32_t)(g_lock_white * 256.0f);
    for (int i = 0; i < 24 * 24; i++) {
        uint32_t c = 0xFF000000u;
        for (int sft = 0; sft < 24; sft += 8) c |= ((((px[i] >> sft) & 255) * (256 - k) + 255 * k) >> 8) << sft;
        faded[i] = c;
    }
    return faded;
}

/* Turn-based always fights on Namek. The screen's stage is +0x6C (0 Namek, st5). The list starts on Yoshiwara
 * and Namek is 6 to the right (STAGES.md), so it presses through like a player: Cross, Right 6 times, Cross. +0x2E4
 * isn't safe to write: on a later visit it held a pointer (st5). If +0x6C still isn't 0, it is written directly,
 * which gives the right fight even if the picture on this screen is left behind. After that the list won't open. */
void tb_stage(uint16_t* b, uint16_t* lx, uint16_t* ly)
{
    static int phase = 0, rights = 0; static unsigned long long t = 0; static bool was = false;
    if (!g_tb_mode || !on_stagesel()) { was = false; return; }
    const unsigned long long now = GetTickCount64();
    const uint16_t CROSS = 0x4000, RIGHT = 0x0020;
    const uint32_t stage = g_seen_stage.obj + 0x6C;
    if (!was) { was = true; phase = 0; rights = 0; t = now; }
    if (phase >= 5) { if (stage_focus() == 0) *b = (uint16_t)(*b & ~CROSS); return; }
    *b = 0; *lx = 128; *ly = 128;
    const unsigned long long since = now - t;
    switch (phase) {
    case 0: {   /* coming back through Re-select it is already Namek, with the focus on the BGM box */
        if (since <= 1500) break;
        if (vm_read32(stage) == 0) { phase = 5; std::printf("[tb] stage set to Namek\n"); std::fflush(stdout); break; }
        const uint32_t focus = stage_focus();
        if (focus == 0) { phase = 1; t = now; }
        else if (since % 500 < 120) *b = focus == 3 ? 0x0010 : 0x0080;   /* Up, Left */
        break; }
    case 1:
        *b = CROSS;
        if (since > 200) { phase = 2; t = now; }
        break;
    case 2:
        if (since > 500 && since < 620) *b = RIGHT;
        if (since >= 780) { t = now - 280; if (++rights == 6) { phase = 3; t = now; } }
        break;
    case 3:
        if (since > 400) *b = CROSS;
        if (since > 600) { phase = 4; t = now; }
        break;
    default:
        if (since > 500) {
            const uint32_t got = vm_read32(stage);
            if (got != 0) vm_write32(stage, 0);
            phase = 5;
            std::printf("[tb] stage set to Namek%s\n", got != 0 ? " directly" : ""); std::fflush(stdout);
        }
        break;
    }
}

/* Training's list gets a third row above 1 Player. The cursor is counted from the bottom (2 Players 0,
 * 1 Player 1), held in a list widget at menu +0x90: +0 rows, +4 cursor, +8 rows shown, +0xC top, +0x10 bottom.
 * Cross on the new row starts 1 Player with the mode on. */
void tb_list_rows(uint16_t b)
{
    static uint16_t prev = 0;
    const uint16_t pressed = (uint16_t)(b & ~prev);
    prev = b;
    /* TURN_BASED=1 brings the row back; parked until the maintainer continues with the mode (11-02) */
    static const bool tb_on = [] { const char* e = std::getenv("TURN_BASED"); return e && *e == '1'; }();
    if (!g_training_mode || !tb_on) g_tb_mode = false;
    const int want = g_training_mode && tb_on ? 3 : 2;
    g_jstars_fb_rows = want;
    if (!screen_up(g_seen_main, 0x0087D600u)) return;
    const uint32_t w = g_seen_main.obj + 0x90;
    if ((int)vm_read32(w) != want) {
        vm_write32(w, (uint32_t)want); vm_write32(w + 8, (uint32_t)want); vm_write32(w + 0xC, 0); vm_write32(w + 0x10, (uint32_t)(want - 1));
        if ((int)vm_read32(w + 4) >= want) { vm_write32(w + 4, 1); vm_write32(0x008E63E8u, 1); }
    }
    if (!g_training_mode || !tb_on || vm_read32(0x0095654Cu) != 2 || !(pressed & 0x4000)) return;
    const bool row = vm_read32(w + 4) == 2;
    g_tb_mode = row;
    if (row) { vm_write32(w + 4, 1); vm_write32(0x008E63E8u, 1); std::printf("[tb] Turn-based chosen\n"); std::fflush(stdout); }
}

int force_me = -1, force_opp = -1, force_sup = -1;

/* AUTO_FIGHT="<me> <opponent> <support> [stage]" (test only): the game walks itself from
 * boot into a Training fight with those picks, pressing what a player would press.
 * Off in normal play. */

void auto_log(const char* what)
{
    std::printf("[auto] %s at %.1f s\n", what, (GetTickCount64() - g_auto.t0) / 1000.0);
    std::fflush(stdout);
}

void auto_press(uint16_t button, unsigned long long now, unsigned hold_ms, unsigned every_ms)
{
    if (now - g_auto.t >= every_ms) { g_auto.t = now; g_auto.button = button; g_auto.press_until = now + hold_ms; }
}

void auto_fight(uint16_t* b, uint16_t* lx, uint16_t* ly)
{
    static bool read = false;
    static int menu_row = -1;
    if (!read) {
        read = true;
        if (const char* e = std::getenv("AUTO_FIGHT")) {
            int me = -1, opp = -1, sup = -1, stage = -1;
            if (std::sscanf(e, "%d %d %d %d", &me, &opp, &sup, &stage) >= 3) {
                force_me = me; force_opp = opp; force_sup = sup;
                g_auto.on = true; g_auto.stage = stage; g_auto.t0 = GetTickCount64();
                auto_log("started");
            }
        }
        /* AUTO_MENU="<row>" (test only): the same walk, stopping on that main menu row (from 0)
         * and handing the pad back. Off in normal play. */
        if (const char* e = std::getenv("AUTO_MENU")) {
            menu_row = std::atoi(e);
            g_auto.on = true; g_auto.t0 = GetTickCount64();
            auto_log("started");
        }
    }
    if (!g_auto.on) return;
    const uint16_t CROSS = 0x4000, START = 0x0008, DOWN = 0x0040;
    const unsigned long long now = GetTickCount64();
    { static uint32_t last_scene = 0xFFFFFFFFu, last_screen = 0xFFFFFFFFu;
      const uint32_t scene = vm_read32(0x0094DEC4u), screen = vm_read32(0x0095654Cu);
      if (scene != last_scene || screen != last_screen) {
          last_scene = scene; last_screen = screen;
          std::printf("[auto] scene %X screen %X at %.1f s\n", scene, screen, (now - g_auto.t0) / 1000.0);
          std::fflush(stdout);
      } }
    const bool menu = main_list_open();
    switch (g_auto.step) {
    case 0:   /* movie, title, save dialog: Cross until the main menu is up */
        if (menu) { g_auto.step = 1; g_auto.t = now; g_auto.press_until = 0; auto_log("main menu"); break; }
        /* Left and Cross in turn: with no save yet the game asks first, and Left then Cross goes on to the main
         * menu without making one (the maintainer 11-04); on the title and with a save, Left is just another button. */
        if (now - g_auto.t0 > 3000 && now - g_auto.t >= 1000) {
            static bool left = false;
            auto_press(left ? (uint16_t)0x0080 : CROSS, now, 120, 1000);
            left = !left;
        }
        break;
    case 1:   /* down to Free Battle, then in, as Training */
        if (now - g_auto.t < 400) break;
        if (!menu) break;
        if (menu_row >= 0) {
            if ((int)vm_read32(0x00956550u) != menu_row) { auto_press(DOWN, now, 80, 300); break; }
            g_auto.on = false;
            auto_log("menu row reached");
            return;
        }
        if (vm_read32(0x00956550u) != 3) { auto_press(DOWN, now, 80, 300); break; }
        g_training_mode = true;
        for (int k = 0; k < 4; k++) g_dummy[k] = false;
        g_auto.step = 2; g_auto.t = now; g_auto.button = CROSS; g_auto.press_until = now + 120;
        auto_log("Free Battle");
        break;
    case 2:   /* 1 Player, until character select is up */
        if (on_charsel()) { g_auto.step = 3; g_auto.t = now; g_auto.press_until = 0; auto_log("character select"); break; }
        auto_press(CROSS, now, 120, 1500);
        break;
    case 3:   /* the picks are written below on every poll; Start until stage select is up */
        if (on_stagesel()) { g_auto.step = 4; g_auto.t = now; g_auto.press_until = 0; auto_log("stage select"); break; }
        if (now - g_auto.t >= 1500) auto_press(START, now, 120, 1500);
        break;
    case 4:   /* the port sets Unlimited time first, then the stage is written and Start goes in */
        if (!on_stagesel()) { g_auto.step = 5; g_auto.t = now; break; }
        if (now - g_auto.t >= 500 && (stage_time() == 0 || now - g_auto.t > 10000)) {
            /* a chosen stage: Cross opens the stage list, the value goes in, Cross confirms it (m99) */
            static int stage_phase = 0;
            static unsigned long long phase_t = 0;
            if (g_auto.stage >= 0 && stage_phase < 3) {
                if (now - phase_t < 900) break;
                phase_t = now;
                if (stage_phase == 1) {
                    std::printf("[auto] stage value was %u, now %d\n", vm_read32(g_seen_stage.obj + 0x2E4u), g_auto.stage);
                    vm_write32(g_seen_stage.obj + 0x2E4u, (uint32_t)g_auto.stage);
                } else {
                    g_auto.button = CROSS; g_auto.press_until = now + 200;
                }
                stage_phase++;
                break;
            }
            if (g_auto.stage >= 0) {
                /* Start does nothing while the stage list is open and Cross closes it, so taking
                 * turns reaches the fight from either state */
                static bool cross_next = false;
                if (now - phase_t < 900) break;
                phase_t = now;
                g_auto.button = cross_next ? CROSS : START; g_auto.press_until = now + 200;
                cross_next = !cross_next;
                break;
            }
            auto_press(START, now, 120, 1500);
        }
        break;
    case 5:   /* loading and intros: until P1 enters its first state, the moment it can move (af12) */
        if (g_auto.p1_moved) { g_auto.on = false; g_auto.press_until = 0; auto_log("fight"); break; }
        auto_press((uint16_t)(CROSS | START), now, 120, 1000);
        break;
    }
    *b = now < g_auto.press_until ? g_auto.button : 0;
    *lx = 128; *ly = 128;
}

/* Free Battle on the main menu (cursor 3) first asks "Free Battle" or
 * "Training Mode" in the port's two-button box, then lets the game go on into
 * Free Battle's own screens (1 Player / 2 Players, character select) with
 * Training on or off. Back (Esc / Circle) stays on the main menu. */
bool main_menu_open()
{
    return main_list_open();
}

void free_battle_box(uint16_t* b, uint16_t* lx, uint16_t* ly)
{
    const uint16_t CROSS = 0x4000, CIRCLE = 0x2000, LEFT = 0x0080, RIGHT = 0x0020, START = 0x0008;
    static uint16_t prev = 0, prev_dir = 0;
    static int state = 0;    /* 0 idle, 1 box open, 2 release then press Cross, 3 release */
    static int pulse = 0;
    static bool left_menu = false;
    const uint16_t cur = *b, pressed = (uint16_t)(cur & ~prev);
    prev = cur;
    const bool on_menu = main_menu_open();
    if (g_training_mode) {   /* leaving Training: back on the main menu after its screens */
        if (!on_menu) left_menu = true;
        else if (left_menu && state == 0) {
            g_training_mode = false;
            training_unlock_slots();
            std::printf("[training] main menu: Training off\n");
        }
    }
    if (state == 1) {
        if (!cellOskDialog_overlay_active()) {
            const int r = cellOskDialog_confirm_poll();
            if (r == 1 || r == 0) {
                if (g_training_mode && r != 0) training_unlock_slots();
                g_training_mode = r == 0; left_menu = false;
                if (r == 0) for (int k = 0; k < 4; k++) g_dummy[k] = false;
                std::printf("[training] Free Battle box: %s\n", r == 0 ? "Training Mode" : "Free Battle");
                state = 2; pulse = 0;
            } else {
                state = 3;
            }
        } else {
            const uint16_t dir = (uint16_t)(((cur & LEFT) || *lx < 64) ? LEFT : ((cur & RIGHT) || *lx > 192) ? RIGHT : 0);
            if (dir && dir != prev_dir) cellOskDialog_overlay_key(dir == LEFT ? 0x25u : 0x27u);   /* VK_LEFT / VK_RIGHT */
            prev_dir = dir;
            if (pressed & CROSS) cellOskDialog_overlay_key(0x0Du);    /* VK_RETURN */
            if (pressed & CIRCLE) cellOskDialog_overlay_key(0x1Bu);   /* VK_ESCAPE */
        }
        *b = 0; *lx = 128; *ly = 128;
        return;
    }
    if (state == 2) {   /* the game opens Free Battle on a fresh Cross press */
        if (pulse == 0 && cur != 0) { *b = 0; return; }
        pulse++;
        *b = (uint16_t)((pulse >= 2 && pulse <= 5) ? CROSS : 0);
        if (pulse > 6) state = 0;
        return;
    }
    if (state == 3) {
        if (cur == 0) state = 0;
        else { *b = 0; return; }
    }
    if ((pressed & (CROSS | START)) && !on_menu && in_menu_scene() && vm_read32(0x00956550u) == 3 &&
        vm_read32(0x0095654Cu) == 0)
        std::printf("[training] box not offered: step %u CCB0 %08X main %08X seen %llu ms ago\n", vm_read32(0x0094DEC4u),
                    vm_read32(0x0096CCB0u), (uint32_t)g_seen_main.obj, GetTickCount64() - g_seen_main.ms);
    if (!g_auto.on && (pressed & (CROSS | START)) && on_menu && vm_read32(0x00956550u) == 3 &&
        cellOskDialog_choice_open(L"Choose a mode", L"Free Battle", L"Training Mode")) {
        state = 1; prev_dir = 0;
        *b = (uint16_t)(cur & ~(CROSS | START));
    }
}

bool g_exit_filter_on = false;
bool g_free_battle_box = true;

/* PROBE_FADE=1 (test only): raw dumps to probe_fade.bin for finding the stage select fade and the combo number.
 * Records: kind (1 stage select screen, 2 time box, 3 battle HUD part), ms, object, vtable, buttons, then words. */
uint32_t g_pf_hud[128]; int g_pf_nhud = 0;
void pf_note_hud(uint32_t o)
{
    for (int i = 0; i < g_pf_nhud; i++) if (g_pf_hud[i] == o) return;
    if (g_pf_nhud < 128) g_pf_hud[g_pf_nhud++] = o;
}
void pf_rec(FILE* f, uint32_t kind, uint32_t obj, uint32_t b, uint32_t words)
{
    const uint32_t hdr[6] = { kind, (uint32_t)GetTickCount64(), obj, vm_read32(obj), b, words };
    std::fwrite(hdr, 4, 6, f);
    for (uint32_t k = 0; k < words; k++) { const uint32_t v = vm_read32(obj + k * 4); std::fwrite(&v, 4, 1, f); }
}
void fade_probe_tick(uint16_t b)
{
    static FILE* f = nullptr;
    if (!f) f = std::fopen("probe_fade.bin", "wb");
    if (!f) return;
    static unsigned long long stage_seen = 0, hud_last = 0;
    static uint32_t st = 0, box = 0;
    const unsigned long long now = GetTickCount64();
    if (on_stagesel()) { stage_seen = now; st = g_seen_stage.obj; box = g_seen_stage_time.obj; }
    if (st && now - stage_seen < 4000) { pf_rec(f, 1, st, b, 0x100); pf_rec(f, 2, box, b, 0x40); }
    if (training_fight_scene() && now - hud_last >= (std::getenv("PROBE_VG2") ? 100u : 50u)) {
        hud_last = now;
        if (!std::getenv("PROBE_VG2")) for (int i = 0; i < g_pf_nhud; i++) pf_rec(f, 3, g_pf_hud[i], b, 0x100);
        if (std::getenv("PROBE_VG2")) { if (g_vg_arc) pf_rec(f, 5, g_vg_arc, b, 0x100); pf_rec(f, 4, 0x4902A000u, b, 0x400); }
    }
    if (!training_fight_scene() && !(st && now - stage_seen < 4000)) g_pf_nhud = 0;
    std::fflush(f);
}


/* PROBE_VG=1 (test only): the middle arc's value (HUD part 0x0087A1E0, +0x2C) follows the Victory Gauge. Each
 * time it changes, guest memory is searched for the same number (as a 32-bit integer, as a float, and as a
 * float of value / 1000), keeping the addresses that held the new value every time. */
uint32_t g_vg_arc = 0;
void vg_probe_tick()
{
    static std::vector<uint32_t> cand[3];
    static bool first[3] = { true, true, true };
    static uint32_t last = 0;
    if (!g_vg_arc || vm_read32(g_vg_arc) != 0x0087A1E0u || !training_fight_scene()) return;
    /* VG_SET=<n> (test only): once one address besides the arc is left, write n there on every pad read,
     * and dump 0x800 bytes around it once (vg_dump.bin, from address - 0x400). */
    static const int vg_set = std::getenv("VG_SET") ? std::atoi(std::getenv("VG_SET")) : -1;
    if (vg_set >= 0 && cand[0].size() == 2) {
        const uint32_t ea = cand[0][0] == g_vg_arc + 0x2C ? cand[0][1] : cand[0][0];
        static bool dumped = false;
        if (!dumped) {
            dumped = true;
            if (FILE* f = std::fopen("vg_dump.bin", "wb")) { std::fwrite(&ea, 4, 1, f); std::fwrite(vm_base + ea - 0x400, 1, 0x800, f); std::fclose(f); }
            std::printf("[vg] holding 0x%08X at %d\n", ea, vg_set); std::fflush(stdout);
        }
        static int writes = 0;
        static const bool once = std::getenv("VG_ONCE") != nullptr;
        if (!once || writes++ == 0) vm_write32(ea, (uint32_t)vg_set);
        static unsigned long long lastlog = 0;
        const unsigned long long now = GetTickCount64();
        if (now - lastlog >= 200) {
            lastlog = now;
            std::printf("[vg] t %llu value %d words", now, (int32_t)vm_read32(ea));
            for (int k = -8; k < 8; k++) std::printf(" %08X", vm_read32(ea + k * 4));
            std::printf("\n");
        }
    }
    const uint32_t v = vm_read32(g_vg_arc + 0x2C);
    if (v == last || v == 0) return;
    last = v;
    union { float f; uint32_t u; } a, b; a.f = (float)v; b.f = (float)v / 1000.0f;
    const uint32_t want[3] = { v, a.u, b.u };
    auto big = [](const uint8_t* q) { return ((uint32_t)q[0] << 24) | ((uint32_t)q[1] << 16) | ((uint32_t)q[2] << 8) | q[3]; };
    static const uint32_t ranges[][2] = { { 0x00800000u, 0x00A00000u }, { 0x10000000u, 0x10100000u }, { 0x40000000u, 0x4CA30000u } };
    for (int k = 0; k < 3; k++) {
        std::vector<uint32_t> keep;
        if (first[k]) {
            for (const auto& r : ranges)
                for (uint32_t ea = r[0]; ea < r[1]; ea += 4) if (big(vm_base + ea) == want[k]) keep.push_back(ea);
            first[k] = false;
        } else {
            for (uint32_t ea : cand[k]) if (big(vm_base + ea) == want[k]) keep.push_back(ea);
        }
        cand[k].swap(keep);
        std::printf("[vg] arc %u: %s %zu left:", v, k == 0 ? "int" : k == 1 ? "float" : "float/1000", cand[k].size());
        for (size_t i = 0; i < cand[k].size() && i < 12; i++) std::printf(" %08X", cand[k][i]);
        std::printf("\n");
    }
    std::fflush(stdout);
}

void game_pad_filter(uint32_t port, uint16_t* b, uint16_t* lx, uint16_t* ly)
{
    if (port == 1 && g_tb_running && !pause_up()) { *b = g_tb_buttons[1]; *lx = g_tb_lx[1]; *ly = g_tb_ly[1]; }
    if (port == 0) {
        /* MODEL_BODY=<3 digits>: test only, for fighters with empty models (Jotaro 019). The game names model
         * packs "character/model/character_model_%03d_x.pak"; putting the digits in the three strings makes
         * every fighter and support in the fight load that one body. */
        static bool body_done = false;
        if (!body_done) {
            body_done = true;
            const char* e = std::getenv("MODEL_BODY");
            if (e && std::strlen(e) == 3) {
                /* "%03d_x.pak" starts at +0x20 of each string; it becomes "NNN_x.pak". */
                static const struct { uint32_t ea; char kind; } k_body[] = { { 0x007D2B98u, 'm' }, { 0x007D2BC8u, 'v' }, { 0x007D2BF8u, 'i' } };
                for (const auto& t : k_body) {
                    const uint32_t at = t.ea + 0x20u;
                    if (vm_read32(at) != 0x25303364u) continue;   /* "%03d" */
                    vm_write32(at, ((uint32_t)(uint8_t)e[0] << 24) | ((uint32_t)(uint8_t)e[1] << 16) | ((uint32_t)(uint8_t)e[2] << 8) | 0x5Fu);   /* "NNN_" */
                    vm_write32(at + 4, ((uint32_t)(uint8_t)t.kind << 24) | 0x002E7061u);   /* "x.pa" */
                    vm_write32(at + 8, 0x6B000000u);   /* "k" */
                }
                std::printf("[body] model packs forced to %s\n", e);
            }
            /* FIGHTER_PACK=<3 digits>: test only. Every fighter loads this fighter's battle_character pack
             * (motions, attack table), keeping its own model, text and sound. "%03d_x.pak" is at +0x2C. */
            const char* fp = std::getenv("FIGHTER_PACK");
            if (fp && std::strlen(fp) == 3) {
                static const struct { uint32_t ea; char kind; } k_pack[] = { { 0x007D22C8u, 'm' }, { 0x007D2300u, 'v' }, { 0x007D2338u, 'i' } };
                for (const auto& t : k_pack) {
                    const uint32_t at = t.ea + 0x2Cu;
                    if (vm_read32(at) != 0x25303364u) continue;   /* "%03d" */
                    vm_write32(at, ((uint32_t)(uint8_t)fp[0] << 24) | ((uint32_t)(uint8_t)fp[1] << 16) | ((uint32_t)(uint8_t)fp[2] << 8) | 0x5Fu);
                    vm_write32(at + 4, ((uint32_t)(uint8_t)t.kind << 24) | 0x002E7061u);
                    vm_write32(at + 8, 0x6B000000u);
                }
                std::printf("[pack] fighter packs forced to %s\n", fp);
            }
        }
        /* TGS_CHARSEL: test only. The game names its character select packs in four strings;
         * point them at the unused progress/character_select_tgs/ packs. The 72-byte slots have room. */
        static bool tgs_done = false;
        if (!tgs_done) {
            tgs_done = true;
            if (std::getenv("TGS_CHARSEL")) {
                static const struct { uint32_t ea; const char* name; } k_tgs[] = {
                    { 0x007D4F18u, "progress/character_select_tgs/progress_scene_character_select_ALL_m.pak" },
                    { 0x007D4F60u, "progress/character_select_tgs/progress_scene_character_select_ALL_v.pak" },
                    { 0x007D4FA8u, "progress/character_select_tgs/progress_scene_character_select_%s_m.pak" },
                    { 0x007D4FF0u, "progress/character_select_tgs/progress_scene_character_select_%s_v.pak" },
                };
                /* TGS_CHARSEL is a mask of which packs to swap: 1 ALL_m, 2 ALL_v, 4 %s_m, 8 %s_v. */
                const int tgs_mask = std::atoi(std::getenv("TGS_CHARSEL"));
                int tgs_bit = 1;
                for (const auto& t : k_tgs) {
                    const bool tgs_on = (tgs_mask & tgs_bit) != 0;
                    tgs_bit <<= 1;
                    if (!tgs_on) continue;
                    char buf[72] = {0};
                    std::strncpy(buf, t.name, sizeof buf - 1);
                    for (uint32_t o = 0; o < sizeof buf; o += 4) {
                        const uint8_t* q = (const uint8_t*)buf + o;
                        vm_write32(t.ea + o, ((uint32_t)q[0] << 24) | ((uint32_t)q[1] << 16) | ((uint32_t)q[2] << 8) | q[3]);
                    }
                }
                std::printf("[tgs] character select packs pointed at character_select_tgs\n");
            }
        }
        /* SIX_PROBE=1 (test only, the maintainer 11-05: can a match hold 6 fighters?): the battle setup table (0x009540E4,
         * 0x2C a slot) dumped, 12 slots, whenever the character select is left or a fight starts. FORCE_SLOTS=
         * "slot:id[:ctl[:stars]],..." writes those slots while the character select is up (+0x08 = 1 takes part,
         * +0x0C the character, +0x00 the controller, +0x18 the stars). */
        { static const bool sp = std::getenv("SIX_PROBE") != nullptr;
          static bool was_cs = false, was_fight = false;
          const bool cs = on_charsel(), fight = training_fight_scene() || vm_read32(0x0094DEC4u) == 0x14;
          if (sp && ((was_cs && !cs) || (fight && !was_fight))) {
              std::printf("[six] setup at %s:\n", fight && !was_fight ? "fight start" : "leaving character select");
              for (int k = 0; k < 12; k++) {
                  std::printf("[six]  slot %2d:", k);
                  for (uint32_t o = 0; o < 0x2C; o += 4) std::printf(" %08X", vm_read32(setup_field(k, o)));
                  std::printf("\n");
              }
              std::fflush(stdout);
              /* The battle world (func_00030A14: *(TOC-0x7A90)): per team a list of unit indices at +0x34 + team*0x18
               * (-1 = none; func_00031968 walks 4 of them), and unit records of 0x94 bytes at +0x60 + index*0x94. */
              { const uint32_t w = vm_read32(vm_read32(0x008D3888u - 0x7A90u));
                std::printf("[six] battle world %08X\n", w);
                if (w > 0x10000u && w < 0xF0000000u) {
                    for (int t = 0; t < 2; t++) {
                        std::printf("[six]  team %d units:", t);
                        for (int k = 0; k < 6; k++) std::printf(" %d", (int)(int32_t)vm_read32(w + 0x34 + t * 0x18 + k * 4));
                        std::printf("\n");
                    }
                    for (int u = 0; u < 8; u++) {
                        std::printf("[six]  unit %d:", u);
                        for (uint32_t o = 0; o < 0x40; o += 4) std::printf(" %08X", vm_read32(w + 0x60 + u * 0x94 + o));
                        std::printf("\n");
                    }
                }
                std::fflush(stdout); }
              /* Where else are the team's characters? The VS screen has three boxes a team (the maintainer's screenshot): look
               * for P1's character followed by the partner's at a fixed step, in the game's data and its heap, and
               * show the word one more step on (a third member's place?). */
              const uint32_t c0 = vm_read32(setup_field(0, 0x0C)), c1 = vm_read32(setup_field(1, 0x0C));
              const uint32_t c2 = vm_read32(setup_field(2, 0x0C)), c3 = vm_read32(setup_field(3, 0x0C));
              static const uint32_t ranges[][2] = { { 0x00800000u, 0x00A00000u }, { 0x10000000u, 0x10200000u }, { 0x40000000u, 0x4CA30000u } };
              int found = 0;
              /* the battle's own copy: all four fighters in slot order at one step, then what follows (Supports?) */
              if (fight && !was_fight) {
                  int nb = 0;
                  for (const auto& rg : ranges)
                      for (uint32_t ea = rg[0]; ea < rg[1] && nb < 30; ea += 4) {
                          if (vm_read32(ea) != c0) continue;
                          for (uint32_t st = 4; st <= 0x1000 && nb < 30; st += 4)
                              if (vm_read32(ea + st) == c1 && vm_read32(ea + 2 * st) == c2 && vm_read32(ea + 3 * st) == c3) {
                                  nb++;
                                  std::printf("[six] copy? %08X step %u: then %08X %08X | words before: %08X %08X %08X\n", ea, st,
                                              vm_read32(ea + 4 * st), vm_read32(ea + 5 * st), vm_read32(ea - 4), vm_read32(ea - 8), vm_read32(ea - 12));
                              }
                      }
                  std::fflush(stdout);
              }
              for (const auto& rg : ranges)
                  for (uint32_t ea = rg[0]; ea < rg[1] && found < 40; ea += 4) {
                      if (vm_read32(ea) != c0) continue;
                      for (uint32_t st = 4; st <= 0x200; st += 4) {
                          if (vm_read32(ea + st) != c1) continue;
                          /* the other team should sit nearby with the same step */
                          bool other = false; uint32_t ost = 0;
                          for (uint32_t o = 4; o <= 0x800 && !other; o += 4)
                              if (vm_read32(ea + o) == c2 && vm_read32(ea + o + st) == c3) { other = true; ost = o; }
                          if (!other) continue;   /* both teams with the same step: a team list, not chance */
                          std::printf("[six] %08X: %u then %u %+d later, next %08X | team 2 at %+d: %u %u, its next %08X\n", ea, c0, c1,
                                      (int)st, vm_read32(ea + 2 * st), (int)ost, c2, c3, vm_read32(ea + ost + 2 * st));
                          if (++found >= 40) break;
                      }
                  }
              std::fflush(stdout);
          }
          /* SIX_POKE="addr[,addr...]" (test only): from leaving the character select until the fight, write character
           * 34 at those addresses every poll, to see whether one of them is the VS screen's 3rd box. */
          { static bool between = false;
            if (was_cs && !cs) between = true;
            if (fight) between = false;
            g_six_loading = between;
            if (between) if (const char* e = std::getenv("SIX_POKE")) {
                const char* q = e;
                while (*q) { char* end; const uint32_t a = (uint32_t)std::strtoul(q, &end, 16); if (end == q) break;
                             if (a) vm_write32(a, 34u); q = (*end == ',') ? end + 1 : end; if (*end != ',') break; }
            } }
          /* Which TOC entries point at or near the setup table, so the game code that reads it can be found in the
           * recompiled sources by its r2 offset. Once. */
          { static bool toc_done = false;
            if (sp && !toc_done) { toc_done = true;
                const uint32_t toc = 0x008D3888u;
                for (int32_t off = -0x8000; off < 0x8000; off += 4) {
                    const uint32_t v = vm_read32(toc + (uint32_t)off);
                    if (v >= 0x00953F00u && v < 0x00954300u)
                        std::printf("[six] TOC %+d (r2 %s0x%X) = %08X (setup %+d)\n", off, off < 0 ? "-" : "+", off < 0 ? -off : off,
                                    v, (int)(v - 0x009540E4u));
                }
                std::fflush(stdout); } }
          six_watch_tick(cs);
          /* SIX_LOADDUMP=1 (test only): the 6 battle-world unit records in full, every 3 s while the match loads,
           * to compare a normal load with a stuck 3v3 one */
          { static const bool ld = std::getenv("SIX_LOADDUMP") != nullptr; static unsigned long long last = 0;
            const unsigned long long now = GetTickCount64();
            if (ld && g_six_loading && now - last > 3000) { last = now;
                const uint32_t w = vm_read32(vm_read32(0x008D3888u - 0x7A90u));
                if (w > 0x10000u && w < 0xF0000000u) {
                    std::printf("[sixload] t=%llu world %08X head", now, w);
                    for (uint32_t o = 0; o < 0x60; o += 4) std::printf(" %08X", vm_read32(w + o));
                    std::printf("\n");
                    for (int u = 0; u < 6; u++) {
                        std::printf("[sixload]  unit %d:", u);
                        for (uint32_t o = 0; o < 0x94; o += 4) std::printf(" %08X", vm_read32(w + 0x60 + (uint32_t)u * 0x94u + o));
                        std::printf("\n");
                    }
                    std::fflush(stdout);
                } } }
          /* SIX_3V3=1 (test only): while the match loads, every battle-world unit marked as a Support (+0x08 = 3) is
           * marked as a partner (2), so the battle builder (func_000ED344) makes it a fighter: 3 against 3. */
          { static const bool on3 = std::getenv("SIX_3V3") && std::getenv("SIX_3V3")[0] == '1';   /* mode 2: the role watch in six_veh */
            if (on3 && g_six_loading) {
                const uint32_t w = vm_read32(vm_read32(0x008D3888u - 0x7A90u));
                if (w > 0x10000u && w < 0xF0000000u)
                    for (int u = 0; u < 6; u++) {
                        const uint32_t rec = w + 0x60 + (uint32_t)u * 0x94u;
                        if (vm_read32(rec + 8) == 3u) {
                            vm_write32(rec + 8, 2u);
                            static int n = 0;
                            if (n++ < 12) { std::printf("[six] unit %d: Support -> partner (char %u, team %u)\n", u, vm_read32(rec) >> 16, vm_read32(rec + 0xC)); std::fflush(stdout); }
                        }
                    }
            } }
          was_cs = cs; was_fight = fight;
          if (cs) if (const char* e = std::getenv("FORCE_SLOTS")) {
              const char* q = e;
              while (*q) {
                  int s = -1, id = -1, ctl = -1, st = -1, used = 0;
                  if (std::sscanf(q, "%d:%d%n", &s, &id, &used) < 2) break;
                  q += used;
                  if (*q == ':') { int u2 = 0; if (std::sscanf(q, ":%d%n", &ctl, &u2) == 1) q += u2; }
                  if (*q == ':') { int u3 = 0; if (std::sscanf(q, ":%d%n", &st, &u3) == 1) q += u3; }
                  int team = -1;
                  if (*q == ':') { int u4 = 0; if (std::sscanf(q, ":%d%n", &team, &u4) == 1) q += u4; }
                  if (s >= 0 && s < 12) {
                      vm_write32(setup_field(s, 0x08), 1); vm_write32(setup_field(s, 0x0C), (uint32_t)id);   /* +0x08: 1 takes part, 2 not (six1 dump) */
                      if (ctl >= 0) vm_write32(setup_field(s, 0x00), (uint32_t)ctl);
                      if (st >= 0) vm_write32(setup_field(s, 0x18), (uint32_t)st);
                      if (team >= 0) vm_write32(setup_field(s, 0x10), (uint32_t)team);   /* +0x10: the team, 0 or 1 */
                  }
                  if (*q == ',') q++; else break;
              }
          } }
        auto_fight(b, lx, ly);
        if (g_free_battle_box) free_battle_box(b, lx, ly);
        tb_list_rows(*b);
        if (training_active()) {
            training_tick();
            bal_tick(*b, *lx, *ly);
            g_dummy_live = training_dummy_mask();
            static int dbg = -1;
            if (dbg < 0) { const char* e = std::getenv("TRAINING_DEBUG"); dbg = e && e[0] == '1'; }
            static unsigned long long last = 0;
            if (dbg && GetTickCount64() - last > 3000) {
                last = GetTickCount64();
                std::printf("[training] dbg: scene %X fighters %d mask %X dummies %d recs %u/%u %u/%u %u/%u %u/%u\n",
                            vm_read32(0x0094DEC4u), g_train_count, g_dummy_live, g_ndummy,
                            vm_read32(setup_field(0, 0x08)), vm_read32(setup_field(0, 0x18)),
                            vm_read32(setup_field(1, 0x08)), vm_read32(setup_field(1, 0x18)),
                            vm_read32(setup_field(2, 0x08)), vm_read32(setup_field(2, 0x18)),
                            vm_read32(setup_field(3, 0x08)), vm_read32(setup_field(3, 0x18)));
                std::fflush(stdout);
            }
            training_lock_slots();
            if (on_charsel()) training_charsel(b, lx);
            if (g_training_mode) stage_lock(b, lx);
            tb_stage(b, lx, ly);
            if (on_charsel()) {
                /* TRAINING_FORCE: a file holding "<me> <opponent> <support>", read while on this screen, so one run can walk the cast */
                static int force_poll = 0;
                if (force_poll++ % 30 == 0) {
                    if (const char* e = std::getenv("TRAINING_FORCE")) {
                        if (FILE* ff = std::fopen(e, "r")) {
                            if (std::fscanf(ff, "%d %d %d", &force_me, &force_opp, &force_sup) != 3) force_me = -1;
                            std::fclose(ff);
                        }
                    }
                }
            }
            if (force_me >= 0 && on_charsel()) {    /* scripted sessions: who fights, opponent at 0 stars (the game reads the picks when leaving this screen) */
                vm_write32(setup_field(0, 0x08), 0); vm_write32(setup_field(0, 0x0C), (uint32_t)force_me);
                vm_write32(setup_field(2, 0x08), 0); vm_write32(setup_field(2, 0x0C), (uint32_t)force_opp);
                { static const int stars = std::getenv("TRAINING_FORCE_STARS") ? std::atoi(std::getenv("TRAINING_FORCE_STARS")) : 0;   /* test only */
                  vm_write32(setup_field(2, 0x18), (uint32_t)stars); g_dummy[2] = stars == 0; }
                if (force_sup >= 0) { vm_write32(setup_field(4, 0x08), 0); vm_write32(setup_field(4, 0x0C), (uint32_t)force_sup); }
            }
        }
        if (training_active()) {
            pause_tick(b); settings_tick(); tb_tick(b, lx, ly);
            if (!g_tb_running) freecam_tick(b, lx, ly);
        }
        { static const bool pf = std::getenv("PROBE_FADE") != nullptr; if (pf) fade_probe_tick(*b); }
        { static const bool pv = std::getenv("PROBE_VG") != nullptr; if (pv) vg_probe_tick(); }
        if (g_probe_on != 2 && std::getenv("TRAINING_PROBE")) probe_tick();
        probe_flag_tick();
    }
    if (g_exit_filter_on) exit_option_filter(port, b, lx, ly);
}

}  // namespace

extern "C" void jstars_register_game_overrides(void)
{
    ps3_hle_register_ctx(0xDB9819F3u, "cellGameDataCheck",      h_data_check);
    ps3_hle_register_ctx(0x42A2E133u, "cellGameCreateGameData", h_create_game_data);
    ps3_hle_register_ctx(0x70ACEC67u, "cellGameContentPermit",  h_content_permit);
    const char* pt = std::getenv("PC_TEXT");
    if (!pt || pt[0] != '0') {
        g_spu_put_hook_img = pc_text_put;
    }
    const char* xo = std::getenv("EXIT_OPTION");
    if (!xo || xo[0] != '0') { g_exit_filter_on = true; g_tex_patch_hook = exit_label_patch; }
    { const char* tr = std::getenv("TRAINING_TEST");
      if (tr && tr[0] == '1') g_training_test = true; }
    { const char* fb = std::getenv("TRAINING_MODE");   /* TRAINING_MODE=0: no Free Battle box */
      if (fb && fb[0] == '0') g_free_battle_box = false; }
    g_ppu_icall_hook = training_icall;
    g_rsx_copy_rects = training_copy_rects;
    g_rsx_stamp = stage_lock_stamp;
    if (env_on("FPS_COUNTER")) g_rsx_stamp2 = fps_stamp;
    g_cellpad_filter = game_pad_filter;
    g_cellpad_rstick_filter = freecam_rstick;
    const char* ma = std::getenv("MENU_ARROWS_DPAD");
    if (!ma || ma[0] != '0') g_pad_arrows_dpad = arrows_press_dpad;
    g_pad_mouse_look = in_fight;
    g_pad_mouse_menus = in_mouse_menus;
    g_pad_menu_layout = menu_layout;
    { const char* mj = std::getenv("MENU_JUMP");   /* MENU_JUMP=0: step with the d-pad instead */
      if (!mj || mj[0] != '0') g_pad_menu_jump = menu_jump; }
    g_rsx_ui_scale = ui_scale;
    g_rsx_aa_mode = aa_mode;
}
