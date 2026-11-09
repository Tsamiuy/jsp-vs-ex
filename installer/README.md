# Windows installer

A per-user installer (no admin rights) for the port, and a portable zip made from the same files. They ship none of the
game and download nothing: setup asks for the folder of your own game disc, your own decrypted `EBOOT.elf` (from your own
copy, see RPCS3 and its documentation), copies the port's sources, a
pinned compiler and a Python to the install folder, and then builds the port there (`installer/tools/firstrun.py`: lift,
SPU jobs, compile). You need no Visual Studio: the compiler (llvm-mingw, with CMake and Ninja) is in the package. The
installer and the program contact no server: no update check, no crash report, no telemetry. The one exception is
Online Battle (experimental, `v18` only), which is off unless you turn it on and give a psnr server, see
[Online Battle](#online-battle-experimental).

**The compile step has not been run on a real Windows PC.** See the table.

## Status

| Setup | What | State | Tested on |
|---|---|---|---|
| Windows 10/11, native | Inno Setup wizard, build step with the bundled llvm-mingw, launcher, portable zip | implemented, not run on Windows | - |
| Windows 10/11, native | The earlier version of this installer, which needed Visual Studio 2022 Build Tools (replaced by this change) | tried by the maintainer, reported in issue #5 | see the issue |
| Linux, Windows exe under Wine/Proton | Wizard pages, silent install and uninstall, the lift and SPU steps with the bundled Windows Python, the compile with the bundled Windows llvm-mingw (CPU only, no game started) | verified, one run each: silent install and uninstall, the lift and SPU steps, the compile (no game started). Not run: the wizard walk with the Portable box ticked | Proton-GE (Wine reports 2 CPUs), US disc and its decrypted `EBOOT.elf` |
| Any host with Python 3 | `firstrun_selftest.py`, `portable_selftest.py` (everything up to the compile) | verified | Linux, real US disc folder and `EBOOT.elf` |
| Windows 10/11, native | The launcher's disc check and CPU boost; whether the boost changes the sound problem seen in #5 | implemented, untested (compiled on Linux only) | - |

"Verified" means a run on that setup; "implemented" means the code is in but was not run there.

## What is here

| Path | What |
|---|---|
| `installer/jsp.iss` | The Inno Setup 6 script: disc and EBOOT checks, RPCS3 page, Portable task, install, build step, uninstall; it compiles the settings window (`launcher/JStarsLauncher.cs` as `jsp-settings.exe`, with the C# compiler that comes with Windows) that the shortcuts open |
| `installer/launcher/launcher.c` | `jsp-launcher.exe`, a small program the installer's shortcuts start. It checks that the disc folder exists (message box if not), reads `paths.txt` and `play_settings.txt`, leaves the power plan alone (`CPU_BOOST=1` sets its maximum processor state to 100 % while the game runs and puts the old values back afterwards) and starts the built game. It does not replace `launcher/JStarsLauncher.cs` |
| `installer/tools/assemble_payload.py` | Collects what setup copies: the port (`--tree v15`, the default: `jstars/port`; or `--tree v18`, opt-in until its open issues are fixed: `installer/port`), the patched toolkit (v18: `export_toolkit.py`; v15: `tools/toolkit/apply_toolkit.py`), the toolchain, the Windows Python, `firstrun.py`, licences, and `tree.txt` naming the tree |
| `installer/tools/firstrun.py` | The build step that runs on the player's PC after setup copied the files. Preflight checks with named codes, then resumable steps whose markers hold an input fingerprint; a new log per run in `logs\firstrun-<time>.log` (the newest five are kept) |
| `installer/tools/jsp_spu.py` | Finds the SPU job programs inside the EBOOT and lifts them with the v0.15 tools (from `jstars/spu_jobs/`, arguments instead of fixed folders); used by the `v15` tree |
| `installer/tools/lift_jobs.py` | The same for the v0.19.0 tools (the `v18` tree): scan, wrap, lift, `spu_workloads.c` |
| `installer/tools/export_toolkit.py` | Exports the pinned ps3recomp v0.19.0 commit with `git archive` and applies `installer/port/patches/*.patch` |
| `installer/port/jsp/`, `installer/port/patches/` | The `v18` port: the game glue (`CMakeLists.txt`, `main.cpp`, `stubs.cpp`, `jsp_*.cpp`) and the toolkit patches. Nothing from the game is in them; the PPU and SPU lift are made on the player's PC from the player's EBOOT |
| `installer/port/test/` | Scripted pad routes for testing the installed `v18` game without a controller (see "Test routes" below) |
| `installer/tools/installer_art.py` | Draws the wizard images and icons at build time (needs Pillow and the IBM Plex Mono Bold font file); nothing generated is committed |
| `installer/tools/ex_logo_art.py` | Draws `ex_logo_A.rgba`, the "EX" that toolkit patch 0006 puts in place of the "+" in the title logo, from polygons (our own art, needs Pillow and numpy); `assemble_payload.py` runs it and `firstrun.py` places the file next to `JStarsRecomp.exe`. `JSP_EX_LOGO=0` in the settings file keeps the "+" |
| `installer/tools/installer_music.py`, `installer/music/` | Renders the six opt-in music tracks (formulas, credits and sources in `tracks.tsv`) to WAV files at build time with plain Python; the wizard's "Enable Music?" box (bottom left, off by default, silent installs never play) loops one at random. Credits and licence in `NOTICE.md` |
| `installer/tools/rpcs3_locate.py`, `installer/tools/test_rpcs3_locate.py` | A shared, read-only helper that finds the folders, game, update, fonts, saves and decrypted `EBOOT.elf` of an RPCS3 install, vendored unchanged (see `NOTICE.md`), with its own test |
| `installer/tools/rpcs3_wizard.py`, `installer/tools/rpcs3_wizard_test.py` | The wizard's side of it: the page "Use your RPCS3 install?" pre-fills the disc folder, the dev_flash folder and an already decrypted `EBOOT.elf`, and offers the save import (see below). The test uses fake RPCS3 trees |
| `installer/tools/support_bundle.py`, `support_bundle_test.py` | The Start-menu entry "Make a support bundle" (`support-bundle.cmd` in the payload, also in the portable zip) runs it with the bundled Python: one `jsp-support-<time>.zip` of the logs and settings on the Desktop. Host test: `python3 -I installer/tools/support_bundle_test.py` (a synthetic install folder, no game data) |
| `installer/tools/firstrun_selftest.py` | Checks `firstrun.py` up to the compile against a real disc folder and EBOOT.elf; `--units` needs no game data (fingerprints, path refusals, moved folder, log rotation) |
| `installer/tools/quote_arg_test.py` | Plain Python, no Windows: a model of the Windows argv rules checks that `ArgQ` in `jsp.iss` (used for every path in `build-again.cmd` and the RPCS3 helper calls) round-trips paths that end in a backslash or a drive root, and that the old `"PATH"` form does not. |
| `installer/tools/script_paths_test.py`, `launcher_settings_test.py`, `fake_tool.c` | Run on Windows (or a Windows Python under Wine/Proton) with `fake_tool.c` compiled: which folder names break the generated `build-port.cmd`, and the settings order of the launcher |
| `installer/tools/assemble_portable.py` | Packs the same payload as `jsp-<version>-portable.zip` with `run.cmd` and a short readme |
| `installer/tools/fetch_toolchain.py`, `toolchain.json` | Downloads llvm-mingw, CMake, Ninja, their licence texts and the FFmpeg source (its libavcodec/libavutil headers only go into the payload, for the opening-movie decoder) on the machine that builds the installer, checks the pinned SHA-256 of each, unpacks them into a cache. Nothing downloaded is committed |
| `installer/tools/llvm-mingw.cmake` | CMake toolchain file for the bundled compiler |
| `installer/patches/` | Small patches the payload applies to its own copy of the port and the toolkit (this repository's files are not changed): 0001 to 0003 so that clang builds them, 0004 the optional startup shader loader, 0005 the Windows 10 fix for DXT textures whose size is not a multiple of 4, 0006 the optional "EX" in the title logo, 0007 the window icon and the plain title the launcher asks for |
| `installer/licenses/LGPL-2.1.txt` | The one licence text kept in the tree (some MinGW-w64 headers are LGPL-2.1+); the others are fetched with pinned hashes |
| `installer/tools/firstrun_selftest.py`, `portable_selftest.py`, `make_fixture.py` | Host checks of `firstrun.py` and the portable path, up to the compile; `make_fixture.py` writes a small fake disc folder (no game data) |

## Test routes

`installer/port/test/` holds two scripted pad routes and `run-route.cmd`, which runs the installed game with one of them:

    installer\port\test\run-route.cmd installer\port\test\fight-route-us.txt "C:\path\to\the\install folder"

The runner puts the route's one event line into `PAD_SCRIPT`, sets `PAD_CLOCK=flips` and starts `jsp-launcher.exe`; the
game's pad library then presses the buttons at the given display flips, so a run reaches the same screen each time
without a controller. Events are `time:mask` or `time:mask:polls` (hold length), comma-separated; the masks are listed in
each file.

- `fight-route-us.txt`: US disc with an existing save. Main Menu, Free Battle, Ichigo + Gintoki + Luffy (support) against
  Goku + Naruto, random stage. The fight is on screen at about flip 28400; SQUARE presses from 28700 attack.
- `fight-route-eu-firstboot.txt`: EU disc on a fresh install (no save). It answers the first-boot save dialog, then
  follows the same route 600 flips later; the fight is on screen at about flip 28500.

A fresh US install shows the same first-boot dialog, which `fight-route-us.txt` does not answer: start the game once and
create the save, or use the EU file's first five events as a prefix (not tried on US).

## Building the installer (Windows)

You need: Git, Python 3 with Pillow and numpy (`python -m pip install pillow numpy`), Inno Setup 6.7 or newer (`ISCC.exe`), internet access
for the toolchain download, the 64-bit embeddable Python package from python.org (3.13 was used), and
`IBMPlexMono-Bold.ttf` (SIL OFL 1.1). No Visual Studio and no separate C compiler: the launcher is compiled with the
downloaded llvm-mingw.

```
git clone --recurse-submodules https://github.com/Tsamiuy/jsp-vs-ex.git
cd jsp-vs-ex
mkdir build\installer\fonts
rem put IBMPlexMono-Bold.ttf in build\installer\fonts
rem unpack the embeddable Python into build\installer\toolchain-cache\python (python.exe directly inside)
python installer\tools\fetch_toolchain.py
python installer\tools\installer_art.py
python installer\tools\installer_music.py
build\installer\toolchain-cache\trees\llvm-mingw\bin\x86_64-w64-mingw32-clang.exe -O2 -mwindows -s -o build\installer\jsp-launcher.exe installer\launcher\launcher.c
python installer\tools\assemble_payload.py
rem (--tree v18 stages the v0.19.0 port in installer/port instead; --toolkit-repo and --no-fetch: see the script)
ISCC.exe /DPayload=%CD%\build\installer\payload /O%CD%\build\installer\out %CD%\installer\jsp.iss
python installer\tools\assemble_portable.py
```

The results are `build\installer\out\jsp-setup.exe` and `build\installer\out\jsp-<version>-portable.zip` (with a `.sha256`
file). Give `/DPayload` and `/O` as full paths as above: ISCC resolves a relative `/DPayload` from the script's folder and
fails with "Invalid file handle". `fetch_toolchain.py --check --strict` validates the pins without a download.
`<version>` is the text in `installer/VERSION`; change it only with `python3 tools/release.py bump` (see CONTRIBUTING.md, Releases).
The FFmpeg entry is GitHub's zip of the n9.0.1 tag: GitHub does not promise the same bytes forever, so a hash mismatch there
most likely means it was re-packed (check the tag, then refill). Its majors (avcodec 63, avutil 61) must match the RPCS3 DLLs
players have; firstrun.py's check M1 warns when they do not.
The `v18` toolkit is taken from the submodule
`third_party/ps3recomp`: when that checkout is at another commit (the v0.15 pin), `assemble_payload.py` fetches the `v0.19.0` tag
into it once, on the machine that builds the installer; the player's PC needs no network.
`assemble_payload.py` refuses to finish when the payload holds a file kind that the game ships (`.elf`, `.bin`, `.sfo`,
`.psarc`, `.edat`, `.self`, `.sprx`), and `assemble_portable.py` refuses a payload that already holds first-run output.

The `clang.exe` launcher line, `ISCC` on Windows and the finished package are untested on Windows (the launcher source was
compiled with MinGW-w64 GCC on Linux; the wizard and the payload ran under Wine/Proton).

On GitHub: the `installer` workflow (`.github/workflows/installer.yml`, Actions tab, **Run workflow**) runs the same steps on a
Windows runner and keeps `jsp-setup.exe`, the portable zip if asked and `SHA256SUMS.txt` as a workflow artifact for 7 days
(changeable when started), visible to whoever can see the repository. It downloads the embeddable Python and the font itself,
pinned by SHA-256 in the workflow. Started by hand only; nothing is published.

For an unattended test of the wizard pages without a build:
`jsp-setup.exe /VERYSILENT /DISC=<disc folder> /EBOOT=<EBOOT.elf> /SKIPBUILD=1`. Add
`/TASKS=portable` to keep saves and settings in the install folder (the Portable task). Uninstalling a portable install asks
whether to delete them; a silent uninstall keeps them unless `/REMOVEDATA=1` is given.

## Support bundle

If a first run fails, the game shows a black screen or setup stops, click **Make a support bundle (JSP Fan Revival)** in the Start menu (portable zip: double-click `support-bundle.cmd`). It makes `jsp-support-<time>.zip` on the Desktop (in the install folder if there is no Desktop), selects it in Explorer, and sends nothing anywhere: you attach the file to an issue yourself.

In the zip, as text with your user profile folder replaced by `%USERPROFILE%`: the `logs` and `build` step logs, `game\run` logs (`launcher_log.txt`, `play_stdout.txt`, `play_stderr.txt`, `boost-restore.txt`), `play_settings.txt` (install folder or the settings folder in AppData), `jsp-install.ini`, `build-again.cmd`, `paths.txt`, `VERSION.txt` and `tree.txt` when present, the newest two Inno Setup logs from the temp folder that mention this app, and a `facts.txt` (port version, Windows version, CPU count, RAM, free space on the install drive, Python version, whether `game\JStarsRecomp.exe` exists with size and date, which first-run steps finished, the GPU name if a log has it, and what was shortened or refused). Open the zip and look before you attach it.

Never in the zip: the EBOOT, disc files, saves (`hdd0`, `.sav`), lifted code, shader caches, binaries or an environment dump. Total text is capped at 20 MB; over that, the oldest lines of the largest logs are cut and `facts.txt` says so. Paths outside your profile folder (for example the disc folder on another drive) stay as they are.

## Host checks

`python3 -I installer/tools/firstrun_selftest.py APP_ROOT DISC_FOLDER EBOOT.elf SCRATCH_DIR` and
`python3 -I installer/tools/portable_selftest.py SCRATCH_DIR PAYLOAD_DIR DISC_FOLDER EBOOT.elf` need a payload made by
`assemble_payload.py` (`APP_ROOT` and `PAYLOAD_DIR`), a real disc folder and a decrypted `EBOOT.elf`. They run the build
step up to the SPU lift and stop before the compile; on Linux or macOS they take a few minutes. `make_fixture.py DIR` writes
a fake disc folder and an `EBOOT.elf` stand-in for the checks that need no real data.

## What the player needs

- The disc folder (the one that holds `PS3_GAME`), US BLUS31519 or EU BLES02136. Setup reads it in place.
- The decrypted `EBOOT.elf` of the same copy. No `dev_flash` folder: the game reads nothing from it (tested with an empty
  folder: menus, a fight and sound); `/DEVFLASH=` and `firstrun.py --devflash` still take one, else the install's own empty `dev_flash`.
- Windows 10 or 11, x64. Several GB of free disk space in the install folder and a long first build. The build runs at most
  one compile per CPU core and no more than there are free GB of RAM (an assumption of 1 GB per compile, from a Linux build
  whose largest compiler process used 887 MB).
- The folder must be writable and its path short. The portable zip must not be unpacked under `Program Files`.

If the build stops, `Build again` in the Start menu runs the same command; finished steps are kept.


## Use your RPCS3 install (optional)

The first page after the welcome page asks "Use your RPCS3 install?". If the RPCS3 folder field is filled (it is pre-filled when RPCS3 is in a usual
place) setup looks, read only, at that RPCS3's `games.yml`, `vfs.yml` and its game folders and fills in the disc folder (US BLUS31519 or EU
BLES02136; the game update is not used) and a decrypted `EBOOT.elf` that already sits beside the game. You can change every
field on the next pages, and an empty field means RPCS3 is not used. What it could not find is said in plain words on the page (for example: no
decrypted `EBOOT.elf` beside the game, which you make from your own copy: see RPCS3 and its documentation). Setup never opens `.rap` files,
`act.dat`, RPCS3's cache or its `exdata` folder.

**Read-only disc.** If the disc folder is on a disc drive or a mounted image (Windows reports a CD/DVD-type drive), the decrypted `EBOOT.elf` cannot be created beside the `EBOOT.BIN` there. When the EBOOT field is empty or the file is not found, setup (and the `E1` line of `firstrun.py`) says so: copy `EBOOT.BIN` to a normal folder, make `EBOOT.elf` from that copy (from your own copy, see RPCS3 and its documentation) and select it. Setup asks Windows for the drive type first; for other drives it creates a file with an unused name in `PS3_GAME\USRDIR` and deletes it at once, only on this error path, to see whether the folder can be written.

**Save import.** When RPCS3 has a save of the game (`NPUB31647-B0`), a page before the Ready page lists what importing would do: the files that
would be copied, the ones that would replace a file already in this port's save folder (`REPLACES`), and the ones only in the port's save folder, which stay as
they are. Nothing is copied unless you tick "Import this save", and the copy only runs if the list is still the same when setup copies (otherwise nothing is
written). The destination is the port's save folder (`%APPDATA%\JSP Fan Revival\hdd0`, user `00000001`); nothing is deleted and RPCS3 is never written to.
The save's `PARAM.SFO` can hold your PlayStation account id: it is copied as a file and its content is never read into the log.

For tests: `/RPCS3=<folder>` gives the folder to a silent install, which then fills `/DISC` or `/EBOOT` when you did not give it (a given value wins),
and `/IMPORTSAVE=1` imports the save. The log (`/LOG=`) holds the helper's `--json --redacted` output (lines starting with `rpcs3_wizard>`), never a
real path. Run `python installer/tools/test_rpcs3_locate.py` and `python installer/tools/rpcs3_wizard_test.py` to check the helpers.

## Settings: which file wins

`jsp-launcher.exe` reads `paths.txt` first, then exactly one settings file (`KEY=VALUE` lines, for example `RES_SCALE=3` or `FULLSCREEN=1`).
The first rule that applies wins; files of different rules are never merged. The launcher writes one line naming the rule to
`game\run\launcher_log.txt`, for example `settings: install folder (play_settings.txt)`.

| Rule | Where | Log line starts |
|---|---|---|
| 1. one-run flag | `jsp-launcher.exe --settings <file>` (this start only) | `settings: command line` |
| 2. marker file | an (empty) `settings_here.txt` in the install folder: then only `<install folder>\play_settings.txt` is read, never AppData | `settings: marker file` |
| 3. install folder | `<install folder>\play_settings.txt` | `settings: install folder` |
| 4. AppData | `%APPDATA%\JSP Fan Revival\play_settings.txt` | `settings: AppData` |

With none of them the defaults apply (`settings: none found`). The installed game data (saves) stays where `paths.txt` says.

## Online Battle (experimental)

The game's own Online Battle can run against a [psnr](https://github.com/sp00nznet/psnr) server instead of PSN: a small
matchmaking server (MIT) that one player, or anyone, hosts. Rooms, search and the player list go through the server; the
match itself goes peer to peer between the players' games, through the server's relay when a direct link fails. It is in
the `v18` tree only (an installer assembled with `--tree v18`, see above). Online is off unless these settings are
present. Setting up the server: see the psnr README (it has a `Setup.cmd` for Windows; players need TCP 36100 on the
server, and UDP between each other or the relay).

Add to the settings file (see above) of each player:

    PS3_NET_ONLINE=1
    PSNR_SERVER=192.168.1.20:36100
    PS3_NP_ONLINE_ID=yourname

`PSNR_SERVER` is the server's address and port; `PS3_NP_ONLINE_ID` is the name other players see (up to 16 characters).
Then Main Menu > Online Battle > Friendly Match: one player uses Create a Room, the others Search for a Room.

Two copies on one PC need a second settings file with another name and `PS3_NET_P2P_PORT=3659` (the default is 3658),
started with `jsp-launcher.exe --settings <file>`, and their own save data: `PS3_GAMEDATA_ROOT=<folder>` in that file,
with a copy of your save folder in it.

What has been tested, and what has not: two copies on one Linux PC under Proton, talking to a psnr server on the same
PC, reached a Friendly Match room together, started the battle and played into the fight with data going both ways.
Not yet: Windows, two PCs, a fight to its end, Rank Match. Rollback netcode is planned (see `docs/ROADMAP.md`); until
then the game's own netcode runs the match.

## Checks before the build

`firstrun.py` prints one line per check, `OK`, `WARN` or `FAIL` and a code; any FAIL stops it before anything is built.

| Code | Check |
|---|---|
| D1 D2 D3 | `PS3_GAME\PARAM.SFO`, a supported title id, `PS3_GAME\USRDIR` |
| E1 E2 | `EBOOT.elf` is a decrypted PowerPC64 ELF; its size is plausible (7.6 to 64 MB) |
| P1 P2 | install folder and disc folder paths: refused with `[ ] ;` (CMake cannot configure); the install folder also with `& ^ % ! "` (break the generated `.cmd` files). Spaces, `( )` and accents are fine (tested, see below) |
| S1 | free space where the build goes: FAIL under 4 GB, WARN under 10 GB (ASSUMPTION: no measured total yet) |
| T1 to T3 | The bundled llvm-mingw clang++, CMake and Ninja (`toolchain\`) |
| F1 | only when a `dev_flash` folder is given (`--devflash`): it has `sys` |
| R1 | WARN: the install folder moved since the last build; the compile tree and the build and place markers are reset (lift and SPU output holds no paths and stays) |

Each step marker (`build\markers\<step>.done`) holds a fingerprint of the step's inputs: lift and SPU jobs, the EBOOT and the lifter
tools; build, the digests of both plus the port sources, the toolkit, the compiler (Python version, size and time of
`clang++`) and the install folder; place, the build digest and the three paths. "Build again" redoes only the steps whose inputs
changed and prints which input, so a game update (another EBOOT) lifts again. A marker in the old plain format counts as stale.
`--force STEP` still redoes one step.
