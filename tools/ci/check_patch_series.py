#!/usr/bin/env python3
"""Apply both patch series to scratch copies of the pinned ps3recomp, the way the installer build does, and fail on any hunk that does not apply.

    python3 tools/ci/check_patch_series.py [--scratch DIR]        (needs Git, and the submodule third_party/ps3recomp checked out)

  current  installer/port/patches on ps3recomp v0.19.0: installer/tools/export_toolkit.py (the tool assemble_payload.py --tree current
           and so firstrun use): `git archive` of the pin, then `git apply -p1` of every patch in name order.
  legacy   installer/patches on the v0.15 tree: tools/toolkit/apply_toolkit.py, the port files of jstars/port and then
           assemble_payload.apply_patches (git apply -p1, then the reverse check that each patch is really in).
Nothing is written outside the scratch folder. Standard library only.
"""
import argparse
import importlib.util
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "installer/tools"))


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scratch")
    a = ap.parse_args()
    scratch = pathlib.Path(a.scratch or tempfile.mkdtemp(prefix="jsp_series_"))
    ok = True

    # current tree: installer/port/patches on the v0.19.0 pin
    import export_toolkit
    n_cur = len(list((ROOT / "installer/port/patches").glob("*.patch")))
    try:
        names = export_toolkit.export(ROOT / "third_party/ps3recomp", scratch / "current" / "ps3recomp", fetch=True)
        print(f"current: ps3recomp {export_toolkit.TAG} ({export_toolkit.PIN[:12]}), {len(names)} of {n_cur} patches applied")
        if len(names) != n_cur:
            ok = False
    except RuntimeError as e:
        print(f"::error::current series (installer/port/patches): {e}")
        ok = False

    # legacy tree: installer/patches on the v0.15 tree plus the port files
    ap_mod = load("assemble_payload", ROOT / "installer/tools/assemble_payload.py")
    out = scratch / "legacy"
    (out / "port").mkdir(parents=True)
    for n in ap_mod.PORT_FILES:
        data = (ROOT / "jstars/port" / n).read_bytes()
        if b"\0" not in data:
            data = data.replace(b"\r\n", b"\n")   # as assemble_payload.py does: the patches are LF
        (out / "port" / n).write_bytes(data)
    r = subprocess.run([sys.executable, "-I", str(ROOT / "tools/toolkit/apply_toolkit.py"), "--out", str(out / "ps3recomp")],
                       capture_output=True, text=True)
    if r.returncode:
        print("::error::legacy: apply_toolkit.py failed\n" + r.stdout + r.stderr)
        ok = False
    elif not ap_mod.apply_patches(out):
        print("::error::legacy series (installer/patches): a patch does not apply (details above)")
        ok = False
    else:
        print(f"legacy: {len(list((ROOT / 'installer/patches').glob('*.patch')))} patches applied on the v0.15 tree")
    if not a.scratch:
        shutil.rmtree(scratch, ignore_errors=True)
    print("RESULT: " + ("ok" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
