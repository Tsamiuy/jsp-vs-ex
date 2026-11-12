#!/usr/bin/env python3
"""Make a support bundle: one zip of the logs and settings that a tester can attach to an issue.

Run by the payload's bundled Python (the Start-menu entry "Make a support bundle" runs support-bundle.cmd, which does):
    toolchain\\python\\python.exe -X utf8 support_bundle.py --root APP [--out DIR]
Writes jsp-support-YYYYmmdd-HHMMSS.zip to DIR (default: the Desktop, else the install folder) and prints its path.
Stdlib only. Contacts no server: the zip is only made, never sent; the tester attaches it by hand.

What goes in (text only: .log .txt .ini .cmd), every file through firstrun.scrub (the user's profile folder becomes %USERPROFILE%):
  logs\\*.log, build\\**\\*.log (step logs), game\\run\\*.txt, the *.log, *.txt, *.ini and *.cmd files of the install folder
  (play_settings.txt, jsp-install.ini, build-again.cmd, paths.txt, VERSION.txt, tree.txt when present), the settings file in
  %APPDATA%\\JSP Fan Revival, and the newest two Inno Setup logs in %TEMP% that mention this app. Plus facts.txt.
facts.txt: version, Windows, CPU count and model, AVX2, RAM, free space, the game exe, step markers, the GPU (from the game's log) and
each display adapter's driver version (read from the registry, never changed).
What never goes in: the EBOOT, disc files, saves (hdd0, .sav), lift output (.c/.h), shader caches, binaries, environment dumps.
A total cap (CAP_BYTES, uncompressed) applies: over it, the oldest lines of the largest logs go first and facts.txt says what was cut.

Idea credit: the installer of undecuple's Killzone 3 port (Project Helghast) has a similar support package; no code is copied from it.
"""
import argparse
import ctypes
import datetime
import os
import pathlib
import platform
import re
import shutil
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import firstrun  # noqa: E402  scrub() is reused, not rewritten (firstrun.py sits beside this file, in the payload too)

APPDIR = firstrun.APPDIR
CAP_BYTES = 20 * 1000 * 1000
TEXT_KINDS = {".log", ".txt", ".ini", ".cmd"}
# Same list as assemble_payload.FORBIDDEN (the kinds the game ships); repeated because the payload has no assemble_payload.py.
# .sav: saves. They are never read, whatever folder they are in.
DENIED_KINDS = {".elf", ".bin", ".sfo", ".psarc", ".edat", ".self", ".sprx", ".sav"}
DENIED_DIRS = {"hdd0", "dev_hdd0", "dev_flash", "savedata", "shadercache", "shader_cache", "shaders"}
SETTINGS_NAMES = ("play_settings.txt", "settings_here.txt")
NOTE = "[support_bundle: the oldest {n} lines ({b} bytes) of this file were cut to keep the bundle under {cap} MB]\n"


def denied(rel):
    """Why a path is never read ('' when it may be): game data kinds, saves, game-data folders, an EBOOT by name."""
    parts = [p.lower() for p in pathlib.PurePosixPath(rel.replace("\\", "/")).parts]
    if any(p in DENIED_DIRS for p in parts[:-1]):
        return "game data or saves folder"
    name = parts[-1]
    if pathlib.PurePosixPath(name).suffix in DENIED_KINDS:
        return "game data or save kind"
    if name.startswith("eboot"):
        return "EBOOT"
    return ""


