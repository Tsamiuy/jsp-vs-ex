#!/usr/bin/env python3
"""Make a support bundle: one zip of the logs and settings that a tester can attach to an issue.

Run by the payload's bundled Python (the Start-menu entry "Make a support bundle" runs support-bundle.cmd, which does):
    toolchain\\python\\python.exe -X utf8 support_bundle.py --root APP [--out DIR]
Writes jsp-support-YYYYmmdd-HHMMSS.zip to DIR (default: the Desktop, else the install folder) and prints its path.
Stdlib only. Contacts no server: the zip is only made, never sent; the tester attaches it by hand.

What goes in (text only: .log .txt .ini .cmd), every file and file name through firstrun.scrub (the user's profile folder becomes
%USERPROFILE%) and net_scrub, which puts placeholders in for:
  - the address and name fields of the known log lines ([online], [net], [psnr], [sceNp], [sceNpMatching2], [sceNpSignaling]: this
    player's and other players' online names (also with ',' or ')' in them), the server, the peer's address, community preset labels and
    unknown section names) and the whole values of the settings keys server, name, address, PSNR_SERVER, PS3_NP_ONLINE_ID and
    JSP_ROLLBACK_JOIN ('#' and ';' inside a value included), also on lines commented out with '#' or ';' (LF or CRLF line ends);
  - an ini section header line other than [repsn], [community] and [install] (preset labels, sections the user wrote); a match report
    (game/run/reports/jsp-match-report-*.txt) keeps its own headers ([end], [session], [settings], [rollbacks], [commits], [notes]);
  - wherever else they appear: every value of those keys in play_settings.txt, repsn.ini and community.ini (also earlier lines of a key and
    commented-out lines; names also as the runtime cleans them; community.ini's preset labels and active=), and the login name (USERNAME / USER and the profile folder's last part,
    as written and cleaned: the runtime makes it the online name when none is set);
  - every IPv4 address except loopback (127.x) and 0.0.0.0.
  Known limits: a name or host name that is in none of those settings files and in no known line format stays as written (for example a
  server given only on the game's command line, or an IPv6 address); the settings are read from the files the launcher uses by default,
  not from a file given to jsp-launcher.exe with --settings; a common word used as a name is replaced wherever it stands as a word.
  logs\\*.log, build\\**\\*.log (step logs), game\\run\\*.txt (play_stderr.prev.txt is the launch before the last one), the *.log, *.txt, *.ini and *.cmd files of the install folder
  (play_settings.txt, jsp-install.ini, build-again.cmd, paths.txt, VERSION.txt, tree.txt when present), the settings file in
  %APPDATA%\\JSP Fan Revival, and the newest two Inno Setup logs in %TEMP% that mention this app. Plus facts.txt.
facts.txt: version, Windows, CPU count and model, AVX2, RAM, free space, the game exe, step markers, the GPU (from the game's log) and
each display adapter's driver version (read from the registry, never changed), and each display (monitor name, size, refresh rate, which one
the game window is on when the game is running; no serial numbers), and a Netcode section: the mode the settings files choose, decided as the
game decides it (Off, Delay over re-psn, Rollback direct or over re-psn with host / join and the relay choice, or the refusal and why; server,
join address and online name only as "set" / "not set"; whether the rollback status badge JSP_ROLLBACK_BADGE is on) and, per launch, what the game's
[net] lines say (rollback started, refused and why, path, session synchronized, rollback and desync counts, the badge and the last status line: path,
input delay, last and largest rollback, the key lines), read before the size cap cuts anything.
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
import struct
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
REPORTS_KEPT = 10   # newest match reports taken from game\run\reports
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
    # ENHANCEMENT (testers): the match reports of the rollback netcode (jsp_net_game.cpp: one small text file per match that ended on a desync or a
    # failure, in "reports" under the game's working folder, jsp-match-report-<time>.txt); the newest REPORTS_KEPT
    rep = run / "reports"
    if rep.is_dir():
        reps = [p for p in rep.glob("jsp-match-report-*.txt") if p.is_file() and not p.is_symlink()]
        for p in sorted(reps, key=lambda f: f.stat().st_mtime, reverse=True)[:REPORTS_KEPT]:
            offer(f"game/run/reports/{p.name}", p)
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


def read_raw(path):
    """File text through firstrun.scrub (the profile folder) only: the netcode summary reads this, never the zip."""
    return firstrun.scrub(pathlib.Path(path).read_bytes().decode("utf-8", "replace"))


def read_scrubbed(path, secrets=None):
    """File text through firstrun.scrub (the profile folder) and net_scrub (server and join addresses, online names), as UTF-8 bytes."""
    return net_scrub(read_raw(path), secrets).encode("utf-8")


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


def read_kv(path, app=None):
    """KEY=VALUE lines of play_settings.txt exactly as jsp-launcher.exe's load_env (installer/launcher/launcher.c) turns them into the game's
    environment: a line starting with '#' or ';' or without '=' is skipped, the key is the text before the first '=' as it stands, trailing
    CR, LF and spaces leave the value, a value starting with @APP@ gets the install folder instead of the token, the last line of a key wins.
    Environment names are case-insensitive on Windows (jsp_rollback=1 is JSP_ROLLBACK=1 to the game), so the keys are returned in upper case.
    A UTF-8 BOM stays part of the first key, as load_env reads it (that line names no variable the game reads; net_secrets still sees it)."""
    out = {}
    try:
        text = pathlib.Path(path).read_bytes()[:1000000].decode("utf-8", "replace")
    except OSError:
        return out
    for line in text.split("\n"):
        k, eq, v = line.partition("=")
        if not eq or line[:1] in ("#", ";") or not k:
            continue
        v = v.rstrip("\r\n ")
        if app is not None and v.startswith("@APP@"):
            v = str(app) + v[5:]
        out[k.upper()] = v
    return out


def read_kv_all(path):
    """Every KEY=VALUE of a settings file for the secrets, also earlier lines of a key and lines commented out with '#' or ';':
    [(KEY upper, value)]. Not what the game sees (read_kv is): what must stay out of the zip."""
    out = []
    try:
        text = pathlib.Path(path).read_bytes()[:1000000].decode("utf-8-sig", "replace")
    except OSError:
        return out
    for line in text.split("\n"):
        k, eq, v = line.strip().lstrip("#;").partition("=")
        if eq and k.strip():
            out.append((k.strip().upper(), v.strip()))
    return out


def _valid_hostport(s):   # jsp_online.cpp valid_hostport: host[:port], port 1..65535, host of letters, digits, '.', '-', '_'
    host, c, port = s.partition(":")
    if c and not (port.isascii() and port.isdigit() and len(port) <= 5 and 1 <= int(port) <= 65535):
        return False
    return 0 < len(host) <= 253 and all(ch.isascii() and (ch.isalnum() or ch in ".-_") for ch in host)


def _valid_name(n):       # jsp_online.cpp valid_name: 3-16 of A-Za-z0-9_- starting with a letter
    return 3 <= len(n) <= 16 and n.isascii() and n[0].isalpha() and all(ch.isalnum() or ch in "_-" for ch in n)


# the keys jsp_online.cpp's parse() takes per section kind, with its checks (a line that fails one is rejected: the earlier value stays)
_INI_KEYS = {"repsn": {"enabled": lambda v: v in ("0", "1"), "server": _valid_hostport, "name": _valid_name},
             "community": {"enabled": lambda v: v in ("0", "1"), "active": lambda v: True},
             "preset": {"address": _valid_hostport, "name": _valid_name}}


def read_online_ini(path, into, kind, raw=None):
    """repsn.ini (kind 'repsn') / community.ini (kind 'community') into `into` ({section lower: {key lower: value}}) as jsp_online.cpp's parse()
    reads them: a UTF-8 BOM is skipped, a line over 512 bytes is rejected (the section stays), a '[' line without ']' ends the section,
    only the sections of that file kind (and preset labels it accepts), only the keys it knows with valid values (later files win per key).
    Known limit: the 64 KB file cap is applied by cutting, not by refusing the file as jsp_online.cpp does. `raw` gets every key=value of the
    file, in any section or none, valid or not, also on lines commented out with ';' or '#' ({section: {key: [values]}}: the values
    net_secrets() must keep out of the zip)."""
    try:
        text = pathlib.Path(path).read_bytes()[:65536].decode("utf-8-sig", "replace")
    except OSError:
        return False
    sec = ""
    for line in text.split("\n"):
        too_long = len(line.encode("utf-8")) > 512
        line = line.strip()
        if not line:
            continue
        if line[0] in ";#":
            k, eq, v = line.lstrip(";#").partition("=")
            if eq and raw is not None:
                raw.setdefault(sec.lower(), {}).setdefault(k.strip().lower(), []).append(v.strip())
            continue
        if too_long:
            k, eq, v = line.partition("=")
            if eq and raw is not None:
                raw.setdefault(sec.lower(), {}).setdefault(k.strip().lower(), []).append(v.strip())
            continue                                             # rejected; the section stays (jsp_online.cpp kMaxLine)
        if line.startswith("["):
            sec = line[1:-1].strip() if line.endswith("]") else ""
            continue
        k, eq, v = line.partition("=")
        if not eq:
            continue
        k, v, ls = k.strip().lower(), v.strip(), sec.lower()
        if raw is not None:
            raw.setdefault(ls, {}).setdefault(k, []).append(v)
        if not sec:
            continue
        label = sec[7:].strip() if ls.startswith("preset ") else ""
        group = ls if ls == kind else "preset" if kind == "community" and label and len(label) <= 32 and \
            all(ch.isascii() and (ch.isalnum() or ch in "_-") for ch in label) else None
        check = _INI_KEYS.get(group or "", {}).get(k)
        if check is not None and check(v):
            into.setdefault(ls if group != "preset" else "preset " + sec[7:].strip(), {})[k] = \
                (v + ":36100" if ":" not in v else v) if k in ("server", "address") else v   # with_port()
    return True


def net_settings(app, appdata=None, env=None):
    """The files the game and the launcher read for the netcode: {'play': {...}, 'ini': {section: {...}}, 'ini_raw': {...}, 'read': [where
    each was found]}. Same places as the launcher (InstalledSettingsFile, OnlineIni.SharedDir) and jsp_online.cpp (shared folder first, then
    the game's own repsn.ini). PS3RECOMP_SHARED_DIR: a play_settings.txt line wins (the launcher sets it for the game), else this process's."""
    app = pathlib.Path(app)
    env = os.environ if env is None else env
    portable = firstrun.is_portable(app)
    local = app / "play_settings.txt"
    user = pathlib.Path(appdata) / APPDIR if appdata else None
    play = local if (portable or local.is_file() or (app / "settings_here.txt").is_file() or user is None) else user / "play_settings.txt"
    kv = read_kv(play, app) if play.is_file() else {}
    sd = kv["PS3RECOMP_SHARED_DIR"] if "PS3RECOMP_SHARED_DIR" in kv else env.get("PS3RECOMP_SHARED_DIR")
    shared = pathlib.Path(sd) if sd else app / "shared" if portable else pathlib.Path(appdata) / "PS3Recomp" if appdata else None
    game_dir = app if portable or user is None else user
    s = {"play": kv, "play_raw": read_kv_all(play) if play.is_file() else [], "ini": {}, "ini_raw": {}, "read": []}
    if play.is_file():
        s["read"].append("play_settings.txt (" + ("install folder" if play == local else "user data folder") + ")")
    for d, n, kind, where in ((shared, "repsn.ini", "repsn", "shared folder"), (shared, "community.ini", "community", "shared folder"),
                              (game_dir, "repsn.ini", "repsn", "game folder")):
        if d is not None and read_online_ini(d / n, s["ini"], kind, s["ini_raw"]):
            s["read"].append(f"{n} ({where})")
    return s


def _flag(v):
    return bool(v) and v[0] != "0"   # jpno::flag_on


def _valid_port(p):       # jpno::valid_port
    return 0 < len(p) <= 5 and p.isascii() and p.isdigit() and 1 <= int(p) <= 65535


def _valid_join(s):       # jpno::valid_join: a.b.c.d:port
    ip, c, port = s.rpartition(":")
    if not c or not _valid_port(port):
        return False
    parts = ip.split(".")
    return len(parts) == 4 and all(0 < len(o) <= 3 and o.isascii() and o.isdigit() and int(o) <= 255 for o in parts)


def net_mode(s):
    """The facts.txt lines for the chosen netcode (pure). Mirrors, in this order, jsp_online.cpp's boot (which sets PS3_NET_ONLINE and
    PSNR_SERVER from the files unless PSNR_SERVER is already set), the runtime's test for the game's own online (np_psnr_enabled: PS3_NET_ONLINE
    and PSNR_SERVER both present) and jpno::resolve_env (JSP_ROLLBACK on while PS3_NET_ONLINE is present, to any value: refused, neither runs;
    then the role keys). The keys in play_settings.txt are the game's environment (the launcher passes every line on)."""
    p, ini = s["play"], s["ini"]
    rep, com = ini.get("repsn", {}), ini.get("community", {})
    rep_en, com_en = rep.get("enabled") == "1", com.get("enabled") == "1"
    env_server = bool(p.get("PSNR_SERVER"))                  # has(): set and not empty
    files_online = False
    if env_server:
        why = "PSNR_SERVER is set in play_settings.txt: it wins and the settings files are not used"
    elif rep_en and com_en:
        why = "re-psn and community both enabled: the game refuses its own online"
    elif com_en:
        why = "community server enabled: no client yet, the game stays offline"
    elif rep_en and rep.get("server"):
        why, files_online = "re-psn", True                   # jsp_online.cpp sets PS3_NET_ONLINE=1 and PSNR_SERVER
    elif rep_en:
        why = "re-psn enabled but no valid server set: the game stays offline"
    else:
        why = ""
    net_online = "PS3_NET_ONLINE" in p or files_online      # present, whatever the value (the runtime's and resolve_env's test)
    if net_online and (env_server or files_online):
        delay = "Delay (re-psn" + (", server from PSNR_SERVER in play_settings.txt" if env_server else "") + ")"
    elif net_online:
        delay = "Off: PS3_NET_ONLINE is set in play_settings.txt but no server is (" + (why or "no re-psn server") + ")"
    elif env_server:
        delay = "Off: PSNR_SERVER is set in play_settings.txt without PS3_NET_ONLINE (the game's own online needs both; the settings files are not used)"
    else:
        delay = "Off" + (f" ({why})" if why else "")
    if _flag(p.get("JSP_ROLLBACK", "")):
        listen, join = p.get("JSP_ROLLBACK_LISTEN", ""), p.get("JSP_ROLLBACK_JOIN", "")
        role = p.get("JSP_ROLLBACK_ROLE", "").lower()
        role_key = any(p.get(k) for k in ("JSP_ROLLBACK_LISTEN", "JSP_ROLLBACK_JOIN", "JSP_ROLLBACK_VIA", "JSP_ROLLBACK_BENCH"))   # jpno::role_keys
        if not role_key and net_online:   # ENHANCEMENT (native-room pairing): the lobby start next to the game's own online (jpno NC_NATIVE)
            mode = ("Online Battle + rollback (the game's Friendly Match room pairs the players; rollback runs the fight), relay allowed: "
                    + ("yes" if _flag(p.get("JSP_ROLLBACK_RELAY", "")) else "no")
                    + (", room pairing off (JSP_ROLLBACK_NATIVE=0)" if p.get("JSP_ROLLBACK_NATIVE", "") and not _flag(p.get("JSP_ROLLBACK_NATIVE", "")) else ""))
        elif not role_key:
            mode = "Rollback lobby: no session at start (the game's own online is off, so no Online Battle room to pair with)"
        elif net_online:
            mode = "Rollback refused: PS3_NET_ONLINE is set too (the game's own online) with a rollback role key, so neither runs"
        elif p.get("JSP_ROLLBACK_VIA", "").lower() == "repsn":
            if role not in ("host", "join"):
                mode = "Rollback re-psn refused: JSP_ROLLBACK_ROLE is not host or join"
            elif listen and not _valid_port(listen):
                mode = "Rollback re-psn refused: JSP_ROLLBACK_LISTEN is not a port from 1 to 65535"
            else:
                mode = f"Rollback re-psn {role}, relay allowed: " + ("yes" if _flag(p.get("JSP_ROLLBACK_RELAY", "")) else "no")
        elif bool(listen) == bool(join):
            mode = "Rollback direct refused: not exactly one of JSP_ROLLBACK_LISTEN (host) / JSP_ROLLBACK_JOIN (join)"
        elif listen and not _valid_port(listen):
            mode = "Rollback direct refused: JSP_ROLLBACK_LISTEN is not a port from 1 to 65535"
        elif join and not _valid_join(join):
            mode = "Rollback direct refused: JSP_ROLLBACK_JOIN is not IP:PORT"
        else:
            mode = "Rollback direct " + ("host" if listen else "join")
    else:
        mode = delay
    server = bool(rep.get("server") or p.get("PSNR_SERVER"))
    name = bool(rep.get("name") or p.get("PS3_NP_ONLINE_ID"))
    st = lambda b: "set" if b else "not set"
    return [f"  mode: {mode}",
            f"  re-psn server: {st(server)}   online name: {st(name)}   rollback join address: {st(p.get('JSP_ROLLBACK_JOIN'))}",
            "  rollback status badge (JSP_ROLLBACK_BADGE): " + ("on" if _flag(p.get("JSP_ROLLBACK_BADGE", "")) else "off"),
            "  settings files read: " + (", ".join(s["read"]) if s["read"] else "none found")]


