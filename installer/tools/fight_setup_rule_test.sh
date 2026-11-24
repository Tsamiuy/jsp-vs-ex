#!/usr/bin/env bash
# Host test of the battle setup table rule of the per-tick fight hash (installer/port/jsp/jsp_fight_setup_rule.h). No game, no GPU: needs only g++ (C++20).
#   bash installer/tools/fight_setup_rule_test.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_fight_setup_rule_test.XXXXXX")
trap 'rm -f "$out"' EXIT
g++ -std=c++20 -O2 -Wall -Wextra -I "$d/../port/jsp" -o "$out" "$d/fight_setup_rule_test.cpp"
"$out"
