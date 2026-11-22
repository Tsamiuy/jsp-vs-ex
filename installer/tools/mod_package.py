#!/usr/bin/env python3
"""Install one data-only mod ZIP using private staging; never run package contents."""
import argparse
import json
import os
import pathlib
import re
import shutil
import stat
import struct
import sys
import tempfile
import unicodedata
import zipfile
from dataclasses import dataclass


class PackageError(ValueError):
    pass


@dataclass(frozen=True)
class Limits:
    entries: int = 2048
    per_file: int = 128 * 1024 * 1024
    total: int = 512 * 1024 * 1024
    archive: int = 512 * 1024 * 1024
    central: int = 2 * 1024 * 1024
    manifest: int = 1024 * 1024


RESERVED = {"con", "prn", "aux", "nul", "conin$", "conout$"} | {
    prefix + digit for prefix in ("com", "lpt") for digit in "123456789¹²³"
}


def component(value):
    if not value or value in {".", ".."} or value[-1] in ". ":
        raise PackageError("Invalid or ambiguous path component")
    if len(value.encode("utf-16-le")) > 480 or any(unicodedata.category(c).startswith("C") or c in '<>:"|?*\\' for c in value):
        raise PackageError("Unsupported path characters or component length")
    if value.split(".", 1)[0].rstrip(" ").casefold() in RESERVED:
        raise PackageError("Reserved Windows device name")
    return unicodedata.normalize("NFC", value).casefold()


def metadata(f, limits):
    """Bound real central records before ZipFile allocates an object per entry."""
    size = os.fstat(f.fileno()).st_size
    if size > limits.archive:
        raise PackageError("ZIP is larger than the package limit")
    f.seek(max(0, size - 65557))
    tail = f.read(65557)
    at = tail.rfind(b"PK\x05\x06")
    if at < 0 or len(tail) - at < 22:
        raise PackageError("ZIP has no complete directory footer")
    _, disk, start, count_disk, count, length, offset, comment = struct.unpack_from("<4s4H2LH", tail, at)
    footer = size - len(tail) + at
    if disk or start or count_disk != count or count == 65535 or length == 0xFFFFFFFF or offset == 0xFFFFFFFF:
        raise PackageError("Split and ZIP64 packages are unsupported")
    if at + 22 + comment != len(tail) or offset + length != footer or length > limits.central:
        raise PackageError("ZIP directory layout or size is unsupported")
    if not count or count > limits.entries:
        raise PackageError("ZIP entry count exceeds the package limit")
    f.seek(offset)
    end, actual = offset + length, 0
    while f.tell() < end:
        head = f.read(46)
        if len(head) != 46 or head[:4] != b"PK\x01\x02":
            raise PackageError("Invalid ZIP directory record")
        name, extra, note = struct.unpack_from("<3H", head, 28)
        step = name + extra + note
        if f.tell() + step > end:
            raise PackageError("Truncated ZIP directory record")
        f.seek(step, os.SEEK_CUR)
        actual += 1
        if actual > limits.entries:
            raise PackageError("ZIP actual entry count exceeds the package limit")
    if actual != count or f.tell() != end:
        raise PackageError("ZIP entry count does not match its directory")


def namespace(infos, limits):
    seen, nodes, files = set(), {}, []
    for info in infos:
        raw = info.orig_filename
        if raw != info.filename or "\x00" in raw or raw.startswith(("/", "\\")):
            raise PackageError("Absolute or NUL-containing ZIP path")
        parts = (raw[:-1] if info.is_dir() else raw).split("/")
        if len(parts) > 16 or len(raw) > 512:
            raise PackageError("ZIP path is too deep or long")
        keys = tuple(component(p) for p in parts)
        if keys in seen:
            raise PackageError("Duplicate or case-aliased ZIP entry")
        seen.add(keys)
        is_dir = info.is_dir()
        mode = stat.S_IFMT(info.external_attr >> 16)
        if mode not in {0, stat.S_IFREG, stat.S_IFDIR} or (mode == stat.S_IFDIR) != is_dir and mode:
            raise PackageError("ZIP links and special or inconsistent file types are unsupported")
        if info.external_attr & 0x400 or info.flag_bits & 0x41:
            raise PackageError("Reparse points and encrypted ZIP entries are unsupported")
        if info.external_attr & 0x10 and not is_dir:
            raise PackageError("Inconsistent directory attributes")
        if info.compress_type not in {zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED}:
            raise PackageError("Unsupported ZIP compression method")
        for n in range(1, len(parts) + 1):
            key, spelling = keys[:n], tuple(parts[:n])
            directory = n < len(parts) or is_dir
            old = nodes.get(key)
            if old and old != (spelling, directory):
                raise PackageError("ZIP file/directory conflict or path alias")
            nodes[key] = spelling, directory
        if is_dir:
            if info.file_size:
                raise PackageError("Directory entries must not contain data")
        else:
            if info.file_size > limits.per_file or info.file_size < 0:
                raise PackageError("ZIP member exceeds the per-file limit")
            files.append((info, tuple(parts)))
    manifests = [parts for _, parts in files if parts[-1] == "mod.json"]
    if len(manifests) != 1 or len(manifests[0]) not in {1, 2}:
        raise PackageError("Package needs one mod.json at its root or inside one top folder")
    prefix = manifests[0][:-1]
    if prefix and any(parts[:1] != prefix for _, parts in files):
        raise PackageError("Package contains files outside its mod folder")
    if sum(info.file_size for info, _ in files) > limits.total:
        raise PackageError("ZIP expanded size exceeds the package limit")
    return [(info, parts[len(prefix):]) for info, parts in files]


