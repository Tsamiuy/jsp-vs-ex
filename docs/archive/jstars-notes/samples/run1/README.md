# samples/run1 (2026-10-20): no sampler output

Demo run, 330 s, default build (c3eaf8b + kick-wait main.cpp), env `PAD_SCRIPT=31:0x4000 PERF=1
RSX_FRAMELOG=60 PS3_SAMPLE=2`. `stdout.txt` / `stderr.txt` are the full logs (PERF lines are valid).

**There are no `[samp]` lines.** `PS3_SAMPLE` is read by `ps3_sampler_start()`
(`ps3recomp-src\runtime\ppu\ppu_loader.cpp`), and only the toolkit's boot harness
(`runtime\ppu\tests\boot_main.cpp:1225`) calls it. `jstars\port\main.cpp` never does, so the
sampler never starts in this port. A sampler run needs that one call added to `main.cpp` first.
Note: the sampler prints a report early and then every 10 s, so a killed run still has reports.
