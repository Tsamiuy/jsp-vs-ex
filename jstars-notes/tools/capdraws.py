#!/usr/bin/env python3
"""Per-draw view of one RSX_CAP frame: what each op drew, and what it changed.

usage: python3 capdraws.py <frame_NN dir> [--rt HEX] [--step N]

For every op in manifest.txt prints the draw line (fp, code, vs, blend key,
cmask, t0 and its size). If the frame was captured with RSX_CAP_PASSES
(pass_opNNNN_pre/post_rt_XXXXXXXX.bmp per draw), it also compares the
target before and after the draw: how many sampled pixels changed and the
bounding box of the change, so "this quad drew nothing" is a number. If
RSX_CAP_PASSES=2 wrote fp_opNNNN.hlsl, the header's fp_constants, alpha test
and texscale are printed under the op (one file per distinct program code,
named by the first op that used it; later ops with the same code refer to it).
Stdlib only, read-only. --rt limits the snapshot diff to one target offset.
"""
import os, re, struct, sys

def read_bmp(path):
    with open(path, 'rb') as f: d = f.read()
    if d[:2] != b'BM': return None
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    bpp = struct.unpack_from('<H', d, 28)[0]
    return d, off, w, abs(h), h < 0, bpp // 8

def diff(pa, pb, step):
    A = read_bmp(pa); B = read_bmp(pb)
    if not A or not B or A[2] != B[2] or A[3] != B[3]: return None
    da, oa, w, h, top, bp = A; db, ob = B[0], B[1]
    row = (w * bp + 3) & ~3
    n = chg = 0; x0 = y0 = 10**9; x1 = y1 = -1
    for y in range(0, h, step):
        ry = y if top else h - 1 - y
        base_a = oa + ry * row; base_b = ob + ry * row
        for x in range(0, w, step):
            n += 1
            i = x * bp
            if da[base_a + i:base_a + i + 3] != db[base_b + i:base_b + i + 3]:
                chg += 1
                if x < x0: x0 = x
                if x > x1: x1 = x
                if y < y0: y0 = y
                if y > y1: y1 = y
    return n, chg, (x0, y0, x1, y1) if chg else None

def fp_header(path):
    out = []
    with open(path, errors='replace') as f:
        for line in f:
            line = line.rstrip('\n')
            if line.startswith('*/'): break
            if 'fp_constants[' in line or 'alpha=' in line or line.startswith('/* fp='):
                out.append(line.strip())
    return out

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if not args: print(__doc__); return
    d = args[0]
    rt_only = None; step = 4
    for i, a in enumerate(sys.argv):
        if a == '--rt': rt_only = int(sys.argv[i + 1], 16)
        if a == '--step': step = int(sys.argv[i + 1])
    files = os.listdir(d)
    snaps = {}
    for f in files:
        m = re.match(r'pass_op(\d+)_(pre|post)_rt_([0-9A-Fa-f]+)\.bmp$', f)
        if m: snaps.setdefault((int(m.group(1)), int(m.group(3), 16)), {})[m.group(2)] = os.path.join(d, f)
    fps = {}
    for f in files:
        m = re.match(r'fp_op(\d+)\.hlsl$', f)
        if m: fps[int(m.group(1))] = os.path.join(d, f)
    code_of = {}
    with open(os.path.join(d, 'manifest.txt'), errors='replace') as f:
        lines = [l.rstrip('\r\n') for l in f]
    tex = {}
    cur = None
    for l in lines:
        m = re.match(r'op(\d+) (DRAW|CLEAR)\s+(.*)', l)
        if m:
            cur = int(m.group(1)); tex[cur] = []
            continue
        if cur is not None and l.startswith('      t'):
            tex[cur].append(l.strip())
    print(lines[0])
    for l in lines:
        m = re.match(r'op(\d+) (DRAW|CLEAR)\s+(.*)', l)
        if not m: continue
        op = int(m.group(1)); kind = m.group(2); rest = m.group(3)
        rt = re.search(r'rt=0x([0-9A-Fa-f]+)', rest); rt = int(rt.group(1), 16) if rt else 0
        code = re.search(r'code=0x([0-9A-Fa-f]+)', rest)
        short = re.sub(r' mrt=0x0/0x0/0x0', '', rest)
        short = re.sub(r' t2=0x00000000 t3=0x00000000', '', short)
        t0 = next((t for t in tex.get(op, []) if t.startswith('t0 ')), '')
        t0 = re.sub(r'remap=0x[0-9A-Fa-f]+', '', t0).replace('t0 off=', 't0@')
        print('op%03d %s %s %s' % (op, kind, short, t0))
        if code:
            c = code.group(1)
            if op in fps: code_of[c] = op
            src = fps.get(op) or (fps.get(code_of[c]) if c in code_of else None)
            if src:
                for h in fp_header(src):
                    if h.startswith('/* fp='): continue
                    print('        %s%s' % (h, '' if op in fps else '   (fp_op%04d.hlsl)' % code_of[c]))
        key = (op, rt)
        if key in snaps and 'pre' in snaps[key] and 'post' in snaps[key] and (rt_only is None or rt == rt_only):
            r = diff(snaps[key]['pre'], snaps[key]['post'], step)
            if r:
                n, chg, bb = r
                print('        changed %d/%d sampled px (%.1f%%)%s' % (chg, n, 100.0 * chg / max(1, n),
                      ('  bbox x%d-%d y%d-%d' % bb) if bb else '  -> drew NOTHING visible'))

if __name__ == '__main__':
    main()
