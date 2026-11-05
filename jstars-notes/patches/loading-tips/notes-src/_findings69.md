
## 69. Loading-screen tips page, root cause: the "screen as texture" heuristic steals the snapshot (2026-10-22, AI assistant, read-only + patch)

Closes §66/§68. The snapshot at 0x0082C900 is right in guest memory (§68). What goes wrong is the
bind, not the upload or the cache:

- `rsx_d3d12_backend.c`, per-draw SRV window fill (`/* Display-sized sampler source -> the
  rendered frame ... */`): when a unit's texture is `s_d3d.width x s_d3d.height` and
  `off_rt_find(raw) < 0`, and `SCREEN_AS_TEX` is on (default 1), the draw gets `s_screen_copy`
  (the copy of the last completed frame, made for the water reflection, `screen_copy_capture`)
  bound at that unit and the guest-texture upload path is never reached (`continue`).
- The tips snapshot is a 1280x720 DXT1 texture that is not an offscreen RT, so every loading-
  screen draw of it sampled the previous frame. At that point the frame is the white fade (6450's
  end state), and from then on the loading screen draws itself: white in, white out. "Now
  loading..." (512x78) does not match the size test and shows.
- Consistent with every run: the guest bytes were right (run F), the cache re-checksums and
  `TEX_NOCACHE`/`TEX_VERIFY`/`TEX_MIPS=0` changed nothing (runs G, H), no 2D transfer (run C),
  `LUM_READBACK` irrelevant (run D), and the page quads themselves paint fine (run A).
- Fix: `patches\loading-tips\loading-tips-fix.patch` (default on, `SCREEN_AS_TEX_ANYFMT=1`
  reverts): the heuristic skips compressed formats (DXT1/3/5). The RSX cannot write compressed
  data, so a display-sized DXT texture is never a frame readback; the reflection's A8R8G8B8
  surface keeps the old path. No-code confirmation: `SCREEN_AS_TEX=0` (page shows, reflection
  lost for that run).
- Reusable lesson (REUSABLE.md candidate): a size-only heuristic that substitutes a backend
  resource for guest memory must at least exclude formats the GPU cannot produce, and should
  prefer the guest bytes whenever they are non-empty.
