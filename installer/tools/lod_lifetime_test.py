#!/usr/bin/env python3
"""Compile actual authored LOD hooks/callbacks with the real store and fake VM.

Run inside the pinned Nix shell and shared build lock. This exercises host ledger
ordering, not title engine publication, arbitrary copies or an application ACK.
"""
import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile

spec = importlib.util.spec_from_file_location("session_reader_effects_test",
                                            Path(__file__).with_name("session_reader_effects_test.py"))
reader_test = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader_test)
PREFIX, function = reader_test.PREFIX, reader_test.function

MAIN = r'''
static void init(uint32_t o, float a=30, float b=60, float c=100, uint32_t flag=1) {
    vm_write32(o,0x008843D0); put_float(o+0x50,a); put_float(o+0x54,b);
    put_float(o+0x5c,c); vm_write32(o+0x58,flag);
}
static ppu_context context(uint32_t o) {
    ppu_context c; for(int i=0;i<32;i++) c.gpr[i]=0x12340000u+i;
    c.gpr[3]=o; return c;
}
static void expect(uint32_t o,float a,float b,float c,uint32_t f=1) {
    CHECK(get_float(o+0x50)==a); CHECK(get_float(o+0x54)==b);
    CHECK(get_float(o+0x5c)==c); CHECK(vm_read32(o+0x58)==f);
}
int main(int argc,char** argv) {
    const bool max_only=argc>1 && !std::strcmp(argv[1],"max-only");
    constexpr uint32_t o=0x10000, other=0x20000;
    auto unbound=context(o); retired_object=o;
    jsp_lod_initialize_enter(&unbound); jsp_lod_retire_enter(&unbound); jsp_lod_select_enter(&unbound);
    CHECK(!jsp_lod_hooks_active()); CHECK(lod_hook_entries==0); retired_object=0;
    CHECK(jsp_lod_bind_loaded_image((const uint8_t*)"abc",3)==1);
    CHECK(jsp_lod_hooks_active()==bool(JSP_LOD_ENTRY_HOOKS_VERIFIED));
    jsp_setting_set_live("MODEL_DETAIL","4"); jsp_setting_set_live("OBJECT_DISTANCE","2");
    init(o); auto ctx=context(o); auto saved=ctx;
    const int before=lod_hook_entries;
    training_icall(0x003959C8,ctx.gpr);
    jsp_lod_select_enter(&ctx);
    CHECK(lod_hook_entries==before+1); // legacy indirect OR verified wrapper, exactly once
    CHECK(!std::memcmp(&ctx,&saved,sizeof(ctx)));
    expect(o,120,240,200);
    if (!JSP_LOD_ENTRY_HOOKS_VERIFIED) {
        // Disabled callback has no memory/register/cache effect; legacy input
        // handling stays active for an unknown image/manual vanilla lift.
        auto old=g_lod_orig.size(); jsp_lod_initialize_enter(&ctx); jsp_lod_retire_enter(&ctx);
        CHECK(g_lod_orig.size()==old); expect(o,120,240,200);
        CHECK(!std::memcmp(&ctx,&saved,sizeof(ctx)));
        std::printf("disabled callbacks: %d checks,%d failures\n",checks,failures);
        return failures ? 1 : 0;
    }
    if(!max_only) {
        jsp_lod_initialize_enter(&ctx);
        CHECK(g_lod_orig.empty());
        init(o,120,33,44,0); // first fresh original equals previous MD4 output
        jsp_lod_select_enter(&ctx); expect(o,480,132,88,0);
        CHECK(!std::memcmp(&ctx,&saved,sizeof(ctx)));
    }
    jsp_lod_initialize_enter(&ctx); init(o,30,60,100,1);
    jsp_setting_set_live("OBJECT_DISTANCE","0"); jsp_lod_select_enter(&ctx);
    CHECK(vm_read32(o+0x58)==0); // known originalflag1, now overwritten by MAX
    jsp_lod_initialize_enter(&ctx); init(o,11,22,33,0); // fresh legitimate flag0 equals old MAX output
    jsp_setting_set_live("MODEL_DETAIL","1"); jsp_setting_set_live("OBJECT_DISTANCE","1");
    jsp_lod_select_enter(&ctx); expect(o,11,22,33,0);
    if(max_only) {
        std::printf("MAX originalflag1/freshflag0: %d checks,%d failures\n",checks,failures);
        return failures ? 1 : 0;
    }
    const int normal_entries=lod_hook_entries;
    retired_object=o;
    jsp_lod_select_enter(&ctx); // normal fast path must make no guest access
    CHECK(lod_hook_entries==normal_entries+1);
    retired_object=0;
    jsp_setting_set_live("MODEL_DETAIL","4"); jsp_setting_set_live("OBJECT_DISTANCE","2");
    jsp_lod_select_enter(&ctx); jsp_lod_retire_enter(&ctx);
    retired_object=o; init(other); auto second=context(other);
    jsp_setting_set_live("SHADOW_RANGE","2"); jsp_lod_select_enter(&second);
    expect(other,120,240,200); retired_object=0;
    // The fake engine initializes under the same test lock. This intentionally
    // proves host container ordering only; real engine writers are not covered.
    constexpr int threads=4, loops=200;
    for(int i=0;i<threads;i++) init(0x40000+i*0x1000);
    std::vector<std::thread> workers;
    for(int i=0;i<threads;i++) workers.emplace_back([i] {
        auto c=context(0x40000+i*0x1000);
        for(int j=0;j<loops;j++) {
            jsp_lod_retire_enter(&c); jsp_lod_initialize_enter(&c);
            { std::lock_guard<std::mutex> lock(g_lod_mutex); init((uint32_t)c.gpr[3]); }
            if((j%7)==0) jsp_setting_set_live("SHADOW_RANGE",(j%2)?"1":"2");
            jsp_lod_select_enter(&c);
        }
    });
    for(auto& worker:workers) worker.join();
    for(int i=0;i<threads;i++) expect(0x40000+i*0x1000,120,240,200);
    CHECK(!std::memcmp(&ctx,&saved,sizeof(ctx))); jsp_lod_hook_leave(&ctx);
    CHECK(!std::memcmp(&ctx,&saved,sizeof(ctx)));
    retired_object=other;
    CHECK(jsp_lod_bind_loaded_image((const uint8_t*)"changed",7)==0);
    CHECK(!jsp_lod_hooks_active()); CHECK(g_lod_orig.empty());
    jsp_lod_select_enter(&second); // revoked callback must not access poisoned memory
    retired_object=0;
    std::printf("lifetime callbacks: %d checks,%d failures; %d contended iterations\n",checks,failures,threads*loops);
    return failures ? 1 : 0;
}
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    ap.add_argument("--mutations", action="store_true")
    args = ap.parse_args()
    port = Path(__file__).resolve().parents[1] / "port/jsp"
    game = (port / "jsp_game.cpp").read_text()
    prefix = game[game.index("struct LodOrig {"):game.index("void lod_select_hook(")]
    select = function(game, "void lod_select_hook(").replace("{", "{\n    ++lod_hook_entries;", 1)
    callbacks = "\n".join(function(game, 'extern "C" void ' + n + '(') for n in (
        "jsp_lod_initialize_enter", "jsp_lod_retire_enter", "jsp_lod_select_enter", "jsp_lod_hook_leave", "jsp_lod_lifetime_reset"))
    branch = function(game[game.index("int training_icall("):], "if (target == 0x003959C8u)")
    core = (PREFIX + '#include "jsp_lod_lifetime.h"\n' + "#include <thread>\n#include <vector>\nstruct ppu_context { uint64_t gpr[32]{}; };\n"
            + "static std::atomic<int> lod_hook_entries{0};\n"
            + function(game, "float session_setting_float(") + prefix + select + callbacks
            + "int training_icall(uint32_t target,uint64_t* gpr) {" + branch + "return 0;}\n" + MAIN)
    with tempfile.TemporaryDirectory(prefix="jsp-lod-lifetime-") as temp:
        variants = [("disabled", 0, core, [], True), ("verified", 1, core, [], True),
                    ("verified-max", 1, core, ["max-only"], True)]
        if args.mutations:
            for name, symbol, argv in (("no-initialize", "jsp_lod_initialize_enter", []),
                                        ("no-initialize-max", "jsp_lod_initialize_enter", ["max-only"]),
                                        ("no-retire", "jsp_lod_retire_enter", [])):
                old = function(game, 'extern "C" void ' + symbol + '(')
                new = old.replace("if (jsp_lod_hooks_active())", "if (false)", 1)
                assert old != new
                variants.append((name, 1, core.replace(old, new, 1), argv, False))
            duplicate = core.replace("if (!JSP_LOD_ENTRY_HOOKS_VERIFIED || !jsp_lod_hooks_active()) lod_select_hook", "if (true) lod_select_hook", 1)
            assert duplicate != core
            variants.append(("duplicate-indirect", 1, duplicate, [], False))
        for name, enabled, text, argv, succeeds in variants:
            source, exe = Path(temp) / f"{name}.cpp", Path(temp) / name
            (Path(temp)/"jsp_lod_hooks_build.h").write_text(f"#define JSP_LOD_ENTRY_HOOKS_VERIFIED {enabled}\n"
                '#define JSP_LOD_PROGRAM_SHA256 "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"\n'
                '#define JSP_LOD_PROGRAM_SEGMENT_SHA256 ""\n')
            source.write_text(text)
            subprocess.run([args.cxx, "-std=c++20", "-O1", "-pthread", "-I", str(temp), "-I", str(port), str(source),
                            str(port / "jsp_settings.cpp"), str(port / "jsp_lod_lifetime.cpp"), "-o", str(exe)], check=True)
            result = subprocess.run([str(exe), *argv], capture_output=True, text=True)
            assert (result.returncode == 0) == succeeds, f"{name}: {result.returncode}\n{result.stdout}\n{result.stderr}"
            print(name + ": " + (result.stdout.strip() if succeeds else "rejected as expected"))


if __name__ == "__main__":
    main()
