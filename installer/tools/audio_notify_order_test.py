"""Exercise patch 3005's tick-locked producer and idle query while notification and receive are in flight."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

PATCH = Path(__file__).resolve().parents[1] / "port/patches/3005-audio-tick-lock.patch"


def block(source, start):
    begin = source.index(start)
    opening = source.index("{", begin)
    depth = 1
    for end in range(opening + 1, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if depth == 0:
            return source[begin:end + 1]
    raise ValueError(f"Incomplete patch block at {start}")


def main(patch):
    added = "\n".join(line[1:] for line in patch.read_text().splitlines()
                      if line.startswith("+") and not line.startswith("+++"))
    idle = block(added, "int ps3_audio_lock_idle(void)")
    producer = block(added, "if (g_ps3_audio_lock_target >= 0)")
    fixture = r'''
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <mutex>
#include <thread>
constexpr int CELL_AUDIO_MAX_NOTIFY_EVENT_QUEUES = 1, SYS_EVENT_QUEUE_MAX = 1, CELL_AUDIO_BLOCK_SAMPLES = 256;
struct sys_event_queue_info { bool active; int count, waiters; std::mutex lock; };
thread_local std::promise<void>* observing = nullptr;
void signal_observer_progress() {
    if (observing) {
        observing->set_value();
        observing = nullptr;
    }
}
void mutex_lock(std::mutex* lock) {
    signal_observer_progress();
    lock->lock();
}
void mutex_unlock(std::mutex* lock) { lock->unlock(); }
sys_event_queue_info g_sys_event_queues[] = {{true, 0, 1}};
struct { bool in_use; uint32_t key; } s_notify_queues[] = {{true, 1}};
uint32_t sys_event_find_queue_by_key(uint32_t key) { return key; }
long long g_ps3_audio_lock_done = 0, g_ps3_audio_lock_target = 1;
int g_ps3_audio_lock_mute = 0, s_lock_wake = 0, s_mix_buffer = 0;
void audio_mix_one_block() {}
int audio_backend_room() { return CELL_AUDIO_BLOCK_SAMPLES; }
void audio_backend_submit(int, int) {}
void QueryPerformanceCounter(int*) {}
void WaitForSingleObject(int, int) { assert(false); }
std::mutex gate;
std::condition_variable changed;
bool inside_notify = false, allow_notify = false;
void audio_notify_event_queues() {
    std::unique_lock<std::mutex> lock(gate);
    inside_notify = true;
    changed.notify_one();
    changed.wait(lock, [] { return allow_notify; });
    std::lock_guard<std::mutex> queue_lock(g_sys_event_queues[0].lock);
    ++g_sys_event_queues[0].count;
}
''' + idle + r'''
void mix_one_iteration() {
    int q0 = 0, blocks = 1;
    do {
''' + producer + r'''
    } while (false);
}
int main() {
    std::thread mixer(mix_one_iteration);
    bool premature;
    {
        std::unique_lock<std::mutex> lock(gate);
        changed.wait(lock, [] { return inside_notify; });
        premature = ps3_audio_lock_idle();
        allow_notify = true;
    }
    changed.notify_one();
    mixer.join();
    if (premature) return 10;
    if (g_ps3_audio_lock_done != 1 || ps3_audio_lock_idle()) return 11;
    auto& queue = g_sys_event_queues[0];
    // Pause the receiver between its two decrements, while it owns the queue.
    // The observer either attempts the queue lock or returns on the old unlocked path.
    std::unique_lock<std::mutex> receiver(queue.lock);
    --queue.count;
    std::promise<void> progress;
    auto observer_progress = progress.get_future();
    bool idle_during_receive = false;
    std::thread observer([&] {
        observing = &progress;
        idle_during_receive = ps3_audio_lock_idle();
        signal_observer_progress();
    });
    observer_progress.get();
    --queue.waiters;
    receiver.unlock();
    observer.join();
    if (idle_during_receive) return 14;
    if (ps3_audio_lock_idle()) return 12;
    queue.waiters = 1;
    return ps3_audio_lock_idle() ? 0 : 13;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / "audio.cpp"
        binary = Path(tmp) / "audio"
        source.write_text(fixture)
        subprocess.run([*shlex.split(os.environ.get("CXX", "c++")),
                        "-std=c++17", "-pthread", str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    main(Path(sys.argv[1]) if len(sys.argv) == 2 else PATCH)
