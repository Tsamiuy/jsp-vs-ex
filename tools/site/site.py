#!/usr/bin/env python3
"""Fan-site generator, NFO look: README.md + NOTICE.md -> _site/ (index.html, style.css). Stdlib only, no network.

    python3 -I tools/site/site.py --demo            # build from the built-in placeholder sample
    python3 -I tools/site/site.py --demo --check    # ... and refuse a bad page (exit 1)
    python3 -I tools/site/site.py --check           # build from --src (README.md + NOTICE.md) and check

Options: --src DIR (read only) --out DIR --repo URL (--fonts and --images are accepted but unused in this look).
One centred 80-column monospace column on black: an ASCII logo, a banner line, a bracketed nav, then each README
section as an ASCII box (text wrapped at 76 columns) and a final disclaimer block. Phones get the same content as
plain flowing blocks. The only input is README.md and NOTICE.md; the generator adds short UI labels. No JavaScript.
"""
import argparse
import html
import importlib.util
import pathlib
import re
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def _load(name):  # loaded by path: `python3 -I` keeps the script dir off sys.path
    sp = importlib.util.spec_from_file_location(name, pathlib.Path(__file__).with_name(name + ".py"))
    m = importlib.util.module_from_spec(sp)
    sp.loader.exec_module(m)
    return m


ascii_logo = _load("ascii_logo")
NAME = "J-Stars Victory VS-EX"
TAGLINE = "Unofficial fan project. No game files included."
DISCORD = "https://discord.gg/h3j2wFwqvn"
DEFAULT_REPO = "https://github.com/Tsamiuy/jsp-vs-ex"  # ASSUMPTION: final public repo URL; override with --repo
DESCRIPTION = ("J-Stars Victory VS-EX is an unofficial fan project: a PC port of a PS3 crossover fighter, "
               "made by static recompilation. No game files included.")
W, INNER = 80, 76  # box width and text width

# ---------------------------------------------------------------- markdown subset -> character cells

def slug(t):
    return re.sub(r"[^a-z0-9]+", "-", t.lower()).strip("-")


class Ctx:
    repo = DEFAULT_REPO


_TOK = re.compile(r"`([^`]+)`|!\[([^\]]*)\]\(([^)\s]+)\)|\[([^\]]+)\]\(([^)\s]+)\)|<(https?://[^>\s]+)>"
                  r"|\*\*(.+?)\*\*|(?<![\w*])\*([^*]+?)\*(?![\w*])")


def segs(s, href=None, cls=None):
    """Inline markdown -> list of (text, href, cls); cls 'o' marks code. Emphasis markers are dropped."""
    out, pos = [], 0
    for m in _TOK.finditer(s):
        if m.start() > pos:
            out.append((s[pos:m.start()], href, cls))
        pos = m.end()
        if m.group(1) is not None:
            out.append((m.group(1), href, "o"))
        elif m.group(3) is not None:
            out.append((m.group(2) or "image", href, cls))
        elif m.group(5) is not None:
            u = m.group(5)
            if not re.match(r"^(https?:|#|mailto:)", u):
                u = Ctx.repo + "/blob/main/" + u
            out += segs(m.group(4), u, cls)
        elif m.group(6) is not None:
            out.append((m.group(6), m.group(6), cls))
        else:
            out += segs(m.group(7) or m.group(8), href, cls)
    if pos < len(s):
        out.append((s[pos:], href, cls))
    return out


def cells(s):
    """Inline markdown -> list of (char, href, cls) with whitespace collapsed to single spaces."""
    flat = []
    for t, h, c in segs(s.replace("\n", " ")):
        flat += [(ch, h, c) for ch in t]
    return flat


def words(cs):
    ws, cur = [], []
    for c in cs:
        if c[0].isspace():
            if cur:
                ws.append(cur)
                cur = []
        else:
            cur.append(c)
    if cur:
        ws.append(cur)
    return ws


def wrap(cs, width, first="", rest=None):
    """Wrap char cells into lines (lists of cells) of at most `width` columns, with line prefixes."""
    rest = first if rest is None else rest
    lines, cur, pre = [], [], first
    for w in words(cs):
        room = width - len(pre)
        while len(w) > room:  # hard split an over-long word
            if cur:
                lines.append(_pfx(pre, cur))
                cur, pre = [], rest
                room = width - len(pre)
                continue
            lines.append(_pfx(pre, w[:room]))
            w, pre = w[room:], rest
            room = width - len(pre)
        if cur and len(cur) + 1 + len(w) > room:
            lines.append(_pfx(pre, cur))
            cur, pre = [], rest
        cur = cur + [(" ", None, None)] + w if cur else list(w)
    if cur or not lines:
        lines.append(_pfx(pre, cur))
    return lines


