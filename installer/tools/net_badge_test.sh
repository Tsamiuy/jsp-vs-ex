#!/usr/bin/env bash
# Host test of the rollback status badge (installer/port/jsp/jsp_net_badge.h): text, key, refresh gate, layout, and that every status line the
# game can write is the exact line installer/tools/support_bundle.py reads. No game, no GPU: needs g++ (C++20) and python3.
#   bash installer/tools/net_badge_test.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_net_badge_test.XXXXXX")
lines=$(mktemp "${TMPDIR:-/tmp}/jsp_net_badge_lines.XXXXXX")
trap 'rm -f "$out" "$lines"' EXIT
g++ -std=c++20 -Wall -Wextra -o "$out" "$d/net_badge_test.cpp"
"$out" "$lines"
python3 -I - "$d" "$lines" <<'PY'
import re, sys
sys.path.insert(0, sys.argv[1])
import support_bundle as sb
lines = open(sys.argv[2], encoding="utf-8").read().splitlines()
bad = [ln for ln in lines if not re.fullmatch(sb._NET_STATUS, ln) or sb.net_line_safe("[net] " + ln) != "[net] " + ln]
if len(lines) != 4 or bad:
    print("FAIL: status lines the bundle does not read as written:", bad or lines)
    sys.exit(1)
print("net badge: the bundle reads all", len(lines), "status lines as written")
PY
