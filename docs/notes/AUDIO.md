# Audio: where CRI ASR writes the cellAudio port (2026-10-27, AI assistant, read-only)

Static reading of `jstars\port\recompiled` and `jstars\EBOOT.elf`. Runtime values are not known yet.

## The sleep the AI assistant saw
- `func_004D518C` is `sys_timer_usleep(r3 * 1000)` (lv2 syscall 0x8D).
- Its 16/17 ms caller is `func_004D4E94` (lr 0x004D4F30). That function is a fixed-rate pacer: it
  sleeps until the next period, using `obj+0x9C` period, `obj+0xA0` last time and `obj+0xA4`
  drift.
- So "CRI Server Manager" only sleeps there between server ticks. The pacer isn't the problem.

## The cellAudio output driver (CRI "cellAudioPort" ASR output)
Function table at 0x7ABB88 (OPDs), followed by the string "E20101215:cellAudioPort...":

| slot | OPD | code | role |
|---|---|---|---|
| 0 | 0x8C7A38 | func_006A5674 | |
| 1 | 0x8C7A40 | func_006A567C | |
| 2 | 0x8C7A48 | func_006A5A50 | open/start (calls cellAudioPortOpen at 0x6A58A8-0x6A5994, PortStart at 0x6A59F0) |
| 3 | 0x8C7A50 | **func_006A5CC8** | **output: called with (obj, samples, count)** |

func_006A5CC8 is only reached indirectly, through this table. No lifted code calls it directly.

**func_006A5CC8** (output, obj = the driver object):
1. It appends samples into the object's buffer. `obj+0x2000` is the sample count (256 per block).
2. Once 256 samples (one block) are buffered, it checks `obj+0x2008` (blocks of lead):
   - **lead > 1**: it calls func_006A5BA4, which copies to the port ring.
   - **lead <= 1**: for each of the 2 ports in `obj+0x2010/0x2014` that is not -1, it calls
     **`cellAudioAddData` (stub func_00733EB0, NID 0xDAB029AA)**.
     **That NID isn't registered in our HLE, and there is no cellAudioAddData in
     libs/audio/cellAudio.c.** It has never shown up as an "unresolved NID" in perf\audio runs, so
     this path hasn't run (yet).
3. `lead` is set at open: `((48000 / (int)f) + 255) / 256 + 1`, where f is a float from the init
   parameters (e.g. 60.0 gives 5). If f were 0, our lifter's divide gives 0, so lead = 1 and the
   AddData path would be taken.

