#!/usr/bin/env bash
# Host test of the Online page's Netcode choice (Off / Delay / Rollback): installer/port/jsp/jsp_net_online.h and the page state in jsp_port_menu.cpp.
# Fake backend and fake settings file: no game, no GPU; needs only g++ (C++20).
#   bash installer/tools/net_online_test.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_net_online_test.XXXXXX")
trap 'rm -f "$out"' EXIT
g++ -std=c++20 -Wall -I"$d/../port/jsp" -o "$out" "$d/net_online_test.cpp"
"$out"
