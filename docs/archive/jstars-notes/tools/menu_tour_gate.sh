#!/usr/bin/env bash
# Closed-loop tour with screenshots: title -> main menu -> Free Battle -> character select ->
# stage select -> loading -> intro cards -> fight. Each step presses a button and waits until
# screen_match.py recognises the next screen (refs in tools/refs), so load-time differences do
# not derail it. Needs save data present (CROSS on the title -> "Save data loaded" -> OK -> menu).
# usage: menu_tour_gate.sh <out_dir> [stage_rights]   (GATE_FILE=<path>: created when START
#   leaves Stage Select, deleted when the fight HUD appears; for NV3089_GATE-style logs)
#   stage_rights: press RIGHT that many times on Stage Select before starting (stage survey).
# Set any env switches before calling. Writes PNGs, stdout/stderr, and tour.log in <out_dir>.
set -u
OUT="$1"; RIGHTS="${2:-0}"; mkdir -p "$OUT"
J="$HOME/Desktop/PS3Recomp/jstars"
T="$HOME/Desktop/PS3Recomp/jstars-notes/tools"
SNAP="$(cygpath -w "$T/snap_now.ps1")"
PAD="$J/run/pad.txt"; CUR="$OUT/_cur.bmp"   # not TMP: that name is the exported Windows temp dir
log() { echo "[$(date +%H:%M:%S)] $*" | tee -a "$OUT/tour.log"; }
snap() { powershell -ExecutionPolicy Bypass -File "$SNAP" -Out "$(cygpath -w "$OUT/$1.png")" >/dev/null; }
grab() { { [ -n "$CUR" ] && rm -rf -- "$CUR"; }; powershell -ExecutionPolicy Bypass -File "$SNAP" -Out "$(cygpath -w "$CUR")" >/dev/null 2>&1; }
press() { echo "$1" > "$PAD"; }
alive() { tasklist 2>/dev/null | grep -qi JStarsRecomp; }
# wait_for <ref> <timeout_s> [button_to_repeat_every_8s]: 0 when seen, 1 on timeout or exit
wait_for() {
    local ref=$1 to=$2 btn=${3:-} t0=$SECONDS last=$SECONDS
    while (( SECONDS - t0 < to )); do
        alive || { log "game exited while waiting for $ref"; return 1; }
        grab
        local v; v=$(python3 "$T/screen_match.py" "$CUR" "$ref" 2>/dev/null | awk '{print $1}')
        if [ -n "$v" ] && awk "BEGIN{exit !($v < 25)}"; then log "saw $ref ($v) after $((SECONDS - t0)) s"; return 0; fi
        if [ -n "$btn" ] && (( SECONDS - last >= 8 )); then press "$btn"; last=$SECONDS; log "  $ref: diff=$v, pressed $btn"; cp "$CUR" "$OUT/_wait_$ref.bmp" 2>/dev/null; [ -d "$CUR" ] && log "  grab made a directory?"; fi
        sleep 1
    done
    log "timeout waiting for $ref"; return 1
}
finish() {
    cp "$J/run/stdout.txt" "$OUT/stdout.txt" 2>/dev/null; cp "$J/run/stderr.txt" "$OUT/stderr.txt" 2>/dev/null
    { taskkill //IM JStarsRecomp.exe >/dev/null 2>&1; sleep 3; taskkill //F //IM JStarsRecomp.exe >/dev/null 2>&1; }; wait $RUN 2>/dev/null
    cp "$J/run/stdout.txt" "$OUT/stdout.txt" 2>/dev/null; cp "$J/run/stderr.txt" "$OUT/stderr.txt" 2>/dev/null
    rm -f "$CUR"; log "tour done: $OUT ($1)"; exit 0
}

: > "$PAD"; rm -f "$J/run/stdout.txt"   # a stale log would satisfy the wait below at once
export PAD_FILE="$(cygpath -w "$PAD")" PAD_SCRIPT="30:0x4000" PERF=1 RSX_FRAMELOG=60
powershell -ExecutionPolicy Bypass -File "$(cygpath -w "$J/run_test.ps1")" 900 > "$OUT/run_test.txt" 2>&1 &
RUN=$!
until grep -q "PAD_SCRIPT t=30.0s" "$J/run/stdout.txt" 2>/dev/null; do sleep 2; done
until alive; do sleep 1; done

# Title. The boot CROSS (PAD_SCRIPT 30 s) may already have opened "Save data loaded" on the
# title; otherwise wait for the title (START returns there if the idle demo started) and CROSS.
screen_now() { grab; python3 "$T/screen_match.py" "$CUR" title dialog_ok main_menu 2>/dev/null | head -1; }
t0=$SECONDS; seen=""
while (( SECONDS - t0 < 240 )); do
    alive || finish "game exited before the title"
    r=$(screen_now); v=${r%% *}; k=${r##* }
    if [ -n "$v" ] && awk "BEGIN{exit !($v < 25)}"; then seen=$k; break; fi
    (( (SECONDS - t0) % 10 < 1 )) && log "  waiting for title: best=$r"
    sleep 1
done
log "boot screen: ${seen:-none}"
case "$seen" in
    title)     snap 05_title; press 0x4000; wait_for dialog_ok 30 || finish "no save dialog"; snap 06a_loaded ;;
    dialog_ok) snap 06a_loaded ;;
    main_menu) ;;
    *) press 0x0008; wait_for title 60 || finish "no title"; snap 05_title
       press 0x4000; wait_for dialog_ok 30 || finish "no save dialog"; snap 06a_loaded ;;
esac
[ "$seen" = main_menu ] || { press 0x4000; wait_for main_menu 30 || finish "no main menu"; }; sleep 2; snap 06_menu
for i in 1 2 3; do press "0x0040 6"; sleep 2; done
wait_for menu_freebattle 10 || finish "cursor not on Free Battle"; snap 07_menu_freebattle
press 0x4000; wait_for freebattle 20 || finish "no Free Battle screen"; sleep 1; snap 08_freebattle
press 0x4000; wait_for charsel 30 || finish "no character select"; sleep 2; snap 09_charsel
press 0x0008; wait_for stage 30 || finish "no stage select"; sleep 2
# Stage Select (the maintainer, 2026-10-21): CROSS opens the stage row, RIGHT N times picks the stage,
# CROSS confirms; START then starts the match.
if (( RIGHTS > 0 )); then
    press "0x4000 6"; sleep 2
    for ((i = 0; i < RIGHTS; i++)); do press "0x0020 6"; sleep 2; done
    press "0x4000 6"; sleep 2
fi
sleep 1; snap 10_stage
[ -n "${GATE_FILE:-}" ] && : > "$GATE_FILE" && log "gate on"
press 0x0008; wait_for press_x 120 || finish "no 'Now PRESS x'"; snap 11_loading
press 0x4000; sleep 20; snap 13_intro_a
sleep 25; snap 14_intro_b
wait_for fight_hud 300 || finish "fight HUD never appeared"; snap 15_fight_start
[ -n "${GATE_FILE:-}" ] && rm -f "$GATE_FILE" && log "gate off"
sleep 20; snap 16_fight_20s
# BURST=N: N extra screenshots ~0.2 s apart right after the 20 s shot (catches flashing frames).
for ((b = 0; b < ${BURST:-0}; b++)); do snap "16b_burst_$(printf %02d $b)"; sleep 0.2; done
press "0x4000 8"; sleep 0.6; snap 17_fight_jump
sleep 15; snap 18_fight_35s
finish ok
