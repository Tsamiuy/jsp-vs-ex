# Decoder for the game's $CMP files. Read-only on the game's data.
# $CMP header 0x20 bytes (+0x10 unpacked size), then chunks: tag, u32 unpacked, u32 chunk size incl. header, u32 x.
# $CL0: LZ tokens. Even t: literal run of t>>1 bytes. Odd t: copy (t>>1)&0x1F bytes from distance ((t>>6)<<8)|next byte.
# $CLH: the same LZ tokens, Huffman coded in an inner $CH0 block.
import struct


def lz(d, p, end, out, base):
    while p < end:
        t = d[p]; p += 1
        if not t & 1:
            n = t >> 1
            out += d[p:p + n]; p += n
        else:
            n = (t >> 1) & 0x1F
            dist = ((t >> 6) << 8) | d[p]; p += 1
            if dist == 0 or dist > len(out) - base: raise ValueError('bad dist %d at out %x in %x' % (dist, len(out), p))
            for _ in range(n): out.append(out[-dist])


def huff(d, p):
    assert d[p:p + 4] == b'$CH0', d[p:p + 4]
    out_n, in_n, cnt = struct.unpack('>III', d[p + 4:p + 16])
    if cnt == 1: return bytes([d[p + 19]]) * out_n   # one symbol, no bits
    tab = {}
    q = p + 16
    for k in range(cnt):
        c = d[q] | (d[q + 1] << 8); ln = d[q + 2] >> 3; sym = d[q + 3]
        tab[(ln, c)] = sym; q += 4
    res = bytearray(); end = p + in_n
    acc = 0; ln = 0
    bitpos = (q) * 8; lim = end * 8
    while len(res) < out_n:
        if bitpos >= lim: raise ValueError('huff ran out at %d of %d' % (len(res), out_n))
        acc = (acc << 1) | ((d[bitpos >> 3] >> (7 - (bitpos & 7))) & 1); ln += 1; bitpos += 1
        s = tab.get((ln, acc))
        if s is not None:
            res.append(s); acc = 0; ln = 0
        elif ln > 24:
            raise ValueError('no code at %d' % len(res))
    return bytes(res)


def decode(d):
    assert d[:4] == b'$CMP'
    total = struct.unpack('>I', d[16:20])[0]
    out = bytearray(); p = 0x20
    while p + 16 <= len(d) and d[p:p + 1] == b'$':
        tag = d[p:p + 4]; a, n, x = struct.unpack('>III', d[p + 4:p + 16])
        base = len(out)
        if tag == b'$CL0':
            lz(d, p + 16, p + n, out, base)
        elif tag == b'$CLH':
            h = huff(d, p + 16)
            assert h[:4] == b'$CL0'
            lz(h, 16, len(h), out, base)
        elif tag == b'$CR0':
            out += d[p + 16:p + n]
        elif tag == b'$CH0':
            out += huff(d, p)
        else:
            raise ValueError('tag %r at %x' % (tag, p))
        if len(out) - base != a: raise ValueError('chunk at %x gave %x of %x' % (p, len(out) - base, a))
        p += n
    return bytes(out), total
