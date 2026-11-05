# Per-draw cost, round 2: VPULL_MEMO and TEX_HINT (2026-10-25)

`drawcost2.patch` (+57/-5 in rsx_commands.c, rsx_commands.h and rsx_d3d12_backend.c) applies to
`ps3recomp-src` at 3f6d509 (`git apply --check` passes). It compiles with the same warnings as
before. **Not built or run.** Both parts are on by default and exact (same results).

| switch | what | share of the drawing thread's own samples it targets (perf\perf6\leaf_pdb) |
|---|---|---|
| `VPULL_MEMO=0` off | `vpull_prepare` reuses the last fetch plan, layout and table while the vertex program (`vp_gen`) and the new vertex-array state generation (`vattr_gen`) are unchanged. A strip record is many 256-index batches with the same state; each batch used to rebuild the plan. | `vpull_prepare` 12.6% |
| `TEX_HINT=0` off | the texture-slot scan (up to 1024 slots, for binds the per-batch memo misses) starts at the slot last seen holding this key, if it still holds it. A key lives in one slot only, so that is the slot the scan would have found. | `vp_upload_tex_slot` 9.3% |

`vattr_gen` (rsx_commands.h) gets a fresh value from the same global counter as `vp_gen` on every
write that changes what `rsx_fetch_plan_init` reads:
- vertex-array format (0x1740+) and offset (0x1680+);
- DATA4F (0x1C00+);
- base offset (0x1738) and base index (0x173C);
- the frequency-divider op (0x1FC0).

**Offline (`test/test_vattr_gen.c` with the real rsx_commands.c):** 2,000,000 random methods over
all of those plus constants, textures and viewport state. **1,360,048 state changes, 0 without a
new `vattr_gen`.**

TEX_HINT is reviewed in code, not run:
- the hint only chooses where the scan *starts*;
- the slot is re-checked against all five key fields;
- everything after the match (used / verify / checksum / redo) is unchanged.

## Expected (*inferred*)
Together about 0.7-1.7 µs off the 12-16 µs per draw. The two shares above are of the thread's
*exe* samples, which are ~30% of its samples, against the busy time the AI assistant's drawcost.py measures.

## Run for the AI assistant (us/draw is the metric; same power mode)
Hidden Leaf with FIXED_CHARS=1, `PERF=1 GCM_FLIPCOUNT=1`, 3 runs each, alternating:
- defaults;
- `VPULL_MEMO=0 TEX_HINT=0`.

Compare `tools\drawcost.py` us/draw (draws / decode / prepass / record parts) and the screenshots.
`[VPULL] ... CPU path: layout` should be the same in both (the memo keeps the n_cpu count).
