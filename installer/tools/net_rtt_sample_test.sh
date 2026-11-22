#!/usr/bin/env bash
set -euo pipefail
d=$(cd "$(dirname "$0")" && pwd)
port="$d/../port/jsp"
out=$(mktemp "${TMPDIR:-/tmp}/jsp_rtt_samples.XXXXXX")
trap 'rm -f "$out"' EXIT
g++ -std=c++20 -Wall -DGEKKONET_STATIC -DGEKKONET_NO_ASIO \
    -I"$port/gekkonet/include" -I"$port/gekkonet/include/private" \
    "$d/net_rtt_sample_test.cpp" "$port/gekkonet/src/net.cpp" -o "$out"
"$out"
