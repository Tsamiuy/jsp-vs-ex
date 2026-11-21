"""dxt5enc_any.py in.png out.inc [white] -- straight-alpha PNG of any size -> RSX DXT5 blocks (linear rows of 4x4
blocks, the last block row / column padded with clear texels) as a C array body. Same block coder as dxt5enc.py
(Exit Game label), which only took 512x64. Made for the main menu row labels (300x43, 2026-11-19)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from png import read
src, oinc = sys.argv[1:3]
w, h, bpp, rows = read(src)
clear = (255,) * 3 if len(sys.argv) > 3 and sys.argv[3] == "white" else (0,) * 3
def px(x, y):
    if x >= w or y >= h: return clear + (0,)
    r = rows[y]; i = x * bpp
    return (r[i], r[i + 1], r[i + 2], r[i + 3] if bpp == 4 else 255)
def to565(c): return ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)
def c565(v):
    r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))
out = bytearray()
for by in range((h + 3) // 4):
    for bx in range((w + 3) // 4):
        p = [px(bx * 4 + i % 4, by * 4 + i // 4) for i in range(16)]
        al = [q[3] for q in p]; a0, a1 = max(al), min(al); abits = 0
        if a0 != a1:
            pal = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
            for i, a in enumerate(al): abits |= min(range(8), key=lambda j: abs(pal[j] - a)) << (3 * i)
        vis = [q[:3] for q in p if q[3] > 0] or [clear]
        lum = lambda c: c[0] * 3 + c[1] * 6 + c[2]
        hi = max(vis, key=lum); lo = min(vis, key=lum)
        c0, c1 = to565(hi), to565(lo)
        if c0 < c1: c0, c1 = c1, c0
        e0, e1 = c565(c0), c565(c1)
        cp = [e0, e1, tuple((2 * a + b) // 3 for a, b in zip(e0, e1)), tuple((a + 2 * b) // 3 for a, b in zip(e0, e1))]
        idx = 0
        if c0 != c1:
            for i, q in enumerate(p):
                qq = q[:3] if q[3] > 0 else clear
                idx |= min(range(4), key=lambda j: sum((cp[j][k] - qq[k]) ** 2 for k in range(3))) << (2 * i)
        out += bytes([a0, a1]) + abits.to_bytes(6, 'little') + c0.to_bytes(2, 'little') + c1.to_bytes(2, 'little') + idx.to_bytes(4, 'little')
open(oinc, 'w').write(',\n'.join(', '.join('0x%02X' % b for b in out[i:i + 16]) for i in range(0, len(out), 16)) + '\n')
print(src, w, h, len(out), 'bytes')