def collect(app, appdata=None, temp=None):
    """-> (entries {zip name: source path}, refused [(name, why)], not_text count). Nothing outside app except the settings file
    in appdata and Inno's setup logs in temp."""
    app = pathlib.Path(app)
    entries, refused, not_text = {}, [], 0

    def offer(name, path):
        why = denied(name)
        if why:
            refused.append((name, why))
        elif pathlib.Path(name).suffix.lower() in TEXT_KINDS and path.is_file():
            entries[name] = path

    def walk(sub, only_log=False):
        nonlocal not_text
        top = app / sub if sub else app
        if not top.is_dir():
            return
        for d, dirs, files in os.walk(top):
            if not sub:
                dirs[:] = []                                    # the install folder itself: top level only
            dirs[:] = [x for x in dirs if x.lower() not in DENIED_DIRS and x != "__pycache__"]
            for f in files:
                p = pathlib.Path(d) / f
                rel = p.relative_to(app).as_posix()
                ext = p.suffix.lower()
                if ext in DENIED_KINDS or denied(rel):
                    refused.append((rel, denied(rel) or "game data or save kind"))
                elif ext not in TEXT_KINDS or (only_log and ext != ".log"):
                    not_text += 1
                elif p.is_file() and not p.is_symlink():
                    entries[rel] = p

    walk("")
    walk("logs")
    walk("build", only_log=True)
    run = app / "game" / "run"
    if run.is_dir():
        for p in sorted(run.iterdir()):
            if p.is_file() and not p.is_symlink():
                offer(f"game/run/{p.name}", p)
    if appdata:
        d = pathlib.Path(appdata) / APPDIR
        for n in SETTINGS_NAMES:
            offer(f"appdata/{n}", d / n)
    if temp and pathlib.Path(temp).is_dir():
        logs = sorted(pathlib.Path(temp).glob("Setup Log *.txt"), key=lambda f: f.stat().st_mtime, reverse=True)
        keep = 0
        for p in logs:
            try:
                head = p.read_bytes()[:400000].decode("utf-8", "replace").lower()
            except OSError:
                continue
            if APPDIR.lower() in head or str(app).lower().replace("\\", "/") in head.replace("\\", "/"):
                entries[f"setup-logs/{p.name}"] = p
                keep += 1
                if keep == 2:
                    break
    return entries, refused, not_text


def read_scrubbed(path):
    """File text through firstrun.scrub, as UTF-8 bytes."""
    t = pathlib.Path(path).read_bytes().decode("utf-8", "replace")
    return firstrun.scrub(t).encode("utf-8")


def cut_oldest(data, drop):
    """Drop about `drop` bytes of the oldest lines of data; -> (new bytes, lines cut, bytes cut)."""
    i = data.find(b"\n", min(drop, len(data) - 1))
    i = len(data) if i < 0 else i + 1
    return data[i:], data[:i].count(b"\n"), i


def trim(blobs, cap):
    """Keep the total under cap by cutting the oldest lines of the largest file first (it is halved at most each time, so the
    big logs share the loss). -> list of what was cut."""
    cuts = {}
    note_room = 200
    while sum(len(b) for b in blobs.values()) + note_room * len(cuts) > cap:
        name = max(blobs, key=lambda k: len(blobs[k]))
        if len(blobs[name]) < 2 * note_room:
            break
        excess = sum(len(b) for b in blobs.values()) + note_room * len(cuts) - cap
        data = blobs[name]
        body = data
        n0, b0 = cuts.get(name, (0, 0))
        if n0:                                                   # strip the earlier note before cutting again
            body = data[data.index(b"\n") + 1:]
        body, n, b = cut_oldest(body, min(excess, len(body) // 2))
        cuts[name] = (n0 + n, b0 + b)
        blobs[name] = NOTE.format(n=n0 + n, b=b0 + b, cap=cap // 1000000).encode() + body
    return [f"{k}: oldest {n} lines ({b} bytes) cut" for k, (n, b) in sorted(cuts.items())]


def ram_gb():
    try:
        class MS(ctypes.Structure):
            _fields_ = [("l", ctypes.c_ulong), ("load", ctypes.c_ulong)] + [(f"f{i}", ctypes.c_ulonglong) for i in range(7)]
        m = MS()
        m.l = ctypes.sizeof(MS)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m)):
            return f"{m.f0 / 2**30:.1f} GB"
    except (AttributeError, OSError):
        pass
    try:                                                         # not Windows (host tests)
        with open("/proc/meminfo") as f:
            return f"{int(f.readline().split()[1]) / 2**20:.1f} GB"
    except (OSError, ValueError, IndexError):
        return "unknown"


def reg_values(root, path, names):
    """Values of one registry key (HKEY_LOCAL_MACHINE only, read only); {} when absent or not Windows."""
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, path) as k:
            out = {}
            for n in names:
                try:
                    out[n] = str(winreg.QueryValueEx(k, n)[0]).strip()
                except OSError:
                    pass
            return out
    except (ImportError, OSError):
        return {}


