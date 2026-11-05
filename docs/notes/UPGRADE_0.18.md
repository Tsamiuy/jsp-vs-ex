# ps3recomp v0.18.0 check (2026-10-27, read-only)

Release: v0.18.0 "One Hundred Fifty-Five", tagged 2026-10-26 (380 commits, 256 files,
+23.8k/-1.7k lines since v0.17.0; `rsx_d3d12_backend.c` +1155/-148). Notes: upstream README "Changelog".
Checked in a throwaway cloud clone; nothing downloaded to the PC, nothing changed.

**Download:** neither zip is needed. We work from git (`ps3recomp-src`, bundles). "Source code (zip)"
is the source without git history. The other zip is the prebuilt win-x64 SDK bundle, which is not
enough on its own (SETUP.md). When we upgrade, we fetch the tag through git (cloud + bundle, as for 0.17).

**What matters for us**
- D3D12 (@rigaz29): indexed lists drawn with their own indices (#530), depth/stencil state and samplers in the
  guest-FP pipelines (#558), per-zeta-offset depth surfaces readable as textures + FP depth export (#559),
  texture upload ring with LRU slots (#579), vertices converted once per draw (#518), batch held across two
  flips (#583), texture-slot SRVs no longer overwrite the SRV heap (#557), `D3D12_VSYNC=0`, RenderDoc capture.
  These overlap our zeta targets, TEX_POOL/TEX_HINT slots, vertex fetch and present pacing.
- Small, possibly useful fixes: shadow compare is `depth FUNC reference` (#424, shadows), flip restores
  ZSTENCIL clear value / 0 = near plane (#526), WPOS conventions (#446, #589), 28-bit vertex array wrap (#504),
  getenv out of hot paths (#437/#519, like our rsx_env.h).
- SPU: AVX2/SSE4.1 helpers (#500, #512), per-thread dispatch cache (#481), DMA/reservation work.
- Runtime: offline NP/NpEula answers, cellPad fixes, WASAPI 60 ms queue (#514), cellVdec drain.
- **Known issue in the release:** a SPURS `EventFlagWait` boot hang (You Don't Know Jack, 4 in 4 runs).
  J-Stars uses SPURS.

**Trial merges (cloud, throwaway)**
- `jstars-port` (5896aa6, on v0.15.0) + v0.18.0: **122 conflict blocks in 22 files, 61 in
  rsx_d3d12_backend.c** (cellGcmSys 10, rsx_fp_decompiler 12, rsx_commands 7, ...). Includes the
  0.15 to 0.17 conflicts we never resolved.
- `generic-fixes` (0468f71, on v0.17.0) + v0.18.0: **no conflicts.**

**Recommendation:** don't move J-Stars to v0.18.0 now: very large backend overlap and a fresh SPURS
regression. Rebase `generic-fixes` onto v0.18.0 (clean) and use v0.18 as the base for the next game
(the next game). For J-Stars, at most cherry-pick small items (#424 shadow compare, #526, #557) one at a time
with an fps/screenshot check, after the maintainer approves.
