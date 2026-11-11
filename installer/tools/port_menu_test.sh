#!/usr/bin/env bash
# Host test for the page state of installer/port/jsp/jsp_port_menu.cpp. No game, no GPU: needs only g++ (C++20).
#   bash installer/tools/port_menu_test.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_port_menu_test.XXXXXX")
trap 'rm -f "$out"' EXIT
g++ -std=c++20 -Wall -o "$out" "$d/port_menu_test.cpp"
"$out"
