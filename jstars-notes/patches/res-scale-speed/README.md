# RES_SCALE: where the extra GPU time goes, and safe speed-ups (2026-10-27, the AI assistant, read-only)

Read from the AI assistant's uncommitted RES_SCALE backend (ps3recomp-src working tree), RESOLUTION.md, the AI assistant's
thread, and the latest `jstars\run\stderr.txt` (21:24, RES_SCALE 3 with the zdown depth copies).

## Where things stand (measured in that run)
- **4K already runs at 27-30 real f/s** (10-flip windows from `[flip]` lines: mostly 28.4-29.9, dips to
  26.6-27.2).
- `[PERF6]` gpu-wait is 0.0 ms in 541 of 669 windows and at most 0.4 ms in the rest. So with zdown the
  GPU isn't what limits 4K on this RTX laptop; the CPU drawing thread is, the same as at 720p.
- `[PERF7]`: 690-800 depth snapshots per batch (each ~1.1 Mpx: the guest-size zdown output), 29-30
  target switches and 23 shared depth clears per batch. `[PERF7z]`: the scene zeta 0x0E960000 has
  ~93 snapshots per batch, and each shadow map (2048x1024, 1024x1024) has 57.
- Earlier (the AI assistant): 1440p 16 -> 18.5 f/s once the shadow maps were kept at 1x. The scene depth copies at
  full size were the rest; zdown removed that.

So the list below is about headroom (weaker GPUs, heavier views, future texture packs), and about
not breaking the picture. It isn't needed to reach 30 at 4K on this machine.

## What must not be repeated (the flat ground)
- The floor's fragment program samples **t15 = the scene's own depth** (FINDINGS §43 pass 3: it
  compares against it together with the shadow cascades). The AI assistant's first speed-up re-copied the depth
  less often, so the floor (and water) read an older depth and the ground went flat.
- Rule: any depth a draw samples must equal the depth as it was just before that draw (or be provably
  unread). Skipping a copy is only safe when nothing can read it. "Copy less often", "copy once per
  pass" or "reuse the copy from earlier in the frame" break this.
- zdown itself is safe on timing but keeps one sample per RS x RS block (the centre one). The floor's
  self-comparison at 4K therefore sees 720p depth. Look for 1-3 pixel stair-steps or halos where
  characters or rubble stand on the floor's shadowed parts (*inferred* possible look difference, not
  seen yet).

## Speed-ups that don't change the picture (ranked)

| # | idea | why it is exact | expected gain | risk | work |
|---|---|---|---|---|---|
| 1 | **(Done 2026-11-02, FINDINGS "Frame rate lows")** **Skip `dt_snapshot` for texture units the fragment program never samples.** In the record loop (`if (dr->tex_zt[_u] >= 0 && dt_snapshot(...))`), require `fp_info(dr->fp_addr)->tex_units` bit `_u`, and fall back to copying when fi is NULL. | RSX keeps textures bound across draws. A unit the program doesn't sample can't see the copy, and `dirty` stays set, so the next draw that really samples it still copies first. | Unknown until counted (step 0). If most of the ~700 snapshots come from a zeta left bound on t15 by draws whose program doesn't read t15, most of them go. | Very low: exact by construction. | Small |
| 2 | **Draws that sample a depth they don't write: no copy.** Bind that depth read-only (a second DSV with `D3D12_DSV_FLAG_READ_ONLY_DEPTH`, state `DEPTH_READ \| PIXEL_SHADER_RESOURCE`) and give the SRV the live resource. Depth write is bit 1 of `dr->zstate` (rsx_zstate_pack: test bit0, write bit1). | The draw can't change the depth it reads, so the live depth is exactly "the depth before this draw". | Every snapshot for water, particles and decals with depth write off. | Low if the states are right. The 2 barriers it saves also remove the depth decompress on transition (see 3). | Medium |
| 3 | **Fewer full-size depth transitions.** Each zdown transitions the full-size depth DEPTH_WRITE -> NON_PIXEL_SHADER_RESOURCE -> back. At 4K that is a 33 MB surface; on NVIDIA a depth-to-SRV transition can decompress it (*inferred*, not measured). Ideas 1 and 2 remove most of these. Otherwise, batch consecutive snapshots of the same zeta when nothing was drawn between them. | Same copies, fewer barriers. | Only matters if the GPU becomes the limit. | Low | Small |
| 4 | **Shadow-map snapshots (57+57 per batch at 1x).** They are re-dirtied ~57 times per batch, so casters and samplers interleave. Apply 1 and 2 to them too. | As 1 and 2. | Small (1x maps). | Low | Comes free with 1 and 2 |
| 5 | Shared depth clears (23 per batch) and target switches (30): fast clears at any size; leave them. | n/a | ~0 | n/a | n/a |

### Step 0: count before changing anything (diagnostic, ~10 lines next to the [PERF7] counters)
For every dt_snapshot call that actually copies, classify it:
- (a) the unit is not in `fi->tex_units` (idea 1 removes it);
- (b) the unit is sampled but the draw doesn't write depth, i.e. `!(dr->zstate & 2)` (idea 2 removes it);
- (c) sampled and written: the copy is required.

Print a/b/c per batch in `[PERF7]`. If (a)+(b) is small, ideas 1 and 2 aren't worth doing. At 4K the
GPU already keeps up here, so the measurement decides.
