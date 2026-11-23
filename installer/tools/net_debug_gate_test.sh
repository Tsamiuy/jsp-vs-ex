#!/usr/bin/env bash
# The rollback test-switch gate (jsp_net_debug_gate.h): the rule (net_debug_gate_test.cpp), and a source scan that jsp_net_game.cpp reads every gated key
# only through gated_get (a direct jsp_setting_get / getenv of one would bypass JSP_ROLLBACK_ALLOW_DEBUG). Needs g++ (C++20) and python3.
set -euo pipefail
d=$(cd "$(dirname "$0")" && pwd)
p="$d/../port/jsp"
out=$(mktemp -d "${TMPDIR:-/tmp}/jsp_debug_gate.XXXXXX"); trap 'rm -rf "$out"' EXIT
"${CXX:-g++}" -std=c++20 -O1 -Wall -Wextra -I"$p" "$d/net_debug_gate_test.cpp" -o "$out/test"
"$out/test"
python3 - "$p" <<'PY'
import pathlib, re, sys
p = pathlib.Path(sys.argv[1])
h = (p / "jsp_net_debug_gate.h").read_text(encoding="utf-8")
keys = re.findall(r'^\s+"(JSP_[A-Z_]+)",', h[h.index("kGated[]"):h.index("};", h.index("kGated[]"))], re.M)
assert len(keys) == 7, keys
bad = []
for f in sorted(p.glob("*.cpp")) + sorted(p.glob("*.h")):
    if f.name == "jsp_net_debug_gate.h": continue
    for n, line in enumerate(f.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        for k in keys:
            if f'"{k}"' in line and "gated_get(" not in line:
                bad.append(f"{f.name}:{n}: {k} read outside gated_get")
reads = sum(1 for k in keys for line in (p / "jsp_net_game.cpp").read_text(encoding="utf-8").splitlines() if f'gated_get("{k}"' in line)
if reads != len(keys): bad.append(f"jsp_net_game.cpp: {reads} gated_get reads of the {len(keys)} gated keys, expected one each")
for b in bad: print("FAIL", b)
if bad: sys.exit(1)
print(f"net debug gate: the {len(keys)} gated keys are read only through gated_get")
PY
