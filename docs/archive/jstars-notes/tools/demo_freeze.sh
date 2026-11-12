#!/usr/bin/env bash
# Attract demo for the 2nd-fight freeze: X at 40 s (video skip), run for RUN_SECS (default 720),
# screenshots every 10 s from 450 s, and a `threads` dump every 10 s from 450 s (threads_<t>.txt).
# usage: demo_freeze.sh <out_dir>   (set env switches before calling)
set -u
OUT="$1"; RUN="${RUN_SECS:-720}"; mkdir -p "$OUT"
J="$HOME/Desktop/PS3Recomp/jstars"
SNAP="$(cygpath -w "$HOME/Desktop/PS3Recomp/jstars-notes/tools/snap_now.ps1")"
alive() { tasklist 2>/dev/null | grep -qi JStarsRecomp; }
rm -f "$J/run/stdout.txt"; unset PAD_FILE
export PAD_SCRIPT="${DEMO_SKIP-40:0x4000}"; [ -z "$PAD_SCRIPT" ] && unset PAD_SCRIPT
export PERF=1
DBG="$OUT/dbg.txt"; : > "$DBG"; : > "$DBG.out"; export PS3_DEBUG="$(cygpath -w "$DBG" | tr '\' '/')"
powershell -ExecutionPolicy Bypass -File "$(cygpath -w "$J/run_test.ps1")" "$RUN" > "$OUT/run_test.txt" 2>&1 &
RUN_PID=$!
until alive; do sleep 1; done; t0=$SECONDS
for ((s = 450; s < RUN - 5; s += 10)); do
  while (( SECONDS - t0 < s )); do sleep 1; done
  alive || break
  powershell -ExecutionPolicy Bypass -File "$SNAP" -Out "$(cygpath -w "$OUT/t$s.png")" >/dev/null
  echo threads > "$DBG"; sleep 2; cp "$DBG.out" "$OUT/threads_$s.txt"
done
wait $RUN_PID
cp "$J/run/stdout.txt" "$J/run/stderr.txt" "$OUT/" 2>/dev/null
echo "freeze demo done: $OUT"
