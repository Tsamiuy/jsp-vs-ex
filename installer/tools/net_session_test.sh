#!/usr/bin/env bash
# Host tests of the experimental rollback netcode's session layer (installer/port/jsp/jsp_net_session.cpp over the pinned GekkoNet copy in
# installer/port/jsp/gekkonet/, no game, no GPU; needs g++ (C++20) and python3):
#   net_transport_test   endpoint wire form, pad wire encoding, message channel with epochs, rendezvous, UDP mux (real localhost UDP)
#   net_m2_link_test     the real host/client settings handshake (jsp_session_handshake.h) over the session's datagram adapter, 30% loss each way
#   net_session_test.py  two processes (host and client) of net_session_test: a fake deterministic game driven through the real stepper and glue
#                        (jsp_stepper.h, jsp_net_glue.h); scenarios with loss, jitter, blackouts, desync detection, refusals and the tick rate
#   bash installer/tools/net_session_test.sh [SCRATCH_DIR]
set -euo pipefail
d=$(cd "$(dirname "$0")" && pwd)
port="$d/../port/jsp"
g="$port/gekkonet"
out="${1:-}"
if [[ -z "$out" ]]; then out=$(mktemp -d "${TMPDIR:-/tmp}/jsp_net_session_test.XXXXXX"); trap 'rm -rf "$out"' EXIT; fi
mkdir -p "$out/obj"
common=(-std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -DGEKKONET_STATIC -DGEKKONET_NO_ASIO -I"$g/include" -I"$port")
for f in "$g"/src/*.cpp; do   # third-party, unchanged: compiled without warnings
  g++ "${common[@]}" -I"$g/include/private" -I"$g/thirdparty" -w -c "$f" -o "$out/obj/$(basename "$f" .cpp).o"
done
g++ "${common[@]}" -c "$port/jsp_net_session.cpp" -o "$out/jsp_net_session.o"
for t in net_transport_test net_m2_link_test net_session_test; do
  g++ "${common[@]}" "$d/$t.cpp" "$out/jsp_net_session.o" "$out"/obj/*.o -o "$out/$t" -pthread
done
"$out/net_transport_test"
"$out/net_m2_link_test"
python3 -I "$d/net_session_test.py" --exe "$out/net_session_test"