def _pfx(pre, cs):
    return [(ch, None, None) for ch in pre] + cs


def run_html(cs):
    """Cells -> HTML, grouping runs of the same link/code class."""
    out, i = [], 0
    while i < len(cs):
        j = i
        while j < len(cs) and cs[j][1:] == cs[i][1:]:
            j += 1
        t = html.escape("".join(c[0] for c in cs[i:j]), quote=False)
        _, h, c = cs[i]
        if c:
            t = '<span class="%s">%s</span>' % (c, t)
        if h:
            t = '<a href="%s">%s</a>' % (html.escape(h), t)
        out.append(t)
        i = j
    return "".join(out)


def blocks(text):
    lines = text.strip("\n").split("\n")
    i = 0
    while i < len(lines):
        ln = lines[i]
        if not ln.strip():
            i += 1
        elif ln.startswith("```"):
            j = i + 1
            while j < len(lines) and not lines[j].startswith("```"):
                j += 1
            yield "code", "\n".join(lines[i + 1:j])
            i = j + 1
        elif ln.startswith("#"):
            m = re.match(r"(#+)\s+(.*)", ln)
            yield "h", m.group(2)
            i += 1
        elif ln.startswith("|"):
            j = i
            while j < len(lines) and lines[j].startswith("|"):
                j += 1
            rows = [[c.strip() for c in r.strip().strip("|").split("|")] for r in lines[i:j]]
            has_head = len(rows) > 1 and all(re.fullmatch(r":?-+:?", c) for c in rows[1])
            rows = [r for r in rows if not all(re.fullmatch(r":?-+:?", c) for c in r)]
            yield "table", (has_head, rows)
            i = j
        elif re.match(r"(- |\d+\. )", ln):
            ordered = bool(re.match(r"\d+\. ", ln))
            items = []
            while i < len(lines) and lines[i].strip() and (re.match(r"(- |\d+\. )", lines[i]) or lines[i].startswith("  ")):
                if lines[i].startswith("  "):
                    items[-1] += " " + lines[i].strip()
                else:
                    items.append(re.sub(r"^(- |\d+\. )", "", lines[i]))
                i += 1
            yield "ol" if ordered else "ul", items
        elif ln.startswith(">"):
            q = []
            while i < len(lines) and lines[i].startswith(">"):
                q.append(lines[i].lstrip("> ").rstrip())
                i += 1
            yield "quote", " ".join(q)
        else:
            p = []
            while i < len(lines) and lines[i].strip() and not re.match(r"(\||- |\d+\. |>|```|#)", lines[i]):
                p.append(lines[i].strip())
                i += 1
            yield "p", " ".join(p)


def sections(md):
    parts = re.split(r"^## (.+)$", md, flags=re.M)
    return parts[0], [(parts[k].strip(), parts[k + 1]) for k in range(1, len(parts), 2)]


