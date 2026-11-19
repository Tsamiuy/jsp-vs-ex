#!/usr/bin/env python3
"""The J-Stars Victory VS-EX wordmark: our own blocky, chamfered capitals, drawn as polygons (no font file).

Each glyph is a list of polygons on a grid 10 units high (y down), built from bars 3 units thick with 45-degree cut
corners; overlapping pieces are fine (they are drawn as a union). A slant shears the line to the right like a heavy
italic. The same outlines go to the site (inline SVG) and to the PNG art (tools/site/repo_art.py, installer art).

    from wordmark import layout, svg, mask_polys
    polys, width = layout("VS-EX", slant=0.22)      # polygons in grid units, slanted, and the advance width
    svg("VS-EX", height_px=64, outline=3, fill="url(#g)", ink="#000")   # an <svg> element (string)

CLI: python3 -I tools/site/wordmark.py TEXT   # prints the SVG
Stdlib only. Letters: A-Z (no Q), 0-9, space, '-', '.', '·', '+', '/'.
"""
import sys

H = 10.0          # cap height in grid units
GAP = 1.3         # space between letters
SLANT = 0.22      # x shift per unit of height (about 12 degrees)


def R(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


TOP_L = [(2, 0), (8, 0), (8, 3), (0, 3), (0, 2)]               # top bar, top-left corner cut
TOP_R = [(0, 0), (6, 0), (8, 2), (8, 3), (0, 3)]               # top bar, top-right corner cut
TOP_LR = [(2, 0), (6, 0), (8, 2), (8, 3), (0, 3), (0, 2)]
BOT_L = [(0, 7), (8, 7), (8, 10), (2, 10), (0, 8)]             # bottom bar, bottom-left corner cut
BOT_R = [(0, 7), (8, 7), (8, 8), (6, 10), (0, 10)]
BOT_LR = [(0, 7), (8, 7), (8, 8), (6, 10), (2, 10), (0, 8)]
STEM = R(0, 0, 3, 10)
RSTEM = R(5, 0, 8, 10)

GLYPHS = {
    "A": (8, [TOP_LR, R(0, 2, 3, 10), R(5, 2, 8, 10), R(0, 5, 8, 7.5)]),
    "B": (8, [TOP_R, STEM, R(5, 2, 8, 8), R(0, 4.25, 7.5, 5.75), BOT_R]),
    "C": (8, [TOP_L, R(0, 2, 3, 8), BOT_L]),
    "D": (8, [TOP_R, STEM, R(5, 2, 8, 8), BOT_R]),
    "E": (8, [TOP_L, R(0, 2, 3, 8), R(0, 3.75, 7, 6.25), BOT_L]),
    "F": (8, [TOP_L, R(0, 2, 3, 10), R(0, 4, 7, 6.5)]),
    "G": (8, [TOP_L, R(0, 2, 3, 8), BOT_L, R(5, 4.5, 8, 10), R(4, 4.5, 8, 6.75)]),
    "H": (8, [STEM, RSTEM, R(0, 3.75, 8, 6.25)]),
    "I": (3, [STEM]),
    "J": (8, [[(5, 0), (8, 0), (8, 8), (6, 10), (5, 10)], [(0, 6), (3, 6), (3, 7), (6, 7), (6, 10), (2, 10), (0, 8)]]),
    "K": (8, [STEM, [(5, 0), (8, 0), (8, 1.5), (4.6, 5.6), (2.4, 5.6), (2.4, 4.6)], [(2.4, 4.4), (4.6, 4.4), (8, 8.5), (8, 10), (5, 10), (2.4, 6.6)]]),
    "L": (8, [R(0, 0, 3, 8), BOT_L]),
    "M": (9, [STEM, R(6, 0, 9, 10), [(0, 0), (3, 0), (4.5, 3.5), (6, 0), (9, 0), (9, 1.5), (5.6, 6.5), (3.4, 6.5), (0, 1.5)]]),
    "N": (8, [STEM, RSTEM, [(0, 0), (3, 0), (8, 7), (8, 10), (5, 10), (0, 3)]]),
    "O": (8, [TOP_LR, R(0, 2, 3, 8), R(5, 2, 8, 8), BOT_LR]),
    "P": (8, [TOP_R, STEM, R(5, 2, 8, 6.5), R(0, 5, 7, 7.5)]),
    "R": (8, [TOP_R, STEM, R(5, 2, 8, 5), [(0, 4.5), (8, 4.5), (8, 5.5), (6.6, 6.9), (0, 6.9)], R(5, 6.4, 8, 10)]),
    "S": (8, [TOP_L, R(0, 2, 3, 6), R(0, 3.5, 8, 6.5), R(5, 3.5, 8, 8), BOT_R]),
    "T": (8, [R(0, 0, 8, 3), R(2.5, 0, 5.5, 10)]),
    "U": (8, [R(0, 0, 3, 8), RSTEM[:2] + [(8, 8), (5, 8)], BOT_LR]),
    "V": (8, [[(0, 0), (3, 0), (3, 6), (4, 7.4), (5, 6), (5, 0), (8, 0), (8, 7), (5, 10), (3, 10), (0, 7)]]),
    "W": (9, [STEM, R(6, 0, 9, 10), [(0, 8.5), (3.4, 3.5), (5.6, 3.5), (9, 8.5), (9, 10), (6, 10), (4.5, 6.5), (3, 10), (0, 10)]]),
    "X": (8, [[(0, 0), (3, 0), (4, 3), (5, 0), (8, 0), (5.8, 5), (8, 10), (5, 10), (4, 7), (3, 10), (0, 10), (2.2, 5)]]),
    "Y": (8, [[(0, 0), (3, 0), (3, 3), (5, 3), (5, 0), (8, 0), (8, 5), (6.5, 6.5), (5.5, 6.5), (5.5, 10), (2.5, 10), (2.5, 6.5), (1.5, 6.5), (0, 5)]]),
    "Z": (8, [R(0, 0, 8, 3), [(5, 3), (8, 3), (3, 7), (0, 7)], R(0, 7, 8, 10)]),
    "0": (8, [TOP_LR, R(0, 2, 3, 8), R(5, 2, 8, 8), BOT_LR]),
    "1": (5, [[(0, 0), (5, 0), (5, 10), (2, 10), (2, 3), (0, 3)]]),
    "2": (8, [TOP_R, R(5, 2, 8, 6), [(0, 3.5), (8, 3.5), (8, 6.5), (3, 6.5), (3, 7), (0, 7)], R(0, 7, 8, 10)]),
    "3": (8, [TOP_R, R(5, 2, 8, 8), R(2, 3.75, 8, 6.25), BOT_R]),
    "4": (8, [R(0, 0, 3, 6.5), R(0, 4, 8, 6.5), RSTEM]),
    "5": (8, [R(0, 0, 8, 3), R(0, 2, 3, 6), R(0, 3.5, 8, 6.5), R(5, 3.5, 8, 8), BOT_R]),
    "6": (8, [TOP_L, R(0, 2, 3, 8), R(0, 3.75, 8, 6.25), R(5, 3.75, 8, 8), BOT_LR]),
    "7": (8, [R(0, 0, 8, 3), [(5, 0), (8, 0), (8, 3), (5, 10), (2, 10), (5, 3)]]),
    "8": (8, [TOP_LR, R(0, 2, 3, 8), R(5, 2, 8, 8), R(0, 4, 8, 6), BOT_LR]),
    "9": (8, [TOP_LR, R(0, 2, 3, 6), R(0, 3.75, 8, 6.25), R(5, 2, 8, 8), BOT_R]),
    "-": (5, [R(0, 3.75, 5, 6.25)]),
    ".": (3, [R(0, 7, 3, 10)]),
    "·": (3, [R(0, 3.75, 3, 6.25)]),
    "+": (7, [R(0, 3.75, 7, 6.25), R(2.25, 1.5, 4.75, 8.5)]),
    "/": (6, [[(4, 0), (6, 0), (2, 10), (0, 10)]]),
    " ": (4, []),
}


def _area(p):
    return sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(p, p[1:] + p[:1])) / 2


