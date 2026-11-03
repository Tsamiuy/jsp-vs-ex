#!/usr/bin/env python3
"""First-run build step of the installer: builds this repository's J-Stars port on the player's PC.

Layout (checks, then resumable steps with markers) adapted from the maintainer's installer for another PS3 port (same MIT
licence). The build recipe is this repository's: docs/notes/SETUP.md and jstars/port/build.bat (ps3recomp v0.15 with
toolkit/ps3recomp-local.patch applied when the installer is built, ppu_loader + ppu_lifter, SPU jobs). The compiler is the bundled
llvm-mingw with CMake and Ninja (toolchain/, hash-pinned in installer/tools/toolchain.json; nothing from Microsoft is needed). Nothing is downloaded and no server is contacted; no game file is ever part of the payload.

    python firstrun.py --root APP --disc DISC_FOLDER --eboot EBOOT.elf [--devflash DIR] --hdd0 DIR [--log FILE | --logdir DIR]
                       [--until checks|lift|spu|build|all] [--toolchain DIR] [--jobs N] [--force STEP]

Steps (each leaves build/markers/<step>.done holding a fingerprint of its inputs; a re-run skips a finished step whose inputs are
unchanged and says which input changed otherwise; a marker in the old plain format counts as stale):
  checks  preflight, one line per check: "OK code ...", "WARN code ..." or "FAIL code ..." (codes in PREFLIGHT below)
  lift    PPU: ppu_loader.py, ppu_lifter.py  -> build/analysis, build/recompiled   (the SETUP.md pipeline; the v18 tree adds find_functions.py)
  spu     the 18 JOBCRT jobs embedded in the EBOOT -> build/spu (jsp_spu.py, or lift_jobs.py for the v18 tree: scan, wrap, lift, spu_workloads.c)
  build   cmake + ninja with the bundled llvm-mingw (cmake/llvm-mingw.cmake; the same commands in build/build-port.cmd) -> build/port/JStarsRecomp.exe
  place   game/JStarsRecomp.exe, game/EBOOT.elf, game/run/, paths.txt for the launcher
Which tree is built is written by assemble_payload.py into <root>/tree.txt: "v15" (this repository's jstars/port on the v0.15 toolkit,
no file = v15) or "v18" (installer/port: the glue in port/, the ps3recomp v0.18.0 toolkit with installer/port/patches applied, exe jsp.exe).
Exit code 0 when everything asked for passed. The last line is "RESULT: ...". The log (--log) is appended to.

Portable zip (layout of --interactive --portable adapted from the same installer):

    python firstrun.py --interactive --portable [--root DIR] [--until STEP] [--log FILE]

run.cmd starts this. It asks for the disc folder and the decrypted EBOOT.elf, runs the same checks as the
installer pages and runs the steps (the compiler is bundled: no question about it).
The answers are kept in <root>/jsp-install.ini (paths inside the folder as @APP@/...), so a second start asks nothing that is still
valid. Saves (hdd0), logs, build and settings stay inside the folder. Moving or renaming the folder after the first run is fine:
paths inside it are stored relative (@APP@), the launcher expands them, and a CMake cache of the old place is dropped before a
rebuild. jsp-install.ini [install] portable=1 (written by this mode, or by the installer's Portable task) puts hdd0 and
play_settings.txt in the install folder; otherwise they are in the JSP Fan Revival folder of APPDATA.
"""
import argparse
import configparser
import hashlib
import json
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import time

# ======================================================================================================================
# GAME PROFILE: every J-Stars-specific value of this file is in this block, the disc and file checks (check_disc, check_eboot) and
# the "place" step's paths.txt lines. Key names follow the shared installer profile draft (product_name, short_name, titles,
# toolchains, steps); the rest of the file (preflight framework, path rules, step fingerprints, logs) names no game.
# ======================================================================================================================
PROFILE = {
    "product_name": "J-Stars Victory VS EX: Fan Revival",
    "short_name": "JSP Fan Revival",          # also the %APPDATA% folder and the install folder name (installer/jsp.iss AppDirName)
    "titles": {"BLUS31519": "US", "BLES02136": "EU"},   # one port for both regions
    "toolchains": ["vs-buildtools"],           # llvm-mingw is not wired up here
    "steps": ["lift", "spu_jobs", "build", "place"],
    # End of the last executable section per region (the region's ELF section table): .rodata and the SPU job images follow in the same
    # segment and must not be lifted as code. ASSUMPTION: only the US disc has been built so far; EU uses the same flow with its own value.
    "code_end": {"US": "0x734330", "EU": "0x734528"},
    "elf_min_bytes": 0x734528,                # the larger code_end: a file shorter than that cannot hold the code the lifter is told to lift
    "elf_max_bytes": 64 << 20,                # ASSUMPTION: the US file is 9.7 MB; far beyond that is not this game's EBOOT
    "exe": "JStarsRecomp.exe",
    "game_title_env": "J-Stars Victory VS+",  # PS3_TITLE in paths.txt
}
TITLE_IDS = PROFILE["titles"]
CODE_END = PROFILE["code_end"]
EXE = PROFILE["exe"]
TOC = "0x8d3888"          # r2 of the lifted code (both regions); the v18 lifter takes it as --toc
TREE_EXE = {"v15": EXE, "v18": "jsp.exe"}
# ======================================================================================================================
STEPS = ["checks", "lift", "spu", "build", "place"]
WIN = os.name == "nt"

_log = None
LOGS_KEPT = 5


def scrub(text):
    """Replace the user's profile folder with %USERPROFILE% (a log is pasted into bug reports; the user name is in that path).
    Case-insensitive, both slash kinds."""
    homes = {os.environ.get("USERPROFILE", ""), str(pathlib.Path.home())}
    for h in sorted((x for x in homes if len(x) > 3), key=len, reverse=True):
        # One pass over the text (a build log can be tens of MB with thousands of copies of the path).
        # The drive colon may also be "_": CMake names object files after the source path (CMakeFiles/x.dir/C_/Users/...).
        pat = "".join(r"[\\/]" if c in "\\/" else "[:_]" if c == ":" else re.escape(c) for c in h)
        text = re.sub(pat, "%USERPROFILE%", text, flags=re.IGNORECASE)
    return text


