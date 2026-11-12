# FIFO_FAST: shader constants straight to their handler (2026-10-25, the maintainer's yes)

`fifo-fast.patch` (+70/-1: rsx_commands.c +40, rsx_commands.h +4, cellGcmSys.c +26/-1) applies to
`ps3recomp-src` at db2a45d (`git apply --check` passes on the main checkout). **Not built or run.**
On by default; **`FIFO_FAST=0`** restores the old path. It also turns itself off when any of these
diagnostics is set, because they want to see every word: `TCONST_DBG`, `SEQ_DBG`, `RSX_TRACE`,
`GCM_SUBCH7`, `GCM_OBJDBG`, `GCM_SUBCH_GATE`, `GCM_VAL_WATCH`, `RSX_LIVE_FEED_DBG`, the live and
dispatch draw engines.

## Why (CPU_PROFILE §28)
- On Hidden Leaf the drawing thread is the frame's limit (~30-33 ms busy per heavy frame against a
  33 ms budget).
- About half of its own code time is `rsx_process_method`. For each FIFO data word that function
  runs a long chain of range and equality tests; `SET_TRANSFORM_CONSTANT` (0x1F00-0x1F7C, the
  vertex shader constants, the bulk of a fight frame's data) is handled after about 70 of them.
- Each word also goes through the walker's per-word diagnostics and dispatch.

## What
1. `rsx_tconst_fast()` (rsx_commands.c) is the constant handler on its own: the same slot and lane
   maths, the same `vertex_constants` write and the same `lo/hi/dirty` tracking.
   `rsx_process_method` calls it first, right after the sync hook and the `RSX_TRACE` hook, which
   don't concern these methods. Nothing earlier in the chain claims 0x1F00-0x1F7C (checked against
   every test before the original handler).
2. In the FIFO walker (cellGcmSys.c), a run of constant words on subchannel 0 goes word by word to
   `rsx_tconst_fast`, skipping the per-word diagnostics and dispatch. It stops at the first word that
   isn't a constant (the normal loop takes over there), and it ends the block on an unmapped word,
   as the normal loop does.

The word reads are unchanged: `vm_read32` here is already the inline load.

## Offline test (`test/test_fifofast.c` with the real rsx_commands.c; `runff.bat` builds it)
- 4,000,000 random words: 84% constants in the window, 6% CONSTANT_LOAD (some past slot 512), and
  the neighbours 0x1F80+ / 0x1FC0 / DATA4F / 0x0Axx.
- **Identical state with FIFO_FAST on and off:** running hash 1A0B02145A233CFE and final hash
  7200A54A7B0D541C both times (all 512 constants, lo/hi/dirty, load index).
- **27.4 -> 17.4 ns per word** through `rsx_process_method` (mixed stream). The walker loop saves
  more per word on top (not measured offline).

## Expected (*inferred*)
The gain depends on how many constant words a frame carries. At ~1,400 draws and ~100 constant words
each, that is ~140k words and ~2 ms off the drawing thread. The histogram run below gives the real
count.

## Run for the AI assistant (same power mode as before, Extreme Performance off)
1. Once, cheap: Hidden Leaf fight with `TCONST_FIFO=1`. It prints `[TCFIFO] method histogram
   (blocks, dwords)` after 200,000 FIFO blocks, which shows the real share of constant words (and
   which other methods are frequent, for a next fast path).
2. Hidden Leaf and the demo fight, `GCM_FLIPCOUNT=1 PERF=1`, defaults vs `FIFO_FAST=0`. Compare:
   - flips/s and the share of 2- vs 3-vblank frames (tools\flipsum.py);
   - [PERF2] drain per window;
   - screenshots, which must match.

## v2 (`fifo-fast-v2.patch`, on f4784cd = v1 committed; +51/-5; same `FIFO_FAST=0` off switch)
The AI assistant found that v1's walker loop never ran: constants arrive as blocks that *start* with
`SET_TRANSFORM_CONSTANT_LOAD` (0x1EFC), and v1 stopped at a block whose first word isn't a constant.
v2 changes three things:
1. **Walker:** word 0 of a 0x1EFC block goes through `rsx_process_method` (it sets the load index, as
   the normal loop would). The constant words after it then take the fast loop.
2. **Vertex-program words** (`SET_TRANSFORM_PROGRAM`, 0x0B80-0x0BFC) were the most frequent data in
   The AI assistant's menu histogram (78k dwords). They get the same treatment:
   - `rsx_vprog_fast()` is the original handler, taken first in `rsx_process_method` (nothing else
     lives in 0x0B00-0x0BFF);
   - those words also take the fast loop in the walker.
3. **`TCONST_FIFO=<n>`** (n > 1) prints a histogram of every n blocks and resets the counts after
   each one, so the later histograms are from the fight. `TCONST_FIFO=1` behaves as before (one
   histogram at 200,000 blocks).

**Offline:** test_fifofast.c, extended with program-load and program words (some past the 16 KB
store) and hashing `vp_ucode`, the write cursor, `vp_ucode_bytes`, `vp_gen` and `vp_dirty`.
4,000,000 words gave **identical running and final hashes** with FIFO_FAST on and off. All three
files compile with the same warnings as before.

**Run (same power mode):**
1. Hidden Leaf fight with `TCONST_FIFO=20000` to get a fight histogram.
2. Hidden Leaf and the demo, `GCM_FLIPCOUNT=1 PERF=1`, defaults vs `FIFO_FAST=0`. Compare flips/s,
   2- vs 3-vblank share, [PERF2] drain, and screenshots.
