# Fight hitches: shader compiles and texture creation (2026-10-21, AI assistant thread)

`shader-hitches.patch` applies to `ps3recomp-src` at **d4b3d58** (branch `jstars-port`, the
colour patches applied). `git apply --check` passes against that commit, and against the working
tree with `--ignore-whitespace` (the checked-out `rsx_d3d12_backend.c` has CRLF line endings
today, so without that flag every LF patch fails on it). It changes only
`libs/video/rsx_d3d12_backend.c` (+633/-62). **Not built or run.** Three switches, all **opt-in**:

| switch | what | off |
|---|---|---|
| `SHADER_CACHE=1` | compiled shader bytecode kept on disk, one file per shader in `shadercache\` (or `SHADER_CACHE_DIR=<dir>`), keyed by the final HLSL text | unset |
| `PSO_ASYNC=1` / `=2` | guest-FP pipelines built on worker threads (`PSO_ASYNC_THREADS=<n>`, default 2); a draw whose pipeline is not ready is skipped for that frame (1) or drawn with a sibling pipeline, same shaders but another blend/cull/mask (2); `PSO_ASYNC_WAIT=<ms>` (default 0) lets the present thread wait up to that long per frame for builds to land | unset |
| `TEX_LRU=1` / `TEX_POOL=1` | texture slot eviction: least-recently-bound slot first (LRU); POOL also prefers an unused slot whose D3D resources already have the new texture's size and format when it was last bound `TEX_POOL_AGE` batches ago (default 4) | unset |

Everything else in the patch is measurement: a `[PERF5]` line and three new fields on `[PERF3]`
(see "What to measure"). With all three switches off the only behaviour change is that the
in-memory pixel-shader blob table and the pipeline-failure table moved to file scope.

If the patch no longer applies, `apply.py` rebuilds it from the current file: `git show
HEAD:libs/video/rsx_d3d12_backend.c > base.c; python3 apply.py base.c new.c test\pure.h;
diff -u base.c new.c > shader-hitches.patch`. It stops on the first anchor it cannot find.

## The problem, measured (stage survey, `stages\sNN\stderr.txt`)

Over the fight part of each survey run (≈320-390 s of `[PERF]` windows per stage):

| | per fight | per event | where |
|---|---|---|---|
| **pipeline builds** (`pso` in `[PERF]`, present thread) | **11-39 s**, 200-760 misses | **43-78 ms each** | 120-450 at every match start (one window at 1-2 fps lasting 8-16 s, e.g. s01 line 5935: 214 misses, 13.95 s); then windows of 5-15 misses = 0.3-1.3 s, which are the 10-17 fps dips |
| **new textures** (`tex`, `[PERF3] new`) | **7-11 s**, 4,000-7,600 uploads | **1.4-2.1 ms each**, 80% of them also (re)creating the two D3D resources | bursts of 300-800 per window as the camera reveals the stage (s07 line 9403: 802 new in 0.86 s at 8.5 fps) |

A pipeline miss today = decompile (fast) + `D3DCompile` of the HLSL (unless the same HLSL text
was compiled this run) + `CreateGraphicsPipelineState`, all on the present thread inside the
prepass. The split between the last two is not in any log yet; `[PERF5]` adds it.

## Ranking (expected gain, risk)

1. **`SHADER_CACHE=1`: largest, safest.** From the second run of a stage on, every `D3DCompile`
   becomes a file read; the 8-16 s match-start stall and the mid-fight compile hitches shrink to
   the driver's `CreateGraphicsPipelineState` share (unmeasured; typically a few ms on NVIDIA,
   whose own driver cache also warms). First run of a stage: unchanged. Risk: low. Files carry a
   header (magic, version, both hashes, HLSL length, blob size); anything that fails it is
   ignored and compiled as before. The key is the final HLSL text, so any decompiler or backend
   patch that changes the generated shader changes the key automatically; the compiler DLL's
   size and date are folded into the file name. Stale files just go unused (delete
   `shadercache\` to reclaim space).
2. **`PSO_ASYNC`: removes the hitch itself, first run included, at a visible cost.** The present
   thread never waits for a build; objects with a brand-new shader appear a frame or two late
   (mode 1) or with a sibling blend state for those frames (mode 2). At a match start with a cold
   cache that is 120-450 builds over 2 threads, so some objects pop in over several seconds; with
   `SHADER_CACHE=1` warm, each build is milliseconds and the pops should be invisible.
   `PSO_ASYNC_WAIT=8` is the middle ground (wait up to 8 ms per frame, then skip). Risk: medium:
   two worker threads call `D3DCompile` and `CreateGraphicsPipelineState` (both thread-safe by
   contract); the decompiler and every cache table stay on the present thread; blob and VS
   references are held per job. If the job ring (1024) is full the build happens in place.
3. **`TEX_POOL=1` (implies LRU): smaller, safe.** Removes the two `CreateCommittedResource` calls
   from most new uploads; how much of the 1.4-2.1 ms that is, `[PERF3] created N (t)` will say
   (guess: 0.3-0.6 ms, i.e. 20-40% of the texture time). LRU on its own only helps if many "new"
   uploads are textures that were evicted and come back; the new `again` counter answers that.
   Risk: low: only the choice of free slot changes; a reused resource goes through the same
   PSR → COPY_DEST path today's "reupload" case uses, and render_frame drains the GPU before the
   prepass, so nothing in flight references the slot.

Not drafted: decoding textures on worker threads (the `decode` timer says whether it is worth
it; the decode writes straight into the slot's upload buffer, so it could be queued like
`VFETCH_ASYNC` and synced before `Close`), and an `ID3D12PipelineLibrary` to also cache the
driver's compiled pipelines across runs (needs periodic serialisation because runs are killed on
a timer).

## What to measure (AI assistant)

All with `PERF=1`, same stage (s06 Namek or s01 Kameari, both have many misses), compare
`[PERF]`, `[PERF3]`, `[PERF4]`, the new `[PERF5]`, and screenshots.

1. **Baseline with the patch, switches off.** Picture must match the previous build. New lines:
   `[PERF5] shaders: fxc N t | disk hits 0 | pso create N t | async ...` and
   `[PERF3] ... created N (t) | decode t | again N | policy 0`. `fxc t` vs `pso create t` says which
   half of a miss the disk cache can remove; `created (t)` vs the `new` time says what the pool can
   remove; `again` says whether LRU matters.
2. **`SHADER_CACHE=1`, twice.** First run fills `shadercache\` (expect `disk hits 0`, speed as
   baseline, files appearing). Second run: `disk hits` ≈ the misses, `fxc` ≈ 0, the match-start
   window and the dip windows much shorter. Count the files and their size.
3. **`SHADER_CACHE=1 PSO_ASYNC=1`**, then `=2`, then `PSO_ASYNC=1 PSO_ASYNC_WAIT=8`. Look at
   `[PERF4]` (frames over 54 ms should nearly vanish), `[PERF5] skipped` (draws skipped while
   pending) and `sibling`, and at the screenshots for objects missing for more than a moment.
   `[PSO] async pipeline builds on: ...` confirms it started.
4. **`TEX_POOL=1`**: `created` should fall towards 0 after the first windows of the fight,
   `tex` seconds with it; `policy 2` on the `[PERF3]` line. Then `TEX_LRU=1` alone only if `again`
   was large.
5. If a switch wins, make it default-on with its off switch (`SHADER_CACHE=0`, `PSO_ASYNC=0`,
   `TEX_POOL=0`), as with the other speed patches.

## How it works

**Disk cache.** `shcache_key()` hashes the final HLSL text (after every rewrite the backend
applies: constant-buffer layout, `FP_TEX`, `FP_FORCE`, depth bitcast) plus the profile with two
independent 64-bit hashes. `vp_get_vs` and `vp_get_fp_pso` try `shcache_load()` before
`D3DCompile` and `shcache_store()` after a successful one. The file is written to a temp name
and renamed, so a run killed mid-write never leaves a half file under the real name. The first
hash is also the key of the existing in-memory PS blob table, which is unchanged in behaviour.

**Async builds.** `vp_get_fp_pso` keeps decompiling and filling the pipeline description on the
present thread. Where it used to call `CreateGraphicsPipelineState` it now hands a job to a
worker: the description, a reference to the VS blob, and either the PS blob (if the HLSL was
compiled before) or a copy of the HLSL text. The worker runs the disk cache / `D3DCompile` and
`CreateGraphicsPipelineState`, then marks the job done; the present thread collects finished
jobs at the start of each prepass (and at its end, with the optional wait) into the same caches
as before. A key already in flight is detected before the decompile, so pending draws cost one
scan per call, not a decompile each. The draw pass skips a draw whose pipeline is pending
(`s_pso_pending_hit`); depth-only draws were already skipped without a pipeline.

**Slot policy.** `tex_scan_candidates()` finds, among the slots not bound in this batch, an empty
one, the one bound longest ago, and the one bound longest ago whose resource already matches
the new texture's size, format and cube-ness; `tex_pick_slot()` chooses. Slots now record the
batch they were last bound in (`last_gen`).

## Evidence (offline, `test\test_shader_hitches.c`)

`gcc -std=c17 -O2 -Wall -Wextra test_shader_hitches.c && a.out` builds against `test\pure.h`,
which `apply.py` extracts from the patched file (the key, the header check and the slot
policy, verbatim). Passed on 2026-10-21:

- key: same text → same key; one byte, the length or the profile changed → both halves differ;
  5,000 near-identical programs → no collision; the header check accepts the matching header
  and rejects each field changed; header is 32 bytes.
- slot policy on a synthetic fight (2,400 frames, 900 textures of 6 sizes, 512 slots, a drifting
  window plus camera swings): first-free (today) 48,799 uploads / 40,845 resource creations;
  `TEX_LRU` 38,672 / 32,306; `TEX_POOL` 38,681 / **1,862**. The real reduction depends on how
  many "new" uploads are returning textures and how many sizes the stage uses; the counters
  above measure that.

Nothing here was compiled with clang-cl: the C is written against the file's own idioms and
reviewed, and `git apply --check` passes, but the first build is the AI assistant's.

## Results (AI assistant, Namek, 2026-10-21, `hitches\`) and the "low hit rate" question

- `SHADER_CACHE=1`: the match-start window is a full hit from the second run on (230/230 disk
  hits, 0 compiles, pso 10.4 s -> 0.18 s, 1.5 -> 17 fps). The compiles that remain (259 in run 2,
  22 in run 3, 261 in run 4) are bursts of fragment programs never compiled before in any run,
  brought by character switches and specials that differ per run; the key does not vary between
  runs (every compile wrote a new file, none was re-written; no vertex shader compiled after run
  1; the burst's 259 programs have a flat HLSL-length spread, so they are distinct programs, not
  mask variants). Full evidence FINDINGS §39; `tools\shcache_stat.py` shows what each run added.
  Recommendation: default-on, keep `shadercache\` across builds, warm it once per character.
- `PSO_ASYNC=1`: match start 17 fps with the cache; a cold 344-miss burst runs at 19 fps instead
  of 2, with draws skipped over ~5 windows because 2 workers finish 30-45 compiles per window.
  Try `PSO_ASYNC_THREADS=4`. The AI assistant saw nothing visibly missing in mode 1 (176 draws
  skipped over a run); mode 2 and mode 1 with `PSO_ASYNC_WAIT=8` were also run, same numbers.
- `TEX_POOL=1`: new uploads 5658 -> 1482, texture time 5.7 s -> 1.3 s.
