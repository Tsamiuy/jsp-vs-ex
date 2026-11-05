"""Fit drawing-thread busy ms per batch = fixed + us/draw * draws over the fight windows ([PERF]+[PERF6], PERF=1).
usage: drawfit.py <run_dir>...  Runs named <setting><n> (on1, off2...) are also pooled per setting.
Compares settings at the same draw count (busy@2000/@3000), which removes the camera effect."""
import re, sys, os
SECS = int(os.environ.get('FIGHT_SECS', '70'))
def load(run):
    s = open(os.path.join(run, 'stderr.txt'), errors='replace').read().splitlines()
    rows = []; cur = None
    for ln in s:
        m = re.search(r'\[PERF\] ([\d.]+) fps.*pso [\d.]+s \d+% (\d+) calls', ln)
        if m: cur = (float(m.group(1)), int(m.group(2)) / 20); continue
        if '[PERF6]' in ln and cur and cur[1] > 0:
            g = lambda k: float(re.search(k + r' ([\d.]+)', ln).group(1))
            rows.append((cur[0], cur[1], g('render_frame') + g('drain') + g('submit') + g('present')))
    tail = []; t = 0
    for r in reversed(rows):
        t += 20 / r[0]; tail.append(r)
        if t >= SECS: break
    return tail
def fit(L):
    n = len(L); mx = sum(r[1] for r in L) / n; my = sum(r[2] for r in L) / n
    b = sum((r[1] - mx) * (r[2] - my) for r in L) / sum((r[1] - mx) ** 2 for r in L)
    return my - b * mx, b * 1000, mx, my
groups = {}
for run in sys.argv[1:]:
    L = load(run); a, b, mx, my = fit(L)
    at2k = (a + b / 1000 * 2000)
    print('%-6s n=%3d draws %5.0f busy %4.1f  fit: %4.1f ms + %4.1f us/draw  -> busy@2000 draws %4.1f ms  @3000 %4.1f' % (os.path.basename(run), len(L), mx, my, a, b, at2k, a + b * 3))
    groups.setdefault(re.sub(r'\d+$', '', os.path.basename(run)), []).extend(L)
for k, L in groups.items():
    a, b, mx, my = fit(L)
    print('POOLED %-4s n=%3d fit %4.1f ms + %4.1f us/draw -> @2000 %4.1f  @3000 %4.1f ms' % (k, len(L), a, b, a + b * 2, a + b * 3))
