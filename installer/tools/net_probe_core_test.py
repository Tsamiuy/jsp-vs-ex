#!/usr/bin/env python3
"""Run existing actual sysNet/NID loopback coverage with recorder off and armed.

The relay choice is a controlled np_psnr stub; it tests the real sysNet early
return, not wire/relay delivery. No game or external network is used.
"""

import argparse
import os
import subprocess
import tempfile
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--sdk", type=Path, required=True)
    p.add_argument("--cc", default=os.environ.get("CC", "cc"))
    a = p.parse_args()
    sdk = a.sdk.resolve()
    source = (sdk / "libs/network/tests/test_sys_net.c").read_text()
    source = source.replace(
        '#include "../sysNet.c"',
        '#include "' + str(sdk / "libs/network/sysNet.c") + '"',
    )
    old = "int     np_psnr_p2p_route(const void* b, uint32_t l, const uint8_t ip[4], uint16_t p) { (void)b; (void)l; (void)ip; (void)p; return 0; }"
    assert source.count(old) == 1
    source = source.replace(
        old,
        "static int fixture_relay, fixture_sender_failed;\n"
        + old.replace(
            "return 0;",
            "if (fixture_relay) { fixture_sender_failed++; errno=EIO; return 1; } return 0;",
        ),
    )
    old = "int main(void)\n{"
    assert source.count(old) == 1
    source = source.replace(
        old,
        old
        + '\n    int capture = getenv("FIXTURE_CAPTURE") != NULL;\n    if (capture) { assert(ps3_net_probe_init()); ps3_net_probe_script_report(0,0,0,0x4000,0x4000,8); }',
        1,
    )
    old = '    assert(memcmp(vm_base + BUF, "ping", 4) == 0);'
    assert source.count(old) == 1
    source = source.replace(
        old,
        old
        + '\n    /* The real sendto(NULL) falls back to send, but has one wrapper record. */\n    if(capture) {\n        assert(call("sendto", c, BUF, 4, 0, 0, 0)==4);\n        assert(call("recv", srv, BUF, 16, 0, 0, 0)==4);\n    }',
        1,
    )
    final = """    if(capture) {
        int32_t relay_socket=C3("socket",SYS_NET_AF_INET,SYS_NET_SOCK_DGRAM_P2P,0);
        put_sockaddr(ADDR_A,LOOP,12345);fixture_relay=1;
        assert(call("sendto",relay_socket,BUF,3,0,ADDR_A,16)==3);
        assert(fixture_sender_failed==1);fixture_relay=0;
        assert(C1("socketclose",relay_socket)==0);
        ps3_net_probe_record records[256];ps3_net_probe_stats stats;
        int n=ps3_net_probe_poll(records,256,&stats);unsigned operations=0;
        assert(n>1 && records[0].kind==PS3_NET_PROBE_SCRIPT_REPORT && stats.online_registered);
        for(int i=1;i<n;i++)operations|=1u<<records[i].operation;
        assert((operations&0x1eu)==0x1eu && stats.filtered_wouldblock>0);
        assert(stats.admitted==stats.returned && !stats.in_flight && !stats.incomplete);
        assert(records[n-1].operation==PS3_NET_PROBE_SENDTO && records[n-1].api_result==3);
        puts("PASS actual sysNet cores/NID dispatch with armed probe and controlled route return");
    } else { ps3_net_probe_stats stats;assert(!ps3_net_probe_poll(NULL,0,&stats));assert(stats.state==PS3_NET_PROBE_OFF); }
"""
    at = source.rindex("    return 0;")
    source = source[:at] + final + source[at:]
    clean = {
        k: v
        for k, v in os.environ.items()
        if not k.startswith(("PS3_NET_PROBE_", "FIXTURE_CAPTURE", "PS3_NET_TRACE"))
    }
    with tempfile.TemporaryDirectory(prefix="net-probe-core-") as temporary:
        root = Path(temporary)
        path = root / "fixture.c"
        exe = root / "fixture"
        path.write_text(source)
        subprocess.run(
            [
                a.cc,
                "-std=gnu17",
                "-O1",
                "-UNDEBUG",
                "-I" + str(sdk / "include"),
                str(path),
                "-pthread",
                "-o",
                str(exe),
            ],
            check=True,
        )
        for mode in ["off", "on"]:
            env = dict(clean)
            if mode == "on":
                env.update(FIXTURE_CAPTURE="1", PS3_NET_PROBE_ARM_SCRIPT="0")
            result = subprocess.run(
                [str(exe)],
                check=False,
                env=env,
                capture_output=True,
                text=True,
                timeout=45,
            )
            assert not result.returncode, mode + "\n" + result.stdout + result.stderr
            print("actual sysNet/NID " + mode + ": PASS", flush=True)


if __name__ == "__main__":
    main()
