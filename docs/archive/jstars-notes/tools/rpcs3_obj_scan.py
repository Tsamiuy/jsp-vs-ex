"""Read-only: find the particle objects that own the beam records found by rpcs3_beam_scan.py.

Run while RPCS3 is still paused on the same Kamehameha frame:
    python docs\archive\jstars-notes\\tools\\rpcs3_obj_scan.py

In our port each particle object holds three block pointers: +0xB0 (0x160 bytes),
+0xB4 (the 0x130 record the beam job reads) and +0xB8 (0x80 bytes). This script
reads the record addresses from reference\\rpcs3_beam_scan.txt, looks for 4-byte
big-endian values equal to them in guest memory, treats each hit at +0xB4 of an
object, and dumps the object (0x100 bytes) and its +0xB0 and +0xB8 blocks.
Output: docs\archive\jstars-notes\\reference\\rpcs3_obj_scan.txt
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rpcs3_beam_scan as s  # noqa: E402  (same read-only helpers)

MAX_OBJ = 8


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ref = os.path.join(os.path.dirname(here), "reference")
    src = os.path.join(ref, "rpcs3_beam_scan.txt")
    if not os.path.exists(src):
        print("Run rpcs3_beam_scan.py first.")
        return 1
    recs = []
    for line in open(src, encoding="utf-8"):
        m = re.match(r"\s+0x[0-9A-F]+\s+guest=0x([0-9A-F]{8}).*score=4", line)
        if m:
            g = int(m.group(1), 16)
            if g not in recs:
                recs.append(g)
    recs = recs[:40]
    if not recs:
        print("No good record candidates in rpcs3_beam_scan.txt.")
        return 1
    pid = s.find_pid()
    if not pid:
        print("rpcs3.exe is not running.")
        return 1
    h = s.k32.OpenProcess(s.PROCESS_QUERY_INFORMATION | s.PROCESS_VM_READ, False, pid)
    if not h:
        print("Could not open rpcs3.exe for reading.")
        return 1
    # guest base: take the candidates the record scan found and keep the one whose guest 0x30000000
    # holds the game's file table ("STPK"); guest page 0 itself is unmapped, so don't test that.
    cands = []
    for line in open(src, encoding="utf-8"):
        if line.startswith("guest base candidates"):
            cands = [int(x, 16) for x in re.findall(r"0x([0-9A-F]+)", line.split(":", 1)[1])]
    base = None
    for b in cands + [0x300000000]:
        head = s.read(h, b + 0x30000000, 0x100) or b""
        if b"STPK" in head:
            base = b
            break
    if base is None:
        print("Could not confirm the guest base; rerun rpcs3_beam_scan.py with RPCS3 paused, then this script.")
        return 1
    print("Scanning guest memory for pointers to %d records (read-only)..." % len(recs))

    want = {struct.pack(">I", g): g for g in recs}
    hits = []  # (guest addr of the pointer, record)
    lo, hi = base + 0x30000000, base + 0x40000000
    for rbase, rsize in s.regions(h):
        a0, a1 = max(rbase, lo), min(rbase + rsize, hi)
        off = a0
        while off < a1 and len(hits) < 200:
            n = min(s.CHUNK, a1 - off)
            data = s.read(h, off, n)
            if data:
                for pat, g in want.items():
                    k = data.find(pat)
                    while k != -1:
                        if k % 4 == 0:
                            hits.append((off + k - base, g))
                        k = data.find(pat, k + 1)
            off += n

    out = ["rpcs3_obj_scan: %d records, %d pointers found" % (len(recs), len(hits))]

    def rd(guest, size):
        return s.read(h, base + guest, size) or b""

    shown = 0
    for p, rec in hits:
        obj = p - 0xB4
        hdr = rd(obj, 0x100)
        if len(hdr) < 0xC0:
            continue
        b0, b4, b8 = s.be32(hdr, 0xB0), s.be32(hdr, 0xB4), s.be32(hdr, 0xB8)
        if b4 != rec or not (0x30000000 <= b0 < 0x40000000 and 0x30000000 <= b8 < 0x40000000):
            out.append("pointer to record 0x%08X at 0x%08X (not an object layout: B0=%08X B8=%08X)" % (rec, p, b0, b8))
            continue
        out.append("")
        out.append("== object 0x%08X  B0=0x%08X  B4(record)=0x%08X  B8=0x%08X" % (obj, b0, b4, b8))
        out += s.hexrows(hdr, "obj")
        out += s.hexrows(rd(b0, 0x160), "B0 ")
        out += s.hexrows(rd(b8, 0x80), "B8 ")
        shown += 1
        if shown >= MAX_OBJ:
            break
    s.k32.CloseHandle(h)
    dst = os.path.join(ref, "rpcs3_obj_scan.txt")
    with open(dst, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(out[0])
    print("Done. Full results: %s" % dst)
    return 0


if __name__ == "__main__":
    sys.exit(main())