def scrub_file(path):
    """The same for a command's log file after it ran (build output echoes full paths)."""
    p = pathlib.Path(path)
    try:
        b = p.read_bytes()
        t = b.decode("utf-8", "replace")
        n = scrub(t)
        if n != t:
            p.write_bytes(n.encode("utf-8"))
    except OSError:
        pass


def say(msg):
    msg = scrub(msg)
    print(msg, flush=True)
    if _log:
        _log.write(msg + "\n")
        _log.flush()


def open_logdir(d):
    """logs/firstrun-YYYYmmdd-HHMMSS.log in d; the newest LOGS_KEPT stay (this one included)."""
    d = pathlib.Path(d)
    d.mkdir(parents=True, exist_ok=True)
    old = sorted(d.glob("firstrun-*.log"), key=lambda f: f.name)
    for f in old[:max(0, len(old) - (LOGS_KEPT - 1))]:
        try:
            f.unlink()
        except OSError:
            pass
    return d / f"firstrun-{time.strftime('%Y%m%d-%H%M%S')}.log"


APP = "@APP@"           # stands for the install folder in paths.txt and jsp-install.ini (the launcher expands it)
INI = "jsp-install.ini"
APPDIR = "JSP Fan Revival"


def norm(p):
    """A comparable spelling of a path: forward slashes, no trailing slash, case-folded on Windows."""
    p = str(p).replace("\\", "/").rstrip("/")
    return p.lower() if WIN else p


def pack_path(p, root):
    """A path inside the install folder is stored as @APP@/rel so the folder can be moved or renamed."""
    if not p:
        return ""
    p = str(p)
    r = str(root).replace("\\", "/").rstrip("/")
    q = p.replace("\\", "/")
    if norm(q) == norm(r):
        return APP
    if norm(q).startswith(norm(r) + "/"):
        return APP + q[len(r):]
    return p


def unpack_path(s, root):
    s = (s or "").strip()
    if s.startswith(APP):
        rest = s[len(APP):].lstrip("/\\")
        return str(pathlib.Path(root) / rest) if rest else str(root)
    return s


def read_ini(root):
    cp = configparser.ConfigParser(interpolation=None)
    cp.optionxform = str
    f = pathlib.Path(root) / INI
    if f.is_file():
        try:
            cp.read(f, encoding="utf-8-sig")
        except configparser.Error:
            pass
    return cp


def ini_get(cp, key, default=""):
    return cp.get("install", key, fallback=default) if cp.has_section("install") else default


def write_ini(root, cp):
    f = pathlib.Path(root) / INI
    with open(f, "w", encoding="utf-8", newline="") as fh:
        for sec in cp.sections():
            fh.write(f"[{sec}]\r\n" if WIN else f"[{sec}]\n")
            for k, v in cp.items(sec):
                fh.write(f"{k}={v}\r\n" if WIN else f"{k}={v}\n")


def is_portable(root, flag=False):
    return bool(flag) or ini_get(read_ini(root), "portable", "0") == "1"


def data_dir(root, portable):
    """Where saves (hdd0) and play_settings.txt live: the install folder when portable, else %APPDATA%\\JSP Fan Revival."""
    ad = os.environ.get("APPDATA")
    return pathlib.Path(root) if portable or not ad else pathlib.Path(ad) / APPDIR


# ---------------------------------------------------------------- inputs
def sfo_field(path, key):
    """One value of a PARAM.SFO (header: magic, version, key table, data table, count; 16-byte entries); '' if absent."""
    d = pathlib.Path(path).read_bytes()
    if len(d) < 20 or d[:4] != b"\0PSF":
        return ""
    kt, dt, n = struct.unpack_from("<III", d, 8)
    for i in range(min(n, 200)):
        ko, _fmt, ln, _mx, do = struct.unpack_from("<HHIII", d, 20 + 16 * i)
        k = d[kt + ko:d.index(b"\0", kt + ko)].decode("ascii", "replace")
        if k == key:
            return d[dt + do:dt + do + ln].split(b"\0")[0].decode("ascii", "replace")
    return ""


# Preflight codes (one line each: "OK code text", "WARN code text" or "FAIL code text"):
#   D1 PARAM.SFO  D2 title id  D3 PS3_GAME\\USRDIR   E1 decrypted ELF  E2 ELF size   T1-T3 bundled toolchain   F1 dev_flash
#   P1 install folder path  P2 disc folder path       S1 free space    R1 install folder moved (build tree reset)
BAD_PATH_CHARS = "[];"          # break CMake's configure (list separators); refused in the install and disc paths
BAD_SCRIPT_CHARS = '&^%!"'      # break the generated .cmd files (cmd.exe metacharacters); refused in the install path
ELF_MIN_BYTES, ELF_MAX_BYTES = PROFILE["elf_min_bytes"], PROFILE["elf_max_bytes"]
FREE_FAIL_GB, FREE_WARN_GB = 4, 10   # ASSUMPTION: lifted C++ about 200 MB plus objects; no measured total yet


def path_problems(path, what):
    """[(code-less message)] for characters in `path` that break CMake or the generated scripts."""
    out = []
    bad = sorted({c for c in str(path) if c in BAD_PATH_CHARS})
    if bad:
        out.append(f"{what} contains {' '.join(bad)} (CMake cannot configure with these in a path): move it to a folder without them")
    bad = sorted({c for c in str(path) if c in BAD_SCRIPT_CHARS})
    if bad and what == "install folder":
        out.append(f"{what} contains {' '.join(bad)} (the generated build scripts break on these): choose another install folder")
    return out


def check_paths(root, disc):
    out = []
    for code, what, p in (("P1", "install folder", root), ("P2", "disc folder", disc)):
        bad = path_problems(p, what)
        out.append(("FAIL", code, bad[0]) if bad else ("OK", code, f"{what} path is usable"))
    return out


