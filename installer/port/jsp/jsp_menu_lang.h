// jsp_menu_lang.h -- the language of the two-page Main Menu's own text (help lines and row labels), and the gate that keeps the page
// feature to the languages that have all of it. Header only, no OS calls: installer/tools/menu_lang_test.sh runs it.
//
// Why (a tester's screenshot, EU disc in Spanish): the row labels are swapped by matching US texture hashes (feat_menu_pages.cpp,
// k_mp_lab_fnv), which do not match on another disc, so the rows stayed the game's Spanish while mp_help wrote English help lines.
// Now: the feature runs only when (a) the text language has a complete table here AND its row labels exist as pictures
// (labels_ready), and (b) the disc's label textures are the US ones (feat_menu_pages.cpp mp_tex_upload). Otherwise it is off and the
// game's own one-page menu shows, with one log line saying why. Never a mixed menu.
//
// Language pick (ASSUMPTION: it follows the toolkit's sysutil_language rule as the review of the toolkit described it, not read from the toolkit
// source in this tree: PS3_LANG as a number or code, then the host UI language, then English): the game reads the same inputs, so port
// and game agree. shown() then maps it to what the disc displays (a US disc has en, fr and es only; de and it show English).
// The feature is decided before the menu is set up: gate_disc() needs a US title id (BLUS31519 / NPUB31647) and the shown language needs its
// row-label pictures (labels_ready); only the texture check in mp_tex_upload stays as a backstop when the title id is not known.
//
// The table below has the message catalogue's shape (ids MENU_*, columns en es fr de it) and moves into messages.tsv unchanged
// (there the cells are Latin-1; here they are UTF-8 and encode_utf16 turns them into the game's UTF-16LE).
// DRAFT: every es, fr, de and it cell is a machine draft pending native review (es Latin American tu, fr vous, game's own terms win).
#ifndef JSP_MENU_LANG_H
#define JSP_MENU_LANG_H
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>   // std::u16string too
#include <vector>

namespace jml {
enum Id { MENU_BATTLE_MODES, MENU_TRAINING_MODE, MENU_IMPOSSIBLE_MODE, MENU_PORT_SETTINGS,
          MENU_HELP_BATTLE, MENU_HELP_TRAINING, MENU_HELP_IMPOSSIBLE, MENU_HELP_PORT_SETTINGS,
          MENU_CHOOSE_MODE, MENU_FREE_BATTLE, MENU_CHOOSE_FIGHT, MENU_IMPOSSIBLE, N_IDS };
static const char* const k_langs[5] = { "en", "es", "fr", "de", "it" };   // column order of the table
// id <TAB> en <TAB> es <TAB> fr <TAB> de <TAB> it, one row per Id in enum order, first byte is the newline after R"MSG(
static const char k_table[] = R"MSG(
MENU_BATTLE_MODES	Battle Modes	Modos de combate	Modes de combat	Kampfmodi	Modalità di battaglia
MENU_TRAINING_MODE	Training Mode	Modo entrenamiento	Mode entraînement	Trainingsmodus	Modalità allenamento
MENU_IMPOSSIBLE_MODE	Impossible Mode	Modo imposible	Mode impossible	Unmöglich-Modus	Modalità impossibile
MENU_PORT_SETTINGS	Port Settings	Ajustes del port	Paramètres du portage	Port-Einstellungen	Impostazioni del port
MENU_HELP_BATTLE	Choose a battle mode.	Elige un modo de combate.	Choisissez un mode de combat.	Wähle einen Kampfmodus.	Scegli una modalità di battaglia.
MENU_HELP_TRAINING	Practice your moves against a training dummy.	Practica tus movimientos contra un muñeco de entrenamiento.	Entraînez-vous contre un mannequin d'entraînement.	Übe deine Moves an einer Trainingspuppe.	Allenati con le tue mosse contro un manichino.
MENU_HELP_IMPOSSIBLE	Take on boss fights against an unbeatable CPU.	Enfréntate a combates de jefe contra una CPU invencible.	Affrontez des combats de boss contre un CPU imbattable.	Stell dich Bosskämpfen gegen eine unschlagbare CPU.	Affronta combattimenti contro boss gestiti da una CPU imbattibile.
MENU_HELP_PORT_SETTINGS	Change the PC port's display, graphics, control and online settings.	Cambia los ajustes de pantalla, gráficos, controles y online del port para PC.	Modifiez les paramètres d'affichage, de graphismes, de commandes et en ligne du portage PC.	Ändere die Anzeige-, Grafik-, Steuerungs- und Online-Einstellungen der PC-Portierung.	Modifica le impostazioni di schermo, grafica, comandi e online del port per PC.
MENU_CHOOSE_MODE	Choose a mode	Elige un modo	Choisissez un mode	Wähle einen Modus	Scegli una modalità
MENU_FREE_BATTLE	Free Battle	Combate libre	Combat libre	Freier Kampf	Battaglia libera
MENU_CHOOSE_FIGHT	Choose a fight	Elige un combate	Choisissez un combat	Wähle einen Kampf	Scegli una battaglia
MENU_IMPOSSIBLE	Impossible	Imposible	Impossible	Unmöglich	Impossibile
)MSG";

