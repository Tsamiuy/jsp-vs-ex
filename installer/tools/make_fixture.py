#!/usr/bin/env python3
"""Fixture for wizard and self-tests: a disc folder with a synthetic PS3_GAME/PARAM.SFO (TITLE_ID only) and a 64-byte stand-in
for a decrypted ELF header. No game data. Usage: make_fixture.py DIR [TITLE_ID]  -> DIR/disc, DIR/EBOOT.elf, DIR/EBOOT.BIN"""
import pathlib
import struct
import sys


def param_sfo(title_id):
    keys, data = b"TITLE_ID\0", title_id.encode() + b"\0" * 4   # 16 data bytes
    kt = 20 + 16
    dt = kt + len(keys) + (-len(keys) % 4)
    head = b"\0PSF" + struct.pack("<IIII", 0x101, kt, dt, 1)
    entry = struct.pack("<HHIII", 0, 0x204, len(title_id) + 1, 16, 0)
    return head + entry + keys + b"\0" * (-len(keys) % 4) + data


def main():
    d = pathlib.Path(sys.argv[1])
    tid = sys.argv[2] if len(sys.argv) > 2 else "BLUS31519"
    (d / "disc/PS3_GAME/USRDIR").mkdir(parents=True, exist_ok=True)
    (d / "disc/PS3_GAME/PARAM.SFO").write_bytes(param_sfo(tid))
    (d / "EBOOT.elf").write_bytes(b"\x7fELF\x02\x02\x01\0" + b"\0" * 8 + b"\0\x02\0\x15" + b"\0" * 44)
    (d / "EBOOT.BIN").write_bytes(b"SCE\0" + b"\0" * 60)
    (d / "dev_flash/sys").mkdir(parents=True, exist_ok=True)   # the wizard's dev_flash page only needs a sys folder


if __name__ == "__main__":
    main()
