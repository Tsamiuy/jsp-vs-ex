#!/usr/bin/env python3
"""Two-page Main Menu row labels in es, fr, de and it: our own art, no game pixels.

One command regenerates every picture from the text table in installer/port/jsp/jsp_menu_lang.h (the one place the words live: a reviewed
translation is edited there, then this is run again):

  python3 -I installer/tools/mp_label_langs.py OUT_DIR [FONT.ttf]            writes mp_label_<lang>_<name>_{light,dark}.inc
  python3 -I installer/tools/mp_label_langs.py --check [FONT.ttf]            glyph check only: every character of every label has a glyph
  python3 -I installer/tools/mp_label_langs.py --preview PNG OUT_DIR [FONT]  also a contact sheet of the pictures

<name> is battle, training, impossible, portset (MENU_BATTLE_MODES, MENU_TRAINING_MODE, MENU_IMPOSSIBLE_MODE, MENU_PORT_SETTINGS).
Each picture is a 300 x 43 DXT5 body (13200 bytes, 4x4 blocks in rows, as the .inc files of the English labels, packed like
docs/archive/jstars-notes/tools/dxt5enc_any.py), light = the normal look (white letters, black rim), dark = the highlighted look (black
letters, white glow). The letters come from menu_label_art.py (Fira Sans Heavy Italic, SIL OFL 1.1, pinned by SHA-256 there), at its cap
height and slant; a label wider than the picture is squeezed sideways to fit.

NOT RUN YET (written in a quiet window with no Pillow in the shell): the sizes LEFT 10 / BASE 31, the light = black-clear and dark = white-clear
choice of the clear colour, and the match of the look with the English labels (those were drawn from Impact by mm_label.ps1, not from Fira Sans)
are ASSUMPTIONS to check against a screenshot. The .inc files are not in the tree, so jsp_menu_lang.h keeps labels_ready false for these languages.
Also open before a language can be switched on: the page-1 title picture ("Battle Modes", 512 x 64) and the help-bar watermark are English pictures too.
"""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

W, H, LEFT, BASE = 300, 43, 10, 31
LANGS = ["en", "es", "fr", "de", "it"]
LABELS = [("MENU_BATTLE_MODES", "battle"), ("MENU_TRAINING_MODE", "training"), ("MENU_IMPOSSIBLE_MODE", "impossible"), ("MENU_PORT_SETTINGS", "portset")]

def table(header):
    """{id: {lang: text}} from the raw string in jsp_menu_lang.h (tab-separated, UTF-8)."""
    src = open(header, encoding="utf-8").read()
    body = re.search(r'R"MSG\(\n(.*?)\)MSG"', src, re.S).group(1)
    out = {}
    for line in body.splitlines():
        cells = line.split("\t")
        if len(cells) == 6:
            out[cells[0]] = dict(zip(["en", "es", "fr", "de", "it"], cells[1:]))
    return out

def to565(c): return ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)
def c565(v):
    r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))

