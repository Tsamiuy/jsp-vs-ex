// feat_menu_pages.cpp -- the two-page main menu (the maintainer 2026-11-19, 2026-11-20).
//
// On by default; MENU_PAGES=0 turns it off (the game's own one-page menu, nothing here runs).
//   page 0 (top):          Battle Modes, J-Customize, J-Point Shop, Gallery, Port Settings, Options
//   page 1 (Battle Modes): J-Adventure, Victory Road, Arcade, Free Battle, Online Battle, Training Mode, Impossible Mode
// The game's own list, highlight and pictures are kept; the row labels, preview art, title and help lines are rewritten
// in guest memory per page (details in the comments below; map: the project's game map notes (menus_flow)).
//
// What this file needs from the rest of the port (a few lines in jsp_game.cpp, see jsp_game.cpp):
//   jsp_main_menu_obj(), jsp_main_list_open(), jsp_in_menu_scene()   the main menu object and screen state
//   calls into here: mp_pad(port, buttons) from the pad filter, psbar_tick() for port 0, mp_tex_upload() from the texture
//   patch hook, mp_on() / mp_rows() for the row count; g_mp_entry (1 Training row, 2 Impossible row) and
//   g_mp_training_start are read where Training and Impossible fights start.
//   Hooks (lift_game_hooks.json): 0x001E3BE0 jsp_mp_hilite_enter, 0x001E0830 jsp_mp_menuenter_enter / _leave,
//   0x0032B2B0 jsp_mpf_calc_enter / _leave (layout calc), 0x0033C5FC jsp_mpf_binder_enter (help-line texts).
//   The Port Settings row opens feat_port_settings.cpp's screen (jsp_ps_open / jsp_ps_active; weak, may be absent).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include "jsp_mp_pack.h"   // the packed pictures (host test: installer/tools/menu_lang_test.sh)
#include "jsp_menu_lang.h"   // the text language and the gate (host test: installer/tools/menu_lang_test.sh)
#ifdef _WIN32
#include <windows.h>
#endif
#include "ppu_recomp.h"
#include "jsp_region.h"

extern "C" uint32_t jsp_main_menu_obj(void);   // jsp_game.cpp: the main menu object while it is up, else 0
extern "C" int jsp_main_list_open(void);       // jsp_game.cpp: the main list is up (no submenu open)
extern "C" int jsp_in_menu_scene(void);        // jsp_game.cpp: the menu scene is running
extern "C" __attribute__((weak)) void jsp_ps_open(void);
extern "C" __attribute__((weak)) int jsp_ps_active(void);
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
#if JSP_US_CODE
void func_00717C2C(ppu_context* ctx);          // the game's new (operator new); US 1.00 only (an EU lift has no function there)
#endif
extern "C" { int g_jsp_ps_game_bar = 0; }      // 1: the game's button bar shows the Port Settings hints

void mp_tex_upload(uint32_t off, uint32_t w, uint32_t h, uint32_t fmt);
void mp_pad(uint32_t port, uint16_t* b);
void psbar_tick();
bool mp_on(); int mp_rows();
extern volatile bool g_mp_training_start; extern volatile int g_mp_entry;

static void ps_open_safe() { if (jsp_ps_open) jsp_ps_open(); }
static int ps_active_safe() { return jsp_ps_active ? jsp_ps_active() : 0; }

static uint32_t mpf_name_hash(const char* n) { uint32_t h = 1; for (; *n; n++) h = (h + (uint8_t)*n) * 0x89u; return h; }
static void mpf_read_name(uint32_t a, char* out, int max) { int q = 0; if (a >= 0x10000u && a < 0x30000000u) for (; q < max - 1; q++) { const char c = (char)vm_base[a + q]; if (!c) break; out[q] = c; } out[q] = 0; }
static uint32_t mpf_game_new(ppu_context* ctx, uint32_t size)
{
    ppu_context c = *ctx; c.lr = 0; c.gpr[3] = size;
#if JSP_US_CODE
    func_00717C2C(&c); while (g_trampoline_fn) { auto f = g_trampoline_fn; g_trampoline_fn = 0; f(&c); }
    return (uint32_t)c.gpr[3];
#else
    (void)c; return 0;   // never reached on EU (mp_on() is false); 0 = no memory, which every caller checks
#endif
}

/* The menus' button bar with the port's settings screen (the maintainer 2026-11-19: the game's own PlayStation icons).
 * Helper map 2026-11-19 (game map notes menus_flow.md "Button bar"): the bar is an object of class 0x008847F0 with 7 text boxes
 * at +0x34 + i*0x80 (string pointer of box i at +0x8C + i*0x80), drawn every frame from those pointers; the text takes
 * <PAD=...> tags with the button written in the game's own code characters (x 00D7, o 25CB, square 25A1, triangle 25B3,
 * L1 FF2C FF11, R1 FF32 FF11). While the settings screen is up box 0 shows our hints and the other boxes nothing; the
 * game's pointers come back when it closes. The strings are made in guest memory by mp_help (it has a guest context). */
static uint32_t g_psbar_str = 0, g_psbar_empty = 0;
   /* 1: the game's bar shows the hints, the screen draws none of its own */
static const uint32_t kPsbarBytes = 512u;   /* guest bytes for the hint string */
static void psbar_fill(uint32_t a)   /* a: kPsbarBytes of guest memory (mp_help gets it, it has a guest context) */
{
    if (g_psbar_str || !a) return;
    static const char16_t t[] = u"<PAD=\uFF30\u32A7\uFF30\u32A8> Change   <PAD=\uFF2C\uFF11\uFF32\uFF11> Tabs   <PAD=\u25A1> Defaults   <PAD=\u00D7> Back";   /* the texts name the buttons the Japanese way: 25CB is "confirm" (drawn as Cross here), 00D7 is "back" (drawn as Circle); FF30 32A7 / 32A8 = d-pad left / right (a test run) */
    const uint32_t n = (uint32_t)(sizeof t / sizeof t[0]);
    if (n * 2u + 8u > kPsbarBytes) return;
    /* the texts the boxes draw are UTF-16 with the low byte first (as mp_help's own help lines; a test run: a
     * string written high byte first drew nothing) */
    for (uint32_t i = 0; i < n; i++) { vm_base[a + 2u * i] = (uint8_t)t[i]; vm_base[a + 2u * i + 1u] = (uint8_t)(t[i] >> 8); }
    for (uint32_t i = 0; i < 4; i++) vm_base[a + 2u * n + i] = 0;
    g_psbar_empty = a + 2u * (n - 1u);   /* the string's own end: an empty string */
    g_psbar_str = a; g_jsp_ps_game_bar = 1;
    std::printf("[psbar] hint text at %08X\n", a); std::fflush(stdout);
}
void psbar_tick()
{
    struct Bar { uint32_t obj; uint32_t was[7]; };
    static std::vector<Bar> bars; static bool on = false;
    const bool want = ps_active_safe() != 0 && g_psbar_str != 0;
    if (want && !on) {
        bars.clear();
        for (uint32_t a = 0x10000000u; a < 0x20000000u - 0x410u && bars.size() < 8; a += 4) {
            if (vm_read32(a) != 0x008847F0u) continue;
            Bar b; b.obj = a; for (int i = 0; i < 7; i++) b.was[i] = vm_read32(a + 0x8Cu + (uint32_t)i * 0x80u);
            bars.push_back(b);
            std::printf("[psbar] bar %08X ready %u page %u/%u box0 text %08X\n", a, vm_read32(a + 0x404u), vm_read32(a + 0x3D4u), vm_read32(a + 0x3D0u), b.was[0]);
            for (int i = 0; i < 7; i++) { const uint32_t tp = b.was[i]; if (tp < 0x10000u || tp >= 0x30000000u) continue;   /* the game's own texts, raw bytes */
                std::printf("[psbar]   box %d mode %u flags %08X text %08X raw:", i, vm_read32(a + 0x34u + (uint32_t)i * 0x80u + 0x5Cu), vm_read32(a + 0x34u + (uint32_t)i * 0x80u + 0x7Cu), tp);
                for (int k = 0; k < 40; k++) std::printf(" %02X", vm_base[tp + (uint32_t)k]); std::printf("\n"); }
        }
        std::printf("[psbar] %u bar(s)\n", (unsigned)bars.size()); std::fflush(stdout);
        on = true;
    }
    if (want) for (const Bar& b : bars) { if (vm_read32(b.obj) != 0x008847F0u) continue;
        for (int i = 0; i < 7; i++) { const uint32_t at = b.obj + 0x8Cu + (uint32_t)i * 0x80u, to = i == 0 ? g_psbar_str : g_psbar_empty; if (vm_read32(at) != to) vm_write32(at, to); } }
    if (!want && on) {
        for (const Bar& b : bars) { if (vm_read32(b.obj) != 0x008847F0u) continue;
            for (int i = 0; i < 7; i++) { const uint32_t at = b.obj + 0x8Cu + (uint32_t)i * 0x80u; if (vm_read32(at) == g_psbar_str || vm_read32(at) == g_psbar_empty) vm_write32(at, b.was[i]); } }
        bars.clear(); on = false;
    }
}

