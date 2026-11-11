#!/usr/bin/env bash
# Host test for installer/port/jsp/jsp_online.cpp. No game, no GPU: needs only g++ (C++20), runs in a temp folder.
#   bash installer/tools/online_settings_test.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_online_test.XXXXXX")
trap 'rm -f "$out"' EXIT
g++ -std=c++20 -Wall -o "$out" "$d/online_settings_test.cpp"
"$out"
