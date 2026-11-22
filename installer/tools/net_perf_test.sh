#!/usr/bin/env bash
# Host test of the match perf summary (installer/port/jsp/jsp_net_perf.h). No game, no GPU: needs g++ (C++20).
#   bash installer/tools/net_perf_test.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_net_perf_test.XXXXXX")
trap 'rm -f "$out"' EXIT
g++ -std=c++20 -Wall -Wextra -I"$d/../port/jsp" -o "$out" "$d/net_perf_test.cpp"
"$out"
