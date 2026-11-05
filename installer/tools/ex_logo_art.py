#!/usr/bin/env python3
"""The "EX" that replaces the "+" in the title logo (toolkit patch 0006), drawn from polygons: our own art, nothing of the
game is read or embedded.

Slanted E and X with, from the outside in, a thin gold rim, a black outline, a red bevel in four facets and an off-white
face with a stepped shade and a faint deterministic grain, rendered 4x supersampled and box-filtered down. The output is
the raw file the patch loads: 156 x 88 straight-alpha RGBA8, rows top to bottom, no header, for the block-aligned
rectangle x 748..903, y 416..503 of the 1024 x 531 logo texture. Needs Pillow and numpy (`pip install pillow numpy`).

    python installer/tools/ex_logo_art.py OUT.rgba            # write the file
    python installer/tools/ex_logo_art.py --check OUT.rgba    # render again, exit 1 when the file is missing or differs

Deterministic: same script + Pillow + numpy = same bytes. REFERENCE_SHA256 is the file as rendered with Pillow 12.3.0 and
numpy 2.5.3; another version may round a few edge pixels differently (the patch reads any 156 x 88 file).
"""
import argparse
import hashlib
import math
import pathlib
import sys

import numpy as np
from PIL import Image, ImageDraw

REFERENCE_SHA256 = "3a85fec556cc4af0ebb22c540d791aeb3144065a5201168e51fabb1d96d09884"

SS = 4                      # supersampling factor
SLANT = 0.24                # x shift per unit of height
CROSS = (751, 428, 825, 502)  # the rectangle the patch clears, [x0, y0, x1, y1)
BOTTOM = 499.5              # bottom edge of the cleared "+"
RECT = (748, 416, 156, 88)  # x, y, w, h of the output in logo pixels (whole 4 x 4 blocks)

GOLD = (246, 190, 0)
BLACK = (0, 0, 0)
FACE = (238, 229, 222)
FACETS = {"top": (244, 0, 24), "left": (170, 0, 10), "bottom": (226, 0, 16), "right": (100, 0, 8)}
HI_PINK = (197, 125, 131)   # light edge on top/left facets
IN_DARK = (106, 32, 41)     # dark inner edge on top/left facets

# letter shapes, design height 70 (unslanted; x right, y down)
E_POLY = [(6, 0), (54, 0), (54, 18), (22, 18), (22, 26), (46, 26), (46, 44), (22, 44), (22, 52), (54, 52),
          (54, 70), (6, 70), (0, 64), (0, 6)]
X_POLY = [(0, 0), (24, 0), (33, 22), (42, 0), (66, 0), (46, 35), (66, 70), (42, 70), (33, 48), (24, 70), (0, 70),
          (20, 35)]
E_W, X_W = 54, 66


def letters(h, gap, x0, ybottom):
    """E and X polygons for design height h, sheared, with the bottom-left of the E at (x0, ybottom)."""
    s = h / 70.0
    out, cx = [], 0.0
    for poly, w in ((E_POLY, E_W), (X_POLY, X_W)):
        pts = [((cx + px * s), (py * s)) for px, py in poly]
        out.append([(x0 + x + SLANT * (h - y), ybottom - h + y) for x, y in pts])
        cx += w * s + gap
    return out


def _orient(p):
    a = sum(p[i][0] * p[(i + 1) % len(p)][1] - p[(i + 1) % len(p)][0] * p[i][1] for i in range(len(p)))
    return 1 if a > 0 else -1


def _normals(p):
    """Inward unit normals of each edge i (p[i] -> p[i+1])."""
    sg, n = _orient(p), len(p)
    res = []
    for i in range(n):
        ex, ey = p[(i + 1) % n][0] - p[i][0], p[(i + 1) % n][1] - p[i][1]
        l = math.hypot(ex, ey)
        res.append((sg * -ey / l, sg * ex / l))
    return res


def miter(p, d):
    """Vertices of the polygon offset by d (d > 0 inward, d < 0 outward) with mitred corners."""
    nm, n, out = _normals(p), len(p), []
    for i in range(n):
        a, b = nm[i - 1], nm[i]
        k = 1 + a[0] * b[0] + a[1] * b[1]
        k = max(k, 0.35)                              # miter limit
        out.append((p[i][0] + d * (a[0] + b[0]) / k, p[i][1] + d * (a[1] + b[1]) / k))
    return out


def facet(nrm):
    """Facet name from the outward normal of an edge (inward normal negated)."""
    ox, oy = -nrm[0], -nrm[1]
    if oy < -0.55:
        return "top"
    if ox < -0.5:
        return "left"
    if oy > 0.55:
        return "bottom"
    return "right"


