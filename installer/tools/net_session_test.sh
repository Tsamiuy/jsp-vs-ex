#!/usr/bin/env bash
# Host tests of the experimental rollback netcode's session layer (installer/port/jsp/jsp_net_session.cpp over the pinned GekkoNet copy in
# installer/port/jsp/gekkonet/, no game, no GPU; needs g++ (C++20) and python3):
#   net_transport_test   endpoint wire form, pad wire encoding, message channel with epochs, rendezvous, UDP mux (real localhost UDP)
#   net_m2_link_test     the real host/client settings handshake (jsp_session_handshake.h) over the session's datagram adapter, 30% loss each way
#   net_session_test.py  two processes (host and client) of net_session_test: a fake deterministic game driven through the real stepper and glue
#                        (jsp_stepper.h, jsp_net_glue.h); scenarios with loss, jitter, blackouts, desync detection, refusals and the tick rate;
#                        record_replay plays both peers' recordings back with net_replay_test
#   net_replay_test      the replay and stress modes in one process (stress below: every update rolls back D ticks; --flaky plants a re-simulation
#                        that differs, which must be reported at exactly that tick)
#   replay_dump          a replay's inputs as text; below: a stress session's input recording is decoded (600 ticks) and read by replay_inputs.py
#                        (summary, timeline, rollback model)
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
for t in net_transport_test net_m2_link_test net_session_test net_replay_test replay_dump; do
  g++ "${common[@]}" "$d/$t.cpp" "$out/jsp_net_session.o" "$out"/obj/*.o -o "$out/$t" -pthread
done
"$out/net_transport_test"
"$out/net_m2_link_test"
for a in "--distance 1" "--distance 4" "--distance 10" "--distance 4 --players 4" "--distance 4 --flaky 200" "--distance 9 --flaky 50"; do
  "$out/net_replay_test" --mode stress --ticks 600 $a
done
# ENHANCEMENT (replays): the input recording of a stress session, decoded and analysed
"$out/net_replay_test" --mode stress --ticks 600 --distance 3 --record "$out/stress.gkrp"
"$out/replay_dump" "$out/stress.gkrp" --out "$out/stress.csv"
n=$(grep -c '^[0-9]' "$out/stress.csv"); [[ $n -ge 590 && $n -le 610 ]] || { echo "replay_dump: $n ticks decoded, expected about 600"; exit 1; }
grep -q '^# user_data_text stress test$' "$out/stress.csv" || { echo "replay_dump: user data missing"; exit 1; }
python3 -I "$d/replay_inputs.py" "$out/stress.csv" summary | grep -q "^player 2: " || { echo "replay_inputs summary: no player 2 line"; exit 1; }
python3 -I "$d/replay_inputs.py" "$out/stress.csv" timeline --to 1 | grep -q "tick" || { echo "replay_inputs timeline: no events"; exit 1; }
python3 -I "$d/replay_inputs.py" "$out/stress.csv" rollback --ping 67 --delay 0,2 --costs 6,6,8 | grep -q "^ *67 *0 *3 |" || { echo "replay_inputs rollback: no 67 ms / delay 0 / 3 ticks row"; exit 1; }
echo "replay inputs: ok ($n ticks)"
python3 -I "$d/net_session_test.py" --exe "$out/net_session_test" --replay-exe "$out/net_replay_test"
