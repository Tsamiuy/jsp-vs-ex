# Mods

A mod is a folder in the port's mods folder with a `mod.json` and the files it names. The mods folder's list (`mods.txt`) says which mods load, in order. The rules for `mod.json` (schema 1) are in `installer/port/jsp/jsp_mods_core.h` (`parse_manifest`): an id of `[a-z0-9-]` that equals the folder name, a version, `interfaces` (`cpk-entry` 1, `roster` 1), and overrides of entries of the game's archives, each by a file with its SHA-256 or by an alias. A mod that breaks any rule is refused whole; the other mods still load.

A mod that only changes logos, movies or sound is cosmetic. Every other mod is gameplay, and online play compares the gameplay mods of both players (see [ONLINE.md](ONLINE.md)).

## Getting a mod verified

Verification checks that a mod follows the rules above and is safe to list. It does not make the mod part of the port.

1. Put the mod in a zip: `mod.json` at the top, or inside one top-level folder. Upload it somewhere with an HTTPS link that stays the same (a GitHub release asset works).
2. Open a "Mod verification" issue and fill in the form. Paste your `mod.json`.
3. A maintainer adds the label `mod-check`. The checks run on GitHub and post one comment: a table with pass or fail for each check, the canonical hash of your mod and the list of its files. Nothing from the mod is run, and nothing is copied or stored.
4. To check a changed mod, edit the issue (keep it to the same mod id) and ask a maintainer to remove the label and add it again. The issue text is read when the label is added.
5. A maintainer plays the mod. If it works, a pull request adds it to `mods/tested.json` with its canonical hash. The automated checks alone never put a mod on the list.

What the checks refuse:

- a download that is not HTTPS, is over 200 MB, or redirects to a non-HTTPS or non-public address;
- a zip with more than 2000 entries or more than 500 MB unpacked, an encrypted entry, a symbolic link, an absolute path, a `..` part, a backslash, a name Windows cannot use (a device name, a trailing dot or space, or one of `\ : < > " | ? *`), the same path twice or two paths that differ only in case;
- executables and scripts (`.exe .dll .bat .cmd .ps1 .py .sh .so` and a few more), and source files or a path in the port's source tree (`port/`, `installer/`, `tools/`, `third_party/`, `CMakeLists.txt`, `.cpp .h` and other C and C++ files, patches);
- a `mod.json` that breaks the schema 1 rules, an override file that is missing or whose SHA-256 differs, an id or version that differs from the form, or a pasted `mod.json` that differs from the one in the zip;
- a mod that the form calls cosmetic while its manifest makes it gameplay.

Only the form and the zip are read. The checks cannot tell whether a mod is good or whether a file is allowed to be shared: that is the author's confirmation and the maintainer's play-through.

## The tested list

`mods/tested.json` (schema 1) lists the mods a maintainer has played. Each entry has `id`, `name`, `version`, `sha256` (the canonical mod hash), `tested_on` (the port release, like `0.6.1`) and `notes`. Entries are sorted by id, then version; an id and version, and a hash, appear once. `tools/ci/check_tested_mods.py` checks this in CI. A pull request that adds an entry links the verification issue.

## Canonical mod hash

The canonical mod hash names the exact content of a mod, however it was zipped. The checks and the port compute it the same way. Version 1:

1. The files of the mod are all files under the mod folder (the zip's top level, or its one top-level folder), `mod.json` included. Folders are not files. Each file's path is relative to the mod folder, with `/` between parts, with no leading `/`, and it must pass `valid_rel_path` in `jsp_mods_core.h`. The path is used as it is: no case folding, no Unicode normalisation.
2. For each file compute `h`, the SHA-256 of its bytes as 64 lowercase hex digits.
3. Sort the files by the UTF-8 bytes of the path, byte by byte (so `B` comes before `a`).
4. Build the byte string: the ASCII text `jsp-mod-hash 1` and a line feed (`0x0A`), then for each file in that order `h`, one space (`0x20`), the path in UTF-8, and a line feed. No other bytes, no byte order mark, no carriage returns.
5. The canonical mod hash is the SHA-256 of that byte string, as 64 lowercase hex digits.

Order of entries, compression, timestamps, folder entries and the name of the top-level folder do not matter. Changing a byte of any file, a file name or adding or removing a file changes the hash.

Known answer: a mod whose files are `mod.json` containing `{}` and `game/logo/a.bin` containing `a` gives `ce2c86505349b1c37a35fc28a64aa9a5ddc4acac8b6a7e51962ab93e7f22a1ac`. The reference code is `canonical_hash()` in `tools/ci/mod_verify.py`; the port's code must give the same answer, and `tools/ci/test_mod_verify.py` holds the test.
