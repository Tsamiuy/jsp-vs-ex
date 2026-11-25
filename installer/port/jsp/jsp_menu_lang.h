// jsp_menu_lang.h -- the language of the two-page Main Menu's own text (help lines and row labels), and the gate that keeps the page
// feature to the languages that have all of it. Header only, no OS calls: installer/tools/menu_lang_test.sh runs it.
//
// Why (a tester's screenshot, EU disc in Spanish): the row labels are swapped by matching the hash of the texture the game uploads, and the
// hashes were the US English ones only, so on another disc the rows stayed the game's Spanish while mp_help wrote English help lines.
// Now every (disc, language) has its own hash table (k_hashes_us, k_hashes_eu below: row labels, page title, help-bar watermark, taken from the
// disc files with the project's own CPK / $CMP / STPK-SRD readers, which are not in this repository). The feature runs only when
//   (a) the text language has a complete table here AND its pictures exist (labels_ready), and
//   (b) the disc is known to have hashes for the language the disc shows (hashes(), the gate).
// A title id that is not known (no PARAM.SFO) tries the tables of every disc for the language; a known disc with no tables (JP) is off. Otherwise the
// game's own one-page menu shows, with one log line saying why. Never a mixed menu: one language per menu and per pop-up window.
//
// Language pick: pick() mirrors the toolkit's sysutil_language() on Windows and POSIX (not its macOS CFLocale fallback: the port is Windows only) (libs/system/cellSysutil.c of ps3recomp v0.19.0, ae92407, read there), because the
// game reads the same value and port and game must agree:
//   - PS3_LANG set and not empty: a number read by strtol over the WHOLE string (so "005" is 5, "3x" is not a number), taken if 0..19 (the toolkit's
//     CELL_SYSUTIL_LANG_TURKISH); else the code list matched EXACTLY and case-insensitively (ja en en-us fr es de it nl pt pt-pt ru ko zh-t zh-s fi sv da
//     no pl pt-br en-gb tr: "es-MX" and "fr_FR.UTF-8" are NOT in it, they give English). It never falls through to the host language.
//   - else Windows: the primary language id of GetUserDefaultUILanguage().
//   - else POSIX: LC_ALL, LC_MESSAGES, LANG in this order; a value that is empty, "C", "POSIX" or starts with the language letters c or pos
//     (C.UTF-8, POSIX.UTF-8) is skipped and the next variable is read; the first other value decides (its first letters up to _ - . @, at most 3).
//   - else English. A language the port has no text for (ja, ru, en-gb, ...) is English here too.
// shown() then maps it to what the disc displays (a US disc has en, fr and es only; de and it show English there).
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

