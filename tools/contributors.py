#!/usr/bin/env python3
"""CONTRIBUTORS.md: the generated Activity and Play testers blocks, from merged pull requests and play-test issues.

    python3 tools/contributors.py --fetch --data data.json   # read the repository with the gh CLI, write data.json
    python3 tools/contributors.py --data data.json           # rewrite the two generated blocks in CONTRIBUTORS.md
    python3 tools/contributors.py --check                    # exit 1 when a marker is missing or a block is not
                                                             # valid generated text (no network)
    python3 tools/contributors.py --root DIR                 # another repository root (the test's fixture)

Only the text between the marker pairs is rewritten; every other line of CONTRIBUTORS.md is kept as it is:

    <!-- contributors:start --> ... <!-- contributors:end -->    one line per author of merged pull requests
    <!-- playtesters:start --> ... <!-- playtesters:end -->      authors of play-test issues who ticked the consent box

Data format (JSON): {"pulls": [{number, title, author, merged_at, labels}], "issues": [{number, author, labels,
body}]}. author is a login (or an object with "login"); labels are names (or objects with "name"). Bots (logins
ending in [bot] or starting with app/) are not listed. The output is deterministic: the same data gives the same bytes.
"""
import argparse
import json
import pathlib
import re
import subprocess
import sys

PAGE = "CONTRIBUTORS.md"
CONSENT = re.compile(r"^\s*[-*]\s*\[[xX]\]\s*List me as a play tester", re.MULTILINE)
NONE = "- None yet."
NEWEST = 3
BLOCKS = {
    "contributors": re.compile(r"- \[[^\]\n]+\]\(https://github\.com/[^)\n]+\): \d+ merged pull requests?, "
                               r"most recent #\d+(, #\d+){0,%d}$" % (NEWEST - 1)),
    "playtesters": re.compile(r"- \[[^\]\n]+\]\(https://github\.com/[^)\n]+\): \d+ play-test reports? "
                              r"\(#\d+(, #\d+)*\)$"),
}


class Error(Exception):
    pass


def login(who):
    if isinstance(who, dict):
        who = who.get("login")
    return who if isinstance(who, str) and who else None


def is_bot(name):
    return name.endswith("[bot]") or name.startswith("app/")


def label_names(labels):
    return {l.get("name") if isinstance(l, dict) else l for l in labels or []}


def plural(n, word):
    return "%d %s%s" % (n, word, "" if n == 1 else "s")


def contributors_block(pulls):
    by = {}
    for p in pulls:
        name = login(p.get("author"))
        if name and not is_bot(name):
            by.setdefault(name, []).append(p)
    lines = []
    for name in sorted(by, key=lambda n: (-len(by[n]), n.lower(), n)):
        newest = sorted(by[name], key=lambda p: (p.get("merged_at") or "", p["number"]), reverse=True)[:NEWEST]
        recent = ", ".join("#%d" % p["number"] for p in newest)
        lines.append("- [%s](https://github.com/%s): %s, most recent %s"
                     % (name, name, plural(len(by[name]), "merged pull request"), recent))
    return lines or [NONE]


def playtesters_block(issues):
    by = {}
    for i in issues:
        name = login(i.get("author"))
        if (name and not is_bot(name) and "playtest" in label_names(i.get("labels"))
                and CONSENT.search(i.get("body") or "")):
            by.setdefault(name, []).append(i["number"])
    lines = []
    for name in sorted(by, key=lambda n: (-len(by[n]), n.lower(), n)):
        nums = ", ".join("#%d" % n for n in sorted(by[name]))
        lines.append("- [%s](https://github.com/%s): %s (%s)" % (name, name, plural(len(by[name]), "play-test report"), nums))
    return lines or [NONE]


def span(text, key):
    start, end = "<!-- %s:start -->" % key, "<!-- %s:end -->" % key
    if text.count(start) != 1 or text.count(end) != 1:
        raise Error("%s needs exactly one %s and one %s" % (PAGE, start, end))
    a = text.index(start) + len(start)
    b = text.index(end)
    if b < a:
        raise Error("%s: %s comes before %s" % (PAGE, end, start))
    return a, b


def render(text, data):
    new = {"contributors": contributors_block(data.get("pulls", [])),
           "playtesters": playtesters_block(data.get("issues", []))}
    for key, lines in new.items():
        a, b = span(text, key)
        text = text[:a] + "\n" + "\n".join(lines) + "\n" + text[b:]
    return text


def check(text):
    errors = []
    for key, rx in BLOCKS.items():
        try:
            a, b = span(text, key)
        except Error as e:
            errors.append(str(e))
            continue
        body = text[a:b]
        if not (body.startswith("\n") and body.endswith("\n")) or not body.strip():
            errors.append("%s block must be on its own lines between the markers" % key)
            continue
        for line in body.strip("\n").split("\n"):
            if line != NONE and not rx.match(line):
                errors.append("%s block: not generated text: %r" % (key, line))
    return errors


def fetch(repo):
    def gh(*args):
        cmd = ["gh", *args] + (["-R", repo] if repo else [])
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, check=True)
        except (OSError, subprocess.CalledProcessError) as e:
            raise Error("gh failed (%s): %s" % (" ".join(cmd[:3]), getattr(e, "stderr", None) or e))
        return json.loads(r.stdout or "[]")
    pulls = [{"number": p["number"], "title": p["title"], "author": login(p.get("author")),
              "merged_at": p.get("mergedAt"), "labels": sorted(label_names(p.get("labels")))}
             for p in gh("pr", "list", "--state", "merged", "--limit", "1000",
                         "--json", "number,title,author,mergedAt,labels")]
    issues = [{"number": i["number"], "author": login(i.get("author")), "labels": sorted(label_names(i.get("labels"))),
               "body": i.get("body") or ""}
              for i in gh("issue", "list", "--label", "playtest", "--state", "all", "--limit", "1000",
                          "--json", "number,author,labels,body")]
    return {"pulls": sorted(pulls, key=lambda p: p["number"]), "issues": sorted(issues, key=lambda i: i["number"])}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parent.parent))
    ap.add_argument("--data", help="JSON file: input of the generator, or the output of --fetch")
    ap.add_argument("--fetch", action="store_true", help="write --data from the repository with the gh CLI")
    ap.add_argument("--repo", help="OWNER/NAME for --fetch (default: the gh default for the checkout)")
    ap.add_argument("--check", action="store_true", help="validate the markers and blocks only")
    args = ap.parse_args(argv)
    page = pathlib.Path(args.root) / PAGE
    try:
        if args.fetch:
            if not args.data:
                raise Error("--fetch needs --data FILE to write")
            pathlib.Path(args.data).write_text(json.dumps(fetch(args.repo), indent=1, sort_keys=True) + "\n",
                                               encoding="utf-8")
            return 0
        if not page.is_file():
            raise Error("%s not found" % page)
        text = page.read_text(encoding="utf-8")
        if args.check:
            errors = check(text)
            for e in errors:
                print("contributors: " + e, file=sys.stderr)
            return 1 if errors else 0
        if not args.data:
            raise Error("give --data FILE (or --check)")
        try:
            data = json.loads(pathlib.Path(args.data).read_text(encoding="utf-8"))
            out = render(text, data)
        except (OSError, ValueError, KeyError, TypeError, AttributeError) as e:
            raise Error("bad data file %s: %r" % (args.data, e))
        if out != text:
            page.write_text(out, encoding="utf-8")
        return 0
    except Error as e:
        print("contributors: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
