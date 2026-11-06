#!/usr/bin/env python3
"""Tests for tools/contributors.py on fixture data and a fixture CONTRIBUTORS.md (no network).

    python3 tools/test_contributors.py
"""
import contextlib
import io
import json
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import contributors  # noqa: E402

PAGE = """# Contributors

- [hand](https://github.com/hand): written by hand.

## Activity (generated)

<!-- contributors:start -->
- None yet.
<!-- contributors:end -->

Hand-written text between the blocks.

## Play testers

<!-- playtesters:start -->
- None yet.
<!-- playtesters:end -->

- [also-hand](https://github.com/also-hand): last line, by hand.
"""
BOX = "### Listing\n\n- [x] List me as a play tester in CONTRIBUTORS.md (your GitHub name only)\n"
NOBOX = "### Listing\n\n- [ ] List me as a play tester in CONTRIBUTORS.md (your GitHub name only)\n"


def pr(n, who, when):
    return {"number": n, "title": "t%d" % n, "author": who, "merged_at": when, "labels": []}


def issue(n, who, body, labels=("playtest",)):
    return {"number": n, "author": who, "labels": list(labels), "body": body}


PULLS = [pr(1, "bob", "2026-01-01T00:00:00Z"), pr(2, "alice", "2026-01-02T00:00:00Z"),
         pr(3, "alice", "2026-01-03T00:00:00Z"), pr(4, "alice", "2026-01-04T00:00:00Z"),
         pr(5, "alice", "2026-01-05T00:00:00Z"), pr(6, "ci[bot]", "2026-01-06T00:00:00Z"),
         pr(7, "app/robot", "2026-01-07T00:00:00Z"), pr(8, "carol", "2026-01-08T00:00:00Z"),
         pr(9, {"login": "bob"}, "2026-01-09T00:00:00Z")]

CASES = 0


def case(ok, what):
    global CASES
    CASES += 1
    if not ok:
        print("FAIL: " + what)
        sys.exit(1)


def run(root, data=None, *extra):
    args = ["--root", str(root)] + ([] if data is None else ["--data", str(data)]) + list(extra)
    err = io.StringIO()
    with contextlib.redirect_stderr(err):
        code = contributors.main(args)
    return code, err.getvalue()


def main():
    with tempfile.TemporaryDirectory() as t:
        root = pathlib.Path(t)
        page = root / "CONTRIBUTORS.md"
        data = root / "data.json"
        page.write_text(PAGE, encoding="utf-8")
        data.write_text(json.dumps({"pulls": PULLS, "issues": [
            issue(10, "dave", BOX), issue(11, "dave", BOX), issue(12, "erin", NOBOX), issue(13, "frank", BOX, ["bug"]),
            issue(14, "gina", BOX), issue(15, "gina", NOBOX)]}), encoding="utf-8")
        case(run(root, data)[0] == 0, "generator runs")
        out = page.read_text(encoding="utf-8")
        lines = out.split("\n")
        case("- [hand](https://github.com/hand): written by hand." in lines
             and "Hand-written text between the blocks." in lines
             and "- [also-hand](https://github.com/also-hand): last line, by hand." in lines, "hand lines kept")
        case("ci[bot]" not in out and "robot" not in out, "bots skipped")
        a, b = contributors.span(out, "contributors")
        block = out[a:b].strip("\n").split("\n")
        case(block == [
            "- [alice](https://github.com/alice): 4 merged pull requests, most recent #5, #4, #3",
            "- [bob](https://github.com/bob): 2 merged pull requests, most recent #9, #1",
            "- [carol](https://github.com/carol): 1 merged pull request, most recent #8"],
            "sorting by count then name, newest three pull requests: %r" % block)
        a, b = contributors.span(out, "playtesters")
        block = out[a:b].strip("\n").split("\n")
        case(block == ["- [dave](https://github.com/dave): 2 play-test reports (#10, #11)",
                       "- [gina](https://github.com/gina): 1 play-test report (#14)"],
             "consent required, label required: %r" % block)
        case(run(root, None, "--check")[0] == 0, "generated page passes --check")
        case(run(root, data)[0] == 0 and page.read_text(encoding="utf-8") == out, "second run is idempotent")

        data.write_text(json.dumps({"pulls": [], "issues": [issue(1, "erin", NOBOX)]}), encoding="utf-8")
        run(root, data)
        out = page.read_text(encoding="utf-8")
        a, b = contributors.span(out, "playtesters")
        case(out[a:b] == "\n- None yet.\n", "no play testers")
        case(run(root, None, "--check")[0] == 0, "None yet passes --check")

        page.write_text(PAGE.replace("<!-- playtesters:end -->\n", ""), encoding="utf-8")
        code, err = run(root, data)
        case(code == 1 and "playtesters" in err, "missing marker is an error (generator)")
        case(run(root, None, "--check")[0] == 1, "missing marker is an error (--check)")
        page.write_text(PAGE.replace("- None yet.\n<!-- contributors:end", "- made up by hand\n<!-- contributors:end"),
                        encoding="utf-8")
        case(run(root, None, "--check")[0] == 1, "hand-edited block fails --check")
    print("test_contributors: ok (%d cases)" % CASES)


if __name__ == "__main__":
    main()
