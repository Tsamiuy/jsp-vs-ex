#!/usr/bin/env python3
"""texbin.py <tex_XXXXXXXX_WxH_FF.bin> [out.png]
Decode a texture source dump written by RSX_CAP_TEX=1 (patches\loading-tips)
to a PNG. Width, height and RSX format come from the file name. Handles DXT1
(0x86), DXT3 (0x87), DXT5 (0x88) and linear A8R8G8B8 (0x85); LN/UN bits are
ignored. Stdlib only (zlib PNG writer). Also prints a byte histogram summary
so a blank buffer (all FF / all 00) is obvious without opening the image."""
import sys, os, re, struct, zlib, collections

def c565(v):
    r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))

def decode_bc(data, w, h, fmt):
    bw, bh = (w + 3) // 4, (h + 3) // 4
    bs = 8 if fmt == 0x86 else 16
    px = bytearray(w * h * 4)
    for by in range(bh):
        for bx in range(bw):
            o = (by * bw + bx) * bs
            if o + bs > len(data): break
            blk = data[o:o + bs]
            alpha = None
            if fmt == 0x87:
                a16 = struct.unpack('<4H', blk[:8]); alpha = [((a16[i // 4] >> ((i % 4) * 4)) & 15) * 17 for i in range(16)]
                blk = blk[8:]
            elif fmt == 0x88:
                a0, a1 = blk[0], blk[1]
                bits = int.from_bytes(blk[2:8], 'little')
                pal = [a0, a1]
                if a0 > a1: pal += [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
                else: pal += [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
                alpha = [pal[(bits >> (3 * i)) & 7] for i in range(16)]
                blk = blk[8:]
            c0, c1 = struct.unpack('<2H', blk[:4]); idx = int.from_bytes(blk[4:8], 'little')
            p0, p1 = c565(c0), c565(c1)
            if c0 > c1 or fmt != 0x86:
                p2 = tuple((2 * a + b) // 3 for a, b in zip(p0, p1)); p3 = tuple((a + 2 * b) // 3 for a, b in zip(p0, p1))
                pal = [p0, p1, p2, p3]
            else:
                p2 = tuple((a + b) // 2 for a, b in zip(p0, p1)); pal = [p0, p1, p2, (0, 0, 0)]
            for i in range(16):
                x, y = bx * 4 + i % 4, by * 4 + i // 4
                if x >= w or y >= h: continue
                r, g, b = pal[(idx >> (2 * i)) & 3]
                a = 255 if alpha is None else alpha[i]
                if fmt == 0x86 and c0 <= c1 and ((idx >> (2 * i)) & 3) == 3: a = 0
                q = (y * w + x) * 4; px[q:q + 4] = bytes((r, g, b, a))
    return px

def decode_argb(data, w, h, pitch):
    px = bytearray(w * h * 4)
    for y in range(h):
        row = data[y * pitch:y * pitch + w * 4]
        for x in range(min(w, len(row) // 4)):
            a, r, g, b = row[x * 4:x * 4 + 4]
            q = (y * w + x) * 4; px[q:q + 4] = bytes((r, g, b, a))
    return px

def write_png(path, w, h, px):
    raw = b''.join(b'\x00' + bytes(px[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))

def main():
    src = sys.argv[1]
    m = re.search(r'tex_([0-9A-Fa-f]{8})_(\d+)x(\d+)_([0-9A-Fa-f]{2})\.bin$', os.path.basename(src))
    if not m: sys.exit("name must be tex_<off>_<w>x<h>_<fmt>.bin")
    w, h, fmt = int(m.group(2)), int(m.group(3)), int(m.group(4), 16)
    data = open(src, 'rb').read()
    hist = collections.Counter(data)
    top = hist.most_common(3)
    print("%s: %d bytes, distinct byte values %d, top: %s" % (src, len(data), len(hist),
          ", ".join("%02X x%d (%.1f%%)" % (v, n, 100.0 * n / len(data)) for v, n in top)))
    base = fmt & 0x9F
    if base in (0x86, 0x87, 0x88):
        if base == 0x86:
            blocks = len(data) // 8
            white = sum(1 for o in range(0, blocks * 8, 8) if data[o:o + 8] == b'\xff\xff\xff\xff\x00\x00\x00\x00')
            print("DXT1 blocks %d, pure-white blocks (FFFF FFFF 00000000) %d (%.1f%%)" % (blocks, white, 100.0 * white / max(blocks, 1)))
        px = decode_bc(data, w, h, base)
    elif base == 0x85:
        px = decode_argb(data, w, h, w * 4)
    else:
        sys.exit("format 0x%02X not handled (DXT1/3/5 and A8R8G8B8 only)" % fmt)
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(src)[0] + '.png'
    write_png(out, w, h, px)
    print("wrote", out)

if __name__ == '__main__':
    main()
