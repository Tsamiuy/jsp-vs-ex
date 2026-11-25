# Code map

A map of the code for maintainers: which layer a file belongs to, where the program starts, how the two kinds of online play
are wired, which toolkit patch the glue depends on, and how the build and the checks run. It describes `main` only; changes
that are still in review are named in one line where they matter, nothing more.

| Page | What |
|---|---|
| [layers.md](layers.md) | The layers from setup to lifted code, every glue file by purpose, the entry points and the startup sequence |
| [netcode.md](netcode.md) | The game's own Online Battle and the experimental rollback netcode: settings, start decision, match sequence, threads |
| [patches.md](patches.md) | Every toolkit patch on `main`: number, toolkit files, area, switches and the glue that calls into it |
| [build-and-test.md](build-and-test.md) | The player build (payload, setup, first run, CMake), the CI workflows and jobs, the host tests |
| [mods.md](mods.md) | The mods folder: load sequence, the overlay and loose-file tables, the interfaces, how a mod set reaches online, and what the verifier checks against the loader |

## How to read it

- Paths are relative to the repository root. `jsp/` is short for `installer/port/jsp/`, `patches/` for
  `installer/port/patches/`. Toolkit paths (`runtime/...`, `libs/...`, `tools/...`) are inside ps3recomp v0.19.0 as
  `installer/tools/export_toolkit.py` exports and patches it.
- `file:line` points at the line on `main` when the map was last brought up to date (commit `876e32c`). Lines drift: search for the
  symbol named next to it.
- Hex numbers such as `0x00642C3C` are addresses of game functions in the US 1.00 EBOOT, the ones the glue hooks.
- **inferred** marks a statement read from the code's shape or comments, not traced through or run.
- The diagrams are Mermaid; GitHub draws them.

## Where to start

| You want to change | Start at |
|---|---|
| What happens before the game's first instruction | [layers.md, startup sequence](layers.md#4-startup-sequence), `jsp/main.cpp` `main()` |
| A hook on a game function | `jsp/stubs.cpp` `ps3_load_prx_modules()` (registration order), `jsp/jsp_hook_chain.h` |
| A lift-time hook (entry/leave wrapper, instruction patch) | `jsp/lift_game_hooks.json`, `jsp/lift_insn_patches.json`, `installer/tools/firstrun.py` `step_lift` |
| The renderer or another runtime library | [patches.md](patches.md), then `patches/INDEX.md` and the patch itself |
| A setting | `jsp/jsp_settings.h` (key table shared with the settings window), `installer/launcher/launcher.c` |
| Online or rollback | [netcode.md](netcode.md), [../ROLLBACK.md](../ROLLBACK.md), [../ONLINE.md](../ONLINE.md) |
| The mods loader, an interface or the mod verifier | [mods.md](mods.md), [../MODS.md](../MODS.md) |
| How a rollback match starts and ends (team size, KO, disconnect, stall, adaptive delay) | [netcode.md, start and match](netcode.md#4-rollback-start-and-match) |
| A test | [build-and-test.md, host tests](build-and-test.md#3-host-tests), `tools/ci/host-tests.txt` |

## Open findings

Things the mapping turned up that are not fixed here. Each is a lead to check, not a confirmed bug.

| Finding | Evidence |
|---|---|
| `--env NAME=VALUE` is applied inside `main()`, after the static constructors, so it cannot set switches read before `main()` (the `SIX_*` defaults in `feat_3v3.cpp:59-66` and their `MENU_PAGES` / `SIX` gate, the rollback start decision, the online ini overrides, `JSP_SNAPSHOT_THREADS`); `MENU_PAGES` for the menu itself is read later (`feat_menu_pages.cpp:178`) and does reach it. inferred from the order | `jsp/main.cpp:594-606`; constructors in [layers.md](layers.md#process-start-before-main) |
| The test autopilot (`AUTO_FIGHT`, `AUTO_MENU`) sets `g_ppu_icall_hook` and `g_cellpad_filter`, then `jsp_register_game_overrides` assigns both without chaining, so the autopilot may be inert. inferred, not run | `jsp/jsp_autopilot.cpp:205-208`, `jsp/jsp_game.cpp:4568`, `:4574` |
| The `six_stacks` / `six_arm` diagnostic threads exist twice | `jsp/feat_3v3.cpp:252-365`, `jsp/jsp_game.cpp:2347-2460` |
| Code comments still give old names: the first-run docstring and its profile table say `JStarsRecomp.exe` (the target is `jsp`), and three comments call the title-logo hook "toolkit patch 0006" (it is 2013) | `installer/tools/firstrun.py:18-19`, `:66`, `:76`; `firstrun.py:870`, `installer/tools/ex_logo_art.py:2`, `installer/tools/assemble_payload.py:204` |
| `firstrun.py` `PROFILE["toolchains"]` says `vs-buildtools` and "llvm-mingw is not wired up here", but the build step uses only llvm-mingw | `installer/tools/firstrun.py:59`, `:778-797` |
| `patches/INDEX.md` declares groups 0, 1, 2, 3 and 6; group 7 (22 patches) is not declared, group 0 ("build") holds runtime fixes, and 3002/3003 (video decode) sit in the audio group | `installer/tools/patch_index.py:77` (`GROUPS` at `:20`) |
| `PumpThread` (tag `K` heartbeat) has no caller in the glue; only a host test starts it (`installer/tools/net_transport_test.cpp:148`), although the header's thread contract names it as a user of the mux. inferred from a search | `jsp/jsp_net_session.h:108`, `jsp/jsp_net_session.cpp:212-240` |
| `jsp_net_team_rule.h` and `jsp_train_readout.h` both declare `namespace jptr`; their names do not clash today and no file includes both, but a later name in either could | `jsp/jsp_net_team_rule.h:12`, `jsp/jsp_train_readout.h:29` |
| No workflow runs `patch_index.py --check`, `fetch_toolchain.py --check --strict`, a CMake configure of `installer/port/jsp`, or compiles `installer/jsp.iss` on a pull request | [build-and-test.md](build-and-test.md#2-ci) |