/* ---------------------------------------------------------------------------------------------------------------
 * Two-page menu (the maintainer 2026-11-19): a two-page main menu that keeps the game's own look.
 *   page 0 (top):          Battle Modes, J-Customize, J-Point Shop, Gallery, Options
 *   page 1 (Battle Modes): J-Adventure, Victory Road, Arcade, Free Battle, Online Battle, Training Mode, Impossible
 * The game's list stays its own (game map notes menus_flow.md "Main list internals"): the list widget at menu+0x40
 * {count, cursor, visible, top, bottom} gets 5 or 7 rows, so the game's Up / Down wrap inside the page. Row N of the
 * screen keeps its place, its bullet and its highlight timeline; what it SHOWS is changed by rewriting, in guest memory,
 * the pictures the renderer reads for that row (it re-reads a texture whose bytes changed): the row labels (300x43
 * DXT5, a white and a black one per row, found by their bytes when the game uploads them) and the preview art
 * (1059x354 DXT5). The help line is the text box on pane _f_l_cl00; its text pointer (+0x58) is pointed at the row's
 * string. A row whose action is another row's (J-Customize .. Options on page 0, Training / Impossible on page 1)
 * moves the list cursor to that real row on Cross; the highlight part (func_001E3BE0, +0x40 wanted row) is held on
 * the screen row meanwhile. The new labels are drawn by docs/archive/jstars-notes/tools/mm_label.ps1 from a Windows font.
 * ------------------------------------------------------------------------------------------------------------- */
/* The pictures of a language, one blob of 269440 bytes of DXT5 (8 row labels 300x43, light then dark, for Battle Modes / Training / Impossible /
 * Port Settings; the page-1 title 512x64; the help-bar watermark 1024x128), packed as base 85 text of a 16-byte block RLE (jsp_mp_pack.h) in
 * mp_pic_<lang>.inc, made by installer/tools/mp_label_langs.py from the table in jsp_menu_lang.h (our own art from Fira Sans Heavy Italic, SIL OFL 1.1;
 * nothing from the game). A language whose file is absent has no set and jml::labels_ready() is false for it, so the feature stays off there (the
 * game's own menu). Unpacked once, for the shown language only (mp_cur_set). */
static const size_t kMpBlob = 8u * 13200u + 32768u + 131072u;
#if __has_include("mp_pic_en.inc")
#define MP_HAVE_EN 1
static const char k_mp_pic_en[] =
#include "mp_pic_en.inc"
;
#endif
#if __has_include("mp_pic_es.inc")
#define MP_HAVE_ES 1
static const char k_mp_pic_es[] =
#include "mp_pic_es.inc"
;
#endif
#if __has_include("mp_pic_fr.inc")
#define MP_HAVE_FR 1
static const char k_mp_pic_fr[] =
#include "mp_pic_fr.inc"
;
#endif
#if __has_include("mp_pic_de.inc")
#define MP_HAVE_DE 1
static const char k_mp_pic_de[] =
#include "mp_pic_de.inc"
;
#endif
#if __has_include("mp_pic_it.inc")
#define MP_HAVE_IT 1
static const char k_mp_pic_it[] =
#include "mp_pic_it.inc"
;
#endif
volatile bool g_mp_training_start = false;
volatile int g_mp_entry = 0;            /* 1 Training row, 2 Impossible row: read when Free Battle's screens open */
static int g_mp_page = 0;               /* 0 top page, 1 Battle Modes */
static int g_mp_pin = -1;               /* the screen row the highlight is held on while the cursor sits on a real row */
static int g_mp_back = -1;              /* the screen row to come back to after that row's screen */
static bool g_mp_away = false;          /* the main list was left since g_mp_back was set */
static int g_mp_applied = -1;           /* the page whose pictures are in the textures */
static uint32_t g_mp_hilite = 0, g_mp_hilite_vt = 0;   /* the row highlight part and its class word (from its per-frame hook) */
static bool mp_hilite_ok() { const uint32_t hp = g_mp_hilite; return hp >= 0x10000u && hp < 0x30000000u && g_mp_hilite_vt && vm_read32(hp) == g_mp_hilite_vt; }
/* the preview pictures fade out when the settings screen is asked for and in again when it closes (the maintainer 2026-11-19):
 * 0 none, 1 fading out / gone, -1 fading in; ps_fade() = how far gone, 0..1 */
static int g_ps_fade_dir = 0; static unsigned long long g_ps_fade_t = 0;
static float ps_fade()
{
    if (!g_ps_fade_dir) return 0.0f;
    const float p = (float)(GetTickCount64() - g_ps_fade_t) / 300.0f;
    if (g_ps_fade_dir > 0) return p > 1.0f ? 1.0f : p;
    if (p >= 1.0f) { g_ps_fade_dir = 0; return 0.0f; }
    return 1.0f - p;
}
static int g_ps_wait = 0; static unsigned long long g_ps_wait_t = 0;   /* settings screen: 1 list sliding out before it opens, 2 it is (or was just) open */
static int g_mp_trans = 0, g_mp_trans_to = 0, g_mp_trans_row = 0;   /* page change: 1 list sliding out, 2 sliding in */
static unsigned long long g_mp_trans_t = 0;
/* on by default since 2026-11-21 (the maintainer: cleaner than the game's chain of pop-up windows); MENU_PAGES=0 gives the
 * game's own main menu */
/* The text language (jsp_menu_lang.h): PS3_LANG, else the Windows UI language, else English. The feature runs only for a language with all of its
 * text and row-label pictures AND the disc's texture hashes for that language (jml::hashes; g_mp_off is set by the backstop in mp_tex_upload when
 * no label of the language matches); else the game's own menu, with one log line. */
static std::atomic<bool> g_mp_off{false}; static bool g_mp_us_seen = false;
static int g_mp_disc = -1;   /* jml::Disc: 1 a US disc, 2 the EU disc, 0 another known one, -1 not known: the title id, set by jsp_mp_init before the menu exists. Only the US 1.00
                                * build has the menu-page hooks, so the pages never show on the EU disc (2 is kept for a future EU hook set) */