def _clean_online_id(v):   # np_psnr.c clean_online_id: the allowed characters, at most 16
    return "".join(ch for ch in v if ch.isascii() and (ch.isalnum() or ch in "-_"))[:16]


def net_secrets(s, env=None):
    """{value: placeholder} of the private values: server and join addresses (whole and host part) and online names from the settings files,
    and the login name (USERNAME / USER and the profile folder's last part, as written and as np_psnr.c cleans it: the runtime uses it as the
    online name when none is set)."""
    env = os.environ if env is None else env
    pairs = [(k.lstrip("\ufeff").upper(), v) for k, v in s["play"].items()] + list(s.get("play_raw", []))   # a BOM hides a key from the game, not from us
    ini = s.get("ini_raw", s["ini"])
    out = {}

    def add(v, what, address=False):
        v = (v or "").strip()
        if len(v) >= 3:
            out.setdefault(v, what)
            if address:
                host = v.rsplit(":", 1)[0] if v.count(":") == 1 else v
                if len(host) >= 3:
                    out.setdefault(host, what)
    def name(v):
        add(v, "<online name>")
        add(_clean_online_id(v or ""), "<online name>")   # as np_psnr.c cleans a name before it uses it
    for k, v in pairs:
        if k == "JSP_ROLLBACK_JOIN":
            add(v, "<join address>", True)
        elif k == "PSNR_SERVER":
            add(v, "<server>", True)
        elif k == "PS3_NP_ONLINE_ID":
            name(v)
    for sec, kv in ini.items():
        vals = lambda key: kv.get(key) if isinstance(kv.get(key), list) else [kv.get(key)]
        for v in vals("server") + vals("address"):
            add(v, "<server>", True)
        for v in vals("name"):
            name(v)
        for v in vals("active"):
            add(v, "<preset>")
        if sec.startswith("preset "):
            add(sec[7:].strip(), "<preset>")
    prof = env.get("USERPROFILE") or ""
    for v in (env.get("USERNAME"), env.get("USER"), re.split(r"[\\/]", prof.rstrip("\\/"))[-1] if prof else None):
        add(v, "<user name>")
        add(_clean_online_id(v or ""), "<user name>")
    return out