// Texture hashes of the Main Menu pictures the port replaces, per disc and language: FNV-1a (32 bit) over the first w*h bytes of the DXT5 texture as the
// game uploads it. lab[k][0] = the row label k in its normal state (menu_off_tx_00k), lab[k][1] = highlighted (menu_on_tx_00k), k = 0 J-Adventure, 1 Victory Road,
// 2 Free Battle, 3 Online Battle, 4 J-Customize, 5 J-Point Shop, 6 Gallery, 7 Options, 8 Arcade; the title is the 512 x 64 picture of main_title_<pack>.srd,
// the mark the 1024 x 128 of main_pl_<pack>.srd. A table with title 0 is absent (the disc has no pack for that language: the game shows English).
// Measured from the disc files (progress CPK, scene packs progress/mainmenu/progress_scene_mainmenu_<pack>_{m,v}.pak): the US English set equals the hashes the port had measured in a run (all 20), which
// is how "the uploaded bytes are the file bytes" was shown; the other sets use the same loader (ASSUMPTION, not yet logged in a run: MP_LOG=1 prints
// "[mpup] label ... fnv" per upload).
struct Hashes { uint32_t lab[9][2]; uint32_t title, mark; };
static const Hashes k_hashes_us[5] = {   // US disc, packs en=US es=NS fr=FR
    { { { 0xF4B325B5u, 0x3F2447E9u }, { 0x3279759Au, 0x818DE97Fu }, { 0x9AD8290Fu, 0x43F7E08Eu }, { 0x6C080904u, 0x17F0DF08u }, { 0xF7AC5B9Fu, 0xABEFBBDEu }, { 0xB51E68F3u, 0x1094E6D6u }, { 0x4EF0C670u, 0x8409F6A1u }, { 0x4D537C25u, 0xB2D58678u }, { 0xFDC7E81Fu, 0x167EE758u } }, 0x31EB9C37u, 0x4D99F9AAu },   // en
    { { { 0x6A0FDAF9u, 0x56382323u }, { 0x6A5AFFF5u, 0xB44250FAu }, { 0x3178CC47u, 0x5F4B180Fu }, { 0x1D527721u, 0x12C608DDu }, { 0x13AA1A16u, 0x2D92E638u }, { 0x82344247u, 0x682B66EBu }, { 0xD8B28641u, 0x21E5CDF6u }, { 0xBE7EBE93u, 0x54BB4AA7u }, { 0x1F53F676u, 0x4485D72Bu } }, 0xDEB29747u, 0xE7F2BF22u },   // es
    { { { 0xC1FC75A9u, 0x8CF10586u }, { 0x830DB98Bu, 0x22ABE59Fu }, { 0xEA1F5271u, 0xAD5EB99Fu }, { 0xD4957139u, 0xF4CBC520u }, { 0x3D375F85u, 0xC29D0C8Eu }, { 0xE1F140B7u, 0x3EE06147u }, { 0x3D0875F1u, 0xB3447AE3u }, { 0xF9C12DABu, 0xE7517319u }, { 0x1F53F676u, 0x4485D72Bu } }, 0xEC613636u, 0x9DFC82F4u },   // fr
    {},   // de: no pack on this disc (the game shows English)
    {},   // it: no pack on this disc (the game shows English)
};
static const Hashes k_hashes_eu[5] = {   // EU disc, packs en=US es=SP fr=FR de=DE it=IT
    { { { 0xF4B325B5u, 0x3F2447E9u }, { 0x3279759Au, 0x818DE97Fu }, { 0x9AD8290Fu, 0x43F7E08Eu }, { 0x6C080904u, 0x17F0DF08u }, { 0xF7AC5B9Fu, 0xABEFBBDEu }, { 0xB51E68F3u, 0x1094E6D6u }, { 0x4EF0C670u, 0x8409F6A1u }, { 0x4D537C25u, 0xB2D58678u }, { 0xFDC7E81Fu, 0x167EE758u } }, 0x31EB9C37u, 0x4D99F9AAu },   // en
    { { { 0x6A0FDAF9u, 0x56382323u }, { 0x6A5AFFF5u, 0xB44250FAu }, { 0x3178CC47u, 0x5F4B180Fu }, { 0x1D527721u, 0x12C608DDu }, { 0x0C591C39u, 0x75E59CEFu }, { 0x82344247u, 0x682B66EBu }, { 0xD8B28641u, 0x21E5CDF6u }, { 0xBE7EBE93u, 0x54BB4AA7u }, { 0x1F53F676u, 0x4485D72Bu } }, 0xDEB29747u, 0xE7F2BF22u },   // es
    { { { 0xC1FC75A9u, 0x8CF10586u }, { 0x830DB98Bu, 0x22ABE59Fu }, { 0xEA1F5271u, 0xAD5EB99Fu }, { 0xD4957139u, 0xF4CBC520u }, { 0x3D375F85u, 0xC29D0C8Eu }, { 0xE1F140B7u, 0x3EE06147u }, { 0x3D0875F1u, 0xB3447AE3u }, { 0xF9C12DABu, 0xE7517319u }, { 0x1F53F676u, 0x4485D72Bu } }, 0xEC613636u, 0x9DFC82F4u },   // fr
    { { { 0xDB6E5BD4u, 0xB8361DB1u }, { 0xA82B40F0u, 0x8877D3EFu }, { 0xBDBEE873u, 0x849B6718u }, { 0x58D15CC3u, 0xB9985CC0u }, { 0x56701D20u, 0xF045443Cu }, { 0x42CCA838u, 0x85F8CFC8u }, { 0x3D0875F1u, 0xB3447AE3u }, { 0xAAA2A382u, 0xBD781BE8u }, { 0x1F53F676u, 0x4485D72Bu } }, 0x374DCA71u, 0xE39AB2C4u },   // de
    { { { 0x8831FC1Bu, 0xDF716ECEu }, { 0x752A81A4u, 0x4BBD59C0u }, { 0xF1CFB3DFu, 0xA7A74EC3u }, { 0xB743EC28u, 0x9362E44Bu }, { 0x0F139C13u, 0x6878834Eu }, { 0xE08306C6u, 0xDCB15CBBu }, { 0x26E952ABu, 0x0AB45B5Cu }, { 0xD6FD0948u, 0x98A490E8u }, { 0xAE088AC8u, 0xC28AD462u } }, 0x791D72DAu, 0xC2C89898u },   // it
};
enum Disc { DISC_UNKNOWN = -1, DISC_OTHER = 0, DISC_US = 1, DISC_EU = 2 };

