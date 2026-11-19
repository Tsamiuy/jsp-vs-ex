#!/usr/bin/env python3
"""Fan-site generator, main-menu look: README.md + docs/features.tsv + NOTICE.md -> _site/. Stdlib only, no network.

    python3 -I tools/site/site.py --check                    # build from the repository into _site/ and check it
    python3 -I tools/site/site.py --src DIR --out DIR --check

Writes index.html (the menu page: one section at a time, driven by tools/site/site.js, inlined and allowed by its
hash in a strict Content-Security-Policy), plain.html (the same sections, no script), style.css, plain.css and
assets/ (the divider strips, the logo mark and the social preview from the repository's assets/).
The look is our own drawing of a fighting-game menu, in CSS and inline SVG: a speed-line burst page, red and white
stripe frames, slanted dark panels, menu items in heavy italic white with a black outline stacked on a diagonal, the
chosen one on a yellow-to-orange bar, a caption band and a button-hint bar. No game art, no logo, no web font
(system font stacks; the mark is tools/site/ascii_logo.py's ASCII star with the J as text, the title is
tools/site/wordmark.py's own polygons). Colours from data/theme.json.
Content: README sections (one menu item each, its first sentence as the caption), the feature list from
docs/features.tsv and NOTICE.md, so the page cannot drift from them. Link buttons from data/nav.json, the same list
the README's buttons are drawn from. --check runs tools/site/check_site.py on the result.
"""
import argparse
import base64
import hashlib
import html
import importlib.util
import json
import pathlib
import re
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent


def _load(name):  # by path: `python3 -I` keeps the script dir off sys.path
    sp = importlib.util.spec_from_file_location(name, HERE / (name + ".py"))
    m = importlib.util.module_from_spec(sp)
    sp.loader.exec_module(m)
    return m


theme = _load("theme")
wordmark = _load("wordmark")
ascii_logo = _load("ascii_logo")
NAME = "J-Stars Victory VS-EX"
REPO = "https://github.com/Tsamiuy/jsp-vs-ex"
SITE = "https://tsamiuy.github.io/jsp-vs-ex/"
DESCRIPTION = ("J-Stars Victory VS-EX is an unofficial fan port of a PS3 crossover fighter to Windows x64, made by "
               "static recompilation. No game files included; you need your own copy.")
ASSET_FILES = ("divider-stripe.png", "divider-glow.png", "logo-mark-512.png", "social-preview.png")
# Short menu labels for long README headings (the section keeps its full heading)
SHORT = {
    "What goes online and where your data goes": "Online and your data",
    "Troubleshooting and reporting bugs": "Troubleshooting",
    "Related projects and credits": "Credits",
    "Licence and disclaimer": "Licence",
    "Settings and controllers": "Settings",
    "What makes it different": "What's different",
}

# ---------------------------------------------------------------- markdown subset -> HTML


def slug(t):
    t = re.sub(r"<[^>]+>", "", t)
    return re.sub(r"[^a-z0-9]+", "-", t.lower()).strip("-")


class Links:
    """Rewrites README links for the site: #anchors to section ids, repository files to GitHub."""
    def __init__(self):
        self.anchor = {}

    def fix(self, url):
        if url.startswith("#"):
            return "#" + self.anchor.get(url[1:], url[1:])
        if re.match(r"[a-z][a-z0-9+.-]*:", url, re.I):
            return url
        path, _, frag = url.partition("#")
        kind = "tree" if path.endswith("/") or "." not in path.rsplit("/", 1)[-1] else "blob"
        return f"{REPO}/{kind}/main/{path.rstrip('/')}" + (f"#{frag}" if frag else "")


LINKS = Links()
_TOK = re.compile(r"`([^`]+)`|\[!\[([^\]]*)\]\([^)\s]+\)\]\(([^)\s]+)\)|!\[([^\]]*)\]\(([^)\s]+)\)"
                  r"|\[([^\]]+)\]\(([^)\s]+)\)|<(https?://[^>\s]+)>|\*\*(.+?)\*\*|(?<![\w*])\*([^*]+?)\*(?![\w*])")


