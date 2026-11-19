#!/usr/bin/env python3
"""The palette (data/theme.json) for the site, the README badges, the repository art and the installer art.

    from theme import load, rgb, contrast
    t = load()                 # the parsed file
    t.color("panel")           # '#1B1B1F'; "light.burst_base" reads a mode colour
    contrast("#000", "#fff")   # 21.0 (WCAG 2 relative-luminance ratio)

CLI: python3 -I tools/site/theme.py   # prints every checked pair with its ratio; exit 1 when one is under its minimum
Stdlib only.
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
PATH = ROOT / "data" / "theme.json"


class Theme:
    def __init__(self, data):
        self.data = data
        self.colors = data["colors"]
        self.modes = data["modes"]
        self.fonts = data["fonts"]

    def color(self, name):
        if "." in name:
            mode, key = name.split(".", 1)
            return self.modes[mode][key]
        return self.colors[name]

    def rgb(self, name):
        return rgb(self.color(name))

    def pair_ratios(self):
        """[(fg name, bg name, ratio, minimum)] for every pair the palette declares."""
        return [(fg, bg, contrast(self.color(fg), self.color(bg)), float(mn)) for fg, bg, mn in self.data["pairs"]]


def load(path=PATH):
    return Theme(json.loads(pathlib.Path(path).read_text(encoding="utf-8")))


def rgb(hexstr):
    h = hexstr.lstrip("#")
    if len(h) == 3:
        h = "".join(c * 2 for c in h)
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def _lum(c):
    def ch(v):
        v /= 255.0
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = (ch(v) for v in rgb(c))
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(a, b):
    la, lb = sorted((_lum(a), _lum(b)), reverse=True)
    return round((la + 0.05) / (lb + 0.05), 2)


def main():
    t = load()
    bad = 0
    for fg, bg, r, mn in t.pair_ratios():
        ok = r >= mn
        bad += not ok
        print(f"{'ok ' if ok else 'LOW'} {r:5.2f} (min {mn}) {fg} on {bg}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
