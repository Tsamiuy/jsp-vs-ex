# Save import safety and issue 30

Investigated against upstream `58594b64654550a82158c1e5ca360def99f960c1` on 2026-11-07.
The [issue 30 discussion](https://github.com/Tsamiuy/jsp-vs-ex/issues/30) corrects its initial premise:
RPCS3 stores this game's `SYS-DATA` in plaintext, without the console's binary `PARAM.PFD`.
The reported RPCS3-to-v15 load succeeded. Importing an RPCS3 save into v18 and opening a port-written save
in RPCS3 still need runtime tests. This change does not resolve those two compatibility questions.

## Reproduced installer defects

The previous wizard called the shared helper's direct overwrite operation. A source-copy failure after its
first output write replaced existing progress with a partial file. Synthetic tests reproduced this without
game files: `GAME.DAT` changed from `original progress` to `partial import` after the helper reported failure.
The same path followed destination symbolic links, and a directory alias could bypass the lexical refusal
to write into an RPCS3 installation.

The wizard now stages every incoming file before replacing any file in that save. For each replacement it
moves the old file into a temporary backup on the destination filesystem, then installs the staged file.
An I/O failure rolls those changes back, including new files. Stale destination files remain untouched.
If rollback also fails, originals remain under the reported `.jsp-save-import-*` recovery folder's `old`
subfolder. Do not play until that recovery has been inspected and completed.
An interruption or unexpected exception during replacement or rollback propagates to the caller and
retains staging, including any originals already moved there. Cleanup is allowed only before changes
start, after every replacement succeeds, or after rollback completes. Injected `KeyboardInterrupt`,
`SystemExit` and `RuntimeError` during publication, plus interruption during rollback, reproduced a
cleanup defect in the initial fix and now preserve the recovery originals.

Preview and copy both reject links/reparse points in the save or incoming target paths, file/directory
collisions, and resolved destinations inside RPCS3. The vendored `rpcs3_locate.py` remains unchanged.
The safeguard is per save directory: a later save's failure does not undo an earlier completed save.
Close both programs for import. Power loss and concurrent filesystem changes are outside this guarantee.

## Repeatable host verification

Run from the repository root with Python 3.11 or newer:

```sh
python3 installer/tools/rpcs3_wizard_test.py
python3 installer/tools/test_rpcs3_locate.py
python3 tools/ci/repo_checks.py
```

These tests create temporary synthetic data only. Coverage includes source-copy failure after a partial
write, failure after earlier files have been published, failed rollback retaining recovery originals,
links present at preview or introduced afterwards, an alias into RPCS3, unchanged stale files, and
byte-identical import of a plain `SYS-DATA` plus metadata without synthesizing a `PARAM.PFD`.
On Linux, all 22 wizard tests and 35 unchanged shared-helper tests pass. The initial 20-case wizard suite
against the original upstream wizard produced six failures. The partial-write data loss and accepted
destination links are direct reproductions of old defects. Two tests inject failures in the new publish
and rollback operations; their failures against old code show that the new recovery behavior is absent,
not two additional independently reproduced old bugs. Repository checks and the feature/version checks
also pass. The Ubuntu `ci` checks job now runs both save-import test scripts on pushes and pull requests.
This does not establish Windows junction behavior or in-game save compatibility.

## Remaining runtime verification

Use copies of owned saves in disposable game-data roots; keep originals read-only. For RPCS3-to-v18,
record source file hashes, import using the wizard's preview and plan token, boot the normal v18 build,
confirm known progress rather than a new-save prompt, and compare imported hashes after a load-only run.
For the return direction, save once in the port, copy that complete directory to a disposable RPCS3 user,
and confirm both load and progress there. Record the toolkit/build pins, logs, changed filenames and
hashes. Do not upload saves, account metadata or game screenshots to the repository. A synthetic copy
test proves byte preservation only; it cannot substitute for these two runtime checks.
