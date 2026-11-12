# FP register files: 64 entries, not 48

Patch: `fp-regs64.patch`, `libs/video/rsx_fp_decompiler.c` only, on 283ad47 (`git apply --check`
passes). Opt-in: `FP_REGS64=1`. Not built or run in the game.

**Why:** fragment-program register indices are 6 bits (`FP_OUT_REG_MASK`, `FP_SRC_REG_MASK`:
0..63), but the decompiled HLSL declares `float4 r[48]; float4 h[48];`. A program that uses
r/h 48..63 fails D3DCompile with **X3504 "array index out of bounds"**.

The Free Battle run's `stdout.txt` has exactly that for 3 programs: fp `0x0FC27A81`, `0x0FC268C1`
and `0x0FC10641`. Their draws then use the fallback pipeline instead, the debug-font one. It samples
t0, discards where t0.r <= 0.5, and outputs the vertex colour. A surface drawn that way comes out
flat and dark.

**Checked offline:** a one-instruction test program that writes r[50] fails fxc with X3504 when
declared with 48, and compiles with `FP_REGS64=1`.

**Test (Free Battle, Yoshiwara):** run with `FP_REGS64=1`. The three `[FP] PS compile FAIL` lines
should be gone; then look at the floor. To see which surfaces those programs draw, run
`FP_PICK=0x0FC27A81` (writes `duck_fp.hlsl`), or use `FP_IDCOLOR=1` with and without the switch.
