# Known problems

Of the default build (the v19 tree, built on ps3recomp v0.19.0), pre-release 0.3.0. The status of each item is tracked in
[issue #9](https://github.com/Tsamiuy/jsp-vs-ex/issues/9) (latest status comment 2026-11-13). "Not re-checked" means not
run again on v19. To report a problem, see "Troubleshooting and reporting a problem" in the [README](README.md).

| What happens | Affects | Workaround | State |
|---|---|---|---|
| Training, Pause, Restart: the Yes/No box was titled "Hide HUD: Off" | 0.1.0 and 0.2.0 | none needed | fixed in 0.3.0 ([#164](https://github.com/Tsamiuy/jsp-vs-ex/pull/164), see CHANGELOG) |
| Training: Return to character select after using Unlimited Stamina can leave a black screen and freeze; it happens with Unlimited Stamina on or off | 0.1.0, reported by a play tester; not seen in our runs | none known | open; a related fix (Unlimited Stamina no longer writes into fighters the game has freed) is in, the freeze itself is not explained |
| The frame rate drops to about 20 per second in heavy scenes: many fighters on screen, a building being destroyed, the Temple of Athena stage, online matches. In one log it follows the number of draws per frame, not the network | 0.1.0 reports | the "Two-thread drawing" box (`PIPE=1`, Display page, off by default) is an option for busy fights | open; slowdown when buildings collapse is not measured yet |
| Online: a solo battle against the computer started from the online lobby froze on the Versus screen; after a connection error in Ranked, the Online Battle menu froze | 0.1.0 reports | none known; a support bundle made right after the freeze helps | open, no log yet |
| The online lobby's Selected Stage picture is drawn with horizontal stripes | 0.1.0 report | none | open |
| Setup refuses an EBOOT as an unknown build (check `E3`) | game update 1.02, digital editions, other builds | use the EBOOT of the 1.00 disc of the same region as the disc folder | open as reported; a disc image given as a single `.iso` file is recognised but cannot be read: setup needs an extracted disc folder |
| Lighting on some floors washed out; a white bar under the left portrait | earlier v18 build | none | not compared on v19; the bar is not explained |
| A loading page; a black window of about two minutes at the first start; log files stay at 0 bytes during play and fill at exit | v19 reports | wait for the window | not re-checked after the boot fix |
| "Fight again" from the result menu left a flat brown screen | v18 | none | not re-checked on v19 |
| Not tried yet: key rebinds and controller remaps by hand, playing to a K.O., characters other than the route's five, Windows 10 with the menu fix, long runs, the lost-device case | v19 | none | not tested |
| Needs a CPU with AVX2 | default build | a hand build with `-DJSP_AVX2=OFF` | by design |
| Online and Cross-Save upload need PSN and do nothing; Cross-Save is now "Exit Game". Online Battle over a re-psn server is experimental and off unless turned on | default build | see [docs/ONLINE.md](docs/ONLINE.md) | by design |
| A hand build from this repository (pinned ps3recomp v0.15.0 submodule plus `jstars/toolkit/ps3recomp-local.patch`, see [docs/BUILDING.md](docs/BUILDING.md)) has not been tried on a clean machine | hand builds | use the installer, which applies the same patch | not tested |
| The scripts change your power plan and mount the disc image | `.bat`/`.ps1` scripts | read [docs/SCRIPTS.md](docs/SCRIPTS.md) first | by design |
