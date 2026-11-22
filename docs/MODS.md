# Loading mods

Put each mod in its own folder under `mods` next to the game launcher, with
`mod.json` directly inside that folder. Restart the game to load changes.
Only use mod files you trust. The game reads manifests and replacement data;
it does not run scripts supplied by a mod.

`mods.txt` is optional. It is read next to the active settings file, or next to
the launcher when no settings file was found. Mods named there load in that
order. Other mod folders load afterwards in alphabetical order. A later mod
can replace an earlier mod's files; grouped replacements keep their existing
all-or-nothing rules.

Use `-id` to turn a mod off, even when its folder is present. For example:

```text
# These load first, in this order.
stage-colours
music-pack
# This folder is ignored.
-alternate-rules
```

The first line naming an ID wins, including an off line. To change a choice,
edit that line rather than adding the ID again. IDs use lowercase letters,
digits and hyphens, with 1 to 40 characters. A leading hyphen is now reserved
for the off marker; an older mod whose ID starts with a hyphen needs a new
matching folder and manifest ID before it can load. Removing a folder also
removes an unlisted mod. A listed enabled mod whose folder is missing is refused.

Folders without a manifest and folders reached through links or junctions
are skipped. A broken or unreadable manifest is refused. If a mods list exists
but cannot be opened or read, the game loads no mods; it does not discard off
lines and enable every folder. A directory enumeration error refuses the
whole set, loads stock files and disables online play for that start. Check
the `[mods]` lines in `play_stderr.txt` for the reason and the actual load order.
The launcher's “listed” count counts only `mods.txt`, not discovered folders.

`JSP_MODS=0` in the active settings turns all mods off. Advanced overrides
`JSP_MODS_DIR` and `JSP_MODS_LIST` select another folder or list. Gameplay mods
still contribute to the rollback build identifier; mismatched or refused sets
cannot start rollback, and the game's own online remains off with gameplay mods.
Cosmetic-only sets keep their existing online policy.

This change adds folder discovery and list controls. The settings window also supports the ZIP import described below. Host tests use synthetic files; Windows directory and sharing
behavior and real modded gameplay have not been run for this change.

## Managing the list in the settings window

Open the **Mods** tab to see installed manifest folders and any listed IDs whose
folder is missing. Tick a mod to enable it, untick it to write an off entry,
and use **Move up** or **Move down** to set priority. **Save mod choices** applies
the draft to `mods.txt`; opening the tab and changing ticks do not save it.
Starting the game with unsaved choices asks whether to save or discard them.
**Refresh** reads the current files again and asks before discarding a draft.

The tab shows the actual folder and list paths, including advanced overrides.
Relative overrides resolve against the game's run folder. The default list sits
beside the settings file this window uses on the next start. Existing comments
are kept; inline comments may move onto their own lines when the list is reordered.
Listed IDs whose folder is missing remain listed until you disable them.
An invalid or unreadable list cannot be saved from the tab. If another program
changes the list or its configured paths, refresh before trying to save again.
The global `JSP_MODS=0` switch is preserved and the tab says when it is active.

Both settings-window variants use the same management behavior. This feature
has compiled and run synthetic C# file tests under Mono; the window layout,
Windows file sharing and a real game start remain untested. ZIP installation uses the bundled offline helper described below.

## Installing a ZIP package

Choose **Install ZIP...** on the Mods tab. Save or discard any pending list
changes first, then select one package. Keep the window open until it finishes.
A successful install refreshes the list. It does not edit your enabled/off
choices: a previously disabled ID stays disabled.

A package contains one `mod.json`, either directly at the ZIP root or inside
one enclosing folder with all its files. The manifest must be UTF-8 JSON with
schema 1 and a valid mod ID. The game still validates the manifest's interfaces,
replacement data and compatibility when it starts. Existing mod IDs are refused;
no installed folder or settings file is overwritten.

The importer accepts stored or Deflate ZIP files, up to 2,048 entries, 128 MiB
per file and 512 MiB of expanded data. The archive itself is limited to 512 MiB,
its directory metadata to 2 MiB and its manifest to 1 MiB. ZIP64, split/encrypted
archives, links, reparse points, special files, path traversal, Windows device
names and conflicting or case-aliased paths are refused. Package scripts are
copied as data and never executed.

Extraction happens in a private temporary folder that cannot be discovered as
a mod. The complete folder is published only after validation and CRC checks,
without replacing an existing destination. Failed extraction removes its own
staging folder; if the destination is changed externally or cleanup is blocked,
the error explains that staging remains. A Mods destination reached through a
link or junction is refused. Only the bundled Python and helper are used;
missing tools are reported and nothing is downloaded.

Synthetic importer, packaging and C# argument tests run on Linux. Native Windows
file sharing, the ZIP button on screen and a real modded game start remain
untested for this change.

In the game, **Options > Port Settings > Mods & Launcher** explains where to
manage mods. Installation and changes to the mod list use the launcher **Mods**
tab before the next game start.
