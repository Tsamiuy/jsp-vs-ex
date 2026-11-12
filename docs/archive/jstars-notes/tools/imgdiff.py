"""Mean abs difference (0-255) of two BMPs over the whole frame, and over a 4x4 grid.
usage: imgdiff.py a.bmp b.bmp"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importlib.util
spec = importlib.util.spec_from_file_location('sm', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'screen_match.py'))
src = open(spec.origin).read().split('shot = load(')[0]
ns = {'__file__': spec.origin}; exec(src, ns)
a, b = ns['load'](sys.argv[1]), ns['load'](sys.argv[2])
w, h = a[0], a[1]
print(f"all {ns['diff'](a, b, (0, 0, w, h)):.1f}")
print(' '.join(f"{ns['diff'](a, b, (x*w//4, y*h//4, (x+1)*w//4, (y+1)*h//4)):.0f}" for y in range(4) for x in range(4)))
