# What the scripts do (read before running)

- `Play J-Stars.bat` / `jstars/play.ps1` mount the game disc image read-only, turn CPU boost on, run the game, then turn boost off and unmount. The image path defaults to `RPCS3\games\J-Stars Victory Vs+.iso` next to the project; set the environment variable `JSTARS_ISO` to use another path.
- `tools/boost/` changes **Maximum processor state** of your *current* Windows power plan (100 % = boost, 99 % = no boost) for both plugged-in and battery. It does not restore your earlier value; set it back in Power Options if you want something else. No admin rights needed. Read `tools/boost/README.txt` first.
- The `.bat`/`.ps1` launchers start PowerShell with `-ExecutionPolicy Bypass` so that these unsigned local scripts can run. Read them first; they download and install nothing.
- Test scripts (`jstars/run_test.ps1`, `jstars-notes/tools/*.sh`) close the game normally and force-kill it only if it does not exit.
