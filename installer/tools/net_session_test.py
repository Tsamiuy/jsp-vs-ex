#!/usr/bin/env python3
"""Two-process rollback session tests (no game, no GPU): starts net_session_test as host and client over localhost UDP and checks them.
    python3 installer/tools/net_session_test.py --exe PATH [--only NAME] [--ticks N] [--deadline S]
Scenarios and what each must show are in SCENARIOS below; exit 1 on any failure. Build: installer/tools/net_session_test.sh (which also runs it).
The exe may also be the MinGW build when run where Windows programs run (not done by default).
Every wait is bounded by the scenario's deadline (--deadline, 120 s from the host's start, both processes together): a process still running then is killed and the
scenario FAILS as hung, naming who hung and printing both outputs' tails (installer/tools/net_session_driver_test.py tests this with fake processes)."""
import argparse, pathlib, re, subprocess, sys, tempfile, threading, time

# Each process bounds itself (rendezvous and handshake 15 s, the main loop --max-ms, 90 s by default, all from its start); the deadline is that plus a margin.
DEADLINE_S = 120


class Proc:
    """One net_session_test process; a thread reads its output (stdout and stderr) so no read can block the driver."""
    def __init__(self, argv):
        self.p = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
        self.lines, self.port_or_eof = [], threading.Event()
        self.t = threading.Thread(target=self._read, daemon=True)
        self.t.start()

    def _read(self):
        for line in self.p.stdout:
            self.lines.append(line)
            if line.startswith("PORT "): self.port_or_eof.set()
        self.port_or_eof.set()

    def port(self):
        return next((l.split()[1] for l in self.lines if l.startswith("PORT ") and len(l.split()) > 1), None)

    def out(self):
        return "".join(self.lines)


def run(exe, host_args, client_args, tmp, deadline_s=DEADLINE_S):
    """Returns (host rc, host output, client rc, client output, problems); problems names a process that did not exit by the deadline (then killed)."""
    end = time.monotonic() + deadline_s
    left = lambda: max(0.0, end - time.monotonic())
    procs, problems = {}, []
    try:
        h = procs["host"] = Proc([exe, "--role", "host", "--out", str(tmp / "h.log")] + host_args)
        h.port_or_eof.wait(left())
        port = h.port()
        if port is None:
            bare = next((l.strip() for l in h.lines if l.startswith("PORT ")), None)
            problems.append(f"host printed a PORT line without a port number: {bare!r}" if bare is not None else
                            "host printed no PORT line" + (f" and exited ({h.p.poll()})" if h.p.poll() is not None else f" within the {deadline_s} s deadline"))
        else:
            procs["client"] = Proc([exe, "--role", "client", "--peer", f"127.0.0.1:{port}", "--out", str(tmp / "c.log")] + client_args)
        for who, pr in procs.items():
            try: pr.p.wait(timeout=left())
            except subprocess.TimeoutExpired: problems.append(f"hung: the {who} was still running at the {deadline_s} s scenario deadline (killed)")
    finally:
        for pr in procs.values():
            if pr.p.poll() is None: pr.p.kill()
            pr.p.wait()
            pr.t.join(10)
    h, c = procs.get("host"), procs.get("client")
    return (h.p.returncode if h else None, h.out() if h else "", c.p.returncode if c else None, c.out() if c else "", problems)


def tails(tmp, ho, co, n=8):
    """Both processes' output tails and their tick logs' tails (for a hung or broken scenario)."""
    out = []
    for who, text, log in (("host", ho, "h.log"), ("client", co, "c.log")):
        out.append(f"{who} output (last {n} lines):")
        out += ["  " + l for l in text.splitlines()[-n:]] or ["  (none)"]
        p = tmp / log
        out.append(f"{who} {log} (last {n} lines): " + ("" if p.exists() else "(not written: written at the end of a run)"))
        if p.exists(): out += ["  " + l for l in p.read_text(errors="replace").splitlines()[-n:]]
    return out


def summary(out):
    m = [l for l in out.splitlines() if l.startswith("SUMMARY")]
    return m[-1] if m else ""


def num(s, k, default=None):
    m = re.search(r"\b%s=(-?\d+)" % k, s)
    return int(m.group(1)) if m else default


def logs(tmp):
    def rd(p):
        t = p.read_text().split() if p.exists() else []   # a missing log reads as empty here; check() fails every kind that compares logs on it (LOG_KINDS)
        return {int(t[i]): t[i + 1] for i in range(0, len(t), 2)}
    return rd(tmp / "h.log"), rd(tmp / "c.log")


