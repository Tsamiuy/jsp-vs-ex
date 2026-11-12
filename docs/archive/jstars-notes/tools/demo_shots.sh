#!/usr/bin/env bash
# Run the idle demo and take screenshots at fixed times after the game starts (Ace card ~200 s).
# usage: demo_shots.sh <out_dir> [seconds_list]   (set env switches before calling)
set -u
OUT="$1"; AT="${2:-150 190 200 210 230 250}"; mkdir -p "$OUT"
J="$HOME/Desktop/PS3Recomp/jstars"
SNAP="$(cygpath -w "$HOME/Desktop/PS3Recomp/jstars-notes/tools/snap_now.ps1")"
alive() { tasklist 2>/dev/null | grep -qi JStarsRecomp; }
rm -f "$J/run/stdout.txt"; unset PAD_FILE
# X at 40 s skips the intro video (~1:35 saved, the maintainer 2026-10-24); DEMO_SKIP= (empty) keeps the old timing.
export PAD_SCRIPT="${DEMO_SKIP-40:0x4000}"; [ -z "$PAD_SCRIPT" ] && unset PAD_SCRIPT
export PERF=1
powershell -ExecutionPolicy Bypass -File "$(cygpath -w "$J/run_test.ps1")" 270 > "$OUT/run_test.txt" 2>&1 &
RUN=$!
until alive; do sleep 1; done; t0=$SECONDS
for s in $AT; do
  while (( SECONDS - t0 < s )); do sleep 1; done
  alive || break
  powershell -ExecutionPolicy Bypass -File "$SNAP" -Out "$(cygpath -w "$OUT/t$s.png")" >/dev/null
done
wait $RUN
cp "$J/run/stdout.txt" "$J/run/stderr.txt" "$OUT/" 2>/dev/null
echo "demo done: $OUT"
