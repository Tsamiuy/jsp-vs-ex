// jsp_unlock.cpp -- "unlock all" cheat for J-Stars, off unless the environment variable JSP_UNLOCK_ALL is set (not 0).
//
// ENHANCEMENT (opt-in, off by default; the launcher sets it for one start): edits the game's LIVE in-memory save after the game has loaded and verified
// SYS-DATA; the game then writes it with its own checksum at its next save point (the port never rewrites the file).
//
// Save layout: docs/notes/CARDS.md (the card bit field); the other offsets are listed in the tables below.
//   Save manager S = *(u32*)0x0095B90C; the 350000-byte live buffer is *(u32*)S (the game works in it directly).
//   func_003F91E0(S, mode) is the SYS-DATA checksum; mode 0 is the verify called from @0x003FEB4C (return 1 = valid), mode 1 restamps.
//   Hook point and ordering: see save_update_wrap below. Not handled yet: a brand-new save (func_003FD628 init) is edited
//   from the second boot on, once a first save exists. The game's own debug unlock-all func_003FD568 is NOT used (it wipes progress),
//   and the per-item setters are not used (they fire trophy checks): the bytes are written directly.
//
// What it sets: all 276 card bits, J-Points (999999), the three gauges (0x29A90/94/98 = 36000), battle characters and
// supports, 156 titles, 134 player icons, deck slot sets and the J-Stars decks 02-05, and the three coin counts (99).
// Titles and icons are visible to online opponents.
#include <cstdio>
#include <cstdint>
#include <cstdlib>

#include "ps3emu/ps3types.h"
#include "ppu_recomp.h"

extern "C" void ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));

namespace {

typedef void (*fn_t)(ppu_context*);

const uint32_t k_save_mgr = 0x0095B90Cu;     // holds the address of the save manager S
const uint32_t k_off_jp_total = 0x29A88u;    // lifetime J-Points; game cap 999999
const uint32_t k_off_jp = 0x29A8Cu;          // spendable J-Points; game cap 999999
const uint32_t k_off_cards = 0x49810u;       // card bit field, bit N (LSB first) = card row N of F_CardName
const uint32_t k_card_rows = 476;            // rows 0-299: 5 per character, slots 0-3 named; rows 300-475: J-Adventure, slot 0 named
const uint32_t k_jp_cap = 999999u;

// Big-endian u32 fields, one row each. kSet = store value; kMax = raise to value; kOr = OR bits in (bit fields: owned flags only go up).
enum Op { kSet, kMax, kOr };
struct U32Edit { uint32_t off; uint32_t value; Op op; };
const U32Edit k_edits[] = {
    // J-Points
    {k_off_jp_total, k_jp_cap, kSet},
    {k_off_jp, k_jp_cap, kSet},
    // gauges Friendship/Effort/Triumph to the cap 36000 (top level; shop rows are gated by them)
    {0x29A90u, 36000u, kMax}, {0x29A94u, 36000u, kMax}, {0x29A98u, 36000u, kMax},
    // battle characters: ids 0-18, 22-41 (word 0 bits 0-18, 22-31; word 1 bits 0-9), supports 200-207, 209-213 (stored as id-136: word 2)
    {0x29A9Cu, 0xFFC7FFFFu, kOr}, {0x29AA0u, 0x000003FFu, kOr}, {0x29AA4u, 0x00003EFFu, kOr},
    // deck slot sets (Set A/B expansions): K+0x78+4*deck, bits 0-9, 5 decks
    {0x49888u, 0x3FFu, kOr}, {0x4988Cu, 0x3FFu, kOr}, {0x49890u, 0x3FFu, kOr}, {0x49894u, 0x3FFu, kOr}, {0x49898u, 0x3FFu, kOr},
    // Bronze/Silver/Gold coins (item index 11-13, cap 99)
    {0x29AD4u, 99u, kMax}, {0x29AD8u, 99u, kMax}, {0x29ADCu, 99u, kMax},
};
const uint32_t k_off_titles = 0x29B54u;      // 156 title bits, LSB-first bytes (setter func_003FA9B4: byte id>>3, bit id&7)
const uint32_t k_titles = 156;
const uint32_t k_off_icons = 0x29B7Du;       // 134 player-icon bits, same layout (setter func_003FA8E4)
const uint32_t k_icons = 134;
const uint32_t k_off_deck_flag = 0x4989Cu;   // +0x44*d, d=1..4: byte |= 0x04 (J-Stars Deck 02-05 bought)

bool card_named(uint32_t row) { return row < 300 ? (row % 5) <= 3 : (row % 5) == 0; }

void wr32be(uint32_t buf, uint32_t off, uint32_t v)
{
    for (int i = 0; i < 4; i++) vm_write8(buf + off + (uint32_t)i, (uint8_t)(v >> (24 - 8 * i)));
}

void set_cards(uint32_t buf)
{
    uint8_t bytes[(k_card_rows + 7) / 8];
    for (auto& b : bytes) b = 0;
    for (uint32_t r = 0; r < k_card_rows; r++) if (card_named(r)) bytes[r >> 3] |= (uint8_t)(1u << (r & 7));
    // keep bits of unnamed rows as they are (they are 0 in every save seen)
    for (uint32_t i = 0; i < sizeof bytes; i++) vm_write8(buf + k_off_cards + i, (uint8_t)(vm_read8(buf + k_off_cards + i) | bytes[i]));
}

uint32_t rd32be(uint32_t buf, uint32_t off)
{
    return (vm_read8(buf + off) << 24) | (vm_read8(buf + off + 1) << 16) | (vm_read8(buf + off + 2) << 8) | vm_read8(buf + off + 3);
}

void set_bits(uint32_t buf, uint32_t off, uint32_t count)   // OR in bits 0..count-1, LSB first
{
    for (uint32_t i = 0; i < count; i++) vm_write8(buf + off + (i >> 3), (uint8_t)(vm_read8(buf + off + (i >> 3)) | (1u << (i & 7))));
}

uint32_t count_bits(uint32_t buf, uint32_t off, uint32_t count)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; i++) n += (vm_read8(buf + off + (i >> 3)) >> (i & 7)) & 1u;
    return n;
}

