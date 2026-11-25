#!/usr/bin/env bash
# Host test of toolkit patch 6502 (no game, no GPU, no network; needs a C++ compiler and the patched toolkit): virtual read-only files
# for cellFs (include/ps3emu/fs_vfile.h). Runs the patch's own test (runtime/ppu/tests/test_fs_vfile.cpp: open, SEEK_END, read, fstat =
# virtual size, a read crossing the virtual end, close, re-open, AioRead, SdataOpen) and the toolkit's other cellFs tests, which must
# pass unchanged with the provider pointer NULL. CXX picks the compiler (g++ or clang++; default c++).
#   bash installer/tools/fs_vfile_test.sh TOOLKIT_DIR [SCRATCH_DIR]      (TOOLKIT_DIR: installer/tools/export_toolkit.py --out DIR)
set -euo pipefail
tk="${1:?usage: fs_vfile_test.sh TOOLKIT_DIR [SCRATCH_DIR]}"
[[ -f "$tk/runtime/ppu/tests/test_fs_vfile.cpp" ]] || { echo "no $tk/runtime/ppu/tests/test_fs_vfile.cpp (is patch 6502 applied?)" >&2; exit 3; }
out="${2:-}"
if [[ -z "$out" ]]; then out=$(mktemp -d "${TMPDIR:-/tmp}/jsp_fs_vfile_test.XXXXXX"); trap 'rm -rf "$out"' EXIT; fi
mkdir -p "$out"
export CI=1   # a missing compiler fails instead of skipping
py="${PYTHON:-$(command -v python3 || command -v python)}"
rc=0
for t in vfile errors large_file rw_switch aio imports path_ops; do
  echo "== test_ppu_fs_$t"
  "$py" -I "$tk/tools/test_ppu_fs_$t.py" --exe-out "$out/test_fs_$t" || rc=1
done
exit $rc
