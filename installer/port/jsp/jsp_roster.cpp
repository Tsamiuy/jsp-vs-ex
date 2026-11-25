// jsp_roster.cpp -- guest side of the "roster" interface of the mods folder: extra fighters on the character select.
//
// ENHANCEMENT (opt-in: nothing here runs unless an applied mod in the mods folder carries a roster section, and only on the US 1.00 disc,
// BLUS31519). The rules (which ids, where they go, the cap, the digest) are pure code in jsp_mods_core.h; this file only puts the result into
// the running guest. It is written from the lift and the notes in docs, with these facts of the US 1.00 image (TOC base r2 = 0x008D3888):
//
//   * The select screen's fighter list is one array of 32-bit ids ending in -1, at 0x00756C64 (52 ids). Two TOC entries point at it, one at the
//     start (r2-12668) and one at start+4 (r2-12672). Only the grid builder (func_001857D0) and the random pick (func_001882D4) read it through
//     them. So a longer list is a copy in free guest memory with both TOC entries re-pointed; the original array is never edited.
//   * The grid is 4 cells a row, 16 rows, inside the static select object at 0x009540E8. It uses ceil(n/4) rows for the n ids, one more for the
//     random card, rounded up to an even count (a page is 2 rows). The random pick keeps its ids in a stack buffer of 56 words: the list is
//     capped at 56 ids (jsp_mods_core.h kRosterGrid).
//   * The intro's series-logo switch (func_00127A94) takes the fighter id from the intro object at +0x60. An added fighter has no logo of its
//     own, so a mod may name a stock fighter whose logo it borrows: the id is swapped for the length of the call and put back.
//   * "Owned" is a bit field of three big-endian words at +0x29A9C of the live save (bit = id % 32 of word id / 32); the live save buffer is
//     found from the save manager pointer at 0x0095B90C. After the game's save update (func_0014E898) the bits of the owned fighters are
//     set, between the game's own checksum verify and restamp (func_003F91E0 with r4 = 0 / 1), the same way jsp_unlock.cpp edits the save.
//
// Safety: every expectation is checked before anything changes (both TOC entries, the whole stock list and its terminator, the code word the
// logo switch relies on, the free area). If one fails, or the copy is found damaged later, the TOC entries go back to the stock array first,
// nothing further is registered, a hook that is already in place falls back to calling straight through (one flag gates them all), and the
// mod set is reported as not run as listed (online refused, jsp_mods_roster_failed).
//   * The select card is a texture looked up BY NAME in the shared card set: the face setters (func_001980C8, func_00197E68) format
//     "_rgb8_256_chara_ico_%03d.png" and call func_0051C0D8(set, name), a tail stub that sets r4 = 2 and jumps to the name lookup
//     func_0051BDC8(set, kind, name) @0x0051BDC8 (r3 set, r4 kind, r5 name; the texture or 0 in r3). An added id has only a 64x55 stub
//     of its own in that set, so "card_as" (jsp_mods_core.h) redirects the name: the lookup's entry hook (lift_game_hooks.json,
//     jsp_card_as_enter) rewrites the three digits of exactly an added id's name to the donor's in the caller's name buffer, and
//     jsp_card_as_leave puts them back. The hook is on func_0051BDC8 and not on func_0051C0D8 itself because that stub tail-calls
//     (the lifter refuses a hook around a tail call) and it has about a hundred direct callers, which a table hook would not see.
//     Nothing else is touched: no archive, no other name.
//     LIMITS: the hook sees lookups by NAME only. func_001CF87C stores precomputed name hashes (including 019..021), so a screen that looks
//     a card up by hash skips it: only the character select grid is meant, other screens are unverified. ASSUMPTION: the donor's card is in the
//     shared set on every screen that shows the grid; a per-id pak (chara_256_ico_%03d_ALL_m.pak, func_0043521C, no static caller seen) might
//     mean the donor's card is not in the set on some screen. Both go on the GPU-run checklist.
// ASSUMPTION: the free areas below are never used by the game or the runtime: no pointer in the image refers to them and the runtime
// allocators keep elsewhere (checked by reading the image and the allocator ranges, not by a memory trace).
// A fight with an added fighter is untested: nothing here serves model, voice or card data.
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "ps3emu/ps3types.h"
#include "ppu_recomp.h"
#include "jsp_hook_chain.h"
#include "jsp_mods_core.h"   // pure rules (the card name redirect)

