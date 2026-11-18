#!/usr/bin/env python3
"""Two-process rollback session tests (no game, no GPU): starts net_session_test as host and client over localhost UDP and checks them.
    python3 installer/tools/net_session_test.py --exe PATH [--only NAME] [--ticks N]
Scenarios and what each must show are in SCENARIOS below; exit 1 on any failure. Build: installer/tools/net_session_test.sh (which also runs it).
The exe may also be the MinGW build when run where Windows programs run (not done by default)."""
import argparse, pathlib, re, subprocess, sys, tempfile



def run(exe, host_args, client_args, tmp, timeout=120):
    h = subprocess.Popen([exe, "--role", "host", "--out", str(tmp / "h.log")] + host_args, stdout=subprocess.PIPE, text=True)
    port = h.stdout.readline().split()[1]
    c = subprocess.Popen([exe, "--role", "client", "--peer", f"127.0.0.1:{port}", "--out", str(tmp / "c.log")] + client_args,
                         stdout=subprocess.PIPE, text=True)
    co, _ = c.communicate(timeout=timeout)
    ho = h.stdout.read(); h.wait(timeout=timeout)
    return h.returncode, ho, c.returncode, co


def summary(out):
    m = [l for l in out.splitlines() if l.startswith("SUMMARY")]
    return m[-1] if m else ""


def num(s, k, default=None):
    m = re.search(r"\b%s=(-?\d+)" % k, s)
    return int(m.group(1)) if m else default


def logs(tmp):
    def rd(p):
        d = {}
        for l in p.read_text().split():
            pass
        t = p.read_text().split()
        return {int(t[i]): t[i + 1] for i in range(0, len(t), 2)}
    return rd(tmp / "h.log"), rd(tmp / "c.log")


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
    a = ap.parse_args()
    fails = 0
    for name, (ha, ca, kind) in SCENARIOS.items():
        if a.only and a.only != name: continue
        with tempfile.TemporaryDirectory() as td:
            tmp = pathlib.Path(td)
            ex = ["--ticks", str(a.ticks), "--seed", "5"]
            hr, ho, cr, co = run(a.exe, ha + ex, ca + ex, tmp)
            hs, cs = summary(ho), summary(co)
            fl = check(kind, hs, cs, hr, cr, tmp, a.ticks)
            print(f"{'PASS' if not fl else 'FAIL'} {name}")
            for s in (hs, cs): print("   ", s[:330])
            for x in fl: print("   !", x)
            fails += bool(fl)
    print("RESULT", "FAIL" if fails else "ok", f"({fails} failed)")
    return 1 if fails else 0


sys.exit(main())
