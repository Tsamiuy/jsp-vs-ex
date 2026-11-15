#!/usr/bin/env python3
"""Compile the real startup bridge against a fake runtime observer, not getenv.

No renderer/Windows API/title is exercised. The backend observer has its own
production-source test; these controls challenge bridge/capability wiring only.
"""
import os
from pathlib import Path
import subprocess
import tempfile


TEST = r'''
#include "jsp_session_reader.h"
#include <cstdio>
#include <cstdlib>
static int effective, calls, checks, failures;
extern "C" int rsx_d3d12_pipe_effective_state(void) { ++calls; return effective; }
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; std::printf("FAIL%d:%s\n", __LINE__, #c); } } while(0)
int main()
{
    using namespace jpss;
    const bool capable = EXPECT_CAPABLE != 0;
    for (int state : {-2,-1,0,1,2}) {
        effective = state; calls = 0;
        // Deliberately disagree with actual state; configuration is not used.
        setenv("PIPE", state == 1 ? "0" : "1", 1);
        const auto observation = reader_startup();
        const bool known = capable && (state == 0 || state == 1);
        CHECK(observation.available == known);
        CHECK(calls == (capable ? 1 : 0));
        if (known) {
            CHECK(observation.applied.choices[(size_t)Key::Pipe] == state);
            CHECK(observation.applied.choices[(size_t)Key::Render60] == 0);
            SessionLayer layer;
            CHECK(layer.install(conservative(), observation.applied) ==
                  (state == 1 ? Result::Ok : Result::RestartRequired));
        }
    }
    effective = 0;
    const auto before_close = reader_startup();
    effective = -1;
    const auto closed = reader_startup();
    CHECK(before_close.available == capable);
    CHECK(!closed.available); // a fresh read, not a cached lifetime claim
    std::printf("startup bridge: %dchecks,%dfailures (fake runtime)\n", checks, failures);
    return failures ? 1 : 0;
}
'''


def main():
    port = Path(__file__).resolve().parent.parent / 'port' / 'jsp'
    unit = port / 'jsp_session_startup.cpp'
    variants = [('win-capable', ['-D_WIN32', '-DJSP_SESSION_RENDER60_ABSENT=1'], True),
                ('win-unattested', ['-D_WIN32'], False),
                ('win-unknown-cap', ['-D_WIN32', '-DJSP_SESSION_RENDER60_ABSENT=0'], False),
                ('other-platform', ['-DJSP_SESSION_RENDER60_ABSENT=1'], False)]
    with tempfile.TemporaryDirectory(prefix='jsp-startup-bridge-') as folder:
        temp = Path(folder)
        test = temp / 'test.cpp'
        test.write_text(TEST)
        for name, defines, capable in variants:
            exe = temp / name
            subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            '-I', str(port), *defines, f'-DEXPECT_CAPABLE={int(capable)}',
                            str(test), str(unit), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
        original = unit.read_text()
        altered = original.replace('const int state = rsx_d3d12_pipe_effective_state();', 'const int state = 0;', 1)
        assert altered != original
        mutant = temp / 'constant-state.cpp'
        mutant.write_text(altered)
        exe = temp / 'constant-state'
        subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                        '-I', str(port), '-D_WIN32', '-DJSP_SESSION_RENDER60_ABSENT=1', '-DEXPECT_CAPABLE=1',
                        str(test), str(mutant), '-o', str(exe)], check=True)
        result = subprocess.run([str(exe)], check=False)
        if result.returncode == 0:
            raise SystemExit('constant startup state incorrectly passed')
        print('constant startup-state mutation rejected', flush=True)


if __name__ == '__main__':
    main()
