# Testing Online Battle with rollback

A one-page guide for friends who test online play. Online play is experimental: things will break, and your reports are what fixes them.

## 1. Install

Test build: download `jsp-setup.exe` (or the portable zip) from the `0.7.0-test1` pre-release on the Releases page of the fork the
person who invited you linked, not from the main project's releases. Everyone in the group must use the same pre-release; the
`SHA256SUMS` file next to the downloads lists each file's hash (PowerShell: `Get-FileHash <file> -A SHA256`). Online rollback
needs the US disc; with an EU disc, untick rollback (section 5). After install, the main menu shows the build name `0.7.0-test1`.
Your antivirus or SmartScreen may flag the unsigned files and the on-PC build; allow the folder.

1. Run `jsp-setup.exe` (or unpack the portable zip outside Program Files and run `run.cmd`). It asks for your own disc folder and your own
   decrypted `EBOOT.elf`, then builds the game on your PC. That takes a while; let it finish.
2. Nothing else to set up. At the end of the build, the first run turns online on:
   - the project's public re-psn server (`107.175.17.162:36100`, with its relay on);
   - a random online name such as `Player1234`;
   - rollback netcode for Online Battle fights.

   If you already had online settings (your own server, a LAN server, or Off), they are kept as they are.
3. Optional: open the settings window (the Start-menu or desktop shortcut), **Online** tab. The line at the top says what the next start
   does; it should read "Online Battle + rollback". You can change your online name there (3 to 16 letters, digits, `-` or `_`,
   starting with a letter). Every player on the server needs a different name.

## 2. Host or join

Both players: start the game, then Main Menu > **Online Battle** > **Friendly Match**.

- **Host:** pick **Create a Room**.
- **Join:** pick **Search for a Room** and join the host's room.

When you are both in the room, start the battle as usual. As soon as the room has exactly two players, the two games connect to each
other for rollback (this takes a few seconds, while you pick characters). The fight then runs on rollback netcode. After the match you
can play again in the same room.

You only need the menus. No IP addresses, no port forwarding: if your routers block a direct link, the server's relay carries the match.

Good to know:

- Each game also opens a second, hidden connection to the server for rollback, named `<your online name>-rb`. You will not see it in
  the game; it may show up in the server's logs.
- **F9** is kept for the rollback cancel key. Do not bind it to a game button.
- The first fights on a new install can hitch once per new effect while the game builds its graphics programs; that goes away.
  **Tip:** before your first online match, play one offline Free Battle (any characters). It builds most of the fight's graphics
  ahead of time, so your online fights are smooth from the first second.

## 3. What to report

After a session, tell us:

- who hosted and who joined, and roughly where you are (same city, other country), wired or Wi-Fi;
- what happened: did the room work, did the fight start, did it feel like offline, any freeze, stutter, desync (the two screens
  showing different things) or a crash, and at what point;
- the text of any notice the game showed on the main menu (it names what went wrong and the fix);
- the support bundle from **both** players (next section).

## 4. Where the reports and logs are

- **Support bundle (send this):** Start menu > **Make a support bundle (JSP Fan Revival)** (portable: double-click
  `support-bundle.cmd`). It makes `jsp-support-<time>.zip` on your Desktop and sends nothing by itself. It holds the logs below, your
  settings and a `facts.txt` that names the netcode in use and how the last two starts went. Server addresses, online names and IP
  addresses are replaced by placeholders. Open it and look before you send it.
- **Match reports:** a match that ends on a desync or a failure writes one small text file,
  `<install folder>\game\run\reports\jsp-match-report-<time>.txt`. The support bundle includes the newest ten.
- **Logs:** `<install folder>\game\run\play_stderr.txt` (the `[net]` and `[online]` lines; `play_stderr.prev.txt` is the start
  before), `play_stdout.txt` and `launcher_log.txt`.

## 5. Turning it off

Settings window, **Online** tab:

- untick **Rollback netcode for Online Battle fights** to play Online Battle with the game's own netcode;
- or choose **Off** to stop online play.

The in-game **Port Settings > Online** page has the same choices (Netcode: Off, Delay, Rollback, Online Battle + rollback). Changes
apply on the next start.

## Known limits

- Experimental, and not yet tried between two Windows PCs over the internet. Expect bugs.
- Both players need the same build of the game (the same release and the same gameplay mods, or none). Two different builds cannot
  pair for rollback; the log says so.
- Rollback pairs a room with exactly two players. It is meant for Friendly Match; Rank Match is not tested.
- Gameplay mods turn the game's own online off for the session.
- Rollback needs the US disc (BLUS31519). On an EU disc it falls back automatically: Online Battle uses the game's own netcode.
- If the two games cannot connect for rollback, they try again (three times per room); the reason is in the log and in the menu
  notice. Report it with both support bundles.
- The public server is run by the project for testing. It may be down or restarted; your online name is not reserved.
- The relay passes the match through the server when a direct link fails; that adds some delay.