def inline(s):
    s = re.sub(r"<!--.*?-->", "", s)
    out, pos = [], 0
    for m in _TOK.finditer(s):
        out.append(html.escape(s[pos:m.start()], quote=False))
        pos = m.end()
        g = m.groups()
        if g[0] is not None:
            out.append(f"<code>{html.escape(g[0], quote=False)}</code>")
        elif g[1] is not None:      # an image inside a link (a badge): the link with the image's alt text, no outside image
            out.append(f'<a href="{html.escape(LINKS.fix(g[2]))}">{html.escape(g[1], quote=False)}</a>')
        elif g[3] is not None:      # a bare image: its alt text only
            out.append(html.escape(g[3], quote=False))
        elif g[5] is not None:
            out.append(f'<a href="{html.escape(LINKS.fix(g[6]))}">{inline(g[5])}</a>')
        elif g[7] is not None:
            out.append(f'<a href="{html.escape(g[7])}">{html.escape(g[7], quote=False)}</a>')
        elif g[8] is not None:
            out.append(f"<strong>{inline(g[8])}</strong>")
        else:
            out.append(f"<em>{inline(g[9])}</em>")
    out.append(html.escape(s[pos:], quote=False))
    return "".join(out)


def plain_text(s):
    return html.unescape(re.sub(r"<[^>]+>", "", inline(s)))


def blocks(md, hlevel=3):
    """Markdown subset -> HTML: headings, paragraphs, lists, tables, blockquotes, fenced code. Raw HTML lines (the
    README's dividers and header) are dropped."""
    lines = md.split("\n")
    out, i = [], 0
    while i < len(lines):
        ln = lines[i]
        st = ln.strip()
        if not st or st == "---" or st.startswith("<"):
            i += 1
            continue
        if st.startswith("```"):
            j = i + 1
            while j < len(lines) and not lines[j].strip().startswith("```"):
                j += 1
            out.append("<pre><code>" + html.escape("\n".join(lines[i + 1:j]), quote=False) + "</code></pre>")
            i = j + 1
            continue
        m = re.match(r"(#{2,6})\s+(.*)", st)
        if m:
            lv = min(6, hlevel + len(m.group(1)) - 3)
            out.append(f'<h{lv}>{inline(m.group(2))}</h{lv}>')
            i += 1
            continue
        if st.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].strip().startswith("|"):
                rows.append([c.strip() for c in lines[i].strip().strip("|").split("|")])
                i += 1
            head, body = rows[0], [r for r in rows[1:] if not all(re.fullmatch(r":?-+:?", c) for c in r)]
            th = "".join(f"<th>{inline(c)}</th>" for c in head) if any(head) else ""
            t = '<div class="tw"><table>' + (f"<thead><tr>{th}</tr></thead>" if th else "") + "<tbody>"
            t += "".join("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>" for r in body)
            out.append(t + "</tbody></table></div>")
            continue
        if st.startswith(">"):
            q = []
            while i < len(lines) and lines[i].strip().startswith(">"):
                q.append(lines[i].strip()[1:].strip())
                i += 1
            out.append(f"<blockquote><p>{inline(' '.join(q))}</p></blockquote>")
            continue
        m = re.match(r"(\d+\.|[-*])\s+(.*)", st)
        if m:
            tag = "ol" if m.group(1)[0].isdigit() else "ul"
            items = []
            while i < len(lines):
                s2 = lines[i].strip()
                m2 = re.match(r"(\d+\.|[-*])\s+(.*)", s2)
                if m2:
                    items.append(m2.group(2))
                elif s2 and items and lines[i].startswith("  "):
                    items[-1] += " " + s2
                else:
                    break
                i += 1
            out.append(f"<{tag}>" + "".join(f"<li>{inline(x)}</li>" for x in items) + f"</{tag}>")
            continue
        para = []
        while i < len(lines) and lines[i].strip() and not re.match(r"(#{2,6}\s|\||>|```|<|(\d+\.|[-*])\s)", lines[i].strip()):
            para.append(lines[i].strip())
            i += 1
        out.append(f"<p>{inline(' '.join(para))}</p>")
    return "\n".join(out)


def first_sentence(md):
    for ln in md.split("\n"):
        st = ln.strip()
        if not st or st.startswith(("<", "|", "#", "```", "---")):
            continue
        st = re.sub(r"^(>|\d+\.|[-*])\s*", "", st)
        t = plain_text(st).strip()
        if not t:
            continue
        m = re.match(r"(.+?[.:!?])(\s|$)", t)
        t = m.group(1) if m else t
        return t if len(t) <= 130 else t[:127].rsplit(" ", 1)[0] + "..."
    return ""


# ---------------------------------------------------------------- sources -> sections


def split_h2(md):
    parts = re.split(r"^## +(.+)$", md, flags=re.M)
    return parts[0], [(parts[i].strip(), parts[i + 1]) for i in range(1, len(parts), 2)]


