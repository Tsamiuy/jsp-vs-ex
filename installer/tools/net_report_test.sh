#!/usr/bin/env bash
# Host test of the online match report (installer/port/jsp/jsp_net_report.h): ring order, size bound, the sanitiser (addresses, home
# folders, names), the file name and the written file. No game, no GPU: needs only g++ (C++17).
#   bash installer/tools/net_report_test.sh [SCRATCH_DIR]
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_net_report_test.XXXXXX")
own=0; scratch=$1
if [ -z "$scratch" ]; then scratch=$(mktemp -d "${TMPDIR:-/tmp}/jsp_net_report_dir.XXXXXX"); own=1; fi
trap 'rm -f "$out"; [ "$own" = 0 ] || rm -rf "$scratch"' EXIT
g++ -std=c++17 -O1 -Wall -Wextra -Werror -o "$out" "$d/net_report_test.cpp"
"$out" "$scratch"
