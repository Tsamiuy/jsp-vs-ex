# J-Stars cards

**On 2026-11-01 all 276 cards were unlocked on a test save**, as a check of the save format (see "How all cards were unlocked"). The tables below show that save as it was before, with 172 unlocked.

Counted on 2026-11-01 from a play-through save. The game has **276 cards** and the save shows **172 unlocked**.

| Group | Characters | In the game | Unlocked |
|---|---:|---:|---:|
| Fighters and their partners, normal cards | 44 | 176 | 124 |
| Support-only characters, normal cards | 16 | 64 | 48 |
| J-Adventure cards (A, B and C) | 12 | 36 | 0 |
| **Total** | **60** | **276** | **172** |

Every character has 4 normal cards, one each from ★ to ★★★★. The 12 characters marked below also have 3 J-Adventure cards, so they have 7 in all.

| Character | Type | Normal cards | Unlocked | J-Adventure cards |
|---|---|---:|---:|---:|
| Luffy | Fighter | 4 | 3 | 3 |
| Ace | Fighter | 4 | 2 | 3 |
| Hancock | Fighter | 4 | 3 | - |
| Akainu | Fighter | 4 | 3 | - |
| Naruto | Fighter | 4 | 2 | 3 |
| Sasuke | Fighter | 4 | 4 | - |
| Madara | Fighter | 4 | 3 | - |
| Goku | Fighter | 4 | 4 | 3 |
| Frieza | Fighter | 4 | 1 | - |
| Vegeta | Fighter | 4 | 3 | - |
| Yusuke | Fighter | 4 | 3 | 3 |
| Toguro | Fighter | 4 | 4 | - |
| Hiei | Fighter | 4 | 4 | 3 |
| Toriko | Fighter | 4 | 4 | 3 |
| Zebra | Fighter | 4 | 2 | 3 |
| Ichigo | Fighter | 4 | 1 | 3 |
| Aizen | Fighter | 4 | 3 | - |
| Rukia | Support | 4 | 3 | - |
| Gon | Fighter | 4 | 3 | 3 |
| Killua | Fighter | 4 | 3 | - |
| Hisoka | Support | 4 | 3 | - |
| Kenshin | Fighter | 4 | 2 | - |
| Shishio | Fighter | 4 | 3 | - |
| Seiya | Fighter | 4 | 2 | 3 |
| Kenshiro | Fighter | 4 | 2 | - |
| Raoh | Fighter | 4 | 1 | - |
| Oga | Fighter | 4 | 4 | 3 |
| Baby Beel | Partner of Oga | 4 | 3 | - |
| Gintoki | Fighter | 4 | 3 | - |
| Kagura | Support | 4 | 3 | - |
| Sadaharu | Support | 4 | 1 | - |
| Medaka | Fighter | 4 | 2 | - |
| Kumagawa | Support | 4 | 2 | - |
| Ryotsu | Fighter | 4 | 4 | - |
| Arale | Fighter | 4 | 2 | - |
| Gatchan | Partner of Arale | 4 | 2 | - |
| Koro-sensei | Fighter | 4 | 2 | - |
| Saiki | Fighter | 4 | 3 | - |
| Momotaro | Fighter | 4 | 4 | - |
| Edajima | Support | 4 | 4 | - |
| Bo-bobo | Fighter | 4 | 3 | - |
| Don Patch | Partner of Bo-bobo | 4 | 4 | - |
| Tsuna | Fighter | 4 | 2 | - |
| Reborn | Partner of Tsuna | 4 | 2 | - |
| Luckyman | Fighter | 4 | 4 | - |
| Taro Yamada | Fighter | 4 | 4 | - |
| Bookshop Biddy | Partner of Taro | 4 | 4 | - |
| Jonathan | Fighter | 4 | 3 | - |
| Joseph | Fighter | 4 | 1 | - |
| Nūbē | Fighter | 4 | 3 | - |
| Neuro | Support | 4 | 4 | - |
| Chitoge | Support | 4 | 2 | - |
| Jaguar | Support | 4 | 3 | - |
| Kuroko | Support | 4 | 2 | - |
| Bossun | Support | 4 | 4 | - |
| Himeko | Support | 4 | 3 | - |
| Switch | Support | 4 | 3 | - |
| Lala | Support | 4 | 3 | - |
| Allen | Support | 4 | 4 | - |
| Hinata | Support | 4 | 4 | - |

