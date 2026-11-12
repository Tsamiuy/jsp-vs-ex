#!/usr/bin/env python3
"""Emit the ASCII glyph atlas in the main menu's label style (ENHANCEMENT, toolkit patch 2517) for the port's overlay text.

Our own art, made the way the "Training" label is (menu_label_art.py: Fira Sans Heavy Italic, SIL OFL 1.1, pinned by SHA-256 there;
heavier, sheared, cap height 21 px): per glyph, style 0 = "off" (white-to-grey letters, black rim) and style 1 = "on" (black letters,
white glow). Printable ASCII 0x21-0x7E; 0x20 has only an advance. No game pixels are read or kept.

File menu_font.jmf (little endian), loaded by libs/video/jsp_overlay.h:
  header, 10 x u32: 'JMF1', atlas_w, atlas_h, nstyles(6), first(0x20), count(95), cell_h, baseline(row of the baseline in a cell),
                    style_stride (rows per style in the atlas), cap (cap height in px, 21)
  count x 8 x u16: x, y (cell origin in style 0), w (cell width; cell height is cell_h), pen_x (pen origin column in the cell,
                    signed), advance (1/16 px), 0, 0, 0
  atlas: atlas_w x atlas_h straight-alpha RGBA8 rows top to bottom; style s is at y + s * style_stride.
                    Styles: 0 off, 1 on (rim and body merged, the single-pass picture), 2 off rim (black), 3 off body, 4 on glow (white), 5 on body (black).
                    A text string is blitted in two passes, plane 2+2k for every glyph, then plane 3+2k for every glyph (k = 0 off, 1 on), so a
                    glyph's rim never covers the previous glyph's body; a file with nstyles < 6 is drawn in one pass with style 0/1.
Draw a glyph at pen (px, baseline by) with its cell at (px - pen_x, by - baseline).

  python3 -I installer/tools/menu_font_art.py OUT_DIR [FONT.ttf] [--preview PNG]     (needs Pillow and numpy)
"""
import importlib.util, math, os, struct, sys
import numpy as np
from PIL import Image, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("menu_label_art", os.path.join(HERE, "menu_label_art.py"))
mle = importlib.util.module_from_spec(spec); spec.loader.exec_module(mle)

PAD = 8            # blank margin around the ink (rim 2.5 px + glow ~5 px)
BASELINE = 34      # baseline row inside a cell
CELL_H = 52        # 34 above (ascenders ~25 + pad), 18 below (descenders ~7 + pad)
ATLAS_W = 1024
FIRST, LAST = 0x20, 0x7E
# The glyph cells used to be drawn one after the other, each alpha-over the last, and every letter carries extra weight (EXTRA 0.4 px a side), extra
# shear and a black rim of ~2.5 px + blur, so the next letter's rim ate the body of the previous one. The overlay now blits a string in two passes
# (all rims, then all bodies, planes 2-5), so only a body can cover a body. TRACK_PX is added to every advance except the space's: the smallest
# 0.5 px step at which no adjacent pair of the sample strings of installer/tools/port_overlay_test.c (two-pass model) has any body pixel covered.
TRACK_PX = 1.0

def main():
    args = sys.argv[1:]
    prev = None
    if "--preview" in args:
        i = args.index("--preview"); prev = args[i + 1]; del args[i:i + 2]
    out = args[0]; font = mle.find_font(args[1] if len(args) > 1 else None)
    SS = mle.SS
    f = ImageFont.truetype(font, mle.SIZE * SS)
    t = f.getbbox("T"); capnow = t[3] - t[1]
    f = ImageFont.truetype(font, int(round(mle.SIZE * SS * mle.CAP * SS / capnow)))
    glyphs = []     # (cw, adv16, pen_x, [off, on])
    for c in range(FIRST, LAST + 1):
        ch = chr(c); adv = f.getlength(ch) / SS
        if ch == " ":
            glyphs.append((0, int(round(adv * 16)), 0, None)); continue
        bb = f.getbbox(ch, anchor="ls")
        ink = (bb[2] - bb[0]) / SS
        cw = int(math.ceil(ink)) + 2 * PAD + 5      # +5: the shear leans the top right
        m = mle.letters(font, ch, cw, CELL_H, PAD, BASELINE)
        off, on = mle.styles(m, cw, CELL_H, BASELINE)
        pl = mle.planes(m, cw, CELL_H, BASELINE)
        for a in (off, on, *pl):                          # nothing may touch the cell border
            al = a[..., 3]
            assert al[0].max() == 0 and al[-1].max() == 0 and al[:, 0].max() == 0 and al[:, -1].max() == 0, ch
        glyphs.append((cw, int(round((adv + TRACK_PX) * 16)), int(round(PAD - bb[0] / SS)), [off, on, *pl]))
    # shelf packing, style 0
    x = y = 0; pos = []
    for cw, _, _, _ in glyphs:
        if x + cw > ATLAS_W: x = 0; y += CELL_H
        pos.append((x, y)); x += cw
    stride = y + CELL_H
    NST = 6
    H = stride * NST
    atlas = np.zeros((H, ATLAS_W, 4), np.uint8)
    for (cw, _, _, im), (px, py) in zip(glyphs, pos):
        if im is None: continue
        for s in range(NST):
            atlas[py + s * stride: py + s * stride + CELL_H, px:px + cw] = im[s]
    hdr = struct.pack("<4s9I", b"JMF1", ATLAS_W, H, NST, FIRST, LAST - FIRST + 1, CELL_H, BASELINE, stride, mle.CAP)
    met = b"".join(struct.pack("<8H", p[0], p[1], g[0], g[2] & 0xFFFF, g[1], 0, 0, 0) for g, p in zip(glyphs, pos))
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, "menu_font.jmf"), "wb").write(hdr + met + atlas.tobytes())
    print("wrote", out, "atlas %dx%d, %d glyphs" % (ATLAS_W, H, len(glyphs)))
    if prev:
        P = Image.new("RGBA", (ATLAS_W, H), (92, 92, 130, 255))
        A = Image.fromarray(atlas, "RGBA"); P.paste(A, (0, 0), A); P.save(prev)

main()
