#!/usr/bin/env python3
"""Tests the two-process driver installer/tools/net_session_test.py with fake processes (no build, no network): a process that never exits must turn
into a reported FAILURE that names the scenario and the process, within the scenario deadline, with no process left behind; and every scenario kind
that compares the two tick logs must fail when one of them is missing or empty.
    python3 installer/tools/net_session_driver_test.py [--driver PATH]
--driver runs another copy of the driver (the negative control: the driver before the deadline fix blocks on a host that never exits).
POSIX only (the fake process is a script started through its #! line); exit 1 on any failure."""
import argparse, importlib.util, os, pathlib, subprocess, sys, tempfile, time

HERE = pathlib.Path(__file__).resolve().parent
DEADLINE = 3      # the scenario deadline given to the driver (seconds)
OUTER = 60        # this test's own bound on one driver run: far above DEADLINE, so only a driver that does not bound its waits reaches it

# The fake net_session_test: FAKE_MODE picks what each role does; each role writes its pid so the test can check nothing is left running.
FAKE = r'''import os, sys, time
role = sys.argv[sys.argv.index("--role") + 1]
mode = os.environ["FAKE_MODE"]
open(os.path.join(os.environ["FAKE_DIR"], "pid." + role), "w").write(str(os.getpid()))
def hang(): time.sleep(600)
if role == "host":
    if mode == "host_no_port": sys.exit(9)
    if mode == "host_bad_port": print("PORT ", flush=True); hang()
    print("PORT 40000", flush=True)
    if mode in ("host_hangs", "both_hang"): hang()
    print("SUMMARY role=host fake", flush=True); time.sleep(0.5); sys.exit(0)
print("SUMMARY role=client fake", flush=True)
if mode in ("client_hangs", "both_hang"): hang()
sys.exit(0)
'''

# mode: (must appear in the driver's output, must not appear)
CASES = {
    "host_hangs": (["FAIL clean_2x2", "hung: the host", "host output", "client output"], ["hung: the client"]),
    "client_hangs": (["FAIL clean_2x2", "hung: the client"], ["hung: the host"]),
    "both_hang": (["FAIL clean_2x2", "hung: the host", "hung: the client"], []),
    "host_no_port": (["FAIL clean_2x2", "host printed no PORT line and exited (9)"], ["Traceback"]),
    "host_bad_port": (["FAIL clean_2x2", "host printed a PORT line without a port number", "hung: the host"], ["Traceback", "no PORT line"]),
}

# check() on summaries that pass every other rule of a kind that compares tick logs: with both logs it passes, with a log missing or empty it must fail
# (a hash comparison over no ticks finds no difference). slots=0..1/4 = two remote slots, matching the two connected events.
T = 600
BASE = ("SUMMARY role=x slots=0..1/4 maxtick=600 rollbacks=5 maxdepth=3 stalls=0 desync=-1 failed=- ref_ticks=600 ref_bad=0 tail_neutral=1 "
        "iters=700 paced_ms=0 keepalive_polls=50 events: connected(h=2) connected(h=3)")
CHECK_KINDS = {
    "agree_rb": BASE,
    "busy_ok": BASE,
    "cheat": BASE.replace("desync=-1", "desync=300"),
    "leave": BASE + " disconnected(h=2) disconnected(h=3)",
}


