#!/usr/bin/env python3
"""Automated checks for a mod verification issue (.github/ISSUE_TEMPLATE/mod_verification.yml).

    ISSUE_BODY=... python3 -I tools/ci/mod_verify.py --out comment.md

Reads the issue form's fields from the environment variable ISSUE_BODY, downloads the mod from its HTTPS link, checks it
and writes one markdown comment to --out. Standard library only. The mod is only ever read as data: nothing in it is
executed, imported, built or unpacked to disk (every file is hashed from the archive as a stream), and nothing is
copied anywhere. Exit code 0 when the comment was written (pass or fail is in the comment), 2 on a usage error.

The manifest rules are a Python copy of parse_manifest() in installer/port/jsp/jsp_mods_core.h (schema 1).
The canonical mod hash is specified in docs/MODS.md ("Canonical mod hash"); canonical_hash() below is that spec.
"""
import argparse
import hashlib
import http.client
import io
import ipaddress
import json
import os
import re
import socket
import stat
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile

HASH_HEADER = b"jsp-mod-hash 1\n"
MAX_DOWNLOAD = 200 * 1024 * 1024      # bytes of the download
MAX_UNPACKED = 500 * 1024 * 1024      # bytes of all files together, as read
MAX_ENTRIES = 2000                    # entries in the archive
MAX_FILE = 300 * 1024 * 1024          # one file
MAX_MANIFEST = 256 * 1024             # mod.json
MAX_REDIRECTS = 5
MAX_JSON_DEPTH = 32                   # parse_manifest: value(depth) fails above 32
READ_BLOCK = 64 * 1024                # download reads: the deadline is checked after each
TIMEOUT = 30                          # seconds for each connect or read
DEADLINE = 300                        # seconds for the whole download
MAX_LISTED = 200                      # files named in the comment
MAX_COMMENT = 60000                   # GitHub's limit is 65536

REFUSED_EXT = {".exe", ".dll", ".bat", ".cmd", ".ps1", ".py", ".sh", ".so",
               ".msi", ".com", ".scr", ".vbs", ".jar", ".dylib", ".lnk", ".pyc",
               ".asi", ".js", ".hta", ".wsf", ".cpl", ".reg", ".pif", ".vbe", ".jse", ".sys", ".ocx", ".msix", ".appx",
               ".zip", ".7z", ".rar"}   # the last three: a nested archive hides its files from the checks
SNIFF = ((b"\x7fELF", "a Linux program"), (b"#!", "a script"), (b"PK\x03\x04", "a nested zip"))
SOURCE_EXT = {".cpp", ".h", ".c", ".cc", ".hpp", ".inc", ".patch", ".diff"}
SOURCE_NAMES = {"cmakelists.txt"}
SOURCE_DIRS = {"port", "installer", "third_party", "tools"}

SUPPORTED = {"cpk-entry": 1, "roster": 1}                  # supported_interfaces()
ARCHIVE_FOLDERS = {"battle", "character", "game", "map", "progress", "sound"}   # archive_of()
COSMETIC_PREFIXES = ("game/logo/", "movie/", "sound/")     # is_cosmetic_path()
ROSTER_TITLE = "BLUS31519"
ROSTER_IDS = {19, 20, 21}
ROSTER_GRID = 56
STOCK_ROSTER = (32, 36, 28, 30, 33, 34, 41, 39, 40, 24, 38, 37, 13, 14, 31, 7, 9, 8, 4, 5, 6, 17, 18, 15, 16, 27, 25, 26,
                35, 29, 10, 12, 11, 22, 23, 0, 1, 2, 3, 206, 209, 205, 210, 212, 211, 203, 213, 204, 207, 202, 200, 201)

FORM_FIELDS = [   # (key, label) in the order of mod_verification.yml
    ("mod_id", "Mod id"),
    ("mod_name", "Mod name"),
    ("version", "Mod version"),
    ("author", "Author"),
    ("link", "Download link"),
    ("manifest", "mod.json"),
    ("class", "Kind of change"),
    ("sections", "What the mod changes"),
    ("game", "Game version and region tested"),
    ("rights", "Confirmation"),
]
RIGHTS_BOXES = 3


# ---- the issue form -------------------------------------------------------------------------------------------------------