_IPV4 = re.compile(r"(?<![\d.])((?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(?:\.(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3})(?![\d]|\.\d)")
_NET_RULES = (   # always applied, also when the settings files were not found
    (re.compile(r"(?im)^(\s*[#;]?\s*(?:server|name|address|JSP_ROLLBACK_JOIN|PSNR_SERVER|PS3_NP_ONLINE_ID)\s*=[ \t]*)\S[^\r\n]*"), r"\1<set>"),   # the whole value: '#' or ';' inside it is part of it
    # an ini section header of its own line that is not one of the program's (a preset label; an unknown section: _SECTION_HEADER below)
    (re.compile(r"(?im)^(\s*\[\s*preset\s+)[^\]\r\n]*(\]\s*)(?=\r?$)"), r"\1<preset>\2"),
    (re.compile(r"(\[online\] community method enabled \(preset )[^\r\n]*?(\): not supported yet)"), r"\1<preset>\2"),
    (re.compile(r"(rejected \(unknown section \[)[^\r\n]*(\]\))"), r"\1<set>\2"),
    (re.compile(r"(\[online\] re-psn enabled: server )\S+"), r"\1<server>"),
    (re.compile(r"(\[online\] online id )\S+"), r"\1<online name>"),
    (re.compile(r"(\[online\] --psnr )\S+"), r"\1<server>"),
    # jsp_net_game.cpp: the name may hold anything up to the fixed tail; without the tail the rest of the line is the name
    (re.compile(r"(?m)(\[net\] re-psn: (?:creating|joining) a room on )\S+( as )[^\r\n]*?( \(relay (?:allowed|not allowed)\), local UDP port \d+)(?=\r?$)"),
     r"\1<server>\2<online name>\3"),
    (re.compile(r"(\[net\] re-psn: (?:creating|joining) a room on )(?!<server> as <online name> \(relay)\S+( as )[^\r\n]*"), r"\1<server>\2<online name>"),
    (re.compile(r"(the online name ')[^\r\n]*(' is longer than 16 characters)"), r"\1<online name>\2"),
    (re.compile(r"(\[net\] rendezvous: )\S+"), r"\1<peer address>"),
    # the toolkit's NP lines (libs/network): this player's and other players' online names, the server
    (re.compile(r"(\[psnr\] player )\"[^\"\r\n]*\""), r"\1<online name>"),
    (re.compile(r"(\[psnr\] )\"[^\r\n]*\"( is not a valid online ID)"), r"\1<online name>\2"),
    (re.compile(r"(\[psnr\] connected to )\S+?(:\d+ as )\"[^\r\n]*\"( \()"), r"\1<server>\2<online name>\3"),
    (re.compile(r"(\[psnr\] )\S+?(:\d+: another player there is already )\"[^\r\n]*\"( -- pick another)"), r"\1<server>\2<online name>\3"),
    (re.compile(r"(\[psnr\] could not reach )\S+(:\d+ -- )"), r"\1<server>\2"),
    (re.compile(r"(\[sceNp\] (?:GetNpId|GetOnlineId|GetOnlineName|GetUserProfile)\(user=-?\d+\) -> )\"[^\"\r\n]*\""), r"\1<online name>"),
    (re.compile(r"(\[sceNp\] Init\(poolSize=\d+, username=)\"[^\"\r\n]*\""), r"\1<online name>"),
    # (from toolkit patch 6019 on, "room N: member M left" has no name: an online ID has no space, so "member M" is never one)
    (re.compile(r"(?m)(\[sceNpMatching2\] room \d+: )(?!member \d+ left\r?$)[^\r\n]*?( joined as member \d+| left)(?=\r?$)"), r"\1<online name>\2"),
    # a peer's name may hold ',' or ')': each rule runs up to the fixed tail of its format
    (re.compile(r"(\[sceNpMatching2\] signaling: member \d+ \()[^\r\n]*(\) at \d+\.\d+\.\d+\.\d+:\d+)"), r"\1<online name>\2"),
    (re.compile(r"(\[sceNpSignaling\] conn \d+ \()[^\r\n]*(, \d+\.\d+\.\d+\.\d+:\d+\) (?:established|dead))"), r"\1<online name>\2"),
    (re.compile(r"(\[sceNpSignaling\] ActivateConnection\(ctx=\d+, )[^\r\n]*(\) -> conn \d+ \((?:active|pending)\))"), r"\1<online name>\2"),
)
# An ini section header of its own line that is not one of the program's (an unknown section the user wrote) becomes [<set>].
# FIX (testers): net_scrub() leaves the header names in `sections` alone: a match report's own sections (REPORT_SECTIONS), which this rule
# turned into [<set>] in every bundle. In any other file (an ini) the same header is still scrubbed.
_SECTION_HEADER = re.compile(r"(?im)^(\s*\[)(?!\s*(?:repsn|community|install|preset\s[^\]\r\n]*)\s*\])([^\]\r\n]*)(\]\s*)(?=\r?$)")
# The headers jsp_net_report.h render() writes in a match report (jsp-match-report-*.txt), and its "[truncated]" line: program words only.
REPORT_SECTIONS = ("end", "session", "settings", "rollbacks", "commits", "notes", "truncated")
_MATCH_REPORT = re.compile(r"(?:^|/)game/run/reports/jsp-match-report-[^/]*\.txt$")