class Canvas:
    def __init__(self, x0, y0, w, h):
        self.x0, self.y0, self.w, self.h = x0, y0, w, h
        self.img = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.img)
        self.face = Image.new("L", (w * SS, h * SS), 0)      # textured-face coverage
        self.fd = ImageDraw.Draw(self.face)

    def pt(self, p):
        return ((p[0] - self.x0) * SS, (p[1] - self.y0) * SS)

    def poly(self, pts, col, face=False):
        q = [self.pt(p) for p in pts]
        self.d.polygon(q, fill=tuple(col) + (255,))
        if face:
            self.fd.polygon(q, fill=255)

    def ring(self, polys, d, col):
        """Outward band 0..d around each polygon, as a union of mitred trapezoids."""
        for p in polys:
            o, n = miter(p, -d), len(p)
            for i in range(n):
                self.poly([p[i], p[(i + 1) % n], o[(i + 1) % n], o[i]], col)
            self.poly(p, col)

    def band(self, p, d0, d1, col_for_edge):
        """Inward band d0..d1 of polygon p, each edge's trapezoid coloured by col_for_edge(i) (None = skip)."""
        a, b, n = miter(p, d0), miter(p, d1), len(p)
        for i in range(n):
            c = col_for_edge(i)
            if c is not None:
                self.poly([a[i], a[(i + 1) % n], b[(i + 1) % n], b[i]], c)

    def finish(self):
        """Downsample to 1x (premultiplied box filter); returns float RGBA array and face coverage."""
        a = np.asarray(self.img, dtype=np.float64) / 255.0
        pre = a[..., :3] * a[..., 3:4]
        h, w = self.h, self.w
        pre = pre.reshape(h, SS, w, SS, 3).mean((1, 3))
        al = a[..., 3].reshape(h, SS, w, SS).mean((1, 3))
        rgb = np.where(al[..., None] > 0, pre / np.maximum(al[..., None], 1e-9), 0)
        f = np.asarray(self.face, dtype=np.float64).reshape(h, SS, w, SS).mean((1, 3)) / 255.0
        return np.dstack([rgb * 255, al * 255]), f


def draw_shapes(cv, polys, gold, black, bevel):
    """Layered build of one set of shapes: gold rim, black outline, red bevel (4 facets), face."""
    cv.ring(polys, black + gold, GOLD)
    cv.ring(polys, black, BLACK)
    for p in polys:
        cv.poly(p, FACETS["bottom"])
        nm = _normals(p)
        fc = [facet(n) for n in nm]
        cv.band(p, 0, bevel, lambda i: FACETS[fc[i]])
        cv.band(p, 0, 1.0, lambda i: HI_PINK if fc[i] in ("top", "left") else None)
        cv.band(p, bevel - 1.0, bevel, lambda i: IN_DARK if fc[i] in ("top", "left") else None)
        cv.poly(miter(p, bevel), FACE, face=True)


def texture(face_cov, y0, ytop, ybot, seed):
    """Deterministic face shade (h, w, 3) in levels: a stepped darkening over the lower part of the face plus a faint
    hairline grain."""
    h, w = face_cov.shape
    delta = np.zeros((h, w, 3))
    ys = (np.arange(h) + y0 + 0.5)
    t = np.clip((ys - (ytop + 0.55 * (ybot - ytop))) / (0.45 * (ybot - ytop)), 0, 1)
    t = np.floor(t * 8) / 8                                  # steps, not a smooth gradient
    delta += (t[:, None, None] * np.array([-52, -68, -73])[None, None, :])
    rng = np.random.RandomState(seed)
    g = Image.new("L", (w, h), 128)
    gd = ImageDraw.Draw(g)
    for _ in range(int(w * h / 38)):                         # hairlines, mostly diagonal like a brushed crosshatch
        x, y = rng.randint(0, w), rng.randint(0, h)
        ln = rng.randint(4, 11)
        dx, dy = ((1, -1), (1, 1), (1, 0))[rng.choice(3, p=[0.45, 0.35, 0.2])]
        v = int(rng.choice([-8, -6, 5, 7]))
        gd.line([(x, y), (x + dx * ln, y + dy * ln)], fill=128 + v)
    delta += (np.asarray(g, dtype=np.float64) - 128)[..., None]
    return delta * face_cov[..., None]


def render():
    """The EX as 156 x 88 RGBA bytes: letters 64 high, 6 apart, inside a black 5 + gold 2 ring, bevel 5."""
    ring, h, bevel, seed = 7, 64, 5, 11
    polys = letters(h, 6, CROSS[0] + 2 + ring, BOTTOM - ring)
    x0, y0, w, hh = RECT
    cv = Canvas(x0, y0, w, hh)
    draw_shapes(cv, polys, 2, 5, bevel)
    ys = [q[1] for p in polys for q in miter(p, bevel)]
    rgba, fcov = cv.finish()
    rgba[..., :3] += texture(fcov, y0, min(ys), max(ys), seed) * (rgba[..., 3:4] / 255.0)
    return np.clip(np.rint(rgba), 0, 255).astype(np.uint8).tobytes()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out", help="the .rgba file to write (or to compare with --check)")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args(argv)
    data, out = render(), pathlib.Path(a.out)
    digest = hashlib.sha256(data).hexdigest()
    if a.check:
        ok = out.is_file() and out.read_bytes() == data
        print(f"{out}: {'matches' if ok else 'missing or differs'}")
        return 0 if ok else 1
    out.write_bytes(data)
    note = "same as the reference" if digest == REFERENCE_SHA256 else "differs from the reference (another Pillow/numpy?)"
    print(f"wrote {out} ({len(data)} bytes, sha256 {digest[:16]}..., {note})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
