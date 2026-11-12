"""Real frame rate from GCM_FLIPCOUNT=1 [flip] lines, fight part only.
usage: flipsum.py <run_dir>...  (menu_tour_play runs: fight = last PLAY_SECS+~5 s of the flip clock)
Prints avg flips/s, 5 s window min, 10-flip group stats (p50/p95/max ms, groups > 600 ms = stutter),
batches per flip ([PERF] batch fps / flips), process CPU avg from thread_cpu.txt."""
import re, sys, os, bisect, statistics
SECS = int(os.environ.get('FIGHT_SECS', '120'))
for run in sys.argv[1:]:
    s = open(os.path.join(run, 'stderr.txt'), errors='replace').read()
    f = [(int(a), int(b)) for a, b in re.findall(r'\[flip\] (\d+) at (\d+) ms', s)]
    if len(f) < 50: print(run, 'no flips'); continue
    end = f[-1][1]; st = end - SECS * 1000
    ts = [t for _, t in f]; ns = [n for n, _ in f]
    n_at = lambda t: ns[max(0, bisect.bisect_right(ts, t) - 1)]
    avg = (n_at(end) - n_at(st)) / SECS
    win = [(n_at(st + k * 1000 + 5000) - n_at(st + k * 1000)) / 5 for k in range(0, SECS - 4, 5)]
    w = [x for x in f if x[1] >= st]
    g = sorted((t2 - t1) * 10 / (n2 - n1) for (n1, t1), (n2, t2) in zip(w, w[1:]) if n2 > n1)
    p = lambda q: g[min(len(g) - 1, int(q * len(g)))]
    # batch fps over the same span: [PERF] lines come every 20 batches; use the last ones covering SECS
    perf = [float(x) for x in re.findall(r'\[PERF\] ([\d.]+) fps', s)]
    bt = 0.0; tail = []
    for x in reversed(perf):
        if x <= 0: continue
        bt += 20 / x; tail.append(x)
        if bt >= SECS: break
    bpf = (20 * len(tail) / bt) / avg if bt and avg else 0
    cpu = ''
    tc = os.path.join(run, 'thread_cpu.txt')
    if os.path.exists(tc):
        tot = [int(m.replace(',', '')) for m in re.findall(r'process total ([\d,]+)%', open(tc).read())]
        if tot: cpu = 'cpu %d%%' % (sum(tot) / len(tot))
    print('%-14s %5.1f f/s  min5s %4.1f  10f p50 %4d p95 %4d max %5d ms  >600ms %3d  b/f %.2f  %s' % (
        os.path.basename(run.rstrip('/')), avg, min(win), p(.5), p(.95), g[-1], sum(1 for x in g if x > 600), bpf, cpu))