def load_driver(path):
    spec = importlib.util.spec_from_file_location("net_session_driver_under_test", path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def check_logs(driver):
    """Returns failures: a kind that passes with a log missing or empty, or fails with both logs (then the summaries above are wrong, not the driver)."""
    try: m, f = load_driver(driver), []
    except BaseException as e: return [f"could not import the driver ({type(e).__name__}: {e}); it must run main() only when started directly"]
    good = "".join(f"{t} {t:016x}\n" for t in range(1, T + 1))
    for kind, s in CHECK_KINDS.items():
        for case, h_log, c_log in (("both logs", good, good), ("no host log", None, good), ("no client log", good, None), ("empty client log", good, "")):
            with tempfile.TemporaryDirectory() as td:
                tmp = pathlib.Path(td)
                for name, text in (("h.log", h_log), ("c.log", c_log)):
                    if text is not None: (tmp / name).write_text(text)
                try: r = m.check(kind, s, s, 0, 0, tmp, T)
                except Exception as e: r = None; f.append(f"{kind}, {case}: check() raised {type(e).__name__}: {e}")
                if r is None: continue
                if case == "both logs" and r: f.append(f"{kind}, {case}: check() failed a good run: {r}")
                if case != "both logs" and not r: f.append(f"{kind}, {case}: check() passed (the hash checks compared nothing)")
    return f


def alive(pid):
    try: os.kill(pid, 0)
    except ProcessLookupError: return False
    except PermissionError: return True
    # a zombie (killed, not yet reaped by its exited parent) counts as gone; /proc where there is one (Linux), else ps (other POSIX systems)
    if pathlib.Path("/proc/self/stat").exists():
        try: return pathlib.Path(f"/proc/{pid}/stat").read_text().split(") ")[-1][:1] != "Z"
        except OSError: return False
    st = subprocess.run(["ps", "-o", "stat=", "-p", str(pid)], capture_output=True, text=True).stdout.strip()
    return bool(st) and not st.startswith("Z")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--driver", default=str(HERE / "net_session_test.py"))
    a = ap.parse_args()
    if os.name != "posix": print("net_session_driver_test: POSIX only"); return 1
    fails = 0
    with tempfile.TemporaryDirectory() as td:
        d = pathlib.Path(td)
        exe = d / "fake_net_session_test"
        exe.write_text(f"#!{sys.executable}\n" + FAKE)
        exe.chmod(0o755)
        for mode, (want, avoid) in CASES.items():
            run = d / mode; run.mkdir()
            env = dict(os.environ, FAKE_MODE=mode, FAKE_DIR=str(run))
            t0 = time.monotonic()
            # Own process group, output to a file: a driver that blocks is killed with its fakes, and a fake holding an inherited pipe cannot block this test.
            with open(run / "driver.out", "w+") as o:
                p = subprocess.Popen([sys.executable, "-I", a.driver, "--exe", str(exe), "--only", "clean_2x2", "--deadline", str(DEADLINE)],
                                     stdout=o, stderr=subprocess.STDOUT, env=env, start_new_session=True)
                try: rc = p.wait(timeout=OUTER)
                except subprocess.TimeoutExpired: rc = None; os.killpg(p.pid, 9); p.wait()
                o.seek(0); out = o.read()
            dt = time.monotonic() - t0
            f = []
            if rc is None: f.append(f"the driver blocked: still running after {OUTER} s (scenario deadline {DEADLINE} s)")
            elif rc != 1: f.append(f"driver exit {rc}, expected 1 (a failed scenario)")
            elif dt > DEADLINE + 15: f.append(f"the driver took {dt:.1f} s for a {DEADLINE} s deadline")
            f += [f"output lacks {w!r}" for w in want if w not in out]
            f += [f"output has {w!r}" for w in avoid if w in out]
            for pf in run.glob("pid.*"):
                pid = int(pf.read_text())
                if alive(pid): f.append(f"the fake {pf.suffix[1:]} (pid {pid}) is still running after the driver returned"); os.kill(pid, 9)
            print(f"{'PASS' if not f else 'FAIL'} {mode} ({dt:.1f} s)")
            for x in f: print("   !", x)
            if f: print("   | " + "\n   | ".join(out.splitlines()[-25:]))
            fails += bool(f)
    f = check_logs(a.driver)
    print(f"{'PASS' if not f else 'FAIL'} tick_logs_required ({len(CHECK_KINDS)} kinds)")
    for x in f: print("   !", x)
    fails += bool(f)
    print("net_session_driver_test:", "FAIL" if fails else "ok", f"({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