extern "C" void jsp_mp_init(const char* title_id) { g_mp_disc = jml::disc_kind(title_id); }   /* main.cpp, next to jsp_mods_init */
static const std::string& mp_lang()
{
    static const std::string l = [] { int w = 0;
#ifdef _WIN32
        w = (int)(GetUserDefaultUILanguage() & 0x3FF);
        return jml::shown(jml::pick(std::getenv("PS3_LANG"), w), g_mp_disc);
#else
        return jml::shown(jml::pick(std::getenv("PS3_LANG"), w, std::getenv("LC_ALL"), std::getenv("LC_MESSAGES"), std::getenv("LANG")), g_mp_disc);
#endif
        }();
    return l;
}
extern "C" const char* jsp_mp_lang(void) { return mp_lang().c_str(); }   /* the language the disc shows, for the port's pop-up texts (jsp_game.cpp) */
struct MpSet { const uint8_t (*lab)[2][13200]; const uint8_t* title; const uint8_t* mark; };
static MpSet mp_set(const std::string& l)   /* the picture set of a language; {} when its file is not in the build or does not unpack */
{
    static std::map<std::string, std::vector<uint8_t>> cache;   /* unpacked blobs, kept for the run */
    auto it = cache.find(l);
    if (it == cache.end()) {
        const char* text = nullptr;
#ifdef MP_HAVE_EN
        if (l == "en") text = k_mp_pic_en;
#endif
#ifdef MP_HAVE_ES
        if (l == "es") text = k_mp_pic_es;
#endif
#ifdef MP_HAVE_FR
        if (l == "fr") text = k_mp_pic_fr;
#endif
#ifdef MP_HAVE_DE
        if (l == "de") text = k_mp_pic_de;
#endif
#ifdef MP_HAVE_IT
        if (l == "it") text = k_mp_pic_it;
#endif
        it = cache.emplace(l, text ? jmp::unpack(text, kMpBlob) : std::vector<uint8_t>()).first;
        if (text && it->second.empty()) { std::printf("[mpages] the picture set of %s does not unpack\n", l.c_str()); std::fflush(stdout); }
    }
    const std::vector<uint8_t>& v = it->second;
    if (v.size() != kMpBlob) return { nullptr, nullptr, nullptr };
    return { (const uint8_t (*)[2][13200])v.data(), v.data() + 8u * 13200u, v.data() + 8u * 13200u + 32768u };
}
static void mp_register_sets()   /* tells jsp_menu_lang.h which languages have all their pictures */
{
    for (int i = 0; i < 5; i++) if (mp_set(jml::k_langs[i]).lab) jml::label_sets() |= 1u << i;
}
static const MpSet& mp_cur_set() { static const MpSet m = mp_set(mp_lang()); return m; }
/* US only for now (jsp_region.h): the pages rewrite guest memory at US 1.00 addresses and their lift hooks are in the US lift only, so an EU
 * build keeps the game's own one-page menu (mp_on() false: the pad filter, the texture patch and the row count leave the menu alone). */
bool mp_on()
{
    static const bool on = [] { const char* e = std::getenv("MENU_PAGES"); if (e && e[0] == '0') return false;
        if (!jsp_game_is_us()) return false;   /* EU build: jsp_game.cpp logs it with the other US-only features */
        mp_register_sets(); std::string why = jml::gate_disc(g_mp_disc); if (why.empty()) why = jml::gate(mp_lang(), g_mp_disc, true);
        if (!why.empty()) { std::printf("[mpages] off: %s\n", why.c_str()); std::fflush(stdout); return false; }
        return true; }();
    return on && !g_mp_off.load();
}
int mp_page_now() { return g_mp_page; }
bool mp_busy() { return g_mp_trans != 0 || g_ps_wait != 0; }
/* The top page's "Port Settings" row is there only when the screen it opens is in the build (feat_port_settings.cpp,
 * the maintainer 2026-11-21: the GitHub package leaves the screen out for now). PORTSET_ROW=0 drops it anyway (tests). */
static bool mp_ps_row() { static const bool on = [] { const char* e = std::getenv("PORTSET_ROW"); return jsp_ps_open != nullptr && !(e && e[0] == '0'); }(); return on; }
int mp_rows() { return g_mp_page == 0 ? (mp_ps_row() ? 6 : 5) : 7; }   /* page 0: Port Settings above Options since 2026-11-19 */
/* label id: 0 J-Adventure, 1 Victory Road, 2 Free Battle, 3 Online, 4 J-Customize, 5 J-Point Shop, 6 Gallery, 7 Options,
 * 8 Arcade (added last, so its pictures come after the others); the hashes of each disc and language are jml::hashes() (jsp_menu_lang.h) */
static const int k_mp_row_lab[9] = { 0, 1, 8, 2, 3, 4, 5, 6, 7 };   /* screen row -> the label id whose pictures it draws */
/* preview art by screen row (a test run): the image each row shows, FNV of the first w*h bytes; rows 5 and
 * 6 have two textures each (the second is the US one, main_img_004/005_HOKUBEI; row 5's shows the J-Stars logo) */
static const uint32_t k_mp_art_fnv[9][2] = { { 0x783B68CDu, 0 }, { 0x20EA680Bu, 0 }, { 0xDD94478Fu, 0 }, { 0xE2FCE328u, 0 }, { 0x2C8D8DCCu, 0 },
    { 0xF292D3B7u, 0x892AD2CCu }, { 0xA4FEA33Fu, 0 }, { 0x747A16B5u, 0 }, { 0xE6603CB4u, 0 } };
static const uint32_t kMpLabBytes = 13200u, kMpArtBytes = 377360u;   /* 75 x 11 and 265 x 89 blocks of 16 bytes */
/* One row texture. Its bytes as the game loaded them (orig, FNV forig over the first w*h bytes) and the FNV of what the
 * port last wrote there (fwrote). A texture is only written while it still holds one of the two: when the game frees
 * the menu's textures and the memory holds something else, the slot is dropped (a test run: a
 * picture written by the port came back through the upload hook and was taken for another row's texture). */
struct MpTex { uint32_t addr = 0, bytes = 0, prefix = 0, forig = 0, fwrote = 0; std::vector<uint8_t> orig; };
static MpTex g_mp_lab[9][2];
static MpTex g_mp_art[9][2];
static MpTex g_mp_title, g_mp_mark;   /* "Main Menu": the orange title (512x64) and the grey watermark of the help bar (1024x128) */
static std::mutex g_mp_tex_mx;
/* what screen row r shows on a page: a label id (0..8), 100+k for new label k (0 Battle Modes, 1 Training, 2 Impossible),
 * -1 nothing; the art: a screen row whose art it shows, or -1 for its own */
static int mp_lab_of(int page, int r) { static const int p0[9] = { 100, 4, 5, 6, 103, 7, -1, -1, -1 }, q0[9] = { 100, 4, 5, 6, 7, -1, -1, -1, -1 }, p1[9] = { 0, 1, 8, 2, 3, 101, 102, -1, -1 };
    return page == 0 ? (mp_ps_row() ? p0[r] : q0[r]) : p1[r]; }
/* page 0 row 4 (Port Settings) shows the picture of its own place (image 4, the one Online Battle has on the other page): while the
 * list slides out the game asks for the cursor row's picture every frame, and holding another one there made the two
 * pictures alternate (a test run) */
static int mp_art_of(int page, int r) { static const int p0[9] = { 3, 5, 6, 7, -1, 8, -1, -1, -1 }, q0[9] = { 3, 5, 6, 7, 8, -1, -1, -1, -1 }, p1[9] = { -1, -1, -1, -1, -1, 3, 2, -1, -1 };
    return page == 0 ? (mp_ps_row() ? p0[r] : q0[r]) : p1[r]; }
