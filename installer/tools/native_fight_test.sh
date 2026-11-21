#!/usr/bin/env bash
# Host test of the pure part of the native online fight takeover (installer/port/jsp/jsp_net_native_fight.h). Needs g++ (C++20).
#   bash installer/tools/native_fight_test.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_native_fight_test.XXXXXX")
trap 'rm -f "$out"' EXIT
g++ -std=c++20 -Wall -Wextra -o "$out" "$d/native_fight_test.cpp"
"$out"
