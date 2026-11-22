"""Exercise patch 3007's actual output helper with a bounded fake WASAPI queue."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import sys
import tempfile

PATCH = Path(__file__).resolve().parents[1] / "port/patches/3007-audio-lock-output-cushion.patch"


def main(patch):
    text = patch.read_text()
    begin = text.index("+#ifdef _WIN32\n")
    end = text.index("\n #ifdef _WIN32", begin)
    helper = "\n".join(line[1:] for line in text[begin:end].splitlines() if line.startswith("+"))
    # Windows long is 32-bit even on 64-bit systems. Preserve long long and fixed-width types.
    helper = re.sub(r"(?<=static )long\b|(?<=const )long\b|(?<=\()long(?=\))", "int32_t", helper)
    fixture = r"""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#define _WIN32 1
#define AUDIO_BACKEND_WASAPI 1
#define CELL_AUDIO_BLOCK_SAMPLES 256
#define CELL_AUDIO_SAMPLE_RATE 48000
#define CHECK(c, n) do { if (!(c)) { fprintf(stderr, "audio output control %d failed at %d\n", n, __LINE__); return n; } } while (0)
typedef uint32_t u32;
static u32 s_wasapi_buf_frames = 2880;
static int queued, reopen_cap = -1, submissions, overflows;
static int s_lk_fill_ms = 60;
static float s_mix_buffer[512];
static float last_output[(2 * CELL_AUDIO_BLOCK_SAMPLES + 16) * 2];
static u32 last_n;
static int audio_backend_room(void) {
    if (reopen_cap >= 0) { s_wasapi_buf_frames = (u32)reopen_cap; queued = 0; reopen_cap = -1; }
    return s_wasapi_buf_frames ? (int)s_wasapi_buf_frames - queued : -1;
}
static void audio_backend_submit(const float* frames, u32 n) {
    if (n > sizeof(last_output) / (2 * sizeof(float)) || n > s_wasapi_buf_frames - (u32)queued) { overflows++; return; }
    for (u32 i = 0; i < n * 2; ++i) if (!isfinite(frames[i]) || fabsf(frames[i]) > 1.0f) overflows++;
    memcpy(last_output, frames, n * 2 * sizeof(float)); last_n = n; queued += (int)n; submissions++;
}
"""
    checks = r"""
static void reset(int smooth, int cap) {
    s_lk_smooth = smooth; s_lk_cushion_ms = 50; s_lk_drc = 0; s_lk_log = 0;
    s_lk_low_ms = 12; s_lk_conceal_ms = 150;
    queued = submissions = overflows = 0; reopen_cap = -1;
    s_wasapi_buf_frames = (u32)cap;
    for (int i = 0; i < 512; ++i) s_mix_buffer[i] = 0.5f;
    lk_begin();
}

static void prepare_history(void) {
    for (int i = 0; i < 4; ++i) lk_submit_block();
}

int main(void) {
    _Static_assert(INT_MAX == 2147483647, "fixture needs 32-bit int");
    reset(1, 2880);
    CHECK(lk_cushion_frames() == 576, 1);
    s_lk_cushion_ms = INT_MAX; CHECK(lk_cushion_frames() == 576, 2);
    s_lk_cushion_ms = INT_MIN; CHECK(lk_cushion_frames() == 0, 3);
    s_lk_cushion_ms = 4; CHECK(lk_cushion_frames() == 192, 4);
    lk_submit_block();
    CHECK(queued == 256 && submissions == 1 && !overflows, 5);
    CHECK(last_output[0] > 0 && last_output[0] < 0.01f && last_output[254] == 0.5f, 6);
    CHECK(s_lk_bursts == 1 && s_lk_have_last, 7);

    reset(1, 0); reopen_cap = 2880;
    lk_submit_block();
    CHECK(s_lk_bursts == 1 && s_lk_trough_min == 0 && queued == 256 && !overflows, 8);
    queued = 2700; lk_submit_block();
    CHECK(s_lk_squeezed == 1 && last_n == 180 && queued == 2880 && !overflows, 9);
    lk_submit_block(); CHECK(s_lk_drops == 1 && submissions == 2, 10);

    reset(0, 2880); lk_submit_block();
    CHECK(queued == 256 && last_output[0] == 0.5f && !overflows, 11);
    reset(1, 7200); s_lk_drc = 4; lk_submit_block();
    CHECK(last_n > 256 && last_n <= 260 && !overflows, 12);

    reset(1, 2880); prepare_history(); queued = 0;
    lk_idle_tail();
    CHECK(queued == 832 && s_lk_conc && s_lk_conceals == 1 && !overflows, 13);
    lk_submit_block(); CHECK(!s_lk_conc && s_lk_conc_frames == 832 + 128 && !overflows, 14);

    const int extremes[] = { 44739242, INT_MAX };
    for (unsigned i = 0; i < sizeof(extremes) / sizeof(extremes[0]); ++i) {
        reset(1, 2880); prepare_history(); queued = 2877;
        s_lk_low_ms = extremes[i]; lk_idle_tail();
        CHECK(queued == 2880 && last_n == 3 && !overflows && s_lk_conc_t == 3, 15);
    }
    reset(1, 2880); prepare_history(); queued = 0;
    s_lk_low_ms = INT_MIN; lk_idle_tail(); CHECK(!s_lk_conc && queued == 0, 16);

    float frame[2];
    s_lk_conceal_ms = INT_MAX;
    lk_conc_start(); lk_conc_frame(frame);
    CHECK(fabsf(frame[0] - 0.5f) < 0.0001f && fabsf(frame[1] - 0.5f) < 0.0001f, 23);
    s_lk_conc_t = INT32_MAX; lk_conc_frame(frame);
    CHECK(s_lk_conc_t == (int64_t)INT32_MAX + 1, 17);
    lk_conc_stop(); CHECK(s_lk_long_gaps == 0, 18);
    const int64_t limit = INT64_C(103079215056);
    lk_conc_start(); s_lk_conc_t = limit; lk_conc_stop();
    CHECK(s_lk_long_gaps == 0, 19);
    lk_conc_start(); s_lk_conc_t = limit + 1; lk_conc_stop();
    CHECK(s_lk_long_gaps == 1, 20);
    lk_conc_start(); s_lk_conc_t = INT64_MAX - 1;
    lk_conc_frame(frame); lk_conc_frame(frame);
    CHECK(s_lk_conc_t == INT64_MAX && frame[0] == 0.0f && frame[1] == 0.0f, 21);
    s_lk_conceal_ms = INT_MIN; lk_conc_start(); lk_conc_frame(frame);
    CHECK(frame[0] == 0.0f && frame[1] == 0.0f, 22);
    puts("PASS actual revision-two audio helper: room, rate, concealment and integer bounds");
    return 0;
}
"""
    with tempfile.TemporaryDirectory(prefix="jsp-audio-output-") as scratch:
        source = Path(scratch) / "probe.c"
        source.write_text(fixture + "\n" + helper + "\n" + checks)
        binary = Path(scratch) / ("probe.exe" if os.name == "nt" else "probe")
        compiler = shlex.split(os.environ.get("CC", "cc"))
        flags = shlex.split(os.environ.get("CFLAGS", ""))
        subprocess.run([*compiler, "-std=c11", "-O1", "-Wall", "-Wextra", *flags, str(source), "-o", str(binary), "-lm"], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main(Path(sys.argv[1]) if len(sys.argv) > 1 else PATCH)
