# Can J-Stars run above 30 fps? (2026-10-27, read-only)

Short answer: not by unlocking the game itself. Its frame pacing waits on vblanks, and its simulation advances by a
fixed step per game frame tuned for 30 fps. More frames per second from the game would mean a faster game, not
a smoother one. The realistic route to smoother motion is frame generation outside the game (option 1 below).

## 1. Where the game paces its frames

- The game's main thread paces every frame in `func_006507DC` (ppu_recomp_033.cpp ~41566; CPU_PROFILE §7/§37:
  it is the thread seen ~95% busy in flip/vblank polls). Per frame:
  1. wait for the previous flip to complete: `func_00733650` (flip status) polled with `sys_timer_usleep(200)`
     (syscall 0x8D, r3 = 0xC8);
  2. SetFlip + flush, then, when its second argument N ≥ 1, wait until a vblank counter at
     `*(*(TOC−0x4A80))` reaches N (it is counted up by a vblank handler), and reset it to 0.
- Flip mode is VSYNC (`cellGcmSetFlipMode(2)`, FINDINGS "frames lock to vblanks"), flips complete on 60 Hz ticks,
  and measured frames are 2 vblanks (30 fps) or 3 (20 fps) (CPU_PROFILE §37-41: 29-30 fps on most stages at best).
  So the game itself never asks for more than one frame per 2 vblanks.

## 2. The simulation step is per frame, for 30 fps

From the skill-effects work (VFX.md, FINDINGS tscale/track):
- The particle updater (`func_0063E19C`) gets dt = 1.0 every call: one frame, not seconds.
- A global timing object (`Q = *(0x0096CF90)`) holds **30.0** and **1/30**. The effect curves use it to turn
  per-second rates into per-frame steps, and particle lifetimes drop by a fixed amount per frame
  (23.8 → 22.3 → 20.8 …).
- The SPU jobs (animation sampler image 1, particle simulation image 18, quad builder image 17) are kicked once
  per game frame with per-frame inputs.
- So effects, animation, timers and, very likely, movement and input windows all count game frames. At 60 game
  frames per second they would all run twice as fast.

## 3. Hints from outside

- No 60 fps patch for BLUS31519 turned up (RPCS3 patch notes or mods), and even the PS4 version stays at 30 fps
  ([Cubed3 review](https://www.cubed3.com/games/reviews/playstation-4/j-stars-victory-vsplus)). That is a strong
  hint the engine has no 60 fps mode to switch on.

## 4. Options, lowest risk first

1. **Frame generation outside the game (recommended, no code risk).** Keep the game at a steady 30 and let a
   frame-generation tool create the in-between frames. On this PC (NVIDIA RTX laptop GPU), "Lossless
   Scaling" (works on any window) or a driver-level frame generator can show 60 or more. Cost: a little
   latency and occasional artefacts on fast motion/HUD. It works best once the port holds 30 without 20-fps
   dips (the current pacing work).
2. **Interpolation inside the port (high effort, high risk).** Our port would draw an extra frame between two game
   frames with blended object transforms. The game's frames arrive as baked GPU command streams with
   SPU-skinned vertices, so there is no clean "object list" to interpolate; it would mean matching draws across
   frames and blending vertex data. Weeks of work, many edge cases (effects, UI, cuts). Not recommended now.
3. **Run the game logic at 60 (very high risk).** Set the vblank wait to 1, change the timing object to 60, and
   find and halve every per-frame constant (animation, physics, AI, input windows, SPU job inputs). Without a
   known patch, and with the PS4 version also at 30, expect broken game speed and timing. Not recommended.

## 5. Cheap checks the AI assistant could run later (logging only), if we want certainty

- Log `func_006507DC`'s second argument N and the value of the vblank counter at return. N = 1 with an extra
  vblank from the flip wait vs N = 2 tells exactly how 30 is reached.
- Find the writer of the timing object's 30.0 (a guard on `Q + 4` from boot). If it is computed from a frame-rate
  setting (e.g. 60 / N), there may be a hidden mode worth a test. If it is a constant, option 3 is ruled out.
