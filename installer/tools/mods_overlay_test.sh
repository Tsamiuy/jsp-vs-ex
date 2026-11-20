#!/usr/bin/env bash
# Host test of the mods/ framework core (installer/port/jsp/jsp_mods_core.h): mods.txt and manifest parsing, the CPK TOC rewrite and the
# virtual-file plan on SYNTHETIC archives the test writes itself, file and alias sources, refusals, conflicts, groups, classes, digest.
# No game data, no GPU: needs only a C++20 compiler (CXX, default g++; clang++ works too).
#   bash installer/tools/mods_overlay_test.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_mods_overlay_test.XXXXXX")
trap 'rm -f "$out"' EXIT
"${CXX:-g++}" -std=c++20 -O1 -Wall -Wextra -I "$d/../port/jsp" -o "$out" "$d/mods_overlay_test.cpp"
"$out"
