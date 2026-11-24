#!/usr/bin/env bash
# Host test of the Training readout (installer/port/jsp/jsp_train_readout.h): combo state, text, key, redraw gate and layout.
# No game, no GPU: needs g++ (C++20).
#   bash installer/tools/train_readout_test.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_train_readout_test.XXXXXX")
trap 'rm -f "$out"' EXIT
g++ -std=c++20 -Wall -Wextra -o "$out" "$d/train_readout_test.cpp"
"$out"