def layout(text, slant=SLANT, gap=GAP):
    """-> (polygons in grid units after the slant, total width including the slant overhang)."""
    out, x = [], 0.0
    text = text.upper()
    for i, ch in enumerate(text):
        w, polys = GLYPHS[ch]
        for p in polys:
            if _area(p) < 0:          # one winding for every piece, so a nonzero SVG fill never cuts a hole
                p = p[::-1]
            out.append([(x + px + (H - py) * slant, py) for px, py in p])
        x += w + (gap if i < len(text) - 1 else 0)
    return out, x + H * slant


def path_d(polys, scale, ox=0.0, oy=0.0):
    def f(v):
        s = f"{v:.2f}".rstrip("0").rstrip(".")
        return s if s != "-0" else "0"
    return "".join("M" + "L".join(f"{f(ox + x * scale)} {f(oy + y * scale)}" for x, y in p) + "Z" for p in polys)


def svg(text, height_px, outline=0.0, fill="#fff", ink="#000", slant=SLANT, cls=None, label=None):
    """An inline <svg> of TEXT, height_px tall plus the outline. The outline is a stroke drawn under the fill, so the
    seams of overlapping pieces never show. fill may be a url(#id) whose gradient the caller defines."""
    polys, w = layout(text, slant)
    s = height_px / H
    pad = outline
    vw, vh = w * s + 2 * pad, height_px + 2 * pad
    d = path_d(polys, s, pad, pad)
    a = f' class="{cls}"' if cls else ""
    lab = f' role="img" aria-label="{label}"' if label else ' aria-hidden="true"'
    under = (f'<path d="{d}" fill="{ink}" stroke="{ink}" stroke-width="{2 * outline:.2f}" stroke-linejoin="round"/>'
             if outline else "")
    return (f'<svg{a} xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {vw:.1f} {vh:.1f}" width="{vw:.0f}" '
            f'height="{vh:.0f}"{lab} focusable="false">{under}<path d="{d}" fill="{fill}"/></svg>')


def mask_polys(text, height_px, x0, y0, slant=SLANT):
    """Polygons in pixels for a raster drawer, top-left of the cap box at (x0, y0); -> (polygons, width_px)."""
    polys, w = layout(text, slant)
    s = height_px / H
    return [[(x0 + x * s, y0 + y * s) for x, y in p] for p in polys], w * s


if __name__ == "__main__":
    print(svg(" ".join(sys.argv[1:]) or "VS-EX", 64, 3, "#FFB000"))
