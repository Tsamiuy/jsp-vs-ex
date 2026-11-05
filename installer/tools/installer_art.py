#!/usr/bin/env python3
"""Installer artwork for J-Stars Victory VS EX: Fan Revival, in the fan site's NFO look.

Image sizes per Inno DPI, own ICO writer, --check by bytes (layout adapted from the maintainer's installer for another PS3 port).
Everything comes from tools/site/ascii_logo.py's cells
(the compact banner star with the J) drawn in IBM Plex Mono Bold (SIL OFL 1.1; the font file is not in the repository: put
IBMPlexMono-Bold.ttf in build/installer/fonts/) and the site palette (red band at the top, black base,
yellow star, orange J, off-white text). No game logo or art. Needs Pillow (`pip install pillow`).

    python installer/tools/installer_art.py            # write build/installer/art/*
    python installer/tools/installer_art.py --check    # render again, exit 1 when a file is missing or differs
    python installer/tools/installer_art.py --preview  # also build/installer/art/preview.png (contact sheet, not shipped)

Outputs: wizard-{100,125,150,200}.bmp (Welcome/Finish panel), header-{...}.bmp (small top-right image), and
jsp-setup.ico, jsp-launcher.ico, jsp-uninstall.ico (16/24/32/48/64 as 32-bit DIB, 256 as PNG). The icons at 48 px and up
is the star+J as glyph art at 256 px only (a 22-column glyph star was mush at 48 and 64 px, judged by eye);
from 16 to 64 px a pixel drawing of the same star+J shape.
Images are generated at release time, never committed (tools/ci/repo_checks.py bans .png/.jpg/.bmp/.ico in git).
Deterministic: same script + font + Pillow = same bytes.
ASSUMPTION: Inno 6's documented image sizes per DPI (WizardImageFile / WizardSmallImageFile help topics), checked by
compiling the installer.
"""
import argparse
import io
import math
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "site"))
import ascii_logo  # noqa: E402

OUT = ROOT / "build" / "installer" / "art"
FONT = ROOT / "build" / "installer" / "fonts" / "IBMPlexMono-Bold.ttf"
# The site palette
RED, YELLOW, BLACK, PAPER, ORANGE = (0x90, 0, 0), (0xFF, 0xEA, 0x2E), (0, 0, 0), (0xF6, 0xF0, 0xED), (0xFF, 0x8C, 0x19)
DARK = (0x2B, 0x2B, 0x2B)   # Inno 6.7 "modern dark" page colour, sampled from the wizard
TAGCOL = {"s": YELLOW, "j": ORANGE, "v": PAPER, "1": ORANGE, "2": PAPER}
WIZARD = {100: (164, 314), 125: (192, 386), 150: (246, 459), 200: (328, 604)}
HEADER = {100: (55, 55), 125: (64, 68), 150: (83, 80), 200: (110, 106)}
ICON_SIZES = (16, 24, 32, 48, 64, 256)
ICONS = ("setup", "launcher", "uninstall")
SS = 4           # supersampling: draw at 4x, scale down once


def star_cells():
    """The site logo's star with the 3D J :
    ascii_logo's mirrored star rows with only the J put in, so the star stays whole where the logo lays VS-EX over it."""
    rows = [l.ljust(30) + ascii_logo._mirror(l.ljust(30)) for l in ascii_logo._L]
    ch = [list(r) for r in rows]
    tg = [["s" if c != " " else " " for c in r] for r in ch]
    for j, l in enumerate(ascii_logo._J):
        for i, c in enumerate(l):
            if c != "@":
                ch[6 + j][22 + i] = c
                tg[6 + j][22 + i] = "j"
    return [list(zip(r, t)) for r, t in zip(ch, tg)]


def font(px):
    from PIL import ImageFont
    return ImageFont.truetype(str(FONT), max(1, int(px)), layout_engine=ImageFont.Layout.BASIC)


def draw_cells(d, cells, x, y, cw, rh, fpx, colors=TAGCOL, stroke=0):
    """Draw rows of (char, tag) on a fixed grid: cell width cw, row height rh. stroke thickens the glyphs (small icons)."""
    f = font(fpx)
    for j, row in enumerate(cells):
        for i, (c, t) in enumerate(row):
            if c != " ":
                col = colors.get(t, PAPER)
                d.text((x + i * cw, y + j * rh), c, font=f, fill=col, stroke_width=int(stroke), stroke_fill=col)


def text_lines(d, lines, x, y, cw, rh, fpx, fill):
    f = font(fpx)
    for j, s in enumerate(lines):
        d.text((x, y + j * rh), s, font=f, fill=fill)


def new(w, h, bg=DARK):
    from PIL import Image
    return Image.new("RGB", (w * SS, h * SS), bg)


def down(img, w, h):
    from PIL import Image
    return img.resize((w, h), Image.LANCZOS)


# ---------------------------------------------------------------------------------------------------- wizard images
def star_block(d, x, y, width, stroke=0):
    """Draw star_cells() scaled to width; returns its height."""
    cells = star_cells()
    cw = width / len(cells[0])
    rh = cw / 0.6 * 1.18
    draw_cells(d, cells, x, y, cw, rh, cw / 0.6, stroke=stroke)
    return rh * len(cells)


