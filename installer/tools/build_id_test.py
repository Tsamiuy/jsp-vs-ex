"""Host test of installer/port/jsp/jsp_build_id.cmake and its wiring: the rollback build id is made from the release
content, so two PCs that build the same release agree, and a change to the glue, the toolkit, the version or the region
changes it. Needs cmake on PATH.

    python3 -I installer/tools/build_id_test.py SCRATCH_DIR
"""
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "installer" / "port" / "jsp"
MODULE = SRC / "jsp_build_id.cmake"


def build_id(glue, toolkit, version="0.5.0", region="US"):
    driver = Path(glue).parent / "driver.cmake"
    driver.write_text(
        f'include("{MODULE.as_posix()}")\n'
        f'jsp_build_id(ID "{version}" "{region}" "{Path(glue).as_posix()}" "{Path(toolkit).as_posix()}")\n'
        'message("${ID}")\n')
    r = subprocess.run(["cmake", "-P", str(driver)], capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"cmake -P failed: {r.stderr}")
    return r.stderr.strip()          # message() without a mode prints to stderr


def fake_toolkit(dst):
    for rel, text in (("runtime/ppu/ctx.h", "struct ctx; /* a; b [c] */\n"), ("libs/input/pad.c", "int pad(void) { return 0; }\n"),
                      ("include/ps3.h", "#define PS3 1\n"), ("docs/readme.md", "not an input\n")):
        p = dst / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
    return dst


def copy(dst):
    shutil.copytree(SRC, dst)
    return dst


def crlf(tree):
    for p in tree.rglob("*"):
        if p.is_file():
            b = p.read_bytes()
            if b"\0" not in b:
                p.write_bytes(b.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))


def main():
    scratch = Path(sys.argv[1] if len(sys.argv) > 1 else "build-id-scratch").resolve()
    scratch.mkdir(parents=True, exist_ok=True)
    fails = 0

    def check(name, ok):
        nonlocal fails
        print(("ok   " if ok else "FAIL ") + name)
        fails += not ok

    tk = fake_toolkit(scratch / "tk")
    a = copy(scratch / "a" / "jsp")
    base = build_id(a, tk)
    check("id has the 0xXXXXXXXX form", re.fullmatch(r"0x[0-9a-f]{8}", base) is not None)
    check("same content in other folders gives the same id",
          build_id(copy(scratch / "b" / "jsp"), fake_toolkit(scratch / "tk2")) == base)

    c = copy(scratch / "c" / "jsp")
    tkc = fake_toolkit(scratch / "tkc")
    crlf(c)
    crlf(tkc)
    check("a CR-LF copy of every text file gives the same id", build_id(c, tkc) == base)

    d = copy(scratch / "d" / "jsp")
    for rel in ("build/generated.h", "build-x/g.cpp", "CMakeFiles/x.c", "__pycache__/lod_hooks.py"):
        (d / rel).parent.mkdir(parents=True, exist_ok=True)
        (d / rel).write_text("int x;\n")
    (tk / "docs" / "extra.c").write_text("int y;\n")
    check("build folders, caches and toolkit files outside runtime/libs/include do not change the id", build_id(d, tk) == base)

    e = copy(scratch / "e" / "jsp")
    with open(e / "jsp_net_game.cpp", "a", newline="\n") as f:
        f.write("// changed\n")
    check("a changed glue source changes the id", build_id(e, tk) != base)

    tke = fake_toolkit(scratch / "tke")
    (tke / "libs" / "input" / "pad.c").write_text("int pad(void) { return 1; }\n")
    check("a changed toolkit source (a patch) changes the id", build_id(a, tke) != base)

    check("another version changes the id", build_id(a, tk, version="0.5.1") != base)
    check("another region changes the id", build_id(a, tk, region="EU") != base)

    # The wiring: the id reaches the one file that reads it, and that file no longer takes the compile time first.
    cm = (SRC / "CMakeLists.txt").read_text()
    check("CMakeLists.txt computes the id and passes it to jsp_net_game.cpp",
          "jsp_build_id(JSP_NET_BUILD_ID" in cm and
          re.search(r"set_property\(SOURCE jsp_net_game\.cpp APPEND PROPERTY COMPILE_DEFINITIONS JSP_NET_BUILD_ID=", cm) is not None)
    game = (SRC / "jsp_net_game.cpp").read_text()
    fn = game[game.index("uint32_t build_id()"):]
    fn = fn[:fn.index("\n}\n")]
    check("build_id(): the env override, then the content id, the compile time only as the fallback",
          fn.index("JSP_NET_BUILD") < fn.index("#ifdef JSP_NET_BUILD_ID") < fn.index("#else") < fn.index("__DATE__"))

    # The pipelines.bin stamp (toolkit patch 2069): content of the translators, the D3D12 backend and psow_file.h, nothing else.
    def stamp(toolkit):
        driver = Path(toolkit).parent / "stamp_driver.cmake"
        driver.write_text(f'include("{MODULE.as_posix()}")\njsp_psow_stamp(ST "{Path(toolkit).as_posix()}")\nmessage("${{ST}}")\n')
        r = subprocess.run(["cmake", "-P", str(driver)], capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit(f"cmake -P failed: {r.stderr}")
        return r.stderr.strip()

    def video_toolkit(dst):
        for rel, text in (("libs/video/rsx_fp_decompiler.c", "int fp;\n"), ("libs/video/rsx_vp_decompiler.c", "int vp;\n"),
                          ("libs/video/rsx_d3d12_backend.c", "int be;\n"), ("libs/video/psow_file.h", "int ps;\n"),
                          ("libs/video/rsx_vpull.h", "int vpull;\n"), ("libs/video/cellGcmSys.c", "not an input\n"),
                          ("runtime/ppu/ctx.h", "not an input\n")):
            p = dst / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(text)
        return dst

    v0 = video_toolkit(scratch / "v0")
    st0 = stamp(v0)
    check("stamp has the 0x<16 hex>ULL form", re.fullmatch(r"0x[0-9a-f]{16}ULL", st0) is not None)
    v1 = video_toolkit(scratch / "v1")
    check("stamp: the same content in another folder gives the same stamp, and no time or path enters", stamp(v1) == st0)
    v2 = video_toolkit(scratch / "v2")
    crlf(v2)
    check("stamp: a CR-LF copy gives the same stamp", stamp(v2) == st0)
    (v0 / "libs/video/cellGcmSys.c").write_text("changed\n")
    (v0 / "runtime/ppu/ctx.h").write_text("changed\n")
    check("stamp: files that do not decide the bytecode do not change it", stamp(v0) == st0)
    for rel in ("libs/video/rsx_fp_decompiler.c", "libs/video/rsx_vp_decompiler.c", "libs/video/rsx_d3d12_backend.c", "libs/video/psow_file.h"):
        vx = video_toolkit(scratch / ("vx_" + Path(rel).name))
        (vx / rel).write_text("int changed;\n")
        check(f"stamp: a change to {Path(rel).name} changes it", stamp(vx) != st0)
    cm0 = (SRC / "CMakeLists.txt").read_text()
    check("CMakeLists.txt passes PSOW_BUILD_STAMP to the backend in the runtime library's directory",
          "jsp_psow_stamp(JSP_PSOW_STAMP" in cm0 and "TARGET_DIRECTORY ps3recomp_runtime" in cm0 and "PSOW_BUILD_STAMP=${JSP_PSOW_STAMP}" in cm0)

    print("build id: " + ("all checks passed" if not fails else f"{fails} check(s) failed"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
