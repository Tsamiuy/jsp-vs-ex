#!/usr/bin/env python3
"""Release versioning: one version number (installer/VERSION), the CMake project versions that follow it, and CHANGELOG.md.

    python3 tools/release.py --check          # exit 1 (one line per problem) when a version field differs from
                                              # installer/VERSION, it is not a plain X.Y.Z, CHANGELOG.md has no
                                              # "## [Unreleased]" section first, or no "## [X.Y.Z]" section for it
    python3 tools/release.py bump X.Y.Z       # or major | minor | patch: write the new version everywhere (also the
                                              # <!-- version --> spans in the docs) and turn
                                              # "## [Unreleased]" into "## [X.Y.Z] - date" (UTC) with a fresh empty one
                                              # above it; nothing is written unless every check passes; no git is run
    python3 tools/release.py notes X.Y.Z      # print that version's CHANGELOG section (the release notes)
    python3 tools/release.py --root DIR ...   # another repository root (the test's fixture)

installer/VERSION is the single source (the installer and the portable zip read it). The two CMake projects are copied to
and built on the player's PC, so they cannot read that file: each keeps a literal VERSION in its project() call, which
this tool rewrites. 0.0.1 is the first, unreleased version: it needs no CHANGELOG section of its own (everything is under
Unreleased); from the first bump on, every version in installer/VERSION has one.

Docs that name the current release mark it with a span on one line: `<!-- version -->...<!-- /version -->`. --check
requires every X.Y.Z inside a span to equal installer/VERSION, and bump rewrites them, so a span may hold a whole link
(`<!-- version -->[0.2.0](.../releases/tag/v0.2.0)<!-- /version -->`). Every *.md file except CHANGELOG.md is scanned;
text outside the spans (history such as "since 0.1.0") is left alone.

.recomp.json (the project's entry for recomp.fyi) carries the version in its extension field "x-version"; --check requires
it to equal installer/VERSION and bump rewrites it. From 1.0.0 on, bump also sets "status" from "exploring" or
"in-progress" to "released" (the schema's "public build, still maintained"); 0.x versions are pre-releases and leave it.
A repository without .recomp.json is not checked.
"""
import argparse
import datetime
import pathlib
import re
import sys