def parse_form(body):
    """Fields of an issue-form body: '### Label' headings in the form's order. Returns {key: text}; a missing field is ''."""
    labels = {label: key for key, label in FORM_FIELDS}
    out = {key: "" for key, _ in FORM_FIELDS}
    cur, buf = None, []
    seen = set()

    def flush():
        if cur is not None:
            text = "\n".join(buf).strip()
            out[cur] = "" if text == "_No response_" else text
    for line in (body or "").replace("\r\n", "\n").replace("\r", "\n").split("\n"):
        m = re.fullmatch(r"### (.+?)\s*", line)
        if m and m.group(1) in labels and labels[m.group(1)] not in seen:
            flush()
            cur = labels[m.group(1)]
            seen.add(cur)
            buf = []
        else:
            buf.append(line)
    flush()
    out["manifest"] = strip_fence(out["manifest"])
    return out


def strip_fence(text):
    m = re.fullmatch(r"```[A-Za-z]*\n(.*?)\n?```", text.strip(), re.S)
    return m.group(1) if m else text.strip()


def rights_confirmed(text):
    boxes = re.findall(r"^\s*- \[([ xX])\] ", text, re.M)
    return len(boxes) >= RIGHTS_BOXES and all(b in "xX" for b in boxes)


# ---- the download -------------------------------------------------------------------------------------------------------

class Refused(Exception):
    pass


def check_url(url):
    """HTTPS only, no credentials, port 443, a host that resolves to public addresses only."""
    try:
        return _check_url(url)
    except Refused:
        raise
    except (ValueError, UnicodeError, OSError):   # a bad address, a name that cannot be encoded, a resolver error
        raise Refused("the link cannot be used")


def _check_url(url):
    u = urllib.parse.urlsplit(url.strip())
    if u.scheme != "https":
        raise Refused("the link is not HTTPS")
    if u.username or u.password:
        raise Refused("the link has a user name or password")
    try:
        port = u.port
    except ValueError:
        raise Refused("the link has a bad port")
    if port not in (None, 443):
        raise Refused("the link uses a port other than 443")
    host = u.hostname
    if not host:
        raise Refused("the link has no host")
    try:
        infos = socket.getaddrinfo(host, 443, proto=socket.IPPROTO_TCP)
    except (OSError, UnicodeError):
        raise Refused("the host name does not resolve")
    for info in infos:
        ip = ipaddress.ip_address(info[4][0])
        if not ip.is_global:
            raise Refused("the host resolves to a private or local address")
    return url.strip()


class _HttpsOnly(urllib.request.HTTPRedirectHandler):
    max_redirections = MAX_REDIRECTS

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        check_url(urllib.parse.urljoin(req.full_url, newurl))   # raises Refused on http:, private hosts, ...
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def fetch(url, max_bytes=MAX_DOWNLOAD, timeout=TIMEOUT, deadline=DEADLINE):
    """Download url and return its bytes. Raises Refused with a short reason. No cookies, no proxy, no credentials.
    ASSUMPTION: a host that changes its address between the check and the connection (DNS rebinding) is not caught;
    the runner holds no secret and the response is never shown, only hashed and listed."""
    check_url(url)
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), _HttpsOnly())
    req = urllib.request.Request(url, headers={"User-Agent": "jsp-mod-verify"})
    end = time.monotonic() + deadline
    try:
        with opener.open(req, timeout=timeout) as r:
            if urllib.parse.urlsplit(r.geturl()).scheme != "https":
                raise Refused("the link redirects to a non-HTTPS address")
            length = r.headers.get("Content-Length")
            if length and length.isdigit() and int(length) > max_bytes:
                raise Refused("the download is larger than %d MB" % (max_bytes >> 20))
            chunks, total = [], 0
            while True:
                block = r.read(READ_BLOCK)
                if not block:
                    break
                total += len(block)
                if total > max_bytes:
                    raise Refused("the download is larger than %d MB" % (max_bytes >> 20))
                if time.monotonic() > end:
                    raise Refused("the download took longer than %d s" % deadline)
                chunks.append(block)
            return b"".join(chunks)
    except Refused:
        raise
    except http.client.HTTPException as e:
        raise Refused("the server's answer was malformed (%s)" % type(e).__name__)
    except urllib.error.HTTPError as e:
        raise Refused("the server answered HTTP %d" % e.code)
    except (urllib.error.URLError, OSError, ValueError) as e:
        raise Refused("the download failed (%s)" % type(e).__name__)


