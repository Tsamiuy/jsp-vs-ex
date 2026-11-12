"""Summarise a run's speed from stderr: batches/s from [PERF], guest frames/s from the
[FRAMELOG] flip counter, and the [PERF]/[PERF2] split, for windows after a start batch.

usage: python3 perfbase.py <stderr.txt> [first_batch]   (default 5800: demo intro cards)

A "batch" is one render_frame (the mid-frame flush can split a guest frame into several);
"guest fps" counts cellGcmSetFlip requests, i.e. frames the game itself finished.
"""
import re, sys

path = sys.argv[1]
first = int(sys.argv[2]) if len(sys.argv) > 2 else 5800
txt = open(path, encoding='utf-8', errors='replace').read().splitlines()

fl = []   # (batch, flips, verts, present)
perf = []
perf2 = []
batch_at_perf = []
last_batch = 0
for line in txt:
    m = re.search(r'\[FRAMELOG\] batch (\d+):.*vp_verts=(\d+)/\d+.*present=(\d).*flips=(\d+)', line)
    if m:
        last_batch = int(m.group(1))
        fl.append((last_batch, int(m.group(4)), int(m.group(2)), int(m.group(3))))
        continue
    m = re.search(r'\[PERF\] ([\d.]+) fps \| tex ([\d.]+)s .*?\| vtx ([\d.]+)s \((\d+)%.*?(\d+) cyc/vert\) \| render_frame ([\d.]+)s \((\d+)%\) \| prepass ([\d.]+)s .*\| guest ([\d.]+)s \((\d+)%\)', line)
    if m:
        perf.append((last_batch, float(m.group(1)), float(m.group(3)), int(m.group(4)), int(m.group(5)),
                     float(m.group(6)), int(m.group(7)), float(m.group(9)), int(m.group(10))))
        continue
    m = re.search(r'\[PERF2\] drain ([\d.]+)s \((\d+)%.*?of which vtx ([\d.]+)s, mid-frame render ([\d.]+)s \| present ([\d.]+)s \((\d+)%\) \| rest ([\d.]+)s \((\d+)%\)', line)
    if m:
        perf2.append((last_batch, int(m.group(2)), float(m.group(3)), float(m.group(4)), int(m.group(6)), int(m.group(8))))

P = [p for p in perf if p[0] >= first]
P2 = [p for p in perf2 if p[0] >= first]
F = [f for f in fl if f[0] >= first]
if not P:
    print("no [PERF] windows after batch", first); sys.exit(1)

def mean(xs): return sum(xs) / len(xs) if xs else 0.0
bps = [p[1] for p in P]
print(f"windows after batch {first}: {len(P)}")
print(f"batches/s   mean {mean(bps):.2f}  min {min(bps):.2f}  max {max(bps):.2f}")
if len(F) >= 2:
    db = F[-1][0] - F[0][0]; df = F[-1][1] - F[0][1]
    fpb = df / db if db else 0
    print(f"guest frames per batch {fpb:.3f}  ->  guest fps ~{fpb * mean(bps):.2f}"
          f"  (frame time ~{1000.0 / (fpb * mean(bps)) if fpb else 0:.0f} ms)")
    print(f"vp verts per logged batch: mean {mean([f[2] for f in F]):.0f}")
print(f"[PERF] vtx {mean([p[3] for p in P]):.0f}%  cyc/vert {mean([p[4] for p in P]):.0f}  "
      f"render_frame {mean([p[6] for p in P]):.0f}%  guest {mean([p[8] for p in P]):.0f}%")
if P2:
    print(f"[PERF2] drain {mean([p[1] for p in P2]):.0f}%  present {mean([p[4] for p in P2]):.0f}%  "
          f"rest {mean([p[5] for p in P2]):.0f}%")
