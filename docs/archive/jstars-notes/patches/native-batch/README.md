# Native strips: batched vertex fetch (2026-10-20)

`native-batch.patch` applies to `ps3recomp-src` at `e6feaa6`. On 2026-10-20 `git apply --check`
passed against the main checkout. It changes only `upload_strip_vp_native` in
`rsx_d3d12_backend.c`. **It has not been built or run in the game.**

## Why native strips cost ~4,000 cycles per vertex (FINDINGS §28)

`upload_strip_vp_native` writes each vertex in two costly ways:
- **It fetches every vertex on its own** through `vf_one`. That is a one-vertex plan range:
  staging memset, page-cache reset, store and sfence, all per vertex.
- **It copies every join's repeated vertices back out of the upload heap.** Each join copies 2
  or 3 vertices of 256 bytes with `memcpy(&out[o], &out[o-1])`. The upload heap is
  write-combined, and CPU reads from it are uncached and very slow. Match strips are mostly
  3-index pieces, so a join comes every few vertices.

## The patch

- The loop is unchanged: restart handling, joins, odd-position padding, the `maxv` cut-off and
  the carried `s_strip_pending`.
- It now writes **the guest vertex behind each output into a list**. A repeated vertex gets its
  source's index.
- One `vf_fetch` then decodes the list (`RSX_FETCH_VLIST`, from e6feaa6). It uses staged blocks,
  one fence and the worker pool. A repeat is copied from the vertex just decoded, which is
  already cached.
- One output can't be named by the list: a join's first repeat at `o == 0`, which copies the
  previous call's last vertex. It is still copied from the heap, after the fetch, at most once
  per call.
- **Off switch:** `STRIP_NATIVE_PERVERTEX=1` restores the old loop. `VFETCH_LEGACY=1` does too.

## Evidence (standalone, `test/test_native_batch.c`)

- The test copies both loops: per-vertex and batched.
- It uses the J-Stars character layout (8 attributes, stride 64) and a match-like index stream:
  mostly 3-index pieces, 0xFFFF restarts.
- The stream is fed in 256-index calls that continue one record, so joins carry across calls
  and the `o == 0` case occurs.
- Output: **byte-identical**, 383,791 vertices both ways.
- Timing into a real D3D12 upload heap (laptop GPU):

| path | per vertex | 384k vertices |
|---|---|---|
| per-vertex (current default) | **2.22 µs** (~4-5k cycles, as measured in-game) | 853 ms |
| batched (this patch) | **0.031 µs** | 12 ms |

Expected in-game: `vtx` falls by ~98% and `cyc/vert` drops from ~4,000 to below 100. This is
inferred from the test. Compare `PERF=1` against `PERF=1 STRIP_NATIVE_PERVERTEX=1`.

## Does it change what is drawn?

No. The test above gives byte-identical output.

One caveat. Old joins copied the bytes already written. With dedup on (the default), the new path
copies the vertex it just decoded from the same guest bytes, which is the same thing. With
`VFETCH_NODEDUP=1` a repeat is decoded again. That is identical unless the guest rewrites that
vertex in between.
