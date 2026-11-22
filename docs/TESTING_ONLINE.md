# Testing Online Battle with rollback

A short guide for friends who test online play. Online play is experimental: things will break, and your reports are what fixes them.
Online fights are **2 vs 2** for now: each player has one fighter, the CPU fills the other slots.

## Before you start: one copy, same version

- **Keep one copy of the game.** If you had an older version, uninstall it first (Start menu > **Uninstall JSP Fan Revival**), or delete
  the old portable folder. Two copies side by side is the most common reason rollback does not start: the shortcut opens the old one.
- **Check the version.** The main menu shows it in the lower right corner, for example `J-Stars Victory VS-EX 0.7.0`. Both players must
  see exactly the same text there, and use the same gameplay mods (or none).

## Play online in 5 steps

1. **Download** `jsp-setup.exe` (or the portable zip) from the project's Releases page. Both players need the same download.
2. **Run setup.** It asks for your disc folder and your `EBOOT.elf` (see [What setup needs](#what-setup-needs)), then builds the game on
   your PC. This takes a while (minutes, longer on an older PC); let it finish. Online is set up for you at the end. If Windows asks
   to allow the game through the firewall, click **Allow access**.
3. **Start the game** from the Start menu or desktop shortcut. The first start installs the game data from your disc (a few minutes).
4. **Warm up (optional):** play one offline Free Battle (Battle Modes > Free Battle > 1 Player, **2 vs 2**) with the characters you
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

**No ms counter means no rollback**: that fight runs on the game's own netcode. See the first row of the table below.

## If something goes wrong

| What you see | What to do |
|---|---|
| **No ms counter during the fight** (no `Rollback on` line either). | 1. Check the main menu corner on both PCs: exactly the same version? If one PC still has an old copy, uninstall it and start the new one. 2. Go back to the Main Menu and read the dark band at the top (it stays 15 seconds): `Rollback is off: ...` names a setting to fix, `Rollback could not start: ...` says why. 3. Both leave the room and make a new one; the host waits about 10 seconds before starting. |
| **`The other player is on <version>; you're on <version>.`** | The two games are different versions, so rollback is off in this room. The player with the older version installs the same download as the other, then both make a new room. |
| **`Desync detected: match ended`** | The two games stopped agreeing on what happens, so rollback ended. From here on, each screen plays the fight on its own. Finish the fight, go back to the room and start the next one: it tries rollback again. Send both support bundles: a desync is the most useful report. |
| **The picture freezes.** | Wait up to a minute: the game recovers most freezes by itself within a few seconds, and otherwise ends the match. If it stays frozen, close the game (Alt+F4, or Task Manager), then **make the support bundle before you start the game again** (it keeps the logs of the last two starts only). Say who froze, the host or the other player. |
| **`Reconnecting... N s`** during a fight. | The connection dropped for a moment. Wait a few seconds; after 5 seconds without data the match ends with `Opponent disconnected`. |
| **`Opponent disconnected`.** | The other player left or lost the connection. Make a new room when they are back. |
| **Choppy or crackling sound** in an online fight. | Fight sound runs a little behind (about 1/20 of a second) to stay smooth; short gaps fade instead of cutting. It is worst when the game itself stalls, mostly in the first fights of a new install: do the warm-up fight (step 4). If it stays choppy, say when (first fight only, or all) and send the support bundle. |
| **The match ended when you pressed F9** (or a recording or push-to-talk key on F9). | Holding **F9** for 1.5 seconds ends a rollback match on purpose; a tap does nothing. Do not hold F9 during a fight: move push-to-talk (Discord and similar) and any other hold-down key off F9. A quick tap of a recording hotkey such as Alt+F9 is fine. |
| **`Connecting to opponent...` stays on screen.** | Wait: after 45 seconds the match ends with `Opponent did not reach the fight: match ended`. Make a new room. If it happens every time, send both support bundles. |
| **The room is not in the list.** | The host creates the room first; then the other player searches again. Check the version (main menu corner) and that the settings window's **Online** tab shows the same server on both PCs (the **Public server** button sets the default). |
| **The band says `the online name is taken`.** | Every player on the server needs a different online name. Change yours in the settings window's **Online** tab (3 to 16 letters, digits, `-` or `_`, starting with a letter). |
| **EU disc:** `Rollback is off: rollback needs the US disc (BLUS31519).` | Rollback needs the US disc. Online Battle still works, with the game's own netcode. |
| **Windows asks "Allow access?"** the first time you open Online Battle (the box can hide behind the game: press Alt+Tab if the game seems stuck). | Click **Allow access**. If you clicked Cancel, online still works, but more often through the server, which adds delay. |
| **"Windows protected your PC"** when you start `jsp-setup.exe`. | Click **More info**, then **Run anyway**. The files are not code-signed. To check the download, compare its hash with the `SHA256SUMS` file on the release page (PowerShell: `Get-FileHash .\jsp-setup.exe -A SHA256`). |
| **Your antivirus blocks or removes files.** | Restore the file from the quarantine, add the install folder as an exclusion (Windows Security > **Virus & threat protection** > **Manage settings** > **Add or remove exclusions**), then run **Build again** from the Start menu. |

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

## Settings, if you want them

Setup turns online on: a public re-psn test server (`107.175.17.162:36100`, not run by this project; it passes the match through itself
when the two PCs cannot connect directly), a random online name such as `Player1234`, and rollback for Online Battle (US disc only).
Online settings you already had are kept.

The settings window (Start menu or desktop shortcut), **Online** tab: the line at the top says what the next start does; it should start
with "Online Battle + rollback (2 vs 2)". You can change your online name there. Changes apply on the next start.
`JSP_ROLLBACK_PING=0` in `play_settings.txt` hides the ms counter.

## Known limits

- Experimental: expect bugs.
- Two players per room, one per PC, 2 vs 2. Friendly Match only; Rank Match is not tested. 3 vs 3 is offline only.
- Only the host picks characters.
- A sudden gap in the connection itself (a tenth of a second or more with no data) can still cause a short hitch.
- Gameplay mods turn the game's own online off for the session.
- The default server is a public test server that this project does not run; it may be down or restarted, and your online name is
  not reserved. Each game also opens a hidden connection named `<your online name>-rb`, which may show up in the server's logs.
- Matches through the server (`connected through the server`) have more delay than direct ones.