extern "C" void ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));
extern "C" int jsp_mods_roster_fighters(int* id, int* logo_as, int* owned, int cap);   /* jsp_mods.cpp: the applied mods' fighters */
extern "C" int jsp_mods_roster_cards(int* id, int* card_as, int cap);                   /* the added ids that borrow a stock card */
extern "C" int jsp_mods_roster_list(uint32_t* out, int cap);                            /* the final list, no terminator; 0 = does not fit */
extern "C" int jsp_mods_roster_stock(uint32_t* out, int cap);                           /* the stock list, no terminator */
extern "C" void jsp_mods_roster_failed(const char* why);                                /* the set no longer runs as listed */
extern "C" int jsp_unlock_owns_save_hook(void);                                         /* jsp_unlock.cpp: it wraps the save update itself */

namespace {

typedef void (*fn_t)(ppu_context*);

constexpr uint32_t kToc = 0x008D3888u;
constexpr uint32_t kSlotStart = kToc - 12668u;       // TOC entry holding the list start
constexpr uint32_t kSlotStartPlus4 = kToc - 12672u;  // TOC entry holding the list start + 4
constexpr uint32_t kStockList = 0x00756C64u;
constexpr uint32_t kCopyA = 0x0086FE00u;             // between the end of the code (0x00864BE8) and the data (0x00870000)
constexpr uint32_t kCopyB = 0x0097FE00u;             // past the end of the data (0x00978F38), same 64 KiB page
constexpr uint32_t kCopyBytes = 0x100u;              // room for 56 ids and the terminator (228 bytes)
constexpr int kMaxIds = 56;
constexpr uint32_t kLogoSwitch = 0x00127A94u;
constexpr uint32_t kLogoIdLoad = 0x00127AD8u;        // the load of the fighter id from the intro object; its word is checked
constexpr uint32_t kLogoIdLoadWord = 0x801F0060u;
constexpr uint32_t kLogoIdOffset = 0x60u;
constexpr uint32_t kSaveUpdate = 0x0014E898u;
constexpr uint32_t kSaveMgr = 0x0095B90Cu;
constexpr uint32_t kOwnedOffset = 0x29A9Cu;          // three big-endian words: ids 0..63 are fighters; ids 64 and up would hit the support bits (supports 200+ are stored as id - 136 in the third word)
constexpr int kOwnedWords = 3;

// func_0051BDC8(set, kind, name) @0x0051BDC8 is the entry the card hook is on (lift_game_hooks.json). Whether the lift has the hook cannot be
// checked here (the function exists with or without it and the lift leaves no runtime marker): a lift made before the hook just does not redirect.
constexpr uint32_t kCardKind = 2;                    // r4 of func_0051C0D8's tail call: the textures of a set

struct Added { int id, logo_as, owned; };
Added g_added[8];
int g_added_n = 0;
int g_card_n = 0;               // set only once the roster is in place (g_live); 0 = the hook returns at once
uint32_t g_copy = 0;            // where the list copy lives
uint32_t g_want[kMaxIds + 1];   // the copy as it was written, terminator included
int g_want_n = 0;
bool g_live = false;            // false after a failure: every hook then just calls through
fn_t g_orig_logo = nullptr;
fn_t g_orig_update = nullptr;

uint32_t be32(uint32_t a)
{
    return ((uint32_t)vm_read8(a) << 24) | ((uint32_t)vm_read8(a + 1) << 16) | ((uint32_t)vm_read8(a + 2) << 8) | vm_read8(a + 3);
}
void put_be32(uint32_t a, uint32_t v)
{
    for (uint32_t i = 0; i < 4; ++i) vm_write8(a + i, (uint8_t)(v >> (24 - 8 * i)));
}
bool all_zero(uint32_t a, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i) if (vm_read8(a + i)) return false;
    return true;
}
bool has_function(uint32_t addr)
{
    for (uint64_t i = 0; i < function_table_count; ++i) if (function_table[i].addr == addr) return true;
    return false;
}

// Put both TOC entries back on the stock array (used before giving up).
void point_at_stock()
{
    put_be32(kSlotStart, kStockList);
    put_be32(kSlotStartPlus4, kStockList + 4u);
}

