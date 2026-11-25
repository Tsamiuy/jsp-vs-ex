# Mods

A mod is a folder in the port's mods folder with a `mod.json` and the files it names. The mods folder's list (`mods.txt`) says which mods load, in order. The rules for `mod.json` (schema 1) are in `installer/port/jsp/jsp_mods_core.h` (`parse_manifest`): an id of `[a-z0-9-]` that equals the folder name, a version, `interfaces` (`cpk-entry` 1, `roster` 1 or 2), and overrides of entries of the game's archives, each by a file with its SHA-256 or by an alias. A mod that breaks any rule is refused whole; the other mods still load.

A mod that only changes logos, movies or sound is cosmetic. Every other mod is gameplay, and online play compares the gameplay mods of both players (see [ONLINE.md](ONLINE.md)).

## Getting a mod verified

Verification checks that a mod follows the rules above and is safe to list. It does not make the mod part of the port.

1. Put the mod in a zip. The port opens `<id>/mod.json` in the mods folder, so the zip holds one top-level folder named exactly like the mod id, with `mod.json` and the files inside it. A zip with `mod.json` and the files at its top level is also accepted: whoever installs it must unpack it into a folder named like the id. Upload it somewhere with an HTTPS link that stays the same (a GitHub release asset works).
2. Open a "Mod verification" issue and fill in the form. Paste your `mod.json`.
3. A maintainer adds the label `mod-check`. The checks run on GitHub and post one comment: the link that was checked, a table with pass or fail for each check (the download's size and SHA-256 are in it), the canonical hash of your mod and the list of its files. Nothing from the mod is run, and nothing is copied or stored.
4. To check a changed mod, edit the issue (keep it to the same mod id) and ask a maintainer to remove the label and add it again. The issue text is read when the label is added.
5. A maintainer plays the mod. They install it from the link and the hashes in the check comment, not from the issue text, which the author can change after the label is added. If it works, a pull request adds it to `mods/tested.json` with its canonical hash. The automated checks alone never put a mod on the list.

What the checks refuse:

- a download that is not HTTPS, is over 200 MB, or redirects to a non-HTTPS or non-public address. The checks resolve each address once and connect to that address, with the certificate checked against the host name. The checks stop after about 280 s in all; if they stop early, the comment says so and nothing is verified;
- a zip with a folder name other than the mod id, more than 2000 entries or more than 500 MB unpacked, an encrypted entry, a symbolic link, a NUL in a name, a non-ASCII name that the zip does not mark as UTF-8, a file whose path is also a folder of another file, an absolute path, a `..` part, a backslash, a name Windows cannot use (a device name, a trailing dot or space, or one of `\ : < > " | ? *`), the same path twice or two paths that differ only in case (compared case-folded);
- common program, script and archive types, by extension (`.exe .dll .bat .cmd .ps1 .py .sh .so .js .hta .reg .zip .7z .rar .tar .gz .iso .html` and more) and by the first bytes of a file (the first 1 MB of each file is read for this; a Windows program: `MZ` with a `PE` header at the offset in bytes 0x3C-0x3F; Linux and macOS programs, a script `#!`, zip, gzip, xz, 7z, RAR, cabinet and WebAssembly files). This is a list of common types, not every possible one; a maintainer can override a content hit after looking at the file. Also refused: source files and paths in the port's source tree (`port/`, `installer/`, `tools/`, `third_party/`, `CMakeLists.txt`, `.cpp .h` and other C and C++ files, patches);
- a `mod.json` that breaks the schema 1 rules, an override file that is missing or whose SHA-256 differs, an id or version that differs from the form, or a pasted `mod.json` that differs from the one in the zip;
- a mod that the form calls cosmetic while its manifest makes it gameplay.

The zip can also be checked on your own PC with `python3 -I tools/ci/mod_verify.py --zip FILE`: it prints the same table, the zip's SHA-256 and the canonical hash, so a maintainer can hash the exact zip they play.

Only the form and the zip are read. The checks cannot tell whether a mod is good or whether a file is allowed to be shared: that is the author's confirmation and the maintainer's play-through.

## The tested list

`mods/tested.json` (schema 1) lists the mods a maintainer has played. Each entry has `id`, `name`, `version`, `sha256` (the canonical mod hash), `tested_on` (the port release, like `0.6.1`), `content` and `notes`. `content` is always `"own-work"`: the list never holds a mod with content that comes from the game's files or that belongs to someone else. The form's last box asks the author to confirm this, and the validator refuses any other value. Entries are sorted by id, then version; an id and version, and a hash, appear once. `tools/ci/check_tested_mods.py` checks this in CI. A pull request that adds an entry links the verification issue.

## Canonical mod hash

The canonical mod hash names the exact content of a mod, however it was zipped. The checks compute it; the port will compute it the same way. Version 1:

1. The files of the mod are all files under the mod folder (the zip's top level, or its one top-level folder), `mod.json` included. Folders are not files. Each file's path is relative to the mod folder, with `/` between parts, with no leading `/`, and it must pass `valid_rel_path` in `jsp_mods_core.h`. The path is used as it is: no case folding, no Unicode normalisation. A path with a non-ASCII character is accepted only when the zip marks the name as UTF-8 (general purpose flag bit 11); otherwise the name's encoding is not known and the mod is refused.
2. For each file compute `h`, the SHA-256 of its bytes as 64 lowercase hex digits.
3. Sort the files by the UTF-8 bytes of the path, byte by byte (so `B` comes before `a`).
4. Build the byte string: the ASCII text `jsp-mod-hash 1` and a line feed (`0x0A`), then for each file in that order `h`, one space (`0x20`), the path in UTF-8, and a line feed. No other bytes, no byte order mark, no carriage returns.
5. The canonical mod hash is the SHA-256 of that byte string, as 64 lowercase hex digits.

The port does not compute the hash yet; today only `tools/ci/mod_verify.py` does.

Order of entries, compression, timestamps, folder entries and the name of the top-level folder do not matter. Changing a byte of any file, a file name or adding or removing a file changes the hash.

Known answer: a mod whose files are `mod.json` containing `{}` and `game/logo/a.bin` containing `a` gives `ce2c86505349b1c37a35fc28a64aa9a5ddc4acac8b6a7e51962ab93e7f22a1ac`. The reference code is `canonical_hash()` in `tools/ci/mod_verify.py`; the port's code, when it is written, must give the same answer, and `tools/ci/test_mod_verify.py` holds the test.

## Fields beyond overrides

The port's framework also knows these fields, in the order they were added. The rules are pure code in `installer/port/jsp/jsp_mods_core.h` (host test
`installer/tools/mods_overlay_test.sh`); the guest side of each is named below. Any violation refuses the whole mod; the other mods still
load. Mods that change the fight are gameplay and are part of the mod-set digest that online play compares; cosmetic ones are not.

## roster: more fighters on the character select

Interface `roster` 1 (2 with `card_as`; a version 1 mod is still accepted, without that field), US 1.00 disc only (`game.title_ids` must name BLUS31519). `roster.fighters` is a list of objects:

| Field | Meaning |
| --- | --- |
| `id` | 19, 20 or 21, the three ids the game leaves unused (required) |
| `after` | the id it goes behind in the list (default: before the first support) |
| `logo_as` | a stock fighter id whose intro series logo this fighter shows |
| `owned` | whether the fighter is marked owned in the save (default true) |
| `card_as` (needs `roster` 2) | a stock-list id whose select card this fighter shows (default: none, the game's small placeholder card) |

The fighters are part of the digest, except `card_as`.

### card_as: the select card

The game looks a card up by texture name, `_rgb8_256_chara_ico_NNN.png`, in one shared card set. An added id has only a 64x55 stub of
its own in that set. With `card_as` the port rewrites the three digits of exactly that added id's name to the donor's for the length
of the lookup and puts them back. No archive is changed and nothing ships with the mod.

- The donor must be on the stock list (a stock fighter, or a support on it), not an added id and not the fighter itself.
- The redirect is an entry hook on the name lookup `func_0051BDC8` (`lift_game_hooks.json`, `jsp_card_as_enter` and `_leave` in
  `jsp_roster.cpp`). It only exists in a lift made with that hook list: a lift made before it does not redirect (no error); the first run
  hashes the hook list into the lift step, so the lift is made again.
- `card_as` is cosmetic: it is not in the digest, so a peer that does not redirect still plays the same fight. A card is not a model:
  the fighter's fight data, model and voice are still not served by this field.

### Open (needs a game run)

- That the select grid's cards come through this lookup, and that the donor's card shows for the added id (only read from the lift).
- That every stock-list id has a full card in the shared set (checked for 018 and 211).
