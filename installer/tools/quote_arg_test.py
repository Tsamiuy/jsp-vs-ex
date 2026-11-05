#!/usr/bin/env python3
"""Checks the command-line quoting of installer/jsp.iss (ArgQ). Runs anywhere (plain Python, no Windows needed).

A path that ends in a backslash ("F:\\RPCS3\\emu\\dev_flash\\" from the RPCS3 page, a drive root "F:\\") broke the generated
build-again.cmd when it was written as "PATH": under the Windows argv rules (CommandLineToArgvW, the MSVC CRT, Python on Windows) a
backslash right before a quote escapes the quote, so the following arguments were swallowed. This test holds a model of those rules and of
ArgQ: every value must round-trip, the old form must fail for the trailing-backslash values, and jsp.iss must hold no raw "' + X + '"
path argument any more.

    python quote_arg_test.py
"""
import pathlib
import re
import sys


def argv_parse(cmdline):
    """The documented Windows rules for a command line after the program name: 2n backslashes + quote = n backslashes and a quote
    that toggles; 2n+1 backslashes + quote = n backslashes and a literal quote; backslashes elsewhere are literal."""
    args, cur, inq, have, i, n = [], [], False, False, 0, len(cmdline)
    while i < n:
        c = cmdline[i]
        if c == "\\":
            j = i
            while j < n and cmdline[j] == "\\":
                j += 1
            bs = j - i
            if j < n and cmdline[j] == '"':
                cur.append("\\" * (bs // 2))
                if bs % 2:
                    cur.append('"')
                else:
                    inq = not inq
                have = True
                i = j + 1
            else:
                cur.append("\\" * bs)
                have = True
                i = j
        elif c == '"':
            inq = not inq
            have = True
            i += 1
        elif c in " \t" and not inq:
            if have:
                args.append("".join(cur))
            cur, have = [], False
            i += 1
        else:
            cur.append(c)
            have = True
            i += 1
    if have:
        args.append("".join(cur))
    return args


def quote_arg(s):
    """Same steps as ArgQ in jsp.iss: the backslashes at the end of the value are doubled, then the closing quote."""
    n = len(s) - len(s.rstrip("\\"))
    return '"' + s + s[len(s) - n:] + '"'


def old_quote(s):
    return '"' + s + '"'


CASES = ["F:\\JSP Fan Revival", "F:\\JSP Fan Revival\\", "F:\\", "F:", "C:\\Tools\\a b\\RPCS3\\emu\\dev_flash\\", "C:\\x\\\\",
         "\\\\server\\share\\dir\\", "C:\\a b\\EBOOT.elf", "D:\\Games\\J-Stars (US)\\", "C:\\ジョジョ\\"]


def main():
    bad = 0
    for v in CASES:
        line = "--disc " + quote_arg(v) + " --eboot " + quote_arg("E:\\e.elf") + " --devflash " + quote_arg(v)
        want = ["--disc", v, "--eboot", "E:\\e.elf", "--devflash", v]
        got = argv_parse(line)
        ok = got == want
        old = argv_parse("--disc " + old_quote(v) + " --eboot " + old_quote("E:\\e.elf") + " --devflash " + old_quote(v))
        print(("ok   " if ok else "FAIL ") + repr(v) + ("" if old == want else "   (old form gives %d arguments, not 6)" % len(old)))
        bad += not ok
        if v.endswith("\\") and old == want:
            print("FAIL the control did not fail for " + repr(v))
            bad += 1
    src = (pathlib.Path(__file__).resolve().parent.parent / "jsp.iss").read_text(encoding="utf-8", errors="replace")
    raw = re.findall(r"--\w+ \"'", src)
    print(("ok   " if not raw else "FAIL ") + "jsp.iss has %d raw quoted path arguments" % len(raw))
    bad += bool(raw)
    print(("ok   " if "function ArgQ" in src else "FAIL ") + "jsp.iss defines ArgQ")
    bad += "function ArgQ" not in src
    print("RESULT: " + ("ok" if not bad else "%d failed" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
