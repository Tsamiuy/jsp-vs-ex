# Mods

A mod is a folder in the port's mods folder with a `mod.json` and the files it names. The mods folder's list (`mods.txt`) says which mods load, in order. The rules for `mod.json` (schema 1) are in `installer/port/jsp/jsp_mods_core.h` (`parse_manifest`): an id of `[a-z0-9-]` that equals the folder name, a version, `interfaces` (`cpk-entry` 1, `roster` 1 or 2, `loose-file` 1), and overrides of entries of the game's archives, each by a file with its SHA-256 or by an alias. A mod that breaks any rule is refused whole; the other mods still load.

A mod that only changes logos, movies or songs (`sound/<LANG>/BGM_*`) is cosmetic; other sound files (voices, effects) are gameplay. Every other mod is gameplay, and online play compares the gameplay mods of both players (see [ONLINE.md](ONLINE.md)).

## Mod package standard (schema 1)

Every mod follows this standard. It describes what the port and the checks enforce today, not plans. **MUST** is a rule, **SHOULD** a strong recommendation, **MAY** a choice. Each MUST names what enforces it: a check of `tools/ci/mod_verify.py`, by the name in its result table (Archive, Layout, Paths, File types, Manifest, Kind of change), or a refusal by the port's loader (`installer/port/jsp/jsp_mods_core.h` and `jsp_mods.cpp`, logged as `[mods] <id>: refused: <reason>`), or `tools/ci/check_tested_mods.py`. A rule nothing checks yet says "not enforced yet". The loader refuses a mod whole; the other mods still load.

### Package

- A mod MUST be shared as one zip file (Archive: "not a readable zip file").
- The zip MUST hold either one top-level folder named exactly like the mod id, with `mod.json` directly inside it, or `mod.json` at its top level (Layout). Every file MUST be inside that mod folder (Layout: "a file outside the mod folder"). The folder form SHOULD be used: it unpacks straight into the mods folder.
- Installed, the mod MUST be at `mods\<id>\` in the install folder, with `mod.json` directly inside (loader: "no mod.json", "id ... is not the folder name").
- Limits (Archive): at most 2000 entries in the zip, at most 500 MB unpacked in all, no single file over 300 MB, `mod.json` at most 256 KB (the loader's own limit is 1 MiB), no encrypted entry. The zip MUST be at most 200 MB (Download in an issue check; `--zip` refuses a larger file).
- Paths (Paths): relative, with `/` between parts, at most 255 bytes; no drive letter, leading `/`, backslash, `..`, `.` or empty part; no part that ends in a dot or a space; none of `\ : < > " | ? *` or a control character; no Windows device name (`CON`, `PRN`, `AUX`, `NUL`, `COM0`-`COM9`, `LPT0`-`LPT9`, `CONIN$`, `CONOUT$`, with or without an extension); no symbolic link or other entry that is not a plain file or folder; no NUL; a non-ASCII name only when the zip marks it as UTF-8; each path once, and no two paths that differ only in case; no file whose path is also a folder of another file.
- Installed, a file of a mod MUST NOT be reached through a symbolic link or a junction (loader: "it is reached through a link or junction (not followed)").
- No programs, scripts or nested archives (File types). A file is refused by its extension: `.exe .dll .com .scr .msi .msix .appx .sys .ocx .cpl .pif .asi .so .dylib .jar .app .bat .cmd .ps1 .vbs .vbe .js .jse .wsf .hta .reg .lnk .url .py .pyc .sh .lua .php .pl .rb .wasm .html .htm`, and the archives `.zip .7z .rar .tar .gz .tgz .xz .bz2 .cab .iso .apk .dmg`. It is also refused by its first bytes: a Windows program (`MZ` with a `PE` header at the offset in bytes 0x3C-0x3F), a Linux or macOS program, a script (`#!`), a zip, gzip, xz, 7z, RAR, cabinet or WebAssembly file. A maintainer can override a hit on the first bytes after looking at the file. Source and build files are refused too: `.c .cc .cpp .h .hpp .inc .patch .diff`, `CMakeLists.txt`, and any path in a `port/`, `installer/`, `tools/` or `third_party/` folder of the mod.
- There is no list of allowed types: any file that is not refused passes. A mod SHOULD hold only `mod.json`, the files it names, a README and a preview image.
- An installer or helper script is not part of the mod. Inside the mod folder it fails File types; beside the mod folder in the same zip it fails Layout. If an author ships one, it MUST be a separate download, and it MUST only copy the mod folder into `mods\<id>` and add the id to `mods.txt` (an uninstaller only takes them out again). Not enforced yet: nothing checks a separate download, and a maintainer never runs it.

