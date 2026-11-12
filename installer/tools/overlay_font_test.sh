#!/usr/bin/env bash
# Host test for the overlay text spacing: draws the glyph atlas menu_font_art.py makes (needs Pillow, numpy and the pinned font, as the
# payload assembly does) through a model of the two-pass text blit of toolkit patch 2517. No game, no GPU. Optional: PPM=out.ppm TEXT="..." writes a render.
#   bash installer/tools/overlay_font_test.sh [FIRA.ttf]
set -u
d=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/jsp_overlay_font.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
python3 -I "$d/menu_font_art.py" "$tmp" ${1:+"$1"} >/dev/null || exit $?
g++ -std=c++20 -Wall -o "$tmp/t" "$d/overlay_font_test.cpp" || exit $?
if [ -n "${PPM:-}" ]; then "$tmp/t" "$tmp/menu_font.jmf" --ppm "$PPM" "${TEXT:-Port Settings - Online}"; exit $?; fi
"$tmp/t" "$tmp/menu_font.jmf"
