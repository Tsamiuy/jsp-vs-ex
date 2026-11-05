# J-Stars internals (what the game itself does)

Addresses are guest addresses in BLUS31519 v01.00 (`EBOOT.elf` SHA-1 of the encrypted
EBOOT.BIN: `E8451CBFA078B4FCDC345BC5494974748E89E1DF`). Heap addresses (0x4xxxxxxx) were stable
across runs.

## Binary

- Entry OPD 0x889060 → code 0x10230, TOC (r2) 0x8D3888.
- `.text` 0x10230–0x731ADC, import stubs 0x731B10–0x734330 (lift with `--code-end 0x734330`),
  read-only data after that inside the R-X segment.
- 34,055 OPD descriptors, 32,416 unique functions (34,092 lifted).
- TOC-switching linker stubs live at 0x7179BC–0x719F8C. The lifter merges runs of them into one
  function body, so "callers of X" greps through that range are mostly false matches; the first
  `g_trampoline_fn = func_…` in a stub is its real target.
- Firmware: no game PRX/SPRX on the disc — everything is static in the EBOOT.
- Middleware: CRI File System 2.66, CRI Atom/AtomEx (ADX2, HCA audio, SPU decode jobs:
  AtomDecoder, HcaMixer, DspPack1/2, DspPitchShifter, AsrVoice), Sony SPURS + libfiber.
  Shaders are precompiled Cg (`.pcg`). Movies are raw MPEG-2 (`.m2v`).

## Disc data

- 7 CRI CPK archives in `USRDIR\data\PS3\` (TOC is CRI's XOR-masked @UTF; unmask with
  m=0x655F, m=m*0x4115). Contents are `.pak` files (+ `.acb/.awb` in sound). game/patch stay on
  disc; battle/character/map/progress/sound are installed to HDD.
- Game's own `.pak` compression: file starts `$CMP`, payload is `[u32 BE length][data]` chunks,
  streamed by the file server in 64 KB blocks and decompressed by SPU job `80B700`.

## Threads (guest tid: name)

1 main · 2 `_SPU_printf_server` · 3 `sp_job_server_spu_thread` (also runs the fibers) ·
4–9 `sp_job_cpu_thread_{high,normal,low}` ×2 (wait on event queues 3/4/5) ·
10–13 CRI FS File Access 0–3 · 14 CRI FS Memory File System · 15 CRI FS Data Decompression ·
16 CriThread · 17 CRI Server Manager (16 ms tick) · 18 `sp_file_server_load_thread` ·
19 `sp_game_server_video_thread` · 20 `savedata_thread`

## Job system ("sp_job")

- 20 SPURS job chains named "chain control"; each worker block is contiguous, e.g.
  fiber 0x4B7E9980, job descriptor 0x4B7E9D80, guard 0x4B7E9E80, chain 0x4B7E9F80.
  Chain = `GUARD; JOB; NEXT→start` loop.
- Assigning work (`func_00577934`) writes the worker's job fields then calls `func_00592428`
  → `cellSpursJobGuardNotify` → the chain runs SPU job **80FD00**, whose user data (+0x30) is the
  fiber EA. That job signals the PPU fiber by atomically updating the fiber's word 0 (layout in
  FINDINGS §6). The fiber body `func_005778C0` = `loop { WaitSignal(); if quit (+0x1C) return;
  func_0057787C(); }` runs the CPU half of the job.
- `cellFiberPpuSendSignal` is only called from the worker destructors (`func_005779D0`,
  `func_00577AD4`, `func_00577BC0`).
- Job server loop `func_005784EC`: receive on event queue 2 (local port 2, size 1, 20 µs timeout
  polling), kicks come from `func_00536C04` (`sys_event_port_send`).
- Job/handle hash table: global `*(0x8DB600)` → `0x96B6BC` → object 0x48E03500; lwmutex at
  +0x78 (0x48E03578); buckets `+0x90` (0x4B7FE7F0), count `+0x94` (512), hash start `+0x98`,
  sequence counter `+0x74`; 12-byte entries `{ptr, id, state}`. Lookup `func_005382EC`, insert
  `func_005383D4`/`func_00536C94` (open addressing, spins if full), wrappers `func_00536A40`
  (register) / `func_00536A70` (lookup) / `func_00536B04`,`B80` (remove).

## File loading

- Main thread loader wait `func_0052F374(owner, task)`: polls `task+0x24 == 4`, steps state
  machine `func_0052EE60` (11 states, jump table 0x52EEC8), sleeps 100 µs.
- File server object 0x48E074D0 (vtable 0x8876F0, name string "sp_file_server_load_thread",
  main loop `func_0052E594`). Two 64 KB read buffers (0x4B7ABC00, 0x4B7BBC80), staging header at
  0x4B7A3B08, chunk copy `memcpy` at 0x52E874 (`func_006FB480`).
- CRI loader status helpers: `func_005713E4` (status == 1 LOADING), `func_0057136C` (error).
- Partition manager (disc vs HDD per CPK, opens each candidate on fd 10):
  `func_0060B41C → func_005381B0 → func_003F0520 → func_003F0228 → func_003F0200`.
- CRI FS thread chain for opens/closes: `func_004D52BC → 004D429C → 004C7AD0 → 004C78C0 →
  004B0368 → 004B2D24 (loader execute) → 004CB2F8 open / 004CB360 close`.

## Boot sequence (current)

BootCheck → DataCheck/ContentPermit (install check) → SPURS/job chains/fibers → CRI init and CPK
binds → logo stream → (first boot: install with progress dialog) → BootCheck → trophies
("Checking trophies…") → `cellSaveDataAutoLoad2('NPUB31647-B0')` → `cellVdecStartSeq` (movie).
