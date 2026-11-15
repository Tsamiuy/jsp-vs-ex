#!/usr/bin/env python3
"""Check host-runner argv construction without launching tests or compilers."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("host_tests", Path(__file__).with_name("run_host_tests.py"))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class Arguments(unittest.TestCase):
    def command(self, *args, toolkit="/tmp/patched toolkit"):
        return runner.command(["installer/tools/lod_hooks_test.py", *args],
                              {"toolkit": toolkit}, "/tmp/test scratch")

    def test_toolkit_path_remains_one_argument(self):
        argv, why = self.command("--lifter", "{toolkit}/tools/ppu_lifter.py", "--native")
        self.assertEqual(why, "")
        self.assertEqual(argv[-3:], ["--lifter", "/tmp/patched toolkit/tools/ppu_lifter.py", "--native"])
        argv, why = self.command("--loader", "{toolkit}/runtime/ppu/ppu_loader.cpp")
        self.assertEqual(why, "")
        self.assertEqual(argv[-1], "/tmp/patched toolkit/runtime/ppu/ppu_loader.cpp")

    def test_exact_and_mixed_placeholders(self):
        argv, why = self.command("{scratch}", "{toolkit}", "{scratch}/{toolkit}")
        self.assertEqual(why, "")
        self.assertEqual(argv[-3:], ["/tmp/test scratch", "/tmp/patched toolkit",
                                    "/tmp/test scratch//tmp/patched toolkit"])

    def test_missing_toolkit_refuses_launch(self):
        for value in (None, ""):
            for arg in ("{toolkit}", "{toolkit}/tools/ppu_lifter.py"):
                with self.subTest(value=value, arg=arg):
                    self.assertEqual(self.command(arg, toolkit=value), (None, "needs --toolkit"))

    def test_other_missing_declared_values(self):
        for key in ("font", "fake_tool", "launcher"):
            with self.subTest(key=key):
                self.assertEqual(self.command("{" + key + "}/file"),
                                 (None, "needs --" + key.replace("_", "-")))

    def test_literal_arguments_are_preserved(self):
        args = ["plain", "with spaces", "{literal}/file", "$(echo unsafe)", "{a,b}"]
        argv, why = self.command(*args)
        self.assertEqual(why, "")
        self.assertEqual(argv[-len(args):], args)

    def test_unknown_exact_placeholder_still_refuses_launch(self):
        self.assertEqual(self.command("{unknown}"), (None, "needs --unknown"))


if __name__ == "__main__":
    unittest.main()
