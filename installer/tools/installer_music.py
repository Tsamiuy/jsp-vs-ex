#!/usr/bin/env python3
"""Renders the installer's opt-in music: the six formulas in installer/music/ to 8-bit PCM WAV files.

    python installer/tools/installer_music.py [--out DIR] [--seconds 45] [--only N]   # default out: build/installer/music

Pure Python (no Node, no extra packages), so it runs on the Windows CI and release runner. The WAV files are made at build
time and never committed. The formulas are bytebeat/floatbeat expressions of the sample counter t (installer/music/track*.js,
kept as the originals; installer/music/tracks.tsv has title, author, mode, rate and source of each). Each is ported here by
hand with JavaScript's number rules (32-bit integer operators, `%` as fmod, round half up), checked against the originals run
in Node (V8) with the same random numbers.
ENHANCEMENT: the random() calls of the formulas (noise, drums, a random tempo) use a fixed-seed generator (mulberry32,
seed = track number), so the same inputs give the same bytes; a browser player would differ on each run.
bytebeat: the value & 255 is the sample; floatbeat: -1..1 mapped to 0..255. A 20 ms fade-in and a 1 s fade-out towards
silence keep PlaySound's loop from clicking at the loop point.
"""
import argparse
import concurrent.futures
import math
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
MUSIC = ROOT / "installer" / "music"
INF, NAN = float("inf"), float("nan")
PI = math.pi


# ---- JavaScript number rules ----
def i32(x):
    """ToInt32: NaN and infinities give 0, others truncate and wrap."""
    if x != x or x == INF or x == -INF:
        return 0
    n = int(x) & 0xFFFFFFFF
    return n - 0x100000000 if n & 0x80000000 else n


def jround(x):
    return math.floor(x + 0.5)


def fmod(a, b):
    try:
        return math.fmod(a, b)
    except ValueError:
        return NAN


def pw(a, b):
    try:
        return math.pow(a, b)
    except ValueError:
        return NAN
    except OverflowError:
        return INF


def sn(x):
    try:
        return math.sin(x)
    except ValueError:
        return NAN


def cbrt(x):
    if hasattr(math, "cbrt"):
        return math.cbrt(x)
    return math.copysign(abs(x) ** (1.0 / 3.0), x)


def mulberry32(seed):
    s = seed & 0xFFFFFFFF
    mask = 0xFFFFFFFF

    def rnd():
        nonlocal s
        s = (s + 0x6D2B79F5) & mask
        t = ((s ^ (s >> 15)) * (1 | s)) & mask
        t = ((t + (((t ^ (t >> 7)) * (61 | t)) & mask)) & mask) ^ t
        return ((t ^ (t >> 14)) & mask) / 4294967296.0
    return rnd


# ---- the six formulas; each returns f(t) giving a number or a (left, right) pair ----
def track1(rnd):
    """true phonk indeed (eSlashMachine), floatbeat, stereo; `a` is one filter state shared by both channels."""
    st = [0.0]
    D1 = [int(c) for c in "25259474"]
    D2 = [int(c) for c in "4142"]
    D3 = [int(c) for c in "2570"]
    D4 = [int(c) for c in "0001001000101000"]

    def f(t):
        r = t / 48e3 / 60 * 16384 * 135
        ri = i32(r)
        m = 16384 if ri & 1048576 else 32768
        g = 1 if i32(r / 4096) & 3 else fmod(r / 4096, 1)
        g12 = pw(g, 12)
        d4 = D4[(ri >> 12) & 15]
        e1, e2, e3 = D1[7 & (ri >> 13)], D2[3 & (ri >> 13)], D3[3 & (ri >> 17)]
        ph = r / 4096
        out = []
        for lr in (.995, 1.005):
            p = t * pw(2, e1 / 12) / 3 * lr
            a = (st[0] * 24 + (fmod(p, 88) + fmod(p, 59) * 6 / 4 - 64) / pw(fmod(r / e2, 4096) + 128, .7) * 64) / 32
            st[0] = a
            t1 = (a / 4 + (i32(fmod(t / 4 * lr * pw(2, e3 / 12) / 3, 88)) & 255) / 128 - .5) * g12
            t2 = (max(-1, min(1, sn(200 * lr * pw(1e-9, fmod(r / 16384, .25))) * 4))
                  + sn(pw(float(i32(t / 2 * lr) | ((ri >> 12) & 7)), 3)) * (fmod(ph, 1) - 1)) * d4 / 2
            t3 = max(-1, min(1, sn(70 * lr * pw(1e-12, fmod(r / m, 1))) * 8
                             + sn(pw(float(i32(t / 2 * lr) | i32(fmod(r / 8192, 8))), 4)) * pw(fmod(r / m, 1) - 1, 4)))
            out.append(math.tanh(math.atan(t1 + t2 + t3)))
        return out[0], out[1]
    return f


