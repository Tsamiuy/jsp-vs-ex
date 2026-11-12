# Shader-hash memo: CPU_PROFILE §11 item 4 + the vertex-program hashes (2026-10-23)

`shader-hash-memo.patch` applies to `ps3recomp-src` at `6020215`. `git apply --check` passed on
the main checkout. **Not built or run.** Default on; **`SHADER_HASH_MEMO=0`** restores the old
code paths.

The drawing thread re-hashes shader microcode on every draw. This patch stops it from hashing a
program it has already seen. All three memos are **exact**: the same answer as the full hash.

| where | before | after |
|---|---|---|
| `vp_get_fp_pso` (fragment program, every draw, ~1,100 a frame) | `rsx_fp_program_size` + `rsx_fp_code_hash` (byte-wise FNV, a serial multiply per byte) | `rsx_fp_code_hash_memo` (new, `rsx_fp_decompiler.c`): one walk copying the instruction blocks the hash would visit and folding them into a 64-bit key; a per-thread 256x4 table keyed by content (J-Stars copies programs into a ring, so addresses don't repeat) returns the stored hash only when the block count matches and the blocks are byte-identical (`memcmp`). Inline constants are skipped, as the hash skips them. A miss calls `rsx_fp_code_hash` itself. |
| `vp_get_vs` (vertex program, every new draw record) | size scan + byte-wise FNV over the program | reuse the hash while `rsx_state.vp_gen` is unchanged |
| `vf_input_mask` (vertex program, every FIFO index batch, ~2,000 a frame, *inferred*) | size scan + word FNV | reuse the mask while `vp_gen` is unchanged |

**`vp_gen`** is a new `rsx_state` field (`rsx_commands.h`). `rsx_commands.c` gives it a fresh
value from a global counter, never 0, on every `SET_TRANSFORM_PROGRAM` word written and every
change of `SET_TRANSFORM_PROGRAM_START`. Those are the only writers of `vp_ucode`,
`vp_ucode_bytes` and `transform_program_start` (grep). A re-initialised state gets 0, which is
never memoised. The memo also checks the state pointer.

Also: `FP_CONSTBUF` was read with `getenv` twice per draw; it is now read once.

Files: `rsx_commands.c/.h`, `rsx_fp_decompiler.c/.h`, `rsx_d3d12_backend.c` (call sites only,
+58/-12; **for the AI assistant to apply**).

## Checks (offline)
- **Compiles:** `rsx_commands.c`, `rsx_fp_decompiler.c`, `rsx_d3d12_backend.c` and
  `rsx_vulkan_backend.c` (uses `rsx_state`) with clang-cl `/Zs`. The warnings are the same as on
  the unpatched files.
- **Exact** (`test/test_fpmemo.c`, linked with the patched `rsx_fp_decompiler.c`; `runfpm.bat`
  builds it): 300,000 calls on 40 programs (8-247 instructions) copied around a 1 MB ring, with
  constants re-patched every call, code edited in place, END moved, and the byte limit sometimes
  cut short. **0 hash mismatches and 0 size mismatches** against `rsx_fp_code_hash` /
  `rsx_fp_program_size`.
- **Cost per fragment-program lookup** (including a copy of the program into the ring):
  **235 instructions 4.06 -> 0.46 µs; 40 instructions 0.70 -> 0.09 µs.**

## Expected (*inferred*)
- Fragment program: `[PERF]` shows ~1.1-1.3 MB hashed per frame (~1 KB a draw), so roughly
  **~1 ms/frame** saved.
- Vertex programs: 0.5-4k cycles per index batch for the mask (tools\vphash_bench.c). The per-record
  hash is byte-wise, so ~4x that per draw record. Together **~1-3 ms/frame**, depending on the
  program sizes.
- The drawing thread sets the frame, so fps can rise a little, towards 30 in the demo fight.
  `prepass ... pso` and `vtx` in `[PERF]` should drop.

## Run for the AI assistant: demo fight before/after
As for runtime-overheads (`perf\rto`): `thread_cpu.ps1` over 360-410 s, fps and `[PERF4]` over the
600 s run, and screenshots t360/t380/t400.
1. Before: `SHADER_HASH_MEMO=0`.
2. After: defaults.

Compare `[PERF]` `vtx` (s and cyc/vert), `prepass` and `pso`, `[PERF2]` `drain`, fps, `[PERF4]`,
and the screenshots: they must match `SHADER_HASH_MEMO=0` within the usual run-to-run diff.
`[PERF5]` `pso create` should stay ~0 in steady fights. A rise would mean a key changed, which
shouldn't happen.