# The kinds whose checks compare the two tick logs: each needs both logs with ticks in them, or a hash comparison over nothing would pass.
LOG_KINDS = ("agree", "agree_rb", "agree_paced", "busy_ok", "cheat", "leave")


def agree(tmp, upto):
    h, c = logs(tmp)
    common = [t for t in h if t in c and t <= upto]
    bad = [t for t in common if h[t] != c[t]]
    return len(common), bad[:3]


IMP = ["--loss", "50", "--jitter", "20", "--delay", "10"]
ADV = ["--loss", "200", "--jitter", "40", "--delay", "10"]
SCENARIOS = {
    # name: (host args, client args, kind). Ticks are completed simulation ticks (stepper tick numbering: Advance(F) = tick F+1).
    "clean_2x2": (["--local", "2"], ["--local", "2"], "agree"),
    # Runs (live + replay) per main-loop iteration, window = first tick..tick T: the old two-step loop ran about 0.5. Live ticks per iteration (rate=) are lower by design: every
    # rollback replays ticks at one per iteration (the fake game's inputs change every 3 frames: ~0.63 live, 1.00 Runs); the game's replay clock makes those iterations cheap
    "tick_rate": (["--local", "2", "--change", "40", "--min-rate", "0.9", "--min-all-rate", "0.95"], ["--local", "2", "--change", "40", "--min-rate", "0.9", "--min-all-rate", "0.95"], "rate"),
    # dense inputs (a change every 3 loops, hundreds of rollbacks): every replayed tick costs an iteration, so only Runs per iteration is bounded
    "tick_rate_dense": (["--local", "2", "--min-all-rate", "0.95"], ["--local", "2", "--min-all-rate", "0.95"], "rate_all"),
    # unequal frame times (the pacing must not run away: total pacing sleep under 5% of the run, both peers reach every tick)
    "unequal_tick_rate": (["--local", "2", "--tick-ms", "6", "--delay", "10"], ["--local", "2", "--tick-ms", "10", "--delay", "10"], "agree_paced"),
    # a peer that polls but does not update for 1.5 s (a long commit gate with the idle hook) while the other leaves: the Disconnected raised by those polls must still reach the game
    "leave_during_poll_stall": (["--local", "2", "--poll-stall", "195,1500"], ["--local", "2", "--leave", "200"], "leave"),
    # the game thread is busy for 7 s (a replay behind a slow render) and does not call the session; the 5 s disconnect timer of the peer must not fire because the
    # keepalive thread polls (GekkoNet's Poll sends its NetworkHealth ping). Control: the same without the thread, the peer must time out.
    "busy_stall_keepalive": (["--local", "2", "--busy-stall", "195,7000", "--timeout-ms", "5000"], ["--local", "2", "--timeout-ms", "5000"], "busy_ok"),
    "busy_stall_no_keepalive": (["--local", "2", "--busy-stall", "195,7000", "--timeout-ms", "5000", "--keepalive-ms", "0"], ["--local", "2", "--timeout-ms", "5000"], "busy_dies"),
    "impaired_2x2": (["--local", "2"] + IMP, ["--local", "2"] + IMP, "agree_rb"),
    "adv_20pct_loss": (["--local", "2"] + ADV, ["--local", "2"] + ADV, "agree_rb"),
    "adv_80ms_jitter": (["--local", "2", "--loss", "50", "--jitter", "80", "--delay", "20"], ["--local", "2", "--loss", "50", "--jitter", "80", "--delay", "20"], "agree_rb"),
    "adv_oneside_30pct": (["--local", "2", "--loss", "300"], ["--local", "2"], "agree_rb"),
    "split_1_3": (["--local", "1"] + ADV, ["--local", "3"] + ADV, "agree_rb"),
    "split_3_1_delay": (["--local", "3", "--delays", "1,1,1"] + IMP, ["--local", "1", "--delays", "2"] + IMP, "agree_rb"),
    "cross_peer_delays": (["--local", "2", "--delays", "1,1"] + IMP, ["--local", "2", "--delays", "3,3"] + IMP, "agree_rb"),
    "cross_peer_delays_adv": (["--local", "2", "--delays", "3,3"] + ADV, ["--local", "2", "--delays", "0,0"] + ADV, "agree_rb"),
    "dirty_padding": (["--local", "2", "--dirty", "1"], ["--local", "2", "--dirty", "1"], "agree"),
    "blackout_resume": (["--local", "2", "--blackout", "1000,2500", "--timeout-ms", "8000"], ["--local", "2", "--blackout", "1000,2500", "--timeout-ms", "8000"], "agree_rb"),
    "cheat": (["--local", "2"], ["--local", "2", "--cheat", "300"], "cheat"),
    "leave": (["--local", "2"], ["--local", "2", "--leave", "200"], "leave"),
    "settings_mismatch": (["--local", "2", "--settings", "a"], ["--local", "2", "--settings", "b"], "mismatch"),
    # refusals: GekkoNet desyncs with unequal local delays on one peer; the session must refuse to start
    "refuse_unequal_3_0": (["--local", "2", "--delays", "3,0"], ["--local", "2", "--max-ms", "4000"], "refuse"),
    "refuse_unequal_0_3": (["--local", "2", "--delays", "0,3"], ["--local", "2", "--max-ms", "4000"], "refuse"),
    "refuse_1_3_delays_3_0_1": (["--local", "1", "--max-ms", "4000"], ["--local", "3", "--delays", "3,0,1"], "refuse_client"),
    "refuse_pred_ge_depth": (["--local", "2", "--pred", "10", "--depth", "10"], ["--local", "2", "--max-ms", "4000"], "refuse"),
    # the stepper's ring is shallower than GekkoNet's window: a real eviction -> the stepper refuses the load, the session ends, the peer learns at once
    "ring_too_shallow": (["--local", "2", "--timeout-ms", "30000", "--max-ms", "12000"] + ADV, ["--local", "2", "--stepper-depth", "3", "--timeout-ms", "30000"] + ADV, "shallow"),
}


