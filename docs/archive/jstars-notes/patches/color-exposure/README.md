# color-exposure: four opt-in patches for the colour problems (2026-10-21)

Write-up: `docs\notes\COLOR_EXPOSURE.md`. Base: `jstars-port` at e799d57. Each patch applies on
its own with `git apply`; applying all four in this order also works (checked on a copy here):

| patch | switch (all off by default) | what it does |
|---|---|---|
| `tex-expand.patch` | `TEX_EXPAND=1` | Upstream 122690f: `_bx2` fetches and biased-unsigned units return signed values. Logs `[FPEXPAND]` once per program that has either, switch on or off. |
| `color-diag.patch` | none (logs only) | `[TEXFMT]` (with `TEXFMTDBG=1`) prints the whole ADDRESS word (`addr=`); `[RSXPK]` logs SET_SHADER_PACKER changes. |
| `tonemap-exposure.patch` | `TONEMAP_EXPOSURE=<v>` | Forces the tone-map's exposure constant `c1.w`; logs the live value as `[TONEMAP]`. A/B for the auto-exposure cause. |
| `lum-readback.patch` | `LUM_READBACK=1` (`LUM_READBACK_MAXPX`, default 4096) | Writes small offscreen colour targets (the 20x11 average-luminance target) back to guest memory each frame, so the game's exposure loop sees real data. Logs `[LUMRB]`. |
| `lum-readback-2.patch` | same switches | Pass 2 fix on top of lum-readback: the 20x11 target is a 1280x720 resource (clip dims < 16 become window size), so v1 never read it. v2 copies only the guest clip rect, applies MAXPX to it, logs rect, resource size and `meanRGB`. Applies to HEAD d8de15f (checked 2026-10-21 ~15:00). |
| `lum-readback-3.patch` | same switches | Pass 3 fix on top of v2: records SET_CONTEXT_DMA_COLOR_A..D in `rsx_state` (rsx_commands.c/.h) and resolves the readback target through it. The 20x11 target is at IO offset 0x039FFA40 in main memory; v2 wrote the local alias. Per-target `[LUMRB]` log with `dma=` and `(main|local)`. Applies to HEAD cc50482; `--ignore-whitespace` on the CRLF tree (checked 2026-10-21 ~16:30). |
| `rt-drawlog.patch` | `RT_DRAWLOG=<hex>[,<hex>...]` (`RT_DRAWLOG_MAX`, default 400) | rsx_commands.c only. Logs `[RTDRAW]` surface binds, colour_target changes, clears, BEGIN (prim, fp, attrib0, viewport) and draw batches for the listed colour surfaces, before any backend filtering. For the never-drawn glare layer 0x0E590000. Applies to 1ef9684; gcc syntax check passes. |
| `cap-alpha.patch` | `RSX_CAP_ALPHA=1` | Backend capture writer: every capture BMP gets a grey `<name>_a.bmp` with the alpha channel. The HDR encode divides the scene by its alpha, so this shows the HDR scale behind the burned panels. |
| `lum-readback-4.patch` | same switches as lum-readback | Fix on top of v3: routes SET_CONTEXT_DMA_COLOR_A..D into `process_surface_method` (the range gate skipped them, so v3 always logged `dma=0`). rsx_commands.c, 7 lines. Applies to de12d74; gcc syntax check passes (checked 2026-10-21 ~18:00). |
| `tonemap-log.patch` | none new (uses `LUM_READBACK=1`, `TONEMAP_EXPOSURE`) | Backend, on top of tonemap-exposure + lum-readback-2/3: the `[TONEMAP]` match no longer requires c1.y < 0.001 (it went quiet once the readback worked), logs `c1=(63.75, La=, thr=, E=)` on the first draws, every 120th and on a >2% change; `[LUMRB]` adds the PPU's log-average luminance `Lgeo` and the predicted `E~=0.72/Lgeo`. Applies to c8d2329; not compiled. |
| `fp-gain.patch` | `FP_EXPOSURE=<E>` or `FP_GAIN=<g>`, with `FP_GAIN_KEYS=<code,...>` and/or `FP_GAIN_MININSTR=<n>`; `FP_GAIN_LOG=1` | Backend only, experiment (keep off by default): brightness of chosen fragment programs only. FP_EXPOSURE applies the tone-map's per-pixel factor L*E/(L*E+1) to those programs' output, which reproduces `TONEMAP_EXPOSURE=<E>` on just them (run with LUM_READBACK off); FP_GAIN is a plain rgb multiplier. Programs are chosen by code hash (`[FPGAIN] ... code=` or the new `code=` in RSX_CAP shader headers) or by instruction count (characters are the ~200+ instruction programs). Applies to c8d2329 (HEAD and CRLF tree with --ignore-whitespace); not compiled. |
| `cap-code.patch` | none (capture output) | Backend, 23 lines, applies to ba84d89 (HEAD and CRLF tree with --ignore-whitespace): the RSX capture manifest `DRAW` line gets `code=0x%08X` (the FP_GAIN_KEYS / shader-cache program hash) appended. Needed to map programs to draws (characters vs stage) for FP_GAIN_KEYS; see COLOR_EXPOSURE.md Pass 9. |
| `fp-gain-keys.patch` | none new (raises the `FP_GAIN_KEYS` cap from 32 to 128 keys) | Backend, on top of fp-gain (applies to e0709b6): one constant, so the full character key list (73 programs, `color\capcode\fp_gain_keys.txt`) fits. |
| `lum-rt-drawlog.patch` | `LUM_RT=<hex rt offset>` (unset = off) | Backend (applies to d89b94e and the current working tree): logs `[LUMRT] draw_arrays/draw_indexed #n prim first count noct ready vb` for every draw the FIFO hands the backend while the colour target is `LUM_RT`, before the guards that drop draws silently. Run with `LUM_RT=0E590000` together with lumflag-trace-4. |

Removed from this tree: the five `lumflag-trace*.patch` log patches, because they carried context lines of
recompiled game code. `COLOR_EXPOSURE.md` and `FINDINGS.md` still describe what they logged.

Files touched: `libs/video/rsx_fp_decompiler.c/.h`, `rsx_texture_layout.c/.h` (tex-expand);
`rsx_commands.c/.h` (color-diag); `rsx_d3d12_backend.c` (all four).

Verification done in the analysis container (no D3D12 there): the decompiler, texture layout and
`rsx_commands.c` compile with gcc; `test_expand.c` passes 21/21 (upstream's cases plus the opt-in
gating). The backend hunks follow the file's own patterns (`cap_readback_write`, `dr_gamma_key`)
but were **not compiled**: expect at most a trivial fix-up. Build and run only by the AI assistant that owns the backend.

Suggested order: color-diag + tonemap-exposure first (cheap A/B), then lum-readback, then
tex-expand if `[FPEXPAND]` reports anything. Test steps are at the end of `COLOR_EXPOSURE.md`.

**Line endings (checked 2026-10-21 ~13:40):** the patches are LF and apply cleanly to the
committed HEAD (2df51e8, whose blobs are LF). The working tree had CRLF conversions at that time
(`rsx_d3d12_backend.c` showed a 9179-line CR-only diff, and `rsx_fp_decompiler.h`,
`rsx_texture_layout.c`, `rsx_commands.c/.h` were CRLF too), on which `git apply` fails on
context. Either restore LF first (`git checkout -- <file>` on a file whose only change is CR, or
`git diff --ignore-cr-at-eol` to confirm) or apply with `git apply --ignore-whitespace`.
