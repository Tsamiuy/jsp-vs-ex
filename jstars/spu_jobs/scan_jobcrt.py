"""Find every JOBCRT SPURS job binary embedded in EBOOT.elf.

Layout (seen in all J-Stars jobs):
  +0x00..0x1F code (entry), +0x20 0xA5 x16 pad, +0x30 "JOBCRT Ver14",
  +0x3C u32 = offset of the "%SPURS JOB INFO%" block minus 0x10,
  info block: tag(16) u32 u32 ff*8, then (offset,size) u32 pairs ending with
  ffffffff ffffffff; the binary ends after that terminator, rounded up to 16.
That end is the job descriptor's sizeBinary, so FNV-1a over it (the runtime's
truncated basis) gives the fingerprint the runtime dispatches by.

usage: python3 scan_jobcrt.py EBOOT.elf [--json out.json]
"""
import json, struct, sys

def fnv(d):
    h = 1469598103934665603          # runtime's (truncated) basis, see spu_workload.c
    for c in d:
        h = ((h ^ c) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h

def segments(b):
    phoff, = struct.unpack('>Q', b[0x20:0x28]); phnum, = struct.unpack('>H', b[0x38:0x3A])
    return [struct.unpack('>IIQQQQQQ', b[phoff + i * 56:phoff + (i + 1) * 56]) for i in range(phnum)]

def scan(b):
    segs = segments(b)
    def va(o):
        for t, f, off, v, pa, fs, ms, al in segs:
            if t == 1 and off <= o < off + fs:
                return v + o - off
    jobs = []
    o = b.find(b'JOBCRT Ver14')
    while o >= 0:
        s = o - 0x30
        if b[s + 0x20:s + 0x30] == b'\xA5' * 16:
            info = s + struct.unpack('>I', b[s + 0x3C:s + 0x40])[0] + 0x10
            if b[info:info + 16] == b'%SPURS JOB INFO%':
                p = info + 0x20
                while b[p:p + 8] != b'\xFF' * 8:
                    p += 8
                end = s + ((p + 8 - s + 15) & ~15)   # sizeBinary is 16-byte aligned
                size = end - s
                d = b[s:end]
                # code end: last "bi $lr" before the data area, +4, 16-aligned
                data_off = struct.unpack('>I', b[s + 0x40:s + 0x44])[0]
                last = max(k for k in range(0, min(data_off, size) - 3, 4) if d[k:k + 4] == b'\x35\x00\x00\x00')
                code_end = (last + 4 + 15) & ~15
                jobs.append(dict(va=va(s), size=size, fp='%016X' % fnv(d), code_end=code_end, off=s))
        o = b.find(b'JOBCRT Ver14', o + 1)
    return jobs

if __name__ == '__main__':
    b = open(sys.argv[1], 'rb').read()
    jobs = scan(b)
    for j in jobs:
        print('va 0x%08X size %6d fp %s code_end 0x%X' % (j['va'], j['size'], j['fp'], j['code_end']))
    if '--json' in sys.argv:
        json.dump(jobs, open(sys.argv[sys.argv.index('--json') + 1], 'w'), indent=1)
