# Testing Online Battle with rollback

A short guide for friends who test online play. Online play is experimental: things will break, and your reports are what fixes them.
Online fights are **2 vs 2** for now: each player has one fighter, the CPU fills the other slots.

## Before you start: one copy, same version

- **Keep one copy of the game.** If you had an older version, uninstall it first (Start menu > **Uninstall JSP Fan Revival**), or delete
  the old portable folder. Two copies side by side is the most common reason rollback does not start: the shortcut opens the old one.
- **Check the version.** The main menu shows it in the lower right corner, for example `J-Stars Victory VS-EX 0.7.0`. Both players must
  use the same release and the same enabled cosmetic mods. Native Online Battle refuses gameplay mods.

## Play online in 5 steps

1. **Download** `jsp-setup.exe` (or the portable zip) from the project's Releases page. Both players need the same download.
2. **Run setup.** It asks for your disc folder and your `EBOOT.elf` (see [What setup needs](#what-setup-needs)), then builds the game on
   your PC. This takes a while (minutes, longer on an older PC); let it finish. Open Settings > Online, choose Friendly Match + rollback, enter your approved server and online name, and save. If Windows asks
   to allow the game through the firewall, click **Allow access**.
3. **Start the game** from the Start menu or desktop shortcut. The first start installs the game data from your disc (a few minutes).
4. **Warm up (recommended):** play one offline Free Battle (Battle Modes > Free Battle > 1 Player, **2 vs 2**) with the characters you
   plan to use online. The game builds their graphics once, so the online fights stutter less. Then go straight to Online Battle;
   no need to restart the game.
5. Both players: **Main Menu > Battle Modes > Online Battle > Friendly Match**. One player picks **Create a Room** (the host), the
   other **Search for a Room** and joins it. When both are in the room, the host waits about 10 seconds, then starts the battle.
   Only the host picks characters; the other player is on Random Select.

After a match you both return to the room and the host can start the next fight. To leave, back out of the room with Circle.
**Esc** quits the game.

## How do I know rollback is on?

Look for these, in this order:

1. As the fight starts: `Rollback on: direct connection` (or `Rollback on: connected through the server`) at the bottom left, for a
   few seconds.
2. **During the whole fight: a ping counter in ms under the timer** (bottom right), for example `62 ms`. Green is below 100 ms,
   yellow below 180 ms, red above. It appears a second or two after the fight starts.

The ping counter can also be hidden with `JSP_ROLLBACK_PING=0`. Check the rollback start notice and match report.

## If something goes wrong

| What you see | What to do |
|---|---|
| **No ms counter during the fight.** | Check whether the counter was hidden, whether both players saw the rollback start notice and whether the match report records a connection. |
| **The other player is on a different version.** | Both players should compare their release and enabled mods. The incompatible rollback room is skipped. |
| **`Desync detected: match ended`** | The two games stopped agreeing on what happens, so rollback ended. From here on, each screen plays the fight on its own. Finish the fight, go back to the room and start the next one: it tries rollback again. Send both support bundles: a desync is the most useful report. |
| **The picture freezes.** | A lost flip can sometimes recover. If the game stays frozen, close it and keep the logs from both players. |
| **`Reconnecting... N s`** during a fight. | The connection dropped for a moment. Wait a few seconds; after 5 seconds without data the match ends with `Opponent disconnected`. |
| **`Opponent disconnected`.** | The other player left or lost the connection. Make a new room when they are back. |
| **Choppy or crackling sound.** | Save both logs and note when it occurred, your output device, and whether you changed devices during play. |
| **The match ended when you pressed F9** (or a recording or push-to-talk key on F9). | Holding **F9** for 1.5 seconds ends a rollback match on purpose; a tap does nothing. Do not hold F9 during a fight: move push-to-talk (Discord and similar) and any other hold-down key off F9. A quick tap of a recording hotkey such as Alt+F9 is fine. |
| **`Connecting to opponent...` stays on screen.** | Wait: after 45 seconds the match ends with `Opponent did not reach the fight: match ended`. Make a new room. If it happens every time, send both support bundles. |
| **The room is not in the list.** | The host creates the room first; then the other player searches again. Check the version (main menu corner) and that the settings window's **Online** tab shows the same server on both PCs (the **Public server** button sets the default). |
| **The band says `the online name is taken`.** | Every player on the server needs a different online name. Change yours in the settings window's **Online** tab (3 to 16 letters, digits, `-` or `_`, starting with a letter). |
| **EU disc:** `Rollback is off: rollback needs the US disc (BLUS31519).` | Rollback needs the US disc. Online Battle still works, with the game's own netcode. |
| **Windows asks "Allow access?"** the first time you open Online Battle (the box can hide behind the game: press Alt+Tab if the game seems stuck). | Click **Allow access**. If you clicked Cancel, online still works, but more often through the server, which adds delay. |
| **"Windows protected your PC"** when you start `jsp-setup.exe`. | Click **More info**, then **Run anyway**. The files are not code-signed. To check the download, compare its hash with the `SHA256SUMS` file on the release page (PowerShell: `Get-FileHash .\jsp-setup.exe -A SHA256`). |
| **Your antivirus blocks a file.** | Verify the release source and published checksums before investigating the detection. |

## Support bundle (send this)

1. Close the game.
2. Start menu > **Make a support bundle (JSP Fan Revival)** (portable copy: double-click `support-bundle.cmd` in its folder).
3. It writes `jsp-support-<time>.zip` on your Desktop. It sends nothing by itself. Server addresses, online names and IP addresses are
   replaced by placeholders; open it and look before you send it.
4. Send the zip from **both** players, with a few lines:
   - who hosted and who joined, roughly where you are (same city, other country), wired or Wi-Fi;
   - what happened and when: did the ms counter show, did it feel like offline, any freeze, stutter, choppy sound, desync or crash;
   - the exact text of any message the game showed.

Make the bundle soon after the problem: it holds the logs of the last two game starts only. A match that ends on a desync or a
failure also writes a small report in `<install folder>\game\run\reports\`; the bundle includes the newest ten.

## What setup needs

- Your disc folder (the one with `PS3_GAME`), US or EU. Rollback needs the US disc.
- `EBOOT.elf`: the game's program file in decrypted form, made from your own copy of the game. The `EBOOT.BIN` on the disc is the
  encrypted form, and setup refuses it. This project does not explain how to make `EBOOT.elf`: see [RPCS3](https://rpcs3.net) and its
  documentation. If you already play the game in RPCS3, setup's "Use your RPCS3 install?" page can fill in both.
When rollback is off or could not start, the main menu shows a dark band at the top for 15 seconds. It says what went wrong and what
to do. Read it before you make a room.

## Settings, if you want them

First run preserves saved settings. A fresh installation with no online configuration starts offline. To play online, open **Settings > Online**, choose **re-psn**, enter the server address approved for your session and your online name, then save. Select rollback separately for a supported disc. The launcher displays shared preferences. Per-game settings, environment variables and launch arguments can affect the session.

If you already had online settings (your own server, a LAN server, or Off), they are kept as they are.

The launcher and **Options > Port Settings > Online** offer Off, standard re-psn, custom rollback, Community and Friendly Match rollback. Choose **Friendly Match + rollback** for the game's Online Battle rooms. Custom rollback supports automatic pairing and optional match codes. Save changes and restart.

With the two-page menu turned off (`MENU_PAGES=0`), Online Battle is on the Main Menu itself, the fifth row. Hold **F9** for 1.5 seconds to cancel rollback.

## What to report

After a session, tell us:

- who hosted and who joined, and roughly where you are (same city, other country), wired or Wi-Fi;
- what happened: did the room work, did the fight start, did it feel like offline, any freeze, stutter, desync (the two screens
  showing different things) or a crash, and at what point;
- the text of any band the game showed at the top of the main menu;
- the support bundle from **both** players (next section).

## Where the reports and logs are

- **Support bundle (send this):** Start menu > **Make a support bundle (JSP Fan Revival)** (portable: double-click
  `support-bundle.cmd`). It makes `jsp-support-<time>.zip` on your Desktop and sends nothing by itself. It holds the logs below, your
  settings and a `facts.txt` that names the netcode in use and how the last two starts went. Server addresses, online names and IP
  addresses are replaced by placeholders. Open it and look before you send it.
- **Match reports:** a match that ends on a desync or a failure writes one small text file,
  `<install folder>\game\run\reports\jsp-match-report-<time>.txt`. The support bundle includes the newest ten.
- **Logs:** `<install folder>\game\run\play_stderr.txt` (the `[net]` and `[online]` lines; `play_stderr.prev.txt` is the start
  before), `play_stdout.txt` and `launcher_log.txt`.

## Known limits

First run preserves saved settings. A fresh installation with no online configuration starts offline. To play online, open **Settings > Online**, choose **re-psn**, enter the server address approved for your session and your online name, then save. Select rollback separately for a supported disc. The launcher displays shared preferences. Per-game settings, environment variables and launch arguments can affect the session.
