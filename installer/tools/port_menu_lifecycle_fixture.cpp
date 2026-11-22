// The host driver supplies the three production callback bodies in lifecycle.inc.
#define JSP_PORT_MENU_TEST 1
#include "../port/jsp/jsp_port_menu.cpp"
#include "../port/jsp/jsp_overlay_owner.h"
#include "../port/jsp/jsp_overlay_api.h"
#include <cassert>
#include <mutex>

namespace {
struct ppu_context { uint64_t gpr[32]{}; };
jpm::Menu g_menu;
std::mutex g_mu;
uint32_t g_obj = 0, g_obj_vt = 0, g_ver_flip = 0;
constexpr uint32_t k_mode_g = 0, k_cursor = 16;
int g_ver_state = 0, g_join_next = 0;
bool g_ver_seen = false, g_ver_vis = false, g_dead_logged = false, g_drawn = false;
bool g_net_watch = false, g_note_vis = false, g_joining = false, g_help = false;
uint64_t g_note_since = 0;
unsigned g_note_gen = 0;
jpno::Notice g_note;
struct { bool any() const { return true; } } g_ver;
bool top_items() { return true; }
int flow_state() { return 0; }
uint32_t vm_read32(uint32_t) { return 0; }
void vm_write32(uint32_t, uint32_t) {}
unsigned cellGcm_flip_request_count() { return 1; }
uint64_t mono_ms() { return 0; }
unsigned jsp_net_notice_gen() { return 0; }
int jsp_net_notice(char*, int, char*, int) { return 0; }
bool port_menu_env() { return false; }
void g_orig_build(ppu_context*) {}
std::string pixels;
void ver_draw() { pixels = "version"; g_drawn = true; }
void join_draw() { pixels = "join"; g_drawn = true; }
int jsp_online_describe(int, char*, int) { return -1; }
#include "lifecycle.inc"
}

extern "C" int jsp_overlay_begin(int, int) { pixels = "menu"; return 1; }
extern "C" float jsp_overlay_text_width(const char* text, float scale) { return std::string(text).size() * scale; }
extern "C" void jsp_overlay_end() {}
extern "C" void jsp_overlay_close() { pixels.clear(); }
extern "C" void jsp_overlay_fill_rect(int, int, int, int, uint32_t) {}
extern "C" void jsp_overlay_text_style(float, float, const char*, float, uint32_t, int) {}

int main() {
    for (unsigned which = 0; which < 4; ++which) {
        g_menu.page = jpm::CLOSED; g_joining = g_help = false;
        g_ver_seen = g_ver_vis = true;
        draw_page();
        assert(jpo::menu_active() && pixels == "version");
        if (which == 1) g_menu.page = jpm::MAIN;
        if (which == 2) g_help = true;
        if (which == 3) { g_joining = true; g_join_next = 2; }
        ppu_context ctx{};
        on_build_options(&ctx);
        ver_refresh();  // no visible top menu, equal to the builder's reset state
        jpo::StatusFrame network;
        if (network) pixels = "Opponent disconnected";
        if (which == 0) assert(network && pixels == "Opponent disconnected");
        else assert(!network && pixels == (which == 3 ? "join" : "menu"));
    }
}
