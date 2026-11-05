# RPCS3 reference shots: index

All taken by the user in RPCS3 with the performance overlay on (~1918x1080 captures; ours are
1280x720). The files are currently in this folder, not in a `shots\` subfolder (the user's
screenshots are named by Windows' default `Screenshot 2026-10-20 HHMMSS.png`). Nothing was moved or renamed.
RPCS3's compatibility list rates J-Stars **"Ingame"** (not "Playable"), so only intro, title and demo are
reliable references (see `..\..\RPCS3_COMPARISON.md`).

| File | Scene | Overlay (FPS / CPU / guest RSX) |
|---|---|---|
| `Screenshot 2026-10-20 174938.png` | Title screen, "Press any button" (character collage, yellow Korosensei bottom right) | 30.00 fps, 33.3 ms; host CPU total 8.6 %; guest RSX 1 % |
| `Screenshot 2026-10-20 174948.png` | Title screen, a few seconds later (same layout) | 30.01 fps; CPU 9.5 %; guest RSX 1 % |
| `Screenshot 2026-10-20 174958.png` | Intro card: Portgas D. Ace, One Piece, island and sea behind, subtitle "Looks like you've been taking care of my little brother" | 30.03 fps, but min 20.9 and frametime max 150.2 ms (spike near loading); CPU 10.6 %; guest RSX 21 % |
| `Screenshot 2026-10-20 175018.png` | Title screen, second character set (Sasuke, Rukia, Beelzebub, Gintama, Medaka, Kuroko) | 29.92 fps; CPU 9.4 %; guest RSX 1 % |
| `Screenshot 2026-10-20 175042.png` | Intro card: Naruto Uzumaki on a hillside, Rukia in the corner | 30.00 fps, min 9.0 ms frametime (jittery trace); CPU 11.6 %; guest RSX 11 % |
| `Screenshot 2026-10-20 180305.png` | **Close-up of Ace's face and hat** (intro card, taken later than the others) | 30.02 fps, min 16.6 ms / max 83.4 ms; CPU 10.7 %; guest RSX 22 % |

## Overlay summary
- Every shot runs at about 30 fps: this is the frame cap, so it says nothing about speed headroom.
- Ace intro card: framerate min about 21 fps, one 150 ms frametime spike near loading.
- Host CPU (PPU+SPU+RSX threads): about 9 to 12 % total on every shot (PPU about 3-4 %, SPU about 3-4 %, RSX about 3 %).
- Guest RSX load: 1 % on the title, 11 % to 22 % on the intro cards.
- RPCS3 settings (renderer, resolution scale, etc.) were not recorded: add them to `settings.txt`.

## Video

- `Screen Recording 2026-10-21 170237.mp4`: gameplay, 5 min 52 s, ~30 fps. Main menu, then Stage
  Select (**Yoshiwara Paradise**), the Luffy intro card and a full fight. Frames every 30 s are in
  `frames\`; the comparison is in `COMPARISON_yoshiwara.md`.
- **Menu reference frames from the same video** (`frames\frame_001s` to `frame_060s`): main menu
  (0:01-0:10), Free Battle and **Character Select** (~0:15-0:25), and **Stage Select** scrolling
  through stages (~0:35-1:00; Kameari Police Station at 0:40).
  - Character select shows the title bar text "Character Select" (empty in our port) and clean CPU
    difficulty stars (speckled in ours): direct references for the menu fixes.
