# VP_CODE_GUARD: page-guard a beam vertex page and log every writer (logging only)

Patch: `beam-guard.patch` (`libs/video/rsx_d3d12_backend.c` + `runtime/ppu/ppu_loader.cpp`, about +35), on
c80d0f1. `git apply --check` passes, and both files compile syntax-only (the .cpp with the recompiled-header
include path) with no errors and no new kinds of warning. Not built or run. Nothing changes unless
`VP_CODE_GUARD` is set (the `[GUARD]` field additions only print when a guard is armed).

Why a page guard: the existing store watch (`PPU_WWATCH`) only sees `vm_write*`. The lifted dcbz is an inline
`memset(vm_base+ea, 0, 128)`, vector stores go through `vm_raw_st16`/`VM_RAW_ST`, and HLE `_sys_memset` writes
host memory directly. All of those bypass it, and they are exactly how buffers get cleared. The existing
page guard (`ppu_guard_page`, Windows VEH) traps **any** host write into the page, from any thread (PPU, SPU
DMA, our own code), and names the writer.

What it adds:
- `VP_CODE_GUARD=1`: on the first `VP_CODE` draw that is logged, the guard is armed on the page of that draw's
  first vertex (`[VPCODE] guarding the page of 0x...`). Use `VP_CODE_FROM` so that this is a beam draw.
- `[GUARD] WRITE` lines now also carry `flip= ms= tid= old= lr=`: the flip count (the same counter as
  `[VPCODE]` and `[dma-range]`), the time, the writing thread, the word before the write, and the guest lr.
- `PPU_GUARD_MAX=<n>`: cap the WRITE lines (default unlimited). The existing logic stays: a call stack and
  registers for writes to the watched 32-byte line, and `[GUARD] CLEARED` when the watched word goes from
  non-zero to 0.

Run (Kamehameha demo):
`VP_CODE=9A64BF1C VP_CODE_N=50 VP_CODE_FROM=<first beam frame, e.g. 7260> VP_CODE_GUARD=1 PPU_GUARD_MAX=20000`
plus the dmaclock `SPU_DMA_RANGE=48440000-48A60000 SPU_DMA_RANGE_AFTER=200 SPU_DMA_RANGE_N=3000
SPU_DMA_RANGE_PUT=1` for the PUT timeline. Expect some slowdown while the page is guarded (every write
faults once).

Reading: for the guarded page, list the writes in order of flip:
- At about F-2 (the ring slot is refilled), SPU DMA writes with non-zero `old`→data (the tid is an SPU thread).
- **Then, before the next draw that uses the page (F), a write that turns the data to 0.** Its guest-fn,
  lr and tid name the clearer:
  - a guest function on the PPU thread (dcbz/memset loop): the game's own clear, so its timing relative to
    the RSX is what we model wrong;
  - our runtime code (RIP in a host function, guest-fn 0): our emulation zeroes it (allocator, mapping,
    coherence).
