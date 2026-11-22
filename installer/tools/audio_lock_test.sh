#!/usr/bin/env bash
# Host test of toolkit patches 3005 and 3007 (the tick-locked audio of rollback matches; no game, no GPU, no audio device): runs the patch's own
# Windows test, libs/audio/tests/test_audio_lock_race.c, which drives the real cellAudio.c code (that path is the WASAPI mix thread's):
#   - a mix thread (audio_lock_step) against a guest audio thread that receives each notify event and works on it, and a game thread that raises the
#     target and waits for ps3_audio_lock_idle, with rewinds: at every idle the guest took and finished every block, and the read index matches;
#   - a WASAPI device re-open taken over by the first block of a burst (the burst's level sample and the device's fill setting come with it);
#   - concealment on a device with less room than it wants (no frame cut off).
# Needs a Windows C compiler and a way to run the .exe: MINGW_CC (e.g. llvm-mingw's x86_64-w64-mingw32-clang, also a Windows .exe called from WSL),
# else x86_64-w64-mingw32-gcc or x86_64-w64-mingw32-clang on PATH. With none it says SKIP and passes, unless CI=1.
#   bash installer/tools/audio_lock_test.sh TOOLKIT_DIR [SCRATCH_DIR] [SECONDS]      (TOOLKIT_DIR: installer/tools/export_toolkit.py --out DIR)
set -euo pipefail
tk="${1:?usage: audio_lock_test.sh TOOLKIT_DIR [SCRATCH_DIR] [SECONDS]}"
src="$tk/libs/audio/tests/test_audio_lock_race.c"
[[ -f "$src" ]] || { echo "no $src (is patch 3007 applied?)" >&2; exit 3; }
out="${2:-}"
if [[ -z "$out" ]]; then out=$(mktemp -d "${TMPDIR:-/tmp}/jsp_audio_lock_test.XXXXXX"); trap 'rm -rf "$out"' EXIT; fi
mkdir -p "$out"
cc="${MINGW_CC:-$(command -v x86_64-w64-mingw32-gcc || command -v x86_64-w64-mingw32-clang || true)}"
if [[ -z "$cc" ]]; then
    [[ "${CI:-}" == 1 ]] && { echo "no Windows C compiler (set MINGW_CC)" >&2; exit 2; }
    echo "SKIP audio_lock_test: no Windows C compiler (set MINGW_CC)"; exit 0
fi
# a Windows compiler run from WSL takes Windows paths
winpath() { if [[ "$cc" == *.exe ]] && command -v wslpath >/dev/null; then wslpath -w "$1"; else printf '%s' "$1"; fi; }
exe="$out/test_audio_lock_race.exe"
"$cc" -std=gnu11 -O2 -Wall -Wextra -Werror -I"$(winpath "$tk/include")" "$(winpath "$src")" -lole32 -o "$(winpath "$exe")"
# the compiler's runtime DLLs (llvm-mingw links libunwind/libc++ dynamically) next to the exe
bindir=$(dirname "$cc")
for d in "$bindir/../x86_64-w64-mingw32/bin" "$bindir"; do
    for f in "$d"/libunwind.dll "$d"/libc++.dll "$d"/libwinpthread-1.dll; do [[ -f "$f" ]] && cp -f "$f" "$out/"; done
done
"$exe" "${3:-2}"
