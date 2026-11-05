#!/usr/bin/env python3
"""Assemble the installer payload (what jsp.iss copies into {app}): a port of the game, built on the player's PC.

Two trees (--tree): "v15" (default) is this repository's earlier port (jstars/port) on the v0.15 toolkit as described below;
"v18" is installer/port, the glue for the ps3recomp v0.18.0 toolkit, with installer/port/patches applied to the exported toolkit
(installer/tools/export_toolkit.py). v18 stays opt-in until its open issues (sound, fight speed, fight graphics) are fixed. firstrun.py reads <payload>/tree.txt to know which recipe to run.

Layout adapted from the maintainer's installer for another PS3 port (same MIT licence). The payload holds only code that
is MIT-licensed or ours: the port sources (jstars/port), the ps3recomp v0.15 toolkit with this repository's patch already
applied (tools/toolkit/apply_toolkit.py, run here when the installer is built, so the player needs no Git), the embeddable
Windows Python, firstrun.py + jsp_spu.py, the launcher, icons, NOTICE and licence texts. Never any game file, lifted code or
image in git; the compiler is the bundled llvm-mingw (toolchain.json); no Visual Studio is needed.

    python installer/tools/assemble_payload.py [--out build/installer/payload] [--repo DIR] [--python DIR]
                                               [--launcher EXE] [--art DIR] [--toolchain-cache DIR]
                                               [--tree v15|v18] [--toolkit-repo GIT_DIR] [--no-fetch]

Toolchain: llvm-mingw, CMake and Ninja come from the cache that fetch_toolchain.py filled from installer/tools/toolchain.json
(hash-pinned; each tree must carry the manifest's hash in fetched.json), minus the entries' "prune" globs, with their licence
texts in licenses/<group>/. The patches in installer/patches/ are applied to the payload's copy of the port and the toolkit.

Needs Git on PATH and the submodule checked out (apply_toolkit.py / export_toolkit.py export the pinned commit; the v18 toolkit
tag is fetched into the submodule once, on the machine that builds the installer, when it is not there yet). Refuses to finish when the
payload holds a file of a kind the game ships (.elf .bin .sfo .psarc .edat .self .sprx).
"""
import argparse
import fnmatch
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
# Written into the payload root (so the portable zip has it too). The Python prints only the zip's path with --quiet.
SUPPORT_CMD = """@echo off
rem Makes jsp-support-<time>.zip (logs and settings, user name removed) on the Desktop, to attach to an issue. Nothing is sent anywhere.
chcp 65001 >nul
setlocal
set "PY=%~dp0toolchain\\python\\python.exe"
if not exist "%PY%" (
  echo The bundled Python is missing: %PY%
  if not defined JSP_NOPAUSE pause
  exit /b 2
)
set "ZIP="
for /f "usebackq delims=" %%L in (`""%PY%" -X utf8 "%~dp0support_bundle.py" --root "%~dp0." --quiet"`) do set "ZIP=%%L"
if not defined ZIP (
  echo The support bundle could not be made. Run this file from a Command Prompt to see the error.
  if not defined JSP_NOPAUSE pause
  exit /b 1
)
echo Support bundle: %ZIP%
echo Attach this one file to your issue. It holds logs and settings only, with your user name removed.
if not defined JSP_NOPAUSE explorer /select,"%ZIP%"
exit /b 0
"""

FORBIDDEN = {".elf", ".bin", ".sfo", ".psarc", ".edat", ".self", ".sprx"}
PORT_FILES = ("CMakeLists.txt", "config.toml", "main.cpp", "stubs.cpp", "jstars_fiber.cpp", "jstars_debug.cpp",
              "jstars_game.cpp", "jstars_sysutil.cpp", "exit_label_dark.inc", "exit_label_light.inc")
