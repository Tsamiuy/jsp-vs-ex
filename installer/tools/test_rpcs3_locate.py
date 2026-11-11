"""Tests for rpcs3_locate (stdlib unittest, fake trees in temp dirs)."""
import builtins
import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rpcs3_locate as L  # noqa: E402

SERIAL = "BLUS31519"


def make_sfo(**kv):
    keys = b""
    data = b""
    entries = b""
    for k, v in kv.items():
        koff, doff = len(keys), len(data)
        keys += k.encode() + b"\0"
        if isinstance(v, int):
            raw, fmt, ln, mx = struct.pack("<i", v), 0x0404, 4, 4
        else:
            raw = v.encode() + b"\0"
            mx = max(len(raw), 8)
            raw, fmt, ln = raw.ljust(mx, b"\0"), 0x0204, len(v.encode()) + 1
        entries += struct.pack("<HHIII", koff, fmt, ln, mx, doff)
        data += raw
    ktab = 20 + len(entries)
    dtab = ktab + len(keys)
    return (b"\0PSF" + struct.pack("<IIII", 0x101, ktab, dtab, len(kv)) + entries + keys + data)


def w(path, content=b"x"):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content if isinstance(content, bytes) else content.encode())
    return path


class Base(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name).resolve()
        self.addCleanup(self._td.cleanup)
        # isolate from the real machine: no real home, XDG or Wine variables
        for p in (mock.patch.object(L.Path, "home", return_value=self.tmp / "nohome"),
                  mock.patch.dict(os.environ, {"XDG_CONFIG_HOME": "", "WINEHOMEDIR": ""})):
            p.start()
            self.addCleanup(p.stop)

    def tree(self, name="rpcs3", cfg="config", disc=True, update_ver="01.29"):
        root = self.tmp / name
        cfgd = root / cfg if cfg else root
        self.disc = self.tmp / "discs" / "Sample Game"
        if disc:
            (self.disc / "PS3_GAME" / "USRDIR").mkdir(parents=True, exist_ok=True)
        w(cfgd / "games.yml", f"{SERIAL}: {self.disc}/\nOTHER00001: /nowhere/\n")
        w(cfgd / "vfs.yml", "/dev_hdd0/: $(EmulatorDir)dev_hdd0/\n/dev_flash/: $(EmulatorDir)dev_flash/\n")
        (root / "dev_flash" / "data" / "font").mkdir(parents=True, exist_ok=True)
        if update_ver:
            w(root / "dev_hdd0" / "game" / SERIAL / "PARAM.SFO",
              make_sfo(APP_VER=update_ver, TITLE_ID=SERIAL, CATEGORY="GD"))
        return root


