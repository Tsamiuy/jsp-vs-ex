# Troubleshooting

Find what you see, then follow the fix. Setup refusals with a code (`D1` to `S1`) are in [INSTALL.md](INSTALL.md#what-setup-refuses).

Contents: [Setup stops](#setup-stops) · [Black screen](#black-screen) · [Controller does not work](#controller-does-not-work) · [Picture is stretched](#picture-is-stretched) · [Windows or antivirus warns](#windows-or-antivirus-warns) · [Where the logs are](#where-the-logs-are) · [Support bundle](#support-bundle) · [Report a bug](#report-a-bug)

## Setup stops

- **You see:** setup or the first build stops with an error.
- **Cause:** a refused input, too little disk space or memory, or a failed build step.
- **Fix:** read the line with the code and see [INSTALL.md](INSTALL.md#what-setup-refuses). If there is no code, click **Build again** in the Start menu to resume.
- **Log:** `logs\firstrun-<time>.log` in the install folder (the newest five are kept).

## Black screen

- **You see:** the game starts and the window stays black.
- **Cause:** not known in general; the first run may have failed, or the graphics driver lacks Direct3D 12.
- **Fix:** click **Build again** in the Start menu. If that does not help, make a support bundle.
- **Log:** `game\run\launcher_log.txt`, `play_stdout.txt` and `play_stderr.txt` in the install folder.

## Controller does not work

- **You see:** the pad does nothing in the game.
- **Cause:** a pad that only works through DirectInput is not handled by the code.
- **Fix:** use the keyboard, or a pad that Windows reads through XInput (Xbox-style pads). Buttons can be remapped in the settings window.

## Picture is stretched

- **You see:** the picture fills a wide monitor.
- **Cause:** the picture is 16:9. Fullscreen on a monitor of another shape (16:10, ultrawide) stretches it. There is no letterboxing in the code, and this was not tried by hand on such monitors.
- **Fix:** use the window, or a resolution scale, in the settings window. Wide screens without stretching are planned ([Features: Wide screens](FEATURES.md#wide-screens)).

## Windows or antivirus warns

The setup program and the game are not code-signed. See [Check your download](INSTALL.md#check-your-download).

## Where the logs are

| Log | Place |
|---|---|
| Setup and build steps | `logs\firstrun-<time>.log` and the `build` step logs |
| Launcher and game | `game\run\launcher_log.txt`, `play_stdout.txt`, `play_stderr.txt` |
| Settings | `play_settings.txt` (install folder, or `%APPDATA%\JSP Fan Revival\`) |

## Support bundle

Click **Make a support bundle (JSP Fan Revival)** in the Start menu. For the portable zip, double-click `support-bundle.cmd`.

It writes `jsp-support-<time>.zip` on the Desktop, with the logs and settings as text, with your user profile folder replaced. It never holds the EBOOT, disc files or saves. It sends nothing anywhere. Open the zip and look before you attach it.

Details: [installer/README.md, "Support bundle"](../installer/README.md#support-bundle).

## Report a bug

1. Make a support bundle.
2. Open a [new issue](https://github.com/Tsamiuy/jsp-vs-ex/issues/new/choose) and pick a form: bug report, performance report, play-test report, feature request, or feedback or question.
3. Attach the zip, and say which graphics card and CPU you have.

Never attach game files.
