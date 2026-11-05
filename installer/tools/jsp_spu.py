#!/usr/bin/env python3
"""SPU job step of the installer build: find the SPURS job binaries inside EBOOT.elf and lift them to C.

Adapted from this repository (MIT): jstars/spu_jobs/scan_jobcrt.py (the scan, unchanged in substance) and
jstars/spu_jobs/lift_all_jobs.py (the lift loop and the spu_workloads.c registration table), with the folder layout turned
into arguments so it runs from the installer payload. Standard library only; runs the bundled ps3recomp v0.15 tools
(wrap_spu_elf.py, spu_lifter.py) with the same Python.

    python jsp_spu.py EBOOT.elf --tools PS3RECOMP/tools --work DIR --out DIR/spu [--scan-only] [--jobs 4]

Every job is embedded in the EBOOT (JOBCRT layout: "JOBCRT Ver14" tag at +0x30 after 16 bytes of 0xA5, info block
"%SPURS JOB INFO%"); nothing is read from the disc and nothing needs a run-time dump. The fingerprint is FNV-1a-64 over the
job's sizeBinary bytes with the runtime's truncated basis, which is what the runtime dispatches by.
"""
import argparse
import json
import pathlib
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

# Already named by hand in this repository's notes (va -> (dir, symbol stem, name)); other jobs are named after their address.
KNOWN = {
    0x0080B700: ("spujob_80B700", "job_80B700", "spujob_80B700"),
    0x0080FD00: ("spujob_80FD00", "job_80FD00", "spujob_80FD00"),
    0x00829A80: ("crijob_asrvoice", "cri_asrvoice", "crijob_asrvoice"),
    0x00819780: ("crijob_atomdec", "cri_atomdec", "crijob_atomdec"),
    0x0080CB00: ("spujob_80CB00", "job_80CB00", "spujob_80CB00"),
}