// Help-line strings: the game's nine row help texts lie in one block of the resident pack (progress/resident/progress_resident_<pack>_m.pak, decoded, from
// 0x6330: UTF-16LE strings with a BOM, each on a 16-byte boundary), and the port finds the block from the game's own pointer minus the row's offset. The
// offsets differ per pack because the strings differ in length: help_us / help_eu[lang][row] = offset of main-menu row `row` from row 0 (the same row order
// as lab[] above). Read from the disc files: the 9 strings are the 11 strings of the block at fixed positions 0, 3, 10, 1, 2, 4, 5, 6, 7 (ASSUMPTION for
// de, it and the EU es: the order is the US one, spot-checked by reading the first words of rows 0 to 3; fr and the US es agree with an independent
// reading). A row with offset 0 in column 1 means no table.
static const uint32_t k_help_us[5][9] = {
    { 0x0, 0xF0, 0x3F0, 0x50, 0xC0, 0x140, 0x1D0, 0x250, 0x2B0 },   // en (US)
    { 0x0, 0xF0, 0x460, 0x50, 0xC0, 0x140, 0x1E0, 0x260, 0x2E0 },   // es (NS)
    { 0x0, 0x120, 0x4F0, 0x60, 0xF0, 0x180, 0x230, 0x2C0, 0x350 },   // fr
    { 0 }, { 0 } };
static const uint32_t k_help_eu[5][9] = {
    { 0x0, 0xF0, 0x3F0, 0x50, 0xC0, 0x140, 0x1D0, 0x250, 0x2B0 },   // en (US)
    { 0x0, 0xF0, 0x460, 0x50, 0xC0, 0x140, 0x1E0, 0x260, 0x2E0 },   // es (SP)
    { 0x0, 0x120, 0x4F0, 0x60, 0xF0, 0x180, 0x230, 0x2C0, 0x350 },   // fr
    { 0x0, 0x100, 0x4E0, 0x50, 0xC0, 0x160, 0x210, 0x2B0, 0x350 },   // de
    { 0x0, 0x110, 0x4C0, 0x50, 0xC0, 0x160, 0x210, 0x2A0, 0x300 } };   // it
