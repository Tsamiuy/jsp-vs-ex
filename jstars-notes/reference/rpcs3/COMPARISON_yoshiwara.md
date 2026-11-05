# Yoshiwara Paradise: RPCS3 gameplay video vs our port (2026-10-21)

## Sources

- **RPCS3:** `Screen Recording 2026-10-21 170237.mp4` (5 min 52 s, 1920x1080, RPCS3 overlay on).
- **Frames:** 12 frames, one every 30 s, in `frames\frame_NNNs.jpg`. They were extracted with
  Windows' built-in media stack (`Windows.Media.Editing`, a script in my scratchpad); ffmpeg is not
  installed and nothing was installed.
- **The stage is Yoshiwara Paradise (Gintama):** `frame_060s.jpg` is its Stage Select screen.
  Teams: Toriko, Naruto, support Luffy vs Ichigo, Goku, support Gintoki.
- **What the video covers:**
  - 0:05 main menu;
  - 1:00 stage select;
  - 1:30 Luffy intro card;
  - 2:30 to 5:30 fight: wide hall, stairs, side rooms, an Ultimate Attack at 4:30.
- **Ours:** `menu\tours\t6_texmips_default\` (newest Yoshiwara run, defaults + `TEX_MIPS`):
  - `16_fight_20s.png`, `18_fight_35s.png`;
  - `13_intro_a.png` (the Naruto card).

Cameras and moments differ, so this is a look comparison, not pixel by pixel. The RPCS3 frames are
video-compressed and 1080p; ours are 720p screenshots.

## Differences, most noticeable first

1. **Overall tone: contrast, brightness and colour.**
   - RPCS3 looks warm and soft, with darker mid-tones and a light haze over the hall.
   - Ours is harsher: bright areas clip to near-white or flat yellow (the upper gallery's lamp
     strips, the shoji panels, the right-hand floor tiles), and dark areas go flat teal or black.
   - This is the main input for the colour and brightness work. Likely in the post chain: the
     tone-map pass (fp `0x0FC9FC01`-type, gamma 0.4545), its exposure source (the luminance
     downsample chain, 20x11 / 80x45 targets, §20), or the bloom composite.
2. **No glow (bloom) around bright things.** In RPCS3 the gold lanterns, white panels and energy
   effects have a soft halo (Ichigo's beam at 4:00, the hall lights at 3:30). Ours has none. This is
   the same post chain: the bloom targets exist (§20), but their result may be lost or very weak.
3. **Floor sheen.**
   - RPCS3's tiled floor is mid-grey with a glossy reflection and specular sheen that follows the
     camera.
   - Ours is flatter: one side dark teal, the other side very bright tiles, and little reflection.
4. **Character shading.** RPCS3 skin is smooth and softly shaded (Luffy at 1:30, Toriko in the fight).
   Ours is flatter, with the known blotches (texture unit 1, §31).
5. **Background softness on intro cards.** The shoji wall behind Luffy is slightly soft in RPCS3; our
   Naruto card's background is sharp. This could be depth of field, or just video softness. Low
   confidence until the same card is compared.
6. **Dust and smoke.** RPCS3 shows smoke and haze near the floor during hits (4:30) and black rock
   chunks flying (2:30). We have no matching frame yet.
   - The black chunks matter: this stage really draws large dark stone pieces when the floor
     breaks. So not every dark block in our shots is a bug. Compare at the same moment before
     filing one.
7. **Texture sharpness.** Ours is crisper and grainier; RPCS3 is smoother. Part of this is RPCS3's
   output and the video. The rest is mipmaps and filtering (`TEX_MIPS`, the unit-0 point sampler).

## Suggestions for the colour and brightness work

- Capture one fight frame with `RSX_CAP_PASSES=2`. Compare the main scene RT before the post chain
  with the final image, to see whether the harshness comes from the scene or the post chain.
- Then check the tone-map pass inputs (exposure / average luminance texture) and the bloom
  composite against what RPCS3 shows.
- An RPCS3 screenshot of a fixed camera, like our `16_fight_20s.png` (fight start, facing the hall),
  would allow a direct colour check.