static int mp_real_row(int page, int r) { static const int p0[6] = { -1, 5, 6, 7, -1, 8 }, q0[5] = { -1, 5, 6, 7, 8 };
    if (page == 0) return mp_ps_row() ? (r >= 0 && r <= 5 ? p0[r] : -1) : (r >= 0 && r <= 4 ? q0[r] : -1);
    return r == 5 || r == 6 ? 3 : r; }   /* page 0 row 4: the port's settings screen */
static uint32_t mp_fnv(const uint8_t* p, uint32_t n) { uint32_t h = 0x811C9DC5u; for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x01000193u; return h; }
static void mp_put(MpTex& t, const uint8_t* src)
{
    if (!t.addr || !src) return;
    const uint32_t f = mp_fnv(vm_base + t.addr, t.prefix);
    if (f != t.forig && f != t.fwrote) { std::printf("[mpages] texture at %08X holds something else now: let go\n", t.addr); std::fflush(stdout); t.addr = 0; return; }
    if (!std::memcmp(vm_base + t.addr, src, t.bytes)) { t.fwrote = f; return; }   /* already so: no write, no re-upload */
    std::memcpy(vm_base + t.addr, src, t.bytes); t.fwrote = mp_fnv(src, t.prefix);
    { static const bool lg = std::getenv("MP_LOG") != nullptr; static int n = 0; if (lg && n < 400) { n++; std::printf("[mpput] %08X <- fnv %08X\n", t.addr, t.fwrote); std::fflush(stdout); } }
}
/* the pictures of one page into the row textures (call with the lock held) */
static void mp_write_page(int page)
{
    static const std::vector<uint8_t> blank(kMpLabBytes, 0);   /* DXT5 blocks of alpha 0 */
    for (int r = 0; r < 9; r++) {
        const int want = mp_lab_of(page, r), slot = k_mp_row_lab[r];
        for (int st = 0; st < 2; st++) {
            const uint8_t* src = want < 0 ? blank.data() : want >= 100 ? mp_cur_set().lab[want - 100][st] : (g_mp_lab[want][st].orig.size() == kMpLabBytes ? g_mp_lab[want][st].orig.data() : nullptr);
            mp_put(g_mp_lab[slot][st], src); }
    }
    mp_put(g_mp_title, page == 1 ? mp_cur_set().title : (g_mp_title.orig.empty() ? nullptr : g_mp_title.orig.data()));
    mp_put(g_mp_mark, page == 1 ? mp_cur_set().mark : (g_mp_mark.orig.empty() ? nullptr : g_mp_mark.orig.data()));
    g_mp_applied = page;
}
/* g_tex_patch_hook: register the row textures by their bytes when the game uploads them as loaded (each main menu
 * visit may load them again), and give them the page's pictures. An upload of a texture the port wrote is skipped. */
void mp_tex_upload(uint32_t off, uint32_t w, uint32_t h, uint32_t fmt)
{
    if (!mp_on() || !vm_base || (fmt & 0x9F) != 0x88) return;
    const bool lab = w == 300 && h == 43, art = false && w == 1059 && h == 354;   /* the preview art is steered by image index instead (mp_preview_image) */
    const bool ttl = w == 512 && h == 64, mrk = w == 1024 && h == 128;
    if (ttl || mrk) {   /* the "Main Menu" title / watermark (FNV of the whole picture, a test run) */
        MpTex& t = ttl ? g_mp_title : g_mp_mark; const uint32_t f2 = mp_fnv(vm_base + off, w * h);
        std::lock_guard<std::mutex> lk2(g_mp_tex_mx);
        if (!(ttl ? jml::is_title(g_mp_disc, mp_lang(), f2) : jml::is_mark(g_mp_disc, mp_lang(), f2))) return;
        t.addr = off; t.bytes = w * h; t.prefix = w * h; t.forig = f2; t.fwrote = f2; t.orig.assign(vm_base + off, vm_base + off + t.bytes);
        { static int n = 0; if (n++ < 8) { std::printf("[mpages] %s at %08X\n", ttl ? "title" : "watermark", off); std::fflush(stdout); } }
        mp_write_page(g_mp_page); return; }
    if (!lab && !art) return;
    const uint32_t f = mp_fnv(vm_base + off, w * h);
    std::lock_guard<std::mutex> lk(g_mp_tex_mx);
    { static const bool lg = std::getenv("MP_LOG") != nullptr; static int n = 0; static std::set<uint64_t> seen;   /* MP_LOG=1: each upload of a row picture once per picture, place and cursor row */
      const uint64_t key = ((uint64_t)off << 32) ^ f ^ ((uint64_t)vm_read32(0x00956550u) << 60);
      if (lg && n < 600 && seen.insert(key).second) { n++; std::printf("[mpup] %s %08X fnv %08X page %d cursor %u\n", lab ? "label" : "art", off, f, g_mp_page, vm_read32(0x00956550u)); std::fflush(stdout); } }
    MpTex* hit = nullptr; const char* what = ""; int id = -1, sub = -1;
    if (lab) { int k = 0, st = 0; if (jml::find_label(g_mp_disc, mp_lang(), f, &k, &st)) { hit = &g_mp_lab[k][st]; what = st ? "label black" : "label white"; id = k; sub = st; } }
    if (art) for (int r = 0; r < 9 && !hit; r++) for (int v = 0; v < 2; v++) if (k_mp_art_fnv[r][v] && k_mp_art_fnv[r][v] == f) { hit = &g_mp_art[r][v]; what = "art"; id = r; sub = v; break; }
    if (!hit) {
        /* Not a label of this disc and language: the hashes are per disc and language (jml::hashes), so this is a disc or pack the tables do not
         * know (a title id that was not readable and a language the disc does not show, a patched pack). 18 label-sized textures (9 rows, a
         * normal and a highlighted one each) of one main-menu visit and not one match: the feature turns itself off (the game's own one-page
         * menu, restored by mp_off_restore). ASSUMPTION: the count 18 (one menu has 9 rows); the other discs' tables are not yet confirmed by a run. */
        static int seen = 0;
        const uint32_t mo = jsp_main_menu_obj();   /* only uploads of a main-menu visit count, and all 18 */
        if (lab && !g_mp_us_seen && mo >= 0x10000u && mo < 0x30000000u && vm_read32(mo) == 0x0087D600u && ++seen >= 18 && !g_mp_off.exchange(true))
            { std::printf("[mpages] off: %s\n", jml::gate(mp_lang(), g_mp_disc, false).c_str()); std::fflush(stdout); }
        return; }
    /* the port's own picture coming back (the bytes of another row, written here): not that row's texture */
    for (int k = 0; k < 9; k++) for (int q = 0; q < 2; q++) { const MpTex& t = lab ? g_mp_lab[k][q] : g_mp_art[k][q]; if (t.addr == off && &t != hit) return; }
    if (hit->addr == off && hit->fwrote == f) return;   /* what the port wrote, or the page wants it as loaded */
    if (lab) g_mp_us_seen = true;   /* a US label was seen: the backstop never fires after this */
    hit->addr = off; hit->bytes = lab ? kMpLabBytes : kMpArtBytes; hit->prefix = w * h; hit->forig = f; hit->fwrote = f;
    hit->orig.assign(vm_base + off, vm_base + off + hit->bytes);
    { static int n = 0; if (n++ < 60) { std::printf("[mpages] %s %d/%d at %08X\n", what, id, sub, off); std::fflush(stdout); } }
    mp_write_page(g_mp_page);
}
static void mp_set_page(int page)
{
    g_mp_page = page;
    std::lock_guard<std::mutex> lk(g_mp_tex_mx);
    mp_write_page(page);
    std::printf("[mpages] page %d\n", page); std::fflush(stdout);
}
/* the list widget of the main menu (menu+0x40): the page's row count; the cursor back on the screen row */
static void mp_list_fix(uint32_t menu)
{
    if (!mp_on() || menu < 0x10000u || menu >= 0x30000000u || vm_read32(menu) != 0x0087D600u) return;
    const uint32_t L = menu + 0x40u, n = (uint32_t)mp_rows();
    uint32_t cur = vm_read32(L + 4);
    /* ours already; a cursor on another row is the way out to that row's screen until the list has been left. Back from
     * that screen the list may keep its count and the real row (a test run: back from Options the cursor was 8
     * of 5 rows, and the next Down was lost), so the screen row is put back then. */
    const bool stray = vm_read32(L) == n && cur >= n && g_mp_back < 0 && g_mp_pin < 0;   /* the maintainer 2026-11-19: after Exit Game -> No the cursor sat on a row the page does not have */
    if (vm_read32(L) == n && !(g_mp_back >= 0 && g_mp_away) && !stray) return;
    { static int said = 0; if (said++ < 12) { std::printf("[mpages] list before: count %u cursor %u visible %u top %u bottom %u (page %d, back %d)\n", vm_read32(L), cur, vm_read32(L + 8), vm_read32(L + 12), vm_read32(L + 16), g_mp_page, g_mp_back); std::fflush(stdout); } }
    if (g_mp_back >= 0 && g_mp_away) { cur = (uint32_t)g_mp_back; g_mp_back = -1; g_mp_away = false; }
    else if (g_mp_back >= 0) { /* a page change (Circle): the row asked for */ cur = (uint32_t)g_mp_back; g_mp_back = -1; }
    else if (cur >= n) { int back = 0; for (int r = 0; r < (int)n; r++) if (mp_real_row(g_mp_page, r) == (int)cur) back = r; cur = (uint32_t)back; }
    vm_write32(L, n); vm_write32(L + 4, cur); vm_write32(L + 8, n); vm_write32(L + 12, 0); vm_write32(L + 16, n - 1);
    vm_write32(0x00956550u, cur);
}
/* The late switch-off (the texture backstop in mp_tex_upload) comes after the menu was set up with 5-7 rows, hidden panes and our help line.
 * On the next guest-thread call (the highlight hook, the help binder) the list goes back to the game's 9 rows with the cursor on the real
 * row, and the port's page state is cleared; the panes (mp_layout_frame) and the help pointer (mp_help) are put back where they are known. */
