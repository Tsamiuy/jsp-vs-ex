
## 35. Neon grass / saturated sky: auto-exposure stuck, luminance target never read back (2026-10-21) — STUDY, patches not yet run

See `COLOR_EXPOSURE.md` and `patches\color-exposure\` (four opt-in patches + README).
- The tone-map (fp `0x0FC9FC01`, `rsxcap_flush1` op325) takes its exposure from constant `c1.w`,
  which the PPU computes from the 20x11 average-luminance target `0x039FFA40` (ops 321-324). The
  backend never writes any render target back to guest memory and the game does not copy it with
  NV0039 (no `[GCM2D-UNH]` in any log), so the PPU reads black: `c1.w = 0.72 / 1e-4 = 7200` in all
  six captured tone-map frames across two scenes. With `Lm = L·E/(L·E+1)` and `out = 1-exp(-4·c·Lm)`,
  E = 7200 saturates `Lm` for every pixel: grass (0.1,0.5,0.05) comes out (0.33,0.86,0.18) instead of
  (0.12,0.48,0.06), the sky clips. Fix: `lum-readback.patch` (`LUM_READBACK=1`); A/B:
  `tonemap-exposure.patch` (`TONEMAP_EXPOSURE=1`, logs `[TONEMAP]`).
- `_bx2` / biased-remap fetches (upstream 122690f) ported as `tex-expand.patch` (`TEX_EXPAND=1`),
  with a `[FPEXPAND]` log that reports use even when off. Use by J-Stars is unknown: the shaders
  are not stored raw on disk (no Cg headers; `fp_fail.bin`'s bytes absent from ELF and CPKs), so
  only a run can tell. Offline: decompiler compiles, 21/21 tests.
- SET_SHADER_PACKER (sRGB writes) is never applied by the D3D12 backend; `color-diag.patch` logs
  it (`[RSXPK]`) and adds `addr=` to `[TEXFMT]`.
- Ruled out: scene RT precision (it is 8-bit A8R8G8B8, HDR packed in alpha), blend/clear decode,
  Namek's teal/green palette (canon).
