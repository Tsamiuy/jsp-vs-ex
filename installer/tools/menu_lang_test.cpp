// Host test of installer/port/jsp/jsp_menu_lang.h: language pick, gate/fallback, table shape, UTF-16 encoding of the accented strings.
#include "../port/jsp/jsp_menu_lang.h"
#include "../port/jsp/jsp_mp_pack.h"
#include <cstdio>
#include <set>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main()
{
    using namespace jml;
    // pick (mirrors the toolkit's sysutil_language, ps3recomp v0.19.0 libs/system/cellSysutil.c): PS3_LANG as a number (strtol over the whole string, 0..19) or an
    // EXACT code (any case); unset or empty follows Windows, else POSIX LC_ALL, LC_MESSAGES, LANG; else English
    CHECK(pick("es", 0x09) == "es"); CHECK(pick("FR", 0) == "fr"); CHECK(pick("3", 0) == "es"); CHECK(pick("4", 0) == "de"); CHECK(pick("5", 0) == "it");
    CHECK(pick("1", 0) == "en"); CHECK(pick("18", 0) == "en"); CHECK(pick("0", 0) == "en"); CHECK(pick("ja", 0) == "en"); CHECK(pick("pt", 0) == "en");   /* a language without text is shown as English */
    CHECK(pick("005", 0) == "it"); CHECK(pick(" 3", 0) == "es"); CHECK(pick("+4", 0) == "de"); CHECK(pick("3x", 0) == "en"); CHECK(pick("20", 0) == "en"); CHECK(pick("19", 0) == "en"); CHECK(pick("-3", 0) == "en");
    CHECK(pick("es-MX", 0) == "en"); CHECK(pick("fr_FR.UTF-8", 0) == "en"); CHECK(pick("deu", 0) == "en"); CHECK(pick("es_ES", 0, "fr") == "en");   // not in the code list: English, never the host language
    CHECK(pick("en-us", 0) == "en"); CHECK(pick("EN-GB", 0) == "en"); CHECK(pick("it", 0x0C) == "it");
    CHECK(pick(nullptr, 0x0A) == "es"); CHECK(pick("", 0x0C) == "fr"); CHECK(pick(nullptr, 0) == "en"); CHECK(pick(nullptr, 0x11) == "en");   // 0x11 = Japanese
    CHECK(pick(nullptr, 0, "es_ES.UTF-8") == "es"); CHECK(pick(nullptr, 0, "de") == "de"); CHECK(pick("fr", 0, "es") == "fr"); CHECK(pick(nullptr, 0x0A, "de") == "es");
    CHECK(pick(nullptr, 0, "es-MX") == "es");                      // a host locale is cut at _ - . @ like the toolkit (unlike a PS3_LANG code)
    CHECK(pick(nullptr, 0, "esp") == "en"); CHECK(pick(nullptr, 0, "ESX") == "en"); CHECK(pick(nullptr, 0, "fr@euro") == "fr");
    // POSIX: C, POSIX, C.UTF-8, POSIX.UTF-8 and empty values are skipped and the next variable decides; the first other value decides, even an unknown one
    CHECK(pick(nullptr, 0, "C", nullptr, "de_DE.UTF-8") == "de"); CHECK(pick(nullptr, 0, "POSIX", "C.UTF-8", "it_IT") == "it"); CHECK(pick(nullptr, 0, "", "", "fr") == "fr");
    CHECK(pick(nullptr, 0, "C.UTF-8", "es_ES.UTF-8", "de") == "es"); CHECK(pick(nullptr, 0, "POSIX.UTF-8", nullptr, "de") == "de");
    CHECK(pick(nullptr, 0, "C", "C", "C") == "en"); CHECK(pick(nullptr, 0, "ja_JP.UTF-8", "es", "de") == "en");   // ja decides: English, not the next variable
    CHECK(pick(nullptr, 0, "zz", "es", "de") == "en");
    CHECK(pick(nullptr, 0, "en_GB.UTF-8", "es") == "en");
    // the table: every language complete, en never empty
    for (const char* l : k_langs) { CHECK(complete(l)); for (int i = 0; i < N_IDS; i++) CHECK(!text((Id)i, l).empty()); }
    CHECK(text(MENU_HELP_BATTLE, "en") == "Choose a battle mode."); CHECK(text(MENU_BATTLE_MODES, "es") == "Modos de combate");
    CHECK(text(MENU_BATTLE_MODES, "xx").empty()); CHECK(!complete("xx"));
    // the hash tables (table tests only: the EU tables are data for a future EU hook set, the EU build has no menu-page hooks): US disc en, es, fr; EU disc all five; every table has 18 label hashes + title + mark, all nonzero and all different
    CHECK(hashes(DISC_US, "en") && hashes(DISC_US, "es") && hashes(DISC_US, "fr") && !hashes(DISC_US, "de") && !hashes(DISC_US, "it"));
    CHECK(hashes(DISC_EU, "en") && hashes(DISC_EU, "es") && hashes(DISC_EU, "fr") && hashes(DISC_EU, "de") && hashes(DISC_EU, "it"));
    CHECK(!hashes(DISC_OTHER, "en") && !hashes(DISC_UNKNOWN, "en") && !hashes(DISC_US, "xx"));
    { int tables = 0;
      for (int d = DISC_US; d <= DISC_EU; d++) for (const char* l : k_langs) { const Hashes* h = hashes(d, l); if (!h) continue; tables++;
          std::set<uint32_t> seen; seen.insert(h->title); seen.insert(h->mark);
          for (int k = 0; k < 9; k++) for (int st = 0; st < 2; st++) seen.insert(h->lab[k][st]);
          CHECK(seen.size() == 20 && !seen.count(0));
          for (int k = 0; k < 9; k++) for (int st = 0; st < 2; st++) { int kk = -1, ss = -1; CHECK(find_label(d, l, h->lab[k][st], &kk, &ss) && kk == k && ss == st); }
          CHECK(is_title(d, l, h->title) && is_mark(d, l, h->mark) && !is_title(d, l, h->mark)); }
      CHECK(tables == 8); }
    // the English set is the one the port measured in a run (title 31EB9C37, watermark 4D99F9AA, first row pair F4B325B5 / 3F2447E9, Arcade FDC7E81F / 167EE758)
    CHECK(hashes(DISC_US, "en")->title == 0x31EB9C37u && hashes(DISC_US, "en")->mark == 0x4D99F9AAu && hashes(DISC_US, "en")->lab[0][0] == 0xF4B325B5u
          && hashes(DISC_US, "en")->lab[0][1] == 0x3F2447E9u && hashes(DISC_US, "en")->lab[8][0] == 0xFDC7E81Fu && hashes(DISC_US, "en")->lab[8][1] == 0x167EE758u);
    // a language's hashes are found only in its own table: US English hashes are not a Spanish label, an unknown disc tries every disc's table
    { int k, s2; const uint32_t en = hashes(DISC_US, "en")->lab[2][0]; CHECK(!find_label(DISC_US, "es", en, &k, &s2)); CHECK(find_label(DISC_UNKNOWN, "en", en, &k, &s2));
      const uint32_t de = hashes(DISC_EU, "de")->lab[1][1]; CHECK(!find_label(DISC_US, "de", de, &k, &s2)); CHECK(find_label(DISC_EU, "de", de, &k, &s2)); CHECK(find_label(DISC_UNKNOWN, "de", de, &k, &s2));
      const uint32_t sp = hashes(DISC_EU, "es")->lab[4][0];                 // the EU Spanish pack differs from the US one in 2 of the 18 labels
      CHECK(find_label(DISC_EU, "es", sp, &k, &s2)); CHECK(!find_label(DISC_US, "es", sp, &k, &s2)); }
    // gate (lang, disc, textures ok): on only when the text, the pictures AND the disc's hashes for that language exist; every other case names a reason (the game's own
    // one-page menu, never a mixed one). Pictures: all five languages are in the build in the port; here the test sets the bits.
    label_sets() = 0x1Fu;
    for (int d : { (int)DISC_US, (int)DISC_EU, (int)DISC_UNKNOWN }) CHECK(gate("en", d, true).empty());
    CHECK(gate("es", DISC_US, true).empty() && gate("fr", DISC_US, true).empty() && gate("es", DISC_EU, true).empty() && gate("de", DISC_EU, true).empty() && gate("it", DISC_EU, true).empty());
    CHECK(gate("de", DISC_US, true).find("hashes") != std::string::npos);    // the review's G1 case: pictures exist, the disc has no hashes: off, not a mixed menu
    CHECK(gate("it", DISC_US, true).find("hashes") != std::string::npos);
    CHECK(gate("es", DISC_OTHER, true).find("hashes") != std::string::npos); CHECK(gate("en", DISC_OTHER, true).find("hashes") != std::string::npos);
    CHECK(!gate("en", DISC_US, false).empty());                              // the late backstop: no label matched
    CHECK(!gate("", DISC_US, true).empty()); CHECK(!gate("zz", DISC_US, true).empty());
    // consistency: for every disc and every system language, the language the disc shows is on only with hashes; a language shown without a table is never "on"
    for (int d : { (int)DISC_US, (int)DISC_EU }) for (const char* l : k_langs) { const std::string sh = shown(l, d); CHECK(hashes(d, sh) != nullptr); CHECK(gate(sh, d, true).empty()); }
    label_sets() = 1u;
    for (const char* l : { "es", "fr", "de", "it" }) CHECK(gate(l, DISC_EU, true).find("pictures") != std::string::npos);   // no pictures in the build: off
    CHECK(labels_ready("en") && !labels_ready("es"));
    // UTF-16LE with the terminating zero: a-grave, n-tilde, e-acute, u-umlaut, e-grave
    { const auto e = encode_utf16("\xC3\xA0\xC3\xB1\xC3\xA9\xC3\xBC\xC3\xA8"); const uint8_t w[] = { 0xE0,0, 0xF1,0, 0xE9,0, 0xFC,0, 0xE8,0, 0,0 };
      CHECK(e.size() == sizeof w && !std::memcmp(e.data(), w, sizeof w)); }
    { const auto e = encode_utf16(text(MENU_BATTLE_MODES, "it")); CHECK(e.size() == std::string("Modalit\xC3\xA0 di battaglia").size() * 2); }   // 21 letters (22 bytes in UTF-8) + the zero
    CHECK(encode_utf16("").size() == 2); CHECK(encode_utf16("A") == std::vector<uint8_t>({ 'A', 0, 0, 0 }));
    CHECK(encode_utf16("\xC3").empty()); CHECK(encode_utf16("\xF0\x9F\x98\x80").empty());   // truncated; above U+FFFF
    // every cell encodes, and no code point is above U+00FF (the catalogue's Latin-1 rule)
    for (const char* l : k_langs) for (int i = 0; i < N_IDS; i++) { const auto e = encode_utf16(text((Id)i, l)); CHECK(!e.empty());
        for (size_t k = 1; k < e.size(); k += 2) CHECK(e[k] == 0); }
    // the disc: US title ids keep the feature; the EU one has its own tables (unused in the game until the EU build has the hooks); another known id (JP ...) is off before the menu is set up; an unreadable PARAM.SFO
    // (the default "BLES00000"), an empty or null id are unknown and leave it to the texture backstop
    CHECK(disc_us("BLUS31519") == 1); CHECK(disc_us("NPUB31647") == 1); CHECK(disc_us("BLES02136") == 0); CHECK(disc_us("BLJS10258") == 0); CHECK(disc_us("BLJS10257") == 0); CHECK(disc_us("NPEB02204") == -1);   // the EU save-folder name, not a confirmed disc id: unknown, tries both tables
    CHECK(disc_us("") == -1); CHECK(disc_us(nullptr) == -1); CHECK(disc_us("BLES00000") == -1); CHECK(disc_us("BLUS99999") == -1); CHECK(disc_us("blus31519") == -1);
    CHECK(disc_kind("BLUS31519") == DISC_US && disc_kind("NPUB31647") == DISC_US && disc_kind("BLES02136") == DISC_EU && disc_kind("BLJS10258") == DISC_OTHER && disc_kind("BLES00000") == DISC_UNKNOWN);
    CHECK(gate_disc(DISC_OTHER).size() > 0); CHECK(gate_disc(DISC_US).empty()); CHECK(gate_disc(DISC_EU).empty()); CHECK(gate_disc(DISC_UNKNOWN).empty());
    // the shown language: a US disc has en, fr, es; de and it show English there, so the feature stays on for them; for the EU disc shown() keeps all five (a table value: the game shows its own menu there, the EU build has no menu-page hooks)
    CHECK(shown("de", 1) == "en"); CHECK(shown("it", 1) == "en"); CHECK(shown("es", 1) == "es"); CHECK(shown("fr", 1) == "fr"); CHECK(shown("de", DISC_EU) == "de"); CHECK(shown("it", -1) == "en"); CHECK(shown("de", -1) == "en"); CHECK(shown("de", DISC_OTHER) == "en"); CHECK(shown("es", -1) == "es"); CHECK(shown("fr", -1) == "fr");   // unknown disc: de and it could only be English
    label_sets() = 0x1Fu;
    CHECK(gate(shown(pick("de", 0), 1), 1, true).empty());               // US disc, German system: English shown, feature on
    CHECK(gate(shown(pick("es", 0), 1), 1, true).empty());               // US disc, Spanish: the NS pack's hashes and the Spanish pictures
    label_sets() = 1u; CHECK(!gate(shown(pick("es", 0), 1), 1, true).empty());   // Spanish pictures not in the build: off, never mixed
    // a language turns on only when its picture set is in the build (the port sets the bits from the files)
    label_sets() |= 2u; CHECK(labels_ready("es")); CHECK(gate(shown(pick("es", 0), 1), 1, true).empty()); CHECK(!labels_ready("fr")); CHECK(!gate("fr", 1, true).empty()); CHECK(!labels_ready("xx"));
    label_sets() = 1u; CHECK(!labels_ready("es"));
    // unknown disc (no PARAM.SFO): de and it show English, so the gate is on with the English tables and no mixed menu; a JP disc is off for every language
    label_sets() = 0x1Fu;
    CHECK(gate(shown(pick("de", 0), DISC_UNKNOWN), DISC_UNKNOWN, true).empty()); CHECK(shown(pick("it", 0), DISC_UNKNOWN) == "en");
    CHECK(gate_disc(disc_kind("NPEB02204")).empty());
    for (const char* l : k_langs) CHECK(!gate(shown(l, DISC_OTHER), DISC_OTHER, true).empty());
    label_sets() = 1u;
    // the help-line offsets (resident pack, per disc and language): one table per language that has hashes, row 0 at 0, 9 distinct 16-aligned offsets, the same tables
    // where the packs are the same, an unknown disc tries every disc's table of the language
    { for (int d : { (int)DISC_US, (int)DISC_EU }) for (const char* l : k_langs) { const auto t = help_tables(d, l); CHECK(t.size() == (hashes(d, l) ? 1u : 0u));
          for (const uint32_t* o : t) { std::set<uint32_t> seen; CHECK(o[0] == 0); for (int r = 0; r < 9; r++) { CHECK(o[r] % 16 == 0 && o[r] < 0x600); seen.insert(o[r]); } CHECK(seen.size() == 9); } }
      CHECK(help_tables(DISC_US, "en")[0][1] == 0xF0 && help_tables(DISC_US, "en")[0][2] == 0x3F0 && help_tables(DISC_US, "en")[0][8] == 0x2B0);   // the offsets the port had measured
      CHECK(help_tables(DISC_US, "fr")[0][1] == 0x120 && help_tables(DISC_US, "fr")[0][2] == 0x4F0 && help_tables(DISC_US, "fr")[0][8] == 0x350);
      CHECK(help_tables(DISC_US, "es")[0][2] == 0x460 && help_tables(DISC_US, "es")[0][8] == 0x2E0 && help_tables(DISC_EU, "es")[0][8] == 0x2E0);
      CHECK(help_tables(DISC_EU, "de")[0][1] == 0x100 && help_tables(DISC_EU, "de")[0][2] == 0x4E0 && help_tables(DISC_EU, "it")[0][1] == 0x110 && help_tables(DISC_EU, "it")[0][8] == 0x300);
      CHECK(help_tables(DISC_UNKNOWN, "fr").size() == 2 && help_tables(DISC_UNKNOWN, "de").size() == 1 && help_tables(DISC_UNKNOWN, "xx").empty() && help_tables(DISC_OTHER, "en").empty());
      CHECK(help_tables(DISC_US, "de").empty());                               // never the English offsets for another language: that was the review's N1
      const uint32_t* en = help_tables(DISC_US, "en")[0]; for (const char* l : { "es", "fr" }) CHECK(std::memcmp(en, help_tables(DISC_US, l)[0], 9 * 4) != 0); }
    // the late switch-off restores the game's 9-row list with the cursor on the real row
    { uint32_t w[5] = { 6, 4, 6, 0, 5 }; restore_list(w, 8); CHECK(w[0] == 9 && w[1] == 8 && w[2] == 9 && w[3] == 0 && w[4] == 8); }
    { uint32_t w[5] = { 7, 0, 7, 0, 6 }; restore_list(w, -1); CHECK(w[0] == 9 && w[1] == 0 && w[4] == 8); }
    { uint32_t w[5] = { 7, 0, 7, 0, 6 }; restore_list(w, 99); CHECK(w[1] == 8); }
    // the pop-up texts: the game's own words, one language per window: the window is in the language when every cell is ASCII, else all English (the overlay font is ASCII only)
    CHECK(text(MENU_FREE_BATTLE, "es") == "Combate libre"); CHECK(text(MENU_FREE_BATTLE, "it") == "Battaglia libera"); CHECK(text(MENU_FREE_BATTLE, "de") == "Freier Kampf");
    { const Id w1[3] = { MENU_CHOOSE_FIGHT, MENU_FREE_BATTLE, MENU_IMPOSSIBLE }, w2[3] = { MENU_CHOOSE_MODE, MENU_FREE_BATTLE, MENU_TRAINING_MODE };
      CHECK(dialog_lang(w1, 3, "es") == "es" && dialog_lang(w1, 3, "fr") == "fr" && dialog_lang(w1, 3, "it") == "it");
      CHECK(dialog_lang(w1, 3, "de") == "en");                              // "Unmöglich": the whole window English, not "Wähle..."/"Freier Kampf" with an English Impossible
      CHECK(dialog_lang(w2, 3, "fr") == "en");                              // "Mode entraînement"
      CHECK(dialog_lang(w2, 3, "es") == "es"); CHECK(dialog_lang(w2, 3, "de") == "en"); CHECK(dialog_lang(w2, 3, "it") == "en"); CHECK(dialog_lang(w2, 3, "en") == "en");
      for (const char* l : k_langs) for (const Id* w : { w1, w2 }) { const std::string wl = dialog_lang(w, 3, l);
          for (int i = 0; i < 3; i++) { const std::string t = text(w[i], wl); CHECK(ascii_only(t)); CHECK(t == text(w[i], "en") || wl != "en"); } }
      CHECK(text_overlay(MENU_FREE_BATTLE, "fr") == u"Combat libre"); CHECK(text_overlay(MENU_CHOOSE_MODE, "en") == u"Choose a mode"); CHECK(text_overlay(MENU_CHOOSE_FIGHT, "es") == u"Elige un combate"); }
    // complete() looks at the raw cells: a language is complete only when no cell falls back to English
    CHECK(cell_of(MENU_FREE_BATTLE, "es") == "Combate libre"); CHECK(cell_of(MENU_FREE_BATTLE, "xx").empty());
    // the packed pictures (jsp_mp_pack.h): a golden text made by the Python tool, a round trip, malformed input
    {
        std::vector<uint8_t> blob(32, 0); for (int i = 0; i < 16; i++) blob.push_back((uint8_t)i);
        for (int k = 0; k < 3; k++) blob.insert(blob.end(), 16, 0xAB);
        for (int i = 16; i < 48; i++) blob.push_back((uint8_t)i);
        blob.insert(blob.end(), 200 * 16, 0);
        static const char gold[] = "IsYOEMK-^P!!!!!!!!!!!!!!!!!!!#!ZvT0#t]GB%5C8R&P,0p[0m<&[0m<&[0m<&[0m:&'/sQk(JYE#)d>63+%%)E,=ctU-XIgf.r.Yv02mM.vv=%!!!!!!!!!!!!!!!!!76(A!!!!!!!!!!!!!!!!!!";
        CHECK(blob.size() == 3328); CHECK(jmp::pack(blob) == gold);                              // the Python tool writes the same text
        CHECK(jmp::unpack(gold, blob.size()) == blob);
        CHECK(jmp::unpack(gold, blob.size() + 16).empty()); CHECK(jmp::unpack(gold, blob.size() - 16).empty());   // a wrong output size
        std::string cut(gold, sizeof gold - 1 - 8); CHECK(jmp::unpack(cut.c_str(), blob.size()).empty());         // truncated
        std::string bad(gold); bad[10] = '"'; CHECK(jmp::unpack(bad.c_str(), blob.size()).empty());               // a character outside the alphabet
        CHECK(jmp::unpack("", 16).empty()); CHECK(jmp::unpack("!", 16).empty()); CHECK(jmp::unpack(nullptr, 16).empty());
        uint32_t x = 12345; std::vector<uint8_t> r; for (int i = 0; i < 16 * 300; i++) { x = x * 1664525u + 1013904223u; r.push_back((i / 16) % 7 == 0 ? 0 : (uint8_t)(x >> 24)); }
        CHECK(jmp::unpack(jmp::pack(r).c_str(), r.size()) == r);                                  // runs and literals mixed
        std::vector<uint8_t> z(16 * 1000, 0); const std::string zs = jmp::pack(z); CHECK(zs.size() < 220); CHECK(jmp::unpack(zs.c_str(), z.size()) == z);   // runs longer than 128
        const std::string& a = jmp::alphabet(); CHECK(a.size() == 85 && a.find('"') == std::string::npos && a.find('\\') == std::string::npos && a.find('?') == std::string::npos && a.find('@') == std::string::npos);
    }
    if (!fails) std::printf("menu_lang_test: ok\n");
    return fails ? 1 : 0;
}