void give_up(const char* why)
{
    if (g_copy) point_at_stock();
    g_live = false;
    std::fprintf(stderr, "[roster] given up: %s\n", why);
    jsp_mods_roster_failed(why);
}

// The copy must still hold exactly what was written, terminator included, and both TOC entries must still point at it.
bool copy_intact()
{
    if (be32(kSlotStart) != g_copy || be32(kSlotStartPlus4) != g_copy + 4u) return false;
    for (int i = 0; i < g_want_n; ++i) if (be32(g_copy + 4u * (uint32_t)i) != g_want[i]) return false;
    return true;
}

// The id word is swapped only for the length of the call: the intro object is the game's own.
void logo_wrap(ppu_context* c)
{
    if (!g_live) { g_orig_logo(c); return; }
    const uint32_t obj = (uint32_t)c->gpr[3];
    const uint32_t id = be32(obj + kLogoIdOffset);
    int use = -1;
    for (int i = 0; i < g_added_n; ++i) if (g_added[i].logo_as >= 0 && (uint32_t)g_added[i].id == id) use = g_added[i].logo_as;
    if (use < 0) { g_orig_logo(c); return; }
    put_be32(obj + kLogoIdOffset, (uint32_t)use);
    g_orig_logo(c);
    put_be32(obj + kLogoIdOffset, id);
}

// Runs right after every save update. Checks the copy, then marks the owned fighters in the live save when it holds a valid save.
void owned_after_update(ppu_context* c)
{
    if (!g_live) return;
    if (!copy_intact()) { give_up("the list copy in guest memory was changed"); return; }
    const uint32_t mgr = vm_read32(kSaveMgr);
    const uint32_t buf = mgr ? vm_read32(mgr) : 0;
    if (!buf) return;
    uint32_t mask[kOwnedWords] = {};
    for (int i = 0; i < g_added_n; ++i)
        if (g_added[i].owned && g_added[i].id >= 0 && g_added[i].id < 64) mask[g_added[i].id / 32] |= 1u << (g_added[i].id % 32);
    bool missing = false;
    for (int w = 0; w < kOwnedWords; ++w) if ((be32(buf + kOwnedOffset + 4u * (uint32_t)w) & mask[w]) != mask[w]) missing = true;
    if (!missing) return;
    ppu_context t = *c;
    t.gpr[3] = mgr; t.gpr[4] = 0;
    func_003F91E0(&t);
    if ((uint32_t)t.gpr[3] != 1u) return;   // not a valid loaded save (not read yet, half read, damaged): leave it alone
    for (int w = 0; w < kOwnedWords; ++w) put_be32(buf + kOwnedOffset + 4u * (uint32_t)w, be32(buf + kOwnedOffset + 4u * (uint32_t)w) | mask[w]);
    t = *c;
    t.gpr[3] = mgr; t.gpr[4] = 1;
    func_003F91E0(&t);                      // restamp the checksum
    std::fprintf(stderr, "[roster] owned bits set: %08X %08X %08X\n", be32(buf + kOwnedOffset), be32(buf + kOwnedOffset + 4), be32(buf + kOwnedOffset + 8));
}

void save_update_wrap(ppu_context* c)
{
    g_orig_update(c);
    owned_after_update(c);
}

// What the entry hook changed on this thread, for the leave hook (func_0051BDC8 does not recurse and does not switch fibers).
thread_local jmods::CardRedirect t_card;
int g_card_ids[8], g_card_donors[8];

}  // namespace

// func_0051BDC8 entry (lift_game_hooks.json): an added id's card name gets its donor's digits for the length of the lookup (the pure rule
// is jmods::card_redirect_enter in jsp_mods_core.h).
extern "C" void jsp_card_as_enter(ppu_context* c)
{
    t_card.at = 0;
    if (g_card_n <= 0 || !g_live || (uint32_t)c->gpr[4] != kCardKind) return;
    const uint32_t p = (uint32_t)c->gpr[5];
    if (p < 0x10000u || p >= 0xE0000000u) return;   // the guest's stacks are at 0xD0000000
    static bool said = false;
    if (jmods::card_redirect_enter([](uint32_t a) { return (uint8_t)vm_read8(a); }, [](uint32_t a, uint8_t v) { vm_write8(a, v); },
                                   p, g_card_ids, g_card_donors, g_card_n, t_card) && !said) {
        said = true;
        std::fprintf(stderr, "[roster] card_as: first redirected card lookup (lr %08X)\n", (uint32_t)c->lr);
    }
}
extern "C" void jsp_card_as_leave(ppu_context*)
{
    jmods::card_redirect_leave([](uint32_t a, uint8_t v) { vm_write8(a, v); }, t_card);
}