static bool g_mp_restored = false;
static void mp_off_restore(uint32_t menu)
{
    if (!g_mp_off.load() || g_mp_restored || menu < 0x10000u || menu >= 0x30000000u || vm_read32(menu) != 0x0087D600u) return;
    const uint32_t L = menu + 0x40u; uint32_t w[5] = { vm_read32(L), vm_read32(L + 4), vm_read32(L + 8), vm_read32(L + 12), vm_read32(L + 16) };
    const int n = g_mp_page == 0 ? (mp_ps_row() ? 6 : 5) : 7;
    jml::restore_list(w, w[1] < (uint32_t)n ? mp_real_row(g_mp_page, (int)w[1]) : (int)w[1]);
    for (int i = 0; i < 5; i++) vm_write32(L + 4u * (uint32_t)i, w[i]);
    vm_write32(0x00956550u, w[1]);
    g_mp_page = 0; g_mp_pin = -1; g_mp_back = -1; g_mp_away = false; g_mp_entry = 0; g_mp_trans = 0; g_ps_wait = 0;
    g_mp_restored = true;
    std::printf("[mpages] switched off after the menu was up: the game's 9-row list is back (cursor %u)\n", w[1]); std::fflush(stdout);
}
/* a page change: with the list's own slide (state 4 = fl_out) when the highlight part is known, else at once */
static void mp_change_page(int page, int row, uint32_t menu)
{
    static const bool slide = [] { const char* e = std::getenv("MENU_PAGES_SLIDE"); return !(e && e[0] == '0'); }();
    const uint32_t hp = g_mp_hilite;
    if (slide && mp_hilite_ok() && vm_read32(hp + 0x38) == 3u) {
        g_mp_trans = 1; g_mp_trans_to = page; g_mp_trans_row = row; g_mp_trans_t = GetTickCount64(); vm_write32(hp + 0x38, 4u);
        g_ps_fade_dir = 1; g_ps_fade_t = GetTickCount64();   /* the preview pictures fade out for the change and in after it: while the list slides the game and the page each ask for a picture, and the two alternated (the maintainer 2026-11-19) */
        return; }
    mp_set_page(page); g_mp_back = row; mp_list_fix(menu);
}
/* Cross / Start and Circle on the main list (game_pad_filter, ports 0 and 1) */
static void mp_preview_log(uint32_t menu); static void mp_preview_image(uint32_t menu); static uint32_t mp_preview_part(uint32_t menu);
void mp_pad(uint32_t port, uint16_t* b)
{
    if (!mp_on() && port == 0 && jsp_main_list_open() && std::getenv("MP_LOG")) { const uint32_t m = jsp_main_menu_obj(); if (m >= 0x10000u && vm_read32(m) == 0x0087D600u) mp_preview_log(m); }
    if (!mp_on() || port > 1) return;
    static bool boot = false; if (!boot) { boot = true; const char* am = std::getenv("AUTO_MENU"); if (am && am[0] >= '0' && am[0] <= '9' && !std::getenv("MENU_PAGES_TOP")) g_mp_page = 1; }   /* AUTO_MENU=<row> walks the battle rows (MENU_PAGES_TOP=1: stay on the top page); AUTO_FIGHT presses Battle Modes itself */
    static uint16_t prev[2] = { 0, 0 };
    const uint16_t CROSS = 0x4000, CIRCLE = 0x2000, START = 0x0008;
    const uint16_t cur = *b, pressed = (uint16_t)(cur & ~prev[port]); prev[port] = cur;
    /* a press the page change used is eaten until it is let go, whatever screen the game thinks is up (run
     * 2026-11-19_0043: the Cross on Battle Modes also opened J-Adventure; jsp_main_list_open() is not true on every poll) */
    static bool eat[2] = { false, false };
    if (eat[port]) { if (cur & (CROSS | START | CIRCLE)) { *b = (uint16_t)(*b & ~(CROSS | START | CIRCLE)); return; } eat[port] = false; }
    if (g_mp_applied != g_mp_page) { std::lock_guard<std::mutex> lk(g_mp_tex_mx); mp_write_page(g_mp_page); }
    if (g_ps_wait == 1) {   /* the list is sliding out for the settings screen */
        *b = 0;
        const bool ok = mp_hilite_ok();
        const unsigned long long el = GetTickCount64() - g_ps_wait_t;
        if (el >= 300 && (!ok || vm_read32(g_mp_hilite + 0x38) == 0u || el > 1500)) { ps_open_safe(); g_ps_wait = 2; }   /* not before the pictures are gone */
        return; }
    if (g_ps_wait == 2 && !ps_active_safe()) {   /* the settings screen closed: the list slides in again (fl_in, state 1) */
        g_ps_wait = 0; g_ps_fade_dir = -1; g_ps_fade_t = GetTickCount64();
        if (mp_hilite_ok() && vm_read32(g_mp_hilite + 0x38) != 3u) { vm_write32(g_mp_hilite + 0x34, 0u); vm_write32(g_mp_hilite + 0x38, 1u); }
    }
    if (g_mp_trans) {   /* the page change runs: the list slides out, the pictures change, it slides in (MENU_PAGES_SLIDE=0: at once) */
        *b = 0;
        const uint32_t hp = g_mp_hilite, menu2 = jsp_main_menu_obj(); const unsigned long long now3 = GetTickCount64();
        const bool ok = mp_hilite_ok() && menu2 >= 0x10000u && menu2 < 0x30000000u && vm_read32(menu2) == 0x0087D600u;
        const uint32_t st = ok ? vm_read32(hp + 0x38) : 0u;
        if (g_mp_trans == 1 && (!ok || st == 0u || now3 - g_mp_trans_t > 1500)) {
            mp_set_page(g_mp_trans_to); g_mp_back = g_mp_trans_row; if (ok) { mp_list_fix(menu2); vm_write32(hp + 0x34, 0u); vm_write32(hp + 0x38, 1u); }
            g_mp_trans = 2; g_mp_trans_t = now3; }
        else if (g_mp_trans == 2 && (!ok || st == 3u || now3 - g_mp_trans_t > 1500)) { if (ok && st != 3u && st != 2u) vm_write32(hp + 0x38, 1u); g_mp_trans = 0;
            if (menu2 >= 0x10000u && menu2 < 0x30000000u && vm_read32(menu2) == 0x0087D600u) mp_preview_image(menu2);
            g_ps_fade_dir = -1; g_ps_fade_t = now3; }
        return; }
    { static unsigned long long closed_since = 0; const unsigned long long now2 = GetTickCount64();
      if (g_mp_back >= 0 && !g_mp_away) {   /* on the way out through another row: really gone only when the game says so */
          if (vm_read32(0x0095654Cu) != 0 || !jsp_in_menu_scene()) g_mp_away = true;
          else if (!jsp_main_list_open()) { if (!closed_since) closed_since = now2; else if (now2 - closed_since > 700) g_mp_away = true; }
          else closed_since = 0; }
      else closed_since = 0; }
    if (!jsp_main_list_open()) return;
    const uint32_t menu = jsp_main_menu_obj();
    if (menu < 0x10000u || menu >= 0x30000000u || vm_read32(menu) != 0x0087D600u) return;
    mp_list_fix(menu);
    mp_preview_image(menu);
    mp_preview_log(menu);
    /* Up / Down on the list while the highlight is held on a Training / Impossible row: the hold ends here, and the move
     * starts from the row the player sees (a tester's video: the highlight stayed on Training Mode for about
     * 7 s of presses after coming back to the Battle Modes page). Not while the list slides (highlight state not 3). */
    if (g_mp_pin >= 0 && (pressed & (0x0010 | 0x0040)) && !(mp_hilite_ok() && vm_read32(g_mp_hilite + 0x38) != 3u)) {
        std::printf("[mpages] Up / Down on the list: the hold of row %d ends (the game's cursor was %u)\n", g_mp_pin, vm_read32(menu + 0x44)); std::fflush(stdout);
        vm_write32(menu + 0x44, (uint32_t)g_mp_pin); vm_write32(0x00956550u, (uint32_t)g_mp_pin);
        g_mp_pin = -1; g_mp_back = -1; g_mp_away = false;
    }
    const int row = (int)vm_read32(menu + 0x44);
    if (pressed & (CROSS | START)) {
        if (g_mp_page == 0 && row == 0) {   /* Battle Modes: the second page, same screen */
            *b = (uint16_t)(*b & ~(CROSS | START)); eat[port] = true; mp_change_page(1, 0, menu); return; }
        if (g_mp_page == 0 && row == 4 && mp_ps_row()) {   /* Port Settings (the maintainer 2026-11-19): the port's settings screen over the menu (feat_port_settings.cpp) */
            *b = (uint16_t)(*b & ~(CROSS | START)); eat[port] = true;
            /* the list slides out first, as for the game's own screens (the highlight part's fl_out, state 4), then the screen opens */
            g_ps_fade_dir = 1; g_ps_fade_t = GetTickCount64();
            if (mp_hilite_ok() && vm_read32(g_mp_hilite + 0x38) == 3u) { vm_write32(g_mp_hilite + 0x38, 4u); g_ps_wait = 1; g_ps_wait_t = GetTickCount64(); }
            else { ps_open_safe(); g_ps_wait = 2; }
            return; }
        const int real = mp_real_row(g_mp_page, row);
        g_mp_entry = (g_mp_page == 1 && row == 5) ? 1 : (g_mp_page == 1 && row == 6) ? 2 : 0;
        if (g_mp_entry == 1) g_mp_training_start = true;
        if (real >= 0 && real != row) {   /* another row's action: the cursor goes there, the highlight stays */
            g_mp_pin = row; g_mp_back = row; g_mp_away = false;
            vm_write32(menu + 0x44, (uint32_t)real); vm_write32(0x00956550u, (uint32_t)real);
            std::printf("[mpages] page %d row %d -> the game's row %d%s\n", g_mp_page, row, real, g_mp_entry == 1 ? " (Training)" : g_mp_entry == 2 ? " (Impossible)" : ""); std::fflush(stdout);
        }
        return;
    }
    if ((pressed & CIRCLE) && g_mp_page == 1) {   /* back to the top page, on Battle Modes */
        *b = (uint16_t)(*b & ~CIRCLE); eat[port] = true; mp_change_page(0, 0, menu); return; }
}
/* func_001E0830 (main menu enter: the list gets 9 rows and the saved cursor): the page's rows instead */
static thread_local uint32_t t_mp_enter_menu = 0;
extern "C" void jsp_mp_menuenter_enter(ppu_context* ctx) { t_mp_enter_menu = (uint32_t)ctx->gpr[3]; }
extern "C" void jsp_mp_menuenter_leave(ppu_context* ctx)
{
    if (!mp_on()) return;
    const uint32_t m = t_mp_enter_menu;
    if (g_mp_back >= 0) g_mp_away = true;   /* the menu is set up again: we were away */
    { static int said = 0; if (said++ < 6) { std::printf("[mpages] menu enter r3 %08X vt %08X list %u/%u, back %d\n", m, (m >= 0x10000u && m < 0x30000000u) ? vm_read32(m) : 0u,
          (m >= 0x10000u && m < 0x30000000u) ? vm_read32(m + 0x40) : 0u, (m >= 0x10000u && m < 0x30000000u) ? vm_read32(m + 0x44) : 0u, g_mp_back); std::fflush(stdout); } }
    mp_list_fix(m);
}
/* The preview part (class vtable 0x0087D770, update func_001E2820; helper map: +0x3C wanted image, +0x48 shown, +0x4C
 * state). func_001E2820 tail-calls, so it cannot carry a hook: the object is found by its vtable near the menu object
 * (scan of +-4 MB, once per menu object). MP_LOG=1 logs its words when they change, with the cursor. */
