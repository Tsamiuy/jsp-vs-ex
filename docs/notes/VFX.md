# VFX: skill and other effects, how hard to fix (2026-10-23, read-only)

The maintainer asked whether the missing effects ("all the effects of skills and everything that is currently
missing") are easy to fix. Nothing was changed. Sources: COLOR_EXPOSURE.md Rounds 7-11, FINDINGS §38/§45,
the Athena and Yoshiwara pass captures, and the fight screenshots in `stages\`.

## What "effects" are made of in this game, and what our port does with each

J-Stars draws effects through one particle server (`OpParticleEffectWorkServer`) in several passes per frame:

| part | what it looks like | in our port |
|---|---|---|
| Particle colour pass (`RenderParticleColor`) | the effect sprites themselves: sparks, slashes, rings, smoke, trails | **drawn**: blended sprite draws in every fight capture (e.g. Athena intro ops 401-404, Yoshiwara fight; green arcs and blue aura visible in `stages\t0wrap3\s05\18`, `s09\18`) |
| Distortion pass | heat-haze / shock-wave ripple, into the grey 640x360 layer 0x0DB30000 | **drawn** (24 draws per Yoshiwara frame); whether the composite applies it correctly was never checked |
| Luminance / glow pass (`RenderLuminance`) | the bright glow and halo around effects, lanterns and panels, added twice in the HDR encode | **missing**: the glare layer 0x0E590000 is cleared every frame and never drawn into |
| (2026-11-02) | | The row above is out of date: the glow draws do run, see FINDINGS "Effect layers and the missing glow" |
| Sun shafts / bloom post | soft light from bright areas | partly; the quarter-res sky layer (STAGE_TEXTURES §10) is this family |

So effects are not entirely gone. The part that is definitely missing is the **glow**: every effect lacks its
bright core and halo, so beams, explosions and auras look flat and dim next to RPCS3. Attack effects in RPCS3
(e.g. the fire burst in `reference\rpcs3\Screenshot 2026-10-22 013736.png`) owe much of their look to that glow.

## The glow: how far the diagnosis is

From Rounds 7-11 (static reading of the recompiled PPU code plus the AI assistant's logs):
- The game does enter luminance mode, and the glow particles do submit draw records into the `pe::luminance`
  pass. The renderer executes every pass at frame flush, with no filter.
- So the glow draws die at one of three points: (a) the record's command descriptor is 0, so the record is
  skipped; (b) the record has empty vertex batches, so no FIFO draw is emitted; (c) the draw reaches our backend
  and a guard drops it.
- The decisive logging patches are ready but have never been run: `lumflag-trace-5.patch` (+ trace-4 and
  `lum-rt-drawlog.patch` with `LUM_RT=0E590000`). One Yoshiwara fight run tells which of (a)-(c) it is.

## Verdict

- **Is it easy?** Probably **medium**: one log run to find the point, then most likely a small fix. For (a) or
  (b) it is a one- or few-line change in the recompiled PPU code; for (c) it is a backend guard. Not a rewrite.
- **Risk: medium.** Turning the glow on changes brightness everywhere the game adds glow (lanterns, panels, sky
  and effects), because the HDR encode adds it twice. That overlaps the brightness work the maintainer parked for the end,
  and may need a second pass on exposure once it is on.
- **What the AI assistant would test:** (1) the trace-5 log run (no visual change); (2) after the fix, Yoshiwara,
  Hidden Leaf and Athena fights with special moves, against RPCS3 frames; fps (more draws into a full-screen
  layer); and that menus and the loading screen are unchanged.
- **Unknown:** whether any *specific* skill effect is missing entirely (sprite not drawn at all), as opposed to
  drawn without glow. That needs one example from the maintainer: a move in RPCS3 and the same move in our port.

## Update (21:20): skill effects are missing entirely, not just dull

The maintainer: "all skill effects are missing." Goku's laser never shows, and neither does any projectile or extra
effect of any character's skill. So this is a second problem, separate from (and bigger than) the glow layer.

### What the code says (checked)

The D3D12 backend only draws four primitive types through the vertex-program path: TRIANGLES, TRIANGLE_STRIP,
TRIANGLE_FAN and QUADS.
- `d3d12_draw_indexed` **skips** every other type ("skipping prim=%u (not wired)", printed only 3 times
  per run).
- `d3d12_draw_arrays` sends POINTS, LINES, LINE_STRIP, LINE_LOOP, QUAD_STRIP and POLYGON to the old
  fixed-function passthrough. That path applies **no vertex program**, so no camera transform, and no
  fragment program or texture. That geometry lands in the wrong place or is invisible.
- **Point sprites are not implemented at all:** SET_POINT_SPRITE_CONTROL is not even parsed into `rsx_state`.

Effect systems on PS3 commonly use exactly these: point-sprite particles, quad strips for beams and ribbon
trails, and lines for speed streaks. The game's ambient particles (sparks, smoke) go through QUADS and do show,
which fits "only the skill effects are missing" if those use the other types.

Ruled out from the logs: no fragment programs fail to decompile or compile in the latest tours (0 in
`dexp\s00`, `s05`, `s07`), and there are no dropped draws.

### Ranked causes

1. **Skill effects use primitive types the backend does not draw** (point sprites, quad strips, lines).
   Inferred from the code. The draw sampling log only ever saw prim 6 and 8, but it samples 1 call in 1000,
   so it cannot rule out others.
2. **The effect is drawn into an off-screen effect layer that is never composited, or composited wrong**
   (the distortion layer 0x0DB30000, the 640x360 effect layer 0x0DC20000).
3. **The effect geometry comes from an SPU job or an instanced draw (frequency divider) whose vertices we
   fetch wrong**, so it ends up degenerate or far off-screen.

### What the AI assistant should capture (one run)

Apply `patches\prim-log\` (logging only, `PRIM_LOG=1`). Then:
- Goku in a fight using the laser skill, a few times, plus one other character's projectile skill.
- In the same run, `RSX_CAP=<frame during the beam>:3:1 RSX_CAP_PASSES=1` (catch the beam on screen).

Reading:
- `[PRIM]` lines during the skill (prim 1, 2, 4, 9 or 10, with fp and target) confirm cause 1, and say which
  primitive types are needed.
- No `[PRIM]` lines: look in the capture manifests for draws into 0x0DB30000/0x0DC20000 during the beam
  (cause 2), or for draws with huge or zero-size geometry (cause 3).

### Verdict (revised)

- **If it is cause 1 (most likely): medium-to-large, but well understood.**
  - Quad strips and polygons are easy: expand them to triangles on the VP path, the same as quads and fans
    already are.
  - Lines need a line pipeline on the VP path.
  - Point sprites are the biggest piece. D3D12 has no point sprites, so each point must be expanded into a
    quad of the game's point size, with generated texture coordinates.
  - A few days of work across several patches, each testable on its own.
- **Risk: low for what works today.** Only draws that are currently not drawn at all would start drawing.
  The main risks are effects that appear the wrong size or position before point size and sprite
  coordinates are right, and some fps cost during heavy skills.
- **Testing:** every character's skills in a fight (Goku's laser first), compared with RPCS3; fps during
  heavy skills; menus and stages unchanged.
- **The glow layer** (section above) stays a separate, later fix. Skills will look dull until it is done too.

## Update (2026-10-24): the unsupported-shape idea is ruled out; where effects can be lost instead

The AI assistant's 600 s demo with `PRIM_LOG=1` (77c4672) logged **zero** POINTS/LINES/QUAD_STRIP/POLYGON draws.
Light-streak effects do appear (`perf\primlog\demo_skills.png`, 344/356/360 s, Goku vs Madara on Namek), so
some effect geometry reaches the screen through quads and strips. Cause 1 above is wrong.

Where a skill effect can still be lost, in pipeline order:

1. **Before the FIFO (game side).** Effects are particle-server records executed at frame flush, in the
   same executor as the glow (COLOR_EXPOSURE.md Round 11). A record is dropped silently if its command
   descriptor is 0, or if its eight vertex batches are empty. The glow records already die this way (or in
   the backend). Effect types built by the same vertex-build path (`func_00631C24` / the inline path, Round 10)
   would vanish the same way. Visible streaks would then be a different element type. This fits "glow and
   most skill effects missing" with a single cause. SPU-built effect geometry (one of the 18 lifted jobs)
   coming out empty would look the same from here.
2. **At the backend, invisible.** The draw arrives, but a shader or pipeline is not ready on first use. If
   `PSO_ASYNC` is on, a new effect shader can miss the short life of the skill. Other ways to be invisible:
   zero alpha, a blend into a layer that is never composited (0x0DB30000/0x0DC20000), or depth (effects
   drawn with depth export or depth-test states we map wrongly).
3. **Placed wrong:** instanced (frequency divider) or SPU-generated vertices fetched with the wrong
   stride or format, so the shape lands off-screen or degenerate.

### Next diagnostic (decides 1 against 2/3 in one run)

A player-controlled fight, Goku using the laser three times, with:
- `RSX_CAP=<frame just before the laser>:4:1 RSX_CAP_PASSES=1`, giving one frame without the beam and
  three with it;
- `[PERF5]` lines kept (async posted/skipped) and `PSO_ASYNC` noted;
- `lumflag-trace-5.patch` with its name filter widened from `pe::l*` to all `pe::*` passes (drop the
  `_nm[4] == 'l'` test), so `[LUMEXEC]` covers every particle pass.

Reading:
- The beam frames have extra draws in the manifest (new fp or texture codes, blend=1) but no beam on
  screen: it is cause 2 or 3, in the backend. Then inspect those ops' headers and pass images.
- The draw list barely changes, and `[LUMEXEC]` shows `fn=0` or `batches: none` records in a `pe::` pass
  during the laser: it is cause 1. The game drops them, which is the same fix as the glow, and fixing it
  should bring back both.
- `[PERF5]` shows async skipped > 0 during the skill: try `PSO_ASYNC=0` once.

### Verdict (re-revised)

Not known yet. If it is cause 1, it shares the glow fix: probably a small PPU-side fix once the trace
names the drop point, and **medium** effort overall. If it is in the backend (2/3), it is likely a targeted
fix of a similar size. Either way, the next step is one diagnostic run, not a big rewrite.

## Update (2026-10-24, Kamehameha capture): the beam draws run but output alpha 0, most likely from the vertex colour

The AI assistant's capture (FINDINGS "Goku's Kamehameha", `perf\scratch\laser\demo_async0\cap\frame_06`): the beam's
draws go into the distortion layer 0x0DB30000 (cleared to 0.498 grey) and the alpha layer 0x0DC20000 (cleared
to 0). Every post-draw snapshot stays unchanged. Reading the shaders and blend keys of those draws (checked):

- **0x0DB30000 draws**, code 0x9A64BF1C (e.g. `fp_op0571.hlsl`): `h0 = saturate(tex0 * col0 * 2)`, then
  `rgb = h0.rgb * 0.5`, `a = h0.a`. Blend key 0x122088B = colour SRC_ALPHA·src + ONE·dst (additive), alpha
  ZERO/ONE (alpha kept). 0x162088B is the same with a reverse-subtract colour op (negative offsets). t0 is
  DXT1 (alpha 1). **The layer can only stay at exactly 127 if `src.a = tex.a × col0.a × 2` is 0, so col0.a = 0.**
- **0x0DC20000 draws**, code 0xAD204EA4 (`fp_op0548.hlsl`): `out.a = saturate(tex0.a × col0.a × 2) × c0.x`,
  cmask=8 (alpha only), blend 0x1261043 = alpha ONE·src + INV_SRC_ALPHA·dst. Again **nothing reaches the layer
  unless col0.a > 0**. That fits the capture, which cannot show alpha anyway.
- Shaders are not the cause (they are straightforward). Neither is async building (PSO_ASYNC=0 gave the same).
  Depth could cull them too (the layers are depth-tested against the half-res depth 0x0D940000, rebuilt by
  op568 from the scene depth bytes), but that would not explain both layers staying exactly flat while
  every draw carries vertex colour × texture alpha.

So the beam's **vertex colour alpha (COLOR0.a) reaches the fragment program as 0** (inferred, strongly).
Possible reasons, ranked:

1. **The colour attribute is fetched wrong:** a packed-colour type or byte order we decode differently (UB
   vs UB256, ARGB vs RGBA), a frequency-divider/instanced colour (per-particle colour shared by 4 or 6
   vertices), or the attribute disabled so the constant register supplies 0.
2. **The vertex program builds the colour from per-particle constants** (relative addressing `c[a0.x + n]`),
   and we read the wrong constant (a0 handling, or constants outside the uploaded range).
3. **Depth culling against the rebuilt half-res depth** (only if 1 and 2 turn out clean).

### Next diagnostic (one run, Goku's laser, no code change)

- `VP_ATTRS=1` prints each new vertex layout (attribute type, size, stride, frequency, divider op). The
  beam's layout should be one of the few new ones that appear when the laser fires.
- `RSX_DBG_VP=04EEE600` (op571's t0, the beam texture) writes the beam's vertex program to `duck_vp.hlsl`.
  It only fires if that program is first compiled while this texture is bound; if not, take another beam
  t0 from the manifest (0x04732D80, 0x04FC4600).
- Optionally `DEPTH_OFF=1` once: if the beam appears (in the wrong depth order), it is cause 3 instead.

Reading: in the VP, check how `o[1]` (COL0) is written. If it comes from an attribute, check that
attribute's type and frequency in `[VPATTR]`. If it comes from `c[a0...]`, it is cause 2.

### Verdict (updated)

Likely a **targeted fix of small-to-medium size** in vertex fetch or the VP decompiler, once the run names the
attribute or constant. Fixing it should bring back skill effects broadly, because all of them in this capture
use vertex colour × texture. The glow layer stays separate.

## Resume here (paused 2026-10-24)

- Skill effects: the Kamehameha's draws run, but their alpha is `tex.a × col0.a`, so COLOR0.a must
  arrive as 0. The decisive run has been asked of the AI assistant but not done yet: Goku's laser with
  `VP_ATTRS=1 RSX_DBG_VP=04EEE600` (and optionally `DEPTH_OFF=1`). Read the `[VPATTR]` lines that appear
  when the laser fires, and `duck_vp.hlsl`, to see how COL0 is produced.
- The glow layer is separate: run `lumflag-trace-5` (COLOR_EXPOSURE.md Round 11) when brightness work resumes.

## Update (2026-10-24 afternoon): the beam's vertex buffers are empty when we draw them; the RSX doesn't wait

The AI assistant's `VP_CODE` run (`perf\scratch\laser\vpcode`): 40 beam draws (code 9A64BF1C), all `draw_arrays` QUADS
count=8, layout a0 float3 / a3 ubyte4 / a8 float2, stride 24, in **main memory** (IO offsets 0x0314xxxx).
**Every vertex byte is zero.** The VP copies `o[1] = v[3]`, so alpha = 0, and the quads are degenerate anyway.
The constants look sane (camera matrices at c[256..271]).

Who writes them, and why not before the draw (checked in code, plus inference):
- Particle quads in main memory that are rebuilt every frame are the classic SPU-job output. The PPU
  queues the draws in the FIFO, an SPU job fills the vertex buffer, and the FIFO **waits for it** with an
  NV406E semaphore ACQUIRE (`cellGcmSetWaitLabel`) on a label the SPU (or PPU) releases when the data is
  ready.
- **Our FIFO walker does not wait on ACQUIRE by default.** In `cellGcmSys.c` (NV406E 0x64/0x68/0x6C):
  RELEASE writes the label, but ACQUIRE only blocks when `GCM_SEMA_ACQUIRE=1`. That is off by default,
  because a label nothing writes would stall the drain forever. So we run straight past the wait and draw
  the particle buffers before the SPU job has written them. The visible streaks are probably effects whose
  vertices were already written (static, or written earlier in the frame).
- That also fits "all skill effects missing": every effect built by those jobs would be empty in the same
  way. The glow may be lost the same way (its records' batches empty, VFX.md cause 1).

Other possibilities, less likely:
2. The IO-to-EA mapping for 0x0314xxxx resolves to a different place than the one the producer writes
   (that happened once with the luminance target, §40). Data would then never appear at our address.
3. The producer never runs (an SPU job or particle stage failing). Then the label is never released
   either.

### Diagnostic (proposed, not run)

One laser run (demo with X at 40 s, as before) with `GCM_SEMA_ACQUIRE=1 GCM_RECDBG=1
VP_CODE=9A64BF1C VP_CODE_N=40` (vp-code build). This is an existing switch; no code change.
- The beam appears, and `[VPCODE]` bytes are non-zero: cause 1. The fix is to honour ACQUIRE, possibly with a
  timeout so that a label nothing writes cannot freeze the game (wait up to N ms, then go on and log it).
- The run hangs or stalls: the game waits on a label we never release. That points to 3, or to a release
  path we miss. `[SEMA]` lines (first 12) show the label and the expected value.
- Bytes still zero and no hang: cause 2 (address) or 3. Next would be `MEMPEEK` on the resolved EA over
  a few frames.

Risk of the eventual fix: medium. Blocking on labels can stall the drain if any label is released late,
so it needs a timeout and fps checks. But it is the hardware behaviour, and it may also fix other "data not
ready yet" glitches.
- (14:00) First `GCM_SEMA_ACQUIRE=1` run: no new effects. The maintainer says the blue shapes are a Madara effect that
  always showed. Separately, the second demo fight stops flipping (a freeze the maintainer also saw with ACQUIRE off),
  so that freeze is not caused by ACQUIRE. Waiting for the AI assistant's sema2/nosema2 runs (`VP_CODE_N=3000`) to see
  whether the beam's vertex bytes are non-zero with ACQUIRE on.

### (14:20) ACQUIRE ruled out; next: is anyone writing those addresses at all?

The AI assistant (sema2/nosema2, `VP_CODE_N=3000`): beam vertex bytes stay zero with and without `GCM_SEMA_ACQUIRE=1`
(32 vs 16 non-zero rows of ~18,000, which belong to other quads). So it is not a missing wait. Next comes case
(c): an address mismatch, or a producer that never runs.

Addresses: the beam's vertex arrays sit at IO offsets 0x0313xxxx and 0x0374xxxx (two buffers, likely double-
buffered by frame). The game's IO map is `ioAddr=0x45300000, ioSize=0x3A00000` (`_cellGcmInitBody` in
stdout). `cellGcmResolveIO` maps 1 MB pages through the EA table, so with the default linear map:
IO 0x03145820 → EA **0x48445820**, IO 0x03747130 → EA **0x48A47130**. The whole IO window is
EA 0x45300000-0x48D00000.

**MEMPEEK step (existing switches, no code change), one demo run with the Kamehameha:**
- `VRAMSCAN=1 VRAMSCAN_BASE=0x48000000 VRAMSCAN_FRAMES=<a>:<b>`: which 64 KB blocks of EA 0x48000000-
  0x49FFFFFF change from frame to frame over the beam window (512 blocks, 32 MB, covering both buffers and
  the end of the IO window).
- `MEMPEEK=48A47130:12 MEMPEEK_FRAMES=<a>:<b>`: the first beam vertex (pos + colour + uv = 24 bytes, 6
  words) at render time on each frame of the window. A second run with `MEMPEEK=48445820:12` covers the other
  buffer.
- `<a>:<b>`: the frame_count range around the beam in that run (it was 10210-10270 in `demo_async0`; with the
  X-at-40 s skip, take it from the VP_CODE run's timing, or use a wide 10000:10600).

Reading:
- The blocks around 0x484xxxxx/0x48Axxxxx **change** every frame, and MEMPEEK shows **non-zero** values at render
  time: data is written, but after we fetched it for the draw. That is a timing problem within the frame
  (an SPU job or PPU code finishing after the FIFO reached the draw, with some other sync we don't model).
- The blocks change, but MEMPEEK stays zero at our exact address: the producer writes nearby. The IO-to-EA
  mapping or the offset we fetch from is wrong. Compare the changed blocks with the beam's IO offsets.
- **Nothing** in that window changes during the beam: the producer is not writing into the IO window at
  all. It is dead (a failing SPU particle job or PPU stage), or it writes somewhere else and DMAs later with a
  transfer we don't emulate. Then VRAMSCAN over main memory near the effect heap would be next.

### (15:10) VP_CODE_LATER: the beam vertices never land at the EAs we read (later0/later2)

At draw time and at the end of the same frame, all 300 beam draws' first vertices are zero. Two frames later
165/300 hold heap-like junk (pointers 0x4875xxxx/0x48A5xxxx, small ints, FFFFFFFF). So the region is a
per-frame allocator that gets reused, and **the beam's vertices are never written there in our run**, at least
not between the draw and the end of that frame.

What can explain it:
1. **Written between the end of our frame and the next reuse** (the producer runs very late, e.g. a PPU job
   thread that lags). LATER=0 is too early and LATER=2 too late. Cheap to rule out: `VP_CODE_LATER=1`.
2. **The producer writes somewhere else**: we fetch from the wrong address. The vertex-array offsets
   come from SET_VERTEX_DATA_ARRAY_OFFSET with bit 31 = main memory, resolved through the IO table. If
   the game remaps IO pages (cellGcmMapEaIoAddress) after init, or the arrays use the other
   location, our EA differs from the producer's. The junk being pointers into the same 0x48xxxxxx window
   suggests the right *region* but the wrong *slot*. That could also be the vertex data base offset or the
   frame allocator's view of which buffer belongs to this frame.
3. **The producer never runs**: the particle vertex build (PPU `func_00631C24` / the inline path, Round 10,
   or an SPU job) fails or is skipped, and the buffer is allocated (and zeroed) but never filled. The glow
   would die the same way.

Next read-only checks (existing switches only):
- `VP_CODE_LATER=1` (same run as before): rules case 1 in or out.
- `SPU_DMA_RANGE=48300000-48D00000` (spu_dma.h): logs SPU transfers into the beam's region (the first 64,
  then a put/get byte rate every 5 s). PUTs during the beam with sizes like 8×24 = 192 bytes mean an SPU
  job writes particle vertices. Compare their EAs with the `[VPCODE] ea=`: the same EA means case 1, and
  different EAs with the same sizes mean case 2. No PUTs at all means a PPU producer or case 3.
- `GCM_IODBG`-style mapping log, if one exists: any cellGcmMapEaIoAddress or unmap calls after init (case 2).
  `grep -i "mapeaio\|MapMainMemory" stdout` on an existing run first.
- Checked: the later2 run's stdout has no MapMainMemory or MapEaIoAddress calls after `_cellGcmInitBody`
  (ioAddr 0x45300000, ioSize 0x3A00000). So the IO table is the plain linear init map, and an IO remap is
  ruled out as the cause. Case 2 would have to be a wrong array offset or base, not the mapping.

## Update (15:50): the SPU job writes the beam's vertices to the right place, but after we draw

The AI assistant (`perf\scratch\laser\dmaafter2`, dma-range-after 66d1e1e): 3,007 SPU PUTs into 0x4844…–0x48A6…, all
from one SPU program (pc 0x02EC0, lsa 0x06400), sizes multiples of 96 B (one quad = 4 × 24 B). **Every one of the
300 beam vertex EAs is covered**, and the covering PUT comes after the draw, within the same few flips. So the
data is right and the address is right: **we draw too early.**

### What sync the game relies on (checked in code where stated)

- Our FIFO walker drains commands as soon as `put` moves (`cellGcmSys.c`, control register at
  GCM_CONTROL_GUEST_ADDR). That is the same as the real RSX, which also follows `put`.
- NV406E ACQUIRE (cellGcmSetWaitLabel) is implemented behind `GCM_SEMA_ACQUIRE=1`, and turning it on changed
  nothing. So the game does not guard these draws with a wait label (or does, on a label that is already at
  the value).
- Self-JUMP parks are honoured and only abandoned after 50 ms with `put` elsewhere. A park is not involved
  here: the draws are reached and executed, they are just early.
- **Most likely (inferred): there is no explicit sync. The game relies on the real RSX running behind the
  PPU.** It records frame N's particle draws, and the SPU job writes their vertices during the rest of frame N.
  On a PS3 the GPU is still busy with frame N-1 (the PPU waits only on the previous frame's flip or label), so
  by the time it reaches those draws the data is there. Our walker is never busy, so it runs right behind
  `put` and reads the buffers before the job finishes.

### Fix candidates

- **A. Hold the drain one flip behind (with a timeout).** Stop at a flip command until `put` has reached the
  next frame's flip, or until N ms pass (as GCM_PARK_GRACE_MS does), whichever comes first. That emulates
  the real RSX's lag, needs no game knowledge, and covers every SPU-built buffer at once, all effects and
  probably the glow. Cost: one frame of extra latency (~33 ms at 30 fps). Risk: a title that waits for its
  current frame's fence before writing the next frame would stall until the timeout; the timeout bounds it.
- **B. Re-fetch late:** keep the walker as it is, but record vertex sources and fetch them at render time,
  after the flip. That is less general (VPULL already copies "once per frame", which may be too early as
  well) and would not cover index buffers or constants written late.
- Recommendation: A, opt-in first (e.g. `GCM_FIFO_LAG=1`, timeout 50 ms), tested on the laser, then fps
  and menus.

### Read-only check first: `patches\beam-clock\`

It adds `ms=`/`flip=` to the `[VPCODE]` and `[dma-range]` lines, so the gap between draw and PUT is measured
in flips. Same flip means a lag of 1 is enough; more needs a lag of 2. Logging only.

## Update (16:10): we draw two flips late, because flips complete on the 60 Hz beat regardless of the walker

beam-clock (3573ab4, `perf\scratch\laser\dmaclock`): for all 50 beam draws, the last SPU write covering their
vertices came **2 flips before** the draw, and the bytes were already zero at draw time. That reverses the
15:50 reading: the walker is two frames behind the game, and the game has already recycled the buffers.

Cause (checked in code): `cellGcm_request_tick` completes a pending flip on every 60 Hz tick
(`GCM_PENDING_SET(3)`), whatever the walker has reached. The guest sees the flip as DONE and keeps going, so it
runs up to two frames ahead of the walker and reuses its per-frame particle buffers before we draw them.
`GCM_FLIP_NEEDS_FIFO=1` exists, but it is strict (it waits for get == put).

Fix drafted: `patches\flip-at-word\` (`GCM_FLIP_AT_WORD=1`, opt-in): a flip counts as done only once the walker
has decoded that flip's marker, with a 100 ms safety release. Quick confirmation without any patch first:
`GCM_FLIP_NEEDS_FIFO=1`. This should bring back all SPU-built effects, and possibly the glow (its records may
be emptied by the same recycling). The 15:50 "hold the walker back" idea is withdrawn: it would make it worse.

## Update (16:40): neither flip gate helped; where the data goes between the PUT and the draw

The AI assistant: `GCM_FLIP_NEEDS_FIFO=1` and `GCM_FLIP_AT_WORD=1` (7cbe7e7) both leave all 300 beam rows zero. The
AT_WORD gate never fired (0 release lines), so **the walker is not behind on flips**. The AI assistant's alternative, a
lag between decode and a later replay/fetch, does not apply to these numbers: `[VPCODE]` reads guest memory
inside `d3d12_draw_arrays`, which `rsx_process_method` calls **while the walker decodes the FIFO**. Vertex
fetch for rendering (VPULL, VFETCH_ASYNC) comes later still, but the bytes are already zero at decode.

Re-reading dmaclock with that: for every draw, the last covering SPU PUT came exactly 2 walker-flips before,
and the walker is not behind. So the game itself draws, in frame F, quads its SPU job wrote in frame F-2,
which looks like a by-design 3-frame vertex ring. **Something zeroes them in frame F-1 or F before the draw.**
The candidates:
1. **An SPU job zeroing them with SDCRZ** (MFC 0x89). Our SDCRZ handler returns before the `SPU_DMA_RANGE`
   logger, so these zero-fills were invisible in every log so far.
2. **PPU code** (a memset or a frame allocator reset) that thinks the buffer is free one frame early. Since the
   gate never held, it is not reacting to an early flip, but it could react to an early label or reference.
3. **Our emulation zeroing wrongly**, for example an SDCRZ or cache-line-zero at a mis-resolved EA.

Next read-only check: `patches\sdcrz-log\` (`SPU_SDCRZ_RANGE`), logging only, plus the dmaclock run.

## Update (17:55): no SPU zero-fill; is the walker behind the guest after all?

The AI assistant (3ab2a5b): zero `[sdcrz]` hits on the beam arrays. The last covering SPU PUT is still 2 flips before
the draw (47/50 with defaults, 50/50 with `VPULL=0 VFETCH_ASYNC=0`), and the bytes are zero.

Two readings remain, and one measurement decides between them:
- **The walker is two frames behind the guest.** The "flip" counter in both logs is the walker's own count of
  decoded flip words. If the guest put the beam draw in the FIFO at about the time of the SPU PUT, and the
  walker decoded it two frames later, the guest had already recycled the buffer. GCM_FLIP_AT_WORD could not
  catch this if J-Stars writes the lv1 0xFEAD flip word inline (its PPU request counter only counts the HLE
  marker path, so it would stay 0, and the gate never held).
- **The game draws two-frame-old quads by design**, and something on the PPU side clears them first.

**Measurement (logging only): `patches\fifo-queued\`.** It adds `queued_flips`/`queued_bytes` (flip words and
bytes queued between get and put) to each `[VPCODE]` line. A PPU write watch is the follow-up only if
`queued_flips` is 0-1. It is harder than it sounds: the lifted dcbz (inline memset of 128 bytes) and vector
stores (`vm_raw_st16`) bypass `PPU_WWATCH`, and those are exactly how buffers get cleared.

## Update (18:20): the game draws two-frame-old quads by design (a ring of 3); something clears them in between

From the AI assistant's fifoqueued/dmaclock logs (my analysis of `perf\scratch\laser\fifoqueued\stderr.txt`):
- `queued_flips=0` at all 50 beam draws, so the walker keeps up with the guest.
- The SPU particle buffers rotate through **three rings** per flip: flip 6614 writes 0x4845xxxx, 6615
  0x4875xxxx, 6616 0x48A5xxxx, 6617 0x4845xxxx again, and so on. Draws at flip F use the ring written at F-2,
  and the SPU next rewrites it at F+1. That is a deliberate triple buffer: the GPU reads what the SPU finished
  two frames ago.
- For all 50 draws, no SPU write touches the quad between F-2 and the draw (no PUTs, no SDCRZ). Yet the bytes
  are zero at the draw.

So **something other than SPU DMA clears the ring slot between F-2 and F**. That is either the PPU (a clear
through dcbz, vector stores or memset, none of which `PPU_WWATCH` sees) or our emulation (an allocator, a
coherence path, or a host memset).

**Next (logging only): `patches\beam-guard\` (`VP_CODE_GUARD=1`).** It page-guards the first logged beam
vertex's 4 KB page with the existing VEH guard, which traps every host write whoever makes it, and adds
`flip= ms= tid= old= lr=` to `[GUARD] WRITE` lines. The clearer then shows up as the write that turns the slot
to zero between its F-2 fill and its F draw.

Possible outcomes:
- If it is a game clear on the PPU, the game expects the RSX to have consumed the slot before the clear, but
  the clear comes **before** our walker decodes the draw. That cannot be when the walker keeps up, unless the
  clear is meant for a different ring index: an off-by-one in a ring counter, for example a frame counter
  advanced twice, which our flip or vblank handling could cause.
- If it is our own code, the fix is in that path.

## Update (18:35): the SPU particle job itself writes zero quads (beam-guard)

The AI assistant (b9e2b2d, `perf\scratch\laser\guard`): all 1,781 SPU writes into the guarded beam page wrote zeros.
The PPU writes there are only heap reuse (a page clear by 0x00594E5C at flip 6602). So the vertices are
already zero when they leave the SPU. It is not timing on the RSX side.

The writer: **image 17 = `spujob_857F80`** (EBOOT 0x857F80; `jstars\port\spu\spu_workloads.c:106`). Its
`func_00002EC0` is a generic PUT routine (LSA/EA/size from registers, cmd 0x20), and the 0x6400 buffer in
local store is what it sends. So the job's output buffer is empty.

Next (logging only): `patches\dmatrace-after\` adds a start time and a line count to `SPU_DMATRACE`, plus
non-zero counts for PUTs and large GETs. One run with `SPU_DMATRACE=17 SPU_DMATRACE_AFTER=~220
SPU_DMATRACE_N=3000` shows whether the job's inputs are empty (an upstream PPU problem) or its computation
produces zeros from good inputs (a lifted-code problem).

## Update (18:55): image 17 gets good inputs and still outputs zeros

dmatrace (9c4cb08): the job's 304-byte input GETs (EA 0x4924xxxx, from pcs 0x9E0/0xFB8/0x760/0xF18) are all
non-zero. 398 of 399 PUTs from 0x2EC0 are all zero. So **the lifted job's computation (or its control flow)
produces nothing from good inputs.** From the code: no unlifted instructions; the reciprocal estimate ops
match the hardware tables; channel reads are only tag status and the inbound mailbox. 0x2860→0x2EC0 is the
flush that PUTs the output buffer once it is full or the job ends.

Next (logging only): `patches\spu-lsdump\` writes image 17's whole local store and registers at its first
all-zero PUTs. I'll analyse the dumps offline against the lifted source, to see whether the vertices land
elsewhere in LS (a pointer problem) or the per-particle code skips them (which field).

### Update (LS dumps read)

The dumps answer the pointer question: image 17 does run its quad writer into the 0x6400 buffer. At the PUT
(regs_1), the output pointer r80 = 0x64C0 = 0x6400 + 0xC0, which is exactly the 2 quads (0x60 bytes = 4 × 24-byte
vertices each) the PUT sends, and r120 = 0x3FBC0 is the job context `spu_run_spurs_job` builds, so the job is
started through `spurs_job.c`. Nothing landed elsewhere: LS above the code holds only the job's tables
(0x6000: 0x6400/0xB000/0xB400/0xB800/0xBC00 buffer table, "%SPURS JOB INFO%"), the 0x6800 descriptors
(5000.0f, record EA) and the input records at 0xAA30.. (0x130 B; colour 128,128,128,100, scales 0.51/0.51/1.01,
offsets -3/-0.95). So the vertex values themselves are computed as zero, colour included.
The quad stores are the `stqd/stqx ... $r80` run at LS 0x149C-0x16CC (and a second one from 0x18B4).

Next: `patches\interp-img\` (SPU_INTERP_IMG=17) runs the job on the interpreter instead of the lift. Real
vertices with the interpreter = lifting bug (then bisect); still zero = input-side problem.

### Update (who fills the records)

The AI assistant's record-area PUT trace (`perf\scratch\laser\recwriters`, SPU_DMA_RANGE on 0x49200000-0x49300000):
the only SPU PUTs land at 0x492E7xxx-0x492FCxxx (pcs 0x26A8/0x3150/0x3740/0x37C0, 16 or 0 bytes). No SPU
writes the particle records at 0x49245840.. at all, and image 17 only GETs them. So the records are written by
the PPU. Their first 0x40 bytes (zero in every record) are 16 floats, most likely the particle/emitter world
matrix; a zero matrix gives zero positions and a zero alpha/fade gives zero colour, which matches the all-zero
quads. Next: `PPU_GUARD_EA=49245840` (already in the tree) names the PPU functions that write the record page,
and which of them should write +0x00..+0x3F.

### Update (record lifecycle)

func_0063E840 is the particle spawn: it memsets *(obj+0xB0) (0x160 B), *(obj+0xB4) (0x130 B = the record image 17
GETs; its +0x10C/+0x114 = 1.0 match the LS dumps) and *(obj+0xB8) (0x80 B), then fills only +0x108..+0x114 of
the record. func_0063D5A0 (per-frame curve updater: 0x63CAD4/0x63C994/0x63CCF0/0x63D050) writes +0x40..+0x8F
every frame (recguard2) but never +0x00..+0x3F. Before the clear, +0x00 held ±pi/2, so some other writer fills
the first 64 bytes during a particle's life; recguard2 armed too late to see it. Next: guard from the first
image-17 GET, filtered to record offsets 0x00-0x3F, logging new values and callstacks.

### Update (interp result, the +0x121 flag)

Interp run (SPU_INTERP_IMG=17): all 73 image-17 PUTs still zero, so this is not a lift bug in the job. Image 17 has
never PUT a non-zero byte in any traced run, menus included. In the job, LS 0x1074-0x107C tests record+0x121 & 0x02:
set → 0x1CE0, the real path (size +0x70, colour +0x80 clamped to 255, +0x100/+0x110, pi/2 rotation); clear → 0x1080,
which sets size = colour = 0 and the default basis (0,1,0,1)/(1,0,0,1), exactly what the dumps show at r127+0/+0x30.
Every record has +0x120..+0x12F = 0. The PPU tests the same bit (func_00632DC0, func_0062A2FC); there is no direct
byte store to +0x121 in the lifted PPU code. Test: `patches\rec-flag` (SPU_FORCE_REC_FLAG=17) forces the bit.

### Update (who should set +0x121)

rec-flag works: with bit 0x02 forced, image-17 PUTs are non-zero and Goku shows a new blue-white glow, but no proper
beam (positions at +0x00..+0x3F still zero). flagguard: no non-zero store ever reaches +0x120..+0x123. Static
search of the whole lifted PPU: no stb/sth/stw/std/vector store sets +0x120/+0x121 anywhere; the only writers are
the spawn memset (0x63E934), the pool-init memset (func_00625014 clears the 0x130 pool at sys+0x80) and an
unrelated reset (func_00570A04). func_0062A2FC builds the per-frame job descriptors image 17 reads (the 0x6800
entries) and, like func_00632DC0, skips records with the bit clear. Hypothesis: the records (flag + position block)
are filled by something outside the lifted PPU, most likely an SPU simulation job that never runs or never PUTs in
our build. Next: full GET+PUT map of the record arena with image ids at beam time, plus the dispatched-job list.

### Update (setter search exhausted; workaround is likely the fix)

All non-zero EBOOT code words lie inside lifted functions (58 stray words in the first page excepted), and no store in
the EBOOT targets record +0x120/+0x121 except the memsets; the B4 users are spawn 0x63E840, the per-frame update
0x63E19C (writes B4 +0x40/+0x60/+0x70/+0x80/+0xE0/+0x100 via 0x63D5A0/0x63D100) and the descriptor build 0x62A2FC.
Image 17's flag path (0x1CE0..) reads record +0x60/+0x70/+0x80/+0xE0/+0xF0/+0x100/+0x110 and the emitter params,
never +0x00..+0x3F; the bit-clear path (0x1080) always emits zero quads, so on hardware visible records must carry
the bit. Forcing it (SPU_FORCE_REC_FLAG=17) is a reasonable fix candidate; open: why the bit is set on hardware
(needs an RPCS3 memory read of a beam record at +0x120, the maintainer's machine).

### Update (flag forced: quads degenerate)

flagls (SPU_FORCE_REC_FLAG=17): beam draws now carry non-zero vertices, but v0 == v1 in every draw, at
direction-sized positions near the origin, so the quads are zero-width. A stretched quad is pos ± width·side,
side = f(direction, camera); record +0xE0..+0xFF (likely the direction) is zero in every dump. Its only PPU writer
is func_0063E19C's 0x63D5A0 call at lr 0x63E5B4 (B4+0xE0), gated on *(B8+0x74) & 0x08 (loc_0063E578). Next: LS dump
at a forced-flag PUT to trace the 0x1CE0 path, and a guard on B8+0x74.

### Update (flagls2 LS dumps: why the forced quads are zero-size)

Forced-flag PUTs: each quad's 4 vertices are identical; colour = record +0x80 × emitter tint B400+0x70
(128×1, 128×0.925, 128×0.8 = 80 76 66) with alpha 0. In the job's flag path, alpha and size scale with lane products
of record +0x100 (r9 = (+0x100 quad) × rot8 × rot12) and +0x110 (r8 = (+0x110) × rot4). Records hold
+0x100 = (0,0,0,1) and +0x110 = (0,1,0,0), so both products are 0: size 0, alpha 0. +0x10C/+0x114 = 1.0 come from
spawn; +0x100/+0x104 should be the current value of a per-frame curve: func_0063E19C calls func_0063D100
(lr 0x63E49C) with r4 = B4+0x100, r5 = B0+0xC8, r6 = curve def *(*(obj+0x18)+0x44), r7 = state B8+0x58; D100 integrates
into B0+0xC8/+0xCC and copies to B4+0x100/+0x104, and returns early without writing when the state byte's bit 0 is set.
The +0x110 value (scale) has no PPU writer after spawn either. Correction: in func_0063E19C the B4+0xE0 write
(lr 0x63E5B4) runs when *(B8+0x74) & 0x08 is CLEAR (the AI assistant), not set.
- d100log: D100 runs (state byte bit 0 set then cleared), curve defs at 0x409Bxxxx; 35/40 particles (curve 0x409BAF10)
  get value 0, 5 (curve 0x409BC7F0) get 1.0. Curves are 0x50-byte keys indexed by B8+0x59; need full key dumps to
  judge. +0x110 (size) still 0 everywhere: guard g110 pending.
- g110: nothing but the memsets writes record +0x110. Correction to the flagls2 note: spawn sets +0x108/+0x10C and
  +0x110/+0x114 to (0, 1.0) pairs, i.e. (sin, cos) of a zero angle, so +0x100..+0x117 are rotations, not size/alpha;
  the lane-product reading was wrong. Next: `patches\itrace` (per-instruction interpreter trace of the flag path).
- curvedump: curve defs (0x409AF540 etc.) hold structured data (1.0/0.5/10.0/20.0 keys, byte tables), so the effect data loads; zero values come from evaluation or a missing input, not empty tables.

### Update (itrace: what zeroes size and alpha)

Interpreter trace of the flag path (perf\scratch\laser\itrace): colour is right (128, 118.4, 102.4); size =
record +0x70 × (rec +0x110 × +0x114) × emitter params, alpha = colour alpha × (rec +0x108 × +0x10C) × (job params
0xBC60 × 0xBC64). All three pairs read (0, 1.0), so r6/r9/r8 = 0 (0x1D38/0x1D50/0x1D64), half-sizes r90/r91 = 0
(0x1E10/0x1E18), alpha r102 = 0 (0x1DC0). They behave like (progress, max) pairs that never advance. d100log agrees:
D100's state time/rate (B8+0x5C/+0x60) stay 0. D100's rate = clamp(1 / (key duration × T)), T =
*(*(*(rtoc-0x5034)) + 4) with rtoc 0x8D3888 (TOC slot 0x8CE854); spawn reads the same global. Suspect: a global
time-scale / frame-time value that is 0 in our run. Next: log that global and the dt argument (f1) of 0x63E19C.
- tscale2: T = *(Q+4) = 30.0, *(Q+8) = 1/30, dt f1 = 1.0: timing global is normal. D100 init should store rate 1.0 at B8+0x60; next run tracks one particle across frames (life, state, respawn).
- track: particles live ~20 frames (life 23.8 → 0, -1.5/frame); D100 advances its own state (time +0.05/frame, rate =
  *(Q+8)) but by design writes the record (B4+0x100/+0x104) only on init/key-change frames (bit 0x01); steady state
  returns at loc_0063D1A4. Record +0x100/+0x104 stay 0 even after a key change. Open: whether the zero comes from the
  curve data (key pair (0, 0.5) for 0x409AF540) or from the job's lane use. Proposed: per-frame log of one particle's
  B4+0x100..+0x11F + job param block next to its forced-flag quads; decisive alternative: RPCS3 read of a beam record.
- track2: the record is live data. Over one particle's life +0x40/+0x60/+0x70/+0x80/+0xE0/+0x100 all change (e.g. +0x100
  = 0.3, 1.0; +0xE0 = pi/2; +0x80 alpha 64-255). Only +0x110 never changes: (0, 1.0, 0, 0) on all 40 frames, set by
  spawn (r7 = 0, fpr26 = 1.0) and by nothing else. In the job (itrace) the quad half-sizes are r90/r91 × r111, r111 =
  r16 × r2 = (r8 × emitter 0.15) × emitter 0.8, r8 = (+0x110 quad) × (rotated by 4 bytes) = 0. Spawn's own code writes
  0 there, so on hardware it would be 0 too unless something else updates it: the remaining contradiction. Needs an
  RPCS3 read of one beam record (+0x100..+0x11F) to settle (the maintainer's machine).

## Beam: summary and resume point (parked 2026-10-24, needs RPCS3 ground truth)

Chain, each step with its evidence (runs under perf\scratch\laser\<name>):
1. Image 17 (spujob_857F80, particle quad builder) PUTs all-zero quads; same on the interpreter (interp), so not a
   lift bug. It never PUT a non-zero byte in any run, menus included.
2. Per record it tests +0x121 & 0x02 (LS 0x1074). Clear → zero-size path 0x1080; set → real path 0x1CE0. The bit is
   never set (flagguard: only memsets write +0x120..+0x123). No store in the whole EBOOT sets it (all code is lifted;
   static search). Readers: func_00632DC0, func_0062A2FC (descriptor build).
3. Forcing it (SPU_FORCE_REC_FLAG=17, opt-in, the AI assistant 2cd9b46+) gives non-zero quads and a glow on Goku, but each
   quad's 4 corners are equal (flagls, flagls2).
4. itrace (interpreter trace of 0x1CE0-0x2860): half-sizes r90/r91 × r111, r111 = (r8 × 0.15) × 0.8 (emitter B410/B4A0),
   r8 = (rec+0x110 quad) × (same rotated 4 bytes). rec+0x110 = (0, 1.0, 0, 0) → r8 = 0 → zero size. Alpha uses
   products of rec+0x60 and rec+0x100 lanes.
5. track/track2: particles live ~20 frames; the PPU updater (func_0063E19C → D5A0/D100) keeps +0x40/+0x60/+0x70/
   +0x80/+0xE0/+0x100 live. +0x110 = (0, 1.0) on every frame; only spawn (func_0063E840) writes it (r7 = 0,
   fpr26 = 1.0); g110 guard: no other writer.
6. Ruled out: timing global (T = *(*(*(TOC 0x8E3858 - 0x5034)) + 4) = 30, *(Q+8) = 1/30, dt = 1.0: tscale2), curve
   data (curvedump: real keyframes), other SPU jobs (arena: only image 17 touches the record arena), missing lift.

Open contradiction: spawn's own code stores 0 at +0x110, so hardware would also get 0 unless a writer we don't run
exists, or the job reading in step 4 is wrong somewhere.

RPCS3 read needed (the maintainer's machine): during a Kamehameha, take a record EA from an image-17 GET (e.g. The AI assistant's
SPU_DMATRACE=17 log, or the descriptor's 4th input at job desc +0x48/+0x4C) and read that record's +0x100..+0x12F in
RPCS3's memory viewer. Compare +0x110..+0x117 (ours: 0, 1.0) and +0x121 (ours: 0x00). If RPCS3 shows +0x110 ≠ 0 and
bit 0x02 set, a PPU writer is being skipped in our build; set a write breakpoint there in RPCS3 to name it. If RPCS3
also shows (0, 1.0), re-check step 4's lane reading with an RPCS3 SPU trace of 0x1CE0-0x1E20.

## RPCS3 ground truth (2026-10-25, tools\rpcs3_beam_scan.py, reference\rpcs3_beam_scan.txt)

The maintainer ran a read-only scan of RPCS3 paused on the Kamehameha. Guest base found (host 0x300000000, guest heap layout
matches ours shifted by 0x10000000). ~90 beam-type records in a 0x130 pool around guest 0x39B1xxxx. Field by field,
RPCS3 vs our port:
- +0x00..+0x3F: a 3x4 rotation matrix (e.g. -0.093 -0.996 0 / 0 0 1 / -0.996 0.093 0 / 0 0 0 1) | ours: all 0
- +0x90/+0xA0: UVs (0.25, 0.5, 0.125 ...) | ours: 0
- +0xB0: world position (23.5, -0.12, 16.6, 1) | ours: 0
- +0xC0..+0xF0: (0,0,0.1|10|15,0), (0,1,0,0), (0,0,0,1), scale (1|1.5|2, ..., 1, 0) | ours: 0
- +0x108 / +0x10C / +0x110 / +0x114: 1.0 / 1.0 / 1.0 / 1.0 | ours: 0 / 1.0 / 0 / 1.0
- +0x118..+0x11F: 1C080083 336F0380 | ours: 0
- +0x120 flags: 00023604, 04033604, 00033604 ... (+0x121 = 0x02/0x03, bit 0x02 set) | ours: 00000000
- +0x124: link (same) ; +0x128: 8F1581C7 00200000 | ours: 0
- +0x40..+0x80 (the PPU updater's fields) are populated in both.
So in RPCS3 most of the record is filled by a writer that never runs in our build. The lifted PPU has no stores
of this shape (no +0x118/+0x128 writer, no +0x120 setter, the spawn really stores 0 at +0x108/+0x110), and the
odd words at +0x118/+0x128 look like a whole-record copy-back. Strongest suspect: an SPU job that writes the
0x130 record back (e.g. image 1 spujob_7E7E00, whose PUTs at pc 0x3150 are size 0 and "REJECTED malformed
transfer" in every run of ours), or a job output write-back that spurs_job.c doesn't perform.
- Image 1 (spujob_7E7E00): LS 0x3050-0x3174 builds 64-byte rotation matrices from quaternions (what RPCS3 records hold
  at +0x00..+0x3F); after the loop it issues 3 PUTs (LS 0x31F4/0x3284/0x3314, tags 1-3) with EAs from a param block
  (r123 +44/+48/+52) and sizes = bytes produced. Our runs: only size-0/16-byte PUTs ("REJECTED malformed", pc 0x3150)
  → image 1 processes ~0 items. Open: item count r120 or the cull test at LS 0x890-0x8A8 (fcgt vs r81). spurs_job.c
  doesn't write job output buffers back (the AI assistant confirmed), but image 1 PUTs its own output, so that isn't needed here.
- Correction: img1dump shows image 1 (spujob_7E7E00) is the skeletal-animation sampler, not a particle job: its input
  is 2 animation items (18 bones, frame 290.0 / 260.0) and its output buffer holds per-bone (quaternion, translation,
  scale) at a 0x30 stride; the LS 0x3050 loop turns bone quaternions into matrices. Its size-0 PUTs are empty
  animation outputs, unrelated to the beam. Next ground truth: tools\rpcs3_obj_scan.py finds the RPCS3 objects that
  own the beam records (pointer at obj+0xB4) and dumps obj, B0 and B8, for comparison with ours.

## RPCS3 object scan (tools\rpcs3_obj_scan.py, reference\rpcs3_obj_scan.txt) and the real suspect: image 18

RPCS3's beam particle objects look like ours (obj+0x0C = 9, B0/B4/B8 pointers at +0xB0/+0xB4/+0xB8). Their B4 render
record is built from their B0 sim block: B4+0x40 = B0+0x10, B4+0x70 ≈ B0+0x70, B4+0x80 = B0+0x90 (colour),
B4+0xB0 = B0+0xE0 (world pos), B4+0xC0 = B0+0xF0, B4+0xD0 = B0+0x110, B4+0xF0 = B0+0x100, B4+0x00..+0x3F = a matrix
from the B0+0x140 rotation, plus UVs, +0x108..+0x117 and the +0x120 flags. No lifted PPU function writes B4 like that.
The particle module creates three SPU job types (func_00627AA0 / AF0 / B40, TOC slots 0x8DE420/428/430): image 17
(857F80), image 18 (85E080) and image 16 (852200). Image 18's code has a 0x80-byte PUT (B8 size) at LS 0x1F74 and a
0x130-byte PUT (record size) at LS 0x1FCC, stepping a pointer by 0x160 (B0 size): it is the particle simulation job
that writes the B4 records and B8 blocks back. In our runs no image ever PUT into the record arena, so image 18
either doesn't run or doesn't reach its PUTs. Next: SPU_DMATRACE=18 at beam time, and image-18 dispatch counts.
- img18 (the AI assistant, b8cf6fa): image 18 runs (as often as image 16). Its descriptor: in/out 0x80 from 0x492B8B00 → LS 0x6C00,
  size_out 0x130; cache list: 0x10-byte pointer list from 0x492B8C50 → LS 0x9400, 0x160 B0 block from 0x4920FEC0 → LS
  0x9800. All its traced DMA are GETs at pc 0x1330 from ea 0x3FF00 (0x160) and 0 (0x80), alternating; no PUTs. The
  pointer list in guest memory holds "0003FF00 00000000" instead of real B0/B8/B4 EAs (0x3FF00 = our runner's
  SPU_JOB_RETURN_LS, seen on the SPU stack). So image 18 never gets valid particle addresses and never writes the
  records. Next: guard 0x492B8C50 from the first image-18 job to name the writer of that pair.

## FIXED (2026-10-25, the AI assistant 8516777): skill effects draw

Root cause: spurs_job.c started SPURS jobs with sp = stack_top - 16. The job's prologue saves LR at sp+16, i.e. at
stack_top, which is where the first cache-list buffer begins, so image 18's 0x10-byte particle pointer list was
overwritten with the return address 0x3FF00 (and 0) before the job read it. Image 18 then fetched from 0x3FF00/0,
never reached a real particle, and never PUT the B4 records/B8 blocks; image 17 saw unflagged, half-empty records
and built zero quads. Fix: sp = stack_top - 32 (default on; SPURS_JOB_SP_GAP=0 restores the old layout). Beam
streaks, Ace's fire and sparks draw; image-17 PUTs are non-zero; speed unchanged. SPU_FORCE_REC_FLAG is no longer
needed (leave it off). Remaining open: the glow layer (render target 0x0E590000 never drawn) — re-check now, since
any SPURS job with a cache list was affected by the same stack overlap.
