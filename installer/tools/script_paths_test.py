#!/usr/bin/env python3
"""Which install-folder names break the generated build-port.cmd? Runs on Windows (or under Wine/Proton with a Windows Python).

    python script_paths_test.py FAKE_TOOL.exe SCRATCH_DIR

FAKE_TOOL.exe is fake_tool.c compiled (any C compiler; it writes its arguments to %FAKE_OUT%). For each name below the test makes an
install folder with that character in its path, a fake Visual Studio folder whose cmake.exe is the fake tool, writes build-port.cmd with
firstrun.write_build_cmd and runs it the way step_build does (firstrun.cmd_run_line). A name passes when the configure call got exactly
-S <root>\\port and -B <root>\\build\\port and the build call ran. The result decides firstrun.BAD_SCRIPT_CHARS.
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import firstrun  # noqa: E402

NAMES = {"space": "a b", "parens": "a (1)", "close-paren": "a)b", "square": "a[b]", "semicolon": "a;b", "ampersand": "a&b", "caret": "a^b",
         "percent": "a%b", "percent-var": "a%TEMP%b", "bang": "a!b", "quote": 'a"b', "apostrophe": "a'b", "equals": "a=b", "comma": "a,b",
         "hash": "a#b", "dollar": "a$b", "at": "a@b", "plus": "a+b", "tilde": "a~b", "accent": "café", "cjk": "ジョジョ",
         "double-space": "a  b", "trailing-dot": "a.b", "leading-dash": "-a"}


def main(tool, scratch):
    tool, scratch = pathlib.Path(tool).resolve(), pathlib.Path(scratch).resolve()
    for label, name in NAMES.items():
        base = scratch / label
        shutil.rmtree(base, ignore_errors=True)
        try:
            root = base / name / "app"
            (root / "port").mkdir(parents=True)
            vs = base / name / "tc"
            for p in firstrun.toolchain_paths(vs).values():
                p.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(tool, p)   # the fake tool records its arguments and exits 0
            ns = argparse.Namespace(root=str(root), eboot="x", toolchain=str(vs), python=None, jobs=1, force=[])
            c = firstrun.Ctx(ns, "US", "BLUS31519")
            (root / "build").mkdir()
            cmd = firstrun.write_build_cmd(c, 1)
            rec = base / "rec.txt"
            env = dict(os.environ, FAKE_OUT=str(rec))
            subprocess.run(firstrun.cmd_run_line(cmd), env=env, capture_output=True)
            lines = rec.read_bytes().decode("utf-8", "replace").splitlines() if rec.is_file() else []
            conf = [l.split("\t")[1:] for l in lines if "-S" in l.split("\t")]
            built = any("--build" in l.split("\t") for l in lines)
            good = bool(conf) and conf[0][conf[0].index("-S") + 1] == str(root / "port") and \
                conf[0][conf[0].index("-B") + 1] == str(root / "build" / "port") and built
            print(f"{'ok   ' if good else 'BREAKS'} {label:12} {name!r}" + ("" if good else f"  got {len(lines)} call(s)"))
        except OSError as e:
            print(f"skip   {label:12} {name!r}: cannot make the folder ({e.__class__.__name__})")
    return 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:3]))
