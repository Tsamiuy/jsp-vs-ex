#!/usr/bin/env bash
# Host test of the post-game agreement of the rollback netcode (installer/port/jsp/jsp_net_postgame.h). No game, no GPU: needs only g++ (C++20).
#   bash installer/tools/net_postgame_test.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_net_postgame_test.XXXXXX")
trap 'rm -f "$out"' EXIT
g++ -std=c++20 -O2 -Wall -Wextra -o "$out" "$d/net_postgame_test.cpp"
"$out"
