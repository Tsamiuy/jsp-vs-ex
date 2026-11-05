"""Sum the shader/texture hitch counters of one run. usage: hitchsum.py <run_dir>...
Fight part = [PERF] windows after the last 100+ fps (loading screen) window."""
import re, sys, os
def num(p, s, d=0.0):
    m = re.search(p, s); return float(m.group(1)) if m else d
for run in sys.argv[1:]:
    lines = open(os.path.join(run, 'stderr.txt'), errors='replace').read().splitlines()
    perf = [i for i, l in enumerate(lines) if l.startswith('[PERF] ')]
    # fight start: the last window under 10 fps before the HUD; use the biggest-pso window as anchor
    # fight start: first window after the last loading-screen window (those run at 100+ fps)
    fast = [i for i in perf if num(r'\[PERF\] ([\d.]+) fps', lines[i]) > 100]
    best = next((i for i in perf if fast and i > fast[-1]), perf[0])
    fps = [num(r'\[PERF\] ([\d.]+) fps', lines[i]) for i in perf if i >= best]
    after = lambda tag: [l for l in lines[best:] if l.startswith(tag)]
    p5 = after('[PERF5]'); p3 = after('[PERF3] tex'); p1 = [lines[i] for i in perf if i >= best]
    fxc = sum(num(r'fxc \d+ ([\d.]+)s', l) for l in p5); fxcn = sum(num(r'fxc (\d+)', l) for l in p5)
    hits = sum(num(r'disk hits (\d+)', l) for l in p5)
    pc = sum(num(r'pso create \d+ ([\d.]+)s', l) for l in p5); pcn = sum(num(r'pso create (\d+)', l) for l in p5)
    skip = sum(num(r'skipped (\d+)', l) for l in p5); sib = sum(num(r'sibling (\d+)', l) for l in p5)
    pso = sum(num(r'pso ([\d.]+)s', l) for l in p1); miss = sum(num(r'(\d+) MISS', l) for l in p1)
    new = sum(num(r'new \d+ ([\d.]+)s', l) for l in p3); newn = sum(num(r'new (\d+)', l) for l in p3)
    cr = sum(num(r'created \d+ \(([\d.]+)s', l) for l in p3); crn = sum(num(r'created (\d+)', l) for l in p3)
    again = sum(num(r'again (\d+)', l) for l in p3)
    first = lines[best]
    print(f"{os.path.basename(run)}: start window {num(r'\[PERF\] ([\d.]+) fps', first):.2f} fps pso {num(r'pso ([\d.]+)s', first):.2f}s {num(r'(\d+) MISS', first):.0f} miss | "
          f"fight {len(fps)} win, avg {sum(fps)/max(1,len(fps)):.1f} fps, <20fps {sum(f<20 for f in fps)} win | pso {pso:.1f}s {miss:.0f} miss | "
          f"fxc {fxcn:.0f} {fxc:.1f}s, disk hits {hits:.0f}, pso create {pcn:.0f} {pc:.1f}s, skipped {skip:.0f} sibling {sib:.0f} | "
          f"tex new {newn:.0f} {new:.1f}s, created {crn:.0f} {cr:.1f}s, again {again:.0f}")