def features_html(tsv):
    rows = [r.split("\t") for r in tsv.strip("\n").split("\n")]
    head, body = rows[0], rows[1:]
    ix = {k: head.index(k) for k in ("area", "feature", "detail", "status")}
    out = ['<div class="tw"><table><thead><tr><th>Feature</th><th>Area</th><th>Status</th>'
           '<th>Detail</th></tr></thead><tbody>']
    for r in body:
        st = r[ix["status"]]
        cls = {"available": "ok", "works": "ok", "in progress": "wip", "planned": "plan"}.get(st, "na")
        out.append(f'<tr><td>{inline(r[ix["feature"]])}</td><td>{inline(r[ix["area"]])}</td>'
                   f'<td><span class="st st-{cls}">{html.escape(st)}</span></td><td>{inline(r[ix["detail"]])}</td></tr>')
    out.append("</tbody></table></div>")
    return "\n".join(out), len(body)


def collect(src):
    readme = (src / "README.md").read_text(encoding="utf-8")
    notice = (src / "NOTICE.md").read_text(encoding="utf-8")
    tsv = (src / "docs" / "features.tsv").read_text(encoding="utf-8")
    nav = json.loads((src / "data" / "nav.json").read_text(encoding="utf-8"))
    version = (src / "installer" / "VERSION").read_text(encoding="utf-8").strip()
    pre, secs = split_h2(readme)
    tagline = re.search(r"<b>(.+?)</b>", pre)
    tagline = html.unescape(re.sub(r"<[^>]+>", "", tagline.group(1))) if tagline else DESCRIPTION
    intro = re.sub(r"(?s)<div.*?</div>", "", pre)
    LINKS.anchor = {slug(h): slug(h) for h, _ in secs}
    out = [{"id": "start", "head": "Start", "label": "Start", "detail": tagline,
            "body": f"<p>{html.escape(tagline, quote=False)}</p>\n" + blocks(intro)}]
    for h, body in secs:
        out.append({"id": slug(h), "head": h, "label": SHORT.get(h, h), "detail": first_sentence(body),
                    "body": blocks(body)})
    fh, n = features_html(tsv)
    out.insert(2, {"id": "features", "head": "Features", "label": "Features",
                   "detail": f"Every feature of the port and its state: {n} rows from the project's feature list.",
                   "body": "<p>From the project's feature list (" + f'<a href="{REPO}/blob/main/docs/FEATURES.md">'
                           "docs/FEATURES.md</a>).</p>\n" + fh})
    _npre, nsecs = split_h2(notice)
    nbody = "\n".join(f"<h3>{inline(h)}</h3>\n" + blocks(b, 4) for h, b in nsecs)
    out.append({"id": "notices", "head": "Notices", "label": "Notices",
                "detail": "Third-party software, licences and credits (NOTICE.md).", "body": nbody})
    return {"sections": out, "tagline": tagline, "nav": nav, "version": version}


# ---------------------------------------------------------------- pages


def site_href(h):
    return LINKS.fix(h)


def buttons_html(nav, plain_page):
    items = [(b["label"], site_href(b["href"]), b.get("primary", False)) for b in nav["buttons"]]
    w = nav["wide"]
    items.append((w["label"], site_href(w["href"]), False))
    items.append(("Source code", REPO, False))
    cls = "pbtn" if plain_page else "btn"
    return (f'<ul class="{cls}s">' + "".join(
        f'<li><a class="{cls}{" primary" if p else ""}" href="{html.escape(h)}"><span>{html.escape(l)}</span></a></li>'
        for l, h, p in items) + "</ul>")


def extra_links_html(d):
    """The project links of data/nav.json that no section already links to (the changelog, the issue form, the
    Discord, the source), as a short list under the menu items: the sections cover the rest, so the page has no
    separate button row. The arrow keys move through the sections only (site.js reads "ol a")."""
    nav = d["nav"]
    items = [(b["label"], site_href(b["href"])) for b in nav["buttons"] + [nav["wide"]]] + [("Source code", REPO)]
    body = "\n".join(s["body"] for s in d["sections"])
    left = [(l, h) for l, h in items if f'href="{html.escape(h)}"' not in body]
    if not left:
        return ""
    return ('<p class="menu-sub">Links</p><ul class="ext">' + "".join(
        f'<li><a href="{html.escape(h)}">{html.escape(l)}</a></li>' for l, h in left) + "</ul>")


