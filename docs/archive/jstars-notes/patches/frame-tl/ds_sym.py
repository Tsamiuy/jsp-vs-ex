"""Name the DRAW_SAMPLE=1 output (draw_samples.txt) against the exe that wrote it.

usage: ds_sym.py <draw_samples.txt> <exe with pdb> [first_window [last_window]]

Pools the chosen windows and prints:
  - where the drawing thread was: exe / ntdll (kernel calls and ntdll code) /
    ntwait (blocked in a wait stub; under-counted, see the README) / d3d12 / nvidia;
  - the exe functions charged (e1 = first exe frame on the stack), overall and
    for each non-exe class, so ntdll and driver time shows its exe caller;
  - the top caller pairs (e1 <- e2).
"""
import re, subprocess, sys
from collections import Counter, defaultdict

SYM = r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\llvm-symbolizer.exe"
path, exe = sys.argv[1], sys.argv[2]
w0 = int(sys.argv[3]) if len(sys.argv) > 3 else 0
w1 = int(sys.argv[4]) if len(sys.argv) > 4 else 10**9

rows, win = [], -1
for line in open(path, encoding="utf-8", errors="replace"):
    m = re.match(r"\[DS\] window (\d+):", line)
    if m:
        win = int(m.group(1)); continue
    if line.startswith("[DS]"):
        win = -1; continue   # a new run's start line
    p = line.split()
    if len(p) == 5 and w0 <= win <= w1:
        rows.append((int(p[0]), p[1], p[2], int(p[3], 16), int(p[4], 16)))
if not rows:
    sys.exit("no samples in those windows")

addrs = sorted({r[3] for r in rows} | {r[4] for r in rows} - {0})
out = subprocess.run([SYM, "--obj=" + exe, "--functions=linkage", "--no-inlines"],
                     input="".join(hex(0x140000000 + a) + "\n" for a in addrs),
                     capture_output=True, text=True).stdout
name = {0: "-"}
for a, b in zip(addrs, out.strip().split("\n\n")):
    ls = b.strip().splitlines()
    fn = ls[0].split("(")[0] if ls else "?"
    name[a] = fn if fn not in ("??", "") else hex(a)

tot = sum(r[0] for r in rows)
cls = Counter()
f_all, f_cls, pairs = Counter(), defaultdict(Counter), Counter()
for n, c, ran, e1, e2 in rows:
    cls[c] += n
    f_all[name[e1]] += n
    f_cls[c][name[e1]] += n
    pairs[(name[e1], name[e2])] += n

print(f"{tot} samples")
print("  " + " | ".join(f"{c} {100*k/tot:.1f}%" for c, k in cls.most_common()))
print("\nexe function charged (first exe frame), all classes:")
for fn, k in f_all.most_common(25):
    print(f"  {100*k/tot:5.1f}%  {fn}")
for c in ("ntdll", "ntwait", "d3d12", "nvidia", "other"):
    if not f_cls[c]:
        continue
    print(f"\n{c} samples ({100*cls[c]/tot:.1f}%), by exe caller:")
    for fn, k in f_cls[c].most_common(12):
        print(f"  {100*k/tot:5.1f}%  {fn}")
print("\ntop caller pairs (e1 <- e2):")
for (a, b), k in pairs.most_common(25):
    print(f"  {100*k/tot:5.1f}%  {a} <- {b}")