def is_match_report(name):
    """True for a match report's path in the bundle (game/run/reports/jsp-match-report-*.txt, as collect() offers them)."""
    return _MATCH_REPORT.search(name.replace("\\", "/")) is not None


# The program's own log tags and ini section names at the start of a line ("[net]", "[online]", "[repsn]"): never a private value, so a
# name that happens to read "net" or "online" does not turn every tag into a placeholder. Only these: any other bracketed word is scrubbed.
_LINE_TAG = (r"(?m:^\s*\[(?:net|online|psnr|repsn|community|install|portmenu|version|jstars-game|support_bundle|D3D12|"
             r"sceNp[A-Za-z0-9]*|cell[A-Z][A-Za-z0-9]*)\])")


_PLACEHOLDER = r"<(?:set|server|online name|join address|user name|peer address|IPv4|preset)>"


def net_scrub(text, secrets=None, sections=()):
    """Takes the private netcode values out of a file's text: first the address and name fields of the known log lines and settings keys
    (so a name that is also a word of those lines, "member" or "room", cannot break them) and every section header line that is not the
    program's (_SECTION_HEADER) except the names in `sections` (REPORT_SECTIONS for a match report), then the values in `secrets`
    (net_secrets) wherever they stand except inside the program's own line tags, the kept section headers and the placeholders, then every
    IPv4 address except loopback (127.x) and 0.0.0.0 (a port after it stays)."""
    for pat, rep in _NET_RULES:
        text = pat.sub(rep, text)
    kept = {x.lower() for x in sections}
    text = _SECTION_HEADER.sub(lambda m: m.group(0) if m.group(2).strip().lower() in kept else m.group(1) + "<set>" + m.group(3), text)
    tags = _LINE_TAG
    if kept:   # a kept header line is a program word: a secret that reads "notes" or "session" must not change it
        tags += r"|(?im:^[ \t]*\[(?:" + "|".join(re.escape(x) for x in sorted(kept)) + r")\](?=[ \t]*\r?$))"
    keep = re.compile(tags + "|" + _PLACEHOLDER)
    for v in sorted(secrets or {}, key=len, reverse=True):
        b1 = "(?<![A-Za-z0-9_])" if (v[:1].isalnum() or v[:1] == "_") else ""
        b2 = "(?![A-Za-z0-9_])" if (v[-1:].isalnum() or v[-1:] == "_") else ""
        pat = re.compile(tags + "|" + _PLACEHOLDER + "|" + b1 + re.escape(v) + b2, re.IGNORECASE)
        text = pat.sub(lambda m, r=secrets[v]: m.group(0) if keep.fullmatch(m.group(0).strip()) or keep.fullmatch(m.group(0)) else r, text)
    return _IPV4.sub(lambda m: m.group(1) if m.group(1).startswith("127.") or m.group(1) == "0.0.0.0" else "<IPv4>", text)