def check_space(root):
    p = pathlib.Path(root).resolve()
    while not p.exists() and p.parent != p:
        p = p.parent
    free = shutil.disk_usage(p).free / 2 ** 30
    if free < FREE_FAIL_GB:
        return [("FAIL", "S1", f"{free:.1f} GB free where the build goes; at least {FREE_FAIL_GB} GB is needed")]
    if free < FREE_WARN_GB:
        return [("WARN", "S1", f"{free:.1f} GB free; {FREE_WARN_GB} GB or more is recommended")]
    return [("OK", "S1", f"{free:.0f} GB free")]


def disc_root(disc):
    """The folder that holds PS3_GAME; the PS3_GAME folder itself is accepted too (as the installer page does)."""
    d = pathlib.Path(str(disc).strip().strip('"'))
    if not (d / "PS3_GAME" / "PARAM.SFO").is_file() and (d / "PARAM.SFO").is_file() and d.name.upper() == "PS3_GAME":
        return d.parent
    return d


def ffmpeg_include(root):
    """The FFmpeg headers the payload ships (installer/tools/assemble_payload.py --ffmpeg-headers), or None. With them the toolkit
    is built with PS3RECOMP_FFMPEG_RUNTIME: it decodes the opening movie through FFmpeg DLLs loaded at run time."""
    inc = pathlib.Path(root) / "ffmpeg" / "include"
    return inc if (inc / "libavcodec" / "avcodec.h").is_file() else None


def ffmpeg_dll_names(inc):
    """The DLL names the runtime loads: avcodec-<major>.dll, avutil-<major>.dll (the majors of the shipped headers; only those
    match the structs it reads), and swresample-*.dll that avcodec needs beside it."""
    def major(rel, key):
        try:
            for line in (inc / rel).read_text(errors="replace").splitlines():
                parts = line.split()
                if len(parts) >= 3 and parts[0] == "#define" and parts[1] == key and parts[2].isdigit():
                    return parts[2]
        except OSError:
            pass
        return None
    c = major("libavcodec/version_major.h", "LIBAVCODEC_VERSION_MAJOR")
    u = major("libavutil/version.h", "LIBAVUTIL_VERSION_MAJOR")
    return [n for n in (c and f"avcodec-{c}.dll", u and f"avutil-{u}.dll") if n]


def check_movie(root, rpcs3):
    """M1 (WARN only): can the opening movie be decoded? The FFmpeg DLLs come from the player's RPCS3 folder (RPCS3 ships them)."""
    inc = ffmpeg_include(root)
    if not inc:
        return [("WARN", "M1", "movie decoder: this build has no FFmpeg headers, the opening movie stays black")]
    names = ffmpeg_dll_names(inc)
    if not rpcs3:
        return [("WARN", "M1", "movie decoder: no RPCS3 folder given, the opening movie stays black (it needs " + ", ".join(names) + " from RPCS3)")]
    miss = [n for n in names if not (pathlib.Path(rpcs3) / n).is_file()]
    if miss:
        return [("WARN", "M1", f"movie decoder: {', '.join(miss)} not in the RPCS3 folder {rpcs3} (another RPCS3 version?), the opening movie stays black")]
    return [("OK", "M1", "movie decoder: " + ", ".join(names) + " from the RPCS3 folder")]


def check_flash(df):
    """Only when a dev_flash folder is given: the game reads nothing from it (tested 2026-11-04 with an empty folder: menus, a
    fight, sound), so neither the installer nor the portable zip asks for one any more."""
    ok = bool(df) and (pathlib.Path(df) / "sys").is_dir()
    return ("OK" if ok else "FAIL", "F1", f"dev_flash folder {df or '(none)'}" + ("" if ok else " has no sys folder"))


# ---- GAME CHECKS (profile): the disc layout and the EBOOT ----
def check_disc(disc):
    disc = disc_root(disc)
    sfo = disc / "PS3_GAME" / "PARAM.SFO"
    if not sfo.is_file():
        return [("FAIL", "D1", f"{sfo} is missing: pick the folder that holds PS3_GAME")], ""
    out = [("OK", "D1", "PARAM.SFO found")]
    tid = sfo_field(sfo, "TITLE_ID")
    out.append(("OK" if tid in TITLE_IDS else "FAIL", "D2", f"TITLE_ID {tid or '(none)'}" + (f" ({TITLE_IDS[tid]})" if tid in TITLE_IDS else
                                                                          ": supported are " + " and ".join(TITLE_IDS))))
    out.append(("OK" if (disc / "PS3_GAME" / "USRDIR").is_dir() else "FAIL", "D3", "PS3_GAME\\USRDIR"))
    return out, tid


def disc_read_only(disc):
    """True when the disc folder is on a read-only drive, so a decrypted EBOOT.elf cannot be made beside the EBOOT.BIN there.
    (1) Windows: the drive is a CD/DVD type (a disc or a mounted image), GetDriveTypeW, nothing written. (2) Any drive, only called once
    the EBOOT.elf is missing: a file with a name that does not exist yet is created in PS3_GAME/USRDIR (exclusive) and deleted at once;
    if it cannot be created the drive is write-protected. A folder's read-only attribute is not used: Explorer sets it on writable folders."""
    if not disc:
        return False
    if os.name == "nt":
        import ctypes
        drive = os.path.splitdrive(os.path.abspath(disc))[0]
        if drive and ctypes.windll.kernel32.GetDriveTypeW(drive + "\\") == 5:    # DRIVE_CDROM
            return True
    d = pathlib.Path(disc) / "PS3_GAME" / "USRDIR"
    probe = d / "jsp-setup-write-test.tmp"
    if not d.is_dir() or probe.exists():
        return False
    try:
        os.close(os.open(probe, os.O_CREAT | os.O_EXCL | os.O_WRONLY))
    except OSError:
        return True
    probe.unlink()
    return False


READONLY_HINT = ("the disc folder is on a read-only drive (a disc or a mounted image), so the decrypted EBOOT.elf cannot be made there; "
                 "copy EBOOT.BIN from PS3_GAME\\USRDIR to a normal folder, make EBOOT.elf from that copy (from your own copy, see RPCS3 "
                 "and its documentation), then pass it as --eboot")


