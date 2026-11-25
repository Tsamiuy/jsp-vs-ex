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

def dxt5_decode(data, w, h):
    """DXT5 bytes (4x4 blocks in rows) -> (h, w, 4) uint8 straight alpha; for the preview of the .inc files already in the tree."""
    import numpy as np
    out = np.zeros((h, w, 4), np.uint8); n = 0
    for by in range((h + 3) // 4):
        for bx in range((w + 3) // 4):
            b = data[n:n + 16]; n += 16; a0, a1 = b[0], b[1]; ab = int.from_bytes(b[2:8], "little")
            pal = [a0, a1] + ([((6 - i) * a0 + (i + 1) * a1) // 7 for i in range(6)] if a0 > a1 else [((4 - i) * a0 + (i + 1) * a1) // 5 for i in range(4)] + [0, 255])
            c0 = int.from_bytes(b[8:10], "little"); c1 = int.from_bytes(b[10:12], "little"); idx = int.from_bytes(b[12:16], "little")
            e0, e1 = c565(c0), c565(c1); cp = [e0, e1, tuple((2 * x + y) // 3 for x, y in zip(e0, e1)), tuple((x + 2 * y) // 3 for x, y in zip(e0, e1))]
            for i in range(16):
                x = bx * 4 + i % 4; y = by * 4 + i // 4
                if x < w and y < h: out[y, x, :3] = cp[(idx >> (2 * i)) & 3]; out[y, x, 3] = pal[(ab >> (3 * i)) & 7]
    return out

def _fit(mla, Image, font, text, w, h, left, base, cap, avail_right):
    """Mask of the text at the given cap height on a canvas twice as wide, squeezed sideways to end before avail_right (texture px), cropped to w x h."""
    old = mla.CAP; mla.CAP = cap
    try: m = mla.letters(font, text, 2 * w, h, left, base)
    finally: mla.CAP = old
    bb = m.getbbox(); avail = avail_right * mla.SS
    if bb and bb[2] > avail:
        a = (bb[2] - left * mla.SS) / float(avail - left * mla.SS)
        m = m.transform(m.size, Image.AFFINE, (a, 0, left * mla.SS * (1 - a), 0, 1, 0), Image.BICUBIC)
    return m.crop((0, 0, w * mla.SS, h * mla.SS))

def title_picture(mla, Image, np, font, text):
    """The orange page title (512 x 64): yellow-to-orange italic letters with a dark brown rim. Colours and place measured on the title of the
    tree's English picture (yellow 255,230,40 at the top to orange 220,128,3 at the bottom, rim 90,36,8, letters x 2..397, y 12..48); the shapes are ours."""
    W2, H2, SS = 512, 64, mla.SS
    m = _fit(mla, Image, font, text, W2, H2, 6, 44, 28, 500)
    rim = m.filter(mla.ImageFilter.MaxFilter(2 * int(2.5 * SS) + 1)).filter(mla.ImageFilter.GaussianBlur(SS * 0.6))
    fill = np.asarray(m.filter(mla.ImageFilter.GaussianBlur(SS * 0.35)), dtype=float) / 255
    rim_a = np.asarray(rim, dtype=float) / 255
    ys = (np.arange(H2 * SS)[:, None] / SS - (44 - 28)) / 28.0; g = np.clip(ys, 0, 1)
    top = np.array([255.0, 230.0, 40.0]); bot = np.array([220.0, 128.0, 3.0]); rimc = np.array([90.0, 36.0, 8.0])
    body = top[None, None, :] * (1 - g[..., None]) + bot[None, None, :] * g[..., None]
    a_out = np.maximum(rim_a, fill)
    col = body * fill[..., None] + rimc[None, None, :] * (1 - fill[..., None])
    arr = np.zeros((H2 * SS, W2 * SS, 4)); arr[..., :3] = np.broadcast_to(col, (H2 * SS, W2 * SS, 3)); arr[..., 3] = a_out * 255
    return mla._down(arr, W2, H2)

def mark_picture(mla, Image, np, font, text):
    """The help bar's grey watermark (1024 x 128): big heavy italic letters, flat grey 156,157,156 at alpha 26, ending at x 1004 (measured on the
    tree's English picture: letters x 592..1004, y 33..86); the text is right aligned, the shapes are ours."""
    W2, H2, SS = 1024, 128, mla.SS
    old = mla.CAP; mla.CAP = 50
    try: m = mla.letters(font, text, 2 * W2, H2, 0, 82)
    finally: mla.CAP = old
    bb = m.getbbox()
    if bb:   # right align to x 1004 (the left edge may go to 8 at most; longer texts are squeezed)
        wd = (bb[2] - bb[0]) / SS
        if wd > 1004 - 8:
            k = (1004 - 8) / wd; a = 1.0 / k
            m = m.transform(m.size, Image.AFFINE, (a, 0, bb[0] * (1 - a), 0, 1, 0), Image.BICUBIC); bb = m.getbbox(); wd = (bb[2] - bb[0]) / SS
        dx = int(round((1004 - wd) * SS)) - bb[0]
        m = m.transform(m.size, Image.AFFINE, (1, 0, -dx, 0, 1, 0), Image.BILINEAR)
    m = m.crop((0, 0, W2 * SS, H2 * SS))
    a = np.asarray(m.filter(mla.ImageFilter.GaussianBlur(SS * 0.35)), dtype=float) / 255
    arr = np.zeros((H2 * SS, W2 * SS, 4)); arr[..., 0] = 156; arr[..., 1] = 157; arr[..., 2] = 156; arr[..., 3] = a * 26
    return mla._down(arr, W2, H2)

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
    import numpy as np
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
    new = {}   # (lang, name, style) -> straight-alpha RGBA array, for the preview
    for lang in LANGS:
        for id_, name in LABELS:
            m = mla.letters(font, tbl[id_][lang], 2 * W, H, LEFT, BASE)   # drawn on a canvas twice as wide, so the true width is measured
            bb = m.getbbox(); avail = (W - 8) * mla.SS    # 8 px right margin for the rim
            if bb and bb[2] > avail:      # wider than the picture: squeeze sideways to fit
                k = (avail - LEFT * mla.SS) / float(bb[2] - LEFT * mla.SS); a = 1.0 / k
                m = m.transform(m.size, Image.AFFINE, (a, 0, LEFT * mla.SS * (1 - a), 0, 1, 0), Image.BICUBIC)
            m = m.crop((0, 0, W * mla.SS, H * mla.SS))
            for style, arr, clear in zip(("light", "dark"), mla.styles(m, W, H, BASE), ((0, 0, 0), (255, 255, 255))):
                note = "main menu row label \"%s\" (%s), %s look: 300x43 DXT5 (13200 bytes), drawn by mp_label_langs.py from jsp_menu_lang.h (own art)" % (tbl[id_][lang], lang, style)
                open(os.path.join(out, "mp_label_%s_%s_%s.inc" % (lang, name, style)), "w", encoding="utf-8").write(inc(dxt5(arr, clear), note))
                new[(lang, name, style)] = arr; sheet.append(arr)
    pics = {}   # lang -> (title, mark) arrays
    for lang in LANGS:
        t_, m_ = title_picture(mla, Image, np, font, tbl["MENU_BATTLE_MODES"][lang]), mark_picture(mla, Image, np, font, tbl["MENU_BATTLE_MODES"][lang])
        pics[lang] = (t_, m_)
        note = "main menu page-1 %s \"%s\" (%s), %s, DXT5, drawn by mp_label_langs.py from jsp_menu_lang.h (own art)"
        open(os.path.join(out, "mp_title_%s.inc" % lang), "w", encoding="utf-8").write(inc(dxt5(t_, (0, 0, 0)), note % ("title", tbl["MENU_BATTLE_MODES"][lang], lang, "512x64 (32768 bytes)")))
        open(os.path.join(out, "mp_mark_%s.inc" % lang), "w", encoding="utf-8").write(inc(dxt5(m_, (0, 0, 0)), note % ("watermark", tbl["MENU_BATTLE_MODES"][lang], lang, "1024x128 (131072 bytes)")))
    if prev and pics:   # second sheet PREV_title.png: per row the old English then en es fr de it: the title (512 x 64) and the watermark at half size, its alpha x8 (it is 26 of 255)
        P2 = Image.new("RGBA", (1024, 64 * 6), (92, 92, 130, 255))
        for ri, c in enumerate(["old"] + LANGS):
            if c == "old":
                rd = lambda n, w, h: dxt5_decode(bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", re.sub(r"/\*.*?\*/", "", open(os.path.join(root, "installer", "port", "jsp", n)).read(), flags=re.S))), w, h)
                tt, mm = rd("mm_title_battle.inc", 512, 64), rd("mm_mark_battle.inc", 1024, 128)
            else: tt, mm = pics[c]
            mm = mm.copy(); mm[..., 3] = np.minimum(255, mm[..., 3].astype(int) * 8).astype(np.uint8)
            a = Image.fromarray(tt, "RGBA"); b = Image.fromarray(mm, "RGBA").resize((512, 64), Image.LANCZOS)
            P2.paste(a, (0, ri * 64), a); P2.paste(b, (512, ri * 64), b)
        P2.save(os.path.splitext(prev)[0] + "_title.png")
    if prev and sheet:   # contact sheet: per row the old Impact English (the .inc files of the tree) then en es fr de it; light looks above, dark looks below
        import numpy as np
        inc_dir = os.path.join(root, "installer", "port", "jsp"); cols = ["old"] + LANGS
        P = Image.new("RGBA", (W * len(cols), H * len(LABELS) * 2 + 10), (92, 92, 130, 255)); P.paste((255, 255, 255, 255), (0, H * len(LABELS), W * len(cols), H * len(LABELS) + 10))   # a white bar between the light (above) and dark (below) looks
        for si, style in enumerate(("light", "dark")):
            for ri, (_, name) in enumerate(LABELS):
                for ci, c in enumerate(cols):
                    if c == "old":
                        f = os.path.join(inc_dir, "mm_label_%s_%s.inc" % (name, style))
                        if not os.path.exists(f): continue
                        arr = dxt5_decode(bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", re.sub(r"/\*.*?\*/", "", open(f).read(), flags=re.S))), W, H)   # the comment holds "300x43", not a byte
                    else: arr = new[(c, name, style)]
                    im = Image.fromarray(arr, "RGBA"); P.paste(im, (ci * W, (si * len(LABELS) + ri) * H + si * 10), im)
        P.save(prev)
    print("wrote", out); return 0

if __name__ == "__main__":
    sys.exit(main())