// The offset tables that can apply to the language on the disc (an unknown disc: every disc's table of the language, the caller keeps the one whose strings validate).
inline std::vector<const uint32_t*> help_tables(int disc, const std::string& lang)
{
    std::vector<const uint32_t*> v;
    for (int i = 0; i < 5; i++) if (lang == k_langs[i]) {
        if ((disc == DISC_US || disc == DISC_UNKNOWN) && k_help_us[i][1]) v.push_back(k_help_us[i]);
        if ((disc == DISC_EU || disc == DISC_UNKNOWN) && k_help_eu[i][1]) v.push_back(k_help_eu[i]); }
    return v;
}
// the table of a disc (DISC_US, DISC_EU) and language, nullptr when there is none
inline const Hashes* hashes(int disc, const std::string& lang)
{
    const Hashes* t = disc == DISC_US ? k_hashes_us : disc == DISC_EU ? k_hashes_eu : nullptr;
    if (!t) return nullptr;
    for (int i = 0; i < 5; i++) if (lang == k_langs[i]) return t[i].title ? &t[i] : nullptr;
    return nullptr;
}
// has this disc hashes for the language (an unknown disc: any known disc has)
inline bool hashes_ready(int disc, const std::string& lang)
{
    if (disc == DISC_UNKNOWN) return hashes(DISC_US, lang) || hashes(DISC_EU, lang);
    return hashes(disc, lang) != nullptr;
}
// is this hash a row label of the language on the disc (an unknown disc: on any disc); gives the label k and state st (0 normal, 1 highlighted)
inline bool find_label(int disc, const std::string& lang, uint32_t f, int* k, int* st)
{
    for (int d = DISC_US; d <= DISC_EU; d++) { if (disc != DISC_UNKNOWN && disc != d) continue;
        const Hashes* h = hashes(d, lang); if (!h) continue;
        for (int i = 0; i < 9; i++) for (int s = 0; s < 2; s++) if (h->lab[i][s] == f) { *k = i; *st = s; return true; } }
    return false;
}
inline bool is_title(int disc, const std::string& lang, uint32_t f)
{ for (int d = DISC_US; d <= DISC_EU; d++) { if (disc != DISC_UNKNOWN && disc != d) continue; const Hashes* h = hashes(d, lang); if (h && h->title == f) return true; } return false; }
inline bool is_mark(int disc, const std::string& lang, uint32_t f)
{ for (int d = DISC_US; d <= DISC_EU; d++) { if (disc != DISC_UNKNOWN && disc != d) continue; const Hashes* h = hashes(d, lang); if (h && h->mark == f) return true; } return false; }
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

// Row labels, the page title and the watermark are pictures (feat_menu_pages.cpp mp_set), made from this table by installer/tools/mp_label_langs.py.
// Which languages have all their pictures (a bit per k_langs index): the port sets it from the picture files that are in the build
// (feat_menu_pages.cpp mp_register_sets); English only until then, so the host test sees the default.
inline unsigned& label_sets() { static unsigned m = 1; return m; }
inline bool labels_ready(const std::string& lang) { for (int i = 0; i < 5; i++) if (lang == k_langs[i]) return (label_sets() >> i) & 1u; return false; }

