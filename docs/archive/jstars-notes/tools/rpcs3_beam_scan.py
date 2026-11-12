"""Read-only scan of a running RPCS3 for J-Stars beam particle records.

Run while RPCS3 is paused on Goku's Kamehameha:
    python docs\archive\jstars-notes\\tools\\rpcs3_beam_scan.py

It opens rpcs3.exe with query + read access only (no write, no debugger attach),
walks its readable memory and looks for the particle record layout we know from
our port (docs/archive/jstars-notes VFX.md):
  record size 0x130, colour block at +0x80 = 43000000 43000000 43000000 (128,128,128)
  +0x124 = link to the next record (a guest pointer)
It also finds the guest memory base from the game's file table ("STPK" at guest
0x30000000, followed by "game_resident_parameter_m.pak").

Output: docs\archive\jstars-notes\\reference\\rpcs3_beam_scan.txt
"""
import ctypes
import ctypes.wintypes as wt
import os
import struct
import sys

PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
MEM_COMMIT = 0x1000
PAGE_NOACCESS = 0x01
PAGE_GUARD = 0x100

COLOUR = bytes.fromhex("430000004300000043000000")
REC = 0x130
CHUNK = 16 << 20
MAX_CAND = 5000
MAX_DUMP = 10


class MBI(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p),
                ("AllocationProtect", wt.DWORD), ("PartitionId", wt.WORD),
                ("RegionSize", ctypes.c_size_t), ("State", wt.DWORD),
                ("Protect", wt.DWORD), ("Type", wt.DWORD)]


k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = wt.HANDLE
k32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k32.VirtualQueryEx.restype = ctypes.c_size_t
k32.VirtualQueryEx.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.POINTER(MBI), ctypes.c_size_t]
k32.ReadProcessMemory.restype = wt.BOOL
k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                  ctypes.POINTER(ctypes.c_size_t)]
k32.CloseHandle.argtypes = [wt.HANDLE]


def find_pid(name="rpcs3.exe"):
    import subprocess
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq " + name, "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if parts and parts[0].lower() == name and len(parts) > 1:
            return int(parts[1].strip('"'))
    return None


def regions(h):
    addr = 0
    mbi = MBI()
    while k32.VirtualQueryEx(h, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)):
        base = mbi.BaseAddress or 0
        size = mbi.RegionSize
        if (mbi.State == MEM_COMMIT and not (mbi.Protect & PAGE_GUARD)
                and not (mbi.Protect & PAGE_NOACCESS) and mbi.Protect != 0):
            yield base, size
        addr = base + size
        if addr >= 0x7FFFFFFFFFFF:
            break


def read(h, addr, size):
    buf = ctypes.create_string_buffer(size)
    got = ctypes.c_size_t(0)
    if not k32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, size, ctypes.byref(got)) or got.value == 0:
        return None
    return buf.raw[:got.value]


def be32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def f32(w):
    return struct.unpack(">f", struct.pack(">I", w))[0]


def hexrows(b, base_label):
    lines = []
    for o in range(0, len(b), 16):
        w = [be32(b, o + i) for i in range(0, 16, 4) if o + i + 4 <= len(b)]
        lines.append("  %s+%03X: %s   %s" % (base_label, o, " ".join("%08X" % x for x in w),
                                          " ".join("%10.4g" % f32(x) for x in w)))
    return lines