VERSION_FILE = "installer/VERSION"
CMAKE_FILES = ("installer/port/jsp/CMakeLists.txt", "jstars/port/CMakeLists.txt")
CHANGELOG = "CHANGELOG.md"
FIRST = "0.0.1"   # the pre-release version that needs no section
PLAIN = re.compile(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
CMAKE_VERSION = re.compile(r"(project\(\s*\w+\s+VERSION\s+)(\S+)")
HEADING = re.compile(r"^## .*$", re.M)
UNRELEASED = "## [Unreleased]"
MARKER = re.compile(r"(<!-- version -->)(.*?)(<!-- /version -->)")
OPEN_MARKER = "<!-- version -->"
ANY_VERSION = re.compile(r"(?<![\d.])\d+\.\d+\.\d+(?![\d.])")
RECOMP_JSON = ".recomp.json"
RECOMP_VERSION = re.compile(r'("x-version"\s*:\s*")([^"]*)(")')
RECOMP_STATUS = re.compile(r'("status"\s*:\s*")(exploring|in-progress)(")')
SKIP_DIRS = {".git", "node_modules", "build", "work", "vendor"}


def parse(v):
    m = PLAIN.match(v)
    return tuple(int(x) for x in m.groups()) if m else None


def read(root, rel):
    return (root / rel).read_text(encoding="utf-8")


def cmake_version(text):
    m = CMAKE_VERSION.search(text)
    return m.group(2) if m else None


def sections(text):
    """[(heading line, body)] in file order; the text before the first '## ' is skipped."""
    marks = list(HEADING.finditer(text))
    out = []
    for i, m in enumerate(marks):
        end = marks[i + 1].start() if i + 1 < len(marks) else len(text)
        out.append((m.group(0).rstrip(), text[m.end():end]))
    return out


def find_section(text, version):
    for head, body in sections(text):
        if head == f"## [{version}]" or head.startswith(f"## [{version}] "):
            return body.strip()
    return None


def doc_files(root):
    """Every *.md file that may hold version spans (CHANGELOG.md keeps its history and is never rewritten)."""
    out = []
    for p in sorted(root.rglob("*.md")):
        rel = p.relative_to(root)
        if rel.as_posix() == CHANGELOG or any(part in SKIP_DIRS for part in rel.parts[:-1]):
            continue
        out.append(rel.as_posix())
    return out


def span_problems(rel, text, version):
    problems = []
    for n, line in enumerate(text.splitlines(), 1):
        spans = MARKER.findall(line)
        if line.count(OPEN_MARKER) != len(spans):
            problems.append(f"{rel}:{n}: a version span must open and close on one line")
        for _, inner, _ in spans:
            found = ANY_VERSION.findall(inner)
            if not found:
                problems.append(f"{rel}:{n}: a version span holds no X.Y.Z")
            for v in found:
                if v != version:
                    problems.append(f"{rel}:{n}: version span says {v}, {VERSION_FILE} is {version}")
    return problems


def fill_spans(text, version):
    return MARKER.sub(lambda m: m.group(1) + ANY_VERSION.sub(version, m.group(2)) + m.group(3), text)


def check(root):
    problems = []
    try:
        version = read(root, VERSION_FILE).strip()
    except OSError:
        return [f"{VERSION_FILE}: missing"]
    if parse(version) is None:
        problems.append(f"{VERSION_FILE}: {version!r} is not a plain X.Y.Z")
    for rel in CMAKE_FILES:
        try:
            found = cmake_version(read(root, rel))
        except OSError:
            problems.append(f"{rel}: missing")
            continue
        if found is None:
            problems.append(f"{rel}: no project(<name> VERSION x.y.z) line")
        elif found != version:
            problems.append(f"{rel}: project VERSION is {found}, {VERSION_FILE} is {version}")
    try:
        log = read(root, CHANGELOG)
    except OSError:
        problems.append(f"{CHANGELOG}: missing")
        return problems
    heads = sections(log)
    if not heads or heads[0][0] != UNRELEASED:
        problems.append(f"{CHANGELOG}: the first section must be '{UNRELEASED}'")
    if version != FIRST and find_section(log, version) is None:
        problems.append(f"{CHANGELOG}: no '## [{version}]' section for the current version")
    for rel in doc_files(root):
        problems += span_problems(rel, read(root, rel), version)
    if (root / RECOMP_JSON).exists():
        m = RECOMP_VERSION.search(read(root, RECOMP_JSON))
        if m is None:
            problems.append(f'{RECOMP_JSON}: no "x-version" field')
        elif m.group(2) != version:
            problems.append(f'{RECOMP_JSON}: x-version is {m.group(2)}, {VERSION_FILE} is {version}')
    return problems


def next_version(current, arg):
    cur = parse(current)
    if arg in ("major", "minor", "patch"):
        i = ("major", "minor", "patch").index(arg)
        new = list(cur[:i]) + [cur[i] + 1] + [0] * (2 - i)
        return ".".join(map(str, new))
    return arg


def bump(root, arg, today=None):
    """Return (changed files, error). Nothing is written when there is an error."""
    problems = check(root)
    if problems:
        return [], problems
    current = read(root, VERSION_FILE).strip()
    new = next_version(current, arg)
    if parse(new) is None:
        return [], [f"{new!r} is not X.Y.Z, major, minor or patch"]
    if parse(new) <= parse(current):
        return [], [f"{new} is not greater than the current version {current}"]
    log = read(root, CHANGELOG)
    head, body = sections(log)[0]
    if not body.strip():
        return [], [f"{CHANGELOG}: the Unreleased section is empty; list the user-visible changes first"]
    today = today or datetime.datetime.now(datetime.timezone.utc).date().isoformat()
    # Every output is built before the first write.
    start = log.index(UNRELEASED)
    out = {VERSION_FILE: new + "\n",
           CHANGELOG: log[:start] + f"{UNRELEASED}\n\n## [{new}] - {today}" + log[start + len(UNRELEASED):]}
    for rel in CMAKE_FILES:
        out[rel] = CMAKE_VERSION.sub(lambda m: m.group(1) + new, read(root, rel), count=1)
    for rel in doc_files(root):
        text = read(root, rel)
        filled = fill_spans(text, new)
        if filled != text:
            out[rel] = filled
    if (root / RECOMP_JSON).exists():
        text = RECOMP_VERSION.sub(lambda m: m.group(1) + new + m.group(3), read(root, RECOMP_JSON), count=1)
        if parse(new)[0] >= 1:
            text = RECOMP_STATUS.sub(lambda m: m.group(1) + "released" + m.group(3), text, count=1)
        out[RECOMP_JSON] = text
    for rel, text in out.items():
        (root / rel).write_text(text, encoding="utf-8", newline="")
    return list(out), []


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parent.parent))
    ap.add_argument("--check", action="store_true")
    ap.add_argument("command", nargs="?", choices=("bump", "notes"))
    ap.add_argument("arg", nargs="?")
    a = ap.parse_args(argv)
    root = pathlib.Path(a.root)
    if a.check or not a.command:
        problems = check(root)
        for p in problems:
            print("release: " + p)
        if problems:
            return 1
        print(f"release: OK ({read(root, VERSION_FILE).strip()})")
        return 0
    if not a.arg:
        ap.error(f"{a.command} needs a version")
    if a.command == "notes":
        body = find_section(read(root, CHANGELOG), a.arg)
        if body is None:
            print(f"release: no '## [{a.arg}]' section in {CHANGELOG}")
            return 1
        print(body)
        return 0
    changed, errors = bump(root, a.arg)
    for e in errors:
        print("release: " + e)
    if errors:
        return 1
    new = read(root, VERSION_FILE).strip()
    print(f"release: bumped to {new}; changed:")
    for rel in changed:
        print("  " + rel)
    print(f"Next: git add {' '.join(changed)} && git commit -m 'Release {new}', then merge it to main by pull request.")
    print(f"The release workflow then tags v{new} and drafts the release; failed builds can retry the same-commit tag until a release exists.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
