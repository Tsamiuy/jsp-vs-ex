#!/usr/bin/env python3
"""Tests the two-process driver installer/tools/net_session_test.py with fake processes (no build, no network): a process that never exits must turn
into a reported FAILURE that names the scenario and the process, within the scenario deadline, with no process left behind.
    python3 installer/tools/net_session_driver_test.py [--driver PATH]
--driver runs another copy of the driver (the negative control: the driver before the deadline fix blocks on a host that never exits).
POSIX only (the fake process is a script started through its #! line); exit 1 on any failure."""
import argparse, os, pathlib, subprocess, sys, tempfile, time

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
}


def alive(pid):
    try: os.kill(pid, 0)
    except ProcessLookupError: return False
    except PermissionError: return True
    # a zombie (killed, not yet reaped by its exited parent) counts as gone
    try: return pathlib.Path(f"/proc/{pid}/stat").read_text().split(") ")[-1][:1] != "Z"
    except OSError: return False


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
    print("net_session_driver_test:", "FAIL" if fails else "ok", f"({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