def fnv(d):
    h = 1469598103934665603          # runtime's (truncated) basis, see spu_workload.c
    for c in d:
        h = ((h ^ c) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def segments(b):
    phoff, = struct.unpack(">Q", b[0x20:0x28])
    phnum, = struct.unpack(">H", b[0x38:0x3A])
    return [struct.unpack(">IIQQQQQQ", b[phoff + i * 56:phoff + (i + 1) * 56]) for i in range(phnum)]


def scan(b):
    """@ scan_jobcrt.py: every JOBCRT job as dict(va, size, fp, code_end, off)."""
    segs = segments(b)

    def va(o):
        for t, f, off, v, pa, fs, ms, al in segs:
            if t == 1 and off <= o < off + fs:
                return v + o - off

    jobs = []
    o = b.find(b"JOBCRT Ver14")
    while o >= 0:
        s = o - 0x30
        if b[s + 0x20:s + 0x30] == b"\xA5" * 16:
            info = s + struct.unpack(">I", b[s + 0x3C:s + 0x40])[0] + 0x10
            if b[info:info + 16] == b"%SPURS JOB INFO%":
                p = info + 0x20
                while b[p:p + 8] != b"\xFF" * 8:
                    p += 8
                end = s + ((p + 8 - s + 15) & ~15)   # sizeBinary is 16-byte aligned
                size = end - s
                d = b[s:end]
                # code end: last "bi $lr" before the data area, +4, 16-aligned
                data_off = struct.unpack(">I", b[s + 0x40:s + 0x44])[0]
                last = max(k for k in range(0, min(data_off, size) - 3, 4) if d[k:k + 4] == b"\x35\x00\x00\x00")
                code_end = (last + 4 + 15) & ~15
                jobs.append(dict(va=va(s), size=size, fp="%016X" % fnv(d), code_end=code_end, off=s))
        o = b.find(b"JOBCRT Ver14", o + 1)
    return jobs


def name_for(j):
    if j["va"] in KNOWN:
        return KNOWN[j["va"]]
    v = "%X" % j["va"]
    return ("spujob_" + v, "job_" + v, "spujob_" + v)


def lift_one(j, b, tools, work, out):
    d, stem, _ = name_for(j)
    dest = out / d
    if (dest / "spu_recomp.c").is_file():
        return d, 0, "already lifted"
    bin_p, elf_p = work / (d + ".bin"), work / (d + ".elf")
    bin_p.write_bytes(b[j["off"]:j["off"] + j["size"]])
    py = [sys.executable, "-I", "-X", "utf8"]
    r = subprocess.run([*py, str(tools / "wrap_spu_elf.py"), str(bin_p), "--entry", "0x0", "--base", "0x0", "--out", str(elf_p)],
                       capture_output=True, text=True)
    if r.returncode:
        return d, r.returncode, "wrap failed: " + (r.stdout + r.stderr).strip()[-300:]
    r = subprocess.run([*py, str(tools / "spu_lifter.py"), "--auto-functions", str(elf_p), "--base", "0x0",
                        "--code-end", hex(j["code_end"]), "--symbol-prefix", "jstars_%s_" % stem, "-o", str(dest),
                        "--source-name", "spu_recomp.c", "--header-name", "spu_recomp.h",
                        "--report", str(work / ("lift_%s.json" % stem))], capture_output=True, text=True)
    tail = [l.strip() for l in (r.stdout + r.stderr).splitlines() if "lifted" in l or "unsupported" in l]
    return d, r.returncode, " | ".join(tail)[-300:]


def registration_source(jobs):
    """@ lift_all_jobs.py: the spu_workloads.c that registers every lifted job by fingerprint."""
    ext, reg = [], []
    for i, j in enumerate(jobs, 1):
        d, stem, nm = name_for(j)
        ext.append("extern void jstars_%s_spu_func_00000000(spu_context*);" % stem)
        ext.append("extern void jstars_%s_spu_recomp_register(void);" % stem)
        reg.append("    spu_begin_image(%d); jstars_%s_spu_recomp_register();" % (i, stem))
        reg.append('    spu_workload_register_img(0x%sULL, jstars_%s_spu_func_00000000, %d, "%s");' % (j["fp"], stem, i, nm))
    table = "\n".join(" *   %-16s EBOOT 0x%08X  %6d B  fp %s  --code-end 0x%X"
                      % (name_for(j)[2], j["va"], j["size"], j["fp"], j["code_end"]) for j in jobs)
    return """/* spu_workloads.c -- registers the lifted SPURS job binaries of this EBOOT.
 *
 * GENERATED by the installer's jsp_spu.py (after jstars/spu_jobs/lift_all_jobs.py) on the player's PC.
 *
%s
 */
#include "spu_workload.h"

extern void spu_begin_image(int image_id);

%s

void jstars_spu_workloads_register(void)
{
%s
    spu_begin_image(0);
}

/* Auto-register at startup (before the game adds any SPURS workload/task). */
__attribute__((constructor)) static void jstars_spu_workloads_register_ctor(void)
{
    jstars_spu_workloads_register();
}
""" % (table, "\n".join(ext), "\n".join(reg))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("eboot")
    ap.add_argument("--tools", required=True, help="ps3recomp tools folder (wrap_spu_elf.py, spu_lifter.py)")
    ap.add_argument("--work", required=True, help="folder for the .bin/.elf/report files")
    ap.add_argument("--out", required=True, help="folder for <job>/spu_recomp.c and spu_workloads.c")
    ap.add_argument("--scan-only", action="store_true")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--json", help="write the scan result here")
    a = ap.parse_args(argv)
    b = pathlib.Path(a.eboot).read_bytes()
    jobs = scan(b)
    for j in jobs:
        print("va 0x%08X size %6d fp %s code_end 0x%X" % (j["va"], j["size"], j["fp"], j["code_end"]))
    print(f"{len(jobs)} JOBCRT jobs found in the EBOOT")
    if a.json:
        pathlib.Path(a.json).write_text(json.dumps(jobs, indent=1))
    if a.scan_only or not jobs:
        return 0 if jobs else 1
    work, out = pathlib.Path(a.work), pathlib.Path(a.out)
    work.mkdir(parents=True, exist_ok=True)
    out.mkdir(parents=True, exist_ok=True)
    bad = 0
    with ThreadPoolExecutor(max(1, a.jobs)) as ex:
        for d, rc, msg in ex.map(lambda j: lift_one(j, b, pathlib.Path(a.tools), work, out), jobs):
            print(f"{d}: rc={rc} {msg}")
            bad += rc != 0
    if bad:
        print(f"{bad} job(s) failed to lift", file=sys.stderr)
        return 1
    (out / "spu_workloads.c").write_text(registration_source(jobs), newline="\n")
    print(f"wrote spu_workloads.c with {len(jobs)} jobs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