def check_eboot(path, disc=None):
    p = pathlib.Path(path)
    if not p.is_file():
        return [("FAIL", "E1", f"{p} is missing" + (f": {READONLY_HINT}" if disc_read_only(disc) else ""))]
    size = p.stat().st_size
    h = p.read_bytes()[:20] if size >= 20 else b""
    if h[:4] == b"SCE\0":
        return [("FAIL", "E1", "this is the encrypted EBOOT.BIN; setup needs the decrypted EBOOT.elf (see RPCS3 and its documentation)")]
    ok = len(h) == 20 and h[:4] == b"\x7fELF" and h[4] == 2 and h[5] == 2 and h[18:20] == b"\0\x15"
    out = [("OK" if ok else "FAIL", "E1", "decrypted PowerPC64 ELF" if ok else "not a PlayStation 3 (PowerPC64) ELF program")]
    if ok:
        lvl = "OK" if ELF_MIN_BYTES <= size <= ELF_MAX_BYTES else "FAIL"
        out.append((lvl, "E2", f"size {size / 1e6:.1f} MB" + ("" if lvl == "OK" else
                    f" is outside {ELF_MIN_BYTES / 1e6:.1f} to {ELF_MAX_BYTES / 1e6:.0f} MB: cut off or not this game's EBOOT")))
    return out


def exe(n):
    return n + (".exe" if WIN else "")


def toolchain_paths(tc):
    """The bundled tools (payload toolchain/): llvm-mingw's clang wrapper, CMake and Ninja."""
    tc = pathlib.Path(tc)
    return {"T1 llvm-mingw clang": tc / "llvm-mingw" / "bin" / exe("x86_64-w64-mingw32-clang++"),
            "T2 cmake": tc / "cmake" / "bin" / exe("cmake"),
            "T3 ninja": tc / "ninja" / exe("ninja")}


def check_toolchain(tc):
    return [("OK" if p.is_file() else "FAIL", k.split()[0],
             f"{' '.join(k.split()[1:])}: {p}" + ("" if p.is_file() else " is missing (the install is incomplete: run setup again)"))
            for k, p in toolchain_paths(tc).items()]


# ---------------------------------------------------------------- markers and process helper
def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def tree_hash(path, skip=("__pycache__",)):
    """sha256 over the relative names and contents of the files under path (one file or a folder); '' when absent."""
    p = pathlib.Path(path)
    if not p.exists():
        return ""
    h = hashlib.sha256()
    files = [p] if p.is_file() else sorted(f for f in p.rglob("*") if f.is_file() and not any(x in f.parts for x in skip))
    for f in files:
        h.update((f.name if p.is_file() else f.relative_to(p).as_posix()).encode() + b"\0")
        h.update(sha(f).encode())
    return h.hexdigest()


def digest(d):
    return hashlib.sha256(json.dumps(d, sort_keys=True).encode()).hexdigest()[:16]


MARKER_TAG = "fp1 "     # a marker is "fp1 " + JSON of the step's inputs; anything else is an old plain marker and counts as stale


def payload_tree(root):
    """'v18' when <root>/tree.txt says so, else 'v15' (payloads made before the file existed)."""
    f = pathlib.Path(root) / "tree.txt"
    try:
        v = f.read_text().split()[0]
    except (OSError, IndexError):
        return "v15"
    return v if v in TREE_EXE else "v15"


class Ctx:
    def __init__(self, a, region, tid):
        self.a, self.region, self.tid = a, region, tid
        self.root = pathlib.Path(a.root)
        self.build = self.root / "build"
        self.tools = self.root / "ps3recomp" / "tools"
        self.eboot = pathlib.Path(a.eboot)
        self.py = [sys.executable, "-X", "utf8"]
        self._cache = {}
        self.tree = payload_tree(self.root)
        self.exe = TREE_EXE[self.tree]

    def cached(self, key, fn):
        if key not in self._cache:
            self._cache[key] = fn()
        return self._cache[key]

    def toolchain_id(self):
        """Python version and the size and time of the bundled clang++: a changed compiler rebuilds."""
        cl = toolchain_paths(self.a.toolchain)["T1 llvm-mingw clang"]
        st = f"{cl.stat().st_size}:{int(cl.stat().st_mtime)}" if cl.is_file() else "none"
        return f"python {sys.version.split()[0]}; llvm-mingw clang++ {st}"

    def inputs(self, step):
        """The named inputs of a step; the dependent steps include the digest of the steps before them."""
        eb = self.cached("eboot", lambda: sha(self.eboot))
        tools = self.cached("tools", lambda: tree_hash(self.tools))
        if step == "lift":
            return {"eboot": eb, "region": self.region, "code_end": CODE_END[self.region], "toolkit_tools": tools, **({"tree": "v18"} if self.tree == "v18" else {})}
        if step == "spu":
            if self.tree == "v18":
                return {"eboot": eb, "toolkit_tools": tools, "lift_jobs": self.cached("lift_jobs", lambda: tree_hash(self.root / "tools" / "lift_jobs.py"))}
            return {"eboot": eb, "toolkit_tools": tools, "jsp_spu": self.cached("jsp_spu", lambda: tree_hash(self.root / "tools" / "jsp_spu.py"))}
        if step == "build":
            return {"lift": digest(self.inputs("lift")), "spu": digest(self.inputs("spu")),
                    "port_sources": self.cached("port", lambda: tree_hash(self.root / "port")),
                    "toolkit": self.cached("toolkit", lambda: tree_hash(self.root / "ps3recomp")),
                    "toolchain": self.cached("tc", self.toolchain_id), "install_folder": str(self.root).lower(),
                    "ffmpeg": self.cached("ffmpeg", lambda: tree_hash(self.root / "ffmpeg") if (self.root / "ffmpeg").is_dir() else "")}
        return {"build": digest(self.inputs("build")), "disc": str(self.a.disc), "hdd0": str(self.a.hdd0), "devflash": str(self.a.devflash),
                "rpcs3": str(getattr(self.a, "rpcs3", ""))}

    def marker(self, step):
        return self.build / "markers" / f"{step}.done"

    def stale_reason(self, step):
        """'' when the marker is current, else why the step has to run."""
        m = self.marker(step)
        if step in self.a.force:
            return "forced"
        if not m.is_file():
            return "no marker"
        t = m.read_text().strip()
        if not t.startswith(MARKER_TAG):
            return "old plain marker (counts as stale)"
        try:
            old = json.loads(t[len(MARKER_TAG):])
        except ValueError:
            return "unreadable marker"
        new = self.inputs(step)
        diff = sorted(k for k in set(old) | set(new) if old.get(k) != new.get(k))
        return ("input changed: " + ", ".join(diff)) if diff else ""

    def mark(self, step):
        self.marker(step).parent.mkdir(parents=True, exist_ok=True)
        self.marker(step).write_text(MARKER_TAG + json.dumps(self.inputs(step), sort_keys=True) + "\n")


