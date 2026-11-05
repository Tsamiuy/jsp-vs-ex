# Features

What the port adds or changes compared with the PS3 game, from the notes (`docs/notes/STATUS.md`,
`docs/notes/FINDINGS.md`). Anything not listed there has not been checked.

- **Higher resolution.** `RES_SCALE` renders at 720p, 1440p, 4K or 5K (5K is marked not tested). Measured on one
  machine in a match: 1440p about 29.0 frames per second and 4K about 28.4. The window can be resized, and F11
  or the launcher's fullscreen option gives borderless fullscreen.
- **Exit Game.** In Options, the PSN-only Cross-Save entry is now "Exit Game", with a Yes/No box.
- **Launcher.** A window to start the game, pick the resolution and window size, choose how to play, rebind keys
  and set the fight HUD size. Details: `docs/notes/LAUNCHER.md`.
- **Input.** Keyboard, keyboard and mouse (mouse camera and clickable main menu) or controller, each with its own
  bindings. A DualSense or DualSense Edge over USB or Bluetooth is read directly. The notes list hands-on tests of
  the DualSense buttons, mouse mode and rebinds as still pending.
- **Two players.** A keyboard or a second controller can be player 2. Split-screen fights (different teams) are
  drawn by the game as before; the notes mark improving them as parked, and the fight HUD size setting leaves
  them at full size.
- **Training Mode.** Free Battle offers a "Free Battle / Training" choice: the game's own character select, partner
  slots locked, a 0-star computer fighter stands still, nobody is KO'd and health refills 1.5 s after the last hit,
  unlimited time, no J-Points. Scripted keyboard runs pass; not tested yet: KOs in a normal Free Battle right after
  Training, a fight with a human Player 2, a mouse click on the Training box. Details: `docs/notes/TRAINING_MODE.md`.
- **Rendering.** The bump-mapped texture instructions (`fp-bem`) are decompiled instead of rejected.

Parked in the notes: split-screen layout, brightness and glare, DLSS.
