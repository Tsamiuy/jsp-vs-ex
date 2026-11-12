
**Reading of runs C, D and E (2026-10-22 16:30):**
- Run C: no `[NV3089G]`, no `[GCM2D-UNH]` from Stage Select to the fight HUD, so the game uses
  no RSX 2D transfer for the snapshot: the CPU (or an SPU job) reads the surface itself.
- Run D (`LUM_READBACK=1 LUM_READBACK_MAXPX=921600`, every used 1280x720 offscreen target written
  back one frame late): loading screen still white. Consistent with a read that happens right
  after a fence inside the frame that composes the page: a write-back that lands at the next
  drain is too late. (It also does not cover the display buffer, which lives only in the
  swapchain; if the snapshot reads the display surface, a write-back must come from there.)
- Run E: `MEMPEEK` shows the snapshot texture at 0xC082C900 holding the same DXT1 bytes on all
  141 frames 6380-6520 (`E3 20 A3 20 AA AA AA AA` repeated: c0=0x20E3, c1=0x20A3, a dark brown,
  index 2 everywhere), so it was not encoded inside that window. Run E's route reached the
  loading screen later than runs A/first capture: `cap3\frame_02` (6500) still has the summary
  screen (141 draws, no 0x0082C900 draw). `VRAMSCAN` printed nothing because no 64 KB block of
  0xC0800000-0xC2800000 changed in those frames (the switch works; the window was wrong for the
  surfaces and the time was before the encode).
- The white therefore comes from what the CPU reads out of guest memory for the composed
  surface: white, not zeros. Likely the game fills that surface's memory itself (a CPU clear to
  the page's white before the RSX composes the page on top), then reads it back after the fence;
  on hardware the RSX wrote the page over the fill, here only the GPU copy has it.
- Next: run F (`MEMPEEK_FRAMES=6300:7500`, `VRAMSCAN_BASE=0xCC600000 VRAMSCAN_FRAMES=6300:7500`,
  `GCM_REFLOG=1`, `RSX_CAP=6500:12:30 RSX_CAP_TEX=1`) to see the encode frame, the CPU writes to
  the UI layer / scene surfaces around it, and what the snapshot decodes to.
- Fix shape: at the fence that follows the page composition (or on the CPU fill, if VRAMSCAN
  shows one), run the recorded batch without present (the backend already does this in
  `vp_flush_if_full`), read the surface back synchronously (as `cap_readback_write` does) and
  write it into guest memory before the fence value is published. Gate it so a match frame never
  pays for it (only while a 1280x720 colour target was drawn by UI quads alone, or an explicit
  `RT_SNAPSHOT=<off>` list).
