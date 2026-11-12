"""Drawing-thread cost per draw from PERF=1 [PERF] + [PERF6] lines (the AI assistant, 2026-10-25).
usage: drawcost.py <run_dir>...   (FIGHT_SECS=70 default; the last FIGHT_SECS of batches = the fight)
draws/batch = [PERF] pso lookups / 20. busy = render_frame + drain + submit + present (ms/batch).
us/draw does not depend on how many draws the camera sees, so it compares settings across runs
whose fps differ because of the view (Hidden Leaf village side ~3000 draws/batch, park ~1400)."""
import re, sys, os
SECS = int(os.environ.get('FIGHT_SECS', '70'))
for run in sys.argv[1:]:
    s = open(os.path.join(run, 'stderr.txt'), errors='replace').read().splitlines()
    rows = []; cur = None
    for ln in s:
        m = re.search(r'\[PERF\] ([\d.]+) fps.*pso [\d.]+s \d+% (\d+) calls', ln)
        if m: cur = (float(m.group(1)), int(m.group(2)) / 20); continue
        if '[PERF6]' in ln and cur and cur[1] > 0:
            g = lambda k: float(re.search(k + r' ([\d.]+)', ln).group(1))
            busy = g('render_frame') + g('drain') + g('submit') + g('present')
            rows.append((cur[0], cur[1], busy, g('draws'), g('decode/other'), g('prepass'), g('record'), g('vtx')))
    tail = []; t = 0
    for r in reversed(rows):
        t += 20 / r[0]; tail.append(r)
        if t >= SECS: break
    if not tail: print(run, 'no [PERF6]'); continue
    for name, L in (('fight', tail), ('heaviest20%', sorted(tail)[:max(1, len(tail) // 5)])):
        n = len(L); d = sum(r[1] for r in L) / n; b = sum(r[2] for r in L) / n
        u = lambda i: 1000 * sum(r[i] for r in L) / n / d
        print('%-12s %-12s %4.1f batch/s  draws/batch %5.0f  busy %4.1f ms  %4.1f us/draw = draws %3.1f (vtx %3.1f) + decode %3.1f + prepass %3.1f + record %3.1f' % (
            os.path.basename(run.rstrip('/')), name, sum(r[0] for r in L) / n, d, b, 1000 * b / d, u(3), u(7), u(4), u(5), u(6)))
