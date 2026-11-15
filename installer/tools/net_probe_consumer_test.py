#!/usr/bin/env python3
"""Compile the actual opt-in port consumer with the actual SDK recorder.

Synthetic context/callback/error adapters only; no title or Windows API proof.
Run under the shared build lock. Full-source Windows syntax remains separate.
"""

import argparse
import importlib.util
import os
import subprocess
import tempfile
from pathlib import Path

spec = importlib.util.spec_from_file_location("net_probe_test", Path(__file__).with_name("net_probe_test.py"))
probe_test = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe_test)
PEEK, PORT, PREFIX = probe_test.PEEK, probe_test.PORT, probe_test.PREFIX

MAIN = r"""
extern "C" {
int g_cellpad_script_report_index=-1;
void (*g_cellpad_script_report_observer)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,int32_t)=nullptr;
}
static void (*registered)(ppu_context*);
static std::atomic<int> original_calls{0};
static bool recurse;
extern "C" void ps3_hle_register_ctx(uint32_t nid,const char*,void(*fn)(ppu_context*)) {
    assert(nid==0x189A74DA && !registered);registered=fn;
}
extern "C" int32_t cellSysutilCheckCallback(void) {
    ++original_calls;
    if(recurse) {
        recurse=false;ppu_context child{};registered(&child);
        assert(child.gpr[3]==(uint64_t)(int64_t)(int32_t)0x80010001u);
        assert(probe_load64(&s_probe.copied)==0); // nested pump cannot drain
    }
    errno=987;
#ifdef PROBE_FAKE_WINDOWS
    fake_system=111;fake_socket=222;
#endif
    return (int32_t)0x80010001u;
}
int main(int argc,char** argv) {
    assert(argc==2);std::string mode=argv[1];
    if(mode!="off")setenv("PS3_NET_PROBE_ARM_SCRIPT","0",1);
    errno=321;jsp_register_net_probe();assert(fixture_errno_peek()==321);
    if(mode=="off") {assert(!registered && !g_cellpad_script_report_observer && g_cellpad_script_report_index==-1);return 0;}
    assert(registered && g_cellpad_script_report_observer==ps3_net_probe_script_report && g_cellpad_script_report_index==0);
    g_cellpad_script_report_observer(0,0,0,0x4000,0x4000,8);
    ppu_context c{};c.thread_id=5;c.lr=0x12345678;
    for(int i=0;i<70;i++){auto ticket=probe_enter(&c,PS3_NET_PROBE_SEND,7,12,4);probe_leave(&ticket,i);}
    if(mode=="io")assert(std::freopen("/dev/full","w",stdout));
    if(mode=="threads") {
        std::vector<std::thread> threads;
        for(int i=0;i<8;i++)threads.emplace_back([]{ppu_context ctx{};registered(&ctx);assert(ctx.gpr[3]==(uint64_t)(int64_t)(int32_t)0x80010001u);assert(fixture_errno_peek()==987);});
        for(auto& thread:threads)thread.join();assert(original_calls==8);
    } else {
        recurse=mode=="recursive";auto saved=c;registered(&c);
        assert(original_calls==(mode=="recursive"?2:1));
        assert(c.gpr[3]==(uint64_t)(int64_t)(int32_t)0x80010001u && fixture_errno_peek()==987);
#ifdef PROBE_FAKE_WINDOWS
        assert(fake_system==111 && fake_socket==222);
#endif
        c.gpr[3]=saved.gpr[3];assert(!std::memcmp(&c,&saved,sizeof(c)));
        assert(probe_load64(&s_probe.copied)==32 && s_probe.count==39);
    }
    if(mode=="io")assert(probe_load(&s_probe.io_failed) && probe_load(&s_probe.incomplete));
    return 0;
}
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--sdk", type=Path, required=True)
    ap.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    ap.add_argument("--mutations", action="store_true")
    a = ap.parse_args()
    sdk = a.sdk.resolve()
    recorder = (sdk / "libs/network/sysNet_probe.inc").read_text()
    port = (PORT / "jsp_net_probe.cpp").read_text()
    clean = {k: v for k, v in os.environ.items() if not k.startswith("PS3_NET_PROBE_")}
    with tempfile.TemporaryDirectory(prefix="net-probe-consumer-") as temporary:
        root = Path(temporary)
        (root / "ppu_recomp.h").write_text('#include "ppu_context.h"\n')
        (root / "winsock2.h").write_text(
            "/* authored fake error adapter, not Windows SDK */\n"
        )
        inc = [
            "-I" + str(root),
            "-I" + str(sdk / "include"),
            "-I" + str(sdk / "runtime/ppu"),
            "-I" + str(sdk / "runtime/platform"),
            "-I" + str(sdk / "libs/network"),
        ]

        def run(label, body, windows=False, mutant=False):
            source = root / (label + ".cpp")
            exe = root / label
            source.write_text(
                ("#define PROBE_FAKE_WINDOWS 1\n" if windows else "")
                + PREFIX
                + "\n"
                + recorder
                + "\n"
                + PEEK
                + "\n"
                + body
                + "\n"
                + MAIN
            )
            subprocess.run(
                [
                    a.cxx,
                    "-std=gnu++20",
                    "-O1",
                    "-pthread",
                    *inc,
                    str(source),
                    "-o",
                    str(exe),
                ],
                check=True,
            )
            bad = []
            for mode in ["off", "recursive", "io", "threads"]:
                result = subprocess.run(
                    [str(exe), mode],
                    check=False,
                    env=clean,
                    capture_output=True,
                    text=True,
                    timeout=20,
                )
                if result.returncode:
                    bad.append(mode)
                if result.returncode and not mutant:
                    raise AssertionError(label + " " + mode + "\n" + result.stderr)
            if mutant:
                assert bad, label + " escaped"
            print(
                label
                + (
                    ": rejected " + ",".join(bad)
                    if mutant
                    else ":4 actual consumer cases pass"
                ),
                flush=True,
            )

        run("consumer-native", port)
        run("consumer-fake-windows", port, windows=True)
        if a.mutations:
            run(
                "missing-original",
                port.replace("cellSysutilCheckCallback(); // direct", "0; // direct"),
                mutant=True,
            )
            run(
                "nested-drain",
                port.replace("if (depth == 1) drain();", "if (depth >= 1) drain();"),
                mutant=True,
            )
            run(
                "missing-consumer-wsa",
                port.replace("WSASetLastError(socket);", "(void)socket;"),
                windows=True,
                mutant=True,
            )
            run(
                "missing-consumer-system",
                port.replace("SetLastError(system);", "(void)system;"),
                windows=True,
                mutant=True,
            )
            early = port.replace(
                "struct HostErrors {\n#ifdef _WIN32",
                "struct HostErrors {\n    int crt = errno;\n#ifdef _WIN32",
            ).replace(
                "    int crt = errno; // save OS stores before calling a CRT errno accessor\n",
                "",
            )
            run("consumer-crt-save-first", early, windows=True, mutant=True)
            late = port.replace("        errno = crt;\n", "").replace(
                "        SetLastError(system);\n#endif\n",
                "        SetLastError(system);\n#endif\n        errno = crt;\n",
            )
            run("consumer-crt-restore-last", late, windows=True, mutant=True)


if __name__ == "__main__":
    main()
