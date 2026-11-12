"""Parser for the D3D12 backend's RSX_CAP manifest.txt (shared by the capture tools).

Format (as written by rsx_d3d12_backend.c on 2026-10-20):
    frame_count=5690 draws=1452 backbuffer=1280x720
    offrt[6] off=0x0ED10000 1280x720 dxgi=28
    --- ops ---
    op000 CLEAR rt=0x0ED10000 mrt=0x0/0x0/0x0 cc=(0.000,0.000,0.000,0.000)
    op001 DRAW  rt=0x0ED10000 mrt=... vp=0,0,1280x720 fp=0x0FBFFFC1 vs=18 n=66 cmask=F blend=0 bk=0x0 t0=... t3=...
          t0 off=0xC0DC7300 fmt=0x86 (SZ) 512x512 remap=0xAAE4
    --- vp constants (first offscreen geometry draws) ---
       c[466] = ...
Addresses are returned as 8-digit upper-case hex strings without 0x.
"""
import re

_KV = re.compile(r"(\w+)=(\S+)")
_OP = re.compile(r"^op(\d+)\s+(\w+)\s+(.*)$")
_TEX = re.compile(r"^\s+t(\d+) off=0x([0-9A-Fa-f]+) fmt=0x([0-9A-Fa-f]+) \((\w+)\) (\d+)x(\d+)")
_RT = re.compile(r"^offrt\[(\d+)\] off=0x([0-9A-Fa-f]+) (\d+)x(\d+)")


def _hex(v):
    return "%08X" % int(v, 16)


def load_manifest(path):
    header, rts, ops = {}, {}, []
    section = "head"
    with open(path, "r", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("--- ops"):
                section = "ops"
                continue
            if line.startswith("---"):
                section = "tail"
                continue
            if section == "head":
                m = _RT.match(line)
                if m:
                    rts[_hex(m.group(2))] = {"slot": int(m.group(1)), "w": int(m.group(3)),
                                             "h": int(m.group(4))}
                else:
                    header.update(_KV.findall(line))
            elif section == "ops":
                m = _OP.match(line)
                if m:
                    kv = dict(_KV.findall(m.group(3)))
                    op = {"idx": int(m.group(1)), "kind": m.group(2), "raw": line,
                          "rt": _hex(kv.get("rt", "0")), "kv": kv, "tex": {}, "texinfo": {}}
                    for k, v in kv.items():
                        if re.fullmatch(r"t\d+", k):
                            op["tex"][k] = _hex(v)
                    op["n"] = int(kv.get("n", "0"))
                    op["vp"] = kv.get("vp", "")
                    ops.append(op)
                    continue
                m = _TEX.match(line)
                if m and ops:
                    ops[-1]["texinfo"]["t" + m.group(1)] = {
                        "off": _hex(m.group(2)), "fmt": int(m.group(3), 16), "layout": m.group(4),
                        "w": int(m.group(5)), "h": int(m.group(6))}
    return {"header": header, "rts": rts, "ops": ops}


def is_fullscreen_pass(op):
    """Six vertices = one quad as two triangles: the post-process passes."""
    return op["kind"] == "DRAW" and op["n"] == 6