# The [net] lines of jsp_net_game.cpp, by their format strings. PUBLIC: every field is a program word or a number, so the line is printed as the
# game wrote it (a secret is never matched against it: an online name "host" or "relay" must not change "rollback opt-in: host"). PRIVATE:
# lines with an address or a name; the field is replaced by its placeholder. Any other [net] line: net_scrub() of the text after the tag.
_RESOLVE_WHY = (   # jpno::resolve_env's refusal reasons (jsp_net_online.h)
    "JSP_ROLLBACK=1 and the game's own online (re-psn or a community server) are both on; only one multiplayer method may be active, neither runs",
    "JSP_ROLLBACK=1 with a role key (LISTEN, JOIN, VIA or BENCH) and the game's own online (re-psn or a community server) are both on; only one multiplayer method may be active, neither runs",
    "JSP_ROLLBACK_VIA=repsn needs JSP_ROLLBACK_ROLE=host or join: rollback off",
    "JSP_ROLLBACK_LISTEN is not a port from 1 to 65535: rollback off",
    "JSP_ROLLBACK_VIA=repsn: the re-psn transport is not wired in this build: rollback off",
    "JSP_ROLLBACK=1 needs exactly one of JSP_ROLLBACK_LISTEN=PORT (host) or JSP_ROLLBACK_JOIN=IP:PORT (client): rollback off",
    "JSP_ROLLBACK_JOIN is not IP:PORT: rollback off",
)
# jsp_net_badge.h log_line: path, input delay, last and largest rollback in ticks ("-" when not known yet), rollback count.
_NET_STATUS = r"status: path (?:direct|relay|-), delay (?:\d+f|-), last rollback (?:\d+f|-), largest (?:\d+f|-), rollbacks \d+"
_NET_PUBLIC = re.compile("|".join([
    r"env decision: (?:rollback start|REFUSED|rollback not requested) \(static constructors arrived: (?:EnvInit|online boot) first, (?:EnvInit|online boot) last\)",
    "REFUSED: (?:" + "|".join(re.escape(w) for w in _RESOLVE_WHY) + ")",
    r"rollback opt-in: (?:host|client), build [0-9a-f]{8}, hooks installed",
    r"re-psn path: (?:direct|relay|punching|-)",
    r"handshake state -> -?\d+ \((?:host|client)\)",
    r"commit tick=\d+ hash=[0-9a-f]{8} raw=[0-9a-f]{8}",
    r"rollback to tick=\d+ \(longest game-thread stretch without a session call since the last rollback: \d+ ms, keepalive polls so far: \d+\)",
    r"DESYNC at tick \d+ \(stepper tick numbering, session-relative; add the base \d+\): local=[0-9a-f]{8} remote=[0-9a-f]{8}",
    r"gekko session synchronized",
    r"session started: base tick \d+ slots \d+ of \d+",
    r"phase: armed \(settings agreed, waiting for the first fight tick\)",
    r"startup evidence available: starting the rendezvous",
    r"re-psn: no server / online name in repsn\.ini \(set them on the Online page under Delay\): off",
    r"JSP_ROLLBACK_JOIN is not IP:PORT: off",
    r"listening/joining: host, local UDP port \d+",
    r"ended: (?:Opponent disconnected|Match ended|Desync detected: match ended)",
    r"status badge: (?:on|off)",
    _NET_STATUS,
]))
_NET_PRIVATE = (
    (re.compile(r"(re-psn: (?:creating|joining) a room on )\S+( as ).*?( \(relay (?:allowed|not allowed)\), local UDP port \d+)"), r"\1<server>\2<online name>\3"),
    (re.compile(r"(re-psn: the online name ').*(' is longer than 16 characters \(repsn\.ini name=\): off)"), r"\1<online name>\2"),
    (re.compile(r"(listening/joining: )(?!host,).*(, local UDP port \d+)"), r"\1<join address>\2"),
    (re.compile(r"(rendezvous: )\S+((?: via re-psn, path (?:direct|relay|punching|-), room \d+)?)"), r"\1<peer address>\2"),
)


