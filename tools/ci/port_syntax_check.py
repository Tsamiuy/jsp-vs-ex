#!/usr/bin/env python3
"""Syntax-only compile of the current port's own sources against the patched ps3recomp, with no game data.

    python3 tools/ci/port_syntax_check.py --toolkit DIR [--cxx CXX] [--cc CC]

DIR is the patched toolkit (installer/tools/export_toolkit.py --out DIR). The port includes ppu_recomp.h, which the lifter writes from
the player's EBOOT, so a real build and link are impossible without it. The toolkit's own tools/check_ppu_scaffold.py faces the same
problem and builds a stand-in from the lifter's HEADER_PREAMBLE (the real ABI: ppu_context, vm_* helpers, PPU_FENCE); this does the same
and compiles with `-fsyntax-only`, with the include directories and the flags installer/port/jsp/CMakeLists.txt gives the jsp target:
  C++20  installer/port/jsp/*.cpp  and the PPU boot scaffold runtime/ppu/{ppu_loader,ppu_hle,ppu_fs,ppu_sysprx}.cpp
  C17    runtime/spu/spu_dispatch_mt.c  (built by the port with SPU_USE_DISPATCH_MT)
Not covered: anything generated (the lifted PPU and SPU code, the NID table), the runtime library, linking, running.
Compilers: the llvm-mingw clang the installer bundles (x86_64-w64-mingw32-clang++ / -clang). Standard library only.
"""
import argparse
import importlib.util
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
INCS = ["include", "runtime/ppu", "runtime/spu", "runtime/memory", "runtime/syscalls", "runtime/platform", "libs/video"]
FLAGS = ["-fsyntax-only", "-w", "-ffp-contract=off", "-mavx2", "-mfma", "-fno-strict-aliasing", "-fwrapv"]   # CMakeLists.txt: JSP_AVX2 on
SCAFFOLD = ["ppu_loader", "ppu_hle", "ppu_fs", "ppu_sysprx"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--toolkit", required=True)
    ap.add_argument("--cxx", default="x86_64-w64-mingw32-clang++")
    ap.add_argument("--cc", default="x86_64-w64-mingw32-clang")
    a = ap.parse_args()
    tk = pathlib.Path(a.toolkit).resolve()
    spec = importlib.util.spec_from_file_location("ppu_lifter", tk / "tools/ppu_lifter.py")
    lifter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(lifter)
    with tempfile.TemporaryDirectory(prefix="jsp_stub_") as stub:
        port = ROOT / "installer/port/jsp"
        # the guest functions the port calls or hooks: the real header declares every lifted function like this (ppu_lifter.py, "external")
        funcs = sorted({m for p in port.glob("*") if p.suffix in (".cpp", ".h", ".inc")
                        for m in re.findall(r"\bfunc_[0-9A-F]{8}\b", p.read_text(encoding="utf-8", errors="replace"))})
        decls = "".join(f"void {f}(ppu_context* ctx);\n" for f in funcs)
        (pathlib.Path(stub) / "ppu_recomp.h").write_text(
            lifter.HEADER_PREAMBLE + '\n#ifdef __cplusplus\nextern "C" {\n#endif\nvoid ppu_recomp_register(void);\n' + decls
            + '#ifdef __cplusplus\n}\n#endif\n', encoding="utf-8")
        incs = [f"-I{stub}", *[f"-I{tk / d}" for d in INCS], f"-I{port}"]
        jobs = [(a.cxx, ["-std=c++20", "-x", "c++"], p) for p in sorted(port.glob("*.cpp"))]
        jobs += [(a.cxx, ["-std=c++20", "-x", "c++"], tk / "runtime/ppu" / f"{n}.cpp") for n in SCAFFOLD]
        jobs += [(a.cc, ["-std=c17", "-DSPU_USE_DISPATCH_MT", "-x", "c"], tk / "runtime/spu/spu_dispatch_mt.c")]
        bad = 0
        for cc, std, src in jobs:
            r = subprocess.run([cc, *FLAGS, *std, *incs, str(src)], capture_output=True, text=True, encoding="utf-8", errors="replace")
            name = src.name
            if r.returncode:
                bad += 1
                print(f"FAIL {name}\n{(r.stdout + r.stderr)[-3000:]}")
            else:
                print(f"ok   {name}")
    print(f"port syntax check: {len(jobs) - bad}/{len(jobs)} files compile")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
