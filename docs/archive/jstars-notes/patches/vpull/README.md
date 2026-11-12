# VPULL: vertex decoding on the GPU for indexed strips (2026-10-23)

`vpull.patch` applies to `ps3recomp-src` at `5f73fea` (the shader-hash memo); `git apply --check`
passed on the main checkout. **Not built or run.** **Opt-in: `VPULL=1`.** With `VPULL` unset or
`0` nothing changes: same root signature, same buffers, same shaders, same draws.

Scope and reasoning: `CPU_PROFILE.md` §12.

## Size
4 files, **+449 / -6 lines**, patch 36 KB:

| file | lines | what |
|---|---|---|
| `libs/video/rsx_vpull.h` (new) | +179 | the HLSL decoder, decode tables, layout check, guest byte copy |
| `libs/video/rsx_d3d12_backend.c` | +247 / -6 | buffers, root signature, upload, record, replay, `[VPULL]` stats. **For the AI assistant to apply.** |
| `libs/video/rsx_vp_decompiler.c/.h` | +23 | pulling mode for the vertex shader translator |

## What it does
Today every strip vertex is decoded on the CPU by 3 worker threads into a 256-byte slot. That's about
720k slots and 185 MB per frame in the demo fight. The drawing thread also builds the vertex lists and
the strip joins.

With `VPULL=1`, an indexed triangle strip whose attributes form one interleaved stream works like this:
- **Per FIFO index batch, on the drawing thread:**
  1. Read the indices once for min and max.
  2. Copy the guest bytes of vertices min..max into a 64 MB byte ring (x2 parity). MAIN memory is
     resolved one IO page at a time.
  3. Write the indices to the STRIP_INDEXED index buffer as `k + (vertex - min)`, with each restart
     turned into a strip cut.

  No 256-byte slots, no decode jobs, no join or parity logic.
- **Per record:** a 512-byte table (`b2`) holds each attribute's offset, stride, type and components,
  plus the constant register values.
- **Vertex shader:** `SV_VertexID` is the vertex number in the record's region. `vpull_attr` reads
  the bytes (`t8`) and decodes all 7 types big-endian, exactly as `rsx_fetch_plan_vertex` does:
  - F32 is a byte swap.
  - Half floats use the same bit rules.
  - S32K and UB256 are integer-to-float, which is exact.
  - UB, S1 and CMP read their value from tables (`t9`, 275 KB) that the CPU fills with the same
    divisions. GPU division is not correctly rounded, so the tables keep the values identical.
- **Draw:** `DrawIndexedInstanced` with a strip cut, as STRIP_INDEXED does. The pulling shader is
  its own VS cache entry, so it gets its own pipelines.

**Takes the CPU path as before:**
- non-indexed strips;
- quads, fans and lists;
- attributes with a frequency divider;
- attributes with different strides or memory locations;
- unknown types;
- attributes more than 4 KB apart;
- a new record whose first batch spans over 4096 vertices and more than 8x its index count;
- `VFETCH_LEGACY`.

A batch that continues a pulled record is always pulled, so the strip never breaks.

## Checks (offline)
- **Bit-exact decode on the GPU** (`test/test_vpull.c`, D3D11 compute shader with the same
  `RSX_VPULL_HLSL`, against the real CPU decoder `rsx_fetch_plan_range` from `rsx_vertex_fetch.c`):
  - 400 random layouts, all pulled, with all 7 types, 1-4 components, strides 1-64, odd offsets,
    constant attributes, base index and base offset, LOCAL and MAIN with shuffled IO pages.
  - The bytes went through `rsx_vpull_layout_from_plan` + `rsx_vpull_copy` + the index rebasing,
    in batches of 256 indices.
  - **18,034,688 attribute values compared, 0 mismatches.**
  - **One known difference:** an attribute that straddles a 1 MB IO page *when the next IO page is
    not the next guest page*. The CPU decode reads on linearly past the page and the copy follows the
    IO table; this shows as 10 values in the test, which shuffles the pages on purpose. Real
    `cellGcmMapMainMemory` mappings are contiguous, so this is not expected in the game (inferred).
- **The shader compiles** (`test/test_vpull_vs.c`): a vertex program decompiled in pulling mode
  compiles as vs_5_0. Its only input is `SV_VertexID` and it binds t8, t9, b0 and b2. Normal mode
  still declares ATTR0-15.
- **Compiles:** `rsx_d3d12_backend.c`, `rsx_vp_decompiler.c` and `rsx_vertex_fetch.c` with clang-cl
  `/Zs`. The warnings are the same as before.
- **Not checked:** D3D12 pipeline creation and drawing in the game, GPU cost, the picture.

## Expected (*inferred*)
- **CPU:** the 3 decode workers go idle (about one core). The upload is the guest bytes the batches
  span plus 4 bytes per index, instead of 185 MB/frame.
- **Drawing thread:** the strip list and join loop goes. What's left is two passes over each batch's
  indices and one memcpy per batch. `vtx` should drop from ~10 ms to a few ms.
- **GPU:** fewer vertex shader runs (strip cuts, no joins), but each run decodes its attributes. The
  shader is 816 static instructions for 3 attributes, with branches on the type.
- **fps:** at best the demo's 3-vblank frames go back to 2 (ceiling ~30).

## Run for the AI assistant: demo fight, three runs
As before (`perf\rto`): `thread_cpu.ps1` over 360-410 s, fps and `[PERF4]` over the 600 s run,
screenshots t360/t380/t400, and `PERF=1`.
1. `VPULL=0` (baseline).
2. `VPULL=1`.
3. `VPULL=1` again (run-to-run spread).

What to look for:
- `[VPULL] records / batches / indices / copied MB / CPU path: layout, sparse / dropped` after each
  `[PERF4]`. "copied MB" per window against today's ~3.7 GB is the upload saving. "layout" and
  "sparse" show how much still takes the CPU path. **"dropped" must stay 0.**
- `[PERF]` `vtx` and `render_frame`, `[PERF2]` `drain`, `[PERF5]` pso creates (the new pipelines
  are built once), fps, `[PERF4]`, and per-thread CPU (the workers should idle).
- The screenshots against `VPULL=0`, within the usual 1.5-3.8 whole-frame diff. If a mesh is
  missing or torn, `VPULL=0` brings it back, and a `STRIP_DUMP` frame narrows it down.
- `[VPULL] vertex pulling on` should appear at start. `buffer allocation failed` means VPULL is off.
