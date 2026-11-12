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

// Layout in the 1280x720 overlay space. ASSUMPTION: read off a main-menu shot (960x523, x1.333): the button legend bar
// (Confirm / Back) spans y 636..675 with its text centred near y 654 and its right end near x 1180; the line sits in that bar's
// right part, right edge 1160, cap height centred on the legend's text. A shot decides.
inline constexpr float kRight = 1160.0f, kY1 = 645.0f, kScale1 = 0.8f;

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
inline bool top_menu(bool obj_alive, bool panel_seen, uint32_t flips_since_update, int panel_state)
{
    return obj_alive && panel_seen && flips_since_update <= 2 && panel_state == 0;
}

}  // namespace jpv