static uint32_t mp_preview_part(uint32_t menu)
{
    static uint32_t found = 0, for_menu = 0;
    if (found && vm_read32(found) == 0x0087D770u && for_menu == menu) return found;
    found = 0; for_menu = menu;
    const uint32_t lo = menu > 0x400000u ? (menu - 0x400000u) & ~3u : 0x10000u, hi = menu + 0x400000u;
    for (uint32_t a = lo; a < hi; a += 4) if (vm_read32(a) == 0x0087D770u) { found = a; break; }
    std::printf("[mpages] preview part %08X (menu %08X)\n", found, menu); std::fflush(stdout);
    return found;
}
/* The preview shows image +0x40 of its part, and the game writes the list cursor there (+0x3C = 2 asks for the switch,
 * +0x48 / +0x4C run its animation; a test run, rows 0..8 = images 0..8). With the pages the
 * screen row's picture is asked for: the image of the row whose picture the page gives that row. */
static void mp_preview_image(uint32_t menu)
{
    if (!mp_on() || !jsp_main_list_open()) return;
    const uint32_t part = mp_preview_part(menu); if (!part) return;
    const int row = g_mp_pin >= 0 ? g_mp_pin : (int)vm_read32(menu + 0x44);
    if (row < 0 || row > 8) return;
    const int ar = mp_art_of(g_mp_page, row), want = ar >= 0 ? ar : row;
    const uint32_t img = vm_read32(part + 0x40);
    if (img != (uint32_t)want && (img == (uint32_t)row || img == vm_read32(menu + 0x44) || g_mp_pin >= 0)) vm_write32(part + 0x40, (uint32_t)want);
}
static void mp_preview_log(uint32_t menu)
{
    static const bool lg = std::getenv("MP_LOG") != nullptr;
    if (!lg) return;
    const uint32_t part = mp_preview_part(menu); if (!part) return;
    static uint32_t last[12]; static int n = 0; uint32_t v[12];
    for (int k = 0; k < 12; k++) v[k] = vm_read32(part + 0x30u + 4u * (uint32_t)k);
    if (!std::memcmp(v, last, sizeof v) || n >= 300) return;
    std::memcpy(last, v, sizeof v); n++;
    std::printf("[mpprev] part %08X cursor %u words +30..+5C:", part, vm_read32(0x00956550u));
    for (int k = 0; k < 12; k++) std::printf(" %08X", v[k]);
    std::printf("\n"); std::fflush(stdout);
}
/* func_001E3BE0 (the row highlight, per frame): held on the screen row while the cursor is on a real row */
extern "C" void jsp_mp_hilite_enter(ppu_context* ctx)
{
    if (g_mp_off.load()) { mp_off_restore(jsp_main_menu_obj()); return; }
    if (!mp_on()) return;
    const uint32_t part = (uint32_t)ctx->gpr[3];
    if (part < 0x10000u || part >= 0x30000000u) return;
    g_mp_hilite = part; g_mp_hilite_vt = vm_read32(part);
    { const uint32_t menu = jsp_main_menu_obj(); if (menu >= 0x10000u && menu < 0x30000000u && vm_read32(menu) == 0x0087D600u) mp_preview_image(menu); }
    if (g_mp_pin >= 0) {
        const uint32_t menu = jsp_main_menu_obj(); const bool open = jsp_main_list_open() && menu >= 0x10000u && menu < 0x30000000u;
        const uint32_t cur = open ? vm_read32(menu + 0x44) : 0xFFFFFFFFu;
        const bool back = open && cur == (uint32_t)g_mp_pin;
        /* the cursor has left both the held row and the row it stood for (the stick moved it): let go at once */
        const bool moved = open && cur < (uint32_t)mp_rows() && cur != (uint32_t)g_mp_pin && (int)cur != mp_real_row(g_mp_page, g_mp_pin) &&
                           !(mp_hilite_ok() && vm_read32(g_mp_hilite + 0x38) != 3u);
        static int k = 0;
        if (moved) { k = 0; g_mp_pin = -1; g_mp_back = -1; g_mp_away = false; }
        else {
            vm_write32(part + 0x40, (uint32_t)g_mp_pin);
            if (back) { if (++k > 30) { k = 0; g_mp_pin = -1; } } else k = 0;   /* back on the page row: let go */
        }
    }
}
/* the bullets of rows the page does not have: hidden (layout calc leave, the main list layout) */
static bool g_mp_restored_panes = false;   /* after a late switch-off (g_mp_off): the hidden panes were shown again */
static void mp_layout_frame(uint32_t lay)
{
    const bool was_off = g_mp_off.load();   /* switched off after the menu was set up: show every row's panes again, once */
    if ((was_off && g_mp_restored_panes) || (!was_off && !mp_on()) || !jsp_main_list_open() || lay < 0x10000u || lay >= 0x30000000u) return;
    const uint32_t list = vm_read32(lay + 0x14); if (list < 0x10000u || list >= 0x30000000u) return;
    const uint32_t arr = vm_read32(list + 0x08), cnt = vm_read32(list + 0x0C); if (cnt < 9u || cnt > 200u) return;
    static const uint32_t h0 = mpf_name_hash("i_main_menu_pl_0000");
    if (vm_read32(vm_read32(arr) + 0x108) != h0) return;
    static uint32_t hp[9], hf[9]; static bool init = false;
    if (!init) { init = true; for (int r = 0; r < 9; r++) { char a[40], c[40];
        if (r == 0) { std::snprintf(a, 40, "i_main_menu_pl_0000"); std::snprintf(c, 40, "i_main_menu_fire_0000"); }
        else { std::snprintf(a, 40, "i_main_menu_pl_%03d", r - 1); std::snprintf(c, 40, "i_main_menu_fire_%03d", r - 1); }
        hp[r] = mpf_name_hash(a); hf[r] = mpf_name_hash(c); } }
    const int n = was_off ? 9 : mp_rows();
    if (was_off) g_mp_restored_panes = true;
    for (uint32_t i = 0; i < cnt; i++) { const uint32_t pn = vm_read32(arr + i * 4); if (pn < 0x10000u || pn >= 0x30000000u) continue;
        const uint32_t h = vm_read32(pn + 0x108);
        for (int r = 0; r < 9; r++) if (h == hp[r] || h == hf[r]) vm_write32(pn + 0x100, r < n ? (vm_read32(pn + 0x100) | 1u) : (vm_read32(pn + 0x100) & ~1u)); }   /* rows of the page shown again (the maintainer 2026-11-19: no grey bullet behind Training Mode / Impossible until the cursor moved) */
}
/* the help line: the text box on pane _f_l_cl00 (binder func_0033C5FC). Its text pointer is one of the nine help strings,
 * which lie in one block at fixed distances from row 0's (a test run); the new rows get strings of ours. */
