#!/usr/bin/env python3
"""The README's badge row and menu buttons, from the palette (data/theme.json) and the button list (data/nav.json).

    python3 -I tools/site/badges.py           # rewrite the blocks between the markers in README.md
    python3 -I tools/site/badges.py --check   # exit 1 when README.md's blocks differ from what this would write

Blocks: <!-- badges:start --> ... <!-- badges:end --> (shields.io badges in the palette's ink and red; the Discord
badge keeps Discord's own colour) and <!-- nav:start --> ... <!-- nav:end --> (the drawn buttons, three per row at
250 px, the wide Discord button at 758 px, then a plain row of section links). The button images are drawn by
tools/site/repo_art.py. Stdlib only.
"""
import argparse
import importlib.util
import json
import pathlib
import re
import sys
import urllib.parse

ROOT = pathlib.Path(__file__).resolve().parents[2]
REPO = "Tsamiuy/jsp-vs-ex"


def _load(name):
    sp = importlib.util.spec_from_file_location(name, pathlib.Path(__file__).with_name(name + ".py"))
    m = importlib.util.module_from_spec(sp)
    sp.loader.exec_module(m)
    return m


theme = _load("theme")

# (label, value or None for a dynamic shields path, link, kind) in README order
SECTION_LINKS = [("Status", "#status"), ("What you need", "#download-and-play"),
                 ("Online and your data", "#what-goes-online-and-where-your-data-goes"),
                 ("Settings and controllers", "#settings-and-controllers"), ("How it works", "#how-it-works"),
                 ("Roadmap", "#roadmap"), ("How AI was used", "#how-ai-was-used"),
                 ("Licence", "#licence-and-disclaimer"), ("Credits", "#related-projects-and-credits"),
                 ("Fan site", "https://tsamiuy.github.io/jsp-vs-ex/")]


def _hex(name, t):
    return t.color(name).lstrip("#").upper()


def badges(t):
    lab, val, acc = (_hex(t.data["badges"][k], t) for k in ("label", "value", "accent"))
    q = f"style=flat-square&labelColor={lab}"
    gh = f"https://github.com/{REPO}"

    def st(label, value, color):
        return ("https://img.shields.io/badge/" + urllib.parse.quote(label.replace("-", "--")) + "-"
                + urllib.parse.quote(value.replace("-", "--")) + f"-{color}?{q}")
    row1 = [
        ("latest release", f"https://img.shields.io/github/v/release/{REPO}?include_prereleases&label=release&{q}&color={val}", f"{gh}/releases"),
        ("downloads", f"https://img.shields.io/github/downloads/{REPO}/total?label=downloads&{q}&color={val}", f"{gh}/releases"),
        ("licence: MIT", st("licence", "MIT", acc), "LICENSE"),
        ("last commit", f"https://img.shields.io/github/last-commit/{REPO}?label=last%20commit&{q}&color={val}", f"{gh}/commits/main"),
        ("issues", f"https://img.shields.io/github/issues/{REPO}?label=issues&{q}&color={val}", f"{gh}/issues"),
    ]
    row2 = [
        ("CI", f"https://img.shields.io/github/actions/workflow/status/{REPO}/ci.yml?branch=main&label=CI&{q}&color={acc}", f"{gh}/actions/workflows/ci.yml"),
        ("platform: Windows x64", st("platform", "Windows x64", acc), "#download-and-play"),
        ("status: work in progress", st("status", "work in progress", acc), "#status"),
        ("docs: DeepWiki", st("docs", "DeepWiki", acc), f"https://deepwiki.com/{REPO}"),
        ("Discord", f"https://img.shields.io/badge/Discord-join-5865F2?logo=discord&logoColor=white&{q}", "https://discord.gg/D2eTEnCK62"),
    ]

    def line(row):
        return "\n".join(f'<a href="{h}"><img src="{u}" alt="{a}"></a>' for a, u, h in row)
    return line(row1) + "\n<br>\n" + line(row2)


def nav():
    d = json.loads((ROOT / "data" / "nav.json").read_text(encoding="utf-8"))
    b = d["buttons"]
    rows = []
    for i in range(0, len(b), 3):
        rows.append("\n".join(f'<a href="{x["href"]}"><img src="assets/nav/{x["id"]}.png" alt="{x["label"]}" width="250"></a>'
                              for x in b[i:i + 3]))
    w = d["wide"]
    rows.append(f'<a href="{w["href"]}"><img src="assets/nav/{w["id"]}-wide.png" alt="{w["label"]}" width="758"></a>')
    rows.append("<sub>" + " · ".join(f'<a href="{h}">{l}</a>' for l, h in SECTION_LINKS) + "</sub>")
    return "\n\n".join(rows)


def render(readme, t):
    for name, body in (("badges", badges(t)), ("nav", nav())):
        pat = re.compile(rf"(<!-- {name}:start -->\n)(?:.*?\n)?(<!-- {name}:end -->)", re.S)
        if not pat.search(readme):
            raise SystemExit(f"badges: README.md has no <!-- {name}:start --> ... <!-- {name}:end --> block")
        readme = pat.sub(lambda m: m.group(1) + body + "\n" + m.group(2), readme)
    return readme


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--readme", default=str(ROOT / "README.md"))
    a = ap.parse_args()
    p = pathlib.Path(a.readme)
    old = p.read_text(encoding="utf-8")
    new = render(old, theme.load())
    if a.check:
        print("badges: README blocks " + ("match" if new == old else "differ: run tools/site/badges.py"))
        return 0 if new == old else 1
    p.write_text(new, encoding="utf-8", newline="\n")
    print("badges: README.md " + ("unchanged" if new == old else "updated"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
