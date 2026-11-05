**Update 3 (2026-10-22 ~17:15, AI assistant thread): cause and fix.** Run F (`cap4\frame_05`,
`RSX_CAP_TEX=1`) dumped the snapshot's guest bytes, and they decode to the complete tips page
(`stages\loading\snapshot_decoded.png`); `TEX_NOCACHE`, `TEX_VERIFY=frame` and `TEX_MIPS=0` (runs
G, H) still gave a white screen. The fault is the `SCREEN_AS_TEX` heuristic in the backend's
per-draw texture bind: a texture that is exactly 1280x720 and not one of our offscreen RTs gets the
copy of the previous frame bound instead of its own bytes (added for the water reflection). The
snapshot qualifies, so the loading screen drew the white fade frame, then itself. Fix
`patches\loading-tips\loading-tips-fix.patch`: compressed formats are excluded from the heuristic
(default on, `SCREEN_AS_TEX_ANYFMT=1` reverts). Confirmation without code: `SCREEN_AS_TEX=0`.
Details FINDINGS §69.