class FindTests(Base):
    def test_portable_windows_layout(self):
        root = self.tree("portable")
        w(root / "rpcs3.exe")
        with mock.patch.object(L.Path, "home", return_value=self.tmp / "nohome"):
            c = L.find_rpcs3(str(root))
        self.assertEqual(c[0]["source"], "hint")
        self.assertEqual(c[0]["root"], str(root))
        self.assertEqual(c[0]["config_dir"], str(root / "config"))
        self.assertEqual(c[0]["dev_hdd0"], str(root / "dev_hdd0"))
        self.assertEqual(c[0]["dev_flash"], str(root / "dev_flash"))
        json.dumps(c)

    def test_hint_may_be_the_exe(self):
        root = self.tree("portable")
        exe = w(root / "rpcs3.exe")
        self.assertEqual(L.find_rpcs3(str(exe))[0]["root"], str(root))

    def test_linux_config_layout(self):
        home = self.tmp / "home" / "me"
        root = self.tree("home/me/.config/rpcs3", cfg=None)
        flat = self.tree("home/me/.var/app/net.rpcs3.RPCS3/config/rpcs3")
        with mock.patch.object(L.Path, "home", return_value=home), \
                mock.patch.dict(os.environ, {"XDG_CONFIG_HOME": ""}), \
                mock.patch.object(L, "_is_windows", return_value=False):
            c = L.find_rpcs3()
        self.assertEqual([x["source"] for x in c], ["linux-config", "flatpak"])
        self.assertEqual(c[0]["config_dir"], str(root))
        self.assertEqual(c[1]["root"], str(flat))
        self.assertEqual(L.game_path(c[0], SERIAL)["disc_dir"], str(self.disc))

    def test_wine_z_form(self):
        root = self.tree("home/me/.config/rpcs3")
        with mock.patch.object(L, "_is_windows", return_value=True), \
                mock.patch.dict(os.environ, {"WINEHOMEDIR": "\\??\\" + str(self.tmp / "home" / "me")}):
            cands = L._candidate_paths(None)
        self.assertEqual(cands[0][1], "wine-z-linux-config")
        self.assertTrue(cands[0][0].as_posix().endswith(".config/rpcs3") or
                        str(cands[0][0]).endswith("rpcs3"))

    def test_vfs_moves_dev_hdd0(self):
        root = self.tree()
        other = self.tmp / "bigdrive"
        w(root / "config" / "vfs.yml",
          f'"/dev_hdd0/": {other}/hdd\n/dev_flash/: $(EmulatorDir)flash2/\n# c\n')
        c = L.find_rpcs3(str(root))[0]
        self.assertEqual(c["dev_hdd0"], str(other / "hdd"))
        self.assertEqual(c["dev_flash"], str(root / "flash2"))

    def test_no_candidates(self):
        with mock.patch.object(L.Path, "home", return_value=self.tmp / "nohome"):
            self.assertEqual(L.find_rpcs3(), [])
            r = L.locate(None, {SERIAL: "01.29"})
            r2 = L.locate(str(self.tmp / "nope"), {SERIAL: "01.29"})
        self.assertFalse(r["ok"])
        self.assertEqual(r["problems"][0]["code"], "not_found")
        self.assertEqual(r2["code"], "not_rpcs3")


class SfoTests(Base):
    def test_param_sfo(self):
        p = w(self.tmp / "PARAM.SFO", make_sfo(APP_VER="01.29", TITLE_ID=SERIAL, TITLE="Sample Game",
                                               CATEGORY="GD", VERSION="01.01", PARENTAL_LEVEL=5))
        r = L.param_sfo(p)
        self.assertTrue(r["ok"])
        self.assertEqual(r["keys"]["APP_VER"], "01.29")
        self.assertEqual(r["keys"]["TITLE"], "Sample Game")
        self.assertEqual(r["keys"]["PARENTAL_LEVEL"], 5)
        self.assertEqual(L.param_sfo(self.tmp)["keys"]["TITLE_ID"], SERIAL)

    def test_bad_and_missing(self):
        self.assertEqual(L.param_sfo(w(self.tmp / "a" / "PARAM.SFO", b"junk" * 10))["code"], "bad_sfo")
        self.assertEqual(L.param_sfo(self.tmp / "zz")["code"], "not_found")
        trunc = make_sfo(A="b")[:22]
        self.assertEqual(L.param_sfo(w(self.tmp / "t" / "PARAM.SFO", trunc))["code"], "bad_sfo")