// Row labels are pictures (300 x 43 DXT5 pairs, one white one black, feat_menu_pages.cpp k_mp_new_lab): only English ones exist.
// Open: per-language pictures would be built at run time from the player's own label texture plus the table text; not done.
inline bool labels_ready(const std::string& lang) { return lang == "en"; }

// code of a PS3 system language number (the PS3_LANG 0..19 order), "" when outside the five
inline std::string code_of_number(int n) { switch (n) { case 1: case 18: return "en"; case 2: return "fr"; case 3: return "es"; case 4: return "de"; case 5: return "it"; } return ""; }
// code of a Windows primary language id (GetUserDefaultUILanguage() & 0x3FF)
inline std::string code_of_winlang(int primary) { switch (primary) { case 0x09: return "en"; case 0x0C: return "fr"; case 0x0A: return "es"; case 0x07: return "de"; case 0x10: return "it"; } return ""; }
// the language of a locale text such as "es_ES.UTF-8", "fr-FR" or "de": its first two letters; "" when not one of the five
inline std::string code_of_locale(const char* loc)
{
    if (!loc || !loc[0] || !loc[1]) return "";
    std::string s; s += (char)((loc[0] >= 'A' && loc[0] <= 'Z') ? loc[0] + 32 : loc[0]); s += (char)((loc[1] >= 'A' && loc[1] <= 'Z') ? loc[1] + 32 : loc[1]);
    if (loc[2] && loc[2] != '_' && loc[2] != '-' && loc[2] != '.' && loc[2] != '@') return "";
    for (const char* l : k_langs) if (s == l) return s;
    return "";
}
// The system language the game is told, by the rule the review gave for the toolkit's sysutil_language() (libs/system/cellSysutil.c; ASSUMPTION
// until read in the toolkit tree here): PS3_LANG as a number or a code, else the host UI language (win_primary: Windows primary language id, 0 =
// not Windows; posix_locale: LC_ALL, else LC_MESSAGES, else LANG on other systems), else English. A language the port has no text for (ja, ru, ...) is
// reported as "en": the game shows English for a language its disc has no pack for (ASSUMPTION; see shown()).
inline std::string pick(const char* ps3_lang, int win_primary, const char* posix_locale = nullptr)
{
    std::string r;
    if (ps3_lang && ps3_lang[0]) {
        std::string s(ps3_lang);
        if (s.size() <= 2 && s.find_first_not_of("0123456789") == std::string::npos) r = code_of_number(std::atoi(s.c_str()));
        else r = code_of_locale(ps3_lang);
    } else if (win_primary) r = code_of_winlang(win_primary);
    else r = code_of_locale(posix_locale);
    return r.empty() ? "en" : r;
}
// the title id of a disc: 1 = a US disc (BLUS31519 or its PSN id NPUB31647), 0 = another one (EU BLES02136 ...), -1 = not known
inline int disc_us(const char* title_id)
{
    if (!title_id || !title_id[0]) return -1;
    const std::string t(title_id);
    return (t == "BLUS31519" || t == "NPUB31647") ? 1 : 0;
}
// The language the disc SHOWS: the US disc has packs for en, fr and es only (it never asks for DE or IT: the review of the US disc), so de and it
// show English there. Other discs show all five.
inline std::string shown(const std::string& lang, int disc)
{
    if (disc == 1 && (lang == "de" || lang == "it")) return "en";
    return lang;
}
// the window of the state the late switch-off puts back: the main list's words {count, cursor, visible, top, bottom} to the game's own 9 rows,
// the cursor on the given real row (clamped to 0..8)
inline void restore_list(uint32_t w[5], int real_row) { w[0] = 9; w[1] = (uint32_t)(real_row < 0 ? 0 : real_row > 8 ? 8 : real_row); w[2] = 9; w[3] = 0; w[4] = 8; }
// the cell of id in lang ("" when the language or id is unknown); an empty cell falls back to English (per string, as the spec's fallback)
inline std::string text(Id id, const std::string& lang)
{
    int col = -1; for (int i = 0; i < 5; i++) if (lang == k_langs[i]) col = i;
    if (col < 0 || id < 0 || id >= N_IDS) return "";
    const char* p = k_table + 1; int row = 0;
    while (*p && row < id) { if (*p == '\n') row++; p++; }
    std::vector<std::string> cell(1); for (; *p && *p != '\n'; p++) { if (*p == '\t') cell.emplace_back(); else cell.back() += *p; }
    if (cell.size() != 6) return "";
    return cell[1 + col].empty() ? cell[1] : cell[1 + col];
}
// The cell as a UTF-16 wide string for the port's own pop-up windows (the toolkit's overlay font is ASCII only, menu_font.jmf): a cell with a
// character above 0x7F falls back to English per string until that font is widened (the UI spec's font patch).
inline std::u16string text_overlay(Id id, const std::string& lang)
{
    std::string t = text(id, lang); bool ascii = !t.empty(); for (unsigned char c : t) if (c > 0x7F) ascii = false;
    if (!ascii) t = text(id, "en");
    std::u16string o; for (unsigned char c : t) o += (char16_t)c;
    return o;
}
inline bool complete(const std::string& lang)
{
    for (int i = 0; i < N_IDS; i++) if (text((Id)i, lang).empty()) return false;
    return true;
}
// UTF-8 to UTF-16LE with the terminating zero; code points above U+FFFF and malformed input give an empty vector
inline std::vector<uint8_t> encode_utf16(const std::string& s)
{
    std::vector<uint8_t> o; size_t i = 0;
    auto put = [&](uint32_t u) { o.push_back((uint8_t)u); o.push_back((uint8_t)(u >> 8)); };
    while (i < s.size()) { const uint8_t c = (uint8_t)s[i]; uint32_t u;
        if (c < 0x80) { u = c; i++; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < s.size() && ((uint8_t)s[i + 1] & 0xC0) == 0x80) { u = ((c & 0x1Fu) << 6) | ((uint8_t)s[i + 1] & 0x3Fu); i += 2; }
        else if ((c & 0xF0) == 0xE0 && i + 2 < s.size() && ((uint8_t)s[i + 1] & 0xC0) == 0x80 && ((uint8_t)s[i + 2] & 0xC0) == 0x80) { u = ((c & 0x0Fu) << 12) | (((uint8_t)s[i + 1] & 0x3Fu) << 6) | ((uint8_t)s[i + 2] & 0x3Fu); i += 3; }
        else return {};
        put(u); }
    put(0); return o;
}
// the disc gate, before the menu is set up: "" = on (a US disc, or a title id not known: the texture backstop then decides)
inline std::string gate_disc(int disc) { return disc == 0 ? "this is not a US disc (title id), its row-label textures are not the US ones" : ""; }
// the gate: "" = on, else the reason (one log line)
inline std::string gate(const std::string& lang, bool us_label_textures)
{
    if (lang.empty()) return "text language is not one of en, es, fr, de, it";
    if (!complete(lang)) return "no complete text table for " + lang;
    if (!labels_ready(lang)) return "no row-label pictures for " + lang + " yet";
    if (!us_label_textures) return "this disc's row-label textures are not the US ones";
    return "";
}
}  // namespace jml
#endif
