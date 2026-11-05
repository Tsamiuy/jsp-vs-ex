# Setup, build and run

## Folder layout (`%USERPROFILE%\Desktop\PS3Recomp\`)

| Path | What |
|---|---|
| `ps3recomp-v0.15.0-win-x64\` | Prebuilt SDK bundle (lib, headers, Python tools, docs). **Not enough on its own**: it has no `templates\`, `runtime\ppu\` (per-game boot scaffold) or `libs\` source. |
| `ps3recomp-src\` | `git clone` of sp00nznet/ps3recomp at the bundle's commit `0fe7dbf5`. The port builds the runtime **from this**. Has local edits — see RUNTIME_BUGS.md. |
| `RPCS3\` | RPCS3 0.0.43 with the PS3 firmware installed as RPCS3 documents. Its `dev_flash` is used read-only at run time. ISO: `RPCS3\games\J-Stars Victory Vs+.iso`. |
| `jstars\` | Everything for this title (below). |
| `docs\notes\` | These notes. `jstars-notes\` holds the patches, analysis tools and measurements. |

### `jstars\`

| Path | What |
|---|---|
| `EBOOT.BIN`, `PARAM.SFO` | Copied out of the ISO |
| `EBOOT.elf` | The EBOOT is taken from your own copy of the game; for how to obtain the decrypted file, see RPCS3 and its documentation. |
| `out\` | `ppu_loader.py` output (functions/imports/image/loader JSON), `imports_named.json`, `hle_coverage.json` (import → handler status), a generated `ppu_hle_nids.cpp` |
| `spu_jobs\` | Raw SPU job binaries + wrapped ELFs + lift reports |
| `port\` | The game project (copy of `ps3recomp-src\templates\project`, project name `JStarsRecomp`) |
| `port\recompiled\` | Lifter output (65 files). **Contains temporary diagnostics** in `ppu_recomp_027.cpp` (`JSTARS-DIAG`, `JSTARS-COPY`) and `ppu_recomp_032.cpp` (`JSTARS-DIAG3`, kick log, `JSTARS_KICKLOG=1`). Re-lift to get clean copies (`jstars\ppu_recomp_032.cpp.orig` is a clean backup of 032). |
| `port\spu\` | Lifted SPU jobs + `spu_workloads.c` (registration) |
| `port\build\JStarsRecomp.exe` | The build |
| `hdd0\` | Host folder for guest `/dev_hdd0` (game data install lands in `hdd0\game\BLUS31519-INSTALL\`, 2.5 GB) |
| `run\` | Test-run logs (`stdout.txt`, `stderr.txt`), debug console files, `spu_miss\` dumps |
| `build.log`… | Build logs |
| `run_test.ps1` | Boot-test script (below) |

Leftovers that can be deleted: `hdd0\game\BLUS31519-INSTALL.renames-lost\`
(2.5 GB, an interrupted test install), `hdd0\game\BLES00000\` (empty, from the
old title-id bug), `jstars\ppu_recomp_027.cpp.orig` (NOT pristine — it was
overwritten with an instrumented copy).

## Tools installed

- Python 3.15 via Windows Python Install Manager — command `python3`
  (real path `%LOCALAPPDATA%\Python\pythoncore-3.15-64\python.exe`).
  Packages: `pip install --user -r ps3recomp-src\requirements.txt` (capstone,
  pycryptodome, construct, tabulate, tqdm).
- Visual Studio 2022 Build Tools 17.14 (winget) with the C++ workload,
  **clang-cl 19.1.5**, and VS's bundled CMake + Ninja.
  `vcvars64.bat` does **not** put CMake/Ninja/LLVM on PATH — `build.bat` adds them.

## Pipeline

```bash
# 1. load (function table, imports, TOC)
python3 ps3recomp-src/tools/ppu_loader.py jstars/EBOOT.elf -o jstars/out
# 2. lift (25 s). --code-end stops .rodata in the R-X segment being lifted as code
python3 ps3recomp-src/tools/ppu_lifter.py jstars/EBOOT.elf \
    --functions jstars/out/EBOOT.functions.json --hle-stubs jstars/out/EBOOT.imports.json \
    --code-end 0x734330 --chunks 64 --jobs 16 --output jstars/port/recompiled
# 3. build (first build ~10 min, ~0.4 GB RAM per lifted file)
jstars\port\build.bat [parallel-jobs]      (default 6)
```

Re-lifting overwrites `recompiled\` (and removes the temporary diagnostics).

SPU jobs (see FINDINGS.md §2, §4, §15). All 18 JOBCRT jobs in the EBOOT, in one go:
```bash
python3 jstars/spu_jobs/lift_all_jobs.py      # lifts missing ones + regenerates port/spu/spu_workloads.c
```
By hand, for a job that is not embedded in the EBOOT:
```bash
python3 ps3recomp-src/tools/wrap_spu_elf.py jstars/spu_jobs/X.bin --entry 0x0 --base 0x0 --out jstars/spu_jobs/X.elf
python3 ps3recomp-src/tools/spu_lifter.py --auto-functions jstars/spu_jobs/X.elf --base 0x0 \
    [--code-end 0xNNNN] --symbol-prefix jstars_<name>_ -o jstars/port/spu/<name> \
    --source-name spu_recomp.c --header-name spu_recomp.h --report jstars/spu_jobs/lift_<name>.json
```
then add it to `port\spu\spu_workloads.c` (fingerprint = FNV-1a-64 of exactly
`sizeBinary` bytes; the runtime prints it on a dispatch MISS).

## Running: `jstars\run_test.ps1`

```powershell
powershell -ExecutionPolicy Bypass -File jstars\run_test.ps1 90                       # 90 s run
powershell -ExecutionPolicy Bypass -File jstars\run_test.ps1 75 -Commands stat,threads # + console queries
```

It mounts the ISO read-only as the disc, sets the env below, runs the exe for N
seconds with stdout/stderr → `run\`, sends any `-Commands` to the debug console
near the end (answers in `run\dbg.txt.out`), kills it and unmounts.
**`-NoNewWindow` matters** — without it the redirected logs came out empty.

| Env var | Set to |
|---|---|
| `PS3_VFS_ROOT` | mounted ISO root (`E:/`) — the folder containing `PS3_GAME` |
| `PS3_HDD0_ROOT` | `jstars/hdd0` |
| `PS3_DEV_FLASH` | `RPCS3/dev_flash` |
| `SPU_DUMP_MISS` | `run/spu_miss` — unlifted SPU job binaries are dumped here |
| `PS3_DEBUG` | set only with `-Commands` — enables the debug console |

The first boot runs the HDD data install (~5 min). Later boots reuse it.

Norton/Avast scan each new build, so the first start after a rebuild is slow.
