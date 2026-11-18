#!/usr/bin/env bash
# Host test of the experimental rollback netcode's snapshot undo ring (installer/port/jsp/jsp_snapshot_ring.h, both ring modes): commits, rollbacks
# of 1..8 ticks with the replay's re-commits, a full-copy cross-check, and the pool-short eviction signal the session ends on. Also prints host
# timings. No game, no GPU: needs only g++ (C++20).
#   bash installer/tools/snapshot_ring_test.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_snapshot_ring_test.XXXXXX")
trap 'rm -f "$out"' EXIT
g++ -std=c++20 -O2 -Wall -Wextra -o "$out" "$d/snapshot_ring_test.cpp"
"$out"
