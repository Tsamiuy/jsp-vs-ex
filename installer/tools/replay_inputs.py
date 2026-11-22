"""ENHANCEMENT (replays): read the inputs of a rollback replay and say what they mean for rollback.

    python3 installer/tools/replay_inputs.py REPLAY.gkrp|DUMP.csv [--dump-exe replay_dump] timeline [--player N] [--from S] [--to S]
    python3 installer/tools/replay_inputs.py REPLAY ... summary
    python3 installer/tools/replay_inputs.py REPLAY ... rollback [--ping 20,40,60,80,100,150] [--delay 0,1,2] [--remote 1] [--costs restore,rerun,save]

A .gkrp file is decoded with replay_dump (installer/tools/replay_dump.cpp, built by net_session_test.sh); a .csv is its output.

timeline   what each player did, in game time (30 ticks a second): presses and releases by name, how long each was held, and the stick
           directions (8 directions, dead zone 32 of 128) as they change.
summary    per player: presses per button, actions per minute, and how often the input changes from one tick to the next. That change rate is
           the misprediction rate of rollback's usual guess ("the same as last tick"), the number that decides how often a match rolls back.
           Shown for the raw pad (what the session compares today: any stick byte that moves counts) and for "meaningful" input (buttons plus the
           8-way stick direction), the rate a quantised stick would give.
rollback   a model of a match with these inputs: for each one-way latency and input delay, how late the remote inputs arrive (ticks), how many
           rollbacks per second that causes, how deep they are and, with per-operation costs (ms, measured with the in-game bench), how much
           re-simulation work per second it adds. The model: the remote input of tick t arrives k = max(0, ceil(latency / 33.3 ms) - delay) ticks
           late; each tick whose input differs from the previous one triggers one rollback of k ticks (changes closer together than k merge into one).
           It assumes steady latency (no jitter) and the "same as last tick" prediction GekkoNet uses.
"""
import argparse, csv, io, math, os, pathlib, subprocess, sys

TICK_S = 1.0 / 30.0
# cellPad masks: buttons = DIGITAL2 << 8 | DIGITAL1 (the same masks PAD_SCRIPT uses)
BUTTONS = [(0x0001, "Select"), (0x0002, "L3"), (0x0004, "R3"), (0x0008, "Start"), (0x0010, "Up"), (0x0020, "Right"), (0x0040, "Down"), (0x0080, "Left"),
           (0x0100, "L2"), (0x0200, "R2"), (0x0400, "L1"), (0x0800, "R1"), (0x1000, "Triangle"), (0x2000, "Circle"), (0x4000, "Cross"), (0x8000, "Square")]
DEAD = 32
DIRS = ["right", "up-right", "up", "up-left", "left", "down-left", "down", "down-right"]


