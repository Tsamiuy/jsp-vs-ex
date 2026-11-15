# What the scripts do (read before running)

- `jsp-launcher.exe` (the installer's shortcuts start it) reads `paths.txt` and `play_settings.txt`, checks that the disc folder exists,
  turns each `KEY=VALUE` line of the settings file into an environment variable for the game and starts it. It leaves your Windows power
  plan alone unless `CPU_BOOST=1` is set; then it sets the maximum processor state to 100 % while the game runs and puts the old values back
  afterwards. It downloads and installs nothing.
- `jsp-settings.exe` is the settings window. It writes `play_settings.txt` and starts the game through the launcher.
- `run.cmd` (portable zip), `build-again.cmd` and `support-bundle.cmd` run the bundled Python on the scripts in the install folder
  (`firstrun.py`, `support_bundle.py`). The support bundle sends nothing anywhere; see [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
- `tools/boost/` changes **Maximum processor state** of your *current* Windows power plan (100 % = boost, 99 % = no boost) for both plugged-in and battery.
  It does not restore your earlier value; set it back in Power Options if you want something else. No admin rights needed. Read `tools/boost/README.txt` first.
  The installed launcher does not use it.
- Test scripts under `docs/archive/jstars-notes/tools/` (`*.sh`) belong to the v0.15 work; they close the game normally and force-kill it only if it does not exit.
