# dxt5enc.py in.png out.bin out.inc -- 512x64 straight-alpha PNG -> RSX DXT5 (0x88) blocks + C array
import sys, struct
from png import read
src, obin, oinc = sys.argv[1:4]
w, h, bpp, rows = read(src)
def px(x, y): r = rows[y]; i = x*bpp; return r[i], r[i+1], r[i+2], r[i+3]
def to565(c): return ((c[0]>>3)<<11)|((c[1]>>2)<<5)|(c[2]>>3)
def c565(v):
    r=(v>>11)&31; g=(v>>5)&63; b=v&31
    return ((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2))
clear = (255,)*3 if len(sys.argv) > 4 and sys.argv[4] == "white" else (0,)*3   # colour of clear texels (bilinear edges)
out = bytearray()
for by in range(h//4):
    for bx in range(w//4):
        p = [px(bx*4+i%4, by*4+i//4) for i in range(16)]
        al = [q[3] for q in p]; a0, a1 = max(al), min(al); abits = 0
        if a0 != a1:
            pal = [a0, a1] + [((7-i)*a0+i*a1)//7 for i in range(1, 7)]
            for i, a in enumerate(al): abits |= min(range(8), key=lambda j: abs(pal[j]-a)) << (3*i)
        vis = [q[:3] for q in p if q[3] > 0] or [clear]
        lum = lambda c: c[0]*3+c[1]*6+c[2]
        hi = max(vis, key=lum); lo = min(vis, key=lum)
        c0, c1 = to565(hi), to565(lo)
        if c0 < c1: c0, c1 = c1, c0
        e0, e1 = c565(c0), c565(c1)
        cp = [e0, e1, tuple((2*a+b)//3 for a, b in zip(e0, e1)), tuple((a+2*b)//3 for a, b in zip(e0, e1))]
        idx = 0
        if c0 != c1:
            for i, q in enumerate(p):
                qq = q[:3] if q[3] > 0 else clear
                idx |= min(range(4), key=lambda j: sum((cp[j][t]-qq[t])**2 for t in range(3))) << (2*i)
        out += bytes([a0, a1]) + abits.to_bytes(6, 'little') + struct.pack('<2H', c0, c1) + idx.to_bytes(4, 'little')
open(obin, 'wb').write(out)
open(oinc, 'w').write('\n'.join(','.join('0x%02X' % x for x in out[i:i+24]) + ',' for i in range(0, len(out), 24)) + '\n')
print(len(out))