def mark_html():
    """The project mark: tools/site/ascii_logo.py's star with the 3D J as text cells in a monospace stack, the star in
    the title yellow and the J in orange (classes .ms and .mj), on the dark title plate."""
    out = []
    for row in ascii_logo.star_j_cells():
        line, i = "", 0
        while i < len(row):
            tag = row[i][1]
            j = i
            while j < len(row) and (row[j][1] == tag or row[j][0] == " "):
                j += 1
            seg = html.escape("".join(c for c, _ in row[i:j]), quote=False)
            line += f'<span class="m{tag}">{seg}</span>' if tag in ("s", "j") else seg
            i = j
        out.append(line.rstrip())
    return '<pre class="mark" aria-hidden="true">' + "\n".join(out) + "</pre>"


def title_svgs():
    top = wordmark.svg("J-STARS VICTORY", 30, 3, fill="var(--text)", ink="var(--ink)", cls="wm-top")
    big = wordmark.svg("VS-EX", 74, 5, fill="url(#g-title)", ink="var(--ink)", cls="wm-big")
    big = big.replace('focusable="false">', 'focusable="false"><defs><linearGradient id="g-title" x1="0" y1="0" x2="0" '
                      'y2="1"><stop offset="0" stop-color="var(--title-top)"/><stop offset="1" '
                      'stop-color="var(--title-bottom)"/></linearGradient></defs>', 1)
    return top, big


def head(title, css, csp, plain_page):
    og = (f'<meta property="og:title" content="{NAME}, Fan Revival">'
          f'<meta property="og:description" content="{html.escape(DESCRIPTION)}">'
          f'<meta property="og:image" content="{SITE}assets/social-preview.png">'
          '<meta property="og:type" content="website"><meta name="twitter:card" content="summary_large_image">')
    return ('<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
            '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
            f'<meta http-equiv="Content-Security-Policy" content="{csp}">\n'
            '<meta name="referrer" content="no-referrer">\n<meta name="color-scheme" content="light dark">\n'
            f'<title>{title}</title>\n<meta name="description" content="{html.escape(DESCRIPTION)}">\n{og}\n'
            '<link rel="icon" href="assets/logo-mark-512.png">\n'
            f'<link rel="stylesheet" href="{css}">\n</head>\n')


def legal_html():
    return ('<p class="legal">Unofficial fan project. Not affiliated with, authorized or endorsed by the game\'s '
            'publisher, developer or rights holders. Trademarks belong to their owners. No game files are included; '
            'you need your own copy of the game. The page\'s drawings are the project\'s own.</p>')


def build_index(d, js):
    secs = d["sections"]
    sha = base64.b64encode(hashlib.sha256(js.encode("utf-8")).digest()).decode()
    csp = ("default-src 'none'; style-src 'self'; img-src 'self'; script-src 'sha256-" + sha + "'; "
           "base-uri 'none'; form-action 'none'")
    top, big = title_svgs()
    menu = "".join(f'<li><a href="#{s["id"]}" data-detail="{html.escape(s["detail"])}">{html.escape(s["label"])}</a></li>'
                   for s in secs)
    body = "\n".join(f'<section class="sec" id="{s["id"]}" aria-labelledby="h-{s["id"]}">\n'
                     f'<h2 id="h-{s["id"]}">{inline(s["head"])}</h2>\n{s["body"]}\n</section>' for s in secs)
    return (head(f"{NAME}: Fan Revival", "style.css", csp, False) +
            '<body>\n<a class="skip" href="#panel">Skip to the content</a>\n'
            '<div class="frame frame-top" aria-hidden="true"></div>\n'
            '<header class="masthead">\n<div class="plate-wrap"><div class="plate">' + mark_html() +
            f'<h1 class="title"><span class="vh">{NAME}</span>{top}{big}</h1>'
            '<p class="sub"><span>Fan Revival</span></p></div></div>\n'
            f'<p class="tagline">{html.escape(d["tagline"])}</p>\n'
            '</header>\n'
            '<main class="stage">\n<nav id="menu" class="menu" aria-label="Sections">'
            f'<p class="menu-head">Main Menu</p><ol>{menu}</ol>{extra_links_html(d)}</nav>\n'
            f'<div id="panel" class="panel" tabindex="-1">\n{body}\n</div>\n</main>\n'
            f'<p id="detail" class="caption" aria-live="polite">{html.escape(secs[0]["detail"])}</p>\n'
            '<footer class="hints">\n<ul class="keys"><li><kbd>&#8593;</kbd><kbd>&#8595;</kbd> Select</li>'
            '<li><kbd>Enter</kbd> Open</li><li><kbd>Esc</kbd> Back</li><li class="tap">Tap an item to open it</li></ul>\n'
            f'<p class="ver"><span>{NAME} {html.escape(d["version"])}</span></p>\n</footer>\n'
            f'<div class="foot">{legal_html()}<p class="alt"><a href="plain.html">Plain page (no script)</a> · '
            f'<a href="{REPO}">Source code on GitHub</a></p></div>\n'
            '<div class="frame frame-bottom" aria-hidden="true"></div>\n'
            f'<script>{js}</script>\n</body>\n</html>\n')


