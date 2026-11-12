#!/usr/bin/env python3
"""Summarize an RSX_CAP capture: how much of each render target has content.

usage: python3 capstat.py <capture dir | frame dir> [--step N] [--thresh T]

For every frame_NN folder it prints the frame number from manifest.txt and,
for each BMP (render targets + backbuffer), its size, the share of sampled
pixels with R+G+B > thresh, the mean colour, and how many draws/clears wrote
it and how many draws sampled it. "0.0 %" means nothing was drawn (or it was
cleared to black), which the old by-eye method could not tell from "dark".

Stdlib only (no Pillow). Reads uncompressed 24/32-bit BMPs as the backend
writes them. Read-only: it never changes the capture.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from capmanifest import load_manifest  # noqa: E402


def bmp_stats(path, step=8, thresh=6):
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"BM":
        return None
    pix_off = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if bpp not in (24, 32):
        return {"w": w, "h": abs(h), "err": f"{bpp} bpp"}
    h = abs(h)
    bypp = bpp // 8
    row = ((w * bypp + 3) // 4) * 4
    n = lit = 0
    sr = sg = sb = 0
    for y in range(0, h, step):
        base = pix_off + y * row
        for x in range(0, w, step):
            i = base + x * bypp
            b, g, r = data[i], data[i + 1], data[i + 2]
            n += 1
            sr += r; sg += g; sb += b
            if r + g + b > thresh:
                lit += 1
    n = max(n, 1)
    return {"w": w, "h": h, "lit": lit, "n": n,
            "mean": (sr / n, sg / n, sb / n)}


def frame_dirs(path):
    if os.path.isfile(os.path.join(path, "manifest.txt")):
        return [path]
    return sorted(os.path.join(path, d) for d in os.listdir(path)
                  if d.startswith("frame_") and os.path.isdir(os.path.join(path, d)))


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    opts = dict(re.findall(r"--(\w+)=?(\S*)", " ".join(a for a in argv if a.startswith("--"))))
    step = int(opts.get("step") or 8)
    thresh = int(opts.get("thresh") or 6)
    if not args:
        print(__doc__)
        return 1
    for fd in frame_dirs(args[0]):
        man = load_manifest(os.path.join(fd, "manifest.txt"))
        writes, reads = {}, {}
        for op in man["ops"]:
            writes[op["rt"]] = writes.get(op["rt"], 0) + 1
            for t in op["tex"].values():
                reads[t] = reads.get(t, 0) + 1
        print(f"== {os.path.basename(fd)}  frame_count={man['header'].get('frame_count', '?')} "
              f"ops={len(man['ops'])}")
        print(f"   {'target':<20} {'size':>10} {'lit%':>6}  {'mean RGB':<16} {'writes':>6} {'reads':>6}")
        for name in sorted(os.listdir(fd)):
            if not name.endswith(".bmp"):
                continue
            s = bmp_stats(os.path.join(fd, name), step, thresh)
            key = name[3:-4].upper() if name.startswith("rt_") else "00000000"
            if s is None or "err" in s:
                print(f"   {name:<20} unreadable {s and s.get('err')}")
                continue
            m = s["mean"]
            print(f"   {name[:-4]:<20} {s['w']:>4}x{s['h']:<5} {100.0 * s['lit'] / s['n']:6.1f}  "
                  f"{m[0]:5.1f},{m[1]:5.1f},{m[2]:5.1f}  {writes.get(key, 0):>6} {reads.get(key, 0):>6}")
        # Linear (LN) textures that are not tracked RTs: often a render target the
        # backend never saw being drawn (other MRT slot, depth, earlier frame).
        untracked = sorted({op["tex"][t] for op in man["ops"] for t, ti in op["texinfo"].items()
                            if ti["layout"] == "LN" and op["tex"].get(t) not in man["rts"]})
        if untracked:
            print("   sampled LN textures that are not tracked RTs: " + ", ".join(untracked))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
