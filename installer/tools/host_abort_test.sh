#!/usr/bin/env bash
# Focused real NetSession worker test; no title or guest VM.
set -euo pipefail
d=$(cd "$(dirname "$0")" && pwd)
p="$d/../port/jsp"; g="$p/gekkonet"
out="${1:-}"
if [[ -z "$out" ]]; then out=$(mktemp -d "${TMPDIR:-/tmp}/jsp_host_abort.XXXXXX"); trap 'rm -rf "$out"' EXIT; fi
mkdir -p "$out"
cxx="${CXX:-g++}"
common=(-std=c++20 -O1 -pthread -DGEKKONET_STATIC -DGEKKONET_NO_ASIO -I"$g/include" -I"$p")
for f in "$g"/src/*.cpp; do "$cxx" "${common[@]}" -I"$g/include/private" -I"$g/thirdparty" -w -c "$f" -o "$out/$(basename "$f" .cpp).o"; done
"$cxx" "${common[@]}" "$p/jsp_net_session.cpp" "$d/host_abort_test.cpp" "$out"/*.o -o "$out/test"
"$out/test"
