# Fight freeze: cause and fix (2026-10-31)

## What players saw

The game froze during fights and the window stopped responding. Busier fights froze sooner: about 6 minutes after launch in a busy fight in normal play, 12 to 14 minutes in scripted fights. Older builds did it too, and demo runs were already stopping on 10-24.

## Cause

`libs/video/cellGcmSys.c` logged an inline upload message under a capped counter:

```c
static int _it = 0;
if (_it++ < 6)
    printf("[GCM2D] inline write ...");
```

The counter kept counting after the cap. It counted every inline word, about 88,000 per frame in fights by estimate, so it passed 2^31 within minutes and went negative. The check was then true for every word, so each word did a `printf`. The FIFO walker fell to about 120 words per second and flips stopped. Waits on jobs and flips, and the "guard still closed" lines, came after that.

## Fix

In the toolkit (`libs/video`, `libs/spurs`), 132 capped counters now stop at their cap: `n < 8 ? (n++, 1) : 0`. Output is otherwise unchanged. Four counters of the form `n++ < 8 || n % k == 0`, and counters in other parts of the code, were left; none is on the upload path, and they would overflow only after far longer.

The fix is part of `toolkit/ps3recomp-local.patch`. A toolkit built with `tools/toolkit/apply_toolkit.py` from this version or later has it. A toolkit built from an older patch freezes in long fights.

## Evidence

- A scripted run on the fixed build had no freeze in 30 minutes after launch, about 26 minutes of it in a fight, and that message printed exactly 6 times.
- A normal session of 20 minutes, about 18 of them in one fight with 330 hits, closed normally with no sign of the freeze.
- Before the fix: a freeze in every long run, at 6 to 18 minutes.
- The overflow itself was not watched directly. It is the explanation that fits all of the above.

## Ruled out

The performance log, frame log and flip count switches, and the indirect-call hook. A build from 10-26, before that hook existed, froze too.