### mod.json

`mod.json` MUST be UTF-8 (one leading byte order mark is allowed) and strict JSON (Manifest; loader): no comments, no trailing comma, no key twice in one object, no `\uD800`-`\uDFFF` escape, at most 32 levels deep.

| Field | Type | Required | Rule | Enforced by |
| --- | --- | --- | --- | --- |
| `schema` | integer | yes | exactly `1` | Manifest; loader |
| `id` | string | yes | 1 to 40 characters of `a-z`, `0-9` and `-`; equal to the mod folder's name | Manifest, Layout; loader |
| `version` | string | yes | 1 to 40 bytes, no space and no control character, for example `"1.0.0"` | Manifest; loader |
| `title` | string | no (SHOULD) | the mod's name for people | Manifest; loader (type only) |
| `game.title_ids` | array of strings | no (SHOULD; MUST name `BLUS31519` for `roster` and `loose`) | the disc ids the mod is made for: `BLUS31519` (US), `BLES02136` (EU). When present and not empty, the running disc must be in it | Manifest (types, roster and loose); loader: "made for another game version" |
| `interfaces` | object, name to integer | yes | every interface the mod uses, with a version from 1 to the port's own: `cpk-entry` 1, `roster` 1 or 2, `loose-file` 1. An unknown name or a higher version refuses the mod | Manifest; loader |
| `gameplay` | boolean | no, default `false` | `true` makes the mod gameplay; `false` never makes it cosmetic | Manifest; loader |
| `overrides` | array of objects | no | replaced archive entries; needs `cpk-entry` | Manifest; loader |
| `overrides[].entry` | string | yes | a path inside an archive, exactly as the game stores it (case too); its first folder is `battle`, `character`, `game`, `map`, `progress` or `sound`; once per mod; the row must exist (the loader suggests a near match: "did you mean") | Manifest (form); loader (row) |
| `overrides[].file` | string | `file` or `alias` | a file of the mod, relative to the mod folder (by convention `files/<entry>`); not empty, under 4 GiB, and it must fit the entry's own slot (the entry's size rounded up to 2048 bytes) | Manifest (present, not empty); loader (size, slot: "a larger replacement is not supported yet") |
| `overrides[].sha256` | string | with `file` | the file's SHA-256, 64 hex digits (write them in lower case; upper case is accepted) | Manifest; loader re-hashes at start |
| `overrides[].alias` | string | `file` or `alias` | another entry of the same archive, served from the player's own copy; it must exist and not be compressed; no `sha256` and no `derived` with it | Manifest (same archive); loader |
| `overrides[].derived` | boolean | no, `file` only | see Provenance | Manifest; loader (type) |
| `groups` | array of non-empty arrays of strings | no | entries that load together or not at all: each names an override `entry` or a `loose` path of this mod; a group is all overrides or all loose files; an entry is in one group at most. A group that loses a member to a later mod is dropped whole | Manifest; loader |
| `conflicts_ok` | array of strings | no | ids of mods whose overlap is expected; it only silences the `[mods] conflict` line, it never changes which mod wins | Manifest (types); loader |
| `roster` | object | no | only the key `fighters`, 1 to 56 fighters; needs interface `roster`; see [roster](#roster-more-fighters-on-the-character-select) | Manifest; loader |
| `loose` | array of 1 to 64 objects | no | files the game opens by a name that is in no archive; needs interface `loose-file`; only the keys `path`, `file`, `alias`, `sha256`, `derived`; see [loose-file](#loose-file-files-the-game-opens-by-a-name-that-is-in-no-archive) | Manifest; loader |

Each roster fighter (any other key is refused; Manifest; loader):

| Key | Type | Required | Rule |
| --- | --- | --- | --- |
| `id` | integer | yes | 19, 20 or 21, not already in the list (stock or an earlier mod) |
| `after` | integer | no | 0 to 255, an id already in the list at that point (default: before the first support) |
| `logo_as` | integer | no | a stock fighter id (on the stock list, below 200) |
| `owned` | boolean | no | default `true` |
| `card_as` | integer | no, needs `roster` 2 | an id on the stock list |

A `loose` file's `path` is relative to `data/PS3`, is one of the voice bank or loading tip names the game builds for ids 19 to 21, is listed once (case does not matter) and has no row in its archive (loader); an `.acb` and its `.awb` are in the same mod. `file`, `sha256`, `alias` and `derived` follow the override rules; an alias names an entry in any of the six archive folders.

Other keys: the loader ignores keys it does not know at the top level and in an override object. A mod SHOULD NOT carry keys this page does not list: a later schema may give them a meaning.

Example, a cosmetic logo mod (`mods\my-logo\mod.json`):

```json
{
  "schema": 1,
  "id": "my-logo",
  "version": "1.0.0",
  "title": "My boot logo",
  "game": { "title_ids": ["BLUS31519"] },
  "interfaces": { "cpk-entry": 1 },
  "overrides": [
    { "entry": "game/logo/logo_US_v.pak", "file": "files/game/logo/logo_US_v.pak",
      "sha256": "0000000000000000000000000000000000000000000000000000000000000000" }
  ]
}
```

The `sha256` here is a placeholder: use the SHA-256 of your own file. A roster example and a loose-file example are in their sections below.

### Classes: gameplay or cosmetic

- The loader decides the class; the author cannot lower it. A mod is **cosmetic** only when its `mod.json` parsed in full, `gameplay` is not `true`, `cpk-entry` is its only interface, and every override entry is on the allow-list: `game/logo/...`, `movie/...` or a song, `sound/<LANG>/BGM_...` (a file directly in a language folder). Everything else is **gameplay**: voices and other sound files, any `roster` or `loose` section (even one that only sets `card_as`), any other entry. A listed mod that the loader refuses counts as gameplay unless it is provably cosmetic.
- The loader logs the class (`[mods] <id> <version>: gameplay` or `cosmetic`). In an issue check, Kind of change fails when the form says cosmetic and the manifest makes the mod gameplay.
- Online: the gameplay mods that were applied make the **mod-set digest**, a SHA-256 over `jsp-modset 1`, the EBOOT's id and, for each such mod in load order, its id and version, each entry or loose path it serves with the SHA-256 of the bytes served, and its roster fighters (not `card_as`). Rollback folds the digest into the build word that both games compare, so two players meet only with the same gameplay mods, the same versions and the same load order. When a listed gameplay mod was refused, or a copy of an archive a gameplay mod changes differs from the one the plan was made from, the set "does not run as listed" and rollback does not start at all. Cosmetic mods are not in the digest: two players may use different ones. See [ROLLBACK.md](ROLLBACK.md) and [ONLINE.md](ONLINE.md).
- The game's own online (Ranked and Free over re-psn) has no build word to compare, so any gameplay mod, applied or refused, turns it off for that session (`[mods] game online refused: gameplay mod set active`). A set of cosmetic mods only, or no mods, leaves it on.

### Hashes

- Every `file` source carries the SHA-256 of that file (Manifest; loader). The loader opens each mod file once, hashes it through that handle and serves the same bytes, so a mismatch refuses the mod.
- The [canonical mod hash](#canonical-mod-hash) names the whole mod folder, every file included (README and preview too). `tools/ci/mod_verify.py` computes it; the port does not yet. The tested list names a mod by it, so any change to any file means a new hash.

### Region

- `game.title_ids` names the discs a mod is for. A mod SHOULD name only the discs it was tested on. Without `title_ids` (or with an empty list) the loader tries the mod on any disc.
- `cpk-entry` follows each disc's own archive index: an entry must exist on that disc and the file must fit its slot there, or the mod is refused.
- `roster` and `loose-file` run on the US 1.00 disc (`BLUS31519`) only: `title_ids` MUST name it (Manifest; loader), and on any other disc the loader refuses the mod whole. Such a mod is gameplay, so a refused one also turns online off for that session. From 0.7.0 the refusal line ends with "the roster interface is not available on this disc yet" (or the loose-file interface).

### Provenance

- `"derived": true` MUST be set on every `file` source (an override or a loose file) whose bytes come from the game's own files or from a third party: a model or a texture from another game, a pack rebuilt from the game's own pack, art someone else made. Not enforced yet: only the author knows where the bytes come from. The loader logs `[mods] <id>: marked derived (game or third-party bytes): it can be verified, never listed as tested`, and an issue check shows a Derived content row. The mark is bookkeeping: it is not in the digest. An `alias` ships no bytes and takes no `derived` (Manifest; loader).
- Third-party content MUST be named in the mod's README, with where it comes from. Not enforced yet.
- A mod with a derived file, or with content that is not the author's own work, is never put on the tested list: every entry of `mods/tested.json` has `"content": "own-work"` (`check_tested_mods.py`), the form's last box asks the author to confirm it, and the maintainer who plays the mod checks the mark. Nothing compares the mark with the list automatically yet.

### Recommended contents

- A mod SHOULD have a `README.txt` or `README.md` in its folder that says what it changes (the entries, and gameplay or cosmetic), which disc and port release it was made and tested with, how to remove it (take the id out of `mods.txt`, or delete `mods\<id>`; the game is stock at the next start), and any third-party content.
- A mod SHOULD have a preview image (`preview.png` or `preview.jpg`) in its folder.
- Replacement files SHOULD sit under `files/`, at the path of the entry they replace.

### Versioning

- A mod SHOULD change `version` whenever any of its files change. Not enforced: the checks only compare the version with the form. A new version is a new canonical hash and needs its own check and play-through before it is on the tested list, where an id and version, and a hash, appear once (`check_tested_mods.py`). For a gameplay mod the version is part of the digest, so both players need the same version.
- Each interface has its own integer version. A mod MUST declare every interface it uses (Manifest; loader: overrides need `cpk-entry`, `roster` needs `roster`, `loose` needs `loose-file`) and SHOULD declare the lowest version that has the keys it uses. A bump adds keys or rules; the port still loads mods of the lower versions, and a port older than the bump refuses a mod that declares the new version, so the mod is never half-applied. Example: `card_as` came with `roster` 2, `roster` 1 mods still load, and a mod with `card_as` MUST declare `roster` 2 (Manifest; loader: "unknown key").
- A change that old manifests cannot follow raises `schema`. This port reads schema 1 only (Manifest; loader).

### Before you share a mod

1. Run `python3 -I tools/ci/mod_verify.py --zip <your zip>` from a copy of this repository. Every row must pass.
2. Keep the canonical mod hash and the zip's SHA-256 it prints. Give them with the download link.
3. Unpack the zip into the mods folder, add the id to `mods.txt`, start the game once and play what the mod changes.
4. Read the `[mods]` lines in `game\run\play_stderr.txt`: `[mods] list ...: N enabled (<id>)`, then `[mods] <id> <version>: gameplay` (or `cosmetic`), one `[mods] override ...` or `[mods] loose ...` line per file, and `[mods] set: ...`. There must be no `[mods] <id>: refused: ...` line, and no `[mods] conflict ...` line you did not expect.
5. Check that `derived` is set where it belongs, and that the README names any third-party content.

## Planned: online mod sync (not built)

A proposal from the maintainer. None of it is built, and the names may change. Today both players install the same mods by hand, and rollback refuses a match when the gameplay mods differ.

- **A session folder per match.** The mods for a match go into their own folder, named by the match's mod-set digest. The launcher points the game at that folder and its list for that start instead of the player's own `mods.txt` (it already passes both paths to the game). The player's own mods folder and `mods.txt` are not touched.
- **A game restart.** The loader reads the mods once, at start, so joining a match with another mod set restarts the game with the session folder, and the game starts again with the player's own list after the match.
- **Cleanup.** On exit and on the next start the launcher deletes the session folders. It deletes only inside them, never follows a link or a junction, and leaves a session folder alone if the folder itself is a link.
- **Data only.** Only mods that pass this standard are synced, checked again on the receiving PC before anything is written: no programs, no scripts, no installers.
- **Not verified.** A mod whose canonical hash is not on the tested list needs the player's yes in a "not verified" prompt. Only tested mods are fetched without asking.
- **The room says what it needs.** The host lists its mod ids, versions and canonical hashes in the room.
- **Where mods come from.** Two ways are compared: a pinned source (a fixed download per tested mod, checked by its hash), and peer to peer from the host, checked by the same hash.
- **Derived mods as patches.** A mod with derived files is synced as a patch against the player's own disc files, rebuilt on the player's PC and checked by hash, never as the files themselves.

Open questions:

- Does the room carry the mod-set digest, the canonical hashes, or both? The digest covers only the gameplay mods as they are served; the canonical hash covers a whole mod folder.
- Are cosmetic mods synced too, or does each player keep their own?
- How does a player get back into the room after the restart?
- Where does a pinned source live, and does the tested list then need a download field (a new schema of `mods/tested.json`)?
- Peer to peer: how much upload a host can give, and whether it goes through re-psn.
- Which patch format for derived mods, and how the player's disc copy is checked first (region, game update)?
- How many session folders are kept, and how much disk they may use.
- The port does not compute the canonical hash yet; the sync needs it in the game or the launcher.

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

### Limits

The hook sees lookups by name only. The game also stores precomputed name hashes (including for ids 19 to 21), so a screen that looks a card up by
hash skips it: only the character select grid is meant, other screens are unverified.

### Open (needs a game run)

- That the select grid's cards come through this lookup, and that the donor's card shows for the added id (only read from the lift).
- That every stock-list id has a full card in the shared set (checked for 018 and 211).
- That the donor's card is in the set on every screen that shows the grid: a per-id card pak (`chara_256_ico_%03d_ALL_m.pak`, loader without a static caller) might mean it is not on some screen.

## loose-file: files the game opens by a name that is in no archive

Interface `loose-file` 1, US 1.00 disc only. `loose` is a list of files the game asks for by a path that no CPK has a row for: the
voice banks and loading tips of an added fighter (ids 19, 20, 21). The mods folder serves them.

```json
"interfaces": { "roster": 1, "loose-file": 1 },
"loose": [
  { "path": "sound/JP/CV_019_00_ST_JP.acb", "file": "files/CV_019_00_ST_JP.acb", "sha256": "..." },
  { "path": "sound/JP/CV_019_00_ST_JP.awb", "file": "files/CV_019_00_ST_JP.awb", "sha256": "..." },
  { "path": "progress/game/loading_game/tips/loading_game_tips_chara_019_000_ALL_m.pak",
    "alias": "progress/game/loading_game/tips/loading_game_tips_chara_018_000_ALL_m.pak" }
]
```

- `path` is relative to `data/PS3` (the same names as a CPK entry) and starts in one of the six archive folders. Only the names the game
  builds for the added ids are accepted: `sound/<LANG>/(CV|PV)_0<id>_<dd>_ST_<LANG>.(acb|awb)` and the tip packs under
  `progress/game/loading_game/tips/` (`..._chara_0<id>_000_ALL_(m|v).pak`, and `<LANG>/..._(title|mes)_chara_0<id>_000_<LANG>_(m|v).pak`).
- A path that has a row in its archive is refused: a CPK hit wins before any loose lookup, so use `cpk-entry` there.
- The source is `file` (a file of the mod, checked against its `sha256`) or `alias` (an uncompressed row of a CPK in the player's own copy,
  so no game data ships). `derived: true` on a `file` marks bytes taken from the game or a third party: a verifier may check such a mod
  but never lists it as tested.
- An `.acb` and its `.awb` come from one mod (the ACB carries the AWB's hash); `groups` may also list loose paths (a group is all cpk
  entries or all loose files).
- The later mod wins a path (a `[mods] conflict loose ...` line; `conflicts_ok` silences it); a group that loses a member is dropped whole.
- A loose file is gameplay: its lower-case path and the sha256 of the bytes served are in the digest (voices count as gameplay).
- A copy at the same path in the game folder (an old installer's leftovers) loses to the mods-folder copy; this is logged once.
- Served by the toolkit's path resolver (patch 6504, `ps3emu/fs_path_ops.h`): a read-only open or a stat of the path is answered with
  the mod's file (or the disc copy of the archive an alias names), and the virtual-file view (patch 6502) serves the bytes that were hashed for a `file` source (the mod's open handle). An `alias` window is read from a fresh open of the disc archive; its hash is checked once per path when the game first opens it, and a mismatch refuses online play (a change of the archive after that is not seen, as for the CPK copies).

Songs are the only sound files that are cosmetic: `sound/<LANG>/BGM_*` in `cpk-entry`. Any other sound override is gameplay.

### Open (needs a game run)

- The path form CRI uses when a name is in no CPK (bare relative, or under `data/PS3`): `loose_rel_from_guest` takes both, the first run
  reads the `[fs] open` lines. If the game opens through another path function than `cellFsOpen`, nothing is served.
- That the game accepts a voice bank or tip served this way, and that a view's size via `cellFsFstat` is what CRI uses.
- Directory listings do not show a mod-only loose file (only open and stat by path are answered).
- The cue-sheet name table has no names for ids 19 to 21 (static reading): a bank may load and still not play.

## derived: bytes from the game or a third party

A `file` source of a `cpk-entry` override or a `loose` entry may carry `"derived": true` (an alias takes none: it ships no bytes). It is
bookkeeping only: not in the digest, logged as `[mods] <id>: marked derived`, and `jmods::manifest_derived` tells a verifier to check the
mod but never put it on the tested list. A port that does not know the key ignores it on an override.
