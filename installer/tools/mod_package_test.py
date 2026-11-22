#!/usr/bin/env python3
"""Synthetic ZIPs only: validate extraction, no-overwrite, rollback and shipping."""
import dataclasses
import io
import json
import os
import pathlib
import stat
import struct
import sys
import tempfile
import unittest
import zipfile
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import assemble_payload
import assemble_portable
import mod_package as mp

MANIFEST = json.dumps({"schema": 1, "id": "sample"}).encode()


class Packages(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        self.mods = self.root / "mods"
        self.mods.mkdir()
        self.zip = self.root / "package.zip"

    def tearDown(self):
        self.tmp.cleanup()

    def make(self, entries=(), prefix="", manifest=MANIFEST):
        with zipfile.ZipFile(self.zip, "w") as z:
            z.writestr(prefix + "mod.json", manifest)
            for name, value in entries:
                z.writestr(name, value)
        return self.zip

    def refuse(self, **kw):
        with self.assertRaises(mp.PackageError):
            mp.install_package(self.zip, self.mods, **kw)
        self.assertEqual(list(self.mods.iterdir()), [])

    def test_root_and_wrapper(self):
        for prefix in ("", "release/"):
            with self.subTest(prefix=prefix):
                self.make([(prefix + "data.txt", b"data")], prefix)
                result = mp.install_package(self.zip, self.mods)
                self.assertEqual((result / "data.txt").read_bytes(), b"data")
                self.assertEqual(sorted(p.name for p in self.mods.iterdir()), ["sample"])
                import shutil
                shutil.rmtree(result)

    def test_path_namespace(self):
        for bad in ("../escape", "/absolute", "C:/drive", "//server/share", "x:y", "a//b", "a/./b", "a/../b", "con", "NUL.txt", "aux .txt", "CONIN$", "conout$.txt", "COM¹", "LPT².txt", "tail.", "tail ", "bad\nname"):
            with self.subTest(path=bad):
                self.make([(bad, b"x")])
                self.refuse()
        self.make([("a/b", b"x")])
        self.zip.write_bytes(self.zip.read_bytes().replace(b"a/b", b"a\\b"))
        self.refuse()
        for names in (("a", "a/b"), ("A/x", "a/y"), ("a", "a"), ("é", "e\u0301")):
            with self.subTest(names=names):
                self.make([(n, b"x") for n in names])
                self.refuse()

    def test_nul_original_filename(self):
        self.make([("nulX", b"x")])
        self.zip.write_bytes(self.zip.read_bytes().replace(b"nulX", b"nul\0"))
        self.refuse()

    def test_special_types_reparse_encryption_compression(self):
        for mode in (stat.S_IFLNK, stat.S_IFIFO, stat.S_IFCHR, stat.S_IFSOCK, stat.S_IFDIR):
            with self.subTest(mode=mode):
                info = zipfile.ZipInfo("special")
                info.external_attr = (mode | 0o600) << 16
                self.make([(info, b"x")])
                self.refuse()
        info = zipfile.ZipInfo("reparse")
        info.external_attr = (stat.S_IFREG | 0o600) << 16 | 0x400
        self.make([(info, b"x")])
        self.refuse()
        for offset, mask in ((8, 1), (10, 99)):
            self.make()
            b = bytearray(self.zip.read_bytes())
            center = b.index(b"PK\x01\x02")
            struct.pack_into("<H", b, center + offset, mask)
            self.zip.write_bytes(b)
            self.refuse()

    def test_manifest_and_root_shape(self):
        for value in ({"schema": True, "id": "sample"}, {"schema": 2, "id": "sample"}, {"schema": 1, "id": "-bad"}, {"schema": 1, "id": "con"}, {"schema": 1, "id": "../bad"}, []):
            with self.subTest(manifest=value):
                self.make(manifest=json.dumps(value).encode())
                self.refuse()
        self.make(manifest=b'{"schema":1,"id":"sample","other":NaN}')
        self.refuse()
        self.make(manifest=b'{"schema":1,"id":"sample","huge":' + b"1" * 5000 + b"}")
        self.refuse()
        self.make(manifest=b'{"schema":1,"id":"sample","id":"other"}')
        self.refuse()
        self.make([("other/mod.json", MANIFEST)])
        self.refuse()
        self.make([("outside.txt", b"x")], prefix="wrapped/")
        self.refuse()

    def test_limits_and_actual_central_count(self):
        self.make([("a", b"12345678")])
        for limits in (mp.Limits(entries=1), mp.Limits(per_file=4), mp.Limits(total=4), mp.Limits(archive=4), mp.Limits(central=4), mp.Limits(manifest=4)):
            with self.subTest(limits=limits):
                self.refuse(limits=limits)
        self.make([("a", b"x"), ("b", b"x")])
        b = bytearray(self.zip.read_bytes())
        footer = b.rindex(b"PK\x05\x06")
        struct.pack_into("<HH", b, footer + 8, 1, 1)
        self.zip.write_bytes(b)
        self.refuse(limits=mp.Limits(entries=2))

    def test_crc_and_publish_failure_cleanup(self):
        self.make([("large", b"x" * 200000)])
        with zipfile.ZipFile(self.zip) as z:
            info = z.getinfo("large")
        b = bytearray(self.zip.read_bytes())
        n, e = struct.unpack_from("<HH", b, info.header_offset + 26)
        b[info.header_offset + 30 + n + e + 100000] ^= 1
        self.zip.write_bytes(b)
        self.refuse()
        self.make()
        with mock.patch.object(mp, "publish", side_effect=OSError("synthetic publication failure")):
            self.refuse()

    def test_actual_streamed_limits(self):
        self.make([("a", b"x")])
        original = zipfile.ZipFile.open

        def opened(z, name, *a, **kw):
            if getattr(name, "filename", name) == "a":
                return io.BytesIO(b"x" * 100)
            return original(z, name, *a, **kw)

        with mock.patch.object(zipfile.ZipFile, "open", opened):
            self.refuse(limits=dataclasses.replace(mp.Limits(), per_file=50))
            self.refuse(limits=dataclasses.replace(mp.Limits(), total=len(MANIFEST) + 50))

    def test_existing_mod_and_case_alias(self):
        self.make()
        # Win32 normalizes trailing dots/spaces before creating the directory.
        # Keep real case-alias/no-overwrite checks there; exercise distinct
        # on-disk suffix aliases on POSIX, where these names are representable.
        names = ("sample", "SAMPLE") if os.name == "nt" else ("sample", "SAMPLE", "sample.", "sample ")
        for name in names:
            dest = self.mods / name
            dest.mkdir()
            (dest / "keep").write_bytes(b"unchanged")
            with self.assertRaises(mp.PackageError):
                mp.install_package(self.zip, self.mods)
            self.assertEqual((dest / "keep").read_bytes(), b"unchanged")
            (dest / "keep").unlink()
            dest.rmdir()
            self.assertEqual(list(self.mods.iterdir()), [])
        # Even an empty destination arriving after the precheck must not be replaced on POSIX.
        publish = mp.publish

        def raced(stage, target):
            target.mkdir()
            publish(stage, target)

        with mock.patch.object(mp, "publish", raced), self.assertRaises(mp.PackageError):
            mp.install_package(self.zip, self.mods)
        self.assertTrue((self.mods / "sample").is_dir())
        self.assertFalse((self.mods / "sample/mod.json").exists())
        self.assertEqual([p.name for p in self.mods.iterdir()], ["sample"])

    @unittest.skipIf(os.name == "nt", "POSIX symlink fixture; Windows reparse checks require Windows evidence")
    def test_existing_root_link(self):
        self.make()
        self.mods.rmdir()
        outside = self.root / "outside"
        outside.mkdir()
        self.mods.symlink_to(outside, target_is_directory=True)
        with self.assertRaises(mp.PackageError):
            mp.install_package(self.zip, self.mods)
        self.assertEqual(list(outside.iterdir()), [])

    @unittest.skipIf(os.name == "nt", "POSIX FIFO source fixture")
    def test_nonregular_archive_source(self):
        os.mkfifo(self.zip)
        self.refuse()

    def test_scripts_stay_inert(self):
        marker = self.root / "ran"
        self.make([("install.py", ("open(" + repr(str(marker)) + ", 'w').write('bad')").encode())])
        mp.install_package(self.zip, self.mods)
        self.assertFalse(marker.exists())

    def test_packaged_helper(self):
        payload = self.root / "payload"
        (payload / "tools").mkdir(parents=True)
        assemble_payload.copy_runtime_tools(payload)
        helper = payload / "tools/mod_package.py"
        self.assertEqual(helper.read_bytes(), (HERE / "mod_package.py").read_bytes())
        self.assertIn("tools/mod_package.py", assemble_portable.REQUIRED)
        for name in (*assemble_portable.REQUIRED, "tools/lift_jobs.py", "tree.txt"):
            f = payload / name
            f.parent.mkdir(parents=True, exist_ok=True)
            if not f.exists():
                f.write_bytes(b"fixture")
        (payload / "tree.txt").write_text("v18\n")
        (payload / "VERSION.txt").write_text("0.0.0\n")
        out = self.root / "packed"
        self.assertEqual(assemble_portable.main(["--payload", str(payload), "--out", str(out), "--top", "fixture"]), 0)
        archive = next(out.glob("*.zip"))
        with zipfile.ZipFile(archive) as z:
            self.assertEqual(z.read("fixture/tools/mod_package.py"), helper.read_bytes())
        helper.unlink()
        self.assertNotEqual(assemble_portable.main(["--payload", str(payload), "--out", str(out), "--top", "fixture"]), 0)


if __name__ == "__main__":
    unittest.main()
