#!/usr/bin/env python3
"""Checks of the generated fan site (_site/: index.html, plain.html, style.css, plain.css) and of tools/site/site.js.

  1. both pages are well-formed HTML (every tag closed in order, ids unique), no inline event handler;
  2. every #fragment resolves on its page; every relative link and image resolves to a file in the site, with the
     fragment of an other-page link (index.html#x) present there;
  3. nothing loads from outside: no external script, stylesheet or image, no @import or url() in the CSS; index.html
     has exactly one inline script, equal to tools/site/site.js and allowed by its sha256 in the page's CSP; plain.html
     has none;
  4. plain.html and index.html agree: the same sections in the same order with the same headings, and the same set of
     links inside them;
  5. the script: no network, storage or eval calls, no outside URL, balanced brackets, `node --check` when node is on
     PATH (SKIP otherwise, said on stdout);
  6. every class used in index.html has a selector in style.css, and every class of plain.html one in plain.css
     (NO_RULE_OK lists the hooks only the script uses);
  7. every link button of data/nav.json (the README's buttons) is a link on both pages, so none can be dropped;
  8. every text colour pair the palette declares (data/theme.json "pairs") meets its contrast minimum.

Carried from Qwark's Awakening's tools/release/check_site.py, itself from Project Helghast's (same author, MIT): the
page parser, the cross-page agreement rules, the script checks and the class-to-selector rule; adapted here to the
generated _site/ layout, the palette contrast check and the nav button coverage.

    python3 -I tools/site/check_site.py [SITE_DIR [SRC_DIR]]     # defaults: _site and the repository
"""
import base64
import hashlib
import importlib.util
import json
import html.parser
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
NO_RULE_OK = {"sec"}   # .sec is styled through html.js .sec and .sec + .sec; listed so a rename is noticed
VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "track", "wbr"}