NOTICE_V18 = """J-Stars Victory VS-EX: Fan Revival
An unofficial fan project. Not affiliated with or endorsed by the game's developer or publisher.
No game files are included or downloaded: the port is built on your PC from your own copy of the game.
This program contacts no server: no update check, no crash report, no telemetry.

The game glue is the work of Tsamiuy and contributors (MIT licence, licenses/jstars-port-LICENSE.txt), built on ps3recomp by
Ned Heller (sp00nznet) and contributors (MIT licence, licenses/ps3recomp-LICENSE.txt, v0.18.0 with this project's patches).
More in licenses/jstars-port-NOTICE.md.
Python (PSF licence, toolchain/python/LICENSE.txt) is the Windows embeddable package. The build uses the bundled llvm-mingw
(LLVM: Apache-2.0 with LLVM exception; MinGW-w64 and winpthreads: see licenses/llvm-mingw/), CMake (BSD-3-Clause) and Ninja
(Apache-2.0); their licence texts are in licenses/. Nothing from Microsoft is needed or included.
"""
NOTICE = """J-Stars Victory VS-EX: Fan Revival
An unofficial fan project. Not affiliated with or endorsed by the game's developer or publisher.
No game files are included or downloaded: the port is built on your PC from your own copy of the game.
This program contacts no server: no update check, no crash report, no telemetry.

The game port is the work of Tsamiuy and contributors (the J-Stars port, MIT licence, licenses/jstars-port-LICENSE.txt), built on ps3recomp by
Ned Heller (sp00nznet) and contributors (MIT licence, licenses/ps3recomp-LICENSE.txt, v0.15.0 with the port's patch). The
patched video library uses SMAA by Jorge Jimenez et al. (MIT, header text kept in its files). More in licenses/jstars-port-NOTICE.md.
Python (PSF licence, toolchain/python/LICENSE.txt) is the Windows embeddable package. The build uses the bundled llvm-mingw
(LLVM: Apache-2.0 with LLVM exception; MinGW-w64 and winpthreads: see licenses/llvm-mingw/), CMake (BSD-3-Clause) and Ninja
(Apache-2.0); their licence texts are in licenses/. Nothing from Microsoft is needed or included.
"""


def tree_bytes(p):
    return sum(f.stat().st_size for f in pathlib.Path(p).rglob("*") if f.is_file())


AVCONFIG_H = """/* libavutil/avconfig.h: generated by FFmpeg's configure; written by assemble_payload.py for x86-64 Windows (the DLLs of
   RPCS3's FFmpeg build). Only the headers are used: ps3recomp loads FFmpeg at run time. */
#ifndef AVUTIL_AVCONFIG_H
#define AVUTIL_AVCONFIG_H
#define AV_HAVE_BIGENDIAN 0
#define AV_HAVE_FAST_UNALIGNED 1
#endif /* AVUTIL_AVCONFIG_H */
"""


def ffmpeg_source(explicit, cache):
    """-> (folder with libavcodec/avcodec.h or None, what was used / why not). An explicit folder wins; else the
    ffmpeg-headers tree of fetch_toolchain.py's cache, only when it carries the manifest's hash."""
    if explicit:
        p = pathlib.Path(explicit)
        return (p, f"from {p}") if (p / "libavcodec/avcodec.h").is_file() else (None, f"no FFmpeg headers at {p}")
    sys.path.insert(0, str(ROOT / "installer/tools"))
    import fetch_toolchain as ft
    ent = next((ft.expand(c) for c in ft.load()["components"] if c.get("name") == "ffmpeg-headers"), None)
    if not ent:
        return None, "no ffmpeg-headers entry in toolchain.json"
    tree = pathlib.Path(cache) / "trees" / "ffmpeg-headers"
    if ft.read_fetched(cache).get("ffmpeg-headers", {}).get("sha256") != ent["sha256"] or not (tree / "libavcodec/avcodec.h").is_file():
        return None, "ffmpeg-headers is not in the toolchain cache with the manifest's hash (run fetch_toolchain.py)"
    return tree, f"FFmpeg {ent['version']} from the toolchain cache (sha256 {ent['sha256'][:16]})"


