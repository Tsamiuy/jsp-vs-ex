// Host test of installer/port/jsp/jsp_menu_lang.h: language pick, gate/fallback, table shape, UTF-16 encoding of the accented strings.
#include "../port/jsp/jsp_menu_lang.h"
#include <cstdio>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main()
{
    using namespace jml;
    // pick: PS3_LANG as code (any case) or number; unset follows Windows then English; an unknown value gives ""
    CHECK(pick("es", 0x09) == "es"); CHECK(pick("FR", 0) == "fr"); CHECK(pick("3", 0) == "es"); CHECK(pick("4", 0) == "de"); CHECK(pick("5", 0) == "it");
    CHECK(pick("1", 0) == "en"); CHECK(pick("18", 0) == "en"); CHECK(pick("0", 0).empty()); CHECK(pick("ja", 0).empty()); CHECK(pick("pt", 0).empty());
    CHECK(pick(nullptr, 0x0A) == "es"); CHECK(pick("", 0x0C) == "fr"); CHECK(pick(nullptr, 0) == "en"); CHECK(pick(nullptr, 0x11).empty());   // 0x11 = Japanese
    // the table: every language complete, en never empty
    for (const char* l : k_langs) { CHECK(complete(l)); for (int i = 0; i < N_IDS; i++) CHECK(!text((Id)i, l).empty()); }
    CHECK(text(MENU_HELP_BATTLE, "en") == "Choose a battle mode."); CHECK(text(MENU_BATTLE_MODES, "es") == "Modos de combate");
    CHECK(text(MENU_BATTLE_MODES, "xx").empty()); CHECK(!complete("xx"));
    // gate: only en on US textures is on; every other case names a reason (the game's own one-page menu, never a mixed one)
    CHECK(gate("en", true).empty());
    CHECK(!gate("en", false).empty());                       // an EU disc's textures: off
    for (const char* l : { "es", "fr", "de", "it" }) CHECK(gate(l, true).find("pictures") != std::string::npos);   // no label pictures yet: off
    CHECK(!gate("", true).empty()); CHECK(!gate("zz", true).empty());
    // UTF-16LE with the terminating zero: a-grave, n-tilde, e-acute, u-umlaut, e-grave
    { const auto e = encode_utf16("\xC3\xA0\xC3\xB1\xC3\xA9\xC3\xBC\xC3\xA8"); const uint8_t w[] = { 0xE0,0, 0xF1,0, 0xE9,0, 0xFC,0, 0xE8,0, 0,0 };
      CHECK(e.size() == sizeof w && !std::memcmp(e.data(), w, sizeof w)); }
    { const auto e = encode_utf16(text(MENU_BATTLE_MODES, "it")); CHECK(e.size() == (std::string("Modalit\xC3\xA0 di lotta").size() - 1 + 1) * 2); }   // 20 letters + zero
    CHECK(encode_utf16("").size() == 2); CHECK(encode_utf16("A") == std::vector<uint8_t>({ 'A', 0, 0, 0 }));
    CHECK(encode_utf16("\xC3").empty()); CHECK(encode_utf16("\xF0\x9F\x98\x80").empty());   // truncated; above U+FFFF
    // every cell encodes, and no code point is above U+00FF (the catalogue's Latin-1 rule)
    for (const char* l : k_langs) for (int i = 0; i < N_IDS; i++) { const auto e = encode_utf16(text((Id)i, l)); CHECK(!e.empty());
        for (size_t k = 1; k < e.size(); k += 2) CHECK(e[k] == 0); }
    if (!fails) std::printf("menu_lang_test: ok\n");
    return fails ? 1 : 0;
}