void mp_help(ppu_context* ctx)
{
    const bool was_off = g_mp_off.load();
    if ((!was_off && !mp_on()) || !jsp_main_list_open()) return;
    const uint32_t box = (uint32_t)ctx->gpr[3], pn = (uint32_t)ctx->gpr[4];
    static const uint32_t hpane = mpf_name_hash("_f_l_cl00");
    if (box < 0x10000u || box >= 0x30000000u || pn < 0x10000u || pn >= 0x30000000u || vm_read32(pn + 0x108) != hpane) return;
    const uint32_t menu = jsp_main_menu_obj(); if (menu < 0x10000u || vm_read32(menu) != 0x0087D600u) return;
    static const uint32_t* off = nullptr;   /* the offset table of this disc and language whose strings validated (jml::help_tables) */
    static uint32_t base = 0, own[4] = { 0, 0, 0, 0 };
    const uint32_t cur = vm_read32(menu + 0x44), tp = vm_read32(box + 0x58);
    const int show = g_mp_pin >= 0 ? g_mp_pin : (int)cur;
    if (cur < 9u && tp >= 0x10000u && tp < 0x30000000u) {   /* the game's own pointer: where the block is */
        for (const uint32_t* t : jml::help_tables(g_mp_disc, mp_lang())) {
            if (tp < t[cur]) continue;
            const uint32_t b0 = tp - t[cur]; bool ok = true;
            for (int r = 0; r < 9 && ok; r++) ok = vm_read16(b0 + t[r] - 2u) == 0 && vm_read16(b0 + t[r]) != 0;
            if (ok) { if (b0 != base) { base = b0; std::printf("[mpages] help strings at %08X\n", base); std::fflush(stdout); } off = t; break; } }
    }
    if (was_off) { if (cur < 9u && base && off) vm_write32(box + 0x58, base + off[cur]); mp_off_restore(menu); return; }   /* switched off late: the game's own line again */
    if (!g_psbar_str) psbar_fill(mpf_game_new(ctx, kPsbarBytes));
    if (!own[0]) { static const jml::Id ids[4] = { jml::MENU_HELP_BATTLE, jml::MENU_HELP_TRAINING, jml::MENU_HELP_IMPOSSIBLE, jml::MENU_HELP_PORT_SETTINGS };
        for (int k = 0; k < 4; k++) { const std::vector<uint8_t> u = jml::encode_utf16(jml::text(ids[k], mp_lang()));   /* UTF-16LE, as the game's strings */
            const uint32_t a = u.empty() ? 0 : mpf_game_new(ctx, (uint32_t)u.size() + 16u); if (!a) continue;
            std::memcpy(vm_base + a, u.data(), u.size()); own[k] = a; } }
    if (show < 0 || show > 8) return;
    const int want = mp_lab_of(g_mp_page, show);
    uint32_t to = 0;
    if (want >= 100) to = own[want - 100];
    else if (want >= 0 && base && off) { int r = 0; while (r < 9 && k_mp_row_lab[r] != want) r++; if (r < 9) to = base + off[r]; }
    if (to) vm_write32(box + 0x58, to);
}
/* The port's settings screen gets the menu's plain background (the maintainer 2026-11-19: "the white manga like background be
 * the only thing behind the Port Settings menu"): while it is up, every pane of the main menu's row list, preview
 * pictures and title text is hidden (pane +0x100 bit 0), found by each layout's first pane name; the bits as they were
 * are put back when it closes. The JP gauges, the help band and the button bar stay, as on the Options screen. */