def build_plain(d):
    secs = d["sections"]
    csp = "default-src 'none'; style-src 'self'; img-src 'self'; base-uri 'none'; form-action 'none'"
    toc = "".join(f'<li><a href="#{s["id"]}">{html.escape(s["label"])}</a></li>' for s in secs)
    body = "\n".join(f'<section class="sec" id="{s["id"]}" aria-labelledby="h-{s["id"]}">\n'
                     f'<h2 id="h-{s["id"]}">{inline(s["head"])}</h2>\n{s["body"]}\n</section>' for s in secs)
    return (head(f"{NAME}: plain page", "plain.css", csp, True) +
            '<body>\n<p><img class="rule" src="assets/divider-stripe.png" width="900" height="22" alt=""></p>\n'
            f'<header><h1>{NAME}, Fan Revival</h1>\n<p>{html.escape(d["tagline"])}</p>\n'
            f'<nav aria-label="Project links">{buttons_html(d["nav"], True)}</nav>\n'
            '<p><a href="index.html">Menu page</a> (the same content, one section at a time)</p>\n'
            f'<nav aria-label="Sections"><ol class="toc">{toc}</ol></nav></header>\n<main>\n{body}\n</main>\n'
            f'<footer>{legal_html()}<p>Version {html.escape(d["version"])}. <a href="{REPO}">Source code on GitHub</a></p>'
            '</footer>\n</body>\n</html>\n')


def build_css(t, nsec):
    rays = _rays(56)
    stops, shift = [], -rays[0][0]
    for i, (a0, a1) in enumerate(rays):
        b0, b1 = a0 + shift, a1 + shift
        nxt = rays[i + 1][0] + shift if i + 1 < len(rays) else 360
        stops.append(f"var(--ray) {b0:.2f}deg {b1:.2f}deg, var(--burst) {b1:.2f}deg {nxt:.2f}deg")
    burst = f"conic-gradient(from {-shift:.2f}deg at 14% 38%, " + ", ".join(stops) + ")"

    def tok(mode):
        m = t.modes[mode]
        return (f"--burst: {m['burst_base']}; --ray: {m['burst_ray']}; --line: {m['burst_line']}; "
                f"--page-text: {m['page_text']}; --page-link: {m['page_link']};")
    base = " ".join(f"--{k.replace('_', '-')}: {v};" for k, v in t.colors.items())
    fonts = f"--heavy: {t.fonts['heavy']}; --body: {t.fonts['body']}; --mono: {t.fonts['mono']};"
    return CSS.replace("/*TOKENS*/", f":root {{ {base} {fonts} {tok('light')} }}\n"
                       f"@media (prefers-color-scheme: dark) {{ :root {{ {tok('dark')} }} }}") \
              .replace("/*BURST*/", burst)


def _rays(n):
    sp = importlib.util.spec_from_file_location("repo_art_rays", HERE / "repo_art.py")
    m = importlib.util.module_from_spec(sp)
    sp.loader.exec_module(m)   # repo_art imports Pillow only inside its drawing functions
    return m.rays(n)