// code of a PS3 system language number (the PS3_LANG 0..19 order), "" when outside the five
inline std::string code_of_number(int n) { switch (n) { case 1: case 18: return "en"; case 2: return "fr"; case 3: return "es"; case 4: return "de"; case 5: return "it"; } return ""; }
// code of a Windows primary language id (GetUserDefaultUILanguage() & 0x3FF)
inline std::string code_of_winlang(int primary) { switch (primary) { case 0x09: return "en"; case 0x0C: return "fr"; case 0x0A: return "es"; case 0x07: return "de"; case 0x10: return "it"; } return ""; }
// the language letters of a locale text, as the toolkit's sysutil_locale_code() reads them: up to 3 letters before _ - . or @, lower-cased
inline std::string locale_letters(const char* loc)
{
    std::string ll;
    for (size_t i = 0; i < 3 && loc[i] && loc[i] != '_' && loc[i] != '-' && loc[i] != '.' && loc[i] != '@'; i++)
        ll += (char)((loc[i] >= 'A' && loc[i] <= 'Z') ? loc[i] + 32 : loc[i]);
    return ll;
}
// a POSIX locale variable the toolkit skips (no preference: the next variable is read): empty, "C", "POSIX", C.UTF-8, POSIX.UTF-8
inline bool locale_skipped(const char* loc)
{
    if (!loc || !*loc || !std::strcmp(loc, "C") || !std::strcmp(loc, "POSIX")) return true;
    const std::string ll = locale_letters(loc);
    return ll.empty() || ll == "c" || ll == "pos";
}
// the language of a locale text such as "es_ES.UTF-8", "fr-FR" or "de": its letters when they are one of the five; "" otherwise
inline std::string code_of_locale(const char* loc)
{
    if (locale_skipped(loc)) return "";
    const std::string ll = locale_letters(loc);
    for (const char* l : k_langs) if (ll == l) return ll;
    return "";
}
// a PS3_LANG code of the toolkit's list (exact, case-insensitive), as one of the five or ""; the other codes (ja nl pt ru ko ... en-gb) are English here
inline std::string code_of_ps3_lang(const char* s)
{
    static const char* const five[] = { "en", "en-us", "fr", "es", "de", "it" }; static const char* const to[] = { "en", "en", "fr", "es", "de", "it" };
    for (int i = 0; i < 6; i++) { const char *a = s, *b = five[i]; for (; *a && *b; a++, b++) { char x = *a, y = *b; if (x >= 'A' && x <= 'Z') x += 32; if (x != y) break; } if (!*a && !*b) return to[i]; }
    return "";
}
// The system language the game is told, by the toolkit's sysutil_language() (see the header comment for the rule): PS3_LANG, else the host UI language
// (win_primary: Windows primary language id, 0 = not Windows; lc_all, lc_messages, lang: the POSIX variables in this order), else English.
inline std::string pick(const char* ps3_lang, int win_primary, const char* lc_all = nullptr, const char* lc_messages = nullptr, const char* lang = nullptr)
{
    std::string r;
    if (ps3_lang && ps3_lang[0]) {
        char* end = nullptr; const long n = std::strtol(ps3_lang, &end, 10);
        if (end && *end == '\0' && n >= 0 && n <= 19) r = code_of_number((int)n);
        else r = code_of_ps3_lang(ps3_lang);
    } else if (win_primary) r = code_of_winlang(win_primary);
    else for (const char* v : { lc_all, lc_messages, lang }) if (!locale_skipped(v)) { r = code_of_locale(v); break; }
    return r.empty() ? "en" : r;
}
// the title id of a disc: DISC_US = a US disc (BLUS31519 or its PSN id NPUB31647), DISC_EU = the EU disc (BLES02136), DISC_OTHER = another known id
// (JP BLJS10258, Anison BLJS10257), DISC_UNKNOWN = anything else (no PARAM.SFO gives "BLES00000", empty, null; NPEB02204 is only the EU save-folder
// name, not a confirmed disc id, so it is unknown too and tries both tables)
inline int disc_kind(const char* title_id)
{
    if (!title_id) return DISC_UNKNOWN;
    const std::string t(title_id);
    if (t == "BLUS31519" || t == "NPUB31647") return DISC_US;
    if (t == "BLES02136") return DISC_EU;
    if (t == "BLJS10258" || t == "BLJS10257") return DISC_OTHER;
    return DISC_UNKNOWN;
}
// 1 = a US disc, 0 = another known disc, -1 = not known
inline int disc_us(const char* title_id) { const int k = disc_kind(title_id); return k == DISC_UNKNOWN ? -1 : k == DISC_US ? 1 : 0; }
// The language the disc SHOWS: the US disc has packs for en, fr and es only (it never asks for DE or IT: the review of the US disc), so de and it
// show English there; so they do when the disc is not known (only the English tables could apply for sure; if it was an EU disc the texture backstop
// turns the feature off), and on another known disc (JP: no tables, the feature is off there; ASSUMPTION: it has no de and it packs). The EU disc shows all
// five. With the feature off the port's pop-up windows still follow this language, each window in one language (dialog_lang).
inline std::string shown(const std::string& lang, int disc)
{
    if (disc != DISC_EU && (lang == "de" || lang == "it")) return "en";
    return lang;
}
// the window of the state the late switch-off puts back: the main list's words {count, cursor, visible, top, bottom} to the game's own 9 rows,
// the cursor on the given real row (clamped to 0..8)
inline void restore_list(uint32_t w[5], int real_row) { w[0] = 9; w[1] = (uint32_t)(real_row < 0 ? 0 : real_row > 8 ? 8 : real_row); w[2] = 9; w[3] = 0; w[4] = 8; }
// the raw cell of id in lang ("" when the language or id is unknown or the cell is empty)
inline std::string cell_of(Id id, const std::string& lang)
{
    int col = -1; for (int i = 0; i < 5; i++) if (lang == k_langs[i]) col = i;
    if (col < 0 || id < 0 || id >= N_IDS) return "";
    const char* p = k_table + 1; int row = 0;
    while (*p && row < id) { if (*p == '\n') row++; p++; }
    std::vector<std::string> cell(1); for (; *p && *p != '\n'; p++) { if (*p == '\t') cell.emplace_back(); else cell.back() += *p; }
    if (cell.size() != 6) return "";
    return cell[1 + col];
}
// the cell of id in lang ("" when the language or id is unknown); an empty cell falls back to English (per string, as the spec's fallback; complete() looks at the raw cells)
inline std::string text(Id id, const std::string& lang)
{
    bool known = false; for (const char* l : k_langs) if (lang == l) known = true;
    if (!known) return "";
    const std::string c = cell_of(id, lang);
    return c.empty() ? cell_of(id, "en") : c;
}
inline bool ascii_only(const std::string& t) { if (t.empty()) return false; for (unsigned char c : t) if (c > 0x7F) return false; return true; }
// The language of one pop-up window (a dialog of n texts): the toolkit's overlay font is ASCII only (menu_font.jmf), so when any cell of the window
// is not ASCII the whole window is English; never one window with two languages. Widen the font (the UI spec's font patch) to lift this.
inline std::string dialog_lang(const Id* ids, int n, const std::string& lang)
{
    for (int i = 0; i < n; i++) if (!ascii_only(cell_of(ids[i], lang))) return "en";
    return lang;
}
// The cell as a wide string for the port's pop-up windows; pass the language dialog_lang() chose for the window.
inline std::u16string text_overlay(Id id, const std::string& lang)
{
    const std::string t = text(id, lang);
    std::u16string o; for (unsigned char c : t) o += (char16_t)c;
    return o;
}
inline bool complete(const std::string& lang)
{
    for (int i = 0; i < N_IDS; i++) if (cell_of((Id)i, lang).empty()) return false;
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
// the disc gate, before the menu is set up: "" = on (a disc with tables, or a title id not known: the texture backstop then decides)
inline std::string gate_disc(int disc) { return disc == DISC_OTHER ? "this disc has no label hash tables (only the US and EU discs do)" : ""; }
// the gate: "" = on, else the reason (one log line). lang is the language the disc shows; disc as from disc_kind; textures_ok false = the backstop saw no label match
inline std::string gate(const std::string& lang, int disc, bool textures_ok)
{
    if (lang.empty()) return "text language is not one of en, es, fr, de, it";
    if (!complete(lang)) return "no complete text table for " + lang;
    if (!labels_ready(lang)) return "no row-label pictures for " + lang + " yet";
    if (!hashes_ready(disc, lang)) return "no label hashes for " + lang + " on this disc";
    if (!textures_ok) return "this disc's row-label textures matched none of the known ones";
    return "";
}
}  // namespace jml
#endif
