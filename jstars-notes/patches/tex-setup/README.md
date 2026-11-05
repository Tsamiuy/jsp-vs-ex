# Texture setup: per-batch memo, outcome counters, opt-in once-per-frame check (2026-10-20)

`tex-setup.patch` applies to `ps3recomp-src` at `e32a781`. On 2026-10-20 `git apply --check`
passed against the main checkout, including its uncommitted edits. It changes only
`rsx_d3d12_backend.c`. **Not built or run.**

## What the timing run says

Source: `run\perf2_run.txt`. In each 20-batch window, `tex` is 0.55 s (43%) for 187,810
`vp_upload_tex_slot` calls, which is ~2.9 µs per call. The rest of the SRV loop is small
(srv 0.59 s in total).

What a call does, from the code:
- **Re-bind within the same batch.** The slot is already `used`. The call scans the 96 slots and
  returns, which costs roughly 0.1 µs. An older run had 306k calls in 0.03 s.
- **Texture held from an earlier batch.** The call takes a sparse checksum of the guest bytes:
  1024 reads strided across the whole texture, each one likely a cache and TLB miss.
- **Changed or new texture.** The call does a full decode and copy, and sometimes creates a new
  resource.

`used` is reset at every `render_frame`. Since the mid-frame flush (`b7efcb5`), a match frame is
several batches, so every held texture is checksummed again in each batch. My best guess is that
the 2.9 µs average comes from checksums and re-uploads, not from re-binds. That is inferred from
the code; the counters below will measure it.

## The patch

1. **`[PERF3]` line** after `[PERF2]`. It gives the count and seconds per outcome: `memo`,
   `bound`, `csum-same`, `frame-ok`, `reupload`, `new`, `no-slot` and `fail`, plus the number
   of resources created. This is measurement only.
2. **Per-batch memo (default on, off with `TEXCACHE_LEGACY=1`).** It answers a re-bind of a
   texture that an earlier draw in the same batch already resolved, without the slot scan.
   - It cannot change what is drawn. It returns only a slot that the scan would also return:
     the same key fields, still marked `used` in this batch, and a key only ever lives in one
     slot.
   - `TEX_BUDGET` also disables it, because the budget counts every call.
   - Expected gain: only the re-bind part, a few percent.
3. **`TEX_VERIFY=frame` (opt-in, default off).** A held texture is checksummed once per guest
   frame (keyed on the flip count) instead of once per batch.
   - This is how things worked before the mid-frame flush, when a frame was a single batch.
   - It differs only if the game rewrites a texture's bytes in place within one frame.
   - It is off by default so it can't hide a texture bug. Turn it on to measure how much the
     per-batch checksums cost.

## What to run

`PERF=1`, then `PERF=1 TEX_VERIFY=frame`, and compare `tex` on the `[PERF]` line with the
`[PERF3]` breakdown.
- If `csum-same` dominates, the per-batch checksums are the cost. `TEX_VERIFY=frame` shows the
  gain, and a proper fix would be to verify once per frame by default, or to run the checksums
  in parallel on the vertex worker pool. Both keep the bytes compared the same.
- If `reupload` or `new` dominates, slots are thrashing, or textures really are changing.

## Worth checking for the pale/untextured surfaces

`VP_TEX_SLOTS` is 96. If one batch binds more than 96 different guest textures, the extra ones
get **no slot**. `vp_upload_tex_slot` returns -1 and that unit gets a **null SRV**, so it renders
untextured or flat. A non-zero `no-slot` in `[PERF3]` would make this a real cause of the pale
surfaces. Nothing in this patch changes that behaviour.