def reset_if_moved(c):
    """The compile tree (CMake cache, ninja files) holds absolute paths: when the install folder is not the one the tree was made in
    (folder renamed or moved), drop the compile tree and the build/place markers; lift and SPU output holds no paths and stays."""
    f = c.build / "root.txt"
    now = str(c.root.resolve())
    old = f.read_text().strip() if f.is_file() else ""
    if old and old.lower() != now.lower():
        say(f"WARN R1 install folder moved from {old} to {now}: compile tree reset")
        shutil.rmtree(c.build / "port", ignore_errors=True)
        for s_ in ("build", "place"):
            c.marker(s_).unlink(missing_ok=True)
    c.build.mkdir(parents=True, exist_ok=True)
    f.write_text(now + "\n")


def run(name, cmd, logfile, cwd=None, env=None):
    """Run one command, write its output to logfile, return (ok, seconds)."""
    t0 = time.time()
    say(f"[{name}] start")
    pathlib.Path(logfile).parent.mkdir(parents=True, exist_ok=True)
    with open(logfile, "wb") as f:
        rc = subprocess.run(cmd if isinstance(cmd, str) else [str(c) for c in cmd], stdout=f, stderr=subprocess.STDOUT, cwd=cwd, env=env).returncode
    dt = time.time() - t0
    scrub_file(logfile)
    say(f"[{name}] {'ok' if rc == 0 else 'FAILED rc=' + str(rc)} in {dt:.0f} s (log {logfile})")
    return rc == 0, dt


def tree_size(p):
    p = pathlib.Path(p)
    return sum(f.stat().st_size for f in p.rglob("*") if f.is_file())


# ---------------------------------------------------------------- steps
def step_lift(c):
    """@ docs/notes/SETUP.md of this repository: ppu_loader.py, then ppu_lifter.py with --code-end (about 25 s on the
    maintainer's PC). -X utf8: the tools open files without an encoding (Windows would write the generated C in the code page)."""
    an, out = c.build / "analysis", c.build / "recompiled"
    an.mkdir(parents=True, exist_ok=True)
    out.mkdir(parents=True, exist_ok=True)
    e = str(c.eboot)
    ok, t1 = run("lift: ppu_loader", [*c.py, c.tools / "ppu_loader.py", e, "-o", str(an) + os.sep], an / "ppu_loader.log")
    if not ok:
        return False
    funcs, jobs_args, t3 = an / "EBOOT.functions.json", ["--jobs"], 0
    if c.tree == "v18":
        # the v0.18 lifter wants the function list completed by find_functions.py and the TOC value
        funcs = an / "functions.json"
        ok, t3 = run("lift: find_functions", [*c.py, c.tools / "find_functions.py", e, "--seed-json", an / "EBOOT.functions.json",
                                              "--output", funcs], an / "find_functions.log")
        if not ok:
            return False
        jobs_args = ["--toc", TOC, "-j"]   # --toc goes before the job count; the pair is spliced in front of it
    ok, t2 = run("lift: ppu_lifter", [*c.py, c.tools / "ppu_lifter.py", e, "--functions", funcs,
                                      "--hle-stubs", an / "EBOOT.imports.json", "--code-end", CODE_END[c.region],
                                      "--chunks", "64", *jobs_args, str(c.a.jobs), "--output", out], out / "lift.log")
    t1 += t3
    if not ok:
        return False
    n = len(list(out.glob("ppu_recomp_*.cpp")))
    say(f"[lift] {n} lifted files, {tree_size(out) / 1e6:.0f} MB of C++ in {out}, {t1 + t2:.0f} s")
    return n > 0 and (out / "ppu_recomp.h").is_file()


def step_spu(c):
    if c.tree == "v18":
        ok, _ = run("spu: jobs", [*c.py, c.root / "tools" / "lift_jobs.py", "--eboot", c.eboot, "--ps3recomp", c.root / "ps3recomp",
                                  "--out", c.build / "spu", "-j", str(min(4, c.a.jobs))], c.build / "spu.log")
        n = len(list((c.build / "spu").glob("*/spu_recomp.c")))
        say(f"[spu] {n} lifted job images")
        return ok and n > 0 and (c.build / "spu" / "spu_workloads.c").is_file()
    ok, _ = run("spu: jobs", [*c.py, c.root / "tools" / "jsp_spu.py", c.eboot, "--tools", c.tools, "--work", c.build / "spuwork",
                              "--out", c.build / "spu", "--jobs", str(min(4, c.a.jobs))], c.build / "spu.log")
    n = len(list((c.build / "spu").glob("*/spu_recomp.c")))
    say(f"[spu] {n} lifted job images")
    return ok and n > 0 and (c.build / "spu" / "spu_workloads.c").is_file()


def avail_gb():
    """Free physical memory in GB (Windows: GlobalMemoryStatusEx, Linux: MemAvailable); 0 when unknown."""
    if WIN:
        import ctypes

        class MS(ctypes.Structure):
            _fields_ = [("l", ctypes.c_ulong), ("load", ctypes.c_ulong)] + [(n, ctypes.c_ulonglong) for n in
                                                                            ("tot", "avail", "tpf", "apf", "tv", "av", "ae")]
        m = MS()
        m.l = ctypes.sizeof(MS)
        ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m))
        return m.avail / 2 ** 30
    try:
        for line in open("/proc/meminfo"):
            if line.startswith("MemAvailable:"):
                return int(line.split()[1]) / 2 ** 20
    except OSError:
        pass
    return 0


