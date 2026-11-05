# PRIM_LOG: which primitive types the game draws that we drop (logging only)

Patch: `prim-log.patch` (`libs/video/rsx_d3d12_backend.c`, about +35), on c1ef556. `git apply --check`
passes, and a syntax-only compile with the build's warnings shows no errors. Not built or run.
Behaviour is unchanged.

`PRIM_LOG=1` logs every draw whose primitive is not TRIANGLES, TRIANGLE_STRIP, TRIANGLE_FAN or QUADS, once
per (call, prim, fp, colour target):
`[PRIM] draw_arrays|draw_indexed prim=N count=N fp=... rt=... tex0=... vp_bytes=N noct=0/1`.
It also prints a running total every 600 such draws. Primitive numbers: 1 POINTS, 2 LINES, 3 LINE_LOOP,
4 LINE_STRIP, 9 QUAD_STRIP, 10 POLYGON.

Use: a fight with Goku's laser skill and one other projectile skill; see VFX.md "What the AI assistant should capture".