CSS = r"""/* J-Stars Victory VS-EX fan site: generated by tools/site/site.py from data/theme.json. Our own drawing of a
   fighting-game menu: nothing from the game, system fonts only. */
/*TOKENS*/
*, *::before, *::after { box-sizing: border-box; }
html { -webkit-text-size-adjust: 100%; }
body { margin: 0; min-height: 100vh; color: var(--page-text); background-color: var(--burst);
  background-image: /*BURST*/; background-attachment: fixed; font: 16px/1.55 var(--body); overflow-x: hidden; overflow-wrap: break-word; }
a { color: var(--link); }
.vh, .skip:not(:focus) { position: absolute; width: 1px; height: 1px; overflow: hidden; clip-path: inset(50%); white-space: nowrap; }
.skip:focus { position: fixed; z-index: 9; top: 8px; left: 8px; padding: 8px 14px; background: var(--key-bg); color: var(--key-text); }
:focus-visible { outline: 3px solid var(--glow-yellow); outline-offset: 2px; }

/* red and white stripe frames */
.frame { height: 30px; background: repeating-linear-gradient(115deg, var(--stripe-red) 0 64px, var(--stripe-white) 64px 74px);
  border-bottom: 4px solid var(--ink); }
.frame-top { margin: 0; }
.frame-bottom { border-bottom: 0; border-top: 4px solid var(--ink); margin-top: 28px; }

/* title plate: slanted dark panel with a hard black shadow */
.masthead { max-width: 1240px; margin: 22px auto 0; padding: 0 16px; }
.plate-wrap { filter: drop-shadow(9px 9px 0 var(--ink)); }
.plate { display: flex; flex-wrap: wrap; align-items: center; gap: 4px 20px; padding: 16px 48px 18px 40px; background: var(--panel);
  clip-path: polygon(24px 0, 100% 0, calc(100% - 24px) 100%, 0 100%); }
.mark { flex: none; margin: 0; font: 800 7px/1.18 var(--mono); letter-spacing: 0; color: var(--title-top); white-space: pre;
  text-shadow: .4px 0 0 currentColor, -.4px 0 0 currentColor, 0 .3px 0 currentColor; }
.mark .ms { color: var(--title-top); }
.mark .mj { color: var(--glow-orange); }
.title { margin: 0; display: flex; flex-direction: column; gap: 2px; line-height: 0; }
.title svg { display: block; height: auto; max-width: 100%; }
.wm-top { width: 300px; }
.wm-big { width: 300px; }
.sub { margin: 0 0 0 auto; align-self: flex-end; }
.sub span { display: inline-block; padding: 6px 54px; font: italic 900 1.5rem/1.2 var(--heavy); color: var(--text);
  -webkit-text-stroke: 6px var(--ink); paint-order: stroke fill; letter-spacing: .02em;
  background: linear-gradient(90deg, transparent, var(--glow-yellow) 18%, var(--glow-orange) 82%, transparent); }
.tagline { margin: 18px 0 8px; font-weight: 700; color: var(--page-text); max-width: 72ch; }

/* the stage: menu on the left, the open section on the right */
.stage { max-width: 1240px; margin: 26px auto 0; padding: 0 16px; display: grid; grid-template-columns: 320px minmax(0, 1fr);
  gap: 22px; align-items: start; }
.menu { position: sticky; top: 12px; padding: 16px 0 20px; background: var(--panel);
  clip-path: polygon(0 0, 100% 0, calc(100% - 34px) 100%, 0 100%); }
.menu-head { margin: 0 0 10px; padding: 0 18px; font: italic 900 1.7rem/1.1 var(--heavy); color: var(--title-bottom);
  background: linear-gradient(var(--title-top), var(--title-bottom)); -webkit-background-clip: text; background-clip: text;
  -webkit-text-fill-color: transparent; }
.menu ol { list-style: none; margin: 0; padding: 0; }
.menu a { display: block; padding: 3px 18px 3px 22px; text-decoration: none; color: var(--text);
  font: italic 900 1.08rem/1.35 var(--heavy); -webkit-text-stroke: 4px var(--ink); paint-order: stroke fill;
  letter-spacing: .015em; }
.menu a:hover, .menu a.on, .menu a:focus-visible { outline: none;
  background: linear-gradient(90deg, transparent, var(--glow-yellow) 10%, var(--glow-orange) 78%, transparent); }
.menu a.on { font-size: 1.22rem; }
.menu-sub { margin: 16px 0 4px; padding: 8px 18px 0; border-top: 3px solid var(--panel-edge); font: italic 900 .95rem/1.2 var(--heavy);
  color: var(--title-top); }
.menu .ext { list-style: none; margin: 0; padding: 0; }
.menu .ext a { font-size: .95rem; padding: 2px 18px 2px 22px; }
.menu .ext a::after { content: " \2197"; font-style: normal; -webkit-text-stroke: 0; color: var(--muted); }
.panel { position: relative; background: var(--panel); color: var(--text); padding: 22px 30px 28px 38px;
  clip-path: polygon(22px 0, 100% 0, 100% 100%, 0 100%); box-shadow: inset 0 4px 0 var(--glow-orange); min-height: 420px; outline: none; }
.sec + .sec { margin-top: 34px; padding-top: 18px; border-top: 3px solid var(--panel-edge); }
html.js .sec { display: none; }
html.js .sec.on { display: block; }
html.js .sec + .sec { margin-top: 0; padding-top: 0; border-top: 0; }
.sec h2 { margin: 0 0 14px; font: italic 900 1.9rem/1.15 var(--heavy); color: var(--text); -webkit-text-stroke: 6px var(--ink);
  paint-order: stroke fill; }
.sec h3 { margin: 22px 0 8px; font: italic 900 1.2rem/1.2 var(--heavy); color: var(--title-top); }
.sec h4 { margin: 18px 0 6px; font: italic 800 1.05rem/1.2 var(--heavy); color: var(--muted); }
.sec p, .sec li { max-width: 78ch; }
.sec a { color: var(--link); text-underline-offset: 2px; }
.sec code { font: .9em var(--mono); background: var(--code-bg); padding: 1px 5px; border-radius: 2px; overflow-wrap: anywhere; }
.sec pre { background: var(--code-bg); padding: 12px; overflow-x: auto; font: .8rem/1.4 var(--mono); }
.sec pre code { background: none; padding: 0; }
.sec blockquote { margin: 12px 0; padding: 6px 14px; border-left: 6px solid var(--stripe-red); background: var(--caption); }
.tw { overflow-x: auto; margin: 12px 0; }
.sec table { border-collapse: collapse; width: 100%; font-size: .92rem; }
.sec th { text-align: left; white-space: nowrap; font: italic 900 .9rem/1.3 var(--heavy); color: var(--ink); background: var(--glow-yellow); padding: 6px 10px; }
.sec td { padding: 7px 10px; border-bottom: 1px solid var(--panel-edge); vertical-align: top; color: var(--text); }
.sec tbody tr:nth-child(even) td { background: var(--caption); }
.st { display: inline-block; padding: 0 8px; font: italic 900 .8rem/1.5 var(--heavy); white-space: nowrap; }
.st-ok { background: var(--glow-yellow); color: var(--ink); }
.st-wip { background: var(--glow-orange); color: var(--ink); }
.st-plan { background: var(--key-bg); color: var(--ink); }
.st-na { background: var(--panel-edge); color: var(--text); }

/* caption band and button-hint bar */
.caption { max-width: 1240px; margin: 18px auto 0; padding: 14px 40px; min-height: 3.2em; background: var(--caption); color: var(--text);
  font-size: 1.05rem; clip-path: polygon(0 0, 100% 0, calc(100% - 26px) 100%, 0 100%); box-shadow: inset 0 3px 0 var(--glow-yellow); }
.hints { max-width: 1240px; margin: 14px auto 0; padding: 0 16px; display: flex; flex-wrap: wrap; align-items: center;
  justify-content: space-between; gap: 10px; }
.keys { list-style: none; display: flex; flex-wrap: wrap; gap: 6px 22px; margin: 0; padding: 7px 22px; background: var(--key-bg);
  color: var(--key-text); border: 3px solid var(--ink); border-radius: 999px; font-weight: 700; }
.keys kbd { display: inline-block; min-width: 1.7em; margin-right: 4px; padding: 0 6px; text-align: center; border-radius: 6px;
  background: var(--ink); color: var(--text); font: 700 .85rem/1.6 var(--mono); }
.keys .tap { display: none; }
.ver { margin: 0; }
.ver span { display: inline-block; padding: 6px 26px; background: var(--ink); color: var(--text); font: italic 900 1rem/1.3 var(--heavy);
  clip-path: polygon(14px 0, 100% 0, calc(100% - 14px) 100%, 0 100%); border-bottom: 3px solid var(--stripe-red); }
.foot { max-width: 1240px; margin: 18px auto 0; padding: 0 16px; color: var(--page-text); font-size: .88rem; }
.foot a { color: var(--page-link); }
.legal { margin: 0 0 4px; max-width: 100ch; }
.alt { margin: 0; }

@media (max-width: 860px) {
  .stage { grid-template-columns: minmax(0, 1fr); gap: 16px; }
  .menu { position: static; clip-path: polygon(0 0, 100% 0, calc(100% - 18px) 100%, 0 100%); }
  .menu ol { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); }
  .menu a { font-size: 1rem; padding: 6px 12px; }
  .menu a.on { font-size: 1rem; }
  .panel { padding: 18px 16px 22px 20px; clip-path: polygon(12px 0, 100% 0, 100% 100%, 0 100%); min-height: 0; }
  .plate { padding: 14px 24px 16px 22px; clip-path: polygon(14px 0, 100% 0, calc(100% - 14px) 100%, 0 100%); }
  .wm-top { width: 210px; }
  .wm-big { width: 210px; }
  .mark { font-size: 6px; }
  .sub { margin: 4px 0 0; }
  .sub span { font-size: 1.15rem; padding: 4px 30px; }
  .sec h2 { font-size: 1.5rem; }
  .keys li:not(.tap) { display: none; }
  .keys .tap { display: list-item; }
  .caption { padding: 12px 22px; }
}
@media (max-width: 340px) {
  .menu ol { grid-template-columns: minmax(0, 1fr); }
}
@media (prefers-reduced-motion: reduce) { * { scroll-behavior: auto !important; transition: none !important; animation: none !important; } }
@media (forced-colors: active) { .menu a.on { outline: 3px solid CanvasText; } }
"""

