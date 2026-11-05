# ps3recomp runtime bugs found (and local edits to `ps3recomp-src`)

All found while porting J-Stars against commit `0fe7dbf5` (v0.15.0). Worth reporting upstream
(github.com/sp00nznet/ps3recomp, Discord linked in its README).

## Edited in `ps3recomp-src\runtime\ppu\ppu_fs.cpp` (local, uncommitted)

1. **Wrong `CELL_FS_O_*` constants.** Were `CREAT 0x200, TRUNC 0x400, APPEND 0x100`; SDK values are
   octal `CREAT 0100 (0x40), EXCL 0200, TRUNC 01000, APPEND 02000` (as in
   `libs\filesystem\cellFs.h`). Effect: `O_CREAT` ignored, every open of a new file fails.
   Fixed, plus `O_EXCL` support. (`runtime\syscalls\sys_fs.h` and `docs\PLATFORM_ABSTRACTION.md`
   also have inconsistent values.)
2. Diagnostics added: open-FAIL message prints flags; `PS3_FS_GSTACK` guest call chains on
   close / failed open / (=2) every open; `FS_READ_ALL` read lines include `pos=` and `t=`.

3. `g_ppu_fs_last_read_ms` export (time of last cellFsRead) for the port's DataCheck guard.

## Edited in `ps3recomp-src\libs\video\cellGcmSys.c` (local, uncommitted)

4. **FIFO `RET` with no `CALL` outstanding jumped to IO 0** → walker circles forever. Now skipped.
5. Diagnostics: `GCM_CIRCLE_DUMP=1`; FIFO trace history skips zero words.

`git -C ps3recomp-src diff` shows all of it.

## Not edited, worked around in the port

| Bug | Where | Port workaround |
|---|---|---|
| `<initguid.h>` after DXGI headers → `IID_IDXGIFactory6` undefined at link | `libs\video\rsx_d3d12_backend.c` | link `dxguid` (port CMakeLists) |
| Template `main.cpp` never calls `cellGame_init_from_paramsfo` → title id `BLES00000` | `templates\project\main.cpp` | called in port `main.cpp` |
| `cellFs.c` keeps its own mount table (`/dev_hdd0` → `./gamedata/dev_hdd0`) that differs from `ppu_fs.cpp`'s `PS3_HDD0_ROOT` → rename/unlink etc. hit a different folder than open/read | `libs\filesystem\cellFs.c` | `cellfs_add_path_mapping` in port `main.cpp` |
| `cellGameContentPermit` creates game-data dirs when DataCheck found none; `cellGameDataCheck` counts any dir as data (firmware requires `PARAM.SFO`); `CreateGameData` writes no `PARAM.SFO` | `libs\system\cellGame.c` | `port\jstars_game.cpp` |
| `cellSpursJq` is a stub (no job execution, queue = slot number); Port2 Push/CopyPush/Sync, Create…WithJobDescriptorPool, Shutdown/Join missing | `libs\spurs\cellSpursJq.c` | `port\stubs.cpp` |
| `cellFiber` implements a different API (host function pointers, global scheduler); SDK scheduler API absent; registered `cellFiberPpuCreateFiber` has the wrong signature | `libs\spurs\cellFiber.c` | `port\jstars_fiber.cpp` |
| `UCS2toUTF8` (and likely other cellL10n functions without a `cell` prefix) unregistered, and has a non-SDK signature | `libs\font\cellL10n.c`, `tools\gen_hle_nids.py` (only scans `cell*/sys*/sce*` names) | `port\stubs.cpp` |
| `cellSpursInitializeWithAttribute2`, `cellSpursAttributeEnableSystemWorkload`, `cellVdecOpenEx`, `cellVdecQueryAttrEx` absent | various | not yet |
| `spurs_job.c` treats a job's interrupt mailbox as the SPU printf service only, not `sys_spu_thread_send_event` | `runtime\spu\spurs_job.c` | not needed by J-Stars so far |
| `cellGcmSetDefaultCommandBuffer` does not repoint `gCellGcmCurrentContext` (opt-in `GCM_DEFAULT_CTX_REPOINT` also rewinds the default context, which is wrong for titles that call it every frame) | `libs\video\cellGcmSys.c` | `port\stubs.cpp` (repoint only) |
| Offline `cellNetCtlGetState` returns NOT_INITIALIZED (LBP workaround); hardware returns CELL_OK + Disconnected | `libs\network\cellNetCtl.c` | `port\stubs.cpp` |
| The debug console exists only in `runtime\ppu\tests\boot_main.cpp`, not the template | — | `port\jstars_debug.cpp` |
| `vcvars64.bat` doesn't add VS's CMake/Ninja (vswhere not on PATH) | build env | `port\build.bat` adds them |
| WASAPI shared-mode Initialize fails with 0x88890008 (UNSUPPORTED_FORMAT) when the default device's mix format is not 48 kHz stereo float, so the title runs silent | `libs\audio\cellAudio.c` | local runtime edit: `AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | SRC_DEFAULT_QUALITY` |
| cellSearch and cellMusic (Utility) are absent, so their result callbacks never fire | — | `port\jstars_sysutil.cpp` |
| `spu_workload_fingerprint` uses a truncated FNV basis (1469598103934665603); harmless, but external tools must use the same value | `runtime\spu\spu_workload.c` | documented (FINDINGS §15) |

