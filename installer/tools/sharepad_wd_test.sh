#!/usr/bin/env bash
# Host test of the share pad watchdog rule (installer/port/jsp/jsp_sharepad_wd.h, rematch black screen). No game, no GPU: needs only g++ (C++20).
#   bash installer/tools/sharepad_wd_test.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_sharepad_wd_test.XXXXXX")
trap 'rm -f "$out"' EXIT
g++ -std=c++20 -O2 -Wall -Wextra -I "$d/../port/jsp" -o "$out" "$d/sharepad_wd_test.cpp"
"$out"
