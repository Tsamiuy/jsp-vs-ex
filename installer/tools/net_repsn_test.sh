#!/usr/bin/env bash
# Host tests of rollback over re-psn (native Linux, loopback only, no game, no GPU; needs g++ (C++20), gcc and the patched toolkit):
#   net_repsn_test         the re-psn rendezvous (jsp_net_repsn.h) with a fake transport: room flag and record, roles, punch, relay only when allowed and offered
#   net_repsn_client_test  the real transport (jsp_net_repsn_client.h over the re-psn client of toolkit patch 7918) against a FAKE server that speaks the real
#                          wire format over loopback TCP + UDP, with golden request bytes
#   bash installer/tools/net_repsn_test.sh TOOLKIT_DIR [SCRATCH_DIR]      (TOOLKIT_DIR: installer/tools/export_toolkit.py --out DIR)
set -euo pipefail
d=$(cd "$(dirname "$0")" && pwd)
tk="${1:?usage: net_repsn_test.sh TOOLKIT_DIR [SCRATCH_DIR]}"
port="$d/../port/jsp"
g="$port/gekkonet"
psnr="$tk/libs/network/psnr"
[[ -f "$psnr/psnr.c" ]] || { echo "no psnr client at $psnr" >&2; exit 3; }
out="${2:-}"
if [[ -z "$out" ]]; then out=$(mktemp -d "${TMPDIR:-/tmp}/jsp_net_repsn_test.XXXXXX"); trap 'rm -rf "$out"' EXIT; fi
mkdir -p "$out/obj"
common=(-std=c++20 -O1 -Wall -Wextra -Wno-unused-parameter -DGEKKONET_STATIC -DGEKKONET_NO_ASIO -I"$g/include" -I"$port" -I"$psnr")
for f in "$g"/src/*.cpp; do   # third-party (two marked local additions, gekkonet/PIN.md): compiled without warnings
  g++ "${common[@]}" -I"$g/include/private" -I"$g/thirdparty" -w -c "$f" -o "$out/obj/$(basename "$f" .cpp).o"
done
gcc -O1 -Wall -Wno-unused-function -c "$psnr/psnr.c" -o "$out/psnr.o"
g++ "${common[@]}" -c "$port/jsp_net_session.cpp" -o "$out/jsp_net_session.o"
g++ "${common[@]}" "$d/net_repsn_test.cpp" "$out/jsp_net_session.o" "$out"/obj/*.o -o "$out/net_repsn_test" -pthread
g++ "${common[@]}" "$d/net_repsn_client_test.cpp" "$out/jsp_net_session.o" "$out/psnr.o" "$out"/obj/*.o -o "$out/net_repsn_client_test" -pthread
"$out/net_repsn_test"
"$out/net_repsn_client_test"
