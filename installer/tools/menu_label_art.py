#!/usr/bin/env python3
"""Emit the "Training" label of the main menu's sub-menu panel (toolkit patch 2516) and the "Port Settings" label (patch 2518) as raw RGBA files.

Our own art: the letters come from Fira Sans Heavy Italic (SIL Open Font License 1.1, https://github.com/
mozilla/Fira; pinned by SHA-256 below; the font file is not in the repository: put FiraSans-HeavyItalic.ttf in build/installer/fonts/), drawn heavier and in the game's panel-label style (measured on
the game's own 512 x 64 labels, no pixels of them are read or kept here):
  off: white-to-grey vertical gradient letters, ~2 px black rim, soft 1 px edge;
  on:  black letters with a ~3 px soft white glow.
Cap height 21 px, baseline at y 29, left edge of the letters at x 5, as the game's labels.
Output: OUT_DIR/menu_training_{off,on}.rgba and menu_port_settings_{off,on}.rgba, 512 x 64 straight-alpha RGBA8, rows top to bottom.

  python3 -I installer/tools/menu_label_art.py OUT_DIR [FONT.ttf] [--preview PNG]     (needs Pillow and numpy)
"""
import hashlib, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops

FONT_SHA256 = "a30804a11e8ad1b38d6cabc725644a1048aab6fb7e2811507f0e208a469c8ef9"   # FiraSans-HeavyItalic.ttf (fira 4.301)
TEXT = "Training"
LABELS = [("Training", "menu_training"), ("Port Settings", "menu_port_settings")]   # (text, file stem); each as _off/_on.rgba
W, H, SS = 512, 64, 4          # texture size, supersampling
CAP, BASE, LEFT = 21, 29, 5    # cap height, baseline row, left edge of the letters (texture pixels)
EXTRA = 0.4                    # extra weight (px each side) on top of Heavy: the game's letters are heavier
SHEAR = 0.12                  # extra slant (tan) added to the font's own: the game's letters lean more
SIZE = 31                      # px/em giving ~CAP px caps in this font

def find_font(arg):
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    cands = [arg] if arg else []
    cands += [os.path.join(root, "build", "installer", "fonts", "FiraSans-HeavyItalic.ttf"), "FiraSans-HeavyItalic.ttf"]
    for p in cands:
        if p and os.path.exists(p):
            if hashlib.sha256(open(p, "rb").read()).hexdigest() != FONT_SHA256:
                sys.exit("font %s does not match the pinned SHA-256" % p)
            return p
    sys.exit("FiraSans-HeavyItalic.ttf not found (pass its path)")

def letters(font, text=None, w=None, h=None, left=None, base=None):
    """Mask (supersampled, 'L') of the text with the cap height scaled to CAP texture pixels."""
    text = TEXT if text is None else text
    w = W if w is None else w; h = H if h is None else h
    left = LEFT if left is None else left; base = BASE if base is None else base
    m = Image.new("L", (w * SS, h * SS), 0)
    d = ImageDraw.Draw(m)
    # cap-height of the font at SIZE*SS: measure "T"
    f = ImageFont.truetype(font, SIZE * SS)
    t = f.getbbox("T"); capnow = t[3] - t[1]
    f = ImageFont.truetype(font, int(round(SIZE * SS * CAP * SS / capnow)))
    bb = f.getbbox(text, anchor="ls")
    d.text((left * SS - bb[0], base * SS), text, font=f, fill=255, anchor="ls",
           stroke_width=int(EXTRA * SS), stroke_fill=255)
    k = SHEAR   # x' = x - k (y - baseline): input x = x' + k (y - baseline)
    return m.transform(m.size, Image.AFFINE, (1, k, -k * base * SS, 0, 1, 0), Image.BICUBIC)

def styles(m, w=None, h=None, base=None):
    """The two label styles from a supersampled mask: [off, on] straight-alpha RGBA8 arrays (h, w, 4)."""
    w = W if w is None else w; h = H if h is None else h; base = BASE if base is None else base
    # off: gradient fill inside a black rim
    rim = m.filter(ImageFilter.MaxFilter(2 * int(2.5 * SS) + 1))   # ~2.5 px rim (dilate)
    rim = rim.filter(ImageFilter.GaussianBlur(SS * 0.5))
    ys = np.arange(h * SS)[:, None] / SS
    g = np.clip((ys - (base - CAP)) / CAP, 0, 1)
    shade = np.where(g < 0.45, 255.0, 255.0 - (g - 0.45) / 0.55 * 70.0)       # white top half, to grey 185 at the baseline
    shade = np.broadcast_to(shade, (h * SS, w * SS))
    fill_a = np.asarray(m.filter(ImageFilter.GaussianBlur(SS * 0.35)), dtype=float) / 255
    rim_a = np.asarray(rim, dtype=float) / 255
    a_out = np.maximum(rim_a, fill_a)
    col = shade * fill_a                                                       # premultiplied by the fill; rim is black
    off = np.zeros((h * SS, w * SS, 4))
    off[..., 3] = a_out * 255
    off[..., 0] = off[..., 1] = off[..., 2] = np.where(a_out > 0, col / np.maximum(a_out, 1e-6), 255)
    # on: black letters, white glow
    glow = m.filter(ImageFilter.MaxFilter(SS * 2 + 1)).filter(ImageFilter.GaussianBlur(SS * 1.6))
    glow_a = np.clip(np.asarray(glow, dtype=float) / 255 * 1.5, 0, 1) * 0.85
    on = np.zeros((h * SS, w * SS, 4))
    fa = np.asarray(m.filter(ImageFilter.GaussianBlur(SS * 0.35)), dtype=float) / 255
    oa = fa + glow_a * (1 - fa)
    on[..., 0] = on[..., 1] = on[..., 2] = np.where(oa > 0, 255 * glow_a * (1 - fa) / np.maximum(oa, 1e-6), 0)
    on[..., 3] = oa * 255
    res = []
    for arr in (off, on):
        # premultiplied box downsample, then back to straight alpha
        a = arr[..., 3:4] / 255
        pm = np.concatenate([arr[..., :3] * a, arr[..., 3:4]], axis=2)
        pm = pm.reshape(h, SS, w, SS, 4).mean(axis=(1, 3))
        al = pm[..., 3:4]
        rgb = np.where(al > 0.5, pm[..., :3] / np.maximum(al, 1e-6) * 255, 0)
        res.append(np.clip(np.concatenate([rgb, al], axis=2) + 0.5, 0, 255).astype(np.uint8))
    return res

def main():
    args = [a for a in sys.argv[1:]]
    prev = None
    if "--preview" in args:
        i = args.index("--preview"); prev = args[i + 1]; del args[i:i + 2]
    out = args[0]; font = find_font(args[1] if len(args) > 1 else None)
    os.makedirs(out, exist_ok=True)
    res = []
    for text, stem in LABELS:      # ENHANCEMENT: one more label, the Options panel's 6th row
        for name, img in zip(("off", "on"), styles(letters(font, text))):
            open(os.path.join(out, "%s_%s.rgba" % (stem, name)), "wb").write(img.tobytes())
            if stem == "menu_training": res.append(Image.fromarray(img, "RGBA"))
    if prev:
        P = Image.new("RGBA", (W * 2, H), (92, 92, 130, 255))
        for i, r in enumerate(res): P.paste(r, (i * W, 0), r)
        P.crop((0, 0, 1024, 64)).resize((2048, 128), Image.NEAREST).save(prev)
    print("wrote", out)

if __name__ == "__main__":
    main()
