#!/usr/bin/env python3
"""ASCII logo for the NFO-style page: a five-point star (left half drawn, right half mirrored), a slanted J and
"VS-EX", plus two text lines. Our own art, deterministic, stdlib only.

    from ascii_logo import logo_lines, logo_cells
    logo_lines()   # list of plain strings (the art, a blank line, two text lines)
    logo_cells()   # list of rows; each row is a list of (char, tag): tag in "s" star, "j" J, "v" VS-EX, "1"/"2" text lines

CLI: python3 -I tools/site/ascii_logo.py   # prints the plain logo
"""
# left half of the star (column 30 is the axis); the right half is its mirror image
_L = [
    "                             /",
    "                            / ",
    "                           /  ",
    "                          /   ",
    "                         /    ",
    "   ____________________ /     ",
    "   `-.                        ",
    "      `-.                     ",
    "         `-.                  ",
    "            `-.               ",
    "               `.             ",
    "               /              ",
    "              /               ",
    "             /                ",
    "            /        _.-'`    ",
    "           /    _.-'`         ",
    "          / _.-'`             ",
    "         /-'`                 ",
    "                              ",
]
_J = [
    "    __________ ",
    "   /_____    /|",
    "        /   / /",
    "  /\\   /   / / ",
    " /  \\_/   / /  ",
    " \\_______/ /   ",
    "  \\______/    ",
]
_VS = [
    "__     _______       _______  __",
    "\\ \\   / / ___/      / ____/ |/ /",
    " \\ \\ / /\\__ \\______/ __/  |   / ",
    "  \\ V /___/ /_____/ /___ /   |  ",
    "   \\_//____/     /_____//_/|_|  ",
]
TEXT1 = "      J - S T A R S   V I C T O R Y   ::   F A N   R E V I V A L"
TEXT2 = "          unofficial fan project  ::  no game files included"
_SW = str.maketrans("/\\`'", "\\/'`")


def _mirror(s):
    return s[::-1].translate(_SW)


def logo_cells():
    rows = [l.ljust(30) + _mirror(l.ljust(30)) for l in _L]
    ch = [list(r.ljust(80)) for r in rows]
    tg = [["s" if c != " " else " " for c in r] for r in ch]

    def put(lines, x, y, tag):
        for j, l in enumerate(lines):
            for i, c in enumerate(l):
                if c != "@":
                    ch[y + j][x + i] = c
                    tg[y + j][x + i] = tag
    put(_J, 22, 6, "j")
    put(_VS, 31, 13, "v")
    out = []
    for r, t in zip(ch, tg):
        cells = [(c, k) for c, k in zip(r, t)]
        while cells and cells[-1][0] == " ":
            cells.pop()
        out.append(cells)
    out.append([])
    out.append([(c, "1") for c in TEXT1])
    out.append([(c, "2") for c in TEXT2])
    return out


def logo_lines():
    return ["".join(c for c, _ in row) for row in logo_cells()]


# Compact banner (README code block, console start line): a small mirrored star with the J on the left, VS-EX and the
# name on the right, the notice line under it. Same mirroring rule as the logo.
_BL = [
    "          /",
    "         / ",
    " _______/  ",
    " `-.       ",
    "    `.     ",
    "    /      ",
    "   /  _.-' ",
    "  /.-'     ",
]
_BJ = ["  ____", " /_  /", "/\\/ / ", "\\__/  "]
_BR = [
    "",
    "     __     _______       _______  __",
    "     \\ \\   / / ___/      / ____/ |/ /",
    "      \\ \\ / /\\__ \\______/ __/  |   / ",
    "       \\ V /___/ /_____/ /___ /   |  ",
    "        \\_//____/     /_____//_/|_|  ",
    "",
    "     J-STARS VICTORY :: FAN REVIVAL",
]
BANNER_NOTICE = "   :: unofficial fan project :: no game files included :: you need your own copy ::"