GB_PER_JOB = 1.0   # ASSUMPTION: about 1 GB per clang job on a lifted file (the largest clang process of one Linux build was 887 MB resident)


def build_jobs(requested):
    """Parallel compiles: one per GB of free RAM, at most the CPU count (never fewer than 1)."""
    if requested:
        return requested
    cpu = os.cpu_count() or 2
    g = avail_gb()
    return max(1, min(cpu, int(g / GB_PER_JOB) if g else cpu))


def tool_path(tc):
    tc = pathlib.Path(tc)
    return os.pathsep.join([str(tc / "llvm-mingw" / "bin"), str(tc / "cmake" / "bin"), str(tc / "ninja")])


def build_plan(c, jobs):
    """-> (env, configure command, build command) of the bundled-toolchain build. Paths use forward slashes (CMake)."""
    tc = pathlib.Path(c.a.toolchain)
    fw = lambda p: str(p).replace("\\", "/")
    py = c.a.python or sys.executable
    paths = toolchain_paths(tc)
    env = dict(os.environ)
    env["PATH"] = tool_path(tc) + os.pathsep + env.get("PATH", "")
    b = c.build / "port"
    cfg = [paths["T2 cmake"], "-S", c.root / "port", "-B", b, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
           "-DCMAKE_TOOLCHAIN_FILE=" + fw(c.root / "cmake" / "llvm-mingw.cmake"), "-DLLVM_MINGW_ROOT=" + fw(tc / "llvm-mingw"),
           "-DCMAKE_MAKE_PROGRAM=" + fw(paths["T3 ninja"]), "-DPS3RECOMP_DIR=" + fw(c.root / "ps3recomp"),
           "-DRECOMP_DIR=" + fw(c.build / "recompiled"), "-DSPU_DIR=" + fw(c.build / "spu"),
           "-DCMAKE_C_FLAGS_RELEASE=-O2 -DNDEBUG", "-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG", "-DPython3_EXECUTABLE=" + fw(py)]
    inc = ffmpeg_include(c.root)
    if inc:   # the opening movie: FFmpeg loaded at run time (DLLs from RPCS3, copied by the place step)
        cfg += ["-DPS3RECOMP_FFMPEG_RUNTIME=ON", "-DPS3RECOMP_FFMPEG_INCLUDE_DIR=" + fw(inc)]
    bld = [paths["T2 cmake"], "--build", b, "-j", str(jobs)]
    return env, [str(x) for x in cfg], [str(x) for x in bld]


def write_build_cmd(c, jobs):
    """build/build-port.cmd: the same two commands for a manual rebuild."""
    env, cfg, bld = build_plan(c, jobs)
    q = lambda cmd: " ".join('"' + x + '"' if " " in x else x for x in cmd)
    text = (f"@echo off\nrem Generated by firstrun.py: the build with the bundled llvm-mingw, CMake and Ninja.\n"
            f'set PATH={tool_path(c.a.toolchain)};%PATH%\n'
            f'if not exist "{c.build / "port" / "build.ninja"}" (\n    {q(cfg)} || exit /b 1\n)\n{q(bld)}\n')
    f = c.build / "build-port.cmd"
    # UTF-8 on purpose with chcp 65001: a user name with an accent (a profile folder named after the user) is in these paths and cmd reads a .cmd in the console code page
    f.write_bytes(text.replace("\n", "\r\n").encode("utf-8"))
    return f


def cmd_run_line(script):
    """cmd.exe /c with the script path quoted so that spaces and ( ) survive (with /s the outer pair of quotes is stripped)."""
    return f'cmd.exe /d /s /c ""{script}""'


def cache_home(build_dir):
    """CMAKE_HOME_DIRECTORY of an earlier configure in build_dir, '' when there is none."""
    f = pathlib.Path(build_dir) / "CMakeCache.txt"
    if f.is_file():
        for line in f.read_text(errors="replace").splitlines():
            if line.startswith("CMAKE_HOME_DIRECTORY:"):
                return line.split("=", 1)[1].strip()
    return ""


def drop_stale_cache(c):
    """The install folder was moved or renamed: CMake and Ninja refuse a cache (and re-run against the old -S/-B) made for
    another folder, so it is dropped and configured again. The lifted sources and SPU jobs are kept (they hold no path).
    Returns True when it dropped one."""
    b = c.build / "port"
    home = cache_home(b)
    if home and norm(home) != norm(c.root / "port"):
        say(f"[build] the CMake cache was made for {home}, this folder is {c.root}: configuring again")
        shutil.rmtree(b, ignore_errors=True)
        return True
    return False


def step_build(c):
    drop_stale_cache(c)
    jobs = build_jobs(c.a.jobs_build)
    write_build_cmd(c, jobs)
    env, cfg, bld = build_plan(c, jobs)
    b = c.build / "port"
    say(f"[build] {jobs} parallel compiles ({avail_gb():.1f} GB RAM free, about {GB_PER_JOB:g} GB each); the first build takes a long time "
        "(the maintainer measured 393 s with the earlier compiler on a fast PC)")
    if not (b / "build.ninja").is_file():
        ok, _ = run("build: cmake configure", cfg, c.build / "configure.log", env=env)
        if not ok:
            return False
    ok, _ = run("build: ninja", bld, c.build / "build.log", env=env)
    return ok and (b / c.exe).is_file()


def vfs_root(disc):
    """PS3_VFS_ROOT value: forward slashes and exactly one trailing '/'. A drive root ('E:\\', 'E:/', 'E:') gives 'E:/' (not
    'E://'); a folder ('D:\\Games\\BLUS31519\\', 'D:/Games/BLUS31519') gives 'D:/Games/BLUS31519/'."""
    d = str(disc).replace("\\", "/").rstrip("/")
    return d + "/"