def dxt5(rgba, clear):
    """rgba: (h, w, 4) uint8 straight alpha -> DXT5 bytes (the block coder of dxt5enc_any.py; texels outside the picture are clear, alpha 0)."""
    h, w = rgba.shape[:2]; out = bytearray()
    def px(x, y):
        if x >= w or y >= h: return clear + (0,)
        return tuple(int(v) for v in rgba[y, x])
    for by in range((h + 3) // 4):
        for bx in range((w + 3) // 4):
            p = [px(bx * 4 + i % 4, by * 4 + i // 4) for i in range(16)]
            al = [q[3] for q in p]; a0, a1 = max(al), min(al); abits = 0
            if a0 != a1:
                pal = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
                for i, a in enumerate(al): abits |= min(range(8), key=lambda j: abs(pal[j] - a)) << (3 * i)
            vis = [q[:3] for q in p if q[3] > 0] or [clear]
            lum = lambda c: c[0] * 3 + c[1] * 6 + c[2]
            hi = max(vis, key=lum); lo = min(vis, key=lum)
            c0, c1 = to565(hi), to565(lo)
            if c0 < c1: c0, c1 = c1, c0
            e0, e1 = c565(c0), c565(c1)
            cp = [e0, e1, tuple((2 * a + b) // 3 for a, b in zip(e0, e1)), tuple((a + 2 * b) // 3 for a, b in zip(e0, e1))]
            idx = 0
            if c0 != c1:
                for i, q in enumerate(p):
                    qq = q[:3] if q[3] > 0 else clear
                    idx |= min(range(4), key=lambda j: sum((cp[j][k] - qq[k]) ** 2 for k in range(3))) << (2 * i)
            out += bytes([a0, a1]) + abits.to_bytes(6, "little") + c0.to_bytes(2, "little") + c1.to_bytes(2, "little") + idx.to_bytes(4, "little")
    return bytes(out)

def inc(data, note):
    return "/* %s */\n" % note + ",\n".join(", ".join("0x%02X" % b for b in data[i:i + 16]) for i in range(0, len(data), 16)) + "\n"

def has_glyph(font, ch):
    """A character is drawn when its mask is not empty and differs from the font's missing-glyph box."""
    m = font.getmask(ch)
    if ch == " ": return True
    return m.getbbox() is not None and bytes(m) != bytes(font.getmask("￿"))

def main():
    args = sys.argv[1:]; prev = None; check = "--check" in args
    if check: args.remove("--check")
    if "--preview" in args:
        i = args.index("--preview"); prev = args[i + 1]; del args[i:i + 2]
    import menu_label_art as mla
    from PIL import Image, ImageFont
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    tbl = table(os.path.join(root, "installer", "port", "jsp", "jsp_menu_lang.h"))
    out = None if check else args.pop(0)
    font = mla.find_font(args[0] if args else None)
    if check:
        f = ImageFont.truetype(font, 40); bad = []
        for lang in LANGS:
            for id_, _ in LABELS:
                for ch in tbl[id_][lang]:
                    if not has_glyph(f, ch): bad.append((lang, id_, ch))
        print("missing glyphs:", bad or "none"); return 1 if bad else 0
    os.makedirs(out, exist_ok=True); sheet = []
    for lang in LANGS[1:]:
        for id_, name in LABELS:
            m = mla.letters(font, tbl[id_][lang], W, H, LEFT, BASE)
            bb = m.getbbox()
            if bb and bb[2] > (W - LEFT) * mla.SS:      # wider than the picture: squeeze sideways to fit
                k = ((W - LEFT) * mla.SS - LEFT * mla.SS) / float(bb[2] - LEFT * mla.SS); a = 1.0 / k
                m = m.transform(m.size, Image.AFFINE, (a, 0, LEFT * mla.SS * (1 - a), 0, 1, 0), Image.BICUBIC)
            for style, arr, clear in zip(("light", "dark"), mla.styles(m, W, H, BASE), ((0, 0, 0), (255, 255, 255))):
                note = "main menu row label \"%s\" (%s), %s look: 300x43 DXT5 (13200 bytes), drawn by mp_label_langs.py from jsp_menu_lang.h (own art)" % (tbl[id_][lang], lang, style)
                open(os.path.join(out, "mp_label_%s_%s_%s.inc" % (lang, name, style)), "w", encoding="utf-8").write(inc(dxt5(arr, clear), note))
                if style == "light": sheet.append(Image.fromarray(arr, "RGBA"))
    if prev and sheet:
        P = Image.new("RGBA", (W * 4, H * 4), (92, 92, 130, 255))
        for i, s in enumerate(sheet): P.paste(s, ((i % 4) * W, (i // 4) * H), s)
        P.save(prev)
    print("wrote", out); return 0

if __name__ == "__main__":
    sys.exit(main())