def stick_dir(x, y):
    dx, dy = x - 128, 128 - y   # screen up = smaller y
    if dx * dx + dy * dy < DEAD * DEAD: return "neutral"
    a = math.degrees(math.atan2(dy, dx)) % 360
    return DIRS[int(((a + 22.5) % 360) // 45)]


def load(path, dump_exe):
    p = pathlib.Path(path)
    if p.suffix.lower() == ".gkrp":
        if not dump_exe: sys.exit("a .gkrp file needs --dump-exe (replay_dump, built by installer/tools/net_session_test.sh)")
        r = subprocess.run([dump_exe, str(p)], capture_output=True, text=True)
        if r.returncode: sys.exit(f"replay_dump failed ({r.returncode}): {r.stderr.strip()}")
        text = r.stdout
    else:
        text = p.read_text()
    meta, rows = {}, []
    body = []
    for line in text.splitlines():
        if line.startswith("# "):
            k, _, v = line[2:].partition(" "); meta[k] = v
        elif line.strip(): body.append(line)
    rd = csv.DictReader(io.StringIO("\n".join(body)))
    n = int(meta.get("players", "0"))
    for r in rd:
        rows.append([{"b": int(r[f"p{i}_buttons"]), "lx": int(r[f"p{i}_lx"]), "ly": int(r[f"p{i}_ly"]), "rx": int(r[f"p{i}_rx"]), "ry": int(r[f"p{i}_ry"]),
                      "c": int(r[f"p{i}_connected"])} for i in range(n)])
    return meta, rows


def names(mask):
    return [n for m, n in BUTTONS if mask & m]


def meaningful(p):
    return (p["b"], stick_dir(p["lx"], p["ly"]), stick_dir(p["rx"], p["ry"]))


def raw(p):
    return (p["b"], p["lx"], p["ly"], p["rx"], p["ry"], p["c"])


def cmd_timeline(meta, rows, a):
    players = range(len(rows[0]) if rows else 0) if a.player is None else [a.player]
    t0, t1 = int((a.start or 0) / TICK_S), int(a.end / TICK_S) if a.end else len(rows)
    for pl in players:
        print(f"== player {pl + 1} (slot {pl})")
        held = {}; prev = None
        for t in range(len(rows)):
            p = rows[t][pl]
            if prev is None: prev = {"b": 0, "lx": 128, "ly": 128, "rx": 128, "ry": 128, "c": p["c"]}
            show = t0 <= t < t1
            down, up = p["b"] & ~prev["b"], prev["b"] & ~p["b"]
            for n in names(down): held[n] = t
            out = []
            out += [f"+{n}" for n in names(down)]
            for n in names(up):
                d = (t - held.pop(n, t)) * TICK_S
                out.append(f"-{n} ({d:.2f}s)")
            for side, xk, yk in (("L", "lx", "ly"), ("R", "rx", "ry")):
                a0, a1 = stick_dir(prev[xk], prev[yk]), stick_dir(p[xk], p[yk])
                if a0 != a1: out.append(f"{side}-stick {a1}")
            if out and show: print(f"  {t * TICK_S:7.2f}s  tick {t + 1:5d}  " + "  ".join(out))
            prev = p


def changes(rows, pl, key):
    c = []
    for t in range(1, len(rows)):
        if key(rows[t][pl]) != key(rows[t - 1][pl]): c.append(t)
    return c


def cmd_summary(meta, rows, a):
    secs = len(rows) * TICK_S
    print(f"replay: {len(rows)} ticks = {secs:.1f} s, {meta.get('players')} players, {meta.get('file_bytes', '?')} bytes"
          + (f", recorded by the {meta['jspr_recorded_by']} of build {meta['jspr_build']}" if "jspr_build" in meta else ""))
    for pl in range(len(rows[0]) if rows else 0):
        presses = {}
        for t in range(1, len(rows)):
            for n in names(rows[t][pl]["b"] & ~rows[t - 1][pl]["b"]): presses[n] = presses.get(n, 0) + 1
        total = sum(presses.values())
        rc, mc = changes(rows, pl, raw), changes(rows, pl, meaningful)
        print(f"player {pl + 1}: {total} presses ({total / secs * 60:.0f} per minute)"
              + (": " + ", ".join(f"{n} {c}" for n, c in sorted(presses.items(), key=lambda x: -x[1])) if presses else ""))
        print(f"  input changes: raw {len(rc)} ({len(rc) / secs:.2f}/s, {100 * len(rc) / max(1, len(rows)):.1f}% of ticks), "
              f"meaningful {len(mc)} ({len(mc) / secs:.2f}/s, {100 * len(mc) / max(1, len(rows)):.1f}% of ticks)"
              + (f"; {len(rc) - len(mc)} raw changes are stick movement inside one direction" if len(rc) > len(mc) else ""))


def rollback_events(rows, remotes, key, k):
    """Ticks at which a rollback starts, in a steady-latency model with lateness k ticks (changes within k ticks of a pending one merge)."""
    ch = sorted({t for pl in remotes for t in changes(rows, pl, key)})
    ev, pending_until = [], -1
    for t in ch:
        arrive = t + k
        if arrive <= pending_until: continue   # the rollback for an earlier change in the same window re-runs this tick too
        ev.append(t); pending_until = arrive
    return ev


def cmd_rollback(meta, rows, a):
    secs = len(rows) * TICK_S
    remotes = [int(x) - 1 for x in a.remote.split(",")]
    costs = [float(x) for x in a.costs.split(",")] if a.costs else None   # restore, re-run tick, save (ms)
    print(f"model: {secs:.1f} s of inputs, remote player(s) {a.remote}, steady latency, prediction = the same input as last tick")
    if costs: print(f"costs per operation (ms): restore {costs[0]}, re-run tick {costs[1]}, save {costs[2]}")
    hdr = f"{'one-way ms':>10} {'delay':>5} {'late ticks':>10} | {'raw: rollbacks/s':>16} {'re-run ticks/s':>14}" + (f" {'extra ms/s':>10}" if costs else "") \
        + f" | {'meaningful: rollbacks/s':>24} {'re-run ticks/s':>14}" + (f" {'extra ms/s':>10}" if costs else "")
    print(hdr); print("-" * len(hdr))
    for ping in [int(x) for x in a.ping.split(",")]:
        for d in [int(x) for x in a.delay.split(",")]:
            k = max(0, math.ceil(ping / (TICK_S * 1000)) - d)
            line = f"{ping:>10} {d:>5} {k:>10} |"
            for key in (raw, meaningful):
                ev = rollback_events(rows, remotes, key, k) if k > 0 else []
                rps, rr = len(ev) / secs, len(ev) * k / secs
                line += f" {rps:>16.2f} {rr:>14.2f}" if key is raw else f" {rps:>24.2f} {rr:>14.2f}"
                if costs: line += f" {rps * (costs[0] + costs[2] * k) + rr * costs[1]:>10.0f}"
                if key is raw: line += " |"
            print(line)
    print("delay = input delay in ticks (each tick is 33 ms of added input lag); late ticks = how far behind the remote input arrives = rollback depth")
    if costs: print("extra ms/s: work per second of play added by rollbacks (1000 = one full second of CPU per second: the game could not keep up)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("replay"); ap.add_argument("--dump-exe")
    sub = ap.add_subparsers(dest="cmd", required=True)
    t = sub.add_parser("timeline"); t.add_argument("--player", type=lambda s: int(s) - 1); t.add_argument("--from", dest="start", type=float); t.add_argument("--to", dest="end", type=float)
    sub.add_parser("summary")
    r = sub.add_parser("rollback"); r.add_argument("--ping", default="10,20,33,50,67,100,150"); r.add_argument("--delay", default="0,1,2")
    r.add_argument("--remote", default="2", help="1-based player numbers whose inputs come over the network (default 2)"); r.add_argument("--costs", help="restore,rerun,save in ms (from the bench)")
    a = ap.parse_args()
    meta, rows = load(a.replay, a.dump_exe)
    if not rows: sys.exit("no ticks in the replay")
    {"timeline": cmd_timeline, "summary": cmd_summary, "rollback": cmd_rollback}[a.cmd](meta, rows, a)


if __name__ == "__main__":
    sys.exit(main())
