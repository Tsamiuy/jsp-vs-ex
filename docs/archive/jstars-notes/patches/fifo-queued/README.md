# queued_flips on [VPCODE]: is the walker behind the guest at the beam draws? (logging only)

Patch: `fifo-queued.patch` (`libs/video/cellGcmSys.c` + `libs/video/rsx_d3d12_backend.c`, about +40), on
3ab2a5b. `git apply --check` passes, and both files compile syntax-only with no errors and no new kinds of
warning. Not built or run. It only adds fields to the existing `[VPCODE]` line, so it does nothing unless
`VP_CODE` is set.

`cellGcm_fifo_queued()` walks the FIFO from the walker's get to the guest's put (following JUMPs, bounded) and
counts flip words (0xFEADxxxx, both the lv1 form and GCM_FLIP_MARKER) and command bytes. `[VPCODE] #id ms=
flip=` now ends with `queued_flips=<n> queued_bytes=<n>`: the whole frames the guest has already queued
**beyond** this beam draw.

Run: the dmaclock settings, plus nothing new (`VP_CODE=9A64BF1C VP_CODE_N=50 VP_CODE_FROM=6800`, and
`SPU_DMA_RANGE...` as before, to keep the PUT timeline).

Reading:
- `queued_flips` ≈ 2 at the beam draws: the walker is two frames behind the guest, and the guest recycled the
  quads after its own frame pacing allowed. Fix: make the walker keep up (drain more than one flip per tick
  when frames are queued), or tie the guest's pacing signal (label or flip) to the walker.
- `queued_flips` 0-1: the walker keeps up, and the game itself draws quads written two frames earlier, which
  are zeroed in between by the PPU. The next step is then a PPU clear watch. That is harder, because dcbz
  (inline `memset` in the lifted code) and vector stores (`vm_raw_st16`) bypass `PPU_WWATCH`.