def wizard(dpi):
    """Side image: the star with the 3D J, the title and the notice on the dark page colour (no subtitle band)."""
    w, h = WIZARD[dpi]
    s = SS * w / 164.0
    im = new(w, h)
    from PIL import ImageDraw
    d = ImageDraw.Draw(im)
    y = 24 * s
    y += star_block(d, 6 * s, y, (164 - 12) * s, stroke=0.25 * s) + 18 * s
    text_lines(d, ["J-STARS", "VICTORY", "VS EX"], 10 * s, y, 0, 23 * s, 21 * s, PAPER)
    text_lines(d, ["unofficial fan", "project. No game", "files included.", "Bring your own", "copy of the game."],
               10 * s, y + 84 * s, 0, 12.5 * s, 11 * s, ORANGE)
    return down(im, w, h)


def header(dpi):
    w, h = HEADER[dpi]
    im = icon_glyph(max(w, h))
    from PIL import Image
    bg = Image.new("RGB", im.size, DARK)
    bg.paste(im, (0, 0), im)
    return bg.crop((0, 0, w, h))


# --------------------------------------------------------------------------------------------------------- icons
def icon_glyph(n):
    """The star with the 3D J as glyph art at every size (the icon stays the ASCII star), on a
    transparent background. Below 128 px the glyph strokes are thickened so the star's lines still land on pixels."""
    from PIL import Image, ImageDraw
    S = max(n, 64) * SS * 4
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    m = 0.02 * S
    cells = star_cells()
    hgt = (S - 2 * m) / len(cells[0]) / 0.6 * 1.18 * len(cells)
    stroke = (S / n) * (0.12 if n >= 128 else 0.3 if n >= 48 else 0.22)
    star_block(d, m, (S - hgt) / 2, S - 2 * m, stroke=stroke)
    return im.resize((n, n), Image.LANCZOS)


def icon_image(n):
    return icon_glyph(n)


def ico_bytes(sizes=ICON_SIZES):
    from PIL import Image
    entries, blobs, off = [], [], 6 + 16 * len(sizes)
    for n in sizes:
        im = icon_image(n).convert("RGBA")
        if n >= 256:
            b = io.BytesIO()
            im.save(b, "PNG", optimize=False, compress_level=9)
            blob = b.getvalue()
        else:
            px = im.load()
            rows = b"".join(b"".join(bytes((px[x, y][2], px[x, y][1], px[x, y][0], px[x, y][3])) for x in range(n))
                            for y in range(n - 1, -1, -1))
            mask = b"\0" * (((n + 31) // 32) * 4 * n)
            blob = struct.pack("<IiiHHIIiiII", 40, n, 2 * n, 1, 32, 0, len(rows) + len(mask), 0, 0, 0, 0) + rows + mask
        entries.append(struct.pack("<BBBBHHII", n % 256, n % 256, 0, 0, 1, 32, len(blob), off))
        blobs.append(blob)
        off += len(blob)
    return struct.pack("<HHH", 0, 1, len(sizes)) + b"".join(entries) + b"".join(blobs)


# ------------------------------------------------------------------------------------------------------ driver
def bmp_bytes(im):
    b = io.BytesIO()
    im.save(b, "BMP")
    return b.getvalue()


def render_all():
    files = {}
    for dpi in WIZARD:
        files[f"wizard-{dpi}.bmp"] = bmp_bytes(wizard(dpi))
        files[f"header-{dpi}.bmp"] = bmp_bytes(header(dpi))
    ico = ico_bytes()
    for name in ICONS:
        files[f"jsp-{name}.ico"] = ico   # one picture for all three until distinct ones are wanted
    return files


def preview(path):
    from PIL import Image
    ims = [wizard(d) for d in WIZARD] + [header(d) for d in HEADER]
    for n in ICON_SIZES:
        ic = icon_image(n).resize((max(n, 64),) * 2, Image.NEAREST)
        bg = Image.new("RGB", ic.size, DARK)
        bg.paste(ic, (0, 0), ic)
        ims.append(bg)
    W = sum(i.width for i in ims) + 10 * (len(ims) + 1)
    sheet = Image.new("RGB", (W, 640), (60, 60, 60))
    x = 10
    for i in ims:
        sheet.paste(i, (x, 10))
        x += i.width + 10
    sheet.save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--preview", action="store_true")
    a = ap.parse_args()
    if not FONT.is_file():
        print(f"font {FONT} missing (put IBM Plex Mono Bold there; SIL OFL 1.1)", file=sys.stderr)
        return 2
    files, again = render_all(), render_all()
    if files != again:
        print("not deterministic: two renders differ", file=sys.stderr)
        return 1
    if a.check:
        bad = [n for n, b in files.items() if not (OUT / n).is_file() or (OUT / n).read_bytes() != b]
        print("installer_art: " + (f"{len(bad)} file(s) missing or changed: {' '.join(bad)}" if bad else
                                    f"{len(files)} files match"))
        return 1 if bad else 0
    OUT.mkdir(parents=True, exist_ok=True)
    for n, b in files.items():
        (OUT / n).write_bytes(b)
    if a.preview:
        preview(OUT / "preview.png")
    print(f"installer_art: wrote {len(files)} files to {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
