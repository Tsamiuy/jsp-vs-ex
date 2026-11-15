#!/usr/bin/env python3
"""Actual probe/wrappers/consumer and full cellPad fixtures; no title or delivery proof.

Run native mode through the project's shared build lock. --sdk must be a fresh
export containing7905. SDL is used only for types/linkage; device enumeration is
stubbed. Pass --sdl-config from an existing pinned shell/cache when needed.
"""

import argparse
import hashlib
import json
import os
import re
import shlex
import subprocess
import tempfile
from pathlib import Path

PORT = Path(__file__).resolve().parents[1] / "port/jsp"

PREFIX = r"""
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <thread>
#include <vector>
#include <string>
#include "win32_compat.h"
#include "ppu_context.h"
#include "sysNet.h"
static std::atomic<uint64_t> now_ms{1000};
static std::atomic<int> clock_calls{0};
static uint64_t fixture_clock();
#define GetTickCount64 fixture_clock
#ifdef PROBE_FAKE_WINDOWS
static thread_local DWORD fake_system=0;
static thread_local int fake_socket=0;
static DWORD fixture_GetLastError() { return fake_system; }
static void fixture_SetLastError(DWORD v) { fake_system=v; }
#define GetLastError fixture_GetLastError
#define SetLastError fixture_SetLastError
static thread_local int fake_crt=0;
static int* fixture_errno_pointer() { fake_system=555;fake_socket=666;return &fake_crt; }
#undef errno
#define errno (*fixture_errno_pointer())
static int WSAGetLastError() { return fake_socket; }
static void WSASetLastError(int v) { fake_socket=v; }
#define _WIN32 1
#endif
static uint64_t fixture_clock() {
    ++clock_calls; errno=765;
#ifdef PROBE_FAKE_WINDOWS
    fake_system=333;fake_socket=444;
#endif
    return now_ms;
}
"""
PEEK = r"""
static int fixture_errno_peek() {
#ifdef PROBE_FAKE_WINDOWS
 return fake_crt;
#else
 return errno;
#endif
}
"""