def net_line_safe(line, secrets=None):
    """One raw '[net] ...' line as facts.txt may print it (see _NET_PUBLIC / _NET_PRIVATE)."""
    rest = line[6:]
    if _NET_PUBLIC.fullmatch(rest):
        return line
    for pat, rep in _NET_PRIVATE:
        if pat.fullmatch(rest):
            return "[net] " + pat.sub(rep, rest)
    return net_scrub(line, secrets)   # the whole line: the [net] rules need the tag (net_scrub keeps it)


def net_log_lines(blobs, secrets=None, shown=None):
    """The facts.txt lines for what the game's [net] lines say (pure). `blobs`: the logs' RAW text (str or bytes; only firstrun.scrub applied),
    before the size cap and before net_scrub: the summary is computed from every line as the game wrote it, and each line passes
    net_line_safe() before anything is read from it, so no server, join address or online name is printed."""
    names = [n for n in NET_LOG_ORDER if n in blobs] + sorted(n for n in blobs if n not in NET_LOG_ORDER)
    out, key = [], None
    for name in names:
        b = blobs[name]
        text = b.decode("utf-8", "replace") if isinstance(b, bytes) else b
        lines = [net_line_safe(ln.strip(), secrets) for ln in text.splitlines() if ln.lstrip().startswith("[net] ")]
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
        out.append(f"  {(shown or {}).get(name, name)}{label}:")
        out.append("    env decision: " + (last("[net] env decision: ") or "not in the logs"))
        out.append("    rollback started: " + (f"yes ({started.split(',')[0]})" if started is not None else "no"))
        out.append("    refused: " + (refused if refused is not None else "no") + ("" if not off else "   stopped before the start: " + off[-1]))
        out.append("    path: " + (path or "not in the logs"))
        out.append("    session synchronized: " + ("yes" if any(ln.startswith("[net] gekko session synchronized") for ln in lines) else "no"))
        out.append(f"    rollbacks: {sum(ln.startswith('[net] rollback to tick=') for ln in lines)}   desync lines: {sum(ln.startswith('[net] DESYNC at tick') for ln in lines)}")
        status = [ln[len("[net] status: "):] for ln in lines if re.fullmatch(_NET_STATUS, ln[6:])]   # only the exact program line (a scrubbed one never matches)
        badge = last("[net] status badge: ")
        out.append("    last status line: " + (status[-1] if status else "not in the logs") + "   status badge: " + (badge or "not in the logs"))
        if ended is not None:
            out.append("    session ended: " + ended)
        if key is None:
            noisy = ("[net] commit tick=", "[net] rollback to tick=", "[net] waiting", "[net] status: ")
            kept = [ln for ln in lines if not ln.startswith(noisy)]
            key = ((shown or {}).get(name, name), kept if len(kept) <= NET_KEY_LINES else kept[:10] + ["..."] + kept[-(NET_KEY_LINES - 10):])
    if not out:
        return ["  not in the logs (no [net] lines: rollback was off, or the game has not run since it was installed)"]
    out.append(f"  key [net] lines of {key[0]} (per-tick commit, rollback, waiting and status lines left out; kept here even when the log is cut):")
    out += [f"    {ln[:240]}" for ln in key[1]]
    return out


