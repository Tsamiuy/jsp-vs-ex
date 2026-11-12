"""Which known screen is this? Compares regions of a screenshot BMP with reference BMPs.
usage: screen_match.py <shot.bmp> <name>     -> prints the mean abs difference (0-255) for that ref
       screen_match.py <shot.bmp>            -> prints every ref's difference, best first
Refs live in tools/refs/<name>.bmp (1280x720, from earlier screenshots). A difference under
~25 means "this screen"."""
import struct, sys, os

REFS = {   # name: (x0, y0, x1, y1) region that identifies the screen
    'title':           (480, 585, 800, 625),    # "Press any button" bar, bottom centre
    'dialog_ok':       (550, 455, 730, 505),    # OK button of the save-data dialogs
    'main_menu':       (90, 60, 410, 105),      # "Main Menu" header
    'menu_freebattle': (200, 268, 380, 305),    # Free Battle highlighted in the main menu
    'freebattle':      (100, 95, 390, 135),     # "Free Battle" header
    'charsel':         (380, 630, 700, 665),    # START Finish / SELECT Default bar
    'stage':           (150, 110, 430, 145),    # "Stage Select" header
    'press_x':         (900, 625, 1190, 675),   # "Now PRESS x" on the loading screen (white page)
    'press_x_tips':    (900, 625, 1190, 675),   # same, over the tips page (shown since 3b68f46)
    'fight_hud':       (940, 635, 1030, 668),   # "TIME" label of the fight HUD
}
HERE = os.path.dirname(os.path.abspath(__file__))

def load(path):
    d = open(path, 'rb').read()
    off, = struct.unpack_from('<I', d, 10)
    w, h = struct.unpack_from('<ii', d, 18)
    bpp, = struct.unpack_from('<H', d, 28)
    bp = bpp // 8; row = (w * bp + 3) & ~3
    flip = h > 0; h = abs(h)
    def px(x, y):
        yy = (h - 1 - y) if flip else y
        o = off + yy * row + x * bp
        return d[o], d[o + 1], d[o + 2]
    return w, h, px

def diff(a, b, reg):
    (wa, ha, pa), (wb, hb, pb) = a, b
    x0, y0, x1, y1 = reg
    if wa < x1 or ha < y1 or wb < x1 or hb < y1: return 255.0
    s = n = 0
    for y in range(y0, y1, 2):
        for x in range(x0, x1, 2):
            p, q = pa(x, y), pb(x, y)
            s += abs(p[0] - q[0]) + abs(p[1] - q[1]) + abs(p[2] - q[2]); n += 3
    return s / n

shot = load(sys.argv[1])
names = [sys.argv[2]] if len(sys.argv) > 2 else list(REFS)
res = sorted((diff(shot, load(os.path.join(HERE, 'refs', k + '.bmp')), REFS[k]), k) for k in names)
# "press_x" matches either loading-screen variant: the plain white page or the tips page.
if len(sys.argv) > 2 and sys.argv[2] == 'press_x':
    alt = diff(shot, load(os.path.join(HERE, 'refs', 'press_x_tips.bmp')), REFS['press_x_tips'])
    res = [(min(res[0][0], alt), 'press_x')]
for v, k in res: print(f'{v:.1f} {k}')
