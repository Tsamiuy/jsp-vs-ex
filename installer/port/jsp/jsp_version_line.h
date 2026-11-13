// ENHANCEMENT (display, default on): the port's version line in the main menu's lower right corner.
// Pure text and value choice, no game or overlay calls, so installer/tools/version_line_test.cpp runs it on the host.
//
// Value sources, first valid wins:
//   1. env JSP_PORT_VERSION (tests)
//   2. release.txt, first line: written by assemble_payload.py --release X.Y.Z only in a release build (release.yml passes the tag's
//      version, the script checks it equals installer/VERSION). A local or PR build has no release.txt and shows nothing, so a
//      non-release build is never presented as a release. (VERSION.txt exists in every payload and is NOT used.)
//      The payload root is the install folder, the exe lives in its game\ subfolder (exe folder, then its parent).
// Off: JSP_VERSION_LINE=0. JSP_VERSION_LINE=dev shows "dev" when no version is known; without it a dev build shows nothing.
#pragma once
#include <string>
#include <cstdlib>
#include <cstdint>

namespace jpv {

inline constexpr const char* kName = "J-Stars Victory VS-EX";   // the exact project name, with the hyphen

// Layout in the 1280x720 overlay space. MEASURED on a main-menu screenshot (1280x720 canvas, pixel rows/columns):
//   - the button legend bar (Confirm / Back) has a light interior at rows 632..661 (dark border rows 628..631 above, 662..666 below),
//     centre 646.5. The bar's right end is an arrow point: the inner (light) edge runs x 1158 (row 632) .. 1166 (row 646) .. 1155 (row 660),
//     so the narrowest interior x over the whole bar is 1155 (kBarEdgeMinX); the dark border continues to x 1177 outside it.
//   - the game's own "Back" label: ink rows 637..656 (centre 646.5, 19 px incl. its outline), ends at x 361; "Confirm" 169..252.
//   - the overlay font (menu_font.jmf, cap 21, baseline 34 in a 52 px cell): at scale 1 the capitals' body spans rows -22..0 around the
//     baseline and the rim -24..+2; J and y descend to +8 / +9 (rim). The pen y of jsp_overlay_text_style is the top of the capitals
//     (baseline = y + 21 * scale).
// The earlier layout drew at scale 0.8, right 1160, y 645: ink rows 639..667, right end 1160+ past the edge at 1155: it crowded the bar and its
// slanted end. Now: scale 0.65 (capitals 14.3 px body / 17.6 px with rim, a little under the 19 px of the game's labels), the capitals' ink
// box centred on the bar (baseline 653.3: rim 637.7..655.3, descenders to 659.2 < 662), right edge 12 px left of the narrowest inner edge.
inline constexpr float kBarTop = 632.0f, kBarBottom = 662.0f;     // interior rows [top, bottom)
inline constexpr float kBarEdgeMinX = 1155.0f;                     // narrowest inner x of the slanted right end over the interior rows
inline constexpr float kBackRight = 361.0f;                        // right end of the game's "Back" label
inline constexpr float kMargin = 12.0f;                            // clear space from the slanted edge (and from "Back")
inline constexpr float kFontCap = 21.0f;                           // menu_font.jmf header: cap height at scale 1
inline constexpr float kInkAbove = 24.0f, kInkBelow = 9.0f;        // rim ink above / below the baseline at scale 1 (measured, J and y)
inline constexpr float kInkSide = 3.0f;                            // ASSUMPTION: rim reaches at most 3 px (scale 1) past the advance box
inline constexpr float kRight = kBarEdgeMinX - kMargin, kScale1 = 0.65f;      // 1143
inline constexpr float kY1 = 653.3f - kFontCap * kScale1;                     // 639.65: baseline 653.3 = bar centre 646.5 + 6.8

struct InkBox { float x0, y0, x1, y1; };
// Ink box of line 1 for a text advance width (pixels at the used scale), as drawn: right-aligned at kRight.
inline InkBox ink_box(float text_width, float scale = kScale1)
{
    const float base = kY1 + kFontCap * scale;
    return { kRight - text_width - kInkSide * scale, base - kInkAbove * scale, kRight + kInkSide * scale, base + kInkBelow * scale };
}

inline bool valid_version(const std::string& v)
{
    if (v.empty() || v.size() > 24) return false;
    auto alnum = [](char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    if (!alnum(v[0])) return false;
    for (char c : v) if (!alnum(c) && c != '.' && c != '-' && c != '_' && c != '+') return false;
    return true;
}

inline std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return s.substr(a, b - a);
}

// release.txt: the first line (a UTF-8 BOM is tolerated); "" when not a valid version.
inline std::string parse_version_txt(const std::string& text)
{
    std::string t = text;
    if (t.size() >= 3 && (uint8_t)t[0] == 0xEF && (uint8_t)t[1] == 0xBB && (uint8_t)t[2] == 0xBF) t.erase(0, 3);
    const size_t nl = t.find('\n');
    const std::string v = trim(nl == std::string::npos ? t : t.substr(0, nl));
    return valid_version(v) ? v : "";
}

struct Inputs {
    const char* switch_env = nullptr;   // JSP_VERSION_LINE
    const char* port_version = nullptr; // JSP_PORT_VERSION
    std::string file_version;           // parse_version_txt of release.txt, "" when absent
};

struct Lines { std::string line1; bool any() const { return !line1.empty(); } };

// The value choice. Nothing at all (empty line) when off, or when no version is known and the switch is not "dev".
inline Lines choose(const Inputs& in)
{
    Lines out;
    const std::string sw = in.switch_env ? in.switch_env : "";
    if (sw == "0") return out;
    std::string ver;
    if (in.port_version && valid_version(in.port_version)) ver = in.port_version;
    else ver = in.file_version;
    if (ver.empty()) {
        if (sw != "dev") return out;
        out.line1 = std::string(kName) + " dev";
        return out;
    }
    out.line1 = std::string(kName) + " " + ver;
    return out;
}

// The main menu's top list is up: the main-menu object is alive, the panel (sub-menu list, same object class for every sub-menu)
// has been updated within the last two flips (a dead scene stops updating it) and was closed (state 0) at its last update.
// Panel states (re-notes-data-logo R5): 0 closed, 1 open requested, 2 opening, 3 active, 4 close requested, 5 closing.
// MEASURED in a Proton run of the development tree (JSP_VERSION_PROBE): the idle Main Menu is flow state S+0x30 == 4 (S=[0x009535F0])
// with menu mode [0x0095653C+0x10] == 0. State 1/2 is the title -> menu transition (panel -1, not the menu), 6/7 with mode 5 is the menu
// leaving into Options. flow_state is -1 when S is 0. Without this rule the panel object stays alive and closed while the menu leaves, and
// the line stayed up. The line hides on the first flip either value changes.
inline bool top_menu(bool obj_alive, bool panel_seen, uint32_t flips_since_update, int panel_state, int flow_state, int menu_mode)
{
    return obj_alive && panel_seen && flips_since_update <= 2 && panel_state == 0 && flow_state == 4 && menu_mode == 0;
}

}  // namespace jpv
