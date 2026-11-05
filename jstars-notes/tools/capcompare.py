#!/usr/bin/env python3
"""Compare render-target content between two RSX_CAP captures (e.g. a bisect pair).

usage: python3 capcompare.py <capture A> <capture B> [--step N]

Frames are paired by position (frame_00 with frame_00, ...). For every BMP that
exists in either, prints lit% in A and B and the difference, so "which RT
went black when I flipped switch X" is one command. Read-only.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from capstat import bmp_stats, frame_dirs  # noqa: E402


def lit(path, step):
    if not os.path.exists(path):
        return None
    s = bmp_stats(path, step)
    return None if s is None or "err" in s else 100.0 * s["lit"] / s["n"]


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    step = 8
    for a in argv:
        if a.startswith("--step="):
            step = int(a.split("=", 1)[1])
    if len(args) != 2:
        print(__doc__)
        return 1
    fa, fb = frame_dirs(args[0]), frame_dirs(args[1])
    for da, db in zip(fa, fb):
        print(f"== A {da}\n   B {db}")
        names = sorted({n for d in (da, db) for n in os.listdir(d) if n.endswith(".bmp")})
        for n in names:
            a, b = lit(os.path.join(da, n), step), lit(os.path.join(db, n), step)
            fmt = lambda v: "   -  " if v is None else f"{v:6.1f}"
            diff = "" if a is None or b is None else f"{b - a:+7.1f}"
            flag = "  <<" if a is not None and b is not None and abs(b - a) >= 5 else ""
            print(f"   {n[:-4]:<16} {fmt(a)} {fmt(b)} {diff}{flag}")
    if len(fa) != len(fb):
        print(f"(frame counts differ: {len(fa)} vs {len(fb)}; extra frames ignored)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
