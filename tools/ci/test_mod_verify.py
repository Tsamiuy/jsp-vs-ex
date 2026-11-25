#!/usr/bin/env python3
"""Host test for tools/ci/mod_verify.py and tools/ci/check_tested_mods.py. Local fixtures only; no network (the downloader is injected).

Run: python3 -I tools/ci/test_mod_verify.py
"""
import hashlib
import tempfile
import warnings
import importlib.util
import io
import json
import pathlib
import socket
import struct
import sys
import zipfile

HERE = pathlib.Path(__file__).resolve().parent


def load(name):
    spec = importlib.util.spec_from_file_location(name, HERE / (name + ".py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


mv = load("mod_verify")
tm = load("check_tested_mods")
failed = 0
warnings.simplefilter("ignore", UserWarning)   # the duplicate-name fixture


def check(name, cond, extra=""):
    global failed
    if not cond:
        failed += 1
        print("FAIL: %s %s" % (name, extra))


def make_zip(entries, order=None, prefix="", symlink=None):
    """entries {path: bytes}; returns zip bytes."""
    b = io.BytesIO()
    with zipfile.ZipFile(b, "w", zipfile.ZIP_DEFLATED) as z:
        for p in (order or list(entries)):
            z.writestr(prefix + p, entries[p])
        for p in (symlink or []):
            zi = zipfile.ZipInfo(prefix + p)
            zi.create_system = 3
            zi.external_attr = (0o120777 << 16)
            z.writestr(zi, b"target")
    return b.getvalue()


def raw_zip(name, data=b"x"):
    """A zip whose entry has the exact (unsafe) name; writestr would keep it, ZipInfo does not sanitise here."""
    b = io.BytesIO()
    with zipfile.ZipFile(b, "w") as z:
        z.writestr(zipfile.ZipInfo(name), data)
    return b.getvalue()


def manifest(**kw):
    m = {"schema": 1, "id": "tiny-logo", "version": "1.0", "title": "Tiny logo",
         "game": {"title_ids": ["BLUS31519"]}, "interfaces": {"cpk-entry": 1}, "overrides": []}
    m.update(kw)
    return m


def sha(b):
    return hashlib.sha256(b).hexdigest()


def body(mid="tiny-logo", link="https://example.invalid/m.zip", man=None, cls="Cosmetic", rights=True):
    man = man if man is not None else manifest()
    ticks = "x" if rights else " "
    return ("### Mod id\n\n%s\n\n### Mod name\n\nTiny logo\n\n### Mod version\n\n1.0\n\n### Author\n\nsomeone\n\n"
            "### Download link\n\n%s\n\n### mod.json\n\n```json\n%s\n```\n\n### Kind of change\n\n%s\n\n"
            "### What the mod changes\n\nthe intro logo\n\n### Game version and region tested\n\nUS BLUS31519\n\n"
            "### Confirmation\n\n- [%s] no game files\n- [%s] port source not edited\n- [%s] own work\n"
            % (mid, link, json.dumps(man, indent=2), cls, ticks, ticks, ticks))


# ---- form parsing
f = mv.parse_form(body())
check("form id", f["mod_id"] == "tiny-logo")
check("form link", f["link"] == "https://example.invalid/m.zip")
check("form manifest fence stripped", json.loads(f["manifest"])["id"] == "tiny-logo")
check("form crlf", mv.parse_form(body().replace("\n", "\r\n"))["version"] == "1.0")
check("form no response", mv.parse_form("### Mod id\n\n_No response_\n")["mod_id"] == "")
check("form rights", mv.rights_confirmed(f["rights"]) and not mv.rights_confirmed(mv.parse_form(body(rights=False))["rights"]))
check("form first heading wins", mv.parse_form("### Mod id\n\na\n\n### Mod id\n\nb\n")["mod_id"] == "a\n\n### Mod id\n\nb")
check("form garbage", mv.parse_form(None)["mod_id"] == "" and mv.parse_form("hello")["link"] == "")

# ---- canonical hash: known answer, order and folder independence
files = {"mod.json": sha(b"{}"), "game/logo/a.bin": sha(b"a")}
text = b"jsp-mod-hash 1\n" + sha(b"a").encode() + b" game/logo/a.bin\n" + sha(b"{}").encode() + b" mod.json\n"
check("hash format", mv.canonical_hash(files) == sha(text))
check("known answer in docs/MODS.md", mv.canonical_hash(files) == "ce2c86505349b1c37a35fc28a64aa9a5ddc4acac8b6a7e51962ab93e7f22a1ac" and "ce2c86505349b1c37a35fc28a64aa9a5ddc4acac8b6a7e51962ab93e7f22a1ac" in (HERE.parent.parent / "docs" / "MODS.md").read_text(encoding="utf-8"))
M = json.dumps(manifest()).encode()
E = {"mod.json": M, "files/b.txt": b"bb", "files/a.txt": b"a"}
h_root = mv.canonical_hash({p: v[1] for p, v in mv.scan_zip(make_zip(E)).files.items()})
h_rev = mv.canonical_hash({p: v[1] for p, v in mv.scan_zip(make_zip(E, order=["files/b.txt", "files/a.txt", "mod.json"])).files.items()})
h_dir = mv.canonical_hash({p: v[1] for p, v in mv.scan_zip(make_zip(E, prefix="tiny-logo/")).files.items()})
check("hash order", h_root == h_rev)
check("hash folder", h_root == h_dir)
E2 = dict(E)
E2["files/a.txt"] = b"A"
check("hash content", mv.canonical_hash({p: v[1] for p, v in mv.scan_zip(make_zip(E2)).files.items()}) != h_root)
E3 = {"mod.json": M, "files/b.txt": b"bb", "files/c.txt": b"a"}
check("hash name", mv.canonical_hash({p: v[1] for p, v in mv.scan_zip(make_zip(E3)).files.items()}) != h_root)
check("hash bytes sort", mv.canonical_hash({"B": sha(b"1"), "a": sha(b"2")}) ==
      sha(b"jsp-mod-hash 1\n" + sha(b"1").encode() + b" B\n" + sha(b"2").encode() + b" a\n"))
try:
    mv.canonical_hash({"a": "ABC"})
    check("hash bad digest", False)
except ValueError:
    pass

# ---- zip refusals
def problems(data, **kw):
    return mv.scan_zip(data, **kw).problems

check("zip ok", not problems(make_zip(E)))
check("zip not a zip", problems(b"hello")[0][0] == "Archive")
check("zip slip dotdot", any(n == "Paths" for n, _ in problems(raw_zip("../evil.txt"))))
check("zip slip nested", any(n == "Paths" for n, _ in problems(raw_zip("a/../../evil.txt"))))
check("zip absolute", any(n == "Paths" for n, _ in problems(raw_zip("/etc/x"))))
check("zip drive", any(n == "Paths" for n, _ in problems(raw_zip("C:/x"))))
check("zip backslash", any(n == "Paths" for n, _ in problems(raw_zip("a\\..\\x"))))
check("zip symlink", any("symbolic" in t for _, t in problems(make_zip(E, symlink=["files/l"]))))
check("zip device name", any(n == "Paths" for n, _ in problems(make_zip({"mod.json": M, "files/nul.txt": b"x"}))))
check("zip duplicate", any("twice" in t for _, t in problems(make_zip({"mod.json": M}, order=["mod.json", "mod.json"]))))
check("zip case clash", any("case" in t for _, t in problems(make_zip({"mod.json": M, "a": b"1", "A": b"2"}))))
check("zip entry cap", any("entries" in t for _, t in problems(make_zip(E), max_entries=2)))
check("zip unpacked cap", any("unpacked" in t for _, t in problems(make_zip({"mod.json": M, "big": b"0" * 5000}), max_unpacked=1000)))
check("zip file cap", any("over" in t for _, t in problems(make_zip({"mod.json": M, "big": b"0" * 5000}), max_file=1000)))
check("zip no manifest", any(n == "Layout" for n, _ in problems(make_zip({"files/a": b"1"}))))
check("zip two top folders", any(n == "Layout" for n, _ in problems(make_zip({"a/mod.json": M, "b/x": b"1"}))))
check("zip stray file beside folder", any(n == "Layout" for n, _ in problems(make_zip({"a/mod.json": M, "x": b"1"}))))
# A lying header: declared size small, real data larger, must be stopped by the read count (unpacked cap) not trusted.
b = bytearray(make_zip({"mod.json": M, "big": b"0" * 5000}))
i = b.find(b"PK\x01\x02")
while i != -1:
    if b[i + 46:i + 49] == b"big":
        struct.pack_into("<I", b, i + 24, 10)          # central directory: uncompressed size 10
    i = b.find(b"PK\x01\x02", i + 4)
r = mv.scan_zip(bytes(b), max_unpacked=2000)
check("zip lying header stopped by the read count", any("unpacked" in t or "unreadable" in t for _, t in r.problems), str(r.problems))
# the download cap and link checks
for bad in ("http://example.com/a.zip", "https://user:pw" + "@example.com/a.zip", "https://example.com:8443/a.zip", "file:///etc/passwd", "ftp://x/a"):
    try:
        mv.check_url(bad)
        check("url refused " + bad, False)
    except mv.Refused:
        pass
real = socket.getaddrinfo
socket.getaddrinfo = lambda host, *a, **k: [(2, 1, 6, "", ("127.0.0.1" if host == "local.test" else "93.184.216.34", 443))]
try:
    try:
        mv.check_url("https://local.test/a.zip")
        check("url loopback refused", False)
    except mv.Refused:
        pass
    check("url public ok", mv.check_url("https://example.test/a.zip") == "https://example.test/a.zip")
finally:
    socket.getaddrinfo = real

# ---- file types
check("type exe", mv.refused_type("files/a.EXE") and mv.refused_type("x/b.dll") and mv.refused_type("c.ps1") and mv.refused_type("d.sh"))
check("type source", mv.refused_type("a.cpp") and mv.refused_type("x/b.h") and mv.refused_type("sub/CMakeLists.txt"))
check("type port dir", mv.refused_type("port/jsp/x.txt"))
check("type ok", not mv.refused_type("game/logo/a.bin") and not mv.refused_type("readme.txt") and not mv.refused_type("portrait/a.png"))

# ---- manifest
good_file = b"logo-bytes"
ov = [{"entry": "game/logo/a.bin", "file": "files/a.bin", "sha256": sha(good_file)}]
fl = {"mod.json": (1, "0" * 64), "files/a.bin": (len(good_file), sha(good_file))}


def mf(m, folder="tiny-logo", files=fl):
    return mv.check_manifest(json.dumps(m), folder, files)


errs, info = mf(manifest(overrides=ov))
check("manifest ok", not errs and info["gameplay"] is False, str(errs))
check("manifest gameplay path", mf(manifest(overrides=[{"entry": "battle/x.bin", "alias": "battle/y.bin"}]))[1]["gameplay"] is True)
check("manifest gameplay flag", mf(manifest(overrides=ov, gameplay=True))[1]["gameplay"] is True)
check("manifest bad json", mv.check_manifest("{", "x", {})[0])
check("manifest dup key", mv.check_manifest('{"schema":1,"schema":1}', "x", {})[0])
check("manifest not object", mv.check_manifest("[]", "x", {})[0])
check("manifest schema 2", "schema 2" in mf(manifest(schema=2))[0][0])
check("manifest schema bool", mf(manifest(schema=True))[0])
check("manifest bad id", mf(manifest(id="Bad_Id"), folder="")[0])
check("manifest id mismatch", mf(manifest(), folder="other")[0])
check("manifest no version", mf({k: v for k, v in manifest().items() if k != "version"})[0])
check("manifest version space", mf(manifest(version="1 0"))[0])
check("manifest unknown interface", mf(manifest(interfaces={"magic": 1}))[0])
check("manifest newer interface", mf(manifest(interfaces={"cpk-entry": 2}))[0])
check("manifest no interfaces", mf({k: v for k, v in manifest().items() if k != "interfaces"})[0])
check("manifest overrides need cpk-entry", mf(manifest(interfaces={"roster": 1}, overrides=ov))[0])
check("manifest bad archive folder", mf(manifest(overrides=[{"entry": "evil/x.bin", "alias": "evil/y.bin"}]))[0])
check("manifest dotdot entry", mf(manifest(overrides=[{"entry": "game/../x.bin", "alias": "game/y.bin"}]))[0])
check("manifest both file and alias", mf(manifest(overrides=[{"entry": "game/a", "file": "files/a.bin", "alias": "game/b", "sha256": sha(good_file)}]))[0])
check("manifest alias sha", mf(manifest(overrides=[{"entry": "game/a", "alias": "game/b", "sha256": sha(good_file)}]))[0])
check("manifest wrong sha", mf(manifest(overrides=[{"entry": "game/a", "file": "files/a.bin", "sha256": "0" * 64}]))[0])
check("manifest missing file", mf(manifest(overrides=[{"entry": "game/a", "file": "files/none.bin", "sha256": "0" * 64}]))[0])
check("manifest duplicate entry", mf(manifest(overrides=ov + ov))[0])
check("manifest group unknown", mf(manifest(overrides=ov, groups=[["game/zzz"]]))[0])
check("manifest group ok", not mf(manifest(overrides=ov, groups=[["game/logo/a.bin"]]))[0])
check("manifest gameplay type", mf(manifest(gameplay="yes"))[0])
roster = {"fighters": [{"id": 19, "after": 3}]}
check("manifest roster ok", not mf(manifest(interfaces={"roster": 1}, roster=roster))[0])
check("manifest roster class", mf(manifest(interfaces={"roster": 1}, roster=roster))[1]["gameplay"] is True)
check("manifest roster bad id", mf(manifest(interfaces={"roster": 1}, roster={"fighters": [{"id": 5}]}))[0])
check("manifest roster wrong title", mf(manifest(interfaces={"roster": 1}, game={"title_ids": ["BLES02136"]}, roster=roster))[0])
check("manifest roster needs interface", mf(manifest(roster=roster))[0])
check("manifest roster logo_as", mf(manifest(interfaces={"roster": 1}, roster={"fighters": [{"id": 19, "logo_as": 999}]}))[0])

# ---- the whole run with an injected downloader
def run(zbytes, form_body=None, fetcher=None):
    form = mv.parse_form(form_body or body())
    return mv.run_checks(form, fetcher or (lambda url: zbytes))


Z = make_zip({"mod.json": json.dumps(manifest(overrides=ov)).encode(), "files/a.bin": good_file})
rows, fls, digest, info = run(Z, body(man=manifest(overrides=ov)))
check("run pass", all(r[1] for r in rows), str([r for r in rows if not r[1]]))
check("run hash", digest == mv.canonical_hash({p: v[1] for p, v in fls.items()}))
rows, *_ = run(Z, body(man=manifest(overrides=ov), rights=False))
check("run rights fail", any(r[0] == "Rights" and not r[1] for r in rows))
rows, *_ = run(Z, body(man=manifest(version="9")))
check("run pasted differs", any(r[0] == "Pasted mod.json" and not r[1] for r in rows))
rows, *_ = run(Z, body(man=manifest(overrides=ov), cls="Cosmetic"))
check("run cosmetic claim ok", any(r[0] == "Kind of change" and r[1] for r in rows))
Zg = make_zip({"mod.json": json.dumps(manifest(overrides=[{"entry": "battle/a.bin", "file": "files/a.bin", "sha256": sha(good_file)}])).encode(),
               "files/a.bin": good_file})
rows, *_ = run(Zg, body(man=manifest(overrides=[{"entry": "battle/a.bin", "file": "files/a.bin", "sha256": sha(good_file)}]), cls="Cosmetic"))
check("run cosmetic claim false", any(r[0] == "Kind of change" and not r[1] for r in rows))
Zx = make_zip({"mod.json": json.dumps(manifest(overrides=ov)).encode(), "files/a.bin": good_file, "files/run.exe": b"MZ", "port/x.txt": b"1"})
rows, *_ = run(Zx, body(man=manifest(overrides=ov)))
check("run bad type", any(r[0] == "File types" and not r[1] for r in rows))


def refuse(url):
    raise mv.Refused("the link is not HTTPS")


rows, fls, digest, _ = run(None, fetcher=refuse)
check("run download refused", any(r[0] == "Download" and not r[1] for r in rows) and digest is None and not fls)
rows, fls, digest, _ = run(b"not a zip")
check("run not zip", any(r[0] == "Archive" and not r[1] for r in rows) and digest is None)
rows, *_ = run(make_zip({"readme.txt": b"hi"}))
check("run no manifest", any(r[0] == "Layout" and not r[1] for r in rows))

# ---- the comment
rows, fls, digest, _ = run(Z, body(man=manifest(overrides=ov)))
c = mv.render(mv.parse_form(body(man=manifest(overrides=ov))), rows, fls, digest)
check("comment note", mv.NOTE in c)
check("comment hash", digest in c)
check("comment files", "`files/a.bin`, 10 B" in c and "`mod.json`" in c)
check("comment link and download hash", "Link checked: `https://example.invalid/m.zip`" in c and "SHA-256 " + hashlib.sha256(Z).hexdigest() in c)
check("comment table", "| Manifest | pass |" in c and c.startswith("## Mod check: all automated checks passed"))
rows, fls, digest, _ = run(b"nope")
c = mv.render(mv.parse_form(body()), rows, fls, digest)
check("comment fail", "FAIL" in c and "problems found" in c and "Canonical mod hash" not in c and mv.NOTE in c)
evil = {"mod.json": M, "files/@everyone|`x`.txt": b"1"}
Ze = make_zip(evil)
rows, fls, digest, _ = run(Ze)
c = mv.render(mv.parse_form(body(mid="@admin|x")), rows, fls, digest)
check("comment escapes", "@" not in c.replace("@everyone", "") or "`files/?everyone" in c)
check("comment no mention", "@everyone" not in c and "@admin" not in c)
many = {"mod.json": M}
many.update({"f/%04d" % n: b"x" for n in range(300)})
rows, fls, digest, _ = run(make_zip(many))
c = mv.render(mv.parse_form(body()), rows, fls, digest)
check("comment file list capped", "and 101 more" in c and len(c) < mv.MAX_COMMENT)

# ---- main(): ISSUE_BODY through env, an out file, no other input
with tempfile.TemporaryDirectory() as d:
    out = pathlib.Path(d) / "c.md"
    rc = mv.main(["--out", str(out)], fetcher=lambda u: Z, env={"ISSUE_BODY": body(man=manifest(overrides=ov))})
    check("main writes comment", rc == 0 and mv.NOTE in out.read_text(encoding="utf-8"))
    check("main no body", mv.main(["--out", str(out)], env={}) == 2)

# ---- round 2: manifest parity with the port
for key in ("overrides", "groups", "conflicts_ok"):
    check("manifest null " + key, mf(dict(manifest(), **{key: None}))[0])
check("manifest alias null with file", mf(manifest(overrides=[{"entry": "game/a", "alias": None, "file": "files/a.bin", "sha256": sha(good_file)}]))[0])
check("manifest file null with alias", mf(manifest(overrides=[{"entry": "game/a", "alias": "game/b", "file": None}]))[0])
check("manifest alias sha null", mf(manifest(overrides=[{"entry": "game/a", "alias": "game/b", "sha256": None}]))[0])
check("manifest surrogate", mv.check_manifest(json.dumps(manifest()).replace("Tiny logo", "\\ud800"), "tiny-logo", {})[0])
check("manifest escaped backslash before u is not a surrogate", not mv.check_manifest(json.dumps(manifest(title="\\ud800")), "tiny-logo", {})[0])
check("manifest real utf-8 title ok", not mv.check_manifest(json.dumps(manifest(title="\u00e9"), ensure_ascii=False), "tiny-logo", {})[0])
deep = "[" * 40 + "]" * 40
check("manifest too deep", mv.check_manifest(json.dumps(manifest())[:-1] + ', "x": ' + deep + "}", "tiny-logo", {})[0])
check("manifest very deep (recursion)", mv.check_manifest(json.dumps(manifest())[:-1] + ', "x": ' + "[" * 5000 + "]" * 5000 + "}", "tiny-logo", {})[0])
d32 = "[" * 32 + "]" * 32
check("manifest depth 32 ok", not mv.check_manifest(json.dumps(manifest())[:-1] + ', "x": ' + d32 + "}", "tiny-logo", {})[0])
check("manifest version 40 bytes ok", not mf(manifest(version="a" * 40))[0])
check("manifest version 21 two-byte chars", mf(manifest(version="\u00e9" * 21))[0])
check("manifest version 41", mf(manifest(version="a" * 41))[0])
R = {"roster": 1}
ra = lambda fighters, **kw: mf(manifest(interfaces=R, roster={"fighters": fighters}, **kw))[0]
check("roster repeated id", ra([{"id": 19}, {"id": 19}]))
check("roster after stock id ok", not ra([{"id": 19, "after": 32}]))
check("roster after unknown id", ra([{"id": 19, "after": 100}]))
check("roster after earlier fighter ok", not ra([{"id": 19}, {"id": 20, "after": 19}]))
check("roster after later fighter", ra([{"id": 20, "after": 19}, {"id": 19}]))
check("alias in another archive", mf(manifest(overrides=[{"entry": "game/a", "alias": "battle/b"}]))[0])
check("alias same archive ok", not mf(manifest(overrides=[{"entry": "game/a", "alias": "game/b"}]))[0])
check("alias in no archive", mf(manifest(overrides=[{"entry": "game/a", "alias": "zzz/b"}]))[0])
check("empty override file", mf(manifest(overrides=[{"entry": "game/a", "file": "files/e", "sha256": sha(b"")}]), files={"files/e": (0, sha(b""))})[0])
check("roster interface alone is gameplay", mf(manifest(interfaces={"cpk-entry": 1, "roster": 1}, overrides=ov))[1]["gameplay"] is True)
check("bom accepted", all(r[1] for r in mv.check_zip(make_zip({"mod.json": b"\xef\xbb\xbf" + json.dumps(manifest()).encode(), "a": b"1"}))[0]))

# ---- round 2: zip
Zm = lambda **kw: make_zip({"mod.json": M, "files/a": b"1", **kw})
r = mv.check_zip(make_zip(E, prefix="other/"), mv.parse_form(body()))[0]
check("top folder must be the mod id", any(x[0] == "Layout" and not x[1] for x in r))
r = mv.check_zip(make_zip(E, prefix="tiny-logo/"), mv.parse_form(body()))[0]
check("top folder equal to the id ok", all(x[1] for x in r if x[0] == "Layout"))
r = mv.check_zip(make_zip(E, prefix="other/"))[0]
check("local mode top folder must be the manifest id", any(x[0] == "Manifest" and not x[1] for x in r))
nulzip = make_zip({"mod.json": M, "files/aXb": b"1"}).replace(b"files/aXb", b"files/a\x00b")
check("NUL in a name", any("NUL" in t for _, t in problems(nulzip)), str(problems(nulzip)))
def noflag(data):
    b = bytearray(data)
    for sig, off in ((b"PK\x03\x04", 6), (b"PK\x01\x02", 8)):
        i = b.find(sig)
        while i != -1:
            fl = struct.unpack_from("<H", b, i + off)[0]
            struct.pack_into("<H", b, i + off, fl & ~0x800)
            i = b.find(sig, i + 4)
    return bytes(b)
check("non-ASCII name with the UTF-8 flag ok", not problems(make_zip({"mod.json": M, "files/\u00e9.txt": b"1"})))
check("non-ASCII name without the flag", any("non-ASCII" in t for _, t in problems(noflag(make_zip({"mod.json": M, "files/\u00e9.txt": b"1"})))))
check("file is also a folder", any("also a folder" in t for _, t in problems(make_zip({"mod.json": M, "files": b"1", "files/a": b"2"}))))
check("casefold clash", any("case" in t for _, t in problems(make_zip({"mod.json": M, "stra\u00dfe": b"1", "STRASSE": b"2"}))))
for ext in (".asi", ".js", ".hta", ".wsf", ".cpl", ".reg", ".pif", ".vbe", ".jse", ".sys", ".ocx", ".msix", ".appx", ".zip", ".7z", ".rar"):
    check("type " + ext, mv.refused_type("files/a" + ext))
pe = bytearray(b"MZ" + bytes(0x80)); pe[0x3C:0x40] = (0x40).to_bytes(4, "little"); pe[0x40:0x44] = b"PE\0\0"
check("sniff PE", mv.sniff(bytes(pe)) == "a Windows program")
check("sniff MZ without PE header", mv.sniff(b"MZ\x90" + bytes(100)) == "" and mv.sniff(b"MZ") == "")
bad_off = bytearray(pe); bad_off[0x3C:0x40] = (0x7fffffff).to_bytes(4, "little")
check("sniff MZ offset out of range", mv.sniff(bytes(bad_off)) == "")
check("sniff row says a maintainer can override", any("override" in x[2] for x in mv.check_zip(make_zip({"mod.json": M, "files/p.dat": bytes(pe)}))[0] if x[0] == "File types"))
for magic in (bytes(pe), b"\x7fELF", b"#!/bin/sh", b"PK\x03\x04zz"):
    sc = mv.scan_zip(make_zip({"mod.json": M, "files/blob.dat": magic}))
    check("sniff %r" % magic, "files/blob.dat" in sc.sniffed)
    r = mv.check_zip(make_zip({"mod.json": M, "files/blob.dat": magic}))[0]
    check("sniff row %r" % magic, any(x[0] == "File types" and not x[1] for x in r))
check("sniff plain data ok", not mv.scan_zip(make_zip({"mod.json": M, "files/blob.dat": b"CPK \x00"})).sniffed)
for bad in ("https://[::1", "https://exa mple.com/\udcff", "https://" + "a" * 300 + ".com/"):
    try:
        mv.check_url(bad)
        check("check_url bad " + bad[:12], False)
    except mv.Refused:
        pass
try:
    socket.getaddrinfo = lambda *a, **k: (_ for _ in ()).throw(UnicodeError("idna"))
    mv.check_url("https://example.test/")
    check("check_url unicode error", False)
except mv.Refused:
    pass
finally:
    socket.getaddrinfo = real
def boom(url):
    raise RuntimeError("x")
with tempfile.TemporaryDirectory() as d:
    out = pathlib.Path(d) / "c.md"
    mv.main(["--out", str(out)], fetcher=boom, env={"ISSUE_BODY": body()})
    check("a comment is always written", "problems found" in out.read_text(encoding="utf-8") and mv.NOTE in out.read_text(encoding="utf-8"))
    zp = pathlib.Path(d) / "m.zip"
    zp.write_bytes(Zm())
    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = mv.main(["--zip", str(zp)])
    check("local mode", rc == 0 and hashlib.sha256(zp.read_bytes()).hexdigest() in buf.getvalue() and "Canonical mod hash" in buf.getvalue(), buf.getvalue()[:300])
    check("local mode missing file", mv.main(["--zip", str(zp) + "x"]) == 2)

# ---- the tested list validator
H = "a" * 64
ent = {"id": "tiny-logo", "name": "Tiny logo", "version": "1.0", "sha256": H, "tested_on": "0.6.1", "content": "own-work", "notes": ""}
check("tested empty ok", not tm.check({"schema": 1, "mods": []}))
check("tested one ok", not tm.check({"schema": 1, "mods": [ent]}))
check("tested repo file ok", not tm.check(json.loads((HERE.parent.parent / "mods" / "tested.json").read_text(encoding="utf-8"))))
check("tested content other", tm.check({"schema": 1, "mods": [dict(ent, content="game-derived")]}))
check("tested content missing", tm.check({"schema": 1, "mods": [{k: v for k, v in ent.items() if k != "content"}]}))
check("tested bad schema", tm.check({"schema": 2, "mods": []}))
check("tested bad hash", tm.check({"schema": 1, "mods": [dict(ent, sha256="A" * 64)]}))
check("tested bad release", tm.check({"schema": 1, "mods": [dict(ent, tested_on="v1")]}))
check("tested extra key", tm.check({"schema": 1, "mods": [dict(ent, extra="x")]}))
check("tested duplicate", tm.check({"schema": 1, "mods": [ent, dict(ent, sha256="b" * 64)]}))
check("tested duplicate hash", tm.check({"schema": 1, "mods": [ent, dict(ent, id="zz")]}))
check("tested unsorted", tm.check({"schema": 1, "mods": [dict(ent, id="zz"), dict(ent, sha256="c" * 64)]}))

print("test_mod_verify: %d failure(s)" % failed)
sys.exit(1 if failed else 0)
