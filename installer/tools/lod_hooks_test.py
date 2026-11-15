#!/usr/bin/env python3
"""Exercise real patched lifter + build verifier using authored synthetic PPC.

No original bytes or lifted title statements. --native additionally compiles
synthetic emission; invoke that mode only through the shared build lock.
"""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

PORT = Path(__file__).resolve().parents[1] / "port/jsp"
spec = importlib.util.spec_from_file_location("lod_hooks", PORT / "lod_hooks.py")
hooks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hooks)
BASE, END, CALLER, TAILER = 0x3955B4, 0x395A90, 0x395A80, 0x395A88


def expect_failure(fn):
    try:
        fn()
    except (ValueError, OSError, KeyError):
        return
    raise AssertionError("negative control unexpectedly accepted")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--lifter", type=Path, required=True)
    ap.add_argument("--native", action="store_true")
    ap.add_argument("--cxx", default="c++")
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        manifest = json.loads((PORT / "lift_lod_hooks.json").read_text())
        code = bytearray(bytes.fromhex("4e800020") * ((END - BASE) // 4))
        for e in manifest["hooks"]:
            at = int(e["addr"], 0) - BASE
            code[at:at + 8] = bytes.fromhex("386300014e800020")  # addi r3,r3,1; blr
        branch = 0x48000001 | ((BASE - CALLER) & 0x03FFFFFC)
        code[CALLER - BASE:CALLER - BASE + 4] = branch.to_bytes(4, "big")
        code[TAILER - BASE:TAILER - BASE + 4] = (0x48000000 | ((BASE - TAILER) & 0x03FFFFFC)).to_bytes(4, "big")
        image = root / "authored.bin"
        image.write_bytes(code)
        manifest["program"]["sha256"] = hashlib.sha256(code).hexdigest()
        manifest["program"]["segment_sha256"] = "0" * 64
        config = root / "hooks.json"
        config.write_text(json.dumps(manifest))
        functions = [{"start": e["addr"], "end": e["end"]} for e in manifest["hooks"]]
        functions += [{"start": hex(CALLER), "end": hex(TAILER)}, {"start": hex(TAILER), "end": hex(END)}]
        funcs = root / "functions.json"
        out = root / "out"

        def lift(entries=functions, hook_config=config, enable=True):
            funcs.write_text(json.dumps(entries))
            command = [sys.executable, str(args.lifter), str(image), "--raw", "--base", hex(BASE),
                       "--functions", str(funcs), "--jobs", "1", "--output", str(out)]
            if enable:
                command += ["--fn-hooks", str(hook_config)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=60)
            return result

        def valid_lift():
            r = lift()
            assert r.returncode == 0, r.stdout[-1000:] + r.stderr[-1000:]
            hooks.stamp(out, config, image, "US", args.lifter)
            assert hooks.verify(out, config, image, "US", args.lifter)

        valid_lift()
        text = "\n".join(p.read_text() for p in out.glob("ppu_recomp*.cpp"))
        assert "func_003955B4(ctx);" in text  # actual named direct caller remains routed through wrapper
        expect_failure(lambda: hooks.verify(out, config, image, "EU", args.lifter))
        expect_failure(lambda: hooks.verify(out, config, None, "US", args.lifter))
        receipt = out / hooks.RECEIPT
        saved = receipt.read_text()
        receipt.unlink()
        expect_failure(lambda: hooks.verify(out, config, image, "US", args.lifter))
        receipt.write_text(saved)
        config.write_text(json.dumps(manifest) + " ")
        expect_failure(lambda: hooks.verify(out, config, image, "US", args.lifter))
        config.write_text(json.dumps(manifest))
        altered_tool = root / "changed_lifter.py"
        altered_tool.write_text(args.lifter.read_text() + "\n# changed tool\n")
        expect_failure(lambda: hooks.verify(out, config, image, "US", altered_tool))

        # A changed generated file cannot retain a previously active header.
        target = next(p for p in out.glob("ppu_recomp*.cpp") if "jsp_lod_initialize_enter(ctx);" in p.read_text())
        original = target.read_text()
        target.write_text(original.replace("jsp_lod_initialize_enter(ctx);", "", 1))
        header = root / hooks.HEADER
        hooks.write_header(header, True)
        cli = subprocess.run([sys.executable, str(PORT / "lod_hooks.py"), "--out", str(out),
            "--manifest", str(config), "--lifter", str(args.lifter), "--image", str(image),
            "--region", "US", "--header", str(header)], capture_output=True, text=True)
        assert cli.returncode != 0 and "VERIFIED 0" in header.read_text()
        expect_failure(lambda: hooks.stamp(out, config, image, "US", args.lifter))
        target.write_text(original)
        hooks.stamp(out, config, image, "US", args.lifter)
        body_start = original.index("{", original.index("static void func_003955B4__body")) + 1
        for injection in ("g_trampoline_fn = target; return;", "/* } */ g_trampoline_fn = target; return;"):
            target.write_text(original[:body_start] + injection + original[body_start:])
            expect_failure(lambda: hooks.stamp(out, config, image, "US", args.lifter))
        target.write_text(original)

        # Explicit emitted interior entries bypass the generic outer wrapper.
        # The final-output verifier rejects them for our three bounded ranges.
        r = lift(functions + [{"start": hex(BASE + 4), "end": hex(BASE + 8)}])
        assert r.returncode != 0 and "overlapping entry" in r.stderr
        bad_range = copy.deepcopy(functions)
        bad_range[0]["end"] = hex(BASE + 8)
        r = lift(bad_range)
        assert r.returncode != 0 and "end changed" in r.stderr
        r = lift(functions[1:])
        assert r.returncode != 0 and "no lifted function start" in r.stderr
        bad = copy.deepcopy(manifest)
        bad["hooks"][0]["enter"] = "invalid symbol"
        bad_config = root / "bad.json"
        bad_config.write_text(json.dumps(bad))
        r = lift(hook_config=bad_config)
        assert r.returncode != 0 and "not a C identifier" in r.stderr
        old_word = bytes(code[4:8])
        code[4:8] = (0x48000000 | ((0x39595C - (BASE + 4)) & 0x03FFFFFC)).to_bytes(4, "big")
        image.write_bytes(code)
        r = lift()
        assert r.returncode != 0 and "tail-calls or falls through" in r.stderr
        code[4:8] = old_word
        image.write_bytes(code)
        valid_lift()

        # Production image guard ignores a forced US region for unknown bytes.
        assert not hooks.plan(PORT / "lift_lod_hooks.json", image, "US", args.lifter)["enabled"]
        assert not hooks.plan(config, image, "EU", args.lifter)["enabled"]
        expect_failure(lambda: hooks.stamp(out, PORT / "lift_lod_hooks.json", image, "US", args.lifter))
        r = lift(enable=False)
        assert r.returncode == 0, r.stderr[-800:]
        hooks.stamp(out, PORT / "lift_lod_hooks.json", image, "US", args.lifter)
        assert not hooks.verify(out, PORT / "lift_lod_hooks.json", image, "US", args.lifter)
        receipt.unlink()
        assert not hooks.verify(out, config, None, "", args.lifter)
        valid_lift()
        if args.native:
            # Compile only the real emitted authored-synthetic functions; runtime
            # registration boilerplate is irrelevant to the wrapper call oracle.
            import re
            cpp = "\n".join(p.read_text() for p in out.glob("ppu_recomp*.cpp"))
            pieces = []
            pattern = r"(?:static )?void func_[0-9A-F]{8}(?:__body)?\(ppu_context\* ctx\) \{"
            for m in re.finditer(pattern, cpp):
                start, at, depth = m.start(), m.end(), 1
                while depth:
                    depth += (cpp[at] == "{") - (cpp[at] == "}")
                    at += 1
                pieces.append(cpp[start:at])
            prelude = '''#include <cstdint>\n#include <cassert>\n#include <vector>\n
struct ppu_context { uint64_t gpr[32]{}; uint64_t lr{}; };\n
#define PPU_MM_SNAP\n
static void (*g_trampoline_fn)(void*) = nullptr;\n
#define DRAIN_TRAMPOLINE(ctx) do { while(g_trampoline_fn) { auto f=g_trampoline_fn; g_trampoline_fn=nullptr; f(ctx); } } while(0)\n
static std::vector<int> events;\n
extern "C" void jsp_lod_initialize_enter(ppu_context* c) { assert(c->gpr[3]==7); events.push_back(1); }\n
extern "C" void jsp_lod_retire_enter(ppu_context*) { events.push_back(2); }\n
extern "C" void jsp_lod_select_enter(ppu_context*) { events.push_back(3); }\n
extern "C" void jsp_lod_hook_leave(ppu_context*) { events.push_back(4); }\n
void func_003955B4(ppu_context*);\n'''
            entries = re.findall(r'\{\s*0x[0-9A-F]+ULL,\s*func_[0-9A-F]{8},\s*"func_[0-9A-F]{8}"\s*\}', cpp)
            assert len(entries) >= 5
            table_cpp = '\nstruct Entry { uint64_t ea; void(*fn)(ppu_context*); const char* name; };\nstatic Entry emitted[] = {' + ','.join(entries) + '};\n'
            main_cpp = '''\nint main() {
ppu_context c; c.gpr[3]=7; func_00395A80(&c); assert(c.gpr[3]==8); assert((events==std::vector<int>{1,4}));
events.clear(); c.gpr[3]=7; bool found=false; for(auto e:emitted) if(e.ea==0x3955B4) { e.fn(&c); found=true; }
assert(found && c.gpr[3]==8); assert((events==std::vector<int>{1,4}));
events.clear(); c.gpr[3]=7; func_00395A88(&c); DRAIN_TRAMPOLINE(&c); assert(c.gpr[3]==8); assert((events==std::vector<int>{1,4}));
events.clear(); c.gpr[3]=7; func_0039595C(&c); func_003959C8(&c); assert(c.gpr[3]==9); assert((events==std::vector<int>{2,4,3,4}));
}\n'''
            source = root / "emission.cpp"
            source.write_text(prelude + "\n".join(pieces) + table_cpp + main_cpp)
            subprocess.run([args.cxx, "-std=c++20", "-O1", str(source), "-o", str(root / "emission")], check=True)
            subprocess.run([str(root / "emission")], check=True)
    print("PASS: actual lifter wrappers, image/config/tool/output/interior refusal" +
          (", compiled direct/table callbacks" if args.native else " (native mode not requested)"))


if __name__ == "__main__":
    main()
