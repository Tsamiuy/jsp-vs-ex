# Testing Online Battle with rollback

A one-page guide for friends who test online play. Online play is experimental: things will break, and your reports are what fixes them.
Online fights are **2 vs 2** for now.

## Play online in 5 steps

1. **Download** `jsp-setup.exe` (or the portable zip) from the project's Releases page (or build it yourself). Both players need the
   same build.
2. **Run setup.** It asks for your disc folder and your `EBOOT.elf` (see [What setup needs](#what-setup-needs)), then builds the game on
   your PC. This can take a long time, for example 10-30 minutes; let it finish. Online is set up for you at the end.
3. **Start the game** from the Start menu or desktop shortcut (Start Game). The first start installs the game data from your disc,
   which takes a few minutes.
4. Both players: **Main Menu > Battle Modes > Online Battle > Friendly Match**.
5. One player picks **Create a Room**. The other picks **Search for a Room** and joins it. Start the battle as usual.

When the fight runs on rollback, the game shows `Connecting to opponent...` as the fight starts, then `Rollback on: direct connection` (or
`Rollback on: connected through the server`) for a few seconds. For the whole fight, the ping in ms under the timer (bottom right, for
example `62 ms`) also shows that rollback is on: it is the round trip to the other player (through the server when the match uses the
relay), green below 100 ms, yellow below 180 ms, red above. It appears shortly after the fight starts. If you see none of these, the fight
runs on the game's own netcode (see below). `JSP_ROLLBACK_PING=0` in `play_settings.txt` hides the ping.

No IP addresses and no port forwarding. After a match you can play again in the same room.

**Same build:** the main menu shows the build in its lower right corner, for example `J-Stars Victory VS-EX 0.6.1`. Both
players must see the same text there and use the same gameplay mods (or none).

**Leave and quit:** after a match, back out of the room with the game's own menus (Circle) to reach the Main Menu. **Esc** quits the
game. During a rollback fight, holding **F9** for 1.5 seconds ends the match.

## What setup needs

- Your disc folder (the one with `PS3_GAME`), US or EU.
- `EBOOT.elf`: the game's program file in decrypted form, made from your own copy of the game. The `EBOOT.BIN` on the disc is the
  encrypted form, and setup refuses it. This project does not explain how to make `EBOOT.elf`: see [RPCS3](https://rpcs3.net), its
  Quickstart guide and its documentation. If you already play the game in RPCS3, setup's "Use your RPCS3 install?" page can fill
  in both.

## If something goes wrong

When rollback is off or could not start, the main menu shows a dark band at the top for 15 seconds. It says what went wrong and what
to do. Read it before you make a room.

| What you see | What to do |
|---|---|
| **The room is not in the list.** | The host creates the room first; then the other player searches again. Check that both show the same build in the main menu corner, and that the settings window's **Online** tab names the same server on both PCs (the **Public server** button sets it). |
| **The fight starts without `Connecting to opponent...`** (it runs on the game's own netcode). | Go back to the Main Menu and read the band at the top. `Rollback is off: ...` names a setting to fix. `Rollback could not start: ...` names the reason, for example another build. The line at the top of the settings window's **Online** tab must start with "Online Battle + rollback (2 vs 2)". Then both leave the room and make a new one (after three failed tries the game stops trying in that room). |
| **`Connecting to opponent...` stays on screen.** | Wait: after 45 seconds the match ends with `Opponent did not reach the fight: match ended`. Make a new room. If it happens every time, send both support bundles. |
| **`Reconnecting... N s (F9 held: end match)`** during a fight. | The connection dropped for a moment. Wait a few seconds, or hold F9 to end the match. |
| **`Opponent disconnected`.** | The other player left or lost the connection. Make a new room when they are back. |
| **Stutter in the first fights.** | Normal on a new install: the game builds each new effect's graphics once, and while one PC builds, the other PC's screen waits too. Before your first online match, **both** players play one offline Free Battle (Battle Modes > Free Battle > 1 Player, **2 vs 2**), with the characters you plan to use online. It builds most of them ahead of time. |
| **Windows asks "Allow access?" for the game** (the first time you open Online Battle; the box can hide behind the full-screen game, so press Alt+Tab if the game seems stuck). | Click **Allow access**. If you clicked Cancel, online still works, but more often through the server (`Rollback on: connected through the server`), which adds delay. Setup's **Allow online play through Windows Firewall** box sets this up so the question never comes. |
| **The band says `the online name is taken`.** | Every player on the server needs a different online name. Change yours in the settings window's **Online** tab (3 to 16 letters, digits, `-` or `_`, starting with a letter). Do not end it in `-rb`: the game uses `<your name>-rb` for its hidden rollback connection. |
| **EU disc:** the **Online** tab says "Online Battle with the game's own netcode (rollback off).", or the band says `Rollback is off: rollback needs the US disc (BLUS31519).` | Rollback needs the US disc. Online Battle still works, with the game's own netcode. |
| **"Windows protected your PC"** (SmartScreen) when you start `jsp-setup.exe`. | Click **More info**, then **Run anyway**. The files are not code-signed. To check the download first, compare its hash with the `SHA256SUMS` file on the release page (PowerShell: `Get-FileHash .\jsp-setup.exe -A SHA256`). |
| **Your antivirus blocks or removes files** during the build or at the first start. | Restore the file from the antivirus quarantine. Add the install folder as an exclusion (Windows Security: **Virus & threat protection** > **Manage settings** > **Add or remove exclusions** > **Add an exclusion** > **Folder**). Then run **Build again** from the Start menu. |

## Settings, if you want them

At the end of the build, setup turns online on:

- a public test server (re-psn, `107.175.17.162:36100`); when the two PCs cannot connect directly, it passes the match through
  itself (the relay);
- a random online name such as `Player1234`;
- rollback netcode for Online Battle fights (US disc only).

If you already had online settings (your own server, a LAN server, or Off), they are kept as they are.

The settings window (the Start-menu or desktop shortcut), **Online** tab: the line at the top says what the next start does, and you can
change your online name there. Untick **Rollback netcode for Online Battle fights** to play Online Battle with the game's own netcode,
or choose **Off** to stop online play. The in-game **Options > Port Settings > Online** page has the same choices (Netcode: Off,
Delay, Rollback, Online Battle + rollback). Changes apply on the next start.

With the two-page menu turned off (`MENU_PAGES=0`), Online Battle is on the Main Menu itself, the fifth row. **F9** is the rollback
cancel key: do not bind it to a game button.

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

- Experimental. Tried with two copies on one Windows 11 PC; not tried between two PCs. Expect bugs.
- Two different builds cannot pair for rollback; the log says so.
- Rollback pairs a room with exactly two players, one per PC, 2 vs 2 (each with a CPU partner). It is meant for Friendly Match; Rank
  Match is not tested. 3 vs 3 is offline only: a rollback match never runs the 3v3 code.
- Gameplay mods turn the game's own online off for the session.
- Each game opens a second, hidden connection to the server for rollback, named `<your online name>-rb`. It may show up in the
  server's logs.
- The default server is a public re-psn test server; this project does not run it. It may be down or restarted; your online name is
  not reserved. Any other re-psn server can be typed in the **Online** tab.
- The relay adds some delay.
