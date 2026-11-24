#!/usr/bin/env bash
# The rollback start's team-size rule (jsp_net_team_rule.h): 2 vs 2 starts, 3 vs 3 is refused, an unreadable world is let through (logged); and
# jsp_net_game.cpp's start_session applies it. Needs g++ (C++20).
set -euo pipefail
d=$(cd "$(dirname "$0")" && pwd)
p="$d/../port/jsp"
out=$(mktemp -d "${TMPDIR:-/tmp}/jsp_team_rule.XXXXXX"); trap 'rm -rf "$out"' EXIT
"${CXX:-g++}" -std=c++20 -O1 -Wall -Wextra -I"$p" "$d/net_team_rule_test.cpp" -o "$out/test"
"$out/test"
awk '/^void start_session\(/,/^}/' "$p/jsp_net_game.cpp" > "$out/start_session.txt"
grep -q 'jptr::classify' "$out/start_session.txt" && grep -q 'jptr::kRefusal' "$out/start_session.txt" \
  || { echo "FAIL: start_session does not apply the team-size rule"; exit 1; }
echo "net team rule: start_session applies it"
