#!/usr/bin/env python3
"""Test actual runtime image binder and SHA-256 against authored bytes/hashlib."""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile

PORT = Path(__file__).resolve().parents[1] / "port/jsp"


def elf(at=120, payload=bytes(range(96))):
    out = bytearray(at + len(payload))
    out[:6] = b"\x7fELF\x02\x02"
    struct.pack_into(">Q", out, 32, 64)
    struct.pack_into(">Q", out, 24, 0x10000)
    struct.pack_into(">HH", out, 54, 56, 1)
    struct.pack_into(">IIQQQQQQ", out, 64, 1, 5, at, 0x10000, 0x10000, len(payload), len(payload)+16, 16)
    out[at:] = payload
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cxx", default="c++")
    ap.add_argument("--loader", type=Path, required=True)
    args = ap.parse_args()
    spec = importlib.util.spec_from_file_location("lod_hooks", PORT / "lod_hooks.py")
    helper = importlib.util.module_from_spec(spec); spec.loader.exec_module(helper)
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        base = elf()
        program = {"sha256": hashlib.sha256(base).hexdigest(), "segment_sha256": helper.segment_hash(base)}
        loader = args.loader.read_text()
        begin = loader.index('extern "C" { int (*g_ppu_image_load_hook)')
        end = loader.index('    /* Arm PPU_GUARD_EA', begin)
        # Compile the actual SDK loader through all PT_LOAD copies. Exclude only
        # the subsequent optional platform diagnostic threads and final logging.
        load_core = loader[begin:end] + "    return entry;\n}\n"
        assert load_core.index("g_ppu_image_load_hook(file") < load_core.index("memcpy(vm_base")
        cpp = r'''
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
static int resets=0;
extern "C" void jsp_lod_lifetime_reset(void) { ++resets; }
''' + '#include "' + str(PORT / 'jsp_lod_lifetime.cpp') + '"\n' + r'''
static uint8_t arena[0x20000],*vm_base=arena;
static uint64_t ppu_vm_size=sizeof(arena);
static int g_ppu_primary_priority,ppu_image_overlap_end;
static uint32_t g_tls_vaddr,g_tls_filesz,g_tls_memsz;
struct Range { uint64_t begin,end; };
static std::vector<Range> g_ppu_image_ranges;
template<class T> static void sys_memory_set_image_query(T) {}
static uint16_t be16(const uint8_t* p) { return (uint16_t(p[0])<<8)|p[1]; }
''' + load_core + r'''
int main(int argc,char** argv) {
    std::ifstream f(argv[2],std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),{});
    if(std::string(argv[1])=="loader") {
        g_ppu_image_load_hook=jsp_lod_bind_loaded_image;
        std::memset(arena,0xa5,sizeof(arena));
        bool accepted=std::string(argv[3])=="accept";
        auto entry=ppu_load_elf(argv[2]); assert(bool(entry)==accepted);
        assert(jsp_lod_hooks_active()==bool(accepted && JSP_LOD_ENTRY_HOOKS_VERIFIED));
        assert(arena[0x10000]==(accepted ? uint8_t(std::stoi(argv[4])):0xa5));
        std::memset(arena,0xa5,sizeof(arena));
        assert(ppu_load_elf("/does-not-exist/jsp-lod-synthetic-image")==0);
        assert(!jsp_lod_hooks_active()); assert(arena[0x10000]==0xa5);
        return 0;
    }
    if(std::string(argv[1])=="sha") {
        Sha256 h;for(size_t i=0;i<data.size();i+=37) h.update(data.data()+i,std::min(size_t(37),data.size()-i));
        std::cout<<h.finish(); return 0;
    }
    assert(!jsp_lod_hooks_active());
    bool match=std::string(argv[1])=="match";
    int accepted=jsp_lod_bind_loaded_image(data.data(),data.size());
    assert(accepted==(JSP_LOD_ENTRY_HOOKS_VERIFIED ? int(match):1));
    assert(jsp_lod_hooks_active()==(JSP_LOD_ENTRY_HOOKS_VERIFIED && match));
    assert(resets==1);
    jsp_lod_bind_loaded_image(nullptr,0); assert(!jsp_lod_hooks_active()); assert(resets==2);
    const uint8_t bad[]={1,2,3};
    assert(jsp_lod_bind_loaded_image(bad,sizeof(bad))==!JSP_LOD_ENTRY_HOOKS_VERIFIED);
    assert(!jsp_lod_hooks_active()); assert(resets==3);
}
'''
        source = temp / "guard.cpp"; source.write_text(cpp)
        checks = 0
        for enabled in (0, 1):
            helper.write_header(temp / helper.HEADER, enabled, {"program": program})
            exe = temp / f"guard{enabled}"
            subprocess.run([args.cxx, "-std=c++20", "-O1", "-I", str(temp), "-I", str(PORT), str(source), "-o", str(exe)], check=True)
            fixtures = [("match", base), ("match", elf(256)), ("mismatch", elf(payload=b"x"*96)),
                        ("mismatch", b"not ELF"), ("mismatch", base[:63])]
            for mode, data in fixtures:
                path = temp / "input"; path.write_bytes(data)
                subprocess.run([str(exe), mode, str(path)], check=True); checks += 1
                valid_elf = data[:6] == b"\x7fELF\x02\x02" and len(data) >= 120
                accepted = valid_elf and (not enabled or mode == "match")
                first = data[-96] if valid_elf else 0
                subprocess.run([str(exe), "loader", str(path), "accept" if accepted else "reject", str(first)],
                               check=True, capture_output=True); checks += 1
            for n in (0,1,3,55,56,63,64,65,127,128,129,511,1024,1000000):
                data = (bytes(range(256))*((n+255)//256))[:n]
                path = temp / "input"; path.write_bytes(data)
                result = subprocess.check_output([str(exe), "sha", str(path)], text=True)
                assert result == hashlib.sha256(data).hexdigest(); checks += 1
        # Delivery controls compile mutations of that exact authored SDK prefix.
        path = temp / "input"; path.write_bytes(base)
        for name, old, new in (
            ("no-revoke", 'if (g_ppu_image_load_hook) g_ppu_image_load_hook(nullptr, 0);', ''),
            ("no-buffer-check", 'if (g_ppu_image_load_hook && !g_ppu_image_load_hook(file, (size_t)sz))', 'if (false)')):
            changed = cpp.replace(old,new,1); assert changed != cpp
            mutant = temp / (name+".cpp"); mutant.write_text(changed)
            exe = temp / name
            subprocess.run([args.cxx,"-std=c++20","-O1","-I",str(temp),"-I",str(PORT),str(mutant),"-o",str(exe)],check=True)
            result = subprocess.run([str(exe),"loader",str(path),"accept","0"],capture_output=True)
            assert result.returncode != 0, name + " unexpectedly accepted"
            checks += 1
        print(f"PASS: {checks} runtime image/SHA checks (full/segment/changed/malformed/disabled/revoke)")


if __name__ == "__main__":
    main()
