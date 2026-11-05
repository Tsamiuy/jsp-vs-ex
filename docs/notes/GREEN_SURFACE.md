# Green surface drawn over the characters (Goku intro card): causes (2026-10-20)

Read-only study. Nothing was built or run. Sources:
- `ps3recomp-src` at c1b6225 (`rsx_d3d12_backend.c`);
- FINDINGS §22, §27 and §29;
- captures `rsxcap_flush1`, `rsxcap_pass3`, `rsxcap_exp32`;
- screenshots `run\shots_nbatch\shot_300s.png` and `run\shots_shadowcmp\shot_300s.png`;
- `[PERF2]` lines in `run\stderr_shadowfix.txt` / `stderr_kick.txt`.

**Symptom:** a large green ground/water surface with stretched streaks fills the lower left of
the Goku intro. Its edge is a hard straight diagonal, and it covers Goku's face and body and the
rocks. Only text draws over it.

> **Update (2026-10-20, after the RPCS3 comparison): cause 2 is the one.** The 18:30 shot is
> from our port. The user's RPCS3 shot of the same moment (`184154`, with the overlay) shows the
> green layer is the **water**: blue/teal and translucent, lying *behind* Goku. Capture
> `rsxcap_flush1` op 317 is that water: fp `0x0FC9F281`, one 6-vertex quad on the scene RT
> `0x0ED10000`. It samples the 256x256 reflection RT `0x09BE5E80` (t0), the refraction downsample
> `0x09C65F00` (t1), water normal maps (t2-t5), a 1024x1024 DXT1 (t6), the shadow maps (t7/t8) and
> the scene zeta `0x0E960000` (t15). It is drawn *after* the switch to the reflection and
> downsample targets, and the backend clears its shared depth buffer on every switch. So the
> water is tested against an empty buffer and covers everything drawn before it.
>
> - **Fix:** `patches/zeta-targets/` (depth kept per zeta surface, `ZETA_TARGETS=0` = old). It
>   also gives the water its real scene depth at t15, which it presumably uses to fade the shore.
>   That probably explains the wrong colour as well (green streaks instead of translucent blue),
>   because t15 used to get the screen copy.
> - **Cause 1** (depth clamp) is now commit 7340a7b. It is a correct change, but not this bug.

## Likely causes, ranked

### 1. Depth is clamped, not clipped, at the near plane (most likely; patch ready)

- **Code:** every guest-FP pipeline is built from a zeroed `D3D12_GRAPHICS_PIPELINE_STATE_DESC`
  (`vp_get_fp_pso`, also the VP fallback in `compile_vp`). The backend never sets
  `RasterizerState.DepthClipEnable` (`grep` finds it nowhere), so it is FALSE: D3D *clamps* depth
  to [0,1] instead of clipping.
  - RSX clips at the near and far planes by default (`SET_ZMIN_MAX_CONTROL`, which this backend
    does not decode).
  - The VP epilogue already maps GL clip z to D3D's 0..w range (`vp_posscale/posoffset` from the
    viewport z scale and offset), so turning on D3D clipping gives RSX's near/far clip exactly.
- **Why it fits:**
  - The camera in this card sits low, next to Goku on the ground. The ground or water plane
    runs under the camera, so part of it lies between the camera and the near plane.
  - On RSX that part is clipped away. Here it is clamped to depth 0, the nearest possible value,
    so it wins the depth test against everything.
  - It ends at the line where the plane crosses the near plane, which projects to a hard straight
    edge.
  - Its texture is stretched, because those triangles are millimetres from the camera.
  - It covers characters and rocks alike, but not the text, which is drawn after it with no depth
    test.
- **Patch:** `patches/depth-clip/depth-clip.patch`. It sets `DepthClipEnable = TRUE` on the
  guest-FP and VP fallback pipelines. `DEPTH_CLAMP=1` brings back the old clamping.
  `git apply --check` passes on c1b6225 and on the main checkout as of 18:40. The file
  syntax-checks clean.
- **Risk:** anything the game draws with z outside the clip range will now disappear instead of
  being clamped, for example a fullscreen pass whose quad sits at z = -w. If the title, the HUD or
  the post chain loses a pass, `DEPTH_CLAMP=1` shows whether this is why. The fix would then be to
  clip scene draws only.

### 2. Depth thrown away between colour targets or batches (unlikely right now)

- The shared depth buffer is cleared at the start of every batch and on every colour-target
  switch (FINDINGS §26, lead 2).
- Scene geometry drawn after a switch away and back, or after a mid-frame flush, is tested against
  an empty buffer and covers everything drawn earlier.
- Recent runs show `mid-frame render 0.00s` (no flush since native strips). The captured frames
  draw the whole scene on `0x0ED10000` with no switch between geometry draws (the 256x256
  `0x09BE5E80` pass and the post chain come after). So this probably isn't the cause today.
- Those captures predate native strips, though. A fresh capture would confirm.

### 3. Guest depth test, function and write mask are ignored for colour passes

- Every colour draw gets LESS_EQUAL + write (the shadow-pass round 2 honours the guest state for
  depth-only passes only).
- A backdrop drawn last at the far plane, or a surface drawn with writes off, behaves differently
  from RSX. This changes what wins only in unusual setups, so it ranks low for this symptom.
- It is worth a switch later: the same `zstate` that round 2 records could feed colour PSOs.

### 4. Not occlusion, but the water's look

- The post chain (and possibly the water) samples `0x0E960000` 1280x720. That is most likely the
  scene's depth buffer, but it is not a tracked RT, so it gets the screen copy (`SCREEN_AS_TEX`).
- A water shader that fades by scene depth would then come out opaque or streaky. This does not
  explain covering the characters.

## What the AI assistant should test

1. Apply `depth-clip.patch`, build, and run the demo to the Goku intro (screenshot at ~300 s as in
   `shots_nbatch`), with `PS3_VERBOSE=0`.
   - If the green wedge is gone or shrinks to the real water line behind Goku, this is the fix.
2. Same build with `DEPTH_CLAMP=1`: the wedge should come back. This is the A/B.
3. Re-check the title screen (`shot_055s`) and a later match frame for missing passes (see the
   risk under cause 1).
4. Only if the wedge stays: take an `RSX_CAP` of the intro with the current build and look for a
   colour-target switch or batch split before the green draw (cause 2).
