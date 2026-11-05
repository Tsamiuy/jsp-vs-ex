#!/usr/bin/env python3
"""Tests for tools/release.py on small fixture repositories (no network, no game files).

    python3 tools/test_release.py
"""
import contextlib
import io
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import release  # noqa: E402

CMAKE = "cmake_minimum_required(VERSION 3.20)\nproject({name}\n    VERSION {v}\n    LANGUAGES C CXX\n)\n"
LOG = """# Changelog

## [Unreleased]

- Added a thing.

## [0.1.0] - 2026-01-02

- First.
"""
TMP = None
CASES = 0


def repo(version="0.1.0", cmake=None, log=LOG):
    d = pathlib.Path(tempfile.mkdtemp(dir=TMP))
    (d / "installer/port/jsp").mkdir(parents=True)
    (d / "jstars/port").mkdir(parents=True)
    (d / release.VERSION_FILE).write_text(version + "\n", encoding="utf-8")
    for rel, name in zip(release.CMAKE_FILES, ("jsp", "JStarsRecomp")):
        v = cmake.get(rel, version) if cmake else version
        (d / rel).write_text(CMAKE.format(name=name, v=v), encoding="utf-8")
    if log is not None:
        (d / release.CHANGELOG).write_text(log, encoding="utf-8")
    return d


def snapshot(d):
    return {p.relative_to(d).as_posix(): p.read_text(encoding="utf-8") for p in d.rglob("*") if p.is_file()}


def run(*args):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        code = release.main(list(args))
    return code, out.getvalue()


def expect(cond, what):
    global CASES
    CASES += 1
    if not cond:
        print(f"FAIL: {what}")
        sys.exit(1)


def main():
    global TMP
    with tempfile.TemporaryDirectory() as t:
        TMP = t
        # check
        d = repo()
        code, out = run("--root", str(d), "--check")
        expect(code == 0 and out.strip() == "release: OK (0.1.0)", "check ok")
        code, out = run("--root", str(d))
        expect(code == 0, "check is the default")
        d = repo("0.0.1", log="# Changelog\n\n## [Unreleased]\n\n- x\n")
        expect(run("--root", str(d), "--check")[0] == 0, "0.0.1 needs no own section")
        d = repo(cmake={release.CMAKE_FILES[0]: "0.1.1"})
        code, out = run("--root", str(d), "--check")
        expect(code == 1 and release.CMAKE_FILES[0] in out and out.count("\n") == 1, "cmake mismatch, one line")
        d = repo(cmake={release.CMAKE_FILES[1]: "0.0.9"})
        expect(release.CMAKE_FILES[1] in run("--root", str(d), "--check")[1], "second cmake mismatch")
        d = repo("1.2")
        expect("not a plain X.Y.Z" in run("--root", str(d), "--check")[1], "version not plain")
        d = repo("0.2.0")
        expect("no '## [0.2.0]'" in run("--root", str(d), "--check")[1], "changelog lacks current version")
        d = repo(log="# Changelog\n\n## [0.1.0] - 2026-01-02\n\n- First.\n")
        expect("Unreleased" in run("--root", str(d), "--check")[1], "no Unreleased first")
        d = repo(log=None)
        expect("CHANGELOG.md: missing" in run("--root", str(d), "--check")[1], "no changelog")
        d = repo()
        (d / release.CMAKE_FILES[0]).write_text("project(x)\n", encoding="utf-8")
        expect("no project(" in run("--root", str(d), "--check")[1], "cmake without version")

        # bump
        d = repo()
        code, out = run("--root", str(d), "bump", "0.2.0")
        expect(code == 0 and "installer/VERSION" in out and "Next:" in out, "bump exact")
        expect(snapshot(d)[release.VERSION_FILE] == "0.2.0\n", "VERSION written")
        for rel in release.CMAKE_FILES:
            expect(release.cmake_version(snapshot(d)[rel]) == "0.2.0", "cmake written " + rel)
            expect("cmake_minimum_required(VERSION 3.20)" in snapshot(d)[rel], "minimum untouched")
        log = snapshot(d)[release.CHANGELOG]
        expect(log.startswith("# Changelog\n\n## [Unreleased]\n\n## [0.2.0] - 20"), "changelog heading turned")
        expect("- Added a thing." in release.find_section(log, "0.2.0"), "entries moved under the version")
        expect(run("--root", str(d), "--check")[0] == 0, "check ok after bump")
        expect(run("--root", str(d), "bump", "patch")[0] == 1, "second bump: Unreleased empty")
        for arg, want in (("major", "1.0.0"), ("minor", "0.2.0"), ("patch", "0.1.1")):
            d = repo()
            expect(run("--root", str(d), "bump", arg)[0] == 0 and snapshot(d)[release.VERSION_FILE] == want + "\n",
                   f"bump {arg} -> {want}")

        # refusals leave the tree unchanged
        def refused(d, arg, text):
            before = snapshot(d)
            code, out = run("--root", str(d), "bump", arg)
            expect(code == 1 and text in out and snapshot(d) == before, f"refuse {arg}: {text}")

        refused(repo(), "0.1.0", "not greater")
        refused(repo(), "0.0.9", "not greater")
        refused(repo(), "1.0", "is not X.Y.Z")
        refused(repo(log="# Changelog\n\n## [Unreleased]\n\n## [0.1.0] - 2026-01-02\n\n- First.\n"), "patch", "empty")
        refused(repo(cmake={release.CMAKE_FILES[0]: "9.9.9"}), "patch", release.CMAKE_FILES[0])

        # notes
        d = repo()
        code, out = run("--root", str(d), "notes", "0.1.0")
        expect(code == 0 and out.strip() == "- First.", "notes body only")
        code, out = run("--root", str(d), "notes", "0.1.0")
        expect("Unreleased" not in out, "notes stop at the next heading")
        expect(run("--root", str(d), "notes", "9.9.9")[0] == 1, "notes for a missing version")
    print(f"test_release: ok ({CASES} cases)")


if __name__ == "__main__":
    main()