def reg_subkeys(path):
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, path) as k:
            return [winreg.EnumKey(k, i) for i in range(winreg.QueryInfoKey(k)[0])]
    except (ImportError, OSError):
        return []


def cpu_model(read=reg_values):
    """The CPU's model name: the registry's ProcessorNameString on Windows, /proc/cpuinfo elsewhere (host tests)."""
    v = read(None, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0", ("ProcessorNameString",)).get("ProcessorNameString", "")
    if v:
        return " ".join(v.split())
    try:
        with open("/proc/cpuinfo") as f:
            for line in f:
                if line.lower().startswith("model name"):
                    return " ".join(line.split(":", 1)[1].split())
    except OSError:
        pass
    return platform.processor() or "unknown"


def avx2():
    """IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE = 40); "unknown" off Windows or on a Windows too old to say."""
    try:
        return "yes" if ctypes.windll.kernel32.IsProcessorFeaturePresent(40) else "no (or not reported by this Windows)"
    except (AttributeError, OSError):
        return "unknown"


# The display adapters' driver keys (the Display device class); each numbered subkey is one installed adapter.
DISPLAY_CLASS = r"SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}"


def gpu_drivers(subkeys=reg_subkeys, read=reg_values):
    """'NAME: driver VERSION (DATE)' for each display adapter in the registry; [] when none or not Windows."""
    out = []
    for sk in subkeys(DISPLAY_CLASS):
        if not sk.isdigit():
            continue
        v = read(None, DISPLAY_CLASS + "\\" + sk, ("DriverDesc", "DriverVersion", "DriverDate"))
        if v.get("DriverDesc") and v.get("DriverVersion"):
            out.append(f"{v['DriverDesc']}: driver {v['DriverVersion']}" + (f" ({v['DriverDate']})" if v.get("DriverDate") else ""))
    return out


def gpu_name(blobs):
    """The GPU from the toolkit's Direct3D 12 start-up line ("[D3D12] adapter: NAME (VRAM N MB)", printed to the game's stdout,
    which the launcher keeps in game\\run\\play_stdout.txt); else the first other log line that names a GPU or adapter."""
    exact = re.compile(rb"\[D3D12\] adapter: ([^\n\r]{1,160})")
    loose = re.compile(rb"(?i)\b(?:gpu|adapter|graphics (?:card|device)|video card)\b[^\n:=]{0,20}[:=]\s*([^\n\r]{3,120})")
    for pat in (exact, loose):
        for name in sorted(blobs):
            m = pat.search(blobs[name])
            if m:
                return f"{m.group(1).decode('utf-8', 'replace').strip()} (from {name})"
    return "not found in the logs"


def facts(app, blobs, refused, not_text, cuts, names):
    app = pathlib.Path(app)
    L = ["J-Stars Fan Revival support bundle", f"made: {datetime.datetime.now():%Y-%m-%d %H:%M:%S}"]
    for n in ("VERSION.txt", "tree.txt"):
        p = app / n
        L.append(f"{n}: " + (p.read_text("utf-8", "replace").strip().replace("\n", " | ")[:200] if p.is_file() else "absent"))
    L.append(f"Windows: {platform.platform()} ({platform.version()})")
    L.append(f"CPUs: {os.cpu_count() or os.environ.get("NUMBER_OF_PROCESSORS", "unknown")}   RAM: {ram_gb()}")
    L.append(f"CPU: {cpu_model()}   AVX2: {avx2()}")
    try:
        L.append(f"free space on the install drive: {shutil.disk_usage(app).free / 2**30:.1f} GB")
    except OSError:
        L.append("free space on the install drive: unknown")
    L.append(f"bundled Python: {sys.version.split()[0]} ({platform.architecture()[0]})")
    exe_name = firstrun.TREE_EXE[firstrun.payload_tree(app)]   # the tree's exe: jsp.exe (current) or JStarsRecomp.exe (legacy)
    exe = app / "game" / exe_name
    if exe.is_file():
        st = exe.stat()
        L.append(f"game\\{exe_name}: present, {st.st_size} bytes, {datetime.datetime.fromtimestamp(st.st_mtime):%Y-%m-%d %H:%M:%S}")
    else:
        L.append(f"game\\{exe_name}: absent")
    mk = app / "build" / "markers"
    done = sorted(p.stem for p in mk.glob("*.done")) if mk.is_dir() else []
    L.append("first-run step markers: " + (", ".join(done) if done else "none") + "   (steps: " + ", ".join(firstrun.STEPS) + ")")
    L.append(f"GPU: {gpu_name(blobs)}")
    drivers = gpu_drivers()
    L.append("GPU driver: " + ("; ".join(drivers) if drivers else "not found (not Windows, or no display driver key)"))
    L.append("")
    L.append("files in this zip:")
    L += [f"  {n}" for n in names]
    L.append("")
    L.append("shortened: " + ("; ".join(cuts) if cuts else "nothing"))
    L.append("refused (never read): " + ("; ".join(f"{n} ({w})" for n, w in refused[:40]) if refused else "nothing"))
    L.append(f"skipped as not text (binaries, lift output, caches): {not_text} files")
    return firstrun.scrub("\n".join(L) + "\n")


def desktop():
    """The user's Desktop folder (Windows asks the shell, so a redirected or OneDrive Desktop is found); '' when none."""
    try:
        buf = ctypes.create_unicode_buffer(520)
        if ctypes.windll.shell32.SHGetFolderPathW(None, 0x10, None, 0, buf) == 0 and buf.value:   # CSIDL_DESKTOPDIRECTORY
            return buf.value
    except (AttributeError, OSError):
        pass
    d = pathlib.Path.home() / "Desktop"
    return str(d) if d.is_dir() else ""


def make(app, out=None, appdata=None, temp=None, cap=CAP_BYTES):
    """Builds the zip; -> its path. out None: the Desktop, else app."""
    app = pathlib.Path(app)
    entries, refused, not_text = collect(app, appdata, temp)
    blobs = {}
    for name, p in entries.items():
        try:
            blobs[name] = read_scrubbed(p)
        except OSError:
            pass                                                 # unreadable or gone: skipped like an absent file
    cuts = trim(blobs, cap - 20000)                              # room for facts.txt
    names = sorted(list(blobs) + ["facts.txt"])
    f = facts(app, blobs, refused, not_text, cuts, names).encode("utf-8")
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    for d in ([pathlib.Path(out)] if out else [pathlib.Path(x) for x in (desktop(),) if x] + [app]):
        try:
            d.mkdir(parents=True, exist_ok=True)
            zp = d / f"jsp-support-{stamp}.zip"
            with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED) as z:
                z.writestr("facts.txt", f)
                for n in names:
                    if n != "facts.txt":
                        z.writestr(n, blobs[n])
            return zp
        except OSError as e:
            print(f"cannot write to {firstrun.scrub(str(d))}: {firstrun.scrub(str(e))}", file=sys.stderr)
    raise SystemExit("no folder to write the zip to")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", required=True, help="the install folder (or the unpacked portable folder)")
    ap.add_argument("--out", help="folder for the zip (default: Desktop, else the install folder)")
    ap.add_argument("--quiet", action="store_true", help="print only the zip's path (support-bundle.cmd reads it)")
    a = ap.parse_args(argv)
    if not pathlib.Path(a.root).is_dir():
        print(f"not a folder: {a.root}", file=sys.stderr)
        return 2
    zp = make(a.root, a.out, os.environ.get("APPDATA"), os.environ.get("TEMP") or os.environ.get("TMP"))
    if a.quiet:
        print(zp)                                                # the real path: the caller opens Explorer on it
    else:
        print(f"Support bundle written: {firstrun.scrub(str(zp))}")
        print("Attach this one file to your issue. It holds logs and settings only, with your user name removed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
