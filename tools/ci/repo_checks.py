#!/usr/bin/env python3
"""Repository checks run by CI on every pull request (and locally before a push).

    python3 tools/ci/repo_checks.py                  # tracked files
    python3 tools/ci/repo_checks.py --commits A..B   # also check the commits in A..B

Checks: no game or build files, no large files, no personal paths or e-mail
addresses, no AI tool or vendor names outside AGENTS.md, one Discord invite
(data/nav.json), no merge-conflict markers, Python files parse; with --commits: noreply
identities and no tool trailers. Needs only Python 3 and git.
"""
import argparse
import ast
import json
import re
import subprocess
import sys
import warnings

MAX_BYTES = 2 * 1024 * 1024
BANNED_EXT = {".iso", ".elf", ".self", ".sprx", ".prx", ".pup", ".pkg", ".rap", ".edat", ".sdat",
              ".pam", ".png", ".jpg", ".bmp", ".dds", ".wav", ".zip", ".7z", ".bin", ".bundle", ".hlsl",
              ".dll", ".exe", ".pdb", ".lib", ".obj", ".log"}
# Our own generated art, allowed by exact path (also allowed in .gitignore). Nothing from the game.
# All of it is drawn by tools/site/repo_art.py, which --check renders again and compares.
ART_ALLOW = {"assets/banner-2x.png", "assets/social-preview.png", "assets/logo-mark-512.png", "assets/divider-stripe.png",
             "assets/divider-glow.png"}
ART_ALLOW_RE = re.compile(r"assets/nav/[a-z0-9-]+\.png")   # the README's menu buttons (data/nav.json)
# Third-party SMAA 1x files (MIT, github.com/iryoku/smaa), kept byte for byte with their licence headers, which carry the
# authors' e-mail addresses (the licence asks us to keep them) and include a shader source with a banned extension. They
# reach the repository only inside toolkit patch 2511, so that patch is allowed by its exact path.
SMAA_ALLOW = {"installer/port/patches/2511-d3d12-edge-smoothing-fxaa-smaa.patch"}
BANNED_NAMES = re.compile(r"(^|/)(EBOOT\.[^/]*|PARAM\.SFO)$", re.I)

TEXT_RULES = [
    ("personal path", re.compile(r"[A-Za-z]:[\\/]+Users[\\/]+(?!Public\b|Default\b|%|\$|<)[^\\/\s\"'`]+", re.I)),
    ("personal path", re.compile(r"/home/(?!runner\b|<)[a-z_][a-z0-9_-]*/")),
    ("personal path", re.compile(r"/Users/(?!Shared\b|<)[A-Za-z][\w.-]*/")),
    ("AI tool or vendor name", re.compile(r"\b(Claude|Anthropic|Codex|ChatGPT|OpenAI|Copilot|Gemini)\b")),
    ("merge-conflict marker", re.compile(r"^(<{7}|>{7})( |$)|^={7}$", re.M)),
]
EMAIL = re.compile(r"(?<![\w.+-])[\w.+-]{1,64}@[\w-]{1,63}(\.[\w-]{1,63}){1,8}")
NOREPLY = re.compile(r"(@users\.noreply\.github\.com|^noreply@github\.com)$", re.I)
AI_EXEMPT = {"AGENTS.md"}
SELF = "tools/ci/repo_checks.py"  # holds the patterns themselves
DISCORD = re.compile(r"https?://(?:www\.)?discord(?:app)?\.(?:gg|com/invite)/[A-Za-z0-9-]+")   # as tools/site/check_site.py
TRAILER = re.compile(r"^(Co-Authored-By|Claude-Session|Generated with)\b|claude\.ai/code", re.I | re.M)


def git(*args):
    return subprocess.run(["git", *args], check=True, capture_output=True, text=True).stdout


def check_files(problems):
    # Submodules (mode 160000) are pointers to another repository's commit, not files here: skip them.
    try:   # the one Discord invite (the wide button of data/nav.json); tools/site/test_site.py checks the same in the host tests
        invite = json.load(open("data/nav.json", encoding="utf-8"))["wide"]["href"]
    except (OSError, ValueError, KeyError, TypeError):
        invite = None
    gitlinks = {e.split("\t", 1)[1] for e in git("ls-files", "-s", "-z").split("\0") if e.startswith("160000 ")}
    for path in git("ls-files", "-z").split("\0"):
        if not path or path in gitlinks:
            continue
        low = path.lower()
        ext = low[low.rfind("."):] if "." in low.rsplit("/", 1)[-1] else ""
        if (ext in BANNED_EXT and path not in ART_ALLOW and not ART_ALLOW_RE.fullmatch(path)
             and path not in SMAA_ALLOW) or BANNED_NAMES.search(path):
            problems.append(f"{path}: game, build or binary file type")
            continue
        data = open(path, "rb").read()
        if len(data) > MAX_BYTES:
            problems.append(f"{path}: {len(data)} bytes, over {MAX_BYTES}")
            continue
        if b"\0" in data[:8192]:
            continue
        text = data.decode("utf-8", errors="replace")
        if invite and path != SELF:
            for m in sorted(set(DISCORD.findall(text))):
                if m != invite:
                    problems.append(f"{path}: Discord invite {m} is not the one in data/nav.json")
        for n, line in enumerate(text.splitlines(), 1):
            if path == SELF:
                break
            for name, rx in TEXT_RULES:
                if name == "AI tool or vendor name" and path in AI_EXEMPT:
                    continue
                if rx.search(line):
                    problems.append(f"{path}:{n}: {name}")
            for m in EMAIL.finditer(line):
                if path not in SMAA_ALLOW and not NOREPLY.search(m.group(0)):
                    problems.append(f"{path}:{n}: e-mail address")
        if path.endswith(".py"):
            try:
                with warnings.catch_warnings():
                    warnings.simplefilter("ignore", SyntaxWarning)
                    ast.parse(text, path)
            except SyntaxError as e:
                problems.append(f"{path}:{e.lineno}: Python syntax error: {e.msg}")


def check_commits(rng, problems):
    log = git("log", "--no-merges", "--format=%h%x1f%ae%x1f%ce%x1f%B%x1e", rng)
    for rec in filter(str.strip, log.split("\x1e")):
        sha, author, committer, body = rec.strip().split("\x1f", 3)
        for who, mail in (("author", author), ("committer", committer)):
            if not NOREPLY.search(mail):
                problems.append(f"commit {sha}: {who} e-mail is not a GitHub noreply address")
        if TRAILER.search(body):
            problems.append(f"commit {sha}: tool trailer or session link in the message")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--commits", help="also check the commits in this range, e.g. origin/main..HEAD")
    a = ap.parse_args()
    problems = []
    check_files(problems)
    if a.commits:
        check_commits(a.commits, problems)
    for p in problems:
        print(p)
    print(f"repo_checks: {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
