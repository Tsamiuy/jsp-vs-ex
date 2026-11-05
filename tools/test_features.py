#!/usr/bin/env python3
"""Tests for tools/features.py on small fixture repositories (no network, no game files).

    python3 tools/test_features.py
"""
import contextlib
import io
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import features  # noqa: E402

HEAD = "\t".join(features.COLUMNS)
ROWS = [
    "boot\tgame\tBoot\tBoot\tThe title screen\tworks\t[note](docs/notes/A.md)\t",
    "launcher\tport\tInput\tLauncher\tSettings window\tavailable\ton\t",
    "training\tport\tModes\tTraining\tA dummy\tin progress\topt-in\tTRAINING=1",
    "palettes\tport\tLater\tPalettes\tColours\tplanned\toff\t",
]
ROADMAP = """# Roadmap

- ✅ Boots ([details](FEATURES.md#boot))
- ✅ Launcher ([details](FEATURES.md#launcher))
- 🔧 Training ([details](FEATURES.md#training))
- 📋 **Palettes** for characters, wrapped over
  two lines ([details](FEATURES.md#palettes))
"""


TMP = None   # one temporary folder for every fixture, removed at the end


def repo(rows=None, roadmap=ROADMAP):
    d = pathlib.Path(tempfile.mkdtemp(dir=TMP))
    (d / "docs/notes").mkdir(parents=True)
    (d / "docs/notes/A.md").write_text("# A\n", encoding="utf-8")
    (d / features.DATA).write_text("\n".join([HEAD] + (ROWS if rows is None else rows)) + "\n", encoding="utf-8")
    (d / features.ROADMAP).write_text(roadmap, encoding="utf-8")
    return d


def run(d, *args):
    err = io.StringIO()
    with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
        code = features.main(["--root", str(d), *args])
    return code, err.getvalue()


def expect_fail(name, d, text):
    code, err = run(d, "--check")
    assert code == 1 and text in err, f"{name}: expected '{text}', got code {code}:\n{err}"


def main():
    d = repo()
    assert run(d, "--check")[0] == 1, "a missing page must fail the check"
    assert run(d)[0] == 0
    code, err = run(d, "--check")
    assert code == 0, err
    page = (d / features.PAGE).read_text(encoding="utf-8")
    assert '<a id="training"></a>Training' in page
    assert "](notes/A.md)" in page, "links from the root are rewritten for docs/"
    assert "Port features: 1 available, 1 in progress, 1 planned." in page

    (d / features.DATA).write_text((d / features.DATA).read_text(encoding="utf-8").replace("A dummy", "Dummy"),
                                   encoding="utf-8")
    expect_fail("stale", d, "is stale")

    expect_fail("marker", repo(roadmap=ROADMAP.replace("🔧 Training", "✅ Training")), "✅ but #training")
    expect_fail("dead id", repo(roadmap=ROADMAP + "- 📋 Gone ([details](FEATURES.md#gone))\n"), "matches no row")
    expect_fail("uncited", repo(roadmap=ROADMAP.replace("  two lines ([details](FEATURES.md#palettes))\n", "")),
                "planned row #palettes is not cited")
    expect_fail("no marker", repo(roadmap=ROADMAP.replace("- ✅ Launcher", "- Launcher")), "needs exactly one of")
    expect_fail("bad link", repo(ROWS[:1] + ["x\tport\tA\tX\tSee [n](docs/notes/B.md)\tavailable\ton\t"] + ROWS[1:]),
                "not in the repository")
    expect_fail("duplicate id", repo(ROWS + [ROWS[1]]), "used twice")
    expect_fail("status", repo(ROWS + ["y\tport\tA\tY\tY\tdone\ton\t"]), "port status 'done'")
    expect_fail("evidence", repo(ROWS + ["z\tgame\tA\tZ\tZ\tworks\t\t"]), "'works' needs evidence")
    expect_fail("quote", repo(ROWS + ['q\tport\tA\tQ\tA "quoted" word\tplanned\toff\t']), "straight double quote")
    print("test_features: ok (12 cases)")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="features-test-") as TMP:
        main()