## Where the numbers come from

- **Total (reading):** the names are in `F_CardName_US.pak` in `progress_resident_US_m.pak`, with effects in `F_CustomCombi`. There are 476 slots and 276 are filled: rows 0-299 are normal cards, 5 slots per character with the fifth empty, and rows 300-475 are J-Adventure cards, 5 slots each with 4 empty. The art agrees: `customize_card_s_img_NNN_NNN` has 277 images, the extra one being 000_000.
- **Unlocked (tested):** counted by eye from J-Customize's Display list, 15 pages in run cd3. 14 full pages plus 4 cards make 172, which the per-character numbers add up to. All five decks were empty (run cd4), so no cards were out of the list. The save didn't change in any of these runs.
- **J-Adventure cards (tested):** none are set in the save's bit field, and the J-Adventure slot's list ends at Hinata on page 15 (run cd5).
- **Who is Support (reading):** the shop's support table has 13 rows (`01_support_character_jp_shop_PS3`). Kagura & Sadaharu and SKET-dan (Bossun, Himeko, Switch) share a row, which makes 16 card characters. Baby Beel, Gatchan and Reborn share a fighter row. Don Patch and Bookshop Biddy are assumed to be partners of Bo-bobo and Taro, since neither has a shop row (inferred).
- **Where the save stores cards (tested):** `SYS-DATA` offset 0x49810 is a bit field: bit N, counted from the lowest bit of each byte, is card row N in `F_CardName`. It matches all 60 per-character counts from the card list, and it adds up to the same 172. The bits for J-Adventure rows 300-475 are clear. The file also has a checksum in its first 4 bytes, so an edited save is reported as corrupted (run ms3), and the formula isn't found.
- **Shop (tested, runs ms2 to ms5):** Bronze Coins buy ★ and ★★ and the Bronze box shows Sold Out. Silver Coins buy ★★ and ★★★, and Gold's box shows no coins too. Coins are bought in a different tab, as described above. Spending a coin on a card was never tested. Writing coins to memory at 0x4091ACF8 and 0x4091CB58 didn't change the count shown.

## How all cards were unlocked

The save in memory sits at `0x408DA100`, so the card bits are at `0x40923910`. The port's debug `w32` set every named card row there, then one Bronze Coin was bought (500 JP) and the shop was left, so the game wrote the save with its own checksum.

- Buying coins: J-Point Shop, J-Adventure & J-Customize tab, bottom of the list. Right adds to the cart, Cross, Left to Yes (No is the default), Cross. The number after the slash is coins owned. The game saves on leaving the shop, not at the purchase.
- **Tested** (runs mu5, mu6, keyboard input): `SAVE complete` in the log, and all 276 bits set. After a reboot the save loads with no corrupted message, the list has 23 pages (23 x 12 = 276), and J-Adventure cards are on pages 21-23. Save was unchanged by that check.
- JP went down by 500, and Bronze Coins went up by 1. Those are the only intended changes beyond the cards; 66 bytes of the file differ, including the header's checksum and date, and the play time wasn't checked separately.
- **Not tested:** using the cards in a fight, or buying with Silver or Gold Coins.

## Missing normal cards

| Stars | Missing | Characters |
|---:|---:|---|
| ★★ | 15 | Ace, Akainu, Naruto, Frieza, Ichigo, Hisoka, Seiya, Raoh, Sadaharu, Medaka, Arale, Koro-sensei, Reborn, Joseph, Himeko |
| ★★★ | 25 | Luffy, Naruto, Madara, Frieza, Yusuke, Zebra, Ichigo, Aizen, Gon, Kenshin, Seiya, Kenshiro, Raoh, Sadaharu, Medaka, Kumagawa, Gatchan, Koro-sensei, Saiki, Tsuna, Reborn, Joseph, Chitoge, Kuroko, Lala |
| ★★★★ | 28 | Ace, Hancock, Frieza, Vegeta, Zebra, Ichigo, Rukia, Killua, Kenshin, Shishio, Kenshiro, Raoh, Baby Beel, Gintoki, Kagura, Sadaharu, Kumagawa, Arale, Gatchan, Bo-bobo, Tsuna, Jonathan, Joseph, Nūbē, Chitoge, Jaguar, Kuroko, Switch |

## Pictures

Screenshots of the card list were taken during the checks. They are not published in this repository.
