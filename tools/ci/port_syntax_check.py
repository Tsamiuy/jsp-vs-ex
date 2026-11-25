#!/usr/bin/env python3
"""Syntax-only compile of the current port's own sources against the patched ps3recomp, with no game data.

    python3 tools/ci/port_syntax_check.py --toolkit DIR [--cxx CXX] [--cc CC] [--region US|EU|both]

DIR is the patched toolkit (installer/tools/export_toolkit.py --out DIR). The port includes ppu_recomp.h, which the lifter writes from
the player's EBOOT, so a real build and link are impossible without it. The toolkit's own tools/check_ppu_scaffold.py faces the same
problem and builds a stand-in from the lifter's HEADER_PREAMBLE (the real ABI: ppu_context, vm_* helpers, PPU_FENCE); this does the same
and compiles with `-fsyntax-only`, with the include directories and the flags installer/port/jsp/CMakeLists.txt gives the jsp target:
  C++20  installer/port/jsp/*.cpp (with the rollback build definitions; the pinned GekkoNet copy in gekkonet/ is not checked)  and the PPU boot scaffold runtime/ppu/{ppu_loader,ppu_hle,ppu_fs,ppu_sysprx}.cpp
  C17    runtime/spu/spu_dispatch_mt.c  (built by the port with SPU_USE_DISPATCH_MT)
Two passes (--region both, the default; jsp_region.h):
  US  the stand-in declares every game function the port names (the US lift has them all), as the real US build sees it.
  EU  the port sources again with -DJSP_REGION_EU=1 (what CMakeLists.txt adds for JSP_GAME_REGION=EU), the stand-in declaring only the
      names in tools/ci/eu_lift_funcs.txt (those the EU lift also has): a direct call of a US-only function outside `#if JSP_US_CODE`
      fails here as it fails in a real EU build (2026-11-25: 92 errors in five files of 6ada03e); the other names are poisoned (#pragma GCC
      poison), so a source that declares a US-only function itself (a link error in the real build) fails too. The scaffold and SPU
      files run once.
  Every port file that tests JSP_US_CODE must include jsp_region.h itself (checked first, by text): the passes run with -w, and an undefined
  JSP_US_CODE would read as 0 and drop the US code without a word.
  LIMITS of the EU pass: it only sees lifted functions called BY NAME. A US address used as a number is not caught: calls through
  ps3_indirect_call_to or function_table / ppu_register_function / jsp_hook_chain at a US address, and vm_read*/vm_write* of US data
  addresses; those stay behind jsp_game_is_us() at run time and are reviewed by hand. Nor does a name on eu_lift_funcs.txt mean the same
  function: the EU code is shifted, so a name that compiles on EU is in general a different function there.
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
INCS = ["include", "runtime/ppu", "runtime/spu", "runtime/memory", "runtime/syscalls", "runtime/platform", "libs/video", "libs/network/psnr"]
FLAGS = ["-fsyntax-only", "-w", "-ffp-contract=off", "-mavx2", "-mfma", "-fno-strict-aliasing", "-fwrapv"]   # CMakeLists.txt: JSP_AVX2 on
# CMakeLists.txt: the jsp target is built with JSP_ROLLBACK_BUILD and links jsp_gekkonet, whose include folder and definitions are PUBLIC
PORT_DEFS = ["-DJSP_ROLLBACK_BUILD=1", "-DGEKKONET_STATIC", "-DGEKKONET_NO_ASIO"]
SCAFFOLD = ["ppu_loader", "ppu_hle", "ppu_fs", "ppu_sysprx"]
FILE_INCS = {"jsp_net_probe.cpp": ["libs/network"], "jsp_stepper.cpp": ["libs/input"]}   # CMakeLists.txt: set_source_files_properties(... INCLUDE_DIRECTORIES)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--toolkit", required=True)
    ap.add_argument("--cxx", default="x86_64-w64-mingw32-clang++")
    ap.add_argument("--cc", default="x86_64-w64-mingw32-clang")
    ap.add_argument("--region", choices=("US", "EU", "both"), default="both")
    a = ap.parse_args()
    tk = pathlib.Path(a.toolkit).resolve()
    spec = importlib.util.spec_from_file_location("ppu_lifter", tk / "tools/ppu_lifter.py")
    lifter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(lifter)
    port = ROOT / "installer/port/jsp"
    # the guest functions the port calls or hooks: the real header declares every lifted function like this (ppu_lifter.py, "external")
    funcs = sorted({m for p in port.glob("*") if p.suffix in (".cpp", ".h", ".inc")
                    for m in re.findall(r"\bfunc_[0-9A-F]{8}\b", p.read_text(encoding="utf-8", errors="replace"))})
    eu_have = {ln.strip() for ln in (ROOT / "tools/ci/eu_lift_funcs.txt").read_text(encoding="utf-8").splitlines()
               if ln.strip() and not ln.startswith("#")}
    # a file that tests JSP_US_CODE without jsp_region.h would compile its US code out silently under -w (an undefined macro is 0)
    no_inc = [p.name for p in sorted(port.glob("*")) if p.suffix in (".cpp", ".h", ".inc") and p.name != "jsp_region.h"
              and re.search(r"^\s*#\s*(el)?if\b.*\bJSP_US_CODE\b", t := p.read_text(encoding="utf-8", errors="replace"), re.M)
              and not re.search(r'^\s*#\s*include\s+"jsp_region\.h"', t, re.M)]
    for n in no_inc:
        print(f"FAIL {n}: tests JSP_US_CODE but does not include jsp_region.h")
    passes = [r for r in ("US", "EU") if a.region in (r, "both")]
    with tempfile.TemporaryDirectory(prefix="jsp_stub_") as tmp:
        jobs = []
        for region in passes:
            stub = pathlib.Path(tmp) / region
            stub.mkdir()
            have = funcs if region == "US" else [f for f in funcs if f in eu_have]
            decls = "".join(f"void {f}(ppu_context* ctx);\n" for f in have)
            # EU: the names the EU lift lacks are poisoned, so a source that declares one itself (and would only fail at the link) fails here too;
            # code inside `#if JSP_US_CODE` and comments are not affected
            poison = "".join(f"#pragma GCC poison {f}\n" for f in funcs if f not in have)
            (stub / "ppu_recomp.h").write_text(
                lifter.HEADER_PREAMBLE + '\n#ifdef __cplusplus\nextern "C" {\n#endif\nvoid ppu_recomp_register(void);\n' + decls
                + '#ifdef __cplusplus\n}\n#endif\n' + poison, encoding="utf-8")
            incs = [f"-I{stub}", *[f"-I{tk / d}" for d in INCS], f"-I{port}", f"-I{port / 'gekkonet/include'}"]
            rdef = ["-DJSP_REGION_EU=1"] if region == "EU" else []
            jobs += [(region, a.cxx, ["-std=c++20", *PORT_DEFS, *rdef, "-x", "c++"], incs, p) for p in sorted(port.glob("*.cpp"))]
            if region == passes[0]:
                jobs += [(region, a.cxx, ["-std=c++20", "-x", "c++"], incs, tk / "runtime/ppu" / f"{n}.cpp") for n in SCAFFOLD]
                jobs += [(region, a.cc, ["-std=c17", "-DSPU_USE_DISPATCH_MT", "-x", "c"], incs, tk / "runtime/spu/spu_dispatch_mt.c")]
        bad = 0
        for region, cc, std, incs, src in jobs:
            extra = [f"-I{tk / d}" for d in FILE_INCS.get(src.name, [])]
            r = subprocess.run([cc, *FLAGS, *std, *incs, *extra, str(src)], capture_output=True, text=True, encoding="utf-8", errors="replace")
            name = f"{src.name} ({region})"
            if r.returncode:
                bad += 1
                print(f"FAIL {name}\n{(r.stdout + r.stderr)[-3000:]}")
            else:
                print(f"ok   {name}")
    print(f"port syntax check: {len(jobs) - bad}/{len(jobs)} files compile" + (f"; {len(no_inc)} file(s) without jsp_region.h" if no_inc else ""))
    return 1 if bad or no_inc else 0


if __name__ == "__main__":
    sys.exit(main())
