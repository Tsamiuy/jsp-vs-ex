"""Symbolize the 'tid N in exe rva=0x...' lines of a set of `threads` dumps and count, per host
thread, which functions it was caught in. Usage: symdumps.py <dump dir> <exe> <out.txt>"""
import glob, os, re, subprocess, sys
from collections import Counter, defaultdict

d, exe, out = sys.argv[1], sys.argv[2], sys.argv[3]
SYM = r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\llvm-symbolizer.exe"
pat = re.compile(r"tid (\d+) in exe rva=(0x[0-9A-Fa-f]+)")
any_pat = re.compile(r"^\[dbg\]\s+tid (\d+) in (\S+)")
samples = []          # (tid, rva)
seen = Counter()      # tid -> dumps it appeared in (any location)
for f in sorted(glob.glob(os.path.join(d, "threads_*.txt"))):
    for line in open(f, encoding="utf-8", errors="replace"):
        m = any_pat.search(line)
        if m: seen[m.group(1)] += 1
        m = pat.search(line)
        if m: samples.append((m.group(1), int(m.group(2), 16)))
addrs = sorted({r for _, r in samples})
inp = "\n".join(hex(0x140000000 + r) for r in addrs) + "\n"
res = subprocess.run([SYM, "--obj=" + exe, "--functions=linkage", "--no-inlines"],
                     input=inp, capture_output=True, text=True).stdout
# llvm-symbolizer prints 2 lines + a blank per address
blocks = [b for b in res.strip().split("\n\n")]
name = {}
for r, b in zip(addrs, blocks):
    ls = b.strip().splitlines()
    fn = ls[0] if ls else "?"
    loc = ls[1].split("\\")[-1] if len(ls) > 1 else ""
    name[r] = f"{fn.split('(')[0]}  [{loc}]"
per = defaultdict(Counter)
for tid, r in samples: per[tid][name[r]] += 1
with open(out, "w") as o:
    for tid, c in sorted(per.items(), key=lambda kv: -sum(kv[1].values())):
        tot = sum(c.values())
        o.write(f"tid {tid}: {tot} samples in exe (seen in {seen[tid]} dumps)\n")
        for fn, k in c.most_common(12):
            o.write(f"   {k:4d} {100*k/tot:5.1f}%  {fn}\n")
print(open(out).read()[:6000])
