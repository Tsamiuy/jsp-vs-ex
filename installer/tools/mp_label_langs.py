#!/usr/bin/env python3
"""Two-page Main Menu pictures in en, es, fr, de and it: row labels, page title and help-bar watermark. Our own art, no game pixels.

One command regenerates every picture from the text table in installer/port/jsp/jsp_menu_lang.h (the one place the words live: a reviewed
translation is edited there, then this is run again; add --font-hash check by the pinned SHA-256 in menu_label_art.py):

  python3 installer/tools/mp_label_langs.py OUT_DIR [FONT.ttf]            writes OUT_DIR/mp_pic_<lang>.inc  (use installer/port/jsp as OUT_DIR)
  python3 installer/tools/mp_label_langs.py --check [FONT.ttf]            glyph check only: every character of every label has a glyph
  python3 installer/tools/mp_label_langs.py --preview PNG OUT_DIR [FONT]  also contact sheets PNG and PNG_title.png

(Run it without -I when Pillow and numpy come from a PYTHONPATH, as in a nix python3.withPackages environment. Needs Pillow and numpy; the font is
Fira Sans Heavy Italic 4.301, SIL OFL 1.1, the file pinned by SHA-256 in menu_label_art.py.)

What is in a picture set (one blob of 269440 bytes per language, DXT5, 4x4 blocks in rows):
  8 row labels 300 x 43 (13200 bytes each): battle, training, impossible, portset, each light then dark;   light = the normal look (white-to-grey
  letters, black rim), dark = the highlighted look (black letters, white glow); the letters come from menu_label_art.py at its cap height and slant,
  a label wider than the picture is squeezed sideways to fit;
  the page-1 title 512 x 64 (32768 bytes): yellow-to-orange italic letters, dark brown rim;
  the help-bar watermark 1024 x 128 (131072 bytes): flat grey at alpha 26, right aligned.
The blob is packed by installer/port/jsp/jsp_mp_pack.h's format (16-byte block RLE, then base 85 text in a string literal) into mp_pic_<lang>.inc
(about 0.5 MB for all five languages together; the unpacker and its host test are jsp_mp_pack.h and installer/tools/menu_lang_test.cpp).

Run on Linux with Pillow and numpy: the glyph check passes and the contact sheets were looked at. ASSUMPTIONS still to check against a game screenshot:
LEFT 10 / BASE 31 of the 300 x 43 picture, the clear colour (black for light, white for dark), the title and watermark place and scale, and the look
against the game's own labels (the old English ones were drawn from Impact by mm_label.ps1: wider letters, more tracking).
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

def rle(blob):
    """16-byte block RLE of jsp_mp_pack.h: op < 0x80 -> op+1 literal blocks follow; op >= 0x80 -> the next block repeats (op & 0x7F)+1 times."""
    nb = len(blob) // 16; blk = lambda i: blob[i * 16:i * 16 + 16]; o = bytearray(); i = 0
    while i < nb:
        r = 1
        while i + r < nb and r < 128 and blk(i) == blk(i + r): r += 1
        if r >= 2: o.append(0x80 | (r - 1)); o += blk(i); i += r; continue
        l = 1
        while i + l < nb and l < 128 and not (i + l + 1 < nb and blk(i + l) == blk(i + l + 1)): l += 1
        o.append(l - 1); o += blob[i * 16:(i + l) * 16]; i += l
    return bytes(o)

def unrle(d, out_bytes):
    o = bytearray(); i = 0
    while i < len(d):
        b = d[i]; i += 1; c = (b & 0x7F) + 1
        if b < 0x80: o += d[i:i + c * 16]; i += c * 16
        else: o += d[i:i + 16] * c; i += 16
    assert len(o) == out_bytes
    return bytes(o)

def alphabet():
    return "".join(chr(c) for c in range(33, 127) if chr(c) not in '"\\?@')[:85]

def b85(d):
    A = alphabet(); out = []
    for i in range(0, len(d), 4):
        g = d[i:i + 4]; n = len(g); v = int.from_bytes(g + b"\0" * (4 - n), "big"); t = []
        for _ in range(5): t.append(A[v % 85]); v //= 85
        out.append("".join(reversed(t))[:n + 1])
    return "".join(out)

def unb85(text):
    A = alphabet(); out = bytearray(); i = 0
    while i < len(text):
        g = text[i:i + 5]; v = 0
        for k in range(5): v = v * 85 + (A.index(g[k]) if k < len(g) else 84)
        out += v.to_bytes(5, "big")[1:][:len(g) - 1] if False else v.to_bytes(4, "big")[:len(g) - 1]; i += 5
    return bytes(out)

def pack(blob):
    r = rle(blob); return b85(len(r).to_bytes(4, "little") + r)

def unpack(text, out_bytes):
    d = unb85(text); n = int.from_bytes(d[:4], "little"); assert len(d) == 4 + n
    return unrle(d[4:], out_bytes)

def pic_inc(text, note):
    """The packed text as the body of a C string literal, in lines of 118 characters."""
    lines = ['"%s"' % text[i:i + 118] for i in range(0, len(text), 118)]
    return "/* %s */\n" % note + "\n".join(lines) + "\n"

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
    blobs = {}   # lang -> {("lab", name, style) | "title" | "mark": DXT5 bytes}
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
                blobs.setdefault(lang, {})[("lab", name, style)] = dxt5(arr, clear)
                new[(lang, name, style)] = arr; sheet.append(arr)
    pics = {}   # lang -> (title, mark) arrays
    for lang in LANGS:
        t_, m_ = title_picture(mla, Image, np, font, tbl["MENU_BATTLE_MODES"][lang]), mark_picture(mla, Image, np, font, tbl["MENU_BATTLE_MODES"][lang])
        pics[lang] = (t_, m_)
        note = "main menu page-1 %s \"%s\" (%s), %s, DXT5, drawn by mp_label_langs.py from jsp_menu_lang.h (own art)"
        blobs[lang]["title"] = dxt5(t_, (0, 0, 0)); blobs[lang]["mark"] = dxt5(m_, (0, 0, 0))
        blob = b"".join(blobs[lang][("lab", n, st)] for _, n in LABELS for st in ("light", "dark")) + blobs[lang]["title"] + blobs[lang]["mark"]
        assert len(blob) == 269440
        txt = pack(blob); assert unpack(txt, len(blob)) == blob   # the packed text must give the pictures back
        open(os.path.join(out, "mp_pic_%s.inc" % lang), "w", encoding="utf-8").write(pic_inc(txt, "main menu pictures (%s), packed by mp_label_langs.py from jsp_menu_lang.h: 8 labels, title, watermark" % lang))
    if prev and pics:   # second sheet PREV_title.png: per row the old English then en es fr de it: the title (512 x 64) and the watermark at half size, its alpha x8 (it is 26 of 255)
        P2 = Image.new("RGBA", (1024, 64 * 6), (92, 92, 130, 255))
        for ri, c in enumerate(["old"] + LANGS):
            if c == "old":
                rd = lambda n, w, h: dxt5_decode(bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", re.sub(r"/\*.*?\*/", "", open(os.path.join(root, "installer", "port", "jsp", n)).read(), flags=re.S))), w, h)
                if not os.path.exists(os.path.join(root, "installer", "port", "jsp", "mm_title_battle.inc")): continue   # the old Impact files are gone from the tree
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