def regular_directory(path):
    """Reject existing links/junctions in the destination chain before creating anything."""
    for part in reversed((path, *path.parents)):
        try:
            st = part.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(st.st_mode) or getattr(st, "st_file_attributes", 0) & 0x400:
            raise PackageError("The mods destination is reached through a link or reparse point")
        if not stat.S_ISDIR(st.st_mode):
            raise PackageError("The mods destination is not a directory")


def identity(path):
    regular_directory(path)
    st = path.stat()
    return st.st_dev, st.st_ino


def publish(stage, target):
    if os.name == "nt":
        os.rename(stage, target)  # Windows rename refuses any existing destination.
    elif sys.platform.startswith("linux"):
        import ctypes

        libc = ctypes.CDLL(None, use_errno=True)
        rename = getattr(libc, "renameat2", None)
        if rename is None:
            raise PackageError("This platform cannot publish a mod without overwriting")
        rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
        rename.restype = ctypes.c_int
        if rename(-100, os.fsencode(stage), -100, os.fsencode(target), 1) != 0:  # RENAME_NOREPLACE
            error = ctypes.get_errno()
            raise OSError(error, os.strerror(error))
    else:
        raise PackageError("This platform cannot publish a mod without overwriting")


def unique_object(pairs):
    out = {}
    for k, v in pairs:
        if k in out:
            raise PackageError("Duplicate manifest property")
        out[k] = v
    return out


def reject_constant(_value):
    raise PackageError("Non-finite manifest numbers are unsupported")


DEFAULT_LIMITS = Limits()


def install_package(archive, mods, limits=DEFAULT_LIMITS):
    archive, mods = pathlib.Path(archive).absolute(), pathlib.Path(mods).absolute()
    if str(archive).startswith("\\\\") or str(mods).startswith("\\\\"):
        raise PackageError("Choose local package and mods paths")
    stage = None
    try:
        flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NONBLOCK", 0)
        with os.fdopen(os.open(archive, flags), "rb") as package:
            if not stat.S_ISREG(os.fstat(package.fileno()).st_mode):
                raise PackageError("Choose a regular ZIP file")
            metadata(package, limits)
            package.seek(0)
            with zipfile.ZipFile(package) as z:
                infos = z.infolist()
                if len(infos) > limits.entries:
                    raise PackageError("Too many ZIP entries")
                plan = namespace(infos, limits)
                regular_directory(mods)
                mods.mkdir(parents=True, exist_ok=True)
                root_identity = identity(mods)
                stage = pathlib.Path(tempfile.mkdtemp(prefix=".import-", dir=mods))
                total = 0
                for info, parts in plan:
                    if identity(mods) != root_identity:
                        raise PackageError("Mods directory changed during import")
                    dest = stage.joinpath(*parts)
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    count = 0
                    with z.open(info) as source, dest.open("xb") as out:
                        while data := source.read(65536):
                            count += len(data)
                            total += len(data)
                            if count > limits.per_file or total > limits.total:
                                raise PackageError("Actual expanded bytes exceed package limits")
                            out.write(data)
                        out.flush()
                        os.fsync(out.fileno())
                    if count != info.file_size:
                        raise PackageError("ZIP member ended at the wrong size")
                manifest_path = stage / "mod.json"
                if manifest_path.stat().st_size > limits.manifest:
                    raise PackageError("mod.json is larger than 1 MiB")
                manifest = json.loads(manifest_path.read_text(encoding="utf-8"), object_pairs_hook=unique_object, parse_constant=reject_constant)
                if not isinstance(manifest, dict) or type(manifest.get("schema")) is not int or manifest["schema"] != 1:
                    raise PackageError("Unsupported mod manifest schema")
                mod_id = manifest.get("id")
                if not isinstance(mod_id, str) or not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,39}", mod_id):
                    raise PackageError("Invalid manifest mod ID")
                key = component(mod_id)
                # Include case aliases on case-sensitive hosts, to match the Windows destination policy.
                if any(unicodedata.normalize("NFC", p.name.rstrip(" .")).casefold() == key for p in mods.iterdir()):
                    raise PackageError("A mod with this ID already exists; nothing was replaced")
                if identity(mods) != root_identity:
                    raise PackageError("Mods directory changed before publication")
                target = mods / mod_id
                publish(stage, target)
                stage = None
                return target
    except (OSError, zipfile.BadZipFile, RuntimeError, ValueError, OverflowError) as e:
        raise PackageError(str(e)) from e
    finally:
        if stage is not None:
            try:
                if identity(mods) != root_identity:
                    raise PackageError("Destination changed; private staging was left in place")
                shutil.rmtree(stage)
            except (OSError, PackageError) as e:
                raise PackageError("Private staging was left in place because cleanup was blocked: " + str(e)) from e


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", required=True)
    parser.add_argument("--mods", required=True)
    args = parser.parse_args(argv)
    try:
        target = install_package(args.archive, args.mods)
    except PackageError as e:
        print("Mod was not installed: " + str(e)[:1200], file=sys.stderr)
        return 2
    print("Installed " + target.name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
