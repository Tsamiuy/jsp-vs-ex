#!/usr/bin/env python3
"""Host-check patch 7904 against an actual runtime before 7904 by default.

Usage: python3 installer/tools/pipe_state_test.py --runtime PATH/ps3recomp
Needs git and a native C compiler (CC, default gcc). No game/GPU/network.
Use --already-patched for the fully patched toolkit provided by CI.
Compiles the actual helper, pipe_on and lifecycle bodies with fake Windows/D3D
side effects. This proves neither Windows API behavior nor live worker health.
"""

import argparse
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def function(source, signature):
    start = source.index(signature + "\n{")
    end = source.index("\n}", start) + 2
    return source[start:end]


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise AssertionError(f"expected one source anchor: {old!r}")
    return source.replace(old, new, 1)


PREFIX = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t LONG;
typedef unsigned long DWORD;
typedef unsigned long long ULONGLONG;
typedef void* HANDLE;
typedef void* LPVOID;
#define WINAPI
#define FALSE 0
#define FRAME_COUNT 2
#define RSZ(n,s) ((u32)((n)*(s)))
static unsigned checks, failures;
static unsigned atomic_ops;
static void check(int ok, unsigned line, const char* expression)
{
    ++checks;
    if (!ok) { ++failures; fprintf(stderr, "FAIL %u: %s\n", line, expression); }
}
#define CHECK(x) check((x), __LINE__, #x)
static LONG InterlockedCompareExchange(volatile LONG* p, LONG value, LONG expected)
{
    ++atomic_ops;
    __atomic_compare_exchange_n(p, &expected, value, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}
static LONG InterlockedExchange(volatile LONG* p, LONG value)
{ ++atomic_ops; return __atomic_exchange_n(p, value, __ATOMIC_SEQ_CST); }
typedef struct { int unused; } RfFrame;
typedef struct Com Com;
typedef struct {
    void (*Unmap)(Com*, unsigned, const void*);
    unsigned (*Release)(Com*);
} Vtable;
struct Com { Vtable* lpVtbl; };
static struct {
    void* draws;
    u32 width, height, bb_w, bb_h;
    float clear_color[4];
    int dump_frames_left, dump_skip_left, initialized;
    HANDLE hwnd, fence_event;
    ULONGLONG last_fps_time;
    Com *vertex_buffer, *pipeline_state, *pipeline_state_lines, *pipeline_state_points;
    Com *depth_buffer, *dsv_heap, *root_signature, *fence, *cmd_list, *tex_ring;
    Com *cmd_allocators[FRAME_COUNT], *render_targets[FRAME_COUNT], *rtv_heap;
    Com *swap_chain, *cmd_queue, *device;
} s_d3d;
typedef struct {
    void *userdata, *init, *shutdown, *begin_frame, *end_frame, *present, *clear;
    void *set_render_target, *set_viewport, *set_blend, *set_depth_stencil;
    void *set_shader, *set_vertex_attribs, *draw_arrays, *draw_indexed, *bind_texture;
} rsx_backend;
static rsx_backend s_d3d12_backend;
static int s_draws_buf[2][1];
static float s_rs;
static int s_rs_zfull;
char g_rsx_title_base[128];
static int env_reads, event_calls, thread_calls, wait_calls, log_calls;
static int fail_event, fail_thread, fail_window, fail_d3d;
static int close_during_thread, nested_during_thread, producer_during_window;
static const char* env_keys[64];
static const char* env_values[64];
static unsigned env_count;
static void put(const char* key, const char* value)
{
    CHECK(env_count < 64);
    env_keys[env_count] = key; env_values[env_count++] = value;
}
static char* fake_getenv(const char* key)
{
    ++env_reads;
    for (unsigned i = env_count; i; --i)
        if (!strcmp(env_keys[i-1], key)) return (char*)env_values[i-1];
    return NULL;
}
static int fake_printf(const char* format, ...) { (void)format; ++log_calls; return 0; }
static int fake_fprintf(FILE* stream, const char* format, ...)
{ (void)stream; (void)format; ++log_calls; return 0; }
#define getenv fake_getenv
#define printf fake_printf
#define fprintf fake_fprintf
int rsx_d3d12_pipe_effective_state(void);
void rsx_d3d12_backend_shutdown(void);
static void d3d12_shutdown(void*);
static int pipe_on(void);
static DWORD WINAPI pipe_thread(LPVOID arg) { (void)arg; return 0; }
static HANDLE CreateEventA(void* a, int b, int c, const char* d)
{
    (void)a; (void)b; (void)c; (void)d;
    CHECK(rsx_d3d12_pipe_effective_state() == -1);
    ++event_calls;
    return event_calls == fail_event ? NULL : (HANDLE)(uintptr_t)(event_calls + 10);
}
static HANDLE CreateThread(void* a, size_t b, DWORD (*fn)(LPVOID), void* c, DWORD d, DWORD* e)
{
    (void)a; (void)b; (void)fn; (void)c; (void)d; (void)e;
    CHECK(rsx_d3d12_pipe_effective_state() == -1);
    ++thread_calls;
    if (nested_during_thread) {
        nested_during_thread = 0;
        (void)pipe_on();
        CHECK(rsx_d3d12_pipe_effective_state() == -1);
    }
    if (close_during_thread == 1) rsx_d3d12_backend_shutdown();
    if (close_during_thread == 2) d3d12_shutdown(NULL);
    return fail_thread ? NULL : (HANDLE)(uintptr_t)100;
}
static DWORD GetThreadId(HANDLE h)
{
    (void)h;
    CHECK(rsx_d3d12_pipe_effective_state() == -1);
    return 123;
}
static void pipe_wait(void) { ++wait_calls; }
static void wait_for_gpu(void) { ++wait_calls; }
static void CloseHandle(HANDLE h) { (void)h; }
static void DestroyWindow(HANDLE h) { (void)h; }
static void jsp_overlay_set_output(int a, int b) { (void)a; (void)b; }
static int fx_full(void) { return 0; }
static HANDLE create_window(u32 a, u32 b, const char* c)
{
    (void)a; (void)b; (void)c;
    CHECK(rsx_d3d12_pipe_effective_state() == -1);
    if (producer_during_window) {
        (void)pipe_on();
        CHECK(rsx_d3d12_pipe_effective_state() == -1);
    }
    return fail_window ? NULL : (HANDLE)(uintptr_t)200;
}
static int init_d3d12(u32 a, u32 b) { (void)a; (void)b; return fail_d3d ? -1 : 0; }
static void rsx_set_backend(rsx_backend* p) { (void)p; }
static ULONGLONG GetTickCount64(void) { return 1; }
static void d3d12_init(void) {}
static void unused_callback(void) {}
#define d3d12_begin_frame unused_callback
#define d3d12_end_frame unused_callback
#define d3d12_present unused_callback
#define d3d12_clear unused_callback
#define d3d12_set_render_target unused_callback
#define d3d12_set_viewport unused_callback
#define d3d12_set_blend unused_callback
#define d3d12_set_depth_stencil unused_callback
#define d3d12_set_shader unused_callback
#define d3d12_set_vertex_attribs unused_callback
#define d3d12_draw_arrays unused_callback
#define d3d12_draw_indexed unused_callback
#define d3d12_bind_texture unused_callback
'''

SUFFIX = r'''
#undef printf
#undef fprintf
static void fresh(void)
{
    memset(&s_pipe, 0, sizeof s_pipe); s_pipe.on = -1;
    memset(&s_d3d, 0, sizeof s_d3d);
    __atomic_store_n(&s_pipe_witness, PIPE_WITNESS_NEW, __ATOMIC_SEQ_CST);
    env_count = 0;
    env_reads = event_calls = thread_calls = wait_calls = log_calls = 0;
    fail_event = fail_thread = fail_window = fail_d3d = 0;
    close_during_thread = nested_during_thread = producer_during_window = 0;
}
static void getter_is_pure(int expected)
{
    const int e = env_reads, events = event_calls, threads = thread_calls;
    const int waits = wait_calls, logs = log_calls;
    const unsigned atoms = atomic_ops;
    for (int i = 0; i < 20; ++i) CHECK(rsx_d3d12_pipe_effective_state() == expected);
    CHECK(e == env_reads && events == event_calls && threads == thread_calls && waits == wait_calls && logs == log_calls);
    CHECK(atomic_ops == atoms + 20);
}
static void ready(void)
{
    CHECK(rsx_d3d12_backend_init(1280, 720, "probe") == 0);
    CHECK(s_d3d.initialized == 1);
    getter_is_pure(-1);
}
int main(void)
{
    const char* requests[] = {NULL, "", "0", "1", "1extra", "01", "2", "true"};
    const int expected[] = {0, 0, 0, 1, 1, 0, 0, 0};
    for (unsigned i = 0; i < sizeof requests / sizeof requests[0]; ++i) {
        fresh(); if (requests[i]) put("PIPE", requests[i]);
        getter_is_pure(-1); ready();
        CHECK(pipe_on() == expected[i]); getter_is_pure(expected[i]);
        CHECK(thread_calls == expected[i] && event_calls == 2 * expected[i]);
        const int before = env_reads;
        const unsigned atoms = atomic_ops;
        CHECK(pipe_on() == expected[i] && env_reads == before);
        CHECK(atomic_ops == atoms); // Existing cached per-draw path adds no atomic operation.
    }
    const char* debug[] = {DEBUG_KEYS};
    for (unsigned i = 0; i < sizeof debug / sizeof debug[0]; ++i)
        for (unsigned j = 0; j < 3; ++j) {
            const char* values[] = {"", "0", "1"};
            fresh(); put("PIPE", "1"); put(debug[i], values[j]); ready();
            CHECK(pipe_on() == 0); getter_is_pure(0);
            CHECK(event_calls == 0 && thread_calls == 0);
        }
    for (int failure = 1; failure <= 3; ++failure) {
        fresh(); put("PIPE", "1");
        fail_event = failure <= 2 ? failure : 0; fail_thread = failure == 3;
        ready(); CHECK(pipe_on() == 0); getter_is_pure(0);
        CHECK(thread_calls == (failure == 3));
    }
    for (int on = 0; on <= 1; ++on) {
        fresh(); put("PIPE", on ? "1" : "0"); ready();
        CHECK(pipe_on() == on); getter_is_pure(on);
        rsx_d3d12_backend_shutdown(); getter_is_pure(-1);
        CHECK(rsx_d3d12_backend_init(1280, 720, "again") == 0);
        CHECK(pipe_on() == on); getter_is_pure(-1); // Cached old lifetime is not fresh evidence.
        fresh(); put("PIPE", on ? "1" : "0"); ready();
        CHECK(pipe_on() == on); d3d12_shutdown(NULL); getter_is_pure(-1);
    }
    for (int failure = 1; failure <= 2; ++failure) {
        fresh(); fail_window = failure == 1; fail_d3d = failure == 2;
        CHECK(rsx_d3d12_backend_init(1280, 720, "fail") == -1);
        getter_is_pure(-1); CHECK(pipe_on() == 0); getter_is_pure(-1);
    }
    fresh(); rsx_d3d12_backend_shutdown(); getter_is_pure(-1); ready();
    CHECK(pipe_on() == 0); getter_is_pure(-1);
    fresh(); ready(); CHECK(rsx_d3d12_backend_init(1280, 720, "repeat") == 0);
    CHECK(pipe_on() == 0); getter_is_pure(-1);
    for (int on = 0; on <= 1; ++on) {
        fresh(); put("PIPE", on ? "1" : "0"); CHECK(pipe_on() == on);
        getter_is_pure(-1); ready(); CHECK(pipe_on() == on); getter_is_pure(-1);
        fresh(); put("PIPE", on ? "1" : "0"); producer_during_window = 1;
        ready(); CHECK(pipe_on() == on); getter_is_pure(-1);
    }
    for (int route = 1; route <= 2; ++route) {
        fresh(); put("PIPE", "1"); ready(); close_during_thread = route;
        CHECK(pipe_on() == 1); getter_is_pure(-1); // Late producer cannot overwrite shutdown.
    }
    fresh(); put("PIPE", "1"); ready(); nested_during_thread = 1;
    CHECK(pipe_on() == 1); getter_is_pure(-1); // Unsupported concurrent/reentrant observation.
    fresh(); ready();
    CHECK(pipe_effective_observe_begin() == 1);
    CHECK(pipe_effective_observe_begin() == 0);
    pipe_effective_observe_end(1, 0); getter_is_pure(-1);
    for (int on = 0; on <= 1; ++on) {
        fresh(); put("PIPE", on ? "1" : "0"); ready(); CHECK(pipe_on() == on);
        // Model a caller that took on<0 before another initializer completed.
        // A late claim must invalidate even a known result before it writes a
        // different outcome, rather than leave OFF evidence with actual ON.
        CHECK(pipe_effective_observe_begin() == 0);
        s_pipe.on = !on;
        pipe_effective_observe_end(0, !on); getter_is_pure(-1);
    }
    printf("PIPE witness: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
'''


def harness(source):
    start = source.index("/* PIPE effective-state witness (7904).")
    end = source.index("/* End PIPE effective-state witness. */", start)
    helper = source[start:end]
    producer = function(source, "static int pipe_on(void)")
    debug = re.search(r"static const char\* const dbg\[\] = \{(.*?)NULL \};", producer, re.S)
    keys = re.findall(r'"([A-Z0-9_]+)"', debug[1])
    if not keys:
        raise AssertionError("no actual producer debug exclusions found")
    struct_start = source.index("static struct {\n    int on;")
    struct_end = source.index("} s_pipe = { -1 };", struct_start) + len("} s_pipe = { -1 };")
    bodies = [function(source, s) for s in (
        "static void d3d12_shutdown(void* ud)",
        "int rsx_d3d12_backend_init(u32 width, u32 height, const char* title)",
        "void rsx_d3d12_backend_shutdown(void)",
    )]
    return PREFIX + source[struct_start:struct_end] + "\n" + helper + producer + "\n" + "\n".join(bodies) + SUFFIX.replace("DEBUG_KEYS", ", ".join('"' + k + '"' for k in keys))


def variants(source):
    yield "candidate", source, False
    yield "missing-ready", replace_once(source, "    pipe_effective_ready();\n", ""), True
    yield "missing-producer-publication", replace_once(source, "        pipe_effective_observe_end(observed, s_pipe.on);\n", "        (void)observed;\n"), True
    for signature in ("void rsx_d3d12_backend_shutdown(void)", "static void d3d12_shutdown(void* ud)"):
        body = function(source, signature)
        yield "missing-" + ("full" if signature.startswith("void") else "callback") + "-shutdown", source.replace(body, body.replace("    pipe_effective_close();\n", "", 1), 1), True
    yield "late-producer-overwrites-close", replace_once(source,
        "InterlockedCompareExchange(&s_pipe_witness, on ? PIPE_WITNESS_ON : PIPE_WITNESS_OFF, PIPE_WITNESS_OBSERVING);",
        "InterlockedExchange(&s_pipe_witness, on ? PIPE_WITNESS_ON : PIPE_WITNESS_OFF);"), True
    claim = "        const int observed = pipe_effective_observe_begin();\n"
    publish = "        pipe_effective_observe_end(observed, s_pipe.on);\n"
    yield "claim-after-producer", replace_once(replace_once(source, claim, ""), publish, claim + publish), True
    yield "publish-before-producer", replace_once(replace_once(source, publish, ""), claim, claim + publish), True
    ready = "    pipe_effective_ready();\n"
    init_begin = "    pipe_effective_begin();\n"
    yield "ready-before-backend", replace_once(replace_once(source, ready, ""), init_begin, init_begin + ready), True
    yield "stale-condition-keeps-known", replace_once(source,
        "if (before != PIPE_WITNESS_UNAVAILABLE)",
        "if (before != PIPE_WITNESS_OFF && before != PIPE_WITNESS_ON && before != PIPE_WITNESS_UNAVAILABLE)"), True
    yield "cached-path-polls-witness", replace_once(source, "    return s_pipe.on;\n",
        "    (void)rsx_d3d12_pipe_effective_state();\n    return s_pipe.on;\n"), True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", required=True, type=Path)
    parser.add_argument("--already-patched", action="store_true",
                        help="test the supplied patched runtime without applying 7904 again")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    patch = repo / "installer/port/patches/7904-pipe-effective-state-readback.patch"
    print("test SHA-256:", hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), flush=True)
    print("patch SHA-256:", hashlib.sha256(patch.read_bytes()).hexdigest(), flush=True)
    relative = Path("libs/video/rsx_d3d12_backend.c")
    compiler = shlex.split(os.environ.get("CC", "gcc"))
    with tempfile.TemporaryDirectory(prefix="jsp-pipe-state-") as temporary:
        work = Path(temporary)
        (work / relative.parent).mkdir(parents=True)
        for suffix in ("c", "h"):
            name = relative.with_suffix("." + suffix)
            (work / name).write_bytes((args.runtime / name).read_bytes())
        if not args.already_patched:
            subprocess.run(["git", "apply", "--check", str(patch)], cwd=work, check=True)
            subprocess.run(["git", "apply", str(patch)], cwd=work, check=True)
        source = (work / relative).read_text()
        header = (work / relative.with_suffix(".h")).read_text()
        if "int rsx_d3d12_pipe_effective_state(void);" not in header:
            raise AssertionError("missing public declaration")
        killed = 0
        for label, candidate, negative in variants(source):
            test = work / (label + ".c")
            binary = work / label
            test.write_text(harness(candidate))
            subprocess.run(compiler + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-missing-field-initializers", "-Wno-unused-function", str(test), "-o", str(binary)], check=True, timeout=60)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            expected = 1 if negative else 0
            if result.returncode != expected:
                raise AssertionError(f"{label}: expected exit {expected}, got {result.returncode}\n{result.stdout}\n{result.stderr}")
            if negative:
                killed += 1
            else:
                print(result.stdout.strip())
        stub = re.findall(r"int rsx_d3d12_pipe_effective_state\(void\)\s*\{\s*return -1;\s*\}", source)
        if len(stub) != 1:
            raise AssertionError("missing/unexpected non-Windows unavailable stub")
        test = work / "nonwindows.c"
        test.write_text(stub[0] + "\nint main(void) { return rsx_d3d12_pipe_effective_state() != -1; }\n")
        subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror", str(test), "-o", str(work / "nonwindows")], check=True, timeout=60)
        subprocess.run([str(work / "nonwindows")], check=True, timeout=20)
        print(f"PIPE witness: {killed} production-wiring mutants rejected; non-Windows unavailable stub passes")
        print("patch SHA-256:", hashlib.sha256(patch.read_bytes()).hexdigest())


if __name__ == "__main__":
    main()