CACHE_NAMES = ("pipelines.bin", "shader_cache.bin", "pso_cache.bin")   # beside the game's working folder; patches 2038, 2068, 2069


def _walk_cache(f, size, pos, head_fmt, head_len, magic, body_len):
    """Records of a cache file by their small heads only (the bodies are seeked over, so a multi-gigabyte file costs one tiny read
    per record). -> (records, distinct keys, repeated keys, pos after the last whole record). `body_len(fields)` -> bytes after the head."""
    n, keys = 0, set()
    while size - pos >= head_len:
        f.seek(pos)
        raw = f.read(head_len)
        if len(raw) < head_len:
            break
        fields = struct.unpack(head_fmt, raw)
        if fields[0] != magic:
            break
        end = pos + body_len(fields)
        if end > size:
            break
        n += 1
        keys.add(fields[-1])
        pos = end
    return n, len(keys), n - len(keys), pos


def cache_line(path):
    """One facts line for a cache file: its size and its records. Never raises (a file that cannot be read says so)."""
    name = pathlib.Path(path).name
    try:
        size = pathlib.Path(path).stat().st_size
        with open(path, "rb") as f:
            if name == "pipelines.bin":
                head = f.read(24)
                pos, hdr, shown = 0, None, "no file head (written by an older build)"
                if len(head) == 24 and struct.unpack("<I", head[:4])[0] == 0x48575350:   # 'PSWH'
                    _m, fmt, stamp, hdr, _r = struct.unpack("<IIQII", head)
                    pos, shown = 24, f"file head format {fmt}, record head {hdr} bytes"
                else:
                    # an older file has no head: the record head size is where the next record's magic is
                    f.seek(0)
                    first = f.read(24)
                    if len(first) == 24 and struct.unpack("<I", first[:4])[0] == 0x32575350:   # 'PSW2'
                        _m, vs, ps, _il, _k = struct.unpack("<IIIIQ", first)
                        for h in range(24, 4096, 8):
                            at = h + vs + ps
                            if at + 4 <= size:
                                f.seek(at)
                                if f.read(4) == b"PSW2":
                                    hdr = h
                                    break
                if not hdr:
                    return f"{name}: {size} bytes, {shown}, records not counted"
                n, d, rep, end = _walk_cache(f, size, pos, "<IIIIQ", 24,
                                             0x32575350, lambda t: hdr + t[1] + t[2])
                if pos == 0 and n == 0:
                    return f"{name}: {size} bytes, {shown}, records not counted"
                return f"{name}: {size} bytes, {shown}, {n} records ({d} distinct, {rep} repeated), {size - end} bytes after the last whole record"
            n, d, rep, end = _walk_cache(f, size, 0, "<IIQ", 16, 0x31435344, lambda t: 16 + t[1])   # 'DSC1': magic, size, key
            return f"{name}: {size} bytes, {n} records ({d} distinct, {rep} repeated), {size - end} bytes after the last whole record"
    except (OSError, struct.error) as e:
        return f"{name}: cannot be read ({type(e).__name__})"


