"""What each run added to the shader disk cache (SHADER_CACHE=1, patches\shader-hitches).
usage: shcache_stat.py [<shadercache dir>]   (default: ..\..\jstars\run\shadercache relative to this file)

Reads the 32-byte header of every <kind>_<hash>.dxbc file (magic, version, h1, h2, hlsl_len,
blob_len), groups the files into runs by modification time (a gap of more than 120 s starts a
new run) and prints, per run: files by kind, bytes, HLSL-length and blob-size spread, and how
many files have (nearly) the same HLSL length. Many files within a few dozen bytes of each
other would be variants of one program (gamma/expand/shadow masks); a flat spread means
distinct programs. Read-only."""
import os, struct, sys, time, collections

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, '..', '..', 'jstars', 'run', 'shadercache')
    rows = []; bad = 0
    for f in os.listdir(d):
        p = os.path.join(d, f)
        if not f.endswith('.dxbc'): continue
        st = os.stat(p)
        with open(p, 'rb') as fh: h = fh.read(32)
        if len(h) < 32: bad += 1; continue
        magic, ver, h1, h2, hl, bl = struct.unpack('<4sIQQII', h)
        if magic != b'RSXC' or bl + 32 != st.st_size: bad += 1; continue
        rows.append((st.st_mtime, f.split('_')[0], hl, bl, ver))
    rows.sort()
    print('%s: %d files, %.1f MB, %d bad' % (d, len(rows), sum(r[3] + 32 for r in rows) / 1048576.0, bad))
    runs = []
    for r in rows:
        if runs and r[0] - runs[-1][-1][0] <= 120: runs[-1].append(r)
        else: runs.append([r])
    for i, run in enumerate(runs):
        t0 = time.strftime('%Y-%m-%d %H:%M', time.localtime(run[0][0]))
        t1 = time.strftime('%H:%M', time.localtime(run[-1][0]))
        kinds = collections.Counter(r[1] for r in run)
        print('== run %d  %s-%s  %d files (%s)  %.0f KB  version %s' % (
            i + 1, t0, t1, len(run), ', '.join('%s %d' % kv for kv in sorted(kinds.items())),
            sum(r[3] + 32 for r in run) / 1024.0, ','.join(str(v) for v in sorted(set(r[4] for r in run)))))
        for kind in sorted(kinds):
            lst = [r for r in run if r[1] == kind]
            hl = sorted(r[2] for r in lst); bl = sorted(r[3] for r in lst)
            same = collections.Counter(r[2] for r in lst)
            near = collections.Counter(r[2] // 64 for r in lst)
            print('   %s: hlsl_len min/med/max %d/%d/%d  blob %d/%d/%d  exact-length repeats %d  largest 64-byte cluster %d' % (
                kind, hl[0], hl[len(hl) // 2], hl[-1], bl[0], bl[len(bl) // 2], bl[-1],
                sum(c - 1 for c in same.values() if c > 1), max(near.values())))

if __name__ == '__main__':
    main()