# ---- the archive -------------------------------------------------------------------------------------------------------

def windows_device_name(part):
    base = part.split(".", 1)[0].rstrip(" ").upper()
    if base in ("CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"):
        return True
    if len(base) == 4 and base[:3] in ("COM", "LPT") and (base[3].isdigit() or base[3] in "¹²³"):
        return True
    return False


def valid_rel_path(p):
    """Python copy of valid_rel_path() in jsp_mods_core.h."""
    if not p or len(p.encode("utf-8", "surrogatepass")) > 255 or p[0] == "/" or p[-1] == "/":
        return False
    for part in p.split("/"):
        if part in ("", ".", "..") or part[-1] in ". " or windows_device_name(part):
            return False
        if any(ord(c) < 0x20 or ord(c) == 0x7F or c in '\\:<>"|?*' for c in part):
            return False
    return True


def sniff(head):
    """What the first bytes of a file say it is, or ''. MZ counts only with 'PE\\0\\0' at the offset in bytes 0x3C-0x3F
    (a PE header further than the first block is not seen)."""
    if head.startswith(b"MZ") and len(head) >= 0x40:
        off = int.from_bytes(head[0x3C:0x40], "little")
        if off + 4 <= len(head) and head[off:off + 4] == b"PE\0\0":
            return "a Windows program"
    for magic, what in SNIFF:
        if head.startswith(magic):
            return what
    return ""


def refused_type(path):
    """Why a file of this path is refused, or ''."""
    low = path.lower()
    name = low.rsplit("/", 1)[-1]
    ext = name[name.rfind("."):] if "." in name else ""
    if ext in REFUSED_EXT:
        return "executable or script (%s)" % ext
    if ext in SOURCE_EXT or name in SOURCE_NAMES:
        return "source or build file"
    if low.split("/", 1)[0] in SOURCE_DIRS and "/" in low:
        return "a path of the port's source tree"
    return ""


class Scan:
    """What the archive holds: files {path: (size, sha256 hex)}, the layout problems and the mod root."""

    def __init__(self):
        self.files = {}
        self.problems = []      # (check name, text)
        self.root = ""          # "" or "folder/"
        self.manifest_bytes = None
        self.sniffed = {}       # path -> what its first bytes say it is (a program, a script, a zip)


