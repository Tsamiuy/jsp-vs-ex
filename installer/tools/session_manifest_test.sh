#!/usr/bin/env bash
# Host test for the host-controlled session settings of the experimental rollback netcode (installer/port/jsp/jsp_session_{digest,
# registry,effective,manifest,handshake}.h): manifest encoding, strict decode, digests, the host/client handshake under loss, restore rules.
# No game, no GPU: needs only g++ (C++20).
#   bash installer/tools/session_manifest_test.sh
# Also runs the binary twice with --epoch: the first join epoch of a process is random, so two runs must differ.
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_session_manifest_test.XXXXXX")
trap 'rm -f "$out"' EXIT
g++ -std=c++20 -Wall -Wextra -o "$out" "$d/session_manifest_test.cpp"
"$out"
e1=$("$out" --epoch | grep '^first-epoch')
e2=$("$out" --epoch | grep '^first-epoch')
if [ -z "$e1" ] || [ "$e1" = "$e2" ]; then echo "FAIL epoch start repeats across runs: $e1 / $e2"; exit 1; fi
echo "epoch start differs between runs: ok"
