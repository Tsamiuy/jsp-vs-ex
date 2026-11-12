#!/usr/bin/env python3
"""Find, lift and register every JOBCRT SPURS job embedded in the game's EBOOT (ps3recomp v0.19.0 tools).

After this repository's v0.15 jstars/spu_jobs/scan_jobcrt.py and lift_all_jobs.py (Tsamiuy, MIT): same scan, sizing and fingerprint,
moved to the v0.19.0 tools. Run by installer/tools/firstrun.py on the player's PC; the lifted code is made from the player's own
EBOOT and stays in the install folder.

    python lift_jobs.py --eboot EBOOT.elf --ps3recomp DIR --out DIR [-j 4]

Layout of a job (all 18 in the US EBOOT): +0x00..0x1F code (the entry branches over the header), +0x20 0xA5 x16,
+0x30 "JOBCRT Ver14", +0x3C u32 = offset of the "%SPURS JOB INFO%" block minus 0x10, +0x40 u32 = data-area offset.
The info block is tag(16) u32 u32 ff*8, then (offset, size) u32 pairs ending in ff*8; the binary ends after that
terminator, rounded up to 16 bytes. That end is the job descriptor's sizeBinary, so FNV-1a-64 over it with the
runtime's basis (spu_workload_fingerprint) is what spu_workload_dispatch_job looks up.

Writes <out>/<name>/spu_recomp.{c,h} (CMake globs <out>/*/spu_recomp.c), <out>/spu_workloads.c (registration, constructor)
and <out>/jobs.json. Job binaries and wrapped ELFs go to <out>/bin/.
"""
import argparse, json, os, re, struct, subprocess, sys
from concurrent.futures import ThreadPoolExecutor


def fnv(d):
    h = 1469598103934665603          # the runtime's basis (spu_workload.c), not the textbook one
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
        return None
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
                size = (p + 8 - s + 15) & ~15
                d = b[s:s + size]
                data_off = struct.unpack('>I', b[s + 0x40:s + 0x44])[0]
                # code end: after the last "bi $lr" before the data area, 16-aligned
                last = max(k for k in range(0, min(data_off, size) - 3, 4) if d[k:k + 4] == b'\x35\x00\x00\x00')
                jobs.append(dict(va=va(s), off=s, size=size, fp='%016X' % fnv(d),
                                 code_end=(last + 4 + 15) & ~15))
        o = b.find(b'JOBCRT Ver14', o + 1)
    return jobs


# Names of the jobs identified in the v0.15 notes (docs/archive/jstars-notes/FINDINGS.md); the rest are named by address.
KNOWN = {0x00829A80: 'crijob_asrvoice', 0x00819780: 'crijob_atomdec'}


def name_for(j):
    return KNOWN.get(j['va'], 'spujob_%X' % j['va'])


_INC_RE = re.compile(r'#include\s+"(?:\.\./)+runtime/spu/([A-Za-z0-9_]+\.h)"')


def lift(j, b, args):
    nm = name_for(j)
    out = os.path.join(args.out, nm)
    bdir = os.path.join(args.out, 'bin')
    os.makedirs(bdir, exist_ok=True)
    bin_p, elf_p = os.path.join(bdir, nm + '.bin'), os.path.join(bdir, nm + '.elf')
    with open(bin_p, 'wb') as f:
        f.write(b[j['off']:j['off'] + j['size']])
    tools = os.path.join(args.ps3recomp, 'tools')
    py = sys.executable
    subprocess.run([py, '-I', os.path.join(tools, 'wrap_spu_elf.py'), bin_p, '--entry', '0x0', '--base', '0x0',
                    '--out', elf_p], check=True, capture_output=True)
    r = subprocess.run([py, '-I', os.path.join(tools, 'spu_lifter.py'), '--auto-functions', elf_p, '--base', '0x0',
                        '--code-end', hex(j['code_end']), '--symbol-prefix', 'jsp_%s_' % nm, '-o', out,
                        '--source-name', 'spu_recomp.c', '--header-name', 'spu_recomp.h',
                        '--report', os.path.join(bdir, nm + '.report.json')],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return False, '%s: lifter rc=%d\n%s' % (nm, r.returncode, (r.stdout + r.stderr)[-2000:])
    for fn in ('spu_recomp.c', 'spu_recomp.h'):           # location-independent includes (-I runtime/spu)
        p = os.path.join(out, fn)
        t = open(p, encoding='utf-8').read()
        open(p, 'w', encoding='utf-8', newline='\n').write(_INC_RE.sub(r'#include "\1"', t))
    tail = [l.strip() for l in (r.stdout + r.stderr).splitlines() if 'lifted' in l.lower() or 'unsupported' in l]
    return True, '%s: %s' % (nm, ' | '.join(tail[-3:]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--eboot', required=True)
    ap.add_argument('--ps3recomp', required=True, help='the patched toolkit folder (has tools/)')
    ap.add_argument('--out', required=True)
    ap.add_argument('-j', type=int, default=4)
    args = ap.parse_args()
    b = open(args.eboot, 'rb').read()
    jobs = scan(b)
    if not jobs:
        sys.exit('no JOBCRT jobs found in %s' % args.eboot)
    os.makedirs(args.out, exist_ok=True)
    ok = True
    with ThreadPoolExecutor(args.j) as ex:
        for good, line in ex.map(lambda j: lift(j, b, args), jobs):
            print(line)
            ok &= good
    if not ok:
        sys.exit('lift failed')
    ext, reg, table = [], [], []
    for i, j in enumerate(jobs, 1):
        nm = name_for(j)
        j['name'], j['image_id'] = nm, i
        ext.append('extern void jsp_%s_spu_func_00000000(spu_context*);' % nm)
        ext.append('extern void jsp_%s_spu_recomp_register(void);' % nm)
        reg.append('    spu_begin_image(%d); jsp_%s_spu_recomp_register();' % (i, nm))
        reg.append('    spu_workload_register_img(0x%sULL, jsp_%s_spu_func_00000000, %d, "%s");' % (j['fp'], nm, i, nm))
        table.append(' *   %2d %-16s EBOOT 0x%08X %6d B  fp %s  code end 0x%X'
                     % (i, nm, j['va'], j['size'], j['fp'], j['code_end']))
    src = '''/* spu_workloads.c -- registers J-Stars' lifted SPURS job binaries (game-derived; generated on the player's PC, not in git).
 *
 * GENERATED by installer/tools/lift_jobs.py (after the v0.15 lift_all_jobs.py) -- edit that script.
 * Every JOBCRT job in the EBOOT, wrapped as an SPU ELF (base 0, entry 0: the runtime loads a job at LS 0) and
 * lifted with spu_lifter.py --auto-functions --code-end. Fingerprint = FNV-1a-64 (runtime basis) over sizeBinary.
 *
%s
 */
#include "spu_workload.h"

extern void spu_begin_image(int image_id);

%s

void jsp_spu_workloads_register(void)
{
%s
    spu_begin_image(0);
}

__attribute__((constructor)) static void jsp_spu_workloads_register_ctor(void)
{
    jsp_spu_workloads_register();
}
''' % ('\n'.join(table), '\n'.join(ext), '\n'.join(reg))
    open(os.path.join(args.out, 'spu_workloads.c'), 'w', encoding='utf-8', newline='\n').write(src)
    for j in jobs:
        j['va'] = '0x%08X' % j['va']
    json.dump(jobs, open(os.path.join(args.out, 'jobs.json'), 'w'), indent=1)
    print('wrote spu_workloads.c with %d jobs to %s' % (len(jobs), args.out))


if __name__ == '__main__':
    main()
