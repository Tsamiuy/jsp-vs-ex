#!/usr/bin/env python3
"""Installer artwork for J-Stars Victory VS-EX: Fan Revival, in the fan site's menu look.

Image sizes per Inno DPI, own ICO writer, --check by bytes (layout adapted from the installer of Project Pyrrhus,
undecuple's Killzone 2 port). The drawing helpers and the palette are the repository art's (tools/site/repo_art.py,
data/theme.json): a speed-line burst, red and white stripe frames, a slanted dark panel, the gold star with the J, the
title in our own polygon letters (tools/site/wordmark.py), "Fan Revival" on the yellow-to-orange bar. No font file, no
game logo or art. Needs Pillow (`pip install pillow`).

    python installer/tools/installer_art.py            # write build/installer/art/*
    python installer/tools/installer_art.py --check    # render again, exit 1 when a file is missing or differs
    python installer/tools/installer_art.py --preview  # also build/installer/art/preview.png (contact sheet, not shipped)
    python installer/tools/installer_art.py --out DIR  # write elsewhere

Outputs: wizard-{100,125,150,200}.bmp (Welcome/Finish panel), header-{...}.bmp (small top-right image), and
jsp-setup.ico, jsp-launcher.ico, jsp-uninstall.ico (16/24/32/48/64 as 32-bit DIB, 256 as PNG): the star with the J;
its outline is drawn thicker below 48 px so the shape still reads.
Images are generated at release time, never committed (tools/ci/repo_checks.py bans .png/.jpg/.bmp/.ico in git).
Deterministic: same script + Pillow = same bytes.
ASSUMPTION: Inno 6's documented image sizes per DPI (WizardImageFile / WizardSmallImageFile help topics), checked by
compiling the installer.
"""
import argparse
import importlib.util
import io
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
_sp = importlib.util.spec_from_file_location("repo_art", ROOT / "tools" / "site" / "repo_art.py")
art = importlib.util.module_from_spec(_sp)
_sp.loader.exec_module(art)

OUT = ROOT / "build" / "installer" / "art"
C, LIGHT = art.C, art.LIGHT
DARK = (0x2B, 0x2B, 0x2B)   # Inno 6.7 "modern dark" page colour, sampled from the wizard
WIZARD = {100: (164, 314), 125: (192, 386), 150: (246, 459), 200: (328, 604)}
HEADER = {100: (55, 55), 125: (64, 68), 150: (83, 80), 200: (110, 106)}
ICON_SIZES = (16, 24, 32, 48, 64, 256)
ICONS = ("setup", "launcher", "uninstall")
SS = 4           # supersampling: draw at 4x, scale down once


# ---------------------------------------------------------------------------------------------------- wizard images
def wizard(dpi):
    """Side image: burst, stripe frames, a slanted dark panel with the star, the title and Fan Revival."""
    from PIL import Image, ImageDraw
    w, h = WIZARD[dpi]
    s = SS * min(w / 164.0, h / 314.0)     # one scale for the 164x314 design; the other sizes differ a little in shape
    W, H = w * SS, h * SS
    ox, oy = (W - 164 * s) / 2, (H - 314 * s) / 2
    im = Image.new("RGB", (W, H))
    art.burst(im, int(W * 0.5), int(H * 0.36), LIGHT["burst_base"], LIGHT["burst_ray"], LIGHT["burst_line"], n=40)
    d = ImageDraw.Draw(im)
    fh = int(16 * s)
    art.stripes(im, 0, fh, SS, period=34 * s / SS, gap=5 * s / SS)
    art.stripes(im, H - fh, fh, SS, flip=True, period=34 * s / SS, gap=5 * s / SS)

    def X(v):
        return ox + v * s

    def Y(v):
        return oy + v * s
    art.slanted(d, X(12), Y(112), W - 2 * s, Y(258), 8 * s, C["ink"])
    art.slanted(d, X(6), Y(106), W - 8 * s, Y(252), 8 * s, C["panel"])
    art.mark(im, W / 2, Y(66), 40 * s, 3 * s)
    art.word(im, "J-STARS", 15 * s, X(18), Y(120), C["text"], 1.6 * s)
    art.word(im, "VICTORY", 15 * s, X(18), Y(141), C["text"], 1.6 * s)
    art.word(im, "VS-EX", 31 * s, X(16), Y(166), (C["title_top"], C["title_bottom"]), 2.4 * s)
    art.glow_bar(im, 0, Y(210), W - 4 * s, Y(236), fade=0.12)
    art.word(im, "FAN REVIVAL", 13 * s, X(22), Y(216), C["text"], 1.6 * s)
    art.word(im, "BRING YOUR", 10 * s, X(20), Y(266), C["ink"], 0)
    art.word(im, "OWN COPY", 10 * s, X(20), Y(280), C["ink"], 0)
    return im.resize((w, h), Image.LANCZOS)


def header(dpi):
    from PIL import Image
    w, h = HEADER[dpi]
    n = max(w, h)
    ic = icon_image(n)
    bg = Image.new("RGB", (w, h), DARK)
    bg.paste(ic, ((w - n) // 2, (h - n) // 2), ic)
    return bg


# --------------------------------------------------------------------------------------------------------- icons
def icon_image(n):
    """The star with the J on a transparent background; the outline is thicker at small sizes."""
    from PIL import Image
    S = max(n, 64) * SS * 2
    im = Image.new("RGB", (S, S), C["ink"])
    rel = 0.035 if n >= 48 else 0.06
    art.mark(im, S / 2, S * 0.535, S * (0.5 - rel - 0.01) / 1.0, S * rel)
    # alpha: the star with its outline
    m = art._outline_mask((S, S), art.star_polys(S / 2, S * 0.535, S * (0.5 - rel - 0.01)), S * rel)
    out = im.convert("RGBA")
    out.putalpha(m)
    return out.resize((n, n), Image.LANCZOS)


def ico_bytes(sizes=ICON_SIZES):
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
    ap.add_argument("--out", default=str(OUT))
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    files, again = render_all(), render_all()
    if files != again:
        print("not deterministic: two renders differ", file=sys.stderr)
        return 1
    if a.check:
        bad = [n for n, b in files.items() if not (out / n).is_file() or (out / n).read_bytes() != b]
        print("installer_art: " + (f"{len(bad)} file(s) missing or changed: {' '.join(bad)}" if bad else
                                    f"{len(files)} files match"))
        return 1 if bad else 0
    out.mkdir(parents=True, exist_ok=True)
    for n, b in files.items():
        (out / n).write_bytes(b)
    if a.preview:
        preview(out / "preview.png")
    print(f"installer_art: wrote {len(files)} files to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
