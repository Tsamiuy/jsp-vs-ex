# Depth clip at the near/far planes

`depth-clip.patch` (`libs/video/rsx_d3d12_backend.c`, +15 lines) sets `DepthClipEnable = TRUE` on
the guest-FP pipelines (`vp_get_fp_pso`) and the VP fallback (`compile_vp`). Until now the zeroed
pipeline description left it FALSE, so depth was clamped. Geometry in front of the near plane was
drawn at depth 0 over everything: the green wedge in the Goku intro. `DEPTH_CLAMP=1` restores the
old behaviour.

Base: c1b6225. `git apply --check` passes on c1b6225 and on the main checkout (18:40). Not built or
run. Why, the risk and the test plan: `docs\notes\GREEN_SURFACE.md`.