PLAIN_CSS = r"""/* J-Stars Victory VS-EX plain page: no script, the same content as index.html. Generated by tools/site/site.py. */
/*TOKENS*/
body { margin: 0 auto; max-width: 80ch; padding: 16px; font: 16px/1.55 var(--body); overflow-wrap: break-word; color: var(--page-text); background: var(--burst); }
a { color: var(--page-link); }
img.rule { display: block; width: 100%; height: auto; }
h1, h2 { font-family: var(--heavy); font-style: italic; }
.pbtns { list-style: none; padding: 0; display: flex; flex-wrap: wrap; gap: 8px; }
.pbtn { display: inline-block; padding: 4px 12px; background: var(--panel); color: var(--text); text-decoration: none;
  border-left: 6px solid var(--stripe-red); font-weight: 700; }
.pbtn.primary { background: var(--glow-yellow); color: var(--ink); }
.toc { columns: 2; }
.sec { border-top: 3px solid var(--stripe-red); margin-top: 28px; }
.tw { overflow-x: auto; }
table { border-collapse: collapse; }
th, td { border: 1px solid var(--line); padding: 4px 8px; text-align: left; vertical-align: top; }
code { font-family: var(--mono); }
pre { overflow-x: auto; }
.st { font-weight: 700; padding: 0 6px; }
.st-ok { background: var(--glow-yellow); color: var(--ink); }
.st-wip { background: var(--glow-orange); color: var(--ink); }
.st-plan, .st-na { border: 1px solid var(--line); }
.legal { font-size: .9rem; }
@media (max-width: 520px) { .toc { columns: 1; } }
"""


