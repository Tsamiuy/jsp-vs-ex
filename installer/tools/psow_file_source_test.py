#!/usr/bin/env python3
"""The 32-bit file-size path of the pipeline warm-up cache is gone (toolkit patch 2069).

    python3 installer/tools/psow_file_source_test.py --toolkit DIR [--patches installer/port/patches]

DIR is the patched toolkit (installer/tools/export_toolkit.py --out DIR). The backend and libs/video/psow_file.h must not call
ftell/fseek (a `long` is 32 bits on Windows, so a file of 2 GiB or more reads as negative) or keep a file size in a `long`.
Negative control: the same scan over the lines that patches 2038 and 2068 add (the code on the main branch before patch 2069)
must find them, so a scanner that matches nothing cannot pass. Standard library only."""
import argparse
import pathlib
import re
import sys

BAD = [
    (re.compile(r"\b(ftell|fseek)\s*\("), "ftell/fseek (32-bit offsets on Windows)"),
    (re.compile(r"\blong\s+(sz|size|good|pos)\b"), "a file size or offset held in a `long`"),
    (re.compile(r"%ld\b.*\b(good|sz|size)\b"), "a file size printed as %ld"),
]


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def scan(text):
    hits = []
    for n, line in enumerate(strip_comments(text).splitlines(), 1):
        for rx, why in BAD:
            if rx.search(line):
                hits.append((n, why, line.strip()[:100]))
    return hits


def added_lines(patch_text):
    return "\n".join(l[1:] for l in patch_text.splitlines() if l.startswith("+") and not l.startswith("+++"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--toolkit", required=True)
    ap.add_argument("--patches", default=str(pathlib.Path(__file__).resolve().parents[1] / "port" / "patches"))
    a = ap.parse_args()
    bad = 0
    # negative control: main's code before patch 2069
    old = ""
    for name in ("2038", "2068"):
        for p in sorted(pathlib.Path(a.patches).glob(name + "-*.patch")):
            old += added_lines(p.read_text(encoding="utf-8", errors="replace")) + "\n"
    ctl = scan(old)
    print(f"control (patches 2038 + 2068 as added): {len(ctl)} hits")
    if len(ctl) < 2:
        print("FAIL: the scanner does not find the 32-bit code of patches 2038 and 2068")
        bad += 1
    for rel in ("libs/video/rsx_d3d12_backend.c", "libs/video/psow_file.h"):
        p = pathlib.Path(a.toolkit) / rel
        if not p.exists():
            print(f"FAIL: {rel} missing")
            bad += 1
            continue
        hits = scan(p.read_text(encoding="utf-8", errors="replace"))
        for n, why, line in hits:
            print(f"FAIL {rel}:{n}: {why}: {line}")
        bad += len(hits)
        print(f"{rel}: {len(hits)} hits")
    text = (pathlib.Path(a.toolkit) / "libs/video/psow_file.h").read_text(encoding="utf-8", errors="replace")
    for need in ("_ftelli64", "_fseeki64", "ftello", "fseeko"):
        if need not in text:
            print(f"FAIL: psow_file.h does not use {need}")
            bad += 1
    print("psow_file_source_test: " + ("FAILED" if bad else "all passed"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
