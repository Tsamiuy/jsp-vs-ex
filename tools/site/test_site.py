#!/usr/bin/env python3
"""Host test of the fan site and the drawn art: the site builds and passes tools/site/check_site.py, the README's
badge and button blocks match data/theme.json and data/nav.json, the committed PNGs in assets/ match a fresh render,
the installer art renders the same bytes twice, the palette pairs meet their contrast, and the checker itself catches
a dropped button, an unstyled class and an outside script (negative controls).

    python3 -I tools/site/test_site.py      # needs Pillow (prints SKIP without it)
"""
import importlib.util
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def load(name, path):
    sp = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(sp)
    sp.loader.exec_module(m)
    return m


def run(*args):
    r = subprocess.run([sys.executable, "-I", *map(str, args)], capture_output=True, text=True, cwd=ROOT)
    return r.returncode, (r.stdout + r.stderr).strip()


def main():
    fails = []

    def expect(ok, what):
        print(("ok   " if ok else "FAIL ") + what)
        if not ok:
            fails.append(what)

    theme = load("theme_t", HERE / "theme.py")
    t = theme.load()
    low = [(f, b, r) for f, b, r, mn in t.pair_ratios() if r < mn]
    expect(not low, f"palette contrast pairs ({len(t.data['pairs'])}) {low or ''}")
    expect(theme.contrast("#000000", "#FFFFFF") == 21.0, "contrast formula (black on white is 21)")

    wm = load("wordmark_t", HERE / "wordmark.py")
    polys, w = wm.layout("J-STARS VICTORY VS-EX")
    expect(all(wm._area(p) > 0 for p in polys), "every wordmark piece has the same winding")
    expect(w > 0 and 'aria-label="X"' in wm.svg("X", 10, label="X"), "wordmark SVG")

    rc, out = run(HERE / "badges.py", "--check")
    expect(rc == 0, "README badge and button blocks match the palette and data/nav.json: " + out)

    chk = load("check_t", HERE / "check_site.py")
    with tempfile.TemporaryDirectory() as d:
        site = pathlib.Path(d) / "_site"
        rc, out = run(HERE / "site.py", "--out", site, "--check")
        expect(rc == 0, "site builds and passes check_site\n" + out)
        if rc == 0:
            idx = (site / "index.html").read_text(encoding="utf-8")
            # negative controls on copies of the good site
            for what, edit, want in (
                ("a dropped button", lambda s: s.replace('href="https://github.com/Tsamiuy/jsp-vs-ex/blob/main/CHANGELOG.md"', 'href="#start"', 2), "button 'Changelog'"),
                ("an unstyled class", lambda s: s.replace('<main class="stage">', '<main class="stage nostyle">'), "class 'nostyle'"),
                ("an outside script", lambda s: s.replace("</body>", '<script src="https://example.org/x.js"></script></body>'), "loads https://example.org"),
            ):
                bad = pathlib.Path(d) / "bad"
                if bad.exists():
                    shutil.rmtree(bad)
                shutil.copytree(site, bad)
                (bad / "index.html").write_text(edit(idx), encoding="utf-8")
                probs, _n = chk.check(bad, ROOT)
                expect(any(want in p for p in probs), f"check_site catches {what}")

    try:
        import PIL  # noqa: F401
    except ImportError:
        print("SKIP the art checks: Pillow is not installed")
        return 1 if fails else 0
    rc, out = run(HERE / "repo_art.py", "--check")
    expect(rc == 0, "assets/*.png match a fresh render: " + out)
    with tempfile.TemporaryDirectory() as d:
        rc1, o1 = run(ROOT / "installer/tools/installer_art.py", "--out", pathlib.Path(d) / "a")
        rc2, o2 = run(ROOT / "installer/tools/installer_art.py", "--out", pathlib.Path(d) / "a", "--check")
        expect(rc1 == 0 and rc2 == 0, "installer art renders and matches itself: " + o2)
        bmps = sorted(p.name for p in (pathlib.Path(d) / "a").glob("*"))
        expect(len([b for b in bmps if re.match(r"(wizard|header)-\d+\.bmp", b)]) == 8 and
               len([b for b in bmps if b.endswith(".ico")]) == 3, f"installer art files: {bmps}")
    print(f"test_site: {'FAILED ' + str(len(fails)) if fails else 'all passed'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
