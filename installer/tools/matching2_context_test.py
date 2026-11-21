#!/usr/bin/env python3
"""Native actual-source Matching2 ownership checks; run under the shared build lock.

--sdk must be a fresh authored SDK export containing 7906. No title, guest save,
server or sockets are used. Optional mutations change temporary copies only.
"""

import argparse
import hashlib
import json
import shlex
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
CASES = (
    [f"stale-{kind}-{mode}" for kind in ("join", "leave", "search") for mode in range(5)]
    + [f"blocked-{mode}" for mode in range(3)]
    + ["status-gap", "delayed-start", "max-start", "local-reject", "local-errors", "set-internal",
       "set-failure", "message-zero", "message-multi", "message-failure", "exhaustion-0", "exhaustion-1", "exhaustion-2",
       "tick", "tick-stale", "current", "commit", "search-default", "search-explicit",
       "search-error", "search-disconnect", "done-disconnect", "fail-join", "fail-leave",
       "fail-search", "transport-reentry", "sysutil-reentry", "endpoint-order",
       "reply-before-return", "tick-member", "join-error", "done-error", "search-commit", "repeat-start", "join-target"]
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, **kw):
    return subprocess.run(command, check=False, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, timeout=kw.pop("timeout", 45), **kw)


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"mutation anchor has {text.count(old)} matches: {old[:80]}")
    return text.replace(old, new, 1)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--sdk", type=Path, required=True)
    p.add_argument("--baseline-sdk", type=Path)
    p.add_argument("--cc", default="cc")
    p.add_argument("--sanitize", action="store_true")
    p.add_argument("--mutations", action="store_true")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--case", action="append", choices=CASES)
    a = p.parse_args()
    sdk = a.sdk.resolve()
    source = sdk / "libs/network/sceNpMatching2.c"
    np_source = sdk / "libs/network/np_psnr.c"
    fixture = HERE / "matching2_context_test.c"
    sysutil = sdk / "libs/system/cellSysutil.c"
    a.output.mkdir(parents=True, exist_ok=False)
    result = {"sdk": str(sdk), "sources": {str(x): sha(x) for x in (source, np_source, np_source.with_suffix('.h'), sysutil, fixture, Path(__file__))},
              "compiler": run(shlex.split(a.cc) + ["--version"]).stdout,
              "sanitizers": a.sanitize, "cases": [], "mutations": [], "baseline": []}
    flags = ["-std=gnu17", "-O1", "-g", "-UNDEBUG", "-pthread", "-ffunction-sections", "-I", str(sdk / "include")]
    if a.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
    # No LSan claim: SDK event cache and fixture VM intentionally live to exit.
    import os
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:abort_on_error=1",
               UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
               PS3_NP_SIGNALING_DELAY_MS="60000")
    syso = a.output / "sysutil.o"
    r = run(shlex.split(a.cc) + flags + ["-DcellSysutilQueueGuestCallbackArgs=fixture_real_queue", "-c", str(sysutil), "-o", str(syso)], timeout=120)
    (a.output / "sysutil-compile.log").write_text(r.stdout)
    if r.returncode:
        raise RuntimeError("actual cellSysutil compile failed; see output log")

    def build(m2, out):
        cmd = shlex.split(a.cc) + flags + ["-I", str(source.parent), f'-DNP_SOURCE="{np_source}"', f'-DM2_SOURCE="{m2}"', str(fixture), str(syso), "-Wl,--gc-sections", "-o", str(out)]
        r = run(cmd, timeout=120)
        (a.output / (out.name + "-compile.log")).write_text(r.stdout)
        if r.returncode:
            raise RuntimeError(f"compile failed: {out.name}; see output log")

    ok = True
    if a.baseline_sdk:
        baseline_fixture = HERE / "matching2_stale_test.c"
        result["sources"][str(baseline_fixture)] = sha(baseline_fixture)
        for label, root in (("before", a.baseline_sdk.resolve()), ("after", sdk)):
            test_source = root / "libs/network/tests/test_np_matching2.c"
            result["sources"][str(test_source)] = sha(test_source)
            result["sources"][str(root / "libs/network/sceNpMatching2.c")] = sha(root / "libs/network/sceNpMatching2.c")
            exe = a.output / (label + "-stale")
            r = run(shlex.split(a.cc) + flags + ["-I", str(root / "include"), f'-DJSP_MATCHING2_TEST_FILE="{test_source}"', str(baseline_fixture), str(root / "libs/network/psnr/psnr.c"), "-o", str(exe)], timeout=120)
            (a.output / (label + "-compile.log")).write_text(r.stdout)
            if r.returncode:
                raise RuntimeError("baseline compile failed; see output log")
            for case in ("same-context-positive", "leave-reuse", "join-reuse"):
                r = run([str(exe), case], env=env)
                expected = 1 if label == "before" and case != "same-context-positive" else 0
                good = r.returncode == expected and f"CONTROL {case} {'FAIL' if expected else 'PASS'}" in r.stdout
                (a.output / (label + "-" + case + ".log")).write_text(r.stdout)
                result["baseline"].append({"source": label, "case": case, "returncode": r.returncode, "expected": expected, "pass": good})
                print(f"{'PASS' if good else 'FAIL'} {label} {case}", flush=True)
                ok &= good
    exe = a.output / "context-test"
    build(source, exe)
    for case in a.case or CASES:
        r = run([str(exe), case], env=env, timeout=20)
        (a.output / (case + ".log")).write_text(r.stdout)
        good = r.returncode == 0 and f"PASS {case}:" in r.stdout
        result["cases"].append({"case": case, "returncode": r.returncode, "pass": good})
        print(f"{'PASS' if good else 'FAIL'} {case}", flush=True)
        ok &= good

    if a.mutations:
        text = source.read_text()
        variants = [
            ("ignore-owner", "stale-leave-0", "s_owner[op->ctx] != op->owner", "0"),
            ("late-admission-owner", "status-gap", "s_owner[id] != owner", "0"),
            ("start-revives-max", "max-start", "s_owner[ctxId] != owner || s_exhausted[ctxId]", "s_owner[ctxId] != owner"),
            ("reset-owner-on-init", "stale-join-2", "memset(s_ctx, 0, sizeof(s_ctx)); /* owner/exhaustion history survives */", "memset(s_ctx, 0, sizeof(s_ctx)); memset(s_owner, 0, sizeof(s_owner));"),
            ("tick-ignores-owner", "tick-stale", "s_owner[ctx] == owner &&", "1 &&"),
            ("zero-target-op", "message-zero", "if (!nt) M2_RETURN(CELL_OK); /* baseline: no request id or callback without a target */", "/* mutant: allocate a no-target op */"),
            ("short-flag-stack", "set-internal", "u8 flag_body[15];", "u8 flag_body[13];"),
            ("wrong-flag-payload", "set-internal", "*p++ = 2;\n        p = psnr_put16(p, 4);", "*p++ = 3;\n        p = psnr_put16(p, 4);"),
            ("missing-init-check", "local-errors", "if (!s_init) return (s32)SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED;", "if (0) return (s32)SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED;"),
            ("missing-start-check", "local-errors", "!c->started || s_exhausted[id]", "s_exhausted[id]"),
            ("reset-owner-on-create", "stale-join-0", "memset(&s_ctx[i], 0, sizeof(m2_ctx));", "memset(&s_ctx[i], 0, sizeof(m2_ctx)); s_owner[i] = 1;"),
            ("unlock-reply-transaction", "commit", 'M2_TEST_PAUSE("reply-commit");\n    if (m->type', 'STATE_UNLOCK(); M2_TEST_PAUSE("reply-commit"); STATE_LOCK();\n    if (m->type'),
            ("unlock-search-transaction", "search-commit", 'M2_TEST_PAUSE("search-commit");', 'STATE_UNLOCK(); M2_TEST_PAUSE("search-commit"); STATE_LOCK();'),
            ("stop-keeps-owner", "stale-join-1", "c->started = 0;\n    owner_advance(ctxId);", "c->started = 0; /* mutant keeps owner across stop/start */"),
            ("max-not-retired", "exhaustion-0", "s_exhausted[id] = 1; return 0;", "return 0;"),
            ("start-retires-valid-op", "repeat-start", "if (result == 0) {\n        c->started = 1;", "if (result == 0) {\n        owner_advance(ctxId); c->started = 1;"),
        ]
        # This is the rejected lock order, with the same pre-N checkpoint.
        order_old = "int peer = m->type == PSNR_ROOM_JOINED;\n    if (peer) np_psnr_peer_lock();\n    STATE_LOCK();"
        variants.append(("m-before-n", "blocked-0", order_old,
                         "int peer = m->type == PSNR_ROOM_JOINED;\n    STATE_LOCK();\n    if (peer) np_psnr_peer_lock();"))
        tick_start=text.index("static void m2_tick(void)")
        tick_end=text.index("/* SceNpMatching2RoomMemberUpdateInfo",tick_start)
        tick=text[tick_start:tick_end]
        late_tick=replace_once(tick,"    STATE_LOCK();\n","")
        late_tick=replace_once(late_tick,'M2_TEST_PAUSE("tick-pop");','M2_TEST_PAUSE("tick-pop");\n        STATE_LOCK();')
        variants.append(("detached-tick", "tick-member", tick, late_tick))
        if not a.sanitize:
            variants = [v for v in variants if v[0] != "short-flag-stack"]
        with tempfile.TemporaryDirectory(prefix="m2-context-mutants-") as td:
            for name, case, old, new in variants:
                m2 = Path(td) / f"{name}.c"
                # Keep original relative network includes valid in a temporary
                # source by adding its directory to include search below.
                m2.write_text(replace_once(text, old, new))
                mutant = a.output / name
                build(m2, mutant)
                r = run([str(mutant), case], env=env, timeout=20)
                (a.output / (name + ".log")).write_text(r.stdout)
                rejected = r.returncode != 0
                if name == "short-flag-stack":
                    rejected &= "stack-buffer-overflow" in r.stdout or "runtime error:" in r.stdout
                result["mutations"].append({"name": name, "case": case, "returncode": r.returncode, "rejected": rejected})
                print(f"{'REJECT' if rejected else 'SURVIVED'} {name}", flush=True)
                ok &= rejected
    result["pass"] = bool(ok)
    (a.output / "receipt.json").write_text(json.dumps(result, indent=2) + "\n")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