// The unlock cheat's wrapper calls this itself when it owns the save-update hook (one registration either way).
extern "C" void jsp_roster_after_save_update(ppu_context* c) { owned_after_update(c); }

extern "C" void jsp_register_roster_overrides(void)
{
    int id[8], logo[8], owned[8];
    g_added_n = jsp_mods_roster_fighters(id, logo, owned, 8);
    if (g_added_n <= 0) return;   // no roster mod applied: the stock game, nothing is touched
    for (int i = 0; i < g_added_n; ++i) g_added[i] = { id[i], logo[i], owned[i] };

    // 1. what the mods asked for
    uint32_t list[kMaxIds + 1];
    const int n = jsp_mods_roster_list(list, kMaxIds);
    uint32_t stock[kMaxIds + 1];
    const int ns = jsp_mods_roster_stock(stock, kMaxIds);
    if (n <= 0 || ns != 52) { give_up("no list could be built"); return; }

    // 2. the guest is the US 1.00 image we know
    if (be32(kSlotStart) != kStockList || be32(kSlotStartPlus4) != kStockList + 4u) { give_up("the list pointers are not the stock ones"); return; }
    for (int i = 0; i < ns; ++i) if (be32(kStockList + 4u * (uint32_t)i) != stock[i]) { give_up("the stock fighter list differs"); return; }
    if (be32(kStockList + 4u * (uint32_t)ns) != 0xFFFFFFFFu) { give_up("the stock fighter list has no terminator where expected"); return; }
    if (be32(kLogoIdLoad) != kLogoIdLoadWord) { give_up("the intro logo switch is not the expected code"); return; }
    if (!has_function(kLogoSwitch) || !has_function(kSaveUpdate)) { give_up("a function to hook is not in the lift"); return; }
    uint32_t where = 0;
    if (all_zero(kCopyA, kCopyBytes)) where = kCopyA;
    else if (all_zero(kCopyB, kCopyBytes)) where = kCopyB;
    if (!where) { give_up("no free guest memory for the list copy"); return; }
    const bool want_logo = [] { for (int i = 0; i < g_added_n; ++i) if (g_added[i].logo_as >= 0) return true; return false; }();
    const bool save_hook_ours = !jsp_unlock_owns_save_hook();

    // 3. write the copy, read it back, then re-point
    g_want_n = n + 1;
    for (int i = 0; i < n; ++i) g_want[i] = list[i];
    g_want[n] = 0xFFFFFFFFu;
    for (int i = 0; i < g_want_n; ++i) put_be32(where + 4u * (uint32_t)i, g_want[i]);
    for (int i = 0; i < g_want_n; ++i) if (be32(where + 4u * (uint32_t)i) != g_want[i]) { give_up("the list copy did not read back"); return; }
    g_copy = where;
    put_be32(kSlotStart, where);
    put_be32(kSlotStartPlus4, where + 4u);
    g_live = true;

    // 4. hooks (each only if its job is needed); a hook that cannot be installed undoes everything
    if (want_logo && !jsp_hook_chain::install<fn_t>(kLogoSwitch, logo_wrap, &g_orig_logo)) { give_up("cannot hook the intro logo switch"); return; }
    if (save_hook_ours && !jsp_hook_chain::install<fn_t>(kSaveUpdate, save_update_wrap, &g_orig_update)) {
        give_up("cannot hook the save update: the owned bits cannot be set");
        return;
    }
    {   // the card redirect: it needs the lifted entry hook, which only a lift made with lift_game_hooks.json has
        const int nc = jsp_mods_roster_cards(g_card_ids, g_card_donors, 8);
        g_card_n = nc;
        if (nc > 0) std::fprintf(stderr, "[roster] card_as: %d added id(s) borrow a stock card through the func_0051BDC8 entry hook\n", nc);
    }
    std::fprintf(stderr, "[roster] %d ids at 0x%08X (stock %d), %d added fighter(s), logo hook %d, save hook %s\n", n, where, ns, g_added_n,
                 want_logo ? 1 : 0, save_hook_ours ? "own" : "shared with the unlock cheat");
}
