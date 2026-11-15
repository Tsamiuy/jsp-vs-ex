#!/usr/bin/env python3
"""Run the authored player LOD/shadow hooks with the real settings store and fake VM.

No generated game code, original game data, Windows API or executable is used.
This tests selected hook effects, not complete state coverage or application ACKs.
Run with a C++20 compiler: python3 installer/tools/session_reader_effects_test.py
Add --mutations to challenge the session override and replacement-server controls.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def function(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + '\n'


PREFIX = r'''
#include "jsp_session_reader.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
extern "C" int jsp_setting_gen(void);
extern "C" void jsp_setting_set_live(const char*, const char*);
static std::map<uint32_t, uint32_t> memory;
static uint32_t retired_object = 0;
static uint32_t vm_read32(uint32_t p) {
    if (retired_object && p >= retired_object && p < retired_object + 0x70)
        throw std::runtime_error("read from retired LOD object");
    return memory[p];
}
static void vm_write32(uint32_t p, uint32_t v) {
    if (retired_object && p >= retired_object && p < retired_object + 0x70)
        throw std::runtime_error("write to retired LOD object");
    memory[p] = v;
}
static const char* exp_env(const char*) { return nullptr; } // public non-experiments build
static void put_float(uint32_t p, float f) { uint32_t u; std::memcpy(&u, &f, 4); vm_write32(p, u); }
static float get_float(uint32_t p) { uint32_t u = vm_read32(p); float f; std::memcpy(&f, &u, 4); return f; }
static int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line%d: %s\n", __LINE__, #x); } } while (0)
'''

MAIN = r'''
int main(int argc, char** argv)
{
    using namespace jpss;
    const bool max_distance = argc > 1 && !std::strcmp(argv[1], "max");
    constexpr uint32_t object = 0x10000, server = 0x30000, pointer = 0x90000;
    vm_write32(object, 0x008843D0);
    put_float(object + 0x50, 30); put_float(object + 0x54, 60);
    put_float(object + 0x5c, 100); vm_write32(object + 0x58, 1);
    vm_write32(0x008D3888 + 0x464c, pointer); vm_write32(pointer, server);
    put_float(server + 0xf4, 150);

    // Ordinary local settings still reach the actual authored hooks.
    jsp_setting_set_live("MODEL_DETAIL", "4");
    jsp_setting_set_live("OBJECT_DISTANCE", max_distance ? "0" : "2");
    jsp_setting_set_live("SHADOW_RANGE", "2");
    lod_select_hook(object); shadow_range_tick();
    CHECK(get_float(object + 0x50) == 120);
    CHECK(get_float(object + 0x54) == 240);
    CHECK(get_float(object + 0x5c) == (max_distance ? 100 : 200));
    CHECK(vm_read32(object + 0x58) == (max_distance ? 0u : 1u));
    CHECK(get_float(server + 0xf4) == 300);
    const auto local = reader_snapshot();
    CHECK(!local.state.active);

    // A replacement server can start with a legitimate value numerically
    // equal to the old server's scaled value. Normal must preserve its own
    // original instead of restoring the old object's150 into this object.
    constexpr uint32_t replacement = 0x40000;
    vm_write32(pointer, replacement);
    put_float(replacement + 0xf4, 300);

    // The startup witness is synthetic here; no real pipeline state is proved.
    const Token token{700, 1, 0, 3};
    const ReaderStartup startup{true, conservative()};
    CHECK(reader_begin(token.session) == Result::Ok);
    CHECK(reader_install(token, conservative(), startup) == Result::Ok);
    const auto active = reader_snapshot();
    CHECK(active.state.active && active.state.token == token);
    lod_select_hook(object); shadow_range_tick();
    CHECK(get_float(object + 0x50) == 30);
    CHECK(get_float(object + 0x54) == 60);
    CHECK(get_float(object + 0x5c) == 100);
    CHECK(vm_read32(object + 0x58) == 1);
    CHECK(get_float(replacement + 0xf4) == 300);
    CHECK(reader_current(active.state));

    // A new untouched object at normal settings retains its own values.
    constexpr uint32_t fresh = 0x20000;
    vm_write32(fresh, 0x008843D0);
    put_float(fresh + 0x50, 11); put_float(fresh + 0x54, 22);
    put_float(fresh + 0x5c, 33); vm_write32(fresh + 0x58, 1);
    lod_select_hook(fresh);
    CHECK(get_float(fresh + 0x50) == 11 && get_float(fresh + 0x5c) == 33);

    // Local edits under the temporary layer do not change its actual effects.
    jsp_setting_set_live("MODEL_DETAIL", "2");
    jsp_setting_set_live("OBJECT_DISTANCE", "0.75");
    jsp_setting_set_live("SHADOW_RANGE", "2");
    lod_select_hook(object); shadow_range_tick();
    CHECK(get_float(object + 0x50) == 30 && get_float(object + 0x5c) == 100);
    CHECK(get_float(replacement + 0xf4) == 300);
    CHECK(!reader_current(active.state));
    CHECK(!std::strcmp(std::getenv("MODEL_DETAIL"), "2"));
    auto bad_startup = startup; bad_startup.applied.choices[(size_t)Key::Pipe] = 1;
    auto newer = token; ++newer.revision;
    const auto before_failure = reader_snapshot();
    CHECK(reader_install(newer, conservative(), bad_startup) == Result::RestartRequired);
    CHECK(reader_current(before_failure.state));
    lod_select_hook(object); shadow_range_tick();
    CHECK(get_float(object + 0x50) == 30 && get_float(replacement + 0xf4) == 300);

    // Clearing restores the latest local preferences through the same hooks.
    CHECK(reader_clear(token) == Result::Ok);
    lod_select_hook(object); shadow_range_tick();
    CHECK(get_float(object + 0x50) == 60);
    CHECK(get_float(object + 0x54) == 120);
    CHECK(get_float(object + 0x5c) == 75);
    CHECK(get_float(replacement + 0xf4) == 600);
    CHECK(reader_install(token, conservative(), startup) == Result::NotStaged);

    // Preserve the existing 48-byte reader truncation, including this alias:
    // the trailing4 does not fit, atof sees spaces and the existing clamp is.25.
    const std::string custom = std::string(47, ' ') + "4";
    jsp_setting_set_live("MODEL_DETAIL", custom.c_str());
    lod_select_hook(object);
    CHECK(get_float(object + 0x50) == 7.5f);
    CHECK(get_float(object + 0x54) == 15);
    CHECK(!std::strcmp(std::getenv("MODEL_DETAIL"), custom.c_str()));
    // Object/backend lifetime and all-object completion remain separate proof.
    std::printf("real authored hook effects: %d checks, %d failures (fake VM)\n", checks, failures);
    return failures ? 1 : 0;
}
'''


REWRITE_MAIN = r'''
static void put_object(uint32_t o, float d0, float d1, float cull, uint32_t flag = 1)
{
    vm_write32(o, 0x008843D0); put_float(o+0x50,d0); put_float(o+0x54,d1);
    put_float(o+0x5c,cull); vm_write32(o+0x58,flag);
}
static void expect_object(uint32_t o, float d0, float d1, float cull, uint32_t flag = 1)
{
    CHECK(get_float(o+0x50)==d0); CHECK(get_float(o+0x54)==d1);
    CHECK(get_float(o+0x5c)==cull); CHECK(vm_read32(o+0x58)==flag);
}
int main(int argc, char** argv)
{
    using namespace jpss;
    if (argc != 2) return 2;
    const std::string mode=argv[1];
    constexpr uint32_t first=0x10000, second=0x20000;
    const uint32_t target=mode=="other" ? second : first;
    put_object(first,30,60,100); put_object(second,70,80,90);
    jsp_setting_set_live("MODEL_DETAIL","4"); jsp_setting_set_live("OBJECT_DISTANCE","2");
    lod_select_hook(first); if(mode=="other") lod_select_hook(second);
    if(mode=="partial" || mode=="partial-gen") {
        // Unchanged d0 is still our scaled output; only d1/cull/flag changed.
        put_float(first+0x54,22); put_float(first+0x5c,33); vm_write32(first+0x58,0);
        if(mode=="partial-gen") jsp_setting_set_live("FPS_COUNTER","0");
        lod_select_hook(first); expect_object(first,120,88,66,0);
    } else {
        const float fresh0=mode=="same-first" ? 30.0f : 11.0f;
        if(mode=="before" || mode=="other") put_object(target,fresh0,22,33);
        if(mode=="other") {
            jsp_setting_set_live("FPS_COUNTER","0");
            lod_select_hook(first); // sweep must reconcile every tracked entry
            expect_object(second,44,88,66);
        }
        const Token token{701,1,0,3}; const ReaderStartup startup{true,conservative()};
        CHECK(reader_begin(token.session)==Result::Ok);
        CHECK(reader_install(token,conservative(),startup)==Result::Ok);
        lod_select_hook(target);
        if(mode=="retired") {
            expect_object(first,30,60,100); // retirement follows actual normal restoration
            retired_object=first;
            jsp_setting_set_live("FPS_COUNTER","0");
            try { lod_select_hook(second); }
            catch (const std::runtime_error& error) {
                std::printf("FAIL retired-object policy: %s\n",error.what());
                return 1;
            }
            expect_object(second,70,80,90);
            std::printf("LOD retired-object no-read policy: %dchecks,%dfailures\n",checks,failures);
            return failures ? 1 : 0;
        }
        if(mode!="before" && mode!="other") {
            expect_object(target,30,60,100);
            put_object(target,fresh0,22,33); // new stage after normal restoration
            lod_select_hook(target);
        }
        expect_object(target,fresh0,22,33);
        jsp_setting_set_live("MODEL_DETAIL","2"); jsp_setting_set_live("OBJECT_DISTANCE","0.75");
        lod_select_hook(target); expect_object(target,fresh0,22,33);
        CHECK(reader_clear(token)==Result::Ok);
        lod_select_hook(target); expect_object(target,fresh0*2,44,24.75f);
    }
    std::printf("LOD rewrite %s: %dchecks,%dfailures\n",argv[1],checks,failures);
    return failures ? 1 : 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutations', action='store_true')
    args = parser.parse_args()
    port = Path(__file__).resolve().parent.parent / 'port' / 'jsp'
    game = (port / 'jsp_game.cpp').read_text()
    lod_prefix = game[game.index('struct LodOrig {'):game.index('void lod_select_hook(')]
    core = (PREFIX + function(game, 'float session_setting_float(') + lod_prefix
            + function(game, 'void lod_select_hook('))
    source = core + function(game, 'void shadow_range_tick(') + MAIN
    with tempfile.TemporaryDirectory(prefix='jsp-session-effects-') as folder:
        temp = Path(folder)
        test = temp / 'effects.cpp'
        test.write_text(source)
        store = port / 'jsp_settings.cpp'
        variants = [('production', store)]
        if args.mutations:
            altered = store.read_text().replace('if (g_session_layer.active()) {', 'if (false) {', 1)
            assert altered != store.read_text(), 'session bypass mutation did not apply'
            mutant = temp / 'settings-mutant.cpp'
            mutant.write_text(altered)
            variants.append(('bypass-session', mutant))
            # Removing only the new server-identity guard recreates the old
            # cache hazard while leaving the real session lookup connected.
            old_cache = source.replace('if (srv != range_server) {', 'if (srv != range_server && false) {', 1)
            assert old_cache != source, 'old-cache control did not apply'
            old_test = temp / 'old-cache.cpp'
            old_test.write_text(old_cache)
        for label, unit in variants:
            exe = temp / label
            subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            '-pthread', '-I', str(port), str(test), str(unit), '-o', str(exe)], check=True)
            for arguments in ([], ['max']):
                result = subprocess.run([str(exe), *arguments], check=False)
                if (result.returncode == 0) != (label == 'production'):
                    raise SystemExit(f'{label} {arguments}: unexpected result {result.returncode}')
                print(f'{label} {arguments}: expected result {result.returncode}', flush=True)
        rewrite = temp / 'rewrite.cpp'
        rewrite.write_text(core + REWRITE_MAIN)
        exe = temp / 'rewrite'
        subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                        '-pthread', '-I', str(port), str(rewrite), str(store), '-o', str(exe)], check=True)
        for mode in ('after', 'before', 'other', 'partial', 'partial-gen', 'same-first', 'retired'):
            subprocess.run([str(exe), mode], check=True)
        if args.mutations:
            exe = temp / 'old-shadow-cache'
            subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            '-pthread', '-I', str(port), str(old_test), str(store), '-o', str(exe)], check=True)
            result = subprocess.run([str(exe)], check=False)
            if result.returncode == 0:
                raise SystemExit('old shadow cache incorrectly passed replacement-server control')
            print(f'old shadow cache: expected failure {result.returncode}', flush=True)
            cull_write = 'vm_write32(kv.first + 0x58, od == 0.0f ? 0 : lo.flag);'
            missing_cull = source.replace(cull_write, '', 1)
            assert missing_cull != source, 'cull-reset mutation did not apply'
            cull_test = temp / 'missing-cull.cpp'
            cull_test.write_text(missing_cull)
            exe = temp / 'missing-cull'
            subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            '-pthread', '-I', str(port), str(cull_test), str(store), '-o', str(exe)], check=True)
            result = subprocess.run([str(exe), 'max'], check=False)
            if result.returncode == 0:
                raise SystemExit('missing cull restoration incorrectly passed MAX-to-host-normal control')
            print(f'missing cull restoration: expected failure {result.returncode}', flush=True)
            bad_ledger = core.replace('if (refreshed) g_lod_orig.clear();', '(void)refreshed;', 1)
            assert bad_ledger != core, 'retained-ledger mutation did not apply'
            bad = temp / 'retained-ledger.cpp'
            bad.write_text(bad_ledger + REWRITE_MAIN)
            exe = temp / 'retained-ledger'
            subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            '-pthread', '-I', str(port), str(bad), str(store), '-o', str(exe)], check=True)
            result = subprocess.run([str(exe), 'retired'], check=False)
            if result.returncode == 0:
                raise SystemExit('retained ledger incorrectly passed retired-object no-read policy')
            print(f'retained ledger: expected failure {result.returncode}', flush=True)
            missing_sweep = re.sub(
                r'lod_original_changes\(lo, vm_read32\(kv.first \+ 0x50\),.*?prior_od\);',
                '(void)prior_od;', core, count=1, flags=re.S)
            assert missing_sweep != core, 'sweep-reconciliation mutation did not apply'
            sweep = temp / 'missing-sweep.cpp'
            sweep.write_text(missing_sweep + REWRITE_MAIN)
            exe = temp / 'missing-sweep'
            subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            '-pthread', '-I', str(port), str(sweep), str(store), '-o', str(exe)], check=True)
            for mode in ('before', 'other', 'partial-gen'):
                result = subprocess.run([str(exe), mode], check=False)
                if result.returncode == 0:
                    raise SystemExit(f'missing sweep reconciliation incorrectly passed {mode}')
                print(f'missing sweep {mode}: expected failure {result.returncode}', flush=True)


if __name__ == '__main__':
    main()
