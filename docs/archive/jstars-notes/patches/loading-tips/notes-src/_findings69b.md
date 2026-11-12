- **Addendum (17:20):** the AI assistant's run I (`SCREEN_AS_TEX=0`) also made the **boot logos** appear
  for the first time, so the size-only rule swallowed them too; they are uncompressed 1280x720
  (the `[TEXFMT]` key is per unit/format/width, so they share the RT's entry and never got a line
  of their own), which a compressed-only exclusion would miss. The patch now uses the rule the
  heuristic needed from the start: frame copy only for a texture the RSX could have written
  (uncompressed) whose guest bytes are empty (256 samples over the whole image); anything the CPU
  wrote keeps its own bytes. Switch renamed to `SCREEN_AS_TEX_FORCE=1` (old size-only rule).