def step_place(c):
    game = c.root / "game"
    (game / "run").mkdir(parents=True, exist_ok=True)
    exe = c.exe
    shutil.copy2(c.build / "port" / exe, game / exe)
    shutil.copy2(c.eboot, game / "EBOOT.elf")   # the player's own decrypted file, copied into the player's own install
    if (c.root / "ex_logo_A.rgba").is_file():   # the "EX" in the title logo (toolkit patch 0006; JSP_EX_LOGO=0 turns it off)
        shutil.copy2(c.root / "ex_logo_A.rgba", game / "ex_logo_A.rgba")
    inc = ffmpeg_include(c.root)
    if inc and getattr(c.a, "rpcs3", ""):   # FFmpeg DLLs from the player's RPCS3, beside the exe (Windows looks there first); see check_movie
        src = pathlib.Path(c.a.rpcs3)
        names = ffmpeg_dll_names(inc) + sorted(x.name for x in src.glob("swresample-*.dll"))
        got = [n for n in names if (src / n).is_file()]
        for n in got:
            shutil.copy2(src / n, game / n)
        say(f"[place] movie decoder: {', '.join(got) or 'none'} copied from the RPCS3 folder")
    a = c.a
    disc = pack_path(vfs_root(disc_root(a.disc)), c.root)
    hd = lambda p: pack_path(str(pathlib.Path(p)).replace(chr(92), "/"), c.root)
    pathlib.Path(a.devflash).mkdir(parents=True, exist_ok=True)   # main() set it; see there
    lines = [f"PS3_VFS_ROOT={disc}", f"PS3_HDD0_ROOT={hd(a.hdd0)}", f"PS3_DEV_FLASH={hd(a.devflash)}",
             f"PS3_TITLE={PROFILE['game_title_env']}", "PS3_VERBOSE=0", f"EXE={exe}", "ELF=EBOOT.elf"]
    pathlib.Path(a.hdd0).mkdir(parents=True, exist_ok=True)   # relative to the folder if inside; see unpack_path
    (c.root / "paths.txt").write_text("\n".join(lines) + "\n")
    say(f"[place] {game / exe} ({(game / exe).stat().st_size / 1e6:.0f} MB), paths.txt for the launcher")
    build_settings_window(c.root)
    return True


def build_settings_window(root):
    """jsp-settings.exe (the settings window, tools/jsp-settings.cs) with the C# compiler that comes with Windows (.NET Framework 4),
    when it is not there yet: setup builds it while copying files; the portable zip gets it here. Never fails the step."""
    src, out = root / "tools" / "jsp-settings.cs", root / "jsp-settings.exe"
    if out.is_file() or not src.is_file() or os.name != "nt":
        return
    win = os.environ.get("WINDIR", r"C:\Windows")
    csc = next((p for p in (pathlib.Path(win) / "Microsoft.NET" / f / "v4.0.30319" / "csc.exe" for f in ("Framework64", "Framework"))
                if p.is_file()), None)
    if not csc:
        say("[place] settings window: no C# compiler of Windows found; start the game with jsp-launcher.exe")
        return
    cmd = [str(csc), "/nologo", "/target:winexe", "/optimize+", "/r:System.Windows.Forms.dll", "/r:System.Drawing.dll",
           f"/out:{out}", str(src)]
    if (root / "jsp-launcher.ico").is_file():
        cmd.insert(4, f"/win32icon:{root / 'jsp-launcher.ico'}")
    r = subprocess.run(cmd, capture_output=True, text=True)
    say(f"[place] settings window: {'jsp-settings.exe built' if out.is_file() else 'not built (csc exit ' + str(r.returncode) + ')'}")


def ask_valid(ask, out, title, saved, check, hint):
    """Ask until check(answer) returns no failed line. '' keeps the saved answer, q quits. None = quit or input ended."""
    cur = saved
    while True:
        try:
            ans = ask(f"{title}" + (f" [{cur}]" if cur else "") + ": ").strip().strip('"')
        except EOFError:
            print("(input ended)", file=out)
            return None
        if ans.lower() == "q":
            return None
        ans = ans or cur
        bad = [m for lvl, _cid, m in check(ans) if lvl == "FAIL"] if ans else [hint]
        if not bad:
            return ans
        for m in bad:
            print("  " + m, file=out)


def interactive(a, root, ask=input, out=None):
    """The portable zip's text UI: fills a.disc and a.eboot from questions (same checks as the installer's pages),
    keeps them in jsp-install.ini. Returns False when the player quit (q) or input ended."""
    out = out or sys.stdout
    cp = read_ini(root)
    if not cp.has_section("install"):
        cp.add_section("install")
    saved = {k: unpack_path(ini_get(cp, k), root) for k in ("disc", "eboot")}
    print("\nJ-Stars Victory VS EX: Fan Revival: first run (portable)", file=out)
    print("Nothing of the game is included or downloaded. This builds the port on your PC from your own copy of the game.\n"
          "Everything stays in this folder: " + str(root) + "\nType q at any question to stop; start again later and finished steps are kept.\n",
          file=out)
    d = ask_valid(ask, out, "Folder of your game disc (the one that holds PS3_GAME)", saved["disc"],
                  lambda x: check_disc(x)[0], "Type the folder of your game disc.")
    if d is None:
        return False
    e = ask_valid(ask, out, "Your decrypted EBOOT.elf, from your own copy (see RPCS3 and its documentation)", saved["eboot"],
                  check_eboot, "Type the path of your decrypted EBOOT.elf.")
    if e is None:
        return False
    a.disc, a.eboot = str(disc_root(d)), e
    if ffmpeg_include(root) and not a.rpcs3:   # optional: the opening movie needs RPCS3's FFmpeg DLLs (see check_movie)
        cur = unpack_path(ini_get(cp, "rpcs3"), root)
        while True:
            try:
                ans = ask("RPCS3 folder, for the opening movie (Enter " + ("keeps it" if cur else "skips it: the movie stays black") + ")" +
                          (f" [{cur}]" if cur else "") + ": ").strip().strip('"')
            except EOFError:
                ans = ""
            if ans.lower() == "q":
                return False
            ans = ans or cur
            if not ans:
                break
            lvl, _cid, msg = check_movie(root, ans)[0]
            if lvl == "OK":
                cur = ans
                break
            print("  " + msg, file=out)
        a.rpcs3 = cur
        cp.set("install", "rpcs3", pack_path(cur, root) if cur else "")
    for k, v in (("disc", a.disc), ("title_id", check_disc(a.disc)[1]), ("eboot", a.eboot)):
        cp.set("install", k, pack_path(v, root) if k != "title_id" else v)
    cp.set("install", "portable", "1" if a.portable else ini_get(cp, "portable", "0"))
    write_ini(root, cp)
    return True


