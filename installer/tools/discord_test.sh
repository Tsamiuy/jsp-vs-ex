#!/usr/bin/env bash
# Host test of the Discord Rich Presence and Join rules (installer/port/jsp/jsp_discord_state.h): the join secret, the presence text
# from a given state, the pending join. No game, no Discord, no GPU: needs g++ (C++17).
#   bash installer/tools/discord_test.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_discord_test.XXXXXX")
trap 'rm -f "$out"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -o "$out" "$d/discord_test.cpp"
"$out"
