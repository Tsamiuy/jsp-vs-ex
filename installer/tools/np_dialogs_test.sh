#!/usr/bin/env bash
# Host test of toolkit patch 6018 (no game, no GPU, no network; needs a C compiler and the patched toolkit): the NP message,
# invitation and profile dialogs queue exactly one completion for the title's handler (libs/network/tests/test_np_basic_dialogs.c).
#   bash installer/tools/np_dialogs_test.sh TOOLKIT_DIR [SCRATCH_DIR]      (TOOLKIT_DIR: installer/tools/export_toolkit.py --out DIR)
set -euo pipefail
tk="${1:?usage: np_dialogs_test.sh TOOLKIT_DIR [SCRATCH_DIR]}"
src="$tk/libs/network/tests/test_np_basic_dialogs.c"
[[ -f "$src" ]] || { echo "no $src (is patch 6018 applied?)" >&2; exit 3; }
out="${2:-}"
if [[ -z "$out" ]]; then out=$(mktemp -d "${TMPDIR:-/tmp}/jsp_np_dialogs_test.XXXXXX"); trap 'rm -rf "$out"' EXIT; fi
mkdir -p "$out"
cc="${CC:-$(command -v cc || command -v gcc || command -v clang)}"
"$cc" -std=gnu17 -O1 -Wall -I"$tk/include" "$src" -o "$out/test_np_basic_dialogs"
"$out/test_np_basic_dialogs"
