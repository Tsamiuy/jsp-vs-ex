#!/usr/bin/env python3
"""Filter and follow the ops in one RSX_CAP frame (manifest.txt).

usage: python3 capops.py <frame dir | manifest.txt> [filters]

filters (combine freely):
  --rt=0ED10000        ops that write this target
  --tex=0E590000       ops that sample this texture (any unit)
  --touch=0ED10000     ops that write OR sample it
  --fp=0FC7E981        ops using this fragment program
  --from=1380 --to=1451  op index range
  --post               only fullscreen passes (n=6 draws) and clears
  --summary            one line per target: first/last write, #draws, #clears, who reads it
  --chain=0C6ABF40     walk back from a target: which ops wrote it, what they
                       sampled, and recursively who wrote those (within the frame)
  --details            also print the t0..tN detail lines

Addresses may be given with or without 0x. Read-only.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from capmanifest import load_manifest, is_fullscreen_pass  # noqa: E402


def norm(a):
    return "%08X" % int(a, 16) if a else None


def short(op):
    kv = op["kv"]
    if op["kind"] == "CLEAR":
        return f"op{op['idx']:04d} CLEAR {op['rt']} cc={kv.get('cc', '')}"
    tex = " ".join(f"{k}={v}" for k, v in sorted(op["tex"].items()))
    return (f"op{op['idx']:04d} DRAW  {op['rt']} n={op['n']:<5} fp={kv.get('fp', '')} "
            f"vs={kv.get('vs', '')} blend={kv.get('blend', '')} cmask={kv.get('cmask', '')} "
            f"vp={op['vp']} {tex}")


def summary(man):
    info = {}
    for op in man["ops"]:
        d = info.setdefault(op["rt"], {"first": op["idx"], "last": op["idx"], "draws": 0,
                                       "clears": 0, "readers": []})
        d["last"] = op["idx"]
        d["draws" if op["kind"] == "DRAW" else "clears"] += 1
    for op in man["ops"]:
        for t in set(op["tex"].values()):
            if t in info:
                info[t]["readers"].append(op["idx"])
    print(f"{'target':<10} {'size':>10} {'first':>6} {'last':>6} {'draws':>6} {'clears':>6}  readers")
    for rt, d in sorted(info.items(), key=lambda kv: kv[1]["first"]):
        size = man["rts"].get(rt)
        size = f"{size['w']}x{size['h']}" if size else ("display" if rt == "00000000" else "untracked")
        r = d["readers"]
        rs = ", ".join(str(x) for x in r[:8]) + (f" ... (+{len(r) - 8})" if len(r) > 8 else "")
        print(f"{rt:<10} {size:>10} {d['first']:>6} {d['last']:>6} {d['draws']:>6} {d['clears']:>6}  {rs}")


def chain(man, target, before=None, depth=0, seen=None):
    """Print the ops that produced `target` before op index `before`, recursively.
    Each op is expanded once; later mentions print as "opNNNN (above)"."""
    seen = seen if seen is not None else set()
    pad = "  " * depth
    ops = [op for op in man["ops"] if op["rt"] == target and (before is None or op["idx"] < before)]
    if not ops:
        print(pad + f"{target}: no writes in this frame before op{before}"
              + (" (sampled from an earlier frame or never drawn)" if before else ""))
        return
    # Only the fullscreen passes and clears matter for a post chain; geometry is summarized.
    geo = [op for op in ops if op["kind"] == "DRAW" and not is_fullscreen_pass(op)]
    if geo:
        print(pad + f"{target}: {len(geo)} geometry draws (op{geo[0]['idx']}..op{geo[-1]['idx']})")
    for op in ops:
        if op in geo:
            continue
        if op["idx"] in seen:
            print(pad + f"op{op['idx']:04d} (above)")
            continue
        seen.add(op["idx"])
        print(pad + short(op))
        if op["kind"] != "DRAW":
            continue
        for t in sorted(set(op["tex"].values())):
            if t == target:
                continue
            if t in man["rts"] or any(o["rt"] == t for o in man["ops"]):
                chain(man, t, op["idx"], depth + 1, seen)


def main(argv):
    if not argv or argv[0].startswith("--"):
        print(__doc__)
        return 1
    path = argv[0]
    if os.path.isdir(path):
        path = os.path.join(path, "manifest.txt")
    opts = {}
    for a in argv[1:]:
        m = re.match(r"--(\w+)(?:=(.*))?$", a)
        if m:
            opts[m.group(1)] = m.group(2) if m.group(2) is not None else True
    man = load_manifest(path)
    print(f"# {path}: frame_count={man['header'].get('frame_count', '?')} ops={len(man['ops'])}")
    if "summary" in opts:
        summary(man)
        return 0
    if "chain" in opts:
        chain(man, norm(opts["chain"]))
        return 0
    rt, tex, touch, fp = (norm(opts.get(k)) if isinstance(opts.get(k), str) else None
                          for k in ("rt", "tex", "touch", "fp"))
    lo = int(opts.get("from", 0) or 0)
    hi = int(opts.get("to", 1 << 30) or (1 << 30))
    for op in man["ops"]:
        if not lo <= op["idx"] <= hi:
            continue
        if rt and op["rt"] != rt:
            continue
        if tex and tex not in op["tex"].values():
            continue
        if touch and op["rt"] != touch and touch not in op["tex"].values():
            continue
        if fp and norm(op["kv"].get("fp", "0")) != fp:
            continue
        if "post" in opts and not (op["kind"] == "CLEAR" or is_fullscreen_pass(op)):
            continue
        print(short(op))
        if "details" in opts:
            for t, ti in sorted(op["texinfo"].items()):
                print(f"      {t} off={ti['off']} fmt=0x{ti['fmt']:02X} ({ti['layout']}) {ti['w']}x{ti['h']}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