def collect_toolchain(cache):
    """-> (files {payload path: source}, problems) from the fetch_toolchain.py cache: llvm-mingw, CMake, Ninja (minus the
    manifest's prune globs) under toolchain/ and the licence texts under licenses/<group>/."""
    sys.path.insert(0, str(ROOT / "installer/tools"))
    import fetch_toolchain as ft
    m = ft.load()
    problems = ["manifest: " + p for p in ft.validate(m, ("https",), strict=True)]
    if problems:
        return {}, problems
    fetched = ft.read_fetched(cache)
    files, groups, lic = {}, set(), set()
    for raw in m["components"]:
        e = ft.expand(raw)
        if e.get("stage_only"):   # fetched for another step (the FFmpeg headers: ffmpeg_source), not toolchain/
            continue
        name, tree = e["name"], os.path.join(cache, "trees", e["name"])
        groups.add(e["group"])
        if fetched.get(name, {}).get("sha256") != e["sha256"] or not os.path.isdir(tree):
            problems.append(f"{name}: not in the cache with the manifest's hash (run fetch_toolchain.py)")
            continue
        if e["archive"] == "file":
            src = os.path.join(tree, e["license_file"])
            if os.path.isfile(src):
                files[f"licenses/{e['group']}/{e['license_file']}"] = src
                lic.add(e["group"])
            else:
                problems.append(f"{name}: {e['license_file']} missing in the cache")
            continue
        prune, globs = e.get("prune", []), e.get("license_globs", [])
        for d, dirs, names in os.walk(tree, followlinks=False):
            dirs[:] = sorted(x for x in dirs if not os.path.islink(os.path.join(d, x)))
            for n in sorted(names):
                p = os.path.join(d, n)
                if os.path.islink(p) or not os.path.isfile(p):
                    continue
                rel = os.path.relpath(p, tree).replace(os.sep, "/")
                if any(fnmatch.fnmatchcase(rel, g) for g in globs):
                    files[f"licenses/{e['group']}/{name}-tree-{rel.replace('/', '_')}"] = p
                    lic.add(e["group"])
                if not any(fnmatch.fnmatchcase(rel, g) for g in prune):
                    files[f"toolchain/{e['dest']}/{rel}"] = p
    problems += [f"group {g} has no licence text in the cache" for g in sorted(groups - lic)]
    return files, problems


