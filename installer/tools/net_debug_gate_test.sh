#!/usr/bin/env bash
# The rollback test-switch gate (jsp_net_debug_gate.h): the rule (net_debug_gate_test.cpp), and a source scan that jsp_net_game.cpp reads every gated key
# only through gated_get and jsp_fight_hash.cpp every fight hash switch only through hash_ab (a direct jsp_setting_get / getenv of one would bypass
# JSP_ROLLBACK_ALLOW_DEBUG). Needs g++ (C++20) and python3.
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
assert len(keys) == 9, keys
assert {"JSP_ROLLBACK_INPUTS", "JSP_ROLLBACK_INPUTS_SKIP"} <= set(keys), keys
hs = h.index("kGatedHash[]")
hkeys = re.findall(r'^\s+"(JSP_[A-Z_]+)",', h[hs:h.index("};", hs)], re.M)
assert len(hkeys) == 4, hkeys
bad = []
for f in sorted(p.glob("*.cpp")) + sorted(p.glob("*.h")):
    if f.name == "jsp_net_debug_gate.h": continue
    for n, line in enumerate(f.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        for ks, via in ((keys, "gated_get("), (hkeys, "hash_ab(")):
            for k in ks:
                if f'"{k}"' in line and f'{via}"{k}"' not in line:
                    bad.append(f"{f.name}:{n}: {k} read outside {via[:-1]}")
for fn, ks, via in (("jsp_net_game.cpp", keys, "gated_get("), ("jsp_fight_hash.cpp", hkeys, "hash_ab(")):
    src = (p / fn).read_text(encoding="utf-8").splitlines()
    for k in ks:
        c = sum(1 for line in src if f'{via}"{k}"' in line)
        if c != 1: bad.append(f"{fn}: {c} {via[:-1]} reads of {k}, expected one")
# hash_ab itself must apply the gate (jpdg::honoured with the allow key), so a read through it cannot bypass JSP_ROLLBACK_ALLOW_DEBUG
fh = (p / "jsp_fight_hash.cpp").read_text(encoding="utf-8")
body = fh[fh.index("bool hash_ab(const char* key)"):]
body = body[:body.index("\n}\n")]
if "jpdg::honoured(" not in body or "jpdg::kAllowKey" not in body: bad.append("jsp_fight_hash.cpp: hash_ab does not apply jpdg::honoured with the allow key")
for b in bad: print("FAIL", b)
if bad: sys.exit(1)
print(f"net debug gate: the {len(keys)} gated keys are read only through gated_get, the {len(hkeys)} fight hash switches only through hash_ab")
PY