def scan_zip(data, max_entries=MAX_ENTRIES, max_unpacked=MAX_UNPACKED, max_file=MAX_FILE):
    """Read a zip from memory, hash each file as a stream. Never writes a file, never extracts."""
    s = Scan()
    try:
        z = zipfile.ZipFile(io.BytesIO(data))
        infos = z.infolist()
    except (zipfile.BadZipFile, OSError, ValueError, NotImplementedError):
        s.problems.append(("Archive", "not a readable zip file"))
        return s
    if len(infos) > max_entries:
        s.problems.append(("Archive", "%d entries, the limit is %d" % (len(infos), max_entries)))
        return s
    declared = sum(i.file_size for i in infos)
    if declared > max_unpacked:
        s.problems.append(("Archive", "unpacked size %d MB is over the limit of %d MB" % (declared >> 20, max_unpacked >> 20)))
        return s
    names = []
    for i in infos:
        n = i.filename
        if "\0" in i.orig_filename or "\0" in n:
            s.problems.append(("Paths", "a name with a NUL byte"))
            continue
        if not n.isascii() and not i.flag_bits & 0x800:
            s.problems.append(("Paths", "a non-ASCII name that is not marked UTF-8: %s" % n))
            continue
        isdir = n.endswith("/")
        raw = n.rstrip("/")
        mode = i.external_attr >> 16
        if stat.S_ISLNK(mode):
            s.problems.append(("Paths", "a symbolic link: %s" % n))
        if i.flag_bits & 1:
            s.problems.append(("Archive", "an encrypted entry: %s" % n))
        if n.startswith("/") or re.match(r"^[A-Za-z]:", n) or "\\" in n or n.startswith("\\"):
            s.problems.append(("Paths", "an absolute path or a backslash: %s" % n))
            continue
        if ".." in raw.split("/"):
            s.problems.append(("Paths", "a path with '..': %s" % n))
            continue
        if isdir:
            continue
        if stat.S_ISDIR(mode) or (mode and not stat.S_ISREG(mode) and mode & 0o170000):
            s.problems.append(("Paths", "not a regular file: %s" % n))
            continue
        if not valid_rel_path(raw):
            s.problems.append(("Paths", "a name that cannot be used on every system: %s" % n))
            continue
        names.append((raw, i))
    if s.problems:
        return s
    seen, lowseen = set(), {}
    for raw, _ in names:
        if raw in seen:
            s.problems.append(("Paths", "the same path twice: %s" % raw))
        seen.add(raw)
        key = raw.casefold()
        if key in lowseen and lowseen[key] != raw:
            s.problems.append(("Paths", "two paths that differ only in case: %s, %s" % (lowseen[key], raw)))
        lowseen[key] = raw
    for key, raw in lowseen.items():     # a file whose path is also a folder of another file
        parts = key.split("/")
        for k in range(1, len(parts)):
            if "/".join(parts[:k]) in lowseen:
                s.problems.append(("Paths", "a file is also a folder: %s" % lowseen["/".join(parts[:k])]))
                break
    # The mod root: the folder that holds mod.json, either the archive root or the one top-level folder.
    top = {raw.split("/", 1)[0] for raw, _ in names}
    if "mod.json" in seen:
        s.root = ""
    elif len(top) == 1 and "%s/mod.json" % next(iter(top)) in seen:
        s.root = next(iter(top)) + "/"
    else:
        s.problems.append(("Layout", "mod.json is neither at the top of the zip nor in its one top-level folder"))
        return s
    total = 0
    for raw, i in names:
        if not raw.startswith(s.root):
            s.problems.append(("Layout", "a file outside the mod folder: %s" % raw))
            continue
        rel = raw[len(s.root):]
        if i.file_size > max_file:
            s.problems.append(("Archive", "a file over %d MB: %s" % (max_file >> 20, rel)))
            continue
        h = hashlib.sha256()
        got = 0
        try:
            with z.open(i) as f:
                while True:
                    block = f.read(1 << 20)
                    if not block:
                        break
                    if got == 0:
                        what = sniff(block)
                        if what:
                            s.sniffed[rel] = what
                    got += len(block)
                    total += len(block)
                    if got > max_file or total > max_unpacked:
                        raise Refused("unpacked data is over the limit")
                    h.update(block)
                    if rel == "mod.json" and got <= MAX_MANIFEST:
                        s.manifest_bytes = (s.manifest_bytes or b"") + block
        except Refused as e:
            s.problems.append(("Archive", str(e) + " at " + rel))
            return s
        except (zipfile.BadZipFile, OSError, ValueError, NotImplementedError, EOFError):
            s.problems.append(("Archive", "an unreadable entry: %s" % rel))
            return s
        if rel == "mod.json" and got > MAX_MANIFEST:
            s.problems.append(("Manifest", "mod.json is over %d KB" % (MAX_MANIFEST >> 10)))
        s.files[rel] = (got, h.hexdigest())
    return s


# ---- the canonical mod hash ----------------------------------------------------------------------------------------------

def canonical_hash(files):
    """SHA-256 over HASH_HEADER and one line per file, sorted by the UTF-8 bytes of its relative path:
    '<64 lowercase hex of the file's SHA-256> <path>\\n'. files: {relative path: sha256 hex}. See docs/MODS.md."""
    h = hashlib.sha256()
    h.update(HASH_HEADER)
    for path in sorted(files, key=lambda p: p.encode("utf-8")):
        digest = files[path]
        if not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError("not a lowercase SHA-256: " + digest)
        h.update(digest.encode("ascii") + b" " + path.encode("utf-8") + b"\n")
    return h.hexdigest()


# ---- the manifest ------------------------------------------------------------------------------------------------------

def _archive_of(entry):
    """archive_of() in jsp_mods_core.h: the archive folder of an entry, or '' when it names none."""
    f, sep, _ = entry.partition("/")
    return f if sep and f in ARCHIVE_FOLDERS else ""


def valid_id(s):
    return isinstance(s, str) and re.fullmatch(r"[a-z0-9-]{1,40}", s) is not None


def _pairs(pairs):
    d = {}
    for k, v in pairs:
        if k in d:
            raise ValueError("the key \"%s\" appears twice" % k)
        d[k] = v
    return d


def _is_int(v):
    return isinstance(v, int) and not isinstance(v, bool)