def apply_patches(out):
    """installer/patches/*.patch, in name order, to the payload's copy (git apply works outside a repository; paths a/ b/ relative
    to the payload). The checked-out repository is never edited."""
    # The payload lies inside this repository's checkout (build/installer/payload). There git apply works relative to the
    # checkout's top folder and silently skips every path outside the current folder, so it "succeeds" without changing a
    # file. Stopping the repository search at the payload's parent makes it apply to the payload as a plain folder; the reverse
    # check afterwards proves each patch is really in.
    # core.autocrlf=false: the patches carry the line endings of the files they change (some toolkit files are CRLF), so they
    # must apply byte for byte. With Git for Windows' default (true) git apply normalised the files first, and a
    # checkout with false (Linux, CI) failed on spu_drain.c.
    env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(pathlib.Path(out).resolve().parent))
    for p in sorted((ROOT / "installer/patches").glob("*.patch")):
        r = subprocess.run(["git", "-c", "core.autocrlf=false", "apply", "--recount", "-p1", str(p)],
                           cwd=out, capture_output=True, text=True, env=env)
        if r.returncode:
            print(f"patch {p.name} failed:\n" + r.stdout + r.stderr, file=sys.stderr)
            return False
        r = subprocess.run(["git", "-c", "core.autocrlf=false", "apply", "-R", "--check", "--recount", "-p1", str(p)],
                           cwd=out, capture_output=True, text=True, env=env)
        if r.returncode:
            print(f"patch {p.name} reported success but is not in the payload:\n" + r.stdout + r.stderr, file=sys.stderr)
            return False
        print(f"patch {p.name} applied")
    return True


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default=str(ROOT / "build/installer/payload"))
    ap.add_argument("--repo", default=str(ROOT), help="checkout of this repository (read only)")
    ap.add_argument("--python", default=str(ROOT / "build/installer/toolchain-cache/python"),
                    help="unpacked Windows Python embeddable package (skipped with a warning when absent)")
    ap.add_argument("--launcher", default=str(ROOT / "build/installer/jsp-launcher.exe"))
    ap.add_argument("--toolchain-cache", default=str(ROOT / "build/installer/toolchain-cache"), help="fetch_toolchain.py's cache")
    ap.add_argument("--art", default=str(ROOT / "build/installer/art"))
    ap.add_argument("--ffmpeg-headers", default="",
                    help="an FFmpeg source or headers folder (libavcodec/avcodec.h ...) whose majors match the RPCS3 DLLs (default: "
                         "the ffmpeg-headers entry of fetch_toolchain.py's cache): the payload gets libavcodec/ and libavutil/ "
                         "headers so the game is built with the opening-movie decoder (skipped with a warning when absent)")
    ap.add_argument("--tree", choices=("v15", "v18"), default="v15", help="which port to build on the player's PC (v18: opt-in)")
    ap.add_argument("--toolkit-repo", default=str(ROOT / "third_party/ps3recomp"), help="git checkout the v18 toolkit is exported from")
    ap.add_argument("--no-fetch", action="store_true", help="do not fetch the v18 toolkit tag when the checkout lacks it")
    a = ap.parse_args(argv)
    pub, out = pathlib.Path(a.repo), pathlib.Path(a.out)
    if out.exists():
        shutil.rmtree(out)
    (out / "licenses").mkdir(parents=True)
    (out / "VERSION.txt").write_text((ROOT / "installer/VERSION").read_text().strip() + "\n")
    (out / "NOTICE.txt").write_text(NOTICE_V18 if a.tree == "v18" else NOTICE)
    (out / "tree.txt").write_text(a.tree + "\n")
    (out / "tools").mkdir()
    spu_tool = "lift_jobs.py" if a.tree == "v18" else "jsp_spu.py"
    for n in ("firstrun.py", spu_tool, "rpcs3_locate.py", "rpcs3_wizard.py"):   # the last two: the wizard's RPCS3 page unpacks them to its temp folder
        shutil.copy2(ROOT / "installer/tools" / n, out / "tools" / n)
    shutil.move(out / "tools" / "firstrun.py", out / "firstrun.py")
    # the Start-menu "Make a support bundle" entry: support_bundle.py sits beside firstrun.py (it imports scrub() from it)
    shutil.copy2(ROOT / "installer/tools/support_bundle.py", out / "support_bundle.py")
    (out / "support-bundle.cmd").write_bytes(SUPPORT_CMD.replace("\r\n", "\n").replace("\n", "\r\n").encode("ascii"))
    # the "EX" for the title logo (toolkit patch 0006), drawn here; firstrun.py places it next to JStarsRecomp.exe
    r = subprocess.run([sys.executable, "-I", str(ROOT / "installer/tools/ex_logo_art.py"), str(out / "ex_logo_A.rgba")],
                       capture_output=True, text=True)
    if r.returncode:
        print("ex_logo_art failed (it needs Pillow and numpy):\n" + r.stdout + r.stderr, file=sys.stderr)
        return 1
    print(r.stdout.strip())
    # the settings window (C# 5 source; setup and firstrun.py compile it on the player's PC with Windows' own csc)
    shutil.copy2(pub / "launcher/JStarsLauncher.cs", out / "tools" / "jsp-settings.cs")
    shutil.copy2(a.launcher, out / "jsp-launcher.exe")
    for n in ("setup", "launcher", "uninstall"):
        shutil.copy2(pathlib.Path(a.art) / f"jsp-{n}.ico", out / f"jsp-{n}.ico")
    shutil.copy2(pub / "LICENSE", out / "licenses/jstars-port-LICENSE.txt")
    shutil.copy2(pub / "NOTICE.md", out / "licenses/jstars-port-NOTICE.md")
    if a.tree == "v18":
        # the glue (installer/port/jsp) and the pinned toolkit with installer/port/patches applied
        shutil.copytree(ROOT / "installer/port/jsp", out / "port")
        sys.path.insert(0, str(ROOT / "installer/tools"))
        import export_toolkit
        try:
            names = export_toolkit.export(a.toolkit_repo, out / "ps3recomp", fetch=not a.no_fetch)
        except RuntimeError as e:
            print("export_toolkit failed:\n" + str(e), file=sys.stderr)
            return 1
        print(f"ps3recomp {export_toolkit.TAG} exported, {len(names)} patches applied")
        shutil.copy2(out / "ps3recomp/LICENSE", out / "licenses/ps3recomp-LICENSE.txt")
    else:
        # the port sources
        (out / "port").mkdir()
        for n in PORT_FILES:
            # As the repository stores them (LF): Git for Windows' default core.autocrlf=true checks them out with CRLF, and
            # patch 0002 (LF, applied byte for byte with core.autocrlf=false) then fails on port/CMakeLists.txt.
            data = (pub / "jstars/port" / n).read_bytes()
            if b"\0" not in data:
                data = data.replace(b"\r\n", b"\n")
            (out / "port" / n).write_bytes(data)
            shutil.copystat(pub / "jstars/port" / n, out / "port" / n)
        # the toolkit: pinned v0.15.0 + this repository's patch + SMAA files, applied here
        r = subprocess.run([sys.executable, "-I", str(pub / "tools/toolkit/apply_toolkit.py"), "--out", str(out / "ps3recomp")],
                           capture_output=True, text=True)
        if r.returncode:
            print("apply_toolkit failed:\n" + r.stdout + r.stderr, file=sys.stderr)
            return 1
        print(r.stdout.strip().splitlines()[0])
        shutil.copy2(out / "ps3recomp/LICENSE", out / "licenses/ps3recomp-LICENSE.txt")
        if not apply_patches(out):
            return 1
    (out / "cmake").mkdir()
    shutil.copy2(ROOT / "installer/tools/llvm-mingw.cmake", out / "cmake/llvm-mingw.cmake")
    tfiles, tprob = collect_toolchain(os.path.abspath(a.toolchain_cache))
    if tprob:
        print("toolchain: " + "\n  ".join(tprob), file=sys.stderr)
        return 1
    for dest, src in sorted(tfiles.items()):
        (out / dest).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, out / dest)
    fh, fwhy = ffmpeg_source(a.ffmpeg_headers, os.path.abspath(a.toolchain_cache))
    if fh:   # headers only (LGPL 2.1+); the DLLs come from the player's RPCS3 at install time
        for lib in ("libavcodec", "libavutil"):
            for h in (fh / lib).rglob("*.h"):
                dst = out / "ffmpeg/include" / h.relative_to(fh)
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(h, dst)
        cfg = out / "ffmpeg/include/libavutil/avconfig.h"
        if not cfg.is_file():   # made by FFmpeg's configure, so a source tree has none: the values for x86-64 Windows
            cfg.write_text(AVCONFIG_H, newline="\n")
        lic = next((fh / n for n in ("COPYING.LGPLv2.1", "LICENSE.md") if (fh / n).is_file()), None)
        if lic:
            shutil.copy2(lic, out / "licenses/ffmpeg-COPYING.LGPLv2.1.txt")
        print(f"ffmpeg headers: {fwhy}")
        majors = []   # the majors the DLLs of the player's RPCS3 must have (firstrun.py check M1 compares them at install time)
        for rel, key, dll in (("libavcodec/version_major.h", "LIBAVCODEC_VERSION_MAJOR", "avcodec"),
                              ("libavutil/version.h", "LIBAVUTIL_VERSION_MAJOR", "avutil")):
            try:
                words = [l.split() for l in (out / "ffmpeg/include" / rel).read_text(errors="replace").splitlines()]
                majors.append(next(f"{dll}-{w[2]}.dll" for w in words if len(w) >= 3 and w[:2] == ["#define", key] and w[2].isdigit()))
            except (OSError, StopIteration):
                majors.append(f"{dll}-?.dll")
        print("ffmpeg DLLs the player's RPCS3 must have: " + ", ".join(majors))
    else:
        print(f"WARNING: {fwhy}: the game is built without the movie decoder (the opening movie stays black)", file=sys.stderr)
    py = pathlib.Path(a.python)
    if (py / "python.exe").is_file():
        shutil.copytree(py, out / "toolchain/python", ignore=shutil.ignore_patterns("__pycache__"))
    else:
        print(f"WARNING: no Windows Python at {py}: the payload has no toolchain/python", file=sys.stderr)
    bad = [str(p.relative_to(out)) for p in out.rglob("*") if p.suffix.lower() in FORBIDDEN]
    if bad:
        print("payload holds game-like files: " + " ".join(bad[:20]), file=sys.stderr)
        return 1
    files = [p for p in out.rglob("*") if p.is_file()]
    print(f"payload {out}: {len(files)} files, {tree_bytes(out) / 1e6:.1f} MB")
    for d in sorted(p for p in out.iterdir()):
        print(f"  {d.name:16} {tree_bytes(d) / 1e6 if d.is_dir() else d.stat().st_size / 1e6:8.2f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