def main(argv=None, ask=input):
    global _log
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", help="the install folder (default: the folder of this script)")
    ap.add_argument("--disc")
    ap.add_argument("--eboot")
    ap.add_argument("--devflash", default="", help="the dev_flash folder of an RPCS3 install (read only at run time)")
    ap.add_argument("--rpcs3", default="", help="RPCS3 folder: its FFmpeg DLLs are copied beside the game for the opening movie (optional)")
    ap.add_argument("--hdd0", default="", help="folder for the game's saves and installed data (default: the data folder)")
    ap.add_argument("--log", help="append the log to this file")
    ap.add_argument("--logdir", help=f"write a new firstrun-<time>.log in this folder and keep the newest {LOGS_KEPT}")
    ap.add_argument("--until", default="all", choices=["checks", "lift", "spu", "build", "all"])
    ap.add_argument("--toolchain", help="folder with llvm-mingw/, cmake/bin, ninja/ (default: toolchain next to this script)")
    ap.add_argument("--python", help="python.exe CMake uses for the HLE table (default: this one)")
    ap.add_argument("--jobs", type=int, default=0, help="parallel workers (default: by CPUs and RAM)")
    ap.add_argument("--force", action="append", default=[], help="redo this step even if its marker is current")
    ap.add_argument("--interactive", action="store_true", help="the portable zip's text UI: ask for the inputs, keep the answers")
    ap.add_argument("--answers", help="with --interactive: read the answers from this file, one per line (scripted test runs; "
                                      "an empty line keeps the saved answer)")
    ap.add_argument("--portable", action="store_true", help="saves and settings in the install folder (jsp-install.ini portable=1)")
    a = ap.parse_args(argv)
    a.root = os.path.abspath(a.root or str(pathlib.Path(__file__).resolve().parent))   # CMake is run from other folders: absolute paths only
    root = pathlib.Path(a.root)
    if not a.interactive and not (a.disc and a.eboot):
        ap.error("--disc and --eboot are required (or --interactive)")
    a.portable = is_portable(root, a.portable)
    if a.answers:
        lines = iter(pathlib.Path(a.answers).read_text(encoding="utf-8-sig").splitlines())

        def ask(prompt="", _it=lines):
            try:
                ans = next(_it)
            except StopIteration:
                raise EOFError
            print(f"{prompt}{ans}")
            return ans
    logfile = a.log or (open_logdir(a.logdir) if a.logdir else (str(root / "logs" / "firstrun.log") if a.interactive else None))
    if logfile:
        pathlib.Path(logfile).parent.mkdir(parents=True, exist_ok=True)
        _log = open(logfile, "a", encoding="utf-8")
    a.toolchain = os.path.abspath(a.toolchain or str(root / "toolchain"))
    a.jobs_build = a.jobs                      # 0 = decide from the free RAM when the build starts
    a.jobs = a.jobs or max(1, min(os.cpu_count() or 1, 4))
    say(f"== firstrun {time.strftime('%Y-%m-%d %H:%M:%S')} until={a.until} root={a.root} portable={int(a.portable)}")
    if a.interactive and not interactive(a, root, ask):
        say("RESULT: stopped, nothing was changed beyond the answers saved so far")
        return 3
    a.hdd0 = a.hdd0 or str(data_dir(root, a.portable) / "hdd0")
    loc = root / "build" / "location.txt"
    if loc.is_file() and norm(loc.read_text().strip()) != norm(root):
        say(f"the folder was moved or renamed (was {loc.read_text().strip()}); paths inside it are re-derived")
    loc.parent.mkdir(parents=True, exist_ok=True)
    loc.write_text(str(root) + "\n")
    checks = check_paths(a.root, a.disc)
    disc_checks, tid = check_disc(a.disc) if not path_problems(a.disc, "disc folder") else ([], "")
    checks += disc_checks + check_eboot(a.eboot, a.disc) + check_space(a.root)
    if a.until in ("build", "all"):
        checks += check_toolchain(a.toolchain)
    if a.until == "all" and a.devflash:
        checks.append(check_flash(a.devflash))
    if a.until == "all":
        checks += check_movie(a.root, a.rpcs3)
    for lvl, cid, msg in checks:
        say(f"{lvl:<4} {cid} {msg}")
    if any(lvl == "FAIL" for lvl, _, _ in checks):
        say("RESULT: failed, see the FAIL lines above")
        return 1
    if a.until == "checks":
        say("RESULT: ok, inputs accepted")
        return 0
    # No dev_flash given (the usual case): an empty folder of the install's own, so the runtime never falls back to a
    # dev_flash path of its own. Set after the checks (F1 only checks a folder that was given).
    a.devflash = a.devflash or str(root / "dev_flash")
    c = Ctx(a, TITLE_IDS[tid], tid)
    reset_if_moved(c)
    last = STEPS.index("place" if a.until == "all" else a.until)
    fns = {"lift": step_lift, "spu": step_spu, "build": step_build, "place": step_place}
    for s in STEPS[1:last + 1]:
        why = c.stale_reason(s)
        if not why:
            say(f"[{s}] already done (inputs unchanged), skipped")
            continue
        say(f"[{s}] running: {why}")
        if s == "build" and why.startswith("input changed") and ("toolchain" in why or "ffmpeg" in why):
            shutil.rmtree(c.build / "port", ignore_errors=True)   # the CMake cache names the old compiler
        if not fns[s](c):
            say(f"RESULT: failed, step {s} stopped; see {c.build}")
            return 1
        c.mark(s)
    say(f"RESULT: ok, steps up to {STEPS[last]} done")
    if a.until == "all":
        say("Start the game with jsp-launcher.exe in this folder. The first start installs the game data from your disc.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