def plain_css(t):
    m, d = t.modes["light"], t.modes["dark"]
    base = " ".join(f"--{k.replace('_', '-')}: {v};" for k, v in t.colors.items())
    fonts = f"--heavy: {t.fonts['heavy']}; --body: {t.fonts['body']}; --mono: {t.fonts['mono']};"

    def tok(x):
        return f"--burst: {x['burst_base']}; --line: {x['burst_line']}; --page-text: {x['page_text']}; --page-link: {x['page_link']};"
    return PLAIN_CSS.replace("/*TOKENS*/", f":root {{ {base} {fonts} {tok(m)} }}\n"
                             f"@media (prefers-color-scheme: dark) {{ :root {{ {tok(d)} }} }}")


def build(src, out):
    src, out = pathlib.Path(src), pathlib.Path(out)
    t = theme.load(src / "data" / "theme.json")
    d = collect(src)
    js = (HERE / "site.js").read_text(encoding="utf-8")
    if out.exists():
        shutil.rmtree(out)
    (out / "assets").mkdir(parents=True)
    (out / "index.html").write_text(build_index(d, js), encoding="utf-8", newline="\n")
    (out / "plain.html").write_text(build_plain(d), encoding="utf-8", newline="\n")
    (out / "style.css").write_text(build_css(t, len(d["sections"])), encoding="utf-8", newline="\n")
    (out / "plain.css").write_text(plain_css(t), encoding="utf-8", newline="\n")
    (out / ".nojekyll").write_text("", encoding="utf-8")
    for f in ASSET_FILES:
        shutil.copyfile(src / "assets" / f, out / "assets" / f)
    return d


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", default=str(ROOT))
    ap.add_argument("--out", default=str(ROOT / "_site"))
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    d = build(a.src, a.out)
    print(f"site: wrote {a.out} ({len(d['sections'])} sections)")
    if a.check:
        chk = _load("check_site")
        problems, notes = chk.check(pathlib.Path(a.out), pathlib.Path(a.src))
        for n in notes:
            print(n)
        if problems:
            print("check_site: problems:")
            for p in problems:
                print("  - " + p)
            return 1
        print("check_site: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
