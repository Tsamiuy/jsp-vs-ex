# A2C flag in the capture headers (logging only)

Patch: `a2c-log.patch` (`libs/video/rsx_d3d12_backend.c`, +6/-3), on c3fe813. `git apply --check`
passes, and a syntax-only compile with the build's warnings shows no errors. Not built or run.
Behaviour is unchanged.

Each draw now records whether it asked for alpha-to-coverage (SET_ANTI_ALIASING_CONTROL bit 4) in
bit 24 of `alpha_ctl`, with or without `MENU_A2C`. `vp_record_cb` passes it on as `fp_alpha.w`, so
every RSX_CAP shader header reads `alpha=(enable, ref, func, a2c)`. No shader reads `.w`.

Use it with the leaf capture in STAGE_TEXTURES.md §12.