class TitleTests(Base):
    def test_game_path(self):
        root = self.tree()
        r = L.game_path(root, SERIAL)
        self.assertTrue(r["ok"] and r["has_ps3_game"] and not r["has_psn_game"])
        self.assertEqual(L.game_path(root, "NOPE00000")["code"], "not_found")
        self.assertEqual(L.game_path(root, "OTHER00001")["code"], "missing_disc")

    def test_update_ok_missing_wrong(self):
        root = self.tree()
        self.assertTrue(L.update_dir(root, SERIAL, "01.29")["ok"])
        self.assertTrue(L.update_dir(root, SERIAL, "1.29")["ok"])
        r = L.update_dir(root, SERIAL, "01.30")
        self.assertEqual((r["ok"], r["code"], r["app_ver"]), (False, "wrong_version", "01.29"))
        miss = self.tree("r2", update_ver=None)
        self.assertEqual(L.update_dir(miss, SERIAL)["code"], "missing_update")

    def test_install_dirs(self):
        root = self.tree()
        w(root / "dev_hdd0" / "game" / (SERIAL + "_INSTALL") / "PARAM.SFO",
          make_sfo(APP_VER="01.00", TITLE_ID=SERIAL))
        names = [d["name"] for d in L.install_dirs(root, SERIAL)]
        self.assertEqual(names, [SERIAL, SERIAL + "_INSTALL"])
        self.assertEqual(L.install_dirs(root, SERIAL)[1]["app_ver"], "01.00")

    def test_fonts(self):
        root = self.tree()
        self.assertTrue(L.fonts_dir(root)["path"].endswith("font"))
        (root / "dev_flash" / "data" / "font").rmdir()
        self.assertEqual(L.fonts_dir(root)["code"], "missing_fonts")

    def test_saves_two_users(self):
        root = self.tree()
        for uid in ("00000001", "00000002"):
            w(root / "dev_hdd0/home" / uid / "savedata" / (SERIAL + "-AUTO") / "PARAM.SFO",
              make_sfo(TITLE_ID=SERIAL))
        w(root / "dev_hdd0/home/00000002/savedata/BCUS11111-X/PARAM.SFO", make_sfo(TITLE_ID="BCUS11111"))
        w(root / "dev_hdd0/home/00000002/savedata" / (SERIAL + "-B") / "DATA.BIN", b"1234")
        r = L.saves(root, SERIAL)
        self.assertEqual(sorted(r["users"]), ["00000001", "00000002"])
        self.assertEqual(len(r["users"]["00000001"]), 1)
        self.assertEqual(len(r["users"]["00000002"]), 2)
        e = r["users"]["00000001"][0]
        self.assertEqual(e["title_id"], SERIAL)
        self.assertIsInstance(e["mtime"], float)
        json.dumps(r)

    def test_decrypted_eboot_order(self):
        root = self.tree()
        upd = root / "dev_hdd0/game" / SERIAL
        extra = w(self.tmp / "x" / "EBOOT.elf", b"\x7fELF" + b"e" * 3)
        self.assertEqual(L.decrypted_eboot([upd, self.disc])["code"], "no_decrypted_eboot")
        disc_e = w(self.disc / "PS3_GAME/USRDIR/EBOOT.elf", b"\x7fELF" + b"d" * 10)
        r = L.decrypted_eboot([upd, self.disc, extra])
        self.assertEqual(r["path"], str(disc_e))
        upd_e = w(upd / "USRDIR/EBOOT.elf", b"\x7fELF" + b"u" * 20)
        r = L.decrypted_eboot([upd, self.disc, extra])
        self.assertEqual([f["path"] for f in r["found"]], [str(upd_e), str(disc_e), str(extra)])
        self.assertEqual(r["size_hint"], 24)  # 4-byte ELF magic + 20

    def test_locate_all(self):
        root = self.tree()
        w(self.disc / "PS3_GAME/USRDIR/EBOOT.elf", b"\x7fELF" + b"d")
        r = L.locate(str(root), {SERIAL: "01.29"})
        self.assertTrue(r["ok"])
        t = r["titles"][SERIAL]
        self.assertEqual(t["problems"], [])
        self.assertEqual(r["fonts"], str(root / "dev_flash/data/font"))
        json.dumps(r)

    def test_locate_problems(self):
        root = self.tree(update_ver="01.00")
        r = L.locate(str(root), {SERIAL: "01.29", "OTHER00001": None, "NONE00000": "01.29"})
        self.assertEqual([p["code"] for p in r["titles"][SERIAL]["problems"]],
                         ["wrong_version", "no_decrypted_eboot"])
        self.assertEqual([p["code"] for p in r["titles"]["OTHER00001"]["problems"]],
                         ["missing_disc", "no_decrypted_eboot"])
        self.assertEqual([p["code"] for p in r["titles"]["NONE00000"]["problems"]][:2],
                         ["missing_disc", "missing_update"])

    def test_cli(self):
        root = self.tree()
        import io
        buf = io.StringIO()
        with mock.patch("sys.stdout", buf):
            rc = L.main(["--json", "--rpcs3", str(root), "--title", f"{SERIAL}=01.29"])
        self.assertEqual(rc, 0)
        out = json.loads(buf.getvalue())
        self.assertEqual(out["root"], str(root))
        buf = io.StringIO()
        with mock.patch("sys.stdout", buf):
            L.main(["--json", "--rpcs3", str(root), "--redacted", "--title", SERIAL])
        self.assertEqual(json.loads(buf.getvalue())["root"], "<RPCS3>")


