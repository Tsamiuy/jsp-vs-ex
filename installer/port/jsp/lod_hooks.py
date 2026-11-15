#!/usr/bin/env python3
"""Verify emitted LOD entry wrappers before enabling their runtime callbacks.

Only hashes, addresses and authored hook names are persisted. No generated code
is copied into the receipt. This is build wiring evidence, not an effect ACK.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

RECEIPT = "jsp_lod_hooks.json"
HEADER = "jsp_lod_hooks_build.h"


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for part in iter(lambda: f.read(1 << 20), b""):
            h.update(part)
    return h.hexdigest()


def segment_hash(data):
    # Same loadable-segment identity as firstrun.segment_hash (including header
    # fields); alternate file layouts can identify the same supported program.
    if len(data) < 64 or data[:6] != b"\x7fELF\x02\x02":
        return None
    offset = struct.unpack_from(">Q", data, 32)[0]
    size, count = struct.unpack_from(">HH", data, 54)
    if size != 56 or count > 64 or offset + size * count > len(data):
        return None
    h = hashlib.sha256()
    for i in range(count):
        kind, flags, at, va, pa, n, mem, align = struct.unpack_from(">IIQQQQQQ", data, offset + i * size)
        if kind != 1 or not (n or mem):
            continue
        if at + n > len(data):
            return None
        h.update(struct.pack(">QQQ", va, n, mem))
        h.update(data[at:at + n])
    return h.hexdigest()


def manifest(path):
    spec = json.loads(Path(path).read_text())
    if spec.get("schema") != 1 or spec.get("program", {}).get("region") != "US":
        raise ValueError("invalid LOD hook program manifest")
    if any(not re.fullmatch(r"[0-9a-f]{64}", str(spec["program"].get(key, "")))
           for key in ("sha256", "segment_sha256")):
        raise ValueError("invalid LOD program digest")
    expected = ((0x3955B4, 0x395630, "jsp_lod_initialize_enter"),
                (0x39595C, 0x395968, "jsp_lod_retire_enter"),
                (0x3959C8, 0x395A80, "jsp_lod_select_enter"))
    entries = spec.get("hooks", [])
    actual = [(int(e["addr"], 0), int(e["end"], 0), e["enter"]) for e in entries]
    if actual != list(expected) or any(e["leave"] != "jsp_lod_hook_leave" for e in entries):
        raise ValueError("unexpected LOD entry/range/callback manifest")
    return spec


def plan(manifest_path, image, region, lifter):
    spec = manifest(manifest_path)
    data = Path(image).read_bytes()
    whole = hashlib.sha256(data).hexdigest()
    segments = segment_hash(data)
    supported = spec["program"]
    enabled = region == "US" and (whole == supported["sha256"] or
                                  bool(segments and segments == supported["segment_sha256"]))
    return {"schema": 1, "enabled": enabled, "region": region,
            "program_sha256": whole, "program_segment_sha256": segments,
            "manifest_sha256": sha(manifest_path), "lifter_sha256": sha(lifter),
            "verifier_sha256": sha(__file__)}


def source_files(out):
    return sorted({*Path(out).glob("ppu_recomp*.cpp"), *Path(out).glob("ppu_recomp*.c"),
                   *Path(out).glob("ppu_recomp*.h")})


def check_emission(out, spec, enabled):
    files = source_files(out)
    if not files or not (Path(out) / "ppu_recomp.h").is_file():
        raise ValueError("missing PPU lift output")
    text = "\n".join(p.read_text() for p in files)
    if not enabled:
        if any(e["enter"] in text or e["leave"] in text for e in spec["hooks"]):
            raise ValueError("unsupported program has stale LOD wrappers")
        return
    for e in spec["hooks"]:
        address, end = int(e["addr"], 0), int(e["end"], 0)
        name = f"func_{address:08X}"
        bodies = re.findall(r"\bvoid\s+(func_([0-9A-Fa-f]{8}))\s*\(ppu_context\* ctx\)\s*\{", text)
        starts = [int(x[1], 16) for x in bodies]
        if starts.count(address) != 1 or any(address < a < end for a in starts):
            raise ValueError(f"missing/duplicate/interior LOD entry {address:08X}")
        wrapper = list(re.finditer(r"\bvoid\s+" + name + r"\(ppu_context\* ctx\)\s*\{([^{}]*)\}", text))
        want = f"{e['enter']}(ctx);{name}__body(ctx);{e['leave']}(ctx);"
        if len(wrapper) != 1 or re.sub(r"\s+", "", wrapper[0].group(1)) != want:
            raise ValueError(f"incomplete LOD wrapper {address:08X}")
        if len(re.findall(r"\b" + name + r"__body\b", text)) != 2:
            raise ValueError(f"LOD body can bypass entry {address:08X}")
        if not re.search(r"static void " + name + r"__body\(ppu_context\* ctx\)\s*\{", text):
            raise ValueError(f"missing private LOD body {address:08X}")
        marker = f"/* fn-hooks 0x{address:08X}..0x{end:08X} {e['enter']} {e['leave']} */"
        if text.count(marker) != 1:
            raise ValueError(f"missing/changed final LOD range {address:08X}")
        opening = text.index("{", text.index(f"static void {name}__body(ppu_context* ctx)"))
        # The emitter places the public wrapper immediately after its private
        # body. Bound by that unique definition instead of parsing braces in
        # comments/string literals (which could hide an early continuation).
        if wrapper[0].start() <= opening or "g_trampoline_fn" in text[opening:wrapper[0].start()]:
            raise ValueError(f"LOD body has a tail/continuation {address:08X}")
        table = r"\{\s*0x0*" + f"{address:X}" + r"ULL,\s*" + name + r',\s*"' + name + r'"\s*\}'
        if len(re.findall(table, text, re.I)) != 1:
            raise ValueError(f"missing/duplicate LOD dispatch entry {address:08X}")


def stamp(out, manifest_path, image, region, lifter):
    witness = plan(manifest_path, image, region, lifter)
    check_emission(out, manifest(manifest_path), witness["enabled"])
    witness["sources"] = {p.name: sha(p) for p in source_files(out)}
    (Path(out) / RECEIPT).write_text(json.dumps(witness, sort_keys=True) + "\n")
    return witness


def verify(out, manifest_path, image, region, lifter):
    receipt = Path(out) / RECEIPT
    if image is None:
        # Legacy/manual vanilla lifts retain their old indirect-only behavior.
        # A wrapped lift without its input identity may never become active.
        if receipt.exists():
            raise ValueError("LOD receipt requires the actual input image")
        check_emission(out, manifest(manifest_path), False)
        return False
    expected = plan(manifest_path, image, region, lifter)
    if not receipt.is_file():
        raise ValueError("missing LOD lift receipt; rerun lift")
    witness = json.loads(receipt.read_text())
    if any(witness.get(k) != v for k, v in expected.items()):
        raise ValueError("stale LOD program/config/tool receipt; rerun lift")
    sources = {p.name: sha(p) for p in source_files(out)}
    if witness.get("sources") != sources:
        raise ValueError("LOD lift output changed after verification; rerun lift")
    check_emission(out, manifest(manifest_path), expected["enabled"])
    return expected["enabled"]


def write_header(path, enabled, spec=None):
    text = "// Generated only after verifying actual emitted LOD wrappers.\n#pragma once\n"
    text += f"#define JSP_LOD_ENTRY_HOOKS_VERIFIED {int(enabled)}\n"
    program = (spec or {}).get("program", {})
    text += '#define JSP_LOD_PROGRAM_SHA256 "' + program.get("sha256", "") + '"\n'
    text += '#define JSP_LOD_PROGRAM_SEGMENT_SHA256 "' + program.get("segment_sha256", "") + '"\n'
    path = Path(path)
    if not path.is_file() or path.read_text() != text:
        path.write_text(text)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--manifest", required=True, type=Path)
    ap.add_argument("--lifter", required=True, type=Path)
    ap.add_argument("--image", type=Path)
    ap.add_argument("--region", default="")
    ap.add_argument("--header", required=True, type=Path)
    args = ap.parse_args()
    try:
        enabled = verify(args.out, args.manifest, args.image, args.region, args.lifter)
        write_header(args.header, enabled, manifest(args.manifest))
    except (OSError, ValueError, KeyError, struct.error) as e:
        # A failed verification must also revoke an earlier configure's header.
        write_header(args.header, False)
        ap.exit(1, f"LOD hooks unavailable: {e}\n")


if __name__ == "__main__":
    main()