def _surrogate_escape(text):
    """True when the raw JSON text has a \\uD800-\\uDFFF escape (the port's parser refuses them). Pairs of backslashes are skipped."""
    for m in re.finditer(r"\\(?:u([0-9a-fA-F]{4})|.)", text, re.S):
        if m.group(1) and 0xD800 <= int(m.group(1), 16) <= 0xDFFF:
            return True
    return False


def _too_deep(v, depth=0):
    """The port's parser refuses a value at depth above MAX_JSON_DEPTH (the top value is depth 0)."""
    if depth > MAX_JSON_DEPTH:
        return True
    kids = v.values() if isinstance(v, dict) else v if isinstance(v, list) else ()
    return any(_too_deep(k, depth + 1) for k in kids)


def check_manifest(text, folder_id, files):
    """Manifest schema 1, as parse_manifest() in jsp_mods_core.h. Returns (errors, info) with info =
    {id, version, title, gameplay (derived class)}. `files` is {relative path: (size, sha256)} of the archive."""
    errs = []
    if _surrogate_escape(text):
        return ["mod.json has a surrogate \\u escape, which the port does not support"], None
    try:
        j = json.loads(text, object_pairs_hook=_pairs, parse_constant=lambda c: (_ for _ in ()).throw(ValueError(c)))
    except RecursionError:
        return ["mod.json is nested deeper than %d levels" % MAX_JSON_DEPTH], None
    except ValueError as e:
        return ["mod.json is not valid JSON (%s)" % str(e)[:80]], None
    if _too_deep(j):
        return ["mod.json is nested deeper than %d levels" % MAX_JSON_DEPTH], None
    if not isinstance(j, dict):
        return ["mod.json is not an object"], None
    info = {"id": j.get("id"), "version": j.get("version"), "title": j.get("title"), "gameplay": True}
    if not _is_int(j.get("schema")):
        return ["no integer \"schema\""], info
    if j["schema"] != 1:
        return ["schema %d is not supported (the port reads schema 1)" % j["schema"]], info
    mid = j.get("id")
    if not valid_id(mid):
        errs.append("\"id\" must be [a-z0-9-], 1-40 characters")
    elif folder_id and mid != folder_id:
        errs.append("\"id\" is not the id in the form")
    ver = j.get("version")
    if not isinstance(ver, str) or not 1 <= len(ver.encode("utf-8")) <= 40:
        errs.append("\"version\" must be a string of 1-40 bytes")
    elif any(ord(c) <= 0x20 or ord(c) == 0x7F for c in ver):
        errs.append("\"version\" may not contain spaces or control characters")
    if "title" in j and not isinstance(j["title"], str):
        errs.append("\"title\" must be a string")
    title_ids = []
    if "game" in j:
        g = j["game"]
        if not isinstance(g, dict):
            errs.append("\"game\" must be an object")
        elif "title_ids" in g:
            t = g["title_ids"]
            if not isinstance(t, list):
                errs.append("\"game.title_ids\" must be an array")
            elif not all(isinstance(x, str) for x in t):
                errs.append("\"game.title_ids\" holds a non-string")
            else:
                title_ids = t
    ifaces = {}
    it = j.get("interfaces")
    if not isinstance(it, dict):
        errs.append("no \"interfaces\" object")
    else:
        for name, v in it.items():
            if not _is_int(v) or not 1 <= v <= 1000000:
                errs.append("interface \"%s\" needs a positive integer version" % name)
            elif name not in SUPPORTED:
                errs.append("interface \"%s\" is not supported by this build" % name)
            elif v > SUPPORTED[name]:
                errs.append("interface \"%s\" %d is newer than this build supports (%d)" % (name, v, SUPPORTED[name]))
            else:
                ifaces[name] = v
    gameplay_flag = False
    if "gameplay" in j:
        if not isinstance(j["gameplay"], bool):
            errs.append("\"gameplay\" must be true or false")
        else:
            gameplay_flag = j["gameplay"]
    entries = []
    ov = j.get("overrides")
    if "overrides" in j:
        if not isinstance(ov, list):
            errs.append("\"overrides\" must be an array")
            ov = []
        seen = set()
        for o in ov:
            if not isinstance(o, dict):
                errs.append("an override is not an object")
                continue
            e = o.get("entry")
            if not isinstance(e, str) or not valid_rel_path(e):
                errs.append("an override has no valid \"entry\"")
                continue
            if e.split("/", 1)[0] not in ARCHIVE_FOLDERS or "/" not in e:
                errs.append("entry %s: its first folder names no archive a mod may override" % e)
            if e in seen:
                errs.append("entry %s is overridden twice" % e)
            seen.add(e)
            entries.append(e)
            a, f = o.get("alias"), o.get("file")
            if ("alias" in o) == ("file" in o):
                errs.append("entry %s: give exactly one of \"alias\" and \"file\"" % e)
            elif "alias" in o:
                if not isinstance(a, str) or not valid_rel_path(a):
                    errs.append("entry %s: bad \"alias\"" % e)
                elif _archive_of(a) != _archive_of(e):
                    errs.append("entry %s: alias %s is in another archive (not supported)" % (e, a))
                if "sha256" in o:
                    errs.append("entry %s: an alias takes no \"sha256\"" % e)
            else:
                h = o.get("sha256")
                if not isinstance(f, str) or not valid_rel_path(f):
                    errs.append("entry %s: bad \"file\" path" % e)
                elif not isinstance(h, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", h):
                    errs.append("entry %s: \"file\" needs a 64-digit hex \"sha256\"" % e)
                elif f not in files:
                    errs.append("entry %s: the file %s is not in the mod" % (e, f))
                elif files[f][0] == 0:
                    errs.append("entry %s: the replacement file %s is empty" % (e, f))
                elif files[f][1] != h.lower():
                    errs.append("entry %s: the SHA-256 of %s does not match the file" % (e, f))
        if ov and "cpk-entry" not in ifaces:
            errs.append("overrides need interface \"cpk-entry\"")
    gr = j.get("groups")
    if "groups" in j:
        if not isinstance(gr, list):
            errs.append("\"groups\" must be an array of arrays")
        else:
            grouped = set()
            for g in gr:
                if not isinstance(g, list) or not g:
                    errs.append("a group must be a non-empty array")
                    continue
                for x in g:
                    if not isinstance(x, str):
                        errs.append("a group holds a non-string")
                    elif x not in entries:
                        errs.append("group entry %s has no override in this mod" % x)
                    elif x in grouped:
                        errs.append("entry %s is in two groups" % x)
                    else:
                        grouped.add(x)
    has_roster = "roster" in j
    if has_roster:
        errs.extend(_check_roster(j["roster"], ifaces, title_ids))
    ce = j.get("conflicts_ok")
    if "conflicts_ok" in j:
        if not isinstance(ce, list):
            errs.append("\"conflicts_ok\" must be an array of ids")
        elif not all(isinstance(x, str) for x in ce):
            errs.append("\"conflicts_ok\" holds a non-string")
    # The class, as the port derives it: gameplay unless the manifest has no roster, no gameplay flag, and every entry is cosmetic.
    info["gameplay"] = bool(has_roster or gameplay_flag or set(ifaces) - {"cpk-entry"} or
                            any(not e.startswith(COSMETIC_PREFIXES) for e in entries))
    return errs, info


def _check_roster(r, ifaces, title_ids):
    errs = []
    if "roster" not in ifaces:
        return ["\"roster\" needs interface \"roster\""]
    if ROSTER_TITLE not in title_ids:
        errs.append("a roster mod needs game.title_ids to name %s and runs only on that build" % ROSTER_TITLE)
    if not isinstance(r, dict):
        return errs + ["\"roster\" must be an object"]
    for k in r:
        if k != "fighters":
            errs.append("roster: unknown key \"%s\"" % k)
    fl = r.get("fighters")
    if not isinstance(fl, list) or not fl or len(fl) > ROSTER_GRID:
        return errs + ["roster.fighters must be a non-empty array"]
    have = set(STOCK_ROSTER)    # the stock list, then this mod's fighters in order (an earlier mod's are not known here)
    for o in fl:
        if not isinstance(o, dict):
            errs.append("a roster fighter is not an object")
            continue
        for k in o:
            if k not in ("id", "after", "logo_as", "owned"):
                errs.append("roster fighter: unknown key \"%s\"" % k)
        if not _is_int(o.get("id")) or o["id"] not in ROSTER_IDS:
            errs.append("roster fighter \"id\" must be 19, 20 or 21")
        if "id" in o and _is_int(o["id"]) and o["id"] in have:
            errs.append("roster: fighter id %d is already in the character list" % o["id"])
        if "after" in o and (not _is_int(o["after"]) or not 0 <= o["after"] <= 255):
            errs.append("roster fighter \"after\" must be an integer 0..255")
        elif "after" in o and o["after"] not in have:
            errs.append("roster: fighter goes after id %d, which is not a stock id or an earlier fighter of this mod" % o["after"])
        if "logo_as" in o and (not _is_int(o["logo_as"]) or not 0 <= o["logo_as"] < 200 or o["logo_as"] not in STOCK_ROSTER):
            errs.append("roster fighter \"logo_as\" must be a stock fighter id (on the stock list, below 200)")
        if "owned" in o and not isinstance(o["owned"], bool):
            errs.append("roster fighter \"owned\" must be true or false")
        if _is_int(o.get("id")):
            have.add(o["id"])
    return errs


# ---- the checks and the comment -------------------------------------------------------------------------------------------

def code(text, limit=120):
    """Text from the issue or the mod, safe inside a markdown table cell: a code span with no backtick, bar, line break or '@'."""
    t = "".join(c if (c.isprintable() and c not in "`|\\<>") else "?" for c in str(text))
    t = t.replace("@", "?")
    if len(t) > limit:
        t = t[:limit] + "..."
    return "`%s`" % (t or " ")


def human(n):
    return "%d B" % n if n < 1024 else "%.1f KB" % (n / 1024) if n < 1 << 20 else "%.1f MB" % (n / (1 << 20))


def run_checks(form, fetcher=fetch):
    """Returns (rows [(name, ok, detail)], files {path: (size, sha)}, hash or None, info). The download's SHA-256 is in its row."""
    rows = []

    def add(name, ok, detail=""):
        rows.append((name, ok, detail))
        return ok
    add("Form", all(form[k] for k in ("mod_id", "mod_name", "version", "author", "link", "manifest", "class", "game")) and
        valid_id(form["mod_id"]),
        "" if form["mod_id"] and valid_id(form["mod_id"]) else "a field is empty or the mod id is not [a-z0-9-], 1-40 characters")
    add("Rights", rights_confirmed(form["rights"]), "" if rights_confirmed(form["rights"]) else "all boxes must be ticked")
    try:
        data = fetcher(form["link"])
    except Refused as e:
        add("Download", False, str(e))
        return rows, {}, None, None
    add("Download", True, "%s, SHA-256 %s" % (human(len(data)), hashlib.sha256(data).hexdigest()))
    more, files, digest, info = check_zip(data, form)
    return rows + more, files, digest, info


def check_zip(data, form=None):
    """The checks on the zip's bytes. form None = local mode: nothing is compared with an issue form."""
    rows = []

    def add(name, ok, detail=""):
        rows.append((name, ok, detail))
        return ok
    s = scan_zip(data)
    by = {}
    for name, text in s.problems:
        by.setdefault(name, []).append(text)
    if form and s.root and s.root.rstrip("/") != form["mod_id"]:
        by.setdefault("Layout", []).append("the top-level folder is not named like the mod id (the port opens <id>/mod.json)")
    for name in ("Archive", "Layout", "Paths"):
        add(name, name not in by, "; ".join(by.get(name, [])[:3]) + (" (and more)" if len(by.get(name, [])) > 3 else ""))
    if any(n in by for n in ("Archive", "Layout", "Paths")) and not s.files:
        return rows, {}, None, None
    files = s.files
    bad = []
    for p in sorted(files):
        why = refused_type(p)
        if not why and p in s.sniffed:
            why = "its content is %s (a maintainer can override a content check after looking at the file)" % s.sniffed[p]
        if why:
            bad.append("%s: %s" % (p, why))
    add("File types", not bad, "; ".join(bad[:3]) + (" (and %d more)" % (len(bad) - 3) if len(bad) > 3 else ""))
    info = None
    if "mod.json" in files and s.manifest_bytes is not None and "Manifest" not in by:
        try:
            text = s.manifest_bytes.decode("utf-8-sig")
            errs, info = check_manifest(text, form["mod_id"] if form else "", files)
        except UnicodeDecodeError:
            errs, info = ["mod.json is not UTF-8"], None
        if info and not form and s.root and s.root.rstrip("/") != info["id"]:
            errs.append("the top-level folder is not named like the mod id (the port opens <id>/mod.json)")
        add("Manifest", not errs, "; ".join(errs[:4]) + (" (and %d more)" % (len(errs) - 4) if len(errs) > 4 else ""))
        if info and form:
            same = info["id"] == form["mod_id"] and info["version"] == form["version"]
            add("Form matches mod.json", same, "" if same else "the id or version differs from mod.json in the zip")
            try:
                pasted = json.loads(form["manifest"], object_pairs_hook=_pairs)
                shipped = json.loads(text, object_pairs_hook=_pairs)
                add("Pasted mod.json", pasted == shipped, "" if pasted == shipped else "the pasted mod.json differs from the one in the zip")
            except (ValueError, RecursionError):
                add("Pasted mod.json", False, "the pasted text is not valid JSON")
            claims = form["class"].lower()
            if "cosmetic" in claims and "gameplay" not in claims and info["gameplay"]:
                add("Kind of change", False, "the form says cosmetic, the manifest makes it gameplay")
            else:
                add("Kind of change", True, "gameplay" if info["gameplay"] else "cosmetic")
    else:
        add("Manifest", False, "; ".join(by.get("Manifest", ["no readable mod.json"])))
    digest = canonical_hash({p: v[1] for p, v in files.items()}) if files and "mod.json" in files else None
    return rows, files, digest, info


NOTE = "Automated checks only; a maintainer still plays it before it is added to the tested list."


def render(form, rows, files, digest):
    ok = all(r[1] for r in rows)
    out = ["## Mod check: %s" % ("all automated checks passed" if ok else "problems found"), ""]
    if form:
        out.append("Mod %s, version %s, author %s." % (code(form["mod_id"], 60), code(form["version"], 40), code(form["author"], 60)))
        out.append("Link checked: %s" % code(form["link"], 300))
        out.append("")
    out += ["| Check | Result | Detail |", "| --- | --- | --- |"]
    for name, good, detail in rows:
        out.append("| %s | %s | %s |" % (name, "pass" if good else "FAIL", code(detail, 300) if detail else ""))
    out.append("")
    if digest:
        out += ["Canonical mod hash (SHA-256, spec in docs/MODS.md): `%s`" % digest, ""]
    if files:
        total = sum(v[0] for v in files.values())
        out.append("Files (%d, %s):" % (len(files), human(total)))
        out.append("")
        paths = sorted(files, key=lambda p: p.encode("utf-8"))
        for p in paths[:MAX_LISTED]:
            out.append("- %s, %s" % (code(p, 200), human(files[p][0])))
        if len(paths) > MAX_LISTED:
            out.append("- and %d more" % (len(paths) - MAX_LISTED))
        out.append("")
    out.append(NOTE)
    text = "\n".join(out) + "\n"
    return text if len(text) <= MAX_COMMENT else text[:MAX_COMMENT - 200] + "\n\n(cut)\n\n" + NOTE + "\n"


def main(argv=None, fetcher=fetch, env=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", help="where to write the comment (default: standard output with --zip)")
    ap.add_argument("--zip", help="local mode: check this zip file, no issue and no download; prints the canonical hash and the file's SHA-256")
    a = ap.parse_args(argv)
    env = os.environ if env is None else env
    if a.zip:
        try:
            with open(a.zip, "rb") as f:
                data = f.read(MAX_DOWNLOAD + 1)
        except OSError as e:
            print("mod_verify: cannot read %s: %s" % (a.zip, e), file=sys.stderr)
            return 2
        if len(data) > MAX_DOWNLOAD:
            print("mod_verify: the file is over %d MB" % (MAX_DOWNLOAD >> 20), file=sys.stderr)
            return 2
        rows, files, digest, _ = check_zip(data)
        text = "Zip SHA-256: `%s`\n\n" % hashlib.sha256(data).hexdigest() + render(None, rows, files, digest)
    else:
        body = env.get("ISSUE_BODY")
        if body is None or not a.out:
            print("mod_verify: ISSUE_BODY and --out are needed (or use --zip FILE)", file=sys.stderr)
            return 2
        form = parse_form(body)
        try:
            rows, files, digest, _ = run_checks(form, fetcher)
        except Exception as e:   # a comment is always posted: say the checks did not finish
            rows, files, digest = [("Checks", False, "the checks stopped early (%s)" % type(e).__name__)], {}, None
        text = render(form, rows, files, digest)
        rows_failed = sum(1 for r in rows if not r[1])
        print("mod_verify: %d check(s), %d failed" % (len(rows), rows_failed))
    if a.out:
        with open(a.out, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
