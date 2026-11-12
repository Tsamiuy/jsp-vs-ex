#!/usr/bin/env python3
"""Adds the game to Steam as a non-Steam shortcut (stdlib only). Run by the installer's last page ("Add to Steam", unchecked by default).

    python steam_shortcut.py --app INSTALL_DIR [--steam STEAM_DIR] [--dry-run]

For every Steam account found (STEAM_DIR/userdata/<id>/config/shortcuts.vdf, a binary VDF) one entry is appended:
AppName "J-Stars Victory VS-EX", Exe and StartDir quoted, the launcher icon. Rules:
  - Steam running: nothing is touched (Steam rewrites the file when it exits and would drop the entry).
  - A file that does not parse as shortcuts.vdf, or whose end is not where the format says: left alone, reported.
  - A backup (shortcuts.vdf.jsp-backup-<time>) is written before every change; the old entries stay byte for byte.
  - An entry with this AppName already there: nothing is written (running twice changes nothing).
Exit code 0 = every account is done or was already done, 1 = something was refused, 2 = bad arguments or no Steam found.
Nothing is sent anywhere; no network.
"""
import argparse
import binascii
import os
import pathlib
import struct
import subprocess
import sys
import time

APP_NAME = "J-Stars Victory VS-EX"
END = 8


class VdfError(Exception):
    pass


def _cstr(b, i):
    j = b.find(b"\0", i)
    if j < 0:
        raise VdfError("unterminated string")
    return b[i:j], j + 1


def parse_map(b, i, depth=0):
    """Binary VDF map starting at b[i]: returns (items, index of the end byte, index after it). items = [(type, key, value)]."""
    if depth > 8:
        raise VdfError("nested too deep")
    items = []
    while True:
        if i >= len(b):
            raise VdfError("ended inside a map")
        t = b[i]
        if t == END:
            return items, i, i + 1
        key, i = _cstr(b, i + 1)
        if t == 0:
            val, _, i = parse_map(b, i, depth + 1)
        elif t == 1:
            val, i = _cstr(b, i)
        elif t == 2:
            if i + 4 > len(b):
                raise VdfError("short int")
            val = struct.unpack_from("<I", b, i)[0]
            i += 4
        else:
            raise VdfError("unknown type %d" % t)
        items.append((t, key, val))


def parse_shortcuts(b):
    """Returns (entries, offset of the byte that closes the shortcuts map). Raises VdfError unless the file is exactly
    root { "shortcuts" { "0" {...} "1" {...} } } with nothing after it."""
    if not b.startswith(b"\x00shortcuts\x00"):
        raise VdfError("does not start with the shortcuts map")
    items, end, after = parse_map(b, 0)  # the root map is the unnamed one: type 0 "shortcuts" ... then END
    # parse_map above treats the file as the root's item list; the root's own end byte must be the last byte
    if after != len(b) or len(items) != 1 or items[0][1] != b"shortcuts" or items[0][0] != 0:
        raise VdfError("not a single shortcuts map")
    entries = items[0][2]
    if any(t != 0 for t, _, _ in entries):
        raise VdfError("a shortcuts item is not a map")
    # the shortcuts map's end byte is just before the root's end byte
    if b[end - 1:end] != bytes([END]):
        raise VdfError("shortcuts map end not found")
    return entries, end - 1


def _field(entry, name):
    for t, k, v in entry:
        if k.lower() == name:
            return v
    return None


def make_entry(index, exe, start_dir, icon, name=APP_NAME):
    q_exe = '"%s"' % exe
    q_dir = '"%s"' % start_dir
    appid = (binascii.crc32((q_exe + name).encode("utf-8")) & 0xFFFFFFFF) | 0x80000000

    def s(k, v):
        return b"\x01" + k + b"\0" + v.encode("utf-8") + b"\0"

    def i(k, v):
        return b"\x02" + k + b"\0" + struct.pack("<I", v)
    return (b"\x00" + str(index).encode() + b"\0" + i(b"appid", appid) + s(b"AppName", name) + s(b"Exe", q_exe) +
            s(b"StartDir", q_dir) + s(b"icon", icon) + s(b"ShortcutPath", "") + s(b"LaunchOptions", "") +
            i(b"IsHidden", 0) + i(b"AllowDesktopConfig", 1) + i(b"AllowOverlay", 1) + i(b"OpenVR", 0) + i(b"Devkit", 0) +
            s(b"DevkitGameID", "") + i(b"DevkitOverrideAppID", 0) + i(b"LastPlayTime", 0) + s(b"FlatpakAppID", "") +
            b"\x00tags\x00\x08" + b"\x08")


