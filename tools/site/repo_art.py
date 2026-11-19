#!/usr/bin/env python3
"""The repository art of J-Stars Victory VS-EX, drawn in code: README banner, GitHub social preview, logo mark and the
README/site divider strips. Our own drawings in a fighting-game menu look (speed-line burst, red and white stripe frame,
slanted dark panels, a yellow-to-orange highlight bar, an outlined gradient title); nothing from the game, no font
file (the lettering is tools/site/wordmark.py's polygons). Colours from data/theme.json. Needs Pillow.

    python3 -I tools/site/repo_art.py            # write assets/*.png
    python3 -I tools/site/repo_art.py --check    # render again; exit 1 when a file is missing or its pixels differ
    python3 -I tools/site/repo_art.py --out DIR  # write elsewhere (previews)

Files: banner-2x.png 2560x640, social-preview.png 1280x640, logo-mark-512.png 512x512, divider-stripe.png
and divider-glow.png (900 px wide, transparent, read on GitHub's light and dark pages), and the
README menu buttons nav/<id>.png (500x112, shown at 250) and nav/discord-wide.png (1516x112, shown at 758) from
data/nav.json.
installer/tools/installer_art.py draws the wizard images and icons with the helpers here. Deterministic: same script
and Pillow version, same pixels.
"""
import argparse
import importlib.util
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSETS = ROOT / "assets"


def _load(name):  # by path: `python3 -I` keeps the script dir off sys.path
    sp = importlib.util.spec_from_file_location(name, pathlib.Path(__file__).with_name(name + ".py"))
    m = importlib.util.module_from_spec(sp)
    sp.loader.exec_module(m)
    return m


theme = _load("theme")
wordmark = _load("wordmark")
T = theme.load()
C = {k: theme.rgb(v) for k, v in T.colors.items()}
LIGHT = {k: theme.rgb(v) for k, v in T.modes["light"].items()}


def _pil():
    from PIL import Image, ImageDraw
    return Image, ImageDraw


# ------------------------------------------------------------------ primitives (all sizes in final pixels x ss)

def rays(n, seed=7):
    """Deterministic irregular burst: n (start, end) angles in degrees for the darker rays (also used by the site CSS)."""
    out, a, x = [], 0.0, seed
    step = 360.0 / n
    for _ in range(n):
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        w = step * (0.18 + 0.42 * (x % 1000) / 1000)
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        off = step * 0.25 * ((x % 1000) / 1000 - 0.5)
        out.append((round(a + off, 2), round(a + off + w, 2)))
        a += step
    return out


