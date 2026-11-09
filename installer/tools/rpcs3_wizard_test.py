#!/usr/bin/env python3
"""Tests for rpcs3_wizard.py with fake RPCS3 trees in temp dirs (any OS, Python 3.11+, no game data):

    python3 installer/tools/rpcs3_wizard_test.py

Cases: found, not found, wrong hint, EU disc, EBOOT present or absent (also an encrypted file named .elf), dev_flash missing,
save present or absent, a dry run that lists a replace and stale file, the plan hash that guards the copy, the destination
refusal, and that nothing in the logged (redacted) output holds a real path or the save's PARAM.SFO content.
"""
import contextlib
import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rpcs3_locate as L  # noqa: E402
import rpcs3_wizard as W  # noqa: E402
from test_rpcs3_locate import make_sfo, w  # noqa: E402

SECRET = b"ACCOUNT-ID-0123456789"   # stands for the account id a real save's PARAM.SFO holds


def run(*argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
        rc = W.main(list(argv))
    return rc, out.getvalue()


def read_ini(path):
    d = {}
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if "=" in line:
            k, _, v = line.partition("=")
            d[k] = v
    return d


class T(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name).resolve()
        self.addCleanup(self._td.cleanup)
        for p in (mock.patch.object(L.Path, "home", return_value=self.tmp / "nohome"),
                  mock.patch.dict(os.environ, {"XDG_CONFIG_HOME": "", "WINEHOMEDIR": "", "LOCALAPPDATA": "", "APPDATA": ""})):
            p.start()
            self.addCleanup(p.stop)

    def tree(self, serial="BLUS31519", eboot="elf", flash=True, save=False, games=True):
        root = self.tmp / "rpcs3"
        disc = self.tmp / "discs" / "J-Stars"
        (disc / "PS3_GAME" / "USRDIR").mkdir(parents=True, exist_ok=True)
        w(disc / "PS3_GAME" / "PARAM.SFO", make_sfo(TITLE_ID=serial, VERSION="01.00"))
        if eboot:
            w(disc / "PS3_GAME" / "USRDIR" / "EBOOT.elf", b"\x7fELF" + b"1" * 20 if eboot == "elf" else b"SCE\x00" + b"1" * 20)
        if games:
            w(root / "config" / "games.yml", f"{serial}: {disc}/\n")
        w(root / "config" / "vfs.yml", "/dev_hdd0/: $(EmulatorDir)dev_hdd0/\n/dev_flash/: $(EmulatorDir)dev_flash/\n")
        if flash:
            (root / "dev_flash" / "sys").mkdir(parents=True, exist_ok=True)
        if save:
            s = root / "dev_hdd0" / "home" / "00000001" / "savedata" / "NPUB31647-B0"
            w(s / "PARAM.SFO", make_sfo(TITLE_ID="NPUB31647", ACCOUNT=SECRET.decode()))
            w(s / "GAME.DAT", b"slot")
        self.disc = disc
        return root

    def find(self, root, name="f.ini"):
        ini = self.tmp / name
        rc, out = run("find", "--rpcs3", str(root), "--ini", str(ini), "--json", "--redacted")
        return rc, read_ini(ini), json.loads(out), out

    def test_found(self):
        root = self.tree()
        rc, ini, js, out = self.find(root)
        self.assertEqual(rc, 0)
        self.assertEqual((ini["found"], ini["title_id"], ini["problems"]), ("1", "BLUS31519", "0"))
        self.assertEqual(Path(ini["disc"]), self.disc)
        self.assertEqual(Path(ini["eboot"]), self.disc / "PS3_GAME" / "USRDIR" / "EBOOT.elf")
        self.assertEqual(Path(ini["devflash"]), root / "dev_flash")
        self.assertNotIn(str(self.tmp), out)   # the logged form holds no real path
        self.assertIn("<RPCS3>", js["root"])

    def test_eu_disc(self):
        rc, ini, js, _ = self.find(self.tree(serial="BLES02136"))
        self.assertEqual(ini["title_id"], "BLES02136")

    def test_not_found(self):
        rc, ini, js, out = self.find(self.tmp / "nothing")
        self.assertEqual(rc, 1)
        self.assertEqual(ini["found"], "0")
        self.assertEqual(ini["disc"], "")
        self.assertIn("RPCS3", ini["problem1"])
        self.assertNotIn(str(self.tmp), out)

    def test_wrong_hint_is_not_rpcs3(self):
        other = self.tmp / "otherfolder"
        other.mkdir()
        rc, ini, js, _ = self.find(other)
        self.assertEqual(rc, 1)
        self.assertEqual(js["problems"], ["not_rpcs3"])
        self.assertIn("does not look like an RPCS3 install", ini["problem1"])

    def test_no_game_in_rpcs3(self):
        rc, ini, js, _ = self.find(self.tree(games=False))
        self.assertEqual((rc, ini["disc"], ini["eboot"]), (0, "", ""))
        self.assertIn("missing_disc", js["problems"])

    def test_eboot_absent_or_encrypted(self):
        for kind in (None, "self"):
            with self.subTest(kind=kind):
                self.tmp = Path(tempfile.mkdtemp(dir=self._td.name)).resolve()
                rc, ini, js, _ = self.find(self.tree(eboot=kind))
                self.assertEqual(ini["eboot"], "")
                self.assertEqual(ini["disc"], str(self.disc))
                self.assertEqual(js["problems"], ["no_decrypted_eboot"])
                self.assertIn("see RPCS3 and its documentation", ini["problem1"])
                self.assertNotIn("menu", ini["problem1"].lower())

    def test_dev_flash_missing(self):
        rc, ini, js, _ = self.find(self.tree(flash=False))
        self.assertEqual(ini["devflash"], "")
        self.assertEqual(js["problems"], ["missing_flash"])

    def test_no_save_means_no_plan(self):
        root = self.tree()
        self.assertEqual(self.find(root)[1]["saves"], "0")
        rc, out = run("save-plan", "--rpcs3", str(root), "--hdd0", str(self.tmp / "hdd0"), "--ini", str(self.tmp / "s.ini"),
                      "--list", str(self.tmp / "l.txt"), "--json", "--redacted")
        self.assertEqual((rc, json.loads(out)["code"]), (1, "no_save"))
        self.assertFalse((self.tmp / "hdd0").exists())

    def plan(self, root, hdd0):
        rc, out = run("save-plan", "--rpcs3", str(root), "--hdd0", str(hdd0), "--ini", str(self.tmp / "s.ini"),
                      "--list", str(self.tmp / "l.txt"), "--json", "--redacted")
        return rc, read_ini(self.tmp / "s.ini") if rc == 0 else {}, out

    def test_dry_run_writes_nothing_then_copy(self):
        root = self.tree(save=True)
        hdd0 = self.tmp / "hdd0"
        self.assertEqual(self.find(root)[1]["saves"], "1")
        rc, ini, out = self.plan(root, hdd0)
        self.assertEqual((rc, ini["copies"], ini["replaces"], ini["stale"]), (0, "2", "0", "0"))
        self.assertFalse(hdd0.exists())   # a plan is a dry run
        lst = (self.tmp / "l.txt").read_text(encoding="utf-8-sig")
        self.assertIn("copy     GAME.DAT", lst)
        self.assertNotIn(SECRET.decode(), lst + out)
        self.assertNotIn(str(self.tmp), out)
        rc, out = run("save-copy", "--rpcs3", str(root), "--hdd0", str(hdd0), "--plan", ini["plan"], "--json", "--redacted")
        self.assertEqual((rc, json.loads(out)["copied"]), (0, 2))
        dst = hdd0 / "home" / "00000001" / "savedata" / "NPUB31647-B0"
        self.assertEqual((dst / "GAME.DAT").read_bytes(), b"slot")
        self.assertIn(SECRET, (dst / "PARAM.SFO").read_bytes())   # copied as a file, byte for byte
        self.assertNotIn(SECRET.decode(), out)

    def test_dry_run_lists_replace_and_stale(self):
        root = self.tree(save=True)
        dst = self.tmp / "hdd0" / "home" / "00000001" / "savedata" / "NPUB31647-B0"
        w(dst / "GAME.DAT", b"old-slot")
        w(dst / "EXTRA.BIN", b"zz")
        rc, ini, out = self.plan(root, self.tmp / "hdd0")
        self.assertEqual((ini["replaces"], ini["stale"], ini["copies"]), ("1", "1", "2"))
        lst = (self.tmp / "l.txt").read_text(encoding="utf-8-sig")
        self.assertIn("REPLACES GAME.DAT", lst)
        self.assertIn("kept     EXTRA.BIN", lst)
        self.assertEqual((dst / "GAME.DAT").read_bytes(), b"old-slot")   # still untouched after the dry run
        run("save-copy", "--rpcs3", str(root), "--hdd0", str(self.tmp / "hdd0"), "--plan", ini["plan"])
        self.assertEqual((dst / "GAME.DAT").read_bytes(), b"slot")
        self.assertEqual((dst / "EXTRA.BIN").read_bytes(), b"zz")   # nothing deleted

    def test_changed_plan_copies_nothing(self):
        root = self.tree(save=True)
        dst = self.tmp / "hdd0" / "home" / "00000001" / "savedata" / "NPUB31647-B0"
        w(dst / "GAME.DAT", b"old-slot")
        rc, ini, out = self.plan(root, self.tmp / "hdd0")
        w(dst / "NEW.BIN", b"appeared after the list was shown")
        rc, out = run("save-copy", "--rpcs3", str(root), "--hdd0", str(self.tmp / "hdd0"), "--plan", ini["plan"], "--json")
        self.assertEqual((rc, json.loads(out)["code"]), (3, "plan_changed"))
        self.assertEqual((dst / "GAME.DAT").read_bytes(), b"old-slot")
        rc, out = run("save-copy", "--rpcs3", str(root), "--hdd0", str(self.tmp / "hdd0"), "--plan", "", "--json")
        self.assertEqual(rc, 3)
        src = root / "dev_hdd0/home/00000001/savedata/NPUB31647-B0/GAME.DAT"
        src.write_bytes(b"edited")   # same size changes the plan too
        rc, ini2, _ = self.plan(root, self.tmp / "hdd0")
        self.assertNotEqual(ini["plan"], ini2["plan"])

    def test_destination_inside_rpcs3_is_refused(self):
        root = self.tree(save=True)
        rc, ini, out = self.plan(root, root / "dev_hdd0")
        self.assertEqual(rc, 4)
        self.assertEqual(json.loads(out)["code"], "dst_in_rpcs3")
        self.assertFalse((root / "dev_hdd0" / "home" / "00000001" / "savedata" / "NPUB31647-B0" / "NPUB31647-B0").exists())

    def import_fixture(self):
        root = self.tree(save=True)
        src = root / "dev_hdd0/home/00000001/savedata/NPUB31647-B0"
        hdd0 = self.tmp / "hdd0"
        dst = hdd0 / "home/00000001/savedata/NPUB31647-B0"
        w(dst / "GAME.DAT", b"original progress")
        w(dst / "PARAM.SFO", b"original metadata")
        w(dst / "STALE", b"keep me")
        rc, ini, _ = self.plan(root, hdd0)
        self.assertEqual(rc, 0)
        return root, src, hdd0, dst, ini["plan"]

    def copy(self, root, hdd0, plan):
        rc, out = run("save-copy", "--rpcs3", str(root), "--hdd0", str(hdd0), "--plan", plan, "--json", "--redacted")
        return rc, json.loads(out)

    def test_source_copy_failure_preserves_existing_save(self):
        root, src, hdd0, dst, plan = self.import_fixture()
        before = {p.name: p.read_bytes() for p in dst.iterdir()}
        source = {p.name: p.read_bytes() for p in src.iterdir()}

        def fail_after_write(_a, b, _length):
            b.write(b"partial import")
            raise OSError("simulated device failure")

        with mock.patch.object(L.shutil, "copyfileobj", side_effect=fail_after_write):
            rc, out = self.copy(root, hdd0, plan)
        self.assertEqual((rc, out.get("code")), (4, "copy_failed"))
        self.assertEqual({p.name: p.read_bytes() for p in dst.iterdir()}, before)
        self.assertEqual({p.name: p.read_bytes() for p in src.iterdir()}, source)
        self.assertEqual(list(dst.parent.glob(".jsp-save-import-*")), [])

    def test_later_publish_failure_restores_replaced_and_new_files(self):
        root, src, hdd0, dst, _ = self.import_fixture()
        w(src / "A-NEW", b"new file")
        rc, ini, _ = self.plan(root, hdd0)
        self.assertEqual(rc, 0)
        before = {p.name: p.read_bytes() for p in dst.iterdir()}
        replace = os.replace

        def fail_metadata(a, b):
            if Path(a).parent.name == "new" and Path(b).name == "PARAM.SFO":
                raise OSError("simulated publish failure")
            return replace(a, b)

        with mock.patch.object(W.os, "replace", side_effect=fail_metadata):
            rc, out = self.copy(root, hdd0, ini["plan"])
        self.assertEqual((rc, out.get("code")), (4, "copy_failed"))
        self.assertEqual({p.name: p.read_bytes() for p in dst.iterdir()}, before)
        self.assertEqual(list(dst.parent.glob(".jsp-save-import-*")), [])

    def test_failed_rollback_keeps_original_for_recovery(self):
        root, _src, hdd0, dst, plan = self.import_fixture()
        before = (dst / "GAME.DAT").read_bytes()
        replace = os.replace

        def fail_publish_and_rollback(a, b):
            if Path(a).parent.name in ("new", "old"):
                raise OSError("simulated unavailable destination")
            return replace(a, b)

        with mock.patch.object(W.os, "replace", side_effect=fail_publish_and_rollback):
            rc, out = self.copy(root, hdd0, plan)
        self.assertEqual((rc, out.get("code")), (4, "rollback_failed"))
        stages = list(dst.parent.glob(".jsp-save-import-*"))
        self.assertEqual(len(stages), 1)
        self.assertEqual((stages[0] / "old/GAME.DAT").read_bytes(), before)
        self.assertIn(stages[0].name, out["recovery"])
        self.assertNotIn(str(self.tmp), out["recovery"])

    def test_interruption_preserves_recovery_and_propagates(self):
        for exception in (KeyboardInterrupt, SystemExit, RuntimeError):
            with self.subTest(exception=exception.__name__):
                root, _src, hdd0, dst, plan = self.import_fixture()
                original = (dst / "GAME.DAT").read_bytes()
                replace = os.replace

                def interrupt_publish(a, b):
                    if Path(a).parent.name == "new":
                        raise exception("simulated interruption")
                    return replace(a, b)

                with mock.patch.object(W.os, "replace", side_effect=interrupt_publish):
                    with self.assertRaises(exception):
                        self.copy(root, hdd0, plan)
                stages = list(dst.parent.glob(".jsp-save-import-*"))
                self.assertEqual(len(stages), 1)
                self.assertEqual((stages[0] / "old/GAME.DAT").read_bytes(), original)
                # Each subcase has a distinct filesystem, including recovery data.
                self.tmp = Path(tempfile.mkdtemp(dir=self._td.name)).resolve()

    def test_interrupted_rollback_preserves_recovery_and_propagates(self):
        root, _src, hdd0, dst, plan = self.import_fixture()
        original = (dst / "GAME.DAT").read_bytes()
        replace = os.replace

        def interrupt_rollback(a, b):
            if Path(a).parent.name == "new":
                raise OSError("simulated publish failure")
            if Path(a).parent.name == "old":
                raise KeyboardInterrupt("simulated rollback interruption")
            return replace(a, b)

        with mock.patch.object(W.os, "replace", side_effect=interrupt_rollback):
            with self.assertRaises(KeyboardInterrupt):
                self.copy(root, hdd0, plan)
        stages = list(dst.parent.glob(".jsp-save-import-*"))
        self.assertEqual(len(stages), 1)
        self.assertEqual((stages[0] / "old/GAME.DAT").read_bytes(), original)

    def test_destination_symlink_is_refused_without_writing_target(self):
        root = self.tree(save=True)
        hdd0 = self.tmp / "hdd0"
        dst = hdd0 / "home/00000001/savedata/NPUB31647-B0"
        dst.mkdir(parents=True)
        outside = w(self.tmp / "outside", b"untouched")
        try:
            (dst / "GAME.DAT").symlink_to(outside)
        except OSError:
            self.skipTest("creating a symbolic link is not permitted")
        rc, _ini, out = self.plan(root, hdd0)
        self.assertEqual((rc, json.loads(out).get("code")), (4, "bad_dst"))
        self.assertEqual(outside.read_bytes(), b"untouched")

    def test_plain_save_without_pfd_copies_bytes_unchanged(self):
        root = self.tree(save=True)
        src = root / "dev_hdd0/home/00000001/savedata/NPUB31647-B0"
        (src / "GAME.DAT").rename(src / "SYS-DATA")
        hdd0 = self.tmp / "hdd0"
        rc, ini, _ = self.plan(root, hdd0)
        self.assertEqual(rc, 0)
        rc, _out = self.copy(root, hdd0, ini["plan"])
        self.assertEqual(rc, 0)
        dst = hdd0 / "home/00000001/savedata/NPUB31647-B0"
        self.assertEqual({p.name: p.read_bytes() for p in dst.iterdir()},
                         {p.name: p.read_bytes() for p in src.iterdir()})
        self.assertFalse((dst / "PARAM.PFD").exists())

    def test_destination_link_created_after_plan_is_refused(self):
        root, _src, hdd0, dst, plan = self.import_fixture()
        outside = w(self.tmp / "outside", b"untouched")
        (dst / "GAME.DAT").unlink()
        try:
            (dst / "GAME.DAT").symlink_to(outside)
        except OSError:
            self.skipTest("creating a symbolic link is not permitted")
        rc, out = self.copy(root, hdd0, plan)
        self.assertEqual((rc, out.get("code")), (4, "bad_dst"))
        self.assertEqual(outside.read_bytes(), b"untouched")

    def test_destination_alias_into_rpcs3_is_refused(self):
        root = self.tree(save=True)
        home = root / "dev_hdd0/home"
        (home / "00000001").rename(home / "00000002")
        alias = self.tmp / "alias"
        try:
            alias.symlink_to(root / "dev_hdd0", target_is_directory=True)
        except OSError:
            self.skipTest("creating a symbolic link is not permitted")
        rc, _ini, out = self.plan(root, alias)
        self.assertEqual((rc, json.loads(out).get("code")), (4, "dst_in_rpcs3"))

    def test_other_user_save_is_taken_when_001_has_none(self):
        root = self.tree()
        s = root / "dev_hdd0" / "home" / "00000002" / "savedata" / "NPUB31647-B0"
        w(s / "GAME.DAT", b"u2")
        rc, ini, out = self.plan(root, self.tmp / "hdd0")
        self.assertEqual(ini["copies"], "1")
        run("save-copy", "--rpcs3", str(root), "--hdd0", str(self.tmp / "hdd0"), "--plan", ini["plan"])
        self.assertTrue((self.tmp / "hdd0/home/00000001/savedata/NPUB31647-B0/GAME.DAT").is_file())


if __name__ == "__main__":
    unittest.main()
