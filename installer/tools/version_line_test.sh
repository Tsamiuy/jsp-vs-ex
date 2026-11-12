#!/usr/bin/env bash
# Host test for the version line choice of installer/port/jsp/jsp_version_line.h. No game, no GPU: needs only g++ (C++20).
#   bash installer/tools/version_line_test.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_version_line_test.XXXXXX")
trap 'rm -f "$out"' EXIT
g++ -std=c++20 -Wall -o "$out" "$d/version_line_test.cpp"
"$out"