class ReviewTests(Base):
    """Review fixes: forbidden-by-root, hint validity, copy_save rules, wants modes."""

    def test_cache_only_relative_to_root(self):
        root = self.tree()
        L.find_rpcs3(str(root))
        self.assertTrue(L._forbidden(root / "cache" / "shaders" / "a"))
        self.assertTrue(L._forbidden(root / "dev_hdd0/home/00000001/exdata/x.edat"))
        self.assertTrue(L._forbidden(self.tmp / "any" / "GAME.RAP"))
        self.assertTrue(L._forbidden(self.tmp / "any" / "act.dat"))
        game = w(self.tmp / "cache" / "Games" / "EBOOT.elf", b"\x7fELF" + b"e")
        self.assertFalse(L._forbidden(game))
        self.assertEqual(L.sha256(game)["ok"], True)
        self.assertFalse(L._forbidden(root / "dev_hdd0/home/00000001/savedata/exdata"))

    def test_forbidden_follows_moved_hdd0(self):
        root = self.tree()
        other = self.tmp / "bigdrive" / "hdd"
        w(root / "config" / "vfs.yml", f"/dev_hdd0/: {other}/\n")
        L.find_rpcs3(str(root))
        self.assertTrue(L._forbidden(other / "home/00000001/exdata/x"))

    def test_hint_must_be_rpcs3(self):
        plain = self.tmp / "Documents"
        plain.mkdir()
        c = L.find_rpcs3(str(plain))
        self.assertEqual((c[0]["ok"], c[0]["code"]), (False, "not_rpcs3"))
        for name, layout in (("exe", "rpcs3.exe"), ("rootyml", "vfs.yml"),
                             ("cfgyml", "config/games.yml")):
            r = self.tmp / name
            w(r / layout)
            self.assertNotIn("code", L.find_rpcs3(str(r))[0], name)

    def test_bad_hint_does_not_fall_through(self):
        home = self.tmp / "home" / "me"
        self.tree("home/me/.config/rpcs3", cfg=None)
        plain = self.tmp / "Documents"
        plain.mkdir()
        with mock.patch.object(L.Path, "home", return_value=home), \
                mock.patch.object(L, "_is_windows", return_value=False):
            r = L.locate(str(plain), {SERIAL: "01.29"})
        self.assertFalse(r["ok"])
        self.assertEqual(r["code"], "not_rpcs3")
        self.assertIsNone(r["root"])
        self.assertEqual(r["titles"], {})
        self.assertEqual(r["candidates"], [str(home / ".config/rpcs3")])

    def test_windows_default_places(self):
        la, ap = self.tmp / "Local", self.tmp / "Roaming"
        (la / "Programs" / "rpcs3").mkdir(parents=True)
        (ap / "rpcs3").mkdir(parents=True)
        with mock.patch.object(L, "_is_windows", return_value=True), \
                mock.patch.dict(os.environ, {"LOCALAPPDATA": str(la), "APPDATA": str(ap)}):
            srcs = [c["source"] for c in L.find_rpcs3()]
        self.assertEqual(srcs, ["windows-localappdata", "windows-appdata"])

    def test_copy_save_refuses_rpcs3_dst(self):
        root = self.tree()
        src = self.tmp / "save"
        w(src / "PARAM.SFO")
        L.find_rpcs3(str(root))
        r = L.copy_save(src, root / "dev_hdd0" / "home" / "00000001" / "savedata" / "X", dry_run=False)
        self.assertEqual(r["code"], "dst_in_rpcs3")
        self.assertFalse((root / "dev_hdd0" / "home").exists())

    def test_copy_save_replaces_and_stale(self):
        src = self.tmp / "save"
        w(src / "A", b"1")
        w(src / "B", b"22")
        dst = self.tmp / "out"
        w(dst / "A", b"old")
        w(dst / "OLD", b"zz")
        r = L.copy_save(src, dst, overwrite=True)
        self.assertEqual(r["replaces"], ["A"])
        self.assertEqual(r["stale"], ["OLD"])
        self.assertEqual(sorted(f["rel"] for f in r["files"]), ["A", "B"])
        L.copy_save(src, dst, overwrite=True, dry_run=False)
        self.assertEqual((dst / "OLD").read_bytes(), b"zz")  # merge, nothing deleted
        self.assertEqual((dst / "A").read_bytes(), b"1")

    def test_quoted_windows_path(self):
        root = self.tree()
        w(root / "config" / "games.yml", "BLES02136: 'D:\\Games\\Sample Game 3\\'\n")
        self.assertEqual(L.parse_kv("BLES02136: 'D:\\Games\\Sample Game 3\\'")["BLES02136"],
                         "D:\\Games\\Sample Game 3\\")
        r = L.game_path(root, "BLES02136")
        self.assertEqual(r["disc_dir"], "D:\\Games\\Sample Game 3")

    def test_no_titles(self):
        root = self.tree()
        for w_ in (None, {}):
            r = L.locate(str(root), w_)
            self.assertEqual((r["ok"], r["code"]), (False, "no_titles"))
        self.assertFalse(hasattr(L, "DEFAULT_WANTS"))
        with self.assertRaises(SystemExit), mock.patch("sys.stderr"):
            L.main(["--json", "--rpcs3", str(root)])

    def _disc_sfo(self, **kv):
        w(self.disc / "PS3_GAME" / "PARAM.SFO", make_sfo(**kv))

    def test_disc_only_modes(self):
        root = self.tree(update_ver=None)
        # Like a real disc: APP_VER 01.00, VERSION 02.00; the disc want compares VERSION.
        self._disc_sfo(TITLE_ID="BCES00000", VERSION="02.00", APP_VER="01.00")
        w(self.disc / "PS3_GAME/USRDIR/EBOOT.elf", b"\x7fELF" + b"d")
        r = L.locate(str(root), {SERIAL: "disc:02.00"})
        t = r["titles"][SERIAL]
        self.assertIsNone(t["update"])
        self.assertEqual(t["problems"], [])
        self.assertEqual(t["disc"]["version"], "02.00")
        self.assertEqual(t["disc"]["title_id"], "BCES00000")
        # no version at all: never missing_update
        self.assertEqual(L.locate(str(root), {SERIAL: None})["titles"][SERIAL]["problems"], [])
        # an update that exists is reported
        self.tree(update_ver="01.29")
        u = L.locate(str(root), {SERIAL: None})["titles"][SERIAL]["update"]
        self.assertTrue(u["ok"])

    def test_disc_version_keys(self):
        root = self.tree(update_ver=None)
        w(self.disc / "PS3_GAME/USRDIR/EBOOT.elf", b"\x7fELF" + b"d")
        codes = lambda want: [x["code"] for x in L.locate(str(root), {SERIAL: want})["titles"][SERIAL]["problems"]]
        key = lambda want: L.locate(str(root), {SERIAL: want})["titles"][SERIAL]["disc_ver_key"]
        # First real-disc pair: VERSION 02.00 / APP_VER 01.00
        self._disc_sfo(VERSION="02.00", APP_VER="01.00")
        self.assertEqual(codes("disc:02.00"), []); self.assertEqual(key("disc:02.00"), "VERSION")
        self.assertEqual(codes("disc:VERSION=02.00"), [])
        self.assertEqual(codes("disc:VERSION=01.00"), ["wrong_version"])
        # Second real-disc pair: VERSION 01.01 / APP_VER 01.00
        self._disc_sfo(VERSION="01.01", APP_VER="01.00")
        self.assertEqual(codes("disc:01.00"), []); self.assertEqual(key("disc:01.00"), "APP_VER")
        self.assertEqual(codes("disc:APP_VER=01.00"), [])
        self.assertEqual(codes("disc:APP_VER=01.01"), ["wrong_version"])
        self.assertEqual(codes("disc:02.00"), ["wrong_version"])

    def test_eboot_kind(self):
        root = self.tree(update_ver=None)
        e = w(self.disc / "PS3_GAME/USRDIR/EBOOT.elf", b"SCE\x00" + b"x" * 8)
        b = w(self.disc / "PS3_GAME/USRDIR/EBOOT.BIN", b"SCE\x00" + b"x" * 8)
        self.assertEqual(L.eboot_kind(b)["kind"], "self")
        # an encrypted SELF saved under the .elf name is not offered as decrypted
        self.assertEqual(L.decrypted_eboot([str(self.disc)])["code"], "no_decrypted_eboot")
        w(e, b"\x7fELF" + b"x" * 8)
        self.assertEqual(L.eboot_kind(e)["kind"], "elf")
        self.assertTrue(L.decrypted_eboot([str(self.disc)])["ok"])

    def test_disc_version_wrong(self):
        root = self.tree(update_ver=None)
        self._disc_sfo(VERSION="01.00")
        w(self.disc / "PS3_GAME/USRDIR/EBOOT.elf", b"\x7fELF" + b"d")
        p = L.locate(str(root), {SERIAL: "disc:02.00"})["titles"][SERIAL]["problems"]
        self.assertEqual([x["code"] for x in p], ["wrong_version"])
        self.assertIn("disc", p[0]["text"])
        # plain want with no update: only missing_update (the disc's own version is not what was asked for)
        p = L.locate(str(root), {SERIAL: "02.00"})["titles"][SERIAL]["problems"]
        self.assertEqual([x["code"] for x in p], ["missing_update"])

    def test_game_data_is_not_an_update(self):
        root = self.tree(update_ver=None)
        gd = root / "dev_hdd0/game" / SERIAL
        w(gd / "USRDIR" / "EBOOT.elf", b"\x7fELF" + b"should not be used")
        w(self.disc / "PS3_GAME/USRDIR/EBOOT.elf", b"\x7fELF" + b"d")
        self.assertEqual(L.update_dir(root, SERIAL)["code"], "game_data_only")
        t = L.locate(str(root), {SERIAL: None})["titles"][SERIAL]
        self.assertEqual(t["problems"], [])
        self.assertEqual(t["installs"][0]["kind"], "game_data")
        self.assertEqual(t["decrypted_eboot"]["path"], str(self.disc / "PS3_GAME/USRDIR/EBOOT.elf"))
        self.assertEqual(len(t["decrypted_eboot"]["found"]), 1)
        p = L.locate(str(root), {SERIAL: "01.29"})["titles"][SERIAL]["problems"]
        self.assertIn("missing_update", [x["code"] for x in p])
        # an update folder is classified as such
        self.tree(update_ver="01.29")
        self.assertEqual(L.install_dirs(root, SERIAL)[0]["kind"], "update")


