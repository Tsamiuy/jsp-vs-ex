# Capture helper scripts

Helpers for the `RSX_CAP` frame captures (see ../DEBUGGING.md, "Rendering"). All Python is
stdlib-only (`python3`, no Pillow). None of them edits a capture or any source code.

| Script | Use |
|---|---|
| `capture_match.ps1` | Run the demo-match capture recipe in one command (launches the **existing** build; only the AI assistant should run it). Sets the env vars, takes window screenshots, saves `run_info.txt` + `perf_tail.txt` next to the capture, then runs `capstat.py`. |
| `capstat.py <capture>` | Per frame, per render target: size, **% of sampled pixels lit**, mean RGB, how many ops wrote it and how many sampled it. Tells "nothing drawn" from "dark" without opening images. |
| `capops.py <frame dir>` | Query `manifest.txt`: `--summary` (one line per target), `--chain=<RT>` (what produced this target, recursively), `--post` (only fullscreen passes), `--rt= --tex= --touch= --fp= --from= --to= --details`. |
| `capcompare.py <A> <B>` | Lit% of every RT in two captures side by side; flags changes of 5 points or more (bisect pairs). |
| `bmp2png.ps1 <capture>` | BMP to PNG (optional `-Scale 0.5`, `-Only rt_0ED10000`) into `frame_NN\png\`. |
| `capmanifest.py` | Shared manifest parser (import it for new tools). |

## Typical session

```powershell
# 1. capture (default strip expansion), 275 s run, frames 5650 and 5690
powershell -ExecutionPolicy Bypass -File jstars-notes\tools\capture_match.ps1 -Name rt49
# same with a switch
powershell -ExecutionPolicy Bypass -File jstars-notes\tools\capture_match.ps1 -Name native -Env @{STRIP_NATIVE='1'}

# 2. inspect
python3 jstars-notes\tools\capstat.py jstars\run\rsxcap_rt49
python3 jstars-notes\tools\capops.py  jstars\run\rsxcap_rt49\frame_01 --summary
python3 jstars-notes\tools\capops.py  jstars\run\rsxcap_rt49\frame_01 --chain=0C6ABF40
python3 jstars-notes\tools\capops.py  jstars\run\rsxcap_rt49\frame_01 --post --from=1384
python3 jstars-notes\tools\capcompare.py jstars\run\rsxcap_rt48 jstars\run\rsxcap_rt49
powershell -File jstars-notes\tools\bmp2png.ps1 jstars\run\rsxcap_rt49 -Scale 0.5
```

## Caveats

- Only the **end-of-frame** content of each RT is saved, so "this RT is black" does not say which
  op blackened it. Per-pass snapshots need a backend change (see STATUS.md, next ideas).
- `capture_match.ps1` was written but **not run** (notes-only task). `capstat.py`, `capops.py`
  and `capcompare.py` were run on `run\rsxcap_rt48`.
- A capture holds the frames named by `RSX_CAP=start:count:stride`; the frame number counts
  backend frames and runs ahead of flips.
- Window screenshots: `jstars\snap_window.ps1` (called by `capture_match.ps1`).

## CPU per thread

`thread_cpu.ps1 [-Start 200] [-Window 5] [-Count 12] [-Top 20] [-Out thread_cpu.txt]`: real CPU use
of each JStarsRecomp.exe thread (% of one core; spinning counts, sleeping does not) over consecutive
windows, starting `-Start` seconds after the process appears. Thread ids match the sampler's
`[samp] tid` lines and the `threads` debug command. See `../CPU_PROFILE.md`.

## Unpacking the game's compressed files

`cmp_unpack.py` decodes the game's `$CMP` files (LZ chunks, some Huffman coded). It only reads the bytes it is given and
holds no keys and no game data. Use it from Python:
`import cmp_unpack; data, size = cmp_unpack.decode(open(path, 'rb').read())`. Its output for one fighter's pack was
identical to the copy in the game's memory (see `docs/balance/README.md`).

## Knockdown table

`balance/gen_kd.py` writes `docs/balance/Knockdowns.md` (every fighter's hits whose reaction number knocks down) and the "Knockdowns" section of each fighter's page in `docs/character-details/` from the fighters' unpacked
battle data. The data folder is `KD_DUMPS`; it is not in this repository. The script is written for the layout of the maintainer's working folder, see the note at its top.
