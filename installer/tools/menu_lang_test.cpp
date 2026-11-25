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
    CHECK(pick("1", 0) == "en"); CHECK(pick("18", 0) == "en"); CHECK(pick("0", 0) == "en"); CHECK(pick("ja", 0) == "en"); CHECK(pick("pt", 0) == "en");   /* a language without text is shown as English */
    CHECK(pick("es-MX", 0) == "es"); CHECK(pick("fr_FR.UTF-8", 0) == "fr"); CHECK(pick("deu", 0) == "en");
    CHECK(pick(nullptr, 0x0A) == "es"); CHECK(pick("", 0x0C) == "fr"); CHECK(pick(nullptr, 0) == "en"); CHECK(pick(nullptr, 0x11) == "en");   // 0x11 = Japanese
    CHECK(pick(nullptr, 0, "es_ES.UTF-8") == "es"); CHECK(pick(nullptr, 0, "C") == "en"); CHECK(pick(nullptr, 0, "de") == "de"); CHECK(pick("fr", 0, "es") == "fr"); CHECK(pick(nullptr, 0x0A, "de") == "es");
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
    { const auto e = encode_utf16(text(MENU_BATTLE_MODES, "it")); CHECK(e.size() == std::string("Modalit\xC3\xA0 di battaglia").size() * 2); }   // 21 letters (22 bytes in UTF-8) + the zero
    CHECK(encode_utf16("").size() == 2); CHECK(encode_utf16("A") == std::vector<uint8_t>({ 'A', 0, 0, 0 }));
    CHECK(encode_utf16("\xC3").empty()); CHECK(encode_utf16("\xF0\x9F\x98\x80").empty());   // truncated; above U+FFFF
    // every cell encodes, and no code point is above U+00FF (the catalogue's Latin-1 rule)
    for (const char* l : k_langs) for (int i = 0; i < N_IDS; i++) { const auto e = encode_utf16(text((Id)i, l)); CHECK(!e.empty());
        for (size_t k = 1; k < e.size(); k += 2) CHECK(e[k] == 0); }
    // the disc: a US title id keeps the feature; another one turns it off before the menu is set up; unknown leaves it to the texture backstop
    CHECK(disc_us("BLUS31519") == 1); CHECK(disc_us("NPUB31647") == 1); CHECK(disc_us("BLES02136") == 0); CHECK(disc_us("") == -1); CHECK(disc_us(nullptr) == -1);
    CHECK(gate_disc(0).size() > 0); CHECK(gate_disc(1).empty()); CHECK(gate_disc(-1).empty());
    // the shown language: a US disc has en, fr, es; de and it show English there, so the feature stays on for them; the EU disc shows all five
    CHECK(shown("de", 1) == "en"); CHECK(shown("it", 1) == "en"); CHECK(shown("es", 1) == "es"); CHECK(shown("fr", 1) == "fr"); CHECK(shown("de", 0) == "de"); CHECK(shown("it", -1) == "it");
    CHECK(gate(shown(pick("de", 0), 1), true).empty());                  // US disc, German system: English shown, feature on
    CHECK(!gate(shown(pick("es", 0), 1), true).empty());                 // US disc, Spanish: no Spanish label pictures yet: off, never mixed
    // the late switch-off restores the game's 9-row list with the cursor on the real row
    { uint32_t w[5] = { 6, 4, 6, 0, 5 }; restore_list(w, 8); CHECK(w[0] == 9 && w[1] == 8 && w[2] == 9 && w[3] == 0 && w[4] == 8); }
    { uint32_t w[5] = { 7, 0, 7, 0, 6 }; restore_list(w, -1); CHECK(w[0] == 9 && w[1] == 0 && w[4] == 8); }
    { uint32_t w[5] = { 7, 0, 7, 0, 6 }; restore_list(w, 99); CHECK(w[1] == 8); }
    // the pop-up texts: the game's own words, ASCII-only cells in the wide string, accented cells fall back to English until the overlay font is widened
    CHECK(text(MENU_FREE_BATTLE, "es") == "Combate libre"); CHECK(text(MENU_FREE_BATTLE, "it") == "Battaglia libera"); CHECK(text(MENU_FREE_BATTLE, "de") == "Freier Kampf");
    CHECK(text_overlay(MENU_FREE_BATTLE, "fr") == u"Combat libre"); CHECK(text_overlay(MENU_CHOOSE_MODE, "en") == u"Choose a mode");
    CHECK(text_overlay(MENU_IMPOSSIBLE, "de") == u"Impossible");          // "Unmöglich" is not ASCII: English for now
    CHECK(text_overlay(MENU_CHOOSE_FIGHT, "es") == u"Elige un combate");
    if (!fails) std::printf("menu_lang_test: ok\n");
    return fails ? 1 : 0;
}
