# Faster vertex fetch: draft patch (2026-10-20)

`vfetch-fast.patch` applies to `ps3recomp-src` on top of `c0dbbd4` (branch `jstars-port`).
On 2026-10-20 `git apply --check` passed against the main checkout, including the AI assistant's
uncommitted edits to `rsx_d3d12_backend.c`. **It has not been built into the game or
run.** That is the AI assistant's job, because it owns `rsx_d3d12_backend.c`.

Files touched: `libs/video/rsx_vertex_fetch.{h,c}` and `libs/video/rsx_d3d12_backend.c`.
Nothing else changes: the input layout, PSOs and the 256-byte vertex stride stay the same.

## What it does

1. **A per-draw fetch plan** (`rsx_fetch_plan_init`, `rsx_fetch_plan_range` in
   `rsx_vertex_fetch.c`). The old `rsx_fetch_attrib` worked everything out again for every
   vertex and every attribute: the env switches, the divider, and the LOCAL/MAIN resolve through
   cellGcmSys. The plan does that once per draw. MAIN arrays are resolved once per 1 MB IO page.
   The conversion rules are the same as before.
2. **It fetches only the attributes the VP reads.** The mask comes from the existing
   `rsx_vp_analyze_inputs`, cached per program (same key as `vp_get_vs`: start slot through
   END). When the analysis is not exact, all 16 are fetched. Attribute 0 is always fetched.
   Unread slots are written as zero.
3. **Vertices are staged in cached memory and stored whole.** Each block of 32 vertices is
   built on the stack, then copied with non-temporal 16-byte stores. Every vertex is written as
   256 contiguous bytes, which suits the write-combined upload heap.
4. **A worker pool.** Draws of 2048 output vertices or more are split into chunks of 512 across
   3 workers plus the caller.
5. **Every VP upload path uses the plan:** quads, tris and strip/fan expansion, each indexed and
   non-indexed. The native-strip path uses it one vertex at a time.
6. **`[PERF]` also prints CPU cycles per vertex** for the timed fetch (`QueryThreadCycleTime` on
   the recording thread).

## Switches

| env | effect |
|---|---|
| `VFETCH_LEGACY=1` | old `read_vp_vertex` loops (needed for `VTX_DUMP` in the quad paths) |
| `VFETCH_ALL=1` | plan path, but all 16 attributes |
| `VFETCH_THREADS=<n>` | worker threads besides the caller (default min(ncpu/4, 3); 0 = none) |
| `VFETCH_MIN=<n>` | smallest draw that is split (default 2048 output vertices) |
| `VFETCH_DBG=1` | print each VP's input mask once |

## Evidence

Standalone tests are in `test/`. They link only `rsx_vertex_fetch.c` and stub cellGcmSys.
- `test_vfetch_plan.c`: **4500/4500 random cases are bit-identical** to `rsx_fetch_attrib`.
  The cases cover all types including CMP and unknown, sizes 0-4, stride 0, disabled arrays,
  LOCAL and MAIN (through a scrambled IO page table), the divider, base offset/index, all five
  topologies, and non-indexed, u16 and u32 indices. Masked plans write exactly the masked slots.
- `test_vfetch_pool.c`: the pool code, cut verbatim from the patched backend, gives the same
  output as one thread for 11 draw sizes, 20 times each.
- Timings, 300-400k vertices, writing into a real D3D12 upload heap (laptop GPU):

| path | per vertex | 400k vertices |
|---|---|---|
| old `rsx_fetch_attrib` × 16 | 0.053 µs | ~21 ms |
| plan, 4 attributes read, 1 thread | 0.018 µs | 7.3 ms |
| plan + pool, 1+1 / 3+1 / 7+1 threads | | 5.3 / 4.6 / 4.9 ms |

## Read this before expecting a big fps jump

In-game `[PERF]` measured **~1.6 µs per vertex** (§19). The same old code costs **0.053 µs per
vertex** in isolation on this machine, about 30× less. So most of the in-game "vtx" time is
probably not decode work. It could be the recording thread being starved by PPU/SPU threads, or
slow guest-memory access. This is inferred, not measured. The new cycles-per-vertex figure in
`[PERF]` settles it:
- if cycles/vertex is low (a few hundred at most) while wall time is ~1.6 µs, the thread is
  waiting or starved, and this patch will not fix that;
- if cycles/vertex is high (thousands), the work is real, and the plan plus the pool should cut
  it.

Suggested first run: `PERF=1 VFETCH_THREADS=0`, then `PERF=1 VFETCH_LEGACY=1`, then
`PERF=1` (default). Compare fps and the vtx and cyc/vert columns.

## Next steps after this

- With few attributes the 256-byte vertex write now dominates (the pool stops scaling at 3+1).
  The next win is a compact vertex: per-mask input layout and stride, using the existing
  `rsx_vertex_layout_plan` / `rsx_vp_decompile_compact_ex`. That changes PSOs, so it is a bigger
  change.
- Strip expansion decodes each strip vertex 3 times. It could decode once and copy.
- The plan's VP-mask cache is keyed by a 32-bit hash plus start slot and length. A collision
  would drop attributes. `VFETCH_ALL=1` rules that out.
