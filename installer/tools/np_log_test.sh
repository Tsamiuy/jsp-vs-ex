#!/usr/bin/env bash
# Host test of toolkit patch 6019 (no game, no GPU, no network; needs a C compiler, Python 3 and the patched toolkit):
#   - the NP identity getters log their first call and a changed answer only, never the online id
#     (libs/network/tests/test_np_identity_log.c);
#   - sys_event_port_send's EBUSY logs a bounded number of lines per port (runtime/syscalls/tests/test_evt_full_log.c);
#   - a source check of every printf's arguments in sceNp.c, sceNpBasic.c, sceNpMatching2.c, sceNpSignaling.c and np_psnr.c:
#     it flags the argument forms it knows for an online id, a player's name, an address or the server's name. It is a
#     pattern check of the source, not a guarantee that no such value can reach the log.
#   bash installer/tools/np_log_test.sh TOOLKIT_DIR [SCRATCH_DIR]      (TOOLKIT_DIR: installer/tools/export_toolkit.py --out DIR)
set -euo pipefail
tk="${1:?usage: np_log_test.sh TOOLKIT_DIR [SCRATCH_DIR]}"
for f in libs/network/tests/test_np_identity_log.c runtime/syscalls/tests/test_evt_full_log.c; do
    [[ -f "$tk/$f" ]] || { echo "no $tk/$f (is patch 6019 applied?)" >&2; exit 3; }
done
out="${2:-}"
if [[ -z "$out" ]]; then out=$(mktemp -d "${TMPDIR:-/tmp}/jsp_np_log_test.XXXXXX"); trap 'rm -rf "$out"' EXIT; fi
mkdir -p "$out"
cc="${CC:-$(command -v cc || command -v gcc || command -v clang)}"
"$cc" -std=gnu17 -O1 -Wall -I"$tk/include" "$tk/libs/network/tests/test_np_identity_log.c" -o "$out/test_np_identity_log"
"$out/test_np_identity_log"
"$cc" -std=gnu17 -O1 -Wall "$tk/runtime/syscalls/tests/test_evt_full_log.c" -o "$out/test_evt_full_log"
"$out/test_evt_full_log"

python3 -I - "$tk" <<'PY'
import pathlib, re, sys
tk = pathlib.Path(sys.argv[1])
# Arguments that would put a private value in the log: an online id or name, a peer's address, the server's name.
# A bare `id` is the online id a caller read; `->id` and `.id` are room, member and request numbers. `->data` is an SceNpOnlineId's text.
bad = re.compile(r"\b(online_id|onlineId|npId|s_id|username|named|host|ip|subject)\b|(?<![>.])\bid\b|->data\b"
                 r"|np_psnr_online_id\(\)|np_fake_username\(\)")
fails = n = 0
for rel in ("libs/network/sceNp.c", "libs/network/sceNpBasic.c", "libs/network/sceNpMatching2.c", "libs/network/sceNpSignaling.c", "libs/network/np_psnr.c"):
    src = (tk / rel).read_text()
    for m in re.finditer(r"\b(?:f?printf)\s*\((.*?)\);", src, re.S):
        n += 1
        args = re.sub(r'"(?:\\.|[^"\\])*"', '""', m.group(1))          # format strings out
        args = re.sub(r"\bstrn?len\s*\((?:[^()]|\([^()]*\))*\)", "0", args)  # a length is fine
        args = re.sub(r"\b\w+\s*\?(?!\s*:)", "", args)                         # so is a pointer only tested for NULL (not `x ?: y`, which prints x)
        if bad.search(args):
            line = src.count("\n", 0, m.start()) + 1
            print(f"FAIL: {rel}:{line} prints a private value: {' '.join(m.group(0).split())}")
            fails += 1
print(f"np log sources: {'FAIL' if fails else 'ok'} ({n} printf calls checked, {fails} with a private value)")
sys.exit(1 if fails else 0)
PY
