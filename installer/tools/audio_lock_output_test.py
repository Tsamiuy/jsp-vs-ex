"""Exercise patch 3007's actual output helper with a bounded fake WASAPI queue."""
from pathlib import Path
import os
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
static float s_mix_buffer[512];
static float last_output[528];
static u32 last_n;
static int audio_backend_room(void) {
    if (reopen_cap >= 0) { s_wasapi_buf_frames = (u32)reopen_cap; queued = 0; reopen_cap = -1; }
    return s_wasapi_buf_frames ? (int)s_wasapi_buf_frames - queued : -1;
}
static void audio_backend_submit(const float* frames, u32 n) {
    if (n > 264 || queued + (int)n > (int)s_wasapi_buf_frames) { overflows++; return; }
    for (u32 i = 0; i < n * 2; ++i) if (!isfinite(frames[i]) || fabsf(frames[i]) > 1.0f) overflows++;
    memcpy(last_output, frames, n * 2 * sizeof(float)); last_n = n; queued += (int)n; submissions++;
}
"""
    checks = r"""
static void reset(int smooth, int cap) {
    s_lk_smooth = smooth; s_lk_cushion_ms = 30; s_lk_drc = 2; s_lk_log = 0;
    queued = 0; submissions = overflows = 0; reopen_cap = -1; s_wasapi_buf_frames = (u32)cap;
    for (int i = 0; i < 512; ++i) s_mix_buffer[i] = 0.5f;
    lk_begin();
}
int main(void) {
    reset(1, 2880);
    CHECK(lk_cushion_frames() == 832, 40);
    lk_submit_block();
    CHECK(queued == 1088 && s_lk_sil_frames == 832 && !overflows, 41);
    CHECK(last_n == 256 && last_output[0] > 0.0f && last_output[0] < 0.01f && last_output[254] == 0.5f, 42);
    queued = 0; lk_idle_tail();
    CHECK(last_n == 256 && queued == 256 && last_output[0] > 0.49f && last_output[510] == 0.0f, 43);
    lk_idle_tail(); CHECK(queued == 256 && s_lk_tails == 1, 44);
    s_lk_burst = 1; lk_submit_block();
    CHECK(s_lk_underruns == 1 && queued == 1088 && !overflows, 45);
    /* The room query performs a real backend's pending device reopen. Capacity before it is stale/zero. */
    reset(1, 0); reopen_cap = 2880; lk_submit_block();
    CHECK(queued == 1088 && s_lk_sil_frames == 832 && s_lk_have_last && !overflows, 46);
    s_lk_cushion_ms = INT_MAX; CHECK(lk_cushion_frames() == 832, 47);
    s_lk_cushion_ms = INT_MIN; CHECK(lk_cushion_frames() == 0, 48);
    reset(0, 2880); lk_submit_block();
    CHECK(queued == 256 && s_lk_sil_frames == 0 && !overflows, 49);
    for (int drc = 0; drc <= 8; ++drc) {
        reset(1, 4800); queued = 440; s_lk_have_last = 1; s_lk_drc = drc;
        lk_submit_block();
        const int stretch = drc < 4 ? drc : 4;
        CHECK(last_n == (u32)(256 + stretch) && queued == 440 + 256 + stretch && !overflows, 50);
    }
    reset(1, 2880); queued = 2870; s_lk_have_last = 1; lk_submit_block();
    CHECK(s_lk_drops == 1 && submissions == 0 && !overflows, 51);
    puts("PASS actual audio output helper: bounded cushion, fade tail, recovery, bypass, interpolation and overflow controls");
    return 0;
}
"""
    with tempfile.TemporaryDirectory(prefix="jsp-audio-output-") as scratch:
        source = Path(scratch) / "probe.c"
        source.write_text(fixture + "\n" + helper + "\n" + checks)
        binary = Path(scratch) / ("probe.exe" if os.name == "nt" else "probe")
        compiler = shlex.split(os.environ.get("CC", "cc"))
        flags = shlex.split(os.environ.get("CFLAGS", ""))
        subprocess.run([*compiler, "-std=c11", "-O1", "-Wall", "-Wextra", *flags, str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main(Path(sys.argv[1]) if len(sys.argv) > 1 else PATCH)
