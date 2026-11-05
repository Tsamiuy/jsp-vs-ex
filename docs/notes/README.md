# J-Stars Victory VS+ — ps3recomp port notes

Notes from the static-recompilation port of **J-Stars Victory VS+** (BLUS31519, US disc) with
**ps3recomp v0.15.0** plus local changes (branch `jstars-port` in `ps3recomp-src`). Written so the work can
be resumed without rediscovering anything.

## Where it stands (2026-10-30)

The game plays from start to finish on PC: opening movie with sound, every Main Menu mode, fights at
~29-30 real frames/s on every tested map (CPU_PROFILE §42; 4K 28.4 f/s, FINDINGS "Higher resolution"),
music and sound effects (FINDINGS "Audio"), skill effects and shadows fixed. Render size 720p / 1440p / 4K
(RES_SCALE 1/2/3), fullscreen, keyboard / keyboard & mouse / controller (DualSense) input, fight HUD size,
point-and-click main menu, Exit Game in Options, 2 players. A Training mode was added (TRAINING_MODE.md, FINDINGS "Training mode step 3"); a few cases are still untested, listed in TRAINING_MODE.md.
What changed, how it was tested and the evidence: table at the top of STATUS.md. History: STATUS.md.

## Start here

| File | What is in it |
|---|---|
| [STATUS.md](STATUS.md) | "Right now" on top, the table of recent changes with their tests and evidence, then the dated log |
| [FINDINGS.md](FINDINGS.md) | Every problem: symptom → root cause → fix → files (newest at the end) |
| [SETUP.md](SETUP.md) | Folder layout, installed tools, lift / build / run, scripts and env vars |

## Topic notes

| File | What is in it |
|---|---|
| [CPU_PROFILE.md](CPU_PROFILE.md) | Speed work: profiles, A/B runs, every speed switch (§1-§43) |
| [AUDIO.md](AUDIO.md) | How sound was fixed (CRI SPU jobs, SPURS_CTX_PAD, AUDIO_NBLOCK, CRI_LEAD) |
| [VFX.md](VFX.md) | Skill effects / beams (SPURS job stack gap fix) |
| [SHADOWS_ENV.md](SHADOWS_ENV.md) | Environment shadow flicker (fixed by ZPASS_LOD0) |
| [RESOLUTION.md](RESOLUTION.md) | 4K plan (done: RES_SCALE) |
| [LAUNCHER.md](LAUNCHER.md) | `J-Stars Launcher.exe`: settings file, key bindings, tests |
| [TRAINING_MODE.md](TRAINING_MODE.md) | Training mode design and build steps. Training settings menu in the pause menu (Unlimited Stamina, Free Camera). Time limit lock on Stage select |
| [FREEZE_FIX.md](FREEZE_FIX.md) | Fight freeze: cause (counter overflow) and fix |
| [TEST_FIGHT.md](TEST_FIGHT.md) | `Test Fight.bat`: open the game straight into a Training match with chosen fighters and stage, about 72 s from launch. How to use it, timings, what it can't skip |
| [COLOR_EXPOSURE.md](COLOR_EXPOSURE.md) | Brightness / glare study (parked until near the end) |
| [STAGES.md](STAGES.md), [STAGE_PLAN.md](STAGE_PLAN.md), [STAGE_TEXTURES.md](STAGE_TEXTURES.md) | Per-stage rendering checks |
| [PALE_TEXTURES.md](PALE_TEXTURES.md), [GREEN_SURFACE.md](GREEN_SURFACE.md) | Early texture bugs (solved) |
| [TEXTURE_PACKS.md](TEXTURE_PACKS.md) | Texture replacement / upscaling ideas (open) |
| [DLSS.md](DLSS.md), [UPSCALERS.md](UPSCALERS.md), [FPS_60.md](FPS_60.md) | Upscaling and >30 fps answers |
| [GAME_INTERNALS.md](GAME_INTERNALS.md) | The game's own architecture: job system, fibers, file server, CRI, addresses |
| [GAME_MAP.md](GAME_MAP.md) | Index of the game's data files, triggers and main functions, each marked tested or read-only |
| [WHAT_STARTS_A_SPECIAL.md](WHAT_STARTS_A_SPECIAL.md) | How a button press becomes a special move: the request chain, the move graph file, Jotaro's second actor |
| [DEBUGGING.md](DEBUGGING.md) | How problems were found: tools, env vars, debug console |
| [FAKED_IMPORTS.md](FAKED_IMPORTS.md) | Faked / missing imports |
| [UNUSED_CONTENT.md](UNUSED_CONTENT.md) | Search for unused fighters, moves, stages and menus in the game data; the map99 test stage and the Tokyo Game Show character select |
| [PLAYABLE_EXTRA_CHARACTERS.md](PLAYABLE_EXTRA_CHARACTERS.md) | Can Jotaro, Dio, Dai or Asakura Yoh be made playable? What is on the disc and what a borrowed body does |
| [RUNTIME_BUGS.md](RUNTIME_BUGS.md) | Early toolkit bugs (written 10-19; the full list of toolkit changes is `git log v0.15.0..jstars-port`) |
| [REUSABLE.md](REUSABLE.md) | What carries over to the next game |
| [CARDS.md](CARDS.md) | The 276 cards: where the names and the save bits are, how many exist per character, how the save was checked |
| [UPGRADE_0.17.md](UPGRADE_0.17.md), [UPGRADE_0.18.md](UPGRADE_0.18.md) | Upstream toolkit upgrade surveys (decision: stay on v0.15.0) |
| [RPCS3_COMPARISON.md](RPCS3_COMPARISON.md), [RPCS3_BEAM_READ.md](RPCS3_BEAM_READ.md) | RPCS3 reference captures (`jstars-notes/reference/rpcs3/`) |
| [jstars-notes/tools/README.md](../../jstars-notes/tools/README.md) | RSX_CAP capture helper scripts |
| [jstars-notes/patches/](../../jstars-notes/patches/) | Write-ups of single experiments as diffs (against the branch of that day, **not** a build recipe; the real changes are the `jstars-port` commits) |

Last updated: 2026-10-30 (doc refresh; no code changed).