static void psbg_layout_frame(uint32_t lay)
{
    static std::map<uint32_t, uint32_t> was;   /* pane -> its +0x100 word before */
    const bool on = ps_active_safe() != 0;
    /* the preview pictures: their alpha (pane colour A R G B at +0xB0..+0xBC, the worked-out one +0x30..+0x3C; a test run:
     * both are four 1.0 floats), every frame while a fade runs or they are gone */
    { static bool touched = false; const float gone = ps_fade();
      if ((gone > 0.0f || touched) && lay >= 0x10000u && lay < 0x30000000u) {
          const uint32_t list2 = vm_read32(lay + 0x14);
          if (list2 >= 0x10000u && list2 < 0x30000000u) {
              const uint32_t arr2 = vm_read32(list2 + 0x08), cnt2 = vm_read32(list2 + 0x0C);
              static const uint32_t h_img2 = mpf_name_hash("p_main_img_000");
              if (cnt2 && cnt2 <= 200u && arr2 >= 0x10000u && arr2 < 0x30000000u) { const uint32_t q0 = vm_read32(arr2);
                  if (q0 >= 0x10000u && q0 < 0x30000000u && vm_read32(q0 + 0x108) == h_img2) {
                      /* the pane's alpha is the float at +0xC0 (a test run: +0xBC is blue, +0xB0 does
                       * nothing, +0xC0 fades). Each pane keeps its own alpha as the factor: some are 0 on purpose (a test run:
                       * writing 1.0 to all showed a green "!!" picture that is normally unseen). */
                      static std::map<uint32_t, uint32_t> own;   /* pane -> its alpha word before the fade */
                      for (uint32_t i = 0; i < cnt2; i++) { const uint32_t pn = vm_read32(arr2 + i * 4); if (pn < 0x10000u || pn >= 0x30000000u || vm_read32(pn) != 0x00882EF0u) continue;
                          if (!own.count(pn)) own[pn] = vm_read32(pn + 0xC0);
                          union { float f; uint32_t w; } u; u.w = own[pn]; u.f *= (1.0f - gone);
                          vm_write32(pn + 0xC0, gone > 0.0f ? u.w : own[pn]); }
                      touched = gone > 0.0f; if (!touched) own.clear(); } } } } }
    if (!on) {
        if (!was.empty()) { for (const auto& kv : was) if (vm_read32(kv.first) == 0x00882EF0u) vm_write32(kv.first + 0x100, (vm_read32(kv.first + 0x100) & ~1u) | (kv.second & 1u));
            std::printf("[psbg] %u panes shown again\n", (unsigned)was.size()); std::fflush(stdout); was.clear(); }
        return; }
    if (lay < 0x10000u || lay >= 0x30000000u) return;
    const uint32_t list = vm_read32(lay + 0x14); if (list < 0x10000u || list >= 0x30000000u) return;
    const uint32_t arr = vm_read32(list + 0x08), cnt = vm_read32(list + 0x0C); if (!cnt || cnt > 200u || arr < 0x10000u || arr >= 0x30000000u) return;
    const uint32_t p0 = vm_read32(arr); if (p0 < 0x10000u || p0 >= 0x30000000u) return;
    static const uint32_t h_rows = mpf_name_hash("i_main_menu_pl_0000"), h_img = mpf_name_hash("p_main_img_000"), h_title = mpf_name_hash("p_main_title_tx");
    const uint32_t h = vm_read32(p0 + 0x108);
    if (h != h_rows && h != h_title) return;   /* the pictures (h_img) fade instead, above */
    { static const bool dump = std::getenv("PSBG_DUMP") != nullptr; static int nd = 0;   /* probe: the preview panes' words, to find their colour / alpha */
      if (dump && h == h_img && nd < 2) { nd++;
          for (uint32_t i = 0; i < cnt && i < 5; i++) { const uint32_t pn = vm_read32(arr + i * 4); if (pn < 0x10000u || pn >= 0x30000000u) continue;
              char nm[40]; nm[0] = 0; mpf_read_name(vm_read32(pn + 0x104), nm, 40);
              std::printf("[psbgdump] pane %08X '%s':", pn, nm);
              for (uint32_t o = 0; o < 0x110; o += 4) std::printf(" %03X=%08X", o, vm_read32(pn + o));
              std::printf("\n"); }
          std::fflush(stdout); } }
    for (uint32_t i = 0; i < cnt; i++) { const uint32_t pn = vm_read32(arr + i * 4); if (pn < 0x10000u || pn >= 0x30000000u || vm_read32(pn) != 0x00882EF0u) continue;
        const uint32_t w = vm_read32(pn + 0x100);
        if (!was.count(pn)) was[pn] = w;
        if (w & 1u) vm_write32(pn + 0x100, w & ~1u); }
}

/* ---- hooks: layout calc (the bullets of rows a page does not have, the Port Settings background) and the help binder */
static thread_local uint32_t t_mpf_calc_layout = 0;
/* feat_3v3.cpp has code on the same two addresses (weak: may be absent) */
extern "C" __attribute__((weak)) void jsp_six_calc_enter(ppu_context* ctx);
extern "C" __attribute__((weak)) void jsp_six_calc_leave(ppu_context* ctx);
extern "C" __attribute__((weak)) void jsp_six_binder_enter(ppu_context* ctx);
extern "C" void jsp_mpf_calc_enter(ppu_context* ctx) { t_mpf_calc_layout = (uint32_t)ctx->gpr[3]; if (jsp_six_calc_enter) jsp_six_calc_enter(ctx); }
extern "C" void jsp_mpf_calc_leave(ppu_context* ctx) { if (jsp_six_calc_leave) jsp_six_calc_leave(ctx); mp_layout_frame(t_mpf_calc_layout); psbg_layout_frame(t_mpf_calc_layout); }
extern "C" void jsp_mpf_binder_enter(ppu_context* ctx) { mp_help(ctx); if (jsp_six_binder_enter) jsp_six_binder_enter(ctx); }
extern "C" void jsp_mpf_leave(ppu_context*) {}
