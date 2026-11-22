#!/usr/bin/env bash
# Host test of the rollback frame clock's lost-flip rule (installer/port/jsp/jsp_stall_rule.h). No game, no GPU: needs only g++ (C++20).
#   bash installer/tools/stall_rule_test.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_stall_rule_test.XXXXXX")
trap 'rm -f "$out"' EXIT
g++ -std=c++20 -O2 -Wall -Wextra -I "$d/../port/jsp" -o "$out" "$d/stall_rule_test.cpp"
"$out"