CORE = r"""
#define R32(n) ((uint32_t)ctx->gpr[n])
#define S32(n) ((int32_t)ctx->gpr[n])
struct GuestPointer { uintptr_t a; template<class T> operator T*() const { return (T*)a; } };
#define PTR(n) GuestPointer{R32(n)}
#define RET_S(v) (ctx->gpr[3]=(uint64_t)(int64_t)(int32_t)(v))
static int core_calls;
static int32_t core_result;
static int before_error=321;
static bool arm_inside;
static int32_t core(uint32_t socket, const void* buffer, uint32_t len, int32_t flags) {
    assert(socket==7 && (uintptr_t)buffer==0x1234 && len==12 && flags==4);
    assert(fixture_errno_peek()==before_error);
#ifdef PROBE_FAKE_WINDOWS
    assert(fake_system==41 && fake_socket==42);
#endif
    ++core_calls;
    if(arm_inside) ps3_net_probe_script_report(0,0,0,0x4000,0x4000,8);
    errno=987;
#ifdef PROBE_FAKE_WINDOWS
    fake_system=111;fake_socket=222;
#endif
    return core_result;
}
extern "C" int32_t sys_net_bnet_send(int32_t s,const void* b,uint32_t n,int32_t f) { return core(s,b,n,f); }
extern "C" int32_t sys_net_bnet_sendto(int32_t s,const void* b,uint32_t n,int32_t f,const sys_net_sockaddr* to,uint32_t size) { assert((uintptr_t)to==0x5678 && size==0x9abc); return core(s,b,n,f); }
extern "C" int32_t sys_net_bnet_recv(int32_t s,void* b,uint32_t n,int32_t f) { return core(s,b,n,f); }
extern "C" int32_t sys_net_bnet_recvfrom(int32_t s,void* b,uint32_t n,int32_t f,sys_net_sockaddr* from,uint32_t* size) { assert((uintptr_t)from==0x5678 && (uintptr_t)size==0x9abc); return core(s,b,n,f); }
"""
MAIN = r"""
static ps3_net_probe_stats stats;
static ps3_net_probe_record rows[256];
static int pull() { return ps3_net_probe_poll(rows,256,&stats); }
static ppu_context context() {
    ppu_context c{};c.gpr[3]=7;c.gpr[4]=0x1234;c.gpr[5]=12;c.gpr[6]=4;c.gpr[7]=0x5678;c.gpr[8]=0x9abc;
    c.lr=0x1122334455667788ULL;c.thread_id=0x100000009ULL;return c;
}
static void invoke(void(*fn)(ppu_context*),int32_t result) {
    auto c=context(),saved=c;core_result=result;errno=before_error;
    int calls=core_calls;
#ifdef PROBE_FAKE_WINDOWS
    fake_system=41;fake_socket=42;
#endif
    fn(&c);assert(core_calls==calls+1);
    assert(c.gpr[3]==(uint64_t)(int64_t)result);assert(fixture_errno_peek()==987);
    c.gpr[3]=saved.gpr[3];assert(!std::memcmp(&c,&saved,sizeof(c)));
#ifdef PROBE_FAKE_WINDOWS
    assert(fake_system==111 && fake_socket==222);
#endif
}
static void arm() {
    ps3_net_probe_script_report(0,0,0,0x4000,0x4000,8);
    assert(pull()==1 && rows[0].kind==PS3_NET_PROBE_SCRIPT_REPORT);
    assert(stats.state==PS3_NET_PROBE_ACTIVE && stats.accepted==1);
}
int main(int argc,char** argv) {
    assert(argc==2);std::string mode=argv[1];
    if(mode!="off") setenv("PS3_NET_PROBE_ARM_SCRIPT",mode=="invalid"?"-1":"0",1);
    if(mode=="budget") setenv("PS3_NET_PROBE_BUDGET","1",1);
    assert(ps3_net_probe_init()==(mode!="off" && mode!="invalid"));
    if(mode=="off"||mode=="invalid") {
        int clocks=clock_calls;invoke(h_send,5);invoke(h_sendto,12);invoke(h_recv,(int32_t)0x80010223u);invoke(h_recvfrom,0);
        assert(clock_calls==clocks && probe_load(&s_probe.lock)==0);
        assert(pull()==0 && stats.state==PS3_NET_PROBE_OFF);return 0;
    }
    if(mode=="cross-arm") {
        arm_inside=true;invoke(h_send,5);assert(pull()==1);
        assert(rows[0].kind==PS3_NET_PROBE_SCRIPT_REPORT && stats.admitted==0);return 0;
    }
    if(mode=="marker") {
        ps3_net_probe_script_report(1,0,0,0x4000,0x4000,8);
        ps3_net_probe_script_report(0,1,0,0x4000,0x4000,8);
        ps3_net_probe_script_report(0,0,1,0x4000,0x4000,8);
        ps3_net_probe_script_report(0,0,0,0,0,8);
        ps3_net_probe_script_report(0,0,0,0x10000,0x10000,8);
        ps3_net_probe_script_report(0,0,0,0x4000,0,8);
        ps3_net_probe_script_report(0,0,0,0x4000,0x4000,0);
        assert(pull()==0 && stats.state==PS3_NET_PROBE_WAIT);arm();
        ps3_net_probe_script_report(0,0,0,0x4000,0x4000,8);assert(pull()==0);return 0;
    }
    arm();
    if(mode=="api") {
        invoke(h_send,5);invoke(h_sendto,12);invoke(h_recv,0);
        invoke(h_recvfrom,(int32_t)0x80010209u);
        for(int i=0;i<500;i++)invoke(h_recvfrom,(int32_t)0x80010223u);
        assert(pull()==4 && stats.filtered_wouldblock==500 && stats.admitted==504 && !stats.in_flight);
        assert(rows[0].api_result==5 && rows[1].api_result==12 && rows[2].api_result==0);
        for(int i=0;i<4;i++) {
            assert(rows[i].operation==(uint32_t)i+1);
            assert(rows[i].lr_raw==0x1122334455667788ULL && rows[i].thread_id==0x100000009ULL);
            assert(rows[i].socket_slot==7 && rows[i].requested==12);
        }
    } else if(mode=="late") {
        auto c=context();auto ticket=probe_enter(&c,PS3_NET_PROBE_SEND,7,12,4);
        assert(ticket.active);now_ms+=60001;probe_leave(&ticket,9);
        assert(pull()==1 && rows[0].late && !stats.in_flight && stats.state==PS3_NET_PROBE_CLOSED);
        invoke(h_send,7);assert(pull()==0 && stats.admitted==1);
    } else if(mode=="window") {
        now_ms+=60001;invoke(h_send,5);assert(pull()==0 && stats.state==PS3_NET_PROBE_CLOSED && stats.admitted==0);
    } else if(mode=="budget") {
        invoke(h_send,1);invoke(h_send,2);assert(pull()==1);
        assert(stats.budget_drop==1 && stats.incomplete && stats.state==PS3_NET_PROBE_CAPPED);
    } else if(mode=="full") {
        for(int i=0;i<260;i++)invoke(h_send,i);
        assert(pull()==256 && stats.full_drop==4 && stats.incomplete);
        for(int i=0;i<256;i++)assert(rows[i].api_result==i && rows[i].sequence==(uint64_t)i+2);
    } else if(mode=="contend") {
        assert(probe_lock());invoke(h_send,4);probe_unlock();
        assert(pull()==0 && stats.contention_drop==1 && stats.incomplete && !stats.in_flight);
    } else if(mode=="threads") {
        std::vector<std::thread> threads;
        for(int i=0;i<4;i++)threads.emplace_back([] {
            auto c=context();for(int j=0;j<1000;j++) {auto t=probe_enter(&c,PS3_NET_PROBE_SEND,7,12,4);probe_leave(&t,j);}
        });
        for(auto& t:threads)t.join();int n=pull();assert(n>=0 && n<=256);
        assert(stats.admitted==4000 && stats.returned==4000 && !stats.in_flight);
        assert(stats.accepted-1+stats.full_drop+stats.contention_drop+stats.budget_drop==4000);
        for(int i=1;i<n;i++)assert(rows[i].sequence==rows[i-1].sequence+1);
    } else assert(!"unknown mode");
    return 0;
}
"""
PAD_MAIN = r"""
static int observations, nonordinary_seen;
static void observe(uint32_t port,uint32_t index,uint32_t kind,uint32_t mask,uint32_t final,int32_t len) {
    assert(port==0 && index==0 && mask==CELL_PAD_CTRL_CROSS);
    if(kind!=0){++nonordinary_seen;return;}
    assert(len>0 && (final&mask)==mask && vm_read32(0x100)==(uint32_t)len);
    assert((buttons()&mask)==mask);++observations;
}
static void clear_buttons(u32 port,u16* b,u16* x,u16* y) {(void)port;(void)x;(void)y;*b=0;}
int SDL_NumJoysticks(void) { return 0; }
volatile long g_savedata_done,g_savedata_begun;
unsigned long long ppu_fs_idle_ms(void) { return 0; }
int main(int argc,char** argv) {
    assert(argc==2);const char* mode=argv[1];
    setenv("PAD_SCRIPT","0:0x4000:4",1);
    if(!strcmp(mode,"zero"))setenv("PAD_SCRIPT","0:0:4",1);
    if(!strcmp(mode,"high"))setenv("PAD_SCRIPT","0:0x10000:4",1);
    if(!strcmp(mode,"nonordinary"))setenv("PAD_SCRIPT","+0:0x4000:4",1);
    vm_base=mmap(NULL,0x10000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    assert(vm_base!=MAP_FAILED);ppu_vm_size=0x10000;s_pad_initialized=1;s_max_connect=7;
    assert(g_cellpad_script_report_observer==NULL && g_cellpad_script_report_index==-1);

    g_cellpad_script_report_index=!strcmp(mode,"wrong-index")?1:0;
    g_cellpad_script_report_observer=observe;
    if(!strcmp(mode,"filtered"))g_cellpad_filter=clear_buttons;
    if(!strcmp(mode,"already-held")) {
        setenv("PAD_SCRIPT","1:0x4000:4",1);
        s_sdl_controllers[0]=(SDL_GameController*)&device;attached=1;device_buttons=CELL_PAD_CTRL_CROSS;
        assert(read_pad(0)>0 && buttons()==CELL_PAD_CTRL_CROSS && observations==0);
        fake_ms+=1000;assert(read_pad(0)>0 && observations==1);return 0;
    }
    if(!strcmp(mode,"drain")||!strcmp(mode,"expired")) {
        /* Prime freshness without allowing the selected script yet. */
        setenv("PAD_SCRIPT","1:0x4000:4",1);assert(read_pad(0)>0);fake_ms+=1000;
        pad_poll_backend();s_data_fresh[0]=0;
        assert(read_pad(0)==0 && observations==0);
        if(!strcmp(mode,"expired")){for(int i=0;i<10;i++)read_pad(0);next_report();read_pad(0);assert(observations==0);return 0;}
        next_report();assert(read_pad(0)>0 && observations==1);
    } else {
        assert(read_pad(0)>0);
        assert(observations==(!strcmp(mode,"filtered")||!strcmp(mode,"wrong-index")||!strcmp(mode,"zero")||!strcmp(mode,"high")||!strcmp(mode,"nonordinary")?0:1));
        if(!strcmp(mode,"nonordinary"))assert(nonordinary_seen==1);
    }
    puts("PASS actual cellPad final-report marker");return 0;
}
"""


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--sdk", type=Path, required=True)
    p.add_argument("--sdl-config", default="sdl2-config")
    p.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    p.add_argument("--cc", default=os.environ.get("CC", "cc"))
    p.add_argument("--mutations", action="store_true")
    args = p.parse_args()
    sdk = args.sdk.resolve()
    source = (sdk / "libs/network/sysNet.c").read_text()
    wrappers = "\n".join(
        re.findall(
            r"^static void h_(?:send|sendto|recv|recvfrom)\(.*$", source, re.MULTILINE
        )
    )
    assert wrappers.count("PROBE_SOCKET_RETURN(") == 4
    assert "jsp_net_probe.cpp" in (PORT / "CMakeLists.txt").read_text()
    assert "jsp_register_net_probe();" in (PORT / "stubs.cpp").read_text()
    include = [
        "-I" + str(sdk / "include"),
        "-I" + str(sdk / "runtime/ppu"),
        "-I" + str(sdk / "runtime/platform"),
        "-I" + str(sdk / "libs/network"),
    ]
    clean = {
        k: v
        for k, v in os.environ.items()
        if not k.startswith(("PS3_NET_PROBE_", "PAD_", "YDKJ_"))
    }
    inc = (sdk / "libs/network/sysNet_probe.inc").read_text()
    checks = 0
    with tempfile.TemporaryDirectory(prefix="net-probe-test-") as temp:
        root = Path(temp)

        def suite(text, label, mutant=False):
            cpp = root / (label + ".cpp")
            exe = root / label
            cpp.write_text(text)
            subprocess.run(
                [
                    args.cxx,
                    "-std=gnu++20",
                    "-O1",
                    "-pthread",
                    *include,
                    str(cpp),
                    "-o",
                    str(exe),
                ],
                check=True,
            )
            failures = []
            for mode in [
                "off",
                "invalid",
                "marker",
                "cross-arm",
                "api",
                "late",
                "window",
                "budget",
                "full",
                "contend",
                "threads",
            ]:
                run = subprocess.run(
                    [str(exe), mode],
                    check=False,
                    env=clean,
                    capture_output=True,
                    text=True,
                    timeout=20,
                )
                if run.returncode:
                    failures.append(mode)
                if run.returncode and not mutant:
                    raise AssertionError(label + " " + mode + "\n" + run.stderr)
            if mutant:
                assert failures, label + " mutant escaped"
            print(
                label
                + ": "
                + (
                    "rejected " + ",".join(failures)
                    if mutant
                    else "11 source-derived cases pass"
                ),
                flush=True,
            )

        text = (
            PREFIX
            + "\n"
            + inc
            + "\n"
            + PEEK
            + "\n"
            + CORE
            + "\n"
            + wrappers
            + "\n"
            + MAIN
        )
        suite(text, "native")
        checks += 11
        suite("#define PROBE_FAKE_WINDOWS 1\n" + text, "fake-windows")
        checks += 11
        if args.mutations:
            suite(
                text.replace("probe_leave(&ticket, result);", "(void)ticket;"),
                "missing-leave",
                True,
            )
            suite(
                text.replace(
                    "port != 0 || kind != 0 ||", "port != 0 || kind != 0 || 1 ||"
                ),
                "missing-marker",
                True,
            )
            suite(
                text.replace("if (now >= s_probe.deadline) {", "if (0) {"),
                "missing-window",
                True,
            )
            win = "#define PROBE_FAKE_WINDOWS 1\n" + text
            suite(
                win.replace("WSASetLastError(e.socket);", "(void)e.socket;"),
                "missing-wsa-restore",
                True,
            )
            suite(
                win.replace("SetLastError(e.system);", "(void)e.system;"),
                "missing-system-restore",
                True,
            )
            save_first = win.replace(
                "    ProbeErrors e;\n#ifdef _WIN32",
                "    ProbeErrors e;\n    e.crt = errno;\n#ifdef _WIN32",
            ).replace(
                "    e.crt = errno; // CRT accessor may itself change OS error stores\n",
                "",
            )
            suite(save_first, "crt-save-first", True)
            restore_last = win.replace(
                "    errno = e.crt; // restore CRT before the final OS setters\n", ""
            ).replace(
                "    SetLastError(e.system);\n#endif\n}",
                "    SetLastError(e.system);\n#endif\n    errno = e.crt;\n}",
            )
            suite(restore_last, "crt-restore-last", True)
        pad = (
            (sdk / "libs/input/tests/test_pad_file_reports.c")
            .read_text()
            .split("int main(", 1)[0]
        )
        pad = pad.replace(
            '#include "../cellPad.c"',
            '#include "' + str(sdk / "libs/input/cellPad.c") + '"',
        )
        c = root / "pad.c"
        c.write_text(pad + PAD_MAIN)
        exe = root / "pad"
        sdl = Path(args.sdl_config)
        cflags = shlex.split(subprocess.check_output([str(sdl), "--cflags"], text=True))
        libs = shlex.split(subprocess.check_output([str(sdl), "--libs"], text=True))
        # Split Nix outputs keep headers beside the config script, not --prefix.
        if sdl.is_absolute():
            cflags = ["-I" + str(sdl.resolve().parents[1] / "include"), *cflags]
        subprocess.run(
            [
                args.cc,
                "-std=gnu17",
                "-O1",
                "-UNDEBUG",
                "-ffunction-sections",
                "-fdata-sections",
                *include,
                *cflags,
                str(c),
                "-Wl,--gc-sections",
                "-pthread",
                "-lm",
                *libs,
                "-o",
                str(exe),
            ],
            check=True,
        )
        for mode in [
            "default",
            "drain",
            "expired",
            "filtered",
            "wrong-index",
            "zero",
            "high",
            "nonordinary",
            "already-held",
        ]:
            run = subprocess.run(
                [str(exe), mode],
                check=False,
                env=clean,
                capture_output=True,
                text=True,
                timeout=10,
            )
            assert not run.returncode, mode + "\n" + run.stdout + run.stderr
            checks += 1
        print("actual full cellPad:9 marker/provenance cases pass", flush=True)
        print(
            json.dumps(
                {
                    "cases": checks,
                    "sdk_source_sha256": {
                        name: hashlib.sha256((sdk / name).read_bytes()).hexdigest()
                        for name in [
                            "libs/network/sysNet.c",
                            "libs/network/sysNet_probe.inc",
                            "libs/input/cellPad.c",
                        ]
                    },
                }
            )
        )


if __name__ == "__main__":
    main()