def table_lines(has_head, rows):
    """ASCII table (lines of cells) no wider than INNER columns."""
    n = max(len(r) for r in rows)
    rows = [r + [""] * (n - len(r)) for r in rows]
    cc = [[cells(c) for c in r] for r in rows]
    nat = [max(len(cc[r][c]) for r in range(len(cc))) or 1 for c in range(n)]
    low = [min(nat[c], max([len(w) for r in range(len(cc)) for w in words(cc[r][c])] + [3], default=3), 16) for c in range(n)]
    avail = INNER - (3 * n + 1)
    if sum(nat) <= avail:
        wd = nat
    else:
        wd, extra = low[:], avail - sum(low)
        need = [nat[c] - low[c] for c in range(n)]
        tot = sum(need) or 1
        wd = [low[c] + max(0, extra * need[c] // tot) for c in range(n)]
    border = [(ch, None, None) for ch in "+" + "+".join("-" * (w + 2) for w in wd) + "+"]
    sep = [(ch, None, None) for ch in "+" + "+".join("=" * (w + 2) for w in wd) + "+"]
    out = [border]
    for ri, r in enumerate(cc):
        wr = [wrap(r[c], wd[c]) for c in range(n)]
        for k in range(max(len(x) for x in wr)):
            ln = [("|", None, None)]
            for c in range(n):
                part = wr[c][k] if k < len(wr[c]) else []
                ln += [(" ", None, None)] + part + [(" ", None, None)] * (wd[c] - len(part) + 1) + [("|", None, None)]
            out.append(ln)
        out.append(sep if (has_head and ri == 0) else border)
    return out


def block_out(kind, v):
    """One markdown block -> (ascii lines of cells, flat HTML)."""
    if kind == "p":
        return wrap(cells(v), INNER), "<p>%s</p>" % run_html(cells(v))
    if kind in ("ul", "ol"):
        a, f = [], []
        for k, it in enumerate(v):
            pre = "  - " if kind == "ul" else "%3d. " % (k + 1)
            a += wrap(cells(it), INNER, pre, " " * len(pre))
            f.append("<li>%s</li>" % run_html(cells(it)))
        return a, "<%s>%s</%s>" % (kind, "".join(f), kind)
    if kind == "quote":
        return wrap(cells(v), INNER, "> ", "> "), "<blockquote>%s</blockquote>" % run_html(cells(v))
    if kind == "code":
        lines = []
        for ln in v.split("\n"):
            lines += wrap([(ch, None, "o") for ch in ln], INNER, "    ", "    ") if ln.strip() else [[]]
        return lines, "<pre class=\"code\">%s</pre>" % html.escape(v)
    if kind == "h":
        c = [(ch, None, "y") for ch in ":: %s ::" % v]
        return [c], "<h3>%s</h3>" % html.escape(v)
    if kind == "table":
        tl = table_lines(*v)
        return tl, '<div class="tbl" tabindex="0" role="region" aria-label="Table"><pre>%s</pre></div>' % "\n".join(run_html(x) for x in tl)
    return [], ""


def box(title, body_blocks, sid):
    """A section as an ASCII box (desktop) and plain blocks (phones). body_blocks: list of (kind, value)."""
    asc, flat = [], []
    for kind, v in body_blocks:
        a, f = block_out(kind, v)
        if asc:
            asc.append([])
        asc += a
        flat.append(f)
    label = "[ %s ]" % title.upper()
    left = (W - 2 - len(label)) // 2
    top = "+" + "-" * left + label + "-" * (W - 2 - left - len(label)) + "+"
    top_html = '<span class="r">+%s</span><span class="y">%s</span><span class="r">%s+</span>' % (
        "-" * left, html.escape(label), "-" * (W - 2 - left - len(label)))
    rows = []
    for ln in asc:
        rows.append('<span class="r">| </span>%s%s<span class="r"> |</span>' % (run_html(ln), " " * (INNER - len(ln))))
    rows.append('<span class="r">+%s+</span>' % ("-" * (W - 2)))
    idattr = ' id="%s"' % sid if sid else ""
    return ('<section%s class="nfo"><h2 aria-label="%s"><span class="asc">%s</span><span class="flat">%s</span></h2>\n'
            '<pre class="asc">%s</pre>\n<div class="flat">%s</div><!--/flat--></section>' % (
                idattr, html.escape(title), top_html, html.escape(label), "\n".join(rows), "\n".join(flat)))

# ---------------------------------------------------------------- logo and CSS

def cells_html(rows):
    """Rows of (char, tag) as text; the 3D J and VS-EX get their own class (bold), everything else plain."""
    out = []
    for row in rows:
        line, i = "", 0
        while i < len(row):
            tag = row[i][1]
            j = i
            while j < len(row) and (row[j][1] == tag or row[j][0] == " "):
                j += 1
            seg = html.escape("".join(c for c, _ in row[i:j]), quote=False)
            line += '<b>%s</b>' % seg if tag in ("j", "v") else seg
            i = j
        out.append(line.rstrip())
    return "\n".join(out)


def notice_html():
    """The notice line; it wraps only between its items (non-breaking spaces inside each)."""
    items = [i.strip() for i in ascii_logo.notice_text().split("::") if i.strip()]
    items = [":: " + items[0]] + items[1:-1] + [items[-1] + " ::"]
    return " :: ".join(html.escape(i).replace(" ", "&nbsp;") for i in items)


def logo_html():
    """The header art as in assets/banner-2x.png: the star with the 3D J, then the title; side by side when wide."""
    return ('<pre class="star">%s</pre><pre class="title">%s</pre>'
            % (cells_html(ascii_logo.star_j_cells()), cells_html(ascii_logo.title_cells())))

CSS = r"""
:root{--black:#000;--paper:#F6F0ED;--dim:#9A9592;
--mono:ui-monospace,"Cascadia Mono","DejaVu Sans Mono",Consolas,monospace}
*{box-sizing:border-box}
html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--black);color:var(--paper);font:400 15px/1.5 var(--mono)}
.page{width:80ch;max-width:100%;margin:0 auto;padding:22px 12px 48px}
a{color:var(--paper);font-weight:700;text-decoration:underline;text-decoration-color:var(--dim);text-underline-offset:3px}
a:hover,a:focus-visible{text-decoration-color:var(--paper)}
a:focus-visible{outline:2px solid var(--paper);outline-offset:2px}
.skip{position:absolute;left:8px;top:-60px;background:var(--paper);color:var(--black);padding:6px 10px;z-index:5}
.skip:focus{top:8px}
.sr{position:absolute;width:1px;height:1px;overflow:hidden;clip:rect(0 0 0 0);white-space:nowrap}
pre{margin:0;font:inherit;white-space:pre}
.r{color:var(--dim)}.y{color:var(--paper);font-weight:700}
.logo{display:flex;flex-wrap:wrap;align-items:center;justify-content:center;gap:12px 2ch;margin:0 auto 18px;font-weight:700}
.logo pre{line-height:1.12}
.logo pre.star{font-size:min(14px,calc((100vw - 24px)/39))}
.logo pre.title{font-size:min(18px,calc((100vw - 24px)/19))}
.banner{margin:0 0 14px;text-align:center;line-height:1.4}
.banner span{background:var(--paper);color:var(--black);font-weight:700;padding:0 .5ch;box-decoration-break:clone;-webkit-box-decoration-break:clone}
nav.main{margin:0 0 22px;text-align:center;line-height:1.9}
nav.main span{white-space:nowrap;margin:0 .2em}
section.nfo{margin:0 0 22px}
h2{margin:0;font:inherit;line-height:1.5}
.asc{display:none}
.flat{display:block}
h2 .flat{display:block;font-weight:700;border-top:1px solid var(--dim);padding-top:6px;margin-bottom:6px}
.flat p,.flat ul,.flat ol,.flat blockquote,.flat pre,.flat h3{margin:0 0 .9em}
.flat ul{list-style:none;padding:0}
.flat ul li{padding-left:2ch;text-indent:-2ch}
.flat ul li::before{content:"- "}
.flat ol{padding-left:3ch}
.flat blockquote{padding-left:2ch;border-left:2px solid var(--dim)}
.flat h3{font:inherit;font-weight:700}
.flat pre.code{color:var(--dim);white-space:pre-wrap;overflow-wrap:anywhere}
.flat p,.flat li{overflow-wrap:anywhere}
.tbl{overflow-x:auto;margin:0 0 .9em}
.tbl:focus-visible{outline:2px solid var(--paper)}
.foot{color:var(--paper)}
@media(min-width:780px){
.page{padding:30px 0 60px}
.logo pre.star{font-size:10px}
.logo pre.title{font-size:16px}
.banner span{font-size:13px}
h2 .asc{display:block;white-space:pre;line-height:1.25}
pre.asc{display:block;line-height:1.25}
.flat,h2 .flat{display:none}
}
@media print{.skip{display:none}}
"""

DEMO_README = """# J-Stars Victory VS-EX

A fan project that aims to bring a PS3 crossover fighter to Windows by static recompilation (placeholder text for layout preview; this sample is not the real README).

You need your own copy of the game. Nothing of the game is included.

> **Status:** placeholder. As of 2026-10-29 this sample page only shows how the site looks; nothing here describes real progress.

## Status

As of 2026-10-29 (placeholder): the sample says nothing about the real port. A real status lists what was checked,
what is missing, and how it was measured.

- Menus: placeholder item one
- Fights: placeholder item two
- Known gaps: placeholder item three

## Requirements

- Your own copy of the game (placeholder).
- A Windows x64 PC (placeholder).
- The tools named in the setup notes (placeholder).

## Getting it

1. Placeholder step one: read the notes.
2. Placeholder step two: build from your own files.
3. Placeholder step three: run `play.bat`.

| Item | Value |
|---|---|
| Platform | Windows x64 (placeholder) |
| Graphics | Direct3D 12 (placeholder) |
| Licence | MIT (placeholder) |

## How AI was used

Developed with AI agent assistance under the maintainer's direction.

## Licence

MIT, see `LICENSE`. `NOTICE.md` lists the licences of the software this project builds on (placeholder).
"""

DEMO_NOTICE = """# Notices

## Placeholder notice

This is sample notice text. Trademarks belong to their owners. This is an unofficial fan project.

## Fonts

This page uses only the fonts already on your system (placeholder notice).
"""

# ---------------------------------------------------------------- build

def build(readme, notice, out):
    pre, secs = sections(readme)
    pre = re.sub(r"(?m)^<[a-zA-Z][^\n]*\n?", "", pre)  # raw HTML before the first section (the README banner): the page has its own logo
    pre = re.sub(r"(?m)^(?:\[?!\[|\*\*\[)[^\n]*\n?", "", pre)  # badge rows (remote images) and the bold link nav: repository-only
    _, nsecs = sections(notice)
    ids = {slug(t) for t, _ in secs}
    intro = list(blocks(pre))
    # The placeholder sample repeated its status as a leading quote; the README's leading quote is the own-copy notice: keep that.
    intro = [(k, v) for k, v in intro if k in ("p", "quote") and not (k == "quote" and "status" in ids and "own copy" not in str(v).lower())]
    parts = [box("About", intro, "about")] if intro else []
    for title, body in secs:
        parts.append(box(title, list(blocks(body)), slug(title)))
    nb = []
    for t, b in nsecs:
        nb.append(("h", t))
        nb += list(blocks(b))
    parts.append(box("Notices", nb, "notices"))
    label = {"status": "status", "requirements": "requirements", "getting-it": "getting it",
             "no-game-data": "no game data", "launcher": "launcher", "licence": "licence"}
    # A heading may carry more words ("AI, licence, disclaimer"): match the key inside the section id.
    found = {k: next((i for i in ids if k in i), None) for k in label}
    nav = " ".join('<span>[ <a href="#%s">%s</a> ]</span>' % (found[k], l) for k, l in label.items() if found[k])
    nav += (' <span>[ <a href="#notices">notices</a> ]</span> <span>[ <a href="%s">source code</a> ]</span>'
            ' <span>[ <a href="%s">discord</a> ]</span>' % (Ctx.repo, DISCORD))
    foot_blocks = [
        ("p", "Unofficial fan site. %s %s is not affiliated with, authorized or endorsed by the game's publisher, developer "
              "or rights holders. Trademarks belong to their owners." % (TAGLINE, NAME)),
        ("p", "Source and notices: <%s>. Community: <%s>" % (Ctx.repo, DISCORD)),
        ("p", "System monospace fonts only; nothing is loaded from elsewhere. Generated from the repository's README.md "
              "and NOTICE.md; no JavaScript."),
    ]
    parts.append(box("Disclaimer", foot_blocks, "disclaimer"))
    page = f'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{NAME} - unofficial fan site</title>
<meta name="description" content="{html.escape(DESCRIPTION)}">
<link rel="stylesheet" href="style.css">
</head>
<body>
<a class="skip" href="#main">Skip to content</a>
<div class="page">
<header>
<h1 class="sr">{NAME}</h1>
<div class="logo" role="img" aria-label="{NAME}, Fan Revival">{logo_html()}</div>
<p class="banner"><span>{notice_html()}</span></p>
<nav class="main" aria-label="Sections">{nav}</nav>
</header>
<main id="main">
{chr(10).join(parts)}
</main>
</div>
</body>
</html>
'''
    out.mkdir(parents=True, exist_ok=True)
    for d in ("fonts", "images"):  # stale output of earlier looks
        if (out / d).is_dir():
            shutil.rmtree(out / d)
    (out / "index.html").write_text(page, encoding="utf-8")
    (out / "style.css").write_text(CSS, encoding="utf-8")
    (out / ".nojekyll").write_text("")
    return page, CSS

# ---------------------------------------------------------------- check

def check(page, css):
    errs = []
    if re.search(r"<script", page, re.I):
        errs.append("script tag")
    if re.search(r"<[^>]*\son[a-z]+\s*=", page, re.I):
        errs.append("inline event handler")
    if re.search(r"javascript:", page + css, re.I):
        errs.append("javascript: url")
    for m in re.finditer(r"<(img|link|source|iframe|script|video|audio|embed|object)\b[^>]*>", page, re.I):
        for a in re.finditer(r'\b(?:src|srcset|href|data)="([^"]*)"', m.group(0)):
            for u in re.split(r",\s*", a.group(1)):
                if re.match(r"(https?:)?//", u.strip()):
                    errs.append("external resource in HTML: " + u)
    if re.search(r"@import|url\(\s*['\"]?(https?:)?//", css, re.I):
        errs.append("external resource in CSS (@import or remote url)")
    for m in re.finditer(r"<img\b[^>]*>", page):
        if not re.search(r'\balt="[^"]+"', m.group(0)):
            errs.append("image without alt: " + m.group(0)[:60])
    flat = html.unescape(re.sub(r"<[^>]+>", " ", page)) + " " + " ".join(re.findall(r'(?:href|src)="([^"]*)"', page))
    for pat, lab in ((r"/home/", "/home/"), (r"/mnt/", "/mnt/"), (r"/run/", "/run/"), (r"(?<![\w-])work/", "work/"),
                     (r"file:", "file:"), (r"\bD\d{1,3}\b", "decision id (D<number>)")):
        if re.search(pat, flat):
            errs.append("internal path or word: " + lab)
    ids = set(re.findall(r'<section id="([^"]+)" class="nfo"', page))
    # The real README has "No game data" (own copy, no files here) where the placeholder sample had "Getting it".
    # The README carries the requirements under "How to play" (Windows version, tools, disk space).
    for need, alts in (("status", ("status",)), ("requirements", ("requirements", "how-to-play")), ("licence", ("licence",))):
        if not any(a in i for i in ids for a in alts):
            errs.append("missing section: " + need)
    # Own-copy notice: a section of its own, or the leading notice in the About box.
    about = re.search(r'<section id="about" class="nfo">(.*?)<!--/flat-->', page, re.S)
    about_txt = re.sub(r"<[^>]+>", " ", about.group(1)) if about else ""
    if not (ids & {"getting-it", "no-game-data"}) and not re.search(r"\bno game (code|data|files)\b", about_txt, re.I):
        errs.append("missing section: getting-it or no-game-data")
    sm = re.search(r'<section id="status" class="nfo">(.*?)<!--/flat-->', page, re.S)
    if sm:
        body = sm.group(1).split('<div class="flat">', 1)[-1]
        if not re.search(r"\b(as of|refreshed|updated)\b[^.]{0,40}?\d{4}-\d{2}-\d{2}", re.sub(r"<[^>]+>", " ", body), re.I):
            errs.append("status section lacks a date ('As of', 'refreshed' or 'updated' YYYY-MM-DD)")
    if "nfo" not in page or "Unofficial fan site" not in page or TAGLINE not in page:
        errs.append("missing unofficial label or tagline")
    size = len(page.encode()) + len(css.encode())
    if size > 1_000_000:
        errs.append("HTML+CSS over 1 MB (%d bytes)" % size)
    return errs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    # Defaults fit both trees: the port repo (README in public/, output in work/) and the published repo itself.
    pub = (ROOT / "public" / "README.md").exists()
    ap.add_argument("--src", default=str(ROOT / "public" if pub else ROOT))
    ap.add_argument("--out", default=str(ROOT / ("work/site/_site" if pub else "_site")))
    ap.add_argument("--fonts", help="unused in the NFO look")
    ap.add_argument("--images", help="unused in the NFO look")
    ap.add_argument("--repo", default=DEFAULT_REPO)
    ap.add_argument("--demo", action="store_true", help="build from a built-in placeholder README/NOTICE")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    Ctx.repo = a.repo.rstrip("/")
    if a.demo:
        readme, notice = DEMO_README, DEMO_NOTICE
    else:
        src = pathlib.Path(a.src)
        readme = (src / "README.md").read_text(encoding="utf-8")
        notice = (src / "NOTICE.md").read_text(encoding="utf-8")
    out = pathlib.Path(a.out)
    page, css = build(readme, notice, out)
    print("wrote", out / "index.html", "(%d bytes html, %d css)" % (len(page.encode()), len(css.encode())))
    if a.check:
        errs = check(page, css)
        for e in errs:
            print("FAIL:", e)
        print("site check:", "FAIL" if errs else "PASS")
        sys.exit(1 if errs else 0)


if __name__ == "__main__":
    main()
