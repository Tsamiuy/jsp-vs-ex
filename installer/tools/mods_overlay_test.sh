#!/usr/bin/env bash
# Host test of the mods/ framework core (installer/port/jsp/jsp_mods_core.h): mods.txt and manifest parsing, the CPK TOC rewrite and the
# virtual-file plan on SYNTHETIC archives the test writes itself, file and alias sources, refusals, conflicts, groups, classes, digest;
# and of the pure glue (jsp_mods_glue.h): start decision, copy check, served reads, online rules.
# With TOOLKIT (the patched ps3recomp folder, installer/tools/export_toolkit.py --out DIR) it also builds the glue itself
# (installer/port/jsp/jsp_mods.cpp, its POSIX file branch) against ps3emu/fs_vfile.h and runs it on files in a temporary folder.
# No game data, no GPU: needs only a C++20 compiler (CXX, default g++; clang++ works too).
#   bash installer/tools/mods_overlay_test.sh [TOOLKIT]
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_mods_overlay_test.XXXXXX")
trap 'rm -f "$out"' EXIT
extra=()
if [ -n "${1:-}" ]; then extra=(-DMODS_GLUE_FILES -I "$1/include"); fi
"${CXX:-g++}" -std=c++20 -O1 -Wall -Wextra -I "$d/../port/jsp" "${extra[@]}" -o "$out" "$d/mods_overlay_test.cpp"
"$out"