class Page(html.parser.HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.stack, self.problems, self.ids, self.refs, self.scripts, self.links = [], [], [], [], [], []
        self.script_text = []    # the text of each inline <script>
        self.secs = []           # [(id, heading text)] of top-level section.sec
        self.classes = set()     # every class used in the markup
        self.metas = []
        self.sec_hrefs = {}      # section id -> set of hrefs
        self._sec = None
        self._h2 = None

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        self.classes.update((a.get("class") or "").split())
        if tag == "meta":
            self.metas.append(a)
        if "id" in a:
            self.ids.append(a["id"])
        for k in a:
            if k.startswith("on"):
                self.problems.append(f"inline event handler {k} on <{tag}>")
        for k in ("href", "src"):
            if k in a and a[k] is not None:
                self.refs.append((tag, k, a[k]))
                if self._sec and tag == "a" and k == "href":
                    self.sec_hrefs.setdefault(self._sec, set()).add(a[k])
        if tag == "link":
            self.links.append(a)
        if tag == "script":
            self.scripts.append(a)
            self.script_text.append("")
        if tag == "section" and "sec" in (a.get("class") or "").split():
            self._sec = a.get("id")
            self.secs.append([self._sec, ""])
        if tag == "h2" and self._sec and a.get("id", "").startswith("h-"):
            self._h2 = []
        if tag not in VOID:
            self.stack.append(tag)

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if tag not in VOID and self.stack and self.stack[-1] == tag:
            self.stack.pop()

    def handle_endtag(self, tag):
        if tag in VOID:
            return
        if not self.stack or self.stack[-1] != tag:
            self.problems.append(f"</{tag}> closes {self.stack[-1] if self.stack else 'nothing'}")
            if tag in self.stack:
                while self.stack and self.stack.pop() != tag:
                    pass
            return
        self.stack.pop()
        if tag == "h2" and self._h2 is not None:
            self.secs[-1][1] = "".join(self._h2).strip()
            self._h2 = None
        if tag == "section" and self._sec and "section" not in self.stack:
            self._sec = None

    def handle_data(self, data):
        if self.stack and self.stack[-1] == "script" and self.script_text:
            self.script_text[-1] += data
        if self._h2 is not None:
            self._h2.append(data)

    def close(self):
        super().close()
        if self.stack:
            self.problems.append(f"unclosed tags at the end: {self.stack[-3:]}")
        dup = {i for i in self.ids if self.ids.count(i) > 1}
        if dup:
            self.problems.append(f"duplicate ids: {sorted(dup)[:5]}")


def parse(path):
    p = Page()
    p.feed(path.read_text(encoding="utf-8"))
    p.close()
    return p


def is_external(u):
    return bool(re.match(r"[a-z][a-z0-9+.-]*:|//", u, re.I))


def check_page(path, pages, problems):
    name = path.name
    pg = pages[name]
    problems += [f"{name}: {x}" for x in pg.problems]
    ids = set(pg.ids)
    for tag, k, u in pg.refs:
        if u.startswith("#"):
            if len(u) > 1 and u[1:] not in ids:
                problems.append(f"{name}: #{u[1:]} has no target")
        elif is_external(u):
            if tag in ("link", "script", "img", "iframe", "source"):
                problems.append(f"{name}: <{tag}> loads {u}")
        else:
            target, _, frag = u.partition("#")
            f = (path.parent / target).resolve() if target else path
            if not f.is_file():
                problems.append(f"{name}: {u} does not resolve to a file")
            elif frag and f.suffix == ".html":
                other = pages.get(f.name) or parse(f)
                if frag not in other.ids:
                    problems.append(f"{name}: {u} has no target in {f.name}")
    for sc in pg.scripts:
        if sc.get("src"):
            problems.append(f"{name}: script loaded from a file or site ({sc['src']}): the script is inline")
    want = 1 if name == "index.html" else 0
    if len(pg.scripts) != want:
        problems.append(f"{name}: {len(pg.scripts)} scripts (want {want})")
    for ln in pg.links:
        if "stylesheet" in (ln.get("rel") or "") and is_external(ln.get("href", "")):
            problems.append(f"{name}: external stylesheet {ln.get('href')}")


def check_css(path, problems):
    t = re.sub(r"/\*.*?\*/", "", path.read_text(encoding="utf-8"), flags=re.S)
    if re.search(r"@import", t):
        problems.append(f"{path.name}: @import")
    for m in re.finditer(r"url\(\s*['\"]?([^)'\"]+)", t):
        u = m.group(1)
        problems.append(f"{path.name}: url({u[:40]}) (the pages draw everything in CSS; no url())")
    if path.name == "style.css":
        for need in ("prefers-reduced-motion", ":focus-visible", "prefers-color-scheme: dark", "max-width: 860px"):
            if need not in t:
                problems.append(f"style.css: lacks {need}")


JS_BAD = re.compile(r"\b(fetch|XMLHttpRequest|sendBeacon|WebSocket|EventSource|eval|localStorage|sessionStorage|indexedDB|importScripts)\b"
                    r"|document\.(write|cookie)|new Function|https?://|<script", re.I)


def strip_js(t):
    """Source without comments and string/template contents (for the checks below; regex literals are left as they are)."""
    out, i, n = [], 0, len(t)
    while i < n:
        c = t[i]
        if t.startswith("/*", i):
            j = t.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif t.startswith("//", i) and (i == 0 or t[i - 1] != ":"):
            j = t.find("\n", i)
            i = n if j < 0 else j
        elif c in "'\"`":
            j = i + 1
            while j < n and t[j] != c:
                j += 2 if t[j] == "\\" else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def check_js(label, raw, problems, notes):
    bare = strip_js(raw)
    nocomment = re.sub(r"(?<![:\w])//[^\n]*", "", re.sub(r"/\*.*?\*/", "", raw, flags=re.S))   # strings stay: a URL in one counts
    m = JS_BAD.search(bare) or re.search(r"https?://|<script", nocomment)
    if m:
        problems.append(f"{label}: forbidden {m.group(0)!r}")
    pairs, stack = {")": "(", "]": "[", "}": "{"}, []
    for ch in re.sub(r"/\^?[^/\n]*\$?/[gimsuy]*(?=[.,;)])", "", bare):   # drop simple regex literals
        if ch in "([{":
            stack.append(ch)
        elif ch in ")]}":
            if not stack or stack.pop() != pairs[ch]:
                problems.append(f"{label}: brackets do not balance")
                break
    else:
        if stack:
            problems.append(f"{label}: brackets do not balance (unclosed)")
    node = shutil.which("node")
    if node:
        with tempfile.TemporaryDirectory() as d:
            f = pathlib.Path(d) / "x.js"
            f.write_text(raw, encoding="utf-8")
            r = subprocess.run([node, "--check", str(f)], capture_output=True, text=True)
        if r.returncode:
            problems.append(f"{label}: node --check: " + r.stderr.strip().splitlines()[-1][:150])
        else:
            notes.append(f"node --check {label}: OK")
    else:
        notes.append(f"SKIP node --check {label} (node is not on PATH; bracket-balance and forbidden-call checks ran)")


def selectors(css_path):
    css = re.sub(r"/\*.*?\*/", "", css_path.read_text(encoding="utf-8"), flags=re.S)
    css = re.sub(r"\{[^{}]*\}", "{}", css)   # declarations out (a value like 1.5rem is not a class)
    return set(re.findall(r"\.([A-Za-z_][\w-]*)", css))


def _site_mod():
    sp = importlib.util.spec_from_file_location("site_gen", HERE / "site.py")
    m = importlib.util.module_from_spec(sp)
    sp.loader.exec_module(m)
    return m


def check(site, src=None, js=None):
    """-> (problems, notes) for the generated site directory, the repository it came from (src) and site.js."""
    site = pathlib.Path(site).resolve()
    src = pathlib.Path(src).resolve() if src else ROOT
    js = pathlib.Path(js) if js else HERE / "site.js"
    problems, notes = [], []
    for f in ("index.html", "plain.html", "style.css", "plain.css"):
        if not (site / f).is_file():
            problems.append(f"{f} missing")
    if not js.is_file():
        problems.append(f"{js.name} missing")
    if problems:
        return problems, notes
    pages = {f: parse(site / f) for f in ("index.html", "plain.html")}
    check_page(site / "index.html", pages, problems)
    check_page(site / "plain.html", pages, problems)
    for f in ("style.css", "plain.css"):
        check_css(site / f, problems)
    raw = js.read_text(encoding="utf-8")
    check_js(js.name, raw, problems, notes)
    a, b = pages["index.html"], pages["plain.html"]
    if a.script_text and a.script_text[0] != raw:
        problems.append("index.html's inline script is not tools/site/site.js")
    sha = "'sha256-" + base64.b64encode(hashlib.sha256(raw.encode("utf-8")).digest()).decode() + "'"
    for name, pg, want in (("index.html", a, True), ("plain.html", b, False)):
        csp = [m.get("content", "") for m in pg.metas if (m.get("http-equiv") or "").lower() == "content-security-policy"]
        if not csp:
            problems.append(f"{name}: no Content-Security-Policy")
            continue
        c = csp[0]
        for need in ("default-src 'none'", "base-uri 'none'", "form-action 'none'"):
            if need not in c:
                problems.append(f"{name}: CSP lacks {need}")
        if "unsafe-inline" in c or "unsafe-eval" in c:
            problems.append(f"{name}: CSP allows unsafe-*")
        if want and sha not in c:
            problems.append(f"{name}: CSP does not allow site.js by its hash")
        if not want and "script-src" in c:
            problems.append(f"{name}: CSP allows scripts")
    for page, pg, css in (("index.html", a, "style.css"), ("plain.html", b, "plain.css")):
        have = selectors(site / css)
        for c in sorted(pg.classes - have - NO_RULE_OK):
            problems.append(f"class {c!r} is used in {page} but {css} has no selector for it")
    if a.secs != b.secs:
        problems.append(f"sections differ: index {[s[0] for s in a.secs]} vs plain {[s[0] for s in b.secs]} (or their headings)")
    if not a.secs:
        problems.append("no sections")
    for sid, _h in a.secs:
        if a.sec_hrefs.get(sid, set()) != b.sec_hrefs.get(sid, set()):
            d = a.sec_hrefs.get(sid, set()) ^ b.sec_hrefs.get(sid, set())
            problems.append(f"links in section {sid} differ between the pages: {sorted(d)[:3]}")
    if "plain.html" not in {u for _t, _k, u in a.refs}:
        problems.append("index.html does not link plain.html")
    if "index.html" not in {u.split("#")[0] for _t, _k, u in b.refs}:
        problems.append("plain.html does not link back to index.html")
    nav = {u[1:] for t, k, u in a.refs if u.startswith("#") and len(u) > 1}
    for sid, _h in a.secs:
        if sid not in nav:
            problems.append(f"section {sid} has no link to it on the index page")
    # 7. the README's buttons: every one on both pages
    navj = json.loads((src / "data" / "nav.json").read_text(encoding="utf-8"))
    fix = _site_mod().LINKS.fix
    for btn in navj["buttons"] + [navj["wide"]]:
        want = fix(btn["href"])
        for page, pg in (("index.html", a), ("plain.html", b)):
            if want not in {u for t, k, u in pg.refs if t == "a"}:
                problems.append(f"{page}: the button {btn['label']!r} ({want}) is missing")
    # 8. palette contrast
    sp = importlib.util.spec_from_file_location("theme_chk", HERE / "theme.py")
    th = importlib.util.module_from_spec(sp)
    sp.loader.exec_module(th)
    for fg, bg, r, mn in th.load(src / "data" / "theme.json").pair_ratios():
        if r < mn:
            problems.append(f"contrast {fg} on {bg} is {r} (minimum {mn})")
    notes.append(f"pages agree on {len(a.secs)} sections; {len(navj['buttons']) + 1} buttons on both pages; "
                 f"{len(a.classes)} classes styled; palette pairs checked")
    return problems, notes


def main(argv):
    site = pathlib.Path(argv[1]) if len(argv) > 1 else ROOT / "_site"
    src = pathlib.Path(argv[2]) if len(argv) > 2 else ROOT
    problems, notes = check(site, src)
    for n in notes:
        print(n)
    if problems:
        print("check_site: problems:")
        for p in problems:
            print("  - " + p)
        return 1
    print(f"check_site: OK ({site})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
