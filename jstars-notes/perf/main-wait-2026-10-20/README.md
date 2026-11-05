# main-wait diagnostic, 2026-10-20

`patches\main-wait\sampler-chain.patch` applied as ps3recomp-src commit 0f8af23 (diagnostic only,
opt-in `PS3_SAMPLE_CHAIN=1`). One 300 s run: `VBLANK_QPC=1 PS3_SAMPLE=2 PS3_SAMPLE_CHAIN=1
PS3_POLLTOP=5`, `PERF=1 RSX_FRAMELOG=20`. ~25.4 fps with the sampler on.

Main thread (last report, 87 samples, 72% waiting in ntdll), top chains:
- **59.8%** `spu_LS_00005F8C <- func_004FCE3C <- func_0032DDD8 <- func_006507DC` (all waiting)
- 5.7% `spu_LS_00005F8C <- func_005383D4 <- func_00536C04 <- func_003640B4`
- 2.3% `func_00074C7C <- func_004FCE3C <- func_0011D1EC <- func_0002897C`
- the rest 1 sample each, most through `func_004FCE3C`.

`sys_timer_usleep` poll sites (last 5 s window):
| calls | slept | tid | lr |
|---|---|---|---|
| 52,566 | 1,051 ms | 18 | 0x00538404 |
| 4,138 | 827 ms | 1 (main) | 0x00650820 |
| 3,901 | 780 ms | 1 | 0x006508A0 |
| 1,109 | 221 ms | 1 | 0x00650854 |
| 297 | 4,642 ms | 17 | 0x004D4F30 |

Also: 5,082 `[evt] port_send(port=3): queue 3 FULL -> EBUSY` lines in the run.
Logs: `stderr.txt`, `stdout.txt`. For analysis by the Vertex fetch thread.
