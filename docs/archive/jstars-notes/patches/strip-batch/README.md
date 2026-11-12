# Strip expansion: batched vertex fetch + dedup (2026-10-20)

`strip-batch.patch` applies to `ps3recomp-src` at `8ee4e58`. On 2026-10-20 `git apply --check`
passed against the main checkout. It changes `rsx_d3d12_backend.c` and
`rsx_vertex_fetch.{h,c}`. **It has not been built or run in the game.**

## Why vertex work is 77% of the frame

FINDINGS §27 measured **379 cycles per vertex** after the texture-slot fix. Before that it was
about 60.

The cause is commit `e32a781`. It added the restart-aware strip expansion,
`upload_strip_vp_indexed_rsx`, which is the match's main path. That function fetches every
output vertex through `vf_one`, so each vertex runs its own one-vertex plan range. Every one of
those ranges pays for:
- a staging memset;
- a page-cache reset;
- a store;
- an `sfence`.

The batched paths pay for those once per block of 32 vertices. On top of that, each strip vertex
is decoded about three times, because it appears in up to three triangles.

**Standalone measurement** (`test/test_strip_batch.c`, real D3D12 upload heap, laptop GPU).
The test used J-Stars' character layout from §26: 8 attributes, stride 64. The index stream was
like the match's, mostly 3-index pieces split by 0xFFFF restarts. It gave 497k output vertices:

| path | per vertex | 497k vertices |
|---|---|---|
| per-vertex fetch (current) | **0.159 µs** (≈ the in-game 379 cycles) | 79 ms |
| batched | 0.029 µs | 14 ms |
| batched + dedup (this patch) | **0.023 µs** | 11.5 ms |

## The patch

1. **The strip loop no longer fetches as it goes.**
   - It writes the guest vertex index of each output into a list.
   - Then one `vf_fetch` decodes the list. It uses the new source kind `RSX_FETCH_VLIST`, with
     staged 32-vertex blocks, a single fence, and the worker pool for big batches.
   - The restart, winding, carry-over, `maxv` cut-off and diagnostics logic is unchanged. The
     loop only records the index instead of fetching it.
   - **Off switch:** `STRIPX_PERVERTEX=1`. `VFETCH_LEGACY=1` also keeps the old behaviour.
2. **Dedup in `rsx_fetch_plan_range`, for every path.**
   - If an output's guest vertex is one of the last 4 distinct vertices decoded, its decoded
     256 bytes are copied instead of decoded again.
   - **Off switch:** `VFETCH_NODEDUP=1`.

## Does it change what is drawn?

No:
- `test_strip_batch.c`: the per-vertex output, the batched output and the batched+dedup output
  are byte-identical (497k vertices).
- `test_vfetch_plan.c` was rerun with dedup on: 4500/4500 cases are still bit-identical to
  `rsx_fetch_attrib`, across all topologies and index sizes.

One caveat. Decoding is a pure function of the guest bytes, so dedup can only differ from the old
path if the guest rewrites a vertex in the microseconds between its three decodes. §26 suspects
exactly that race (`VRACE_DBG`). With dedup, the three copies of a vertex are always identical.
The old path could mix two versions within one batch. To compare the cracks with and without
dedup, use `VFETCH_NODEDUP=1`.

## Expected effect

This is inferred from the table, not measured in the game. If the match's vertices now cost about
0.023 µs instead of 0.159 µs, `vtx` should fall by roughly 85%. The `cyc/vert` figure on the
`[PERF]` line should drop from ~379 to ~60 or below. Run `PERF=1`, then
`PERF=1 STRIPX_PERVERTEX=1`, and compare.
