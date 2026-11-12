#!/usr/bin/env python3
"""Run the host tests listed in tools/ci/host-tests.txt, one named result each; exit 1 if any failed.

    python3 tools/ci/run_host_tests.py [--list FILE] [--scratch DIR] [--toolkit DIR] [--fake-tool EXE] [--launcher EXE]
                                       [--strict-skip] [--check-coverage] [--os posix|windows]

--check-coverage only checks that every test file in git (*_test.*, test_*.py, *selftest*) is listed or excluded in the list.
--strict-skip: a test that prints a line starting with SKIP counts as failed (CI: a missing helper must not pass quietly).
Standard library only. Every test runs, the exit code names the failed ones at the end.
"""
import argparse
import fnmatch
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
TEST_NAME = re.compile(r"(^|/)([^/]*_test\.[^/]*|test_[^/]*|[^/]*selftest[^/]*)$")


def read_list(path):
    rows, excludes = [], []
    for n, line in enumerate(pathlib.Path(path).read_text(encoding="utf-8").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|")]
        if parts[0] == "exclude":
            if len(parts) != 3 or not parts[2]:
                sys.exit(f"{path}:{n}: an exclude row needs a glob and a reason")
            excludes.append((parts[1], parts[2]))
        elif len(parts) == 2 and parts[0] in ("all", "posix", "windows"):
            rows.append((parts[0], parts[1].split()))
        else:
            sys.exit(f"{path}:{n}: bad row: {line}")
    return rows, excludes


def check_coverage(rows, excludes):
    tracked = subprocess.run(["git", "-C", str(ROOT), "ls-files"], capture_output=True, text=True, check=True).stdout.splitlines()
    listed = {cmd[0] for _, cmd in rows}
    bad = []
    for f in tracked:
        if TEST_NAME.search(f) and f not in listed and not any(fnmatch.fnmatch(f, g) for g, _ in excludes):
            bad.append(f)
    for f in sorted(listed):
        if f not in tracked:
            bad.append(f"{f} (listed but not in git)")
    for f in bad:
        print(f"::error::host-tests.txt: {f} is not listed or excluded (add a row, or an exclude row with a reason)")
    print(f"coverage: {len(tracked)} tracked files checked, {len(bad)} problem(s)")
    return not bad


def command(cmd, subs, scratch):
    path = ROOT / cmd[0]
    args = []
    for a in cmd[1:]:
        m = re.fullmatch(r"\{(\w+)\}", a)
        if m:
            v = scratch if m.group(1) == "scratch" else subs.get(m.group(1))
            if not v:
                return None, f"needs --{m.group(1).replace('_', '-')}"
            a = str(v)
        args.append(a)
    if path.suffix == ".py":
        return [sys.executable, "-I", str(path), *args], ""
    if path.suffix == ".sh":
        return ["bash", str(path), *args], ""
    if path.suffix == ".c":
        # a C test: compile with the system compiler, include dir from the toolkit (libs/video), run
        cc = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
        if not cc:
            return None, "no C compiler"
        exe = pathlib.Path(scratch) / "t.exe"
        inc = pathlib.Path(args[0]) / "libs" / "video"
        return [("compile", [cc, "-O1", "-Wall", f"-I{inc}", str(path), "-o", str(exe), "-lm"]), [str(exe)]], ""
    return None, "unknown test type"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", default=str(ROOT / "tools/ci/host-tests.txt"))
    ap.add_argument("--scratch")
    ap.add_argument("--toolkit")
    ap.add_argument("--fake-tool")
    ap.add_argument("--launcher")
    ap.add_argument("--font", help="FiraSans-HeavyItalic.ttf (default build/installer/fonts/, where installer.yml puts it)")
    ap.add_argument("--strict-skip", action="store_true")
    ap.add_argument("--check-coverage", action="store_true")
    ap.add_argument("--os", default="windows" if os.name == "nt" else "posix")
    a = ap.parse_args()
    rows, excludes = read_list(a.list)
    if a.check_coverage:
        return 0 if check_coverage(rows, excludes) else 1
    gh = os.environ.get("GITHUB_ACTIONS") == "true"
    base = pathlib.Path(a.scratch or tempfile.mkdtemp(prefix="jsp_host_tests_"))
    subs = {"toolkit": a.toolkit, "fake_tool": a.fake_tool, "launcher": a.launcher, "font": a.font or str(ROOT / "build/installer/fonts/FiraSans-HeavyItalic.ttf")}
    ok, failed, skipped = 0, [], []
    for where, cmd in rows:
        if where not in ("all", a.os):
            skipped.append(f"{cmd[0]} (runs on {where} only)")
            continue
        name = cmd[0]
        scratch = base / re.sub(r"\W+", "_", name)
        shutil.rmtree(scratch, ignore_errors=True)
        scratch.mkdir(parents=True)
        run, why = command(cmd, subs, scratch)
        if run is None:
            print(f"FAIL {name}: {why}")
            failed.append(name)
            continue
        out, rc = "", 0
        env = dict(os.environ, PYTHONUTF8="1")
        steps = [run[0], ("run", run[1])] if isinstance(run[0], tuple) else [("run", run)]
        for _, argv in steps:
            try:
                r = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=600, env=env)
                out += r.stdout + r.stderr
                rc = r.returncode
            except subprocess.TimeoutExpired:
                out += "timed out after 600 s\n"
                rc = 124
            if rc:
                break
        skip = a.strict_skip and re.search(r"^SKIP", out, re.M)
        if gh:
            print(f"::group::{name}")
        print(out[-4000:] if rc or skip else out[-600:])
        if gh:
            print("::endgroup::")
        if rc or skip:
            print(f"FAIL {name}" + (" (the test skipped itself: a helper it needs is missing)" if skip and not rc else f" (exit {rc})"))
            failed.append(name)
        else:
            print(f"ok   {name}")
            ok += 1
    for s in skipped:
        print(f"skip {s}")
    print(f"host tests: {ok}/{ok + len(failed)} pass" + (f"; failed: {', '.join(failed)}" if failed else ""))
    if failed and gh:
        print("::error::failed host tests: " + ", ".join(failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
