# Ace intro card: RPCS3 vs our port (read-only comparison, 2026-10-20)

Sources: RPCS3 `Screenshot 2026-10-20 174958.png` (wide) and `180305.png` (close-up);
ours `jstars\run\shots_nbatch\shot_260s.png` (default settings, native strips, no shadow passes,
the best recent Ace shot; the next-latest `shots_shadowcmp\shot_260s.png` is a shadow-pass test run
showing a black silhouette). The cameras are at different moments of the same intro, so this is
a qualitative comparison. Our shots are 1280x720.

| Item | RPCS3 | Ours (nbatch 260 s) |
|---|---|---|
| Skin colour | smooth, even **peach** | peach base with large **cream/tan patches** with hard edges over the arms, chest, face ("camo") |
| Skin shading | soft gradient shading; faint muscle creases and a rosy cheek | flat; the patches look like a mask, no soft shading |
| Face | freckles, eyes, brow lines and cheek blush intact | freckles visible but blotched; face covered by the same patches |
| Hat | smooth orange with soft shading and a darker rim; clean edges | patchy tan/brown texture with a pixelated, stepped edge, lighter than RPCS3's orange |
| Hair | dark grey with tonal strands | darker, with similar strands; closer to RPCS3 than the skin is |
| Beads, goggles, necklace | red beads, blue-teal goggles with clean edges | same colours and shapes; beads match well |
| Sky | soft green-cyan gradient, bright yellow-green clouds | stronger teal/yellow, noisier; clouds blotchy |
| Stage | island and sea, smooth | rocky mountain texture, **neon yellow-green grass with dithered, noisy edges** along the bottom |
| Shadows | none visible on Ace (even skin) | none correctly: default has no shadow pass; with `SHADOW_PASSES=1` the character becomes a black silhouette (shots_shadowcmp) |
| Overlay | subtitle and name plate identical | identical |

## What this says
- The camo blotches are **not in RPCS3**, so they are a port bug (FINDINGS "Shadow passes
  pass 2" already says they are not shadows: same blotches with the shadow passes off).
- The pattern (large flat patches with hard edges, matching the skin region borders) fits the
  causes still open in `PALE_TEXTURES.md`: wrong texture sampling or lighting inputs, rather than
  gamma alone. In RPCS3 the soft shading shows the shadow/lighting term is smooth.
- Hat edge stepping and the dithered grass suggest a texture filter or mip problem (PALE_TEXTURES
  causes 3 and 4).
- Ace's skin colour in RPCS3 is the target. A pixel-colour check on cheek, hat and floor is still
  to be taken (RPCS3_COMPARISON.md section 2).