def track2(rnd):
    """difficult level (computerman113), bytebeat."""
    def f(t):
        t = t / 6
        ti = i32(t)
        s1 = 3 & (i32(-t) >> (2 if ti & 4096 else 16))
        a = (i32(t * (7 if ti & 16384 else 5) * (5 - (3 & (ti >> 8)))) >> s1) | (ti >> ((3 if ti & 4096 else 4) if ti & 16384 else 3))
        b = sn(200000 / (fmod(t, 4096) + 900)) * 64
        c = (i32(rnd() * 64) & (i32(-t) >> 5)) if ti & 4096 else 0
        o = fmod(a, 256) * .5 + b + c
        return max(min(o + 32, 255), 0)
    return f


def track3(rnd):
    """Changsha wip (D3nschot), bytebeat; a 10035-sample feedback delay."""
    ARR = [-1, None, -1, None, 4, 6, None, 11, None, -1, None, -1, 4, 6, None, 11,
           None, -1, None, -1, 4, 6, 14, 4, 13, 14, 13, 11, 4, 2, 9, 4]
    st = {}
    dt = (2 ** 14) / (600 / 44100 * 120)

    def f(t):
        av = t * 600 / 44100 * 120
        avi = i32(av)
        e = ARR[(avi >> 13) & 31]
        bv = NAN if e is None else t * pw(2, e / 12) / 2 * 3
        m = (i32(bv) & 128) * 1.99 * (1 - fmod((avi >> 6) / 128, 1) / 3 * 2)
        if t == 0:
            st["l"] = i32(dt)
            st["a"] = [1.0] * st["l"]
        a, k = st["a"], t % st["l"]
        c = (i32(m) & 255) % 256 + a[k]
        a[k] = c * .5
        return c * .5
    return f


def track4(rnd):
    """a new adventure (Demari Smith), bytebeat."""
    M = [int(c) for c in "6363896363638919"]
    B = [int(c) for c in "444411111"]
    S = [int(c) for c in "3333555433331111"]

    def f(t):
        ti = i32(t)
        melody = t / 2 * M[(ti >> 13) % 16]
        d = t & 16383
        kick = ((i32(3e3 / d) if d else 0) & 1) * 100
        snare = i32(t * rnd()) & 63 | (ti >> 6) & (63 if (t & 16383) > 8192 else 0)
        bass = abs(sn(t / 2 * B[(ti >> 13) % 8] / 300) * 100)
        synth = t / 2 * S[(ti >> 13) % 16]
        return ((fmod(melody * 0.9, 90) + fmod(melody, 80)) / 3) + ((fmod(synth * 0.8, 80) + fmod(synth, 80)) / 5.5) \
            + kick + (snare / 2) + (bass / 2)
    return f


def track5(rnd):
    """generator (eSlashMachine), floatbeat."""
    st = {"a": 0.0}
    NOTE = [0, 3, 5, 7, 10, 12, 15, 19]

    def f(t):
        if t == 0:
            st["a"] = 0.0
            st["S"] = max(40, rnd() * 200)
            st["l"] = 256 * jround(st["S"])
            st["n"] = [0.0] * st["l"]
        S, l, n = st["S"], st["l"], st["n"]
        X = t / S / 256 + abs(sn(t * PI / 8388608 * S)) * 8
        Xi = i32(X)
        x = Xi & 7
        a = st["a"] + PI / 24e3 * 320 * pw(2, NOTE[x] / 12)
        st["a"] = a
        b = t * PI / 48e3 * 320 / 4
        k = t % l
        A = (sn(sn(sn(a) + a * 2)) / 8 * min(.01, fmod(X, 1)) * (1 - fmod(X, 1)) * 100
             + sn(sn(b) * 2 + b * 2) / 4 * min(.01, fmod(X / 8, 1)) * (1 - fmod(X / 8, 1)) * 120
             + (rnd() - .5) * (1 if (Xi & 7) == 0 else 0) / 8 * sn(X * PI)
             + n[k] / 1.2)
        n[k] = A
        return (A + sn(pw(1e-3, fmod(X / 8, 1)) * PI * 32) / 2) / 3
    return f


