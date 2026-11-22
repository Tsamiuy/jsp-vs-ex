#!/usr/bin/env bash
# Host test for the page state of installer/port/jsp/jsp_port_menu.cpp. No game, no GPU: needs only g++ (C++20).
#   bash installer/tools/port_menu_test.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_port_menu_test.XXXXXX")
trap 'rm -f "$out"' EXIT
g++ -std=c++20 -Wall -o "$out" "$d/port_menu_test.cpp"
"$out"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/jsp_menu_lifecycle.XXXXXX")
trap 'rm -f "$out"; rm -rf "$tmp"' EXIT
python3 - "$d/../port/jsp/jsp_port_menu.cpp" "$tmp/lifecycle.inc" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text()
def body(name):
    start=s.index("void "+name+"(")
    if s.index("\n",start) < s.index("{",start):
        start=s.index("void "+name+"(",start+1) if s[start:s.index("\n",start)].endswith(";") else start
    return s[start:s.index("\n}\n",start)+3]
sink = s.index("struct Overlay {")
render_sink = s[sink:s.index("\n};", sink) + 3]
Path(sys.argv[2]).write_text(render_sink + "\n" + "\n".join(body(n) for n in ("draw_page","on_build_options","ver_refresh")))
PY
g++ -std=c++20 -Wall -I"$tmp" -o "$tmp/lifecycle" "$d/port_menu_lifecycle_fixture.cpp"
"$tmp/lifecycle"