**func_006A5BA4** (port-ring copy):
1. It calls `cellAudioGetPortConfig(obj+0x2010, sp+0x70)` (lr 0x006A5BD8).
2. It requires **`status == 2` (RUN)**. Otherwise it returns 0 and nothing is written.
3. R = `*(u64*)readIndexAddr`, W = `obj+0x200C` (CRI's next write block), nBlock = config u64 @ +0x10.
   - fill = W - R if W > R, else nBlock - (R - W). **W == R counts as full (fill = nBlock).**
4. It writes only if fill < lead. For each port not -1 it does
   `memcpy(portAddr(config @ +0x1C) + W * 0x2000, buffer, 0x2000)` (func_006FB480), then W++
   (mod nBlock).

Our GetPortConfig layout matches what it reads: readIndexAddr @0, status @4, nChannel @8, nBlock
@0x10, portSize @0x18, portAddr @0x1C. The read index is a u64 at 0x58100000.

## So which condition skips the write? (needs one run to tell)
1. **func_006A5CC8 is never called.** The ASR server never reaches the driver's output slot, so it
   isn't a buffer condition at all.
   - Check: count `ps3_indirect_call` hits to 0x8C7A50/0x6A5CC8, or `HLETRACE` of GetPortConfig
     (NID 0x74A66AF0) with lr = 0x006A5BD8. The open path's calls have other lr values.
2. **It is called but returns early.** Dump these at a GetPortConfig with lr 0x006A5BD8:
   - `obj+0x2000` (count), `obj+0x2008` (lead), `obj+0x200C` (W), `obj+0x2010/0x2014` (ports);
   - the config's status, nBlock and portAddr;
   - `*(u64*)0x58100000`.

   Suspects:
   - status != 2 at that moment;
   - lead <= 1, which needs cellAudioAddData;
   - W == R stuck as "full". This can't persist while R advances, but it would if R read back 0 every
     time because the u64 read lands on the wrong half.
3. If lead <= 1: implement **cellAudioAddData (NID 0xDAB029AA)**. It is
   `(u32 portNum, float* src, u32 samples, float volume)` and mixes into the port's current block.
   That is a code change, so it needs the maintainer's yes.

## Pass 2 (2026-10-27): the mix is silent, so what feeds it?
The AI assistant measured that the driver copies blocks from src 0x451730F0, and every one is zero (§ FINDINGS
"Audio: silent").

**Correction on the job numbers** (`jstars\port\spu\spu_workloads.c`, generated table):
- image 3 = **spujob_80B700** (EBOOT 0x0080B700, 5072 B). That is the job PUTting 0x800 bytes to
  0x40002820 ~200x/s, and it is **not AsrVoice**. FINDINGS §(early) "AsrVoice = image 3" is from an
  older registration.
- image 10 = **crijob_atomdec** (EBOOT 0x00819780): the CRI AtomDecoder (HCA/ADX decode).
- image 11 = **crijob_asrvoice** (EBOOT 0x00829A80): the CRI ASR voice mixer.

In perf\audio\a5 the (capped) log shows image 11 dispatched twice:
`job 0x45187580: bin 0x00829A80+29920 ... io=0x07800(0, 0 dma, 0 used) out=0x07800(131072)`.
Image 10 doesn't appear at all. The log keeps only the first lines, so counts are unknown.

**What one run should count** (per image, whole run):
1. Dispatches of image 10 (AtomDecoder) and image 11 (AsrVoice).
   - If image 10 never runs, no voice is ever decoded. The cause is upstream: no voice or BGM is
     started, or the decoder job is never pushed.
   - If image 11 runs only at boot, the ASR server isn't mixing through the SPU.
2. For image 11 jobs: the full descriptor and whether its output (the out buffer DMA'd back,
   0x7800 bytes) has any nonzero word. "io=...(0, 0 dma, 0 used)" means no input DMA list. If that
   holds for every AsrVoice job, it is mixing nothing (*inferred*, needs the descriptor dump).
3. Whether 0x451730F0 is written by a PPU mix loop or by a DMA PUT from image 11.
   - A write watch on that range, or the SPU PUT log filtered to 0x45170000-0x45180000, will tell.
   - That decides whether to follow the SPU path (AsrVoice output) or the PPU path (the ASR server
     chain func_006C92D0 ... func_006F1998).

## Pass 3 (2026-10-27): decode jobs stop after 5
a7 (the AI assistant): img10 AtomDecoder = 5 in 130 s; img11 AsrVoice ~24.6k (~200/s, every one "0 dma"). No SPU
PUT lands in 0x45170000-0x45180000, so the mix buffer 0x451730F0 is written PPU-side.

Job-queue entry points the game imports: all matched to names by NID hash. They are
_cellSpursCreateJobQueueWithJobDescriptorPool, cellSpursJobQueueAttributeInitialize,
cellSpursJobQueuePort2Create/Destroy, _cellSpursJobQueuePort2PushJobBody / CopyPushJobBody,
cellSpursJobQueuePort2Sync (0xFACB3CED), cellSpursShutdownJobQueue and cellSpursJoinJobQueue. All of
them are handled in jstars\port\stubs.cpp. There is no semaphore or TrySync import, so the PPU can
learn about a job's completion only through:
- memory the job writes (a status word, the output ring);
- Port2Sync. Ours returns at once, which is fine when pushes are synchronous.

Hypotheses (*inferred*, need the push dump below):
1. **The decoder's input never arrives.** CRI FS streaming doesn't deliver, so after the first 5
   (header/prime) decodes the voice has nothing to decode and starves.
2. **The 5 decode jobs fail or write their output somewhere the PPU doesn't see.** For example, the
   output or status is PUT through a DMA list our dispatcher doesn't stage, so the PPU never sees
   "decoded N samples" and never pushes again. The AsrVoice "0 dma" on every job fits a header field
   being read wrong.
3. **The voice is stopped by the PPU** after an error status in the job's output.

### Per-push dump for the AI assistant (stubs.cpp jq_push, at least every img10 push and the first ~20 img11)
- The pusher: thread id, lr (`ctx->lr`), r3 (port), r4 (job_ea), r5 (desc_size), r6 (tag),
  r7 (isSync). The image id the fingerprint resolved to.
- The raw descriptor: the first 0x30 bytes at job_ea, plus the DMA list that follows, as words.
  Fields: eaBinary u64 @0, sizeBinary u16 @8 (x16), sizeDmaList u16 @0xA, eaHighInput u32 @0xC,
  useInOutBuffer u32 @0x10, sizeInOrInOut u32 @0x14, sizeOut u32 @0x18, sizeStack u16 @0x1C,
  sizeScratch u16 @0x1E, eaHighCache u32 @0x20, sizeCacheDmaList u32 @0x24. The DMA list entries are
  (size u32, eaLow u32) pairs from @0x30.
- For every input DMA entry: the first 32 bytes at that EA.
  - HCA data starts "HCA\0" (or masked 0xC8C3C100); ADX starts 0x8000.
  - All zero means the stream never arrived (hypothesis 1).
- After the job returns: every EA the job PUT (from the SPU DMA log during the dispatch), its size,
  and whether it is nonzero. Also any word in the user data area that changed (a status/count the
  PPU polls).
- Then compare the img10 job descriptors against our dispatcher's "io=... dma ... used" line. If
  sizeDmaList is nonzero in the raw bytes but the line says 0, the dispatcher reads the header wrong
  (hypothesis 2), and that would also explain AsrVoice's "0 dma".

## Pass 4 (2026-10-27): the decoder runs 5 times on real HCA, then the voice stalls
The AI assistant (JQ_DBG): the AtomDecoder is pushed 5x from tid 17 via Port2CopyPushJobBody.
- The descriptor has no DMA list; this is genuine, not a parse bug.
- sizeOut 0x18000. User data: p1 0x4512D3E0, p2 = the HCA voice context 0x4B7842D0.
- The input is real HCA: "HCA\0" v2.0, 2 ch, 48000 Hz; frames start 0xFFFF, 0x2AA bytes each.
- readPtr advances: header 0x30A, then +0x2AA per frame.
- The output at outA/outB (0x600 apart) is nonzero only once (128 of 384 words). p1/p2 are
  unchanged by the job.

Reading (*inferred*):
- The decoder fetches its own input and output with in-job DMA (no descriptor DMA list), so the
  dispatcher's "0 dma" is expected for CRI jobs. AsrVoice's "0 dma" is not a bug either.
- 5 pushes, then none, is the shape of a **consumer stall**. The decoder refills only when its PCM
  buffers (outA/outB...) are drained. If the stage that reads them (the AsrVoice SPU job, or the PPU
  mixer) never consumes them, the decoder sits idle, and the mix stays silent too.
- Mostly-zero PCM from the first frames is not proof of a bad decode either. Tracks often start with
  silence, and the first HCA frames after the header can decode to near-zero (*inferred*).

### Next dump (one run)
1. **HCA header chunks** at the stream start 0x44E6D200, bytes 0x00-0x60 as words. Check
   `comp`/`dec ` (frame size 0x2AA, ch/band counts), `ciph` (0 = none, 1 = fixed, 56 = keyed; keyed
   needs a key from the game) and `ath`. If ciph = 56, look for the key handed to the decoder in
   p1/p2.
2. **SPU DMA during each of the 5 decoder dispatches**: every GET/PUT (EA, size). Expect GETs from
   readPtr (frames), and PUTs to outA... plus a small PUT of a status/count. That count is where the
   PPU learns "N samples ready". If that status PUT is missing, or lands on an EA nobody reads, that
   is the stall.
3. **SPU DMA during ~20 AsrVoice (img11) dispatches**: does it GET from outA/outB (0x4B7807B0...)? If
   not, it isn't consuming this voice. Also its user data, and where its PUTs go.
4. **Poll the voice context p2 (0x4B7842D0, the first 0x60 bytes) once a second** for the whole run.
   Do any read/write cursors of the PCM ring move after the 5th decode? If none move, nothing
   consumes the decoded PCM.

## Pass 5 (2026-10-27): where the CRI jobs can exit before any DMA (lifted atomdec, read-only)
The AI assistant: SPU_DMATRACE shows no GET/PUT at all from img10 (AtomDecoder) or img11 (AsrVoice). The
interpreter gives the same result. The HCA stream is plain (ciph 0).

Entry path of `jstars\port\spu\crijob_atomdec\spu_recomp.c` (JOBCRT Ver14, same as the game jobs):
- 0x0: set r2, then jump to 0xE0. 0xE0 calls 0xC860 (init/ctors), 0x60, then **0x5D0(ctx=r3, job=r4)**,
  then 0xBA60 and 0xD1D0, then the exit stub at 0xC768.
- **0x5D0** calls **0x660(ctx)** first. **If that returns nonzero, the job stops with code 0x3FFF**
  (the JOBCRT error exit), before main ever runs.
  - 0x660 reads **ctx+0x14 & 0x0FFFFFFF**. If it is nonzero it walks a table (constants 0x80410A11,
    LS 0x170, 0xF010-0xF130), which looks like the JOBCRT relocation/cache setup.
  - **Only the CRI jobs have this block.** crijob_asrvoice has it too (`ilhu 0xFFF`); none of the game
    jobs (80B700, 813580, ...) do. That explains why the game jobs work and both CRI jobs don't.
- Otherwise it calls main **0x180(ctx, job)**:
  - reads job+0x18 and ctx+0x1C, then helper 0x1140(job, 1) = the word at **job+0x34** (the low half of
    user data p1 = 0x4512D3E0);
  - if that is nonzero, it allocates 256 B and does its **first DMA**: a 256 B GET from p1 with the tag
    from **ctx+0x24**;
  - a HALT-ASSERT at pc 0x26C fires if that tag is > 31. It would print "[spu] HALT-ASSERT heqi
    pc=0x0026C"; none in perf\audio logs.

So with no DMA at all, the job leaves before 0x26C. The likely reason is that 0x660 returns nonzero
because of what our dispatcher puts at ctx+0x14 (*inferred*). The next most likely is that main reads
job+0x34 as 0 (descriptor not at LS r4).

### One cheap run (img10 and img11, first few jobs)
1. The job's stop code and status when it returns (0x3FFF = JOBCRT error exit).
2. LS ctx 0x3FBC0-0x3FC00 (64 B) at entry, for an img10 job **and for a working game job (img3)**:
   the word at ctx+0x14, and the words at +0x1C and +0x24.
3. LS 0x3FC30-0x3FC40 at entry, to confirm p1's low word is at job+0x34 in LS.

If ctx+0x14 is garbage or nonzero where the CRI JOBCRT expects 0 (or expects a valid pointer), filling
that field correctly in the dispatcher's CellSpursJobContext2 is the fix. That is a runtime change,
so it needs the maintainer's yes.

## Pass 6 (2026-10-27): what the CRI JOBCRT wants at ctx+0x14
Confirmed by the AI assistant (a11): AsrVoice exits with stop 0x3FFF at pc 0x3FF8 every time (373x); game jobs
exit normally. Our dispatcher puts `(dsz/128 & 0xF) << 28` at ctx+0x14 (0x20000000 for 256 B
descriptors), so the low 28 bits are 0.

Reading atomdec 0x660/0x688 (asrvoice 0x4060 is the same code):
- r5 = ctx+0x14; r2 = r5 & 0x0FFFFFFF. **If r2 == 0 it returns r9 = 0x80410A11, an error code**
  (0x688 -> 0x8F0). The caller (0x5D0 / asrvoice 0x3FD0) then stops with 0x3FFF. **So zero low bits
  are the failure itself.**
- With a nonzero value, r21 = r5 & 0x3FFFF is used as an **LS pointer**. Only the word at r21+4 is
  read, and it is stored to 0xF150, which nothing reads afterwards. r21 itself is stored to 0xF110,
  also never read.
- The rest of the routine uses the job descriptor (job+0x0A, +0x18, +0x24, +0x2C, +0x39, and 4 words
  from +0x30) and LS 0x170 to fill its own globals (0xF010-0xF1A0).
- At the end it **clears the low 28 bits of ctx+0x14** (keeps & 0xF0000000) and returns 0, which is
  success.

So JM2 hands the job a nonzero LS address in ctx+0x14's low 28 bits, which the job consumes and
clears. The CRI JOBCRT only checks that it is there. The game's own jobs don't contain this routine,
which is why they run.

### Fix to try (the maintainer OK'd audio code edits; behind a switch)
In runtime/spu/spurs_job.c, where ctx+0x14 is built: `low28 = LS address of the job descriptor`,
i.e. **0x3FC00**. That makes ctx+0x14 = 0x2003FC00 for 256 B descriptors.
- r21+4 is then a readable descriptor word, and nothing else dereferences it (*inferred* from this
  routine).
- Switch: e.g. `SPURS_JOB_CTX14=desc`, default off at first.
- If any later code turns out to dereference the value, use the LS address of a zeroed 16 B scratch
  word instead.

Expect after the fix: img10/img11 no longer exit with 0x3FFF; SPU_DMATRACE shows the 256 B GET from
p1 (atomdec main, tag from ctx+0x24) and PUTs to the voice outA/outB; a nonzero mix at 0x451730F0.

Side note: the SMC message for AsrVoice ("word 0x28000000 at pc 0x3FF4") is the error stop's word
being different at run time. 0x28000000 is `stopd`. It is a symptom of the same exit, not a separate
problem (*inferred*).

**Paused 2026-10-27 01:00.** State: ctx+0x14 fix handed to the AI assistant (pass 6), untested. Next: read the AI assistant's result. If the jobs still exit, dump the 0x660/0x4060 return code and the first PCs after 0x5F8/0x3FF8. If they run but stay silent, follow the atomdec main (0x180) DMA list and the PUT targets against the voice context p2 (outA/outB) and the PPU mixer at 0x451730F0. Speed work (CPU_PROFILE §42-43) is waiting on the no-PERF survey per stage, Namek's slow stretch, and the perf-cheap/rec-dedup tests.

## Pass 7 (2026-10-27): why ~1 in 10 blocks are empty (read-only; fix 8d34572 now default on)
Code facts:
- **The CRI writer treats W == R as "full"** (func_006A5BA4: fill = W-R if W > R, else nBlock-(R-W),
  so W == R gives nBlock = 8, which is >= lead 5, so it doesn't write).
  - If the reader ever catches the writer (an underrun), CRI stops writing until R is ~4 blocks
    past W, because fill = 8 - (R-W) only drops below lead 5 at R-W = 4.
  - So one underrun costs ~4 empty blocks (~21 ms), not one.
  - On a PS3 this never happens: the hardware reads one block every 5.33 ms, smoothly, and CRI
    stays up to 5 blocks ahead (lead 5 = 26.7 ms).
- **Our reader is bursty.** With a WASAPI device, `audio_mix_thread_func` (cellAudio.c ~550) mixes
  a block whenever the device buffer has room for 256 frames.
  - The device frees room once per period (~10 ms), and the buffer is 1056 frames, ~4 blocks. So
    R jumps by 2 or more blocks at a time, as fast as the loop runs, not one block per 5.33 ms.
  - Each jump eats up to ~half of CRI's 5-block lead at once (*inferred*; the QPC pacing path is
    only used when there is no device).
- **The writer is a 60 Hz tick on tid 17**: CRI Server Manager, paced by func_004D4E94 sleeping
  16/17 whole ms through sys_timer_usleep.
  - Each tick also runs the AtomDecoder (~80/s) and AsrVoice (~200/s) jobs synchronously on that
    thread (stubs.cpp jq_push), so a tick's work time adds to its period.
  - A late tick plus a read burst is enough for R to reach W (*inferred*).

### What to measure (one Hidden Leaf run, sound on)
1. Time between consecutive `audio_mix_one_block` calls, as a histogram (0-1 ms = burst,
   ~5.3 = smooth, >10 = gap), plus `[audio-rate]` silent %.
2. On tid 17: the CRI tick period (between successive returns of func_004D4E94) and the time spent
   inside jq_push per tick, as histograms.
3. Every time func_006A5BA4 returns 0 while samples are pending (its "full" exit): log W, R and the
   fill. A fill of 8 with W == R is the stall described above.

### Fixes to try (in order, each behind a switch)
1. **Smooth reader:** pace the mix thread on QPC at 1 block per 5.333 ms even with a device, and let
   the device buffer absorb it. Keep 2-3 blocks queued in WASAPI; mix early only if it drops under
   1 block. That alone should stop R jumping into W (*inferred*).
2. If ticks are late: run the CRI jobs off tid 17 (asynchronous push with a completion), or, if the
   tick sleep overshoots, make sys_timer_usleep for >= 1 ms waits precise (high-resolution timer).

**Paused 2026-10-27 (project paused).** State: audio gap analysis (pass 7) handed to the AI assistant; outcome in FINDINGS. Speed work: see CPU_PROFILE §42-43 (v4 tick off by default; perf-cheap, rec-dedup drafted).

## Pass 8 (2026-10-27, after the AI assistant's CRI_LEAD=7): the remaining 0.9-2.6% gaps
The AI assistant (FINDINGS "Audio glitches"):
- CRI server ticks are mostly 10-18 ms, but 22-50 ms several times every ~2.7 s.
- CRI_LEAD=7 (capped at nBlock-1) cut silent blocks from 12% to 0.9-2.6%.
- QPC pacing, high-res usleep and thread priority didn't help.

Ideas (*inferred*, read-only):
1. **More ring, more lead.**
   - CRI takes nBlock from cellAudioGetPortConfig and its fill/lead math is modulo nBlock, so a
     bigger ring lets the lead grow past 7.
   - Option: AUDIO_NBLOCK=16. Open the port with 16 blocks (and allocate for it) whatever the game
     asked for, report nBlock 16, and set CRI_LEAD=12 (64 ms). That covers the 50 ms late ticks with
     room to spare.
   - Risk: other code using the game's own nBlock (e.g. the portSize it saved). CRI's driver reads the
     config fresh each block (func_006A5BA4), so CRI itself should follow (*inferred*).
2. **Find the late tick's cause.** The ~2.7 s period suggests a periodic task on tid 17: a CRI FS
   stream read (a chunk refill), or a burst of synchronous AtomDecoder jobs at a chunk boundary.
   - Point the DRAW_SAMPLE/GAME_SAMPLE sampler at tid 17 (the same ds_add_current hook, from the
     CRI server loop), and keep only the samples taken inside ticks longer than 20 ms.
   - If the time is in a file read, make that read asynchronous or prefetch it. If it is decode
     jobs, run them off tid 17.

## CRI_LEAD without the hand edit (installer branch)
The lead write used to be a hand edit in the lifted `ppu_recomp_035.cpp` (after the `cellAudioGetPortConfig` call in
`func_006A5BA4`), which a fresh lift, and so every installer build, does not have. `jstars/port/stubs.cpp` now overrides
`cellAudioGetPortConfig` (NID 0x74A66AF0), calls the runtime's function, and for the call from lr 0x006A5BD8 writes
`min(CRI_LEAD, nBlock-1)` to `obj+0x2008` (obj = r30; checked against `*(obj+0x2010) == port`). `main.cpp` still sets the
defaults AUDIO_NBLOCK=16 and CRI_LEAD=12; `CRI_LEAD=0` keeps CRI's own lead. A `[cri-lead] lead 5 -> 12` line in stderr shows
that it ran. Not yet run on a build of this branch.
