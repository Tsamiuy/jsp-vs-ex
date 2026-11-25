#!/usr/bin/env bash
# Host test of the two-page Main Menu's text language and gate (installer/port/jsp/jsp_menu_lang.h). No game, no GPU: needs g++ (C++20).
#   bash installer/tools/menu_lang_test.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_menu_lang_test.XXXXXX")
trap 'rm -f "$out"' EXIT
g++ -std=c++20 -Wall -Wextra -o "$out" "$d/menu_lang_test.cpp"
"$out"
