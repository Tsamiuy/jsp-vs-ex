#!/usr/bin/env python3
"""Apply the patch series to a scratch copy of the pinned ps3recomp, the way the installer build does, and fail on any hunk that does not apply.

    python3 tools/ci/check_patch_series.py [--scratch DIR]        (needs Git, and the submodule third_party/ps3recomp checked out)

  installer/port/patches on ps3recomp v0.19.0: installer/tools/export_toolkit.py (the tool assemble_payload.py and so firstrun use):
  `git archive` of the pin, then `git apply -p1` of every patch in name order. The legacy series (installer/patches on the v0.15 tree)
  is no longer checked; its last checked state is the git tag legacy-v15 (issue #152).
Nothing is written outside the scratch folder. Standard library only.
"""
import argparse
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "installer/tools"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scratch")
    a = ap.parse_args()
    scratch = pathlib.Path(a.scratch or tempfile.mkdtemp(prefix="jsp_series_"))
    ok = True

    # installer/port/patches on the v0.19.0 pin
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

    if not a.scratch:
        shutil.rmtree(scratch, ignore_errors=True)
    print("RESULT: " + ("ok" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