def append_entry(data, exe, start_dir, icon):
    """New file bytes with the entry appended, or None when the AppName is already there. Raises VdfError."""
    entries, at = parse_shortcuts(data)
    for _, _, e in entries:
        if _field(e, b"appname") == APP_NAME.encode("utf-8"):
            return None
    nxt = 0
    for _, k, _ in entries:
        if k.isdigit():
            nxt = max(nxt, int(k) + 1)
    return data[:at] + make_entry(nxt, exe, start_dir, icon) + data[at:]


FRESH = b"\x00shortcuts\x00\x08\x08"


def steam_running():
    try:
        if os.name == "nt":
            r = subprocess.run(["tasklist", "/FI", "IMAGENAME eq steam.exe", "/NH"], capture_output=True, text=True, timeout=30)
            return "steam.exe" in r.stdout.lower()
        for p in pathlib.Path("/proc").glob("[0-9]*/comm"):
            try:
                if p.read_text().strip() in ("steam", "steam.exe"):
                    return True
            except OSError:
                pass
    except (OSError, subprocess.SubprocessError):
        return True   # cannot tell: refuse
    return False


def find_steam():
    if os.name == "nt":
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as k:
                p = pathlib.Path(winreg.QueryValueEx(k, "SteamPath")[0])
                if p.is_dir():
                    return p
        except (OSError, ImportError):
            pass
        for e in ("ProgramFiles(x86)", "ProgramFiles"):
            if os.environ.get(e) and (pathlib.Path(os.environ[e]) / "Steam").is_dir():
                return pathlib.Path(os.environ[e]) / "Steam"
    return None


def run(app, steam, dry=False, running=steam_running, out=print):
    """Returns the exit code. app = install folder, steam = Steam folder."""
    app = pathlib.Path(app)
    exe = app / "jsp-settings.exe"
    if not exe.is_file():
        exe = app / "jsp-launcher.exe"
    icon = app / "jsp-launcher.ico"
    users = sorted(p for p in (pathlib.Path(steam) / "userdata").glob("*") if p.name.isdigit() and p.name != "0" and p.is_dir())
    if not users:
        out("No Steam account found under %s." % (pathlib.Path(steam) / "userdata"))
        return 2
    if running():
        out("Steam is running. Close Steam completely (it rewrites its shortcut list when it exits), then run this again. Nothing was changed.")
        return 1
    rc = 0
    for u in users:
        f = u / "config" / "shortcuts.vdf"
        try:
            old = f.read_bytes() if f.exists() else FRESH
            new = append_entry(old, str(exe), str(app), str(icon) if icon.is_file() else "")
        except (VdfError, OSError) as e:
            out("Steam account %s: %s is not usable (%s). Left alone." % (u.name, f.name, e))
            rc = 1
            continue
        if new is None:
            out("Steam account %s: already has \"%s\". Nothing changed." % (u.name, APP_NAME))
            continue
        if dry:
            out("Steam account %s: would add \"%s\"." % (u.name, APP_NAME))
            continue
        try:
            f.parent.mkdir(parents=True, exist_ok=True)
            if f.exists():
                bak = f.with_name(f.name + ".jsp-backup-" + time.strftime("%Y%m%d-%H%M%S"))
                n = 1
                while bak.exists():
                    n += 1
                    bak = f.with_name("%s.jsp-backup-%s-%d" % (f.name, time.strftime("%Y%m%d-%H%M%S"), n))
                bak.write_bytes(old)
            tmp = f.with_name(f.name + ".jsp-tmp")
            tmp.write_bytes(new)
            os.replace(tmp, f)
        except OSError as e:
            out("Steam account %s: could not write (%s). Left alone." % (u.name, e))
            rc = 1
            continue
        out("Steam account %s: added \"%s\". Start Steam to see it in the library." % (u.name, APP_NAME))
    return rc


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--app", required=True)
    ap.add_argument("--steam")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args(argv)
    steam = pathlib.Path(a.steam) if a.steam else find_steam()
    if not steam or not steam.is_dir():
        print("Steam was not found for this user.")
        return 2
    return run(a.app, steam, a.dry_run)


if __name__ == "__main__":
    sys.exit(main())
