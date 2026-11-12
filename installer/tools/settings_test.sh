#!/usr/bin/env bash
# Host test for the in-game settings pages (installer/port/jsp/jsp_settings.h, jsp_settings.cpp, the pages in jsp_port_menu.cpp) and
# for the key table against launcher/JStarsLauncher*.cs and the readers. No game, no GPU: needs only g++ (C++20).
#   bash installer/tools/settings_test.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$d/../.." && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_settings_test.XXXXXX")
trap 'rm -f "$out"' EXIT
g++ -std=c++20 -Wall -o "$out" "$d/settings_test.cpp" "$d/../port/jsp/jsp_settings.cpp"
"$out" "$root"
