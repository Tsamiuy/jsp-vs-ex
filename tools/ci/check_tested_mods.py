#!/usr/bin/env python3
"""Check mods/tested.json (the list of mods a maintainer has played). Standard library only.

    python3 tools/ci/check_tested_mods.py [FILE]

Schema 1: {"schema": 1, "mods": [{"id", "name", "version", "sha256", "tested_on", "content", "notes"}]}
  id         the mod id, [a-z0-9-], 1-40 characters
  name       the mod's title, a non-empty string
  version    the mod's version, 1-40 characters, no spaces
  sha256     the canonical mod hash (docs/MODS.md), 64 lowercase hex digits
  tested_on  the port release it was played on, X.Y.Z
  content    exactly "own-work": the author's own work. The list never holds a mod with game-derived or third-party content
  notes      a string, may be empty
An id and version appear once; a hash appears once. Entries are sorted by id, then version.
"""
import json
import pathlib
import re
import sys

KEYS = {"id", "name", "version", "sha256", "tested_on", "content", "notes"}


def check(doc):
    errs = []
    if not isinstance(doc, dict) or doc.get("schema") != 1 or isinstance(doc.get("schema"), bool):
        return ["schema must be 1"]
    if set(doc) != {"schema", "mods"}:
        errs.append("top-level keys must be exactly schema and mods")
    mods = doc.get("mods")
    if not isinstance(mods, list):
        return errs + ["mods must be an array"]
    seen_iv, seen_hash, order = set(), set(), []
    for n, m in enumerate(mods, 1):
        where = "entry %d" % n
        if not isinstance(m, dict):
            errs.append(where + ": not an object")
            continue
        if set(m) != KEYS:
            errs.append(where + ": keys must be exactly " + ", ".join(sorted(KEYS)))
            continue
        if not all(isinstance(m[k], str) for k in KEYS):
            errs.append(where + ": every value must be a string")
            continue
        if not re.fullmatch(r"[a-z0-9-]{1,40}", m["id"]):
            errs.append(where + ": bad id")
        if m["content"] != "own-work":
            errs.append(where + ": content must be \"own-work\"")
        if not m["name"].strip():
            errs.append(where + ": empty name")
        if not 1 <= len(m["version"]) <= 40 or re.search(r"[\x00-\x20\x7f]", m["version"]):
            errs.append(where + ": bad version")
        if not re.fullmatch(r"[0-9a-f]{64}", m["sha256"]):
            errs.append(where + ": sha256 must be 64 lowercase hex digits")
        if not re.fullmatch(r"\d+\.\d+\.\d+", m["tested_on"]):
            errs.append(where + ": tested_on must be a release like 0.6.1")
        if (m["id"], m["version"]) in seen_iv:
            errs.append(where + ": id and version listed twice")
        if m["sha256"] in seen_hash:
            errs.append(where + ": hash listed twice")
        seen_iv.add((m["id"], m["version"]))
        seen_hash.add(m["sha256"])
        order.append((m["id"], m["version"]))
    if order != sorted(order):
        errs.append("entries must be sorted by id, then version")
    return errs


def main(argv):
    path = pathlib.Path(argv[1]) if len(argv) > 1 else pathlib.Path(__file__).resolve().parents[2] / "mods" / "tested.json"
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        print("check_tested_mods: %s: %s" % (path, e))
        return 1
    errs = check(doc)
    for e in errs:
        print("check_tested_mods: " + e)
    print("check_tested_mods: %d problem(s), %d mod(s)" % (len(errs), len(doc.get("mods", [])) if isinstance(doc, dict) else 0))
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