def banner_lines():
    g = [list((l.ljust(11) + l.ljust(11)[::-1].translate(_SW)).ljust(80)) for l in _BL]
    for j, l in enumerate(_BJ):
        for i, c in enumerate(l):
            if c != " ":
                g[3 + j][8 + i] = c
    for j, l in enumerate(_BR):
        for i, c in enumerate(l):
            g[j][24 + i] = c
    return ["".join(r).rstrip() for r in g] + [BANNER_NOTICE]


def banner_cells():
    """banner_lines() as rows of (char, tag): "s" star, "j" J (the overlay at rows 3-6, cols 8-13), "v" VS-EX (rows 1-5,
    from col 24), "1" the name line (row 7), "2" the notice line (last row). Blanks carry the tag " " so a renderer can
    keep exact columns."""
    g = [list((l.ljust(11) + l.ljust(11)[::-1].translate(_SW)).ljust(80)) for l in _BL]
    t = [["s" if c != " " else " " for c in r] for r in g]
    for j, l in enumerate(_BJ):
        for i, c in enumerate(l):
            if c != " ":
                g[3 + j][8 + i], t[3 + j][8 + i] = c, "j"
    for j, l in enumerate(_BR):
        for i, c in enumerate(l):
            g[j][24 + i] = c
            t[j][24 + i] = (("v" if 1 <= j <= 5 else "1") if c != " " else " ")
    rows = []
    for r, tg in zip(g, t):
        cells = list(zip(r, tg))
        while cells and cells[-1][0] == " ":
            cells.pop()
        rows.append(cells)
    rows.append([(c, "2") for c in BANNER_NOTICE])
    return rows


if __name__ == "__main__":
    import sys
    print("\n".join(banner_lines() if "--banner" in sys.argv else logo_lines()))


# Wide header, the same layout as the monochrome banner: the full star with the 3D J on the left (no VS-EX over its leg),
# the title on the right in three parts (spaced J-STARS VICTORY, the VS-EX art, spaced FAN REVIVAL), the notice below.
WIDE_TITLE = "J - S T A R S   V I C T O R Y"
WIDE_SUB = ":: F A N   R E V I V A L ::"


def star_j_cells():
    """The logo's star with the 3D J, rows of (char, tag): "s" star, "j" J."""
    ch = [list(l.ljust(30) + _mirror(l.ljust(30))) for l in _L]
    tg = [["s" if c != " " else " " for c in r] for r in ch]
    for j, l in enumerate(_J):
        for i, c in enumerate(l):
            if c != "@":
                ch[6 + j][22 + i], tg[6 + j][22 + i] = c, "j"
    return [list(zip(r, t)) for r, t in zip(ch, tg)]


def title_cells():
    """The title block, rows of (char, tag): "1" title lines, "v" VS-EX; centred on the VS-EX width."""
    vw = max(len(l) for l in _VS)
    block = [(WIDE_TITLE, "1"), ("", " ")] + [(l.ljust(vw), "v") for l in _VS] + [("", " "), (WIDE_SUB, "1")]
    width = max(len(t) for t, _ in block)
    return [[(c, tag if c != " " else " ") for c in t.center(width)] for t, tag in block]


def notice_text():
    return BANNER_NOTICE.strip()


def wide_cells(gap=4):
    """One grid: star_j_cells() left, title_cells() right (vertically centred), a blank row, the notice centred."""
    star, title = star_j_cells(), title_cells()
    sw = max(len(r) for r in star)
    top = (len(star) - len(title)) // 2
    rows = []
    for r, row in enumerate(star):
        cells = row + [(" ", " ")] * (sw - len(row) + gap)
        if 0 <= r - top < len(title):
            cells += title[r - top]
        rows.append(cells)
    width = max(len(r) for r in rows)
    rows.append([])
    rows.append([(c, "2") for c in notice_text().center(width)])
    return rows