def main():
    pid = find_pid()
    if not pid:
        print("rpcs3.exe is not running. Start J-Stars in RPCS3, pause on the Kamehameha, then run this again.")
        return 1
    h = k32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
    if not h:
        print("Could not open rpcs3.exe for reading (error %d)." % ctypes.get_last_error())
        return 1
    print("Scanning rpcs3.exe (pid %d), read-only. This can take a minute..." % pid)

    bases = []        # candidate host addresses of guest 0
    cands = []        # host addresses of record starts
    scanned = 0
    for rbase, rsize in regions(h):
        off = 0
        while off < rsize:
            n = min(CHUNK, rsize - off)
            data = read(h, rbase + off, n)
            if data:
                scanned += len(data)
                # guest base: "STPK" at guest 0x30000000 with the resident pak name close after it
                i = data.find(b"STPK")
                while i != -1:
                    if data.find(b"game_resident_parameter_m.pak", i, i + 0x10000) != -1:
                        # the table header may sit a little past 0x30000000; the base is 64 KB aligned
                        b = (rbase + off + i - 0x30000000) & ~0xFFFF
                        if b >= 0 and b not in bases:
                            bases.append(b)
                    i = data.find(b"STPK", i + 4)
                # records: colour block at a 16-byte aligned +0x80
                # Records sit in a pool with a 0x130 stride, so require the colour block in
                # the neighbouring record too (before or after); one-off matches are other data.
                i = data.find(COLOUR)
                while i != -1 and len(cands) < MAX_CAND:
                    host = rbase + off + i
                    if host % 16 == 0 and i >= 0x80 and i - 0x80 + REC <= len(data):
                        nxt = data[i + REC: i + REC + 12] if i + REC + 12 <= len(data) else b""
                        prv = data[i - REC: i - REC + 12] if i >= REC else b""
                        if nxt == COLOUR or prv == COLOUR:
                            cands.append(host - 0x80)
                    i = data.find(COLOUR, i + 4)
            off += n

    out = []
    out.append("rpcs3_beam_scan: pid %d, %.1f MB scanned" % (pid, scanned / 1e6))
    out.append("guest base candidates (host address of guest 0x00000000): " +
               (", ".join("0x%X" % b for b in bases) if bases else "none found"))

    def guest_of(host):
        for b in bases:
            if b <= host < b + 0x100000000:
                return host - b
        return None

    # prefer records whose +0x124 link lands on a record-sized step from them
    def score(host):
        rec = read(h, host, REC) or b""
        if len(rec) < REC:
            return -1, rec
        g = guest_of(host)
        link = be32(rec, 0x124)
        s = 0
        if g is not None and link and (link - g) % REC == 0:
            s += 2
        if be32(rec, 0x8C) in (0x42C80000,):   # alpha 100 in our beam records
            s += 1
        if g is not None and 0x30000000 <= g < 0x40000000:   # main game heap
            s += 1
        return s, rec

    scored = []
    for c in cands:
        s, rec = score(c)
        if s >= 0:
            scored.append((s, c, rec))
    scored.sort(key=lambda t: -t[0])
    out.append("record candidates: %d (showing up to %d in detail, best first)" % (len(scored), MAX_DUMP))
    out.append("")
    out.append("summary: host, guest, +0x100..+0x117, +0x120..+0x127")
    for s, c, rec in scored[:60]:
        g = guest_of(c)
        out.append("  0x%X  guest=%s  +100: %s  +120: %s  score=%d" % (
            c, ("0x%08X" % g) if g is not None else "?",
            " ".join("%08X" % be32(rec, o) for o in range(0x100, 0x118, 4)),
            " ".join("%08X" % be32(rec, o) for o in range(0x120, 0x128, 4)), s))
    for s, c, rec in scored[:MAX_DUMP]:
        g = guest_of(c)
        out.append("")
        out.append("== record at host 0x%X guest %s (score %d), with the record before and after" % (
            c, ("0x%08X" % g) if g is not None else "?", s))
        block = read(h, c - REC, 3 * REC)
        if block and len(block) == 3 * REC:
            out += hexrows(block[:REC], "prev")
            out += hexrows(block[REC:2 * REC], "THIS")
            out += hexrows(block[2 * REC:], "next")
        else:
            out += hexrows(rec, "THIS")
    k32.CloseHandle(h)

    here = os.path.dirname(os.path.abspath(__file__))
    dst = os.path.join(os.path.dirname(here), "reference", "rpcs3_beam_scan.txt")
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print("\n".join(out[:8]))
    print("Done. Full results: %s" % dst)
    return 0


if __name__ == "__main__":
    sys.exit(main())