def burst(im, cx, cy, base, ray, line=None, n=56):
    Image, ImageDraw = _pil()
    d = ImageDraw.Draw(im)
    w, h = im.size
    d.rectangle((0, 0, w, h), fill=base)
    r = 2 * math.hypot(w, h)
    for a0, a1 in rays(n):
        p = [(cx, cy)] + [(cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a))) for a in (a0, a1)]
        d.polygon(p, fill=ray)
    if line:
        for a0, _a1 in rays(n * 2, seed=11)[::3]:
            t = math.radians(a0)
            r0 = 0.18 * math.hypot(w, h)
            d.line([(cx + r0 * math.cos(t), cy + r0 * math.sin(t)), (cx + r * math.cos(t), cy + r * math.sin(t))],
                   fill=line, width=max(1, w // 900))


def stripes(im, y0, h, ss, flip=False, period=150, gap=16, skew=0.6):
    """The red and white stripe frame: red slanted blocks with white gaps, a black rule on the inner edge."""
    Image, ImageDraw = _pil()
    d = ImageDraw.Draw(im)
    w = im.size[0]
    P, G = period * ss, gap * ss
    d.rectangle((0, y0, w, y0 + h), fill=C["stripe_white"])
    k = skew * h
    x = -P
    while x < w + P:
        d.polygon([(x + k, y0), (x + P - G + k, y0), (x + P - G, y0 + h), (x, y0 + h)], fill=C["stripe_red"])
        x += P
    rule = max(2, 4 * ss)
    if flip:
        d.rectangle((0, y0, w, y0 + rule), fill=C["ink"])
    else:
        d.rectangle((0, y0 + h - rule, w, y0 + h), fill=C["ink"])


def slanted(d, x0, y0, x1, y1, lean, fill):
    """A panel with both side edges leaning right by `lean` px from bottom to top."""
    d.polygon([(x0 + lean, y0), (x1 + lean, y0), (x1, y1), (x0, y1)], fill=fill)


def vgrad(w, h, top, bottom):
    Image, _ = _pil()
    g = Image.new("RGB", (1, 256))
    for i in range(256):
        t = i / 255
        g.putpixel((0, i), tuple(round(a + (b - a) * t) for a, b in zip(top, bottom)))
    return g.resize((max(1, int(w)), max(1, int(h))), Image.BILINEAR)


def glow_bar(im, x0, y0, x1, y1, fade=0.18):
    """The highlight bar behind a chosen menu item: yellow into orange, fading out at both ends, hard top and bottom."""
    Image, _ = _pil()
    w, h = int(x1 - x0), int(y1 - y0)
    bar = Image.new("RGBA", (w, h))
    for x in range(w):
        t = x / max(1, w - 1)
        a = min(1.0, t / fade, (1 - t) / fade)
        col = tuple(round(a2 + (b2 - a2) * t) for a2, b2 in zip(C["glow_yellow"], C["glow_orange"]))
        for y in range(h):
            bar.putpixel((x, y), col + (round(255 * max(0.0, a)),))
    im.alpha_composite(bar, (int(x0), int(y0))) if im.mode == "RGBA" else im.paste(bar, (int(x0), int(y0)), bar)


def _outline_mask(size, polys, o):
    Image, ImageDraw = _pil()
    m = Image.new("L", size, 0)
    d = ImageDraw.Draw(m)
    for p in polys:
        d.polygon(p, fill=255)
        if o > 0:
            d.line(p + [p[0]], fill=255, width=int(round(2 * o)), joint="curve")
            for x, y in p:
                d.ellipse((x - o, y - o, x + o, y + o), fill=255)
    return m


def word(im, text, cap, x, y, fill, outline=0, ink=None, slant=wordmark.SLANT):
    """Draw TEXT with its cap box's top-left at (x, y), cap px tall. fill: an RGB or (top, bottom) for a gradient.
    -> width in px."""
    Image, _ = _pil()
    polys, w = wordmark.mask_polys(text, cap, x, y, slant)
    if outline:
        im.paste(ink or C["ink"], (0, 0), _outline_mask(im.size, polys, outline))
    m = _outline_mask(im.size, polys, 0)
    if isinstance(fill[0], tuple):
        g = vgrad(w + 4, cap, fill[0], fill[1])
        layer = Image.new("RGB", im.size, fill[1])
        layer.paste(g, (int(x), int(y)))
        im.paste(layer, (0, 0), m)
    else:
        im.paste(fill, (0, 0), m)
    return w


def star_polys(cx, cy, r, inner=0.48, rot=-90):
    pts = []
    for i in range(10):
        rr = r if i % 2 == 0 else r * inner
        a = math.radians(rot + i * 36)
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    return [pts]


def mark(im, cx, cy, r, outline):
    """The logo mark: a gradient five-point star with a black outline, an outlined white slanted J in it."""
    Image, _ = _pil()
    sp = star_polys(cx, cy, r)
    im.paste(C["ink"], (0, 0), _outline_mask(im.size, sp, outline))
    g = vgrad(2 * r, 2 * r, C["title_top"], C["title_bottom"])
    layer = Image.new("RGB", im.size, C["title_bottom"])
    layer.paste(g, (int(cx - r), int(cy - r)))
    im.paste(layer, (0, 0), _outline_mask(im.size, sp, 0))
    cap = r * 0.78
    _p, jw = wordmark.mask_polys("J", cap, 0, 0)
    word(im, "J", cap, cx - jw / 2 - r * 0.02, cy - cap * 0.42, C["text"], outline * 0.8)


def finish(im, w, h):
    Image, _ = _pil()
    return im.convert("RGB").resize((w, h), Image.LANCZOS)


# ------------------------------------------------------------------ the pieces

def banner(W=2560, H=640, ss=2):
    Image, ImageDraw = _pil()
    w, h = W * ss, H * ss
    im = Image.new("RGB", (w, h))
    burst(im, int(w * 0.16), int(h * 0.55), LIGHT["burst_base"], LIGHT["burst_ray"], LIGHT["burst_line"])
    d = ImageDraw.Draw(im)
    s = ss * W / 2560
    stripes(im, 0, int(52 * s), ss)
    stripes(im, h - int(52 * s), int(52 * s), ss, flip=True)
    slanted(d, 70 * s, 104 * s, 2420 * s, 470 * s, 60 * s, C["ink"])          # the black shadow edge of the panel
    slanted(d, 50 * s, 96 * s, 2400 * s, 458 * s, 60 * s, C["panel"])
    mark(im, 300 * s, 278 * s, 150 * s, 9 * s)
    word(im, "J-STARS VICTORY", 64 * s, 520 * s, 128 * s, C["text"], 6 * s)
    word(im, "VS-EX", 200 * s, 512 * s, 228 * s, (C["title_top"], C["title_bottom"]), 10 * s)
    glow_bar(im, 1430 * s, 300 * s, 2330 * s, 404 * s)
    word(im, "FAN REVIVAL", 62 * s, 1560 * s, 322 * s, C["text"], 6 * s)
    slanted(d, 0, 494 * s, w, 568 * s, 0, C["caption"])                          # caption band
    word(im, "UNOFFICIAL FAN PORT · NO GAME FILES INCLUDED · BRING YOUR OWN COPY", 36 * s, 120 * s, 513 * s,
         C["text"], 0)
    return finish(im, W, H)


def social(W=1280, H=640, ss=3):
    Image, ImageDraw = _pil()
    w, h = W * ss, H * ss
    im = Image.new("RGB", (w, h))
    burst(im, int(w * 0.2), int(h * 0.42), LIGHT["burst_base"], LIGHT["burst_ray"], LIGHT["burst_line"])
    d = ImageDraw.Draw(im)
    s = ss
    stripes(im, 0, 44 * s, ss, period=110, gap=12)
    stripes(im, h - 44 * s, 44 * s, ss, flip=True, period=110, gap=12)
    slanted(d, 62 * s, 116 * s, 1226 * s, 452 * s, 40 * s, C["ink"])
    slanted(d, 48 * s, 108 * s, 1212 * s, 442 * s, 40 * s, C["panel"])
    mark(im, 200 * s, 272 * s, 120 * s, 8 * s)
    word(im, "J-STARS VICTORY", 44 * s, 360 * s, 146 * s, C["text"], 5 * s)
    word(im, "VS-EX", 150 * s, 352 * s, 214 * s, (C["title_top"], C["title_bottom"]), 8 * s)
    glow_bar(im, 300 * s, 470 * s, 1180 * s, 540 * s, fade=0.12)
    word(im, "FAN REVIVAL", 44 * s, 470 * s, 483 * s, C["text"], 5 * s)
    d.rectangle((0, 552 * s, w, 596 * s), fill=C["caption"])
    word(im, "UNOFFICIAL PC PORT · BUILT FROM YOUR OWN COPY", 20 * s, 96 * s, 564 * s, C["text"], 0)
    return finish(im, W, H)


def logo(N=512, ss=3):
    Image, _ = _pil()
    n = N * ss
    im = Image.new("RGB", (n, n))
    burst(im, n // 2, n // 2, LIGHT["burst_base"], LIGHT["burst_ray"], None, n=36)
    mark(im, n / 2, n * 0.53, n * 0.44, n * 0.022)
    return finish(im, N, N)


def divider(kind, W=900, ss=4):
    Image, ImageDraw = _pil()
    H = {"stripe": 22, "glow": 18}[kind]
    w, h = W * ss, H * ss
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    if kind == "stripe":
        P, G, k = 60 * ss, 8 * ss, 0.7 * 14 * ss
        d.rectangle((0, 2 * ss, w, 16 * ss), fill=C["stripe_white"] + (255,))
        x = -P
        while x < w + P:
            d.polygon([(x + k, 2 * ss), (x + P - G + k, 2 * ss), (x + P - G, 16 * ss), (x, 16 * ss)], fill=C["stripe_red"] + (255,))
            x += P
        d.rectangle((0, 16 * ss, w, 19 * ss), fill=C["ink"] + (255,))
        d.rectangle((0, 0, w, 2 * ss), fill=C["ink"] + (255,))
    else:   # glow
        glow_bar(im, 0, 3 * ss, w, 15 * ss, fade=0.22)
        d.polygon([(w * 0.22, 15 * ss), (w * 0.78, 15 * ss), (w * 0.76, 17 * ss), (w * 0.24, 17 * ss)], fill=C["ink"] + (255,))
    return im.resize((W, H), Image.LANCZOS)


def nav_items():
    import json
    d = json.loads((ROOT / "data" / "nav.json").read_text(encoding="utf-8"))
    return d["buttons"], d["wide"]


def button(label, primary=False, W=500, H=112, ss=3):
    """A README menu button at 2x (shown 250 px wide): a slanted dark panel with a white edge and a black drop shadow,
    a red slanted tab on the left, the label in outlined white capitals; the primary one on the yellow-to-orange bar."""
    Image, ImageDraw = _pil()
    w, h = W * ss, H * ss
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    s, lean = ss, 22 * ss
    slanted(d, 12 * s, 14 * s, w - 26 * s, h - 2 * s, lean, C["ink"] + (255,))            # drop shadow
    slanted(d, 2 * s, 4 * s, w - 34 * s, h - 10 * s, lean, C["stripe_white"] + (255,))    # light edge for dark pages
    x0, y0, x1, y1 = 7 * s, 9 * s, w - 39 * s, h - 15 * s
    if primary:
        g = Image.new("RGBA", (x1 - x0 + lean, y1 - y0))
        gw = g.size[0]
        for x in range(gw):
            t = x / max(1, gw - 1)
            col = tuple(round(a + (b - a) * t) for a, b in zip(C["glow_yellow"], C["glow_orange"]))
            ImageDraw.Draw(g).line([(x, 0), (x, g.size[1])], fill=col + (255,))
        m = Image.new("L", im.size, 0)
        slanted(ImageDraw.Draw(m), x0, y0, x1, y1, lean, 255)
        layer = Image.new("RGBA", im.size, (0, 0, 0, 0))
        layer.paste(g, (x0, y0))
        im.paste(layer, (0, 0), m)
    else:
        slanted(d, x0, y0, x1, y1, lean, C["panel"] + (255,))
        slanted(d, x0, y0, x0 + 16 * s, y1, lean, C["stripe_red"] + (255,))
    rgb = Image.new("RGB", im.size)
    rgb.paste(im, (0, 0), im)
    _p, uw = wordmark.mask_polys(label, 10, 0, 0)
    avail = (x1 - x0) - 44 * s
    cap = min(38 * s, avail / (uw / 10))
    _p, tw = wordmark.mask_polys(label, cap, 0, 0)
    tx = x0 + 18 * s + ((x1 - x0) - 18 * s - tw) / 2 + lean / 2
    ty = (y0 + y1) / 2 - cap / 2
    # draw the text on an opaque copy, then take its pixels where the text (with outline) is
    word(rgb, label.upper(), cap, tx, ty, C["text"], 4.5 * s)
    tm = _outline_mask(im.size, wordmark.mask_polys(label, cap, tx, ty)[0], 4.5 * s)
    im.paste(rgb, (0, 0), tm)
    return im.resize((W, H), Image.LANCZOS)


def render_all():
    btns, wide = nav_items()
    nav = {f"nav/{b['id']}.png": button(b["label"], b.get("primary", False)) for b in btns}
    nav[f"nav/{wide['id']}-wide.png"] = button(wide["label"], False, W=1516)
    return {**nav,
        "banner-2x.png": banner(),
        "social-preview.png": social(),
        "logo-mark-512.png": logo(),
        "divider-stripe.png": divider("stripe"),
        "divider-glow.png": divider("glow"),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--out", default=str(ASSETS))
    a = ap.parse_args()
    Image, _ = _pil()
    out = pathlib.Path(a.out)
    files = render_all()
    if a.check:
        bad = []
        for name, im in files.items():
            f = out / name
            if not f.is_file():
                bad.append(name + " (missing)")
                continue
            old = Image.open(f)
            if old.size != im.size or old.mode != im.mode or old.tobytes() != im.tobytes():
                bad.append(name)
        print("repo_art: " + (f"{len(bad)} file(s) missing or changed: {', '.join(bad)}" if bad else
                              f"all {len(files)} files match"))
        return 1 if bad else 0
    for name, im in files.items():
        (out / name).parent.mkdir(parents=True, exist_ok=True)
        im.save(out / name, optimize=True)
    print(f"repo_art: wrote {len(files)} files to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