def once_per_handle(s, who, remotes, kinds=("connected",)):
    """Each remote handle's Connected (and Disconnected, where expected) session event reaches the game exactly once (the update-side event index)."""
    out = []
    m = re.search(r"slots=(\d+)\.\.(\d+)/(\d+)", s)
    if m: remotes = int(m.group(3)) - (int(m.group(2)) - int(m.group(1)) + 1)   # the other peer's slots (split scenarios differ)
    for k in kinds:
        n = len(re.findall(r"(?<![a-z])" + k + r"\(h=", s))
        if n != remotes: out.append(f"{who}: {n} {k} events, expected exactly {remotes} (one per remote slot)")
    return out


def check(kind, hs, cs, hr, cr, tmp, T):
    f = []
    if kind in LOG_KINDS:
        f += [f"{who}: tick log {log} missing or empty (each process writes it at the end of its run; the hash checks need both)"
              for who, log, d in zip(("host", "client"), ("h.log", "c.log"), logs(tmp)) if not d]
    if kind in ("agree", "agree_rb", "agree_paced", "busy_ok"):
        for who, s in (("host", hs), ("client", cs)): f += once_per_handle(s, who, 2)
    if kind in ("agree", "agree_rb", "agree_paced"):
        if hr or cr: f.append(f"exit codes host={hr} client={cr}")
        n, bad = agree(tmp, T - 30)
        if n < T - 40: f.append(f"only {n} comparable ticks")
        if bad: f.append(f"hashes differ at ticks {bad}")
        for s in (hs, cs):
            if num(s, "ref_bad", 1) != 0 or num(s, "ref_ticks", 0) < T: f.append("rollback run differs from straight replay: " + s[:120])
            if num(s, "desync", 0) != -1: f.append("unexpected desync: " + s[:100])
            if "failed=-" not in s: f.append("session failed: " + s[:160])
        if kind == "agree_paced":
            for n, s in (("host", hs), ("client", cs)):
                it, pm = num(s, "iters", 1), num(s, "paced_ms", 0)
                if num(s, "maxdepth", 0) > 10: f.append(f"{n}: rollback depth {num(s, 'maxdepth', 0)} above the prediction window 10")
                if num(s, "stalls", 0) > 0.5 * it: f.append(f"{n}: {num(s, 'stalls', 0)} stalls over {it} iterations (pacing starves the peer)")
                if pm > 0.05 * it * 6: f.append(f"{n}: {pm} ms of pacing sleep over {it} iterations of 6 ms (more than 5% of the run: pacing runaway; a correct 1.6% stretch measured 43-79 ms, broken variants 624-1956 ms)")
        if kind == "agree_rb" and not (num(hs, "rollbacks", 0) > 0 and num(cs, "rollbacks", 0) > 0): f.append("no rollbacks happened")
    elif kind == "busy_ok":
        if hr or cr: f.append(f"exit codes host={hr} client={cr}")
        for n, s in (("host", hs), ("client", cs)):
            if "failed=-" not in s or "disconnected" in s: f.append(f"{n}: the peer timed out or the session failed: " + s[-200:])
            if num(s, "maxtick", 0) < T: f.append(f"{n}: stopped at tick {num(s, 'maxtick', 0)} of {T}")
        if num(hs, "keepalive_polls", 0) < 10: f.append("host: the keepalive thread polled " + str(num(hs, "keepalive_polls", 0)) + " times in a 7 s stall")
        n, bad = agree(tmp, T - 30)
        if bad: f.append(f"hashes differ at ticks {bad}")
    elif kind == "busy_dies":
        if "disconnected" not in cs: f.append("control: the client did not time out without the keepalive thread: " + cs[-200:])
    elif kind in ("rate", "rate_all"):
        if hr or cr: f.append(f"exit codes host={hr} client={cr} (7 = a rate below its bound)")
        for n, s in (("host", hs), ("client", cs)):
            m = re.search(r"\ball_rate=([0-9.]+)", s)
            if not m or float(m.group(1)) < 0.95: f.append(f"{n}: runs per iteration {m.group(1) if m else '?'}, expected >= 0.95")
            if kind == "rate":
                m = re.search(r"\brate=([0-9.]+)", s)
                if not m or float(m.group(1)) < 0.9: f.append(f"{n}: live ticks per iteration {m.group(1) if m else '?'}, expected >= 0.9 (sparse inputs)")
    elif kind == "cheat":
        # cheat at tick 300 changes the state after tick 300 = the state Save(299) takes: reported in stepper tick numbering (Save frame + 1), exactly tick 300
        for n, s in (("host", hs), ("client", cs)):
            d = num(s, "desync", -1)
            if d != 300: f.append(f"{n}: first desync tick {d}, expected exactly 300")
        n, bad = agree(tmp, 295)
        if bad: f.append(f"hashes differ before the cheat: {bad}")
    elif kind == "leave":
        f += once_per_handle(hs, "host", 2, ("connected", "disconnected"))
        if "disconnected" not in hs: f.append("host saw no disconnect: " + hs[-160:])
        if num(hs, "maxtick", 0) < T: f.append("host did not run on to the end after the leave")
        if num(hs, "tail_neutral", 0) != 1: f.append("slots of the peer that left do not read neutral (zero wire bytes) on the host")
        n, bad = agree(tmp, 190)
        if bad: f.append(f"hashes differ before the leave: {bad}")
        if cr: f.append(f"leaver exit code {cr}")
    elif kind == "mismatch":
        if hr != 3 or cr != 3: f.append(f"expected both to refuse (exit 3): host={hr} client={cr}")
    elif kind == "refuse":
        if hr != 6 or "equal" not in hs and "ring depth" not in hs: f.append(f"host should refuse to start (exit 6): exit={hr} {hs[:200]}")
    elif kind == "refuse_client":
        if cr != 6 or "equal" not in cs: f.append(f"client should refuse to start (exit 6): exit={cr} {cs[:200]}")
    elif kind == "shallow":
        if cr != 4 or "rollback-deeper-than-the-ring" not in cs: f.append(f"client should fail on the ring depth: exit={cr} {cs[:220]}")
        if "disconnected" not in hs: f.append("host was not told at once (30 s timeout, no disconnect event): " + hs[-200:])
    return f


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True, help="the net_session_test binary (net_session_test.sh builds it and passes it here)")
    ap.add_argument("--only"); ap.add_argument("--ticks", type=int, default=600)
    ap.add_argument("--deadline", type=float, default=DEADLINE_S, help="seconds per scenario, both processes together (default %(default)s)")
    a = ap.parse_args()
    fails = 0
    for name, (ha, ca, kind) in SCENARIOS.items():
        if a.only and a.only != name: continue
        with tempfile.TemporaryDirectory() as td:
            tmp = pathlib.Path(td)
            ex = ["--ticks", str(a.ticks), "--seed", "5"]
            t0 = time.monotonic()
            hr, ho, cr, co, problems = run(a.exe, ha + ex, ca + ex, tmp, a.deadline)
            hs, cs = summary(ho), summary(co)
            fl = problems or check(kind, hs, cs, hr, cr, tmp, a.ticks)
            print(f"{'PASS' if not fl else 'FAIL'} {name} ({time.monotonic() - t0:.1f} s)", flush=True)
            for s in (hs, cs): print("   ", s[:330])
            for x in fl: print("   !", x)
            if problems:
                for x in tails(tmp, ho, co): print("   |", x)
            fails += bool(fl)
    print("RESULT", "FAIL" if fails else "ok", f"({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