def track6(rnd):
    """dystopia (eSlashMachine), floatbeat, stereo; the delay buffer is reset by each channel call at t = 0, as in the original."""
    st = {}
    DG = ["037", "039"]
    N = [int(c) for c in "1001010110010011"]

    def f(T):
        t = T / 1.25
        Tn = t
        t -= i32(t / 4) & 2048
        ti = i32(t)
        out = []
        for lr in (.995, 1.005):
            if t == 0:
                st["l"] = 12288
                st["a"] = [0.0] * 12288
            a, l = st["a"], st["l"]
            n = N[(ti >> 13) & 15]
            ph = fmod(t / 8192, 1)
            d = ((sn(T * lr * PI / 512) * 2 + sn(cbrt(fmod(t, 8192)) * PI * 2 * lr) * 4 * pw(1 - ph, 8)) * n / 2
                 + (rnd() - rnd()) * pw(1 - ph, 5) * min(.05, ph) / .05 * (i32(t / 8192) & 1)) / 4
            u = (T / 2 * lr + sn(T / 8192) / 2) * pw(2, int(DG[(ti >> 18) & 1][T % 3]) / 12)
            q = (i32(u) & 127) + Tn / 2048
            p = ((i32(q) & 128) / 128 - .5) * abs(sn(t * PI / 2 ** 18)) + a[T % l]
            a[T % l] = p / 1.5
            out.append(d * 2 + p / 2 * pw(ph if n == 1 else 1, 2))
        return out[0], out[1]
    return f


PORTS = {f"track{i}.js": fn for i, fn in enumerate((track1, track2, track3, track4, track5, track6), 1)}


def read_tracks():
    rows = []
    for line in (MUSIC / "tracks.tsv").read_text(encoding="utf-8").splitlines():
        if line.strip() and not line.startswith("#"):
            c = line.split("\t")
            rows.append({"file": c[0], "mode": c[1], "rate": int(c[2]), "title": c[3], "author": c[4], "source": c[5]})
    return rows


def render(index, row, seconds):
    """WAV bytes for track number `index` (1-based)."""
    mode, rate = row["mode"], row["rate"]
    f = PORTS[row["file"]](mulberry32(index))
    n = jround(rate * seconds)
    first = f(0)
    ch = 2 if isinstance(first, tuple) else 1
    if mode == "bytebeat":
        def to_byte(v):
            return (i32(v) & 255) if v == v and abs(v) != INF else 128
    else:
        def to_byte(v):
            return jround((max(-1, min(1, v)) + 1) * 127.5) if v == v and abs(v) != INF else 128
    pcm = bytearray(n * ch)
    for i in range(n):
        v = first if i == 0 else f(i)
        if ch == 2:
            pcm[2 * i], pcm[2 * i + 1] = to_byte(v[0]), to_byte(v[1])
        else:
            pcm[i] = to_byte(v[0] if isinstance(v, tuple) else v)
    fade_in, fade_out = jround(rate * 0.02), jround(rate * 1)
    for i in list(range(min(fade_in, n))) + list(range(max(0, n - fade_out), n)):
        g = min(1, i / fade_in, (n - 1 - i) / fade_out)
        if g < 1:
            for c in range(ch):
                pcm[i * ch + c] = jround(128 + (pcm[i * ch + c] - 128) * g)
    head = (b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE" + b"fmt " + struct.pack("<IHHIIHH", 16, 1, ch, rate, rate * ch, ch, 8)
            + b"data" + struct.pack("<I", len(pcm)))
    return head + bytes(pcm)


def _job(args):
    index, row, seconds = args
    return index, render(index, row, seconds)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", type=pathlib.Path, default=ROOT / "build" / "installer" / "music")
    ap.add_argument("--seconds", type=float, default=45)
    ap.add_argument("--only", type=int, help="render only this track number (1-based)")
    a = ap.parse_args()
    rows = read_tracks()
    missing = [r["file"] for r in rows if r["file"] not in PORTS]
    if missing:
        sys.exit(f"installer_music: no Python port for {missing}")
    a.out.mkdir(parents=True, exist_ok=True)
    jobs = [(i, r, a.seconds) for i, r in enumerate(rows, 1) if a.only in (None, i)]
    with concurrent.futures.ProcessPoolExecutor(max_workers=min(4, len(jobs))) as ex:
        for i, wav in ex.map(_job, jobs):
            (a.out / f"music-{i}.wav").write_bytes(wav)
            print(f"{a.out / f'music-{i}.wav'}: {len(wav)} bytes")
    print(f"installer_music: {len(jobs)} tracks in {a.out}")


if __name__ == "__main__":
    main()
