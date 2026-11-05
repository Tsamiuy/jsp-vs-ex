# fp-bem: translate TEXBEM / TXPBEM / TXD fetches (missing ground patches)

Patch: `fp-bem.patch` (`libs/video/rsx_fp_decompiler.c`, +23/-11), on 5896aa6. `git apply --check` passes.
NOT compile-checked (no compiler on my side). Not behind a switch: it changes how three opcodes are translated.

## Evidence (the maintainer's 720p session, jstars\run\play_stdout.txt / play_stderr.txt, 2026-10-27 ~18:53 local)

- The only fragment-program failure of the session:
  `[FP] decompile fail (fp=0x0FCB3F81) n=-1 buffered=1 nconst=13 size=624: | 00 00 34 00 ...`
  and in stderr `[RSX fp] gamma fetch opcode 52 unsupported on unit 0`.
- Decoding the first instruction (halfword-swapped): word0 = 0x34000000 → opcode 0x34 = **TXPBEM**, texture
  unit 0. The game marks unit 0 as gamma (sRGB), and the decompiler rejects TXD / TEXBEM / TXPBEM on a gamma
  unit (`return -1`), so the whole program fails, its PSO is never built, and every draw using it is skipped.
  That's the "ground patches missing at certain spots" (which surfaces exactly: inferred from the maintainer's report,
  not from a capture). The program is 624 bytes (39 instructions), 13 constants.
- Even without gamma these three opcodes weren't translated: they fell to the `TODO: unhandled FP opcode`
  default, writing nothing.

## The change

- TEXBEM → same as TEX, TXPBEM → same as TXP (projective). The bump-environment offset comes from fixed-function
  state these programs don't use; RPCS3 decodes them the same way.
- TXD → `SampleGrad(sampler, src0.xy, src1.xy, src2.xy)` (explicit derivatives, RPCS3 TEXTURE_SAMPLE2D_GRAD).
- The three now take the normal gamma and sign-expand wrapping like TEX/TXP; the gamma rejection is removed.
- Unchanged: a depth-comparison (shadow) unit with these opcodes is still rejected (separate check).

## Check (the AI assistant)

Same 720p stage/spot where the maintainer saw the missing patches: the `[FP] decompile fail (fp=0x0FCB3F81)` line and
the `gamma fetch opcode 52` line should be gone, and the ground patches present. `FP_DUMP`/RSX_CAP of a frame
with that program shows the translated TXPBEM as a projective Sample wrapped in `_rsx_gamma_input`.