void apply_edits(const char* why)
{
    const uint32_t s = vm_read32(k_save_mgr);
    const uint32_t buf = s ? vm_read32(s) : 0;
    if (!buf) { std::fprintf(stderr, "[unlock] %s: no live save yet\n", why); return; }
    for (const U32Edit& e : k_edits) {
        const uint32_t old = rd32be(buf, e.off);
        wr32be(buf, e.off, e.op == kSet ? e.value : e.op == kMax ? (old > e.value ? old : e.value) : (old | e.value));
    }
    set_cards(buf);
    set_bits(buf, k_off_titles, k_titles);
    set_bits(buf, k_off_icons, k_icons);
    for (uint32_t d = 1; d <= 4; d++) vm_write8(buf + k_off_deck_flag + 0x44u * d, (uint8_t)(vm_read8(buf + k_off_deck_flag + 0x44u * d) | 0x04u));
    std::fprintf(stderr, "[unlock] gauges %u/%u/%u chars %08X %08X supports %08X titles %u icons %u decks %02X %02X %02X %02X coins %u/%u/%u\n",
                 rd32be(buf, 0x29A90), rd32be(buf, 0x29A94), rd32be(buf, 0x29A98), rd32be(buf, 0x29A9C), rd32be(buf, 0x29AA0), rd32be(buf, 0x29AA4),
                 count_bits(buf, k_off_titles, k_titles), count_bits(buf, k_off_icons, k_icons),
                 vm_read8(buf + k_off_deck_flag + 0x44), vm_read8(buf + k_off_deck_flag + 0x88), vm_read8(buf + k_off_deck_flag + 0xCC), vm_read8(buf + k_off_deck_flag + 0x110),
                 rd32be(buf, 0x29AD4), rd32be(buf, 0x29AD8), rd32be(buf, 0x29ADC));
    uint32_t n = 0;
    for (uint32_t r = 0; r < k_card_rows; r++) n += (vm_read8(buf + k_off_cards + (r >> 3)) >> (r & 7)) & 1u;
    std::fprintf(stderr, "[unlock] %s: live save 0x%08X: %u card bits, J-Points %u\n", why, buf, n,
                 (vm_read8(buf + k_off_jp) << 24) | (vm_read8(buf + k_off_jp + 1) << 16) | (vm_read8(buf + k_off_jp + 2) << 8) | vm_read8(buf + k_off_jp + 3));
}

// The checksum function is only reached by direct calls inside the lift (a wrapper in the function table never sees
// them), so the hook sits on a function the table does call: func_0014E898, the save manager's update (OPD 0x00897F80
// in a vtable, no direct callers; it calls func_00400194, the request state machine, which runs the verify at
// @0x003FEB4C). After each update: when the card bits are not all set and the live buffer carries a valid checksum
// (func_003F91E0 mode 0 = what the game's own verify uses), the edits are written and the buffer is restamped
// (mode 1, what func_003FF850 does before every save). A buffer that fails the check (not loaded yet, half read, a
// corrupted file) is left alone. A later load of the file (START on the title) drops the edits and they are written
// again the same way; the file on disk is only ever written by the game's own save.
bool cards_all_set(uint32_t buf)
{
    for (uint32_t r = 0; r < k_card_rows; r++)
        if (card_named(r) && !((vm_read8(buf + k_off_cards + (r >> 3)) >> (r & 7)) & 1u)) return false;
    // markers beyond the cards: a save that has the cards but not the rest is edited again
    return rd32be(buf, 0x29A90) >= 36000u && count_bits(buf, k_off_titles, k_titles) == k_titles && (vm_read8(buf + k_off_deck_flag + 0x110) & 4);
}


fn_t g_orig_update;
void save_update_wrap(ppu_context* c)
{
    g_orig_update(c);
    const uint32_t s = vm_read32(k_save_mgr);
    const uint32_t buf = s ? vm_read32(s) : 0;
    if (!buf || cards_all_set(buf)) return;
    ppu_context t = *c;
    t.gpr[3] = s; t.gpr[4] = 0;
    func_003F91E0(&t);
    if ((uint32_t)t.gpr[3] != 1u) return;   // not a valid loaded save
    apply_edits("after load");
    t = *c;
    t.gpr[3] = s; t.gpr[4] = 1;
    func_003F91E0(&t);                      // restamp
}

}  // namespace

extern "C" void jsp_register_unlock_overrides(void)
{
    const char* e = std::getenv("JSP_UNLOCK_ALL");
    if (!e || !*e || e[0] == '0') return;
    for (uint64_t i = 0; i < function_table_count; i++)
        if (function_table[i].addr == 0x0014E898u) {
            g_orig_update = function_table[i].func;
            ppu_register_function(0x0014E898u, save_update_wrap);
            std::fprintf(stderr, "[unlock] JSP_UNLOCK_ALL set: everything is unlocked in the live save after load\n");
            return;
        }
    std::fprintf(stderr, "[unlock] no lifted function at 0014E898\n");
}