def cache_lines(app):
    """The pipeline and shader cache files that exist in the game's working folder (game/run, or game): size and record counts.
    A cache that passed 2 GB once stopped loading, so its size is what a report needs."""
    out = []
    for folder in (pathlib.Path(app) / "game" / "run", pathlib.Path(app) / "game"):
        for n in CACHE_NAMES:
            p = folder / n
            if p.is_file() and not p.is_symlink():
                out.append("cache " + cache_line(p))
    return out or ["cache files (pipelines.bin, shader_cache.bin, pso_cache.bin): none found"]


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
    L += cache_lines(app)
    net = net or {}
    head = net_scrub(firstrun.scrub("\n".join(L) + "\n"), net.get("secrets"))
    # The Netcode section is made safe line by line (net_mode prints program words only, net_log_lines passes every [net] line through
    # net_line_safe): the secrets are not matched against it again, so an online name such as "host" or "relay" cannot change its words.
    N = ["Netcode (from the settings files; the server, the join address and the online names are never written here, only set / not set):"]
    N += net.get("mode") or net_mode(net_settings(app))
    N.append("Netcode in the logs (read before the size cap cut anything):")
    N += net.get("logs") or net_log_lines(blobs, net.get("secrets"))
    L = ["", ""]
    L.append("files in this zip:")
    L += [f"  {n}" for n in names]
    L.append("")
    L.append("shortened: " + ("; ".join(cuts) if cuts else "nothing"))
    L.append("refused (never read): " + ("; ".join(f"{n} ({w})" for n, w in refused[:40]) if refused else "nothing"))
    L.append(f"skipped as not text (binaries, lift output, caches): {not_text} files")
    return head + firstrun.scrub("\n".join(N)) + net_scrub(firstrun.scrub("\n".join(L) + "\n"), net.get("secrets"))


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


_FIXED_PARTS = {"game", "run", "logs", "build", "appdata", "setup-logs"}


def zip_name(name, secrets=None):
    """A file's name in the zip without private values (a user file such as "Bobby-p2.txt"): each part but the bundle's own folders through
    firstrun.scrub and net_scrub, with the placeholders' angle brackets made legal in a Windows file name."""
    parts = name.split("/")
    out = [x if x in _FIXED_PARTS else net_scrub(firstrun.scrub(x), secrets).replace("<", "(").replace(">", ")") for x in parts]
    return "/".join(out)


def make(app, out=None, appdata=None, temp=None, cap=CAP_BYTES):
    """Builds the zip; -> its path. out None: the Desktop, else app."""
    app = pathlib.Path(app)
    entries, refused, not_text = collect(app, appdata, temp)
    ns = net_settings(app, appdata)
    secrets = net_secrets(ns)
    blobs = {}
    net_logs, shown = None, {}
    for name0, p in entries.items():
        try:
            raw = read_raw(p)
        except OSError:
            continue                                             # unreadable or gone: skipped like an absent file
        name = zip_name(name0, secrets)
        while name in blobs:                                     # two names that scrub to the same one
            name = name.rsplit(".", 1)[0] + "-1." + name.rsplit(".", 1)[-1] if "." in name else name + "-1"
        if "[net] " in raw:
            net_logs = net_logs or {}
            net_logs[name0] = raw                                 # the [net] summary reads the raw text (net_scrub could change its words)
        shown[name0] = name
        blobs[name] = net_scrub(raw, secrets, REPORT_SECTIONS if is_match_report(name0) else ()).encode("utf-8")   # FIX: a report keeps its headers
    net = {"mode": net_mode(ns), "logs": net_log_lines(net_logs or {}, secrets, shown), "secrets": secrets}   # before trim(): the summary sees every line
    net_logs = None
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
