# GCM_FLIP_AT_WORD: a flip counts as done only when the FIFO walker has reached it

Patch: `flip-at-word.patch` (`libs/video/cellGcmSys.c`, about +45), on 3573ab4. `git apply --check` passes,
and a syntax-only compile with the build's warnings shows no errors and no new kinds of warning. Not built or
run. **Opt-in** (`GCM_FLIP_AT_WORD=1`), so the default behaviour is unchanged until it is tested; once it is,
it can become default-on with `=0` as the off switch.

## Why (beam-clock, FINDINGS: dmaclock)

For all 50 beam draws, the SPU job's last write to their vertices came exactly **2 flips before** our walker
drew them. By then the game had already recycled (zeroed) the buffers. So we draw two frames late, and the game
doesn't know.

Where the lag comes from (checked in code): `cellGcm_request_tick` (the 60 Hz ticker) marks a flip as
complete on every tick, **whatever the FIFO walker has reached** (`GCM_PENDING_SET(3)`). The guest sees
`CELL_GCM_FLIP_STATUS_DONE`, and its flip handler runs, as soon as the next 60 Hz beat comes, even if the walker
is still two frames behind. A title that paces itself on flip status (J-Stars does) therefore runs ahead, and
reuses per-frame buffers (SPU-built particle quads) that the walker has not drawn yet. On a PS3 a flip completes
only when the RSX executes it, so the game can never get more than its buffering allows ahead of the GPU.

`GCM_FLIP_NEEDS_FIFO=1` already exists, but it waits for get == put, which may starve flips when the guest
keeps writing.

## What it does

- Counts the in-FIFO flip requests the guest makes (`gcm_flip_request(..., in_fifo=1)`, the marker path) as
  `s_flip_ppu_requests`. The walker already counts decoded markers (`s_flip_request_count`).
- `cellGcm_request_tick`, `cellGcmTickFlip` and `cellGcmSetWaitFlip` report a flip as done only while the
  walker has decoded every requested marker. Vblanks keep ticking.
- **Safety timeout:** if the walker owes markers for `GCM_FLIP_AT_WORD_MS` (default 100 ms) without
  progress, for example after a resync skipped one, the owed markers are counted as reached and a line is
  logged. This bounds any stall.
- Titles that flip outside the FIFO (the non-marker path) are unaffected.

## Test plan

1. Quick check without this patch: the Kamehameha demo with the existing `GCM_FLIP_NEEDS_FIFO=1`. If the
   beam appears, the diagnosis is confirmed.
2. With the patch: `GCM_FLIP_AT_WORD=1` plus `VP_CODE=9A64BF1C VP_CODE_N=50 VP_CODE_FROM=6800`. The `[VPCODE]`
   vertex bytes should be non-zero, and the beam visible. Also check other characters' skills.
3. fps on Yoshiwara, Hidden Leaf and Athena fights, and menus and loading screens. Expect the game to slow
   down only if the walker really is the bottleneck (it then runs at the walker's speed, as on hardware).
4. Count of `GCM_FLIP_AT_WORD: walker owed ...` lines: ideally 0 or a few at loading screens.
5. The second-fight freeze the maintainer saw: check whether it changes.
