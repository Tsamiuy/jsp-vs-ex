#!/usr/bin/env python3
"""Make a support bundle: one zip of the logs and settings that a tester can attach to an issue.

Run by the payload's bundled Python (the Start-menu entry "Make a support bundle" runs support-bundle.cmd, which does):
    toolchain\\python\\python.exe -X utf8 support_bundle.py --root APP [--out DIR]
Writes jsp-support-YYYYmmdd-HHMMSS.zip to DIR (default: the Desktop, else the install folder) and prints its path.
Stdlib only. Contacts no server: the zip is only made, never sent; the tester attaches it by hand.

What goes in (text only: .log .txt .ini .cmd), every file through firstrun.scrub (the user's profile folder becomes %USERPROFILE%) and net_scrub
(the re-psn server address, the rollback join address, the online names and every IPv4 address but loopback become placeholders):
  logs\\*.log, build\\**\\*.log (step logs), game\\run\\*.txt (play_stderr.prev.txt is the launch before the last one), the *.log, *.txt, *.ini and *.cmd files of the install folder
  (play_settings.txt, jsp-install.ini, build-again.cmd, paths.txt, VERSION.txt, tree.txt when present), the settings file in
  %APPDATA%\\JSP Fan Revival, and the newest two Inno Setup logs in %TEMP% that mention this app. Plus facts.txt.
facts.txt: version, Windows, CPU count and model, AVX2, RAM, free space, the game exe, step markers, the GPU (from the game's log) and
each display adapter's driver version (read from the registry, never changed), and each display (monitor name, size, refresh rate, which one
the game window is on when the game is running; no serial numbers), and a Netcode section: the mode the settings files choose (Off, Delay over
re-psn or a community server, Rollback direct or over re-psn with host / join and the relay choice; server, join address and online name
only as "set" / "not set") and, per launch, what the game's [net] lines say (rollback started, refused and why, path, session synchronized,
rollback and desync counts, the key lines), read before the size cap cuts anything.
What never goes in: the EBOOT, disc files, saves (hdd0, .sav), lift output (.c/.h), shader caches, binaries, environment dumps.
A total cap (CAP_BYTES, uncompressed) applies: over it, the oldest lines of the largest logs go first and facts.txt says what was cut
(the netcode summary and key [net] lines in facts.txt are made before the cut, so they survive it).

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


def read_scrubbed(path, secrets=None):
    """File text through firstrun.scrub (the profile folder) and net_scrub (server and join addresses, online names), as UTF-8 bytes."""
    t = pathlib.Path(path).read_bytes().decode("utf-8", "replace")
    return net_scrub(firstrun.scrub(t), secrets).encode("utf-8")


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


# ---- the displays (Windows only; read only): model, size, refresh rate, and the one the game window is on ----
# A 60 Hz screen turns missed 30 fps frames into 20 fps, and some PCs run a virtual display (Parsec, a headset, a capture card), so
# facts.txt names every display. No serial numbers or device IDs are read.
class _Rect(ctypes.Structure):
    _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long), ("right", ctypes.c_long), ("bottom", ctypes.c_long)]


class _MonitorInfo(ctypes.Structure):
    _fields_ = [("cbSize", ctypes.c_ulong), ("rcMonitor", _Rect), ("rcWork", _Rect), ("dwFlags", ctypes.c_ulong), ("szDevice", ctypes.c_wchar * 32)]


class _DisplayDevice(ctypes.Structure):
    _fields_ = [("cb", ctypes.c_ulong), ("DeviceName", ctypes.c_wchar * 32), ("DeviceString", ctypes.c_wchar * 128),
                ("StateFlags", ctypes.c_ulong), ("DeviceID", ctypes.c_wchar * 128), ("DeviceKey", ctypes.c_wchar * 128)]


class _DevMode(ctypes.Structure):   # DEVMODEW; only the display fields are used
    _fields_ = [("dmDeviceName", ctypes.c_wchar * 32), ("dmSpecVersion", ctypes.c_ushort), ("dmDriverVersion", ctypes.c_ushort),
                ("dmSize", ctypes.c_ushort), ("dmDriverExtra", ctypes.c_ushort), ("dmFields", ctypes.c_ulong), ("_pos", ctypes.c_byte * 16),
                ("dmColor", ctypes.c_short), ("dmDuplex", ctypes.c_short), ("dmYResolution", ctypes.c_short), ("dmTTOption", ctypes.c_short),
                ("dmCollate", ctypes.c_short), ("dmFormName", ctypes.c_wchar * 32), ("dmLogPixels", ctypes.c_ushort),
                ("dmBitsPerPel", ctypes.c_ulong), ("dmPelsWidth", ctypes.c_ulong), ("dmPelsHeight", ctypes.c_ulong),
                ("dmDisplayFlags", ctypes.c_ulong), ("dmDisplayFrequency", ctypes.c_ulong), ("_rest", ctypes.c_ulong * 8)]


def displays():
    """[{name, adapter, monitor, width, height, hz, primary, handle}] for each attached display; [] off Windows or on any error."""
    try:
        from ctypes import wintypes
        u = ctypes.windll.user32
        u.EnumDisplayMonitors.argtypes = [wintypes.HDC, ctypes.c_void_p, ctypes.c_void_p, wintypes.LPARAM]
        u.EnumDisplayMonitors.restype = wintypes.BOOL
        u.GetMonitorInfoW.argtypes = [ctypes.c_void_p, ctypes.POINTER(_MonitorInfo)]
        u.GetMonitorInfoW.restype = wintypes.BOOL
        u.EnumDisplayDevicesW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, ctypes.POINTER(_DisplayDevice), wintypes.DWORD]
        u.EnumDisplayDevicesW.restype = wintypes.BOOL
        u.EnumDisplaySettingsW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, ctypes.POINTER(_DevMode)]
        u.EnumDisplaySettingsW.restype = wintypes.BOOL
        handles = []
        cb = ctypes.WINFUNCTYPE(wintypes.BOOL, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, wintypes.LPARAM)(
            lambda h, _dc, _r, _l: (handles.append(h), True)[1])
        u.EnumDisplayMonitors(None, None, cb, 0)
        adapters = {}
        i = 0
        while True:
            dd = _DisplayDevice()
            dd.cb = ctypes.sizeof(dd)
            if not u.EnumDisplayDevicesW(None, i, ctypes.byref(dd), 0):
                break
            adapters[dd.DeviceName] = dd.DeviceString
            i += 1
        out = []
        for h in handles:
            mi = _MonitorInfo()
            mi.cbSize = ctypes.sizeof(mi)
            if not u.GetMonitorInfoW(h, ctypes.byref(mi)):
                continue
            mon = _DisplayDevice()
            mon.cb = ctypes.sizeof(mon)
            monitor = mon.DeviceString if u.EnumDisplayDevicesW(mi.szDevice, 0, ctypes.byref(mon), 0) else ""
            dm = _DevMode()
            dm.dmSize = ctypes.sizeof(dm)
            ok = u.EnumDisplaySettingsW(mi.szDevice, 0xFFFFFFFF, ctypes.byref(dm))   # ENUM_CURRENT_SETTINGS
            out.append({"name": mi.szDevice, "adapter": adapters.get(mi.szDevice, ""), "monitor": monitor,
                        "width": dm.dmPelsWidth if ok else 0, "height": dm.dmPelsHeight if ok else 0,
                        "hz": dm.dmDisplayFrequency if ok else 0, "primary": bool(mi.dwFlags & 1), "handle": h})
        return out
    except (AttributeError, OSError, ValueError, ImportError, TypeError):
        return []


def game_display(exe_name):
    """(display handle or None, 'WxH' or '') of the biggest visible top-level window of the running game exe; (None, '') when it is not running."""
    try:
        from ctypes import wintypes
        u, k = ctypes.windll.user32, ctypes.windll.kernel32
        k.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        k.OpenProcess.restype = ctypes.c_void_p
        k.QueryFullProcessImageNameW.argtypes = [ctypes.c_void_p, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
        k.CloseHandle.argtypes = [ctypes.c_void_p]
        u.GetWindowThreadProcessId.argtypes = [ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD)]
        u.IsWindowVisible.argtypes = [ctypes.c_void_p]
        u.GetWindowRect.argtypes = [ctypes.c_void_p, ctypes.POINTER(_Rect)]
        u.MonitorFromWindow.argtypes = [ctypes.c_void_p, wintypes.DWORD]
        u.MonitorFromWindow.restype = ctypes.c_void_p
        best = []
        cache = {}

        def is_game(pid):
            if pid not in cache:
                ok = False
                hp = k.OpenProcess(0x1000, False, pid)   # PROCESS_QUERY_LIMITED_INFORMATION
                if hp:
                    buf, n = ctypes.create_unicode_buffer(1040), wintypes.DWORD(1040)
                    if k.QueryFullProcessImageNameW(hp, 0, buf, ctypes.byref(n)):
                        ok = buf.value.replace("/", "\\").rsplit("\\", 1)[-1].lower() == exe_name.lower()
                    k.CloseHandle(hp)
                cache[pid] = ok
            return cache[pid]

        def each(hwnd, _l):
            pid = wintypes.DWORD()
            u.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if u.IsWindowVisible(hwnd) and is_game(pid.value):
                r = _Rect()
                if u.GetWindowRect(hwnd, ctypes.byref(r)):
                    best.append(((r.right - r.left) * (r.bottom - r.top), hwnd, f"{r.right - r.left}x{r.bottom - r.top}"))
            return True

        u.EnumWindows.argtypes = [ctypes.c_void_p, wintypes.LPARAM]
        u.EnumWindows(ctypes.WINFUNCTYPE(wintypes.BOOL, ctypes.c_void_p, wintypes.LPARAM)(each), 0)
        if not best:
            return None, ""
        _a, hwnd, size = max(best)
        return u.MonitorFromWindow(hwnd, 2), size   # MONITOR_DEFAULTTONEAREST
    except (AttributeError, OSError, ValueError, ImportError, TypeError):
        return None, ""


def display_lines(mons, game):
    """The facts.txt lines for the displays (pure: tested on any OS). game = (handle or None, window size)."""
    if not mons:
        return ["Displays: not found (not Windows, or the query failed)"]
    out = []
    gh, gsize = game
    for i, m in enumerate(mons, 1):
        hz = f"{m['hz']} Hz" if m.get("hz", 0) > 1 else "default Hz (the driver did not say)"
        size = f"{m['width']}x{m['height']}" if m.get("width") else "size unknown"
        on = "   <- game window" if gh is not None and m.get("handle") == gh else ""
        out.append(f"Display {i}{' (primary)' if m.get('primary') else ''}: {m.get('name', '')}, monitor: {m.get('monitor') or 'unnamed'}, "
                   f"on adapter: {m.get('adapter') or 'unknown'}, {size} at {hz}{on}")
    if gh is None:
        out.append("Game window: not running when this bundle was made (a bundle made while the game is open, even frozen, names its display)")
    else:
        out.append(f"Game window: {gsize or 'size unknown'}, on the display marked above")
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


def present_mode(blobs):
    """How the game presents frames, from the toolkit's start-up line ("[D3D12] present: ..." since toolkit patch 2915) in the logs;
    the last one when several runs are kept. Older builds do not print it."""
    pat = re.compile(rb"\[D3D12\] present: ([^\n\r]{1,160})")
    for name in sorted(blobs):
        found = pat.findall(blobs[name])
        if found:
            return f"{found[-1].decode('utf-8', 'replace').strip()} (from {name})"
    return "not in the logs (a build before toolkit patch 2915, or the game has not been started)"


# ---- the netcode: the mode the settings files choose, and what the game's [net] lines say (installer/port/jsp/jsp_net_game.cpp) ----
# The server address, the join address and the online names are private: facts.txt says only "set" / "not set", and net_scrub() takes them
# out of every file in the zip (the settings files, the [online] lines of play_stdout.txt and the [net] lines of play_stderr.txt name them).
NET_LOG_ORDER = ("game/run/play_stderr.txt", "game/run/play_stderr.prev.txt")   # this launch, then the one before (the launcher keeps it)
NET_KEY_LINES = 30


def read_kv(path):
    """KEY=VALUE lines of a settings file (play_settings.txt: '#' starts a comment, the last line of a key wins, as the launcher reads it)."""
    out = {}
    try:
        text = pathlib.Path(path).read_bytes()[:1000000].decode("utf-8", "replace")
    except OSError:
        return out
    for line in text.splitlines():
        line = line.split("#", 1)[0]
        k, eq, v = line.partition("=")
        if eq and k.strip():
            out[k.strip()] = v.strip()
    return out


def read_online_ini(path, into):
    """repsn.ini / community.ini sections into `into` ({section lower: {key lower: value}}), later files winning per key, as jsp_online.cpp reads them."""
    try:
        text = pathlib.Path(path).read_bytes()[:65536].decode("utf-8", "replace")
    except OSError:
        return False
    sec = ""
    for line in text.splitlines():
        line = line.strip()
        if not line or line[0] in ";#":
            continue
        if line.startswith("[") and line.endswith("]"):
            sec = line[1:-1].strip().lower()
            continue
        k, eq, v = line.partition("=")
        if eq and sec:
            into.setdefault(sec, {})[k.strip().lower()] = v.strip()
    return True


def net_settings(app, appdata=None, env=None):
    """The files the game and the launcher read for the netcode: {'play': {...}, 'ini': {section: {...}}, 'read': [where each was found]}.
    Same places as the launcher (InstalledSettingsFile, OnlineIni.SharedDir) and jsp_online.cpp (shared folder first, then the game's own repsn.ini)."""
    app = pathlib.Path(app)
    env = os.environ if env is None else env
    portable = firstrun.is_portable(app)
    local = app / "play_settings.txt"
    user = pathlib.Path(appdata) / APPDIR if appdata else None
    play = local if (portable or local.is_file() or (app / "settings_here.txt").is_file() or user is None) else user / "play_settings.txt"
    shared = pathlib.Path(env["PS3RECOMP_SHARED_DIR"]) if env.get("PS3RECOMP_SHARED_DIR") else \
        app / "shared" if portable else pathlib.Path(appdata) / "PS3Recomp" if appdata else None
    game_dir = app if portable or user is None else user
    s = {"play": read_kv(play) if play.is_file() else {}, "ini": {}, "read": []}
    if play.is_file():
        s["read"].append("play_settings.txt (" + ("install folder" if play == local else "user data folder") + ")")
    for d, n, where in ((shared, "repsn.ini", "shared folder"), (shared, "community.ini", "shared folder"), (game_dir, "repsn.ini", "game folder")):
        if d is not None and read_online_ini(d / n, s["ini"]):
            s["read"].append(f"{n} ({where})")
    return s


def _flag(v):
    return bool(v) and v[0] != "0"   # jpno::flag_on


def net_mode(s):
    """The facts.txt lines for the chosen netcode (pure). Mirrors jpno::resolve_env and jsp_online.cpp: rollback and the game's own online
    (Delay) both on is refused, neither runs."""
    p, ini = s["play"], s["ini"]
    rep, com = ini.get("repsn", {}), ini.get("community", {})
    rep_en, com_en = rep.get("enabled") == "1", com.get("enabled") == "1"
    by_vars = "PS3_NET_ONLINE" in p or bool(p.get("PSNR_SERVER"))   # set by hand in play_settings.txt: the launcher passes them on
    if rep_en and com_en:
        delay = "Delay: re-psn and community both enabled (the game refuses online play)"
    elif com_en:
        delay = "Delay (community server; no client yet, the game stays offline)"
    elif rep_en and rep.get("server"):
        delay = "Delay (re-psn)"
    elif by_vars:
        delay = "Delay (re-psn, from variables in play_settings.txt)"
    elif rep_en:
        delay = "Delay (re-psn enabled but no server set: the game stays offline)"
    else:
        delay = ""
    delay_on = by_vars or (rep_en and bool(rep.get("server")) and not com_en)
    if _flag(p.get("JSP_ROLLBACK", "")):
        if p.get("JSP_ROLLBACK_VIA", "").lower() == "repsn":
            role = p.get("JSP_ROLLBACK_ROLE", "").lower()
            mode = "Rollback re-psn " + (role if role in ("host", "join") else "(no valid role: the game refuses it)")
            mode += ", relay allowed: " + ("yes" if _flag(p.get("JSP_ROLLBACK_RELAY", "")) else "no")
        else:
            hl, hj = bool(p.get("JSP_ROLLBACK_LISTEN")), bool(p.get("JSP_ROLLBACK_JOIN"))
            mode = "Rollback direct " + ("host" if hl and not hj else "join" if hj and not hl else "(not exactly one of host / join: the game refuses it)")
        if delay_on:
            mode += "; the game's own online is on too: the game refuses both"
    else:
        mode = delay or "Off"
    server = bool(rep.get("server") or p.get("PSNR_SERVER"))
    name = bool(rep.get("name") or p.get("PS3_NP_ONLINE_ID"))
    st = lambda b: "set" if b else "not set"
    return [f"  mode: {mode}",
            f"  re-psn server: {st(server)}   online name: {st(name)}   rollback join address: {st(p.get('JSP_ROLLBACK_JOIN'))}",
            "  settings files read: " + (", ".join(s["read"]) if s["read"] else "none found")]


def net_secrets(s):
    """{value: placeholder} of the private values in the settings files: server and join addresses (whole and host part), online names."""
    p, ini = s["play"], s["ini"]
    out = {}

    def add(v, what, address=False):
        v = (v or "").strip()
        if len(v) >= 3:
            out.setdefault(v, what)
            if address:
                host = v.rsplit(":", 1)[0] if v.count(":") == 1 else v
                if len(host) >= 3:
                    out.setdefault(host, what)
    add(p.get("JSP_ROLLBACK_JOIN"), "<join address>", True)
    add(p.get("PSNR_SERVER"), "<server>", True)
    add(p.get("PS3_NP_ONLINE_ID"), "<online name>")
    for sec, kv in ini.items():
        add(kv.get("server"), "<server>", True)
        add(kv.get("address"), "<server>", True)
        add(kv.get("name"), "<online name>")
    return out


_IPV4 = re.compile(r"(?<![\d.])((?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(?:\.(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3})(?![\d]|\.\d)")
_NET_RULES = (   # always applied, also when the settings files were not found
    (re.compile(r"(?im)^(\s*(?:server|name|address|JSP_ROLLBACK_JOIN|PSNR_SERVER|PS3_NP_ONLINE_ID)\s*=[ \t]*)[^\s#;][^\r\n#;]*"), r"\1<set>"),
    (re.compile(r"(\[online\] re-psn enabled: server )\S+"), r"\1<server>"),
    (re.compile(r"(\[online\] online id )\S+"), r"\1<online name>"),
    (re.compile(r"(\[online\] --psnr )\S+"), r"\1<server>"),
    (re.compile(r"(\[net\] re-psn: (?:creating|joining) a room on )\S+( as )\S+"), r"\1<server>\2<online name>"),
    (re.compile(r"(the online name ')[^'\r\n]*(')"), r"\1<online name>\2"),
)


def net_scrub(text, secrets=None):
    """Takes the private netcode values out of a file's text: the values in `secrets` (net_secrets), the address and name fields of the
    known log lines and settings keys, and every IPv4 address except loopback and 0.0.0.0 (a port after it stays)."""
    for v in sorted(secrets or {}, key=len, reverse=True):
        text = re.sub(r"(?<![A-Za-z0-9_])" + re.escape(v) + r"(?![A-Za-z0-9_])", secrets[v], text, flags=re.IGNORECASE)
    for pat, rep in _NET_RULES:
        text = pat.sub(rep, text)
    return _IPV4.sub(lambda m: m.group(1) if m.group(1).startswith("127.") or m.group(1) == "0.0.0.0" else "<IPv4>", text)


def net_log_lines(blobs):
    """The facts.txt lines for what the game's [net] lines say (pure). Run on the logs before the size cap cuts anything."""
    names = [n for n in NET_LOG_ORDER if n in blobs] + sorted(n for n in blobs if n not in NET_LOG_ORDER)
    out, key = [], None
    for name in names:
        lines = [ln.strip() for ln in blobs[name].decode("utf-8", "replace").splitlines() if ln.lstrip().startswith("[net] ")]
        if not lines:
            continue
        label = {NET_LOG_ORDER[0]: " (the last launch)", NET_LOG_ORDER[1]: " (the launch before)"}.get(name, "")

        def last(prefix):
            v = [ln[len(prefix):] for ln in lines if ln.startswith(prefix)]
            return v[-1] if v else None
        started = last("[net] rollback opt-in: ")
        refused = last("[net] REFUSED: ")
        off = [ln[6:] for ln in lines if ln.endswith(": off")]
        path = last("[net] re-psn path: ") or ("direct IP" if any(ln.startswith("[net] listening/joining: ") for ln in lines) else None)
        ended = last("[net] ended: ")
        out.append(f"  {name}{label}:")
        out.append("    env decision: " + (last("[net] env decision: ") or "not in the logs"))
        out.append("    rollback started: " + (f"yes ({started.split(',')[0]})" if started is not None else "no"))
        out.append("    refused: " + (refused if refused is not None else "no") + ("" if not off else "   stopped before the start: " + off[-1]))
        out.append("    path: " + (path or "not in the logs"))
        out.append("    session synchronized: " + ("yes" if any(ln.startswith("[net] gekko session synchronized") for ln in lines) else "no"))
        out.append(f"    rollbacks: {sum(ln.startswith('[net] rollback to tick=') for ln in lines)}   desync lines: {sum(ln.startswith('[net] DESYNC at tick') for ln in lines)}")
        if ended is not None:
            out.append("    session ended: " + ended)
        if key is None:
            noisy = ("[net] commit tick=", "[net] rollback to tick=", "[net] waiting")
            kept = [ln for ln in lines if not ln.startswith(noisy)]
            key = (name, kept if len(kept) <= NET_KEY_LINES else kept[:10] + ["..."] + kept[-(NET_KEY_LINES - 10):])
    if not out:
        return ["  not in the logs (no [net] lines: rollback was off, or the game has not run since it was installed)"]
    out.append(f"  key [net] lines of {key[0]} (per-tick commit, rollback and waiting lines left out; kept here even when the log is cut):")
    out += [f"    {ln[:240]}" for ln in key[1]]
    return out


def facts(app, blobs, refused, not_text, cuts, names, net=None):
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
    exe_name = firstrun.TREE_EXE["v18"]   # jsp.exe; no payload_tree() here, a bundle must also be made from a folder whose tree.txt is odd
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
    L += display_lines(displays(), game_display(exe_name))
    L.append(f"present mode: {present_mode(blobs)}")
    net = net or {}
    L.append("Netcode (from the settings files; the server, the join address and the online names are never written here, only set / not set):")
    L += net.get("mode") or net_mode(net_settings(app))
    L.append("Netcode in the logs (read before the size cap cut anything):")
    L += net.get("logs") or net_log_lines(blobs)
    L.append("")
    L.append("files in this zip:")
    L += [f"  {n}" for n in names]
    L.append("")
    L.append("shortened: " + ("; ".join(cuts) if cuts else "nothing"))
    L.append("refused (never read): " + ("; ".join(f"{n} ({w})" for n, w in refused[:40]) if refused else "nothing"))
    L.append(f"skipped as not text (binaries, lift output, caches): {not_text} files")
    return net_scrub(firstrun.scrub("\n".join(L) + "\n"), net.get("secrets"))


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
    ns = net_settings(app, appdata)
    secrets = net_secrets(ns)
    blobs = {}
    for name, p in entries.items():
        try:
            blobs[name] = read_scrubbed(p, secrets)
        except OSError:
            pass                                                 # unreadable or gone: skipped like an absent file
    net = {"mode": net_mode(ns), "logs": net_log_lines(blobs), "secrets": secrets}   # before trim(): the [net] summary sees every line
    cuts = trim(blobs, cap - 30000)                              # room for facts.txt
    names = sorted(list(blobs) + ["facts.txt"])
    f = facts(app, blobs, refused, not_text, cuts, names, net).encode("utf-8")
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