class MiscTests(Base):
    def test_sha256(self):
        p = w(self.tmp / "f.bin", b"abc" * 1000)
        import hashlib
        r = L.sha256(p)
        self.assertEqual(r["sha256"], hashlib.sha256(b"abc" * 1000).hexdigest())
        self.assertEqual(L.sha256(self.tmp / "no")["code"], "unreadable")
        self.assertEqual(L.sha256(w(self.tmp / "a.rap", b"k"))["code"], "forbidden")

    def test_redact(self):
        root = self.tmp / "rpcs3"
        home = self.tmp / "home" / "me"
        with mock.patch.object(L.Path, "home", return_value=home):
            self.assertEqual(L.redact(root / "dev_hdd0" / "x", [str(root)]), "<RPCS3>/dev_hdd0/x")
            self.assertEqual(L.redact(home / ".config" / "a"), "<home>/.config/a")
            self.assertEqual(L.redact("/ho" + "me/other/Games/x"), "<user>/Games/x")
            self.assertEqual(L.redact("C:\\Us" + "ers\\Bob\\RPCS3\\x"), "<user>/RPCS3/x")
            self.assertEqual(L.redact("D:\\Games\\Sample disc\\PS3_GAME\\USRDIR\\EBOOT.BIN"), "<path>/USRDIR/EBOOT.BIN")
            self.assertEqual(L.redact("/mnt/discs/BLUS31519/PS3_GAME"), "<path>/BLUS31519/PS3_GAME")
            self.assertEqual(L.redact("relative/x"), "relative/x")
            self.assertEqual(L.redact("/opt/x"), "<path>/opt/x")   # other absolute paths: last two components only

    def test_copy_save(self):
        src = self.tmp / "save"
        w(src / "PARAM.SFO", b"s" * 5)
        w(src / "sub" / "DATA", b"d" * 7)
        w(src / "LICENSE.RAP", b"k")
        w(src / "act.dat", b"k")
        dst = self.tmp / "out" / "save"
        r = L.copy_save(src, dst)
        self.assertTrue(r["ok"] and r["dry_run"])
        self.assertEqual({f["rel"]: f["size"] for f in r["files"]}, {"PARAM.SFO": 5, "sub/DATA": 7})
        self.assertIn("LICENSE.RAP", r["skipped"])
        self.assertFalse(dst.exists())
        r = L.copy_save(src, dst, dry_run=False)
        self.assertEqual(r["copied"], 2)
        self.assertEqual(sorted(p.name for p in dst.rglob("*") if p.is_file()), ["DATA", "PARAM.SFO"])
        r = L.copy_save(src, dst, dry_run=False)
        self.assertEqual((r["ok"], r["code"]), (False, "exists"))
        r = L.copy_save(src, dst, overwrite=True, dry_run=False)
        self.assertTrue(r["ok"])
        self.assertEqual(L.copy_save(self.tmp / "none", dst)["code"], "not_found")
        self.assertEqual(L.copy_save(src, src / "in")["code"], "bad_dst")

    def test_never_opens_rap_or_cache(self):
        root = self.tree()
        w(root / "cache" / "shaders" / "a.bin", b"s")
        w(root / "dev_hdd0/home/00000001/exdata/x.rap", b"k")
        w(root / "dev_hdd0/home/00000001/exdata/act.dat", b"k")
        w(root / "dev_hdd0/game" / SERIAL / "LIC.RAP", b"k")
        w(self.disc / "PS3_GAME/USRDIR/EBOOT.elf", b"\x7fELF" + b"d")
        w(root / "dev_hdd0/home/00000001/savedata" / (SERIAL + "-A") / "PARAM.SFO", make_sfo(TITLE_ID=SERIAL))
        w(root / "dev_hdd0/home/00000001/savedata" / (SERIAL + "-A") / "k.rap", b"k")
        opened = []
        real = builtins.open

        def spy(f, *a, **k):
            opened.append(str(f))
            return real(f, *a, **k)

        with mock.patch.object(builtins, "open", spy):
            L.locate(str(root), {SERIAL: "01.29"})
            L.copy_save(root / "dev_hdd0/home/00000001/savedata" / (SERIAL + "-A"), self.tmp / "o",
                        dry_run=False)
            L.sha256(root / "dev_hdd0/game" / SERIAL / "LIC.RAP")
        self.assertTrue(opened)
        for p in opened:
            low = p.lower().replace("\\", "/")
            self.assertFalse(low.endswith(".rap") or low.endswith("act.dat"), p)
            self.assertNotIn("/exdata/", low)
            self.assertNotIn("/cache/", low)
        with self.assertRaises(PermissionError):
            L._open_ro(root / "cache" / "shaders" / "a.bin")
        with self.assertRaises(PermissionError):
            L._open_ro(self.tmp / "f", "wb")


if __name__ == "__main__":
    unittest.main()
